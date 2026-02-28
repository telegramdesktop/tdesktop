@echo off
echo Step 1: Log in to GitHub (one-time)
gh auth login

echo.
echo Step 2: Create gittare/lib_ui and push
cd /d "%~dp0.."
powershell -ExecutionPolicy Bypass -File "scripts\create-lib-ui-fork-and-push.ps1"

pause
