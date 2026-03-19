@echo off
REM Node B (node_b_actuator) 빌드 후 COM24로 플래시 & 모니터
REM 사용법: ESP-IDF 5.x CMD 를 연 뒤 실행

set PROJECT_ROOT=%~dp0..
set NODE=node_b_actuator
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
