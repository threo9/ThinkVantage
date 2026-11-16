param([switch]$Portable)
$ErrorActionPreference='Stop'
$OutputEncoding=New-Object System.Text.UTF8Encoding($false)
[Console]::OutputEncoding=$OutputEncoding
Set-Location (Split-Path -Parent $PSScriptRoot)
$exe='build\ThinkVantage.exe'
if($Portable){
    $exe='dist\ThinkVantage-0.2.0-win64\ThinkVantage.exe'
    $env:PATH="$env:SystemRoot\System32;$env:SystemRoot"
    Remove-Item Env:QT_QPA_PLATFORM_PLUGIN_PATH -ErrorAction SilentlyContinue
}else{
    $env:PATH='E:\Qt6\6.12.0\msvc2022_64\bin;'+$env:PATH
    $env:QT_QPA_PLATFORM_PLUGIN_PATH='E:\Qt6\6.12.0\msvc2022_64\plugins\platforms'
}
& $exe --probe | Set-Content -Encoding utf8 research/v2-probe-admin.json
$p=Start-Process -FilePath $exe -ArgumentList '--ui-smoke','--screenshot','E:\Code\ThinkVantage\docs\screenshots-v2\power.png' -PassThru -WindowStyle Hidden
if(-not $p.WaitForExit(30000)){throw 'UI validation timed out'}
if($p.ExitCode){throw "UI validation failed: $($p.ExitCode)"}
& 'F:\Python\Python311\python.exe' tests/integration.py $exe --same-value-writes --report research/v2-integration-admin.json
if($LASTEXITCODE){throw 'Native integration validation failed'}
Copy-Item -LiteralPath (Join-Path (Split-Path -Parent $exe) 'ui-smoke.json') -Destination docs/ui-verification-v2.json -Force
