param([Parameter(Mandatory=$true)][string]$QtPrefix,[string]$QtRoot='E:\Qt6',[string]$Output)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'scripts\toolchain.ps1') -QtRoot $QtRoot
$kit=Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'relink.json') | ConvertFrom-Json
$response=Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'original-link.rsp')
foreach($pair in @(@($kit.qtPrefix,[IO.Path]::GetFullPath($QtPrefix)),@($kit.qtRoot,[IO.Path]::GetFullPath($QtRoot)))){
    $response=$response.Replace($pair[0].Replace('\','/'),$pair[1].Replace('\','/')).Replace($pair[0].Replace('/','\'),$pair[1].Replace('/','\'))
}
if(-not $Output){$Output=Join-Path $PSScriptRoot 'ThinkVantage-relinked.exe'}
$Output=[IO.Path]::GetFullPath($Output)
Set-Content -LiteralPath (Join-Path $PSScriptRoot 'link.rsp') -Value $response -Encoding utf8
Push-Location $PSScriptRoot
try{
    & (Join-Path $bin 'link.exe') /NOLOGO /LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO /MANIFEST:NO /SUBSYSTEM:WINDOWS /MACHINE:X64 "/OUT:$Output" '@link.rsp'
    if($LASTEXITCODE){throw 'Relink failed'}
}finally{Pop-Location}
