param(
    [switch]$RebuildIcon
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$repoRoot = Split-Path -Parent $PSScriptRoot
$appDir = $PSScriptRoot
$payloadPath = Join-Path $appDir 'payload.zip'
$outDir = Join-Path $PSScriptRoot 'output'
$outExe = Join-Path $outDir 'AzForge-Setup.exe'

# 1. Read version from src/Core/ForgeVersion.h
$verHeader = Join-Path $repoRoot 'src\Core\ForgeVersion.h'
$verMatch = Select-String -Path $verHeader -Pattern 'AZFORGE_VERSION_STRING\s+"([^"]+)"' | Select-Object -First 1
$version = if ($verMatch) { $verMatch.Matches[0].Groups[1].Value } else { '0.1.0' }

# 2. Verify required native build outputs exist
$required = @(
    'Release\d3d8.dll',
    'Release\AzForge.dll',
    'Release\AmodPack.exe',
    'Release\AzmPack.exe',
    'Release\SampleMod.amod',
    'plugins\vs2026\Extension\bin\Release\net472\AzForge.Templates.vsix',
    'plugins\vscode\azforge-mod-tools-1.0.0.vsix'
)
$missing = @()
foreach ($rel in $required) {
    if (-not (Test-Path (Join-Path $repoRoot $rel))) { $missing += $rel }
}
if ($missing.Count -gt 0) {
    throw "Missing build outputs required for installer:`n  - $($missing -join "`n  - ")"
}

# 3. Ensure icon exists
$icoPath = Join-Path $PSScriptRoot 'assets\AzForge.ico'
if ($RebuildIcon -or -not (Test-Path $icoPath)) {
    & (Join-Path $PSScriptRoot 'make-assets.ps1') -RepoRoot $repoRoot
}

# 4. Pack embedded payload.zip
Write-Host "Packing payload.zip for AzForge $version..."
if (Test-Path $payloadPath) { Remove-Item -Force $payloadPath }

$zipStream = [System.IO.File]::Create($payloadPath)
$zip = New-Object System.IO.Compression.ZipArchive($zipStream, [System.IO.Compression.ZipArchiveMode]::Create)

function Add-ZipFile([string]$diskRelPath, [string]$entryName) {
    $full = Join-Path $repoRoot $diskRelPath
    if (-not (Test-Path $full)) {
        throw "Payload file not found: $full"
    }
    [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
        $zip, $full, $entryName.Replace('\', '/'), [System.IO.Compression.CompressionLevel]::Optimal
    ) | Out-Null
}

function Add-ZipDir([string]$diskRelDir, [string]$entryPrefix, [string]$filter = '*') {
    $base = Join-Path $repoRoot $diskRelDir
    $files = Get-ChildItem -Path $base -Filter $filter -File -Recurse
    foreach ($f in $files) {
        $rel = $f.FullName.Substring($base.Length).TrimStart('\')
        Add-ZipFile (Join-Path $diskRelDir $rel) ($entryPrefix.TrimEnd('/') + '/' + $rel.Replace('\', '/'))
    }
}

try {
    # Core loader -> game folder
    Add-ZipFile 'Release\d3d8.dll'    'core/d3d8.dll'
    Add-ZipFile 'Release\AzForge.dll' 'core/AzForge.dll'

    # Optional showcase mod -> game\mods
    Add-ZipFile 'Release\SampleMod.amod' 'samplemod/SampleMod.amod'

    # Developer SDK + CLI tools + MSBuild integration -> sdk folder
    Add-ZipFile 'Release\AmodPack.exe' 'sdk/Release/AmodPack.exe'
    Add-ZipFile 'Release\AzmPack.exe'  'sdk/Release/AzmPack.exe'
    Add-ZipFile 'README.md'            'sdk/README.md'
    Add-ZipFile 'tools\room_grid.py'   'sdk/tools/room_grid.py'

    Add-ZipDir  'sdk\include'            'sdk/sdk/include' '*.h'
    Add-ZipFile 'sdk\subsystems.txt'     'sdk/sdk/subsystems.txt'
    Add-ZipFile 'sdk\SampleMod\README.md'                 'sdk/sdk/SampleMod/README.md'
    Add-ZipFile 'sdk\SampleMod\SampleMod.cpp'             'sdk/sdk/SampleMod/SampleMod.cpp'
    Add-ZipFile 'sdk\SampleMod\SampleMod.vcxproj'         'sdk/sdk/SampleMod/SampleMod.vcxproj'
    Add-ZipFile 'sdk\SampleMod\SampleMod.vcxproj.filters' 'sdk/sdk/SampleMod/SampleMod.vcxproj.filters'
    Add-ZipDir  'sdk\SampleMod\inc_d3d8' 'sdk/sdk/SampleMod/inc_d3d8'
    Add-ZipDir  'sdk\samples'            'sdk/sdk/samples'

    Add-ZipDir  'plugins\templates'                 'sdk/plugins/templates'
    Add-ZipFile 'plugins\vs2026\AzForge.Mod.props'     'sdk/plugins/vs2026/AzForge.Mod.props'
    Add-ZipFile 'plugins\vs2026\AzForgeMod.xml'        'sdk/plugins/vs2026/AzForgeMod.xml'
    Add-ZipFile 'plugins\vs2026\Write-ModManifest.ps1' 'sdk/plugins/vs2026/Write-ModManifest.ps1'
    Add-ZipFile 'plugins\vs2026\Write-Subsystems.ps1'  'sdk/plugins/vs2026/Write-Subsystems.ps1'

    # IDE VSIX packages
    Add-ZipFile 'plugins\vs2026\Extension\bin\Release\net472\AzForge.Templates.vsix' 'vs/plugins/vs2026/AzForge.Templates.vsix'
    Add-ZipFile 'plugins\vscode\azforge-mod-tools-1.0.0.vsix'                        'vscode/plugins/vscode/azforge-mod-tools-1.0.0.vsix'
    Add-ZipFile 'plugins\vscode\README.md'                                           'vscode/plugins/vscode/README.md'
}
finally {
    $zip.Dispose()
    $zipStream.Dispose()
}

# 5. Build single-file WPF executable
$csproj = Join-Path $appDir 'AzForgeSetup.csproj'
Write-Host "Building AzForge-Setup.exe ($version)..."

$dotnet = Get-Command dotnet -ErrorAction SilentlyContinue
if ($dotnet) {
    & $dotnet.Source build $csproj -c Release -p:Version=$version -p:AssemblyVersion=$version.0 -p:FileVersion=$version.0
    if ($LASTEXITCODE -ne 0) { throw "dotnet build failed with exit code $LASTEXITCODE" }
} else {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
    if (-not $msbuild) { throw "Neither dotnet nor MSBuild was found." }
    & $msbuild $csproj /t:Restore,Build /p:Configuration=Release /p:Version=$version /p:AssemblyVersion=$version.0 /p:FileVersion=$version.0
    if ($LASTEXITCODE -ne 0) { throw "MSBuild failed with exit code $LASTEXITCODE" }
}

# 6. Copy final standalone exe to installer/output/AzForge-Setup.exe
$builtExe = Join-Path $appDir 'bin\Release\net472\AzForge-Setup.exe'
if (-not (Test-Path $builtExe)) {
    throw "Built executable not found at $builtExe"
}

New-Item -ItemType Directory -Force -Path $outDir | Out-Null
Copy-Item -Force $builtExe $outExe

$info = Get-Item $outExe
Write-Host ("Done: {0} ({1:N0} bytes)" -f $info.FullName, $info.Length)
