@echo off
REM Build firmware in Docker (Windows). Usage: scripts\build.bat [F411^|F446]
set BOARD=%1
if "%BOARD%"=="" set BOARD=F411
docker image inspect stm32-build >nul 2>&1 || docker build -t stm32-build docker/
if errorlevel 1 exit /b 1
docker run --rm -v "%CD%":/workspace -w /workspace stm32-build make -C firmware all BOARD=%BOARD%
if errorlevel 1 exit /b 1
echo.
echo OK: firmware\build\firmware.bin
