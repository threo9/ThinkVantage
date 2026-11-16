# 0.2.0 接口核对记录

用户请求是对照图片完善功能，并允许提取原厂实现、集成所需运行库。图片和早先研究文档属于参考材料，其描述不自动证明本机支持。

| 功能 | 实读原厂实现 | 本项目路径 |
|---|---|---|
| 电池与温度 | IdeaBatteryAgent.BatteryAgent.GetBatteryInformation | 隔离 .NET Framework 进程调用未修改的原厂程序集，仅导出指定字段 |
| 80% 养护档 | CChargingMode.DoesSupportStorage80 | 实际查询支持，再显示 75%/80% 原厂档说明 |
| 极客性能 | PowerAgent.GetITSMode/SetITSMode | 能力位 0x10，SCM control 172，接电与回读 |
| GPU 三档 | ProductivitySystemAddin.WmiAgent.Get/SetGPUMode | GameZone IsSupportIGPUMode、Get/SetIGPUModeStatus；0 混合、1 核显、2 自动 |
| 独显占用 | GPUInfoHandler.GPUProcessInfo | GPUInfoInit、GetGpuOccupyProcessInfo，不杀进程 |
| GPU OC | SmartDisplayAddin.SmartOCAgent native delegates | COM 初始化、日志回调、InitPlugin、GetOCParam/GetOCState/SetOCState |
| OC 范围 | LENOVO_GAMEZONE_GPU_OC_DATA | ClockID 0 核心、1 显存；检查 Min/Max/Interval，不依赖实例顺序 |
| 开盖开机 | DeviceSettingsSystemAddin.UEFISystem | FBSWIF，GUID D743491E-F484-4952-A87D-8D5DD189B70C；读完整4字节和属性，仅改首字节 |

核对本机 vtr-ui-rainbow/26.0.978 的 Power 页 source maps。gpuWorkMode 使用 BIOS/GameZone，另一份 adaptiveGraphicsModes 使用旧 DynamicGpu，两者枚举不同。旧 CDynamicGPU 本机 Support=0，GameZone 则 IsSupportIGPUMode=3、当前模式=0，因此选择后者。

SEGPUOCPlugin 的 GetAbility=0，固件 IsSupportGpuOC=5，ClockID 对应 0–200/0–400 MHz。插件 fallback 上限500不用于扩大固件范围。保留差异证据，写入要求再次检查和回读；没有启用超频的实机验证。

GUI 不加载混合模式程序集，原厂电池桥接在独立进程运行；GPU 插件保留至原生 worker 退出，以免其后台线程访问已卸载模块。读失败保持未知，零偏移与查询失败分开。写入串行，范围与步长校验；固件变量权限在调用结束恢复。

原厂 RPC 返回403“Service do not trust the connecting Client”。没有修改服务信任配置或伪装受信任客户端，正式功能不依赖该通道。反编译输出与临时诊断留在 research，不随便携包分发。抽取版本与哈希见 vendor/manifest.json。

## 0.3.0 输入与人体感知

本次用户请求为实现新提供的两张图中功能。图片的说明、默认值及相机权限提示属于参考材料，不代表要求自动更改电脑配置或启用相机。程序读取本机状态，不复制截图的开关状态。

| 功能 | 本机原厂来源 | 本项目接口 |
|---|---|---|
| 背光记忆 | IdeaNotebookAddin.KeyboardHandler.Get/SetBacklightReserve | EnergyDrv IOCTL 0x83102154，能力 token 1=3，状态 token 2=1/3，写入 3/19 |
| Caps/Num OSD | IdeaKBDManagerAddin.HotkeyServiceInvoker | LenovoFnAndFunctionKeys 服务 controls 146/147 与 148/149；VantageToast 状态回读，检查服务与 FnHotkeyUtility |
| Fn/Ctrl | DeviceSettingsSystemAddin.FnCtrlAgent | LENOVO_BIOS_ASSISTANT，能力 bit31/bit3，GetValue/SetValue IndexData=1，检查 bit31 后独立回读 |
| 触控板 | SmartInteractAddin.PrecisionTouchpadHelper | Windows Ctrl+Win+F24，HKCU PrecisionTouchPad/Status Enabled 回读，不直接改注册表制造假状态 |
| 人体感知 | SmartInteractAddin 的 HumanPresenceDetectionRpcClient 和 Lenovo.Vantage.SmartSenseRpcClient.NativeWrap | 原始 x64 SmartSenseRpcClient.dll：HPDRpcClientInitialize / GetHPDCapability / GetHPDAllSetting / SetHPD* / Close；保留 64 位返回码，检查 capacity 和逐项能力位 |
| 距离与时序枚举 | 原厂 SmartSense 程序集与 Vantage 前端 source maps | 距离与锁定速度均为 0/1/2 档位；速度不冒充秒数；传感器1/2分别映射原厂标称距离 |

这是独立的人体感知服务 API，与被主 Vantage 服务拒绝的通用 add-in RPC 不同。没有修改服务信任策略。新实现没有加载整个 SmartInteractAddin，没有注入现有进程，也没有实现自有摄像头采集。

检查了 Windows SDK 的 TOUCHPAD_PARAMETERS_V1 与 [Microsoft SystemParametersInfo 文档](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-systemparametersinfow)：touchpadEnabled 是状态字段，SET 调用会忽略该字段。因此沿用本机原厂的快捷键切换路由；仅在当前状态与目标不同时发送，禁止修改键尚未松开时触发，并要求系统状态回读确认。
