param([string]$QtRoot='E:\Qt6',[string]$QtPrefix,[switch]$Package,[int]$Jobs=4,[string[]]$CMakeArgs=@())
. (Join-Path $PSScriptRoot 'toolchain.ps1') -QtRoot $QtRoot
if(-not $QtPrefix){$QtPrefix=Join-Path $projectRoot 'build\qt\static'}
$QtPrefix=[IO.Path]::GetFullPath($QtPrefix)
if(-not (Test-Path -LiteralPath (Join-Path $QtPrefix 'lib\cmake\Qt6\Qt6Config.cmake'))){
    & (Join-Path $PSScriptRoot 'build-qt-static.ps1') -QtRoot $QtRoot -Jobs $Jobs
    if(-not (Test-Path -LiteralPath (Join-Path $QtPrefix 'lib\cmake\Qt6\Qt6Config.cmake'))){throw 'Static Qt prefix not found'}
}
& (Join-Path $PSScriptRoot 'extract-vendor.ps1')
$buildDir=Join-Path $projectRoot 'build\static-release'
$configure=@('-S',$projectRoot,'-B',$buildDir,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_TRY_COMPILE_CONFIGURATION=Release',"-DCMAKE_PREFIX_PATH=$QtPrefix","-DCMAKE_CXX_COMPILER=$compilerPath","-DCMAKE_RC_COMPILER=$rcPath","-DCMAKE_MT=$mtPath",'-DCMAKE_NINJA_FORCE_RESPONSE_FILE=ON','-DBUILD_TESTING=ON')
& $cmake @configure @CMakeArgs
if($LASTEXITCODE){throw 'CMake configure failed'}
& $cmake --build $buildDir --parallel $Jobs -- -d keeprsp
if($LASTEXITCODE){throw 'Build failed'}
& $ctest --test-dir $buildDir --output-on-failure
if($LASTEXITCODE){throw 'Tests failed'}
$exe=Join-Path $buildDir 'ThinkVantage.exe'
$imports=@(& (Join-Path $bin 'link.exe') /dump /dependents $exe | ForEach-Object {if($_ -match '^\s+(\S+\.dll)\s*$'){$Matches[1]}})
if($LASTEXITCODE){throw 'PE import verification failed'}
if(-not $imports.Count){throw 'PE import list was empty'}
if($imports | Where-Object {$_ -match '^(Qt[56]|msvcp|vcruntime|concrt|ucrtbase|api-ms-win-crt)'}){throw 'Unexpected dynamic Qt or C++ runtime dependency'}
$version=(Select-String -LiteralPath (Join-Path $projectRoot 'CMakeLists.txt') -Pattern 'project\(ThinkVantage VERSION ([0-9.]+)').Matches.Groups[1].Value
$qtManifest=Join-Path $QtPrefix 'thinkvantage-build.json'
$qtMetadata=if(Test-Path -LiteralPath $qtManifest){Get-Content -Raw -LiteralPath $qtManifest | ConvertFrom-Json}else{@{prefix=$QtPrefix;external=$true}}
$report=[ordered]@{version=$version;linkage='static';runtime='MT';imports=$imports;executableBytes=(Get-Item -LiteralPath $exe).Length;sha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash;qt=$qtMetadata}
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $buildDir 'static-build.json')
if($Package){
    $distRoot=[IO.Path]::GetFullPath((Join-Path $projectRoot 'dist'))
    $destination=[IO.Path]::GetFullPath((Join-Path $distRoot "ThinkVantage-$version-win64-static"))
    if(-not $destination.StartsWith($distRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe package destination'}
    if(Test-Path -LiteralPath $destination){
        $previous=Join-Path $destination 'ThinkVantage.exe'
        if(Test-Path -LiteralPath $previous){$check=[IO.File]::Open($previous,'Open','ReadWrite','None');$check.Dispose()}
        Remove-Item -LiteralPath $destination -Recurse -Force
    }
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    Copy-Item -LiteralPath $exe,(Join-Path $buildDir 'static-build.json'),(Join-Path $projectRoot 'README.md'),(Join-Path $projectRoot 'THIRD_PARTY_NOTICES.md'),(Join-Path $projectRoot 'docs\VALIDATION.md'),(Join-Path $projectRoot 'docs\IMPLEMENTATION_EVIDENCE.md'),(Join-Path $projectRoot 'docs\FEATURE_COMPLETION_0.7.0.md') -Destination $destination
    Copy-Item -LiteralPath (Join-Path $projectRoot 'licenses'),(Join-Path $projectRoot 'vendor') -Destination $destination -Recurse
    # GPUInfo is proprietary and still imports the dynamic VC runtime. Keep
    # these dependencies beside that DLL; the main executable uses /MT.
    foreach($runtime in @('msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll')){
        $source=Join-Path $env:SystemRoot "System32\$runtime"
        if(Test-Path -LiteralPath $source){Copy-Item -LiteralPath $source -Destination (Join-Path $destination 'vendor\gpu') -Force}
    }
    $qtLicenses=Join-Path $projectRoot 'build\qt\qtbase-everywhere-src-6.12.0\LICENSES'
    if(Test-Path -LiteralPath $qtLicenses){Copy-Item -LiteralPath $qtLicenses -Destination (Join-Path $destination 'licenses\Qt-6.12.0') -Recurse}
    $qtSbom=Join-Path $QtPrefix 'sbom\qtbase-6.12.0.spdx'
    if(Test-Path -LiteralPath $qtSbom){Copy-Item -LiteralPath $qtSbom -Destination $destination}
    # Preserve application objects for relinking with a compatible modified Qt.
    $relink=Join-Path $destination 'relink'
    $objectRoot=Join-Path $buildDir 'CMakeFiles\ThinkVantage.dir'
    foreach($object in Get-ChildItem -LiteralPath $objectRoot -Recurse -File | Where-Object {$_.Extension -in @('.obj','.res')}){
        $target=Join-Path $relink ([IO.Path]::GetRelativePath($buildDir,$object.FullName))
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Copy-Item -LiteralPath $object.FullName -Destination $target
    }
    Copy-Item -LiteralPath (Join-Path $buildDir 'protocol.lib') -Destination $relink
    Copy-Item -LiteralPath (Join-Path $buildDir 'CMakeFiles\ThinkVantage.rsp') -Destination (Join-Path $relink 'original-link.rsp')
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'relink.ps1') -Destination $relink
    New-Item -ItemType Directory -Path (Join-Path $relink 'scripts') -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'toolchain.ps1') -Destination (Join-Path $relink 'scripts')
    [ordered]@{qtPrefix=$QtPrefix;qtRoot=[IO.Path]::GetFullPath($QtRoot)} | ConvertTo-Json | Set-Content (Join-Path $relink 'relink.json')
    Compress-Archive -LiteralPath $destination -DestinationPath "$destination.zip" -Force
}
