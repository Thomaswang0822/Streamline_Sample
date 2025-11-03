@echo off
setlocal enabledelayedexpansion

cd _bin
echo This script is used for ensuring frame numbers matching with captured images filenames. 
set EXE_PATH=StreamlineSample.exe

rem Check for xxhash Python module
call python -c "import xxhash" 2>nul
if %errorlevel% neq 0 (
    echo ERROR: xxhash Python module not found!
    echo Please install it with: pip install xxhash, then rerun this script.
    pause
    exit /b 1
)

rem Set default values first
rem IMPORTANT NOTE: Batch script executes commands in Windows command prompt which can't recognize "/" as path separator.
set Identifier=Script_Test
set OUTPUT_ROOT=..\media\TEST_SCENE\screenshots
set INPUT_ROOT=..\media\TEST_SCENE\NPP_JI

rem Override with command-line arguments if provided
if not "%~1"=="" set Identifier=%~1
if not "%~2"=="" set INPUT_ROOT=%~2
if not "%~3"=="" set OUTPUT_ROOT=%~3

set INPUT_COUNT=0
for %%x in (%INPUT_ROOT%\*.exr) do (
    set /a INPUT_COUNT+=1
)
rem 15 is the batch size, batch count is ceiling division
set /a BATCH_COUNT=(%INPUT_COUNT% + 14) / 15

rem Let user press y/n to confirm the command and total runs.
echo Using parameters:
echo Identifier: %Identifier%
echo Input path: %INPUT_ROOT%
echo Output path: %OUTPUT_ROOT%
echo INPUT_COUNT: %INPUT_COUNT%, BATCH_COUNT: %BATCH_COUNT%
echo Command to run: %EXE_PATH% -EnableHack -Identifier %Identifier% -RenderResolution 1 -ParseJitter -HackPaths "%INPUT_ROOT%" -StoreOutput -BatchIndex i -OutputPath "%OUTPUT_ROOT%"
echo Please confirm command to run and total batch number (BATCH_COUNT * 15 >= total input frames)

choice /c YN /m "Run with these parameters? Double check OUTPUT_ROOT=%OUTPUT_ROOT% is what you normally pass to last arg -OutputPath."
if %errorlevel% equ 2 (
    echo Aborted by user
    exit /b 0
)

set /a END_INDEX=BATCH_COUNT-1
for /L %%i in (0, 1, %END_INDEX%) do (
    echo ===== Batch Index %%i =====
    
    rem Execute the program
    %EXE_PATH% -EnableHack -Identifier %Identifier% -RenderResolution 1 -ParseJitter -HackPaths "%INPUT_ROOT%" -StoreOutput -BatchIndex %%i -OutputPath "%OUTPUT_ROOT%"
)

rem Confirm total numbers first
set OUTPUT_COUNT=0
for %%x in (%OUTPUT_ROOT%\*.exr) do (
    set /a OUTPUT_COUNT+=1
)
if %OUTPUT_COUNT% neq %INPUT_COUNT% (
    echo ERROR: OUTPUT_COUNT %OUTPUT_COUNT% less than expected %INPUT_COUNT%!
)

rem Then go ahead to call python duplicate check helper.
cd ../media
set PY_SCRIPT=check_duplicate.py
set RUN_FOLDER=TEST_SCENE/screenshots

echo.
echo ===== Checking for duplicate frames in !RUN_FOLDER! =====

call python %PY_SCRIPT% "!RUN_FOLDER!"
if %errorlevel% neq 0 (
    echo ERROR: Duplicates found in output_run_%%i
    exit /b 1
)

echo.
echo ===== One scene completed =====
endlocal