//==================================================================================================================================
//  Simple Kernel: Kernel Entrypoint
//==================================================================================================================================
//
// Version 0.y
//
// Author:
//  KNNSpeed
//
// Source Code:
//  https://github.com/KNNSpeed/Simple-Kernel
//
// This program is a small x86-64 program for use with the Simple UEFI Bootloader: https://github.com/KNNSpeed/Simple-UEFI-Bootloader.
// It contains some functions that might prove useful in development of other bare-metal programs, and showcases some of the features
// provided by the bootloader (e.g. Multi-GPU framebuffer support).
//
// The main function of this program, defined as "kernel_main" in the accompanying compile scripts, is passed a pointer to the following
// structure from the bootloader:
/*
  typedef struct {
    EFI_MEMORY_DESCRIPTOR  *Memory_Map;   // The system memory map as an array of EFI_MEMORY_DESCRIPTOR structs
    EFI_RUNTIME_SERVICES   *RTServices;   // UEFI Runtime Services
    GPU_CONFIG             *GPU_Configs;  // Information about available graphics output devices; see below for details
    EFI_FILE_INFO          *FileMeta;     // Kernel64 file metadata
    void                   *RSDP;         // A pointer to the RSDP ACPI table
  } LOADER_PARAMS;
*/
//
// GPU_CONFIG is a custom structure that is defined as follows:
//
/*
  typedef struct {
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE  *GPUArray;             // An array of EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE structs defining each available framebuffer
    UINT64                              NumberOfFrameBuffers; // The number of structs in the array (== the number of available framebuffers)
  } GPU_CONFIG;
*/
//
// The header file, Kernel64.h, contains definitions for the data types of each pointer in the above structures for easy reference.
//
// Note: As mentioned in the bootloader's embedded documentation, GPU_Configs provides access to linear framebuffer addresses for directly
// drawing to connected screens: specifically one for each active display per GPU. Typically there is one active display per GPU, but it is
// up to the GPU firmware maker to deterrmine that. See "12.10 Rules for PCI/AGP Devices" in the UEFI Specification 2.7 Errata A for more
// details: http://www.uefi.org/specifications
//

#include "Kernel64.h"
#include "font8x8.h" // This can only be included by one file since it's h-files containing initialized variables (in this case arrays)

#define MAJOR_VER 0
#define MINOR_VER 'y'

// The character print function can draw raw single-color bitmaps formatted like this, given appropriate height and width values
const unsigned char load_image[48] = {
    0x00, 0x3F, 0x80, 0x00, // ........ ..@@@@@@ @....... ........
    0x01, 0x80, 0x30, 0x00, // .......@ @....... ..@@.... ........
    0x0C, 0x00, 0x06, 0x00, // ....@@.. ........ .....@@. ........
    0x30, 0x1E, 0xE1, 0x80, // ..@@.... ...@@@@. @@@....@ @.......
    0x60, 0x61, 0xC0, 0xC0, // .@@..... .@@....@ @@...... @@......
    0xC0, 0xC0, 0xC0, 0x60, // @@...... @@...... @@...... .@@.....
    0xC0, 0xC0, 0xE0, 0x60, // @@...... @@...... @@@..... .@@.....
    0x60, 0x61, 0xB0, 0xC0, // .@@..... .@@....@ @.@@.... @@......
    0x30, 0x1E, 0x1F, 0x80, // ..@@.... ...@@@@. ...@@@@@ @.......
    0x0C, 0x00, 0x00, 0x00, // ....@@.. ........ ........ ........
    0x01, 0x80, 0x3C, 0x00, // .......@ @....... ..@@@@.. ........
    0x00, 0x3F, 0x80, 0x00  // ........ ..@@@@@@ @....... ........
}; // Width = 27 bits, height = 12 bytes

// load_image2 is what actually looks like load_image's ascii art when rendered
const unsigned char load_image2[96] = {
    0x00, 0x3F, 0x80, 0x00, // ........ ..@@@@@@ @....... ........
    0x00, 0x3F, 0x80, 0x00, // ........ ..@@@@@@ @....... ........
    0x01, 0x80, 0x30, 0x00, // .......@ @....... ..@@.... ........
    0x01, 0x80, 0x30, 0x00, // .......@ @....... ..@@.... ........
    0x0C, 0x00, 0x06, 0x00, // ....@@.. ........ .....@@. ........
    0x0C, 0x00, 0x06, 0x00, // ....@@.. ........ .....@@. ........
    0x30, 0x1E, 0xE1, 0x80, // ..@@.... ...@@@@. @@@....@ @.......
    0x30, 0x1E, 0xE1, 0x80, // ..@@.... ...@@@@. @@@....@ @.......
    0x60, 0x61, 0xC0, 0xC0, // .@@..... .@@....@ @@...... @@......
    0x60, 0x61, 0xC0, 0xC0, // .@@..... .@@....@ @@...... @@......
    0xC0, 0xC0, 0xC0, 0x60, // @@...... @@...... @@...... .@@.....
    0xC0, 0xC0, 0xC0, 0x60, // @@...... @@...... @@...... .@@.....
    0xC0, 0xC0, 0xE0, 0x60, // @@...... @@...... @@@..... .@@.....
    0xC0, 0xC0, 0xE0, 0x60, // @@...... @@...... @@@..... .@@.....
    0x60, 0x61, 0xB0, 0xC0, // .@@..... .@@....@ @.@@.... @@......
    0x60, 0x61, 0xB0, 0xC0, // .@@..... .@@....@ @.@@.... @@......
    0x30, 0x1E, 0x1F, 0x80, // ..@@.... ...@@@@. ...@@@@@ @.......
    0x30, 0x1E, 0x1F, 0x80, // ..@@.... ...@@@@. ...@@@@@ @.......
    0x0C, 0x00, 0x00, 0x00, // ....@@.. ........ ........ ........
    0x0C, 0x00, 0x00, 0x00, // ....@@.. ........ ........ ........
    0x01, 0x80, 0x3C, 0x00, // .......@ @....... ..@@@@.. ........
    0x01, 0x80, 0x3C, 0x00, // .......@ @....... ..@@@@.. ........
    0x00, 0x3F, 0x80, 0x00, // ........ ..@@@@@@ @....... ........
    0x00, 0x3F, 0x80, 0x00  // ........ ..@@@@@@ @....... ........
}; // Width = 27 bits, height = 24 bytes

const unsigned char load_image3[144] = {
    0x00, 0x3F, 0x80, 0x00, // ........ ..@@@@@@ @....... ........
    0x00, 0x3F, 0x80, 0x00, // ........ ..@@@@@@ @....... ........
    0x00, 0x3F, 0x80, 0x00, // ........ ..@@@@@@ @....... ........
    0x01, 0x80, 0x30, 0x00, // .......@ @....... ..@@.... ........
    0x01, 0x80, 0x30, 0x00, // .......@ @....... ..@@.... ........
    0x01, 0x80, 0x30, 0x00, // .......@ @....... ..@@.... ........
    0x0C, 0x00, 0x06, 0x00, // ....@@.. ........ .....@@. ........
    0x0C, 0x00, 0x06, 0x00, // ....@@.. ........ .....@@. ........
    0x0C, 0x00, 0x06, 0x00, // ....@@.. ........ .....@@. ........
    0x30, 0x1E, 0xE1, 0x80, // ..@@.... ...@@@@. @@@....@ @.......
    0x30, 0x1E, 0xE1, 0x80, // ..@@.... ...@@@@. @@@....@ @.......
    0x30, 0x1E, 0xE1, 0x80, // ..@@.... ...@@@@. @@@....@ @.......
    0x60, 0x61, 0xC0, 0xC0, // .@@..... .@@....@ @@...... @@......
    0x60, 0x61, 0xC0, 0xC0, // .@@..... .@@....@ @@...... @@......
    0x60, 0x61, 0xC0, 0xC0, // .@@..... .@@....@ @@...... @@......
    0xC0, 0xC0, 0xC0, 0x60, // @@...... @@...... @@...... .@@.....
    0xC0, 0xC0, 0xC0, 0x60, // @@...... @@...... @@...... .@@.....
    0xC0, 0xC0, 0xC0, 0x60, // @@...... @@...... @@...... .@@.....
    0xC0, 0xC0, 0xE0, 0x60, // @@...... @@...... @@@..... .@@.....
    0xC0, 0xC0, 0xE0, 0x60, // @@...... @@...... @@@..... .@@.....
    0xC0, 0xC0, 0xE0, 0x60, // @@...... @@...... @@@..... .@@.....
    0x60, 0x61, 0xB0, 0xC0, // .@@..... .@@....@ @.@@.... @@......
    0x60, 0x61, 0xB0, 0xC0, // .@@..... .@@....@ @.@@.... @@......
    0x60, 0x61, 0xB0, 0xC0, // .@@..... .@@....@ @.@@.... @@......
    0x30, 0x1E, 0x1F, 0x80, // ..@@.... ...@@@@. ...@@@@@ @.......
    0x30, 0x1E, 0x1F, 0x80, // ..@@.... ...@@@@. ...@@@@@ @.......
    0x30, 0x1E, 0x1F, 0x80, // ..@@.... ...@@@@. ...@@@@@ @.......
    0x0C, 0x00, 0x00, 0x00, // ....@@.. ........ ........ ........
    0x0C, 0x00, 0x00, 0x00, // ....@@.. ........ ........ ........
    0x0C, 0x00, 0x00, 0x00, // ....@@.. ........ ........ ........
    0x01, 0x80, 0x3C, 0x00, // .......@ @....... ..@@@@.. ........
    0x01, 0x80, 0x3C, 0x00, // .......@ @....... ..@@@@.. ........
    0x01, 0x80, 0x3C, 0x00, // .......@ @....... ..@@@@.. ........
    0x00, 0x3F, 0x80, 0x00, // ........ ..@@@@@@ @....... ........
    0x00, 0x3F, 0x80, 0x00, // ........ ..@@@@@@ @....... ........
    0x00, 0x3F, 0x80, 0x00  // ........ ..@@@@@@ @....... ........
}; // Width = 27 bits, height = 36 bytes
// Output_render_text will ignore the last 5 bits of zeros in each row if width is specified as 27.

// To print text requires a bitmap font.
// NOTE: Using Output_render_bitmap instead of Output_render_text technically allows any arbitrary font to be used as long as it is stored the same way as the included font8x8.
// A character would need to be passed as otherfont['a'] instead of just 'a' in this case.

//void kernel_main(EFI_MEMORY_DESCRIPTOR * Memory_Map, EFI_RUNTIME_SERVICES * RTServices, EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE * GPU_Mode, EFI_FILE_INFO * FileMeta, void * RSDP)
void kernel_main(LOADER_PARAMS * LP) // Loader Parameters
{
  // This function call is required to initialize printf. Set default GPU as GPU 0.
  // This function can be used to reset all global printf values and reassign a new default GPU at any time.
  Initialize_Global_Printf_Defaults(LP->GPU_Configs->GPUArray[0]);
  // Technically, printf is immediately usable now. I'd recommend waitng for AVX/SSE init just in case the compiler uses them when optimizing printf.
  //

  /*
    typedef struct {
      EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE  *GPUArray; // This array contains the EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE structures for each available framebuffer
      UINT64                              NumberOfFrameBuffers; // The number of pointers in the array (== the number of available framebuffers)
    } GPU_CONFIG;

    typedef struct {
      EFI_MEMORY_DESCRIPTOR  *Memory_Map;
      EFI_RUNTIME_SERVICES   *RTServices;
      GPU_CONFIG             *GPU_Configs;
      EFI_FILE_INFO          *FileMeta;
      void                   *RSDP;
    } LOADER_PARAMS;
  */

  /*
  typedef UINT64          EFI_PHYSICAL_ADDRESS;
  typedef UINT64          EFI_VIRTUAL_ADDRESS;

  #define EFI_MEMORY_DESCRIPTOR_VERSION  1
  typedef struct {
      UINT32                          Type;           // Field size is 32 bits followed by 32 bit pad
      UINT32                          Pad;            // There's no pad in the spec...
      EFI_PHYSICAL_ADDRESS            PhysicalStart;  // Field size is 64 bits
      EFI_VIRTUAL_ADDRESS             VirtualStart;   // Field size is 64 bits
      UINT64                          NumberOfPages;  // Field size is 64 bits
      UINT64                          Attribute;      // Field size is 64 bits
  } EFI_MEMORY_DESCRIPTOR;
  */

  unsigned char swapped_image[sizeof(load_image2)]; // Local arrays are undefined until set.

//  bitmap_bitswap(load_image, 12, 27, swapped_image);
//  char swapped_image2[sizeof(load_image)];
//  bitmap_bytemirror(swapped_image, 12, 27, swapped_image2);
  bitmap_bitreverse(load_image2, 24, 27, swapped_image);
  for(UINT64 k = 0; k < LP->GPU_Configs->NumberOfFrameBuffers; k++) // Multi-GPU support!
  {
    bitmap_anywhere_scaled(LP->GPU_Configs->GPUArray[k], swapped_image, 24, 27, 0x0000FFFF, 0xFF000000, ((LP->GPU_Configs->GPUArray[k].Info->HorizontalResolution - 5*27) >>  1), ((LP->GPU_Configs->GPUArray[k].Info->VerticalResolution - 5*24) >> 1), 5);
  }

  uint64_t cr0 = control_register_rw(0, 0, 0);
  printf("CR0: %#qx\r\n", cr0);
//  printf("CR1: %#qx\r\n", control_register_rw(1, 0, 0));
  uint64_t cr2 = control_register_rw(2, 0, 0);
  printf("CR2: %#qx\r\n", cr2);
  uint64_t cr3 = control_register_rw(3, 0, 0);
  printf("CR3: %#qx\r\n", cr3);
  uint64_t cr4 = control_register_rw(4, 0, 0);
  printf("CR4: %#qx\r\n", cr4);
  uint64_t cr8 = control_register_rw(8, 0, 0);
  printf("CR8: %#qx\r\n", cr8);
  uint64_t efer = msr_rw(0xC0000080, 0, 0);
  printf("IA32_EFER: %#qx\r\n", efer);
  uint64_t rflags = control_register_rw('f', 0, 0);
  printf("RFLAGS: %#qx\r\n", rflags);
  // Checking CPUID means determining if bit 21 of FLAGS can be toggled
  uint64_t rflags2 = rflags ^ (1 << 21);
  control_register_rw('f', rflags2, 1);
  rflags2 = control_register_rw('f', 0, 0);
  uint64_t cs = read_cs();
  printf("CS: %#qx\r\n", cs);

  // Decode some of the results from the above hex
  printf("\r\n");
  if(cr0 & 0x01)
  {
    printf("Protected mode is enabled. (CR0.PE = 1)\r\n");
  }
  if(cr0 & (1 << 31))
  {
    printf("Paging is enabled. (CR0.PG = 1)\r\n");
  }
  if(cr0 & (1 << 1))
  {
    printf("SSE: CR0.MP = 1\r\n");
  }
  else
  {
    printf("SSE: CR0.MP = 0, need to enable\r\n");
  }
  if(cr0 & (1 << 2))
  {
    printf("SSE: CR0.EM = 1, need to disable\r\n");
  }
  else
  {
    printf("SSE: CR0.EM = 0\r\n");
  }
  if(cr0 & (1 << 3))
  {
    printf("SSE: CR0.TS = 1, need to disable\r\n");
  }
  else
  {
    printf("SSE: CR0.TS = 0\r\n");
  }
  if(cr4 & (1 << 5))
  {
    printf("PAE is enabled. (CR4.PAE = 1)\r\n");
  }
  if(cr4 & (1 << 9))
  {
    printf("SSE: CR4.OSFXSR = 1\r\n");
  }
  else
  {
    printf("SSE: CR4.OSFXSR = 0\r\n");
  }
  if(cr4 & (1 << 10))
  {
    printf("SSE: CR4.OSXMMEXCPT = 1\r\n");
  }
  else
  {
    printf("SSE: CR4.OSXMMEXCPT = 0\r\n");
  }
  if(cr4 & (1 << 18))
  {
    printf("SSE/AVX: CR4.OSXSAVE = 1\r\n");
  }
  else
  {
    printf("SSE/AVX: CR4.OSXSAVE = 0\r\n");
  }
  // Verify we're in long mode (UEFI by default should have put us there)
  if((efer & 0x500) == 0x500)
  {
    printf("Long mode is enabled and active. (IA32e.LME = 1 & IA32e.LMA = 1)\r\n");
  }
  if(rflags & (1 << 9))
  {
    printf("Interrupts are enabled. (IF = 1)\r\n");
  }
  else
  {
    printf("Interrupts are disabled. (IF = 0)\r\n");
  }

  uint16_t gdt_index = cs >> 3; // This index is how many GDT_ENTRY_STRUCTs above GDT BaseAddress the current code segment is
  // Check if 64-bit mode's all set to go.
  DT_STRUCT gdt = get_gdtr(); // GDT is up to 64k from base addr, but CS points to max index in x86_64
  printf("GDTR addr: %#qx, limit: %#hx\r\n", gdt.BaseAddress, gdt.Limit);

  printf("CS GDT Entry: %#qx\r\n", ((GDT_ENTRY_STRUCT *)gdt.BaseAddress)[gdt_index]);
//  printf("CS GDT Entry: %#qx\r\n", *((GDT_ENTRY_STRUCT *)(gdt.BaseAddress + 8*gdt_index)) ); // Same thing

  if( (((GDT_ENTRY_STRUCT *)gdt.BaseAddress)[gdt_index].SegmentLimit2andMisc & (1 << 6)) == 0 ) // CS.D = 0 means "not in 32-bit mode" (either 16- or 64-bit mode)
  {
    if( ((GDT_ENTRY_STRUCT *)gdt.BaseAddress)[gdt_index].SegmentLimit2andMisc & (1 << 5) ) // CS.L = 1 means 64-bit mode if CS.D = 0.
    {
      printf("All good: 64-bit mode enabled. (CS.D = 0, CS.L = 1)\r\n");
    }
  }

  if(rflags2 == rflags)
  {
    printf("CPUID is not supported.\r\n");
  }
  else
  {
    printf("CPUID is supported.\r\n");
    printf("\r\n");
    printf("\r\n");
    cpu_features(0, 0);
    printf("\r\n");
    cpu_features(1, 0);
    printf("\r\n");
    cpu_features(7, 0);
    printf("\r\n");
    cpu_features(0x80000000, 0);
    printf("\r\n");
    cpu_features(0x0D, 0);
    printf("\r\n");
    cpu_features(0x0D, 1);
    printf("\r\n");
    cpu_features(0x80000001, 0);
    printf("\r\n");
    cpu_features(0x80000006, 0);
    printf("\r\n");
    cpu_features(0x80000008, 0);
    printf("\r\n");
  }


  // ASM so that GCC doesn't mess with this loop. This is about as optimized this can get.
  asm volatile("movl $1, %%eax\n\t" // Loop ends on overflow
                "Loop0:\n\t"
                "addl $1, %%eax\n\t"
                "testl %%eax, %%eax\n\t"
                "jne Loop0\n\t" // Count to 2^32, which is about 1 second at 4 GHz. 2^64 is... still insanely long.
                : // no outputs
                : // no inputs
                : // no clobbers
              );

  asm volatile("movl $1, %%eax\n\t" // Loop ends on overflow
                "Loop7:\n\t"
                "addl $1, %%eax\n\t"
                "testl %%eax, %%eax\n\t"
                "jne Loop7\n\t" // Count to 2^32, which is about 1 second at 4 GHz. 2^64 is... still insanely long.
                : // no outputs
                : // no inputs
                : // no clobbers
              );

  asm volatile("movl $1, %%eax\n\t" // Loop ends on overflow
                "Loop8:\n\t"
                "addl $1, %%eax\n\t"
                "testl %%eax, %%eax\n\t"
                "jne Loop8\n\t" // Count to 2^32, which is about 1 second at 4 GHz. 2^64 is... still insanely long.
                : // no outputs
                : // no inputs
                : // no clobbers
              );

  asm volatile("movl $1, %%eax\n\t" // Loop ends on overflow
                "Loop9:\n\t"
                "addl $1, %%eax\n\t"
                "testl %%eax, %%eax\n\t"
                "jne Loop9\n\t" // Count to 2^32, which is about 1 second at 4 GHz. 2^64 is... still insanely long.
                : // no outputs
                : // no inputs
                : // no clobbers
              );

  asm volatile("movl $1, %%eax\n\t" // Loop ends on overflow
                "Loop10:\n\t"
                "addl $1, %%eax\n\t"
                "testl %%eax, %%eax\n\t"
                "jne Loop10\n\t" // Count to 2^32, which is about 1 second at 4 GHz. 2^64 is... still insanely long.
                : // no outputs
                : // no inputs
                : // no clobbers
              );

  asm volatile("movl $1, %%eax\n\t" // Loop ends on overflow
                "Loop11:\n\t"
                "addl $1, %%eax\n\t"
                "testl %%eax, %%eax\n\t"
                "jne Loop11\n\t" // Count to 2^32, which is about 1 second at 4 GHz. 2^64 is... still insanely long.
                : // no outputs
                : // no inputs
                : // no clobbers
              );

  Colorscreen(LP->GPU_Configs->GPUArray[0], 0x000000FF); // Blue in BGRX (X = reserved, technically an "empty alpha channel" for 32-bit memory alignment)
  single_char(LP->GPU_Configs->GPUArray[0], '?', 8, 8, 0x00FFFFFF, 0x00000000);
  single_char_anywhere(LP->GPU_Configs->GPUArray[0], '!', 8, 8, 0x00FFFFFF, 0xFF000000, (LP->GPU_Configs->GPUArray[0].Info->HorizontalResolution >> 2), LP->GPU_Configs->GPUArray[0].Info->VerticalResolution/3);
  single_char_anywhere_scaled(LP->GPU_Configs->GPUArray[0], 'H', 8, 8, 0x00FFFFFF, 0xFF000000, 10, 10, 5);
  string_anywhere_scaled(LP->GPU_Configs->GPUArray[0], "Is it soup?", 8, 8, 0x00FFFFFF, 0x00000000, 10, 10, 1);

  // ASM so that GCC doesn't mess with this loop. This is about as optimized this can get.
  asm volatile("movl $1, %%eax\n\t" // Loop ends on overflow
                "Loop1:\n\t"
                "addl $1, %%eax\n\t"
                "testl %%eax, %%eax\n\t"
                "jne Loop1\n\t" // Count to 2^32, which is about 1 second at 4 GHz. 2^64 is... still insanely long.
                : // no outputs
                : // no inputs
                : // no clobbers
              );

  Colorscreen(LP->GPU_Configs->GPUArray[0], 0x0000FF00); // Green in BGRX (X = reserved, technically an "empty alpha channel" for 32-bit memory alignment)
  single_char(LP->GPU_Configs->GPUArray[0], 'A', 8, 8, 0x00FFFFFF, 0x00000000);
  single_char_anywhere(LP->GPU_Configs->GPUArray[0], '!', 8, 8, 0x00FFFFFF, 0xFF000000, (LP->GPU_Configs->GPUArray[0].Info->HorizontalResolution >> 2), LP->GPU_Configs->GPUArray[0].Info->VerticalResolution/3);
  string_anywhere_scaled(LP->GPU_Configs->GPUArray[0], "Is it really soup?", 8, 8, 0x00FFFFFF, 0x00000000, 50, 50, 3);

  // ASM so that GCC doesn't mess with this loop. This is about as optimized this can get.
  asm volatile("movl $1, %%eax\n\t" // Loop ends on overflow
                "Loop2:\n\t"
                "addl $1, %%eax\n\t"
                "testl %%eax, %%eax\n\t"
                "jne Loop2\n\t" // Count to 2^32, which is about 1 second at 4 GHz. 2^64 is... still insanely long.
                : // no outputs
                : // no inputs
                : // no clobbers
              );

  Colorscreen(LP->GPU_Configs->GPUArray[0], 0x00FF0000); // Red in BGRX (X = reserved, technically an "empty alpha channel" for 32-bit memory alignment)
  printf("PRINTF!! 0x%qx", LP->GPU_Configs->GPUArray[0].FrameBufferBase);
  printf("Whup %s\r\nOh.\r\n", "Yo%%nk");
  Global_Print_Info.scale = 4; // Output scale for systemfont used by printf
  printf("Hello this is a sentence how far does it go before it wraps around?\nA\nB\nC\nD\nE\nF\nG\nH\nI\nJ\nK\nL\nM\nN\nO\nP\nQ\nR\nS\nT\nU\nV\nW\nX\nY\nZ\nYAY");
  formatted_string_anywhere_scaled(LP->GPU_Configs->GPUArray[0], 8, 8, 0x00FFFFFF, 0x00000000, 0,  LP->GPU_Configs->GPUArray[0].Info->VerticalResolution/2, 2, "FORMATTED STRING!! %#x", Global_Print_Info.index);
  formatted_string_anywhere_scaled(LP->GPU_Configs->GPUArray[0], 8, 8, 0x00FFFFFF, 0x00000000, 0,  LP->GPU_Configs->GPUArray[0].Info->VerticalResolution/4, 2, "FORMATTED %s STRING!! %s", "Heyo!", "Heyz!");
  printf("This printf shouldn't move due to formatted string invocation.");
  single_char(LP->GPU_Configs->GPUArray[0], '2', 8, 8, 0x00FFFFFF, 0xFF000000);

  // TODO: test this
//			EFI_PHYSICAL_ADDRESS * base = (EFI_PHYSICAL_ADDRESS*)(arg->defaultGPU.FrameBufferBase);
//				EFI_PHYSICAL_ADDRESS * source = (EFI_PHYSICAL_ADDRESS*)(arg->defaultGPU.FrameBufferBase + arg->defaultGPU.Info->PixelsPerScanLine * 4);
//				EFI_PHYSICAL_ADDRESS * source = (EFI_PHYSICAL_ADDRESS*)(arg->defaultGPU.FrameBufferBase + 4000);

  //size_t copysize = (arg->defaultGPU.Info->PixelsPerScanLine - 1) * arg->defaultGPU.Info->VerticalResolution * 4;
//  EFI_PHYSICAL_ADDRESS source = LP->GPU_Configs->GPUArray[0].FrameBufferBase + 1024;
//  size_t copysize = 3; // memmove: crash on 32+, AVX_memmove: crash on 4+
//  AVX_memmove((UINT32 *)LP->GPU_Configs->GPUArray[0].FrameBufferBase, (UINT32*)(LP->GPU_Configs->GPUArray[0].FrameBufferBase + 1024*4*763), copysize);
  //
  //memset_256bit_u((UINT32 *)LP->GPU_Configs->GPUArray[0].FrameBufferBase, _mm256_set1_epi32(0x00FFFFFF), 1024*767*4 >> 5);
  // This works:
//   memset_32bit((UINT32 *)LP->GPU_Configs->GPUArray[0].FrameBufferBase, 0x00FFFFFF, 1024*767); // The framebuffer will crash if it's not written to with 32-bit uints.
  // memset_32bit((UINT32 *)LP->GPU_Configs->GPUArray[0].FrameBufferBase, 0x00FFFFFF, 1024*767*4 >> 2);
  // Writing to the reserved bits causes a crash.

  // ASM so that GCC doesn't mess with this loop. This is about as optimized this can get.
  asm volatile("movl $1, %%eax\n\t" // Loop ends on overflow
                "Loop3:\n\t"
                "addl $1, %%eax\n\t"
                "testl %%eax, %%eax\n\t"
                "jne Loop3\n\t" // Count to 2^32, which is about 1 second at 4 GHz. 2^64 is... still insanely long.
                : // no outputs
                : // no inputs
                : // no clobbers
              );

  asm volatile("movl $1, %%eax\n\t" // Loop ends on overflow
                "Loop5:\n\t"
                "addl $1, %%eax\n\t"
                "testl %%eax, %%eax\n\t"
                "jne Loop5\n\t" // Count to 2^32, which is about 1 second at 4 GHz. 2^64 is... still insanely long.
                : // no outputs
                : // no inputs
                : // no clobbers
              );

  asm volatile("movl $1, %%eax\n\t" // Loop ends on overflow
                "Loop6:\n\t"
                "addl $1, %%eax\n\t"
                "testl %%eax, %%eax\n\t"
                "jne Loop6\n\t" // Count to 2^32, which is about 1 second at 4 GHz. 2^64 is... still insanely long.
                : // no outputs
                : // no inputs
                : // no clobbers
              );

  Blackscreen(LP->GPU_Configs->GPUArray[0]); // X in BGRX (X = reserved, technically an "empty alpha channel" for 32-bit memory alignment)
  single_pixel(LP->GPU_Configs->GPUArray[0], LP->GPU_Configs->GPUArray[0].Info->HorizontalResolution >> 2, LP->GPU_Configs->GPUArray[0].Info->VerticalResolution >> 2, 0x00FFFFFF);
  single_char(LP->GPU_Configs->GPUArray[0], '@', 8, 8, 0x00FFFFFF, 0x00000000);
  single_char_anywhere(LP->GPU_Configs->GPUArray[0], '!', 8, 8, 0x00FFFFFF, 0xFF000000, 512, 512);
  single_char_anywhere_scaled(LP->GPU_Configs->GPUArray[0], 'I', 8, 8, 0x00FFFFFF, 0xFF000000, 10, 10, 2);
  string_anywhere_scaled(LP->GPU_Configs->GPUArray[0], "OMG it's actually soup! I don't believe it!!", 8, 8, 0x00FFFFFF, 0x00000000, 0, LP->GPU_Configs->GPUArray[0].Info->VerticalResolution/2, 2);

  // ASM so that GCC doesn't mess with this loop. This is about as optimized this can get.
  asm volatile("movl $1, %%eax\n\t" // Loop ends on overflow
                "Loop4:\n\t"
                "addl $1, %%eax\n\t"
                "testl %%eax, %%eax\n\t"
                "jne Loop4\n\t" // Count to 2^32, which is about 1 second at 4 GHz. 2^64 is... still insanely long.
                : // no outputs
                : // no inputs
                : // no clobbers
              );

  LP->RTServices->ResetSystem(EfiResetShutdown, EFI_SUCCESS, 0, NULL); // Shutdown the system
}

void cpu_features(uint64_t rax_value, uint64_t rcx_value) // TODO
{
  // x86 does not memory-map control registers, unlike, for example, STM32 chips
  // System control registers like CR0, CR1, CR2, CR3, CR4, CR8, and EFLAGS
  // are accessed via special asm instructions.
  uint64_t rax, rbx, rcx, rdx = 0;
  printf("CPUID input rax: %#qx, rcx: %#qx\r\n\n", rax_value, rcx_value);

  if(rax_value == 0)
  {
    asm volatile("cpuid"
                 : "=a" (rax), "=b" (rbx), "=c" (rcx), "=d" (rdx) // Outputs
                 : "a" (rax_value) // The value to put into %rax
                 : // CPUID would clobber any of the abcd registers not listed explicitly (all are here, though)
               );

    printf("rax: %#qx\r\n%c%c%c%c%c%c%c%c%c%c%c%c\r\n", rax,
    ((char *)&rbx)[0], ((char *)&rbx)[1], ((char *)&rbx)[2], ((char *)&rbx)[3],
    ((char *)&rdx)[0], ((char *)&rdx)[1], ((char *)&rdx)[2], ((char *)&rdx)[3],
    ((char *)&rcx)[0], ((char *)&rcx)[1], ((char *)&rcx)[2], ((char *)&rcx)[3]);
    // GenuineIntel
    // AuthenticAMD
  }
  else if(rax_value == 1)
  {
    asm volatile("cpuid"
                 : "=a" (rax), "=b" (rbx), "=c" (rcx), "=d" (rdx) // Outputs
                 : "a" (rax_value) // The value to put into %rax
                 : // CPUID would clobber any of the abcd registers not listed explicitly (all are here, though)
               );

    printf("rax: %#qx\r\nrbx: %#qx\r\nrcx: %#qx\r\nrdx: %#qx\r\n", rax, rbx, rcx, rdx);
    if(rcx >> 31) // 1 means hypervisor (i.e. in a VM), 0 means no hypervisor
    {
      printf("You're in a hypervisor!\r\n");
    }
  }
  else if(rax_value == 7 && rcx_value == 0)
  {
    asm volatile("cpuid"
                 : "=a" (rax), "=b" (rbx), "=c" (rcx), "=d" (rdx) // Outputs
                 : "a" (rax_value), "c" (rcx_value) // The values to put into %rax and %rcx
                 : // CPUID would clobber any of the abcd registers not listed explicitly (all are here, though)
              );

    printf("rax: %#qx\r\nrbx: %#qx\r\nrcx: %#qx\r\nrdx: %#qx\r\n", rax, rbx, rcx, rdx);
  }
  else if(rax_value == 0x80000000)
  {
    asm volatile("cpuid"
                 : "=a" (rax), "=b" (rbx), "=c" (rcx), "=d" (rdx) // Outputs
                 : "a" (rax_value) // The value to put into %rax
                 : // CPUID would clobber any of the abcd registers not listed explicitly (all are here, though)
              );
    if(rax >= 0x80000004)
    {
      uint32_t brandstring[12];

      rax_value = 0x80000002;
      asm volatile("cpuid"
                   : "=a" (rax), "=b" (rbx), "=c" (rcx), "=d" (rdx) // Outputs
                   : "a" (rax_value) // The value to put into %rax
                   : // CPUID would clobber any of the abcd registers not listed explicitly (all are here, though)
                );

      brandstring[0] = ((uint32_t *)&rax)[0];
      brandstring[1] = ((uint32_t *)&rbx)[0];
      brandstring[2] = ((uint32_t *)&rcx)[0];
      brandstring[3] = ((uint32_t *)&rdx)[0];

      rax_value = 0x80000003;
      asm volatile("cpuid"
                   : "=a" (rax), "=b" (rbx), "=c" (rcx), "=d" (rdx) // Outputs
                   : "a" (rax_value) // The value to put into %rax
                   : // CPUID would clobber any of the abcd registers not listed explicitly (all are here, though)
                );

      brandstring[4] = ((uint32_t *)&rax)[0];
      brandstring[5] = ((uint32_t *)&rbx)[0];
      brandstring[6] = ((uint32_t *)&rcx)[0];
      brandstring[7] = ((uint32_t *)&rdx)[0];

      rax_value = 0x80000004;
      asm volatile("cpuid"
                   : "=a" (rax), "=b" (rbx), "=c" (rcx), "=d" (rdx) // Outputs
                   : "a" (rax_value) // The value to put into %rax
                   : // CPUID would clobber any of the abcd registers not listed explicitly (all are here, though)
                );

      brandstring[8] = ((uint32_t *)&rax)[0];
      brandstring[9] = ((uint32_t *)&rbx)[0];
      brandstring[10] = ((uint32_t *)&rcx)[0];
      brandstring[11] = ((uint32_t *)&rdx)[0];

      printf("Brand String: %.48s\r\n", (char *)brandstring);
    }
    else
    {
      printf("Brand string not supported\r\n");
    }
  }
  else
  {
    asm volatile("cpuid"
                 : "=a" (rax), "=b" (rbx), "=c" (rcx), "=d" (rdx) // Outputs
                 : "a" (rax_value) // The value to put into %rax
                 : // CPUID would clobber any of the abcd registers not listed explicitly (all are here, though)
               );

    printf("rax: %#qx\r\nrbx: %#qx\r\nrcx: %#qx\r\nrdx: %#qx\r\n", rax, rbx, rcx, rdx);
  }
  // Check for AVX2 or AVX support for AVX memmoves.
}

uint64_t read_cs(void)
{
  uint64_t output = 0;
  asm volatile("mov %%cs, %[dest]"
                : [dest] "=r" (output)
                : // no inputs
                : // no clobbers
                );
  return output;
}

uint64_t get_tick(void) // Finally a way to tell time! Ticks since last CPU reset.
{
    uint32_t high, low = 0;
    asm volatile("rdtscp"
                  : "=a" (low), "=d" (high)
                  : // no inputs
                  : "%rcx"// clobbers
                  );
    return ((uint64_t)high << 32 | low);
}

void enable_AVX(void)
{
  // AVX needs to be enabled before use.
  // if XSAVE, check OSXSAVE & check AVX/AVX2 support (they're in different regs)
  // if AVX supported and OSXSAVE is 0, use XSETBV 7 to enable AVX(2)
  // Then confirm by checking OSXSAVE again

  // If OSXSAVE allows, do this:
#ifdef __AVX512__
  xcr_rw(0, 0xE7, 1);
#elif __AVX__
  xcr_rw(0, 7, 1);
#endif
}

void enable_interrupts(void)
{
  // Check if disabled, if disabled then enable them
  // pushfq gives rflags
  // These might already be enabled, but there's no interrupt table?
}

uint64_t control_register_rw(int crX, uint64_t in_out, int rw) // Read from or write to a control register
{
  // in_out: writes this value if rw = 1
  // rw: 0 = read, 1 = write
  if(rw == 1) // Write
  {
    switch(crX)
    {
      case(0):
        asm volatile("mov %[dest], %%cr0"
                     : // No outputs
                     : [dest] "r" (in_out) // Inputs
                     : // No clobbers
                   );
        break;
      case(1):
        asm volatile("mov %[dest], %%cr1"
                     : // No outputs
                     : [dest] "r" (in_out) // Inputs
                     : // No clobbers
                   );
        break;
      case(2):
        asm volatile("mov %[dest], %%cr2"
                     : // No outputs
                     : [dest] "r" (in_out) // Inputs
                     : // No clobbers
                   );
        break;
      case(3):
        asm volatile("mov %[dest], %%cr3"
                     : // No outputs
                     : [dest] "r" (in_out) // Inputs
                     : // No clobbers
                   );
        break;
      case(4):
        asm volatile("mov %[dest], %%cr4"
                     : // No outputs
                     : [dest] "r" (in_out) // Inputs
                     : // No clobbers
                   );
        break;
      case(8):
        asm volatile("mov %[dest], %%cr8"
                     : // No outputs
                     : [dest] "r" (in_out) // Inputs
                     : // No clobbers
                   );
        break;
      case('f'):
        asm volatile("pushq %[dest]\n\t"
                     "popfq"
                     : // No outputs
                     : [dest] "r" (in_out) // Inputs
                     : "cc" // Control codes get clobbered
                   );
        break;
      default:
        // Nothing
        break;
    }
  }
  else // Read
  {
    switch(crX)
    {
      case(0):
        asm volatile("mov %%cr0, %[dest]"
                     : [dest] "=r" (in_out) // Outputs
                     : // No inputs
                     : // No clobbers
                   );
        break;
      case(1):
        asm volatile("mov %%cr1, %[dest]"
                     : [dest] "=r" (in_out) // Outputs
                     : // No inputs
                     : // No clobbers
                   );
        break;
      case(2):
        asm volatile("mov %%cr2, %[dest]"
                     : [dest] "=r" (in_out) // Outputs
                     : // No inputs
                     : // No clobbers
                   );
        break;
      case(3):
        asm volatile("mov %%cr3, %[dest]"
                     : [dest] "=r" (in_out) // Outputs
                     : // No inputs
                     : // No clobbers
                   );
        break;
      case(4):
        asm volatile("mov %%cr4, %[dest]"
                     : [dest] "=r" (in_out) // Outputs
                     : // No inputs
                     : // No clobbers
                   );
        break;
      case(8):
        asm volatile("mov %%cr8, %[dest]"
                     : [dest] "=r" (in_out) // Outputs
                     : // No inputs
                     : // No clobbers
                   );
        break;
      case('f'):
        asm volatile("pushfq\n\t"
                     "pop %[dest]"
                     : [dest] "=r" (in_out) // Outputs
                     : // No inputs
                     : // No clobbers
                   );
        break;
      default:
        // Nothing
        break;
    }
  }

  return in_out;
}

uint64_t msr_rw(uint64_t msr, uint64_t data, int rw)
{
  // rw: 0 = read, 1 = write
  // data is ignored for reads
  uint32_t high, low = 0;

  if(rw == 1) // Write
  {
    low = ((uint32_t *)&data)[0];
    high = ((uint32_t *)&data)[1];
    asm volatile("wrmsr"
             : // No outputs
             : "a" ((uint64_t)low), "c" (msr), "d" ((uint64_t)high) // Input MSR into %rcx, and high (%rdx) & low (%rax) to write
             : // No clobbers
           );
  }
  else // Read
  {
    asm volatile("rdmsr"
             : "=a" (low), "=d" (high) // Outputs
             : "c" (msr) // Input MSR into %rcx
             : // No clobbers
           );
  }
  return ((uint64_t)high << 32 | low); // For write, this will be data. Reads will be the msr's value.
}

uint64_t xcr_rw(uint64_t xcr, uint64_t data, int rw)
{
  // rw: 0 = read, 1 = write
  // data is ignored for reads
  uint32_t high, low = 0;

  if(rw == 1) // Write
  {
    low = ((uint32_t *)&data)[0];
    high = ((uint32_t *)&data)[1];
    asm volatile("xsetbv"
             : // No outputs
             : "a" ((uint64_t)low), "c" (xcr), "d" ((uint64_t)high) // Input XCR# into %rcx, and high (%rdx) & low (%rax) to write
             : // No clobbers
           );
  }
  else // Read
  {
    asm volatile("xgetbv"
             : "=a" (low), "=d" (high) // Outputs
             : "c" (xcr) // Input XCR# into %rcx
             : // No clobbers
           );
  }
  return ((uint64_t)high << 32 | low); // For write, this will be data. Reads will be the msr's value.
}

DT_STRUCT get_gdtr(void)
{
  DT_STRUCT gdtr_data;
  asm volatile("sgdt %[dest]"
           : [dest] "=m" (gdtr_data) // Outputs
           : // No inputs
           : // No clobbers
         );

  return gdtr_data;
}

void Initialize_Global_Printf_Defaults(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GPU)
{
  // Set global default print information--needed for printf
  Global_Print_Info.defaultGPU = GPU;
  Global_Print_Info.height = 8; // Character font height
  Global_Print_Info.width = 8; // Character font width (in bits)
  Global_Print_Info.font_color = 0x00FFFFFF; // Default font color
  Global_Print_Info.highlight_color = 0x00000000; // Default highlight color
  Global_Print_Info.background_color = 0x00000000; // Default background color
  Global_Print_Info.x = 0; // Leftmost x-coord that's in-bounds (NOTE: per UEFI Spec 2.7 Errata A, (0,0) is always the top left in-bounds pixel.)
  Global_Print_Info.y = 0; // Topmost y-coord
  Global_Print_Info.scale = 1; // Output scale for systemfont used by printf
  Global_Print_Info.index = 0; // Global string index for printf, etc. to keep track of cursor's postion in the framebuffer
  Global_Print_Info.textscrollmode = 1; // What to do when a newline goes off the bottom of the screen: 0 = scroll entire screen, 1 = wrap around to the top
  Colorscreen(GPU, Global_Print_Info.background_color);
}

void Resetdefaultscreen(void)
{
  Global_Print_Info.y = 0;
  Global_Print_Info.index = 0;
  Blackscreen(Global_Print_Info.defaultGPU);
}

void Resetdefaultcolorscreen(void)
{
  Global_Print_Info.y = 0;
  Global_Print_Info.index = 0;
  Colorscreen(Global_Print_Info.defaultGPU, Global_Print_Info.background_color);
}

void Blackscreen(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GPU)
{
  Colorscreen(GPU, 0x00000000);
}

// This will only color the visible area
void Colorscreen(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GPU, UINT32 color)
{
  UINT32 row, col;
  UINT32 backporch = GPU.Info->PixelsPerScanLine - GPU.Info->HorizontalResolution; // The area offscreen is the back porch. Sometimes it's 0.
  for (row = 0; row < GPU.Info->VerticalResolution; row++)
  {
    for (col = 0; col < (GPU.Info->PixelsPerScanLine - backporch); col++) // Per UEFI Spec 2.7 Errata A, framebuffer address 0 coincides with the top leftmost pixel. i.e. screen padding is only HorizontalResolution + porch.
    {
      *(UINT32*)(GPU.FrameBufferBase + 4 * (GPU.Info->PixelsPerScanLine * row + col)) = color; // The thing at FrameBufferBase is an address pointing to UINT32s. FrameBufferBase itself is a 64-bit number.
    }
  }
// Leaving this here for posterity. The framebuffer size might be a fair bit larger than the visible area (possibly for scrolling support? Regardless, some are just the size of the native resolution).
/*  for(UINTN i = 0; i < GPU.FrameBufferSize; i+=4) //32 bpp == 4 Bpp
  {
    *(UINT32*)(GPU.FrameBufferBase + i) = color; // FrameBufferBase is a 64-bit address that points to UINT32s.
  }
*/
}

// Screen turns red if a pixel is put outside the visible area.
void single_pixel(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GPU, UINT32 x, UINT32 y, UINT32 color)
{
  if(y > GPU.Info->VerticalResolution || x > GPU.Info->HorizontalResolution) // Need some kind of error indicator (makes screen red)
  {
    Colorscreen(GPU, 0x00FF0000);
  }

  *(UINT32*)(GPU.FrameBufferBase + (y * GPU.Info->PixelsPerScanLine + x) * 4) = color;
//  Output_render(GPU, 0x01, 1, 1, color, 0xFF000000, x, y, 1, 0); // Make highlight transparent to skip that part of output render (transparent = no highlight)
}

void single_char(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GPU, int character, UINT32 height, UINT32 width, UINT32 font_color, UINT32 highlight_color)
{
  // Height in number of bytes and width in number of bits
  // Assuming "character" is an array of bytes, e.g. { 0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00}, which is U+0040 (@). This is an 8x8 '@' sign.
  if(height > GPU.Info->VerticalResolution || width > GPU.Info->HorizontalResolution)
  {
    Colorscreen(GPU, 0x00FF0000); // Need some kind of error indicator (makes screen red)
  } // Could use an instruction like ARM's USAT to truncate values

  Output_render_text(GPU, character, height, width, font_color, highlight_color, 0, 0, 1, 0);
}

void single_char_anywhere(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GPU, int character, UINT32 height, UINT32 width, UINT32 font_color, UINT32 highlight_color, UINT32 x, UINT32 y)
{
  // x and y are top leftmost coordinates
  // Height in number of bytes and width in number of bits
  // Assuming "character" is an array of bytes, e.g. { 0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00}, which is U+0040 (@). This is an 8x8 '@' sign.
  if(height > GPU.Info->VerticalResolution || width > GPU.Info->HorizontalResolution)
  {
    Colorscreen(GPU, 0x00FF0000); // Need some kind of error indicator (makes screen red)
  } // Could use an instruction like ARM's USAT to truncate values
  else if(x > GPU.Info->HorizontalResolution || y > GPU.Info->VerticalResolution)
  {
    Colorscreen(GPU, 0x0000FF00); // Makes screen green
  }
  else if ((y + height) > GPU.Info->VerticalResolution || (x + width) > GPU.Info->HorizontalResolution)
  {
    Colorscreen(GPU, 0x000000FF); // Makes screen blue
  }

  Output_render_text(GPU, character, height, width, font_color, highlight_color, x, y, 1, 0);
}

// Arbitrarily-sized fonts should work, too.
void single_char_anywhere_scaled(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GPU, int character, UINT32 height, UINT32 width, UINT32 font_color, UINT32 highlight_color, UINT32 x, UINT32 y, UINT32 scale)
{
  // scale is an integer scale factor, e.g. 2 for 2x, 3 for 3x, etc.
  // x and y are top leftmost coordinates
  // Height in number of bytes and width in number of bits
  // Assuming "character" is an array of bytes, e.g. { 0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00}, which is U+0040 (@). This is an 8x8 '@' sign.
  if(height > GPU.Info->VerticalResolution || width > GPU.Info->HorizontalResolution)
  {
    Colorscreen(GPU, 0x00FF0000); // Need some kind of error indicator (makes screen red)
  } // Could use an instruction like ARM's USAT to truncate values
  else if(x > GPU.Info->HorizontalResolution || y > GPU.Info->VerticalResolution)
  {
    Colorscreen(GPU, 0x0000FF00); // Makes screen green
  }
  else if ((y + scale*height) > GPU.Info->VerticalResolution || (x + scale*width) > GPU.Info->HorizontalResolution)
  {
    Colorscreen(GPU, 0x000000FF); // Makes screen blue
  }

  Output_render_text(GPU, character, height, width, font_color, highlight_color, x, y, scale, 0);
}

// Version for images. Just pass in an appropriately-formatted array of bytes as the 'character' pointer.
void bitmap_anywhere_scaled(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GPU, const unsigned char * bitmap, UINT32 height, UINT32 width, UINT32 font_color, UINT32 highlight_color, UINT32 x, UINT32 y, UINT32 scale)
{
  // scale is an integer scale factor, e.g. 2 for 2x, 3 for 3x, etc.
  // x and y are top leftmost coordinates
  // Height in number of bytes and width in number of bits
  // Assuming "character" is an array of bytes, e.g. { 0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00}, which is U+0040 (@). This is an 8x8 '@' sign.
  if(height > GPU.Info->VerticalResolution || width > GPU.Info->HorizontalResolution)
  {
    Colorscreen(GPU, 0x00FF0000); // Need some kind of error indicator (makes screen red)
  } // Could use an instruction like ARM's USAT to truncate values
  else if(x > GPU.Info->HorizontalResolution || y > GPU.Info->VerticalResolution)
  {
    Colorscreen(GPU, 0x0000FF00); // Makes screen green
  }
  else if ((y + scale*height) > GPU.Info->VerticalResolution || (x + scale*width) > GPU.Info->HorizontalResolution)
  {
    Colorscreen(GPU, 0x000000FF); // Makes screen blue
  }

  Output_render_bitmap(GPU, bitmap, height, width, font_color, highlight_color, x, y, scale, 0);
}

// literal strings in C are automatically null-terminated. i.e. "hi" is actually 3 characters long.
// This function allows direct output of a pre-made string, either a hardcoded one or one made via sprintf.
void string_anywhere_scaled(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GPU, const char * string, UINT32 height, UINT32 width, UINT32 font_color, UINT32 highlight_color, UINT32 x, UINT32 y, UINT32 scale)
{
  // scale is an integer scale factor, e.g. 2 for 2x, 3 for 3x, etc.
  // x and y are top leftmost coordinates
  // Height in number of bytes and width in number of bits, per character
  // Assuming "character" is an array of bytes, e.g. { 0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00}, which is U+0040 (@). This is an 8x8 '@' sign.
  if(height > GPU.Info->VerticalResolution || width > GPU.Info->HorizontalResolution)
  {
    Colorscreen(GPU, 0x00FF0000); // Need some kind of error indicator (makes screen red)
  } // Could use an instruction like ARM's USAT to truncate values
  else if(x > GPU.Info->HorizontalResolution || y > GPU.Info->VerticalResolution)
  {
    Colorscreen(GPU, 0x0000FF00); // Makes screen green
  }
  else if ((y + scale*height) > GPU.Info->VerticalResolution || (x + scale*width) > GPU.Info->HorizontalResolution)
  {
    Colorscreen(GPU, 0x000000FF); // Makes screen blue
  }

  //mapping: x*4 + y*4*(PixelsPerScanLine), x is column number, y is row number; every 4*PixelsPerScanLine bytes is a new row
  // y max is VerticalResolution, x max is HorizontalResolution, 'x4' is the LFB expansion, since 1 bit input maps to 4 bytes output

  // A 2x scale should turn 1 pixel into a square of 2x2 pixels
// iterate through characters in string
// start while
  uint32_t index = 0;
  while(string[index] != '\0') // for char in string until \0
  {
    // match the character to the font, using UTF-8.
    // This would expect a font array to include everything in the UTF-8 character set... Or just use the most common ones.
    Output_render_text(GPU, string[index], height, width, font_color, highlight_color, x, y, scale, index);
    index++;
  } // end while
} // end function

// A massively customizable printf-like function. Supports everything printf supports.
void formatted_string_anywhere_scaled(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GPU, UINT32 height, UINT32 width, UINT32 font_color, UINT32 highlight_color, UINT32 x, UINT32 y, UINT32 scale, const char * string, ...)
{
  // scale is an integer scale factor, e.g. 2 for 2x, 3 for 3x, etc.
  // x and y are top leftmost coordinates
  // Height in number of bytes and width in number of bits, per character where "character" is an array of bytes, e.g. { 0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00}, which is U+0040 (@). This is an 8x8 '@' sign.
  if(height > GPU.Info->VerticalResolution || width > GPU.Info->HorizontalResolution)
  {
    Colorscreen(GPU, 0x00FF0000); // Need some kind of error indicator (makes screen red)
  } // Could use an instruction like ARM's USAT to truncate values
  else if(x > GPU.Info->HorizontalResolution || y > GPU.Info->VerticalResolution)
  {
    Colorscreen(GPU, 0x0000FF00); // Makes screen green
  }
  else if ((y + scale*height) > GPU.Info->VerticalResolution || (x + scale*width) > GPU.Info->HorizontalResolution)
  {
    Colorscreen(GPU, 0x000000FF); // Makes screen blue
  }
  va_list arguments;

  va_start(arguments, string);
  ssize_t buffersize = vsnprintf(NULL, 0, string, arguments); // Get size of needed buffer
  char output_string[buffersize + 1]; // (v)snprintf does not account for \0
  vsprintf(output_string, string, arguments); // Write string to buffer
  va_end(arguments);

  string_anywhere_scaled(GPU, output_string, height, width, font_color, highlight_color, x, y, scale);
} // end function

void Output_render_text(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GPU, int character, UINT32 height, UINT32 width, UINT32 font_color, UINT32 highlight_color, UINT32 x, UINT32 y, UINT32 scale, UINT32 index)
{
  // Compact ceiling function, so that size doesn't need to be passed in
  // This should be faster than a divide followed by a mod
  uint32_t row_iterator = width >> 3;
  if((width & 0x7) != 0)
  {
    row_iterator++;
  } // Width should never be zero, so the iterator will always be at least 1
  uint32_t i;

  for(uint32_t row = 0; row < height; row++) // for number of rows in the character of the fontarray
  {
    i = 0;
    for (uint32_t bit = 0; bit < width; bit++) // for bit in column
    {
      if(((bit & 0x7) == 0 ) && (bit > 0))
      {
        i++;
      }
      // if a 1, output NOTE: There's gotta be a better way to do this.
//      if( character[row*row_iterator + i] & (1 << (7 - (bit & 0x7))) )
      if((systemfont[character][row*row_iterator + i] >> (bit & 0x7)) & 0x1)
      {
        // start scale here
        for(uint32_t b = 0; b < scale; b++)
        {
          for(uint32_t a = 0; a < scale; a++)
          {
            *(UINT32*)(GPU.FrameBufferBase + ((y*GPU.Info->PixelsPerScanLine + x) + scale*(row*GPU.Info->PixelsPerScanLine + bit) + (b*GPU.Info->PixelsPerScanLine + a) + scale * index * width)*4) = font_color;
          }
        } //end scale here
      } //end if
      else // if a 0, background or skip (set highlight_color to 0xFF000000 for transparency)
      {
        // start scale here
        for(uint32_t b = 0; b < scale; b++)
        {
          for(uint32_t a = 0; a < scale; a++)
          {
            if(highlight_color != 0xFF000000) // Transparency "color"
            {
              *(UINT32*)(GPU.FrameBufferBase + ((y*GPU.Info->PixelsPerScanLine + x) + scale*(row*GPU.Info->PixelsPerScanLine + bit) + (b*GPU.Info->PixelsPerScanLine + a) + scale * index * width)*4) = highlight_color;
            }
          }
        } //end scale here
      } // end else
    } // end bit in column
  } // end byte in row
}

void Output_render_bitmap(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GPU, const unsigned char * bitmap, UINT32 height, UINT32 width, UINT32 font_color, UINT32 highlight_color, UINT32 x, UINT32 y, UINT32 scale, UINT32 index)
{
  // Compact ceiling function, so that size doesn't need to be passed in
  // This should be faster than a divide followed by a mod
  uint32_t row_iterator = width >> 3;
  if((width & 0x7) != 0)
  {
    row_iterator++;
  } // Width should never be zero, so the iterator will always be at least 1
  uint32_t i;

  for(uint32_t row = 0; row < height; row++) // for number of rows in the character of the fontarray
  {
    i = 0;
    for (uint32_t bit = 0; bit < width; bit++) // for bit in column
    {
      if(((bit & 0x7) == 0 ) && (bit > 0))
      {
        i++;
      }
      // if a 1, output NOTE: There's gotta be a better way to do this.
//      if( character[row*row_iterator + i] & (1 << (7 - (bit & 0x7))) )
      if((bitmap[row*row_iterator + i] >> (bit & 0x7)) & 0x1) // This is pretty much the only difference from Output_render_text
      {
        // start scale here
        for(uint32_t b = 0; b < scale; b++)
        {
          for(uint32_t a = 0; a < scale; a++)
          {
            *(UINT32*)(GPU.FrameBufferBase + ((y*GPU.Info->PixelsPerScanLine + x) + scale*(row*GPU.Info->PixelsPerScanLine + bit) + (b*GPU.Info->PixelsPerScanLine + a) + scale * index * width)*4) = font_color;
          }
        } //end scale here
      } //end if
      else // if a 0, background or skip (set highlight_color to 0xFF000000 for transparency)
      {
        // start scale here
        for(uint32_t b = 0; b < scale; b++)
        {
          for(uint32_t a = 0; a < scale; a++)
          {
            if(highlight_color != 0xFF000000) // Transparency "color"
            {
              *(UINT32*)(GPU.FrameBufferBase + ((y*GPU.Info->PixelsPerScanLine + x) + scale*(row*GPU.Info->PixelsPerScanLine + bit) + (b*GPU.Info->PixelsPerScanLine + a) + scale * index * width)*4) = highlight_color;
            }
          }
        } //end scale here
      } // end else
    } // end bit in column
  } // end byte in row
}

// TODO: Finish/optimize bitmap renderers first.
// Maybe combine output renders into one with a 't' 'b' or 'v' mode.
// This could use AVX
/*
void Output_render_vector(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GPU, UINT32 x_init, UINT32 y_init, UINT32 x_final, UINT32 y_final, UINT32 color, UINT32 scale)
{

}
*/

// Swaps the high 4 bits with the low 4 bits in each byte of an array
void bitmap_bitswap(const unsigned char * bitmap, UINT32 height, UINT32 width, unsigned char * output)
{
  uint32_t row_iterator = width >> 3;
  if((width & 0x7) != 0)
  {
    row_iterator++;
  } // Width should never be zero, so the iterator will always be at least 1

  for(uint32_t iter = 0; iter < height*row_iterator; iter++) // Flip one byte at a time
  {
    output[iter] = (((bitmap[iter] >> 4) & 0xF) | ((bitmap[iter] & 0xF) << 4));
  }
}

// Will invert each individual byte in an array: 12345678 --> 87654321
void bitmap_bitreverse(const unsigned char * bitmap, UINT32 height, UINT32 width, unsigned char * output)
{
  uint32_t row_iterator = width >> 3;
  if((width & 0x7) != 0)
  {
    row_iterator++;
  } // Width should never be zero, so the iterator will always be at least 1

  for(uint32_t iter = 0; iter < height*row_iterator; iter++) // Invert one byte at a time
  {
    output[iter] = 0;
    for(uint32_t bit = 0; bit < 8; bit++)
    {
      if( bitmap[iter] & (1 << (7 - bit)) )
      {
        output[iter] += (1 << bit);
      }
    }
  }
}

// Requires rectangular arrays, and will create a horizontal reflection of entire array (like looking in a mirror)
void bitmap_bytemirror(const unsigned char * bitmap, UINT32 height, UINT32 width, unsigned char * output) // Width in bits, height in bytes
{
  uint32_t row_iterator = width >> 3;
  if((width & 0x7) != 0)
  {
    row_iterator++;
  } // Width should never be zero, so the iterator will always be at least 1

  uint32_t iter, parallel_iter;
  if(row_iterator & 0x01)
  {// Odd number of bytes per row
    for(iter = 0, parallel_iter = row_iterator - 1; (iter + (row_iterator >> 1) + 1) < height*row_iterator; iter++, parallel_iter--) // Mirror one byte at a time
    {
      if(iter == parallel_iter) // Integer divide, (iter%row_iterator == row_iterator/2 - 1)
      {
        iter += (row_iterator >> 1) + 1; // Hop the middle byte
        parallel_iter = iter + row_iterator - 1;
      }

      output[iter] = bitmap[parallel_iter]; // parallel_iter must mirror iter
      output[parallel_iter] = bitmap[iter];
    }
  }
  else
  {// Even number of bytes per row
    for(iter = 0, parallel_iter = row_iterator - 1; (iter + (row_iterator >> 1)) < height*row_iterator; iter++, parallel_iter--) // Mirror one byte at a time
    {
      if(iter - 1 == parallel_iter) // Integer divide, (iter%row_iterator == row_iterator/2 - 1)
      {
        iter += (row_iterator >> 1); // Skip to the next row to swap
        parallel_iter = iter + row_iterator - 1; // Appropriately position parallel_iter based on iter
      }

      output[iter] = bitmap[parallel_iter]; // Parallel_iter must mirror iter
      output[parallel_iter] = bitmap[iter];
    }
  }
}

////////////////////////////////////////////////////

// TODO: Once get video output for printing, check if we're in long mode
// TODO: keyboard driver (PS/2 for starters, then USB)

/*
Note:

typedef enum {
  EfiResetCold, // Power cycle (hard off/on)
  EfiResetWarm, //
  EfiResetShutdown // Uh, normal shutdown.
} EFI_RESET_TYPE;

typedef
VOID
ResetSystem (IN EFI_RESET_TYPE ResetType, IN EFI_STATUS ResetStatus, IN UINTN DataSize, IN VOID *ResetData OPTIONAL);

RT->ResetSystem (EfiResetShutdown, EFI_SUCCESS, 0, NULL); // Shutdown the system
RT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL); // Hard reboot the system - the familiar restart
RT->ResetSystem (EfiResetWarm, EFI_SUCCESS, 0, NULL); // Soft reboot the system -

There is also EfiResetPlatformSpecific, but that's not really important. (Takes a standard 128-bit EFI_GUID as ResetData for a custom reset type)
*/
