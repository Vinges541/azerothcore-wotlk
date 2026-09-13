param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Client
)
$ErrorActionPreference = 'Stop'
$exe = Join-Path $Client 'Wow.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw 'Test client missing' }
$running = @(Get-Process Wow -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe })
if ($running.Count) { throw 'Close the map-lab client before installing' }
$target = Join-Path $Client 'Data\patch-5.MPQ'
if (Test-Path -LiteralPath $target) { throw 'patch-5.MPQ already exists; refusing overwrite' }
if (-not (Test-Path -LiteralPath $Source -PathType Container)) { throw 'Source patch missing' }
& robocopy.exe $Source $target /E /R:1 /W:1 /NFL /NDL /NJH /NJS
if ($LASTEXITCODE -ge 8) { throw "Copy failed: $LASTEXITCODE" }
$count = 0
Get-ChildItem -LiteralPath $Source -Recurse -File | ForEach-Object {
    $relative = $_.FullName.Substring($Source.Length).TrimStart('\')
    $destination = Join-Path $target $relative
    if ((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash) {
        throw "Hash mismatch: $relative"
    }
    $count++
}
Write-Output "Installed and SHA256-verified $count files in $target"
exit 0
