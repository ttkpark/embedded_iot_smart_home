# ESP-IDF 환경 로드 후 인자로 받은 명령 실행
# 사용 예:
#   .\run_with_idf.ps1 "cd node_c_sensor_mock; idf.py build"
#   .\run_with_idf.ps1 "cd node_c_sensor_mock; idf.py -p COM24 flash monitor"

$IDF_EXPORT = "C:\Users\GH\esp\v5.4.2\esp-idf\export.ps1"
$PROJECT_ROOT = Split-Path -Parent $PSScriptRoot

if (-not (Test-Path $IDF_EXPORT)) {
    Write-Error "ESP-IDF export.ps1 not found: $IDF_EXPORT"
    exit 1
}

. $IDF_EXPORT
Set-Location $PROJECT_ROOT

if ($args.Count -gt 0) {
    Invoke-Expression ($args -join " ")
} else {
    Write-Host "Usage: .\run_with_idf.ps1 ""cd node_c_sensor_mock; idf.py build"""
    Write-Host "   or: .\run_with_idf.ps1 ""cd node_c_sensor_mock; idf.py -p COM24 flash monitor"""
}
