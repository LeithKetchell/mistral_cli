@echo off
:: --- Install mistral_cli to System32 (Windows) ---
:: Usage: Run as Administrator

:: Check if the script is run as Administrator
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo Error: This script must be run as Administrator.
    pause
    exit /b 1
)

:: Check if mistral_cli.exe exists in the current directory
if not exist "mistral_cli.exe" (
    echo Error: 'mistral_cli.exe' not found in the current directory.
    echo Please ensure you are in the project directory and have built the binary.
    pause
    exit /b 1
)

:: Check if System32\mistral_cli.exe already exists
if exist "%SystemRoot%\System32\mistral_cli.exe" (
    set /p overwrite="Overwrite? (y/N): "
    if /i "%overwrite%" neq "y" (
        echo Installation aborted.
        pause
        exit /b 0
    )
)

:: Copy the binary to System32
echo Installing mistral_cli to %SystemRoot%\System32...
copy /Y mistral_cli.exe "%SystemRoot%\System32\" >nul

:: Verify the installation
if exist "%SystemRoot%\System32\mistral_cli.exe" (
    echo Success: mistral_cli installed to %SystemRoot%\System32.
    echo You can now run it globally with: mistral_cli
) else (
    echo Error: Failed to install mistral_cli.
    pause
    exit /b 1
)
