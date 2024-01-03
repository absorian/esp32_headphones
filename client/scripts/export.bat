@echo off
< ADF_IDF_PATHS.txt (
set /p ADF_PATH=
set /p IDF_PATH=
)

%IDF_PATH%\export.bat

cd ..
