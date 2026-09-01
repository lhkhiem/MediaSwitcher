[CmdletBinding()]
param(
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$installerDirectory = [IO.Path]::GetFullPath($PSScriptRoot)
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $installerDirectory '..'))
$runtimeRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'build-obs\obs-runtime-stage'))
$innoScript = Join-Path $installerDirectory 'MediaSwitcher.iss'
$outputDirectory = Join-Path $installerDirectory 'output'

Push-Location $repositoryRoot
try {
    if (-not $SkipBuild) {
        & cmake --build build-obs --config Release --target MediaSwitcher
        if ($LASTEXITCODE -ne 0) {
            throw "Release build failed with exit code $LASTEXITCODE."
        }
    }

    $requiredFiles = @(
        'bin\64bit\MediaSwitcher.exe',
        'bin\64bit\app_icon.ico',
        'bin\64bit\Qt6Core.dll',
        'bin\64bit\Qt6Gui.dll',
        'bin\64bit\Qt6Widgets.dll',
        'bin\64bit\Qt6Network.dll',
        'bin\64bit\Qt6Svg.dll',
        'bin\64bit\opengl32sw.dll',
        'bin\64bit\obs.dll',
        'bin\64bit\libobs-d3d11.dll',
        'bin\64bit\avcodec-61.dll',
        'bin\64bit\avdevice-61.dll',
        'bin\64bit\avfilter-10.dll',
        'bin\64bit\avformat-61.dll',
        'bin\64bit\avutil-59.dll',
        'bin\64bit\swresample-5.dll',
        'bin\64bit\swscale-8.dll',
        'bin\64bit\librist.dll',
        'bin\64bit\libx264-164.dll',
        'bin\64bit\srt.dll',
        'bin\64bit\w32-pthreads.dll',
        'bin\64bit\zlib.dll',
        'bin\64bit\platforms\qwindows.dll',
        'obs-plugins\64bit\image-source.dll',
        'obs-plugins\64bit\obs-ffmpeg.dll',
        'obs-plugins\64bit\obs-transitions.dll',
        'data\libobs\default.effect',
        'data\obs-plugins\obs-transitions\fade_transition.effect'
    )
    $missingFiles = @($requiredFiles | Where-Object {
        -not (Test-Path -LiteralPath (Join-Path $runtimeRoot $_) -PathType Leaf)
    })
    if ($missingFiles.Count -gt 0) {
        throw "Runtime stage is missing required files:`n$($missingFiles -join "`n")"
    }

    $innoCandidates = @(
        'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
        'C:\Program Files\Inno Setup 6\ISCC.exe'
    )
    $innoCompiler = $innoCandidates | Where-Object {
        Test-Path -LiteralPath $_ -PathType Leaf
    } | Select-Object -First 1
    if (-not $innoCompiler) {
        throw 'Inno Setup 6 (ISCC.exe) was not found.'
    }

    $vcRedistRoot = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC'
    $vcRedist = Get-ChildItem -LiteralPath $vcRedistRoot -Filter 'vc_redist.x64.exe' -File -Recurse |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if (-not $vcRedist) {
        throw 'vc_redist.x64.exe was not found.'
    }

    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    $arguments = @(
        "/DSourceRoot=$runtimeRoot",
        "/DVCRedistPath=$($vcRedist.FullName)",
        $innoScript
    )
    & $innoCompiler @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Inno Setup Compiler failed with exit code $LASTEXITCODE."
    }

    $setupFile = Get-ChildItem -LiteralPath $outputDirectory -Filter 'MediaSwitcher-Setup-v*-x64.exe' -File |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if (-not $setupFile) {
        throw 'Inno Setup did not create the expected installer.'
    }
    $hash = Get-FileHash -LiteralPath $setupFile.FullName -Algorithm SHA256
    Write-Output "SETUP_PATH=$($setupFile.FullName)"
    Write-Output "SETUP_SIZE=$($setupFile.Length)"
    Write-Output "SETUP_SHA256=$($hash.Hash)"
}
finally {
    Pop-Location
}
