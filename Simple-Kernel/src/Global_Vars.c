//==================================================================================================================================
//  Simple Kernel: Global Variable Definitions
//==================================================================================================================================
//
// Version 1.0
//
// Author:
//  KNNSpeed
//
// Source Code:
//  https://github.com/KNNSpeed/Simple-Kernel
//
// This file contains definitions for variables meant to be used across all user source files. Per C best practices, for global
// variables to be usable across multiple source files that share headers, the headers can only contain declarations of global
// variables ("extern type variable;"). The variables themselves need to be defined in a .c file.
//
// Because of the way in which this project is set up--specifically related to having to use the "-c" (compile only, don't link)
// compiler flag, global variables must be defined this way to avoid issues with linking; global symbols won't be resolved correctly
// otherwise. Note: Defining globals in Kernel64.c and declaring them with 'extern' in Kernel64.h actually does not work here, as
// changes made to the globals in the main function in Kernel64.c don't actually propagate to uses in other files. This solves that.
//
// It is also worth mentioning that, in this project, not using a .c file like this for project-scope global variables relies on
// undefined behavior to work. Namely, global variables would need to be tentatively defined in every source file and must use the
// same name in each file. Abusing undefined behavior like this is bad, as it could break at any time. Once again, using this global
// variable definition file solves that: Every global variable defined in this file has a corresponding "extern" in Kernel64.h, and
// it all works while staying valid C (and lets us keep using only one "master" header file instead of one header for each .c file).
//
// For more information about global variable definitions and declarations, see here:
// https://stackoverflow.com/questions/1433204/how-do-i-use-extern-to-share-variables-between-source-files
//
// To define a "global, read-only constant" the constant should be defined as "static const" in a header file that is common to all
// the .c files that need to access the constant. A global read-only constant can be thought of as a read-only data type that is
// initialized to a non-zero value. Normally, a downside of being static in a common header means the constant will take up space in
// every translation unit (think of it like each .c file gets its own copy of a variable defined as "static" in a common header, and
// changing it in one .c file has no impact on other files). However, GCC merges constants with the "fmerge-constants" flag (on by
// default), so "static const" actually share the same definition and don't hog unnecessary space when that's used.
//
// NOTE: "fmerge-constants" is not the same as "fmerge-all-constants"; the latter also merges variables with the same name between
// different compilation units, which can have all kinds of weird results. Merging constants is not part of the C standard, though,
// so both are disabled here in compiler invocations. Enabling either risks breaking these variables and, as far as I've seen,
// they'll break in a way that defeats the whole purpose of global variables. See: https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html
//
// TIP: Initialize global variables here with data so that compilers put these in the .data section without a linkerscript. Being in
// the .data section means they'll have their own space in the binary and won't be at risk of any funky business messing with them.
//

#include "Kernel64.h"
/*
#include "EfiBind.h"
#include "EfiTypes.h"
#include "EfiError.h"
*/

#define TSC_FALLBACK_CYCLES_PER_SEC 30ULL * 100ULL * 1000000ULL
#define TSC_FALLBACK_CYCLES_PER_MSEC 30ULL * 100ULL * 1000ULL
#define TSC_FALLBACK_CYCLES_PER_USEC 30ULL * 100ULL
#define TSC_FALLBACK_CYCLES_PER_100NSEC 30ULL * 10ULL
#define TSC_FALLBACK_CYCLES_PER_10NSEC 30ULL

//----------------------------------------------------------------------------------------------------------------------------------
// Core Functionality
//----------------------------------------------------------------------------------------------------------------------------------

EFI_PHYSICAL_ADDRESS Global_RSDP_Address = ~0ULL;
ACPI_INTERRUPT_STRUCT Global_ACPI_Interrupt_Table[256] = {0};
TSC_FREQUENCY_STRUCT Global_TSC_frequency = {TSC_FALLBACK_CYCLES_PER_SEC, TSC_FALLBACK_CYCLES_PER_MSEC, TSC_FALLBACK_CYCLES_PER_USEC, TSC_FALLBACK_CYCLES_PER_100NSEC, TSC_FALLBACK_CYCLES_PER_10NSEC};

//----------------------------------------------------------------------------------------------------------------------------------
// Text Printing
//----------------------------------------------------------------------------------------------------------------------------------
/*
// GRAPHICS
typedef struct {
  UINT32            RedMask;
  UINT32            GreenMask;
  UINT32            BlueMask;
  UINT32            ReservedMask;
} EFI_PIXEL_BITMASK;

typedef enum {
  PixelRedGreenBlueReserved8BitPerColor, // 0
  PixelBlueGreenRedReserved8BitPerColor, // 1
  PixelBitMask, // 2
  PixelBltOnly, // 3
  PixelFormatMax // 4
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
  UINT32                     Version;
  UINT32                     HorizontalResolution;
  UINT32                     VerticalResolution;
  EFI_GRAPHICS_PIXEL_FORMAT  PixelFormat; // 0 - 4
  EFI_PIXEL_BITMASK          PixelInformation;
  UINT32                     PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
  UINT32                                 MaxMode;
  UINT32                                 Mode;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION   *Info;
  UINTN                                  SizeOfInfo;
  EFI_PHYSICAL_ADDRESS                   FrameBufferBase;
  UINTN                                  FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

// For printf
typedef struct {
	EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE  defaultGPU;       // Default EFI GOP output device from GPUArray (should be GPUArray[0] if there's only 1)
	UINT32                             height;           // Character font height
	UINT32                             width;            // Character font width (in bits)
	UINT32                             font_color;       // Default font color
	UINT32                             highlight_color;  // Default highlight color
  UINT32                             background_color; // Default background color
	UINT32                             x;                // Leftmost x-coord that's in-bounds (NOTE: per UEFI Spec 2.7 Errata A, (0,0) is always the top left in-bounds pixel.)
	UINT32                             y;                // Topmost y-coord
	UINT32                             xscale;           // Output width scale for systemfont used by printf
  UINT32                             yscale;           // Output height scale for systemfont used by printf
  UINT32                             Reserved;         // Reserved for future use.
  UINT32                             index;            // Global string index for printf, etc. to keep track of cursor's postion in the framebuffer
  UINT32                             textscrollmode;   // What to do when a newline goes off the bottom of the screen: 0 = scroll entire screen, 1 = wrap around to the top
} GLOBAL_PRINT_INFO_STRUCT;
*/

GLOBAL_PRINT_INFO_STRUCT Global_Print_Info = {{1, 1, NULL, 1, 1, 1}, 8, 8, 0x0, 0x0, 0x0, 0, 0, 1, 1, 0, 0, 0};

//----------------------------------------------------------------------------------------------------------------------------------
// Memory
//----------------------------------------------------------------------------------------------------------------------------------
/*
// For memory functions, like malloc and friends in Memory.c
typedef struct {
  UINTN                   MemMapSize;              // Size of the memory map (LP->Memory_Map_Size)
  UINTN                   MemMapDescriptorSize;    // Size of memory map descriptors (LP->Memory_Map_Descriptor_Size)
  EFI_MEMORY_DESCRIPTOR  *MemMap;                  // Pointer to memory map (LP->Memory_Map)
  UINT32                  MemMapDescriptorVersion; // Memory map descriptor version
  UINT32                  Pad;                     // Pad to multiple of 64 bits
} GLOBAL_MEMORY_INFO_STRUCT;
*/
GLOBAL_MEMORY_INFO_STRUCT Global_Memory_Info = {1, 1, NULL, 1, 0};

//----------------------------------------------------------------------------------------------------------------------------------
// Misc
//----------------------------------------------------------------------------------------------------------------------------------

// UEFI Configuration Tables
const EFI_GUID MpsTableGuid = MPS_TABLE_GUID;
const EFI_GUID Acpi10TableGuid = ACPI_10_TABLE_GUID;
const EFI_GUID Acpi20TableGuid = ACPI_20_TABLE_GUID;
const EFI_GUID SmbiosTableGuid = SMBIOS_TABLE_GUID;
const EFI_GUID Smbios3TableGuid = SMBIOS3_TABLE_GUID;
const EFI_GUID SalSystemTableGuid = SAL_SYSTEM_TABLE_GUID;
