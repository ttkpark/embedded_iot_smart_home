@echo off
set MSYSTEM=
call "C:\Users\parkg\esp\v5.4.2\esp-idf\export.bat"
if %ERRORLEVEL% neq 0 (
    echo EXPORT FAILED
    exit /b 1
)
cd /d "E:\바탕화면\archived project\embedded_iot_smart_home\node_a_central"
idf.py build
echo === BUILD_RESULT: %ERRORLEVEL% ===
