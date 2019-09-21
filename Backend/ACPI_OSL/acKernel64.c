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
// IMPORTANT NOTE:
// Do not use the following standard functions in this file, as they will get renamed and use the ACPI ones instead of the Kernel64
// ones. This is because ACPI uses its own built-in versions of these functions, which will cause a link-time conflict with other
// functions of the same name. The solution is to rename the ACPI ones with #define in acKernel64.h, and make sure acKernel64.h
// is not included in files other than this one (and acenv.h in the ACPI backend):
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

// Only for use with -DACPI_DEBUG_OUTPUT -DACPI_USE_DO_WHILE_0
//#define MAX_ACPI_DEBUG_OUTPUT

//
// ACPI has a lot of unused parameters. No need to fill the compile log with them since many are intentional.
//

#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "acpi.h"
#include "accommon.h"
#pragma GCC diagnostic warning "-Wunused-parameter"

#define ACKERNEL64
#include "Kernel64.h"

// I don't like unused variables, but we do need this here.
#define UNUSED(x) (void)x

#if(0)
//
// Force ACPI variables to data section... which means initializing them ALL here.
//

// Redefine macro for use here
#undef ACPI_GLOBAL
#define ACPI_GLOBAL(type,name) \
    type name={0}

#undef ACPI_INIT_GLOBAL
#define ACPI_INIT_GLOBAL(type,name,value) \
    type name=value

//
// acpixf.h variables:
//

ACPI_INIT_GLOBAL (UINT8,            AcpiGbl_EnableInterpreterSlack, FALSE);

ACPI_INIT_GLOBAL (UINT8,            AcpiGbl_AutoSerializeMethods, TRUE);

ACPI_INIT_GLOBAL (UINT8,            AcpiGbl_CreateOsiMethod, TRUE);

ACPI_INIT_GLOBAL (UINT8,            AcpiGbl_UseDefaultRegisterWidths, TRUE);

ACPI_INIT_GLOBAL (UINT8,            AcpiGbl_EnableTableValidation, TRUE);

ACPI_INIT_GLOBAL (UINT8,            AcpiGbl_EnableAmlDebugObject, FALSE);

ACPI_INIT_GLOBAL (UINT8,            AcpiGbl_CopyDsdtLocally, FALSE);

ACPI_INIT_GLOBAL (UINT8,            AcpiGbl_DoNotUseXsdt, FALSE);

ACPI_INIT_GLOBAL (UINT8,            AcpiGbl_Use32BitFadtAddresses, FALSE);

ACPI_INIT_GLOBAL (UINT8,            AcpiGbl_Use32BitFacsAddresses, TRUE);

ACPI_INIT_GLOBAL (UINT8,            AcpiGbl_TruncateIoAddresses, FALSE);

ACPI_INIT_GLOBAL (UINT8,            AcpiGbl_DisableAutoRepair, FALSE);

ACPI_INIT_GLOBAL (UINT8,            AcpiGbl_DisableSsdtTableInstall, FALSE);

ACPI_INIT_GLOBAL (UINT8,            AcpiGbl_RuntimeNamespaceOverride, TRUE);

ACPI_INIT_GLOBAL (UINT8,            AcpiGbl_OsiData, 0);

ACPI_INIT_GLOBAL (BOOLEAN,          AcpiGbl_ReducedHardware, FALSE);

ACPI_INIT_GLOBAL (UINT32,           AcpiGbl_MaxLoopIterations, ACPI_MAX_LOOP_TIMEOUT);

ACPI_INIT_GLOBAL (BOOLEAN,          AcpiGbl_IgnorePackageResolutionErrors, FALSE);

ACPI_INIT_GLOBAL (UINT32,           AcpiGbl_TraceFlags, 0);
ACPI_INIT_GLOBAL (const char *,     AcpiGbl_TraceMethodName, NULL);
ACPI_INIT_GLOBAL (UINT32,           AcpiGbl_TraceDbgLevel, ACPI_TRACE_LEVEL_DEFAULT);
ACPI_INIT_GLOBAL (UINT32,           AcpiGbl_TraceDbgLayer, ACPI_TRACE_LAYER_DEFAULT);

#ifdef ACPI_DEBUG_OUTPUT
ACPI_INIT_GLOBAL (UINT32,           AcpiDbgLevel, ACPI_DEBUG_DEFAULT);
#else
ACPI_INIT_GLOBAL (UINT32,           AcpiDbgLevel, ACPI_NORMAL_DEFAULT);
#endif
ACPI_INIT_GLOBAL (UINT32,           AcpiDbgLayer, ACPI_COMPONENT_DEFAULT);

ACPI_INIT_GLOBAL (UINT8,            AcpiGbl_DisplayDebugTimer, FALSE);

#ifdef ACPI_DEBUGGER
ACPI_INIT_GLOBAL (BOOLEAN,          AcpiGbl_MethodExecuting, FALSE);
ACPI_GLOBAL (char,                  AcpiGbl_DbLineBuf[ACPI_DB_LINE_BUFFER_SIZE]);
#endif

ACPI_GLOBAL (ACPI_TABLE_FADT,       AcpiGbl_FADT);
ACPI_GLOBAL (UINT32,                AcpiCurrentGpeCount);
ACPI_GLOBAL (BOOLEAN,               AcpiGbl_SystemAwakeAndRunning);

//
// acglobal.h variables:
//

ACPI_GLOBAL (ACPI_TABLE_LIST,           AcpiGbl_RootTableList);

ACPI_GLOBAL (ACPI_TABLE_HEADER *,       AcpiGbl_DSDT);
ACPI_GLOBAL (ACPI_TABLE_HEADER,         AcpiGbl_OriginalDsdtHeader);
ACPI_INIT_GLOBAL (UINT32,               AcpiGbl_DsdtIndex, ACPI_INVALID_TABLE_INDEX);
ACPI_INIT_GLOBAL (UINT32,               AcpiGbl_FacsIndex, ACPI_INVALID_TABLE_INDEX);
ACPI_INIT_GLOBAL (UINT32,               AcpiGbl_XFacsIndex, ACPI_INVALID_TABLE_INDEX);
ACPI_INIT_GLOBAL (UINT32,               AcpiGbl_FadtIndex, ACPI_INVALID_TABLE_INDEX);

#if (!ACPI_REDUCED_HARDWARE)
ACPI_GLOBAL (ACPI_TABLE_FACS *,         AcpiGbl_FACS);

#endif /* !ACPI_REDUCED_HARDWARE */

ACPI_GLOBAL (ACPI_GENERIC_ADDRESS,      AcpiGbl_XPm1aStatus);
ACPI_GLOBAL (ACPI_GENERIC_ADDRESS,      AcpiGbl_XPm1aEnable);

ACPI_GLOBAL (ACPI_GENERIC_ADDRESS,      AcpiGbl_XPm1bStatus);
ACPI_GLOBAL (ACPI_GENERIC_ADDRESS,      AcpiGbl_XPm1bEnable);

ACPI_GLOBAL (UINT8,                     AcpiGbl_IntegerBitWidth);
ACPI_GLOBAL (UINT8,                     AcpiGbl_IntegerByteWidth);
ACPI_GLOBAL (UINT8,                     AcpiGbl_IntegerNybbleWidth);

ACPI_GLOBAL (ACPI_MUTEX_INFO,           AcpiGbl_MutexInfo[ACPI_NUM_MUTEX]);

ACPI_GLOBAL (ACPI_OPERAND_OBJECT *,     AcpiGbl_GlobalLockMutex);
ACPI_GLOBAL (ACPI_SEMAPHORE,            AcpiGbl_GlobalLockSemaphore);
ACPI_GLOBAL (ACPI_SPINLOCK,             AcpiGbl_GlobalLockPendingLock);
ACPI_GLOBAL (UINT16,                    AcpiGbl_GlobalLockHandle);
ACPI_GLOBAL (BOOLEAN,                   AcpiGbl_GlobalLockAcquired);
ACPI_GLOBAL (BOOLEAN,                   AcpiGbl_GlobalLockPresent);
ACPI_GLOBAL (BOOLEAN,                   AcpiGbl_GlobalLockPending);

ACPI_GLOBAL (ACPI_SPINLOCK,             AcpiGbl_GpeLock);       /* For GPE data structs and registers */
ACPI_GLOBAL (ACPI_SPINLOCK,             AcpiGbl_HardwareLock);  /* For ACPI H/W except GPE registers */
ACPI_GLOBAL (ACPI_SPINLOCK,             AcpiGbl_ReferenceCountLock);

ACPI_GLOBAL (ACPI_MUTEX,                AcpiGbl_OsiMutex);

ACPI_GLOBAL (ACPI_RW_LOCK,              AcpiGbl_NamespaceRwLock);

ACPI_GLOBAL (ACPI_CACHE_T *,            AcpiGbl_NamespaceCache);
ACPI_GLOBAL (ACPI_CACHE_T *,            AcpiGbl_StateCache);
ACPI_GLOBAL (ACPI_CACHE_T *,            AcpiGbl_PsNodeCache);
ACPI_GLOBAL (ACPI_CACHE_T *,            AcpiGbl_PsNodeExtCache);
ACPI_GLOBAL (ACPI_CACHE_T *,            AcpiGbl_OperandCache);

ACPI_INIT_GLOBAL (UINT32,               AcpiGbl_StartupFlags, 0);
ACPI_INIT_GLOBAL (BOOLEAN,              AcpiGbl_Shutdown, TRUE);
ACPI_INIT_GLOBAL (BOOLEAN,              AcpiGbl_EarlyInitialization, TRUE);

ACPI_GLOBAL (ACPI_GLOBAL_NOTIFY_HANDLER,AcpiGbl_GlobalNotify[2]);
ACPI_GLOBAL (ACPI_EXCEPTION_HANDLER,    AcpiGbl_ExceptionHandler);
ACPI_GLOBAL (ACPI_INIT_HANDLER,         AcpiGbl_InitHandler);
ACPI_GLOBAL (ACPI_TABLE_HANDLER,        AcpiGbl_TableHandler);
ACPI_GLOBAL (void *,                    AcpiGbl_TableHandlerContext);
ACPI_GLOBAL (ACPI_INTERFACE_HANDLER,    AcpiGbl_InterfaceHandler);
ACPI_GLOBAL (ACPI_SCI_HANDLER_INFO *,   AcpiGbl_SciHandlerList);

ACPI_GLOBAL (UINT32,                    AcpiGbl_OwnerIdMask[ACPI_NUM_OWNERID_MASKS]);
ACPI_GLOBAL (UINT8,                     AcpiGbl_LastOwnerIdIndex);
ACPI_GLOBAL (UINT8,                     AcpiGbl_NextOwnerIdOffset);

ACPI_INIT_GLOBAL (BOOLEAN,              AcpiGbl_NamespaceInitialized, FALSE);

ACPI_GLOBAL (UINT32,                    AcpiGbl_OriginalMode);
ACPI_GLOBAL (UINT32,                    AcpiGbl_NsLookupCount);
ACPI_GLOBAL (UINT32,                    AcpiGbl_PsFindCount);
ACPI_GLOBAL (UINT16,                    AcpiGbl_Pm1EnableRegisterSave);
ACPI_GLOBAL (UINT8,                     AcpiGbl_DebuggerConfiguration);
ACPI_GLOBAL (BOOLEAN,                   AcpiGbl_StepToNextCall);
ACPI_GLOBAL (BOOLEAN,                   AcpiGbl_AcpiHardwarePresent);
ACPI_GLOBAL (BOOLEAN,                   AcpiGbl_EventsInitialized);
ACPI_GLOBAL (ACPI_INTERFACE_INFO *,     AcpiGbl_SupportedInterfaces);
ACPI_GLOBAL (ACPI_ADDRESS_RANGE *,      AcpiGbl_AddressRangeList[ACPI_ADDRESS_RANGE_MAX]);

#ifdef ACPI_DBG_TRACK_ALLOCATIONS
ACPI_GLOBAL (ACPI_MEMORY_LIST *,        AcpiGbl_GlobalList);
ACPI_GLOBAL (ACPI_MEMORY_LIST *,        AcpiGbl_NsNodeList);
ACPI_GLOBAL (BOOLEAN,                   AcpiGbl_DisplayFinalMemStats);
ACPI_GLOBAL (BOOLEAN,                   AcpiGbl_DisableMemTracking);
ACPI_GLOBAL (BOOLEAN,                   AcpiGbl_VerboseLeakDump);
#endif

ACPI_GLOBAL (ACPI_NAMESPACE_NODE,       AcpiGbl_RootNodeStruct);
ACPI_GLOBAL (ACPI_NAMESPACE_NODE *,     AcpiGbl_RootNode);
ACPI_GLOBAL (ACPI_NAMESPACE_NODE *,     AcpiGbl_FadtGpeDevice);

#ifdef ACPI_DEBUG_OUTPUT
ACPI_GLOBAL (UINT32,                    AcpiGbl_CurrentNodeCount);
ACPI_GLOBAL (UINT32,                    AcpiGbl_CurrentNodeSize);
ACPI_GLOBAL (UINT32,                    AcpiGbl_MaxConcurrentNodeCount);
ACPI_GLOBAL (ACPI_SIZE *,               AcpiGbl_EntryStackPointer);
ACPI_GLOBAL (ACPI_SIZE *,               AcpiGbl_LowestStackPointer);
ACPI_GLOBAL (UINT32,                    AcpiGbl_DeepestNesting);
ACPI_INIT_GLOBAL (UINT32,               AcpiGbl_NestingLevel, 0);
#endif

ACPI_GLOBAL (UINT8,                     AcpiGbl_CmSingleStep);
ACPI_GLOBAL (ACPI_THREAD_STATE *,       AcpiGbl_CurrentWalkList);
ACPI_INIT_GLOBAL (ACPI_PARSE_OBJECT,   *AcpiGbl_CurrentScope, NULL);

ACPI_INIT_GLOBAL (BOOLEAN,              AcpiGbl_CaptureComments, FALSE);
ACPI_INIT_GLOBAL (ACPI_COMMENT_NODE,   *AcpiGbl_LastListHead, NULL);

ACPI_GLOBAL (UINT8,                     AcpiGbl_SleepTypeA);
ACPI_GLOBAL (UINT8,                     AcpiGbl_SleepTypeB);

#if (!ACPI_REDUCED_HARDWARE)
ACPI_GLOBAL (UINT8,                     AcpiGbl_AllGpesInitialized);
ACPI_GLOBAL (ACPI_GPE_XRUPT_INFO *,     AcpiGbl_GpeXruptListHead);
ACPI_GLOBAL (ACPI_GPE_BLOCK_INFO *,     AcpiGbl_GpeFadtBlocks[ACPI_MAX_GPE_BLOCKS]);
ACPI_GLOBAL (ACPI_GBL_EVENT_HANDLER,    AcpiGbl_GlobalEventHandler);
ACPI_GLOBAL (void *,                    AcpiGbl_GlobalEventHandlerContext);
ACPI_GLOBAL (ACPI_FIXED_EVENT_HANDLER,  AcpiGbl_FixedEventHandlers[ACPI_NUM_FIXED_EVENTS]);
extern ACPI_FIXED_EVENT_INFO            AcpiGbl_FixedEventInfo[ACPI_NUM_FIXED_EVENTS];
#endif /* !ACPI_REDUCED_HARDWARE */

ACPI_GLOBAL (UINT32,                    AcpiMethodCount);
ACPI_GLOBAL (UINT32,                    AcpiGpeCount);
ACPI_GLOBAL (UINT32,                    AcpiSciCount);
ACPI_GLOBAL (UINT32,                    AcpiFixedEventCount[ACPI_NUM_FIXED_EVENTS]);

ACPI_GLOBAL (UINT32,                    AcpiGbl_OriginalDbgLevel);
ACPI_GLOBAL (UINT32,                    AcpiGbl_OriginalDbgLayer);

ACPI_INIT_GLOBAL (UINT8,                AcpiGbl_DbOutputFlags, ACPI_DB_CONSOLE_OUTPUT);

#ifdef ACPI_DISASSEMBLER

ACPI_INIT_GLOBAL (UINT8,                AcpiGbl_NoResourceDisassembly, FALSE);
ACPI_INIT_GLOBAL (BOOLEAN,              AcpiGbl_IgnoreNoopOperator, FALSE);
ACPI_INIT_GLOBAL (BOOLEAN,              AcpiGbl_CstyleDisassembly, TRUE);
ACPI_INIT_GLOBAL (BOOLEAN,              AcpiGbl_ForceAmlDisassembly, FALSE);
ACPI_INIT_GLOBAL (BOOLEAN,              AcpiGbl_DmOpt_Verbose, TRUE);
ACPI_INIT_GLOBAL (BOOLEAN,              AcpiGbl_DmEmitExternalOpcodes, FALSE);
ACPI_INIT_GLOBAL (BOOLEAN,              AcpiGbl_DoDisassemblerOptimizations, TRUE);
ACPI_INIT_GLOBAL (ACPI_PARSE_OBJECT_LIST, *AcpiGbl_TempListHead, NULL);

ACPI_GLOBAL (BOOLEAN,                   AcpiGbl_DmOpt_Disasm);
ACPI_GLOBAL (BOOLEAN,                   AcpiGbl_DmOpt_Listing);
ACPI_GLOBAL (BOOLEAN,                   AcpiGbl_NumExternalMethods);
ACPI_GLOBAL (UINT32,                    AcpiGbl_ResolvedExternalMethods);
ACPI_GLOBAL (ACPI_EXTERNAL_LIST *,      AcpiGbl_ExternalList);
ACPI_GLOBAL (ACPI_EXTERNAL_FILE *,      AcpiGbl_ExternalFileList);
#endif

#ifdef ACPI_DEBUGGER
ACPI_INIT_GLOBAL (BOOLEAN,              AcpiGbl_AbortMethod, FALSE);
ACPI_INIT_GLOBAL (ACPI_THREAD_ID,       AcpiGbl_DbThreadId, ACPI_INVALID_THREAD_ID);

ACPI_GLOBAL (BOOLEAN,                   AcpiGbl_DbOpt_NoIniMethods);
ACPI_GLOBAL (BOOLEAN,                   AcpiGbl_DbOpt_NoRegionSupport);
ACPI_GLOBAL (BOOLEAN,                   AcpiGbl_DbOutputToFile);
ACPI_GLOBAL (char *,                    AcpiGbl_DbBuffer);
ACPI_GLOBAL (char *,                    AcpiGbl_DbFilename);
ACPI_GLOBAL (UINT32,                    AcpiGbl_DbDebugLevel);
ACPI_GLOBAL (UINT32,                    AcpiGbl_DbConsoleDebugLevel);
ACPI_GLOBAL (ACPI_NAMESPACE_NODE *,     AcpiGbl_DbScopeNode);
ACPI_GLOBAL (BOOLEAN,                   AcpiGbl_DbTerminateLoop);
ACPI_GLOBAL (BOOLEAN,                   AcpiGbl_DbThreadsTerminated);
ACPI_GLOBAL (char *,                    AcpiGbl_DbArgs[ACPI_DEBUGGER_MAX_ARGS]);
ACPI_GLOBAL (ACPI_OBJECT_TYPE,          AcpiGbl_DbArgTypes[ACPI_DEBUGGER_MAX_ARGS]);

ACPI_GLOBAL (char,                      AcpiGbl_DbParsedBuf[ACPI_DB_LINE_BUFFER_SIZE]);
ACPI_GLOBAL (char,                      AcpiGbl_DbScopeBuf[ACPI_DB_LINE_BUFFER_SIZE]);
ACPI_GLOBAL (char,                      AcpiGbl_DbDebugFilename[ACPI_DB_LINE_BUFFER_SIZE]);

ACPI_GLOBAL (UINT16,                    AcpiGbl_ObjTypeCount[ACPI_TOTAL_TYPES]);
ACPI_GLOBAL (UINT16,                    AcpiGbl_NodeTypeCount[ACPI_TOTAL_TYPES]);
ACPI_GLOBAL (UINT16,                    AcpiGbl_ObjTypeCountMisc);
ACPI_GLOBAL (UINT16,                    AcpiGbl_NodeTypeCountMisc);
ACPI_GLOBAL (UINT32,                    AcpiGbl_NumNodes);
ACPI_GLOBAL (UINT32,                    AcpiGbl_NumObjects);
#endif /* ACPI_DEBUGGER */

#if defined (ACPI_DISASSEMBLER) || defined (ACPI_ASL_COMPILER)
ACPI_GLOBAL (const char,               *AcpiGbl_PldPanelList[]);
ACPI_GLOBAL (const char,               *AcpiGbl_PldVerticalPositionList[]);
ACPI_GLOBAL (const char,               *AcpiGbl_PldHorizontalPositionList[]);
ACPI_GLOBAL (const char,               *AcpiGbl_PldShapeList[]);
ACPI_INIT_GLOBAL (BOOLEAN,              AcpiGbl_DisasmFlag, FALSE);
#endif

#ifdef ACPI_ASL_COMPILER
ACPI_INIT_GLOBAL (char *,               AcpiGbl_CurrentInlineComment, NULL);
ACPI_INIT_GLOBAL (char *,               AcpiGbl_CurrentEndNodeComment, NULL);
ACPI_INIT_GLOBAL (char *,               AcpiGbl_CurrentOpenBraceComment, NULL);
ACPI_INIT_GLOBAL (char *,               AcpiGbl_CurrentCloseBraceComment, NULL);

ACPI_INIT_GLOBAL (char *,               AcpiGbl_RootFilename, NULL);
ACPI_INIT_GLOBAL (char *,               AcpiGbl_CurrentFilename, NULL);
ACPI_INIT_GLOBAL (char *,               AcpiGbl_CurrentParentFilename, NULL);
ACPI_INIT_GLOBAL (char *,               AcpiGbl_CurrentIncludeFilename, NULL);

ACPI_INIT_GLOBAL (ACPI_COMMENT_NODE,   *AcpiGbl_DefBlkCommentListHead, NULL);
ACPI_INIT_GLOBAL (ACPI_COMMENT_NODE,   *AcpiGbl_DefBlkCommentListTail, NULL);
ACPI_INIT_GLOBAL (ACPI_COMMENT_NODE,   *AcpiGbl_RegCommentListHead, NULL);
ACPI_INIT_GLOBAL (ACPI_COMMENT_NODE,   *AcpiGbl_RegCommentListTail, NULL);
ACPI_INIT_GLOBAL (ACPI_COMMENT_NODE,   *AcpiGbl_IncCommentListHead, NULL);
ACPI_INIT_GLOBAL (ACPI_COMMENT_NODE,   *AcpiGbl_IncCommentListTail, NULL);
ACPI_INIT_GLOBAL (ACPI_COMMENT_NODE,   *AcpiGbl_EndBlkCommentListHead, NULL);
ACPI_INIT_GLOBAL (ACPI_COMMENT_NODE,   *AcpiGbl_EndBlkCommentListTail, NULL);

ACPI_INIT_GLOBAL (ACPI_COMMENT_ADDR_NODE, *AcpiGbl_CommentAddrListHead, NULL);
ACPI_INIT_GLOBAL (ACPI_FILE_NODE,      *AcpiGbl_FileTreeRoot, NULL);

ACPI_GLOBAL (ACPI_CACHE_T *,            AcpiGbl_RegCommentCache);
ACPI_GLOBAL (ACPI_CACHE_T *,            AcpiGbl_CommentAddrCache);
ACPI_GLOBAL (ACPI_CACHE_T *,            AcpiGbl_FileCache);

ACPI_INIT_GLOBAL (BOOLEAN,              AcpiGbl_DebugAslConversion, FALSE);
ACPI_INIT_GLOBAL (ACPI_FILE,            AcpiGbl_ConvDebugFile, NULL);
ACPI_GLOBAL (char,                      AcpiGbl_TableSig[4]);
#endif

#ifdef ACPI_APPLICATION
ACPI_INIT_GLOBAL (ACPI_FILE,            AcpiGbl_DebugFile, NULL);
ACPI_INIT_GLOBAL (ACPI_FILE,            AcpiGbl_OutputFile, NULL);
ACPI_INIT_GLOBAL (BOOLEAN,              AcpiGbl_DebugTimeout, FALSE);

ACPI_GLOBAL (ACPI_SPINLOCK,             AcpiGbl_PrintLock);     /* For print buffer */
ACPI_GLOBAL (char,                      AcpiGbl_PrintBuffer[1024]);
#endif /* ACPI_APPLICATION */

//
// End ACPI global init
//
#endif

// 9.1 Environmental and ACPI Tables
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

// 9.2 Memory Management
// Unneeded, as ACPI's built-in local cache is used.
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

 // Not implemented yet.. TODO
// 9.3 Multithreading and Scheduling Services
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
  if(Milliseconds)
  {
    // 0xCE is MSR_PLATFORM_INFO
    uint64_t max_non_turbo_ratio = (msr_rw(0xCE, 0, 0) & 0x000000000000FF00) >> 8; // Max non-turbo bus multiplier is in this byte
    // Want tsc_frequency in cycles/millisec
    // Isn't it nice that 1 MHz is the exact inverse of 1 usec?
    uint64_t tsc_frequency = max_non_turbo_ratio * 100ULL * 1000ULL; // 100 MHz bus for these CPUs, 133 MHz for Nehalem (but Nehalem doesn't have AVX)
    // Need it in units of Hz.

    if(!tsc_frequency)
    {
      // probably in a vm... So...
      tsc_frequency = 30ULL * 100ULL * 1000ULL; // Let's just say 3GHz, so 3 000 000 cycles/msec
    }

    uint64_t cycle_count = 0;
    uint64_t cycle_count_start = get_tick();
    while((cycle_count / tsc_frequency) < Milliseconds)
    {
      cycle_count = get_tick() - cycle_count_start;
    }
  }
}

void AcpiOsStall(UINT32 Microseconds)
{
  if(Microseconds)
  {
    // 0xCE is MSR_PLATFORM_INFO
    uint64_t max_non_turbo_ratio = (msr_rw(0xCE, 0, 0) & 0x000000000000FF00) >> 8; // Max non-turbo bus multiplier is in this byte
    // Want tsc_frequency in cycles/microsec
    // Isn't it nice that 1 MHz is the exact inverse of 1 usec?
    uint64_t tsc_frequency = max_non_turbo_ratio * 100ULL; // 100 MHz bus for these CPUs, 133 MHz for Nehalem (but Nehalem doesn't have AVX)
    // Need it in units of Hz.

    if(!tsc_frequency)
    {
      // probably in a vm... So...
      tsc_frequency = 30ULL * 100ULL; // Let's just say 3GHz, so 3000 cycles/usec
    }

    uint64_t cycle_count = 0;
    uint64_t cycle_count_start = get_tick();
    while((cycle_count / tsc_frequency) < Microseconds)
    {
      cycle_count = get_tick() - cycle_count_start;
    }
  }
}

void AcpiOsWaitEventsComplete(void)
{
  warning_printf("Unimplemented AcpiOsWaitEventsComplete called\r\n");
}

// 9.4 Mutual Exclusion and Synchronization

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

// 9.5 Interrupt Handling
ACPI_STATUS AcpiOsInstallInterruptHandler(UINT32 InterruptLevel, ACPI_OSD_HANDLER Handler, void *Context)
{
  // The User_ISR_Handler can use case statement to evaluate InterruptLevel, then execute HandlerAddress(Context)
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
  info_printf("ACPI using interrupt %u\r\n", InterruptLevel);

  Global_ACPI_Interrupt_Table[InterruptLevel].InterruptNumber = InterruptLevel;
  Global_ACPI_Interrupt_Table[InterruptLevel].HandlerPointer = Handler;
  Global_ACPI_Interrupt_Table[InterruptLevel].Context = Context;

  return AE_OK;
}

ACPI_STATUS AcpiOsRemoveInterruptHandler(UINT32 InterruptLevel, ACPI_OSD_HANDLER Handler)
{
  UNUSED(Handler);

  info_printf("ACPI no longer using interrupt %u\r\n", InterruptLevel);

  Global_ACPI_Interrupt_Table[InterruptLevel].InterruptNumber = 0;
  Global_ACPI_Interrupt_Table[InterruptLevel].HandlerPointer = 0;
  Global_ACPI_Interrupt_Table[InterruptLevel].Context = 0;

  return AE_OK;
}

// 9.6 Memory Access and Memory Mapped I/O
ACPI_STATUS AcpiOsReadMemory(ACPI_PHYSICAL_ADDRESS Address, UINT64 *Value, UINT32 Width)
{
  // This seems ok
  *Value = 0; // Spec says zero-extended
  if(Width == 8)
  {
    *Value = *(UINT8*)Address;
  }
  else if(Width == 16)
  {
    *Value = *(UINT16*)Address;
  }
  else if(Width == 32)
  {
    *Value = *(UINT32*)Address;
  }
  else if(Width == 64)
  {
    *Value = *(UINT64*)Address;
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
    *(UINT8*)Address = (UINT8)Value;
  }
  else if(Width == 16)
  {
    *(UINT16*)Address = (UINT16)Value;
  }
  else if(Width == 32)
  {
    *(UINT32*)Address = (UINT32)Value;
  }
  else if(Width == 64)
  {
    *(UINT64*)Address = Value;
  }
  else
  {
    return AE_BAD_PARAMETER;
  }

  return AE_OK;
}

// 9.7 Port Input/Output
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

// 9.8 PCI Configuration Space Access
// NOTE: The spec (Version 6.2 of the ACPI Component Architecture User Guide and Programmer Reference) has a typo:
// PciId should be *PciId
ACPI_STATUS AcpiOsReadPciConfiguration(ACPI_PCI_ID *PciId, UINT32 Register, UINT64 *Value, UINT32 Width)
{
  // Somewhere over the rainbow, MMIO is needed for PCIe Extended Config Space access
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

// 9.9 Formatted Output
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

  warning_printf("Unimplemented AcpiOsRedirectOutput called\r\n");
#endif
}

// 9.10 System ACPI Table Access
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
// 9.11 Miscellaneous
UINT64 AcpiOsGetTimer(void)
{
/* // This gives frequency in cycles/sec
  // 0xCE is MSR_PLATFORM_INFO
  uint64_t max_non_turbo_ratio = (msr_rw(0xCE, 0, 0) & 0x000000000000FF00) >> 8; // Max non-turbo bus multiplier is in this byte
  uint64_t tsc_frequency = max_non_turbo_ratio * 100ULL * 1000000ULL; // 100 MHz bus for these CPUs, 133 MHz for Nehalem (but Nehalem doesn't have AVX)
  // Need it in units of Hz.

  if(!tsc_frequency)
  {
    // probably in a vm... So...
    tsc_frequency = 30ULL * 100ULL * 1000000ULL; // Let's just say 3GHz
  }
*/
  // 0xCE is MSR_PLATFORM_INFO
  uint64_t max_non_turbo_ratio = (msr_rw(0xCE, 0, 0) & 0x000000000000FF00) >> 8; // Max non-turbo bus multiplier is in this byte
  // Want cycles per 100 nsec
  uint64_t tsc_frequency = max_non_turbo_ratio * 10ULL; // 100 MHz bus for these CPUs, 133 MHz for Nehalem (but Nehalem doesn't have AVX)
  // Need it in units of Hz.

  if(!tsc_frequency)
  {
    // probably in a vm... So...
    tsc_frequency = 30ULL * 10ULL; // Let's just say 3GHz, or 300 cycles / 100 nsec
  }

  uint64_t cycle_count = get_tick();
  // NOTE: Hope this doesn't break in hypervisors!

  return (cycle_count / tsc_frequency);
}

ACPI_STATUS AcpiOsSignal(UINT32 Function, void *Info)
{
  // Neither Windows nor Linux do anything here.
  UNUSED(Info);

  switch(Function)
  {
    case ACPI_SIGNAL_FATAL:
      break;
    case ACPI_SIGNAL_BREAKPOINT:
      break;
    default:
      break;
  }

  return AE_OK;
}

ACPI_STATUS AcpiOsGetLine(char *Buffer, UINT32 BufferLength, UINT32 *BytesRead)
{
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
ACPI_STATUS AcpiOsEnterSleep(UINT8 SleepState, UINT32 RegaValue, UINT32 RegbValue)
{
  UNUSED(SleepState);
  UNUSED(RegaValue);
  UNUSED(RegbValue);

  return AE_OK;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Custom Functions
//----------------------------------------------------------------------------------------------------------------------------------

//
// Init ACPI (Full)
//

// Main Init function, taken from Chapter 10.1.2.1 (Full ACPICA Initialization) of the ACPI Component Architecture User Guide and Programmer Reference, Revision 6.2
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

//
// Quit ACPI
//

// ACPI does not really need to be terminated once initialized, per Chapter 10.1.3 (Shutdown Sequence) in ACPI Component Architecture User Guide and Programmer Reference, Revision 6.2
// But if for some reason it does, this is how to do it.
ACPI_STATUS Quit_ACPI(void)
{
  return AcpiTerminate();
}

//
// Init Table Manager only
//

ACPI_STATUS InitializeAcpiTablesOnly(void)
{
  ACPI_STATUS Status;

  /* Initialize the ACPICA Table Manager and get all ACPI tables */
  Status = AcpiInitializeTables(NULL, 16, TRUE);

  return Status;
}

//
// Init Rest of ACPI after Table Manager
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

//
// ACPI shutdown
//

// ACPI_Shutdown puts the system into S5
// This function is defined here, declared in Kernel64.h, and called by kernel_main in Kernel64.c
void ACPI_Shutdown(void)
{
  printf("Entering ACPI S5 state (shutting down...)\r\n");
  ACPI_STATUS Acpi_Sleep_Status = AcpiEnterSleepStatePrep(ACPI_STATE_S5); // This handles all the TypeA and TypeB stuff
  if(ACPI_SUCCESS(Acpi_Sleep_Status)) // We have S5
  {
    // ACPI S5 method
    asm volatile ("cli");
    AcpiEnterSleepState(ACPI_STATE_S5); // Should shut down here.
  }
}

//
// ACPI Standby
//

// TODO: This probably won't work without full ACPI enabled
void ACPI_Standby(void)
{
  printf("Entering ACPI S3 state (standby...)\r\n");
  ACPI_STATUS Acpi_Sleep_Status = AcpiEnterSleepStatePrep(ACPI_STATE_S3); // This handles all the TypeA and TypeB stuff
  if(ACPI_SUCCESS(Acpi_Sleep_Status)) // We have S3
  {
    // ACPI S3 method
    asm volatile("cli");
    AcpiEnterSleepState(ACPI_STATE_S3); // Should enter standby here.
  }

  // Then need these
  asm volatile("sti");

  AcpiLeaveSleepStatePrep(ACPI_STATE_S3);

  AcpiLeaveSleepState(ACPI_STATE_S3);
}
