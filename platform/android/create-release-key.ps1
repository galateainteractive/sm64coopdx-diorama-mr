[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,
    [string]$JavaHome,
    [string]$KeyAlias = 'sm64-diorama-mr',
    [string]$DistinguishedName = 'CN=Galatea Interactive, O=Galatea Interactive, C=BR'
)

$ErrorActionPreference = 'Stop'

function Resolve-Keytool {
    param([string]$RequestedJavaHome)

    if (-not [string]::IsNullOrWhiteSpace($RequestedJavaHome)) {
        $candidate = Join-Path $RequestedJavaHome 'bin\keytool.exe'
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
        throw "keytool.exe was not found below JavaHome: $RequestedJavaHome"
    }

    $command = Get-Command keytool.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    throw 'keytool.exe was not found. Supply -JavaHome with a JDK 17 path.'
}

$keytool = Resolve-Keytool -RequestedJavaHome $JavaHome
$resolvedOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $resolvedOutput -Force | Out-Null

$keyPath = Join-Path $resolvedOutput 'sm64-diorama-mr-release.p12'
$propertiesPath = Join-Path $resolvedOutput 'keystore.properties'
$recoveryPath = Join-Path $resolvedOutput 'RECOVERY-INSTRUCTIONS.txt'

foreach ($path in @($keyPath, $propertiesPath)) {
    if (Test-Path -LiteralPath $path) {
        throw "Refusing to overwrite existing release signing material: $path"
    }
}

$bytes = New-Object byte[] 32
$rng = [System.Security.Cryptography.RandomNumberGenerator]::Create()
try {
    $rng.GetBytes($bytes)
}
finally {
    $rng.Dispose()
}
$password = [Convert]::ToBase64String($bytes).TrimEnd('=')
$env:SM64_RELEASE_STORE_PASSWORD = $password

try {
    & $keytool -genkeypair -v `
        -keystore $keyPath `
        -storetype PKCS12 `
        -alias $KeyAlias `
        -keyalg RSA `
        -keysize 4096 `
        -validity 10000 `
        -dname $DistinguishedName `
        -storepass:env SM64_RELEASE_STORE_PASSWORD `
        -keypass:env SM64_RELEASE_STORE_PASSWORD
    if ($LASTEXITCODE -ne 0) {
        throw "keytool exited with code $LASTEXITCODE."
    }

    $portableKeyPath = $keyPath.Replace('\', '/')
    $properties = @(
        "storeFile=$portableKeyPath",
        "storePassword=$password",
        "keyAlias=$KeyAlias",
        "keyPassword=$password"
    ) -join [Environment]::NewLine
    [System.IO.File]::WriteAllText($propertiesPath, $properties + [Environment]::NewLine,
        (New-Object System.Text.UTF8Encoding($false)))

    $recovery = @(
        'SM64 Diorama MR release signing backup',
        '',
        'Keep sm64-diorama-mr-release.p12 and keystore.properties together.',
        'Both files are required to publish compatible updates.',
        'Never commit them or share them in a public release.',
        'Keep at least one encrypted backup on a separate drive or trusted vault.'
    ) -join [Environment]::NewLine
    [System.IO.File]::WriteAllText($recoveryPath, $recovery + [Environment]::NewLine,
        (New-Object System.Text.UTF8Encoding($false)))
}
finally {
    Remove-Item Env:SM64_RELEASE_STORE_PASSWORD -ErrorAction SilentlyContinue
}

Write-Host "Release key: $keyPath"
Write-Host "Signing properties: $propertiesPath"
Write-Host 'The password was saved only in the local signing properties file.'
