param([string]$QtRoot='E:\Qt6',[string]$DownloadProxy,[int]$Jobs=4)
 . (Join-Path $PSScriptRoot 'toolchain.ps1') -QtRoot $QtRoot
if(-not $DownloadProxy){
    $systemProxy=Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings' -ErrorAction SilentlyContinue
    if($systemProxy.ProxyEnable -eq 1){
        $server=$systemProxy.ProxyServer
        if($server -match '(?:^|;)https=([^;]+)'){$server=$Matches[1]}
        if($server -and $server -notmatch '='){$DownloadProxy=if($server -match '^https?://'){$server}else{'http://'+$server}}
    }
}
$work=Join-Path $projectRoot 'build\qt'
$archive=Join-Path $work 'qtbase.tar.xz'
$source=Join-Path $work 'qtbase-everywhere-src-6.12.0'
$qtBuild=Join-Path $work 'obj'
$prefix=Join-Path $work 'static'
$sha='a951bd163c7b80fc6b8c88d7668fb56abf91c152373e13c10666763238131307'
New-Item -ItemType Directory -Path $work,$qtBuild -Force | Out-Null
if(-not (Test-Path -LiteralPath $archive)){
    $download=@('-fL','--retry','2','--connect-timeout','20','-o',$archive)
    if($DownloadProxy){$download+=@('--proxy',$DownloadProxy)}
    & curl.exe @download 'https://download.qt.io/archive/qt/6.12/6.12.0/submodules/qtbase-everywhere-src-6.12.0.tar.xz'
    if($LASTEXITCODE){throw 'Qt source download failed'}
}
if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $sha){throw 'Qt source SHA-256 mismatch'}
if(-not (Test-Path -LiteralPath (Join-Path $source 'configure.bat'))){& tar.exe -xf $archive -C $work;if($LASTEXITCODE){throw 'Qt source extraction failed'}}
$options=@('-static','-static-runtime','-release','-opensource','-confirm-license','-prefix',$prefix,'-nomake','examples','-nomake','tests','-optimize-size','-ltcg','-no-opengl','-no-vulkan','-no-icu','-no-ssl','-no-feature-sql','-no-feature-printsupport','-no-feature-dbus','-no-feature-http','-no-feature-networkproxy','-no-feature-networkdiskcache','-no-feature-socks5','-no-feature-udpsocket','-no-feature-graphicseffect','-no-feature-style-windowsvista','-no-feature-style-windows11','-no-feature-pdf','--','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_TRY_COMPILE_CONFIGURATION=Release',"-DCMAKE_CXX_COMPILER=$compilerPath","-DCMAKE_C_COMPILER=$compilerPath","-DCMAKE_RC_COMPILER=$rcPath","-DCMAKE_MT=$mtPath")
if($env:THINKVANTAGE_MSVC_INCLUDE_PREFIX){$options+="-DCMAKE_CL_SHOWINCLUDES_PREFIX=$env:THINKVANTAGE_MSVC_INCLUDE_PREFIX"}
Push-Location $qtBuild
try{
    & (Join-Path $source 'configure.bat') @options
    if($LASTEXITCODE){throw 'Static Qt configure failed'}
    & $cmake --build . --parallel $Jobs
    if($LASTEXITCODE){throw 'Static Qt build failed'}
    & $cmake --install .
    if($LASTEXITCODE){throw 'Static Qt install failed'}
    [ordered]@{version='6.12.0';sourceSha256=$sha;prefix=$prefix;configureOptions=$options} | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $prefix 'thinkvantage-build.json')
}finally{Pop-Location}
