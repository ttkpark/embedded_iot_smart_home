@echo off
set MSYSTEM=
call "C:\Users\parkg\esp\v5.4.2\esp-idf\export.bat"
cd /d "E:\바탕화면\archived project\embedded_iot_smart_home\node_b_actuator"
idf.py build
if %ERRORLEVEL% neq 0 (
    echo === BUILD FAILED ===
    exit /b 1
)
idf.py -p COM4 flash
echo === FLASH_RESULT: %ERRORLEVEL% ===
