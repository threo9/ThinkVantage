#include "backend.h"
#include "protocol.h"
#include "vendor.h"
#include "input.h"
#include <windows.h>
#include <powrprof.h>
#include <wbemidl.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <QCoreApplication>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QThread>
#include <QDateTime>
#include <QUuid>
#include <QVariant>
#include <functional>
#include <stdexcept>
#include <set>
#include <array>
#include <QMap>
using Microsoft::WRL::ComPtr;
namespace {
struct Failure { QString message; };
QString systemError(DWORD code) {
    wchar_t *p=nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
                  nullptr,code,0,reinterpret_cast<wchar_t*>(&p),0,nullptr);
    QString s=p?QString::fromWCharArray(p).trimmed():QString("错误 %1").arg(code);
    LocalFree(p);
    return QString("%1（%2）").arg(s).arg(code);
}
void check(HRESULT hr) { if(FAILED(hr)) throw Failure{systemError(DWORD(hr))}; }
struct Handle {
    HANDLE h=INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE value):h(value){}
    ~Handle(){ if(h && h!=INVALID_HANDLE_VALUE) CloseHandle(h); }
    Handle(const Handle&)=delete;
};
struct ServiceHandle {
    SC_HANDLE h;
    explicit ServiceHandle(SC_HANDLE value):h(value) { if(!h) throw Failure{systemError(GetLastError())}; }
    ~ServiceHandle(){CloseServiceHandle(h);}
};
struct ComApartment {
    HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    ComApartment(){check(hr); HRESULT s=CoInitializeSecurity(nullptr,-1,nullptr,nullptr,RPC_C_AUTHN_LEVEL_DEFAULT,RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE,nullptr); if(s!=RPC_E_TOO_LATE) check(s);}
    ~ComApartment(){if(SUCCEEDED(hr))CoUninitialize();}
};
struct Bstr {
    BSTR p;
    explicit Bstr(const QString &s):p(SysAllocString(reinterpret_cast<const wchar_t*>(s.utf16()))){}
    ~Bstr(){SysFreeString(p);}
    operator BSTR() const{return p;}
};
QVariant reg(HKEY hive,const wchar_t *path,const wchar_t *name) {
    DWORD type=0,size=0;
    if(RegGetValueW(hive,path,name,RRF_RT_REG_DWORD|RRF_RT_REG_SZ,nullptr,nullptr,&size)!=ERROR_SUCCESS) return {};
    QByteArray buf(int(size+2),0);
    if(RegGetValueW(hive,path,name,RRF_RT_REG_DWORD|RRF_RT_REG_SZ,&type,buf.data(),&size)!=ERROR_SUCCESS)return {};
    if(type==REG_DWORD) return uint(*reinterpret_cast<const DWORD*>(buf.constData()));
    return QString::fromWCharArray(reinterpret_cast<const wchar_t*>(buf.constData()));
}
constexpr auto itsPath=L"SYSTEM\\CurrentControlSet\\Services\\LenovoProcessManagement\\Performance\\PowerSlider";
bool lenovo(){return reg(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\BIOS",L"SystemManufacturer").toString().contains("LENOVO",Qt::CaseInsensitive);}
class Energy {
    Handle file{CreateFileW(L"\\\\.\\EnergyDrv",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
    DWORD openError=GetLastError();
public:
    uint32_t query(uint32_t code,uint32_t token,int width=4,bool requireOutput=true) {
        if(!lenovo()) throw Failure{"当前设备未识别为 Lenovo"};
        if(file.h==INVALID_HANDLE_VALUE)throw Failure{QString("无法访问 Lenovo 设备接口：%1").arg(systemError(openError))};
        DWORD out=0,bytes=0;
        if(!DeviceIoControl(file.h,code,&token,DWORD(width),&out,sizeof(out),&bytes,nullptr))throw Failure{systemError(GetLastError())};
        if(requireOutput && bytes!=4)throw Failure{"设备返回的数据长度异常"};
        if(requireOutput && out==UINT32_MAX)throw Failure{"固件不支持此查询"};
        return out;
    }
    void send(const Protocol::Command &c){query(c.ioctl,c.token,c.bytes,false);}
};
QVariant prop(IWbemClassObject *o,const wchar_t *name){
    if(!o)return {};
    VARIANT v;VariantInit(&v); HRESULT hr=o->Get(name,0,&v,nullptr,nullptr); QVariant r;
    if(SUCCEEDED(hr)) {
        switch(v.vt){
        case VT_BSTR:r=QString::fromWCharArray(v.bstrVal);break;
        case VT_UI1:r=v.bVal;break;
        case VT_UI2:r=v.uiVal;break;
        case VT_I2:r=v.iVal;break;
        case VT_UI4:r=uint(v.ulVal);break;
        case VT_I4:r=int(v.lVal);break;
        case VT_BOOL:r=(v.boolVal!=VARIANT_FALSE);break;
        default:break;
        }
    }
    VariantClear(&v);return r;
}
class Wmi {
    ComPtr<IWbemServices> svc;
public:
    explicit Wmi(const QString &ns="ROOT\\WMI") {
        ComPtr<IWbemLocator> loc;check(CoCreateInstance(CLSID_WbemLocator,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&loc)));
        check(loc->ConnectServer(Bstr(ns),nullptr,nullptr,nullptr,0,nullptr,nullptr,&svc));
        check(CoSetProxyBlanket(svc.Get(),RPC_C_AUTHN_WINNT,RPC_C_AUTHZ_NONE,nullptr,RPC_C_AUTHN_LEVEL_CALL,RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE));
    }
    ComPtr<IWbemClassObject> first(const QString &sql){
        ComPtr<IEnumWbemClassObject> e;
        check(svc->ExecQuery(Bstr("WQL"),Bstr(sql),WBEM_FLAG_FORWARD_ONLY|WBEM_FLAG_RETURN_IMMEDIATELY,nullptr,&e));
        ComPtr<IWbemClassObject> obj;ULONG count=0;HRESULT hr=e->Next(2500,1,&obj,&count);
        check(hr);if(count!=1)throw Failure{"设备未提供此接口或查询超时"};return obj;
    }
    uint32_t invoke(const QString &cls,const QString &method,const QMap<QString,uint32_t> &parameters={},const wchar_t *field=L"Data"){
        auto instance=first("SELECT * FROM "+cls);auto path=prop(instance.Get(),L"__PATH").toString();
        ComPtr<IWbemClassObject> definition,inDefinition,input,output;
        check(svc->GetObject(Bstr(cls),0,nullptr,&definition,nullptr));
        if(!definition)throw Failure{"WMI 未提供类定义"};
        check(definition->GetMethod(Bstr(method),0,&inDefinition,nullptr));
        if(!parameters.isEmpty()){
            if(!inDefinition)throw Failure{"WMI 未提供参数定义"};check(inDefinition->SpawnInstance(0,&input));
            for(auto i=parameters.begin();i!=parameters.end();++i){VARIANT v{};v.vt=VT_I4;v.lVal=LONG(i.value());check(input->Put(Bstr(i.key()),0,&v,0));}
        }
        check(svc->ExecMethod(Bstr(path),Bstr(method),0,nullptr,input.Get(),&output,nullptr));
        auto value=prop(output.Get(),field);if(!value.isValid())throw Failure{"固件方法未返回有效数据"};return value.toUInt();
    }
    void brightness(int value){
        auto instance=first("SELECT * FROM WmiMonitorBrightnessMethods WHERE Active=TRUE");
        QString path=prop(instance.Get(),L"__PATH").toString();
        ComPtr<IWbemClassObject> cls,def,input,output;
        check(svc->GetObject(Bstr("WmiMonitorBrightnessMethods"),0,nullptr,&cls,nullptr));
        if(!cls)throw Failure{"亮度接口未提供类定义"};
        check(cls->GetMethod(L"WmiSetBrightness",0,&def,nullptr));
        if(!def)throw Failure{"亮度接口未提供参数定义"};
        check(def->SpawnInstance(0,&input));
        if(!input)throw Failure{"无法创建亮度设置参数"};
        VARIANT v;VariantInit(&v);v.vt=VT_UI1;v.bVal=BYTE(value);check(input->Put(L"Brightness",0,&v,0));
        v.vt=VT_I4;v.lVal=0;check(input->Put(L"Timeout",0,&v,0));
        check(svc->ExecMethod(Bstr(path),Bstr("WmiSetBrightness"),0,nullptr,input.Get(),&output,nullptr));
        // Some providers omit the output object. Independent state readback
        // remains mandatory in the caller.
        auto rc=prop(output.Get(),L"ReturnValue");if(rc.isValid()&&rc.toUInt()!=0)throw Failure{"亮度接口返回执行错误"};
    }
};
ComPtr<IAudioEndpointVolume> endpoint(EDataFlow flow){
    ComPtr<IMMDeviceEnumerator> e;check(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&e)));
    ComPtr<IMMDevice> d;check(e->GetDefaultAudioEndpoint(flow,eConsole,&d));
    ComPtr<IAudioEndpointVolume> v;check(d->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_INPROC_SERVER,nullptr,reinterpret_cast<void**>(v.GetAddressOf())));return v;
}
QJsonArray choices(std::initializer_list<std::pair<int,QString>> items){QJsonArray r;for(const auto &i:items)r.append(QJsonObject{{"value",i.first},{"label",i.second}});return r;}
QJsonObject state(int value,const QString &provider,const QJsonArray &options={}){return {{"available",true},{"value",value},{"provider",provider},{"options",options}};}
QJsonObject failure(const QString &why){return {{"available",false},{"value",QJsonValue::Null},{"error",why}};}
struct Display {
    QString name;DEVMODEW mode{};
    Display(){
        for(DWORD i=0;;i++){
            DISPLAY_DEVICEW d{};d.cb=sizeof(d);if(!EnumDisplayDevicesW(nullptr,i,&d,0))break;
            if(d.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE){name=QString::fromWCharArray(d.DeviceName);break;}
        }
        mode.dmSize=sizeof(mode);
        if(name.isEmpty()||!EnumDisplaySettingsW(reinterpret_cast<LPCWSTR>(name.utf16()),ENUM_CURRENT_SETTINGS,&mode))throw Failure{"无法读取主显示器模式"};
    }
    QList<DEVMODEW> modes() const {
        QList<DEVMODEW> r;
        for(DWORD i=0;;i++){
            DEVMODEW d{};d.dmSize=sizeof(d);if(!EnumDisplaySettingsW(reinterpret_cast<LPCWSTR>(name.utf16()),i,&d))break;
            if(d.dmPelsWidth==mode.dmPelsWidth && d.dmPelsHeight==mode.dmPelsHeight && d.dmBitsPerPel==mode.dmBitsPerPel && d.dmDisplayOrientation==mode.dmDisplayOrientation && d.dmDisplayFlags==mode.dmDisplayFlags && d.dmDisplayFrequency>1)r<<d;
        }return r;
    }
    void set(int hz){
        for(auto d:modes())if(d.dmDisplayFrequency==DWORD(hz)){
            d.dmFields=DM_PELSWIDTH|DM_PELSHEIGHT|DM_BITSPERPEL|DM_DISPLAYFREQUENCY|DM_DISPLAYFLAGS|DM_DISPLAYORIENTATION;
            auto namePtr=reinterpret_cast<LPCWSTR>(name.utf16());
            if(ChangeDisplaySettingsExW(namePtr,&d,nullptr,CDS_TEST,nullptr)!=DISP_CHANGE_SUCCESSFUL)throw Failure{"显示驱动拒绝此刷新率"};
            if(ChangeDisplaySettingsExW(namePtr,&d,nullptr,0,nullptr)!=DISP_CHANGE_SUCCESSFUL)throw Failure{"刷新率切换失败"};
            return;
        }throw Failure{"此刷新率不在当前分辨率的可用列表中"};
    }
};
const GUID powerGuids[]={
    {0x961cc777,0x2547,0x4f9d,{0x81,0x74,0x7d,0x86,0x18,0x1b,0x8a,0x7a}},
    {0x00000000,0x0000,0x0000,{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}},
    {0xded574b5,0x45a0,0x4f42,{0x87,0x37,0x46,0x34,0x5c,0x09,0xc2,0x38}}
};
bool acOnline(){SYSTEM_POWER_STATUS s{};return GetSystemPowerStatus(&s)&&s.ACLineStatus==1;}
struct Firmware {
    Handle token{nullptr};TOKEN_PRIVILEGES previous{};DWORD previousSize=sizeof(previous);bool adjusted=false;
    std::array<unsigned char,4> data{};DWORD attributes=0;
    Firmware(){
        if(!lenovo())throw Failure{"当前设备未识别为 Lenovo"};
        HANDLE h=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY,&h))throw Failure{systemError(GetLastError())};token.h=h;
        TOKEN_PRIVILEGES p{};p.PrivilegeCount=1;
        if(!LookupPrivilegeValueW(nullptr,SE_SYSTEM_ENVIRONMENT_NAME,&p.Privileges[0].Luid))throw Failure{systemError(GetLastError())};
        p.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;
        if(!AdjustTokenPrivileges(token.h,FALSE,&p,sizeof(previous),&previous,&previousSize)||GetLastError()==ERROR_NOT_ALL_ASSIGNED)throw Failure{"开盖开机需要以管理员身份打开"};
        adjusted=true;
        DWORD n=GetFirmwareEnvironmentVariableExW(L"FBSWIF",L"{D743491E-F484-4952-A87D-8D5DD189B70C}",data.data(),DWORD(data.size()),&attributes);
        if(n!=4 || data[0]>1){if(adjusted)AdjustTokenPrivileges(token.h,FALSE,&previous,0,nullptr,nullptr);adjusted=false;throw Failure{"固件未返回已知的开盖开机状态："+systemError(GetLastError())};}
    }
    ~Firmware(){if(adjusted)AdjustTokenPrivileges(token.h,FALSE,&previous,0,nullptr,nullptr);}
    void set(int value){
        data[0]=static_cast<unsigned char>(value);
        if(!SetFirmwareEnvironmentVariableExW(L"FBSWIF",L"{D743491E-F484-4952-A87D-8D5DD189B70C}",data.data(),DWORD(data.size()),attributes))throw Failure{systemError(GetLastError())};
    }
};
QJsonObject ocState(){
    Wmi w;if(!lenovo()||w.invoke("LENOVO_GAMEZONE_DATA","IsSupportGpuOC")==0)throw Failure{"固件未声明支持 GPU 超频"};
    QJsonObject r;try{r=Vendor::ocProbe();}catch(const std::exception &e){throw Failure{QString::fromUtf8(e.what())};}
    for(int clock=0;clock<2;clock++){
        auto o=w.first(QString("SELECT * FROM LENOVO_GAMEZONE_GPU_OC_DATA WHERE ClockID=%1").arg(clock));
        auto low=prop(o.Get(),L"OCMinOffset"),high=prop(o.Get(),L"OCMaxOffset"),step=prop(o.Get(),L"Interval");
        if(!low.isValid()||!high.isValid()||!step.isValid()||step.toInt()<1||low.toInt()<0||high.toInt()<low.toInt()||high.toInt()>2000)throw Failure{"GPU 超频范围未知"};
        QString key=clock==0?"core":"memory";
        r[key+"Min"]=low.toInt();r[key+"Max"]=qMin(high.toInt(),r[key+"Max"].toInt());r[key+"Step"]=step.toInt();
        if(r[key+"Max"].toInt()<low.toInt()||r[key].toInt(-1)<low.toInt()||r[key].toInt(-1)>r[key+"Max"].toInt())throw Failure{"GPU 超频当前值不在已确认范围内"};
    }
    return r;
}
QJsonObject feature(const QString &id,Energy &e){
    if(id=="backlightRemember"){
        if(e.query(0x83102154,1)!=3)throw Failure{"固件未声明支持背光记忆"};
        auto raw=e.query(0x83102154,2);if(raw!=1&&raw!=3)throw Failure{"背光记忆状态未知"};
        return state(raw==3?1:0,"Lenovo KBLR",choices({{0,"关闭"},{1,"开启"}}));
    }
    if(id=="fnCtrlSwap"){
        if(!Backend::isAdministrator())throw Failure{"Fn 与 Ctrl 互换需要以管理员身份打开后读取"};
        if(!lenovo())throw Failure{"当前设备未识别为 Lenovo"};Wmi w;
        auto cap=w.invoke("LENOVO_BIOS_ASSISTANT","GetCapabilityValue");
        if(!(cap&8)||!(cap&0x80000000u))throw Failure{"固件未声明支持 Fn 与 Ctrl 互换"};
        auto raw=w.invoke("LENOVO_BIOS_ASSISTANT","GetValue",{{"IndexData",1}});
        if(!(raw&0x80000000u))throw Failure{"固件未确认 Fn 与 Ctrl 当前状态"};
        return state(raw&1,"Lenovo BIOS Assistant",choices({{0,"关闭"},{1,"开启"}}));
    }
    if(Input::owns(id)){try{return Input::feature(id);}catch(const std::exception &err){throw Failure{QString::fromUtf8(err.what())};}}
    if(id=="gpu"){
        if(!lenovo())throw Failure{"当前设备未识别为 Lenovo"};Wmi w;
        auto support=w.invoke("LENOVO_GAMEZONE_DATA","IsSupportIGPUMode");
        if(support==0)throw Failure{"固件未声明支持 GPU 模式切换"};
        int value=int(w.invoke("LENOVO_GAMEZONE_DATA","GetIGPUModeStatus"));
        if(value<0||value>2)throw Failure{"GPU 模式不是已核对的混合/核显/自动档位"};
        auto opts=choices({{0,"混合模式"},{1,"仅核显"}});if(support>=3)opts.append(QJsonObject{{"value",2},{"label","自动模式"}});
        return state(value,"Lenovo GameZone",opts);
    }
    if(id=="flip"){Firmware f;return state(f.data[0],"Lenovo FBSWIF",choices({{0,"关闭"},{1,"开启"}}));}
    if(id=="oc"||id=="ocCore"||id=="ocMemory"){
        auto details=ocState();int raw=details["rawState"].toInt();
        if(raw!=1&&raw!=2)throw Failure{"原厂 GPU 组件当前不允许超频，请检查电源与性能模式"};
        QString key=id=="ocCore"?"core":"memory";
        auto r=state(id=="oc"?(raw==2?1:0):details[key].toInt(),"Lenovo GPU OC",id=="oc"?choices({{0,"关闭"},{1,"开启"}}):QJsonArray{});
        r["details"]=details;
        if(id!="oc"){r["min"]=details[key+"Min"];r["max"]=details[key+"Max"];r["step"]=details[key+"Step"];r["unit"]="MHz";r["enabled"]=raw==2;}
        return r;
    }
    if(id=="its"){
        int version=reg(HKEY_LOCAL_MACHINE,itsPath,L"Version").toInt(),caps=reg(HKEY_LOCAL_MACHINE,itsPath,L"ITS_FN_Capability").toInt();
        if(!lenovo()||version<8192||!(caps&0x10))throw Failure{"未检测到兼容的 Lenovo ITS 服务"};
        auto raw=reg(HKEY_LOCAL_MACHINE,itsPath,L"ITS_CurrentSettingV");
        if(!raw.isValid() || (raw.toInt()!=4&&!Protocol::itsCommand(raw.toInt())))throw Failure{"ITS 当前模式未知"};
        ServiceHandle scm(OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT));
        ServiceHandle service(OpenServiceW(scm.h,L"LenovoProcessManagement",SERVICE_QUERY_STATUS));
        SERVICE_STATUS status{};if(!QueryServiceStatus(service.h,&status))throw Failure{systemError(GetLastError())};
        if(status.dwCurrentState!=SERVICE_RUNNING)throw Failure{"Lenovo ITS 服务未运行"};
        auto options=choices({{0,"自适应（自动）"}});
        if(caps&2)options.append(QJsonObject{{"value",1},{"label","节能"}});
        if(caps&8)options.append(QJsonObject{{"value",3},{"label","性能"}});
        if(caps&16)options.append(QJsonObject{{"value",4},{"label","极客性能"}});
        auto r=state(raw.toInt(),"Lenovo ITS",options);r["requiresAc"]=4;
        return r;
    }
    if(id=="charge"){
        auto raw=e.query(Protocol::Charge,255,1);auto mode=Protocol::chargeMode(raw);
        auto opts=Protocol::chargeOptions(raw);
        if(!mode||opts.size()<2)throw Failure{"固件未声明可切换的充电模式"};
        QJsonArray a;QStringList names{"标准充电","电池养护","快速充电"};for(int v:opts)a.append(QJsonObject{{"value",v},{"label",names[v]}});
        auto r=state(*mode,"Lenovo EnergyDrv",a);r["raw"]=QString::number(raw,16);return r;
    }
    if(id=="fnlock"||id=="usbAc"||id=="usbDc"){
        uint32_t cap=id=="fnlock"?0x200:id=="usbAc"?0x40:0x4000;
        uint32_t raw=e.query(Protocol::Settings,2);
        if(!(raw&cap))throw Failure{"固件未声明支持此功能"};
        return state((raw&(cap<<1))?1:0,"Lenovo EnergyDrv",choices({{0,"关闭"},{1,"开启"}}));
    }
    if(id=="night"){
        auto raw=e.query(Protocol::Night,0x11);
        if(!(raw&1))throw Failure{"固件未声明支持夜间充电"};
        return state((raw&0x10)?1:0,"Lenovo EnergyDrv",choices({{0,"关闭"},{1,"开启"}}));
    }
    if(id=="backlight"){
        auto levels=Protocol::backlightLevels(e.query(Protocol::Backlight,1));
        if(!levels)throw Failure{"固件未返回已知的背光能力"};
        auto raw=e.query(Protocol::Backlight,uint32_t((*levels<<4)|2));auto value=Protocol::backlightValue(raw,*levels);
        if(!value)throw Failure{"键盘背光当前状态未知"};
        QJsonArray a;QStringList names{"关闭","低亮度","高亮度","自动"};for(int i=0;i<=*levels;i++)a.append(QJsonObject{{"value",i},{"label",names[i]}});
        return state(*value,"Lenovo EnergyDrv",a);
    }
    if(id=="power"){
        GUID g{};DWORD err=acOnline()?PowerGetUserConfiguredACPowerMode(&g):PowerGetUserConfiguredDCPowerMode(&g);if(err!=ERROR_SUCCESS)throw Failure{systemError(err)};
        int value=-1;for(int i=0;i<3;i++)if(IsEqualGUID(g,powerGuids[i]))value=i;
        if(value<0)throw Failure{"当前电源计划不支持标准 Windows 电源模式"};
        return state(value,"Windows",choices({{0,"最佳能效"},{1,"平衡"},{2,"最佳性能"}}));
    }
    if(id=="brightness"){
        Wmi w;auto o=w.first("SELECT * FROM WmiMonitorBrightness WHERE Active=TRUE");auto v=prop(o.Get(),L"CurrentBrightness");
        if(!v.isValid())throw Failure{"未读取到内置屏幕亮度"};return state(v.toInt(),"Windows WMI");
    }
    if(id=="refresh"){
        Display d;std::set<int> rates;for(const auto &m:d.modes())rates.insert(int(m.dmDisplayFrequency));
        QJsonArray opts;for(int hz:rates)opts.append(QJsonObject{{"value",hz},{"label",QString::number(hz)+" Hz"}});
        return state(int(d.mode.dmDisplayFrequency),"Windows Display",opts);
    }
    if(id=="volume"||id=="speakerMute"||id=="micMute"){
        auto audio=endpoint(id=="micMute"?eCapture:eRender);
        if(id=="volume"){
            float v=0;check(audio->GetMasterVolumeLevelScalar(&v));auto r=state(qRound(v*100),"Windows Core Audio");
            UINT index=0,steps=0;DWORD hardware=0;float low=0,high=0,step=0;
            if(SUCCEEDED(audio->GetVolumeStepInfo(&index,&steps))){r["stepIndex"]=int(index);r["steps"]=int(steps);}
            if(SUCCEEDED(audio->QueryHardwareSupport(&hardware)))r["hardwareVolume"]=(hardware&ENDPOINT_HARDWARE_SUPPORT_VOLUME)!=0;
            if(SUCCEEDED(audio->GetVolumeRange(&low,&high,&step))){r["minDb"]=low;r["maxDb"]=high;r["stepDb"]=step;}
            float db=0;if(SUCCEEDED(audio->GetMasterVolumeLevel(&db)))r["currentDb"]=db;
            return r;
        }
        BOOL mute=FALSE;check(audio->GetMute(&mute));return state(mute?1:0,"Windows Core Audio",choices({{0,"关闭"},{1,"开启"}}));
    }
    throw Failure{"未知功能"};
}
const QStringList ids{"its","power","charge","night","usbAc","usbDc","fnlock","backlight","brightness","refresh","volume","speakerMute","micMute","gpu","flip","oc","ocCore","ocMemory"};
}
namespace Backend {
bool isAdministrator(){BOOL admin=FALSE;SID_IDENTIFIER_AUTHORITY nt=SECURITY_NT_AUTHORITY;PSID sid=nullptr;if(AllocateAndInitializeSid(&nt,2,SECURITY_BUILTIN_DOMAIN_RID,DOMAIN_ALIAS_RID_ADMINS,0,0,0,0,0,0,&sid)){CheckTokenMembership(nullptr,sid,&admin);FreeSid(sid);}return admin;}
bool isWindows11(){
    using Fn=LONG(WINAPI*)(OSVERSIONINFOW*);auto f=reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion"));
    OSVERSIONINFOW v{};v.dwOSVersionInfoSize=sizeof(v);return f && f(&v)==0 && v.dwMajorVersion>=10 && v.dwBuildNumber>=22000;
}
bool relaunchElevated(){QString path=QCoreApplication::applicationFilePath();auto rc=ShellExecuteW(nullptr,L"runas",reinterpret_cast<LPCWSTR>(path.utf16()),L"--elevated-handoff",nullptr,SW_SHOWNORMAL);return reinterpret_cast<INT_PTR>(rc)>32;}
QJsonObject read(){
    QJsonObject r{{"schema",1},{"timestamp",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},{"administrator",isAdministrator()},{"windows11",isWindows11()}};
    r["model"]=reg(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\BIOS",L"SystemProductName").toString();
    r["product"]=reg(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\BIOS",L"SystemFamily").toString();
    SYSTEM_POWER_STATUS p{};
    if(GetSystemPowerStatus(&p)){r["ac"]=p.ACLineStatus==1;r["batteryPercent"]=p.BatteryLifePercent<=100?QJsonValue(p.BatteryLifePercent):QJsonValue(QJsonValue::Null);}
    try{
        ComApartment apartment;Energy e;QJsonObject features;
        for(const auto &id:ids+Input::ids()+QStringList{"fnCtrlSwap"}){try{features[id]=feature(id,e);}catch(const Failure &f){features[id]=failure(f.message);}}
        r["features"]=features;
        QJsonObject battery;
        try{Wmi w;auto full=w.first("SELECT * FROM BatteryFullChargedCapacity WHERE Active=TRUE");auto design=w.first("SELECT * FROM BatteryStaticData WHERE Active=TRUE");
            double f=prop(full.Get(),L"FullChargedCapacity").toDouble(),d=prop(design.Get(),L"DesignedCapacity").toDouble();
            if(f>0&&d>0){battery["fullWh"]=f/1000;battery["designWh"]=d/1000;battery["health"]=qRound(f*100/d);}
            try{auto cycle=w.first("SELECT * FROM BatteryCycleCount WHERE Active=TRUE");auto c=prop(cycle.Get(),L"CycleCount");if(c.isValid())battery["cycles"]=c.toInt();}catch(const Failure&){}
        }catch(const Failure&){}
        auto vendor=Vendor::probe();auto vb=vendor["battery"].toObject();
        auto copy=[&](const char *from,const char *to,double scale=1.){if(vb[from].isDouble())battery[to]=vb[from].toDouble()/scale;};
        copy("FullChargeCapacity","fullWh",1000);copy("DesignCapacity","designWh",1000);copy("RemainingCapacity","currentWh",1000);copy("Temperature","temperatureC");copy("CycleCount","cycles");copy("Voltage","voltageV",1000);copy("Current","currentMa");copy("DesignVoltage","designVoltageV",1000);copy("ChargeStatus","chargeStatus");
        if(vb["DeviceChemistry"].isString())battery["chemistry"]=vb["DeviceChemistry"];
        if(vb["ManufactureName"].isString())battery["manufacturer"]=vb["ManufactureName"];
        if(vendor["storage80"].isBool())battery["storage80"]=vendor["storage80"];
        if(battery["fullWh"].toDouble()>0&&battery["designWh"].toDouble()>0)battery["healthExact"]=battery["fullWh"].toDouble()*100/battery["designWh"].toDouble();
        battery["provider"]=vb["Temperature"].isDouble()?"Lenovo IdeaBatteryAgent":"Windows WMI";
        if(vendor.contains("error"))battery["vendorError"]=vendor["error"];
        r["battery"]=battery;
    }catch(const Failure &f){r["error"]=f.message;}
    return r;
}
QJsonObject apply(const QString &id,int value){
    QJsonObject r{{"ok",false},{"id",id},{"requested",value}};
    std::optional<int> refreshPrevious;
    std::optional<double> volumeTargetDb;
    if(!Protocol::allowedRequest(id,value)){r["error"]="请求不在允许的设置范围内";return r;}
    try{
        if(!isWindows11())throw Failure{"仅支持 Windows 11"};
        Handle mutex(CreateMutexW(nullptr,FALSE,L"Local\\ThinkVantageHardwareWrite"));
        if(!mutex.h)throw Failure{systemError(GetLastError())};
        DWORD wait=WaitForSingleObject(mutex.h,0);
        if(wait!=WAIT_OBJECT_0 && wait!=WAIT_ABANDONED)throw Failure{"另一个设置正在应用，请稍后再试"};
        struct Unlock{HANDLE h;~Unlock(){ReleaseMutex(h);}} unlock{mutex.h};
        ComApartment apartment;Energy e;
        // Read and validate fresh capability state in the worker before every write.
        auto before=feature(id,e);
        if(id=="backlightRemember")e.send({0x83102154,value?19u:3u,4});
        else if(id=="fnCtrlSwap"){
            Wmi w;auto rc=w.invoke("LENOVO_BIOS_ASSISTANT","SetValue",{{"IndexData",1},{"ValueData",uint32_t(value)}},L"ReturnData");
            if(!(rc&0x80000000u))throw Failure{"固件拒绝 Fn 与 Ctrl 互换"};
        }
        else if(Input::owns(id))Input::set(id,value);
        else if(id=="flip"){Firmware f;f.set(value);}
        else if(id=="gpu"){
            auto opts=before["options"].toArray();bool offered=false;for(auto opt:opts)if(opt.toObject()["value"].toInt()==value)offered=true;if(!offered)throw Failure{"此 GPU 模式不受支持"};
            if(value==1){try{if(Vendor::gpuBusy())throw Failure{"独显正在被应用占用，请关闭使用独显的程序后重试"};}catch(const std::exception &err){throw Failure{QString::fromUtf8(err.what())};}}
            Wmi w;w.invoke("LENOVO_GAMEZONE_DATA","SetIGPUModeStatus",{{"mode",uint32_t(value)}});
        }
        else if(id=="oc"||id=="ocCore"||id=="ocMemory"){
            auto d=before["details"].toObject();int enabled=d["rawState"].toInt()==2?1:0,core=d["core"].toInt(),memory=d["memory"].toInt();
            if(id=="oc")enabled=value;
            else {QString key=id=="ocCore"?"core":"memory";int min=d[key+"Min"].toInt(),max=d[key+"Max"].toInt(),step=d[key+"Step"].toInt();if(!enabled||!Protocol::validOffset(value,min,max,step))throw Failure{"超频未开启，或频率偏移超出设备范围"};if(id=="ocCore")core=value;else memory=value;}
            if(enabled&&!acOnline())throw Failure{"GPU 超频需要连接电源"};
            if(!enabled){core=0;memory=0;}
            // The screenshot's coupled policy: adaptive mode becomes performance.
            auto original=reg(HKEY_LOCAL_MACHINE,itsPath,L"ITS_CurrentSettingV");bool changedMode=false;
            if(enabled&&original.isValid()&&original.toInt()==0){ServiceHandle scm(OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT));ServiceHandle service(OpenServiceW(scm.h,L"LenovoProcessManagement",SERVICE_USER_DEFINED_CONTROL));SERVICE_STATUS s{};if(!ControlService(service.h,165,&s))throw Failure{systemError(GetLastError())};changedMode=true;QThread::msleep(250);}
            try{
                Vendor::setOc(enabled,core,memory);auto after=ocState();
                if(after["rawState"].toInt()!=(enabled?2:1)||after["core"].toInt()!=core||after["memory"].toInt()!=memory)throw Failure{"原厂 GPU 插件未确认超频设置"};
            }catch(...){if(changedMode){try{ServiceHandle scm(OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT));ServiceHandle service(OpenServiceW(scm.h,L"LenovoProcessManagement",SERVICE_USER_DEFINED_CONTROL));SERVICE_STATUS s{};ControlService(service.h,163,&s);}catch(...){}}throw;}
        }
        else if(id=="its"){
            auto command=Protocol::itsCommand(value);if(!command)throw Failure{"此性能档位尚未开放"};
            if(value==4&&!acOnline())throw Failure{"请连接电源后切换极客性能模式"};
            bool offered=false;for(auto o:before["options"].toArray())if(o.toObject()["value"].toInt()==value)offered=true;if(!offered)throw Failure{"固件未声明支持此性能档位"};
            ServiceHandle scm(OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT));
            ServiceHandle svc(OpenServiceW(scm.h,L"LenovoProcessManagement",SERVICE_USER_DEFINED_CONTROL));
            SERVICE_STATUS status{};if(!ControlService(svc.h,DWORD(*command),&status))throw Failure{systemError(GetLastError())};
        }else if(id=="charge"){
            auto commands=Protocol::chargeCommands(e.query(Protocol::Charge,255,1),value);if(commands.isEmpty())throw Failure{"固件不支持此充电模式"};
            for(const auto &c:commands)e.send(c);
        }else if(id=="fnlock"||id=="usbAc"||id=="usbDc"){
            uint32_t on=id=="fnlock"?14:id=="usbAc"?10:19,off=id=="fnlock"?15:id=="usbAc"?11:18;
            e.send({Protocol::Settings,value?on:off,4});
        }else if(id=="night")e.send({Protocol::Night,value?0x80000012u:0x12u,4});
        else if(id=="backlight"){
            auto levels=Protocol::backlightLevels(e.query(Protocol::Backlight,1));auto cmd=levels?Protocol::backlightCommand(*levels,value):std::nullopt;
            if(!cmd)throw Failure{"背光档位不受支持"};e.send(*cmd);
        }else if(id=="power"){
            DWORD err=acOnline()?PowerSetUserConfiguredACPowerMode(&powerGuids[value]):PowerSetUserConfiguredDCPowerMode(&powerGuids[value]);if(err!=ERROR_SUCCESS)throw Failure{systemError(err)};
        }else if(id=="brightness"){Wmi w;w.brightness(value);}
        else if(id=="refresh"){refreshPrevious=before["value"].toInt();Display d;d.set(value);}
        else {auto a=endpoint(id=="micMute"?eCapture:eRender);if(id=="volume"){check(a->SetMasterVolumeLevelScalar(float(value)/100,nullptr));float db=0;if(SUCCEEDED(a->GetMasterVolumeLevel(&db)))volumeTargetDb=db;}else check(a->SetMute(value!=0,nullptr));}
        QElapsedTimer timer;timer.start();
        do{
            QThread::msleep(150);auto after=feature(id,e);
            bool exact=after["value"].toInt(-999)==value;
            bool rounded=id=="volume"&&volumeTargetDb&&after["hardwareVolume"].toBool()&&after["currentDb"].isDouble()&&after["minDb"].isDouble()&&after["maxDb"].isDouble()&&after["stepDb"].isDouble()&&Protocol::nearestVolumeStep(*volumeTargetDb,after["currentDb"].toDouble(),after["minDb"].toDouble(),after["maxDb"].toDouble(),after["stepDb"].toDouble());
            if(exact||rounded){r["ok"]=true;r["state"]=after;r["previous"]=before["value"];if(!exact){r["hardwareRounded"]=true;r["message"]=QString("音量已调整为 %1%（设备可用档位）。").arg(after["value"].toInt());}return r;}
        }while(timer.elapsed()<3000);
        throw Failure{"设置已发送，但设备未确认目标状态。请刷新检查当前状态。"};
    }catch(const std::exception &err){r["error"]=QString::fromUtf8(err.what());}
    catch(const Failure &f){
        r["error"]=f.message;
        if(refreshPrevious){try{Display d;d.set(*refreshPrevious);r["error"]=f.message+" 已恢复原刷新率。";}catch(const Failure &restore){r["error"]=f.message+" 恢复失败："+restore.message;}}
    }
    return r;
}
QJsonObject confirmRefresh(int previous){
    // The device worker survives GUI failure. A broken stdin pipe or a timeout
    // restores the previous mode without depending on the UI event loop.
    bool keep=false;QElapsedTimer timer;timer.start();
    HANDLE input=GetStdHandle(STD_INPUT_HANDLE);QByteArray line;
    while(timer.elapsed()<15000){
        DWORD available=0;
        if(!PeekNamedPipe(input,nullptr,0,nullptr,&available,nullptr))break;
        if(available){char buf[64];DWORD n=0;if(!ReadFile(input,buf,qMin<DWORD>(available,64),&n,nullptr)||!n)break;line.append(buf,int(n));if(line.contains('\n')){keep=line.trimmed()=="keep";break;}}
        QThread::msleep(80);
    }
    if(keep)return {{"ok",true},{"message","已保留新刷新率。"}};
    try{Display d;d.set(previous);Display after;if(int(after.mode.dmDisplayFrequency)!=previous)throw Failure{"恢复后的刷新率未确认"};return {{"ok",true},{"message","已恢复原刷新率。"}};}
    catch(const Failure &f){return {{"ok",false},{"error","刷新率自动恢复失败："+f.message}};}
}
}
