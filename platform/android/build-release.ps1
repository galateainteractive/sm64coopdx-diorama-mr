[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SigningProperties,
    [string]$JavaHome,
    [string]$AndroidSdk,
    [string]$GradleUserHome,
    [string]$ShortDrive,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'release-artifacts')
)

$ErrorActionPreference = 'Stop'
$physicalProjectRoot = $PSScriptRoot
$propertiesPath = (Resolve-Path -LiteralPath $SigningProperties).Path

if (-not [string]::IsNullOrWhiteSpace($JavaHome)) {
    $env:JAVA_HOME = (Resolve-Path -LiteralPath $JavaHome).Path
    $env:Path = (Join-Path $env:JAVA_HOME 'bin') + ';' + $env:Path
}

if (-not [string]::IsNullOrWhiteSpace($AndroidSdk)) {
    $env:ANDROID_SDK_ROOT = (Resolve-Path -LiteralPath $AndroidSdk).Path
}
if (-not [string]::IsNullOrWhiteSpace($GradleUserHome)) {
    $env:GRADLE_USER_HOME = [System.IO.Path]::GetFullPath($GradleUserHome)
    New-Item -ItemType Directory -Path $env:GRADLE_USER_HOME -Force | Out-Null
}
if ([string]::IsNullOrWhiteSpace($env:ANDROID_SDK_ROOT)) {
    throw 'ANDROID_SDK_ROOT is not set. Supply -AndroidSdk.'
}

$versionLine = Select-String -LiteralPath (Join-Path $physicalProjectRoot 'gradle.properties') `
    -Pattern '^SM64_VERSION_NAME=(.+)$' | Select-Object -First 1
if (-not $versionLine) { throw 'SM64_VERSION_NAME is missing from gradle.properties.' }
$versionName = $versionLine.Matches[0].Groups[1].Value.Trim()

$projectRoot = $physicalProjectRoot
$mappedDrive = $null
$locationPushed = $false
try {
    if (-not [string]::IsNullOrWhiteSpace($ShortDrive)) {
        $driveName = $ShortDrive.Trim().TrimEnd(':')
        if ($driveName -notmatch '^[D-Zd-z]$') {
            throw 'ShortDrive must be one unused drive letter from D: through Z:.'
        }
        $drive = $driveName.ToUpperInvariant() + ':'
        if (Test-Path -LiteralPath ($drive + '\')) {
            throw "ShortDrive is already in use: $drive"
        }
        $repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $physicalProjectRoot '..\..'))
        & subst.exe $drive $repositoryRoot
        if ($LASTEXITCODE -ne 0) { throw "Unable to map $drive to $repositoryRoot" }
        $mappedDrive = $drive
        $projectRoot = $drive + '\platform\android'
    }

    $env:SM64_RELEASE_KEYSTORE_PROPERTIES = $propertiesPath
    Push-Location $projectRoot
    $locationPushed = $true
    & (Join-Path $projectRoot 'gradlew.bat') --no-daemon assembleRelease
    if ($LASTEXITCODE -ne 0) { throw "Gradle exited with code $LASTEXITCODE." }
}
finally {
    if ($locationPushed) { Pop-Location }
    Remove-Item Env:SM64_RELEASE_KEYSTORE_PROPERTIES -ErrorAction SilentlyContinue
    if ($mappedDrive) { & subst.exe $mappedDrive /D | Out-Null }
}

$sourceApk = Join-Path $physicalProjectRoot 'app\build\outputs\apk\release\app-release.apk'
if (-not (Test-Path -LiteralPath $sourceApk -PathType Leaf)) {
    throw "Release APK was not produced: $sourceApk"
}

$resolvedOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $resolvedOutput -Force | Out-Null
$destination = Join-Path $resolvedOutput "SM64-Diorama-MR-v$versionName.apk"
Copy-Item -LiteralPath $sourceApk -Destination $destination -Force

$buildTools = Get-ChildItem -LiteralPath (Join-Path $env:ANDROID_SDK_ROOT 'build-tools') -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
if (-not $buildTools) { throw 'Android SDK build-tools were not found.' }
$apksigner = Join-Path $buildTools.FullName 'apksigner.bat'
& $apksigner verify --verbose --print-certs $destination
if ($LASTEXITCODE -ne 0) { throw 'APK signature verification failed.' }

$file = Get-Item -LiteralPath $destination
$hash = Get-FileHash -Algorithm SHA256 -LiteralPath $destination
Write-Host "Release APK: $($file.FullName)"
Write-Host "Size: $($file.Length) bytes"
Write-Host "SHA-256: $($hash.Hash)"
