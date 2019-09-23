//==================================================================================================================================
//  Simple Kernel: ACPI OS Services Layer Header
//==================================================================================================================================
//
// Version 0.8
//
// Author:
//  KNNSpeed
//
// Source Code:
//  https://github.com/KNNSpeed/Simple-Kernel
//
// This file provides function prototypes for ACPI OS Services Layer functions in acKernel64.c.
//
// IMPORTANT NOTE:
// See the section titled "Public globals and runtime configuration options" in backend acpixf.h for runtime-modifiable global options.
// Do not set those variables in that file directly without noting it in the Simple Kernel Changes.txt in the ACPICA folder, otherwise
// the ACPICA license will be violated.
//
// The ACPI Component Architecture User Guide and Programmer Reference, Revision 6.2, is mistaken where it notes these globals are in acglobal.h.
//

#ifndef _acKernel64_H
#define _acKernel64_H

#define ACPI_MACHINE_WIDTH 64

#define ACPI_USE_NATIVE_DIVIDE
#define ACPI_USE_NATIVE_MATH64

#define USE_NATIVE_ALLOCATE_ZEROED

// Not yet... TODO
//#define ACPI_MUTEX_TYPE ACPI_OSL_MUTEX
//#define ACPI_MUTEX [Some mutex type]

#ifdef __x86_64__
#define ACPI_FLUSH_CPU_CACHE() __asm __volatile("wbinvd");
#endif

// Solve link-time conflicts
#define strtoul simple_strtoul
#define memcmp simple_memcmp
#define memset simple_memset
#define memmove simple_memmove
#define memcpy simple_memcpy
#define vsnprintf simple_vsnprintf
#define snprintf simple_snprintf
#define sprintf simple_sprintf
// For now...
#define ACPI_SINGLE_THREADED

#ifdef __linux__
// These are from aclinux.h
//#define ACPI_USE_GPE_POLLING

#define ACPI_STRUCT_INIT(field, value)  .field = value

#endif // __linux__

#endif /* _acKernel64_H */
