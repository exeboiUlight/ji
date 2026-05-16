@echo off
echo ========================================
echo Building PankLang VSCode Extension...
echo ========================================

cd /d "%~dp0"

where node >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Node.js is not installed
    exit /b 1
)

echo.
echo [1/3] Cleaning old builds...
del /q ji-lang-*.vsix 2>nul

echo.
echo [2/3] Packaging extension...
npx vsce package >nul 2>&1

echo.
echo ========================================
echo SUCCESS: Extension built successfully!
echo Output: ji-lang-1.0.0.vsix
echo ========================================
echo.
echo To install: code --install-extension ji-lang-1.0.0.vsix

pause