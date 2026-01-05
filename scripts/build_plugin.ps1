$buildDir = "build"
if (-not (Test-Path $buildDir)) { New-Item -ItemType Directory $buildDir | Out-Null }
cmake -S plugin -B $buildDir -DCMAKE_BUILD_TYPE=Release
cmake --build $buildDir --config Release

# copy built binaries to plugin folder if present
if (-not (Test-Path plugin)) { New-Item -ItemType Directory plugin | Out-Null }
Get-ChildItem -Path $buildDir -Recurse -Include *.dll,*.so,*.dylib -File | ForEach-Object {
  Copy-Item -Path $_.FullName -Destination plugin -Force -ErrorAction SilentlyContinue
}
Write-Host "Build finished. Plugin binaries copied to ./plugin/ (if any were found)"