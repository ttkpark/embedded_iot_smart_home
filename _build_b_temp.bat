@echo off
chcp 65001 >nul
set "MSYSTEM="
set "MSYS="
set "PYTHONUTF8=1"
call "C:\Users\parkg\esp\v5.4.2\esp-idf\export.bat" >nul 2>&1
cd /d "E:\바탕화면\archived project\embedded_iot_smart_home\node_b_actuator"
echo === BUILDING NODE B ===
idf.py build
if %ERRORLEVEL% neq 0 (
    echo === BUILD FAILED ===
    exit /b 1
)
echo === FLASHING TO COM4 ===
idf.py -p COM4 flash
echo === DONE (RESULT: %ERRORLEVEL%) ===
