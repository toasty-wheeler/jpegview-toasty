# Builds the 64-bit MSI installer from an existing Release x64 build, with the command line tools of the WiX Toolset v3
# (candle.exe, light.exe, WixUIExtension.dll, e.g. from wix314-binaries.zip of https://github.com/wixtoolset/wix3/releases).
# Use this instead of the JPEGView.Setup project when WiX is not installed in Visual Studio.
#
# Build JPEGView (Release x64, with SolutionDir set so the post-build step copies the config files and DLLs)
# and JPEGViewSettings (Release) first.
#
# Example:  .\jpegview-build-msi.ps1 -WixBin C:\Tools\wix314 -Version 1.4.0 -OutDir C:\Temp\release
param(
	[Parameter(Mandatory = $true)][string]$WixBin,
	[Parameter(Mandatory = $true)][string]$Version,
	[Parameter(Mandatory = $true)][string]$OutDir
)
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path "$PSScriptRoot\..\..").Path
$projectDir = "$root\src\JPEGView\"
$targetDir = "$root\src\JPEGView\bin\x64\Release\"
$setupDir = "$root\src\JPEGView.Setup"
$objDir = "$setupDir\obj\x64\Release"

foreach ($file in "$targetDir\JPEGView.exe", "$targetDir\WICLoader.dll", "$targetDir\JPEGView.ini",
		"$root\src\JPEGViewSettings\bin\Release\net48\JPEGViewSettings.exe") {
	if (-not (Test-Path $file)) { throw "Missing build output: $file" }
}
New-Item -ItemType Directory -Force $objDir, $OutDir | Out-Null

# the WiX sources use relative paths for the bitmaps and the license (res\...)
Push-Location $setupDir
try {
	& "$WixBin\candle.exe" -nologo -arch x64 -dPlatform=x64 "-dJPEGView.ProjectDir=$projectDir" "-dJPEGView.TargetDir=$targetDir" `
		"-dWICLoader.TargetDir=$targetDir" -ext "$WixBin\WixUIExtension.dll" -out "$objDir\Product.wixobj" Product.wxs
	if ($LASTEXITCODE -ne 0) { throw "candle failed ($LASTEXITCODE)" }

	# ICE61: AllowSameVersionUpgrades is used on purpose (see Product.wxs)
	$msi = Join-Path $OutDir "JPEGView64_en-us_$Version.msi"
	& "$WixBin\light.exe" -nologo -ext "$WixBin\WixUIExtension.dll" -cultures:en-us -loc Product_en-us.wxl -sice:ICE61 `
		-out $msi "$objDir\Product.wixobj"
	if ($LASTEXITCODE -ne 0) { throw "light failed ($LASTEXITCODE)" }
	Write-Host "Built $msi"
} finally {
	Pop-Location
}
