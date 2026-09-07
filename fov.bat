@echo off
rem Sets the field of view for Tiny Eden. Keep this next to winmm.dll and
rem double click it, or run "fov 105" from a command prompt.
setlocal
cd /d "%~dp0"

if not exist "winmm.dll" (
    echo This is in the wrong folder.
    echo.
    echo Put fov.bat in the same folder as winmm.dll, the one holding
    echo CGH-Win64-Shipping.exe.
    goto :fail
)

set "FOV=%~1"
if not defined FOV set /p "FOV=Field of view from 40 to 170 (the game itself uses 90): "
if not defined FOV (
    echo No number given, nothing changed.
    goto :fail
)

rem set /a only understands whole numbers, so anything that does not survive
rem the round trip was a typo. Quieter than letting the game ignore the file.
set /a "NUM=FOV" >nul 2>nul
if not "%NUM%"=="%FOV%" (
    echo "%FOV%" is not a whole number. Type just the digits, like 105.
    goto :fail
)
if %NUM% LSS 40 (
    echo %NUM% is too low. The range is 40 to 170.
    goto :fail
)
if %NUM% GTR 170 (
    echo %NUM% is too high. The range is 40 to 170.
    goto :fail
)

>tiny_eden_fov.txt echo %NUM%

echo Field of view set to %NUM%.
echo.
echo If the game is running the camera changes within a second. If not, it
echo takes effect next time you start it.
echo.
pause
exit /b 0

:fail
echo.
pause
exit /b 1
