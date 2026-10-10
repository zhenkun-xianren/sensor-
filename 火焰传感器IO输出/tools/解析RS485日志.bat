@echo off
chcp 65001 >nul
setlocal
where py >nul 2>nul
if %errorlevel%==0 (
    py -3 "%~dp0parse_rs485_log.py" %*
) else (
    python "%~dp0parse_rs485_log.py" %*
)
echo.
pause
