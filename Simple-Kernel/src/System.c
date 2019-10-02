//==================================================================================================================================
//  Simple Kernel: System Initialization
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
// This file contains post-UEFI initialization functions, as well as register access functions, for x86-64 CPUs.
// Intel CPUs made before 2011 (i.e. earlier than Sandy Bridge) may work with most of these, but they're not officially supported.
// AMD CPUs older than Ryzen are also not officially supported, even though they may also work with most of these.
//

#include "Kernel64.h"
#include "avxmem.h"

static void cs_update(void);
static void set_interrupt_entry(uint64_t isr_num, uint64_t isr_addr);
static void set_trap_entry(uint64_t isr_num, uint64_t isr_addr);
static void set_unused_entry(uint64_t isr_num);
static void set_NMI_interrupt_entry(uint64_t isr_num, uint64_t isr_addr);
static void set_DF_interrupt_entry(uint64_t isr_num, uint64_t isr_addr);
static void set_MC_interrupt_entry(uint64_t isr_num, uint64_t isr_addr);
static void set_BP_interrupt_entry(uint64_t isr_num, uint64_t isr_addr);

//----------------------------------------------------------------------------------------------------------------------------------
// System_Init: Initial Setup
//----------------------------------------------------------------------------------------------------------------------------------
//
// Initial setup after UEFI handoff
//

__attribute__((target("no-sse"))) void System_Init(LOADER_PARAMS * LP)
{
  // This memory initialization stuff needs to go first.
  Global_Memory_Info.MemMap = LP->Memory_Map;
  Global_Memory_Info.MemMapSize = LP->Memory_Map_Size;
  Global_Memory_Info.MemMapDescriptorSize = LP->Memory_Map_Descriptor_Size;
  Global_Memory_Info.MemMapDescriptorVersion = LP->Memory_Map_Descriptor_Version;
  // Apparently some systems won't totally leave you be without setting a virtual address map (https://www.spinics.net/lists/linux-efi/msg14108.html
  // and https://mjg59.dreamwidth.org/3244.html). Well, fine; identity map it now and fuhgetaboutit.
  // This will modify the memory map (but not its size), and set Global_Memory_Info.MemMap.

  if((EFI_PHYSICAL_ADDRESS)Set_Identity_VMAP(LP->RTServices) == ~0ULL) // Check for failure
  {
    Global_Memory_Info.MemMap = LP->Memory_Map; // No virtual addressing possible, evidently. Reset the map to how it was before.
  }
  // Don't merge any regions on the map until after SetVirtualAddressMap() has been called. After that call, it can be modified safely.

  // This function call is required to initialize printf. Set default GPU as GPU 0.
  // It can also be used to reset all global printf values and reassign a new default GPU at any time.
  Initialize_Global_Printf_Defaults(LP->GPU_Configs->GPUArray[0]);
  // Technically, printf is immediately usable now, as long as no scrolling, which uses AVX, is needed

  Enable_AVX(); // ENABLING AVX ASAP
  // All good now. Printf to your heart's content.

  // I know this CR0.NE bit isn't always set by default. Set it.
  // Generate and handle exceptions in the modern way, per Intel SDM
  uint64_t cr0 = control_register_rw(0, 0, 0);
  if(!(cr0 & (1 << 5)))
  {
    uint64_t cr0_2 = cr0 ^ (1 << 5);
    control_register_rw(0, cr0_2, 1);
    cr0_2 = control_register_rw(0, 0, 0);
    if(cr0_2 == cr0)
    {
      warning_printf("Error setting CR0.NE bit.\r\n");
    }
  }
  // Same with CR4.OSXMMEXCPT for SIMD errors
  uint64_t cr4 = control_register_rw(4, 0, 0);
  if(!(cr4 & (1 << 10)))
  {
    uint64_t cr4_2 = cr4 ^ (1 << 10);
    control_register_rw(4, cr4_2, 1);
    cr4_2 = control_register_rw(4, 0, 0);
    if(cr4_2 == cr4)
    {
      warning_printf("Error setting CR4.OSXMMEXCEPT bit.\r\n");
    }
  }

  // Make a replacement GDT since the UEFI one is in EFI Boot Services Memory.
  // Don't need anything fancy, just need one to exist somewhere (preferably in EfiLoaderData, which won't change with this software)
  Setup_MinimalGDT();
  printf("GDT set.\r\n");

  // Set up IDT for interrupts
  Setup_IDT();
  printf("IDT set.\r\n");

  Initialize_TSC_Freq();
  printf("TSC frequency set.\r\n");

  // Set up the memory map for use with mallocX (X = 16, 32, 64)
  Setup_MemMap();
  printf("MemMap set.\r\n");

  // Set up paging structures (requires memory map to be set up)
  Setup_Paging();
  printf("Paging set.\r\n");

  // Reclaim Efi Boot Services memory now that GDT, IDT, and Paging have been set up
  ReclaimEfiBootServicesMemory();
  printf("EfiBootServices Memory reclaimed.\r\n");

  // Ditto for EfiLoaderCode, which is just where the bootloader was
  ReclaimEfiLoaderCodeMemory();
  printf("EfiLoaderCode Memory reclaimed.\r\n");

  // HWP
  Enable_HWP();
  // It has a printf in it

  // ACPI
  Find_RSDP(LP);
  printf("Global RSDP found and set. Address: %#qx\r\n", Global_RSDP_Address);

 // This will make more sense after multicore is implemented.
  ACPI_STATUS ACPIInitStatus = InitializeFullAcpi();
//  ACPI_STATUS ACPIInitStatus = InitializeAcpiTablesOnly();
//  ACPIInitStatus = InitializeAcpiAfterTables();
  if(ACPIInitStatus)
  {
    error_printf("ACPI Init Error %#x\r\n", ACPIInitStatus);
    HaCF();
  }
  printf("ACPI Mode Enabled\r\n");

  Set_ACPI_APIC_Mode();
  // It has a printf in it

  Enable_Local_x2APIC(); // TODO This needs to be done per core
  // It has a printf in it

  // TODO enabling multicore stuff goes here, before interrupts

  // Enable Maskable Interrupts
  // Exceptions and Non-Maskable Interrupts are always enabled.
//  Enable_Maskable_Interrupts();
  // It has a printf in it
}

//----------------------------------------------------------------------------------------------------------------------------------
// get_tick: Read RDTSCP
//----------------------------------------------------------------------------------------------------------------------------------
//
// Finally, a way to tell time! Returns reference ticks since the last CPU reset.
// (Well, ok, technically UEFI has a runtime service to check the system time, this is mainly for CPU performance & cycle counting)
//

uint64_t get_tick(void)
{
    uint64_t high = 0, low = 0;
    asm volatile("rdtscp"
                  : "=a" (low), "=d" (high)
                  : // no inputs
                  : "%rcx"// clobbers
                  );

    return (high << 32 | low);
}

//----------------------------------------------------------------------------------------------------------------------------------
// Enable_AVX: Enable AVX/AVX2/AVX512
//----------------------------------------------------------------------------------------------------------------------------------
//
// Check for AVX/AVX512 support and enable it. Needed in order to use AVX functions like AVX_memmove, AVX_memcpy, AVX_memset, and
// AVX_memcmp
//

__attribute__((target("no-sse"))) void Enable_AVX(void)
{
  // Checking CPUID means determining if bit 21 of R/EFLAGS can be toggled
  uint64_t rflags = control_register_rw('f', 0, 0);
//  printf("RFLAGS: %#qx\r\n", rflags);
  uint64_t rflags2 = rflags ^ (1 << 21);
  control_register_rw('f', rflags2, 1);
  rflags2 = control_register_rw('f', 0, 0);
  if(rflags2 == rflags)
  {
    error_printf("CPUID is not supported.\r\n");
    HaCF();
  }
  else
  {
//    printf("CPUID is supported.\r\n");
// Check if OSXSAVE has already been set for some reason. Implies XSAVE support.
    uint64_t rbx = 0, rcx = 0, rdx = 0;
    asm volatile("cpuid"
                 : "=c" (rcx), "=d" (rdx) // Outputs
                 : "a" (0x01) // The value to put into %rax
                 : "%rbx" // CPUID would clobber any of the abcd registers not listed explicitly
               );

    if(rcx & (1 << 27))
    {
//      printf("AVX: OSXSAVE = 1\r\n");
// OSXSAVE has already been set, so it doesn't need to be re-set.
      if(rcx & (1 << 28))
      { // AVX is supported.
//        printf("AVX supported. Enabling AVX... ");
        uint64_t xcr0 = xcr_rw(0, 0, 0);
        xcr_rw(0, xcr0 | 0x7, 1);
        xcr0 = xcr_rw(0, 0, 0);

        if((xcr0 & 0x7) == 0x7)
        { // AVX successfully enabled, so we have that.
//          printf("AVX ON");
          // Now check AVX2 & AVX512
          asm volatile("cpuid"
                       : "=b" (rbx), "=c" (rcx), "=d" (rdx) // Outputs
                       : "a" (0x07), "c" (0x00) // The values to put into %rax and %rcx
                       : // CPUID would clobber any of the abcd registers not listed explicitly
                    );

          // AVX512 feature check if AVX512 supported
          if(rbx & (1 << 16))
          { // AVX512 is supported
//            printf("AVX512F supported. Enabling... ");
            uint64_t xcr0 = xcr_rw(0, 0, 0);
            xcr_rw(0, xcr0 | 0xE7, 1);
            xcr0 = xcr_rw(0, 0, 0);

            if((xcr0 & 0xE7) == 0xE7)
            { // AVX512 sucessfully enabled
              Colorscreen(Global_Print_Info.defaultGPU, Global_Print_Info.background_color); // We can use SSE/AVX/AVX512-optimized functions now!
              printf("AVX512 enabled.\r\n");
              // All done here.
            }
            else
            {
              error_printf("Unable to set AVX512.\r\n");
              HaCF();
            }
            printf("Checking other supported AVX512 features:\r\n");
            if(rbx & (1 << 17))
            {
              printf("AVX512DQ\r\n");
            }
            if(rbx & (1 << 21))
            {
              printf("AVX512_IFMA\r\n");
            }
            if(rbx & (1 << 26))
            {
              printf("AVX512PF\r\n");
            }
            if(rbx & (1 << 27))
            {
              printf("AVX512ER\r\n");
            }
            if(rbx & (1 << 28))
            {
              printf("AVX512CD\r\n");
            }
            if(rbx & (1 << 30))
            {
              printf("AVX512BW\r\n");
            }
            if(rbx & (1 << 31))
            {
              printf("AVX512VL\r\n");
            }
            if(rcx & (1 << 1))
            {
              printf("AVX512_VBMI\r\n");
            }
            if(rcx & (1 << 6))
            {
              printf("AVX512_VBMI2\r\n");
            }
            if(rcx & (1 << 11))
            {
              printf("AVX512VNNI\r\n");
            }
            if(rcx & (1 << 12))
            {
              printf("AVX512_BITALG\r\n");
            }
            if(rcx & (1 << 14))
            {
              printf("AVX512_VPOPCNTDQ\r\n");
            }
            if(rdx & (1 << 2))
            {
              printf("AVX512_4VNNIW\r\n");
            }
            if(rdx & (1 << 3))
            {
              printf("AVX512_4FMAPS\r\n");
            }
            printf("End of AVX512 feature check.\r\n");
          }
          else
          {
            Colorscreen(Global_Print_Info.defaultGPU, Global_Print_Info.background_color); // We can use SSE/AVX-optimized functions now! But not AVX512 ones.
            printf("AVX/AVX2 enabled.\r\n");
            info_printf("AVX512 not supported.\r\n");
          }
          // End AVX512 check

          if(rbx & (1 << 5))
          {
            printf("AVX2 supported.\r\n");
          }
          else
          {
            info_printf("AVX2 not supported.\r\n");
          }
          // Only have AVX to work with, then.
        }
        else
        {
          error_printf("Unable to set AVX.\r\n");
          HaCF();
        }
      }
      else
      {
        error_printf("AVX not supported. Checking for latest SSE features:\r\n");
        if(rcx & (1 << 20))
        {
          printf("Up to SSE4.2 supported.\r\n");
        }
        else
        {
          if(rcx & (1 << 19))
          {
            printf("Up to SSE4.1 supported.\r\n");
          }
          else
          {
            if(rcx & (1 << 9))
            {
              printf("Up to SSSE3 supported.\r\n");
            }
            else
            {
              if(rcx & 1)
              {
                printf("Up to SSE3 supported.\r\n");
              }
              else
              {
                if(rdx & (1 << 26))
                {
                  printf("Up to SSE2 supported.\r\n");
                }
                else
                {
                  printf("This is one weird CPU to get this far. x86_64 mandates SSE2.\r\n");
                }
              }
            }
          }
        }
      }
    }
    else
    {
//      printf("AVX: OSXSAVE = 0\r\n");
// OSXSAVE has not yet been set. Set it.
      if(rcx & (1 << 26)) // XSAVE supported.
      {
//        printf("AVX: XSAVE supported. Enabling OSXSAVE... ");
        uint64_t cr4 = control_register_rw(4, 0, 0);
        control_register_rw(4, cr4 ^ (1 << 18), 1);
        cr4 = control_register_rw(4, 0, 0);

        if(cr4 & (1 << 18))
        { // OSXSAVE successfully enabled.
//          printf("OSXSAVE enabled.\r\n");

          if(rcx & (1 << 28))
          { // AVX is supported
//            printf("AVX supported. Enabling AVX... ");
            uint64_t xcr0 = xcr_rw(0, 0, 0);
            xcr_rw(0, xcr0 | 0x7, 1);
            xcr0 = xcr_rw(0, 0, 0);

            if((xcr0 & 0x7) == 0x7)
            { // AVX successfully enabled, so we have that.
              // Now check AVX2 & AVX512
              asm volatile("cpuid"
                           : "=b" (rbx), "=c" (rcx), "=d" (rdx) // Outputs
                           : "a" (0x07), "c" (0x00) // The values to put into %rax and %rcx
                           : // CPUID would clobber any of the abcd registers not listed explicitly
                        );

              // AVX512 feature check if AVX512 supported
              if(rbx & (1 << 16))
              { // AVX512 is supported
//                printf("AVX512F supported. Enabling... ");
                uint64_t xcr0 = xcr_rw(0, 0, 0);
                xcr_rw(0, xcr0 | 0xE7, 1);
                xcr0 = xcr_rw(0, 0, 0);

                if((xcr0 & 0xE7) == 0xE7)
                { // AVX512 successfully enabled.
                  Colorscreen(Global_Print_Info.defaultGPU, Global_Print_Info.background_color); // We can use SSE/AVX/AVX512-optimized functions now!
                  printf("AVX512 enabled.\r\n");
                  // All done here.
                }
                else
                {
                  error_printf("Unable to set AVX512.\r\n");
                  HaCF();
                }
                printf("Checking other supported AVX512 features:\r\n");
                if(rbx & (1 << 17))
                {
                  printf("AVX512DQ\r\n");
                }
                if(rbx & (1 << 21))
                {
                  printf("AVX512_IFMA\r\n");
                }
                if(rbx & (1 << 26))
                {
                  printf("AVX512PF\r\n");
                }
                if(rbx & (1 << 27))
                {
                  printf("AVX512ER\r\n");
                }
                if(rbx & (1 << 28))
                {
                  printf("AVX512CD\r\n");
                }
                if(rbx & (1 << 30))
                {
                  printf("AVX512BW\r\n");
                }
                if(rbx & (1 << 31))
                {
                  printf("AVX512VL\r\n");
                }
                if(rcx & (1 << 1))
                {
                  printf("AVX512_VBMI\r\n");
                }
                if(rcx & (1 << 6))
                {
                  printf("AVX512_VBMI2\r\n");
                }
                if(rcx & (1 << 11))
                {
                  printf("AVX512VNNI\r\n");
                }
                if(rcx & (1 << 12))
                {
                  printf("AVX512_BITALG\r\n");
                }
                if(rcx & (1 << 14))
                {
                  printf("AVX512_VPOPCNTDQ\r\n");
                }
                if(rdx & (1 << 2))
                {
                  printf("AVX512_4VNNIW\r\n");
                }
                if(rdx & (1 << 3))
                {
                  printf("AVX512_4FMAPS\r\n");
                }
                printf("End of AVX512 feature check.\r\n");
              }
              else
              {
                Colorscreen(Global_Print_Info.defaultGPU, Global_Print_Info.background_color); // We can use SSE/AVX-optimized functions now! Just not AVX512 ones.
                printf("AVX/AVX2 enabled.\r\n");
                info_printf("AVX512 not supported.\r\n");
              }
              // End AVX512 check

              if(rbx & (1 << 5))
              {
                printf("AVX2 supported.\r\n");
              }
              else
              {
                info_printf("AVX2 not supported.\r\n");
              }
              // Only have AVX to work with, then.
            }
            else
            {
              error_printf("Unable to set AVX.\r\n");
              HaCF();
            }
          }
          else
          {
            error_printf("AVX not supported. Checking for latest SSE features:\r\n");
            if(rcx & (1 << 20))
            {
              printf("Up to SSE4.2 supported.\r\n");
            }
            else
            {
              if(rcx & (1 << 19))
              {
                printf("Up to SSE4.1 supported.\r\n");
              }
              else
              {
                if(rcx & (1 << 9))
                {
                  printf("Up to SSSE3 supported.\r\n");
                }
                else
                {
                  if(rcx & 1)
                  {
                    printf("Up to SSE3 supported.\r\n");
                  }
                  else
                  {
                    if(rdx & (1 << 26))
                    {
                      printf("Up to SSE2 supported.\r\n");
                    }
                    else
                    {
                      printf("This is one weird CPU to get this far. x86_64 mandates SSE2.\r\n");
                    }
                  }
                }
              }
            }
          }
        }
        else
        {
          error_printf("Unable to set OSXSAVE in CR4.\r\n");
          HaCF();
        }
      }
      else
      {
        error_printf("AVX: XSAVE not supported.\r\n");
        HaCF();
      }
    }
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// Enable_Local_x2APIC: Enable Core's Local x2APIC
//----------------------------------------------------------------------------------------------------------------------------------
//
// The APIC is already enabled by UEFI--it has to be, since BSP and AP init is handled by firmware. The x2APIC, however, may not be,
// and that's the one we want on supported CPUs to handle interrupts. Unlike prior APICs, the x2APIC uses MSRs instead of MMIO to
// access interrupt-related registers, which is faster (no memory accesses). It is also easier to use, as it removes the Destination
// Format Register and the 128-bit alignment mapping for 32-bit registers, among other changes. See Chapter 10.12.1.2 "x2APIC
// Register Address Space" in the Intel Architecture Manual, Volume 3A for more details and for descriptions of each x2APIC MSR.
//
// A later section of the manual (Ch. 10.12.5.1 x2APIC States) also has this: "On coming out of reset, the local APIC unit is enabled
// and is in the xAPIC mode: IA32_APIC_BASE[EN]=1 and IA32_APIC_BASE[EXTD]=0. The APIC registers are initialized as follows..."
//
// It also appears that all CPUs with AVX also have x2APICs, which is nice.
//

void Enable_Local_x2APIC(void)
{
  // IA32_APIC_BASE_MSR is 0x1B
  // The MMIO base address of the local xAPIC can be remapped by writing a new base address to this MSR, if in APIC mode.
  // NOTE: To disable x2APIC mode, set both bit 11 and bit 10 in IA32_APIC_BASE_MSR to 0--the whole APIC needs to be software
  // disabled in order to transition to the older xAPIC mode. This shouldn't be necessary on supported CPUs, however.

  uint64_t rcx = 0;
  asm volatile("cpuid"
               : "=c" (rcx) // Outputs
               : "a" (0x01) // The value to put into %rax
               : "%rbx", "%rdx" // CPUID clobbers all not-explicitly-used abcd registers
             );

  if(rcx & (1ULL << 21))
  {
    uint64_t ApicBaseMsr = msr_rw(0x1B, 0, 0);
    printf("Apic Base Register: %llx\r\n", ApicBaseMsr);
    if(ApicBaseMsr & (1ULL << 10))
    {
      info_printf("Local x2APIC already enabled on core.\r\n"); // TODO: add a way to get core number/ID for these printfs
    }
    else
    {
      msr_rw(0x1B, ApicBaseMsr | (1ULL << 10), 1);
      if(msr_rw(0x1B, 0, 0) & (1ULL << 10))
      {
        printf("Local x2APIC enabled on core.\r\n");
      }
      else
      {
        warning_printf("Could not enable local x2APIC on core. Interrupts will not be available.\r\n");
      }
    }
  }
  else
  {
    warning_printf("Local x2APIC not supported on core.\r\n");
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// Enable_Maskable_Interrupts: Load Interrupt Descriptor Table and Enable Interrupts
//----------------------------------------------------------------------------------------------------------------------------------
//
// Exceptions and Non-Maskable Interrupts are always enabled.
// This is needed for things like keyboard input.
//

void Enable_Maskable_Interrupts(void)
{
  uint64_t rflags = control_register_rw('f', 0, 0);
  if(rflags & (1ULL << 9))
  {
    // Interrupts are already enabled, do nothing.
    info_printf("Interrupts are already enabled.\r\n");
  }
  else
  {
    uint64_t rflags2 = rflags | (1ULL << 9); // Set bit 9

    control_register_rw('f', rflags2, 1);
    rflags2 = control_register_rw('f', 0, 0);
    if(rflags2 == rflags)
    {
      warning_printf("Unable to enable maskable interrupts.\r\n");
    }
    else
    {
      printf("Maskable Interrupts enabled.\r\n");
    }
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// Enable_HWP: Enable Hardware P-States
//----------------------------------------------------------------------------------------------------------------------------------
//
// Enable hardware power management (HWP) if available.
// Otherwise this will not do anything.
//
// Hardware P-States means you don't have to worry about implmenting CPU P-states transitions in software, as the CPU handles them
// autonomously. Intel introduced this feature on Skylake chips.
//

void Enable_HWP(void)
{
  uint64_t rax = 0;
  asm volatile("cpuid"
               : "=a" (rax) // Outputs
               : "a" (0x06) // The value to put into %rax
               : "%rbx", "%rcx", "%rdx" // CPUID clobbers all not-explicitly-used abcd registers
             );

  if(rax & (1ULL << 7)) // HWP is available
  {
    uint64_t HWP_State = msr_rw(0x770, 0, 0);
    if(HWP_State & 0x1)
    {
      info_printf("HWP is already enabled.\r\n");
    }
    else
    {
      msr_rw(0x770, HWP_State | 0x1, 1);
      if(msr_rw(0x770, 0, 0) & 0x1)
      {
        printf("HWP enabled.\r\n");
      }
      else
      {
        warning_printf("Unable to set HWP.\r\n");
      }
    }
  }
  else
  {
    info_printf("HWP not supported.\r\n");
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// Find_RSDP: Locate The RSDP Address for ACPI
//----------------------------------------------------------------------------------------------------------------------------------
//
// Given the loader parameters, find the Root System Descriptor Ponter (RSDP) so that ACPI can be used.
//

void Find_RSDP(LOADER_PARAMS * LP)
{
  // Search for ACPI tables
  uint8_t RSDPfound = 0;
  uint64_t RSDP_index = 0;
  printf("\r\nAcpi20GUID: %08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x\r\n",
          Acpi20TableGuid.Data1,
          Acpi20TableGuid.Data2,
          Acpi20TableGuid.Data3,
          Acpi20TableGuid.Data4[0],
          Acpi20TableGuid.Data4[1],
          Acpi20TableGuid.Data4[2],
          Acpi20TableGuid.Data4[3],
          Acpi20TableGuid.Data4[4],
          Acpi20TableGuid.Data4[5],
          Acpi20TableGuid.Data4[6],
          Acpi20TableGuid.Data4[7]
        );

//  printf("%#qx\r\n", Acpi20TableGuid);

  for(uint64_t i1 = 0; i1 < LP->Number_of_ConfigTables; i1++)
  {
    printf("Table %llu GUID: %08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x\r\n", i1,
            LP->ConfigTables[i1].VendorGuid.Data1,
            LP->ConfigTables[i1].VendorGuid.Data2,
            LP->ConfigTables[i1].VendorGuid.Data3,
            LP->ConfigTables[i1].VendorGuid.Data4[0],
            LP->ConfigTables[i1].VendorGuid.Data4[1],
            LP->ConfigTables[i1].VendorGuid.Data4[2],
            LP->ConfigTables[i1].VendorGuid.Data4[3],
            LP->ConfigTables[i1].VendorGuid.Data4[4],
            LP->ConfigTables[i1].VendorGuid.Data4[5],
            LP->ConfigTables[i1].VendorGuid.Data4[6],
            LP->ConfigTables[i1].VendorGuid.Data4[7]
          );

    if(!(AVX_memcmp(&LP->ConfigTables[i1].VendorGuid, &Acpi20TableGuid, 16, 0)))
    {
      printf("RSDP 2.0 found!\r\n");
      RSDP_index = i1;
      RSDPfound = 2;
      break;
    }
  }
  // If no RSDP 2.0, check for 1.0
  if(!RSDPfound)
  {
    for(uint64_t i2 = 0; i2 < LP->Number_of_ConfigTables; i2++)
    {
      if(!(AVX_memcmp(&LP->ConfigTables[i2].VendorGuid, &Acpi10TableGuid, 16, 0)))
      {
        printf("RSDP 1.0 found!\r\n");
        RSDP_index = i2;
        RSDPfound = 1;
        break;
      }
    }
  }

  if(!RSDPfound)
  {
    printf("Invalid system: no RSDP.\r\n");
    HaCF();
  }

  Global_RSDP_Address = (EFI_PHYSICAL_ADDRESS)(LP->ConfigTables[RSDP_index].VendorTable);

  // Done!
}

//----------------------------------------------------------------------------------------------------------------------------------
// Hypervisor_check: Are We Virtualized?
//----------------------------------------------------------------------------------------------------------------------------------
//
// Check a bit that Intel and AMD always set to 0. Some hypervisors like Windows 10 Hyper-V set it to 1 (don't know if all VMs do) to
// allow for this kind of check
//

uint8_t Hypervisor_check(void)
{
  // Hypervisor check
  uint64_t rcx;
  asm volatile("cpuid"
               : "=c" (rcx) // Outputs
               : "a" (0x01) // The value to put into %rax
               : "%rbx", "%rdx" // CPUID would clobber any of the abcd registers not listed explicitly (all are here, though)
             );

  if(rcx & (1 << 31)) // 1 means hypervisor (i.e. in a VM), 0 means no hypervisor
  {
    return 1;
  }
  else
  {
    return 0;
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// Initialize_TSC_Freq: Load Global Invariant TSC Frequency
//----------------------------------------------------------------------------------------------------------------------------------
//
// This reads MSR_PLATFORM_INFO and sets the invariant TSC frequency needed for timing functions.
// If the MSR is empty becuase of, e.g., a VM won't report it, fall back to 3GHz. Things won't be too horribly off since AVX+ CPUs
// all operate in the 2-5GHz range.
//

void Initialize_TSC_Freq(void)
{
  // 0xCE is MSR_PLATFORM_INFO
  uint64_t max_non_turbo_ratio = (msr_rw(0xCE, 0, 0) & 0x000000000000FF00) >> 8; // Max non-turbo bus multiplier is in this byte

  if(max_non_turbo_ratio)
  {
    // 100 MHz bus for these CPUs, 133 MHz for Nehalem (but Nehalem doesn't have AVX)
    // That 100MHz includes both AMD and Intel.
    Global_TSC_frequency.CyclesPerSecond = max_non_turbo_ratio * 100ULL * 1000000ULL;
    Global_TSC_frequency.CyclesPerMillisecond = max_non_turbo_ratio * 100ULL * 1000ULL;
    Global_TSC_frequency.CyclesPerMicrosecond = max_non_turbo_ratio * 100ULL; // Isn't it nice that 1 MHz is the exact inverse of 1 usec?
    Global_TSC_frequency.CyclesPer100ns = max_non_turbo_ratio * 10ULL;
    Global_TSC_frequency.CyclesPer10ns = max_non_turbo_ratio;
    printf("Nominal TSC frequency is %llu MHz.\r\n", Global_TSC_frequency.CyclesPerMicrosecond);
  }
  else
  {
    // Probably in a vm... So...
    info_printf("Read 0 from MSR_PLATFORM_INFO, falling back to 3GHz for invariant TSC.\r\n");
    // Initialized to 3GHz in Global_Vars.c
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// ssleep: Sleep for Seconds
//----------------------------------------------------------------------------------------------------------------------------------
//
// Wait for the specified time in Seconds
//

void ssleep(uint64_t Seconds)
{
  if(Seconds)
  {
    uint64_t cycle_count = 0;
    uint64_t cycle_count_start = get_tick();
    while((cycle_count / Global_TSC_frequency.CyclesPerSecond) < Seconds)
    {
      cycle_count = get_tick() - cycle_count_start;
    }
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// msleep: Sleep for Milliseconds
//----------------------------------------------------------------------------------------------------------------------------------
//
// Wait for the specified time in milliseconds
//

void msleep(uint64_t Milliseconds)
{
  if(Milliseconds)
  {
    uint64_t cycle_count = 0;
    uint64_t cycle_count_start = get_tick();
    while((cycle_count / Global_TSC_frequency.CyclesPerMillisecond) < Milliseconds)
    {
      cycle_count = get_tick() - cycle_count_start;
    }
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// usleep: Sleep for Microseconds
//----------------------------------------------------------------------------------------------------------------------------------
//
// Wait for the specified time in microseconds
//

// Accuracy Notes:
// At around 200-300 cycles per 100ns (2-3GHz), a divide is still plenty fast enough for this to be A-OK.
// A worst-case 64-bit integer divide can be up to ~100 cycles, so this can issue at least 20 divides in one microsecond on
// supported CPUs. That gives a worst-case error bound of roughly 1/20 microseconds. In most cases it's probably better (2-3x), but
// it depends on the cycle latency & reciprocal throughput of the CPU architecture. Some might only have an error of 1/60 microseconds,
// or even 1/100. See Document 4 "Instruction Tables" at https://www.agner.org/optimize/ for divide latencies for various architectures.
// If a more exact error bound is needed, profile it/measure it on the system in question. Can't beat that kind of data!

void usleep(uint64_t Microseconds)
{
  if(Microseconds)
  {
    uint64_t cycle_count = 0;
    uint64_t cycle_count_start = get_tick();
    while((cycle_count / Global_TSC_frequency.CyclesPerMicrosecond) < Microseconds)
    {
      cycle_count = get_tick() - cycle_count_start;
    }
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// read_perfs_initial: Measure CPU Performance (Part 1 of 2)
//----------------------------------------------------------------------------------------------------------------------------------
//
// Takes an array of 2x uint64_t (e.g. uint64_t perfcounters[2] = {1, 1}; ), fills first uint64 with aperf and 2nd with mperf.
// This will disable maskable interrupts, as it is expected that get_CPU_freq(perfs, 1) is called to process the result and re-enable
// the interrupts. The functions read_perfs_initial(perfs) and get_CPU_freq(perfs, 1) should sandwich the desired code to measure.
//
// May not work in hypervisors (definitely does not work in Windows 10 Hyper-V), but it's fine on real hardware.
//

uint8_t read_perfs_initial(uint64_t * perfs)
{
  // Check for hypervisor
  if(Hypervisor_check())
  {
    warning_printf("Hypervisor detected. It's not safe to read CPU frequency MSRs. Returning 0...\r\n");
    return 0;
  }
  // OK, not in a hypervisor; continuing...

  // Disable maskable interrupts
  uint64_t rflags = control_register_rw('f', 0, 0);
  uint64_t rflags2 = rflags & ~(1 << 9); // Clear bit 9

  control_register_rw('f', rflags2, 1);
  rflags2 = control_register_rw('f', 0, 0);
  if(rflags2 == rflags)
  {
    warning_printf("read_perfs_initial: Unable to disable maskable interrupts (maybe they are already disabled?). Results may be skewed.\r\n");
  }
  // Now we can safely continue without something like keyboard input messing up tests

  uint64_t turbocheck = msr_rw(0x1A0, 0, 0);
  if(turbocheck & (1 << 16))
  {
    info_printf("NOTE: Enhanced SpeedStep is enabled.\r\n");
  }
  if((turbocheck & (1ULL << 38)) == 0)
  {
    info_printf("NOTE: Turbo Boost is enabled.\r\n");
  }

  asm volatile("cpuid"
               : "=a" (turbocheck) // Outputs
               : "a" (0x06) // The value to put into %rax
               : "%rbx", "%rcx", "%rdx" // CPUID clobbers all not-explicitly-used abcd registers
             );

  if(turbocheck & (1 << 7)) // HWP is available
  {
    if(msr_rw(0x770, 0, 0) & 1)
    {
      info_printf("NOTE: HWP is enabled.\r\n");
    }
  }

  // Force serializing
  asm volatile("cpuid"
               : // No outputs
               : // No inputs
               : "%rax", "%rbx", "%rcx", "%rdx" // CPUID clobbers all not-explicitly-used abcd registers
             );

  perfs[0] = msr_rw(0xe8, 0, 0); // APERF
  perfs[1] = msr_rw(0xe7, 0, 0); // MPERF

  return 1; // Success
}

//----------------------------------------------------------------------------------------------------------------------------------
// get_CPU_freq: Measure CPU Performance (Part 2 of 2)
//----------------------------------------------------------------------------------------------------------------------------------
//
// Get CPU frequency in Hz
// May not work in hypervisors (definitely does not work in Windows 10 Hyper-V), but it's fine on real hardware
//
// avg_or_measure: avg freq = 0 (ignores perfs argument), measuring freq during piece of code = 1
//
// In order to use avg_or_measure = 1, perfs array *MUST* have been filled by read_perfs_initial().
// avg_or_measure = 0 has no such restriction, as it just measures from the last time aperf and mperf were 0, which is either the
// last CPU reset state or last manual reset/overflow of aperf and mperf.
//

uint64_t get_CPU_freq(uint64_t * perfs, uint8_t avg_or_measure)
{
  // Check for hypervisor
  if(__builtin_expect(Hypervisor_check(), 0)) // Using the __builtin_expect() macro described in Draw_arc() in Display.c since this is performance-critical enough to demand it
  {
    warning_printf("Hypervisor detected. It's not safe to read CPU frequency MSRs. Returning 0...\r\n");
    return 0;
  }
  // OK, not in a hypervisor; continuing...

  uint64_t rax = 0, rbx = 0, rcx = 0, maxleaf = 0;
  uint64_t aperf = 1, mperf = 1; // Don't feel like dealing with division by zero.
  uint64_t rflags = 0, rflags2 = 0;

  // Check function argument
  if(avg_or_measure == 1) // Measurement for some piece of code is being done.
  {
    // Force serializing
    asm volatile("cpuid"
                 : // No outputs
                 : // No inputs
                 : "%rax", "%rbx", "%rcx", "%rdx" // CPUID clobbers all not-explicitly-used abcd registers
               );

    uint64_t aperf2 = msr_rw(0xe8, 0, 0); // APERF
    uint64_t mperf2 = msr_rw(0xe7, 0, 0); // MPERF

    aperf = aperf2 - perfs[0]; // Adjusted APERF
    mperf = mperf2 - perfs[1]; // Adjusted MPERF
  }
  else // Avg since reset, or since aperf/mperf overflowed (once every 150-300 years at 2-4 GHz) or were manually zeroed
  {
    // Disable maskable interrupts
    rflags = control_register_rw('f', 0, 0);
    rflags2 = rflags & ~(1 << 9); // Clear bit 9

    control_register_rw('f', rflags2, 1);
    rflags2 = control_register_rw('f', 0, 0);
    if(rflags2 == rflags)
    {
      warning_printf("get_CPU_freq: Unable to disable interrupts (maybe they are already disabled?). Results may be skewed.\r\n");
    }
    // Now we can safely continue without something like keyboard input messing up tests

    uint64_t turbocheck = msr_rw(0x1A0, 0, 0);
    if(turbocheck & (1 << 16))
    {
      info_printf("NOTE: Enhanced SpeedStep is enabled.\r\n");
    }
    if((turbocheck & (1ULL << 38)) == 0)
    {
      info_printf("NOTE: Turbo Boost is enabled.\r\n");
    }

    asm volatile("cpuid"
                 : "=a" (turbocheck) // Outputs
                 : "a" (0x06) // The value to put into %rax
                 : "%rbx", "%rcx", "%rdx" // CPUID clobbers all not-explicitly-used abcd registers
               );

    if(turbocheck & (1 << 7)) // HWP is available
    {
      if(msr_rw(0x770, 0, 0) & 1)
      {
        info_printf("NOTE: HWP is enabled.\r\n");
      }
    }

    // Force serializing
    asm volatile("cpuid"
                 : // No outputs
                 : // No inputs
                 : "%rax", "%rbx", "%rcx", "%rdx" // CPUID clobbers all not-explicitly-used abcd registers
               );

    aperf = msr_rw(0xe8, 0, 0); // APERF
    mperf = msr_rw(0xe7, 0, 0); // MPERF
  }

  // This will force serializing, though we need the output from CPUID anyways.
  // 2 birds with 1 stone.
  asm volatile("cpuid"
               : "=a" (maxleaf) // Outputs
               : "a" (0x00) // The value to put into %rax
               : "%rbx", "%rcx", "%rdx" // CPUID clobbers all not-explicitly-used abcd registers
             );

// DEBUG:
//  printf("aperf: %qu, mperf: %qu\r\n", aperf, mperf);

  if(maxleaf >= 0x15)
  {
    rax = 0x15;
    asm volatile("cpuid"
                 : "=a" (rax), "=b" (rbx), "=c" (rcx) // Outputs
                 : "a" (rax) // The value to put into %rax
                 : "%rdx" // CPUID clobbers all not-explicitly-used abcd registers
               );
//DEBUG: // printf("rax: %qu, rbx: %qu, rcx: %qu\r\n b/a *c: %qu\r\n", rax, rbx, rcx, (rcx * rbx)/rax);

    if((rcx != 0) && (rbx != 0))
    {
      // rcx is nominal frequency of clock in Hz
      // TSC freq (in Hz) from Crystal OSC = (rcx * rbx)/rax
      // Intel's CPUID reference does not make it clear that ebx/eax is TSC's *frequency* / Core crystal clock
      return ((rcx * rbx * aperf)/(rax * mperf)); // Hz
    }
    // (rbx/rax) is (TSC/core crystal clock), so rbx/rax is to nominal crystal
    // clock (in rcx in rcx != 0) as aperf/mperf is to max frequency (that is to
    // say, they're scaling factors)
    if(rcx == 0)
    {
      maxleaf = 0x01; // Don't need maxleaf any more, so reuse it for this
      asm volatile("cpuid"
                   : "=a" (maxleaf) // Outputs
                   : "a" (maxleaf) // The value to put into %rax
                   : "%rbx", "%rcx", "%rdx" // CPUID clobbers any of the abcd registers not listed explicitly
                 );

      uint64_t maxleafmask = maxleaf & 0xF0FF0;


//DEBUG: // printf("maxleafmask: %qu\r\n", maxleafmask);
      // Intel family 0x06, models 4E, 5E, 8E, and 9E have a known clock of 24 MHz
      // See tsc.c in the Linux kernel: https://github.com/torvalds/linux/blob/master/arch/x86/kernel/tsc.c
      // It's also in Intel SDM, Vol. 3, Ch. 18.7.3
      if( (maxleafmask == 0x906E0) || (maxleafmask == 0x806E0) || (maxleafmask == 0x506E0) || (maxleafmask == 0x406E0) )
      {
        return (24000000ULL * rbx * aperf)/(rax * mperf); // Hz
      }
      // Else: Argh...
    }
  }
  // At this point CPUID won't help.

  // Needed for Sandy Bridge, Ivy Bridge, and possibly Haswell, Broadwell -- They all have constant TSC found this way.
  // NOTE: Multiplying tsc_frequency by 100MHz means the number will be something like 2600 MHz, e.g. this multiplication factor determines the unit.

  // Freq = TSCFreq * delta(APERF)/delta(MPERF)
  uint64_t frequency = (Global_TSC_frequency.CyclesPerSecond * aperf) / mperf; // CPUID didn't help, so fall back to Sandy Bridge method.

  // All done, so re-enable maskable interrupts
  // It is possible that RFLAGS changed since it was last read...
  rflags = control_register_rw('f', 0, 0);
  rflags2 = rflags | (1 << 9); // Set bit 9

  control_register_rw('f', rflags2, 1);
  rflags2 = control_register_rw('f', 0, 0);
  if(rflags2 == rflags)
  {
    warning_printf("get_CPU_freq: Unable to re-enable maskable interrupts.\r\n");
  }

  return frequency;
}

//----------------------------------------------------------------------------------------------------------------------------------
// portio_rw: Read/Write I/O Ports
//----------------------------------------------------------------------------------------------------------------------------------
//
// Read from or write to x86 port addresses
//
// port_address: address of the port
// size: 1, 2, or 4 bytes
// rw: 0 = read, 1 = write
// input data is ignored on reads
//

uint32_t portio_rw(uint16_t port_address, uint32_t data, uint8_t size, uint8_t rw)
{
  if(size == 1)
  {
    // data is 8 bits
    uint8_t data8 = (uint8_t)data;

    if(rw == 1) // Write
    {
      asm volatile("outb %[value], %[address]" // GAS syntax (src, dest) is opposite Intel syntax (dest, src)
                    : // No outputs
                    : [value] "a" (data8), [address] "d" (port_address)
                    : // No clobbers
                  );
    }
    else // Read
    {
      asm volatile("inb %[address], %[value]"
                    : [value] "=a" (data8)
                    : [address] "d" (port_address)
                    : // No clobbers
                  );
      data = (uint32_t)data8;
    }
  }
  else if(size == 2)
  {
    // data is 16 bits
    uint16_t data16 = (uint16_t)data;

    if(rw == 1) // Write
    {
      asm volatile("outw %[value], %[address]"
                    : // No outputs
                    : [value] "a" (data16), [address] "d" (port_address)
                    : // No clobbers
                  );
    }
    else // Read
    {
      asm volatile("inw %[address], %[value]"
                    : [value] "=a" (data16)
                    : [address] "d" (port_address)
                    : // No clobbers
                  );
      data = (uint32_t)data16;
    }
  }
  else if(size == 4)
  {
    // data is 32 bits

    if(rw == 1) // Write
    {
      asm volatile("outl %[value], %[address]"
                    : // No outputs
                    : [value] "a" (data), [address] "d" (port_address)
                    : // No clobbers
                  );
    }
    else // Read
    {
      asm volatile("inl %[address], %[value]"
                    : [value] "=a" (data)
                    : [address] "d" (port_address)
                    : // No clobbers
                  );
    }
  }
  else
  {
    error_printf("Invalid port i/o size.\r\n");
  }

  return data;
}

//----------------------------------------------------------------------------------------------------------------------------------
// msr_rw: Read/Write Model-Specific Registers
//----------------------------------------------------------------------------------------------------------------------------------
//
// Read from or write to Model Specific Registers
//
// msr: msr address
// rw: 0 = read, 1 = write
// input data is ignored for reads
//

uint64_t msr_rw(uint64_t msr, uint64_t data, uint8_t rw)
{
  uint64_t high = 0, low = 0;

  if(rw == 1) // Write
  {
    low = ((uint32_t *)&data)[0];
    high = ((uint32_t *)&data)[1];
    asm volatile("wrmsr"
             : // No outputs
             : "a" (low), "c" (msr), "d" (high) // Input MSR into %rcx, and high (%rdx) & low (%rax) to write
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
  return (high << 32 | low); // For write, this will be data. Reads will be the msr's value.
}

//----------------------------------------------------------------------------------------------------------------------------------
// vmxcsr_rw: Read/Write MXCSR (Vex-Encoded)
//----------------------------------------------------------------------------------------------------------------------------------
//
// Read from or write to the MXCSR register (VEX-encoded version)
// Use this function instead of mxcsr_rw() if using AVX instructions
//
// rw: 0 = read, 1 = write
// input data is ignored for reads
//

uint32_t vmxcsr_rw(uint32_t data, uint8_t rw)
{
  if(rw == 1) // Write
  {
    asm volatile("vldmxcsr %[src]"
             : // No outputs
             : [src] "m" (data) // Input 32-bit value into MXCSR
             : // No clobbers
           );
  }
  else // Read
  {
    asm volatile("vstmxcsr %[dest]"
             : [dest] "=m" (data) // Outputs 32-bit value from MXCSR
             :  // No inputs
             : // No clobbers
           );
  }
  return data;
}

//----------------------------------------------------------------------------------------------------------------------------------
// mxcsr_rw: Read/Write MXCSR (Legacy/SSE)
//----------------------------------------------------------------------------------------------------------------------------------
//
// Read from or write to the MXCSR register (Legacy/SSE)
//
// rw: 0 = read, 1 = write
// input data is ignored for reads
//

uint32_t mxcsr_rw(uint32_t data, uint8_t rw)
{
  if(rw == 1) // Write
  {
    asm volatile("ldmxcsr %[src]"
             : // No outputs
             : [src] "m" (data) // Input 32-bit value into MXCSR
             : // No clobbers
           );
  }
  else // Read
  {
    asm volatile("stmxcsr %[dest]"
             : [dest] "=m" (data) // Outputs 32-bit value from MXCSR
             :  // No inputs
             : // No clobbers
           );
  }
  return data;
}

//----------------------------------------------------------------------------------------------------------------------------------
// control_register_rw: Read/Write Control Registers and RFLAGS
//----------------------------------------------------------------------------------------------------------------------------------
//
// Read from or write to the standard system control registers (CR0-CR4 and CR8) and the RFLAGS register
//
// crX: an integer specifying which CR (e.g. 0 for CR0, etc.), use 'f' (with single quotes) for RFLAGS
// in_out: writes this value if rw = 1, input value ignored on reads
// rw: 0 = read, 1 = write
//

uint64_t control_register_rw(int crX, uint64_t in_out, uint8_t rw) // Read from or write to a control register
{
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
                     "popq %[dest]"
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

//----------------------------------------------------------------------------------------------------------------------------------
// xcr_rw: Read/Write Extended Control Registers
//----------------------------------------------------------------------------------------------------------------------------------
//
// Read from or write to the eXtended Control Registers
// XCR0 is used to enable AVX/AVX512/SSE extended registers
//
// xcrX: an integer for specifying which XCR (0 for XCR0, etc.)
// rw: 0 = read, 1 = write
// data is ignored for reads
//

uint64_t xcr_rw(uint64_t xcrX, uint64_t data, uint8_t rw)
{
  uint64_t high = 0, low = 0;

  if(rw == 1) // Write
  {
    low = ((uint32_t *)&data)[0];
    high = ((uint32_t *)&data)[1];
    asm volatile("xsetbv"
             : // No outputs
             : "a" (low), "c" (xcrX), "d" (high) // Input XCR# into %rcx, and high (%rdx) & low (%rax) to write
             : // No clobbers
           );
  }
  else // Read
  {
    asm volatile("xgetbv"
             : "=a" (low), "=d" (high) // Outputs
             : "c" (xcrX) // Input XCR# into %rcx
             : // No clobbers
           );
  }

  return (high << 32 | low); // For write, this will be data. Reads will be the msr's value.
}

//----------------------------------------------------------------------------------------------------------------------------------
// read_cs: Read %CS Register
//----------------------------------------------------------------------------------------------------------------------------------
//
// Read the %CS (code segement) register
// When used in conjunction with get_gdtr(), this is useful for checking if in 64-bit mode
//

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

//----------------------------------------------------------------------------------------------------------------------------------
// get_gdtr: Read Global Descriptor Table Register
//----------------------------------------------------------------------------------------------------------------------------------
//
// Read the Global Descriptor Table Register
//
// The %CS register contains an index for use with the address retrieved from this, in order to point to which GDT entry is relevant
// to the current code segment.
//

DT_STRUCT get_gdtr(void)
{
  DT_STRUCT gdtr_data = {0};
  asm volatile("sgdt %[dest]"
           : [dest] "=m" (gdtr_data) // Outputs
           : // No inputs
           : // No clobbers
         );

  return gdtr_data;
}

//----------------------------------------------------------------------------------------------------------------------------------
// set_gdtr: Set Global Descriptor Table Register
//----------------------------------------------------------------------------------------------------------------------------------
//
// Set the Global Descriptor Table Register
//

void set_gdtr(DT_STRUCT gdtr_data)
{
  asm volatile("lgdt %[src]"
           : // No outputs
           : [src] "m" (gdtr_data) // Inputs
           : // No clobbers
         );
}

//----------------------------------------------------------------------------------------------------------------------------------
// get_idtr: Read Interrupt Descriptor Table Register
//----------------------------------------------------------------------------------------------------------------------------------
//
// Read the Interrupt Descriptor Table Register
//

DT_STRUCT get_idtr(void)
{
  DT_STRUCT idtr_data = {0};
  asm volatile("sidt %[dest]"
           : [dest] "=m" (idtr_data) // Outputs
           : // No inputs
           : // No clobbers
         );

  return idtr_data;
}

//----------------------------------------------------------------------------------------------------------------------------------
// set_idtr: Set Interrupt Descriptor Table Register
//----------------------------------------------------------------------------------------------------------------------------------
//
// Set the Interrupt Descriptor Table Register
//

void set_idtr(DT_STRUCT idtr_data)
{
  asm volatile("lidt %[src]"
           : // No outputs
           : [src] "m" (idtr_data) // Inputs
           : // No clobbers
         );
}

//----------------------------------------------------------------------------------------------------------------------------------
// get_ldtr: Read Local Descriptor Table Register (Segment Selector)
//----------------------------------------------------------------------------------------------------------------------------------
//
// Read the Local Descriptor Table Register (reads segment selector only; the selector points to the LDT entry in the GDT, as this
// entry contains all the info pertaining to the rest of the LDTR)
//

uint16_t get_ldtr(void)
{
  uint16_t ldtr_data = 0;
  asm volatile("sldt %[dest]"
           : [dest] "=m" (ldtr_data) // Outputs
           : // No inputs
           : // No clobbers
         );

  return ldtr_data;
}

//----------------------------------------------------------------------------------------------------------------------------------
// set_ldtr: Set Local Descriptor Table Register (Segment Selector)
//----------------------------------------------------------------------------------------------------------------------------------
//
// Set the Local Descriptor Table Register (reads segment selector only; the selector points to the LDT entry in the GDT, as this
// entry contains all the info pertaining to the rest of the LDTR). Use this in conjunction with set_gdtr() to describe LDT(s).
//

void set_ldtr(uint16_t ldtr_data)
{
  asm volatile("lldt %[src]"
           : // No outputs
           : [src] "m" (ldtr_data) // Inputs
           : // No clobbers
         );
}

//----------------------------------------------------------------------------------------------------------------------------------
// get_tsr: Read Task State Register (Segment Selector)
//----------------------------------------------------------------------------------------------------------------------------------
//
// Read the Task Register (reads segment selector only; the selector points to the Task State Segment (TSS) entry in the GDT, as this
// entry contains all the info pertaining to the rest of the TSR)
//

uint16_t get_tsr(void)
{
  uint16_t tsr_data = 0;
  asm volatile("str %[dest]"
           : [dest] "=m" (tsr_data) // Outputs
           : // No inputs
           : // No clobbers
         );

  return tsr_data;
}

//----------------------------------------------------------------------------------------------------------------------------------
// set_tsr: Set Task State Register (Segment Selector)
//----------------------------------------------------------------------------------------------------------------------------------
//
// Set the Task Register (reads segment selector only; the selector points to the Task State Segment (TSS) entry in the GDT, as this
// entry contains all the info pertaining to the rest of the TSR). Use this in conjunction with set_gdtr() to describe TSR(s).
//

void set_tsr(uint16_t tsr_data)
{
  asm volatile("ltr %[src]"
           : // No outputs
           : [src] "m" (tsr_data) // Inputs
           : // No clobbers
         );
}

//----------------------------------------------------------------------------------------------------------------------------------
// Setup_MinimalGDT: Set Up a Minimal Global Descriptor Table
//----------------------------------------------------------------------------------------------------------------------------------
//
// Prepare a minimal GDT for the system and set the Global Descriptor Table Register. UEFI makes a descriptor table and stores it in
// EFI Boot Services memory. Making a static table embedded in the executable itself will ensure a valid GDT is always in EfiLoaderData
// and won't get reclaimed as free memory.
//
// The cs_update() function is part of this GDT set up process, and it is used to update the %CS sgement selector in addition to the
// %DS, %ES, %FS, %GS, and %SS selectors.
//

// This is the whole GDT. See the commented code in Setup_MinimalGDT() for specific details.
__attribute__((aligned(64))) static volatile uint64_t MinimalGDT[5] = {0, 0x00af9a000000ffff, 0x00cf92000000ffff, 0x0080890000000067, 0}; // TSS is a double-sized entry, so it uses the 4th and 5th slots here
__attribute__((aligned(64))) static volatile TSS64_STRUCT tss64 = {0}; // This is static, so this structure can only be read by functions defined in this .c file

void Setup_MinimalGDT(void)
{
  DT_STRUCT gdt_reg_data = {0};
  uint64_t tss64_addr = (uint64_t)(&tss64);

  uint16_t tss64_base1 = (uint16_t)tss64_addr;
  uint8_t  tss64_base2 = (uint8_t)(tss64_addr >> 16);
  uint8_t  tss64_base3 = (uint8_t)(tss64_addr >> 24);
  uint32_t tss64_base4 = (uint32_t)(tss64_addr >> 32);

//DEBUG: // printf("TSS Addr check: %#qx, %#hx, %#hhx, %#hhx, %#x\r\n", tss64_addr, tss64_base1, tss64_base2, tss64_base3, tss64_base4);

  gdt_reg_data.Limit = sizeof(MinimalGDT) - 1;
  // By having MinimalGDT global, the pointer will be in EfiLoaderData.
  gdt_reg_data.BaseAddress = (uint64_t)MinimalGDT;

/* // The below code is left here to explain each entry's values in the above static global MinimalGDT
  // 4 entries: Null, code, data, TSS

  // Null
  ((uint64_t*)MinimalGDT)[0] = 0;

  // x86-64 Code (64-bit code segment)
  MinimalGDT[1].SegmentLimit1 = 0xffff;
  MinimalGDT[1].BaseAddress1 = 0;
  MinimalGDT[1].BaseAddress2 = 0;
  MinimalGDT[1].Misc1 = 0x9a; // P=1, DPL=0, S=1, Exec/Read
  MinimalGDT[1].SegmentLimit2andMisc2 = 0xaf; // G=1, D=0, L=1, AVL=0
  MinimalGDT[1].BaseAddress3 = 0;
  // Note the 'L' bit is specifically for x86-64 code segments, not data segments
  // Data segments need the 32-bit treatment instead

  // x86-64 Data (64- & 32-bit data segment)
  MinimalGDT[2].SegmentLimit1 = 0xffff;
  MinimalGDT[2].BaseAddress1 = 0;
  MinimalGDT[2].BaseAddress2 = 0;
  MinimalGDT[2].Misc1 = 0x92; // P=1, DPL=0, S=1, Read/Write
  MinimalGDT[2].SegmentLimit2andMisc2 = 0xcf; // G=1, D=1, L=0, AVL=0
  MinimalGDT[2].BaseAddress3 = 0;

  // Task Segment Entry (64-bit needs one, even though task-switching isn't supported)
  MinimalGDT[3].SegmentLimit1 = 0x67; // TSS struct is 104 bytes, so limit is 103 (0x67)
  MinimalGDT[3].BaseAddress1 = tss64_base1;
  MinimalGDT[3].BaseAddress2 = tss64_base2;
  MinimalGDT[3].Misc1 = 0x89; // P=1, DPL=0, S=0, TSS Type
  MinimalGDT[3].SegmentLimit2andMisc2 = 0x80; // G=1, D=0, L=0, AVL=0
  MinimalGDT[3].BaseAddress3 = tss64_base3;
  ((uint64_t*)MinimalGDT)[4] = (uint64_t)tss64_base4; // TSS is a double-sized entry
*/
  // The only non-constant in the GDT is the base address of the TSS struct. So let's add it in.
  ( (TSS_LDT_ENTRY_STRUCT*) (&((GDT_ENTRY_STRUCT*)MinimalGDT)[3]) )->BaseAddress1 = tss64_base1;
  ( (TSS_LDT_ENTRY_STRUCT*) (&((GDT_ENTRY_STRUCT*)MinimalGDT)[3]) )->BaseAddress2 = tss64_base2;
  ( (TSS_LDT_ENTRY_STRUCT*) (&((GDT_ENTRY_STRUCT*)MinimalGDT)[3]) )->BaseAddress3 = tss64_base3;
  ( (TSS_LDT_ENTRY_STRUCT*) (&((GDT_ENTRY_STRUCT*)MinimalGDT)[3]) )->BaseAddress4 = tss64_base4; // TSS is a double-sized entry
  // Dang that looks pretty nuts.

//DEBUG:
// printf("MinimalGDT: %#qx : %#qx, %#qx, %#qx, %#qx, %#qx ; reg_data: %#qx, Limit: %hu\r\n", (uint64_t)MinimalGDT, ((uint64_t*)MinimalGDT)[0], ((uint64_t*)MinimalGDT)[1], ((uint64_t*)MinimalGDT)[2], ((uint64_t*)MinimalGDT)[3], ((uint64_t*)MinimalGDT)[4], gdt_reg_data.BaseAddress, gdt_reg_data.Limit);
/*
  for(uint8_t o = 0; o <= 26; o++)
  {
    printf("TSS64: %#x\r\n", ((uint32_t*)(&tss64))[o]);
  }
*/
// END DEBUG

  set_gdtr(gdt_reg_data);
  set_tsr(0x18); // TSS segment is at index 3, and 0x18 >> 3 is 3. 0x18 is 24 in decimal.
  cs_update();
}

// See the bottom of this function for details about what exactly it's doing
static void cs_update(void)
{
  // Code segment is at index 1, and 0x08 >> 3 is 1. 0x08 is 8 in decimal.
  // Data segment is at index 2, and 0x10 >> 3 is 2. 0x10 is 16 in decimal.
  asm volatile("mov $16, %ax \n\t" // Data segment index
               "mov %ax, %ds \n\t"
               "mov %ax, %es \n\t"
               "mov %ax, %fs \n\t"
               "mov %ax, %gs \n\t"
               "mov %ax, %ss \n\t"
               "movq $8, %rdx \n\t" // 64-bit code segment index
               // Store RIP offset, pointing to right after 'lretq'
               "leaq 4(%rip), %rax \n\t" // This is hardcoded to the size of the rest of this little ASM block. %rip points to the next instruction, +4 bytes puts it right after 'lretq'
               "pushq %rdx \n\t"
               "pushq %rax \n\t"
               "lretq \n\t" // NOTE: lretq and retfq are the same. lretq is supported by both GCC and Clang, while retfq works only with GCC. Either way, opcode is 0x48CB.
               // The address loaded into %rax points here (right after 'lretq'), so execution returns to this point without breaking compiler compatibility
              );

  //
  // NOTE: Yes, this function is more than a little weird.
  //
  // cs_update() will have a 'ret'/'retq' (and maybe some other things) after the asm 'retfq'/'lretq'. It's
  // fine, though, because the asm contains a hardcoded jmp to get back to it. Why not just push an asm
  // label located right after 'lretq' that's been preloaded into %rax (so that the label address gets loaded
  // into the instruction pointer on 'lretq')? Well, it turns out that will load an address relative to the
  // kernel file image base in such a way that the address won't get relocated by the boot loader. Mysterious
  // crashes ensue. Doing it this way solves that, and is fundamentally very similar since 4(%rip) is where the
  // label would be, anyways.
  //
  // That stated, I think the insanity here speaks for itself, especially since this is *necessary* to modify
  // the %cs register in 64-bit mode using 'retfq'. Could far jumps and far calls be used? Yes, but not very
  // easily in inline ASM (far jumps in 64-bit mode are very limited: only memory-indirect absolute far
  // jumps can change %cs). Also, using the 'lretq' trick maintains quadword alignment on the stack, which is
  // nice. So really we just have to deal with making farquad returns behave in a very controlled manner...
  // which includes accepting any resulting movie references from 2001. But hey, it works, and it shouldn't
  // cause issues with kernels loaded above 4GB RAM. It works great between Clang and GCC, too.
  //
  // The only issue with this method really is it'll screw up CPU return prediction for a tiny bit (for a
  // small number of subsequent function calls, then it fixes itself). Only need this once, though!
  //
}

//----------------------------------------------------------------------------------------------------------------------------------
// Setup_IDT: Set Up Interrupt Descriptor Table
//----------------------------------------------------------------------------------------------------------------------------------
//
// UEFI makes its own IDT in Boot Services Memory, but it's not super useful outside of what it needed interrupts for (and boot
// services memory is supposed to be freeable memory after ExitBootServices() is called). This function sets up an interrupt table for
// more intricate use, as a properly set up IDT is necessary in order to use interrupts. x86(-64) is a highly interrrupt-driven
// architecture, so this is a pretty important step.
//

__attribute__((aligned(64))) static volatile IDT_GATE_STRUCT IDT_data[256] = {0}; // Reserve static memory for the IDT

// Special stacks. (1ULL << 12) is 4 kiB
#define NMI_STACK_SIZE (1ULL << 12)
#define DF_STACK_SIZE (1ULL << 12)
#define MC_STACK_SIZE (1ULL << 12)
#define BP_STACK_SIZE (1ULL << 12)
// See the ISR section later in this file for XSAVE area sizes

// Ensuring 64-byte alignment for good measure
__attribute__((aligned(64))) static volatile unsigned char NMI_stack[NMI_STACK_SIZE] = {0};
__attribute__((aligned(64))) static volatile unsigned char DF_stack[DF_STACK_SIZE] = {0};
__attribute__((aligned(64))) static volatile unsigned char MC_stack[MC_STACK_SIZE] = {0};
__attribute__((aligned(64))) static volatile unsigned char BP_stack[BP_STACK_SIZE] = {0}; // Used for #BP and #DB

// TODO: IRQs from hardware (keyboard interrupts, e.g.) might need their own stack, too.

void Setup_IDT(void)
{
  DT_STRUCT idt_reg_data = {0};
  idt_reg_data.Limit = sizeof(IDT_data) - 1; // Max is 16 bytes * 256 entries = 4096, - 1 = 4095 or 0xfff
  idt_reg_data.BaseAddress = (uint64_t)IDT_data;

  //
  // Set up TSS for special IST switches
  // Note: tss64 was defined in above Setup_MinimalGDT section.
  //
  // This is actually really important to do and not super clear in documentation:
  // Without a separate known good stack, you'll find that calling int $0x08 will trigger a general protection exception--or a divide
  // by zero error with no hander will triple fault. The IST mechanism ensures this does not happen. If calling a handler with int $(num)
  // raises a general protection fault (and it's not the GPF exception 13), it might need its own stack. This is assuming that the IDT
  // (and everything else) has been set up correctly.
  //
  // At the very least, it's a good idea to have separate stacks for NMI, Double Fault (#DF), Machine Check (#MC), and Debug (#BP) and
  // thus each should have a corresponding IST entry. I found that having these 4, and aligning the main kernel stack to 64-bytes (in
  // addition to aligning the 4 other stacks as per their instantiation above), solved a lot of head-scratching problems (like the
  // divide-by-zero triple fault). Intel also recommends the first 3 definitely have their own stack.
  //
  // There is some good documentation on 64-bit stack switching in the Linux kernel docs:
  // https://www.kernel.org/doc/Documentation/x86/kernel-stacks
  //

  // The address in IST gets loaded into %rsp, so end of the stack (not 'end - 1') address is needed

  *(uint64_t*)(&tss64.IST_1_low) = (uint64_t) (NMI_stack + NMI_STACK_SIZE);
  *(uint64_t*)(&tss64.IST_2_low) = (uint64_t) (DF_stack + DF_STACK_SIZE);
  *(uint64_t*)(&tss64.IST_3_low) = (uint64_t) (MC_stack + MC_STACK_SIZE);
  *(uint64_t*)(&tss64.IST_4_low) = (uint64_t) (BP_stack + BP_STACK_SIZE);

/* // Same thing as above
  tss64.IST_1_low = (uint32_t) ((uint64_t) (&NMI_stack[NMI_STACK_SIZE]));
  tss64.IST_1_high = (uint32_t) ( ((uint64_t) (&NMI_stack[NMI_STACK_SIZE])) >> 32 );

  tss64.IST_2_low = (uint32_t) ((uint64_t) (&DF_stack[DF_STACK_SIZE]));
  tss64.IST_2_high = (uint32_t) ( ((uint64_t) (&DF_stack[DF_STACK_SIZE])) >> 32 );

  tss64.IST_3_low = (uint32_t) ((uint64_t) (&MC_stack[MC_STACK_SIZE]));
  tss64.IST_3_high = (uint32_t) ( ((uint64_t) (&MC_stack[MC_STACK_SIZE])) >> 32 );

  tss64.IST_4_low = (uint32_t) ((uint64_t) (&BP_stack[BP_STACK_SIZE]));
  tss64.IST_4_high = (uint32_t) ( ((uint64_t) (&BP_stack[BP_STACK_SIZE])) >> 32 );
*/

  // Set up ISRs per ISR.S layout

  //
  // Predefined System Interrupts and Exceptions
  //

  set_interrupt_entry(0, (uint64_t)DE_ISR_pusher0); // Fault #DE: Divide Error (divide by 0 or not enough bits in destination)
  set_BP_interrupt_entry(1, (uint64_t)DB_ISR_pusher1); // Fault/Trap #DB: Debug Exception, using same IST as #BP
  set_NMI_interrupt_entry(2, (uint64_t)NMI_ISR_pusher2); // NMI (Nonmaskable External Interrupt)
  // Fun fact: Hyper-V will send a watchdog timeout via an NMI if the system is halted for a while. Looks like it's supposed to crash the VM via
  // triple fault if there's no handler set up. Hpyer-V-Worker logs that the VM "has encountered a watchdog timeout and was reset" in the Windows
  // event viewer when the VM receives the NMI. Neat.
  set_BP_interrupt_entry(3, (uint64_t)BP_ISR_pusher3); // Trap #BP: Breakpoint (INT3 instruction)
  set_interrupt_entry(4, (uint64_t)OF_ISR_pusher4); // Trap #OF: Overflow (INTO instruction)
  set_interrupt_entry(5, (uint64_t)BR_ISR_pusher5); // Fault #BR: BOUND Range Exceeded (BOUND instruction)
  set_interrupt_entry(6, (uint64_t)UD_ISR_pusher6); // Fault #UD: Invalid or Undefined Opcode
  set_interrupt_entry(7, (uint64_t)NM_ISR_pusher7); // Fault #NM: Device Not Available Exception

  set_DF_interrupt_entry(8, (uint64_t)DF_EXC_pusher8); // Abort #DF: Double Fault (error code is always 0)

  set_interrupt_entry(9, (uint64_t)CSO_ISR_pusher9); // Fault (i386): Coprocessor Segment Overrun (long since obsolete, included for completeness)

  set_interrupt_entry(10, (uint64_t)TS_EXC_pusher10); // Fault #TS: Invalid TSS
  set_interrupt_entry(11, (uint64_t)NP_EXC_pusher11); // Fault #NP: Segment Not Present
  set_interrupt_entry(12, (uint64_t)SS_EXC_pusher12); // Fault #SS: Stack Segment Fault
  set_interrupt_entry(13, (uint64_t)GP_EXC_pusher13); // Fault #GP: General Protection
  set_interrupt_entry(14, (uint64_t)PF_EXC_pusher14); // Fault #PF: Page Fault

  set_interrupt_entry(16, (uint64_t)MF_ISR_pusher16); // Fault #MF: Math Error (x87 FPU Floating-Point Math Error)

  set_interrupt_entry(17, (uint64_t)AC_EXC_pusher17); // Fault #AC: Alignment Check (error code is always 0)

  set_MC_interrupt_entry(18, (uint64_t)MC_ISR_pusher18); // Abort #MC: Machine Check
  set_interrupt_entry(19, (uint64_t)XM_ISR_pusher19); // Fault #XM: SIMD Floating-Point Exception (SSE instructions)
  set_interrupt_entry(20, (uint64_t)VE_ISR_pusher20); // Fault #VE: Virtualization Exception

  set_interrupt_entry(30, (uint64_t)SX_EXC_pusher30); // Fault #SX: Security Exception

  //
  // These are system reserved, if they trigger they will go to unhandled interrupt error
  //

  set_interrupt_entry(15, (uint64_t)CPU_ISR_pusher15);

  set_interrupt_entry(21, (uint64_t)CPU_ISR_pusher21);
  set_interrupt_entry(22, (uint64_t)CPU_ISR_pusher22);
  set_interrupt_entry(23, (uint64_t)CPU_ISR_pusher23);
  set_interrupt_entry(24, (uint64_t)CPU_ISR_pusher24);
  set_interrupt_entry(25, (uint64_t)CPU_ISR_pusher25);
  set_interrupt_entry(26, (uint64_t)CPU_ISR_pusher26);
  set_interrupt_entry(27, (uint64_t)CPU_ISR_pusher27);
  set_interrupt_entry(28, (uint64_t)CPU_ISR_pusher28);
  set_interrupt_entry(29, (uint64_t)CPU_ISR_pusher29);

  set_interrupt_entry(31, (uint64_t)CPU_ISR_pusher31);

  //
  // User-Defined Interrupts
  //

  // By default everything is set to USER_ISR_MACRO.

  set_interrupt_entry(32, (uint64_t)User_ISR_pusher32);
  set_interrupt_entry(33, (uint64_t)User_ISR_pusher33);
  set_interrupt_entry(34, (uint64_t)User_ISR_pusher34);
  set_interrupt_entry(35, (uint64_t)User_ISR_pusher35);
  set_interrupt_entry(36, (uint64_t)User_ISR_pusher36);
  set_interrupt_entry(37, (uint64_t)User_ISR_pusher37);
  set_interrupt_entry(38, (uint64_t)User_ISR_pusher38);
  set_interrupt_entry(39, (uint64_t)User_ISR_pusher39);
  set_interrupt_entry(40, (uint64_t)User_ISR_pusher40);
  set_interrupt_entry(41, (uint64_t)User_ISR_pusher41);
  set_interrupt_entry(42, (uint64_t)User_ISR_pusher42);
  set_interrupt_entry(43, (uint64_t)User_ISR_pusher43);
  set_interrupt_entry(44, (uint64_t)User_ISR_pusher44);
  set_interrupt_entry(45, (uint64_t)User_ISR_pusher45);
  set_interrupt_entry(46, (uint64_t)User_ISR_pusher46);
  set_interrupt_entry(47, (uint64_t)User_ISR_pusher47);
  set_interrupt_entry(48, (uint64_t)User_ISR_pusher48);
  set_interrupt_entry(49, (uint64_t)User_ISR_pusher49);
  set_interrupt_entry(50, (uint64_t)User_ISR_pusher50);
  set_interrupt_entry(51, (uint64_t)User_ISR_pusher51);
  set_interrupt_entry(52, (uint64_t)User_ISR_pusher52);
  set_interrupt_entry(53, (uint64_t)User_ISR_pusher53);
  set_interrupt_entry(54, (uint64_t)User_ISR_pusher54);
  set_interrupt_entry(55, (uint64_t)User_ISR_pusher55);
  set_interrupt_entry(56, (uint64_t)User_ISR_pusher56);
  set_interrupt_entry(57, (uint64_t)User_ISR_pusher57);
  set_interrupt_entry(58, (uint64_t)User_ISR_pusher58);
  set_interrupt_entry(59, (uint64_t)User_ISR_pusher59);
  set_interrupt_entry(60, (uint64_t)User_ISR_pusher60);
  set_interrupt_entry(61, (uint64_t)User_ISR_pusher61);
  set_interrupt_entry(62, (uint64_t)User_ISR_pusher62);
  set_interrupt_entry(63, (uint64_t)User_ISR_pusher63);
  set_interrupt_entry(64, (uint64_t)User_ISR_pusher64);
  set_interrupt_entry(65, (uint64_t)User_ISR_pusher65);
  set_interrupt_entry(66, (uint64_t)User_ISR_pusher66);
  set_interrupt_entry(67, (uint64_t)User_ISR_pusher67);
  set_interrupt_entry(68, (uint64_t)User_ISR_pusher68);
  set_interrupt_entry(69, (uint64_t)User_ISR_pusher69);
  set_interrupt_entry(70, (uint64_t)User_ISR_pusher70);
  set_interrupt_entry(71, (uint64_t)User_ISR_pusher71);
  set_interrupt_entry(72, (uint64_t)User_ISR_pusher72);
  set_interrupt_entry(73, (uint64_t)User_ISR_pusher73);
  set_interrupt_entry(74, (uint64_t)User_ISR_pusher74);
  set_interrupt_entry(75, (uint64_t)User_ISR_pusher75);
  set_interrupt_entry(76, (uint64_t)User_ISR_pusher76);
  set_interrupt_entry(77, (uint64_t)User_ISR_pusher77);
  set_interrupt_entry(78, (uint64_t)User_ISR_pusher78);
  set_interrupt_entry(79, (uint64_t)User_ISR_pusher79);
  set_interrupt_entry(80, (uint64_t)User_ISR_pusher80);
  set_interrupt_entry(81, (uint64_t)User_ISR_pusher81);
  set_interrupt_entry(82, (uint64_t)User_ISR_pusher82);
  set_interrupt_entry(83, (uint64_t)User_ISR_pusher83);
  set_interrupt_entry(84, (uint64_t)User_ISR_pusher84);
  set_interrupt_entry(85, (uint64_t)User_ISR_pusher85);
  set_interrupt_entry(86, (uint64_t)User_ISR_pusher86);
  set_interrupt_entry(87, (uint64_t)User_ISR_pusher87);
  set_interrupt_entry(88, (uint64_t)User_ISR_pusher88);
  set_interrupt_entry(89, (uint64_t)User_ISR_pusher89);
  set_interrupt_entry(90, (uint64_t)User_ISR_pusher90);
  set_interrupt_entry(91, (uint64_t)User_ISR_pusher91);
  set_interrupt_entry(92, (uint64_t)User_ISR_pusher92);
  set_interrupt_entry(93, (uint64_t)User_ISR_pusher93);
  set_interrupt_entry(94, (uint64_t)User_ISR_pusher94);
  set_interrupt_entry(95, (uint64_t)User_ISR_pusher95);
  set_interrupt_entry(96, (uint64_t)User_ISR_pusher96);
  set_interrupt_entry(97, (uint64_t)User_ISR_pusher97);
  set_interrupt_entry(98, (uint64_t)User_ISR_pusher98);
  set_interrupt_entry(99, (uint64_t)User_ISR_pusher99);
  set_interrupt_entry(100, (uint64_t)User_ISR_pusher100);
  set_interrupt_entry(101, (uint64_t)User_ISR_pusher101);
  set_interrupt_entry(102, (uint64_t)User_ISR_pusher102);
  set_interrupt_entry(103, (uint64_t)User_ISR_pusher103);
  set_interrupt_entry(104, (uint64_t)User_ISR_pusher104);
  set_interrupt_entry(105, (uint64_t)User_ISR_pusher105);
  set_interrupt_entry(106, (uint64_t)User_ISR_pusher106);
  set_interrupt_entry(107, (uint64_t)User_ISR_pusher107);
  set_interrupt_entry(108, (uint64_t)User_ISR_pusher108);
  set_interrupt_entry(109, (uint64_t)User_ISR_pusher109);
  set_interrupt_entry(110, (uint64_t)User_ISR_pusher110);
  set_interrupt_entry(111, (uint64_t)User_ISR_pusher111);
  set_interrupt_entry(112, (uint64_t)User_ISR_pusher112);
  set_interrupt_entry(113, (uint64_t)User_ISR_pusher113);
  set_interrupt_entry(114, (uint64_t)User_ISR_pusher114);
  set_interrupt_entry(115, (uint64_t)User_ISR_pusher115);
  set_interrupt_entry(116, (uint64_t)User_ISR_pusher116);
  set_interrupt_entry(117, (uint64_t)User_ISR_pusher117);
  set_interrupt_entry(118, (uint64_t)User_ISR_pusher118);
  set_interrupt_entry(119, (uint64_t)User_ISR_pusher119);
  set_interrupt_entry(120, (uint64_t)User_ISR_pusher120);
  set_interrupt_entry(121, (uint64_t)User_ISR_pusher121);
  set_interrupt_entry(122, (uint64_t)User_ISR_pusher122);
  set_interrupt_entry(123, (uint64_t)User_ISR_pusher123);
  set_interrupt_entry(124, (uint64_t)User_ISR_pusher124);
  set_interrupt_entry(125, (uint64_t)User_ISR_pusher125);
  set_interrupt_entry(126, (uint64_t)User_ISR_pusher126);
  set_interrupt_entry(127, (uint64_t)User_ISR_pusher127);
  set_interrupt_entry(128, (uint64_t)User_ISR_pusher128);
  set_interrupt_entry(129, (uint64_t)User_ISR_pusher129);
  set_interrupt_entry(130, (uint64_t)User_ISR_pusher130);
  set_interrupt_entry(131, (uint64_t)User_ISR_pusher131);
  set_interrupt_entry(132, (uint64_t)User_ISR_pusher132);
  set_interrupt_entry(133, (uint64_t)User_ISR_pusher133);
  set_interrupt_entry(134, (uint64_t)User_ISR_pusher134);
  set_interrupt_entry(135, (uint64_t)User_ISR_pusher135);
  set_interrupt_entry(136, (uint64_t)User_ISR_pusher136);
  set_interrupt_entry(137, (uint64_t)User_ISR_pusher137);
  set_interrupt_entry(138, (uint64_t)User_ISR_pusher138);
  set_interrupt_entry(139, (uint64_t)User_ISR_pusher139);
  set_interrupt_entry(140, (uint64_t)User_ISR_pusher140);
  set_interrupt_entry(141, (uint64_t)User_ISR_pusher141);
  set_interrupt_entry(142, (uint64_t)User_ISR_pusher142);
  set_interrupt_entry(143, (uint64_t)User_ISR_pusher143);
  set_interrupt_entry(144, (uint64_t)User_ISR_pusher144);
  set_interrupt_entry(145, (uint64_t)User_ISR_pusher145);
  set_interrupt_entry(146, (uint64_t)User_ISR_pusher146);
  set_interrupt_entry(147, (uint64_t)User_ISR_pusher147);
  set_interrupt_entry(148, (uint64_t)User_ISR_pusher148);
  set_interrupt_entry(149, (uint64_t)User_ISR_pusher149);
  set_interrupt_entry(150, (uint64_t)User_ISR_pusher150);
  set_interrupt_entry(151, (uint64_t)User_ISR_pusher151);
  set_interrupt_entry(152, (uint64_t)User_ISR_pusher152);
  set_interrupt_entry(153, (uint64_t)User_ISR_pusher153);
  set_interrupt_entry(154, (uint64_t)User_ISR_pusher154);
  set_interrupt_entry(155, (uint64_t)User_ISR_pusher155);
  set_interrupt_entry(156, (uint64_t)User_ISR_pusher156);
  set_interrupt_entry(157, (uint64_t)User_ISR_pusher157);
  set_interrupt_entry(158, (uint64_t)User_ISR_pusher158);
  set_interrupt_entry(159, (uint64_t)User_ISR_pusher159);
  set_interrupt_entry(160, (uint64_t)User_ISR_pusher160);
  set_interrupt_entry(161, (uint64_t)User_ISR_pusher161);
  set_interrupt_entry(162, (uint64_t)User_ISR_pusher162);
  set_interrupt_entry(163, (uint64_t)User_ISR_pusher163);
  set_interrupt_entry(164, (uint64_t)User_ISR_pusher164);
  set_interrupt_entry(165, (uint64_t)User_ISR_pusher165);
  set_interrupt_entry(166, (uint64_t)User_ISR_pusher166);
  set_interrupt_entry(167, (uint64_t)User_ISR_pusher167);
  set_interrupt_entry(168, (uint64_t)User_ISR_pusher168);
  set_interrupt_entry(169, (uint64_t)User_ISR_pusher169);
  set_interrupt_entry(170, (uint64_t)User_ISR_pusher170);
  set_interrupt_entry(171, (uint64_t)User_ISR_pusher171);
  set_interrupt_entry(172, (uint64_t)User_ISR_pusher172);
  set_interrupt_entry(173, (uint64_t)User_ISR_pusher173);
  set_interrupt_entry(174, (uint64_t)User_ISR_pusher174);
  set_interrupt_entry(175, (uint64_t)User_ISR_pusher175);
  set_interrupt_entry(176, (uint64_t)User_ISR_pusher176);
  set_interrupt_entry(177, (uint64_t)User_ISR_pusher177);
  set_interrupt_entry(178, (uint64_t)User_ISR_pusher178);
  set_interrupt_entry(179, (uint64_t)User_ISR_pusher179);
  set_interrupt_entry(180, (uint64_t)User_ISR_pusher180);
  set_interrupt_entry(181, (uint64_t)User_ISR_pusher181);
  set_interrupt_entry(182, (uint64_t)User_ISR_pusher182);
  set_interrupt_entry(183, (uint64_t)User_ISR_pusher183);
  set_interrupt_entry(184, (uint64_t)User_ISR_pusher184);
  set_interrupt_entry(185, (uint64_t)User_ISR_pusher185);
  set_interrupt_entry(186, (uint64_t)User_ISR_pusher186);
  set_interrupt_entry(187, (uint64_t)User_ISR_pusher187);
  set_interrupt_entry(188, (uint64_t)User_ISR_pusher188);
  set_interrupt_entry(189, (uint64_t)User_ISR_pusher189);
  set_interrupt_entry(190, (uint64_t)User_ISR_pusher190);
  set_interrupt_entry(191, (uint64_t)User_ISR_pusher191);
  set_interrupt_entry(192, (uint64_t)User_ISR_pusher192);
  set_interrupt_entry(193, (uint64_t)User_ISR_pusher193);
  set_interrupt_entry(194, (uint64_t)User_ISR_pusher194);
  set_interrupt_entry(195, (uint64_t)User_ISR_pusher195);
  set_interrupt_entry(196, (uint64_t)User_ISR_pusher196);
  set_interrupt_entry(197, (uint64_t)User_ISR_pusher197);
  set_interrupt_entry(198, (uint64_t)User_ISR_pusher198);
  set_interrupt_entry(199, (uint64_t)User_ISR_pusher199);
  set_interrupt_entry(200, (uint64_t)User_ISR_pusher200);
  set_interrupt_entry(201, (uint64_t)User_ISR_pusher201);
  set_interrupt_entry(202, (uint64_t)User_ISR_pusher202);
  set_interrupt_entry(203, (uint64_t)User_ISR_pusher203);
  set_interrupt_entry(204, (uint64_t)User_ISR_pusher204);
  set_interrupt_entry(205, (uint64_t)User_ISR_pusher205);
  set_interrupt_entry(206, (uint64_t)User_ISR_pusher206);
  set_interrupt_entry(207, (uint64_t)User_ISR_pusher207);
  set_interrupt_entry(208, (uint64_t)User_ISR_pusher208);
  set_interrupt_entry(209, (uint64_t)User_ISR_pusher209);
  set_interrupt_entry(210, (uint64_t)User_ISR_pusher210);
  set_interrupt_entry(211, (uint64_t)User_ISR_pusher211);
  set_interrupt_entry(212, (uint64_t)User_ISR_pusher212);
  set_interrupt_entry(213, (uint64_t)User_ISR_pusher213);
  set_interrupt_entry(214, (uint64_t)User_ISR_pusher214);
  set_interrupt_entry(215, (uint64_t)User_ISR_pusher215);
  set_interrupt_entry(216, (uint64_t)User_ISR_pusher216);
  set_interrupt_entry(217, (uint64_t)User_ISR_pusher217);
  set_interrupt_entry(218, (uint64_t)User_ISR_pusher218);
  set_interrupt_entry(219, (uint64_t)User_ISR_pusher219);
  set_interrupt_entry(220, (uint64_t)User_ISR_pusher220);
  set_interrupt_entry(221, (uint64_t)User_ISR_pusher221);
  set_interrupt_entry(222, (uint64_t)User_ISR_pusher222);
  set_interrupt_entry(223, (uint64_t)User_ISR_pusher223);
  set_interrupt_entry(224, (uint64_t)User_ISR_pusher224);
  set_interrupt_entry(225, (uint64_t)User_ISR_pusher225);
  set_interrupt_entry(226, (uint64_t)User_ISR_pusher226);
  set_interrupt_entry(227, (uint64_t)User_ISR_pusher227);
  set_interrupt_entry(228, (uint64_t)User_ISR_pusher228);
  set_interrupt_entry(229, (uint64_t)User_ISR_pusher229);
  set_interrupt_entry(230, (uint64_t)User_ISR_pusher230);
  set_interrupt_entry(231, (uint64_t)User_ISR_pusher231);
  set_interrupt_entry(232, (uint64_t)User_ISR_pusher232);
  set_interrupt_entry(233, (uint64_t)User_ISR_pusher233);
  set_interrupt_entry(234, (uint64_t)User_ISR_pusher234);
  set_interrupt_entry(235, (uint64_t)User_ISR_pusher235);
  set_interrupt_entry(236, (uint64_t)User_ISR_pusher236);
  set_interrupt_entry(237, (uint64_t)User_ISR_pusher237);
  set_interrupt_entry(238, (uint64_t)User_ISR_pusher238);
  set_interrupt_entry(239, (uint64_t)User_ISR_pusher239);
  set_interrupt_entry(240, (uint64_t)User_ISR_pusher240);
  set_interrupt_entry(241, (uint64_t)User_ISR_pusher241);
  set_interrupt_entry(242, (uint64_t)User_ISR_pusher242);
  set_interrupt_entry(243, (uint64_t)User_ISR_pusher243);
  set_interrupt_entry(244, (uint64_t)User_ISR_pusher244);
  set_interrupt_entry(245, (uint64_t)User_ISR_pusher245);
  set_interrupt_entry(246, (uint64_t)User_ISR_pusher246);
  set_interrupt_entry(247, (uint64_t)User_ISR_pusher247);
  set_interrupt_entry(248, (uint64_t)User_ISR_pusher248);
  set_interrupt_entry(249, (uint64_t)User_ISR_pusher249);
  set_interrupt_entry(250, (uint64_t)User_ISR_pusher250);
  set_interrupt_entry(251, (uint64_t)User_ISR_pusher251);
  set_interrupt_entry(252, (uint64_t)User_ISR_pusher252);
  set_interrupt_entry(253, (uint64_t)User_ISR_pusher253);
  set_interrupt_entry(254, (uint64_t)User_ISR_pusher254);
  set_interrupt_entry(255, (uint64_t)User_ISR_pusher255);

  //
  // Gotta love spreadsheet macros for this kind of stuff.
  // Use =$A$1&TEXT(B1,"00")&$C$1&TEXT(D1,"00")&$E$1 in Excel, with the below setup
  // (square brackets denote a cell):
  //
  //          A              B                C                D    E
  // 1 [set_interrupt_entry(] [32] [, (uint64_t)User_ISR_pusher] [32] [);]
  // 2                      [33]                              [33]
  // 3                      [34]                              [34]
  // ...                     ...                               ...
  // 255                    [255]                             [255]
  //
  // Drag the bottom right corner of the cell containing the above macro--there should
  // be a little square there--and grin as 224 sequential interrupt functions
  // technomagically appear. :)
  //
  // Adapted from: https://superuser.com/questions/559218/increment-numbers-inside-a-string
  //

  set_idtr(idt_reg_data);
}

// Set up corresponding ISR function's IDT entry
// This is for architecturally-defined ISRs (IDT entries 0-31) and user-defined entries (32-255)
static void set_interrupt_entry(uint64_t isr_num, uint64_t isr_addr)
{
  uint16_t isr_base1 = (uint16_t)isr_addr;
  uint16_t isr_base2 = (uint16_t)(isr_addr >> 16);
  uint32_t isr_base3 = (uint32_t)(isr_addr >> 32);

  IDT_data[isr_num].Offset1 = isr_base1; // CS base is 0, so offset becomes the base address of ISR (interrupt service routine)
  IDT_data[isr_num].SegmentSelector = 0x08; // 64-bit code segment in GDT
  IDT_data[isr_num].ISTandZero = 0; // IST of 0 uses "modified legacy stack switch mechanism" (Intel Architecture Manual Vol. 3A, Section 6.14.4 Stack Switching in IA-32e Mode)
  // IST = 0 only matters for inter-privilege level changes that arise from interrrupts--keeping the same level doesn't switch stacks
  // IST = 1-7 would apply regardless of privilege level.
  IDT_data[isr_num].Misc = 0x8E; // 0x8E for interrupt (clears IF in RFLAGS), 0x8F for trap (does not clear IF in RFLAGS), P=1, DPL=0, S=0
  IDT_data[isr_num].Offset2 = isr_base2;
  IDT_data[isr_num].Offset3 = isr_base3;
  IDT_data[isr_num].Reserved = 0;
}

// This is to set up a trap gate in the IDT for a given ISR
static void set_trap_entry(uint64_t isr_num, uint64_t isr_addr)
{
  uint16_t isr_base1 = (uint16_t)isr_addr;
  uint16_t isr_base2 = (uint16_t)(isr_addr >> 16);
  uint32_t isr_base3 = (uint32_t)(isr_addr >> 32);

  IDT_data[isr_num].Offset1 = isr_base1; // CS base is 0, so offset becomes the base address of ISR (interrupt service routine)
  IDT_data[isr_num].SegmentSelector = 0x08; // 64-bit code segment in GDT
  IDT_data[isr_num].ISTandZero = 0; // IST of 0 uses "modified legacy stack switch mechanism" (Intel Architecture Manual Vol. 3A, Section 6.14.4 Stack Switching in IA-32e Mode)
  // IST = 0 only matters for inter-privilege level changes that arise from interrrupts--keeping the same level doesn't switch stacks
  // IST = 1-7 would apply regardless of privilege level.
  IDT_data[isr_num].Misc = 0x8F; // 0x8E for interrupt (clears IF in RFLAGS), 0x8F for trap (does not clear IF in RFLAGS), P=1, DPL=0, S=0
  IDT_data[isr_num].Offset2 = isr_base2;
  IDT_data[isr_num].Offset3 = isr_base3;
  IDT_data[isr_num].Reserved = 0;
}

// This is for unused ISRs. They need to be populated otherwise the CPU will triple fault.
static void set_unused_entry(uint64_t isr_num)
{
  IDT_data[isr_num].Offset1 = 0;
  IDT_data[isr_num].SegmentSelector = 0x08;
  IDT_data[isr_num].ISTandZero = 0;
  IDT_data[isr_num].Misc = 0x0E; // P=0, DPL=0, S=0, placeholder interrupt type
  IDT_data[isr_num].Offset2 = 0;
  IDT_data[isr_num].Offset3 = 0;
  IDT_data[isr_num].Reserved = 0;
}

// These are special entries that make use of the IST mechanism for stack switching in 64-bit mode

// Nonmaskable interrupt
static void set_NMI_interrupt_entry(uint64_t isr_num, uint64_t isr_addr)
{
  uint16_t isr_base1 = (uint16_t)isr_addr;
  uint16_t isr_base2 = (uint16_t)(isr_addr >> 16);
  uint32_t isr_base3 = (uint32_t)(isr_addr >> 32);

  IDT_data[isr_num].Offset1 = isr_base1; // CS base is 0, so offset becomes the base address of ISR (interrupt service routine)
  IDT_data[isr_num].SegmentSelector = 0x08; // 64-bit code segment in GDT
  IDT_data[isr_num].ISTandZero = 1; // IST of 0 uses "modified legacy stack switch mechanism" (Intel Architecture Manual Vol. 3A, Section 6.14.4 Stack Switching in IA-32e Mode)
  // IST = 0 only matters for inter-privilege level changes that arise from interrrupts--keeping the same level doesn't switch stacks
  // IST = 1-7 would apply regardless of privilege level.
  IDT_data[isr_num].Misc = 0x8E; // 0x8E for interrupt (clears IF in RFLAGS), 0x8F for trap (does not clear IF in RFLAGS), P=1, DPL=0, S=0
  IDT_data[isr_num].Offset2 = isr_base2;
  IDT_data[isr_num].Offset3 = isr_base3;
  IDT_data[isr_num].Reserved = 0;
}

// Double fault
static void set_DF_interrupt_entry(uint64_t isr_num, uint64_t isr_addr)
{
  uint16_t isr_base1 = (uint16_t)isr_addr;
  uint16_t isr_base2 = (uint16_t)(isr_addr >> 16);
  uint32_t isr_base3 = (uint32_t)(isr_addr >> 32);

  IDT_data[isr_num].Offset1 = isr_base1; // CS base is 0, so offset becomes the base address of ISR (interrupt service routine)
  IDT_data[isr_num].SegmentSelector = 0x08; // 64-bit code segment in GDT
  IDT_data[isr_num].ISTandZero = 2; // IST of 0 uses "modified legacy stack switch mechanism" (Intel Architecture Manual Vol. 3A, Section 6.14.4 Stack Switching in IA-32e Mode)
  // IST = 0 only matters for inter-privilege level changes that arise from interrrupts--keeping the same level doesn't switch stacks
  // IST = 1-7 would apply regardless of privilege level.
  IDT_data[isr_num].Misc = 0x8E; // 0x8E for interrupt (clears IF in RFLAGS), 0x8F for trap (does not clear IF in RFLAGS), P=1, DPL=0, S=0
  IDT_data[isr_num].Offset2 = isr_base2;
  IDT_data[isr_num].Offset3 = isr_base3;
  IDT_data[isr_num].Reserved = 0;
}

// Machine Check
static void set_MC_interrupt_entry(uint64_t isr_num, uint64_t isr_addr)
{
  uint16_t isr_base1 = (uint16_t)isr_addr;
  uint16_t isr_base2 = (uint16_t)(isr_addr >> 16);
  uint32_t isr_base3 = (uint32_t)(isr_addr >> 32);

  IDT_data[isr_num].Offset1 = isr_base1; // CS base is 0, so offset becomes the base address of ISR (interrupt service routine)
  IDT_data[isr_num].SegmentSelector = 0x08; // 64-bit code segment in GDT
  IDT_data[isr_num].ISTandZero = 3; // IST of 0 uses "modified legacy stack switch mechanism" (Intel Architecture Manual Vol. 3A, Section 6.14.4 Stack Switching in IA-32e Mode)
  // IST = 0 only matters for inter-privilege level changes that arise from interrrupts--keeping the same level doesn't switch stacks
  // IST = 1-7 would apply regardless of privilege level.
  IDT_data[isr_num].Misc = 0x8E; // 0x8E for interrupt (clears IF in RFLAGS), 0x8F for trap (does not clear IF in RFLAGS), P=1, DPL=0, S=0
  IDT_data[isr_num].Offset2 = isr_base2;
  IDT_data[isr_num].Offset3 = isr_base3;
  IDT_data[isr_num].Reserved = 0;
}

// Debug (INT3)
static void set_BP_interrupt_entry(uint64_t isr_num, uint64_t isr_addr)
{
  uint16_t isr_base1 = (uint16_t)isr_addr;
  uint16_t isr_base2 = (uint16_t)(isr_addr >> 16);
  uint32_t isr_base3 = (uint32_t)(isr_addr >> 32);

  IDT_data[isr_num].Offset1 = isr_base1; // CS base is 0, so offset becomes the base address of ISR (interrupt service routine)
  IDT_data[isr_num].SegmentSelector = 0x08; // 64-bit code segment in GDT
  IDT_data[isr_num].ISTandZero = 4; // IST of 0 uses "modified legacy stack switch mechanism" (Intel Architecture Manual Vol. 3A, Section 6.14.4 Stack Switching in IA-32e Mode)
  // IST = 0 only matters for inter-privilege level changes that arise from interrrupts--keeping the same level doesn't switch stacks
  // IST = 1-7 would apply regardless of privilege level.
  IDT_data[isr_num].Misc = 0x8E; // 0x8E for interrupt (clears IF in RFLAGS), 0x8F for trap (does not clear IF in RFLAGS), P=1, DPL=0, S=0
  IDT_data[isr_num].Offset2 = isr_base2;
  IDT_data[isr_num].Offset3 = isr_base3;
  IDT_data[isr_num].Reserved = 0;
}


//----------------------------------------------------------------------------------------------------------------------------------
// Setup_Paging: Set Up Paging Structures
//----------------------------------------------------------------------------------------------------------------------------------
//
// UEFI sets up paging structures in EFI Boot Services Memory. Since that memory is to be reclaimed as free memory, there needs to be
// valid paging structures elsewhere doing the job. This function takes care of that and sets up paging structures, ensuring that UEFI
// data in boot services memory is not relied on. Specifically, this sets up identity or 1:1 paging using 1GB pages where possible.
//
// Extra note:
// When using pages, one could follow the example of existing systems and use page files or virtual mappings. I personally propose
// using 1GB pages as "work areas" for multiple cores, particularly since this framework includes no concept of "user space" or
// "kernel space"--everything is in ring 0 when used as-is. Sectioning memory this way could allow multiple cores or hyperthreads to
// each have their own whole number of GBs to work within.
//
// In a system like this, shared data/code pages can be marked by the 'G' (Global) bit, and the 'NX' (No eXecute) bit can be used if a
// page is only meant to contain data. TLB flushing doesn't really matter here since address space switches don't occur: that's what the
// 'G' bit was originally meant for as it would prevent the TLB from flushing that page during a switch. The 'G' bit is ignored by the
// CPU hardware for this purpose if CR4.PGE = 0, but for this use case it wouldn't really matter what value CR4.PGE is. I set it to 1 in
// case anyone wants to use it for its intended purpose, like if someone wants to change CR3 and not invalidate certain pages in the TLB.
//
// In any case, it's a flexible system that works better the more RAM there is, and no swapping or adding/removing pages is necessary. It
// really all comes down to users architecting how they want to use their system memory, though, and this is just one way. Also, the
// malloc() functions provided by this framework are completely decoupled from the paging mechanism: they use the memory map directly,
// which is why they can allocate 4kB memory even though Setup_Paging() sets up 1GB pages. That's important to keep in mind when making
// any changes.
//

// The outermost table (e.g. PML4, PML5) will always take up 4kB, so it can be defined statically like this.
__attribute__((aligned(4096))) static uint64_t outermost_table[512] = {0};
// Since inner tables may not exist for many entries, only the outermost table of the struct is guaranteed to be one table of 4kB bytes.
// Statically allocating an area would otherwise take up a whole 1GB+2MB+4KB to account for fully-loaded 5-level paging with 1GB pages!
// It would be 2MB+4KB for fully-loaded 4-level paging with 1GB pages, or 1GB+2MB+4KB for fully-loaded 4-level paging with 2MB pages.
// Don't even consider using 4kB pages--the paging structures in total take up 512x those numbers worst-case... That's 512GB RAM eaten
// up JUST for 4-level paging structures!

// Page tables are all the same size...
#define PAGE_TABLE_SIZE 512*8

void Setup_Paging(void)
{
  // Disable global pages since we're going to overhaul the page table and don't want anything lingering from UEFI
  // Disable CR4.PGE
  uint64_t cr4 = control_register_rw(4, 0, 0);
  if(cr4 & (1 << 7)) // If on, turn off, else ignore
  {
    uint64_t cr4_2 = cr4 ^ (1 << 7); // Bit toggle
    control_register_rw(4, cr4_2, 1);
    cr4_2 = control_register_rw(4, 0, 0);
    if(cr4_2 == cr4)
    {
      warning_printf("Error disabling CR4.PGE.\r\n");
    }
  }

  // Ok, how much mapped memory do we have that needs to be in CPU pages?

  // The ACPI standard expects the UEFI memory map to describe *all installed memory* and not any virtual address spaces.
  // This means the UEFI map can be used to get the total physical address space (but not by adding up all the pages to due to the variable-sized "hole" from 0xA0000 to 0xFFFFF).
  // See ACPI Specficiation 6.2A, Section 15.4 (UEFI Assumptions and Limitations)
  uint64_t max_ram = GetMaxMappedPhysicalAddress();
//  printf("Total Mapped Memory: %qu Bytes (= %qu GB), Hex: %#qx\r\n", max_ram, max_ram >> 30, max_ram);

  // Check supported paging sizes (either 1GB paging is supported or we fall back to 2MiB: Sandy Bridge i7s, for example, can't do 1GB pages)
  uint64_t rdx = 0;
  asm volatile("cpuid"
               : "=d" (rdx) // Outputs
               : "a" (0x80000001) // The value to put into %rax
               : "%rbx", "%rcx"// CPUID would clobber any of the abcd registers not listed explicitly (all are here, though)
             );

  // In the future, checking for 512GB and 256TB paging would go here. These might imply 5-level paging support (depends if 512GB paging is ever added to a CPU with only
  // 4-level support), and will need either 2 levels or 1 level of tables, respectively, for a minimal paging setup. The 256TB version would be pretty simple to implement with 'outermost_table'...

  if(rdx & (1 << 26))
  {
    // Use 1GB pages
    printf("1 GB pages are available.\r\n");

    // Do we have 5-level paging?
    // Although Intel states it's still a draft, the Linux Kernel went ahead and added 5-level paging around 2016-2017, it seems. Well, I've
    // also added it here for future use. Waaaaaaay future use. And even if the draft changes it isn't hard to modify this code, so no big deal.
    //
    // 5-level paging tables are crazy and can take up to 1GB+2MB+4KB space using 1GB pages. 512GB+1GB+2MB+4KB if using 2MB pages, and
    // 256TB+512GB+1GB+2MB+4KB for 4k pages. Granted, that is for systems with ~128PB RAM, which look like they'll also have 512GB paging.
    // It seems there may also be an option for mapping 256TB pages directly in the PML5.
    //
    // https://www.sandpile.org/x86/paging.htm has a nice visual breakdown of paging structure format.

    uint64_t cr4 = control_register_rw(4, 0, 0);
    if(cr4 & (1 << 12)) // Check CR4.LA57 for 5-level paging support
    {
      // 5-level paging, 3 tables needed for 1GB pages
      printf("5-level paging is active.\r\n");

      if(max_ram >= (1ULL << 52)) // 128PB (1ULL << 57) is the max VA, though 4PB (1ULL << 52) is the max PA
      {
        warning_printf("Hey! There's way too much RAM here. Is the year like 2050 or something?\r\nRAM will be limited to 4PB, the max allowed by 5-level paging wth 1GB pages.\r\n");
        warning_printf("At this point there's probably a new paging size (or a new paging mechanism? Is paging even used anymore?), which needs to be implmented in the code.\r\n");
        warning_printf("8K 120FPS displays must be mainstream by now, too...\r\n");
      }

      uint64_t max_pml5_entry = 1; // Always at least 1 entry

      uint64_t last_pml4_table_max = 1; // Always at least 1 entry
      uint64_t max_pml4_entry = 512;

      uint64_t last_pdp_table_max = 512; // This will decrease to the correct size, but worst-case it will be 512 and account for exactly 512GB RAM
      uint64_t max_pdp_entry = 512;

      // PML5 can hold 512 PML4s
      while(max_ram > (256ULL << 40))
      {
        max_pml5_entry++;
        max_ram -= (256ULL << 40);
      }
      // The last PML5 is accounted for by initializing max_pml5_entry to 1.

      if(max_pml5_entry > 512)
      {
        max_pml5_entry = 512; // If for whatever reason it comes to this some day
      }

      if(max_ram)
      {
        // Each PML4 entry (a PDP table) can account for 512GB RAM when using 1GB page sizes.
        // As it stands PML4s can account for "only" 256TB each.
        // This accounts for the number of PML4s in the last PML5.
        while(max_ram > (512ULL << 30))
        {
          last_pml4_table_max++;
          max_ram -= (512ULL << 30);
        }
        // Any leftover RAM is accounted for in PML4 by initializing last_pml4_table_max to 1.

        if(last_pml4_table_max > 512)
        {
          last_pml4_table_max = 512; // If for whatever reason it comes to this some day
        }

        if(max_ram)
        {
          // The last PDP table may not be full
          last_pdp_table_max = ( (max_ram + ((1 << 30) - 1)) & (~0ULL << 30) ) >> 30; // Catch any extra RAM into one more page

          if(last_pdp_table_max > 512) // Extreme case RAM size truncation
          {
            last_pdp_table_max = 512;
          }
        }
      }

      // Now we have everything we need to know how much space the page tables are going to consume
      uint64_t pml4_space = PAGE_TABLE_SIZE*max_pml5_entry;
      uint64_t pdp_space = pml4_space*max_pml4_entry;

      EFI_PHYSICAL_ADDRESS pml4_base_region = pagetable_alloc(pml4_space + pdp_space); // This zeroes out the area for us
      EFI_PHYSICAL_ADDRESS pdp_base_region = pml4_base_region + pml4_space; // The point is to put pdp_space right on top of pml4_space

      for(uint64_t pml5_entry = 0; pml5_entry < max_pml5_entry; pml5_entry++)
      {
        // Before adding any flags, set the 4k page-aligned address of pml4.
        outermost_table[pml5_entry] = pml4_base_region + (pml5_entry << 12);

        if(pml5_entry == (max_pml5_entry - 1))
        {
          max_pml4_entry = last_pml4_table_max;
        }

        for(uint64_t pml4_entry = 0; pml4_entry < max_pml4_entry; pml4_entry++)
        {
          // Before adding any flags, set the 4k page-aligned address of pdp.
          ((uint64_t*)outermost_table[pml5_entry])[pml4_entry] = pdp_base_region + (((pml5_entry << 9) + pml4_entry) << 12);

          if((pml5_entry == (max_pml5_entry - 1)) && (pml4_entry == (max_pml4_entry - 1))) // Check for special case of last pml4 entry, and if the last entry is a full table this'll account for that, too.
          {
            max_pdp_entry = last_pdp_table_max; // This correction only applies to the very last PDP
          }

          for(uint64_t pdp_entry = 0; pdp_entry < max_pdp_entry; pdp_entry++)
          {
            // pdp defines up to 512x 1GB entries. Only systems with more than 512GB RAM will have multiple PML4 entries. Systems with more than 256TB RAM wil have multiple PML5 entries.
            ((uint64_t*)((uint64_t*)outermost_table[pml5_entry])[pml4_entry])[pdp_entry] = (((pml5_entry << 18) + (pml4_entry << 9) + pdp_entry) << 30) | (0x83); // Flags: NX[63] = 0, PAT[12] = 0, G[8] = 0, 1GB[7] = 1, D[6] = 0, A[5] = 0, PCD[4] = 0, PWT[3] = 0, U/S[2] = 0, R/W[1] = 1, P[0] = 1.
          } // PDP

          ((uint64_t*)outermost_table[pml5_entry])[pml4_entry] |= 0x3; // Now set outer flags: NX[63] = 0, A[5] = 0, PCD[4] = 0, PWT[3] = 0, U/S[2] = 0, R/W[1] = 1, P[0] = 1.
        } // PML4

        outermost_table[pml5_entry] |= 0x3; // Now set outer flags: NX[63] = 0, A[5] = 0, PCD[4] = 0, PWT[3] = 0, U/S[2] = 0, R/W[1] = 1, P[0] = 1.
      } // PML5

    }
    else
    {
      // 4-level paging, 2 tables needed for 1GB pages
      printf("4-level paging is active.\r\n");

      if(max_ram >= (1ULL << 48)) // 256TB is the max with 4-level paging; to get the full 4PB (1 << 52) supported by the AMD64 4-level paging spec would require 16GB pages that don't exist (except on IBM POWER5+ mainframes)...
      {
        warning_printf("Hey! There's way too much RAM here and 5-level paging isn't enabled/supported.\r\nPlease contact your system vendor about this as it is a UEFI firmware issue.\r\nRAM will be limited to 256TB, the max allowed by 4-level paging.\r\n");
      }

      uint64_t max_pml4_entry = 1; // Always at least 1 entry

      uint64_t last_pdp_table_max = 512; // This will decrease to the correct size, but worst-case it will be 512, e.g. exactly 512GB RAM
      uint64_t max_pdp_entry = 512;

      // Each PML4 entry (which points to a full PDP table) can account for 512GB RAM
      while(max_ram > (512ULL << 30))
      {
        max_pml4_entry++;
        max_ram -= (512ULL << 30);
      }
      // Any leftover RAM is accounted for in PML4 by initializing max_pml4_entry to 1.

      if(max_pml4_entry > 512)
      {
        max_pml4_entry = 512; // If for whatever reason it comes to this some day
      }

      if(max_ram)
      {
        // The last PDP table won't be full
        last_pdp_table_max = ( (max_ram + ((1 << 30) - 1)) & (~0ULL << 30) ) >> 30; // Catch any extra RAM into one more page

        if(last_pdp_table_max > 512) // Extreme case (more than 256TB) RAM size truncation, else tables will overflow
        {
          last_pdp_table_max = 512;
        }
      }

      // Now we have everything we need to know how much space the page tables are going to consume
      uint64_t pdp_space = PAGE_TABLE_SIZE*max_pml4_entry;
      EFI_PHYSICAL_ADDRESS pdp_base_region = pagetable_alloc(pdp_space); // This zeroes out the area for us

//      printf("pdp base region: %#qx\r\n", pdp_base_region);

      for(uint64_t pml4_entry = 0; pml4_entry < max_pml4_entry; pml4_entry++)
      {
        // Before adding any flags, allocate 4k page-aligned pdp belonging to this entry.
        // This ensures we don't use up excess memory like in a statically allocated set of paging structures.
        outermost_table[pml4_entry] = pdp_base_region + (pml4_entry << 12);

        if(pml4_entry == (max_pml4_entry - 1)) // Check for special case of last pml4 entry, and if the last entry is a full table this'll account for that, too.
        {
          max_pdp_entry = last_pdp_table_max;
        }

        for(uint64_t pdp_entry = 0; pdp_entry < max_pdp_entry; pdp_entry++)
        {
          // pdp defines up to 512x 1GB entries. Only systems with more than 512GB RAM will have multiple PML4 entries.
          ((uint64_t*)outermost_table[pml4_entry])[pdp_entry] = (((pml4_entry << 9) + pdp_entry) << 30) | (0x83); // Flags: NX[63] = 0, PAT[12] = 0, G[8] = 0, 1GB[7] = 1, D[6] = 0, A[5] = 0, PCD[4] = 0, PWT[3] = 0, U/S[2] = 0, R/W[1] = 1, P[0] = 1.
        } // PDP

        outermost_table[pml4_entry] |= 0x3; // Now set outer flags: NX[63] = 0, A[5] = 0, PCD[4] = 0, PWT[3] = 0, U/S[2] = 0, R/W[1] = 1, P[0] = 1.
      } // PML4
    }
  }
  else
  {
    // Use 2MB pages, need 3 tables, max 256TB RAM

    info_printf("1GB pages are not supported, falling back to 2MB for the page tables instead. Certain system functions will still act like 1GB pages are used, however.\r\n");

    if(max_ram >= (1ULL << 48)) // 256TB is the max with 4-level pages
    {
      warning_printf("Hey! There's way too much RAM here and 5-level paging isn't supported.\r\nRAM will be limited to 256TB, the max allowed by 4-level paging with 2MB pages.\r\n");
      warning_printf("In the event someone actually manages to trigger this error, please be aware that this situation means the paging tables alone will consume 1GB of RAM.\r\n");
    }

    uint64_t max_pml4_entry = 1; // Always at least 1 entry

    uint64_t last_pdp_table_max = 1; // This will decrease to the correct size, but worst-case it will be 512, e.g. exactly 512GB RAM
    uint64_t max_pdp_entry = 512;

    // This is actually a fixed quantity in this case. All PDs will be full due to requiring whole number of GB for compatiblity with newer systems using 1GB pages.
    uint64_t max_pd_entry = 512;

    // Each PML4 entry (which points to a full PDP table) can account for 512GB RAM
    while(max_ram > (512ULL << 30))
    {
      max_pml4_entry++;
      max_ram -= (512ULL << 30);
    }
    // Any leftover RAM is accounted for in PML4 by initializing max_pml4_entry to 1.

    if(max_pml4_entry > 512)
    {
      max_pml4_entry = 512; // If for whatever reason it comes to this some day
    }

    if(max_ram)
    {
      // The last PDP table won't be full
      // But all PDs will be full since this is only allowing whole numbers of GB.
      last_pdp_table_max = ( (max_ram + ((1 << 30) - 1)) & (~0ULL << 30) ) >> 30; // Catch any extra RAM into one more page

      if(last_pdp_table_max > 512) // Extreme case (more than 256TB) RAM size truncation, else tables will overflow
      {
        last_pdp_table_max = 512;
      }
    }

    // Now we have everything we need to know how much space the page tables are going to consume
    uint64_t pdp_space = PAGE_TABLE_SIZE*max_pml4_entry;
    uint64_t pd_space = pdp_space*max_pdp_entry;

    EFI_PHYSICAL_ADDRESS pdp_base_region = pagetable_alloc(pdp_space + pd_space); // This zeroes out the area for us
    EFI_PHYSICAL_ADDRESS pd_base_region = pdp_base_region + pdp_space; // The point is to put pd_space right on top of pdp_space

    for(uint64_t pml4_entry = 0; pml4_entry < max_pml4_entry; pml4_entry++)
    {
      // Before adding any flags, allocate 4k page-aligned pdp belonging to this entry.
      // This ensures we don't use up excess memory like in a statically allocated set of paging structures.
      outermost_table[pml4_entry] = pdp_base_region + (pml4_entry << 12);

      if(pml4_entry == (max_pml4_entry - 1)) // Check for special case of last pml4 entry, and if the last entry is a full table this'll account for that, too.
      {
        max_pdp_entry = last_pdp_table_max;
      }

      for(uint64_t pdp_entry = 0; pdp_entry < max_pdp_entry; pdp_entry++)
      {
        // pdp defines up to 512x 1GB entries. Only systems with more than 512GB RAM will have multiple PML4 entries.
        ((uint64_t*)outermost_table[pml4_entry])[pdp_entry] = pd_base_region + (((pml4_entry << 9) + pdp_entry) << 12);

        for(uint64_t pd_entry = 0; pd_entry < max_pd_entry; pd_entry++)
        {
          ((uint64_t*)((uint64_t*)outermost_table[pml4_entry])[pdp_entry])[pd_entry] = (((pml4_entry << 18) + (pdp_entry << 9) + pd_entry) << 21) | (0x83); // Flags: NX[63] = 0, PAT[12] = 0, G[8] = 0, 2MB[7] = 1, D[6] = 0, A[5] = 0, PCD[4] = 0, PWT[3] = 0, U/S[2] = 0, R/W[1] = 1, P[0] = 1.
        }

        ((uint64_t*)outermost_table[pml4_entry])[pdp_entry] |= 0x3;

      } // PDP

      outermost_table[pml4_entry] |= 0x3; // Now set outer flags: NX[63] = 0, A[5] = 0, PCD[4] = 0, PWT[3] = 0, U/S[2] = 0, R/W[1] = 1, P[0] = 1.
    } // PML4
  }

  control_register_rw(3, (uint64_t)outermost_table, 1);
  // Certain hypervisors like Hyper-V will crash right here if less than 4GB is allocated to the VM. Actually, it appears to be 3968MB or less, as this 4GB number appears to include the entirety of physical address space (including PCI config space, etc.).
  // In Windows Event Viewer, Hyper-V-Worker will throw one of these errors:
  // "<VM Name> was faulted because the guest executed an intercepting instruction not supported by Hyper-V instruction emulation."
  // "<VM Name> was reset because an unrecoverable error occurred on a virtual processor that caused a triple fault."
  // My guess is that this might be somehow related to Windows giving all applications 4GB virtual address space, combined with the fact that this operation changes the paging tables of a type-1 hypervisor.
  // Or maybe Hyper-V has issues with using 1GB pages with < 4GB RAM? Or something to do with how Skylake has exactly 4 data TLBs for 1GB pages and "mov %cr3" causes TLB invalidation?

  // Enable CR4.PGE
  cr4 = control_register_rw(4, 0, 0);
  if(!(cr4 & (1 << 7))) // If off, turn on, else ignore
  {
    uint64_t cr4_2 = cr4 ^ (1 << 7); // Bit toggle
    control_register_rw(4, cr4_2, 1);
    cr4_2 = control_register_rw(4, 0, 0);
    if(cr4_2 == cr4)
    {
      warning_printf("Error setting CR4.PGE bit.\r\n");
    }
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// Get_Brandstring: Read CPU Brand String
//----------------------------------------------------------------------------------------------------------------------------------
//
// Get the 48-byte system brandstring (something like "Intel(R) Core(TM) i7-7700HQ CPU @ 2.80GHz")
//
// "brandstring" must be a 48-byte array. Returns ~0ULL address as a pointer if brand string is not supported.
//

char * Get_Brandstring(uint32_t * brandstring)
{
  uint64_t rax_value = 0x80000000;
  uint64_t rax = 0, rbx = 0, rcx = 0, rdx = 0;

  asm volatile("cpuid"
               : "=a" (rax), "=b" (rbx), "=c" (rcx), "=d" (rdx) // Outputs
               : "a" (rax_value) // The value to put into %rax
               : // CPUID would clobber any of the abcd registers not listed explicitly (all are here, though)
            );
  if(rax >= 0x80000004)
  {
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

     // Brandstrings are [supposed to be] null-terminated.

    return (char*)brandstring;
  }
  else
  {
    error_printf("Brand string not supported\r\n");
    return (char*)~0ULL;
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// Get_Manufacturer_ID: Read CPU Manufacturer ID
//----------------------------------------------------------------------------------------------------------------------------------
//
// Get CPU manufacturer identifier (something like "GenuineIntel" or "AuthenticAMD")
// Useful to verify CPU authenticity, supposedly
//
// "Manufacturer_ID" must be a 13-byte array
//

char * Get_Manufacturer_ID(char * Manufacturer_ID)
{
  uint64_t rbx = 0, rcx = 0, rdx = 0;

  asm volatile("cpuid"
               : "=b" (rbx), "=c" (rcx), "=d" (rdx) // Outputs
               : "a" (0x00) // The value to put into %rax
               : // CPUID would clobber any of the abcd registers not listed explicitly
             );

  Manufacturer_ID[0] = ((char *)&rbx)[0];
  Manufacturer_ID[1] = ((char *)&rbx)[1];
  Manufacturer_ID[2] = ((char *)&rbx)[2];
  Manufacturer_ID[3] = ((char *)&rbx)[3];

  Manufacturer_ID[4] = ((char *)&rdx)[0];
  Manufacturer_ID[5] = ((char *)&rdx)[1];
  Manufacturer_ID[6] = ((char *)&rdx)[2];
  Manufacturer_ID[7] = ((char *)&rdx)[3];

  Manufacturer_ID[8] = ((char *)&rcx)[0];
  Manufacturer_ID[9] = ((char *)&rcx)[1];
  Manufacturer_ID[10] = ((char *)&rcx)[2];
  Manufacturer_ID[11] = ((char *)&rcx)[3];

  Manufacturer_ID[12] = '\0';

//  printf("%c%c%c%c%c%c%c%c%c%c%c%c\r\n",
//  ((char *)&rbx)[0], ((char *)&rbx)[1], ((char *)&rbx)[2], ((char *)&rbx)[3],
//  ((char *)&rdx)[0], ((char *)&rdx)[1], ((char *)&rdx)[2], ((char *)&rdx)[3],
//  ((char *)&rcx)[0], ((char *)&rcx)[1], ((char *)&rcx)[2], ((char *)&rcx)[3]);

  return Manufacturer_ID;
}

//----------------------------------------------------------------------------------------------------------------------------------
// cpu_features: Read CPUID
//----------------------------------------------------------------------------------------------------------------------------------
//
// Query CPUID with the specified RAX and RCX (formerly EAX and ECX on 32-bit)
// Contains some feature checks already.
//
// rax_value: value to put into %rax before calling CPUID (leaf number)
// rcx_value: value to put into %rcx before calling CPUID (subleaf number, if applicable. Set it to 0 if there are no subleaves for the given rax_value)
//

void cpu_features(uint64_t rax_value, uint64_t rcx_value)
{
  // x86 does not memory-map control registers, unlike, for example, STM32 chips
  // System control registers like CR0, CR1, CR2, CR3, CR4, CR8, and EFLAGS
  // are accessed via special asm instructions.
  printf("CPUID input rax: %#qx, rcx: %#qx\r\n\n", rax_value, rcx_value);

  uint64_t rax = 0, rbx = 0, rcx = 0, rdx = 0;

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
    if(rcx & (1 << 31)) // 1 means hypervisor (i.e. in a VM), 0 means no hypervisor
    {
      printf("You're in a hypervisor!\r\n");
    }

    if(rcx & (1 << 12))
    {
      printf("FMA supported.\r\n");
    }
    else
    {
      printf("FMA not supported.\r\n");
    }

    if(rcx & (1 << 1))
    {
      if(rcx & (1 << 25))
      {
        printf("AESNI + PCLMULQDQ supported.\r\n");
      }
      else
      {
        printf("PCLMULQDQ supported (but not AESNI).\r\n");
      }
    }

    if(rcx & (1 << 27))
    {
      printf("AVX: OSXSAVE = 1\r\n");
    }
    else
    {
      printf("AVX: OSXSAVE = 0\r\n");
    }

    if(rcx & (1 << 26))
    {
      printf("AVX: XSAVE supported.\r\n");
    }
    else
    {
      printf("AVX: XSAVE not supported.\r\n");
    }

    if(rcx & (1 << 28))
    {
      printf("AVX supported.\r\n");
    }
    else
    {
      printf("AVX not supported. Checking for latest SSE features:\r\n");
      if(rcx & (1 << 20))
      {
        printf("Up to SSE4.2 supported.\r\n");
      }
      else
      {
        if(rcx & (1 << 19))
        {
          printf("Up to SSE4.1 supported.\r\n");
        }
        else
        {
          if(rcx & (1 << 9))
          {
            printf("Up to SSSE3 supported.\r\n");
          }
          else
          {
            if(rcx & 1)
            {
              printf("Up to SSE3 supported.\r\n");
            }
            else
            {
              if(rdx & (1 << 26))
              {
                printf("Up to SSE2 supported.\r\n");
              }
              else
              {
                printf("This is one weird CPU to get this far. x86_64 mandates SSE2.\r\n");
              }
            }
          }
        }
      }
    }

    if(rcx & (1 << 29))
    {
      printf("F16C supported.\r\n");
    }
    if(rdx & (1 << 22))
    {
      printf("ACPI via MSR supported.\r\n");
    }
    else
    {
      printf("ACPI via MSR not supported.\r\n");
    }
    if(rdx & (1 << 24))
    {
      printf("FXSR supported.\r\n");
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
    if(rbx & (1 << 5))
    {
      printf("AVX2 supported.\r\n");
    }
    else
    {
      printf("AVX2 not supported.\r\n");
    }
    // AVX512 feature check if AVX512 supported
    if(rbx & (1 << 16))
    {
      printf("AVX512F supported.\r\n");
      printf("Checking other supported AVX512 features:\r\n");
      if(rbx & (1 << 17))
      {
        printf("AVX512DQ\r\n");
      }
      if(rbx & (1 << 21))
      {
        printf("AVX512_IFMA\r\n");
      }
      if(rbx & (1 << 26))
      {
        printf("AVX512PF\r\n");
      }
      if(rbx & (1 << 27))
      {
        printf("AVX512ER\r\n");
      }
      if(rbx & (1 << 28))
      {
        printf("AVX512CD\r\n");
      }
      if(rbx & (1 << 30))
      {
        printf("AVX512BW\r\n");
      }
      if(rbx & (1 << 31))
      {
        printf("AVX512VL\r\n");
      }
      if(rcx & (1 << 1))
      {
        printf("AVX512_VBMI\r\n");
      }
      if(rcx & (1 << 6))
      {
        printf("AVX512_VBMI2\r\n");
      }
      if(rcx & (1 << 11))
      {
        printf("AVX512VNNI\r\n");
      }
      if(rcx & (1 << 12))
      {
        printf("AVX512_BITALG\r\n");
      }
      if(rcx & (1 << 14))
      {
        printf("AVX512_VPOPCNTDQ\r\n");
      }
      if(rdx & (1 << 2))
      {
        printf("AVX512_4VNNIW\r\n");
      }
      if(rdx & (1 << 3))
      {
        printf("AVX512_4FMAPS\r\n");
      }
      printf("End of AVX512 feature check.\r\n");
    }
    else
    {
      printf("AVX512 not supported.\r\n");
    }
    // End AVX512 check
    if(rcx & (1 << 8))
    {
      printf("GFNI Supported\r\n");
    }
    if(rcx & (1 << 9))
    {
      printf("VAES Supported\r\n");
    }
    if(rcx & (1 << 10))
    {
      printf("VPCLMULQDQ Supported\r\n");
    }
    if(rcx & (1 << 27))
    {
      printf("MOVDIRI Supported\r\n");
    }
    if(rcx & (1 << 28))
    {
      printf("MOVDIR64B Supported\r\n");
    }
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
      uint32_t brandstring[12] = {0};

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

      // Brandstrings are [supposed to be] null-terminated.

      printf("Brand String: %.48s\r\n", (char *)brandstring);
    }
    else
    {
      printf("Brand string not supported\r\n");
    }
  }
  else if(rax_value == 0x80000001)
  {
    asm volatile("cpuid"
                 : "=a" (rax), "=b" (rbx), "=c" (rcx), "=d" (rdx) // Outputs
                 : "a" (rax_value) // The value to put into %rax
                 : // CPUID would clobber any of the abcd registers not listed explicitly (all are here, though)
               );

    printf("rax: %#qx\r\nrbx: %#qx\r\nrcx: %#qx\r\nrdx: %#qx\r\n", rax, rbx, rcx, rdx);
    if(rdx & (1 << 26))
    {
      printf("1 GB pages are available.\r\n");
    }
    else
    {
      printf("1 GB pages are not supported.\r\n");
    }
    if(rdx & (1 << 29))
    {
      printf("Long Mode supported. (*Phew*)\r\n");
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
}
// END cpu_features

//----------------------------------------------------------------------------------------------------------------------------------
// Interrupt Handlers: Handlers for System Interrupts and Exceptions
//----------------------------------------------------------------------------------------------------------------------------------
//
// Various interrupts and exception handlers are defined here
//
// Remember, the Intel Architecture Manual calls interrupts 0-31 "Exceptions" and 32-255 "Interrupts"...At least most of the time.
// This software calls interrupts without error codes "Interrupts" (labeled ISR) and with error codes "Exceptions" (labeled EXC)--this
// is what GCC does, as well. So, yes, some interrupts have a message that says "... Exception!" despite being handled in ISR code.
// Not much to be done about that.
//
// Reminder: NMI, Double Fault, Machine Check, and Breakpoint have their own stacks from the x86-64 IST mechanism as set in Setup_IDT().
//

// (1ULL << 13) is 8kB
#define XSAVE_SIZE (1ULL << 13)

__attribute__((aligned(64))) static volatile unsigned char cpu_xsave_space[XSAVE_SIZE] = {0}; // Generic space for unhandled/unknown IDT vectors in the 0-31 range.
__attribute__((aligned(64))) static volatile unsigned char user_xsave_space[XSAVE_SIZE] = {0}; // For vectors 32-255, which can't preempt each other due to interrupt gating (IF in RFLAGS is cleared during ISR execution)
//
// Keeping separate xsave areas since the "user" handlers can only fire one at a time.
// The "cpu" handlers are not maskable, and a user handler could trigger one of them. Separating the xsave areas prevents xsave area corruption in this case.
// However, certain cpu handlers can preempt themselves (e.g. the page fault handler can possibly trigger a page fault) and certain CPU exceptions/interrupts can
// arise from other CPU exceptions/interrupts (NMIs are random and they can also page fault, NMIs can interrupt any interrupt...), and that needs to be handled.
//
// Having a dedicated XSAVE area for each non-maskable type of interrupt solves most of these issues (e.g. NMI interrupting anything else). Also, NMIs cannot preempt
// active NMIs until IRET(Q) has been called, and NMIs have the highest priority short of breakpoint and machine check, though a page fault in an NMI can call IRET(Q)
// and ruin everything. A GPF getting called in an NMI is pretty much game over, since it means the NMI handler is screwy, so realistically the only crazy case to
// have to worry about is an NMI handler triggering a page fault, since almost anything else indicates a problem with the NMI handler. Returning from a breakpoint
// nested in an NMI handler is the other case that could mess up the NMI procedure through IRET(Q).
//
// The solutions to this include doing something like what the Linux kernel does (see here: https://lwn.net/Articles/484932/), or write a good NMI handler that
// can't/doesn't page fault and don't put breakpoints in the NMI handler. In all honesty, kernel code shouldn't ever page fault--predominantly in OSes kernels deal with
// userspace programs page faulting, but kernelspace code doesn't page fault itself. This software does not recognize any concept of userspace, as it does not use
// privilege rings, so really no handlers should ever trigger any fault. One can also safely put a breakpoint in the NMI hander if one has a means to do so and is OK
// with restarting the whole system in question when done analyzing the breakpoint instead of returning to the now-unsafe handler (it might work fine, but it'll be at
// risk of a race condition where the NMI handler will need to finish up before another NMI gets triggered--the same problem the page fault handler would cause).
//
// The issue of having stack corruption due to an NMI preempting itself technically also applies to double faults, machine checks, and breakpoints preempting themselves,
// since they all use the x86-64 IST stack-switching mechanism, but if that somehow happens then something is very wrong with the code (DF), the hardware (DF, MC) or the
// debugger (BP).
//
// Quick extra note: In embedded systems programming, interrupts are never supposed to do anything complicated; they're just supposed to set flags and terminate.
// Abiding by this practice in general prevents all kinds of shenanigans from occuring during the execution of an interrupt handler. In complex hardware environments
// like multiple cores and multiple processors, the fewer shenanigans in interrupts the better. x86 in general appears to have attempted to destroy that simplicity, as
// the page fault handlers in any of the major OSes (Windows, Mac, Linux) demonstrate with their paging/swap file mechanisms.
//
__attribute__((aligned(64))) static volatile unsigned char de_xsave_space[XSAVE_SIZE] = {0}; // #DE
__attribute__((aligned(64))) static volatile unsigned char db_xsave_space[XSAVE_SIZE] = {0}; // #DB
__attribute__((aligned(64))) static volatile unsigned char nmi_xsave_space[XSAVE_SIZE] = {0}; // NMI
__attribute__((aligned(64))) static volatile unsigned char bp_xsave_space[XSAVE_SIZE] = {0}; // #BP
__attribute__((aligned(64))) static volatile unsigned char of_xsave_space[XSAVE_SIZE] = {0}; // #OF
__attribute__((aligned(64))) static volatile unsigned char br_xsave_space[XSAVE_SIZE] = {0}; // #BR
__attribute__((aligned(64))) static volatile unsigned char ud_xsave_space[XSAVE_SIZE] = {0}; // #UD
__attribute__((aligned(64))) static volatile unsigned char nm_xsave_space[XSAVE_SIZE] = {0}; // #NM
__attribute__((aligned(64))) static volatile unsigned char df_xsave_space[XSAVE_SIZE] = {0}; // #DF
__attribute__((aligned(64))) static volatile unsigned char cso_xsave_space[XSAVE_SIZE] = {0}; // Coprocessor Segment Overrun
__attribute__((aligned(64))) static volatile unsigned char ts_xsave_space[XSAVE_SIZE] = {0}; // #TS
__attribute__((aligned(64))) static volatile unsigned char np_xsave_space[XSAVE_SIZE] = {0}; // #NP
__attribute__((aligned(64))) static volatile unsigned char ss_xsave_space[XSAVE_SIZE] = {0}; // #SS
__attribute__((aligned(64))) static volatile unsigned char gp_xsave_space[XSAVE_SIZE] = {0}; // #GP
__attribute__((aligned(64))) static volatile unsigned char pf_xsave_space[XSAVE_SIZE] = {0}; // #PF
// 15 is reserved, so it gets the generic cpu_xsave_space
__attribute__((aligned(64))) static volatile unsigned char mf_xsave_space[XSAVE_SIZE] = {0}; // #MF
__attribute__((aligned(64))) static volatile unsigned char ac_xsave_space[XSAVE_SIZE] = {0}; // #AC
__attribute__((aligned(64))) static volatile unsigned char mc_xsave_space[XSAVE_SIZE] = {0}; // #MC
__attribute__((aligned(64))) static volatile unsigned char xm_xsave_space[XSAVE_SIZE] = {0}; // #XM
__attribute__((aligned(64))) static volatile unsigned char ve_xsave_space[XSAVE_SIZE] = {0}; // #VE
// 21-29 are reserved, so they get the generic cpu_xsave_space
__attribute__((aligned(64))) static volatile unsigned char sx_xsave_space[XSAVE_SIZE] = {0}; // #SX
// 31 is reserved, so it gets the generic cpu_xsave_space
// Yes, true, some of these are not possible to trigger naturally these days, but they're all here for completeness.

// A more dynamic way to do this is to initialize something like 21 or so reserved XSAVE areas with malloc (or more like xsave_alloc_pages), like this:
/*
  uint64_t xsave_area_size = 0;
  // Leaf 0Dh, in %rbx of %rcx=0, since %rbx of sub-leaf %rcx=1 is undefined on AMD (so no supervisor component support here)
  asm volatile ("cpuid"
                : "=b" (xsave_area_size) // Outputs
                : "a" (0x0D), "c" (0x00) // Inputs
                : "%rdx" // Clobbers
              );

  printf("xsave_area_size: %#qx\r\n", xsave_area_size);
  #define NUM_CPU_ERRS 21
  uint64_t required_xsave_pages = EFI_SIZE_TO_PAGES(xsave_area_size) * NUM_CPU_ERRS; // xsave area must be 64-byte aligned.
  unsigned char * major_xsave_space = xsave_alloc_pages(required_xsave_pages); // NUM_CPU_ERRS areas, xsave_alloc_pages already zeroes the space
  // Then each --_xsave_space gets assigned an offset into the XSAVE area, and each chunk is 'xsave_area_size' in size.
*/
// The problem with this malloc-style method is that getting the output asm correct so that [m] in "xsave %[m]" comes out right is much more cumbersome.
// The implemented static way merely needs to have the STACK_SIZE increased if something like AVX-1024 ever happens (and the 0xE7 masks will need to be updated,
// but that's true no matter what).


//
// User-Defined Interrupts (no error code)
//

// Remember: Interrupts are not really supposed to do anything too fancy.
// In particular, they really should not be doing anything that could cause an internal CPU exception to occur.

void User_ISR_handler(INTERRUPT_FRAME * i_frame)
{
  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xsave64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (user_xsave_space) // Inputs
                : "memory" // Clobbers
              );

  // OK, since xsave has been called we can now safely use AVX instructions in this interrupt--up until xrstor is called, at any rate.
  // Using an interrupt gate in the IDT means we won't get preempted now by other user ISRs, either, which would wreck the xsave area.

  // First check: Was this called by ACPI?
  // ACPI gets priority over whatever interrupt it decides to claim, until it stops using that interrupt, in which case the interrupt
  // goes back to whatever it was being used for before ACPI claimed it. Basically this grants ACPI a temporary override of whatever
  // interrupt it wants.
  if(Global_ACPI_Interrupt_Table[i_frame->isr_num].InterruptNumber) // This will be 0 if ACPI isn't using it
  { // Yes, so this is why we're here
    Global_ACPI_Interrupt_Table[i_frame->isr_num].HandlerPointer(Global_ACPI_Interrupt_Table[i_frame->isr_num].Context);
  }
  else // No, there's some other interrupt
  {
    switch(i_frame->isr_num)
    {

      //
      // User-Defined Interrupts (32-255)
      //

      // 39 & 47 are spurious vectors from dual-PIC chips (IR(Q)7 and IR(Q)15), if such chips exist in a system. 
      case 39:
        break;
      case 47:
        break;
  //    case 32: // Minimum allowed user-defined case number
  //    // Case 32 code
  //      break;
  //    ....
  //    case 255: // Maximum allowed case number
  //    // Case 255 code
  //      break;

      //
      // End of User-Defined Interrupts
      //

      default:
        error_printf("User_ISR_handler: Unhandled Interrupt! IDT Entry: %#qu\r\n", i_frame->isr_num);
        ISR_regdump(i_frame);
        AVX_regdump((XSAVE_AREA_LAYOUT*)user_xsave_space);
        asm volatile("hlt");
        break;
    }
  }
  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xrstor64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (user_xsave_space) // Inputs
                : "memory" // Clobbers
              );
}

//
// CPU Interrupts (no error code)
//

void CPU_ISR_handler(INTERRUPT_FRAME * i_frame)
{
  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xsave64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (cpu_xsave_space) // Inputs
                : "memory" // Clobbers
              );

  // OK, since xsave has been called we can now safely use AVX instructions in this interrupt--up until xrstor is called, at any rate.
  // Using an interrupt gate in the IDT means we won't get preempted now, either, which would wreck the xsave area.

  error_printf("CPU_ISR_handler: Unhandled Interrupt! IDT Entry: %#qu\r\n", i_frame->isr_num);
  ISR_regdump(i_frame);
  AVX_regdump((XSAVE_AREA_LAYOUT*)cpu_xsave_space);
  asm volatile("hlt");

  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xrstor64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (cpu_xsave_space) // Inputs
                : "memory" // Clobbers
              );
}

//
// CPU Exceptions (have error code)
//

void CPU_EXC_handler(EXCEPTION_FRAME * e_frame)
{
  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xsave64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (cpu_xsave_space) // Inputs
                : "memory" // Clobbers
              );

  // OK, since xsave has been called we can now safely use AVX instructions in this interrupt--up until xrstor is called, at any rate.
  // Using an interrupt gate in the IDT means we won't get preempted now, either, which would wreck the xsave area.

  error_printf("CPU_EXC_handler: Unhandled Exception! IDT Entry: %#qu, Error Code: %#qx\r\n", e_frame->isr_num, e_frame->error_code);
  EXC_regdump(e_frame);
  AVX_regdump((XSAVE_AREA_LAYOUT*)cpu_xsave_space);
  asm volatile("hlt");

  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xrstor64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (cpu_xsave_space) // Inputs
                : "memory" // Clobbers
              );
}

//
// CPU Special Handlers
//

// Vector 0
void DE_ISR_handler(INTERRUPT_FRAME * i_frame) // Fault #DE: Divide Error (divide by 0 or not enough bits in destination)
{
  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xsave64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (de_xsave_space) // Inputs
                : "memory" // Clobbers
              );

  error_printf("Fault #DE: Divide Error! IDT Entry: %#qu\r\n", i_frame->isr_num);
  ISR_regdump(i_frame);
  asm volatile("hlt");

  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xrstor64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (de_xsave_space) // Inputs
                : "memory" // Clobbers
              );
}

// Vector 1
void DB_ISR_handler(INTERRUPT_FRAME * i_frame) // Fault/Trap #DB: Debug Exception
{
  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xsave64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (db_xsave_space) // Inputs
                : "memory" // Clobbers
              );

  error_printf("Fault/Trap #DB: Debug Exception! IDT Entry: %#qu\r\n", i_frame->isr_num);
  ISR_regdump(i_frame);
  asm volatile("hlt");

  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xrstor64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (db_xsave_space) // Inputs
                : "memory" // Clobbers
              );
}

// Vector 2
void NMI_ISR_handler(INTERRUPT_FRAME * i_frame) // NMI (Nonmaskable External Interrupt)
{
  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xsave64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (nmi_xsave_space) // Inputs
                : "memory" // Clobbers
              );

  error_printf("NMI: Nonmaskable Interrupt! IDT Entry: %#qu\r\n", i_frame->isr_num);
  ISR_regdump(i_frame);
  asm volatile("hlt");

  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xrstor64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (nmi_xsave_space) // Inputs
                : "memory" // Clobbers
              );
}

// Vector 3
void BP_ISR_handler(INTERRUPT_FRAME * i_frame) // Trap #BP: Breakpoint (INT3 instruction)
{
  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xsave64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (bp_xsave_space) // Inputs
                : "memory" // Clobbers
              );

  error_printf("Trap #BP: Breakpoint! IDT Entry: %#qu\r\n", i_frame->isr_num);
  ISR_regdump(i_frame);
  asm volatile("hlt");

  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xrstor64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (bp_xsave_space) // Inputs
                : "memory" // Clobbers
              );
}

// Vector 4
void OF_ISR_handler(INTERRUPT_FRAME * i_frame) // Trap #OF: Overflow (INTO instruction)
{
  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xsave64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (of_xsave_space) // Inputs
                : "memory" // Clobbers
              );

  error_printf("Trap #OF: Overflow! IDT Entry: %#qu\r\n", i_frame->isr_num);
  ISR_regdump(i_frame);
  asm volatile("hlt");

  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xrstor64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (of_xsave_space) // Inputs
                : "memory" // Clobbers
              );
}

// Vector 5
void BR_ISR_handler(INTERRUPT_FRAME * i_frame) // Fault #BR: BOUND Range Exceeded (BOUND instruction)
{
  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xsave64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (br_xsave_space) // Inputs
                : "memory" // Clobbers
              );

  error_printf("Fault #BR: BOUND Range Exceeded! IDT Entry: %#qu\r\n", i_frame->isr_num);
  ISR_regdump(i_frame);
  asm volatile("hlt");

  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xrstor64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (br_xsave_space) // Inputs
                : "memory" // Clobbers
              );
}

// Vector 6
void UD_ISR_handler(INTERRUPT_FRAME * i_frame) // Fault #UD: Invalid or Undefined Opcode
{
  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xsave64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (ud_xsave_space) // Inputs
                : "memory" // Clobbers
              );

  error_printf("Fault #UD: Invalid or Undefined Opcode! IDT Entry: %#qu\r\n", i_frame->isr_num);
  ISR_regdump(i_frame);
  asm volatile("hlt");

  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xrstor64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (ud_xsave_space) // Inputs
                : "memory" // Clobbers
              );
}

// Vector 7
void NM_ISR_handler(INTERRUPT_FRAME * i_frame) // Fault #NM: Device Not Available Exception
{
  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xsave64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (nm_xsave_space) // Inputs
                : "memory" // Clobbers
              );

  error_printf("Fault #NM: Device Not Available Exception! IDT Entry: %#qu\r\n", i_frame->isr_num);
  ISR_regdump(i_frame);
  asm volatile("hlt");

  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xrstor64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (nm_xsave_space) // Inputs
                : "memory" // Clobbers
              );
}

// Vector 8
void DF_EXC_handler(EXCEPTION_FRAME * e_frame) // Abort #DF: Double Fault (error code is always 0)
{
  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xsave64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (df_xsave_space) // Inputs
                : "memory" // Clobbers
              );

  error_printf("Abort #DF: Double Fault! IDT Entry: %#qu, Error Code (always 0): %#qx\r\n", e_frame->isr_num, e_frame->error_code);
  EXC_regdump(e_frame);
  AVX_regdump((XSAVE_AREA_LAYOUT*)df_xsave_space);
  while(1) // #DF is an end of the line if this is a single application and not a full-blown OS that runs programs
  {
    asm volatile("hlt");
  }

  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xrstor64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (df_xsave_space) // Inputs
                : "memory" // Clobbers
              );
}

// Vector 9
void CSO_ISR_handler(INTERRUPT_FRAME * i_frame) // Fault (i386): Coprocessor Segment Overrun (long since obsolete, included for completeness)
{
  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xsave64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (cso_xsave_space) // Inputs
                : "memory" // Clobbers
              );

  error_printf("Fault (i386): Coprocessor Segment Overrun! IDT Entry: %#qu\r\n", i_frame->isr_num);
  ISR_regdump(i_frame);
  while(1) // Turns out this is actually an abort!
  {
    asm volatile("hlt");
  }

  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xrstor64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (cso_xsave_space) // Inputs
                : "memory" // Clobbers
              );
}

// Vector 10
void TS_EXC_handler(EXCEPTION_FRAME * e_frame) // Fault #TS: Invalid TSS
{
  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xsave64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (ts_xsave_space) // Inputs
                : "memory" // Clobbers
              );

  error_printf("Fault #TS: Invalid TSS! IDT Entry: %#qu, Error Code: %#qx\r\n", e_frame->isr_num, e_frame->error_code);
  EXC_regdump(e_frame);
  asm volatile("hlt");

  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xrstor64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (ts_xsave_space) // Inputs
                : "memory" // Clobbers
              );
}

// Vector 11
void NP_EXC_handler(EXCEPTION_FRAME * e_frame) // Fault #NP: Segment Not Present
{
  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xsave64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (np_xsave_space) // Inputs
                : "memory" // Clobbers
              );

  error_printf("Fault #NP: Segment Not Present! IDT Entry: %#qu, Error Code: %#qx\r\n", e_frame->isr_num, e_frame->error_code);
  EXC_regdump(e_frame);
  asm volatile("hlt");

  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xrstor64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (np_xsave_space) // Inputs
                : "memory" // Clobbers
              );
}

// Vector 12
void SS_EXC_handler(EXCEPTION_FRAME * e_frame) // Fault #SS: Stack Segment Fault
{
  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xsave64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (ss_xsave_space) // Inputs
                : "memory" // Clobbers
              );

  error_printf("Fault #SS: Stack Segment Fault! IDT Entry: %#qu, Error Code: %#qx\r\n", e_frame->isr_num, e_frame->error_code);
  EXC_regdump(e_frame);
  asm volatile("hlt");

  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xrstor64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (ss_xsave_space) // Inputs
                : "memory" // Clobbers
              );
}

// Vector 13
void GP_EXC_handler(EXCEPTION_FRAME * e_frame) // Fault #GP: General Protection
{
  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xsave64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (gp_xsave_space) // Inputs
                : "memory" // Clobbers
              );

  error_printf("Fault #GP: General Protection! IDT Entry: %#qu, Error Code: %#qx\r\n", e_frame->isr_num, e_frame->error_code);
  // Some of these can actually be corrected. Not always the end of the world.
  // This is just a generic template example.
  switch(e_frame->error_code)
  {
    default:
      EXC_regdump(e_frame);
      AVX_regdump((XSAVE_AREA_LAYOUT*)gp_xsave_space);
      print_system_memmap();
      while(1)
      {
        asm volatile("hlt");
      }
      break;
  }

  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xrstor64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (gp_xsave_space) // Inputs
                : "memory" // Clobbers
              );
}

// Vector 14
void PF_EXC_handler(EXCEPTION_FRAME * e_frame) // Fault #PF: Page Fault
{
  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xsave64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (pf_xsave_space) // Inputs
                : "memory" // Clobbers
              );

  uint64_t cr2 = control_register_rw(2, 0, 0); // CR2 has the page fault linear address
  uint64_t cr3 = control_register_rw(3, 0, 0); // CR3 has the page directory base (bottom 12 bits of address are assumed 0)
  info_printf("Fault #PF: Page Fault! IDT Entry: %#qu, Error Code: %#qx\r\n", e_frame->isr_num, e_frame->error_code);
  printf("CR2: %#qx\r\n", cr2);
  printf("CR3: %#qx\r\n", cr3);
  // This is just a generic template example; page faults are usually not the end of the world. That's why it gets an info_printf
  switch(e_frame->error_code)
  {
    default:
      EXC_regdump(e_frame);
      while(1)
      {
        asm volatile("hlt");
      }
      break;
  }

  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xrstor64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (pf_xsave_space) // Inputs
                : "memory" // Clobbers
              );
}

// Vector 15 is reserved

// Vector 16
void MF_ISR_handler(INTERRUPT_FRAME * i_frame) // Fault #MF: Math Error (x87 FPU Floating-Point Math Error)
{
  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xsave64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (mf_xsave_space) // Inputs
                : "memory" // Clobbers
              );

  error_printf("Fault #MF: x87 Math Error! IDT Entry: %#qu\r\n", i_frame->isr_num);
  ISR_regdump(i_frame);
  AVX_regdump((XSAVE_AREA_LAYOUT *)mf_xsave_space);
  asm volatile("hlt");

  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xrstor64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (mf_xsave_space) // Inputs
                : "memory" // Clobbers
              );
}

// Vector 17
void AC_EXC_handler(EXCEPTION_FRAME * e_frame) // Fault #AC: Alignment Check (error code is always 0)
{
  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xsave64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (ac_xsave_space) // Inputs
                : "memory" // Clobbers
              );

  error_printf("Fault #AC: Alignment Check! IDT Entry: %#qu, Error Code (usually 0): %#qx\r\n", e_frame->isr_num, e_frame->error_code);
  EXC_regdump(e_frame);
  asm volatile("hlt");

  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xrstor64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (ac_xsave_space) // Inputs
                : "memory" // Clobbers
              );
}

// Vector 18
void MC_ISR_handler(INTERRUPT_FRAME * i_frame) // Abort #MC: Machine Check
{
  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xsave64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (mc_xsave_space) // Inputs
                : "memory" // Clobbers
              );

  error_printf("Abort #MC: Machine Check! IDT Entry: %#qu\r\n", i_frame->isr_num);
  ISR_regdump(i_frame);
  AVX_regdump((XSAVE_AREA_LAYOUT*)mc_xsave_space);
  while(1) // There's no escaping #MC
  {
    asm volatile("hlt");
  }

  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xrstor64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (mc_xsave_space) // Inputs
                : "memory" // Clobbers
              );
}

// Vector 19
void XM_ISR_handler(INTERRUPT_FRAME * i_frame) // Fault #XM: SIMD Floating-Point Exception (SSE instructions)
{
  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xsave64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (xm_xsave_space) // Inputs
                : "memory" // Clobbers
              );

  error_printf("Fault #XM: SIMD Floating-Point Exception! IDT Entry: %#qu\r\n", i_frame->isr_num);
  ISR_regdump(i_frame);
  AVX_regdump((XSAVE_AREA_LAYOUT*)xm_xsave_space);
  asm volatile("hlt");

  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xrstor64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (xm_xsave_space) // Inputs
                : "memory" // Clobbers
              );
}

// Vector 20
void VE_ISR_handler(INTERRUPT_FRAME * i_frame) // Fault #VE: Virtualization Exception
{
  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xsave64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (ve_xsave_space) // Inputs
                : "memory" // Clobbers
              );

  error_printf("Fault #VE: Virtualization Exception! IDT Entry: %#qu\r\n", i_frame->isr_num);
  ISR_regdump(i_frame);
  asm volatile("hlt");

  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xrstor64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (ve_xsave_space) // Inputs
                : "memory" // Clobbers
              );
}

// Vectors 21-29 are reserved

// Vector 30
void SX_EXC_handler(EXCEPTION_FRAME * e_frame) // Fault #SX: Security Exception
{
  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xsave64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (sx_xsave_space) // Inputs
                : "memory" // Clobbers
              );

  error_printf("Fault #SX: Security Exception! IDT Entry: %#qu, Error Code: %#qx\r\n", e_frame->isr_num, e_frame->error_code);
  EXC_regdump(e_frame);
  asm volatile("hlt");

  // %rdx: Mask for xcr0 [63:32], %rax: Mask for xcr0 [31:0]
  asm volatile ("xrstor64 %[area]"
                : // No outputs
                : "a" (0xE7), "d" (0x00), [area] "m" (sx_xsave_space) // Inputs
                : "memory" // Clobbers
              );
}

// Vector 31 is reserved


//----------------------------------------------------------------------------------------------------------------------------------
// Interrupt Support Functions: Helpers for Interrupt and Exception Handlers
//----------------------------------------------------------------------------------------------------------------------------------
//
// Various interrupt and exception support functions are defined here, e.g. register dumps.
// Functions here strictly correspond to their similarly-named interrupt handlers.
//
// *Do not mix them* as the ISR and EXC data structures are different, which would put values in the wrong places.
//

//
// Interrupts (no error code)
//

void ISR_regdump(INTERRUPT_FRAME * i_frame)
{
  printf("rax: %#qx, rbx: %#qx, rcx: %#qx, rdx: %#qx, rsi: %#qx, rdi: %#qx\r\n", i_frame->rax, i_frame->rbx, i_frame->rcx, i_frame->rdx, i_frame->rsi, i_frame->rdi);
  printf("r8: %#qx, r9: %#qx, r10: %#qx, r11: %#qx, r12: %#qx, r13: %#qx\r\n", i_frame->r8, i_frame->r9, i_frame->r10, i_frame->r11, i_frame->r12, i_frame->r13);
  printf("r14: %#qx, r15: %#qx, rbp: %#qx, rip: %#qx, cs: %#qx, rflags: %#qx\r\n", i_frame->r14, i_frame->r15, i_frame->rbp, i_frame->rip, i_frame->cs, i_frame->rflags);
  printf("rsp: %#qx, ss: %#qx\r\n", i_frame->rsp, i_frame->ss);
}

//
// Exceptions (have error code)
//

void EXC_regdump(EXCEPTION_FRAME * e_frame)
{
  printf("rax: %#qx, rbx: %#qx, rcx: %#qx, rdx: %#qx, rsi: %#qx, rdi: %#qx\r\n", e_frame->rax, e_frame->rbx, e_frame->rcx, e_frame->rdx, e_frame->rsi, e_frame->rdi);
  printf("r8: %#qx, r9: %#qx, r10: %#qx, r11: %#qx, r12: %#qx, r13: %#qx\r\n", e_frame->r8, e_frame->r9, e_frame->r10, e_frame->r11, e_frame->r12, e_frame->r13);
  printf("r14: %#qx, r15: %#qx, rbp: %#qx, rip: %#qx, cs: %#qx, rflags: %#qx\r\n", e_frame->r14, e_frame->r15, e_frame->rbp, e_frame->rip, e_frame->cs, e_frame->rflags);
  printf("rsp: %#qx, ss: %#qx\r\n", e_frame->rsp, e_frame->ss);
}

//
// AVX Dump
//

void AVX_regdump(XSAVE_AREA_LAYOUT * layout_area)
{
  printf("fow: %#hx, fsw: %#hx, ftw: %#hhx, fop: %#hx, fip: %#qx, fdp: %#qx\r\n", layout_area->fcw, layout_area->fsw, layout_area->ftw, layout_area->fop, layout_area->fip, layout_area->fdp);
  printf("mxcsr: %#qx, mxcsr_mask: %#qx, xstate_bv: %#qx, xcomp_bv: %#qx\r\n", layout_area->mxcsr, layout_area->mxcsr_mask, layout_area->xstate_bv, layout_area->xcomp_bv);

#ifdef __AVX512F__

  uint64_t avx512_opmask_offset = 0;
  // Leaf 0Dh, AVX512 opmask is user state component 5
  asm volatile ("cpuid"
                : "=b" (avx512_opmask_offset) // Outputs
                : "a" (0x0D), "c" (0x05) // Inputs
                : "%rdx" // Clobbers
              );

  uint64_t avx512_zmm_hi256_offset = 0;
  // Leaf 0Dh, AVX512 ZMM_Hi256 is user state component 6
  asm volatile ("cpuid"
                : "=b" (avx512_zmm_hi256_offset) // Outputs
                : "a" (0x0D), "c" (0x06) // Inputs
                : "%rdx" // Clobbers
              );

  uint64_t avx512_hi16_zmm_offset = 0;
  // Leaf 0Dh, AVX512 Hi16_ZMM is user state component 7
  asm volatile ("cpuid"
                : "=b" (avx512_hi16_zmm_offset) // Outputs
                : "a" (0x0D), "c" (0x07) // Inputs
                : "%rdx" // Clobbers
              );

  uint64_t avx_offset = 0;
  // Leaf 0Dh, AVX is user state component 2
  asm volatile ("cpuid"
                : "=b" (avx_offset) // Outputs
                : "a" (0x0D), "c" (0x02) // Inputs
                : "%rdx" // Clobbers
              );

  // Hope screen resolution is high enough to see all this...
  // ZMM_Hi256, AVX, XMM
  printf("ZMM0: 0x%016qx%016qx%016qx%016qx%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 24), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 16), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 8), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 0), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 8), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 0), layout_area->xmm0[1], layout_area->xmm0[0]);
  printf("ZMM1: 0x%016qx%016qx%016qx%016qx%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 56), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 48), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 40), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 32), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 24), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 16), layout_area->xmm1[1], layout_area->xmm1[0]);
  printf("ZMM2: 0x%016qx%016qx%016qx%016qx%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 88), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 80), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 72), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 64), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 40), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 32), layout_area->xmm2[1], layout_area->xmm2[0]);
  printf("ZMM3: 0x%016qx%016qx%016qx%016qx%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 120), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 112), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 104), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 96), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 56), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 48), layout_area->xmm3[1], layout_area->xmm3[0]);
  printf("ZMM4: 0x%016qx%016qx%016qx%016qx%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 152), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 144), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 136), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 128), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 72), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 64), layout_area->xmm4[1], layout_area->xmm4[0]);
  printf("ZMM5: 0x%016qx%016qx%016qx%016qx%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 184), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 176), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 168), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 160), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 88), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 80), layout_area->xmm5[1], layout_area->xmm5[0]);
  printf("ZMM6: 0x%016qx%016qx%016qx%016qx%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 216), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 208), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 200), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 192), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 104), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 96), layout_area->xmm6[1], layout_area->xmm6[0]);
  printf("ZMM7: 0x%016qx%016qx%016qx%016qx%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 248), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 240), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 232), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 224), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 120), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 112), layout_area->xmm7[1], layout_area->xmm7[0]);
  printf("ZMM8: 0x%016qx%016qx%016qx%016qx%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 280), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 272), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 264), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 256), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 136), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 128), layout_area->xmm8[1], layout_area->xmm8[0]);
  printf("ZMM9: 0x%016qx%016qx%016qx%016qx%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 312), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 304), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 296), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 288), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 152), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 144), layout_area->xmm9[1], layout_area->xmm9[0]);
  printf("ZMM10: 0x%016qx%016qx%016qx%016qx%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 344), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 336), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 328), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 320), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 168), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 160), layout_area->xmm10[1], layout_area->xmm10[0]);
  printf("ZMM11: 0x%016qx%016qx%016qx%016qx%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 376), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 368), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 360), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 352), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 184), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 176), layout_area->xmm11[1], layout_area->xmm11[0]);
  printf("ZMM12: 0x%016qx%016qx%016qx%016qx%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 408), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 400), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 392), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 384), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 200), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 192), layout_area->xmm12[1], layout_area->xmm12[0]);
  printf("ZMM13: 0x%016qx%016qx%016qx%016qx%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 440), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 432), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 424), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 416), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 216), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 208), layout_area->xmm13[1], layout_area->xmm13[0]);
  printf("ZMM14: 0x%016qx%016qx%016qx%016qx%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 472), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 464), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 456), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 448), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 232), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 224), layout_area->xmm14[1], layout_area->xmm14[0]);
  printf("ZMM15: 0x%016qx%016qx%016qx%016qx%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 504), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 496), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 488), *(uint64_t*) ((uint8_t*)layout_area + avx512_zmm_hi256_offset + 480), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 248), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 240), layout_area->xmm15[1], layout_area->xmm15[0]);
  // Hi16_ZMM
  printf("ZMM16: 0x%016qx%016qx%016qx%016qx%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 56), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 48), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 40), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 32), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 24), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 16), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 8), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 0));
  printf("ZMM17: 0x%016qx%016qx%016qx%016qx%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 120), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 112), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 104), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 96), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 88), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 80), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 72), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 64));
  printf("ZMM18: 0x%016qx%016qx%016qx%016qx%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 184), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 176), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 168), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 160), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 152), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 144), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 136), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 128));
  printf("ZMM19: 0x%016qx%016qx%016qx%016qx%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 248), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 240), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 232), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 224), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 216), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 208), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 200), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 192));
  printf("ZMM20: 0x%016qx%016qx%016qx%016qx%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 312), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 304), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 296), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 288), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 280), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 272), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 264), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 256));
  printf("ZMM21: 0x%016qx%016qx%016qx%016qx%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 376), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 368), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 360), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 352), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 344), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 336), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 328), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 320));
  printf("ZMM22: 0x%016qx%016qx%016qx%016qx%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 440), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 432), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 424), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 416), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 408), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 400), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 392), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 384));
  printf("ZMM23: 0x%016qx%016qx%016qx%016qx%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 504), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 496), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 488), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 480), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 472), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 464), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 456), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 448));
  printf("ZMM24: 0x%016qx%016qx%016qx%016qx%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 568), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 560), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 552), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 544), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 536), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 528), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 520), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 512));
  printf("ZMM25: 0x%016qx%016qx%016qx%016qx%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 632), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 624), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 616), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 608), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 600), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 592), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 584), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 576));
  printf("ZMM26: 0x%016qx%016qx%016qx%016qx%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 696), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 688), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 680), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 672), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 664), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 656), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 648), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 640));
  printf("ZMM27: 0x%016qx%016qx%016qx%016qx%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 760), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 752), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 744), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 736), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 728), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 720), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 712), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 704));
  printf("ZMM28: 0x%016qx%016qx%016qx%016qx%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 824), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 816), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 808), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 800), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 792), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 784), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 776), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 768));
  printf("ZMM29: 0x%016qx%016qx%016qx%016qx%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 888), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 880), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 872), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 864), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 856), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 848), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 840), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 832));
  printf("ZMM30: 0x%016qx%016qx%016qx%016qx%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 952), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 944), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 936), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 928), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 920), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 912), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 904), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 896));
  printf("ZMM31: 0x%016qx%016qx%016qx%016qx%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 1016), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 1008), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 1000), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 992), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 984), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 976), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 968), *(uint64_t*) ((uint8_t*)layout_area + avx512_hi16_zmm_offset + 960));

  // Apparently the opmask area in the XSAVE extended region assumes 64-bit opmask registers, even though AVX512F only uses 16-bit opmask register sizes. At least, all documentation on the subject appears to say this is the case...
  printf("k0: %#qx, k1: %#qx, k2: %#qx, k3: %#qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_opmask_offset + 0), *(uint64_t*) ((uint8_t*)layout_area + avx512_opmask_offset + 8), *(uint64_t*) ((uint8_t*)layout_area + avx512_opmask_offset + 16), *(uint64_t*) ((uint8_t*)layout_area + avx512_opmask_offset + 24));
  printf("k4: %#qx, k5: %#qx, k6: %#qx, k7: %#qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx512_opmask_offset + 32), *(uint64_t*) ((uint8_t*)layout_area + avx512_opmask_offset + 40), *(uint64_t*) ((uint8_t*)layout_area + avx512_opmask_offset + 48), *(uint64_t*) ((uint8_t*)layout_area + avx512_opmask_offset + 56));

  // ...I wonder if AVX-1024 will ever be a thing.

#elif __AVX__
  uint64_t avx_offset = 0;
  // Leaf 0Dh, AVX is user state component 2
  asm volatile ("cpuid"
                : "=b" (avx_offset) // Outputs
                : "a" (0x0D), "c" (0x02) // Inputs
                : "%rdx" // Clobbers
              );

  // AVX, XMM
  printf("YMM0: 0x%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 8), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 0), layout_area->xmm0[1], layout_area->xmm0[0]);
  printf("YMM1: 0x%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 24), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 16), layout_area->xmm1[1], layout_area->xmm1[0]);
  printf("YMM2: 0x%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 40), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 32), layout_area->xmm2[1], layout_area->xmm2[0]);
  printf("YMM3: 0x%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 56), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 48), layout_area->xmm3[1], layout_area->xmm3[0]);
  printf("YMM4: 0x%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 72), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 64), layout_area->xmm4[1], layout_area->xmm4[0]);
  printf("YMM5: 0x%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 88), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 80), layout_area->xmm5[1], layout_area->xmm5[0]);
  printf("YMM6: 0x%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 104), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 96), layout_area->xmm6[1], layout_area->xmm6[0]);
  printf("YMM7: 0x%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 120), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 112), layout_area->xmm7[1], layout_area->xmm7[0]);
  printf("YMM8: 0x%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 136), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 128), layout_area->xmm8[1], layout_area->xmm8[0]);
  printf("YMM9: 0x%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 152), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 144), layout_area->xmm9[1], layout_area->xmm9[0]);
  printf("YMM10: 0x%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 168), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 160), layout_area->xmm10[1], layout_area->xmm10[0]);
  printf("YMM11: 0x%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 184), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 176), layout_area->xmm11[1], layout_area->xmm11[0]);
  printf("YMM12: 0x%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 200), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 192), layout_area->xmm12[1], layout_area->xmm12[0]);
  printf("YMM13: 0x%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 216), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 208), layout_area->xmm13[1], layout_area->xmm13[0]);
  printf("YMM14: 0x%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 232), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 224), layout_area->xmm14[1], layout_area->xmm14[0]);
  printf("YMM15: 0x%016qx%016qx%016qx%016qx\r\n", *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 248), *(uint64_t*) ((uint8_t*)layout_area + avx_offset + 240), layout_area->xmm15[1], layout_area->xmm15[0]);

#else

// SSE part is built into XSAVE area
  printf("XMM0: 0x%016qx%016qx\r\n", layout_area->xmm0[1], layout_area->xmm0[0]);
  printf("XMM1: 0x%016qx%016qx\r\n", layout_area->xmm1[1], layout_area->xmm1[0]);
  printf("XMM2: 0x%016qx%016qx\r\n", layout_area->xmm2[1], layout_area->xmm2[0]);
  printf("XMM3: 0x%016qx%016qx\r\n", layout_area->xmm3[1], layout_area->xmm3[0]);
  printf("XMM4: 0x%016qx%016qx\r\n", layout_area->xmm4[1], layout_area->xmm4[0]);
  printf("XMM5: 0x%016qx%016qx\r\n", layout_area->xmm5[1], layout_area->xmm5[0]);
  printf("XMM6: 0x%016qx%016qx\r\n", layout_area->xmm6[1], layout_area->xmm6[0]);
  printf("XMM7: 0x%016qx%016qx\r\n", layout_area->xmm7[1], layout_area->xmm7[0]);
  printf("XMM8: 0x%016qx%016qx\r\n", layout_area->xmm8[1], layout_area->xmm8[0]);
  printf("XMM9: 0x%016qx%016qx\r\n", layout_area->xmm9[1], layout_area->xmm9[0]);
  printf("XMM10: 0x%016qx%016qx\r\n", layout_area->xmm10[1], layout_area->xmm10[0]);
  printf("XMM11: 0x%016qx%016qx\r\n", layout_area->xmm11[1], layout_area->xmm11[0]);
  printf("XMM12: 0x%016qx%016qx\r\n", layout_area->xmm12[1], layout_area->xmm12[0]);
  printf("XMM13: 0x%016qx%016qx\r\n", layout_area->xmm13[1], layout_area->xmm13[0]);
  printf("XMM14: 0x%016qx%016qx\r\n", layout_area->xmm14[1], layout_area->xmm14[0]);
  printf("XMM15: 0x%016qx%016qx\r\n", layout_area->xmm15[1], layout_area->xmm15[0]);

// So is x87/MMX part

  printf("ST/MM0: 0x%016qx%016qx\r\n", layout_area->st_mm_0[1], layout_area->st_mm_0[0]);
  printf("ST/MM1: 0x%016qx%016qx\r\n", layout_area->st_mm_1[1], layout_area->st_mm_1[0]);
  printf("ST/MM2: 0x%016qx%016qx\r\n", layout_area->st_mm_2[1], layout_area->st_mm_2[0]);
  printf("ST/MM3: 0x%016qx%016qx\r\n", layout_area->st_mm_3[1], layout_area->st_mm_3[0]);
  printf("ST/MM4: 0x%016qx%016qx\r\n", layout_area->st_mm_4[1], layout_area->st_mm_4[0]);
  printf("ST/MM5: 0x%016qx%016qx\r\n", layout_area->st_mm_5[1], layout_area->st_mm_5[0]);
  printf("ST/MM6: 0x%016qx%016qx\r\n", layout_area->st_mm_6[1], layout_area->st_mm_6[0]);
  printf("ST/MM7: 0x%016qx%016qx\r\n", layout_area->st_mm_7[1], layout_area->st_mm_7[0]);
#endif
}

//----------------------------------------------------------------------------------------------------------------------------------
//  UEFI_Reset: Shutdown or Reboot via UEFI
//----------------------------------------------------------------------------------------------------------------------------------
//
// This calls UEFI-provided shutdown and reboot functions. Not all systems provide these and may rely on ACPI instead, but there do
// exist systems that only provide UEFI reset functionality instead of ACPI. There are also systems that support both.
//
// Available reset types are:
// - EfiResetCold - Cold Reboot (essentially a hard power cycle)
// - EfiResetWarm - Warm Reboot
// - EfiResetShutdown - Shutdown
//
// There is also EfiResetPlatformSpecific, but that's not really important (instead of NULL, it takes a standard 128-bit EFI_GUID as
// ResetData for a custom reset type).
//

void UEFI_Reset(LOADER_PARAMS * LP, EFI_RESET_TYPE ResetType)
{
  if((uint64_t)LP->RTServices->ResetSystem) // Make sure the pointer isn't NULL or 0x0
  {
    asm volatile ("cli"); // Clear interrupts

    if(ResetType == EfiResetCold)
    {
      LP->RTServices->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL); // Hard reboot the system - the familiar restart
    }
    else if(ResetType == EfiResetWarm)
    {
      LP->RTServices->ResetSystem(EfiResetWarm, EFI_SUCCESS, 0, NULL); // Soft reboot the system
    }
    else if(ResetType == EfiResetShutdown)
    {
      LP->RTServices->ResetSystem(EfiResetShutdown, EFI_SUCCESS, 0, NULL); // Shutdown the system
    }
    else
    {
      error_printf("Error: Invalid ResetType provided.\r\n");
    }
  }
  else
  {
    info_printf("UEFI ResetSystem not supoprted.\r\n");
  }
}


//----------------------------------------------------------------------------------------------------------------------------------
//  HaCF: "Halt and Catch Fire"
//----------------------------------------------------------------------------------------------------------------------------------
//
// This is basically a catch-all end of the line. If something gets here, there may be a problem that needs to be addressed. See
// whatever accompanying error message is printed out before getting here for details.
//

void HaCF(void)
{
  while(1)
  {
    asm volatile("hlt");
  }
}
