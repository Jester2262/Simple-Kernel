# Simple Kernel Development Framework
A minimal, cross-platform development environment for building bare-metal x86-64 programs, kernels, and/or full operating systems on UEFI 2.x systems. It is primarily designed to make programs for use with https://github.com/KNNSpeed/Simple-UEFI-Bootloader.

**Version 0.8 (not considered "release-ready" until 1.0)**

The included build system compiles operating system kernels or bare metal programs as native executables for the builder's platform (Windows, Mac, or Linux), which can then be loaded by the bootloader. A kernel framework containing a software renderer, flexible text output, multi-GPU graphical support, and a whole host of low-level system control functions is also included. Many of the provided support functions feature full AVX optimization for x86-64, as well, making them **extremely** fast.  

See Simple_Kernel/inc/Kernel64.h, Simple_Kernel/inc/ISR.h, and Simple_Kernel/startup/avxmem.h for complete function listings. Detailed descriptions are provided for each function in the functions' corresponding .c files (or .S file in the case of ISRs).  

See "Issues" for my to-do list before hitting "official release-ready" version 1.0, and see the "Releases" tab of this project for executable demos. See the "Building an OS Kernel/Bare-Metal x86-64 Application" and "How to Build from Source" sections below for details on how to use this project.  

*Apologies to AMD Ryzen users: I can't test Ryzen-optimized binaries very thoroughly as I don't have any Ryzen machines. The build scripts are provided with the caveat that they ought to produce optimized binaries (I don't see why they wouldn't).*  

## Features

This project is designed to inherit all of the features provided by https://github.com/KNNSpeed/Simple-UEFI-Bootloader, in addition to providing the following:  

- Tons of low-level support functionality in C, like text printing and scrolling, screen drawing, and system register control and diagnostic functions  
- AVX support ***(1)***
- Full access to UEFI 2.x runtime services and other parameters passed by the bootloader, including 1+ linear frame buffer(s) for display output
- Unrestricted access to available hardware ***(2)***
- Minimal development environment tuned for Windows, Mac, and Linux included in repository (can be used with the same Backend folder as the bootloader, barring differences in compiler version requirements)  

***(1)*** A CPU with AVX is required to use most of the included functionality, see "Target System Requirements" below to see where to check if you have it.  
***(2)*** You will need to write your own interfaces (essentially drivers or kernel extensions, depending on the term you're most familiar with) for access to more advanced hardware. I don't yet have drivers for things like PCI-Express, and things like on-board audio differ wildly between systems. Remember: this is not an operating system, this is meant to help make them and other kinds of bare-metal/operating system-less programs. If anyone has had the need or desire to port an application to run with no OS in the way (e.g. to ditch the licensing costs or performance overhead that might be associated with using an OS), this could really help with that.  

// TODO: "Features for printing" "Features for memory management" "Features for drawing to the screen" "Features for low-level system control" "Features for interrupt handling" ... etc. There's a **lot** of stuff here. Maybe make a wiki page.

## Target System Requirements  

*These are the nearly same as the bootloader's requirements. If your target system can run the bootloader and you have a CPU with AVX, you're all set.*  

- x86-64 architecture with AVX (most Intel ix-2xxx or newer or AMD Ryzen or newer CPUs have it, see [the Wikipedia page on AVX](https://en.wikipedia.org/wiki/Advanced_Vector_Extensions#CPUs_with_AVX))
- Secure Boot must be disabled  
- At least 1GB total system RAM *per logical core* is recommended, but 4GB may be considered the minimum ***(1)***
- At least 1 graphics card (Intel, AMD, NVidia, etc.) **with UEFI GOP support**  
- A keyboard  

The earliest GPUs with UEFI GOP support were released around the Radeon HD 7xxx series (~2011). Anything that age or newer should have UEFI GOP support, though older models, like early 7970s, required owners to contact GPU vendors to get UEFI-compatible firmware. On Windows, you can check if your graphics card(s) have UEFI GOP support by downloading TechPowerUp's GPU-Z utility and seeing whether or not the UEFI checkbox is checked. If it is, you're all set!  

*NOTE: You need to check each graphics card if there is a mix, as you will only be able to use the ones with UEFI GOP support. Per the system requirements above, you need at least one compliant device. Multiple devices are supported per https://github.com/KNNSpeed/Simple-UEFI-Bootloader.*  

***(1)*** **IMPORTANT VM INFO:** If using a hypervisor like Microsoft's Hyper-V, use a generation 2 VM with configuration version 9.0 or higher and turn off "Dynamic Memory" in the VM's settings. 4GB RAM is the minimum that must be assigned to the VM regardless of core count; using less RAM causes the VM to crash while setting up identity mapped page tables with 1GB page sizes.

## License and Crediting  

Please see the LICENSE file for information on all licenses covering code created for and used in this project.  

***TL;DR:***  

If you don't give credit to this project, per the license you aren't allowed to do anything with any of its source code that isn't already covered by an existing license (in other words, my license covers most of the code I wrote). That's pretty much it, and why it's "almost" PD, or "PD with Credit" if I have to give it a nickname: there's no restriction on what it gets used for as long as the license is satisfied. If you have any issues, feature requests, etc. please post in "Issues" so it can be attended to/fixed.  

Note that each of these files already has appropriate crediting at the top, so you could just leave what's already there to satisfy the terms. You really should see the license file for complete information, though (it's short!!).  

## Building an OS Kernel/Bare-Metal x86-64 Application

The below "How to Build from Source" section contains complete compilation instructions for each platform, and then all you need to do is put your code in "src" and "inc" in place of mine (leave the "startup" folder as-is). Once compiled, your program can be run in the same way as described in the "Releases" section of https://github.com/KNNSpeed/Simple-UEFI-Bootloader using a UEFI-supporting VM like Hyper-V or on actual hardware.

***Important Points to Consider:***

The entry point function (i.e. the "main" function) of your program should look like this, otherwise the kernel will fail to run:  

```
__attribute__((naked)) void kernel_main(LOADER_PARAMS * LP) // Loader Parameters  
{  

}
```  

The LOADER_PARAMS data type is defined as the following structure:
```
typedef struct {
  UINT32                    UEFI_Version;                   // The system UEFI version
  UINT32                    Bootloader_MajorVersion;        // The major version of the bootloader
  UINT32                    Bootloader_MinorVersion;        // The minor version of the bootloader

  UINT32                    Memory_Map_Descriptor_Version;  // The memory descriptor version
  UINTN                     Memory_Map_Descriptor_Size;     // The size of an individual memory descriptor
  EFI_MEMORY_DESCRIPTOR    *Memory_Map;                     // The system memory map as an array of EFI_MEMORY_DESCRIPTOR structs
  UINTN                     Memory_Map_Size;                // The total size of the system memory map

  EFI_PHYSICAL_ADDRESS      Kernel_BaseAddress;             // The base memory address of the loaded kernel file
  UINTN                     Kernel_Pages;                   // The number of pages (1 page == 4096 bytes) allocated for the kernel file

  CHAR16                   *ESP_Root_Device_Path;           // A UTF-16 string containing the drive root of the EFI System Partition as converted from UEFI device path format
  UINT64                    ESP_Root_Size;                  // The size (in bytes) of the above ESP root string
  CHAR16                   *Kernel_Path;                    // A UTF-16 string containing the kernel's file path relative to the EFI System Partition root (it's the first line of Kernel64.txt)
  UINT64                    Kernel_Path_Size;               // The size (in bytes) of the above kernel file path
  CHAR16                   *Kernel_Options;                 // A UTF-16 string containing various load options (it's the second line of Kernel64.txt)
  UINT64                    Kernel_Options_Size;            // The size (in bytes) of the above load options string

  EFI_RUNTIME_SERVICES     *RTServices;                     // UEFI Runtime Services
  GPU_CONFIG               *GPU_Configs;                    // Information about available graphics output devices; see below GPU_CONFIG struct for details
  EFI_FILE_INFO            *FileMeta;                       // Kernel file metadata
  EFI_CONFIGURATION_TABLE  *ConfigTables;                   // UEFI-installed system configuration tables (ACPI, SMBIOS, etc.)
  UINTN                     Number_of_ConfigTables;         // The number of system configuration tables
} LOADER_PARAMS;
```

Of those pointers, the only data type not defined by UEFI spec is `GPU_CONFIG`, which looks like this:

```
typedef struct {
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE  *GPUArray;             // This array contains the EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE structures for each available framebuffer
  UINT64                              NumberOfFrameBuffers; // The number of pointers in the array (== the number of available framebuffers)
} GPU_CONFIG;
```

You will find some relevant structures defined in "Kernel64.h" of the sample kernel, with the rest defined in the "EfiBind.h" and "EfiTypes.h" files in the "startup" directory.

You will also need to `#include` the "Efi" files from "startup" in your code: refer to the "Kernel64.h" file in the "inc" directory for an example. You may find it easiest to just ```#include "Kernel64.h"``` in your code after removing any unnecessary function prototypes from the file, as it already has all the requisite inclusions and EFI structures for LOADER_PARAMS defined within it.

## How to Build from Source  

Windows: Requires MinGW-w64 based on GCC 8.1.0 or later  
Mac: Requires Mac OS Sierra or later with the latest XCode Command Line Tools for the OS  
Linux: Requires GCC 8.0.0 or later and Binutils 2.29.1 or later  

I cannot make any guarantees whatsoever for earlier versions, especially with the number of compilation and linking flags used.  

***Windows:***  
1. Download and extract or clone this repository into a dedicated folder, preferably somewhere easy like C:\BareMetalx64

2. Download MinGW-w64 "x86_64-posix-seh" from https://sourceforge.net/projects/mingw-w64/ (click "Files" and scroll down - pay attention to the version numbers!).

3. Extract the archive into the "Backend" folder.

4. Open Windows PowerShell or the Command Prompt in the "Simple-Kernel" folder and type ".\Compile.bat"

    *That's it! It should compile and a binary called "Kernel64.exe" will be output into the "Backend" folder.*

***Mac:***  
1. Download and extract or clone this repository into a dedicated folder, preferably somewhere easy like ~/BareMetalx64

2. Open Terminal in the "Simple-Kernel" folder and run "./Compile-Mac.sh"

    *That's it! It should compile and a binary called "Kernel64.mach64" will be output into the "Backend" folder.*

***Linux:***  

1. Download and extract or clone this repository into a dedicated folder, preferably somewhere easy like ~/BareMetalx64

2. If, in the terminal, "gcc --version" reports GCC 8.0.0 or later and "ld --version" reports 2.29.1 or later, do steps 2a, 2b, and 2c. Otherwise go to step 3.

    2a. Type "which gcc" in the terminal, and make a note of what it says (something like /usr/bin/gcc or /usr/local/bin/gcc)

    2b. Open Compile.sh in an editor of your choice (nano, gedit, vim, etc.) and set the GCC_FOLDER_NAME variable at the top to be the part before "bin" (e.g. /usr or /usr/local, without the last slash). Do the same thing for BINUTILS_FOLDER_NAME, except use the output of "which ld" to get the directory path preceding "bin" instead.

    2c. Now set the terminal to the Simple-Kernel folder and run "./Compile.sh", which should work and output Kernel64.elf in the Backend folder. *That's it!*

3. Looks like we need to build GCC & Binutils. Navigate to the "Backend" folder in terminal and do "git clone git://gcc.gnu.org/git/gcc.git" there. This will download a copy of GCC 8.0.0, which is necessary for "static-pie" support (when combined with Binutils 2.29.1 or later, it allows  statically-linked, position-independent executables to be created; earlier versions do not). If that git link ever changes, you'll need to find wherever the official GCC git repository ran off to.

4. Once GCC has been cloned, in the cloned folder do "contrib/download_prerequisites" and then "./configure -v --build=x86_64-linux-gnu --host=x86_64-linux-gnu --target=x86_64-linux-gnu --prefix=$PWD/../gcc-8 --enable-checking=release --enable-languages=c --disable-multilib"

    NOTE: If you want, you can enable other languages like c++, fortran, objective-c (objc), go, etc. with enable-languages. You can also change the name of the folder it will built into by changing --prefix=[desired folder]. The above command line will configure GCC to be made in a folder called gcc-8 inside the "Backend" folder. Be aware that --prefix requires an absolute path.

5. After configuration completes, do "make -j [twice the number of cores of your CPU]" and go eat lunch. Unfortunately, sometimes building the latest GCC produces build-time errors; I ran into an "aclocal-1.15" issue when building via Linux on Windows (fixed by installing the latest version of Ubuntu on Windows and using the latest autoconf).

6. Now just do "make install" and GCC will be put into the gcc-8 folder from step 4.

7. Next, grab binutils 2.29.1 or later from https://ftp.gnu.org/gnu/binutils/ and extract the archive to Backend.

8. In the extracted Binutils folder, do "mkdir build" and "cd build" before configuring with "../configure --prefix=$PWD/../binutils-binaries --enable-gold --enable-ld=default --enable-plugins --enable-shared --disable-werror"

    NOTE: The "prefix" flag means the same thing as GCC's.

9. Once configuration is finished, do "make -j [twice the number of CPU cores]" and go have dinner.

10. Once make is done making, do "make -k check" and do a crossword or something. There should be a very small number of errors, if any.

11. Finally, do "make install" to install the package into binutils-binaries. Congratulations, you've just built some of the biggest Linux sources ever!

12. Open Compile.sh in an editor of your choice (nano, gedit, vim, etc.) and set the GCC_FOLDER_NAME variable at the top (e.g. gcc-8 without any slashes). Do the same thing for the BINUTILS_FOLDER_NAME, except use the binutils-binaries folder.

13. At long last, you should be able to run "./Compile.sh" from within the "Simple-Kernel" folder.

    *That's it! It should compile and a binary called "Kernel64.elf" will be output into the "Backend" folder.*

    For more information about building GCC and Binutils, see these: http://www.linuxfromscratch.org/blfs/view/cvs/general/gcc.html & http://www.linuxfromscratch.org/lfs/view/development/chapter06/binutils.html  

## Change Log  

V0.8 (9/4/2019) - Major update: Memory management subsystem added (paging, malloc/calloc/realloc/free, AllocateFreeAddress). GDT, IDT & interrupts/exceptions implemented (even AVX state can be saved during interrupts!), stack set up (previously this was still using the UEFI's stack), fixed AVX_memmove alignment issue, Bootloader V2.x compatibility, fixed MinGW PE relocation issue, fonts and bitmaps now have independent x- and y-scaling factors, optimized output renderers, fixed tons of bugs, moved everything that needs to be moved out of EfiBootServicesCode/Data, added lots and lots of new functions (especially memory-related ones). New binaries will be made whenever issue #19 is resolved.

V0.z (2/20/2019) - Major update: AVX is now required, separated code out of code files, added a TON of low-level system control functions (port I/O, control register manipulation, HWP support for systems supporting it, cpu feature checks), added CPU frequency measurement (average since boot and for specific user-defined code segments), updated text printing to include wraparound, smooth scrolling, and quick-scrolling, and prettied up code styling. Also, spun-off a new project from this one: https://github.com/KNNSpeed/AVX-Memmove

V0.y (2/1/2019) - Major code cleanup, added printf() and a whole host of text-displaying functions, resolved issues #5 and #6. No new binaries will be made for this version.

V0.x (2/2/2018) - Initial upload of environment and compilable sample. Not yet given a version number.  

## Acknowledgements  

- [Intel Corporation](https://www.intel.com/content/www/us/en/homepage.html) and [Advanced Micro Devices, Inc.](https://www.amd.com/en) for their x86 architecture programming manuals, which have been instrumental in the development of this project. [Intel's docs.](https://software.intel.com/en-us/articles/intel-sdm) [AMD's docs.](https://developer.amd.com/resources/developer-guides-manuals/) [Intel's AVX Intrinsics Guide.](https://software.intel.com/sites/landingpage/IntrinsicsGuide/)
- [OSDev Wiki](http://wiki.osdev.org/Main_Page) for its wealth of available information
- [The FreeBSD Project](https://www.freebsd.org/) for maintaining subr_prf.c, from which freestanding print functions--like the one in this project--can easily be made (credit to [Michael Steil's "A Standalone printf() for Early Bootup"](https://www.pagetable.com/?p=298) for the idea to do something like this)
- [The Data Plane Development Kit project](https://www.dpdk.org/about/) for open-source examples of various really useful optimizations.
- [Philipp Oppermann](https://os.phil-opp.com/) for in-depth explanations and graphics illustrating various x86 constructs (the [section on x86-64 paging](https://os.phil-opp.com/paging-introduction/#paging-on-x86-64) is a particularly useful reference!)
- [Agner Fog](https://www.agner.org/) for [amazing software optimization resources](https://www.agner.org/optimize/).
- [Marcel Sondaar](https://mysticos.combuster.nl/) for the original public domain 8x8 font
- [Daniel Hepper](https://github.com/dhepper/) for converting the 8x8 font into [public domain C headers](https://github.com/dhepper/font8x8)
- [James Molloy](http://www.jamesmolloy.co.uk/tutorial_html/) for [demonstrating assembly macros for use in generating hundreds of interrupts](http://www.jamesmolloy.co.uk/tutorial_html/4.-The%20GDT%20and%20IDT.html)
- [Intel Corporation](https://www.intel.com/content/www/us/en/homepage.html) for EfiTypes.h, the x86-64 EfiBind.h, and EfiError.h (the ones used in this project are derived from [TianoCore EDK II](https://github.com/tianocore/edk2/))
- [UEFI Forum](http://www.uefi.org/) for the [UEFI Specification Version 2.7 (Errata A)](http://www.uefi.org/sites/default/files/resources/UEFI%20Spec%202_7_A%20Sept%206.pdf), as well as for [previous UEFI 2.x specifications](http://www.uefi.org/specifications)
- [PhoenixWiki](http://wiki.phoenix.com/wiki/index.php/Category:UEFI) for very handy documentation on UEFI functions
- [The GNU project](https://www.gnu.org/home.en.html) for [GCC](https://gcc.gnu.org/), a fantastic and versatile compiler, and [Binutils](https://www.gnu.org/software/binutils/), equally fantastic binary utilities
- [MinGW-w64](https://mingw-w64.org/doku.php) for porting GCC to Windows
- [LLVM](https://llvm.org/) for providing a feature-rich and efficient native compiler suite for Apple platforms
