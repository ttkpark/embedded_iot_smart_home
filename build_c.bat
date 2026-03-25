@echo off
set MSYSTEM=
call "C:\Users\GH\esp\v5.4.2\esp-idf\export.bat"
cd /d "C:\Users\GH\Desktop\embedded_iot_smart_home\node_c_sensor_mock"
idf.py build
if %ERRORLEVEL% neq 0 (echo === BUILD FAILED === & exit /b 1)
idf.py -p COM9 flash
echo === FLASH_RESULT: %ERRORLEVEL% ===
