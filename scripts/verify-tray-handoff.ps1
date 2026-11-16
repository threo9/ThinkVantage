$ErrorActionPreference='Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)
$exe='E:\Code\ThinkVantage\dist\ThinkVantage-0.4.0-win64\ThinkVantage.exe'
$normal=$null
$elevated=$null
function Client([string]$Command){
    $p=Start-Process -FilePath $exe -ArgumentList $Command -PassThru -WindowStyle Hidden
    if(-not $p.WaitForExit(12000)){$p.Kill();throw "IPC client timed out: $Command"}
    if($p.ExitCode){throw "IPC client failed: $Command ($($p.ExitCode))"}
}
try {
    $normal=Start-Process -FilePath $exe -ArgumentList '--tray' -PassThru -WindowStyle Hidden
    Start-Sleep -Milliseconds 1500
    if($normal.HasExited){throw 'Normal primary exited unexpectedly'}
    $elevated=Start-Process -FilePath $exe -Verb RunAs -ArgumentList '--elevated-handoff','--tray' -PassThru -WindowStyle Hidden
    Client '--quit'
    if(-not $normal.WaitForExit(10000)){throw 'Normal primary did not release instance lock'}
    Start-Sleep -Milliseconds 1500
    if($elevated.HasExited){throw 'Elevated instance did not take over'}
    Client '--quick'
    Client '--quit'
    if(-not $elevated.WaitForExit(10000)){throw 'Elevated instance did not exit'}
    @{ok=$true;normalReleased=$true;elevatedTookOver=$true;standardUserOpenedElevatedPanel=$true;standardUserQuitElevatedInstance=$true} | ConvertTo-Json | Set-Content research/tray-v040-handoff.json
} catch {
    @{ok=$false;error=$_.Exception.Message} | ConvertTo-Json | Set-Content research/tray-v040-handoff.json
    throw
} finally {
    if(($normal -and -not $normal.HasExited) -or ($elevated -and -not $elevated.HasExited)){
        $cleanup=Start-Process -FilePath $exe -ArgumentList '--quit' -PassThru -WindowStyle Hidden
        $null=$cleanup.WaitForExit(12000)
    }
}
