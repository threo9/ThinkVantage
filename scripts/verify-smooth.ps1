param([switch]$VerifyAdjustment)
$ErrorActionPreference='Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)
$OutputEncoding=New-Object System.Text.UTF8Encoding($false)
[Console]::OutputEncoding=$OutputEncoding
$env:PATH="$env:SystemRoot\System32;$env:SystemRoot"
Remove-Item Env:QT_QPA_PLATFORM_PLUGIN_PATH -ErrorAction SilentlyContinue
Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
$exe='E:\Code\ThinkVantage\dist\ThinkVantage-0.5.2-win64\ThinkVantage.exe'
try {
    $arguments=@('--tray-smoke','--screenshot','E:\Code\ThinkVantage\docs\screenshots-v052\tray.png')
    if($VerifyAdjustment){$arguments+='--verify-adjustment'}
    $p=Start-Process -FilePath $exe -ArgumentList $arguments -PassThru -WindowStyle Hidden -RedirectStandardError 'research/smooth-v052-tray-stderr.log'
    if(-not $p.WaitForExit(55000)){$p.Kill();throw 'Tray verification timed out'}
    if($p.ExitCode){throw "Tray verification failed: $($p.ExitCode)"}
    Copy-Item -LiteralPath 'dist/ThinkVantage-0.5.2-win64/tray-smoke.json' -Destination docs/tray-verification-v052.json -Force
    $p=Start-Process -FilePath $exe -ArgumentList '--ui-smoke','--screenshot','E:\Code\ThinkVantage\docs\screenshots-v052\app.png' -PassThru -WindowStyle Hidden -RedirectStandardError 'research/smooth-v052-ui-stderr.log'
    if(-not $p.WaitForExit(40000)){$p.Kill();throw 'Main UI verification timed out'}
    if($p.ExitCode){throw "Main UI verification failed: $($p.ExitCode)"}
    Copy-Item -LiteralPath 'dist/ThinkVantage-0.5.2-win64/ui-smoke.json' -Destination docs/ui-verification-v052.json -Force
    & 'F:\Python\Python311\python.exe' tests/integration.py $exe --report research/smooth-v052-integration.json *> research/smooth-v052-integration.log
    if($LASTEXITCODE){throw 'Read-only integration failed'}
    @{ok=$true;completed=(Get-Date).ToString('o')} | ConvertTo-Json | Set-Content research/smooth-v052-completion.json
} catch {
    @{ok=$false;error=$_.Exception.Message;completed=(Get-Date).ToString('o')} | ConvertTo-Json | Set-Content research/smooth-v052-completion.json
    exit 1
}
