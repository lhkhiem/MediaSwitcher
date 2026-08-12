[CmdletBinding()]
param(
    [string]$RuntimeRoot = 'D:\deps\obs-runtime',
    [string]$WorkRoot = 'D:\deps\obs-runtime-work'
)

$ErrorActionPreference = 'Stop'
$version = '32.1.2'
$runtimeUrl = "https://cdn-fastly.obsproject.com/downloads/OBS-Studio-$version-Windows-x64.zip"
$sourceUrl = "https://github.com/obsproject/obs-studio/archive/refs/tags/$version.tar.gz"
$runtimeHash = '8d97e4563bd8d22d03e63042aa7dccede1d555c9bd35ce8a9e5019b0d0201bf6'
$sourceHash = 'b4a59410cddb46d0e31df1ee13b8ec66f30862d7e980c1a8c4e3b5d16fae6053'

if (Test-Path -LiteralPath $RuntimeRoot) {
    throw "Runtime root already exists: $RuntimeRoot. Remove it only after verifying it contains no needed data."
}

$downloads = Join-Path $WorkRoot 'downloads'
$artifactRoot = Join-Path $WorkRoot "obs-studio-$version-x64"
$sourceRoot = Join-Path $WorkRoot "obs-studio-$version-source"
New-Item -ItemType Directory -Force -Path $downloads, $artifactRoot, $sourceRoot | Out-Null

$runtimeZip = Join-Path $downloads "OBS-Studio-$version-Windows-x64.zip"
$sourceArchive = Join-Path $downloads "obs-studio-$version.tar.gz"
Invoke-WebRequest -Uri $runtimeUrl -OutFile $runtimeZip
Invoke-WebRequest -Uri $sourceUrl -OutFile $sourceArchive

$actualRuntimeHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $runtimeZip).Hash.ToLowerInvariant()
if ($actualRuntimeHash -ne $runtimeHash) {
    throw "OBS runtime SHA-256 mismatch. Expected $runtimeHash, got $actualRuntimeHash."
}
$actualSourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $sourceArchive).Hash.ToLowerInvariant()
if ($actualSourceHash -ne $sourceHash) {
    throw "OBS source SHA-256 mismatch. Expected $sourceHash, got $actualSourceHash."
}

Expand-Archive -LiteralPath $runtimeZip -DestinationPath $artifactRoot
tar -xzf $sourceArchive -C $sourceRoot
$sourceTree = Join-Path $sourceRoot "obs-studio-$version"

New-Item -ItemType Directory -Path "$RuntimeRoot\bin\64bit", "$RuntimeRoot\lib", "$RuntimeRoot\obs-plugins\64bit", "$RuntimeRoot\data\obs-plugins", "$RuntimeRoot\include" | Out-Null

$binFiles = @(
    'obs.dll', 'libobs-d3d11.dll', 'avcodec-61.dll', 'avdevice-61.dll',
    'avfilter-10.dll', 'avformat-61.dll', 'avutil-59.dll', 'libx264-164.dll',
    'swresample-5.dll', 'swscale-8.dll', 'zlib.dll', 'w32-pthreads.dll',
    'librist.dll', 'srt.dll'
)
foreach ($file in $binFiles) {
    Copy-Item -LiteralPath "$artifactRoot\bin\64bit\$file" -Destination "$RuntimeRoot\bin\64bit\$file"
}
Copy-Item -LiteralPath "$artifactRoot\obs-plugins\64bit\obs-ffmpeg.dll" -Destination "$RuntimeRoot\obs-plugins\64bit\obs-ffmpeg.dll"
Copy-Item -LiteralPath "$artifactRoot\data\libobs" -Destination "$RuntimeRoot\data" -Recurse
Copy-Item -LiteralPath "$artifactRoot\data\obs-plugins\obs-ffmpeg" -Destination "$RuntimeRoot\data\obs-plugins\obs-ffmpeg" -Recurse
Get-ChildItem -LiteralPath "$sourceTree\libobs" -Force | Copy-Item -Destination "$RuntimeRoot\include" -Recurse
@"
#pragma once

#define OBS_DATA_PATH ""data/libobs""
#define OBS_PLUGIN_PATH ""obs-plugins/64bit""
#define OBS_PLUGIN_DESTINATION ""obs-plugins/64bit""

#define OBS_RELEASE_CANDIDATE 0
#define OBS_BETA 0
"@ | Set-Content -LiteralPath "$RuntimeRoot\include\obsconfig.h" -Encoding utf8

if (!(Test-Path -LiteralPath "$RuntimeRoot\data\libobs\default.effect")) {
    throw 'The controlled runtime is missing data\libobs\default.effect after deployment.'
}

$visualStudioRoots = @($env:ProgramFiles, ${env:ProgramFiles(x86)}) |
    Where-Object { $_ } |
    ForEach-Object { Join-Path $_ 'Microsoft Visual Studio' } |
    Where-Object { Test-Path -LiteralPath $_ }
$msvcBin = Get-ChildItem -Path $visualStudioRoots -Directory -Recurse -Filter MSVC -ErrorAction SilentlyContinue |
    Get-ChildItem -Directory -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending |
    ForEach-Object { Join-Path $_.FullName 'bin\Hostx64\x64' } |
    Where-Object { Test-Path -LiteralPath $_ } |
    Select-Object -First 1
$dumpbin = Join-Path $msvcBin 'dumpbin.exe'
$libTool = Join-Path $msvcBin 'lib.exe'
if (!(Test-Path -LiteralPath $dumpbin) -or !(Test-Path -LiteralPath $libTool)) {
    throw 'MSVC dumpbin.exe and lib.exe are required to generate obs.lib from the matching obs.dll.'
}

$exports = & $dumpbin /exports "$artifactRoot\bin\64bit\obs.dll"
$names = $exports | ForEach-Object {
    if ($_ -match '^\s*\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+([^\s=]+)\s+=') { $matches[1] }
} | Where-Object { $_ }
if ($names.Count -lt 100) { throw "Could not extract obs.dll exports: $($names.Count)." }
$defPath = "$RuntimeRoot\lib\obs.def"
[System.IO.File]::WriteAllLines($defPath, @('LIBRARY obs.dll', 'EXPORTS') + $names, [System.Text.UTF8Encoding]::new($false))
& $libTool "/def:$defPath" '/machine:x64' "/out:$RuntimeRoot\lib\obs.lib"
if ($LASTEXITCODE -ne 0) { throw "lib.exe failed with exit code $LASTEXITCODE." }

[ordered]@{
    schema = 1
    product = 'OBS Studio/libobs controlled runtime'
    version = $version
    architecture = 'windows-x64'
    runtimeArtifact = "OBS-Studio-$version-Windows-x64.zip"
    runtimeSha256 = $runtimeHash
    sourceArtifact = "obs-studio-$version source tag"
    sourceSha256 = $sourceHash
    modules = @('obs-ffmpeg')
    graphicsBackend = 'libobs-d3d11'
    importLibrary = 'Generated from matching obs.dll export table'
} | ConvertTo-Json | Set-Content -LiteralPath "$RuntimeRoot\runtime-manifest.json" -Encoding utf8

Write-Output "Prepared controlled OBS runtime $version at $RuntimeRoot"