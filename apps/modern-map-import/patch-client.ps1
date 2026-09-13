param(
    [Parameter(Mandatory = $true)][string]$ClientDirectory,
    [Parameter(Mandatory = $true)][string]$PatcherPath
)

$ErrorActionPreference = 'Stop'
$client = (Resolve-Path -LiteralPath $ClientDirectory).Path
$patcher = (Resolve-Path -LiteralPath $PatcherPath).Path
$exe = Join-Path $client 'Wow.exe'
$core = Join-Path $client 'WarcraftXL.dll'
$backup = "$exe.orig"

function Assert-Hash([string]$Path, [string]$Expected) {
    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    if ($actual -ne $Expected) {
        throw "Unexpected SHA256 for ${Path}: $actual"
    }
}

# Pinned official release v1.1.245 and the supplied, unmodified build 12340 client.
Assert-Hash $patcher 'af1bc8d2d0c8782750976c3b15e1bb98b59950c9549dd0a5c967a35b63791dc6'
Assert-Hash $core '0723d3b115d60ad8aca27bc5caaff8dffa113efb491251d9db0fa4e22426f09d'
$originalHash = 'aa63a5750d60ef16746c686b3d5e26876d98953eab08b1c026cd0faf78e88cb8'
Assert-Hash $exe $originalHash
if (Test-Path -LiteralPath $backup) {
    Assert-Hash $backup $originalHash
}

Push-Location -LiteralPath $client
try {
    & $patcher $exe
    if ($LASTEXITCODE -ne 0) {
        throw "WarcraftXL patcher failed with exit code $LASTEXITCODE"
    }
} finally {
    Pop-Location
}

Assert-Hash $backup $originalHash
if ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -eq $originalHash) {
    throw 'Patcher returned success but Wow.exe did not change'
}
Write-Output "Patched $exe; original preserved at $backup. In-game validation is still required."
