param([switch]$VerifyAdjustment,[string]$Executable,[string]$OutputTag,[string]$Python)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
Set-Location $projectRoot
$OutputEncoding=New-Object System.Text.UTF8Encoding($false)
[Console]::OutputEncoding=$OutputEncoding
$versionMatch=Select-String -LiteralPath (Join-Path $projectRoot 'CMakeLists.txt') -Pattern 'project\(ThinkVantage VERSION ([0-9.]+)'
if(-not $versionMatch){throw 'Cannot determine current project version'}
$expectedVersion=$versionMatch.Matches.Groups[1].Value
if(-not $OutputTag){$OutputTag='v'+$expectedVersion.Replace('.','')}
if($OutputTag -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$'){throw 'OutputTag must be a simple filename tag'}
if(-not $Executable){$Executable=Join-Path $projectRoot "dist\ThinkVantage-$expectedVersion-win64-static\ThinkVantage.exe"}
$images=Join-Path $projectRoot "docs\screenshots-$OutputTag"
$research=Join-Path $projectRoot 'research'
New-Item -ItemType Directory -Path $images,$research -Force | Out-Null
$prefix=Join-Path $research "static-$OutputTag"
$identity=[ordered]@{expectedVersion=$expectedVersion;executable=$Executable;sourceCommit=$null;sourceDirty=$null;executableSourceCommit=$null;verifyAdjustment=[bool]$VerifyAdjustment}
$originalPath=$env:PATH
$originalPluginPath=$env:QT_QPA_PLATFORM_PLUGIN_PATH
$originalPlatform=$env:QT_QPA_PLATFORM
try {
    $git=Get-Command git.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if($git){
        $commit=& $git.Source -C $projectRoot rev-parse HEAD
        if($LASTEXITCODE -eq 0){$identity.sourceCommit="$commit".Trim();$dirty=& $git.Source -C $projectRoot status --porcelain;$identity.sourceDirty=[bool]$dirty}
    }
    $exe=(Resolve-Path -LiteralPath $Executable).Path
    $packageDir=Split-Path -Parent $exe
    $identity.executable=$exe
    $fileVersion=[Diagnostics.FileVersionInfo]::GetVersionInfo($exe).FileVersion
    $identity.version=$fileVersion
    $identity.sha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    if($fileVersion -ne $expectedVersion){throw "Executable version $fileVersion does not match project $expectedVersion ($exe)"}
    $metadataPath=Join-Path $packageDir 'static-build.json'
    if(Test-Path -LiteralPath $metadataPath){
        $metadata=Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
        if($metadata.version -ne $expectedVersion -or $metadata.sha256 -ne $identity.sha256){throw 'Executable does not match static-build.json'}
        if($metadata.PSObject.Properties.Name -contains 'sourceCommit'){$identity.executableSourceCommit=$metadata.sourceCommit}
        if($metadata.PSObject.Properties.Name -contains 'sourceDirty'){$identity.executableSourceDirty=$metadata.sourceDirty}
    }
    # Resolve tools before deliberately restricting the tested app's PATH.

    $pythonCommand=$null
    $pythonArguments=@()
    if($Python){$pythonCommand=Get-Command $Python -CommandType Application -ErrorAction Stop | Select-Object -First 1}
    else {
        foreach($candidate in @('python.exe','python3.exe','py.exe')){
            $pythonCommand=Get-Command $candidate -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
            if($pythonCommand){if($candidate -eq 'py.exe'){$pythonArguments=@('-3')};break}
        }
    }
    if(-not $pythonCommand){throw 'Python 3 is required; use -Python to specify its executable'}
    $pythonPath=& $pythonCommand.Source @pythonArguments -c 'import sys; assert sys.version_info.major == 3; print(sys.executable)'
    if($LASTEXITCODE -ne 0 -or -not $pythonPath){throw 'Unable to resolve a Python 3 interpreter'}
    $pythonPath=(Resolve-Path -LiteralPath "$pythonPath".Trim()).Path
    $identity.python=$pythonPath
    $env:PATH="$env:SystemRoot\System32;$env:SystemRoot"
    Remove-Item Env:QT_QPA_PLATFORM_PLUGIN_PATH -ErrorAction SilentlyContinue
    Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
    $trayReport=Join-Path $packageDir 'tray-smoke.json'
    $uiReport=Join-Path $packageDir 'ui-smoke.json'
    if(Test-Path -LiteralPath $trayReport){Remove-Item -LiteralPath $trayReport -Force}
    $arguments=@('--tray-smoke','--screenshot',('"'+(Join-Path $images 'tray.png')+'"'))
    if($VerifyAdjustment){$arguments+='--verify-adjustment'}
    $p=Start-Process -FilePath $exe -ArgumentList $arguments -PassThru -WindowStyle Hidden -RedirectStandardError "$prefix-tray-stderr.log"
    if(-not $p.WaitForExit(55000)){$p.Kill();throw 'Tray verification timed out'}
    if($p.ExitCode){throw "Tray verification failed: $($p.ExitCode)"}
    Copy-Item -LiteralPath $trayReport -Destination "docs/tray-verification-$OutputTag.json" -Force
    if(Test-Path -LiteralPath $uiReport){Remove-Item -LiteralPath $uiReport -Force}
    $p=Start-Process -FilePath $exe -ArgumentList '--ui-smoke','--screenshot',('"'+(Join-Path $images 'app.png')+'"') -PassThru -WindowStyle Hidden -RedirectStandardError "$prefix-ui-stderr.log"
    if(-not $p.WaitForExit(40000)){$p.Kill();throw 'Main UI verification timed out'}
    if($p.ExitCode){throw "Main UI verification failed: $($p.ExitCode)"}
    Copy-Item -LiteralPath $uiReport -Destination "docs/ui-verification-$OutputTag.json" -Force
    & $pythonPath tests/integration.py $exe --report "$prefix-integration.json" *> "$prefix-integration.log"
    if($LASTEXITCODE){throw 'Read-only integration failed'}
    if((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ne $identity.sha256){throw 'Executable changed while verification was running'}
    $identity.ok=$true;$identity.completed=(Get-Date).ToUniversalTime().ToString('o')
    $identity | ConvertTo-Json -Depth 5 | Set-Content "$prefix-completion.json"
} catch {
    $identity.ok=$false;$identity.error=$_.Exception.Message;$identity.completed=(Get-Date).ToUniversalTime().ToString('o')
    $identity | ConvertTo-Json -Depth 5 | Set-Content "$prefix-completion.json"
    throw
} finally {
    $env:PATH=$originalPath
    $env:QT_QPA_PLATFORM_PLUGIN_PATH=$originalPluginPath
    $env:QT_QPA_PLATFORM=$originalPlatform
}
