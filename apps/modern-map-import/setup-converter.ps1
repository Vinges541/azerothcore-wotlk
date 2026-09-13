param(
    [Parameter(Mandatory = $true)][string]$Archive,
    [Parameter(Mandatory = $true)][string]$Directory
)
$ErrorActionPreference = 'Stop'
if (Test-Path -LiteralPath $directory) {
    throw "Output already exists: $directory"
}
Expand-Archive -LiteralPath $Archive -DestinationPath $directory
$env:DOTNET_BUNDLE_EXTRACT_BASE_DIR = Join-Path $directory 'bundle'
Start-Process -FilePath (Join-Path $directory 'WarcraftXL-Cold-Converter.exe') `
    -WorkingDirectory $directory -WindowStyle Minimized -PassThru | Select-Object Id
