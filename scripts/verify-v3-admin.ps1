param([switch]$InputRoundtrip)
$ErrorActionPreference='Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)
$OutputEncoding=New-Object System.Text.UTF8Encoding($false)
[Console]::OutputEncoding=$OutputEncoding
$env:PATH="$env:SystemRoot\System32;$env:SystemRoot"
Remove-Item Env:QT_QPA_PLATFORM_PLUGIN_PATH -ErrorAction SilentlyContinue
$exe='E:\Code\ThinkVantage\dist\ThinkVantage-0.3.0-win64\ThinkVantage.exe'
try {
    & $exe --probe | Set-Content -Encoding utf8 research/v3-probe-admin.json
    Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
    $p=Start-Process -FilePath $exe -ArgumentList '--ui-smoke','--screenshot','E:\Code\ThinkVantage\docs\screenshots-v3\power.png' -PassThru -WindowStyle Hidden -RedirectStandardError 'research/v3-ui-stderr.log'
    if(-not $p.WaitForExit(45000)){$p.Kill();throw 'UI validation timed out'}
    if($p.ExitCode){throw "UI validation failed: $($p.ExitCode)"}
    Copy-Item -LiteralPath 'dist/ThinkVantage-0.3.0-win64/ui-smoke.json' -Destination docs/ui-verification-v3.json -Force
    $arguments=@('tests/integration.py',$exe,'--same-value-writes','--report','research/v3-integration-admin.json')
    if($InputRoundtrip){$arguments+='--input-roundtrip'}
    & 'F:\Python\Python311\python.exe' @arguments *> research/v3-integration.log
    if($LASTEXITCODE){throw 'Integration validation failed; see research/v3-integration.log'}
    @{ok=$true;completed=(Get-Date).ToString('o')} | ConvertTo-Json | Set-Content research/v3-admin-completion.json
} catch {
    @{ok=$false;error=$_.Exception.Message;completed=(Get-Date).ToString('o')} | ConvertTo-Json | Set-Content research/v3-admin-completion.json
    exit 1
}
