#!/bin/bash
#
# =================================
#
# RELEASE VERSION 1.2
#
# GCC/Clang Kernel64 Mac Compile Script
#
# by KNNSpeed
#
# =================================
#

#
# set +v disables displaying all of the code you see here in the command line
#

set +v

#
# Convert Windows-style line endings (CRLF) to Unix-style line endings (LF)
#

perl -pi -e 's/\r\n/\n/g' c_files_mac.txt
perl -pi -e 's/\r\n/\n/g' h_files.txt

#
# Set various paths needed for portable compilation
#

CurDir=$PWD
LinkerScript="Linker/LinkerScript64-MACH.ld"

#
# These help with debugging the PATH to make sure it is set correctly
#

# echo $PATH
# read -n1 -r -p "Press any key to continue..."

#
# Move into the Backend folder, where all the magic happens
#

cd ../Backend

#
# First things first, delete the objects list to rebuild it later
#

rm objects.list

#
# Create the HFILES variable, which contains the massive set of includes (-I)
# needed by GCC.
#
# Two of the include folders are always included, and they
# are $CurDir/inc/ (the user-header directory) and $CurDir/startup/
#

HFILES=-I$CurDir/inc/\ -I$CurDir/startup/

#
# Loop through the h_files.txt file and turn each include directory into -I strings
#

while read h; do
  HFILES=$HFILES\ -I$h
done < $CurDir/h_files.txt

#
# These are useful for debugging this script, namely to make sure you aren't
# missing any include directories.
#

# echo $HFILES
# read -n1 -r -p "Press any key to continue..."

#
# Loop through and compile the backend .c files, which are listed in c_files_mac.txt
#

set -v
while read f; do
  echo "gcc" -DACPI_USE_LOCAL_CACHE -DACPI_CACHE_T=ACPI_MEMORY_LIST -march=skylake -mavx2 -m64 -mno-red-zone -Og -ffreestanding -fpie -fomit-frame-pointer -fno-delete-null-pointer-checks -fno-common -fno-zero-initialized-in-bss -fno-stack-protector $HFILES -g3 -Wall -Wextra -Wdouble-promotion -Wno-unused-parameter -fmessage-length=0 -ffunction-sections -c -MMD -MP -MF"${f%.*}.d" -MT"${f%.*}.o" -o "${f%.*}.o" "$f"
  "gcc" -DACPI_USE_LOCAL_CACHE -DACPI_CACHE_T=ACPI_MEMORY_LIST -march=skylake -mavx2 -m64 -mno-red-zone -Og -ffreestanding -fpie -fomit-frame-pointer -fno-delete-null-pointer-checks -fno-common -fno-zero-initialized-in-bss -fno-stack-protector $HFILES -g3 -Wall -Wextra -Wdouble-promotion -Wno-unused-parameter -fmessage-length=0 -ffunction-sections -c -MMD -MP -MF"${f%.*}.d" -MT"${f%.*}.o" -o "${f%.*}.o" "$f" &
done < $CurDir/c_files_mac.txt
set +v

#
# Compile the .c files in the startup folder
#

set -v
for f in $CurDir/startup/*.c; do
  echo "gcc" -march=skylake -mavx2 -m64 -mno-red-zone -O3 -ffreestanding -fpie -fomit-frame-pointer -fno-delete-null-pointer-checks -fno-common -fno-zero-initialized-in-bss -fno-stack-protector $HFILES -g3 -Wall -Wextra -Wdouble-promotion -Wpedantic -fmessage-length=0 -ffunction-sections -c -MMD -MP -MF"${f%.*}.d" -MT"${f%.*}.o" -o "${f%.*}.o" "${f%.*}.c"
  "gcc" -march=skylake -mavx2 -m64 -mno-red-zone -O3 -ffreestanding -fpie -fomit-frame-pointer -fno-delete-null-pointer-checks -fno-common -fno-zero-initialized-in-bss -fno-stack-protector $HFILES -g3 -Wall -Wextra -Wdouble-promotion -Wpedantic -fmessage-length=0 -ffunction-sections -c -MMD -MP -MF"${f%.*}.d" -MT"${f%.*}.o" -o "${f%.*}.o" "${f%.*}.c" &
done
set +v

#
# Compile the .S files in the startup folder (Any assembly files needed to
# initialize the system)
#

#set -v
#for f in $CurDir/startup/*.S; do
#  echo "as" -64 -I"$CurDir/inc/" -g -o "${f%.*}.o" "${f%.*}.S"
#  "as" -64 -I"$CurDir/inc/" -g -o "${f%.*}.o" "${f%.*}.S" &
#done
#set +v

set -v
for f in $CurDir/startup/*.S; do
  echo "gcc" -march=skylake -mavx2 -m64 -mno-red-zone -Og -ffreestanding -fpie -fomit-frame-pointer -fno-delete-null-pointer-checks -fno-common -fno-zero-initialized-in-bss -fno-stack-protector $HFILES -g3 -Wall -Wextra -Wdouble-promotion -Wpedantic -fmessage-length=0 -ffunction-sections -c -MMD -MP -MF"${f%.*}.d" -MT"${f%.*}.o" -o "${f%.*}.o" "${f%.*}.S"
  "gcc" -march=skylake -mavx2 -m64 -mno-red-zone -Og -ffreestanding -fpie -fomit-frame-pointer -fno-delete-null-pointer-checks -fno-common -fno-zero-initialized-in-bss -fno-stack-protector $HFILES -g3 -Wall -Wextra -Wdouble-promotion -Wpedantic -fmessage-length=0 -ffunction-sections -c -MMD -MP -MF"${f%.*}.d" -MT"${f%.*}.o" -o "${f%.*}.o" "${f%.*}.S" &
done
set +v

#
# Compile user .c files
#

set -v
for f in $CurDir/src/*.c; do
  echo "gcc" -march=skylake -mavx2 -m64 -mno-red-zone -Og -ffreestanding -fpie -fomit-frame-pointer -fno-delete-null-pointer-checks -fno-common -fno-zero-initialized-in-bss -fno-stack-protector $HFILES -g3 -Wall -Wextra -Wdouble-promotion -Wpedantic -fmessage-length=0 -ffunction-sections -c -MMD -MP -MF"${f%.*}.d" -MT"${f%.*}.o" -o "${f%.*}.o" "${f%.*}.c"
  "gcc" -march=skylake -mavx2 -m64 -mno-red-zone -Og -ffreestanding -fpie -fomit-frame-pointer -fno-delete-null-pointer-checks -fno-common -fno-zero-initialized-in-bss -fno-stack-protector $HFILES -g3 -Wall -Wextra -Wdouble-promotion -Wpedantic -fmessage-length=0 -ffunction-sections -c -MMD -MP -MF"${f%.*}.d" -MT"${f%.*}.o" -o "${f%.*}.o" "${f%.*}.c" &
done
set +v

#
# Wait for compilation to complete
#

echo
echo Waiting for compilation to complete...
echo

wait

#
# Create the objects.list file, which contains properly-formatted (i.e. has
# forward slashes) locations of compiled Backend .o files
#

while read f; do
  echo "${f%.*}.o" | tee -a objects.list
done < $CurDir/c_files_mac.txt

#
# Add compiled .o files from the startup directory to objects.list
#

for f in $CurDir/startup/*.o; do
  echo "$f" | tee -a objects.list
done

#
# Add compiled user .o files to objects.list
#

for f in $CurDir/src/*.o; do
  echo "$f" | tee -a objects.list
done

#
# Link the object files using all the objects in objects.list and an optional
# linker script (it would go in the Backend/Linker directory) to generate the
# output binary, which is called "Kernel64.mach64"
#
# NOTE: Linkerscripts may be needed for bigger projects
#

# Preload's been obsoleted. Now we just get regular executables.
# Also Pagezero protections don't work without virtual memory!!

# "gcc" -nostdlib -T$LinkerScript -Wl,-e,_kernel_main -Wl,-dead_strip -Wl,-pie -Wl,-static -Wl,-map,output.map -Wl,-pagezero_size,0x0 -o "Kernel64.mach64" @"objects.list" # LC_UNIXTHREAD, used by Mac's own kernel
# "gcc" -nostdlib -Wl,-e,_kernel_main -Wl,-dead_strip -Wl,-pie -Wl,-map,output.map -Wl,-pagezero_size,0x0 -o "Kernel64.mach64" @"objects.list" -lSystem # LC_MAIN, requires DYLD
set -v
"gcc" -nostdlib -Wl,-e,_kernel_main -Wl,-dead_strip -Wl,-pie -Wl,-static -Wl,-map,output.map -Wl,-pagezero_size,0x0 -o "Kernel64.mach64" @"objects.list" # LC_UNIXTHREAD, used by Mac's own kernel
"strip" "Kernel64.mach64"
set +v
# Comment the above strip command to keep debug symbols in the output binary.

#
# Output the program size
#

echo
echo Generating binary and Printing size information:
echo
"size" "Kernel64.mach64"
echo

#
# Return to the folder started from
#

cd $CurDir

#
# Prompt user for next action
#

read -p "Cleanup, recompile, or done? [c for cleanup, r for recompile, any other key for done] " UPL

echo
echo "**********************************************************"
echo

case $UPL in
  [cC])
    exec ./Cleanup-Mac.sh
  ;;
  [rR])
    exec ./Compile-Mac.sh
  ;;
  *)
  ;;
esac
