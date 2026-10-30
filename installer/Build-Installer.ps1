[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [string]$VCRedistPath
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

    # The Qt installer may carry a newer redistributable than Visual Studio.
    # Sort by binary version, never by directory name (e.g. "v143").
    $scriptText = [IO.File]::ReadAllText($innoScript, [Text.Encoding]::UTF8)
    $minimumMatch = [regex]::Match($scriptText, '#define MinimumVCRedistVersion "([0-9.]+)"')
    if (-not $minimumMatch.Success) { throw 'Missing MinimumVCRedistVersion in installer script.' }
    $minimumVersion = [version]$minimumMatch.Groups[1].Value
    $candidatePaths = @()
    if ($VCRedistPath) {
        $candidatePaths += (Get-Item -LiteralPath $VCRedistPath).FullName
    } else {
        $cache = [IO.File]::ReadAllText((Join-Path $repositoryRoot 'build-obs\CMakeCache.txt'))
        $qtMatch = [regex]::Match($cache, '(?m)^Qt6_DIR:[^=]+=(.+)$')
        if ($qtMatch.Success) {
            $qtRoot = [IO.Path]::GetFullPath((Join-Path $qtMatch.Groups[1].Value.Trim() '..\..\..\..\..'))
            $candidatePaths += Get-ChildItem -LiteralPath (Join-Path $qtRoot 'vcredist') `
                -Filter '*redist*x64.exe' -File -ErrorAction SilentlyContinue | Select-Object -ExpandProperty FullName
        }
        $vsRoots = @($env:ProgramFiles, ${env:ProgramFiles(x86)}) | Where-Object { $_ } |
            ForEach-Object { Join-Path $_ 'Microsoft Visual Studio' } | Select-Object -Unique
        foreach ($root in $vsRoots) {
            $candidatePaths += Get-ChildItem -LiteralPath $root -Filter 'vc_redist.x64.exe' `
                -File -Recurse -ErrorAction SilentlyContinue | Select-Object -ExpandProperty FullName
        }
    }
    $vcRedist = $candidatePaths | Select-Object -Unique | ForEach-Object {
        $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($_)
        [PSCustomObject]@{
            FullName = $_
            Version = [version]::new($info.FileMajorPart, $info.FileMinorPart, $info.FileBuildPart, $info.FilePrivatePart)
        }
    } | Where-Object { $_.Version -ge $minimumVersion } |
        Sort-Object Version -Descending | Select-Object -First 1
    if (-not $vcRedist) {
        throw "Visual C++ x64 Redistributable >= $minimumVersion is required. Use -VCRedistPath with the Microsoft package; an older package must not be shipped."
    }
    Write-Output "VC_REDIST_PATH=$($vcRedist.FullName)"
    Write-Output "VC_REDIST_VERSION=$($vcRedist.Version)"

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
