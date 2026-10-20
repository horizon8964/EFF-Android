param(
    [string]$Jdk = 'D:\androidstudio\jbr',
    [string]$Sdk = 'D:\android-sdk',
    [string]$Ndk = 'D:\ndk-arm\android-ndk-r30',
    [string]$CmakeRoot = 'D:\cmake',
    [string]$BuildTemp = 'D:\tmp\eff-android'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$env:JAVA_HOME = $Jdk
$env:TEMP = $BuildTemp
$env:TMP = $BuildTemp
New-Item -ItemType Directory -Force -Path $BuildTemp | Out-Null
# Use a short ASCII path for Java's Windows Unix-domain socket implementation.
$socketPath = $BuildTemp.Replace('\', '/')
$env:JAVA_OPTS = "-Djdk.net.unixdomain.tmpdir=$socketPath -Djava.io.tmpdir=$socketPath"
function PropertyPath([string]$value) { $value.Replace('\', '/').Replace(':', '\:') }
$properties = "sdk.dir=$(PropertyPath $Sdk)`ncmake.dir=$(PropertyPath $CmakeRoot)`n"
[IO.File]::WriteAllText((Join-Path $projectRoot 'android/local.properties'), $properties)
& (Join-Path $projectRoot 'android/gradlew.bat') -p (Join-Path $projectRoot 'android') "-PeffNdkPath=$Ndk" :app:assembleDebug :app:lintDebug --no-daemon --console=plain
if ($LASTEXITCODE -ne 0) { throw "Android build/checks failed: $LASTEXITCODE" }
$apkMetadata = Get-Content -Raw (Join-Path $projectRoot 'android/app/build/outputs/apk/debug/output-metadata.json') | ConvertFrom-Json
$version = $apkMetadata.elements[0].versionName
$outputDirectory = Join-Path $projectRoot 'dist'
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$destination = Join-Path $outputDirectory "EFF-Android-$version.apk"
Copy-Item -LiteralPath (Join-Path $projectRoot 'android/app/build/outputs/apk/debug/app-debug.apk') -Destination $destination
Get-FileHash -Algorithm SHA256 -LiteralPath $destination
