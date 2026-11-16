param([string]$QtRoot='E:\Qt6',[switch]$Package)
$ErrorActionPreference='Stop'
$env:VSLANG='1033'
if(-not (Test-Path -LiteralPath (Join-Path $QtRoot 'Tools\MSVC 143\bin\HostX64\x64\1033'))){
    $env:THINKVANTAGE_MSVC_INCLUDE_PREFIX='注意: 包含文件: '
}
$projectRoot=Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'extract-vendor.ps1')
$qt=Join-Path $QtRoot '6.12.0\msvc2022_64'
$msvc=Join-Path $QtRoot 'Tools\MSVC 143'
$sdk=Join-Path $QtRoot 'Tools\WindowsSDK\microsoft.windows.sdk.cpp\c'
$sdkLib=Join-Path $QtRoot 'Tools\WindowsSDK\microsoft.windows.sdk.cpp.x64\c'
$sdkVersion=(Get-ChildItem (Join-Path $sdk 'Include') -Directory | Sort-Object Name -Descending | Select-Object -First 1).Name
$bin=Join-Path $msvc 'bin\HostX64\x64'
$sdkBin=Join-Path $sdk "bin\$sdkVersion\x64"
$cmake=Join-Path $QtRoot 'Tools\CMake_64\bin\cmake.exe'
$env:PATH="$bin;$sdkBin;$(Join-Path $QtRoot 'Tools\Ninja');$qt\bin;$env:PATH"
$env:INCLUDE="$msvc\include;$sdk\Include\$sdkVersion\ucrt;$sdk\Include\$sdkVersion\shared;$sdk\Include\$sdkVersion\um;$sdk\Include\$sdkVersion\winrt;$sdk\Include\$sdkVersion\cppwinrt"
$env:LIB="$msvc\lib\x64;$sdkLib\ucrt\x64;$sdkLib\um\x64"
$compilerPath=(Join-Path $bin 'cl.exe').Replace('\','/')
$rcPath=(Join-Path $sdkBin 'rc.exe').Replace('\','/')
$mtPath=(Join-Path $sdkBin 'mt.exe').Replace('\','/')
& $cmake -S $projectRoot -B "$projectRoot\build" -G Ninja "-DCMAKE_BUILD_TYPE=Release" "-DCMAKE_TRY_COMPILE_CONFIGURATION=Release" "-DCMAKE_PREFIX_PATH=$qt" "-DCMAKE_CXX_COMPILER=$compilerPath" "-DCMAKE_RC_COMPILER=$rcPath" "-DCMAKE_MT=$mtPath" -DBUILD_TESTING=ON
if($LASTEXITCODE){throw 'CMake configure failed'}
& $cmake --build "$projectRoot\build" --parallel
if($LASTEXITCODE){throw 'Build failed'}
& (Join-Path $QtRoot 'Tools\CMake_64\bin\ctest.exe') --test-dir "$projectRoot\build" --output-on-failure
if($LASTEXITCODE){throw 'Tests failed'}
if($Package){
    $destination=Join-Path $projectRoot 'dist\ThinkVantage-0.5.3-win64'
    $resolvedDestination=[IO.Path]::GetFullPath($destination)
    $expectedDestination=[IO.Path]::GetFullPath((Join-Path $projectRoot 'dist\ThinkVantage-0.5.3-win64'))
    if($resolvedDestination -ne $expectedDestination -or -not $resolvedDestination.StartsWith([IO.Path]::GetFullPath((Join-Path $projectRoot 'dist'))+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe package destination'}
    if(Test-Path -LiteralPath $resolvedDestination){Remove-Item -LiteralPath $resolvedDestination -Recurse -Force}
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    Copy-Item -LiteralPath "$projectRoot\build\ThinkVantage.exe" -Destination $destination -Force
    & "$qt\bin\windeployqt.exe" --release --no-translations --no-opengl-sw --no-system-d3d-compiler --no-system-dxc-compiler --no-compiler-runtime --no-quick-import --skip-plugin-types generic,networkinformation,tls,imageformats,iconengines,styles "$destination\ThinkVantage.exe"
    if($LASTEXITCODE){throw 'Qt deployment failed'}
    Copy-Item -LiteralPath "$projectRoot\README.md","$projectRoot\THIRD_PARTY_NOTICES.md" -Destination $destination -Force
    Copy-Item -LiteralPath "$projectRoot\licenses" -Destination $destination -Recurse -Force
    Copy-Item -LiteralPath "$projectRoot\vendor" -Destination $destination -Recurse -Force
    foreach($runtime in @('msvcp140.dll','msvcp140_1.dll','msvcp140_2.dll','msvcp140_atomic_wait.dll','vcruntime140.dll','vcruntime140_1.dll','concrt140.dll')){
        $source=Join-Path $env:SystemRoot "System32\$runtime"
        if(Test-Path -LiteralPath $source){Copy-Item -LiteralPath $source -Destination $destination -Force}
    }
    if(Test-Path -LiteralPath "$projectRoot\docs\VALIDATION.md"){Copy-Item -LiteralPath "$projectRoot\docs\VALIDATION.md" -Destination $destination -Force}
    Copy-Item -LiteralPath "$projectRoot\docs\IMPLEMENTATION_EVIDENCE.md" -Destination $destination -Force
    Compress-Archive -Path $destination -DestinationPath "$projectRoot\dist\ThinkVantage-0.5.3-win64.zip" -Force
}
