param([string]$Destination=(Join-Path (Split-Path -Parent $PSScriptRoot) 'vendor'))
$ErrorActionPreference='Stop'
$ideaSource='C:\ProgramData\Lenovo\Vantage\Addins\IdeaNotebookAddin\1.0.13.114'
$gpuSource='C:\ProgramData\Lenovo\Vantage\Addins\SmartDisplayAddin\1.0.2.623\x64'
$idea=Join-Path $Destination 'idea'
$gpu=Join-Path $Destination 'gpu'
New-Item -ItemType Directory -Force -Path $idea,$gpu | Out-Null
$files=@('IdeaPowerAgent.dll','IdeaBatteryAgent.dll','PowerBattery.dll','PowerContract.dll','PowerProfileContract.dll','BatteryInformationContract.dll','BatteryManagementContract.dll','Lenovo.VantageService.Utilities.dll','Lenovo.VantageService.UtilitiesExtensions.dll','Lenovo.Vantage.RpcClient.dll','Lenovo.Vantage.RpcCommon.dll','Lenovo.Vantage.AddinInterface.dll','Newtonsoft.Json.dll','System.Runtime.WindowsRuntime.dll','msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll','concrt140.dll','iTin.Core.dll','iTin.Core.Hardware.dll','iTin.Core.Hardware.Specification.Dmi.dll','iTin.Core.Hardware.Specification.Smbios.dll','iTin.Core.Hardware.Specification.Tpm.dll','License.pdf','ThirdPartyNotices.md')
foreach($file in $files){Copy-Item -LiteralPath (Join-Path $ideaSource $file) -Destination $idea -Force}
New-Item -ItemType Directory -Force -Path (Join-Path $idea 'x64') | Out-Null
Copy-Item -LiteralPath (Join-Path $ideaSource 'x64\VantageRpcClient.dll') -Destination (Join-Path $idea 'x64') -Force
Copy-Item -LiteralPath (Join-Path $gpuSource 'SEGPUOCPlugin.dll') -Destination $gpu -Force
Copy-Item -LiteralPath 'C:\ProgramData\Lenovo\Vantage\Addins\LenovoProductivitySystemAddin\1.0.0.138\GPUInfo.dll' -Destination $gpu -Force
Copy-Item -LiteralPath (Join-Path $gpuSource 'gpuoc') -Destination $gpu -Recurse -Force
Copy-Item -LiteralPath (Join-Path (Split-Path -Parent $gpuSource) 'License.pdf'),(Join-Path (Split-Path -Parent $gpuSource) 'ThirdPartyNotice.md') -Destination $gpu -Force
$presenceSource='C:\ProgramData\Lenovo\Vantage\Addins\SmartInteractAddin\1.0.9.3'
$presence=Join-Path $Destination 'presence'
New-Item -ItemType Directory -Force -Path $presence | Out-Null
Copy-Item -LiteralPath (Join-Path $presenceSource 'x64\SmartSenseRpcClient.dll'),(Join-Path $presenceSource 'ThirdPartyNotice.md') -Destination $presence -Force
$csc=Join-Path $env:SystemRoot 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
& $csc /nologo /platform:x64 /optimize+ /target:exe /r:System.Web.Extensions.dll "/out:$idea\VendorBridge.exe" (Join-Path (Split-Path -Parent $PSScriptRoot) 'src\vendor_bridge.cs')
if($LASTEXITCODE){throw 'Vendor bridge build failed'}
$manifest=@(Get-ChildItem -LiteralPath $Destination -File -Recurse | Where-Object {$_.Name -ne 'manifest.json'} | ForEach-Object { [ordered]@{file=[IO.Path]::GetRelativePath($Destination,$_.FullName);sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash;bytes=$_.Length} })
[ordered]@{sourceIdea=$ideaSource;sourceGpu=$gpuSource;sourcePresence=$presenceSource;extractedAt=(Get-Date).ToString('o');files=$manifest} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Destination 'manifest.json') -Encoding utf8
