param(
  [Parameter(Mandatory=$true)][string]$BinaryPath,
  [string]$Out = "runtime.zip"
)
$pwd = Get-Location
$td = New-Item -ItemType Directory -Force -Path (Join-Path $pwd.Path "_tmp_pack")
Copy-Item -Path "info.txt" -Destination $td -Force
Copy-Item -Path "main.lua" -Destination $td -Force
New-Item -ItemType Directory -Path (Join-Path $td.FullName "plugin") | Out-Null
Copy-Item -Path $BinaryPath -Destination (Join-Path $td.FullName "plugin") -Force
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory($td.FullName, (Join-Path $pwd.Path $Out))
Write-Host "Created $Out"
Remove-Item -Recurse -Force $td.FullName
