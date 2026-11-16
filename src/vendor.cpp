#include "vendor.h"
#include "vendor_data.h"
#include <windows.h>
#include <objbase.h>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QElapsedTimer>
#include <stdexcept>
#include <vector>
#include <algorithm>
namespace Vendor {
QString root(){
    QDir base(QCoreApplication::applicationDirPath());
    if(QFileInfo::exists(base.filePath("vendor/manifest.json")))return base.filePath("vendor");
    base.cdUp();return base.filePath("vendor");
}
QJsonObject probe(){
    const auto failed=[](const QString &stage,const QString &error){return QJsonObject{{"ok",false},{"error",error},{"diagnostics",QJsonArray{QJsonObject{{"provider","Lenovo VendorBridge"},{"stage",stage},{"error",error}}}}};};
    QProcess p;p.setProgram(QDir(root()).filePath("idea/VendorBridge.exe"));p.setArguments({"probe"});
    QElapsedTimer timer;timer.start();p.start();
    if(!p.waitForStarted(3000))return failed("start","原厂电池组件未能启动："+p.errorString());
    if(!p.waitForFinished(qMax(1,10000-int(timer.elapsed())))){p.kill();p.waitForFinished(1000);return failed("timeout","原厂组件读取超时");}
    const auto bytes=p.readAllStandardOutput().trimmed();
    if(bytes.size()>1024*1024)return failed("parse","原厂电池组件输出超出大小限制");
    auto d=QJsonDocument::fromJson(bytes);
    if(p.exitStatus()!=QProcess::NormalExit||p.exitCode()!=0)return failed("exit","原厂电池组件异常退出，请检查 vendor 目录");
    if(!d.isObject()||d.object().isEmpty())return failed("parse","原厂电池组件未返回有效 JSON");
    return d.object();
}
class Module {
    HMODULE library=nullptr;
public:
    explicit Module(const QString &path){library=LoadLibraryExW(reinterpret_cast<LPCWSTR>(path.utf16()),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);if(!library)throw std::runtime_error("无法加载原厂 GPU 组件");}
    ~Module(){if(library)FreeLibrary(library);}
    template<class F> F get(const char *name){auto p=GetProcAddress(library,name);if(!p)throw std::runtime_error("原厂 GPU 组件接口不匹配");return reinterpret_cast<F>(p);}
};
void __cdecl logCallback(const char*){}
// Worker processes terminate after each request. The plugin's own singleton
// may own background threads, so retain its DLL until process teardown.
Module &gpu(){
    static Module *m=nullptr;
    if(!m){m=new Module(QDir(root()).filePath("gpu/SEGPUOCPlugin.dll"));
        m->get<int(__cdecl*)(void(__cdecl*)(const char*))>("SWRegisterLogCallback")(logCallback);
        m->get<void(__cdecl*)()>("InitPlugin")();}
    return *m;
}
QJsonObject ocProbe(){
    auto &m=gpu();int v[8]={-1,-1,-1,-1,-1,-1,-1,-1};
    m.get<void(__cdecl*)(int*,int*,int*,int*,int*,int*,int*,int*)>("GetOCParam")(&v[0],&v[1],&v[2],&v[3],&v[4],&v[5],&v[6],&v[7]);
    return {{"rawState",m.get<int(__cdecl*)()>("GetOCState")()},{"core",v[0]},{"coreBase",v[1]},{"coreDefault",v[2]},{"coreMax",v[3]},{"memory",v[4]},{"memoryBase",v[5]},{"memoryDefault",v[6]},{"memoryMax",v[7]},{"pluginAbility",m.get<int(__cdecl*)()>("GetAbility")()}};
}
void setOc(int enabled,int core,int memory){gpu().get<void(__cdecl*)(int,int,int)>("SetOCState")(enabled?2:1,core,memory);}
QJsonObject gpuProcessesRaw(){
    const auto unavailable=[](const QString &stage,const QString &error){return QJsonObject{{"available",false},{"provider","Lenovo GPUInfo"},{"stage",stage},{"error",error},{"busy",QJsonValue::Null},{"processes",QJsonArray{}}};};
    try{
        // The standalone worker must establish its own COM apartment; callers
        // must not depend on a backend's apartment in another process.
        struct Apartment{HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);~Apartment(){if(SUCCEEDED(hr))CoUninitialize();}} apartment;
        if(FAILED(apartment.hr)&&apartment.hr!=RPC_E_CHANGED_MODE)return unavailable("initialize","独显信息进程无法初始化 COM");
        static Module *retained=new Module(QDir(root()).filePath("gpu/GPUInfo.dll"));auto &m=*retained;
        if(m.get<int(__cdecl*)()>("GPUInfoInit")()<0)return unavailable("initialize","无法初始化原厂独显信息组件");
        auto get=m.get<int(__cdecl*)(void*,int)>("GetGpuOccupyProcessInfo");
        // The size query returns bytes, as used by the original OEM wrapper.
        for(int attempt=0;attempt<3;++attempt){
            const int size=get(nullptr,0);
            if(size<0||size>1024*1024||(size%sizeof(wchar_t))!=0)return unavailable("size","原厂独显进程列表大小异常");
            if(size==0){auto result=VendorData::parseGpuProcesses({});result["observedAt"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);return result;}
            std::vector<wchar_t> data(size/sizeof(wchar_t)+1,L'\0');
            const int written=get(data.data(),size);
            if(written<0)return unavailable("read","原厂独显进程列表读取失败");
            if(written>size)continue;
            const auto terminator=std::find(data.begin(),data.end()-1,L'\0');
            if(terminator==data.end()-1)return unavailable("read","原厂独显进程列表缺少结束标记");
            if(terminator==data.begin())return unavailable("read","原厂独显进程列表在读取时失效，请重试");
            auto result=VendorData::parseGpuProcesses(QString::fromWCharArray(data.data(),int(terminator-data.begin())));
            result["observedAt"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
            return result;
        }
        return unavailable("read","原厂独显进程列表持续变化，请重试");
    }catch(const std::exception &error){return unavailable("load",QString::fromUtf8(error.what()));}
}
QJsonObject gpuProcesses(){
    const auto unavailable=[](const QString &stage,const QString &error){return QJsonObject{{"available",false},{"provider","Lenovo GPUInfo"},{"stage",stage},{"error",error},{"busy",QJsonValue::Null},{"processes",QJsonArray{}}};};
    QProcess process;process.setProgram(QCoreApplication::applicationFilePath());
    process.setArguments({"--gpu-processes-worker"});
    QElapsedTimer timer;timer.start();process.start();
    if(!process.waitForStarted(3000))return unavailable("workerStart","无法启动独显信息隔离进程："+process.errorString());
    if(!process.waitForFinished(qMax(1,15000-int(timer.elapsed())))){
        process.kill();process.waitForFinished(1000);return unavailable("workerTimeout","独显信息读取超时，请稍后重试");
    }
    if(process.exitStatus()!=QProcess::NormalExit){auto result=unavailable("workerCrash","原厂独显信息组件异常退出，未能确认占用状态");result["nativeExitCode"]=process.exitCode();return result;}
    const auto output=process.readAllStandardOutput();
    if(output.size()>4*1024*1024)return unavailable("workerProtocol","独显信息进程输出超过大小限制");
    // GPUInfo.dll writes diagnostic printf output to stdout. Keep that output
    // inside this process boundary; only the explicit final marker is a result.
    auto result=VendorData::parseGpuWorkerOutput(output);
    if(result["available"].toBool()&&process.exitCode()!=0)return unavailable("workerExit","独显信息进程退出状态与返回结果不一致");
    return result;
}
bool gpuBusy(){
    const auto result=gpuProcesses();
    if(!result["available"].toBool())throw std::runtime_error(result["error"].toString().toUtf8().constData());
    return result["busy"].toBool();
}
}
