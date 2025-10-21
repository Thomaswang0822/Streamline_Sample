@echo off
setlocal enabledelayedexpansion

cd _bin
echo This script is used for ensuring frame numbers matching with captured images filenames. 
set EXE_PATH=StreamlineSample.exe
rem IMPORTANT NOTE: Batch script executes commands in Windows command prompt which can't recognize "/" as path separator.
set OUTPUT_ROOT=..\media\TEST_SCENE\screenshots
set RUN_COUNT=5

rem Check for xxhash Python module
python -c "import xxhash" 2>nul
if %errorlevel% neq 0 (
    echo ERROR: xxhash Python module not found!
    echo Please install it with: pip install xxhash, then rerun this script.
    pause
    exit /b 1
)

echo Command to run: %EXE_PATH% -EnableHack -Identifier FG_TEST -RenderResolution 1 -ParseJitter -HackPaths "../media/TEST_SCENE/NPP_JI" -StoreOutput -OutputMaxCount 10 -OutputPath "%OUTPUT_ROOT%"
echo Total runs: %RUN_COUNT%
rem Let user press y/n to confirm the command and total runs.
choice /c YN /m "Run with these parameters? Double check OUTPUT_ROOT=%OUTPUT_ROOT% is what you normally pass to last arg -OutputPath."
if %errorlevel% equ 2 (
    echo Aborted by user
    exit /b 0
)

for /L %%i in (1,1,%RUN_COUNT%) do (
    echo.
    echo ===== Starting Run %%i/%RUN_COUNT% =====
    
    rem Create run-specific subfolder
    set RUN_FOLDER=%OUTPUT_ROOT%\output_run_%%i
    if not exist !RUN_FOLDER! (
        echo !RUN_FOLDER! does not exist, create it.
        mkdir !RUN_FOLDER!
    ) else (
        rem Clear existing files if folder exists
        del /Q !RUN_FOLDER!/*.exr 2>nul
    )
    
    rem Execute the program
    %EXE_PATH% -EnableHack -Identifier FG_TEST -RenderResolution 1 -ParseJitter -HackPaths "../media/TEST_SCENE/NPP_JI" -StoreOutput -OutputMaxCount 10 -OutputPath "%OUTPUT_ROOT%"

    rem Move and verify captured files
    set FILES_MOVED=0
    for %%f in (%OUTPUT_ROOT%\*.exr) do (
        move /Y "%%f" "!RUN_FOLDER!" >nul
        set /a FILES_MOVED+=1
    )
    
    echo Moved !FILES_MOVED! EXR files to output_run_%%i
    timeout /t 2 /nobreak >nul
)

cd ../media
set PY_SCRIPT=check_duplicate.py
set REL_ROOT=TEST_SCENE/screenshots
rem For each subfolder in REL_ROOT, run the python script to verify no duplicate frame contents.
echo.
echo ===== Checking for duplicate frames =====

for /L %%i in (1,1,%RUN_COUNT%) do (
    set RUN_FOLDER=%REL_ROOT%\output_run_%%i
    echo Checking duplicates in !RUN_FOLDER!...
    python %PY_SCRIPT% "!RUN_FOLDER!"
    if %errorlevel% neq 0 (
        echo ERROR: Duplicates found in output_run_%%i
        exit /b 1
    )
)

echo.
echo ===== All runs completed =====
endlocal