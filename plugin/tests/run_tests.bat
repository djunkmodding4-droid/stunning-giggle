@echo off
REM Build in a separate build dir then run ctest
mkdir -p build
cd build
cmake ..
cmake --build . --config Release
ctest -V
