#include "input.h"
#include "vendor.h"
#include "protocol.h"
#include <windows.h>
#include <tlhelp32.h>
#include <QDir>
#include <QJsonArray>
#include <stdexcept>
#include <array>
namespace {
[[noreturn]] void fail(const QString &s){throw std::runtime_error(s.toUtf8().constData());}
void check(bool ok,const QString &what){if(!ok)fail(what+QString("（错误 %1）").arg(GetLastError()));}
int reg(HKEY hive,const wchar_t *path,const wchar_t *name){DWORD value=0,size=4;auto rc=RegGetValueW(hive,path,name,RRF_RT_REG_DWORD,nullptr,&value,&size);if(rc!=ERROR_SUCCESS||value>1)fail("系统未提供已知状态，无法控制此设置");return int(value);}
struct Service {SC_HANDLE h=nullptr;~Service(){if(h)CloseServiceHandle(h);}};
constexpr auto hotkey=L"SYSTEM\\CurrentControlSet\\Services\\LenovoFnAndFunctionKeys";
constexpr auto osd=L"SYSTEM\\CurrentControlSet\\Services\\LenovoFnAndFunctionKeys\\VantageToast";
constexpr auto touchpad=L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\PrecisionTouchPad\\Status";
void hotkeyReady(bool caps){
    Service manager{OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT)};
    check(manager.h,"无法访问热键服务");Service service{OpenServiceW(manager.h,L"LenovoFnAndFunctionKeys",SERVICE_QUERY_STATUS)};
    check(service.h,"未检测到 Lenovo 热键服务");SERVICE_STATUS status{};check(QueryServiceStatus(service.h,&status),"无法查询热键服务");
    if(status.dwCurrentState!=SERVICE_RUNNING)fail("Lenovo 热键服务未运行");
    HANDLE processes=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);bool found=false;
    if(processes!=INVALID_HANDLE_VALUE){PROCESSENTRY32W p{};p.dwSize=sizeof(p);if(Process32FirstW(processes,&p))do{if(_wcsicmp(p.szExeFile,L"FnHotkeyUtility.exe")==0)found=true;}while(Process32NextW(processes,&p));CloseHandle(processes);}
    if(!found)fail("Lenovo 热键提示程序未运行");
    if(!caps){if(reg(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Services\\LenovoFnAndFunctionKeys\\Capbility",L"SupportNumlkKey")!=1)fail("设备没有 Num Lock 提示能力");return;}
    wchar_t image[2048]{};DWORD size=sizeof(image);
    check(RegGetValueW(HKEY_LOCAL_MACHINE,hotkey,L"ImagePath",RRF_RT_REG_SZ,nullptr,image,&size)==ERROR_SUCCESS,"无法读取热键组件版本");
    QString path=QString::fromWCharArray(image);if(path.startsWith('"'))path=path.section('"',1,1);
    DWORD ignored=0;auto p=reinterpret_cast<LPCWSTR>(path.utf16());DWORD bytes=GetFileVersionInfoSizeW(p,&ignored);QByteArray data(int(bytes),0);
    check(bytes&&GetFileVersionInfoW(p,0,bytes,data.data()),"无法核对热键组件版本");VS_FIXEDFILEINFO *info=nullptr;UINT n=0;
    check(VerQueryValueW(data.data(),L"\\",reinterpret_cast<void**>(&info),&n)&&n>=sizeof(*info),"热键组件版本未知");
    if(info->dwFileVersionMS<0x20000||(info->dwFileVersionMS==0x20000&&info->dwFileVersionLS<0x40000))fail("Caps Lock 提示需要热键组件 2.0.4.0 或更新版本");
    wchar_t language[LOCALE_NAME_MAX_LENGTH]{};ULONG languages=0,length=LOCALE_NAME_MAX_LENGTH;
    if(GetUserPreferredUILanguages(MUI_LANGUAGE_NAME,&languages,language,&length)&&QString::fromWCharArray(language).startsWith("ja",Qt::CaseInsensitive))fail("原厂组件不提供日语环境的 Caps Lock 提示开关");
}
QJsonArray choices(const QStringList &labels){QJsonArray a;for(int i=0;i<labels.size();++i)a.append(QJsonObject{{"value",i},{"label",labels[i]}});return a;}
QJsonObject state(int value,const QString &provider,const QJsonArray &options=choices({"关闭","开启"})){return {{"available",true},{"value",value},{"provider",provider},{"options",options}};}
struct Hpd {
    HMODULE dll=nullptr;void *handle=nullptr;uint64_t capability=0;uint32_t version=0;
    template<class F> F proc(const char *name){auto p=GetProcAddress(dll,name);if(!p)fail("人体感知组件接口不匹配");return reinterpret_cast<F>(p);}
    static void rc(int64_t code){if(code!=0)fail(QString("人体感知服务调用失败（%1），请检查 Lenovo Smart Sense 服务").arg(code));}
    Hpd(){
        auto path=QDir(Vendor::root()).filePath("presence/SmartSenseRpcClient.dll");
        dll=LoadLibraryExW(reinterpret_cast<LPCWSTR>(path.utf16()),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        check(dll,"无法加载人体感知组件，请检查 vendor/presence");
        try{rc(proc<int64_t(WINAPI*)(void**,void*)>("HPDRpcClientInitialize")(&handle,nullptr));if(!handle)fail("人体感知服务未连接");rc(proc<int64_t(WINAPI*)(void*,uint64_t*)>("GetHPDCapability")(handle,&capability));rc(proc<int64_t(WINAPI*)(void*,uint32_t*)>("GetHPDVersion")(handle,&version));}
        catch(...){if(handle)proc<int64_t(WINAPI*)(void*)>("HPDRpcClientClose")(handle);FreeLibrary(dll);throw;}
    }
    ~Hpd(){if(handle)proc<int64_t(WINAPI*)(void*)>("HPDRpcClientClose")(handle);if(dll)FreeLibrary(dll);}
    std::array<uint32_t,12> read(){
        std::array<uint32_t,12> v{};
        using Get=int64_t(WINAPI*)(void*,uint32_t*,uint32_t*,uint32_t*,uint32_t*,uint32_t*,uint32_t*,uint32_t*,uint32_t*,uint32_t*,uint32_t*,uint32_t*,uint32_t*);
        rc(proc<Get>("GetHPDAllSetting")(handle,&v[0],&v[1],&v[2],&v[3],&v[4],&v[5],&v[6],&v[7],&v[8],&v[9],&v[10],&v[11]));
        if(v[0]!=1)fail("人体感知服务未报告设备可用");return v;
    }
};
struct Binding {const char *id;const char *method;uint64_t mask;int index;bool triple;};
const Binding bindings[]={
    {"presenceLogin","SetHPDApproachEnabled",2,2,false},
    {"loginDistance","SetHPDApproachDistance",4,3,true},
    {"loginAuto","SetHPDApproachDistanceAutoAdjust",8,4,false},
    {"presenceLock","SetHPDPresenceLeaveEnabled",16,5,false},
    {"lockTimer","SetHPDPresenceLeaveWaitTime",32,6,true},
    {"cameraAssist","SetHPDPresenceLeaveCameraAssist",64,7,false},
    {"lockDistance","SetHPDPresenceLeaveDistance",128,8,true},
    {"lockAuto","SetHPDPresenceLeaveDistanceAutoAdjust",256,9,false},
    {"presenceVideo","SetHPDVideoAutoPauseResume",512,10,false}
};
const Binding *binding(const QString &id){for(const auto &b:bindings)if(id==b.id)return &b;return nullptr;}
}
namespace Input {
QStringList ids(){QStringList r{"backlightRemember","capsOsd","numOsd","touchpad"};for(const auto &b:bindings)r<<b.id;return r;}
bool owns(const QString &id){return ids().contains(id);}
QJsonObject feature(const QString &id){
    if(id=="touchpad")return state(reg(HKEY_CURRENT_USER,touchpad,L"Enabled"),"Windows Precision Touchpad");
    if(id=="capsOsd"||id=="numOsd"){hotkeyReady(id=="capsOsd");return state(reg(HKEY_LOCAL_MACHINE,osd,id=="capsOsd"?L"ShowCapslkOSD":L"ShowNumlkOSD"),"Lenovo 热键服务");}
    if(auto b=binding(id)){
        Hpd h;auto v=h.read();if(!(h.capability&b->mask))fail("本机人体感知组件未声明支持此设置");
        if(v[b->index]>uint32_t(b->triple?2:1))fail("人体感知当前值未知，未将其视为关闭");
        auto opts=b->triple?choices(id=="lockTimer"?QStringList{"快速","中速","慢速"}:QStringList{"近","中","远"}):choices({"关闭","开启"});
        if(id=="loginAuto"||id=="lockAuto")opts=choices({"手动设置最大检测距离","自动调整"});
        auto r=state(int(v[b->index]),"Lenovo Smart Sense",opts);
        bool enabled=true;
        if(id=="loginAuto"||id=="loginDistance")enabled=v[2]==1;
        if(id=="loginDistance"&&(h.capability&8))enabled=enabled&&v[4]==0;
        if(id=="lockAuto"||id=="lockDistance"||id=="lockTimer"||id=="cameraAssist")enabled=v[5]==1;
        if(id=="lockDistance"&&(h.capability&256))enabled=enabled&&v[9]==0;
        r["enabled"]=enabled;r["sensorType"]=int(v[1]);r["capability"]=int(h.capability);r["faceRegistered"]=(h.capability&1024)?QJsonValue(v[11]==1):QJsonValue(QJsonValue::Null);
        if(id=="loginDistance"||id=="lockDistance"){QJsonArray meters;for(double m:Protocol::presenceMeters(int(v[1]),id=="loginDistance"))meters.append(m);if(!meters.isEmpty())r["meters"]=meters;}
        return r;
    }
    fail("未知输入设备功能");
}
void set(const QString &id,int value){
    auto before=feature(id);if(!before["enabled"].toBool(true))fail("此高级设置当前不可调整，请先开启对应功能并检查距离模式");
    if(id=="touchpad"){
        if(before["value"].toInt()==value)return;
        for(int key:{VK_CONTROL,VK_LWIN,VK_RWIN,VK_MENU,VK_SHIFT})if(GetAsyncKeyState(key)&0x8000)fail("请松开 Ctrl、Win、Alt 和 Shift 键后重试");
        INPUT in[6]{};const WORD keys[]={VK_CONTROL,VK_LWIN,VK_F24,VK_F24,VK_LWIN,VK_CONTROL};
        for(int i=0;i<6;++i){in[i].type=INPUT_KEYBOARD;in[i].ki.wVk=keys[i];in[i].ki.dwFlags=i>=3?KEYEVENTF_KEYUP:0;}
        auto sent=SendInput(6,in,sizeof(INPUT));
        if(sent!=6){
            DWORD error=GetLastError();
            // A partial injection must not leave our synthetic modifiers held.
            if(sent>0){INPUT release[3]{};const WORD up[]={VK_F24,VK_LWIN,VK_CONTROL};for(int i=0;i<3;++i){release[i].type=INPUT_KEYBOARD;release[i].ki.wVk=up[i];release[i].ki.dwFlags=KEYEVENTF_KEYUP;}SendInput(3,release,sizeof(INPUT));}
            SetLastError(error);check(false,"Windows 未接受触控板切换请求");
        }
        return;
    }
    if(id=="capsOsd"||id=="numOsd"){
        Service manager{OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT)};check(manager.h,"无法访问热键服务");
        Service service{OpenServiceW(manager.h,L"LenovoFnAndFunctionKeys",SERVICE_USER_DEFINED_CONTROL)};check(service.h,"修改热键提示需要管理员权限");
        SERVICE_STATUS s{};DWORD command=id=="capsOsd"?(value?146:147):(value?148:149);check(ControlService(service.h,command,&s),"热键提示设置失败");return;
    }
    if(auto b=binding(id)){Hpd h;h.read();if(!(h.capability&b->mask))fail("人体感知能力已改变，请刷新");Hpd::rc(h.proc<int64_t(WINAPI*)(void*,uint32_t)>(b->method)(h.handle,uint32_t(value)));return;}
    fail("未知输入设备写入请求");
}
}
