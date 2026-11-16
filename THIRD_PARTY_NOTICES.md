# Third-party and original runtime notices

## Lenovo original components

At the user's explicit request, version 0.2.0 integrates unchanged files extracted from their local installation:

- IdeaNotebookAddin 1.0.13.114: IdeaBatteryAgent, IdeaPowerAgent, PowerBattery, managed contracts and support libraries.
- SmartDisplayAddin 1.0.2.623: SEGPUOCPlugin and its original configuration.
- LenovoProductivitySystemAddin 1.0.0.138: GPUInfo for querying applications occupying the discrete GPU.
- SmartInteractAddin 1.0.9.3: unchanged x64 SmartSenseRpcClient.dll for the separately available Human Presence Detection service API. Its supplied ThirdPartyNotice.md is preserved; this source directory contains no license PDF.

vendor/manifest.json records source paths, sizes and SHA-256 hashes. Original license PDFs and notices are retained. These components remain proprietary. Authorization for this local integration does not grant new public redistribution rights or change Lenovo's licenses.

Original wrappers and frontend source maps were inspected to establish signatures, enums, units, capabilities and ranges. Binaries were not patched. The RPC signed-client restriction was observed and not altered; separately available WMI, device, SCM and DLL interfaces are used instead.

Lenovo and Vantage are Lenovo trademarks. This is an independent project.

## Qt and Microsoft runtimes

Qt 6.12.0 Core, Gui, Widgets and Network are dynamically linked. Applicable LGPL/GPL/exception texts are retained under licenses; Qt DLLs remain replaceable. Corresponding source is available from [Qt archives](https://download.qt.io/archive/qt/) and [Qt repositories](https://code.qt.io/). Public distribution requires the corresponding source or a compliant source offer and applicable notices.

The package includes compatible Microsoft Visual C++ runtime DLLs from the user's Windows installation and the original runtimes supplied with Lenovo mixed-mode assemblies. The battery bridge targets Windows 11's existing .NET Framework. No .NET SDK is bundled.

## Open-source references

Original C++ protocols reference MIT-licensed [OpenLenovoSettings](https://github.com/dantmnf/OpenLenovoSettings/tree/3cabd36a5e3e7b50c07fccfc3be8045d3b5cb54a) and [WattFlow](https://github.com/SpookySandwich/WattFlow/tree/062858af4aa1d2698c2d4a05e7298e777ebb1191). Full notices are in licenses/OpenLenovoSettings.txt and licenses/WattFlow.txt. No GPL project source was copied into the application.
