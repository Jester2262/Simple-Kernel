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

//#include <stdint.h>
//#include <stdarg.h>

/*
typedef unsigned long       uint64_t;
typedef long                int64_t;
typedef unsigned int        uint32_t;
typedef int                 int32_t;
typedef unsigned short      uint16_t;
typedef short               int16_t;
typedef unsigned char       uint8_t;
typedef char                int8_t;

typedef uint8_t BOOLEAN;
typedef int8_t INT8;
typedef uint8_t UINT8;
typedef int16_t INT16;
typedef uint16_t UINT16;
typedef int32_t INT32;
typedef uint32_t UINT32;
typedef int64_t INT64;
typedef uint64_t UINT64;

#define COMPILER_DEPENDENT_INT64        int64_t
#define COMPILER_DEPENDENT_UINT64       uint64_t

#define ACPI_UINTPTR_T      uintptr_t
*/

#define ACPI_MACHINE_WIDTH 64

#define ACPI_USE_NATIVE_DIVIDE
#define ACPI_USE_NATIVE_MATH64

#define USE_NATIVE_ALLOCATE_ZEROED

//
// Normally ACPI's uninitialized global variables go into .bss, which isn't good for this since their addresses
// need to be fixed relative to other parts of the program.
// This ensures all ACPI_GLOBALs and ACPI_INIT_GLOBALs are extern'd in ACPI, and defined in acKernel64.c instead.
//
/*
#ifdef DEFINE_ACPI_GLOBALS
#undef DEFINE_ACPI_GLOBALS
#endif
*/
// Undefining the above forces these to always be used:
/*
#define ACPI_GLOBAL(type,name) \
  extern type name

#define ACPI_INIT_GLOBAL(type,name,value) \
  extern type name
*/
// They can then be redefined in acKernel64.c

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
