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
  // That'll need AcpiGetTable() & AcpiPutTable()
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
