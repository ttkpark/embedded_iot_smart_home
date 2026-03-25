@echo off
set MSYSTEM=
call "C:\Users\GH\esp\v5.4.2\esp-idf\export.bat"

echo === Building Node A ===
cd /d "C:\Users\GH\Desktop\embedded_iot_smart_home\node_a_central"
idf.py build
if %ERRORLEVEL% neq 0 (echo NODE_A BUILD FAILED & exit /b 1)
idf.py -p COM6 flash
if %ERRORLEVEL% neq 0 (echo NODE_A FLASH FAILED & exit /b 1)
echo === Node A OK ===

echo === Building Node B ===
cd /d "C:\Users\GH\Desktop\embedded_iot_smart_home\node_b_actuator"
idf.py build
if %ERRORLEVEL% neq 0 (echo NODE_B BUILD FAILED & exit /b 1)
idf.py -p COM24 flash
if %ERRORLEVEL% neq 0 (echo NODE_B FLASH FAILED & exit /b 1)
echo === Node B OK ===

echo === Building Node C ===
cd /d "C:\Users\GH\Desktop\embedded_iot_smart_home\node_c_sensor_mock"
idf.py build
if %ERRORLEVEL% neq 0 (echo NODE_C BUILD FAILED & exit /b 1)
idf.py -p COM9 flash
if %ERRORLEVEL% neq 0 (echo NODE_C FLASH FAILED & exit /b 1)
echo === Node C OK ===

echo === ALL 3 NODES DONE ===
