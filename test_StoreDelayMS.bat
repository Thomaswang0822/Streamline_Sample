@echo off
setlocal enabledelayedexpansion

cd _bin
echo This script is used for ensuring the current StoreDelayMS give a consistent frame number matching with captured images. 
set EXE_PATH=StreamlineSample.exe
set OUTPUT_ROOT=D:\haoxuan.wang\Streamline_Sample\media\TEST_SCENE\screenshots
set RUN_COUNT=10
echo Total runs: %RUN_COUNT%

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
        del /Q !RUN_FOLDER!\*.exr 2>nul
    )
    
    rem Execute the program
    rem %EXE_PATH% -EnableHack -Identifier FG_TEST -RenderResolution 1 -ParseJitter -HackPaths "../media/TEST_SCENE/NPP_JI" -StoreOutput -OutputMaxCount 10 -OutputPath "../media/TEST_SCENE/screenshots"

    rem Move and verify captured files
    set FILES_MOVED=0
    for %%f in (%OUTPUT_ROOT%\*.exr) do (
        move /Y "%%f" "!RUN_FOLDER!" >nul
        set /a FILES_MOVED+=1
    )
    
    echo Moved !FILES_MOVED! EXR files to output_run_%%i
    timeout /t 2 /nobreak >nul
)

echo.
echo ===== All runs completed =====
endlocal