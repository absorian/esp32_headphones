@echo off
< IDF_PATH.txt (
set /p IDF_PATH=
)

%IDF_PATH%\export.bat

cd ..
