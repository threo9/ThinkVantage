# ThinkVantage 0.6.0 验证记录

日期：2026-11-16，Asia/Hong_Kong。

## 0.6.0 静态链接与Qt裁剪

按用户最终要求仅裁剪Qt，保留全部32项业务控制、5个页面、9项托盘快捷控制、诊断和动效；未加入业务功能开关或精简配置。除版本号外，backend/input/vendor/window/mode_slider及其现有测试保持原实现。

从校验SHA-256的Qt 6.12.0原始qtbase源码构建Release静态Qt，启用静态MSVC运行库、体积优化及LTCG；关闭OpenGL、Vulkan、ICU、SQL、打印、DBus、SSL、HTTP、网络代理/缓存、SOCKS5、UDP、图形特效、PDF和未使用的Windows主题插件。项目继续使用Fusion和Windows平台插件，保留字体及无障碍功能。Qt源码未修改。安装头文件中的对应QT_FEATURE宏已核对为-1。

应用启用/MT、/O1、/GL、/Gy、/Gw、/OPT:REF及/OPT:ICF，静态插件显式导入。构建配置拒绝动态Qt，打包前使用PE导入表拒绝Qt DLL及主程序的动态VC/UCRT依赖。发布EXE导入表中仅有Windows系统组件；联想原厂DLL、.NET桥接及其所需运行库仍位于vendor目录，不宣称将这些专有组件静态化。

验证：静态Qt构建安装成功，应用Release构建及两项CTest通过。发布包在PATH仅含Windows目录、未指定Qt插件路径时通过原生主窗口5页32控件、托盘9控件、静默读取和透明窗口边缘检查；独立只读集成通过，设置前后一致。后台启动、重复启动唤起、关闭驻留、面板入口、Esc、退出和重启通过。重链接材料实际重新生成EXE，重新链接后的程序也通过原生5页32控件检查。没有运行设置写入测试或切换性能档位。

体积：原0.5.4主程序加Qt库/平台插件共29,905,944字节；新静态EXE为17,596,928字节，减少41.2%。比较不包含两版共同需要的原厂组件、文档及新增重链接材料，不等同于ZIP体积下降比例，也不代表运行速度或内存实测改善。

记录：research/static-qt-build-final.log、research/static-v060-build.log、research/static-v060-integration.json、research/static-v060-lifecycle.json、research/static-v060-relinked-ui.json、research/static-v060-relink.log、docs/static-build-v060.json、docs/tray-verification-v060.json和docs/ui-verification-v060.json。可运行scripts/build.ps1 -Package及scripts/verify-smooth.ps1复现。发布包保留Qt许可证、SPDX SBOM、应用对象和重链接脚本。

## 0.5.4 性能与极客配色微调

根据用户反馈，性能档从橙色微调为更偏黄、饱和度略低的琥珀色，填充渐变由#ffbc54→#ef8328改为#f5ce70→#dda644；Ultra略微加深红色，填充渐变由#ff9d2c→#f13224改为#f28d32→#dc2d2c。对应文字色同步微调为#946520及#b52a26。保留其余两档配色、230ms颜色过渡、粒子、能力判断和设备设置行为。

Release构建与两项CTest通过，UI测试记录8项零失败；四档控件渲染已检查。原生窗口及托盘验证和只读集成记录见research/palette-v054-build.log、research/palette-v054-ui-tests.xml、research/smooth-v054-completion.json、research/smooth-v054-integration.json、docs/tray-verification-v054.json、docs/ui-verification-v054.json及docs/screenshots-v054；scripts/verify-smooth.ps1默认验证0.5.4。

## 0.5.3 性能滑块冷暖配色

根据用户要求，性能滑块由节能的蓝色递进到极客模式的火红色：节能蓝、自适应青、性能橙、Ultra火红。四档填充末端色分别为#2867d8、#168e9b、#ef8328、#f13224，Ultra从橙色渐变到火红色；标题及选中档位采用同色系较深颜色，手柄描边、轨道底色与键盘焦点同步配色。颜色以230ms缓动切换，隐藏时停止，禁用状态使用灰色；Ultra粒子采用暖白和金色。主窗口及托盘共用绘制代码。

保持原有1/0/3/4固件映射、释放后提交、接电校验、静默增量更新与隐藏后暂停粒子的行为，没有更改硬件策略或为了预览切换实际档位。四档对照图来自同一Qt控件的独立渲染。

Release构建与两项CTest通过，UI测试记录8项零失败；四档配色逐一检查。原生主窗口5页32控件、托盘9控件、静默读取、透明窗口样式及带周边桌面的截图检查通过；独立只读集成通过。

验证记录：research/palette-v053-build.log、research/palette-v053-ui-tests.xml、research/smooth-v053-completion.json、research/smooth-v053-integration.json、docs/tray-verification-v053.json、docs/ui-verification-v053.json与docs/screenshots-v053。scripts/verify-smooth.ps1默认验证0.5.3。

## 0.5.2 托盘外侧矩形投影

根据用户实际桌面截图修正右侧、底部的直角细线。QuickPanel创建时增加Qt::NoDropShadowWindowHint，关闭系统弹出窗口投影，保留现有透明背景及软件绘制的圆角柔和阴影。没有修改性能档位、设备设置或面板内容。

原生验证新增两项样式检查：窗口类没有CS_DROPSHADOW，窗口及扩展样式没有原生边框标记；两项通过。此前panel->grab仅包含控件自身，无法发现系统绘制在窗口外部的投影；现在同时使用QScreen::grabWindow截取弹窗及周边桌面，检查tray-desktop.png确认外侧直角线消失。

Release构建与两项CTest通过；托盘9控件、静默读取及关闭驻留检查通过；主窗口5页32控件和独立只读集成通过。没有执行写入或切换Ultra。记录：research/tray-v052-build.log、research/smooth-v052-completion.json、research/smooth-v052-integration.json、docs/tray-verification-v052.json、docs/ui-verification-v052.json、docs/screenshots-v052/tray-desktop.png。scripts/verify-smooth.ps1默认验证0.5.2。

## 0.5.1 性能控件修正

根据用户纠正，移除从参考图沿用的闪电装饰、重置按钮及恢复操作。性能控件改为紧凑布局：上方当前档位与说明、中间滑动轨道、下方节能/自适应/性能/Ultra四个文字标记；主窗口与托盘同步使用。当前档位、轨道、滑块、渐变和粒子全部由Qt QPainter在运行时绘制；性能控件不加载截图、图标或贴图。保留Ultra粒子、释放后提交、接电校验及增量刷新。

验证：Release与两项CTest通过；现有UI测试已按移除重置后的键盘行为更新，8个测试记录零失败。原生主窗口5页32控件、托盘9控件及静默读取检查通过，实际截图已逐一检查；独立只读集成通过，没有改变性能模式或硬件设置。Ultra预览来自独立控件渲染。

记录：research/mode-v051-build.log、research/mode-v051-ui-tests.xml、research/smooth-v051-completion.json、research/smooth-v051-integration.json、docs/ui-verification-v051.json、docs/tray-verification-v051.json与docs/screenshots-v051。scripts/verify-smooth.ps1默认验证0.5.1。

## 0.5.0 性能滑块与平滑更新

用户要求参照所附Codex截图采用性能滑块、极客档对应Ultra且有特殊粒子效果，并改善界面刷新闪烁。图片为视觉参考，没有将模型名称或额外推理档位作为硬件功能需求。

性能滑块按节能→自适应→性能→Ultra排列，依次映射原有固件值1/0/3/4。仅显示本机声明支持的档位；松开拖动或结束键盘调整后才提交。Ultra保持接电校验，恢复按钮选择自适应。控制在主窗口与托盘中共用实现；蓝紫渐变、210ms滑块缓动、最多25个粒子和约30fps局部重绘，可见时才运行，隐藏/最小化暂停；遵循Windows客户端界面动画设置。

移除了后台读取时整页禁用/恢复的状态切换、周期性清空下拉框、托盘QGraphicsDropShadowEffect的整块离屏合成，以及无变化时的动画重启。控件按状态差异更新，下拉菜单打开时保留菜单，拖动和输入防抖期间保留编辑值。更换为受双缓冲支持的局部绘制阴影与短动画；没有通过重建整个界面或定时隐藏/显示实现刷新。

读取与写入状态分开，用户在读取期间的设置请求按顺序排队，写入仍由独立worker重新检查能力并回读。只有等待中的对应控件锁定；回读结果先同步，再静默补读关联设置。没有修改固件性能策略或默认开启Ultra。

验证：Release、协议测试及新增ui_updates测试通过。控件测试覆盖非连续档位映射、只在释放后提交、键盘/恢复、自适应及接电边界、100次相同快照不重启动画、隐藏后暂停粒子、100次相同快照不重建下拉模型、展开菜单保持及拖动值保留。主窗口5页32控件与托盘9控件检查通过，后台读取时控件保持可用。原生排队测试在读取中提交音量32→33，回读后恢复32，成功；独立集成检查报告32项可用，前后设置一致。

Ultra图片与GIF来自独立Qt控件渲染，未切换电脑到极客模式。此次没有逐档施加性能负载或对功耗、温度、帧率做硬件测试，也没有把截图或动画预览视为性能实测。闪烁修复通过控件状态、模型变化、编辑保持和原生截图验证，没有对显示器输出进行逐帧物理测量。

托盘生命周期回归通过：后台启动、重复启动唤起原实例、关闭到托盘、打开快捷面板、Esc收起、显式退出及退出后重启；仅控制测试创建的实例，不改变硬件设置。

记录：research/smooth-v050-build.log、research/smooth-v050-ui-tests.xml、research/smooth-v050-integration.json、research/smooth-v050-lifecycle.json、docs/tray-verification-v050.json、docs/ui-verification-v050.json与docs/screenshots-v050；可用scripts/verify-smooth.ps1复现界面与回读检查。

## 0.4.0 托盘与快捷面板

用户要求增加类似Vantage的托盘，点击提供信息速览、快捷调整及软件入口。新增绿色T图标、单击快捷面板、双击主窗口、右键刷新/管理员模式/退出，关闭主窗口保留托盘。快捷面板含9项设置，与主窗口共享worker和真实状态回读。按Esc或点击外部收起；按当前屏幕工作区定位和限制尺寸。

使用用户/会话隔离的QLockFile与QLocalServer维持单实例。Windows下的IPC仅允许同用户连接，接受固定的show/panel/noop/quit命令；不接受控制ID、数值或任意命令执行。管理员启动等待旧实例退出后接管；硬件worker与独立界面验证不争抢GUI实例锁。

当前操作及其状态刷新结束后再退出，避免由界面退出直接销毁正在写入的worker。隐藏时不反复全量探测；托盘工具提示30秒轻量读取供电与电量，面板可见时10秒刷新，拖动滑条期间暂缓面板轮询。

依赖：增加动态Qt6Network用于本地IPC，不产生互联网请求。接口参考 [Qt托盘文档](https://doc.qt.io/qt-6/qsystemtrayicon.html) 与 [Qt本地服务文档](https://doc.qt.io/qt-6/qlocalserver.html)。没有创建系统服务或开机启动项。

| 验证 | 结果 |
|---|---|
| Release / CTest | 通过，包含新增硬件音量最近档位、非法分贝范围及未知值的确认测试 |
| 托盘与面板 | 原生Windows会话检查通过：图标可见状态、9控件值及禁用状态、面板工作区定位、主窗口入口、关闭后驻留 |
| 主窗口回归 | 5页32控件、折叠、公英制切换与截图检查通过 |
| 生命周期 | 后台启动、重复启动唤起原实例、关闭到托盘、打开面板、Esc收起、显式退出、重新启动通过 |
| 管理员交接 | 普通实例退出释放锁，提升后的实例接管，普通权限客户端可打开和退出同用户管理员实例，通过 |
| 快捷调节闭环 | 通过真实面板音量滑条请求51→52，设备回读53，两个界面同步，然后恢复51；记录包含请求、回读序列及恢复结果 |
| 只读集成 | 32项可用，检查前后设置一致 |

本机音频端点报告hardwareVolume=true、0.5dB硬件步长。独立于UI的worker测试也复现52%请求对应53%实际回读。按 [Microsoft GetVolumeRange 文档](https://learn.microsoft.com/en-us/windows/win32/api/endpointvolume/nf-endpointvolume-iaudioendpointvolume-getvolumerange) 核对后，仅在硬件能力有效、实际分贝位于报告档位、且误差不超过半个硬件步长时确认最近档位，返回实际百分比。没有将音量的取整处理扩展到其他控制。

记录：docs/tray-verification-v040.json、docs/ui-verification-v040.json、research/tray-v040-lifecycle.json、research/tray-v040-handoff.json、research/tray-v040-integration.json。截图位于docs/screenshots-v040。Windows将托盘图标折叠到溢出区时，Shell图标位置查询可能为空，面板使用鼠标所在屏幕定位回退。未修改Windows的图标可见性策略。

## 0.3.1 界面重写

按用户要求重写原生Qt Widgets界面，将相关设置放入同一面板。导航由6页调整为5页，显示和人体感知合并；电池容量、容量比、循环及温度集中展示，充电策略与USB供电分别成组；背光与亮度记忆、键位与OSD提示、触控板分别成组。超频参数、感知距离和锁定参数默认收起，原始诊断JSON也默认收起。

保留32项设备控制、各选项数值、能力判断和硬件回读。新增统一分段选择、开关和下拉箭头，减少重复说明及外链面板。定时刷新仅在选项或单位改变时重建分段按钮，避免反复丢失键盘焦点。

验证范围：Release构建与CTest通过；新版UI检查通过（5页32控件），覆盖选中/禁用状态、高级设置展开/收起和公英制切换；检查1140×820与960×660逻辑像素下的实际窗口截图及展开后的完整页面。管理员只读集成检查报告32项可用，前后设置值一致，没有启用同值写入或临时切换测试。

记录：research/ui-v031-build.log、research/ui-v031-integration.json、docs/ui-verification-v031.json、docs/screenshots-v031。可用 scripts/verify-ui.ps1 复现。此次修改范围为界面与发布版本，不将上一版的硬件场景验证边界扩大。

## 0.3.0 输入与人体存在检测验证

在原有电源页基础上实现用户新给出的两张参考图。没有将图片中的开关默认值自动写入电脑。新增14项设置：背光记忆、Caps/Num OSD、Fn/Ctrl互换、触控板开关，以及9项人体感知控制。

| 检查 | 结果与范围 |
|---|---|
| Release 与 CTest | 通过；新增人体感知距离映射及各新ID的合法/非法范围测试 |
| 管理员只读探测 | 32项设置均可读取；Fn/Ctrl状态0、背光记忆1、Caps/Num OSD均1、触控板1 |
| 人体感知 | Smart Sense capability=2047、sensorType=2（VL53L5）；靠近唤醒1、手动远档2；离开锁定0、锁定自动距离1、锁定速度0、相机辅助0、视频暂停恢复0；Windows Hello已注册 |
| 距离适配 | 本机唤醒0.55/1.0/1.5米，锁定0.9/1.2/1.5米，与原厂前端按传感器类型的映射一致 |
| 新功能同值写入 | 背光、背光记忆、Caps/Num OSD、触控板、Fn/Ctrl、靠近唤醒、唤醒自动模式、唤醒距离、离开锁定、视频开关均回读确认 |
| 实际切换与恢复 | 背光记忆1→0→1，Caps OSD 1→0→1，Num OSD 1→0→1，触控板1→0→1；每一步回读确认 |
| 状态恢复 | 集成测试执行前后32项设置值一致 |
| 非法及禁用请求 | 非法ID/范围均拒绝；离开锁定关闭时，锁定自动距离、手动距离、锁定速度、相机辅助写入均拒绝 |
| UI | 6页32控件检查通过；高级设置展开/收起、公制/英制转换、当前选中与禁用状态通过，完整页面截图已检查 |
| 便携运行 | 使用便携包原生Windows插件及精简PATH验证，不依赖Qt安装目录 |

原始记录：research/v3-build.log、research/v3-probe-admin.json、research/v3-integration-admin.json；界面记录：docs/ui-verification-v3.json、docs/screenshots-v3。

本次未实际让设备进入待机后靠近唤醒，未启用离开锁定、相机辅助或视频自动暂停进行场景测试；没有实际交换Fn/Ctrl后按键验证。功能已接入、设置回读成功不能证明对应驱动在所有电源状态或播放器中均正常工作。禁用中的高级设置仅验证拒绝写入路径，待启用离开锁定后才能验证其实际设置效果。

截图、单位和默认勾选属于参考材料；本机真实状态与能力优先。相机仍由原厂服务及Windows权限管理，本程序没有请求或采集人脸图像。

复现：运行 scripts/build.ps1 -Package，然后以管理员身份运行 scripts/verify-v3-admin.ps1；加 -InputRoundtrip 才执行上述4项临时切换恢复。

## 0.2.0 既有功能的验证记录

## 环境与依赖

ThinkBook 16 G6+ IMH / 21LE，Windows 11 x64。Qt 6.12.0 msvc2022_64、MSVC 143 19.44.35230、Windows SDK 10.0.26100.0。ITS 版本8193，Fn能力26。

提取 IdeaNotebookAddin 1.0.13.114 的原厂电池组件、SmartDisplayAddin 1.0.2.623 的 GPU 插件，以及 ProductivitySystemAddin 1.0.0.138 的 GPUInfo；原文件和许可随包保留。正式应用不使用被服务拒绝的 RPC 路线。

## 本机验证

| 检查 | 结果与范围 |
|---|---|
| MSVC Release / CTest | 通过；6组协议用例，覆盖充电互斥、未知状态、背光、极客控制码、白名单与固件偏移范围/步长 |
| 管理员只读探测 | 18项控制设置均可读取；零偏移与未知状态分开 |
| 电池数据 | 当前80.55Wh、满充84.67Wh、设计85Wh、31°C/87.8°F、77次循环、LiPM、原厂Storage80支持 |
| GPU模式 | GameZone支持值3，当前0；对应混合/核显/自动 |
| GPU超频范围 | 固件支持5，ClockID0核心0–200MHz、ClockID1显存0–400MHz，步长1；基础频率1545/8001MHz |
| 同值写入回读 | 音量、亮度、GPU当前混合模式、开盖开机当前关闭、GPU超频保持关闭，全部通过 |
| 状态保存 | 上述验证前后18项设置值一致 |
| 非法请求 | 未知ID、非法充电/ITS/GPU/Flip值、负音量、超频静态越界全部被拒绝 |
| 刷新率恢复 | 保持原刷新率，模拟确认管道断开，独立进程恢复并回读通过 |
| UI | 5页18控件，超频关闭时频率滑条禁用，能力/状态一致；查看实际截图及完整电源页 |

读取数据是本机驱动和原厂组件的当前报告，不是独立物理测量。原厂GPU插件旧通用 GetAbility=0，而WMI明确支持及范围；该差异保留在诊断和实现说明中。

## 验证边界

没有启用 GPU 超频或施加非零偏移，没有切换到仅核显或自动模式，没有实际开盖开机/关机测试。极客、充电方式、Fn、USB、背光等未逐档切换。未在其他机型、其他BIOS或干净Windows安装上验证。不能把同值写入视为完整硬件范围验证。

## 复现

```powershell
.\scripts\build.ps1 -Package
python tests/integration.py dist/ThinkVantage-0.2.0-win64/ThinkVantage.exe --report research/integration-readonly.json
```

管理员下可加 --same-value-writes 验证上述同值设置。scripts/verify-v2-admin.ps1 用于开发环境中的界面与本机集成验证。

原始构建/探测/集成日志保留在 research，截图保留在 docs/screenshots-v2，原厂抽取文件哈希见 vendor/manifest.json。原厂整个安装包、反编译输出与研究工具不放入便携包。
