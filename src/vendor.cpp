#include "vendor.h"
#include <windows.h>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonArray>
#include <stdexcept>
namespace Vendor {
QString root(){
    QDir base(QCoreApplication::applicationDirPath());
    if(QFileInfo::exists(base.filePath("vendor/manifest.json")))return base.filePath("vendor");
    base.cdUp();return base.filePath("vendor");
}
QJsonObject probe(){
    QProcess p;p.setProgram(QDir(root()).filePath("idea/VendorBridge.exe"));p.setArguments({"probe"});p.start();
    if(!p.waitForFinished(10000)){p.kill();p.waitForFinished();return {{"error","原厂组件读取超时"}};}
    auto d=QJsonDocument::fromJson(p.readAllStandardOutput().trimmed());
    if(p.exitCode()!=0||!d.isObject())return {{"error","原厂电池组件未能启动，请检查 vendor 目录"}};
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
bool gpuBusy(){
    static Module *retained=new Module(QDir(root()).filePath("gpu/GPUInfo.dll"));auto &m=*retained;
    m.get<int(__cdecl*)()>("GPUInfoInit")();
    auto get=m.get<int(__cdecl*)(void*,int)>("GetGpuOccupyProcessInfo");int size=get(nullptr,0);
    if(size<0||size>1024*1024)throw std::runtime_error("无法确认独显占用状态");
    if(size==0)return false;
    QByteArray data(size+sizeof(wchar_t),0);get(data.data(),size);
    auto text=QString::fromWCharArray(reinterpret_cast<const wchar_t*>(data.constData()));
    for(const auto &entry:text.split('|',Qt::SkipEmptyParts)){
        auto fields=entry.split(':');if(fields.size()<2)continue;
        auto name=fields[1].toLower();
        if(name!="dwm.exe"&&name!="explorer.exe"&&name!="lenovovantage.exe"&&name!="thinkvantage.exe")return true;
    }return false;
}
}
