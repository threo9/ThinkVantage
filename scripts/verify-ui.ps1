$ErrorActionPreference='Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)
$OutputEncoding=New-Object System.Text.UTF8Encoding($false)
[Console]::OutputEncoding=$OutputEncoding
$env:PATH="$env:SystemRoot\System32;$env:SystemRoot"
Remove-Item Env:QT_QPA_PLATFORM_PLUGIN_PATH -ErrorAction SilentlyContinue
Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
$exe='E:\Code\ThinkVantage\dist\ThinkVantage-0.3.1-win64\ThinkVantage.exe'
try {
    $p=Start-Process -FilePath $exe -ArgumentList '--ui-smoke','--screenshot','E:\Code\ThinkVantage\docs\screenshots-v031\app.png' -PassThru -WindowStyle Hidden -RedirectStandardError 'research/ui-v031-stderr.log'
    if(-not $p.WaitForExit(45000)){$p.Kill();throw 'UI check timed out'}
    if($p.ExitCode){throw "UI check failed: $($p.ExitCode)"}
    Copy-Item -LiteralPath 'dist/ThinkVantage-0.3.1-win64/ui-smoke.json' -Destination docs/ui-verification-v031.json -Force
    & 'F:\Python\Python311\python.exe' tests/integration.py $exe --report research/ui-v031-integration.json *> research/ui-v031-integration.log
    if($LASTEXITCODE){throw 'Read-only integration failed'}
    @{ok=$true;completed=(Get-Date).ToString('o')} | ConvertTo-Json | Set-Content research/ui-v031-completion.json
} catch {
    @{ok=$false;error=$_.Exception.Message;completed=(Get-Date).ToString('o')} | ConvertTo-Json | Set-Content research/ui-v031-completion.json
    exit 1
}
