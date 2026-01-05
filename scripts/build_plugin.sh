#!/usr/bin/env bash
set -e
BUILD_DIR=build
mkdir -p $BUILD_DIR
cmake -S plugin -B $BUILD_DIR -DCMAKE_BUILD_TYPE=Release
cmake --build $BUILD_DIR --config Release -- -j

# copy any plugin binaries to ./plugin/ (not overwriting existing)
mkdir -p plugin
for f in $(find $BUILD_DIR -maxdepth 3 -type f -name "*.dll" -o -name "*.so" -o -name "*.dylib" 2>/dev/null); do
  echo "Found $f"
  cp -n "$f" plugin/ || true
done

echo "Build finished. Plugin binaries (if built) copied to ./plugin/"