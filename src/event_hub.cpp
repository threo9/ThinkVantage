#include "event_hub.h"
#include <windows.h>
#include <powrprof.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <wrl/client.h>
#include <QCoreApplication>
#include <QWinEventNotifier>
#include <QTimer>
#include <atomic>
#include <mutex>
#include <array>

using Microsoft::WRL::ComPtr;
namespace {
// A callback can arrive on an audio service thread while the window closes.
// Detaching under this lock prevents queueing into an already destroyed object.
class AudioCallback final : public IMMNotificationClient, public IAudioEndpointVolumeCallback {
    std::atomic<ULONG> refs{1};
    std::mutex mutex;
    EventHub *owner;
    void notify(bool topology){std::lock_guard<std::mutex> lock(mutex);if(owner)QMetaObject::invokeMethod(owner,"audioChanged",Qt::QueuedConnection,Q_ARG(bool,topology));}
public:
    explicit AudioCallback(EventHub *hub):owner(hub){}
    void detach(){std::lock_guard<std::mutex> lock(mutex);owner=nullptr;}
    ULONG STDMETHODCALLTYPE AddRef() override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override{auto n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void **out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(iid==__uuidof(IUnknown)||iid==__uuidof(IMMNotificationClient))*out=static_cast<IMMNotificationClient*>(this);
        else if(iid==__uuidof(IAudioEndpointVolumeCallback))*out=static_cast<IAudioEndpointVolumeCallback*>(this);
        if(!*out)return E_NOINTERFACE;AddRef();return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow,ERole,LPCWSTR) override{notify(true);return S_OK;}
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override{notify(true);return S_OK;}
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override{notify(true);return S_OK;}
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR,DWORD) override{notify(true);return S_OK;}
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR,const PROPERTYKEY) override{notify(true);return S_OK;}
    HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA) override{notify(false);return S_OK;}
};
}

struct EventHub::Impl {
    HPOWERNOTIFY power=nullptr;
    HKEY its=nullptr;
    HANDLE registryEvent=nullptr;
    QWinEventNotifier *registryNotifier=nullptr;
    HRESULT apartment=E_FAIL;
    ComPtr<IMMDeviceEnumerator> audio;
    AudioCallback *callback=nullptr;
    std::array<ComPtr<IAudioEndpointVolume>,2> endpoints;
    bool registered=false;
    QTimer *audioDebounce=nullptr;
    bool rebindPending=false;
    void armRegistry(){
        if(its&&registryEvent&&RegNotifyChangeKeyValue(its,FALSE,REG_NOTIFY_CHANGE_LAST_SET,registryEvent,TRUE)!=ERROR_SUCCESS)
            if(registryNotifier)registryNotifier->setEnabled(false);
    }
    void bindAudio(){
        for(auto &endpoint:endpoints){if(endpoint)endpoint->UnregisterControlChangeNotify(callback);endpoint.Reset();}
        if(!audio||!callback)return;
        for(int i=0;i<2;++i){
            ComPtr<IMMDevice> device;
            if(SUCCEEDED(audio->GetDefaultAudioEndpoint(i==0?eRender:eCapture,eConsole,&device))&&
               SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_INPROC_SERVER,nullptr,reinterpret_cast<void**>(endpoints[i].GetAddressOf()))))
                endpoints[i]->RegisterControlChangeNotify(callback);
        }
    }
    ~Impl(){
        if(callback)callback->detach();
        if(registered)audio->UnregisterEndpointNotificationCallback(callback);
        for(auto &endpoint:endpoints){if(endpoint)endpoint->UnregisterControlChangeNotify(callback);endpoint.Reset();}
        audio.Reset();if(callback)callback->Release();
        if(registryNotifier){registryNotifier->setEnabled(false);delete registryNotifier;}
        if(its)RegCloseKey(its);if(registryEvent)CloseHandle(registryEvent);
        if(power)UnregisterPowerSettingNotification(power);
        if(SUCCEEDED(apartment))CoUninitialize();
    }
};

EventHub::EventHub(quintptr window,QObject *parent):QObject(parent),impl(std::make_unique<Impl>()){
    QCoreApplication::instance()->installNativeEventFilter(this);
    impl->audioDebounce=new QTimer(this);impl->audioDebounce->setSingleShot(true);impl->audioDebounce->setInterval(100);
    connect(impl->audioDebounce,&QTimer::timeout,this,[this]{if(closing)return;if(impl->rebindPending){impl->rebindPending=false;impl->bindAudio();}emit invalidated("音频设备或音量变化");});
    impl->power=RegisterPowerSettingNotification(reinterpret_cast<HANDLE>(window),&GUID_ACDC_POWER_SOURCE,DEVICE_NOTIFY_WINDOW_HANDLE);
    if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Services\\LenovoProcessManagement\\Performance\\PowerSlider",0,KEY_NOTIFY,&impl->its)==ERROR_SUCCESS){
        impl->registryEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(impl->registryEvent){
            impl->registryNotifier=new QWinEventNotifier(impl->registryEvent,this);
            connect(impl->registryNotifier,&QWinEventNotifier::activated,this,[this](HANDLE){impl->armRegistry();emit invalidated("Lenovo ITS 状态变化");});
            impl->armRegistry();
        }
    }
    impl->apartment=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(SUCCEEDED(impl->apartment)||impl->apartment==RPC_E_CHANGED_MODE){
        if(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&impl->audio)))){
            impl->callback=new AudioCallback(this);
            impl->registered=SUCCEEDED(impl->audio->RegisterEndpointNotificationCallback(impl->callback));
            impl->bindAudio();
        }
    }
}
EventHub::~EventHub(){closing=true;if(impl->callback)impl->callback->detach();impl->audioDebounce->stop();QCoreApplication::instance()->removeNativeEventFilter(this);}
void EventHub::audioChanged(bool topology){if(closing)return;impl->rebindPending=impl->rebindPending||topology;impl->audioDebounce->start();}
bool EventHub::nativeEventFilter(const QByteArray &,void *message,qintptr *){
    auto msg=static_cast<MSG*>(message);if(!msg)return false;
    if(msg->message==WM_POWERBROADCAST){
        if(msg->wParam==PBT_APMRESUMEAUTOMATIC||msg->wParam==PBT_APMRESUMESUSPEND||msg->wParam==PBT_APMPOWERSTATUSCHANGE||msg->wParam==PBT_POWERSETTINGCHANGE)
            emit invalidated("供电或睡眠恢复事件");
    }else if(msg->message==WM_DISPLAYCHANGE)emit invalidated("显示拓扑或模式变化");
    else if(msg->message==WM_DEVICECHANGE)emit invalidated("设备连接变化");
    else if(msg->message==WM_SETTINGCHANGE)emit invalidated("Windows 设置变化");
    return false;
}
