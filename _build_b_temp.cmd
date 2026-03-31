set MSYSTEM=
set MSYS=
set PYTHONUTF8=1
call C:\Users\parkg\esp\v5.4.2\esp-idf\export.bat
cd /d E:\바탕화면\archived project\embedded_iot_smart_home\node_b_actuator
idf.py build
idf.py -p COM4 flash
pause
