@echo off
REM Flash: copy firmware.bin to the NODE_xxxx drive that appears when Nucleo is plugged in.
if not exist firmware\build\firmware.bin (
  echo Run scripts\build.bat first
  exit /b 1
)
set DRV=
for /f %%d in ('powershell -NoProfile -Command "(Get-Volume | Where-Object { $_.FileSystemLabel -like 'NODE_*' } | Select-Object -First 1).DriveLetter"') do set DRV=%%d
if "%DRV%"=="" (
  echo NODE_F411RE drive not found. Check USB cable - it must be a data cable.
  exit /b 1
)
copy /Y firmware\build\firmware.bin %DRV%:\ >nul
echo Flashed to drive %DRV%: - the board restarts by itself.
