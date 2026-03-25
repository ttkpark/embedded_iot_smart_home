@echo off
set MSYSTEM=
call "C:\Users\GH\esp\v5.4.2\esp-idf\export.bat"
if %ERRORLEVEL% neq 0 (
    echo EXPORT FAILED
    exit /b 1
)
cd /d "C:\Users\GH\Desktop\embedded_iot_smart_home\node_a_central"
if exist sdkconfig del sdkconfig
if exist build rmdir /s /q build
idf.py set-target esp32
idf.py build
echo === BUILD_RESULT: %ERRORLEVEL% ===
