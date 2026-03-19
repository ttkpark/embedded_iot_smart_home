# ESP-IDF 환경 설정 가이드 스크립트
# 1) 가상환경이 없을 때: install.ps1 먼저 실행
# 2) 그 다음 export.ps1 실행

$idfPath = "C:\Users\GH\esp\v5.4.2\esp-idf"
$venvPath = "C:\Users\GH\.espressif\python_env\idf5.4_py3.12_env\Scripts\python.exe"

if (-not (Test-Path $venvPath)) {
    Write-Host "ESP-IDF Python 가상환경이 없습니다. install.ps1을 실행합니다..." -ForegroundColor Yellow
    & "$idfPath\install.ps1"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Host "export.ps1 실행 중..." -ForegroundColor Green
& "$idfPath\export.ps1"
