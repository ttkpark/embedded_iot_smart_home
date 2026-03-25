@echo off
set MSYSTEM=
call "C:\Users\GH\esp\v5.4.2\esp-idf\export.bat"
cd /d "C:\Users\GH\Desktop\embedded_iot_smart_home\node_a_central"
idf.py -p COM9 flash
echo === FLASH_RESULT: %ERRORLEVEL% ===
