param([switch]$VerifyAdjustment,[string]$Executable,[string]$OutputTag='v060')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
Set-Location $projectRoot
$OutputEncoding=New-Object System.Text.UTF8Encoding($false)
[Console]::OutputEncoding=$OutputEncoding
$env:PATH="$env:SystemRoot\System32;$env:SystemRoot"
Remove-Item Env:QT_QPA_PLATFORM_PLUGIN_PATH -ErrorAction SilentlyContinue
Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
if(-not $Executable){$Executable=Join-Path $projectRoot 'dist\ThinkVantage-0.6.0-win64-static\ThinkVantage.exe'}
$exe=(Resolve-Path -LiteralPath $Executable).Path
$packageDir=Split-Path -Parent $exe
$images=Join-Path $projectRoot "docs\screenshots-$OutputTag"
New-Item -ItemType Directory -Path $images -Force | Out-Null
$prefix="research/static-$OutputTag"
try {
    $arguments=@('--tray-smoke','--screenshot',(Join-Path $images 'tray.png'))
    if($VerifyAdjustment){$arguments+='--verify-adjustment'}
    $p=Start-Process -FilePath $exe -ArgumentList $arguments -PassThru -WindowStyle Hidden -RedirectStandardError "$prefix-tray-stderr.log"
    if(-not $p.WaitForExit(55000)){$p.Kill();throw 'Tray verification timed out'}
    if($p.ExitCode){throw "Tray verification failed: $($p.ExitCode)"}
    Copy-Item -LiteralPath (Join-Path $packageDir 'tray-smoke.json') -Destination "docs/tray-verification-$OutputTag.json" -Force
    $p=Start-Process -FilePath $exe -ArgumentList '--ui-smoke','--screenshot',(Join-Path $images 'app.png') -PassThru -WindowStyle Hidden -RedirectStandardError "$prefix-ui-stderr.log"
    if(-not $p.WaitForExit(40000)){$p.Kill();throw 'Main UI verification timed out'}
    if($p.ExitCode){throw "Main UI verification failed: $($p.ExitCode)"}
    Copy-Item -LiteralPath (Join-Path $packageDir 'ui-smoke.json') -Destination "docs/ui-verification-$OutputTag.json" -Force
    & 'F:\Python\Python311\python.exe' tests/integration.py $exe --report "$prefix-integration.json" *> "$prefix-integration.log"
    if($LASTEXITCODE){throw 'Read-only integration failed'}
    @{ok=$true;completed=(Get-Date).ToString('o')} | ConvertTo-Json | Set-Content "$prefix-completion.json"
} catch {
    @{ok=$false;error=$_.Exception.Message;completed=(Get-Date).ToString('o')} | ConvertTo-Json | Set-Content "$prefix-completion.json"
    exit 1
}
