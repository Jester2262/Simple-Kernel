//==================================================================================================================================
//  Simple Kernel: ACPI OS Services Layer
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
// This file provides functions needed by ACPI for OS interaction. Any calls that need to be made to ACPI should be performed in
// this file, as well. See ACPI_Shutdown() for an example.
//
// Note that the AcpiOs... functions are documented in the "ACPI Component Architecture User Guide and Programmer Reference,
// Revision 6.2" documentation, so descriptions have been omitted here. They aren't meant to be used outside of ACPICA.
//
// IMPORTANT NOTE:
// Do not use the following standard functions in this file, as they will get renamed and use the ACPI ones instead of the Kernel64
// ones. This is because ACPI uses its own built-in versions of these functions, which will cause a link-time conflict with other
// functions of the same name. The solution is to rename the ACPI ones with #define in acKernel64.h, and make sure acKernel64.h
// is not included in files other than this one and acenv.h in the ACPI backend:
//
//  strtoul()
//  memcmp()
//  memset()
//  memmove()
//  memcpy()
//  vsnprintf()
//  snprintf()
//  sprintf()
//

//
// Only for use with -DACPI_DEBUG_OUTPUT -DACPI_USE_DO_WHILE_0
//#define MAX_ACPI_DEBUG_OUTPUT
//

// ACPI has a lot of unused parameters. No need to fill the compile log with them since many are intentional.
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "acpi.h"
#include "accommon.h"
#pragma GCC diagnostic warning "-Wunused-parameter"

#define ACKERNEL64
#include "Kernel64.h"

// I don't like unused variables, but we do need this here.
#define UNUSED(x) (void)x

static ACPI_STATUS Set_ACPI_SCI_Override(void);
static ACPI_STATUS Init_EC_Handler(void);
static uint16_t sci_override_flags = 0;

//----------------------------------------------------------------------------------------------------------------------------------
// 9.1 Environmental and ACPI Tables
//----------------------------------------------------------------------------------------------------------------------------------

ACPI_STATUS AcpiOsInitialize(void)
{
//  AcpiGbl_EnableInterpreterSlack = TRUE;
//  AcpiGbl_EnableAmlDebugObject = TRUE;
//  AcpiGbl_Use32BitFacsAddresses = FALSE;
//  AcpiGbl_IgnorePackageResolutionErrors = TRUE;

#ifdef MAX_ACPI_DEBUG_OUTPUT
  AcpiDbgLevel = ACPI_DEBUG_ALL; // All of the errors!
#endif

  return AE_OK;
}

ACPI_STATUS AcpiOsTerminate(void)
{
  return AE_OK;
}

ACPI_PHYSICAL_ADDRESS AcpiOsGetRootPointer(void)
{
  // Already found via Find_RSDP() in System.c
  return (ACPI_PHYSICAL_ADDRESS)Global_RSDP_Address;
}

ACPI_STATUS AcpiOsPredefinedOverride(const ACPI_PREDEFINED_NAMES *PredefinedObject, ACPI_STRING *NewValue)
{
  if((!PredefinedObject) || (!NewValue))
  {
    return AE_BAD_PARAMETER;
  }

  *NewValue = NULL;
  return AE_OK;
}

ACPI_STATUS AcpiOsTableOverride(ACPI_TABLE_HEADER *ExistingTable, ACPI_TABLE_HEADER **NewTable)
{
  if ((!ExistingTable) || (!NewTable))
  {
    return AE_BAD_PARAMETER;
  }

  *NewTable = NULL; // Null pointer :/

  return AE_OK; // Windows
  //return AE_NO_ACPI_TABLES; // Linux/Unix
}

ACPI_STATUS AcpiOsPhysicalTableOverride(ACPI_TABLE_HEADER *ExistingTable, ACPI_PHYSICAL_ADDRESS *NewAddress, UINT32 *NewTableLength)
{
  UNUSED(ExistingTable);

  *NewAddress = 0;
  *NewTableLength = 0;

  return AE_OK;
//  return AE_SUPPORT;
}

//----------------------------------------------------------------------------------------------------------------------------------
// 9.2 Memory Management
//----------------------------------------------------------------------------------------------------------------------------------

// The cache functions are unneeded, as ACPI's built-in local cache is used. Thats what the compiler arguments -DACPI_USE_LOCAL_CACHE
// and -DACPI_CACHE_T=ACPI_MEMORY_LIST do.

/*
ACPI_STATUS AcpiOsCreateCache ()
{
}

ACPI_STATUS AcpiOsDeleteCache()
{
}

ACPI_STATUS AcpiOsPurgeCache ()
{
}

ACPI_STATUS AcpiOsAcquireObject()
{
}

ACPI_STATUS AcpiOsReleaseObject()
{
}
*/

void * AcpiOsMapMemory(ACPI_PHYSICAL_ADDRESS PhysicalAddress, ACPI_SIZE Length)
{
  // If this causes page faults, the paging subsystem will need to be modified...
  // This function wants to add pages to the page table (and thus adds descriptors to the memory map, too)

  UNUSED(Length);

  return (void*)PhysicalAddress;
}

void AcpiOsUnmapMemory(void *LogicalAddress, ACPI_SIZE Length)
{
  // See AcpiOsMapMemory
  UNUSED(Length);
  UNUSED(LogicalAddress);
}

ACPI_STATUS AcpiOsGetPhysicalAddress(void *LogicalAddress, ACPI_PHYSICAL_ADDRESS *PhysicalAddress)
{
  // If using non-identity paging, convert virtual address to physical address here

  *PhysicalAddress = (ACPI_PHYSICAL_ADDRESS)LogicalAddress; // Identity paging!
  return AE_OK;
}

void * AcpiOsAllocate(ACPI_SIZE Size)
{
  void * allocated_memory = malloc((size_t)Size);
  //printf("malloc: %#qx\r\n", allocated_memory);
  return allocated_memory;
}

void AcpiOsFree(void *Memory)
{
  free(Memory);
}

#ifdef USE_NATIVE_ALLOCATE_ZEROED

void * AcpiOsAllocateZeroed(ACPI_SIZE Size)
{
  void * allocated_memory = calloc(1, (size_t)Size);
  //printf("calloc: %#qx\r\n", allocated_memory);
  return allocated_memory;
}

#endif

BOOLEAN AcpiOsReadable(void *Memory, ACPI_SIZE Length)
{
  // If paging is actually used, this needs to be more complex
  // By default all mapped addresses exist (if it's not in the memory map, it's not physically backed by anything and will trigger a page fault)
  uint64_t MaxMappedAddr = GetMaxMappedPhysicalAddress();

  if( (((uint64_t)Memory) + Length) <= MaxMappedAddr )
  {
    return TRUE;
  }

  return FALSE;
}

BOOLEAN AcpiOsWritable(void *Memory, ACPI_SIZE Length)
{
  // If paging is actually used, this needs to be more complex
  // By default all mapped addresses are writable
  uint64_t MaxMappedAddr = GetMaxMappedPhysicalAddress();

  if( (((uint64_t)Memory) + Length) <= MaxMappedAddr )
  {
    return TRUE;
  }

  return FALSE;
}

//----------------------------------------------------------------------------------------------------------------------------------
// 9.3 Multithreading and Scheduling Services
//----------------------------------------------------------------------------------------------------------------------------------
// Not implemented yet.. TODO

#ifdef ACPI_SINGLE_THREADED

ACPI_THREAD_ID AcpiOsGetThreadId(void)
{
  return 1; // For now...
}

ACPI_STATUS AcpiOsExecute(ACPI_EXECUTE_TYPE Type, ACPI_OSD_EXEC_CALLBACK Function, void *Context)
{
  UNUSED(Type);

  Function(Context);

  return AE_OK;
}

#else

ACPI_THREAD_ID AcpiOsGetThreadId(void)
{
  // Get current thread ID
  warning_printf("Unimplemented multithreaded AcpiOsGetThreadId called\r\n");

  return 1; // For now...
}

ACPI_STATUS AcpiOsExecute(ACPI_EXECUTE_TYPE Type, ACPI_OSD_EXEC_CALLBACK Function, void *Context)
{
  UNUSED(Type);

  warning_printf("Unimplemented multithreaded AcpiOsExecute called\r\n");

  Function(Context);

  return AE_OK;
}

#endif

void AcpiOsSleep(UINT64 Milliseconds)
{
  msleep(Milliseconds);
}

void AcpiOsStall(UINT32 Microseconds)
{
  usleep((uint64_t)Microseconds);
}

void AcpiOsWaitEventsComplete(void)
{
  warning_printf("Unimplemented AcpiOsWaitEventsComplete called\r\n");
}

//----------------------------------------------------------------------------------------------------------------------------------
// 9.4 Mutual Exclusion and Synchronization
//----------------------------------------------------------------------------------------------------------------------------------

// ACPI does this when Mutexes aren't supported.
// Windows and Linux do all of the Spinlock/Mutex-to-Semaphore stuff, too.

/*
ACPI_STATUS AcpiOsCreateMutex(ACPI_MUTEX *OutHandle)
{
  return AcpiOsCreateSemaphore(1, 1, OutHandle);
}

void AcpiOsDeleteMutex(ACPI_MUTEX Handle)
{
  AcpiOsDeleteSemaphore(Handle);
}

ACPI_STATUS AcpiOsAcquireMutex(ACPI_MUTEX Handle, UINT16 Timeout)
{
  AcpiOsWaitSemaphore(Handle, 1, Timeout);
  return AE_OK;
}

void AcpiOsReleaseMutex(ACPI_MUTEX Handle)
{
  AcpiOsSignalSemaphore(Handle, 1);
}
*/
// That's all the Mutex-to-Semaphore stuff

#ifdef ACPI_SINGLE_THREADED
// These get called a lot, even in single-threaded mode. Hide the printfs.
ACPI_STATUS AcpiOsCreateSemaphore(UINT32 MaxUnits, UINT32 InitialUnits, ACPI_SEMAPHORE *OutHandle)
{
  UNUSED(MaxUnits);
  UNUSED(InitialUnits);

  *OutHandle = (ACPI_HANDLE) 1;
  return AE_OK;
}

ACPI_STATUS AcpiOsDeleteSemaphore(ACPI_SEMAPHORE Handle)
{
  UNUSED(Handle);

  return AE_OK;
}

ACPI_STATUS AcpiOsWaitSemaphore(ACPI_SEMAPHORE Handle, UINT32 Units, UINT16 Timeout)
{
  UNUSED(Handle);
  UNUSED(Units);
  UNUSED(Timeout);

  return AE_OK;
}

ACPI_STATUS AcpiOsSignalSemaphore(ACPI_SEMAPHORE Handle, UINT32 Units)
{
  UNUSED(Handle);
  UNUSED(Units);

  return AE_OK;
}
#else

// Not yet implemented

ACPI_STATUS AcpiOsCreateSemaphore(UINT32 MaxUnits, UINT32 InitialUnits, ACPI_SEMAPHORE *OutHandle)
{
  UNUSED(MaxUnits);
  UNUSED(InitialUnits);

  warning_printf("Unimplemented multithreaded AcpiOsCreateSemaphore called\r\n");

  *OutHandle = (ACPI_HANDLE) 1;
  return AE_OK;
}

ACPI_STATUS AcpiOsDeleteSemaphore(ACPI_SEMAPHORE Handle)
{
  UNUSED(Handle);

  warning_printf("Unimplemented multithreaded AcpiOsDeleteSemaphore called\r\n");
  return AE_OK;
}

ACPI_STATUS AcpiOsWaitSemaphore(ACPI_SEMAPHORE Handle, UINT32 Units, UINT16 Timeout)
{
  UNUSED(Handle);
  UNUSED(Units);
  UNUSED(Timeout);

  warning_printf("Unimplemented multithreaded AcpiOsWaitSemaphore called\r\n");
  return AE_OK;
}

ACPI_STATUS AcpiOsSignalSemaphore(ACPI_SEMAPHORE Handle, UINT32 Units)
{
  UNUSED(Handle);
  UNUSED(Units);

  warning_printf("Unimplemented multithreaded AcpiOsSignalSemaphore called\r\n");
  return AE_OK;
}
#endif

ACPI_STATUS AcpiOsCreateLock(ACPI_SPINLOCK *OutHandle)
{
  return AcpiOsCreateSemaphore(1, 1, OutHandle);
}

void AcpiOsDeleteLock(ACPI_HANDLE Handle)
{
  AcpiOsDeleteSemaphore(Handle);
}

ACPI_CPU_FLAGS AcpiOsAcquireLock(ACPI_SPINLOCK Handle)
{
  AcpiOsWaitSemaphore(Handle, 1, 0xFFFF);
  return AE_OK;
}

void AcpiOsReleaseLock(ACPI_SPINLOCK Handle, ACPI_CPU_FLAGS Flags)
{
  UNUSED(Flags);

  AcpiOsSignalSemaphore(Handle, 1);
}
// end TODO

//----------------------------------------------------------------------------------------------------------------------------------
// 9.5 Interrupt Handling
//----------------------------------------------------------------------------------------------------------------------------------

ACPI_STATUS AcpiOsInstallInterruptHandler(UINT32 InterruptLevel, ACPI_OSD_HANDLER Handler, void *Context)
{
  // The User_ISR_Handler can use case statement to evaluate InterruptLevel, then execute HandlerAddress(Context)
  // TODO: need to remap SCI using I/O APIC & LAPIC, read ch. 10 in Intel manual vol. 3A
  if((Handler == NULL) || (InterruptLevel > 255))
  {
    return AE_BAD_PARAMETER;
  }

  // The fact that there's already an ISR for every number means AE_ALREADY_EXISTS is a non-issue.
  // It's supposed to already exist here.

  // DT_STRUCT idt_struct = get_idtr();
  // IDT_GATE_STRUCT InterruptNumberData = ((IDT_GATE_STRUCT*)(idt_struct.BaseAddress))[InterruptLevel];

/*
  if(InterruptNumberData.Misc & 0x80)
  {
    error_printf("ACPI tried to install interrupt %u\r\n", InterruptLevel);
    return AE_ALREADY_EXISTS;
  }
*/
  info_printf("ACPI using IRQ %u\r\n", InterruptLevel);

  Global_ACPI_Interrupt_Table[InterruptLevel].InterruptNumber = InterruptLevel;
  Global_ACPI_Interrupt_Table[InterruptLevel].HandlerPointer = Handler;
  Global_ACPI_Interrupt_Table[InterruptLevel].Context = Context;

  return AE_OK;
}

ACPI_STATUS AcpiOsRemoveInterruptHandler(UINT32 InterruptLevel, ACPI_OSD_HANDLER Handler)
{
  UNUSED(Handler);

  info_printf("ACPI no longer using IRQ %u\r\n", InterruptLevel);

  Global_ACPI_Interrupt_Table[InterruptLevel].InterruptNumber = 0;
  Global_ACPI_Interrupt_Table[InterruptLevel].HandlerPointer = 0;
  Global_ACPI_Interrupt_Table[InterruptLevel].Context = 0;

  return AE_OK;
}
//----------------------------------------------------------------------------------------------------------------------------------
// 9.6 Memory Access and Memory Mapped I/O
//----------------------------------------------------------------------------------------------------------------------------------

ACPI_STATUS AcpiOsReadMemory(ACPI_PHYSICAL_ADDRESS Address, UINT64 *Value, UINT32 Width)
{
  // This seems ok
  *Value = 0; // Spec says zero-extended
  if(Width == 8)
  {
    *Value = *(volatile UINT8*)Address;
  }
  else if(Width == 16)
  {
    *Value = *(volatile UINT16*)Address;
  }
  else if(Width == 32)
  {
    *Value = *(volatile UINT32*)Address;
  }
  else if(Width == 64)
  {
    *Value = *(volatile UINT64*)Address;
  }
  else
  {
    return AE_BAD_PARAMETER;
  }

  return AE_OK;
}

ACPI_STATUS AcpiOsWriteMemory(ACPI_PHYSICAL_ADDRESS Address, UINT64 Value, UINT32 Width)
{
  // Likewise
  if(Width == 8)
  {
    *(volatile UINT8*)Address = (UINT8)Value;
  }
  else if(Width == 16)
  {
    *(volatile UINT16*)Address = (UINT16)Value;
  }
  else if(Width == 32)
  {
    *(volatile UINT32*)Address = (UINT32)Value;
  }
  else if(Width == 64)
  {
    *(volatile UINT64*)Address = Value;
  }
  else
  {
    return AE_BAD_PARAMETER;
  }

  return AE_OK;
}

//----------------------------------------------------------------------------------------------------------------------------------
// 9.7 Port Input/Output
//----------------------------------------------------------------------------------------------------------------------------------

ACPI_STATUS AcpiOsReadPort(ACPI_IO_ADDRESS Address, UINT32 *Value, UINT32 Width)
{
  *Value = 0;
  if((Width == 8) || (Width == 16) || (Width = 32))
  {
    *Value = portio_rw((uint16_t)Address, *Value, Width/8, 0);
  }
  else
  {
    return AE_BAD_PARAMETER;
  }

  return AE_OK;
}

ACPI_STATUS AcpiOsWritePort(ACPI_IO_ADDRESS Address, UINT32 Value, UINT32 Width)
{
  if((Width == 8) || (Width == 16) || (Width = 32))
  {
    portio_rw((uint16_t)Address, Value, Width/8, 1);
  }
  else
  {
    return AE_BAD_PARAMETER;
  }

  return AE_OK;
}

//----------------------------------------------------------------------------------------------------------------------------------
// 9.8 PCI Configuration Space Access
//----------------------------------------------------------------------------------------------------------------------------------

// NOTE: The spec (Version 6.2 of the ACPI Component Architecture User Guide and Programmer Reference) has a typo:
// PciId should be *PciId
ACPI_STATUS AcpiOsReadPciConfiguration(ACPI_PCI_ID *PciId, UINT32 Register, UINT64 *Value, UINT32 Width)
{
  // Somewhere over the rainbow, MMIO with MCFG table is needed for PCIe Extended Config Space access
  // That'll need AcpiGetTable() (shouldn't need puttable, since this doesn't get used until after tables have been initially loaded. puttable is for InitializeAcpiTablesOnly situations)
  // This might be useful to look at for an example:
  // http://mirror.nyi.net/NetBSD/NetBSD-release-7/src/sys/dev/acpi/acpica/OsdHardware.c
  // This too:
  // http://cinnabar.sosdg.org/~qiyong/qxr/minix3/source/minix/drivers/power/acpi/osminixxf.c

  if((PciId->Bus >= 255) || (PciId->Device >= 32) || (PciId->Function >= 8))
  {
    return AE_BAD_PARAMETER;
  }

  *Value = 0; // Return value is 0-extended to 64 bits

  // PciId->Segment is the segment group. Only matters if the system has an MCFG otherwise it'll be group 0

  uint16_t pci_config_addr = 0x0CF8;
  uint16_t pci_config_data = 0x0CFC;
  uint32_t pci_addr = 0x80000000U | ((uint32_t)PciId->Bus << 16) | ((uint32_t)PciId->Device << 11) | ((uint32_t)PciId->Function << 8) | (uint8_t)Register;

  switch(Width)
  {
    case 8:
      portio_rw(pci_config_addr, pci_addr, 4, 1);
      *Value = (uint8_t)portio_rw(pci_config_data + (Register & 0x3), 0, 1, 0);
      break;
    case 16:
      portio_rw(pci_config_addr, pci_addr, 4, 1);
      *Value = (uint16_t)portio_rw(pci_config_data + (Register & 0x2), 0, 2, 0);
      break;
    case 32:
      portio_rw(pci_config_addr, pci_addr, 4, 1);
      *Value = (uint32_t)portio_rw(pci_config_data, 0, 4, 0);
      break;
    case 64:
      // Part 1
      portio_rw(pci_config_addr, pci_addr, 4, 1);
      ((uint32_t*)Value)[0] = (uint32_t)portio_rw(pci_config_data, 0, 4, 0);
      // Part 2
      portio_rw(pci_config_addr, pci_addr + 4, 4, 1);
      ((uint32_t*)Value)[1] = (uint32_t)portio_rw(pci_config_data, 0, 4, 0);
      break;
    default:
      return AE_BAD_PARAMETER;
  }

  printf("ACPI PCI READ %#x @ Seg %hu --> %#qx, Width: %u\r\n", pci_addr, PciId->Segment, *Value, Width);

  return AE_OK;
}

ACPI_STATUS AcpiOsWritePciConfiguration(ACPI_PCI_ID *PciId, UINT32 Register, UINT64 Value, UINT32 Width)
{
  // Somewhere over the rainbow, MCFG

  if((PciId->Bus >= 255) || (PciId->Device >= 32) || (PciId->Function >= 8))
  {
    return AE_BAD_PARAMETER;
  }

  // PciId->Segment is the segment group. Only matters if the system has an MCFG otherwise it'll be group 0

  uint16_t pci_config_addr = 0x0CF8;
  uint16_t pci_config_data = 0x0CFC;
  uint32_t pci_addr = 0x80000000U | ((uint32_t)PciId->Bus << 16) | ((uint32_t)PciId->Device << 11) | ((uint32_t)PciId->Function << 8) | (uint8_t)Register;

  switch(Width)
  {
    case 8:
      portio_rw(pci_config_addr, pci_addr, 4, 1);
      portio_rw(pci_config_data + (Register & 0x3), (uint32_t)Value, 1, 1);
      break;
    case 16:
      portio_rw(pci_config_addr, pci_addr, 4, 1);
      portio_rw(pci_config_data + (Register & 0x2), (uint32_t)Value, 2, 1);
      break;
    case 32:
      portio_rw(pci_config_addr, pci_addr, 4, 1);
      portio_rw(pci_config_data, (uint32_t)Value, 4, 1);
      break;
    case 64:
      // Part 1
      portio_rw(pci_config_addr, pci_addr, 4, 1);
      portio_rw(pci_config_data, ((uint32_t*)&Value)[0], 4, 1);
      // Part 2
      portio_rw(pci_config_addr, pci_addr + 4, 4, 1);
      portio_rw(pci_config_data, ((uint32_t*)&Value)[1], 4, 1);
      break;
    default:
      return AE_BAD_PARAMETER;
  }

  printf("ACPI PCI WRITE %#x @ Seg %hu <-- %#qx, Width: %u\r\n", pci_addr, PciId->Segment, Value, Width);

  return AE_OK;
}

//----------------------------------------------------------------------------------------------------------------------------------
// 9.9 Formatted Output
//----------------------------------------------------------------------------------------------------------------------------------

void AcpiOsPrintf(const char *Format, ...)
{
  va_list Args;
  va_start(Args, Format);
  vprintf(Format, Args);
  va_end(Args);
}

void AcpiOsVprintf(const char *Format, va_list Args)
{
  vprintf(Format, Args);
}

void AcpiOsRedirectOutput(void *Destination)
{
  // This function assumes printf output is reconfigurable.
  // Well, we have a working implementation of sprintf() here for that.
  // There is no way to redirect all of Kernel64's printf output to somewhere else.

  // But this is what Windows and Linux do, and ACPI creates this file object when the condition is true
#ifdef ACPI_APPLICATION
  AcpiGbl_OutputFile = Destination;
#else
  UNUSED(Destination);

  warning_printf("Warning: AcpiOsRedirectOutput called, but there's nowhere to redirect the output.\r\n");
#endif
}

//----------------------------------------------------------------------------------------------------------------------------------
// 9.10 System ACPI Table Access
//----------------------------------------------------------------------------------------------------------------------------------

// NOTE: Only the AcpiDump utility uses these. They don't need to be implmented unless AcpiDump functionality is desired.

// Y'know, based on the linux and windows service layers, it looks like we do actually have the capability to
// implement these. Maybe another day.

/*
ACPI_STATUS AcpiOsGetTableByAddress(ACPI_PHYSICAL_ADDRESS Address, ACPI_TABLE_HEADER **OutTable)
{
  UNUSED(Address);
  UNUSED(OutTable);

  warning_printf("Unimplemented AcpiOsGetTableByAddress called... This is an AcpiDump function\r\n");
  return AE_SUPPORT;
}
// NOTE: The spec (Version 6.2 of the ACPI Component Architecture User Guide and Programmer Reference) has a typo:
// **OutAddress should be *OutAddress and the document is completely missing UINT32 *Instance
ACPI_STATUS AcpiOsGetTableByIndex(UINT32 TableIndex, ACPI_TABLE_HEADER **OutTable, UINT32 *Instance, ACPI_PHYSICAL_ADDRESS *OutAddress)
{
  UNUSED(TableIndex);
  UNUSED(OutTable);
  UNUSED(Instance);
  UNUSED(OutAddress);

  warning_printf("Unimplemented AcpiOsGetTableByIndex called... This is an AcpiDump function\r\n");
  return AE_SUPPORT;
}

// NOTE: The spec (Version 6.2 of the ACPI Component Architecture User Guide and Programmer Reference) has a typo:
// **OutAddress should be *OutAddress
ACPI_STATUS AcpiOsGetTableByName(char *Signature, UINT32 Instance, ACPI_TABLE_HEADER **OutTable, ACPI_PHYSICAL_ADDRESS *OutAddress)
{
  UNUSED(Signature);
  UNUSED(Instance);
  UNUSED(OutTable);
  UNUSED(OutAddress);

  warning_printf("Unimplemented AcpiOsGetTableByName called... This is an AcpiDump function\r\n");
  return AE_SUPPORT;
}
*/

//----------------------------------------------------------------------------------------------------------------------------------
// 9.11 Miscellaneous
//----------------------------------------------------------------------------------------------------------------------------------

UINT64 AcpiOsGetTimer(void)
{
  uint64_t cycle_count = get_tick();

  return (cycle_count / Global_TSC_frequency.CyclesPer100ns);
}

ACPI_STATUS AcpiOsSignal(UINT32 Function, void *Info)
{
  // Neither Windows nor Linux do anything here.
  // Oh well.

  switch(Function)
  {
    case ACPI_SIGNAL_FATAL:
      error_printf("Got FATAL signal from ACPI. Halting.\r\n");
      info_printf("Signal details: Type: %#x, Code: %#x, Argument: %#x\r\n", ((ACPI_SIGNAL_FATAL_INFO*)Info)->Type, ((ACPI_SIGNAL_FATAL_INFO*)Info)->Code, ((ACPI_SIGNAL_FATAL_INFO*)Info)->Argument);
      asm volatile ("hlt");
      break;
    case ACPI_SIGNAL_BREAKPOINT:
      info_printf("ACPI Breakpoint signal. %s\r\n", (char*)Info); // It's a message like this: "Executed AML Breakpoint opcode"
      // Do whatever one might want to do upon hitting an ACPI breakpoint opcode
      break;
    default:
      warning_printf("Warning: Unknown signal type received from ACPI.\r\n");
      break;
  }

  return AE_OK;
}

ACPI_STATUS AcpiOsGetLine(char *Buffer, UINT32 BufferLength, UINT32 *BytesRead)
{
  // This is just getline.

  // Don't have keyboard input yet!
  // TODO
  UNUSED(Buffer);
  UNUSED(BufferLength);
  UNUSED(BytesRead);

  warning_printf("Unimplemented AcpiOsGetLine called\r\n");
  return AE_OK;
}

// Because ACPI complains without these
// ...Well, not after removing ACPI debugger-related stuff, it doesn't!
/*
ACPI_STATUS AcpiOsInitializeDebugger(void)
{
  return AE_OK;
}

void AcpiOsTerminateDebugger(void)
{
  // Nothing
}

ACPI_STATUS AcpiOsWaitCommandReady(void)
{
  return AE_OK;
}

ACPI_STATUS AcpiOsNotifyCommandComplete(void)
{
  return AE_OK;
}

void AcpiOsTracePoint(ACPI_TRACE_EVENT_TYPE Type, BOOLEAN Begin, UINT8 *Aml, char *Pathname)
{
  UNUSED(Type);
  UNUSED(Begin);
  UNUSED(Aml);
  UNUSED(Pathname);
}
*/

// This one isn't even documented in the ACPI Component Architecture User Guide and Programmer Reference...
// It is mentioned in changes.txt, though.
ACPI_STATUS AcpiOsEnterSleep(UINT8 SleepState, UINT32 RegaValue, UINT32 RegbValue)
{
  UNUSED(SleepState);
  UNUSED(RegaValue);
  UNUSED(RegbValue);

  // The only thing it seems this function does out in the wild is contain a check for device sleep-state testing, and if true returns AE_CTRL_TERMINATE.
  // Otherwise it doesn't do anything and just returns AE_OK.

  return AE_OK;
}

//==================================================================================================================================
// External Functions
//==================================================================================================================================
//
// These functions are declared in Kernel64.h, as they are meant to be called from programs. The above AcpiOS... functions are what
// allows ACPICA's internal mechanisms to interface with hardware, and are not meant to be called outside of ACPICA.
//

//----------------------------------------------------------------------------------------------------------------------------------
// InitializeFullAcpi: Init ACPI (Full)
//----------------------------------------------------------------------------------------------------------------------------------
//
// Main ACPI init function, taken from Chapter 10.1.2.1 (Full ACPICA Initialization) of the "ACPI Component Architecture User Guide
// and Programmer Reference, Revision 6.2"
//
// Returns AE_OK (0) on success.
//

ACPI_STATUS InitializeFullAcpi(void)
{
  ACPI_STATUS Status;
  /* Initialize the ACPICA subsystem */

  Status = AcpiInitializeSubsystem();
  if(ACPI_FAILURE(Status))
  {
    return Status;
  }

  /* Initialize the ACPICA Table Manager and get all ACPI tables */
  Status = AcpiInitializeTables(NULL, 16, TRUE);
  if(ACPI_FAILURE(Status))
  {
    return Status;
  }

  /* Create the ACPI namespace from ACPI tables */
  Status = AcpiLoadTables();
  if(ACPI_FAILURE(Status))
  {
    return Status;
  }

  /* Note: Local handlers should be installed here */
  // Handle SCI override
  Status = Set_ACPI_SCI_Override();
  if(ACPI_FAILURE(Status))
  {
    return Status;
  }

  // Handle ECDT
  Status = Init_EC_Handler();
  if(ACPI_FAILURE(Status))
  {
    return Status;
  }

  /* Initialize the ACPI hardware */
  Status = AcpiEnableSubsystem(ACPI_FULL_INITIALIZATION);
  if(ACPI_FAILURE(Status))
  {
    return Status;
  }

  /* Complete the ACPI namespace object initialization */
  Status = AcpiInitializeObjects(ACPI_FULL_INITIALIZATION);
  if(ACPI_FAILURE(Status))
  {
    return Status;
  }

  return AE_OK;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Quit_ACPI: Terminate ACPI Subsystem
//----------------------------------------------------------------------------------------------------------------------------------
//
// ACPI does not really need to be terminated once initialized, per Chapter 10.1.3 (Shutdown Sequence) in "ACPI Component Architecture
// User Guide and Programmer Reference, Revision 6.2." But if for some reason it does, this is how to do it.
//
// Returns AE_OK (0) on success.
//

ACPI_STATUS Quit_ACPI(void)
{
  return AcpiTerminate();
}

//----------------------------------------------------------------------------------------------------------------------------------
// InitializeAcpiTablesOnly: Initialize ACPI Table Manager Alone
//----------------------------------------------------------------------------------------------------------------------------------
//
// Init ACPI table manager only, mainly meant for accessing ACPI tables that may be needed for early boot.
// This is meant to be used in conjunction with InitializeAcpiAfterTables(), which performs the remainder of the ACPI init sequence.
//
// NOTE: To initialize APCI, use either InitializeFullAcpi() by itself or the combination of InitializeAcpiTablesOnly() +
// InitializeAcpiAfterTables(), but don't mix them.
//
// Returns AE_OK (0) on success.
//

ACPI_STATUS InitializeAcpiTablesOnly(void)
{
  ACPI_STATUS Status;

  /* Initialize the ACPICA Table Manager and get all ACPI tables */
  Status = AcpiInitializeTables(NULL, 16, TRUE);

  return Status;
}

//----------------------------------------------------------------------------------------------------------------------------------
// InitializeAcpiAfterTables: Initialize ACPI After Table Manager
//----------------------------------------------------------------------------------------------------------------------------------
//
// Init the rest of ACPI, after table manager. Use this only after InitializeAcpiTablesOnly() to finish ACPI initialization.
//
// NOTE: To initialize APCI, use either InitializeFullAcpi() by itself or the combination of InitializeAcpiTablesOnly() +
// InitializeAcpiAfterTables(), but don't mix them.
//
// Returns AE_OK (0) on success.
//

ACPI_STATUS InitializeAcpiAfterTables(void)
{
  ACPI_STATUS Status;

  /* Initialize the ACPICA subsystem */
  Status = AcpiInitializeSubsystem();
  if(ACPI_FAILURE (Status))
  {
    return Status;
  }

  /* Copy the root table list to dynamic memory */
  Status = AcpiReallocateRootTable();
  if(ACPI_FAILURE(Status))
  {
    return Status;
  }

  /* Create the ACPI namespace from ACPI tables */
  Status = AcpiLoadTables();
  if(ACPI_FAILURE(Status))
  {
    return Status;
  }

  /* Note: Local handlers should be installed here */

  // Handle SCI override
  Status = Set_ACPI_SCI_Override();
  if(ACPI_FAILURE(Status))
  {
    return Status;
  }

  // Handle ECDT
  Status = Init_EC_Handler();
  if(ACPI_FAILURE(Status))
  {
    return Status;
  }

  /* Initialize the ACPI hardware */
  Status = AcpiEnableSubsystem(ACPI_FULL_INITIALIZATION);
  if(ACPI_FAILURE(Status))
  {
    return Status;
  }

  /* Complete the ACPI namespace object initialization */
  Status = AcpiInitializeObjects(ACPI_FULL_INITIALIZATION);
  if(ACPI_FAILURE(Status))
  {
    return Status;
  }

  return AE_OK;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Set_ACPI_SCI_Override: Find APIC Override for ACPI SCI Interrupt
//----------------------------------------------------------------------------------------------------------------------------------
//
// The MADT table may contain an override for the ACPI global SCI interrupt, which should be used for APIC mode instead of the default
// legacy PIC values.
//
// Returns AE_OK on success.
//

static ACPI_STATUS Set_ACPI_SCI_Override(void)
{
  ACPI_TABLE_HEADER * MADTTableHeader;
  ACPI_STATUS Status = AcpiGetTable(ACPI_SIG_MADT, 1, &MADTTableHeader);
  if(ACPI_FAILURE(Status))
  {
    error_printf("AcpiGetTable failed.\r\n");
    return Status;
  }

  ACPI_TABLE_MADT * MADTTable = (ACPI_TABLE_MADT*)MADTTableHeader;

  uint32_t MADTTableEnd = MADTTable->Header.Length;
  ACPI_SUBTABLE_HEADER * TypeLength;

  // MADT Structs start at offset 44
  for(uint32_t TableParse = 44; TableParse < MADTTableEnd; TableParse += TypeLength->Length)
  {
    TypeLength = (ACPI_SUBTABLE_HEADER *)((uint64_t)MADTTable + TableParse);
    //printf("MADT Type: %hhx\r\n", TypeLength->Type);
    if(TypeLength->Type == ACPI_MADT_TYPE_INTERRUPT_OVERRIDE)
    {
      ACPI_MADT_INTERRUPT_OVERRIDE * MADTOverrideType = (ACPI_MADT_INTERRUPT_OVERRIDE *)TypeLength;

      if(MADTOverrideType->SourceIrq == AcpiGbl_FADT.SciInterrupt)
      {
        AcpiGbl_FADT.SciInterrupt = (uint16_t)MADTOverrideType->GlobalIrq;
        sci_override_flags = MADTOverrideType->IntiFlags;

        // Get polarity and trigger overrides
        uint8_t pol = MADTOverrideType->IntiFlags & 0x03;
        uint8_t trig = (MADTOverrideType->IntiFlags & 0x0C) >> 2;

        info_printf("\r\nACPI APIC SCI override found: Old IRQ: %hhu, New IRQ: %u\r\n", MADTOverrideType->SourceIrq, MADTOverrideType->GlobalIrq);
        printf("Polarity: %#hx, TriggerLv: %#hx\r\n", pol, trig);

        if(MADTTable->Flags & 0x1) // System has legacy PICs
        {
          // Take care of trigger level type override for SCI
          // ELCR is "Edge/Level Control Register"
          if((!trig) || (trig == 3)) // Level-triggered. "Bus compatible" is level-triggered for APIC
          {
            uint16_t elcr = portio_rw(0x4D0, 0, 1, 0);
            elcr |= portio_rw(0x4D1, 0, 1, 0) << 8;

            if( !(elcr & (1 << MADTOverrideType->SourceIrq)) ) // Is trigger level already set?
            {
              // No, set it
              elcr |= (1 << MADTOverrideType->SourceIrq);
              portio_rw(0x4D0, elcr & 0xFF, 1, 1);
              portio_rw(0x4D1, (elcr >> 8) & 0xFF, 1, 1);
              info_printf("ACPI PIC SCI trigger level override set (edge --> level).\r\n");
            }
            // It is, do nothing.
          }
          else if(trig == 1) // Edge-triggered
          {
            uint16_t elcr = portio_rw(0x4D0, 0, 1, 0);
            elcr |= portio_rw(0x4D1, 0, 1, 0) << 8;

            if( (elcr & (1 << MADTOverrideType->SourceIrq)) ) // Is trigger edge already set?
            {
              // No, set it
              elcr &= ~(1 << MADTOverrideType->SourceIrq);
              portio_rw(0x4D0, elcr & 0xFF, 1, 1);
              portio_rw(0x4D1, (elcr >> 8) & 0xFF, 1, 1);
              info_printf("ACPI PIC SCI trigger level override set (level --> edge).\r\n");
            }
            // It is, do nothing.
          }
          // Any other values are reserved, so nothing can be done for them.
        }

        break;
      }
    }
  }

  return AE_OK;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Init_EC_Handler: Initialize Embedded Controller ACPI Handler
//----------------------------------------------------------------------------------------------------------------------------------
//
// The embedded controller, if there exists an ECDT, needs to have a handler installed before enabling ACPI.
//

static ACPI_STATUS Init_EC_Handler(void)
{
  ACPI_TABLE_HEADER * ECDTTableHeader;
  ACPI_STATUS Status = AcpiGetTable(ACPI_SIG_ECDT, 1, &ECDTTableHeader);
  if(Status == AE_NOT_FOUND)
  {
    printf("No ECDT available.\r\n");
    return AE_OK;
  }
  else if(ACPI_FAILURE(Status))
  {
    error_printf("AcpiGetTable failed.\r\n");
    return Status;
  }

  ACPI_TABLE_ECDT * ECDTTable = (ACPI_TABLE_ECDT*)ECDTTableHeader;

  // TODO
  warning_printf("ECDT not yet implemented\r\n");


  return AE_OK;
}


//----------------------------------------------------------------------------------------------------------------------------------
// Set_ACPI_APIC_Mode: Establish APIC Mode in ACPI
//----------------------------------------------------------------------------------------------------------------------------------
//
// This function sets the ACPI system interrupt mode to APIC mode. This is needed to initialize ACPI for APIC operation.
//

void Set_ACPI_APIC_Mode(void)
{
  ACPI_STATUS APICStatus;

  ACPI_TABLE_HEADER * APICTableHeader;
  APICStatus = AcpiGetTable(ACPI_SIG_MADT, 1, &APICTableHeader);
  if(ACPI_FAILURE(APICStatus))
  {
    error_printf("Could not get MADT/APIC table. %x\r\n", APICStatus);
  }

  ACPI_TABLE_MADT * APICTable = (ACPI_TABLE_MADT*)APICTableHeader;

  if(APICTable->Flags & 0x1) // Check for PICs
  {
    info_printf("System has dual legacy 8259A PICs... ");

    // Remap and mask them.
    // 8259A datasheet:
    // https://pdos.csail.mit.edu/6.828/2016/readings/hardware/8259A.pdf

    portio_rw(0x20, 0x11, 1, 1); // Master PIC: Send ICW1, with a requirement that ICW4 will need to be read (D0 = 1). Also set cascade mode (D1 = 0), interval of 8 (D2 = 0, ignored for x86), edge triggered (D3 = 0, ignored anyways with APICs and ELCR)
    portio_rw(0xA0, 0x11, 1, 1); // Likewise for the slave PIC. For both of these, A0 = 0, and A5-7 (which are D5-7) aren't valid (so set them all zeros) for x86

    portio_rw(0x21, 0x20, 1, 1); // ICW2: Map 8 IRs of master PIC to 0x20 (IDT 32-40)
    portio_rw(0xA1, 0x28, 1, 1); // Similarly, map 8 IRs of slave PIC to 0x28 (IDT 40-47)

    // Note: where it looks like the datasheet states ICW3 is "read only," the sentence is actually stating "the PIC only pays attention to ICW3 in multi-PIC mode" instead of "the ICW3 byte is not modifiable."
    // Because read (`reed`) and read (`red`) are spelled the same way. ...Yeah.
    portio_rw(0x21, 0x04, 1, 1); // ICW3: Tell master PIC it has a slave on IR2.
    portio_rw(0xA1, 0x02, 1, 1); // Tell slave PIC its ID is 2 (this is a BCD number, whereas the master PIC gets a bitmask)

    portio_rw(0x21, 0x01, 1, 1); // ICW4: 80x86 mode (D0 = 1), disable Auto-EOI (D1 = 0), not buffered (D2 = 0, D3 = 0), not fully nested (D4 = 0), D5-D7 are all 0.
    portio_rw(0xA1, 0x01, 1, 1); // Likewise for slave PIC

    portio_rw(0x21, 0xFF, 1, 1); // Mask all interrupts on master PIC
    portio_rw(0xA1, 0xFF, 1, 1); // Mask all interrupts on slave PIC

    info_printf("Masked.\r\n");
  }
  else
  {
    printf("No legacy PICs.\r\n");
  }

  // Get Lapic Address
  LapicAddress = APICTable->Address;

  // Gothrough MADT and set up APICs
  uint32_t APICTableEnd = APICTable->Header.Length;
  ACPI_SUBTABLE_HEADER * TypeLength;

  // MADT Structs start at offset 44
  for(uint32_t TableParse = 44; TableParse < APICTableEnd; TableParse += TypeLength->Length)
  {
    TypeLength = (ACPI_SUBTABLE_HEADER *)((uint64_t)APICTable + TableParse);

    //
    // This was originally going to be a switch() statement, but there are too many variables. That's a lot of
    // wasted space. Cases in a switch statement are labels, and while each one could be "scoped," that's a C++ way
    // of thinking. This isn't C++.
    //
    // I mean, it'll work, but see here for a sampling of the various responses one might get when using case x:{} :
    // https://stackoverflow.com/questions/4241545/c-switch-case-curly-braces-after-every-case
    // Some people get mad, some get uppity, some even say "use functions." To be fair, a switch with function calls
    // is probably the best way to handle this. However, it is harder to debug.
    //
    // At the same time, I also think that this format is quite effective in illustrating how nuts ACPI is when it comes
    // to interrupts, and also the complexity that is x86 interrupt handling as whole. In my opinion, it's worth leaving
    // this format as-is just for that. :)
    //

    if(TypeLength->Type == ACPI_MADT_TYPE_LOCAL_APIC)
    {
      Numcores++; // One of these for each core
      ACPI_MADT_LOCAL_APIC * CoreLapic = (ACPI_MADT_LOCAL_APIC*)TypeLength;

      printf("CPU %hhu: LAPIC ID: %hhu, Flags: %u\r\n", CoreLapic->ProcessorId, CoreLapic->Id, CoreLapic->LapicFlags);
    }
    else if(TypeLength->Type == ACPI_MADT_TYPE_IO_APIC)
    {
      ACPI_MADT_IO_APIC * IOApic = (ACPI_MADT_IO_APIC*)TypeLength;

      // TODO setup ioapic


      printf("I/O APIC: ID: %hhu, Address: %#x, GlobalIrqBase: %u\r\n", IOApic->Id, IOApic->Address, IOApic->GlobalIrqBase);
    }
    else if(TypeLength->Type == ACPI_MADT_TYPE_INTERRUPT_OVERRIDE)
    {
      // Bus is always 0
      ACPI_MADT_INTERRUPT_OVERRIDE * IntOvr = (ACPI_MADT_INTERRUPT_OVERRIDE*)TypeLength;
      uint8_t pol = IntOvr->IntiFlags & 0x03;
      uint8_t trig = (IntOvr->IntiFlags & 0x0C) >> 2;

      printf("IRQ Override: SrcIRQ: %hhu, GSI: %u, Trig: %hhu, Pol: %hhu\r\n", IntOvr->SourceIrq, IntOvr->GlobalIrq, trig, pol);
    }
    else if(TypeLength->Type == ACPI_MADT_TYPE_NMI_SOURCE)
    {
      ACPI_MADT_NMI_SOURCE * NMIOvr = (ACPI_MADT_NMI_SOURCE*)TypeLength;
      uint8_t pol = NMIOvr->IntiFlags & 0x03;
      uint8_t trig = (NMIOvr->IntiFlags & 0x0C) >> 2;

      printf("NMI Override: GSI: %u, Trig: %hhu, Pol: %hhu\r\n", NMIOvr->GlobalIrq, trig, pol);
    }
    else if(TypeLength->Type == ACPI_MADT_TYPE_LOCAL_APIC_NMI)
    {
      // The LINT value is global/applies to all CPUs if ProcessorId is 0xFF
      ACPI_MADT_LOCAL_APIC_NMI * LapicNMI = (ACPI_MADT_LOCAL_APIC_NMI*)TypeLength;
      uint8_t pol = LapicNMI->IntiFlags & 0x03;
      uint8_t trig = (LapicNMI->IntiFlags & 0x0C) >> 2;

      printf("LAPIC NMI: CPU %hhu, LINTn: %hhu, Trig: %hhu, Pol: %hhu\r\n", LapicNMI->ProcessorId, LapicNMI->Lint, trig, pol);
    }
    else if(TypeLength->Type == ACPI_MADT_TYPE_LOCAL_APIC_OVERRIDE)
    {
      ACPI_MADT_LOCAL_APIC_OVERRIDE * Lapic64Addr = (ACPI_MADT_LOCAL_APIC_OVERRIDE*)TypeLength;

      LapicAddress = Lapic64Addr->Address;

      printf("CPU LAPIC address changed to 64-bit address: %#qx\r\n", LapicAddress);
    }
    else if(TypeLength->Type == ACPI_MADT_TYPE_IO_SAPIC)
    {
      error_printf("I/O SAPIC found. IA64 unsupported. ");
      info_printf("The Itanic sunk long ago...\r\n");
    }
    else if(TypeLength->Type == ACPI_MADT_TYPE_LOCAL_SAPIC)
    {
      error_printf("Local SAPIC found. IA64 unsupported. ");
      info_printf("Impressive that you even made it this far.\r\n");
    }
    else if(TypeLength->Type == ACPI_MADT_TYPE_INTERRUPT_SOURCE)
    {
      error_printf("I/O SAPIC Interrupt Source found. IA64 unsupported. ");
      info_printf("That's quite the iceberg.\r\n");
    }
    else if(TypeLength->Type == ACPI_MADT_TYPE_LOCAL_X2APIC)
    {
      Numcores++; // For more than 255 CPUs
      ACPI_MADT_LOCAL_X2APIC * x2CoreLapic = (ACPI_MADT_LOCAL_X2APIC*)TypeLength;

      printf("CPU %u: x2LAPIC ID: %u, Flags: %u\r\n", x2CoreLapic->Uid, x2CoreLapic->LocalApicId, x2CoreLapic->LapicFlags);
    }
    else if(TypeLength->Type == ACPI_MADT_TYPE_LOCAL_X2APIC_NMI)
    {
      // The global LINT from this type overrides that in the Local APIC NMI type for all CPUs if it exists
      // Including those with Uid < 255, which otherwise still need to be described by the non-x2 Local APIC NMI
      // The LINT is global if the Uid is 0xFFFFFFFF
      ACPI_MADT_LOCAL_X2APIC_NMI * x2LapicNMI = (ACPI_MADT_LOCAL_X2APIC_NMI*)TypeLength;
      uint8_t pol = x2LapicNMI->IntiFlags & 0x03;
      uint8_t trig = (x2LapicNMI->IntiFlags & 0x0C) >> 2;

      printf("x2LAPIC NMI: CPU %u, LINTn: %hhu, Trig: %hhu, Pol: %hhu\r\n", x2LapicNMI->Uid, x2LapicNMI->Lint, trig, pol);
    }
    else if(TypeLength->Type == ACPI_MADT_TYPE_GENERIC_INTERRUPT)
    {
      error_printf("This is an ARM-specific type: %hhu.\r\n", TypeLength->Type);
    }
    else if(TypeLength->Type == ACPI_MADT_TYPE_GENERIC_DISTRIBUTOR)
    {
      error_printf("This is an ARM-specific type: %hhu.\r\n", TypeLength->Type);
    }
    else if(TypeLength->Type == ACPI_MADT_TYPE_GENERIC_MSI_FRAME)
    {
      error_printf("This is an ARM-specific type: %hhu.\r\n", TypeLength->Type);
    }
    else if(TypeLength->Type == ACPI_MADT_TYPE_GENERIC_REDISTRIBUTOR)
    {
      error_printf("This is an ARM-specific type: %hhu.\r\n", TypeLength->Type);
    }
    else if(TypeLength->Type == ACPI_MADT_TYPE_GENERIC_TRANSLATOR)
    {
      error_printf("This is an ARM-specific type: %hhu.\r\n", TypeLength->Type);
    }
    else
    {
      printf("Unknown MADT/APIC Table Type: %hhu.\r\n", TypeLength->Type);
    }
  }

  // TODO now setup APICs

  ACPI_OBJECT_LIST        ObjList;
  ACPI_OBJECT             Obj;

  ObjList.Count = 1;
  ObjList.Pointer = &Obj;
  Obj.Type = ACPI_TYPE_INTEGER;
  Obj.Integer.Value = 1; // For control method _PIC, legacy PIC is 0 and APIC is 1, SAPIC is 2 (SAPICs are only for Itanium/IA64 system types)

  APICStatus = AcpiEvaluateObject(ACPI_ROOT_OBJECT, "_PIC", &ObjList, NULL); // _PIC has no return values
  if(ACPI_FAILURE(APICStatus))
  {
    warning_printf("ACPI failed to set _PIC to APIC mode. %#x\r\n", APICStatus);
  }
  else
  {
    printf("ACPI APIC mode set.\r\n");
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// ACPI_Shutdown: Shut Down via ACPI
//----------------------------------------------------------------------------------------------------------------------------------
//
// ACPI_Shutdown puts the system into S5. Many systems don't use EFI_RESET_SYSTEM and instead rely on ACPI to perform the shutdown
// sequence. ACPI doesn't actually need to be fully initialized to use this, as the ACPI shutdown process is simple enough not to
// need it.
//
// This function is defined here, declared in Kernel64.h, and called in Kernel64.c
//

void ACPI_Shutdown(void)
{
  if(AcpiGbl_ReducedHardware)
  {
    warning_printf("ACPI reduced hardware machine, please use UEFI shutdown instead.\r\n");
  }
  //  printf("Entering ACPI S5 state (shutting down...)\r\n");

  ACPI_STATUS Acpi_Sleep_Status = AcpiEnterSleepStatePrep(ACPI_STATE_S5); // This handles all the TypeA and TypeB stuff
  if(ACPI_SUCCESS(Acpi_Sleep_Status)) // We have S5
  {
    // ACPI S5 method
    asm volatile ("cli");
    AcpiEnterSleepState(ACPI_STATE_S5); // Should shut down here.
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// ACPI_Reboot: Reboot via ACPI
//----------------------------------------------------------------------------------------------------------------------------------
//
// ACPI_Reboot restarts the system. Many systems don't use EFI_RESET_SYSTEM and instead rely on ACPI to perform shutdown sequences
// ACPI doesn't actually need to be fully initialized to use this, as the process is simple enough not to need it.
//
// This function is defined here and declared in Kernel64.h.
//

void ACPI_Reboot(void)
{
  if(AcpiGbl_ReducedHardware)
  {
    warning_printf("ACPI reduced hardware machine, please use UEFI reboot instead.\r\n");
  }

//  printf("Entering ACPI Reboot...\r\n");

  asm volatile ("cli"); // Clear interrupts

  ACPI_STATUS Acpi_Sleep_Status = AcpiReset();
  if(ACPI_SUCCESS(Acpi_Sleep_Status))
  {
    ssleep(1); // Give it a second before timing out
    warning_printf("ACPI Reboot timed out.\r\n");
  }
  // So try UEFI's EfiResetWarm/EfiResetCold instead.
}

// This is but a pipe dream at the moment...

//----------------------------------------------------------------------------------------------------------------------------------
// ACPI_Standby: Standby via ACPI
//----------------------------------------------------------------------------------------------------------------------------------
//
// ACPI_Standby puts the system into a sleep state, if supported. Sleep states are S0 (working), S1, S2, S3. Many systems only
// support S3 (standby), S4 (hibernate), and S5 (shutdown). This function handles S1, S2, and S3.
//
// NOTE: DOES NOT WORK: No idea what state it'll leave the system in if it runs, so it's been disabled.
//
// See Ch. 16 of the ACPI Specification, Version 6.3
//

/*

void ACPI_Standby(uint8_t SleepState)
{
  if((SleepState < 1) || (SleepState > 3))
  {
    error_printf("Invalid sleep state: S%hhu. Only S1, S2, & S3 allowed.\r\n", SleepState);
    return ;
  }

  ACPI_STATUS Acpi_Sleep_Status;

  printf("Entering ACPI S%hhu state \r\n", SleepState);

  ACPI_OBJECT_LIST        ObjList;
  ACPI_OBJECT             Obj;

  ObjList.Count = 1;
  ObjList.Pointer = &Obj;
  Obj.Type = ACPI_TYPE_INTEGER;
  Obj.Integer.Value = SleepState;

  Acpi_Sleep_Status = AcpiEvaluateObject(NULL, "\\_TTS", &ObjList, NULL);
  if(ACPI_FAILURE(Acpi_Sleep_Status) && (Acpi_Sleep_Status != AE_NOT_FOUND))
  {
    warning_printf("TTS S%hhu phase warning. %#x\r\n", SleepState, Acpi_Sleep_Status); // Linux notes TTS failures are actually fine and it ignores them outright...
  }

  // Need to walk the ACPI namespace with AcpiWalkNamespace and evaluate _PRW methods
  // Need to put all devices into D3, except any that are intended to wake the system
  // _PSW/_DSW are methods to look at here
  // Need to set AcpiSetFirmwareWakingVector()
  // Need to save entire machine state to RAM

  Acpi_Sleep_Status = AcpiEnterSleepStatePrep(SleepState); // This handles all the TypeA and TypeB stuff
  if(ACPI_SUCCESS(Acpi_Sleep_Status)) // We have SleepState
  {
    asm volatile("cli");

    Acpi_Sleep_Status = AcpiEnterSleepState(SleepState); // Should enter standby here.
    if(ACPI_FAILURE(Acpi_Sleep_Status))
    {
      error_printf("Error going into S%hhu. %#x\r\n", SleepState, Acpi_Sleep_Status);
    }

    // In standby...

    // See line 3074 here for what this does: https://github.com/freebsd/freebsd/blob/master/sys/dev/acpica/acpi.c
    // So this is learned from FreeBSD, which got it from Linux, which got it from Windows. Nice!
    Acpi_Sleep_Status = AcpiWriteBitRegister(ACPI_BITREG_SCI_ENABLE, ACPI_ENABLE_EVENT);
    if(ACPI_FAILURE(Acpi_Sleep_Status))
    {
      warning_printf("Could not manually re-enable SCI. This might be problematic on some systems. %#x\r\n", Acpi_Sleep_Status);
    }

    ACPI_EVENT_STATUS power_button = 1;

    // Line 3095 here has some good documentation on ACPI behavior:
    // https://github.com/freebsd/freebsd/blob/master/sys/dev/acpica/acpi.c

    Acpi_Sleep_Status = AcpiGetEventStatus(ACPI_EVENT_POWER_BUTTON, &power_button);
    if(ACPI_SUCCESS(Acpi_Sleep_Status) && (power_button != 0))
    {
      Acpi_Sleep_Status = AcpiClearEvent(ACPI_EVENT_POWER_BUTTON);
      if(ACPI_FAILURE(Acpi_Sleep_Status))
      {
        error_printf("Failed to clear power button state. %#x\r\n", Acpi_Sleep_Status);
      }
    }
    else if(ACPI_FAILURE(Acpi_Sleep_Status))
    {
      error_printf("Failed to get power button state. %#x\r\n", Acpi_Sleep_Status);
    }

    // Then need these
    asm volatile("sti");

    Acpi_Sleep_Status = AcpiLeaveSleepStatePrep(SleepState);
    if(ACPI_SUCCESS(Acpi_Sleep_Status))
    {
      // Need to restore entire machine state from RAM
      // Need to walk the ACPI namespace with AcpiWalkNamespace and evaluate _PRW methods
      // Need to put all devices into D0
      // May need to clear AcpiSetFirmwareWakingVector()

      Acpi_Sleep_Status = AcpiLeaveSleepState(SleepState); // Should exit standby here
      if(ACPI_FAILURE(Acpi_Sleep_Status))
      {
        error_printf("Error leaving S%hhu. %#x\r\n", SleepState, Acpi_Sleep_Status);
      }

      Obj.Integer.Value = ACPI_STATE_S0;

      Acpi_Sleep_Status = AcpiEvaluateObject(NULL, "\\_TTS", &ObjList, NULL);
      if(ACPI_FAILURE(Acpi_Sleep_Status) && (Acpi_Sleep_Status != AE_NOT_FOUND))
      {
        warning_printf("TTS S0/resume phase warning. %#x\r\n", Acpi_Sleep_Status); // Linux notes TTS failures are actually fine and it ignores them outright...
      }

    }
    else
    {
      error_printf("Preparing to leave S%hhu failed. %#x\r\n", SleepState, Acpi_Sleep_Status);
    }
  }
  else
  {
    error_printf("Preparing to enter S%hhu failed. %#x\r\n", SleepState, Acpi_Sleep_Status);
  }
}

*/

//
// Turns out ACPI keeps track of running state with AcpiGbl_SystemAwakeAndRunning. Nice.
// Learned that this variable exists for this purpose from the FreeBSD source, which I recommend taking a look at.
// https://github.com/freebsd/freebsd/blob/master/sys/dev/acpica/acpi.c lays out what ACPI expects to happen when entering
// sleep modes, and it uses ACPICA functions (which is the same backend used here, too). Some of the things that need to be
// done would be almost impossible to figure out without looking at how bigger systems like Linux & FreeBSD work, e.g. for
// the manual re-enabling of SCI (that must have been a huge headache for the original discoverers of the issue to debug!!).
// The Linux S3 process has an overview here, for the curious: https://wiki.ubuntu.com/Kernel/Reference/S3
//
// Unfortunately, the ACPICA programming reference manual (Rev. 6.2) has a tendency to use functions that don't exist, like
// "AcpiNameToHandle()" in an example for using AcpiWalkNamespace(), so having reference points to see "Oh THAT'S what they
// mean" can be extremely useful. Linux & BSD-derivatives (including the open-source parts of Mac OS) are good sources to
// look at for that kind of clarification.
//
// Speaking of open-source, depending on the license, such code may even be used directly, as long as the license terms of
// the original source material are satisfied. Unfortunately it can get confusing sometimes when it comes to what licenses
// allow and don't allow: There are a few variations of the BSD license, for example, like 2-Clause, 3-Clause, 2-Clause +
// Patent, etc., and it's pretty important to pay attention to which one is being used since they aren't all the same,
// despite all carrying the "BSD" moniker.
//
// It's worth noting that this framework uses a variety of sources for different things, as can be seen in the
// LICENSE_KERNEL file, and Print.c is even BSD-licensed since it is just a heavily-modified version of FreeBSD's
// subr_prf.c, which adds a layer of license compatibility into the mix. Some licenses are flat out incompaitble with
// others, meaning code under one license can't be mixed with code under another. Yes, it's annoying, and it's also why I
// personally avoid GPL code because using even a little bit of GPL'd code means the ENTIRE PROJECT must now be GPL-
// licensed. So you'll only see BSD, MIT, and similarly-licensed code in this project if it's not original work (and it'll
// be clearly marked if it's not), as those are compatible licenses.
//
