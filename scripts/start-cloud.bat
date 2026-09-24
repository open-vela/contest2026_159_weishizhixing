@echo off
cd /d "%~dp0..\cloud"
if exist "..\.venv\Scripts\python.exe" (
  "..\.venv\Scripts\python.exe" app.py
) else if exist ".venv\Scripts\python.exe" (
  ".venv\Scripts\python.exe" app.py
) else (
  python app.py
)
pause
