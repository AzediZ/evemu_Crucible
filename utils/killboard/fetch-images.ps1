# Downloads the type icons, ship renders and NPC corporation logos the killboard pages (and the client's image
# requests) use into the server's image directory, from CCP's image server.  Run it once on a machine with internet
# access; it skips files it already has, so it can be stopped and run again.
#
#   powershell -ExecutionPolicy Bypass -File fetch-images.ps1 -ImageDir C:\path\to\image_cache
#
# ImageDir is the <files><imageDir> of eve-server.xml (relative paths there are relative to the server's bin folder).
# Player corporation and alliance logos aren't fetched: their ids belong to other corporations on CCP's server.

param(
    [Parameter(Mandatory = $true)][string]$ImageDir,
    [switch]$NoRenders
)

$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$base = 'https://images.evetech.net'

foreach ($dir in 'InventoryType', 'Render', 'Corporation') {
    New-Item -ItemType Directory -Force -Path (Join-Path $ImageDir $dir) | Out-Null
}

$client = New-Object System.Net.WebClient
$got = 0; $missing = 0; $have = 0

function Fetch([string]$url, [string]$file) {
    if (Test-Path $file) { $script:have++; return }
    try {
        $client.DownloadFile($url, $file)
        $script:got++
    } catch {
        # no image for this one (404).  leave nothing behind, the server answers 404 itself
        if (Test-Path $file) { Remove-Item $file -Force }
        $script:missing++
    }
}

# types.txt:  typeID and what to get (i icon, b blueprint icon, r icon and render)
$types = Get-Content (Join-Path $here 'types.txt')
$n = 0
foreach ($line in $types) {
    $parts = $line.Split(' ')
    if ($parts.Count -lt 2) { continue }
    $id = $parts[0]; $kind = $parts[1]
    $icon = if ($kind -eq 'b') { 'bp' } else { 'icon' }
    Fetch "$base/types/$id/${icon}?size=64" (Join-Path $ImageDir "InventoryType\${id}_64.png")
    if (($kind -eq 'r') -and -not $NoRenders) {
        Fetch "$base/types/$id/render?size=256" (Join-Path $ImageDir "Render\${id}_256.png")
    }
    $n++
    if (($n % 250) -eq 0) {
        Write-Host ("{0} of {1} types  ({2} downloaded, {3} already there, {4} with no image)" -f $n, $types.Count, $got, $have, $missing)
    }
}

foreach ($id in Get-Content (Join-Path $here 'npc-corps.txt')) {
    if ($id.Trim() -eq '') { continue }
    Fetch "$base/corporations/$id/logo?size=128" (Join-Path $ImageDir "Corporation\${id}_128.png")
}

Write-Host ("Done:  {0} downloaded, {1} already there, {2} with no image." -f $got, $have, $missing)
