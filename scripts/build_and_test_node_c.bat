@echo off
REM Node C (node_c_sensor_mock) 빌드 후 COM24로 플래시 & 모니터
REM 사용법: ESP-IDF 5.x CMD 를 연 뒤, 이 스크립트 실행
REM   cd /d c:\Users\GH\Desktop\embedded_iot_smart_home\scripts
REM   build_and_test_node_c.bat

set PROJECT_ROOT=%~dp0..
set NODE=node_c_sensor_mock
set PORT=COM24

echo ========================================
echo [1/3] %NODE% - set-target esp32
echo ========================================
cd /d "%PROJECT_ROOT%\%NODE%"
call idf.py set-target esp32
if errorlevel 1 goto :error

echo.
echo ========================================
echo [2/3] %NODE% - build
echo ========================================
call idf.py build
if errorlevel 1 goto :error

echo.
echo ========================================
echo [3/3] %NODE% - flash and monitor (PORT=%PORT%)
echo ========================================
call idf.py -p %PORT% flash monitor
goto :eof

:error
echo.
echo [FAIL] %NODE% 빌드 또는 플래시 실패.
exit /b 1
