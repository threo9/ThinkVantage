$ErrorActionPreference='Continue'
Set-Location (Split-Path -Parent $PSScriptRoot)
$r=[ordered]@{administrator=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)}
$g=Get-CimInstance -Namespace root/wmi LENOVO_GAMEZONE_DATA
foreach($m in @('IsSupportIGPUMode','GetIGPUModeStatus','IsSupportGpuOC','IsACFitForOC')){try{$r[$m]=Invoke-CimMethod -InputObject $g -MethodName $m -ErrorAction Stop | Select-Object Data,ReturnValue}catch{$r[$m]=$_.Exception.Message}}
$r.gpuOc=@(Get-CimInstance -Namespace root/wmi LENOVO_GAMEZONE_GPU_OC_DATA | Select-Object ClockID,OCMinOffset,OCMaxOffset,Interval)
$r | ConvertTo-Json -Depth 4 | Set-Content research/admin-probe.json -Encoding utf8
& .\vendor\idea\VendorBridge.exe probe | Set-Content research/admin-vendor-probe.json -Encoding utf8
& 'F:\Python\Python311\python.exe' research/probe_gpu.py
