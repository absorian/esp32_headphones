@echo off
for /f %%i in ('where ..\build:*.elf') do set elf=%%i
xtensa-esp32-elf-addr2line --exe %elf%
