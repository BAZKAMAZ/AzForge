# Writes package/mod.json from what the project says about the mod.
#
# Two things are worked out rather than typed:
#
#   api_version  read from the SDK, so a mod cannot claim a version its headers are not
#   requires     read from what the mod's own sources use, so it cannot forget one
#
# Both come from the SDK as shipped - ForgeApi.h and subsystems.txt - and neither needs the
# loader's sources, which do not travel with it.

param(
    [Parameter(Mandatory = $true)] [string] $Package,
    [Parameter(Mandatory = $true)] [string] $Sdk,
    [Parameter(Mandatory = $true)] [string] $Id,
    [Parameter(Mandatory = $true)] [string] $Name,
    [Parameter(Mandatory = $true)] [string] $Version,
    [string] $Author = '',
    [string] $Description = '',
    [string] $Requires = '',
    [Parameter(Mandatory = $true)] [string] $Entry,
    [string] $Icon = '',
    [string] $Sources = ''
)

$ErrorActionPreference = 'Stop'

$types = Join-Path $Sdk 'include\ForgeTypes.h'
if (-not (Test-Path $types)) {
    throw "ForgeTypes.h is not at $types"
}

$match = Select-String -Path $types -Pattern 'AMOD_API_VERSION\s+(\d+)' | Select-Object -First 1
if (-not $match) {
    throw "AMOD_API_VERSION is not declared in $types"
}
$api = [int] $match.Matches[0].Groups[1].Value

# --- what the loader offers -------------------------------------------------------------------

$known = @()
$list = Join-Path $Sdk 'subsystems.txt'
if (Test-Path $list) {
    $known = @(Get-Content $list | ForEach-Object { $_.Trim() } | Where-Object { $_ })
}

# Amod::Camera:: is how a mod reaches the camera, and Camera::Attach(api->camera, ...) in
# ForgeApi.h is what ties that namespace to the subsystem's name. Reading the tie from the header
# means a subsystem added later is understood without anything here being edited.
$namespaces = @{}
$forgeApi = Join-Path $Sdk 'include\ForgeApi.h'
if (Test-Path $forgeApi) {
    foreach ($hit in Select-String -Path $forgeApi -Pattern '(\w+)::Attach\(api->(\w+)') {
        foreach ($m in $hit.Matches) {
            # objectsMenu is recorded as objects_menu
            $subsystem = [System.Text.RegularExpressions.Regex]::Replace(
                $m.Groups[2].Value, '([a-z0-9])([A-Z])', '$1_$2').ToLowerInvariant()

            if ($known.Count -eq 0 -or $known -contains $subsystem) {
                $namespaces[$m.Groups[1].Value] = $subsystem
            }
        }
    }
}

# --- what this mod uses -----------------------------------------------------------------------

function Get-Needs([string] $root) {
    $found = @()
    if (-not $root -or -not (Test-Path $root)) {
        return $found
    }

    $files = Get-ChildItem $root -Recurse -Include *.cpp, *.h, *.hpp, *.cxx -File -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch '\\(obj|bin|Debug|Release|\.vs)\\' }

    foreach ($file in $files) {
        $text = Get-Content $file.FullName -Raw

        foreach ($space in $namespaces.Keys) {
            if ($text -match "(?<![\w:])$space::") {
                $found += $namespaces[$space]
            }
        }

        # A mod with an AmodUpdate is driven by the frame subsystem
        if ($text -match '\bAmodUpdate\b' -and ($known.Count -eq 0 -or $known -contains 'frame')) {
            $found += 'frame'
        }
    }

    return $found
}

$declared = @()
foreach ($one in $Requires -split ';') {
    $trimmed = $one.Trim()
    if ($trimmed) { $declared += $trimmed }
}

# A list written by hand is taken as it stands - the author has said what they mean, including
# leaving out something they only use when it happens to be there.
if ($declared.Count -gt 0) {
    $needs = $declared

    foreach ($one in $needs) {
        if ($known.Count -gt 0 -and $known -notcontains $one) {
            throw "'$one' is not a subsystem this loader offers. It has: $($known -join ', ')"
        }
    }
}
else {
    $needs = @(Get-Needs $Sources | Sort-Object -Unique)
}

# --- the manifest -------------------------------------------------------------------------------

$manifest = [ordered] @{
    id           = $Id
    name         = $Name
    version      = $Version
    author       = $Author
    description  = $Description
    api_version  = $api
    requires     = $needs
    game_version = '1.0'
    entry        = $Entry
}

if ($Icon) {
    $manifest['icon'] = Split-Path $Icon -Leaf
}

$path = Join-Path $Package 'mod.json'

if (-not (Test-Path $Package)) {
    New-Item -ItemType Directory -Path $Package -Force | Out-Null
}

$json = $manifest | ConvertTo-Json -Depth 4

# PowerShell 5 writes a one-item list as a bare string and an empty one as "", so the brackets
# are put back by hand
if ($needs.Count -eq 0) {
    $json = $json -replace '"requires":\s*(""|null)', '"requires": []'
}
elseif ($needs.Count -eq 1) {
    $json = $json -replace '"requires":\s*"([^"]*)"', '"requires": [ "$1" ]'
}

Set-Content -Path $path -Value $json -Encoding utf8

$how = if ($declared.Count -gt 0) { 'as declared' } else { 'from the sources' }
Write-Host "mod.json -> $path (API $api, requires ${how}: $($needs -join ', '))"
