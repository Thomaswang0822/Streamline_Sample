@echo off
setlocal enabledelayedexpansion

cd _bin
echo This script is used for ensuring frame numbers matching with captured images filenames. 
set EXE_PATH=StreamlineSample.exe
rem IMPORTANT NOTE: Batch script executes commands in Windows command prompt which can't recognize "/" as path separator.
set OUTPUT_ROOT=..\media\TEST_SCENE\screenshots
set BATCH_COUNT=4

rem Check for xxhash Python module
python -c "import xxhash" 2>nul
if %errorlevel% neq 0 (
    echo ERROR: xxhash Python module not found!
    echo Please install it with: pip install xxhash, then rerun this script.
    pause
    exit /b 1
)

echo Command to run: %EXE_PATH% -EnableHack -Identifier FG_TEST -RenderResolution 1 -ParseJitter -HackPaths "../media/TEST_SCENE/NPP_JI" -StoreOutput -BatchIndex i -OutputPath "%OUTPUT_ROOT%"
echo BATCH_COUNT: %BATCH_COUNT%. Please confirm command to run and total batch number (BATCH_COUNT * 15 >= total input frames)
rem Let user press y/n to confirm the command and total runs.
choice /c YN /m "Run with these parameters? Double check OUTPUT_ROOT=%OUTPUT_ROOT% is what you normally pass to last arg -OutputPath."
if %errorlevel% equ 2 (
    echo Aborted by user
    exit /b 0
)

set /a END_INDEX=BATCH_COUNT-1
for /L %%i in (0, 1, %END_INDEX%) do (
    echo ===== Batch Index %%i =====
    
    rem Execute the program
    %EXE_PATH% -EnableHack -Identifier FG_TEST -RenderResolution 1 -ParseJitter -HackPaths "../media/TEST_SCENE/NPP_JI" -StoreOutput -BatchIndex %%i -OutputPath "%OUTPUT_ROOT%"
)

cd ../media
set PY_SCRIPT=check_duplicate.py
set RUN_FOLDER=TEST_SCENE/screenshots

echo.
echo ===== Checking for duplicate frames in !RUN_FOLDER! =====

python %PY_SCRIPT% "!RUN_FOLDER!"
if %errorlevel% neq 0 (
    echo ERROR: Duplicates found in output_run_%%i
    exit /b 1
)

echo.
echo ===== One scene completed =====
endlocal