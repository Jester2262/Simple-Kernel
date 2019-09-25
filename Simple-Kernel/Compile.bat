@echo off
:rerun_script
rem
rem =================================
rem
rem VERSION 1.2
rem
rem GCC (MinGW-w64) Kernel64 Windows Compile Script
rem
rem by KNNSpeed
rem
rem =================================
rem
rem The above @echo off disables displaying all of the code you see here in the
rem command line
rem

rem
rem Allow local variables to be created (so that this script doesn't mess with
rem system variables). Also, ENABLEDELAYEDEXPANSION allows local variables to
rem be modified
rem

setlocal ENABLEDELAYEDEXPANSION

rem
rem Globally enable or disable display of commands that are run at compile time
rem

set echo_stat=on

rem
rem CurDir is DOS path
rem CurDir2 is DOS path with /
rem CurDir3 is Unix path with \ before spaces
rem
rem These are needed because GCC is a native UNIX executable, and expects UNIX-
rem like syntax in certain places. Without these, you couldn't use spaces in
rem folder names!
rem

set CurDir=%CD%
set CurDir2=%CurDir:\=/%
set CurDir3=%CurDir2: =\ %
set GCC_FOLDER_NAME=mingw64
set LinkerScript="Linker/LinkerScript64-PE.ld"

rem
rem Move into the Backend folder, where all the magic happens
rem

cd ../Backend

rem
rem First things first, delete the objects list to rebuild it later
rem

del objects.list

rem
rem Need to set path for various DLLs in the GCC folder, otherwise compile will
rem fail. Due to the above setlocal, this is not a persistent PATH edit; it
rem will only apply to this script.
rem
rem Also Need to tell GCC where to find as and ld
rem

set PATH=%CD%\%GCC_FOLDER_NAME%\bin;%PATH%

rem
rem Make temporary ASCII-formatted text files for command interpreter
rem

type "%CurDir%\c_files_windows.txt" > "%CurDir%\c_files_windows_temp.txt"
type "%CurDir%\h_files.txt" > "%CurDir%\h_files_temp.txt"

rem
rem For debugging this script, this echo helps ensure the path is set correctly
rem

rem echo !PATH!
rem pause

rem
rem Create the HFILES variable, which contains the massive set of includes (-I)
rem needed by GCC.
rem
rem Two of the include folders are always included, and they
rem are %CurDir%/inc/ (the user-header directory) and %CurDir%/startup/
rem

set HFILES="%CurDir%\inc\" -I"%CurDir%\startup\"

rem
rem Loop through the h_files.txt file and turn each include directory into -I strings
rem

FOR /F "usebackq tokens=*" %%h IN ("%CurDir%\h_files_temp.txt") DO set HFILES=!HFILES! -I"%%h"

rem
rem Windows uses backslashes. GCC and *nix-based things expect forward slashes.
rem This converts backslashes into forward slashes on Windows.
rem

set HFILES=!HFILES:\=/!

rem
rem This echo is useful for debugging this script, namely to make sure you
rem aren't missing any include directories.
rem

rem echo !HFILES!
rem pause

rem
rem Loop through and compile the backend .c files, which are listed in c_files_windows.txt
rem
rem Note that Windows has 9 command streams, so per this approach stream 9 can be redirected for each process to hold a lock file.
rem https://stackoverflow.com/questions/18758502/wait-for-multiple-applications-run-asynchronously-from-batch-file-to-finish/18759831
rem

@echo %echo_stat%
FOR /F "usebackq tokens=*" %%f IN ("%CurDir%\c_files_windows_temp.txt") DO start /B "Multicore Compile" 9>"%%~df%%~pf%%~nf.templock" "%GCC_FOLDER_NAME%\bin\gcc.exe" -DACPI_USE_LOCAL_CACHE -DACPI_CACHE_T=ACPI_MEMORY_LIST -march=skylake -mavx2 -mcmodel=small -mno-stack-arg-probe -m64 -mno-red-zone -maccumulate-outgoing-args -Og -ffreestanding -fomit-frame-pointer -fno-delete-null-pointer-checks -fno-common -fno-zero-initialized-in-bss -fno-exceptions -fno-stack-protector -fno-stack-check -fno-strict-aliasing -fno-merge-all-constants -fno-merge-constants --std=gnu11 -I!HFILES! -g3 -Wall -Wextra -Wdouble-promotion -Wno-unused-parameter -fmessage-length=0 -ffunction-sections -c -MMD -MP -Wa,-adghlmns="%%~df%%~pf%%~nf.out" -MF"%%~df%%~pf%%~nf.d" -MT"%%~df%%~pf%%~nf.o" -o "%%~df%%~pf%%~nf.o" "%%~ff"
@echo off

rem
rem Compile the .c files in the startup folder (if any exist)
rem

@echo %echo_stat%
FOR %%f IN ("%CurDir2%/startup/*.c") DO start /B "Multicore Compile" 9>"%CurDir%\startup\%%~nf.templock" "%GCC_FOLDER_NAME%\bin\gcc.exe" -march=skylake -mavx2 -mcmodel=small -mno-stack-arg-probe -m64 -mno-red-zone -maccumulate-outgoing-args -O3 -ffreestanding -fomit-frame-pointer -fno-delete-null-pointer-checks -fno-common -fno-zero-initialized-in-bss -fno-exceptions -fno-stack-protector -fno-stack-check -fno-strict-aliasing -fno-merge-all-constants -fno-merge-constants --std=gnu11 -I!HFILES! -g3 -Wall -Wextra -Wdouble-promotion -Wpedantic -fmessage-length=0 -ffunction-sections -c -MMD -MP -Wa,-adghlmns="%CurDir2%/startup/%%~nf.out" -MF"%CurDir2%/startup/%%~nf.d" -MT"%CurDir2%/startup/%%~nf.o" -o "%CurDir2%/startup/%%~nf.o" "%CurDir2%/startup/%%~nf.c"
@echo off

rem
rem Compile the .S files in the startup folder (Any assembly files needed to
rem initialize the system)
rem

@echo %echo_stat%
FOR %%f IN ("%CurDir2%/startup/*.S") DO start /B "Multicore Compile" 9>"%CurDir%\startup\%%~nf.templock" "%GCC_FOLDER_NAME%\bin\gcc.exe" -march=skylake -mavx2 -mcmodel=small -mno-stack-arg-probe -m64 -mno-red-zone -maccumulate-outgoing-args -Og -ffreestanding -fomit-frame-pointer -fno-delete-null-pointer-checks -fno-common -fno-zero-initialized-in-bss -fno-exceptions -fno-stack-protector -fno-stack-check -fno-strict-aliasing -fno-merge-all-constants -fno-merge-constants --std=gnu11 -I!HFILES! -g3 -Wall -Wextra -Wdouble-promotion -Wpedantic -fmessage-length=0 -ffunction-sections -c -MMD -MP -Wa,-adghlmns="%CurDir2%/startup/%%~nf.out" -MF"%CurDir2%/startup/%%~nf.d" -MT"%CurDir2%/startup/%%~nf.o" -o "%CurDir2%/startup/%%~nf.o" "%CurDir2%/startup/%%~nf.S"
@echo off

rem
rem Compile user .c files
rem

@echo %echo_stat%
FOR %%f IN ("%CurDir2%/src/*.c") DO start /B "Multicore Compile" 9>"%CurDir%\src\%%~nf.templock" "%GCC_FOLDER_NAME%\bin\gcc.exe" -march=skylake -mavx2 -mcmodel=small -mno-stack-arg-probe -m64 -mno-red-zone -maccumulate-outgoing-args -Og -ffreestanding -fomit-frame-pointer -fno-delete-null-pointer-checks -fno-common -fno-zero-initialized-in-bss -fno-exceptions -fno-stack-protector -fno-stack-check -fno-strict-aliasing -fno-merge-all-constants -fno-merge-constants --std=gnu11 -I!HFILES! -g3 -Wall -Wextra -Wdouble-promotion -Wpedantic -fmessage-length=0 -ffunction-sections -c -MMD -MP -Wa,-adghlmns="%CurDir2%/src/%%~nf.out" -MF"%CurDir2%/src/%%~nf.d" -MT"%CurDir2%/src/%%~nf.o" -o "%CurDir2%/src/%%~nf.o" "%CurDir2%/src/%%~nf.c"
@echo off

rem
rem Wait for compilation to complete
rem Compilation will complete when the templock files are no longer in use, which will be when each process started by "start" finishes.
rem

echo.
echo Waiting for compilation to complete...
echo.

:Wait_Backend
1>nul 2>nul timeout 1 /nobreak
FOR /F "usebackq tokens=*" %%f IN ("%CurDir%\c_files_windows_temp.txt") DO ( (call ) 9>"%%~df%%~pf%%~nf.templock" || goto :Wait_Backend ) 2>nul

:Wait_startup
1>nul 2>nul timeout 1 /nobreak
FOR %%f IN ("%CurDir%\startup\*.templock") DO ( (call ) 9>"%CurDir%\startup\%%~nf.templock" || goto :Wait_startup ) 2>nul

:Wait_src
1>nul 2>nul timeout 1 /nobreak
FOR %%f IN ("%CurDir%\src\*.templock") DO ( (call ) 9>"%CurDir%\src\%%~nf.templock" || goto :Wait_src ) 2>nul

rem
rem Create OBJECTS variable, whose sole purpose is to allow conversion of
rem backslashes into forward slashes in Windows
rem

set OBJECTS=

rem
rem Create the objects.list file, which contains properly-formatted (i.e. has
rem forward slashes) locations of compiled Backend .o files
rem

FOR /F "usebackq tokens=*" %%f IN ("%CurDir%\c_files_windows_temp.txt") DO (set OBJECTS="%%~df%%~pf%%~nf.o" & set OBJECTS=!OBJECTS:\=/! & set OBJECTS=!OBJECTS: =\ ! & set OBJECTS=!OBJECTS:"\ \ ="! & echo !OBJECTS! >> objects.list)

rem
rem Add compiled .o files from the startup directory to objects.list
rem

FOR %%f IN ("%CurDir2%/startup/*.o") DO echo "%CurDir3%/startup/%%~nxf" >> objects.list

rem
rem Add compiled user .o files to objects.list
rem

FOR %%f IN ("%CurDir2%/src/*.o") DO echo "%CurDir3%/src/%%~nxf" >> objects.list

rem
rem Delete ASCII-formatted temporary files
rem

del "%CurDir%\c_files_windows_temp.txt"
del "%CurDir%\h_files_temp.txt"

rem
rem Link the object files using all the objects in objects.list and an optional
rem linker script (it would go in the Backend/Linker directory) to generate the
rem output binary, which is called "Kernel64.exe"
rem
rem NOTE: Linkerscripts may be needed for bigger projects
rem

rem MinGW-w64 has a pretty serious issue where it strips relocation tables for PE images by default. Using -Wl,--dynamicbase,--export-all-symbols prevents it from doing so.
rem This is also needed for ASLR compatibility, as ASLR and kernel loading are basically the same...
rem This is why many UEFI build systems using MinGW-w64 pass -Wl,-dll to the linker, since DLLs are by definition relocatable and thus need to keep the reloc table.
rem
rem See these links for more details:
rem https://stackoverflow.com/questions/19451652/mingw-relocation-table
rem https://insights.sei.cmu.edu/cert/2018/08/when-aslr-is-not-really-aslr---the-case-of-incorrect-assumptions-and-bad-defaults.html
rem https://sourceware.org/bugzilla/show_bug.cgi?id=19011
rem https://sourceforge.net/p/mingw-w64/mailman/message/31034877/

rem "%GCC_FOLDER_NAME%\bin\gcc.exe" -s -T%LinkerScript% -nostdlib -Wl,-e,kernel_main -Wl,--dynamicbase,--export-all-symbols -Wl,--subsystem,10 -Wl,-Map=output.map -Wl,--gc-sections -o "Kernel64.exe" @"objects.list"
@echo on
"%GCC_FOLDER_NAME%\bin\gcc.exe" -s -nostdlib -Wl,-e,kernel_main -Wl,--dynamicbase,--export-all-symbols -Wl,--subsystem,10 -Wl,-Map=output.map -Wl,--gc-sections -o "Kernel64.exe" @"objects.list"
@echo off
rem Remove -s in the above command to keep debug symbols in the output binary.

rem
rem Output the program size
rem

echo.
echo Generating binary and Printing size information:
echo.
rem "%GCC_FOLDER_NAME%\bin\objcopy.exe" -O binary "program.exe" "program.bin"
"%GCC_FOLDER_NAME%\bin\size.exe" "Kernel64.exe"
echo.

set /P UPL="Cleanup, recompile, or done? [c for cleanup, r for recompile, any other key for done] "

echo.
echo **********************************************************
echo.

if /I "%UPL%"=="C" endlocal & .\Cleanup.bat
if /I "%UPL%"=="R" endlocal & goto rerun_script

rem
rem Return to the folder started from and exit
rem
rem No more need for local variables
rem

endlocal
