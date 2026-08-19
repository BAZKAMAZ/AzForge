# Writes sdk\subsystems.txt: every subsystem name the loader records, taken from the loader itself.
#
# A mod's build reads that file to check - and to work out - what it requires. Because the list is
# generated here and ships with the SDK, adding a subsystem to ForgeBoot.cpp is all it takes for
# mods to know about it. Nothing in the Visual Studio extension knows any subsystem by name, so it
# never needs updating for one.

param(
    [Parameter(Mandatory = $true)] [string] $Boot,
    [Parameter(Mandatory = $true)] [string] $Out
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Boot)) {
    throw "ForgeBoot.cpp is not at $Boot"
}

$names = @()
foreach ($hit in Select-String -Path $Boot -Pattern '(?:Subsystems::Record|Install)\(\s*"([^"]+)"') {
    foreach ($match in $hit.Matches) {
        $names += $match.Groups[1].Value
    }
}

if ($names.Count -eq 0) {
    throw "No subsystem names in $Boot - has the way they are declared changed?"
}

$names = $names | Sort-Object -Unique
$text = ($names -join "`r`n") + "`r`n"

# Only rewrite when it actually changed, so a mod project does not rebuild for nothing
if ((Test-Path $Out) -and (Get-Content $Out -Raw) -eq $text) {
    return
}

Set-Content -Path $Out -Value $text -NoNewline -Encoding utf8
Write-Host "subsystems.txt -> $Out ($($names.Count) of them)"
