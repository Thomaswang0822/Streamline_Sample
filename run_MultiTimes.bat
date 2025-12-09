@echo off
setlocal enabledelayedexpansion

echo This script is used for ensuring frame numbers matching with captured images filenames. 
set RUN_COUNT=5
set SCRIPT_NAME=.\test_OneScene.bat

for /L %%i in (1,1,%RUN_COUNT%) do (
    echo.
    echo ===== Starting Run %%i/%RUN_COUNT% =====
    
    rem Execute the test_OneScene.bat script with automatic "Y" input
    echo Y | %SCRIPT_NAME%
)

echo.
echo ===== All scenes completed =====
endlocal