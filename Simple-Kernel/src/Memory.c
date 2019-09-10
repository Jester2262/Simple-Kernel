//==================================================================================================================================
//  Simple Kernel: Memory Functions
//==================================================================================================================================
//
// Version 0.9
//
// Author:
//  KNNSpeed
//
// Source Code:
//  https://github.com/KNNSpeed/Simple-Kernel
//
// This file contains memory-related functions. Derived from V1.4 & V2.2 of https://github.com/KNNSpeed/Simple-UEFI-Bootloader
//

#include "Kernel64.h"

#define MEMORY_CHECK_INFO

// AVX_memcmp and related functions in memcmp.c take care of memory comparisons now.
// AVX_memset zeroes things.

//----------------------------------------------------------------------------------------------------------------------------------
//  malloc: Allocate Physical Memory with Alignment
//----------------------------------------------------------------------------------------------------------------------------------
//
// Dynamically allocate physical memory aligned to the nearest suitable address alignment value
//
// IMPORTANT NOTE: This implementation of malloc behaves more like the standard "calloc(3)" in that returned memory is always both
// contiguous and zeroed. Large sizes are also supported; the limit is just how much contiguous memory the system has. A size of 0
// will return 1 UEFI page (4kB) instead of NULL, however, because 0x0 here is actually a valid address that can be used like any
// other (and calloc() will do the same). Also, because the UEFI memory map is quantized in 4kB pages, all allocated regions are
// rounded up to the next 4kB unit (see the description of AllocateFreeAddress() or VAllocateFreeAddress() for more details about
// this).
//
// Don't worry: something like void * ptr = malloc(256) will still work as expected--it's just that the pointer returned will always
// be for at least 4kB or a multiple of 4kB. Doing something like ptr = realloc(ptr, 512) on that will work fine, too. In fact, the
// only syntactical quirk requiring different treatment than "good ol' fashioned malloc/calloc/realloc" is the aformentioned handling
// of NULL. Instead of NULL, this malloc series returns addresses like ~0ULL, ~1ULL, ~2ULL that will guarantee a page fault if used.
//
// Return values of ~0ULL mean "out of memory" and ~1ULL mean "invalid byte alignment" (~1ULL will only occur if this function is
// modified in certain ways. As coded by default, this malloc will not naturally produce this return code on x86). A value of ~2ULL
// is returned by realloc()/vrealloc() if given a size of 0, and it indicates that free()/vfree() was run on the pointer. ~3ULL is
// returned by realloc()/vrealloc() and means "piece not found," where "piece" means "relevant memory map descriiptor."
//
// If one desires a specific alignment for a given amount of memory (like 1GB alignment for 512GB memory), call that mallocX function
// (for example malloc1GB()) directly instead of using "plain" malloc(). The plain malloc() changes the alignment used based on size.
// The size-alignment thresholds of the plain, automatic malloc are 4KB, 2MB, 1GB, 512GB, and 256TB.
//
// Also note that these available alignments match the sizes that x86-64 hardware pages can be. This is to facilitate coupling between
// memory allocation and hardware paging regions, should that be desired (i.e. if a region of dynamically allocated memory ought to
// be constrained by flags in the page table, like NX, G, etc., then malloc alignment needs to match hardware-supported sizes).
// Without modifying the paging structures accordingly, malloc will operate independently of paging mechanisms supported by the CPU
// while still providing the various alignments. Among other benefits, this hardware-software decoupling allows memory allocation
// to be portable across UEFI-supporting architectures that do not all support the same hardware paging sizes--including ones that
// do not support paging at all.
//
// In order to couple an allocated region to a hardware page, see set_region_hwpages().
//

void * malloc(size_t numbytes)
{
  if(numbytes < (2ULL << 20)) // < 2MB
  {
    return malloc4KB(numbytes); // 4kB-aligned
  }
  else if(numbytes < (1ULL << 30)) // < 1GB
  {
    return malloc2MB(numbytes); // 2MB-aligned
  }
  else if(numbytes < (512ULL << 30)) // < 512GB
  {
    return malloc1GB(numbytes); // 1GB-aligned
  }
  else if(numbytes < (256ULL << 40)) // < 256TB
  {
    return malloc512GB(numbytes); // 512GB-aligned
  }
  else // > 256TB
  {
    return malloc256TB(numbytes); // 256TB-aligned
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
//  mallocX: Allocate Physical Memory Aligned to X Bytes
//----------------------------------------------------------------------------------------------------------------------------------
//
// Each of these allocate bytes at physical addresses aligned on X-byte boundaries (X in mallocX). Otherwise, same rules as above.
//

void * malloc4KB(size_t numbytes)
{
  EFI_PHYSICAL_ADDRESS new_buffer = 0; // Make this 0x100000000 to only operate above 4GB

  new_buffer = AllocateFreeAddress(numbytes, new_buffer, (4ULL << 10));

  return (void*)new_buffer;
}

void * malloc2MB(size_t numbytes)
{
  EFI_PHYSICAL_ADDRESS new_buffer = 0; // Make this 0x100000000 to only operate above 4GB

  new_buffer = AllocateFreeAddress(numbytes, new_buffer, (2ULL << 20));

  return (void*)new_buffer;
}

void * malloc1GB(size_t numbytes)
{
  EFI_PHYSICAL_ADDRESS new_buffer = 0; // Make this 0x100000000 to only operate above 4GB

  new_buffer = AllocateFreeAddress(numbytes, new_buffer, (1ULL << 30));

  return (void*)new_buffer;
}

void * malloc512GB(size_t numbytes)
{
  EFI_PHYSICAL_ADDRESS new_buffer = 0; // Make this 0x100000000 to only operate above 4GB

  new_buffer = AllocateFreeAddress(numbytes, new_buffer, (512ULL << 30));

  return (void*)new_buffer;
}

void * malloc256TB(size_t numbytes)
{
  EFI_PHYSICAL_ADDRESS new_buffer = 0; // Make this 0x100000000 to only operate above 4GB

  new_buffer = AllocateFreeAddress(numbytes, new_buffer, (256ULL << 40));

  return (void*)new_buffer;
}

//----------------------------------------------------------------------------------------------------------------------------------
//  calloc: Allocate Physical Memory with Alignment
//----------------------------------------------------------------------------------------------------------------------------------
//
// Dynamically allocate physical memory aligned to the nearest suitable address alignment value
//
// This is just an alias for malloc that takes the calloc(3) syntax.
//
// elements: number of elements in array
// size: size of each element
//

void * calloc(size_t elements, size_t size)
{
  size_t total_size = elements * size; // This is fine if size_t is 64-bits, which is true of all intended targets of this framework.
  void * new_buffer = malloc(total_size);

  return new_buffer;
}

//----------------------------------------------------------------------------------------------------------------------------------
//  realloc: Reallocate Physical Memory with Alignment
//----------------------------------------------------------------------------------------------------------------------------------
//
// Dynamically reallocate memory for an existing pointer from malloc, and free the old region (if moved)
//
// allocated_address: The pointer allocated from malloc
// size: The new desired size
//
// NOTE: Unlike realloc(3), NULL pointer here is actually a valid address at 0x0. Therefore passing in a NULL pointer as
// allocated_address is the same as passing in address 0x0, which could be an actual allocated region. So don't do it.
// Passing in a size of 0, however, will cause free() to be run and will result in a return address of ~2ULL.
//

void * realloc(void * allocated_address, size_t size)
{
  if(size == 0)
  {
    free(allocated_address);
    return ((void*) ~2ULL);
  }

  EFI_MEMORY_DESCRIPTOR * Piece;

  size_t numpages = EFI_SIZE_TO_PAGES(size);
  size_t orig_numpages = 0;

  // Check for malloc in the map, we need the old size
  for(Piece = Global_Memory_Info.MemMap; Piece < (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize))
  {
    // Within each compatible malloc type, check the address
    if((Piece->Type == (EfiMaxMemoryType + 1)) && ((uint8_t*)Piece->PhysicalStart == (uint8_t*)allocated_address))
    {
      // Found it
      orig_numpages = Piece->NumberOfPages;

      if(numpages > orig_numpages) // Grow
      { // Can the malloc area be expanded into adjacent EfiConventionalMemory?
        // Is the next piece an EfiConventionalMemory region?

        size_t additional_numpages = numpages - orig_numpages;
        // If the area right after the malloc region is EfiConventionalMemory, we might be able to just take some pages from there...

        // Check if there's an EfiConventionalMemory region adjacent in memory to the memmap region
        EFI_MEMORY_DESCRIPTOR * Next_Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize); // Remember... sizeof(EFI_MEMORY_DESCRIPTOR) != MemMapDescriptorSize :/
        EFI_PHYSICAL_ADDRESS PhysicalEnd = Piece->PhysicalStart + (Piece->NumberOfPages << EFI_PAGE_SHIFT); // Get the end of this range, which may be the start of another range

        // Quick check for adjacency that will skip scanning the whole memmap
        // Can provide a little speed boost for ordered memory maps
        if(
            (Next_Piece->PhysicalStart != PhysicalEnd)
            ||
            ((uint8_t*)Next_Piece == ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize))
          ) // This OR check protects against the obscure case of there being a rogue entry adjacent the end of the memory map that describes a valid EfiConventionalMemory entry
        {
          // See if PhysicalEnd matches any PhysicalStart for unordered maps
          for(Next_Piece = Global_Memory_Info.MemMap; Next_Piece < (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Next_Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Next_Piece + Global_Memory_Info.MemMapDescriptorSize))
          {
            if(PhysicalEnd == Next_Piece->PhysicalStart)
            {
              // Found one
              break;
            }
          }
        }

        // Is the next piece an EfiConventionalMemory type?
        if(
            (Next_Piece->Type == EfiConventionalMemory) && (Next_Piece->NumberOfPages >= additional_numpages)
            &&
            (uint8_t*)Next_Piece != ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize)
          ) // Check if the loop didn't break (this also covers the case where the memory map is the last valid entry in the map)
        {
          if(Next_Piece->NumberOfPages > additional_numpages)
          {
            // Modify MemMap's entry
            Piece->NumberOfPages = numpages;

            // Modify adjacent EfiConventionalMemory's entry
            Next_Piece->NumberOfPages -= additional_numpages;
            Next_Piece->PhysicalStart += (additional_numpages << EFI_PAGE_SHIFT);
            Next_Piece->VirtualStart += (additional_numpages << EFI_PAGE_SHIFT);

            // Done
          }
          else if(Next_Piece->NumberOfPages == additional_numpages)
          { // If the next piece is exactly the number of pages, we can claim it and reclaim a descriptor
            // This just means that MemMap will have some wiggle room for the next time it needs to be modified.

            // Modify MemMap's entry
            Piece->NumberOfPages = numpages;

            // Erase the claimed descriptor
            // Zero out the piece we just claimed
            AVX_memset(Next_Piece, 0, Global_Memory_Info.MemMapDescriptorSize); // Next_Piece is a pointer
            AVX_memmove(Next_Piece, (uint8_t*)Next_Piece + Global_Memory_Info.MemMapDescriptorSize, ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize) - ((uint8_t*)Next_Piece + Global_Memory_Info.MemMapDescriptorSize));

            // Update Global_Memory_Info
            Global_Memory_Info.MemMapSize -= Global_Memory_Info.MemMapDescriptorSize;

            // Zero out the entry that used to be at the end of the map
            AVX_memset((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize, 0, Global_Memory_Info.MemMapDescriptorSize);

            // Done
          }
          // There is no else, as there are only > and == cases.
          else
          {
            error_printf("realloc: What kind of sorcery is this? Seeing this means there's a bug in realloc.\r\n");
            // And also probably MemMap_Prep()...
            HaCF();
          }
        }
        else // Nope, need to move it altogether. But that's really easy to do for malloc. :)
        {
          // Get a new address
          void * new_address = malloc(size);
          if((EFI_PHYSICAL_ADDRESS)new_address == ~0ULL)
          {
            error_printf("realloc: Insufficient free memory, could not reallocate increased size.\r\n");
            return new_address; // Better to return this invalid address than to return allocated_address
          }
          //
          // NOTE: Any function call that includes a call to MemMap_Prep() renders "Piece" unusable afterwards.
          // This is because it can no longer be guaranteed that the memory map is in the same place as before;
          // MemMap_Prep() might have changed its location.
          //

          // Move the old memory to the new area
          AVX_memmove(new_address, allocated_address, (orig_numpages << EFI_PAGE_SHIFT)); // Need size in bytes

          // Free the old address
          free(allocated_address);

          // Done
          return new_address; // This is one of the few times an early return is used instead of break outside of an error
        }
        // Grow done
      }
      else if(numpages < orig_numpages) // Shrink
      {
        // Is the next piece an EfiConventionalMemory region?
        // If the area right after the malloc region is EfiConventionalMemory, we might be able to just give it the freed pages
        size_t freedpages = orig_numpages - numpages;

        // Check if there's an EfiConventionalMemory region adjacent in memory to the malloc region
        EFI_MEMORY_DESCRIPTOR * Next_Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize); // Remember... sizeof(EFI_MEMORY_DESCRIPTOR) != MemMapDescriptorSize :/
        EFI_PHYSICAL_ADDRESS PhysicalEnd = Piece->PhysicalStart + (Piece->NumberOfPages << EFI_PAGE_SHIFT); // Get the end of this range, which may be the start of another range

        // Quick check for adjacency that will skip scanning the whole memmap
        // Can provide a little speed boost for ordered memory maps
        if(
            (Next_Piece->PhysicalStart != PhysicalEnd)
            ||
            ((uint8_t*)Next_Piece == ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize))
          ) // This OR check protects against the obscure case of there being a rogue entry adjacent the end of the memory map that describes a valid EfiConventionalMemory entry
        {
          // See if PhysicalEnd matches any PhysicalStart for unordered maps
          for(Next_Piece = Global_Memory_Info.MemMap; Next_Piece < (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Next_Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Next_Piece + Global_Memory_Info.MemMapDescriptorSize))
          {
            if(PhysicalEnd == Next_Piece->PhysicalStart)
            {
              // Found one
              break;
            }
          }
        }

        // Is the next piece an EfiConventionalMemory type?
        if(
            (Next_Piece->Type == EfiConventionalMemory)
            &&
            (uint8_t*)Next_Piece != ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize)
          ) // Check if the loop didn't break (this also covers the case where the memory map is the last valid entry in the map)
        { // Yes, we can reclaim without requiring a new entry

          // Modify malloc area's entry
          Piece->NumberOfPages = numpages;

          // Modify adjacent EfiConventionalMemory's entry
          Next_Piece->NumberOfPages += freedpages;
          Next_Piece->PhysicalStart -= (freedpages << EFI_PAGE_SHIFT);
          Next_Piece->VirtualStart -= (freedpages << EFI_PAGE_SHIFT);

          // Done. Nice.
        }
        // No, we need a new memmap entry, which will require a new page if the last entry is on a page edge or would spill over a page edge. Better to be safe then sorry!
        // First, maybe there's room for another descriptor in the last page
        else if((Global_Memory_Info.MemMapSize + Global_Memory_Info.MemMapDescriptorSize) <= (numpages << EFI_PAGE_SHIFT))
        { // Yes, we can reclaim and fit in another descriptor

          // Make a temporary descriptor to hold current malloc entry's values
          EFI_MEMORY_DESCRIPTOR new_descriptor_temp;
          new_descriptor_temp.Type = Piece->Type; // Special malloc type
          new_descriptor_temp.Pad = Piece->Pad;
          new_descriptor_temp.PhysicalStart = Piece->PhysicalStart;
          new_descriptor_temp.VirtualStart = Piece->VirtualStart;
          new_descriptor_temp.NumberOfPages = numpages; // New size of malloc entry
          new_descriptor_temp.Attribute = Piece->Attribute;

          // Modify the descriptor-to-move
          Piece->Type = EfiConventionalMemory;
          // No pad change
          Piece->PhysicalStart += (numpages << EFI_PAGE_SHIFT);
          Piece->VirtualStart += (numpages << EFI_PAGE_SHIFT);
          Piece->NumberOfPages = freedpages;
          // No attribute change

          // Move (copy) the whole memmap that's above this piece (including this freshly modified piece) from this piece to one MemMapDescriptorSize over
          AVX_memmove((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize, Piece, ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize) - (uint8_t*)Piece); // Pointer math to get size

          // Insert the new piece (by overwriting the now-duplicated entry with new values)
          // I.e. turn this piece into what was stored in the temporary descriptor above
          Piece->Type = new_descriptor_temp.Type;
          Piece->Pad = new_descriptor_temp.Pad;
          Piece->PhysicalStart = new_descriptor_temp.PhysicalStart;
          Piece->VirtualStart = new_descriptor_temp.VirtualStart;
          Piece->NumberOfPages = new_descriptor_temp.NumberOfPages;
          Piece->Attribute = new_descriptor_temp.Attribute;

          // Update Global_Memory_Info MemMap size
          Global_Memory_Info.MemMapSize += Global_Memory_Info.MemMapDescriptorSize;

          // Done
        }
        // No, it would spill over to a new page
        else
        {
          // Do we have more than a descriptor's worth of pages reclaimable?
          size_t pages_per_memory_descriptor = EFI_SIZE_TO_PAGES(Global_Memory_Info.MemMapDescriptorSize);

          if((numpages + pages_per_memory_descriptor) < orig_numpages)
          { // Yes, so we can hang on to one [set] of them and make a new EfiConventionalMemory entry for the rest.
            freedpages -= pages_per_memory_descriptor;

            // Make a temporary descriptor to hold current malloc entry's values
            EFI_MEMORY_DESCRIPTOR new_descriptor_temp;
            new_descriptor_temp.Type = Piece->Type; // Special malloc type
            new_descriptor_temp.Pad = Piece->Pad;
            new_descriptor_temp.PhysicalStart = Piece->PhysicalStart;
            new_descriptor_temp.VirtualStart = Piece->VirtualStart;
            new_descriptor_temp.NumberOfPages = numpages + pages_per_memory_descriptor; // New size of malloc entry
            new_descriptor_temp.Attribute = Piece->Attribute;

            // Modify the descriptor-to-move
            Piece->Type = EfiConventionalMemory;
            // No pad change
            Piece->PhysicalStart += ((numpages + pages_per_memory_descriptor) << EFI_PAGE_SHIFT);
            Piece->VirtualStart += ((numpages + pages_per_memory_descriptor) << EFI_PAGE_SHIFT);
            Piece->NumberOfPages = freedpages;
            // No attribute change

            // Move (copy) the whole memmap that's above this piece (including this freshly modified piece) from this piece to one MemMapDescriptorSize over
            AVX_memmove((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize, Piece, ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize) - (uint8_t*)Piece); // Pointer math to get size

            // Insert the new piece (by overwriting the now-duplicated entry with new values)
            // I.e. turn this piece into what was stored in the temporary descriptor above
            Piece->Type = new_descriptor_temp.Type;
            Piece->Pad = new_descriptor_temp.Pad;
            Piece->PhysicalStart = new_descriptor_temp.PhysicalStart;
            Piece->VirtualStart = new_descriptor_temp.VirtualStart;
            Piece->NumberOfPages = new_descriptor_temp.NumberOfPages;
            Piece->Attribute = new_descriptor_temp.Attribute;

            // Update Global_Memory_Info MemMap size
            Global_Memory_Info.MemMapSize += Global_Memory_Info.MemMapDescriptorSize;
          }
          // No, only 1 [set of] page(s) was reclaimable and adding another entry would spill over. So don't do anything then and hang on to the extra empty page(s).
        }
        // Shrink done
      }
      // else:
      // Nothing to be done if equal.

      break;
    } // End "found it"
  } // End for

#ifdef MEMORY_CHECK_INFO
  if((uint8_t*)Piece == ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize)) // This will be true if the loop didn't break
  {
    error_printf("realloc: Piece not found.\r\n");
    return (void*) ~3ULL;
  }
#endif

  return allocated_address;
}

//----------------------------------------------------------------------------------------------------------------------------------
//  free: Free A Physical Memory Address from AllocateFreeAddress (malloc)
//----------------------------------------------------------------------------------------------------------------------------------
//
// Frees addresses allocated by AllocateFreeAddress
//
// allocated_address: pointer from AllocateFreeAddress (therefore also malloc...)
//

void free(void * allocated_address)
{
  // Locate area
  EFI_MEMORY_DESCRIPTOR * Piece;

  // Get a pointer for the descriptor corresponding to the address (scan the memory map to find it)
  for(Piece = Global_Memory_Info.MemMap; (uint8_t*)Piece < ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize))
  {
    if((Piece->Type == (EfiMaxMemoryType + 1)) && ((void*)Piece->PhysicalStart == allocated_address))
    { // Found it, Piece holds the spot now.

      // Zero out the destination
      AVX_memset(allocated_address, 0, Piece->NumberOfPages << EFI_PAGE_SHIFT); // allocated_address is already a pointer

      // Reclaim as EfiConventionalMemory
      Piece->Type = EfiConventionalMemory;

      // Merge conventional memory if possible
      MergeContiguousConventionalMemory();

      // Done
      break;
    }
  }

#ifdef MEMORY_CHECK_INFO
  if((uint8_t*)Piece == ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize)) // This will be true if the loop didn't break
  {
    error_printf("free: Piece not found.\r\n");
  }
#endif
}

//----------------------------------------------------------------------------------------------------------------------------------
//  get_page: Read the Page Table Entry of a Hardware Page
//----------------------------------------------------------------------------------------------------------------------------------
//
// Reads a page table entry corresponding to a hardware page base address, and returns a structure containing the entry's data,
// the memory map descriptor information of the mapped region in which the page base address is located, the hardware page's size
// (in bytes) and whether or not the entire hardware page fits in the region.
//
// hw_page_base_addr: The base address of the hardware page in pointer form (e.g. as returned by malloc)
//
// The return struct is described as below:
//
// typedef struct {
//   uint64_t               PageTableEntryData;  // The page table entry, which contains the hardware page base address and that page's flags.
//   uint64_t               HWPageSize;          // The size of the hardware page (4kB, 2MB, 1GB, 512GB, 256TB, etc.)
//   uint64_t               WholePageInRegion;   // 1 if the whole hardware page fits in the region described by MemoryMapRegionData, 0 otherwise
//   EFI_MEMORY_DESCRIPTOR  MemoryMapRegionData; // The memory map entry describing the region that contains the page base address
// } PAGE_ENTRY_INFO_STRUCT;
//
// Mask for 2MB-256TB entry's flags: 0xFFF0000000001FFF
// Mask for 4kB entry's flags: 0xFFF0000000000FFF
// (These do include PKRU bits 62:59)
//
// The address masks depend on the paging level:
//  256TB: 0x000F000000000000
//  512GB: 0x000FFF8000000000
//  1GB:   0x000FFFFFC0000000
//  2MB:   0x000FFFFFFFE00000
//  4kB:   0x000FFFFFFFFFF000
//
// NOTE: It is true that this function could easily take a non-page-base-address and return info for the corresponding page base.
// However, implmenenting this instead of the error message on non-page-base-address would risk hiding issues in code and encourage
// poor programming practice (or at least enable it). Doing it the current way means that users need to be sure they always know what
// their program is doing and how their memory is laid out, and may even help find some hard-to-spot pointer issues.
//

PAGE_ENTRY_INFO_STRUCT get_page(void * hw_page_base_addr)
{
  uint64_t page_base_address = (uint64_t)hw_page_base_addr;
  PAGE_ENTRY_INFO_STRUCT page_data = {0};

  if(page_base_address & 0xFFF) // There are no page base addresses < 4kB-aligned
  {
    error_printf("Hey! That's not a 4kB-aligned hardware page base address!\r\nget_page() failed.\r\n");
    return page_data;
  }
  else
  {
    EFI_MEMORY_DESCRIPTOR * Piece;

    for(Piece = Global_Memory_Info.MemMap; Piece < (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize))
    {
      EFI_PHYSICAL_ADDRESS PhysicalEnd = Piece->PhysicalStart + (Piece->NumberOfPages << EFI_PAGE_SHIFT); // Get the end of this range, which may be the start of another range

      // Found it
      if((Piece->PhysicalStart <= page_base_address) && (PhysicalEnd > page_base_address))
      {
        uint64_t size_above_page_base_in_region = PhysicalEnd - page_base_address;

        uint64_t cr3 = control_register_rw(3, 0, 0); // CR3 has the page directory base (bottom 12 bits of address are assumed 0)
        uint64_t base_pml_addr = cr3 & PAGE_ENTRY_ADDRESS_MASK;

        uint64_t cr4 = control_register_rw(4, 0, 0);
        if(cr4 & (1 << 12)) // Check CR4.LA57 for 5-level paging support
        {
          // Could this be made into a recursive function? Yes. However this is faster (it's unrolled).
          // Rolling it up into a function would add a lot more overhead and isn't necessary since the paging format is hardwired into CPUs.

          // Need to remove the sign extension
          uint64_t pml5_part = (page_base_address & PML5_MASK) >> 48; // units of 256TB

          // PML5: 256TB pages
          if(((uint64_t*)base_pml_addr)[pml5_part] & (1ULL << 7)) // Bit 7 is the "no deeper table" bit. Need to check it because the page base address might happen to be, e.g., 256TB aligned but described by a 4kB page table.
          {
            page_data.HWPageSize = (256ULL << 40);
            page_data.PageTableEntryData = ((uint64_t*)base_pml_addr)[pml5_part];

            uint64_t pml_base_address = page_data.PageTableEntryData & PML5_ADDRESS_MASK;
            if(page_base_address != pml_base_address)
            {
              warning_printf("get_page: %#qx is not the page base address for this page,\r\nthis is: %#qx. Please try again with the correct address.\r\n", page_base_address, pml_base_address);
              return page_data; // return get_page(pml_base_address);
            }

            if(size_above_page_base_in_region >= page_data.HWPageSize)
            {
              page_data.WholePageInRegion = 1;
            }

          }
          else
          {
            // Find the page_base_address entry...
            uint64_t next_pml_addr = ((uint64_t*)base_pml_addr)[pml5_part] & PAGE_ENTRY_ADDRESS_MASK;
            uint64_t pml4_part = (page_base_address & PML4_MASK) >> 39; // units of 512GB

            // PML4: 512GB
            if(((uint64_t*)next_pml_addr)[pml4_part] & (1ULL << 7))
            {
              page_data.HWPageSize = (512ULL << 30);
              page_data.PageTableEntryData = ((uint64_t*)next_pml_addr)[pml4_part];

              uint64_t pml_base_address = page_data.PageTableEntryData & PML4_ADDRESS_MASK;
              if(page_base_address != pml_base_address)
              {
                warning_printf("get_page: %#qx is not the page base address for this page,\r\nthis is: %#qx. Please try again with the correct address.\r\n", page_base_address, pml_base_address);
                return page_data;
              }

              if(size_above_page_base_in_region >= page_data.HWPageSize)
              {
                page_data.WholePageInRegion = 1;
              }

            }
            else
            {
              // Find the page_base_address entry...
              next_pml_addr = ((uint64_t*)next_pml_addr)[pml4_part] & PAGE_ENTRY_ADDRESS_MASK;
              uint64_t pml3_part = (page_base_address & PML3_MASK) >> 30; // units of 1GB

              // PML3: 1GB
              if(((uint64_t*)next_pml_addr)[pml3_part] & (1ULL << 7))
              {
                page_data.HWPageSize = (1ULL << 30);
                page_data.PageTableEntryData = ((uint64_t*)next_pml_addr)[pml3_part];

                uint64_t pml_base_address = page_data.PageTableEntryData & PML3_ADDRESS_MASK;
                if(page_base_address != pml_base_address)
                {
                  warning_printf("get_page: %#qx is not the page base address for this page,\r\nthis is: %#qx. Please try again with the correct address.\r\n", page_base_address, pml_base_address);
                  return page_data;
                }

                if(size_above_page_base_in_region >= page_data.HWPageSize)
                {
                  page_data.WholePageInRegion = 1;
                }

              }
              else
              {
                // Find the page_base_address entry...
                next_pml_addr = ((uint64_t*)next_pml_addr)[pml3_part] & PAGE_ENTRY_ADDRESS_MASK;
                uint64_t pml2_part = (page_base_address & PML2_MASK) >> 21; // units of 2MB

                // PML2: 2MB
                if(((uint64_t*)next_pml_addr)[pml2_part] & (1ULL << 7))
                {
                  page_data.HWPageSize = (2ULL << 20);
                  page_data.PageTableEntryData = ((uint64_t*)next_pml_addr)[pml2_part];

                  uint64_t pml_base_address = page_data.PageTableEntryData & PML2_ADDRESS_MASK;
                  if(page_base_address != pml_base_address)
                  {
                    warning_printf("get_page: %#qx is not the page base address for this page,\r\nthis is: %#qx. Please try again with the correct address.\r\n", page_base_address, pml_base_address);
                    return page_data;
                  }

                  if(size_above_page_base_in_region >= page_data.HWPageSize)
                  {
                    page_data.WholePageInRegion = 1;
                  }

                }
                else
                {
                  // Find the page_base_address entry...
                  next_pml_addr = ((uint64_t*)next_pml_addr)[pml2_part] & PAGE_ENTRY_ADDRESS_MASK;
                  uint64_t pml1_part = (page_base_address & PML1_MASK) >> 12; // units of 4kB

                  // PML1: 4kB
                  page_data.HWPageSize = (4ULL << 10);
                  page_data.PageTableEntryData = ((uint64_t*)next_pml_addr)[pml1_part];

                  // It's not possible to both get here and trigger a page base address error

                  if(size_above_page_base_in_region >= page_data.HWPageSize)
                  {
                    page_data.WholePageInRegion = 1;
                  }

                }
              }
            }
          }
        } // end 5-level
        else // 4-level paging
        {
          // Need to remove the sign extension
          uint64_t pml4_part = (page_base_address & PML4_MASK) >> 39; // units of 512GB

          // 4-level paging doesn't have 512GB paging sizes

          // Find the page_base_address entry...
          uint64_t next_pml_addr = ((uint64_t*)base_pml_addr)[pml4_part] & PAGE_ENTRY_ADDRESS_MASK;
          uint64_t pml3_part = (page_base_address & PML3_MASK) >> 30; // units of 1GB

          // PML3/PDP: 1GB
          if(((uint64_t*)next_pml_addr)[pml3_part] & (1ULL << 7))
          {
            page_data.HWPageSize = (1ULL << 30);
            page_data.PageTableEntryData = ((uint64_t*)next_pml_addr)[pml3_part];

            uint64_t pml_base_address = page_data.PageTableEntryData & PML3_ADDRESS_MASK;
            if(page_base_address != pml_base_address)
            {
              warning_printf("get_page: %#qx is not the page base address for this page,\r\nthis is: %#qx. Please try again with the correct address.\r\n", page_base_address, pml_base_address);
              return page_data;
            }

            if(size_above_page_base_in_region >= page_data.HWPageSize)
            {
              page_data.WholePageInRegion = 1;
            }

          }
          else
          {
            // Find the page_base_address entry...
            next_pml_addr = ((uint64_t*)next_pml_addr)[pml3_part] & PAGE_ENTRY_ADDRESS_MASK;
            uint64_t pml2_part = (page_base_address & PML2_MASK) >> 21; // units of 2MB

            // PML2/PD: 2MB
            if(((uint64_t*)next_pml_addr)[pml2_part] & (1ULL << 7))
            {
              page_data.HWPageSize = (2ULL << 20);
              page_data.PageTableEntryData = ((uint64_t*)next_pml_addr)[pml2_part];

              uint64_t pml_base_address = page_data.PageTableEntryData & PML2_ADDRESS_MASK;
              if(page_base_address != pml_base_address)
              {
                warning_printf("get_page: %#qx is not the page base address for this page,\r\nthis is: %#qx. Please try again with the correct address.\r\n", page_base_address, pml_base_address);
                return page_data;
              }

              if(size_above_page_base_in_region >= page_data.HWPageSize)
              {
                page_data.WholePageInRegion = 1;
              }

            }
            else
            {
              // Find the page_base_address entry...
              next_pml_addr = ((uint64_t*)next_pml_addr)[pml2_part] & PAGE_ENTRY_ADDRESS_MASK;
              uint64_t pml1_part = (page_base_address & PML1_MASK) >> 12; // units of 4kB

              // PML1/PT: 4kB
              page_data.HWPageSize = (4ULL << 10);
              page_data.PageTableEntryData = ((uint64_t*)next_pml_addr)[pml1_part];

              // It's not possible to both get here and trigger a page base address error

              if(size_above_page_base_in_region >= page_data.HWPageSize)
              {
                page_data.WholePageInRegion = 1;
              }

            }
          }
        } // end 4-level

        // Finally, get the memory map data
        page_data.MemoryMapRegionData = *Piece;

        // Whew!
        break;
      }
    }

    // Loop ended without a discovered address
    if(Piece >= (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize))
    {
      error_printf("get_page: Could not find page base address. It may not be aligned or allocated.\r\n");
      return page_data;
    }
  }

  return page_data;
}

//----------------------------------------------------------------------------------------------------------------------------------
//  set_region_hwpages: Modify the Page Table Entries of All Hardware Pages in a Memory Map Region
//----------------------------------------------------------------------------------------------------------------------------------
//
// This takes a hardware page base address aligned to a memory region in the memory map and applies hardware paging flags (e.g.
// NX, G, etc.) to all the hardware pages corresponding to that region. The input address MUST be that of a hardware page base that
// coincides with the PhysicalStart of a memory region. This function only applies to physical addresses. There is a virtual address
// variant, vset_region_hwpages(), but what it does is take a virtual address in order to find the address's corresponding
// PhysicalStart in the system memory map (and that PhysicalStart still needs to be a hardware page base address!).
//
// NOTE: This function can be used to couple a malloc region to a hardware page or contiguous set of hardware pages. In this case it
// is very important that the malloc-allocated address is a page base address (e.g. for 1GB paging, the address needs to be 1GB
// aligned or else it won't be a page base address) and that the malloc region in question consumes the entire hardware page(s),
// otherwise this will be setting flags that will impact other memory regions within those hardware page(s) (yikes!).
//
// hw_page_base_addr: the base address of the hardware page corresponding to the PhysicalStart of a region in the memory map, in pointer form (e.g. as returned by malloc)
// entry_flags: the flags to set (a 64-bit value that is formatted like a page table entry)
// attributes: attributes that will show up on the memory map
// flags_or_entry: 0 = only flags & memory map attrbutes be set, 1 = the whole page table entry gets replaced (both flags and address in the 'entry_flags' variable, plus memory map attributes)
//
// Returns 0 on success, 1 on failure, 2 on "region too small, some parts of region may have already been set" error, 3 on wrong alignment
//
// If the "PK" bits are not active (CR4.PKE == 0, this is true by default in this framework) then bits 62:59 in 'entry_flags' should
// be 0.
//

uint8_t set_region_hwpages(void * hw_page_base_addr, uint64_t entry_flags, uint64_t attributes, uint8_t flags_or_entry)
{
  uint64_t page_base_address = (uint64_t)hw_page_base_addr;
  uint8_t isFirstPage = 1;

  if(page_base_address & 0xFFF) // There are no page base addresses < 4kB-aligned
  {
    error_printf("Hey! That's not a 4kB-aligned hardware page base address!\r\nset_region_hwpages() failed.\r\n");
    return 3;
  }
  else
  {
    EFI_MEMORY_DESCRIPTOR * Piece;

    // Check for page base address in the map, which should always be a PhysicalStart of some region.
    // This can't just set some random section of EfiConventionalMemory to have certain attributes--that would be bad.
    for(Piece = Global_Memory_Info.MemMap; Piece < (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize))
    {
      // Found it
      if(Piece->PhysicalStart == page_base_address)
      {
        uint64_t regionpages = Piece->NumberOfPages; // Piece->NumberOfPages contains the size of the region, so it can be compared against the PML level to ensure sizes match up correctly before making changes

        uint64_t cr3 = control_register_rw(3, 0, 0); // CR3 has the page directory base (bottom 12 bits of address are assumed 0)
        uint64_t base_pml_addr = cr3 & PAGE_ENTRY_ADDRESS_MASK; // Bits 63:52 should be 0 anyway when CR3 is set, otherwise there would be crashes.

        uint64_t cr4 = control_register_rw(4, 0, 0);
        if(cr4 & (1 << 12)) // Check CR4.LA57 for 5-level paging support
        {
          // Could this be made into a recursive function? Yes. However this is faster (it's unrolled).
          // Rolling it up into a function would add a lot more overhead and isn't necessary since the paging format is hardwired into CPUs.
          while(regionpages)
          {
            // Need to remove the sign extension
            uint64_t pml5_part = (page_base_address & PML5_MASK) >> 48; // units of 256TB, physical addressing maxes out at 52-bit, which is only 16 PML5 entries. Page tables must always be located in physical address space.

            // PML5: 256TB pages
            if(((uint64_t*)base_pml_addr)[pml5_part] & (1ULL << 7)) // Bit 7 is the "no deeper table" bit. Need to check it because the page base address might happen to be, e.g., 256TB aligned but described by a 4kB page table.
            {

              if(isFirstPage)
              {
                // Check to make sure memory region start is hardware page base aligned to this size
                uint64_t pml_base_address = ((uint64_t*)base_pml_addr)[pml5_part] & PML5_ADDRESS_MASK;
                if(page_base_address != pml_base_address)
                {
                  warning_printf("set_region_hwpages: %#qx is not the page base address for this page,\r\nthis is: %#qx. Please try again with the correct address.\r\n", page_base_address, pml_base_address);
                  return 3;
                }

                isFirstPage = 0;
              }

              if(regionpages < (1ULL << 36))
              {
                warning_printf("Error: Region at base address %#qx does not cover entire 256TB page. (5-lvl)\r\n", page_base_address);
                warning_printf("Beware that some hardware pages of this region passed to set_region_hwpages() may have\r\n");
                warning_printf("already been set. Recommendation is to immediately run set_region_hwpages() again on the\r\n");
                warning_printf("same area with its prior values, and then reallocate the region with a size\r\n");
                warning_printf("that consumes all hardware pages encompassed by the region.\r\n");
                warning_printf("NOTE: Memory map attributes for the region have also not been updated.\r\n");
                return 2;
              }

              if(flags_or_entry)
              {
                ((uint64_t*)base_pml_addr)[pml5_part] = entry_flags;
              }
              else
              {
                entry_flags &= PAGE_ENTRY_FLAGS_MASK; // Remove any address bits
                entry_flags |= (((uint64_t*)base_pml_addr)[pml5_part] & PAGE_ENTRY_ADDRESS_MASK);
                ((uint64_t*)base_pml_addr)[pml5_part] = entry_flags;
              }

              regionpages -= 1ULL << 36; // (1ULL << 48) >> 12
              page_base_address += 1ULL << 48; // Get the next aligned address in the set
            }
            else
            {
              // Find the page_base_address entry...
              uint64_t next_pml_addr = ((uint64_t*)base_pml_addr)[pml5_part] & PAGE_ENTRY_ADDRESS_MASK;
              uint64_t pml4_part = (page_base_address & PML4_MASK) >> 39; // units of 512GB

              // PML4: 512GB
              if(((uint64_t*)next_pml_addr)[pml4_part] & (1ULL << 7))
              {

                if(isFirstPage)
                {
                  // Check to make sure memory region start is hardware page base aligned to this size
                  uint64_t pml_base_address = ((uint64_t*)next_pml_addr)[pml4_part] & PML4_ADDRESS_MASK;
                  if(page_base_address != pml_base_address)
                  {
                    warning_printf("set_region_hwpages: %#qx is not the page base address for this page,\r\nthis is: %#qx. Please try again with the correct address.\r\n", page_base_address, pml_base_address);
                    return 3;
                  }

                  isFirstPage = 0;
                }

                if(regionpages < (1ULL << 27))
                {
                  warning_printf("Error: Region at base address %#qx does not cover entire 512GB page. (5-lvl)\r\n", page_base_address);
                  warning_printf("Beware that some hardware pages of this region passed to set_region_hwpages() may have\r\n");
                  warning_printf("already been set. Recommendation is to immediately run set_region_hwpages() again on the\r\n");
                  warning_printf("same area with its prior values, and then reallocate the region with a size\r\n");
                  warning_printf("that consumes all hardware pages encompassed by the region.\r\n");
                  warning_printf("NOTE: Memory map attributes for the region have also not been updated.\r\n");
                  return 2;
                }

                if(flags_or_entry)
                {
                  ((uint64_t*)next_pml_addr)[pml4_part] = entry_flags;
                }
                else
                {
                  entry_flags &= PAGE_ENTRY_FLAGS_MASK;
                  entry_flags |= (((uint64_t*)next_pml_addr)[pml4_part] & PAGE_ENTRY_ADDRESS_MASK);
                  ((uint64_t*)next_pml_addr)[pml4_part] = entry_flags;
                }

                regionpages -= 1ULL << 27; // (1ULL << 39) >> 12
                page_base_address += 1ULL << 39; // Get the next aligned address in the set
              }
              else
              {
                // Find the page_base_address entry...
                next_pml_addr = ((uint64_t*)next_pml_addr)[pml4_part] & PAGE_ENTRY_ADDRESS_MASK;
                uint64_t pml3_part = (page_base_address & PML3_MASK) >> 30; // units of 1GB

                // PML3: 1GB
                if(((uint64_t*)next_pml_addr)[pml3_part] & (1ULL << 7))
                {

                  if(isFirstPage)
                  {
                    // Check to make sure memory region start is hardware page base aligned to this size
                    uint64_t pml_base_address = ((uint64_t*)next_pml_addr)[pml3_part] & PML3_ADDRESS_MASK;
                    if(page_base_address != pml_base_address)
                    {
                      warning_printf("set_region_hwpages: %#qx is not the page base address for this page,\r\nthis is: %#qx. Please try again with the correct address.\r\n", page_base_address, pml_base_address);
                      return 3;
                    }

                    isFirstPage = 0;
                  }

                  if(regionpages < (1ULL << 18))
                  {
                    warning_printf("Error: Region at base address %#qx does not cover entire 1GB page. (5-lvl)\r\n", page_base_address);
                    warning_printf("Beware that some hardware pages of this region passed to set_region_hwpages() may have\r\n");
                    warning_printf("already been set. Recommendation is to immediately run set_region_hwpages() again on the\r\n");
                    warning_printf("same area with its prior values, and then reallocate the region with a size\r\n");
                    warning_printf("that consumes all hardware pages encompassed by the region.\r\n");
                    warning_printf("NOTE: Memory map attributes for the region have also not been updated.\r\n");
                    return 2;
                  }

                  if(flags_or_entry)
                  {
                    ((uint64_t*)next_pml_addr)[pml3_part] = entry_flags;
                  }
                  else
                  {
                    entry_flags &= PAGE_ENTRY_FLAGS_MASK;
                    entry_flags |= (((uint64_t*)next_pml_addr)[pml3_part] & PAGE_ENTRY_ADDRESS_MASK);
                    ((uint64_t*)next_pml_addr)[pml3_part] = entry_flags;
                  }

                  regionpages -= 1ULL << 18; // (1ULL << 30) >> 12
                  page_base_address += 1ULL << 30; // Get the next aligned address in the set
                }
                else
                {
                  // Find the page_base_address entry...
                  next_pml_addr = ((uint64_t*)next_pml_addr)[pml3_part] & PAGE_ENTRY_ADDRESS_MASK;
                  uint64_t pml2_part = (page_base_address & PML2_MASK) >> 21; // units of 2MB

                  // PML2: 2MB
                  if(((uint64_t*)next_pml_addr)[pml2_part] & (1ULL << 7))
                  {

                    if(isFirstPage)
                    {
                      // Check to make sure memory region start is hardware page base aligned to this size
                      uint64_t pml_base_address = ((uint64_t*)next_pml_addr)[pml2_part] & PML2_ADDRESS_MASK;
                      if(page_base_address != pml_base_address)
                      {
                        warning_printf("set_region_hwpages: %#qx is not the page base address for this page,\r\nthis is: %#qx. Please try again with the correct address.\r\n", page_base_address, pml_base_address);
                        return 3;
                      }

                      isFirstPage = 0;
                    }

                    if(regionpages < (1ULL << 9))
                    {
                      warning_printf("Error: Region at base address %#qx does not cover entire 2MB page. (5-lvl)\r\n", page_base_address);
                      warning_printf("Beware that some hardware pages of this region passed to set_region_hwpages() may have\r\n");
                      warning_printf("already been set. Recommendation is to immediately run set_region_hwpages() again on the\r\n");
                      warning_printf("same area with its prior values, and then reallocate the region with a size\r\n");
                      warning_printf("that consumes all hardware pages encompassed by the region.\r\n");
                      warning_printf("NOTE: Memory map attributes for the region have also not been updated.\r\n");
                      return 2;
                    }

                    if(flags_or_entry)
                    {
                      ((uint64_t*)next_pml_addr)[pml2_part] = entry_flags;
                    }
                    else
                    {
                      entry_flags &= PAGE_ENTRY_FLAGS_MASK;
                      entry_flags |= (((uint64_t*)next_pml_addr)[pml2_part] & PAGE_ENTRY_ADDRESS_MASK);
                      ((uint64_t*)next_pml_addr)[pml2_part] = entry_flags;
                    }

                    regionpages -= 1ULL << 9; // (1ULL << 21) >> 12
                    page_base_address += 1ULL << 21; // Get the next aligned address in the set
                  }
                  else
                  {
                    // Find the page_base_address entry...
                    next_pml_addr = ((uint64_t*)next_pml_addr)[pml2_part] & PAGE_ENTRY_ADDRESS_MASK;
                    uint64_t pml1_part = (page_base_address & PML1_MASK) >> 12; // units of 4kB

                    // PML1: 4kB
                    // Can't get here if address isn't both 4kB-aligned and PhysicalStart, so no incorrect page base address error here
                    // regionpages cannot be < 1, so no error message here

                    if(flags_or_entry)
                    {
                      ((uint64_t*)next_pml_addr)[pml1_part] = entry_flags;
                    }
                    else
                    {
                      entry_flags &= PAGE_ENTRY_FLAGS_MASK - 0x1000;
                      entry_flags |= (((uint64_t*)next_pml_addr)[pml1_part] & PAGE_ENTRY_ADDRESS_MASK);
                      ((uint64_t*)next_pml_addr)[pml1_part] = entry_flags;
                    }

                    regionpages -= 1ULL; // (1ULL << 12) >> 12
                    page_base_address += 1ULL << 12; // Get the next aligned address in the set
                  }
                }
              }
            }
          } // end while
        } // end 5-level
        else // 4-level paging
        {
          // Could this be made into a recursive function? Yes. However this is faster (it's unrolled).
          // Rolling it up into a function would add a lot more overhead and isn't necessary since the paging format is hardwired into CPUs.
          while(regionpages)
          {
            // Need to remove the sign extension
            uint64_t pml4_part = (page_base_address & PML4_MASK) >> 39; // units of 512GB

            // 4-level paging doesn't have 512GB paging sizes

            // Find the page_base_address entry...
            uint64_t next_pml_addr = ((uint64_t*)base_pml_addr)[pml4_part] & PAGE_ENTRY_ADDRESS_MASK;
            uint64_t pml3_part = (page_base_address & PML3_MASK) >> 30; // units of 1GB

            // PML3/PDP: 1GB
            if(((uint64_t*)next_pml_addr)[pml3_part] & (1ULL << 7))
            {

              if(isFirstPage)
              {
                // Check to make sure memory region start is hardware page base aligned to this size
                uint64_t pml_base_address = ((uint64_t*)next_pml_addr)[pml3_part] & PML3_ADDRESS_MASK;
                if(page_base_address != pml_base_address)
                {
                  warning_printf("set_region_hwpages: %#qx is not the page base address for this page,\r\nthis is: %#qx. Please try again with the correct address.\r\n", page_base_address, pml_base_address);
                  return 3;
                }

                isFirstPage = 0;
              }

              if(regionpages < (1ULL << 18))
              {
                warning_printf("Error: Region at base address %#qx does not cover entire 1GB page. (4-lvl)\r\n", page_base_address);
                warning_printf("Beware that some hardware pages of this region passed to set_region_hwpages() may have\r\n");
                warning_printf("already been set. Recommendation is to immediately run set_region_hwpages() again on the\r\n");
                warning_printf("same area with its prior values, and then reallocate the region with a size\r\n");
                warning_printf("that consumes all hardware pages encompassed by the region.\r\n");
                warning_printf("NOTE: Memory map attributes for the region have also not been updated.\r\n");
                return 2;
              }

              if(flags_or_entry)
              {
                ((uint64_t*)next_pml_addr)[pml3_part] = entry_flags;
              }
              else
              {
                entry_flags &= PAGE_ENTRY_FLAGS_MASK;
                entry_flags |= (((uint64_t*)next_pml_addr)[pml3_part] & PAGE_ENTRY_ADDRESS_MASK);
                ((uint64_t*)next_pml_addr)[pml3_part] = entry_flags;
              }

              regionpages -= 1ULL << 18; // (1ULL << 30) >> 12
              page_base_address += 1ULL << 30; // Get the next aligned address in the set
            }
            else
            {
              // Find the page_base_address entry...
              next_pml_addr = ((uint64_t*)next_pml_addr)[pml3_part] & PAGE_ENTRY_ADDRESS_MASK;
              uint64_t pml2_part = (page_base_address & PML2_MASK) >> 21; // units of 2MB

              // PML2/PD: 2MB
              if(((uint64_t*)next_pml_addr)[pml2_part] & (1ULL << 7))
              {

                if(isFirstPage)
                {
                  // Check to make sure memory region start is hardware page base aligned to this size
                  uint64_t pml_base_address = ((uint64_t*)next_pml_addr)[pml2_part] & PML2_ADDRESS_MASK;
                  if(page_base_address != pml_base_address)
                  {
                    warning_printf("set_region_hwpages: %#qx is not the page base address for this page,\r\nthis is: %#qx. Please try again with the correct address.\r\n", page_base_address, pml_base_address);
                    return 3;
                  }

                  isFirstPage = 0;
                }

                if(regionpages < (1ULL << 9))
                {
                  warning_printf("Error: Region at base address %#qx does not cover entire 2MB page. (4-lvl)\r\n", page_base_address);
                  warning_printf("Beware that some hardware pages of this region passed to set_region_hwpages() may have\r\n");
                  warning_printf("already been set. Recommendation is to immediately run set_region_hwpages() again on the\r\n");
                  warning_printf("same area with its prior values, and then reallocate the region with a size\r\n");
                  warning_printf("that consumes all hardware pages encompassed by the region.\r\n");
                  warning_printf("NOTE: Memory map attributes for the region have also not been updated.\r\n");
                  return 2;
                }

                if(flags_or_entry)
                {
                  ((uint64_t*)next_pml_addr)[pml2_part] = entry_flags;
                }
                else
                {
                  entry_flags &= PAGE_ENTRY_FLAGS_MASK;
                  entry_flags |= (((uint64_t*)next_pml_addr)[pml2_part] & PAGE_ENTRY_ADDRESS_MASK);
                  ((uint64_t*)next_pml_addr)[pml2_part] = entry_flags;
                }

                regionpages -= 1ULL << 9; // (1ULL << 21) >> 12
                page_base_address += 1ULL << 21; // Get the next aligned address in the set
              }
              else
              {
                // Find the page_base_address entry...
                next_pml_addr = ((uint64_t*)next_pml_addr)[pml2_part] & PAGE_ENTRY_ADDRESS_MASK;
                uint64_t pml1_part = (page_base_address & PML1_MASK) >> 12; // units of 4kB

                // PML1/PT: 4kB
                // Can't get here if address isn't both 4kB-aligned and PhysicalStart, so no incorrect page base address error here
                // regionpages cannot be < 1, so no error message here

                if(flags_or_entry)
                {
                  ((uint64_t*)next_pml_addr)[pml1_part] = entry_flags;
                }
                else
                {
                  entry_flags &= PAGE_ENTRY_FLAGS_MASK - 0x1000;
                  entry_flags |= (((uint64_t*)next_pml_addr)[pml1_part] & PAGE_ENTRY_ADDRESS_MASK);
                  ((uint64_t*)next_pml_addr)[pml1_part] = entry_flags;
                }

                regionpages -= 1ULL; // (1ULL << 12) >> 12
                page_base_address += 1ULL << 12; // Get the next aligned address in the set
              }
            }
          } // end while
        } // end 4-level

        // Finally, set the memory map attributes
        Piece->Attribute = attributes;

        // Whew!
        break;
      }
    }

    // Loop ended without a discovered address
    if(Piece >= (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize))
    {
      error_printf("set_region_hwpages: Could not find page base address. It may not be aligned or allocated.\r\n");
      return 1;
    }
  }

  return 0;
}

//----------------------------------------------------------------------------------------------------------------------------------
//  vmalloc: Allocate Virtual Memory with Alignment
//----------------------------------------------------------------------------------------------------------------------------------
//
// Dynamically allocate virtual memory aligned to the nearest suitable address alignment value
//
// See malloc() for a more detailed description. This is the same as malloc, but for virtual addresses in the memory map instead of
// physical ones.
//

void * vmalloc(size_t numbytes)
{
  if(numbytes < (2ULL << 20)) // < 2MB
  {
    return vmalloc4KB(numbytes); // 4kB-aligned
  }
  else if(numbytes < (1ULL << 30)) // < 1GB
  {
    return vmalloc2MB(numbytes); // 2MB-aligned
  }
  else if(numbytes < (512ULL << 30)) // < 512GB
  {
    return vmalloc1GB(numbytes); // 1GB-aligned
  }
  else if(numbytes < (256ULL << 40)) // < 256TB
  {
    return vmalloc512GB(numbytes); // 512GB-aligned
  }
  else // > 256TB
  {
    return vmalloc256TB(numbytes); // 256TB-aligned
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
//  vmallocX: Allocate Virtual Memory Aligned to X Bytes
//----------------------------------------------------------------------------------------------------------------------------------
//
// Each of these allocate bytes at virtual addresses aligned on X-byte boundaries (X in vmallocX). Otherwise, same rules as above.
//

void * vmalloc4KB(size_t numbytes)
{
  EFI_PHYSICAL_ADDRESS new_buffer = 0; // Make this 0x100000000 to only operate above 4GB

  new_buffer = VAllocateFreeAddress(numbytes, new_buffer, (4ULL << 10));

  return (void*)new_buffer;
}

void * vmalloc2MB(size_t numbytes)
{
  EFI_PHYSICAL_ADDRESS new_buffer = 0; // Make this 0x100000000 to only operate above 4GB

  new_buffer = VAllocateFreeAddress(numbytes, new_buffer, (2ULL << 20));

  return (void*)new_buffer;
}

void * vmalloc1GB(size_t numbytes)
{
  EFI_PHYSICAL_ADDRESS new_buffer = 0; // Make this 0x100000000 to only operate above 4GB

  new_buffer = VAllocateFreeAddress(numbytes, new_buffer, (1ULL << 30));

  return (void*)new_buffer;
}

void * vmalloc512GB(size_t numbytes)
{
  EFI_PHYSICAL_ADDRESS new_buffer = 0; // Make this 0x100000000 to only operate above 4GB

  new_buffer = VAllocateFreeAddress(numbytes, new_buffer, (512ULL << 30));

  return (void*)new_buffer;
}

void * vmalloc256TB(size_t numbytes)
{
  EFI_PHYSICAL_ADDRESS new_buffer = 0; // Make this 0x100000000 to only operate above 4GB

  new_buffer = VAllocateFreeAddress(numbytes, new_buffer, (256ULL << 40));

  return (void*)new_buffer;
}

//----------------------------------------------------------------------------------------------------------------------------------
//  vcalloc: Allocate Virtual Memory with Alignment
//----------------------------------------------------------------------------------------------------------------------------------
//
// Dynamically allocate virtual memory aligned to the nearest suitable address alignment value
//
// This is just an alias for vmalloc that takes the calloc(3) syntax. It's just the virtual memory version of calloc() above.
//
// elements: number of elements in array
// size: size of each element
//

void * vcalloc(size_t elements, size_t size)
{
  size_t total_size = elements * size; // This is fine if size_t is 64-bits, which is true of all intended targets of this framework.
  void * new_buffer = vmalloc(total_size);

  return new_buffer;
}

//----------------------------------------------------------------------------------------------------------------------------------
//  vrealloc: Reallocate Virtual Memory with Alignment
//----------------------------------------------------------------------------------------------------------------------------------
//
// Dynamically reallocate memory for an existing pointer from vmalloc, and vfree the old region (if moved)
//
// allocated_address: The pointer allocated from vmalloc
// size: The new desired size
//
// NOTE: Unlike realloc(3), NULL pointer here is actually a valid address at 0x0. Therefore passing in a NULL pointer as
// allocated_address is the same as passing in address 0x0, which could be an actual allocated region. So don't do it.
// Passing in a size of 0, however, will cause vfree() to be run and will result in a return address of ~2ULL.
//

void * vrealloc(void * allocated_address, size_t size)
{
  if(size == 0)
  {
    vfree(allocated_address);
    return ((void*) ~2ULL);
  }

  EFI_MEMORY_DESCRIPTOR * Piece;

  size_t numpages = EFI_SIZE_TO_PAGES(size);
  size_t orig_numpages = 0;

  // Check for vmalloc in the map, we need the old size
  for(Piece = Global_Memory_Info.MemMap; Piece < (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize))
  {
    // Within each compatible vmalloc type, check the address
    if((Piece->Type == (EfiMaxMemoryType + 2)) && ((uint8_t*)Piece->VirtualStart == (uint8_t*)allocated_address))
    {
      // Found it
      orig_numpages = Piece->NumberOfPages;

      if(numpages > orig_numpages) // Grow
      { // Can the vmalloc area be expanded into adjacent EfiConventionalMemory?
        // Is the next piece an EfiConventionalMemory region?

        size_t additional_numpages = numpages - orig_numpages;
        // If the area right after the vmalloc region is EfiConventionalMemory, we might be able to just take some pages from there...

        // Check if there's an EfiConventionalMemory region adjacent in memory to the memmap region
        EFI_MEMORY_DESCRIPTOR * Next_Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize); // Remember... sizeof(EFI_MEMORY_DESCRIPTOR) != MemMapDescriptorSize :/
        EFI_VIRTUAL_ADDRESS VirtualEnd = Piece->VirtualStart + (Piece->NumberOfPages << EFI_PAGE_SHIFT); // Get the end of this range, which may be the start of another range

        // Quick check for adjacency that will skip scanning the whole memmap
        // Can provide a little speed boost for ordered memory maps
        if(
            (Next_Piece->VirtualStart != VirtualEnd)
            ||
            ((uint8_t*)Next_Piece == ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize))
          ) // This OR check protects against the obscure case of there being a rogue entry adjacent the end of the memory map that describes a valid EfiConventionalMemory entry
        {
          // See if VirtualEnd matches any VirtualStart for unordered maps
          for(Next_Piece = Global_Memory_Info.MemMap; Next_Piece < (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Next_Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Next_Piece + Global_Memory_Info.MemMapDescriptorSize))
          {
            if(VirtualEnd == Next_Piece->VirtualStart)
            {
              // Found one
              break;
            }
          }
        }

        // Is the next piece an EfiConventionalMemory type?
        if(
            (Next_Piece->Type == EfiConventionalMemory) && (Next_Piece->NumberOfPages >= additional_numpages)
            &&
            (uint8_t*)Next_Piece != ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize)
          ) // Check if the loop didn't break (this also covers the case where the memory map is the last valid entry in the map)
        {
          if(Next_Piece->NumberOfPages > additional_numpages)
          {
            // Modify MemMap's entry
            Piece->NumberOfPages = numpages;

            // Modify adjacent EfiConventionalMemory's entry
            Next_Piece->NumberOfPages -= additional_numpages;
            Next_Piece->PhysicalStart += (additional_numpages << EFI_PAGE_SHIFT);
            Next_Piece->VirtualStart += (additional_numpages << EFI_PAGE_SHIFT);

            // Done
          }
          else if(Next_Piece->NumberOfPages == additional_numpages)
          { // If the next piece is exactly the number of pages, we can claim it and reclaim a descriptor
            // This just means that MemMap will have some wiggle room for the next time it needs to be modified.

            // Modify MemMap's entry
            Piece->NumberOfPages = numpages;

            // Erase the claimed descriptor
            // Zero out the piece we just claimed
            AVX_memset(Next_Piece, 0, Global_Memory_Info.MemMapDescriptorSize); // Next_Piece is a pointer
            AVX_memmove(Next_Piece, (uint8_t*)Next_Piece + Global_Memory_Info.MemMapDescriptorSize, ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize) - ((uint8_t*)Next_Piece + Global_Memory_Info.MemMapDescriptorSize));

            // Update Global_Memory_Info
            Global_Memory_Info.MemMapSize -= Global_Memory_Info.MemMapDescriptorSize;

            // Zero out the entry that used to be at the end of the map
            AVX_memset((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize, 0, Global_Memory_Info.MemMapDescriptorSize);

            // Done
          }
          // There is no else, as there are only > and == cases.
          else
          {
            error_printf("vrealloc: What kind of sorcery is this? Seeing this means there's a bug in vrealloc.\r\n");
            // And also probably MemMap_Prep()...
            HaCF();
          }
        }
        else // Nope, need to move it altogether. But that's really easy to do for vmalloc. :)
        {
          // Get a new address
          void * new_address = vmalloc(size);
          if((EFI_VIRTUAL_ADDRESS)new_address == ~0ULL)
          {
            error_printf("vrealloc: Insufficient free memory, could not reallocate increased size.\r\n");
            return new_address; // Better to return this invalid address than to return allocated_address
          }
          //
          // NOTE: Any function call that includes a call to MemMap_Prep() renders "Piece" unusable afterwards.
          // This is because it can no longer be guaranteed that the memory map is in the same place as before;
          // MemMap_Prep() might have changed its location.
          //

          // Move the old memory to the new area
          AVX_memmove(new_address, allocated_address, (orig_numpages << EFI_PAGE_SHIFT)); // Need size in bytes

          // Free the old address
          vfree(allocated_address);

          // Done
          return new_address; // This is one of the few times an early return is used instead of break outside of an error
        }
        // Grow done
      }
      else if(numpages < orig_numpages) // Shrink
      {
        // Is the next piece an EfiConventionalMemory region?
        // If the area right after the vmalloc region is EfiConventionalMemory, we might be able to just give it the freed pages
        size_t freedpages = orig_numpages - numpages;

        // Check if there's an EfiConventionalMemory region adjacent in memory to the vmalloc region
        EFI_MEMORY_DESCRIPTOR * Next_Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize); // Remember... sizeof(EFI_MEMORY_DESCRIPTOR) != MemMapDescriptorSize :/
        EFI_VIRTUAL_ADDRESS VirtualEnd = Piece->VirtualStart + (Piece->NumberOfPages << EFI_PAGE_SHIFT); // Get the end of this range, which may be the start of another range

        // Quick check for adjacency that will skip scanning the whole memmap
        // Can provide a little speed boost for ordered memory maps
        if(
            (Next_Piece->VirtualStart != VirtualEnd)
            ||
            ((uint8_t*)Next_Piece == ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize))
          ) // This OR check protects against the obscure case of there being a rogue entry adjacent the end of the memory map that describes a valid EfiConventionalMemory entry
        {
          // See if VirtualEnd matches any VirtualStart for unordered maps
          for(Next_Piece = Global_Memory_Info.MemMap; Next_Piece < (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Next_Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Next_Piece + Global_Memory_Info.MemMapDescriptorSize))
          {
            if(VirtualEnd == Next_Piece->VirtualStart)
            {
              // Found one
              break;
            }
          }
        }

        // Is the next piece an EfiConventionalMemory type?
        if(
            (Next_Piece->Type == EfiConventionalMemory)
            &&
            (uint8_t*)Next_Piece != ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize)
          ) // Check if the loop didn't break (this also covers the case where the memory map is the last valid entry in the map)
        { // Yes, we can reclaim without requiring a new entry

          // Modify vmalloc area's entry
          Piece->NumberOfPages = numpages;

          // Modify adjacent EfiConventionalMemory's entry
          Next_Piece->NumberOfPages += freedpages;
          Next_Piece->PhysicalStart -= (freedpages << EFI_PAGE_SHIFT);
          Next_Piece->VirtualStart -= (freedpages << EFI_PAGE_SHIFT);

          // Done. Nice.
        }
        // No, we need a new memmap entry, which will require a new page if the last entry is on a page edge or would spill over a page edge. Better to be safe then sorry!
        // First, maybe there's room for another descriptor in the last page
        else if((Global_Memory_Info.MemMapSize + Global_Memory_Info.MemMapDescriptorSize) <= (numpages << EFI_PAGE_SHIFT))
        { // Yes, we can reclaim and fit in another descriptor

          // Make a temporary descriptor to hold current vmalloc entry's values
          EFI_MEMORY_DESCRIPTOR new_descriptor_temp;
          new_descriptor_temp.Type = Piece->Type; // Special vmalloc type
          new_descriptor_temp.Pad = Piece->Pad;
          new_descriptor_temp.PhysicalStart = Piece->PhysicalStart;
          new_descriptor_temp.VirtualStart = Piece->VirtualStart;
          new_descriptor_temp.NumberOfPages = numpages; // New size of vmalloc entry
          new_descriptor_temp.Attribute = Piece->Attribute;

          // Modify the descriptor-to-move
          Piece->Type = EfiConventionalMemory;
          // No pad change
          Piece->PhysicalStart += (numpages << EFI_PAGE_SHIFT);
          Piece->VirtualStart += (numpages << EFI_PAGE_SHIFT);
          Piece->NumberOfPages = freedpages;
          // No attribute change

          // Move (copy) the whole memmap that's above this piece (including this freshly modified piece) from this piece to one MemMapDescriptorSize over
          AVX_memmove((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize, Piece, ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize) - (uint8_t*)Piece); // Pointer math to get size

          // Insert the new piece (by overwriting the now-duplicated entry with new values)
          // I.e. turn this piece into what was stored in the temporary descriptor above
          Piece->Type = new_descriptor_temp.Type;
          Piece->Pad = new_descriptor_temp.Pad;
          Piece->PhysicalStart = new_descriptor_temp.PhysicalStart;
          Piece->VirtualStart = new_descriptor_temp.VirtualStart;
          Piece->NumberOfPages = new_descriptor_temp.NumberOfPages;
          Piece->Attribute = new_descriptor_temp.Attribute;

          // Update Global_Memory_Info MemMap size
          Global_Memory_Info.MemMapSize += Global_Memory_Info.MemMapDescriptorSize;

          // Done
        }
        // No, it would spill over to a new page
        else
        {
          // Do we have more than a descriptor's worth of pages reclaimable?
          size_t pages_per_memory_descriptor = EFI_SIZE_TO_PAGES(Global_Memory_Info.MemMapDescriptorSize);

          if((numpages + pages_per_memory_descriptor) < orig_numpages)
          { // Yes, so we can hang on to one [set] of them and make a new EfiConventionalMemory entry for the rest.
            freedpages -= pages_per_memory_descriptor;

            // Make a temporary descriptor to hold current vmalloc entry's values
            EFI_MEMORY_DESCRIPTOR new_descriptor_temp;
            new_descriptor_temp.Type = Piece->Type; // Special vmalloc type
            new_descriptor_temp.Pad = Piece->Pad;
            new_descriptor_temp.PhysicalStart = Piece->PhysicalStart;
            new_descriptor_temp.VirtualStart = Piece->VirtualStart;
            new_descriptor_temp.NumberOfPages = numpages + pages_per_memory_descriptor; // New size of vmalloc entry
            new_descriptor_temp.Attribute = Piece->Attribute;

            // Modify the descriptor-to-move
            Piece->Type = EfiConventionalMemory;
            // No pad change
            Piece->PhysicalStart += ((numpages + pages_per_memory_descriptor) << EFI_PAGE_SHIFT);
            Piece->VirtualStart += ((numpages + pages_per_memory_descriptor) << EFI_PAGE_SHIFT);
            Piece->NumberOfPages = freedpages;
            // No attribute change

            // Move (copy) the whole memmap that's above this piece (including this freshly modified piece) from this piece to one MemMapDescriptorSize over
            AVX_memmove((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize, Piece, ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize) - (uint8_t*)Piece); // Pointer math to get size

            // Insert the new piece (by overwriting the now-duplicated entry with new values)
            // I.e. turn this piece into what was stored in the temporary descriptor above
            Piece->Type = new_descriptor_temp.Type;
            Piece->Pad = new_descriptor_temp.Pad;
            Piece->PhysicalStart = new_descriptor_temp.PhysicalStart;
            Piece->VirtualStart = new_descriptor_temp.VirtualStart;
            Piece->NumberOfPages = new_descriptor_temp.NumberOfPages;
            Piece->Attribute = new_descriptor_temp.Attribute;

            // Update Global_Memory_Info MemMap size
            Global_Memory_Info.MemMapSize += Global_Memory_Info.MemMapDescriptorSize;
          }
          // No, only 1 [set of] page(s) was reclaimable and adding another entry would spill over. So don't do anything then and hang on to the extra empty page(s).
        }
        // Shrink done
      }
      // else:
      // Nothing to be done if equal.

      break;
    } // End "found it"
  } // End for

#ifdef MEMORY_CHECK_INFO
  if((uint8_t*)Piece == ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize)) // This will be true if the loop didn't break
  {
    error_printf("vrealloc: Piece not found.\r\n");
    return (void*) ~3ULL;
  }
#endif

  return allocated_address;
}

//----------------------------------------------------------------------------------------------------------------------------------
//  vfree: Free A Virtual Memory Address from VAllocateFreeAddress (vmalloc)
//----------------------------------------------------------------------------------------------------------------------------------
//
// Frees addresses allocated by VAllocateFreeAddress
//
// allocated_address: pointer from VAllocateFreeAddress (therefore also vmalloc...)
//

void vfree(void * allocated_address)
{
  // Locate area
  EFI_MEMORY_DESCRIPTOR * Piece;

  // Get a pointer for the descriptor corresponding to the address (scan the memory map to find it)
  for(Piece = Global_Memory_Info.MemMap; (uint8_t*)Piece < ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize))
  {
    if((Piece->Type == (EfiMaxMemoryType + 2)) && ((void*)Piece->VirtualStart == allocated_address))
    { // Found it, Piece holds the spot now.

      // Zero out the destination
      AVX_memset(allocated_address, 0, Piece->NumberOfPages << EFI_PAGE_SHIFT); // allocated_address is already a pointer

      // Reclaim as EfiConventionalMemory
      Piece->Type = EfiConventionalMemory;

      // Merge conventional memory if possible
      MergeContiguousConventionalMemory();

      // Done
      break;
    }
  }

#ifdef MEMORY_CHECK_INFO
  if((uint8_t*)Piece == ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize)) // This will be true if the loop didn't break
  {
    error_printf("vfree: Piece not found.\r\n");
  }
#endif
}

//----------------------------------------------------------------------------------------------------------------------------------
//  vget_page: Read the Page Table Entry of a Hardware Page (Virtual Address Version)
//----------------------------------------------------------------------------------------------------------------------------------
//
// Reads a page table entry corresponding to a hardware page base address, and returns a structure containing the entry's data,
// the memory map descriptor information of the mapped region in which the page base address is located, and whether or not the
// entire hardware page fits in the region. This function uses virtual addresses in the memory map to locate corresponding physical
// ones.
//
// hw_page_base_addr: The base address of the hardware page in pointer form (e.g. as returned by vmalloc)
//
// The return struct is described as below:
//
// typedef struct {
//   uint64_t               PageTableEntryData;  // The page table entry, which contains the hardware page base address and that page's flags.
//   uint64_t               HWPageSize;          // The size of the hardware page (4kB, 2MB, 1GB, 512GB, 256TB, etc.)
//   uint64_t               WholePageInRegion;   // 1 if the whole hardware page fits in the region described by MemoryMapRegionData, 0 otherwise
//   EFI_MEMORY_DESCRIPTOR  MemoryMapRegionData; // The memory map entry describing the region that contains the page base address
// } PAGE_ENTRY_INFO_STRUCT;
//
// Mask for 2MB-256TB entry's flags: 0xFFF0000000001FFF
// Mask for 4kB entry's flags: 0xFFF0000000000FFF
// (These do include PKRU bits 62:59)
//
// The address masks depend on the paging level:
//  256TB: 0x000F000000000000
//  512GB: 0x000FFF8000000000
//  1GB:   0x000FFFFFC0000000
//  2MB:   0x000FFFFFFFE00000
//  4kB:   0x000FFFFFFFFFF000
//
// NOTE: It is true that this function could easily take a non-page-base-address and return info for the corresponding page base.
// However, implmenenting this instead of the error message on non-page-base-address would risk hiding issues in code and encourage
// poor programming practice (or at least enable it). Doing it the current way means that users need to be sure they always know what
// their program is doing and how their memory is laid out, and may even help find some hard-to-spot pointer issues.
//

PAGE_ENTRY_INFO_STRUCT vget_page(void * hw_page_base_addr)
{
  uint64_t page_base_address = (uint64_t)hw_page_base_addr;
  PAGE_ENTRY_INFO_STRUCT page_data = {0};

  EFI_MEMORY_DESCRIPTOR * Piece;

  for(Piece = Global_Memory_Info.MemMap; Piece < (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize))
  {
    EFI_VIRTUAL_ADDRESS VirtualEnd = Piece->VirtualStart + (Piece->NumberOfPages << EFI_PAGE_SHIFT); // Get the end of this range, which may be the start of another range

    // Found it
    if((Piece->VirtualStart <= page_base_address) && (VirtualEnd > page_base_address))
    {

      uint64_t size_above_page_base_in_region = VirtualEnd - page_base_address;

      // Convert virtual address to physical address
      if(Piece->VirtualStart >= Piece->PhysicalStart)
      {
        page_base_address -= (Piece->VirtualStart - Piece->PhysicalStart);
      }
      else
      {
        page_base_address += (Piece->PhysicalStart - Piece->VirtualStart);
      }

      if(page_base_address & 0xFFF) // There are no page base addresses < 4kB-aligned
      {
        error_printf("Hey! That's not a 4kB-aligned hardware page base address!\r\nvget_page() failed.\r\n");
        return page_data;
      }

      uint64_t cr3 = control_register_rw(3, 0, 0); // CR3 has the page directory base (bottom 12 bits of address are assumed 0)
      uint64_t base_pml_addr = cr3 & PAGE_ENTRY_ADDRESS_MASK;

      uint64_t cr4 = control_register_rw(4, 0, 0);
      if(cr4 & (1 << 12)) // Check CR4.LA57 for 5-level paging support
      {
        // Could this be made into a recursive function? Yes. However this is faster (it's unrolled).
        // Rolling it up into a function would add a lot more overhead and isn't necessary since the paging format is hardwired into CPUs.

        // Need to remove the sign extension
        uint64_t pml5_part = (page_base_address & PML5_MASK) >> 48; // units of 256TB

        // PML5: 256TB pages
        if(((uint64_t*)base_pml_addr)[pml5_part] & (1ULL << 7)) // Bit 7 is the "no deeper table" bit. Need to check it because the page base address might happen to be, e.g., 256TB aligned but described by a 4kB page table.
        {
          page_data.HWPageSize = (256ULL << 40);
          page_data.PageTableEntryData = ((uint64_t*)base_pml_addr)[pml5_part];

          uint64_t pml_base_address = page_data.PageTableEntryData & PML5_ADDRESS_MASK;
          if(page_base_address != pml_base_address)
          {
            warning_printf("vget_page: %#qx is not the page base address for this page,\r\nthis is: %#qx. Please try again with the correct address.\r\n", page_base_address, pml_base_address);
            return page_data;
          }

          if(size_above_page_base_in_region >= page_data.HWPageSize)
          {
            page_data.WholePageInRegion = 1;
          }

        }
        else
        {
          // Find the page_base_address entry...
          uint64_t next_pml_addr = ((uint64_t*)base_pml_addr)[pml5_part] & PAGE_ENTRY_ADDRESS_MASK;
          uint64_t pml4_part = (page_base_address & PML4_MASK) >> 39; // units of 512GB

          // PML4: 512GB
          if(((uint64_t*)next_pml_addr)[pml4_part] & (1ULL << 7))
          {
            page_data.HWPageSize = (512ULL << 30);
            page_data.PageTableEntryData = ((uint64_t*)next_pml_addr)[pml4_part];

            uint64_t pml_base_address = page_data.PageTableEntryData & PML4_ADDRESS_MASK;
            if(page_base_address != pml_base_address)
            {
              warning_printf("vget_page: %#qx is not the page base address for this page,\r\nthis is: %#qx. Please try again with the correct address.\r\n", page_base_address, pml_base_address);
              return page_data;
            }

            if(size_above_page_base_in_region >= page_data.HWPageSize)
            {
              page_data.WholePageInRegion = 1;
            }

          }
          else
          {
            // Find the page_base_address entry...
            next_pml_addr = ((uint64_t*)next_pml_addr)[pml4_part] & PAGE_ENTRY_ADDRESS_MASK;
            uint64_t pml3_part = (page_base_address & PML3_MASK) >> 30; // units of 1GB

            // PML3: 1GB
            if(((uint64_t*)next_pml_addr)[pml3_part] & (1ULL << 7))
            {
              page_data.HWPageSize = (1ULL << 30);
              page_data.PageTableEntryData = ((uint64_t*)next_pml_addr)[pml3_part];

              uint64_t pml_base_address = page_data.PageTableEntryData & PML3_ADDRESS_MASK;
              if(page_base_address != pml_base_address)
              {
                warning_printf("vget_page: %#qx is not the page base address for this page,\r\nthis is: %#qx. Please try again with the correct address.\r\n", page_base_address, pml_base_address);
                return page_data;
              }

              if(size_above_page_base_in_region >= page_data.HWPageSize)
              {
                page_data.WholePageInRegion = 1;
              }

            }
            else
            {
              // Find the page_base_address entry...
              next_pml_addr = ((uint64_t*)next_pml_addr)[pml3_part] & PAGE_ENTRY_ADDRESS_MASK;
              uint64_t pml2_part = (page_base_address & PML2_MASK) >> 21; // units of 2MB

              // PML2: 2MB
              if(((uint64_t*)next_pml_addr)[pml2_part] & (1ULL << 7))
              {
                page_data.HWPageSize = (2ULL << 20);
                page_data.PageTableEntryData = ((uint64_t*)next_pml_addr)[pml2_part];

                uint64_t pml_base_address = page_data.PageTableEntryData & PML2_ADDRESS_MASK;
                if(page_base_address != pml_base_address)
                {
                  warning_printf("vget_page: %#qx is not the page base address for this page,\r\nthis is: %#qx. Please try again with the correct address.\r\n", page_base_address, pml_base_address);
                  return page_data;
                }

                if(size_above_page_base_in_region >= page_data.HWPageSize)
                {
                  page_data.WholePageInRegion = 1;
                }

              }
              else
              {
                // Find the page_base_address entry...
                next_pml_addr = ((uint64_t*)next_pml_addr)[pml2_part] & PAGE_ENTRY_ADDRESS_MASK;
                uint64_t pml1_part = (page_base_address & PML1_MASK) >> 12; // units of 4kB

                // PML1: 4kB
                page_data.HWPageSize = (4ULL << 10);
                page_data.PageTableEntryData = ((uint64_t*)next_pml_addr)[pml1_part];

                // It's not possible to both get here and trigger a page base address error

                if(size_above_page_base_in_region >= page_data.HWPageSize)
                {
                  page_data.WholePageInRegion = 1;
                }

              }
            }
          }
        }
      } // end 5-level
      else // 4-level paging
      {
        // Need to remove the sign extension
        uint64_t pml4_part = (page_base_address & PML4_MASK) >> 39; // units of 512GB

        // 4-level paging doesn't have 512GB paging sizes

        // Find the page_base_address entry...
        uint64_t next_pml_addr = ((uint64_t*)base_pml_addr)[pml4_part] & PAGE_ENTRY_ADDRESS_MASK;
        uint64_t pml3_part = (page_base_address & PML3_MASK) >> 30; // units of 1GB

        // PML3/PDP: 1GB
        if(((uint64_t*)next_pml_addr)[pml3_part] & (1ULL << 7))
        {
          page_data.HWPageSize = (1ULL << 30);
          page_data.PageTableEntryData = ((uint64_t*)next_pml_addr)[pml3_part];

          uint64_t pml_base_address = page_data.PageTableEntryData & PML3_ADDRESS_MASK;
          if(page_base_address != pml_base_address)
          {
            warning_printf("vget_page: %#qx is not the page base address for this page,\r\nthis is: %#qx. Please try again with the correct address.\r\n", page_base_address, pml_base_address);
            return page_data;
          }

          if(size_above_page_base_in_region >= page_data.HWPageSize)
          {
            page_data.WholePageInRegion = 1;
          }

        }
        else
        {
          // Find the page_base_address entry...
          next_pml_addr = ((uint64_t*)next_pml_addr)[pml3_part] & PAGE_ENTRY_ADDRESS_MASK;
          uint64_t pml2_part = (page_base_address & PML2_MASK) >> 21; // units of 2MB

          // PML2/PD: 2MB
          if(((uint64_t*)next_pml_addr)[pml2_part] & (1ULL << 7))
          {
            page_data.HWPageSize = (2ULL << 20);
            page_data.PageTableEntryData = ((uint64_t*)next_pml_addr)[pml2_part];

            uint64_t pml_base_address = page_data.PageTableEntryData & PML2_ADDRESS_MASK;
            if(page_base_address != pml_base_address)
            {
              warning_printf("vget_page: %#qx is not the page base address for this page,\r\nthis is: %#qx. Please try again with the correct address.\r\n", page_base_address, pml_base_address);
              return page_data;
            }

            if(size_above_page_base_in_region >= page_data.HWPageSize)
            {
              page_data.WholePageInRegion = 1;
            }

          }
          else
          {
            // Find the page_base_address entry...
            next_pml_addr = ((uint64_t*)next_pml_addr)[pml2_part] & PAGE_ENTRY_ADDRESS_MASK;
            uint64_t pml1_part = (page_base_address & PML1_MASK) >> 12; // units of 4kB

            // PML1/PT: 4kB
            page_data.HWPageSize = (4ULL << 10);
            page_data.PageTableEntryData = ((uint64_t*)next_pml_addr)[pml1_part];

            // It's not possible to both get here and trigger a page base address error

            if(size_above_page_base_in_region >= page_data.HWPageSize)
            {
              page_data.WholePageInRegion = 1;
            }

          }
        }
      } // end 4-level

      // Finally, get the memory map data
      page_data.MemoryMapRegionData = *Piece;

      // Whew!
      break;
    }
  }

  // Loop ended without a discovered address
  if(Piece >= (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize))
  {
    error_printf("vget_page: Could not find page base address. It may not be aligned or allocated.\r\n");
    return page_data;
  }

  return page_data;
}

//----------------------------------------------------------------------------------------------------------------------------------
//  vset_region_hwpages: Modify the Page Table Entries of All Hardware Pages in a Memory Map Region (Virtual Address Version)
//----------------------------------------------------------------------------------------------------------------------------------
//
// This takes a hardware page base address aligned to a memory region in the memory map and applies hardware paging flags (e.g.
// NX, G, etc.) to all the hardware pages corresponding to that region. The input address MUST be that of a hardware page base that
// coincides with the PhysicalStart of a memory region. As this can only apply to physical addresses, this function takes a virtual
// address corresponding to a hardware page base and uses the PhysicalStart of that entry in the memory map. The PhysicalStart must
// still be a page base address, just like in set_region_hwpages(), even if the VirtualStart isn't.
//
// NOTE: This function can be used to couple a malloc region to a hardware page or contiguous set of hardware pages. In this case it
// is very important that the malloc-allocated address is a page base address (e.g. for 1GB paging, the address needs to be 1GB
// aligned or else it won't be a page base address) and that the malloc region in question consumes the entire hardware page(s),
// otherwise this will be setting flags that will impact other memory regions within those hardware page(s) (yikes!).
//
// hw_page_base_addr: the base address of the hardware page corresponding to the PhysicalStart of a region in the memory map, in pointer form (e.g. as returned by vmalloc)
// entry_flags: the flags to set (a 64-bit value that is formatted like a page table entry)
// attributes: attributes that will show up on the memory map
// flags_or_entry: 0 = only flags & memory map attrbutes be set, 1 = the whole page table entry gets replaced (both flags and address in the 'entry_flags' variable, plus memory map attributes)
//
// Returns 0 on success, 1 on failure, 2 on "region too small, some parts of region may have already been set" error
//
// If the "PK" bits are not active (CR4.PKE == 0, this is true by default in this framework) then bits 62:59 in 'entry_flags' should
// be 0.
//

uint8_t vset_region_hwpages(void * hw_page_base_addr, uint64_t entry_flags, uint64_t attributes, uint8_t flags_or_entry)
{
  uint64_t page_base_address = (uint64_t)hw_page_base_addr;
  uint8_t isFirstPage = 1;

  EFI_MEMORY_DESCRIPTOR * Piece;

  // Check for page base address in the map, which should always be a PhysicalStart of some region.
  // This can't just set some random section of EfiConventionalMemory to have certain attributes--that would be bad.
  for(Piece = Global_Memory_Info.MemMap; Piece < (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize))
  {
    // Found it
    if(Piece->VirtualStart == page_base_address)
    {
      page_base_address = Piece->PhysicalStart;

      if(page_base_address & 0xFFF) // There are no physical page base addresses < 4kB-aligned
      {
        error_printf("Hey! That's not a 4kB-aligned hardware page base address!\r\nvset_region_hwpages() failed.\r\n");
        return 3;
      }

      uint64_t regionpages = Piece->NumberOfPages; // Piece->NumberOfPages contains the size of the region, so it can be compared against the PML level to ensure sizes match up correctly before making changes

      uint64_t cr3 = control_register_rw(3, 0, 0); // CR3 has the page directory base (bottom 12 bits of address are assumed 0)
      uint64_t base_pml_addr = cr3 & PAGE_ENTRY_ADDRESS_MASK;

      uint64_t cr4 = control_register_rw(4, 0, 0);
      if(cr4 & (1 << 12)) // Check CR4.LA57 for 5-level paging support
      {
        // Could this be made into a recursive function? Yes. However this is faster (it's unrolled).
        // Rolling it up into a function would add a lot more overhead and isn't necessary since the paging format is hardwired into CPUs.
        while(regionpages)
        {
          // Need to remove the sign extension
          uint64_t pml5_part = (page_base_address & PML5_MASK) >> 48; // units of 256TB

          // PML5: 256TB pages
          if(((uint64_t*)base_pml_addr)[pml5_part] & (1ULL << 7)) // Bit 7 is the "no deeper table" bit. Need to check it because the page base address might happen to be, e.g., 256TB aligned but described by a 4kB page table.
          {

            if(isFirstPage)
            {
              // Check to make sure memory region start is hardware page base aligned to this size
              uint64_t pml_base_address = ((uint64_t*)base_pml_addr)[pml5_part] & PML5_ADDRESS_MASK;
              if(page_base_address != pml_base_address)
              {
                warning_printf("vset_region_hwpages: %#qx is not the page base address for this page,\r\nthis is: %#qx. Please try again with the correct address.\r\n", page_base_address, pml_base_address);
                return 3;
              }

              isFirstPage = 0;
            }

            if(regionpages < (1ULL << 36))
            {
              warning_printf("Error: Region at base address %#qx does not cover entire 256TB page. (5-lvl)\r\n", page_base_address);
              warning_printf("Beware that some hardware pages of this region passed to vset_region_hwpages() may have\r\n");
              warning_printf("already been set. Recommendation is to immediately run vset_region_hwpages() again on the\r\n");
              warning_printf("same area with its prior values, and then reallocate the region with a size\r\n");
              warning_printf("that consumes all hardware pages encompassed by the region.\r\n");
              warning_printf("NOTE: Memory map attributes for the region have also not been updated.\r\n");
              return 2;
            }

            if(flags_or_entry)
            {
              ((uint64_t*)base_pml_addr)[pml5_part] = entry_flags;
            }
            else
            {
              entry_flags &= PAGE_ENTRY_FLAGS_MASK; // Remove any address bits
              entry_flags |= (((uint64_t*)base_pml_addr)[pml5_part] & PAGE_ENTRY_ADDRESS_MASK);
              ((uint64_t*)base_pml_addr)[pml5_part] = entry_flags;
            }

            regionpages -= 1ULL << 36; // (1ULL << 48) >> 12
            page_base_address += 1ULL << 48; // Get the next aligned address in the set
          }
          else
          {
            // Find the page_base_address entry...
            uint64_t next_pml_addr = ((uint64_t*)base_pml_addr)[pml5_part] & PAGE_ENTRY_ADDRESS_MASK;
            uint64_t pml4_part = (page_base_address & PML4_MASK) >> 39; // units of 512GB

            // PML4: 512GB
            if(((uint64_t*)next_pml_addr)[pml4_part] & (1ULL << 7))
            {

              if(isFirstPage)
              {
                // Check to make sure memory region start is hardware page base aligned to this size
                uint64_t pml_base_address = ((uint64_t*)next_pml_addr)[pml4_part] & PML4_ADDRESS_MASK;
                if(page_base_address != pml_base_address)
                {
                  warning_printf("vset_region_hwpages: %#qx is not the page base address for this page,\r\nthis is: %#qx. Please try again with the correct address.\r\n", page_base_address, pml_base_address);
                  return 3;
                }

                isFirstPage = 0;
              }

              if(regionpages < (1ULL << 27))
              {
                warning_printf("Error: Region at base address %#qx does not cover entire 512GB page. (5-lvl)\r\n", page_base_address);
                warning_printf("Beware that some hardware pages of this region passed to vset_region_hwpages() may have\r\n");
                warning_printf("already been set. Recommendation is to immediately run vset_region_hwpages() again on the\r\n");
                warning_printf("same area with its prior values, and then reallocate the region with a size\r\n");
                warning_printf("that consumes all hardware pages encompassed by the region.\r\n");
                warning_printf("NOTE: Memory map attributes for the region have also not been updated.\r\n");
                return 2;
              }

              if(flags_or_entry)
              {
                ((uint64_t*)next_pml_addr)[pml4_part] = entry_flags;
              }
              else
              {
                entry_flags &= PAGE_ENTRY_FLAGS_MASK;
                entry_flags |= (((uint64_t*)next_pml_addr)[pml4_part] & PAGE_ENTRY_ADDRESS_MASK);
                ((uint64_t*)next_pml_addr)[pml4_part] = entry_flags;
              }

              regionpages -= 1ULL << 27; // (1ULL << 39) >> 12
              page_base_address += 1ULL << 39; // Get the next aligned address in the set
            }
            else
            {
              // Find the page_base_address entry...
              next_pml_addr = ((uint64_t*)next_pml_addr)[pml4_part] & PAGE_ENTRY_ADDRESS_MASK;
              uint64_t pml3_part = (page_base_address & PML3_MASK) >> 30; // units of 1GB

              // PML3: 1GB
              if(((uint64_t*)next_pml_addr)[pml3_part] & (1ULL << 7))
              {

                if(isFirstPage)
                {
                  // Check to make sure memory region start is hardware page base aligned to this size
                  uint64_t pml_base_address = ((uint64_t*)next_pml_addr)[pml3_part] & PML3_ADDRESS_MASK;
                  if(page_base_address != pml_base_address)
                  {
                    warning_printf("vset_region_hwpages: %#qx is not the page base address for this page,\r\nthis is: %#qx. Please try again with the correct address.\r\n", page_base_address, pml_base_address);
                    return 3;
                  }

                  isFirstPage = 0;
                }

                if(regionpages < (1ULL << 18))
                {
                  warning_printf("Error: Region at base address %#qx does not cover entire 1GB page. (5-lvl)\r\n", page_base_address);
                  warning_printf("Beware that some hardware pages of this region passed to vset_region_hwpages() may have\r\n");
                  warning_printf("already been set. Recommendation is to immediately run vset_region_hwpages() again on the\r\n");
                  warning_printf("same area with its prior values, and then reallocate the region with a size\r\n");
                  warning_printf("that consumes all hardware pages encompassed by the region.\r\n");
                  warning_printf("NOTE: Memory map attributes for the region have also not been updated.\r\n");
                  return 2;
                }

                if(flags_or_entry)
                {
                  ((uint64_t*)next_pml_addr)[pml3_part] = entry_flags;
                }
                else
                {
                  entry_flags &= PAGE_ENTRY_FLAGS_MASK;
                  entry_flags |= (((uint64_t*)next_pml_addr)[pml3_part] & PAGE_ENTRY_ADDRESS_MASK);
                  ((uint64_t*)next_pml_addr)[pml3_part] = entry_flags;
                }

                regionpages -= 1ULL << 18; // (1ULL << 30) >> 12
                page_base_address += 1ULL << 30; // Get the next aligned address in the set
              }
              else
              {
                // Find the page_base_address entry...
                next_pml_addr = ((uint64_t*)next_pml_addr)[pml3_part] & PAGE_ENTRY_ADDRESS_MASK;
                uint64_t pml2_part = (page_base_address & PML2_MASK) >> 21; // units of 2MB

                // PML2: 2MB
                if(((uint64_t*)next_pml_addr)[pml2_part] & (1ULL << 7))
                {

                  if(isFirstPage)
                  {
                    // Check to make sure memory region start is hardware page base aligned to this size
                    uint64_t pml_base_address = ((uint64_t*)next_pml_addr)[pml2_part] & PML2_ADDRESS_MASK;
                    if(page_base_address != pml_base_address)
                    {
                      warning_printf("vset_region_hwpages: %#qx is not the page base address for this page,\r\nthis is: %#qx. Please try again with the correct address.\r\n", page_base_address, pml_base_address);
                      return 3;
                    }

                    isFirstPage = 0;
                  }

                  if(regionpages < (1ULL << 9))
                  {
                    warning_printf("Error: Region at base address %#qx does not cover entire 2MB page. (5-lvl)\r\n", page_base_address);
                    warning_printf("Beware that some hardware pages of this region passed to vset_region_hwpages() may have\r\n");
                    warning_printf("already been set. Recommendation is to immediately run vset_region_hwpages() again on the\r\n");
                    warning_printf("same area with its prior values, and then reallocate the region with a size\r\n");
                    warning_printf("that consumes all hardware pages encompassed by the region.\r\n");
                    warning_printf("NOTE: Memory map attributes for the region have also not been updated.\r\n");
                    return 2;
                  }

                  if(flags_or_entry)
                  {
                    ((uint64_t*)next_pml_addr)[pml2_part] = entry_flags;
                  }
                  else
                  {
                    entry_flags &= PAGE_ENTRY_FLAGS_MASK;
                    entry_flags |= (((uint64_t*)next_pml_addr)[pml2_part] & PAGE_ENTRY_ADDRESS_MASK);
                    ((uint64_t*)next_pml_addr)[pml2_part] = entry_flags;
                  }

                  regionpages -= 1ULL << 9; // (1ULL << 21) >> 12
                  page_base_address += 1ULL << 21; // Get the next aligned address in the set
                }
                else
                {
                  // Find the page_base_address entry...
                  next_pml_addr = ((uint64_t*)next_pml_addr)[pml2_part] & PAGE_ENTRY_ADDRESS_MASK;
                  uint64_t pml1_part = (page_base_address & PML1_MASK) >> 12; // units of 4kB

                  // PML1: 4kB
                  // Can't get here if address isn't both 4kB-aligned and PhysicalStart, so no incorrect page base address error here
                  // regionpages cannot be < 1, so no error message here

                  if(flags_or_entry)
                  {
                    ((uint64_t*)next_pml_addr)[pml1_part] = entry_flags;
                  }
                  else
                  {
                    entry_flags &= PAGE_ENTRY_FLAGS_MASK - 0x1000;
                    entry_flags |= (((uint64_t*)next_pml_addr)[pml1_part] & PAGE_ENTRY_ADDRESS_MASK);
                    ((uint64_t*)next_pml_addr)[pml1_part] = entry_flags;
                  }

                  regionpages -= 1ULL; // (1ULL << 12) >> 12
                  page_base_address += 1ULL << 12; // Get the next aligned address in the set
                }
              }
            }
          }
        } // end while
      } // end 5-level
      else // 4-level paging
      {
        // Could this be made into a recursive function? Yes. However this is faster (it's unrolled).
        // Rolling it up into a function would add a lot more overhead and isn't necessary since the paging format is hardwired into CPUs.
        while(regionpages)
        {
          // Need to remove the sign extension
          uint64_t pml4_part = (page_base_address & PML4_MASK) >> 39; // units of 512GB

          // 4-level paging doesn't have 512GB paging sizes

          // Find the page_base_address entry...
          uint64_t next_pml_addr = ((uint64_t*)base_pml_addr)[pml4_part] & PAGE_ENTRY_ADDRESS_MASK;
          uint64_t pml3_part = (page_base_address & PML3_MASK) >> 30; // units of 1GB

          // PML3/PDP: 1GB
          if(((uint64_t*)next_pml_addr)[pml3_part] & (1ULL << 7))
          {

            if(isFirstPage)
            {
              // Check to make sure memory region start is hardware page base aligned to this size
              uint64_t pml_base_address = ((uint64_t*)next_pml_addr)[pml3_part] & PML3_ADDRESS_MASK;
              if(page_base_address != pml_base_address)
              {
                warning_printf("vset_region_hwpages: %#qx is not the page base address for this page,\r\nthis is: %#qx. Please try again with the correct address.\r\n", page_base_address, pml_base_address);
                return 3;
              }

              isFirstPage = 0;
            }

            if(regionpages < (1ULL << 18))
            {
              warning_printf("Error: Region at base address %#qx does not cover entire 1GB page. (4-lvl)\r\n", page_base_address);
              warning_printf("Beware that some hardware pages of this region passed to vset_region_hwpages() may have\r\n");
              warning_printf("already been set. Recommendation is to immediately run vset_region_hwpages() again on the\r\n");
              warning_printf("same area with its prior values, and then reallocate the region with a size\r\n");
              warning_printf("that consumes all hardware pages encompassed by the region.\r\n");
              warning_printf("NOTE: Memory map attributes for the region have also not been updated.\r\n");
              return 2;
            }

            if(flags_or_entry)
            {
              ((uint64_t*)next_pml_addr)[pml3_part] = entry_flags;
            }
            else
            {
              entry_flags &= PAGE_ENTRY_FLAGS_MASK;
              entry_flags |= (((uint64_t*)next_pml_addr)[pml3_part] & PAGE_ENTRY_ADDRESS_MASK);
              ((uint64_t*)next_pml_addr)[pml3_part] = entry_flags;
            }

            regionpages -= 1ULL << 18; // (1ULL << 30) >> 12
            page_base_address += 1ULL << 30; // Get the next aligned address in the set
          }
          else
          {
            // Find the page_base_address entry...
            next_pml_addr = ((uint64_t*)next_pml_addr)[pml3_part] & PAGE_ENTRY_ADDRESS_MASK;
            uint64_t pml2_part = (page_base_address & PML2_MASK) >> 21; // units of 2MB

            // PML2/PD: 2MB
            if(((uint64_t*)next_pml_addr)[pml2_part] & (1ULL << 7))
            {

              if(isFirstPage)
              {
                // Check to make sure memory region start is hardware page base aligned to this size
                uint64_t pml_base_address = ((uint64_t*)next_pml_addr)[pml2_part] & PML2_ADDRESS_MASK;
                if(page_base_address != pml_base_address)
                {
                  warning_printf("vset_region_hwpages: %#qx is not the page base address for this page,\r\nthis is: %#qx. Please try again with the correct address.\r\n", page_base_address, pml_base_address);
                  return 3;
                }

                isFirstPage = 0;
              }

              if(regionpages < (1ULL << 9))
              {
                warning_printf("Error: Region at base address %#qx does not cover entire 2MB page. (4-lvl)\r\n", page_base_address);
                warning_printf("Beware that some hardware pages of this region passed to vset_region_hwpages() may have\r\n");
                warning_printf("already been set. Recommendation is to immediately run vset_region_hwpages() again on the\r\n");
                warning_printf("same area with its prior values, and then reallocate the region with a size\r\n");
                warning_printf("that consumes all hardware pages encompassed by the region.\r\n");
                warning_printf("NOTE: Memory map attributes for the region have also not been updated.\r\n");
                return 2;
              }

              if(flags_or_entry)
              {
                ((uint64_t*)next_pml_addr)[pml2_part] = entry_flags;
              }
              else
              {
                entry_flags &= PAGE_ENTRY_FLAGS_MASK;
                entry_flags |= (((uint64_t*)next_pml_addr)[pml2_part] & PAGE_ENTRY_ADDRESS_MASK);
                ((uint64_t*)next_pml_addr)[pml2_part] = entry_flags;
              }

              regionpages -= 1ULL << 9; // (1ULL << 21) >> 12
              page_base_address += 1ULL << 21; // Get the next aligned address in the set
            }
            else
            {
              // Find the page_base_address entry...
              next_pml_addr = ((uint64_t*)next_pml_addr)[pml2_part] & PAGE_ENTRY_ADDRESS_MASK;
              uint64_t pml1_part = (page_base_address & PML1_MASK) >> 12; // units of 4kB

              // PML1/PT: 4kB
              // Can't get here if address isn't both 4kB-aligned and PhysicalStart, so no incorrect page base address error here
              // regionpages cannot be < 1, so no error message here

              if(flags_or_entry)
              {
                ((uint64_t*)next_pml_addr)[pml1_part] = entry_flags;
              }
              else
              {
                entry_flags &= PAGE_ENTRY_FLAGS_MASK - 0x1000;
                entry_flags |= (((uint64_t*)next_pml_addr)[pml1_part] & PAGE_ENTRY_ADDRESS_MASK);
                ((uint64_t*)next_pml_addr)[pml1_part] = entry_flags;
              }

              regionpages -= 1ULL; // (1ULL << 12) >> 12
              page_base_address += 1ULL << 12; // Get the next aligned address in the set
            }
          }
        } // end while
      } // end 4-level

      // Finally, set the memory map attributes
      Piece->Attribute = attributes;

      // Whew!
      break;
    }
  }

  // Loop ended without a discovered address
  if(Piece >= (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize))
  {
    error_printf("vset_region_hwpages: Could not find page base address. It may not be aligned or allocated.\r\n");
    return 1;
  }

  return 0;
}

//----------------------------------------------------------------------------------------------------------------------------------
//  VerifyZeroMem: Verify Memory Is Free
//----------------------------------------------------------------------------------------------------------------------------------
//
// Return 0 if desired section of memory is zeroed (for use in "if" statements)
//

uint8_t VerifyZeroMem(size_t NumBytes, uint64_t BaseAddr) // BaseAddr is a 64-bit unsigned int whose value is the memory address
{
  for(size_t verify_increment = 0; verify_increment < NumBytes; verify_increment++)
  {
    if(*(uint8_t*)(BaseAddr + verify_increment) != 0)
    {
      return 1;
    }
  }
  return 0;
}

//----------------------------------------------------------------------------------------------------------------------------------
//  GetMaxMappedPhysicalAddress: Get the Maximum Physical Address in the Memory Map
//----------------------------------------------------------------------------------------------------------------------------------
//
// Returns the highest physical address reported by the UEFI memory map, which is useful when working around memory holes and setting
// up/working with paging. The returned value is 1 byte over the last usable address, meaning it is the total size of the physical
// address space. Subtract 1 byte from the returned value to get the maximum usable mapped physical address.
//
// 0 will only be returned if there aren't any entries in the map, which should never happen anyways.
//

uint64_t GetMaxMappedPhysicalAddress(void)
{
  EFI_MEMORY_DESCRIPTOR * Piece;
  uint64_t current_address = 0, max_address = 0;

  // Go through the system memory map, adding the page sizes to PhysicalStart. Returns the largest number found, which should be the maximum addressable memory location based on installed RAM size.
  for(Piece = Global_Memory_Info.MemMap; Piece < (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize))
  {
    current_address = Piece->PhysicalStart + EFI_PAGES_TO_SIZE(Piece->NumberOfPages);
    if(current_address > max_address)
    {
      max_address = current_address;
    }
  }

  return max_address;
}

//----------------------------------------------------------------------------------------------------------------------------------
//  GetVisibleSystemRam: Calculate Total Visible System RAM
//----------------------------------------------------------------------------------------------------------------------------------
//
// Calculates the total visible (not hardware- or firmware-reserved) system RAM from the UEFI system memory map. This is mainly meant
// to help identify any memory holes in the installed RAM (e.g. there's often one at 0xA0000 to 0xFFFFF). In other words, this is the
// amount of installed RAM that an OS can actually work with. Windows' msinfo32 utility calls this same value "Total Physical Memory."
//
// Value returned is in bytes.
//

uint64_t GetVisibleSystemRam(void)
{
  EFI_MEMORY_DESCRIPTOR * Piece;
  uint64_t running_total = 0;

  for(Piece = Global_Memory_Info.MemMap; Piece < (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize))
  {
    if(
        (Piece->Type != EfiMemoryMappedIO) &&
        (Piece->Type != EfiMemoryMappedIOPortSpace) &&
        (Piece->Type != EfiPalCode) &&
        (Piece->Type != EfiPersistentMemory) &&
        (Piece->Type != EfiMaxMemoryType)
      )
    {
      running_total += EFI_PAGES_TO_SIZE(Piece->NumberOfPages);
    }
  }

  return running_total;
}

//----------------------------------------------------------------------------------------------------------------------------------
//  GetFreeSystemRam: Calculate Total Free System RAM
//----------------------------------------------------------------------------------------------------------------------------------
//
// Calculates the total EfiConventionalMemory from the UEFI system memory map.
//

uint64_t GetFreeSystemRam(void)
{
  EFI_MEMORY_DESCRIPTOR * Piece;
  uint64_t running_total = 0;

  for(Piece = Global_Memory_Info.MemMap; Piece < (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize))
  {
    if(Piece->Type == EfiConventionalMemory)
    {
      running_total += EFI_PAGES_TO_SIZE(Piece->NumberOfPages);
    }
  }

  return running_total;
}

//----------------------------------------------------------------------------------------------------------------------------------
//  GetFreePersistentRam: Calculate Total Free Non-Volatile System RAM
//----------------------------------------------------------------------------------------------------------------------------------
//
// Calculates the total EfiPersistentMemory from the UEFI system memory map.
//

uint64_t GetFreePersistentRam(void)
{
  EFI_MEMORY_DESCRIPTOR * Piece;
  uint64_t running_total = 0;

  for(Piece = Global_Memory_Info.MemMap; Piece < (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize))
  {
    if(Piece->Type == EfiPersistentMemory)
    {
      running_total += EFI_PAGES_TO_SIZE(Piece->NumberOfPages);
    }
  }

  return running_total;
}

//----------------------------------------------------------------------------------------------------------------------------------
//  GetInstalledSystemRam: Get Total Installed System Ram
//----------------------------------------------------------------------------------------------------------------------------------
//
// Uses SMBIOS tables to report the installed RAM quantity. The UEFI/BIOS calculates the installed memory by directly reading the SPD
// EEPROM, but not all system vendors correctly report system configuration information via SMBIOS, which does have a field for memory.
// So if SMBios doesn't work, this falls back to GuessInstalledSystemRam().
//
// In Windows' msinfo32 utility, the "Installed Physical Memory (RAM)" value is what this number is, and it's really just for
// informational purposes (the OS itself cares much more about the entire physical address space, not just the RAM part of it).
//
// Value returned is in bytes.
//

uint64_t GetInstalledSystemRam(EFI_CONFIGURATION_TABLE * ConfigurationTables, UINTN NumConfigTables)
{
  uint64_t systemram = 0;
  uint8_t smbiostablefound = 0;

  for(uint64_t configtable_iter = 0; configtable_iter < NumConfigTables; configtable_iter++)
  {
    if(!(AVX_memcmp(&ConfigurationTables[configtable_iter].VendorGuid, &Smbios3TableGuid, 16, 0)))
    {
      printf("SMBIOS 3.x table found!\r\n");
      smbiostablefound = 3;

       SMBIOS_TABLE_3_0_ENTRY_POINT * smb3_entry = (SMBIOS_TABLE_3_0_ENTRY_POINT*)ConfigurationTables[configtable_iter].VendorTable;
       SMBIOS_STRUCTURE * smb_header = (SMBIOS_STRUCTURE*)smb3_entry->TableAddress;
       uint8_t* smb3_end = (uint8_t *)(smb3_entry->TableAddress + (uint64_t)smb3_entry->TableMaximumSize);

       while((uint8_t*)smb_header < smb3_end)
       {
         if(smb_header->Type == 17) // Memory socket/device
         {
           uint16_t smb_socket_size = ((SMBIOS_TABLE_TYPE17 *)smb_header)->Size;
           if(smb_socket_size == 0x7FFF) // Need extended size, which is always given in MB units
           {
             uint32_t smb_extended_socket_size = ((SMBIOS_TABLE_TYPE17 *)smb_header)->ExtendedSize;
             systemram += (uint64_t)smb_extended_socket_size << 20;
           }
           else if(smb_socket_size != 0xFFFF)
           {
             if(smb_socket_size & 0x8000) // KB units
             {
               systemram += (uint64_t)smb_socket_size << 10;
             }
             else // MB units
             {
               systemram += (uint64_t)smb_socket_size << 20;
             }
           }
           // Otherwise size is unknown (0xFFFF), don't add it.
         }

         smb_header = (SMBIOS_STRUCTURE*)((uint64_t)smb_header + smb_header->Length);
         while(*(uint16_t*)smb_header != 0x0000) // Check for double null, meaning end of string set
         {
           smb_header = (SMBIOS_STRUCTURE*)((uint64_t)smb_header + 1);
         }
         smb_header = (SMBIOS_STRUCTURE*)((uint64_t)smb_header + 2); // Found end of current structure, move to start of next one
       }

      break;
    }
  }

  if(smbiostablefound != 3)
  {
    for(uint64_t configtable_iter = 0; configtable_iter < NumConfigTables; configtable_iter++)
    {
      if(!(AVX_memcmp(&ConfigurationTables[configtable_iter].VendorGuid, &SmbiosTableGuid, 16, 0)))
      {
        printf("SMBIOS table found!\r\n");
        smbiostablefound = 1;

        SMBIOS_TABLE_ENTRY_POINT * smb_entry = (SMBIOS_TABLE_ENTRY_POINT*)ConfigurationTables[configtable_iter].VendorTable;
        SMBIOS_STRUCTURE * smb_header = (SMBIOS_STRUCTURE*)((uint64_t)smb_entry->TableAddress);
        uint8_t* smb_end = (uint8_t *)((uint64_t)smb_entry->TableAddress + (uint64_t)smb_entry->TableLength);

        while((uint8_t*)smb_header < smb_end)
        {
          if(smb_header->Type == 17) // Memory socket/device
          {
            uint16_t smb_socket_size = ((SMBIOS_TABLE_TYPE17 *)smb_header)->Size;
            if(smb_socket_size == 0x7FFF) // Need extended size, which is always given in MB units
            {
              uint32_t smb_extended_socket_size = ((SMBIOS_TABLE_TYPE17 *)smb_header)->ExtendedSize;
              systemram += (uint64_t)smb_extended_socket_size << 20;
            }
            else if(smb_socket_size != 0xFFFF)
            {
              if(smb_socket_size & 0x8000) // KB units
              {
                systemram += (uint64_t)smb_socket_size << 10;
              }
              else // MB units
              {
                systemram += (uint64_t)smb_socket_size << 20;
              }
            }
            // Otherwise size is unknown (0xFFFF), don't add it.
          }

          smb_header = (SMBIOS_STRUCTURE*)((uint64_t)smb_header + smb_header->Length);
          while(*(uint16_t*)smb_header != 0x0000) // Check for double null, meaning end of string set
          {
            smb_header = (SMBIOS_STRUCTURE*)((uint64_t)smb_header + 1);
          }
          smb_header = (SMBIOS_STRUCTURE*)((uint64_t)smb_header + 2); // Found end of current structure, move to start of next one
        }

        break;
      }
    }
  }

  if(systemram < GetVisibleSystemRam())
  {
    info_printf("No SMBIOS tables or incorrect SMBIOS data found. Approximating RAM...\r\n");
    systemram = GuessInstalledSystemRam();
  }

  return systemram;
}

//----------------------------------------------------------------------------------------------------------------------------------
//  GuessInstalledSystemRam: Attempt to Infer Total Installed System Ram
//----------------------------------------------------------------------------------------------------------------------------------
//
// Infers a value for the total installed system RAM from the UEFI memory map. This is basically an attempt to account for memory
// holes that aren't remapped by the motherboard chipset. The UEFI/BIOS calculates the installed memory by directly reading the SPD
// EEPROM, but not all system vendors correctly report system configuration information via SMBIOS, which does have a field for memory.
//
// This would be useful for systems that don't have a reliable way to get RAM size from the firmware.
//
// Value returned is in bytes.
//

uint64_t GuessInstalledSystemRam(void)
{
  uint64_t ram = GetVisibleSystemRam();
  ram += (63 << 20); // The minimum DDR3 size is 64MB, so it seems like a reasonable offset.
  return (ram & ~((64 << 20) - 1)); // This method will discard quantities < 64MB, but no one using x86 these days should be so limited.
}

//----------------------------------------------------------------------------------------------------------------------------------
//  print_system_memmap: The Ultimate Debugging Tool
//----------------------------------------------------------------------------------------------------------------------------------
//
// Get the system memory map, parse it, and print it. Print the whole thing.
//

/* Reminder of EFI_MEMORY_DESCRIPTOR format:

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

// This array should match the EFI_MEMORY_TYPE enum in EfiTypes.h. If it doesn't, maybe the spec changed and this needs to be updated.
// This is a file scope global variable, which lets it be declared static. This prevents a stack overflow that could arise if it were
// local to its function of use. Static arrays defined like this can actually be made very large, but they cannot be accessed by any
// functions that are not explicitly defined in this file. It cannot be passed as an argument to outside functions, either.
static const char mem_types[20][27] = {
    "EfiReservedMemoryType     ",
    "EfiLoaderCode             ",
    "EfiLoaderData             ",
    "EfiBootServicesCode       ",
    "EfiBootServicesData       ",
    "EfiRuntimeServicesCode    ",
    "EfiRuntimeServicesData    ",
    "EfiConventionalMemory     ",
    "EfiUnusableMemory         ",
    "EfiACPIReclaimMemory      ",
    "EfiACPIMemoryNVS          ",
    "EfiMemoryMappedIO         ",
    "EfiMemoryMappedIOPortSpace",
    "EfiPalCode                ",
    "EfiPersistentMemory       ",
    "EfiMaxMemoryType          ",
    "malloc                    ", // EfiMaxMemoryType + 1
    "vmalloc                   ", // EfiMaxMemoryType + 2
    "Memory Map                ", // EfiMaxMemoryType + 3
    "Page Tables               "  // EfiMaxMemoryType + 4
};

void print_system_memmap(void)
{
  EFI_MEMORY_DESCRIPTOR * Piece;
  uint16_t line = 0;

  printf("MemMap %#qx, MemMapSize: %qu, MemMapDescriptorSize: %qu, MemMapDescriptorVersion: %u\r\n", Global_Memory_Info.MemMap, Global_Memory_Info.MemMapSize, Global_Memory_Info.MemMapDescriptorSize, Global_Memory_Info.MemMapDescriptorVersion);

  // Multiply NumOfPages by EFI_PAGE_SIZE or do (NumOfPages << EFI_PAGE_SHIFT) to get the end address... which should just be the start of the next section.
  for(Piece = Global_Memory_Info.MemMap; Piece < (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize))
  {
    if(line%20 == 0)
    {
      printf("#   Memory Type                 Phys Addr Start      Virt Addr Start  Num Of Pages   Attr\r\n");
    }

    printf("%2hu: %s 0x%016qx   0x%016qx %#qx %#qx\r\n", line, mem_types[Piece->Type], Piece->PhysicalStart, Piece->VirtualStart, Piece->NumberOfPages, Piece->Attribute);
    line++;
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
//  Set_Identity_VMAP: Set Virtual Address Map to Identity Mapping
//----------------------------------------------------------------------------------------------------------------------------------
//
// Get the system memory map, parse it, identity map it, and set the virtual address map accordingly.
// Identity mapping means Physical Address == Virtual Address, also called a 1:1 (one-to-one) map
//
// Returns ~0ULL as a pointer if a failure is encountered.
//

__attribute__((target("no-sse"))) EFI_MEMORY_DESCRIPTOR * Set_Identity_VMAP(EFI_RUNTIME_SERVICES * RTServices)
{
  EFI_MEMORY_DESCRIPTOR * Piece;

  for(Piece = Global_Memory_Info.MemMap; Piece < (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize))
  {
    Piece->VirtualStart = Piece->PhysicalStart;
  }

  if(EFI_ERROR(RTServices->SetVirtualAddressMap(Global_Memory_Info.MemMapSize, Global_Memory_Info.MemMapDescriptorSize, Global_Memory_Info.MemMapDescriptorVersion, Global_Memory_Info.MemMap)))
  {
    // This function gets called too early for print
    // warning_printf("Error setting VMAP. Returning NULL.\r\n");
    return (EFI_MEMORY_DESCRIPTOR*)~0ULL;
  }

  return Global_Memory_Info.MemMap;
}

//----------------------------------------------------------------------------------------------------------------------------------
//  Setup_MemMap: Prepare the Memory Map for Use with Allocators
//----------------------------------------------------------------------------------------------------------------------------------
//
// Take UEFI's memory map and modify it to include the memory map's own location. This prepares it for use with memory management.
//

void Setup_MemMap(void)
{
  // Make a new memory map with the location of the map itself, which is needed to use malloc() and pagetable.
  EFI_MEMORY_DESCRIPTOR * Piece;
  size_t numpages = EFI_SIZE_TO_PAGES(Global_Memory_Info.MemMapSize + Global_Memory_Info.MemMapDescriptorSize); // Need enough space to contain the map + one additional descriptor (for the map itself)

  // Map's gettin' evicted, gotta relocate.
  EFI_PHYSICAL_ADDRESS new_MemMap_base_address = ActuallyFreeAddress(numpages, 0); // This will only give addresses at the base of a chunk of EfiConventionalMemory
  if(new_MemMap_base_address == ~0ULL)
  {
    error_printf("Setup_MemMap: Can't move MemMap for enlargement: Out of memory, memory subsystem not usable.\r\n");
    HaCF();
  }
  else
  {
    EFI_MEMORY_DESCRIPTOR * new_MemMap = (EFI_MEMORY_DESCRIPTOR*)new_MemMap_base_address;
    // Zero out the new memmap destination
    AVX_memset(new_MemMap, 0, numpages << EFI_PAGE_SHIFT);

    // Move (copy) the map from MemMap to new_MemMap
    AVX_memmove(new_MemMap, Global_Memory_Info.MemMap, Global_Memory_Info.MemMapSize);
    // Zero out the old one
    AVX_memset(Global_Memory_Info.MemMap, 0, Global_Memory_Info.MemMapSize);

    // Update Global_Memory_Info MemMap location with new address
    Global_Memory_Info.MemMap = new_MemMap;

    // Get a pointer for the descriptor corresponding to the new location of the map (scan the map to find it)
    for(Piece = Global_Memory_Info.MemMap; (uint8_t*)Piece < ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize))
    {
      if(Piece->PhysicalStart == new_MemMap_base_address)
      { // Found it, Piece holds the spot now. Also, we know the map base is at Piece->PhysicalStart of an EfiConventionalMemory area because we put it there with ActuallyFreeAddress.
        break;
      }

      /*
      // This commented-out code would be used instead of the above conditional if, for some reason, the new address is situated not at the PhysicalStart boundary.
      if((Piece->Type == EfiConventionalMemory) && (Piece->NumberOfPages >= numpages)) // The new map's in EfiConventionalMemory
      {
        EFI_PHYSICAL_ADDRESS PhysicalEnd = Piece->PhysicalStart + (Piece->NumberOfPages << EFI_PAGE_SHIFT); // Get the end of this range for bounds checking (this value might be the start address of the next range)

        if(
            ((uint8_t*)Global_Memory_Info.MemMap >= (uint8_t*)Piece->PhysicalStart)
            &&
            (((uint8_t*)Global_Memory_Info.MemMap + (numpages << EFI_PAGE_SHIFT)) <= (uint8_t*)PhysicalEnd)
          ) // Bounds check
        {
          break; // Found it, Piece holds the spot now. Also, we know it's at the start of Piece->PhysicalStart because we put it there with ActuallyFreeAddress.
        }
      }
      */

    }

    if((uint8_t*)Piece == ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize)) // This will be true if the loop didn't break
    {
      error_printf("Setup_MemMap: MemMap not found.\r\n");
      HaCF();
    }
    else
    {
      // Mark the new area as memmap (it's currently EfiConventionalMemory)
      if(Piece->NumberOfPages == numpages) // Trivial case: The new space descriptor is just the right size and needs no splitting; saves a memory descriptor so MemMapSize doesn't need to be increased
      {
        Piece->Type = EfiMaxMemoryType + 3; // Special memmap type
        // Nothng to do for Pad, PhysicalStart, VirtualStart, NumberOfPages, and Attribute
      }
      else // Need to insert a memmap descriptor. Thanks to the way ActuallyFreeAddress works we know the map's new area is at the base of the EfiConventionalMemory descriptor
      {
        // Make a temporary descriptor to hold current piece's values, but modified for memmap
        EFI_MEMORY_DESCRIPTOR new_descriptor_temp;
        new_descriptor_temp.Type = EfiMaxMemoryType + 3; // Special memmap type
        new_descriptor_temp.Pad = Piece->Pad;
        new_descriptor_temp.PhysicalStart = Piece->PhysicalStart;
        new_descriptor_temp.VirtualStart = Piece->VirtualStart;
        new_descriptor_temp.NumberOfPages = numpages;
        new_descriptor_temp.Attribute = Piece->Attribute;

        // Modify this descriptor (shrink it) to reflect its new values
        Piece->PhysicalStart += (numpages << EFI_PAGE_SHIFT);
        Piece->VirtualStart += (numpages << EFI_PAGE_SHIFT);
        Piece->NumberOfPages -= numpages;

        // Move (copy) the whole memmap that's above this piece (including this freshly modified piece) from this piece to one MemMapDescriptorSize over
        AVX_memmove((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize, Piece, ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize) - (uint8_t*)Piece); // Pointer math to get size

        // Insert the new piece (by overwriting the now-duplicated entry with new values)
        // I.e. turn this piece into what was stored in the temporary descriptor above
        Piece->Type = new_descriptor_temp.Type;
        Piece->Pad = new_descriptor_temp.Pad;
        Piece->PhysicalStart = new_descriptor_temp.PhysicalStart;
        Piece->VirtualStart = new_descriptor_temp.VirtualStart;
        Piece->NumberOfPages = new_descriptor_temp.NumberOfPages;
        Piece->Attribute = new_descriptor_temp.Attribute;

        // Update Global_Memory_Info with new MemMap size
        Global_Memory_Info.MemMapSize += Global_Memory_Info.MemMapDescriptorSize;
      }
      // Done modifying new map.
    }
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
//  MemMap_Prep: Check Memory Map for Space before Modifying It
//----------------------------------------------------------------------------------------------------------------------------------
//
// Check if the memory map has enough free space in its memory area for additional descriptors. If not, move it somewhere that does.
// This should be called before doing anything that adds descriptors, as is done in malloc(), vmalloc(), and pagetable_alloc().
//
// num_additional_descriptors: The number of extra descriptors space is needed for (usually 1, except [v]malloc, which asks for 2)
//
// Returns 0 on success
//
// NOTE: Any function call that includes a call to MemMap_Prep() will render variables holding memory map addresses unusable
// afterwards. This is simply because it can no longer be guaranteed that the memory map is in the same place as before;
// MemMap_Prep() might have changed its location. This is very important to keep in mind when creating functions like realloc() and
// also when working with multiple cores.
//
// A list of functions that call MemMap_Prep():
// - pagetable_alloc()
// - AllocateFreeAddress()
// - VAllocateFreeAddress()
// - All variants of malloc() (i.e. malloc(), mallocX(), vmalloc(), vmallocX(), calloc(), realloc(), vcalloc(), and vrealloc(), as they call the [V]AllocateFreeAddress() functions)
//

uint64_t MemMap_Prep(uint64_t num_additional_descriptors)
{
  // Need enough space to contain the map + "num_additional_descriptors" additional descriptors
  size_t numpages = EFI_SIZE_TO_PAGES(Global_Memory_Info.MemMapSize + num_additional_descriptors*Global_Memory_Info.MemMapDescriptorSize);
  size_t orig_numpages = EFI_SIZE_TO_PAGES(Global_Memory_Info.MemMapSize);

  if(numpages > orig_numpages)
  { // Need to move the map somewhere with more pages available. Don't want to play any fragmentation games with it.

    EFI_MEMORY_DESCRIPTOR * Piece;

    // Find the memmap's descriptor in the memmap
    for(Piece = Global_Memory_Info.MemMap; (uint8_t*)Piece < ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize))
    {
      if(Piece->Type == (EfiMaxMemoryType + 3)) // MemMap type
      { // Found it, Piece holds the spot now. Also, we know the map base is at Piece->PhysicalStart of an EfiConventionalMemory area because we put it there with ActuallyFreeAddress.
        break;
      }
    }

    if((uint8_t*)Piece == ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize)) // This will be true if the loop didn't break
    {
      error_printf("MemMap_Prep: MemMap not found. Has it not been set up yet?\r\n");
      HaCF();
    }
    else // Found memmap, so no issues
    {
      size_t additional_numpages = numpages - orig_numpages;
      // If the area right after the memory map is EfiConventionalMemory, we might be able to just take some pages from there...

      // Check if there's an EfiConventionalMemory region adjacent in memory to the memmap region
      EFI_MEMORY_DESCRIPTOR * Next_Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize); // Remember... sizeof(EFI_MEMORY_DESCRIPTOR) != MemMapDescriptorSize :/
      EFI_PHYSICAL_ADDRESS PhysicalEnd = Piece->PhysicalStart + (Piece->NumberOfPages << EFI_PAGE_SHIFT); // Get the end of this range, which may be the start of another range

      // Quick check for adjacency that will skip scanning the whole memmap
      // Can provide a little speed boost for ordered memory maps
      if(
          (Next_Piece->PhysicalStart != PhysicalEnd)
          ||
          ((uint8_t*)Next_Piece == ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize))
        ) // This OR check protects against the obscure case of there being a rogue entry adjacent the end of the memory map that describes a valid EfiConventionalMemory entry
      {
        // See if PhysicalEnd matches any PhysicalStart for unordered maps
        for(Next_Piece = Global_Memory_Info.MemMap; Next_Piece < (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Next_Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Next_Piece + Global_Memory_Info.MemMapDescriptorSize))
        {
          if(PhysicalEnd == Next_Piece->PhysicalStart)
          {
            // Found one
            break;
          }
        }
      }

      // Is the next piece an EfiConventionalMemory type?
      if(
          (Next_Piece->Type == EfiConventionalMemory) && (Next_Piece->NumberOfPages >= additional_numpages)
          &&
          (uint8_t*)Next_Piece != ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize)
        ) // Check if the loop didn't break (this also covers the case where the memory map is the last valid entry in the map)
      {
        if(Next_Piece->NumberOfPages > additional_numpages)
        {
          // Modify MemMap's entry
          Piece->NumberOfPages = numpages;

          // Modify adjacent EfiConventionalMemory's entry
          Next_Piece->NumberOfPages -= additional_numpages;
          Next_Piece->PhysicalStart += (additional_numpages << EFI_PAGE_SHIFT);
          Next_Piece->VirtualStart += (additional_numpages << EFI_PAGE_SHIFT);

          // Done
        }
        else if(Next_Piece->NumberOfPages == additional_numpages)
        { // If the next piece is exactly the number of pages, we can claim it and reclaim a descriptor
          // This just means that MemMap will have some wiggle room for the next time this function is used.

          // Modify MemMap's entry
          Piece->NumberOfPages = numpages;

          // Erase the claimed descriptor
          // Zero out the piece we just claimed
          AVX_memset(Next_Piece, 0, Global_Memory_Info.MemMapDescriptorSize); // Next_Piece is a pointer
          AVX_memmove(Next_Piece, (uint8_t*)Next_Piece + Global_Memory_Info.MemMapDescriptorSize, ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize) - ((uint8_t*)Next_Piece + Global_Memory_Info.MemMapDescriptorSize));

          // Update Global_Memory_Info
          Global_Memory_Info.MemMapSize -= Global_Memory_Info.MemMapDescriptorSize;

          // Zero out the entry that used to be at the end of the map
          AVX_memset((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize, 0, Global_Memory_Info.MemMapDescriptorSize);

          // Done
        }
        // There is no else, as there are only > and == cases.
        else
        {
          error_printf("MemMap_Prep: What kind of sorcery is this? Seeing this means there's a bug in MemMap_Prep.\r\n");
          HaCF();
        }
      } // End convenient conditions
      else // Nope, need to move it altogether
      {
        // Map's gettin' evicted, gotta relocate.

        // Need 2 descriptors' worth of additional space in case old memmap location becomes an unmergeable EfiConventionalMemory "island"
        numpages = EFI_SIZE_TO_PAGES(Global_Memory_Info.MemMapSize + (Global_Memory_Info.MemMapDescriptorSize << 1));

        EFI_PHYSICAL_ADDRESS new_MemMap_base_address = ActuallyFreeAddress(numpages, 0); // This will only give addresses at the base of a chunk of EfiConventionalMemory
        if(new_MemMap_base_address == ~0ULL)
        {
          error_printf("MemMap_Prep: Can't move memmap for enlargement: Out of memory\r\n");
          return ~0ULL;
        }
        else
        {
          EFI_MEMORY_DESCRIPTOR * new_MemMap = (EFI_MEMORY_DESCRIPTOR*)new_MemMap_base_address;
          // Zero out the new memmap destination
          AVX_memset(new_MemMap, 0, numpages << EFI_PAGE_SHIFT);

          // Mark the old one as EfiConventionalMemory
          // Do this now because any later Piece is not guaranteed to point to the old memmap descriptor in the new map
          // It's a lot faster to do it here than to look for it later when there's another memmap entry
          // For multi-core stuff, all other processors need to be prevented from accessing the map during this whole function
          Piece->Type = EfiConventionalMemory;

          // Move (copy) the map from MemMap to new_MemMap
          AVX_memmove(new_MemMap, Global_Memory_Info.MemMap, Global_Memory_Info.MemMapSize);
          // Zero out the old one
          AVX_memset(Global_Memory_Info.MemMap, 0, Global_Memory_Info.MemMapSize);

          // Update Global_Memory_Info MemMap location with new address
          Global_Memory_Info.MemMap = new_MemMap;

          // Get a pointer for the descriptor corresponding to the new location of the map (scan the map to find it)
          for(Piece = Global_Memory_Info.MemMap; (uint8_t*)Piece < ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize))
          {
            if(Piece->PhysicalStart == new_MemMap_base_address)
            { // Found it, Piece holds the new spot now. Also, we know the map base is at Piece->PhysicalStart of an EfiConventionalMemory area because we put it there with ActuallyFreeAddress.
              break;
            }

            /*
            // This commented-out code would be used instead of the above conditional if, for some reason, the new address is situated not at the PhysicalStart boundary.
            if((Piece->Type == EfiConventionalMemory) && (Piece->NumberOfPages >= numpages)) // The new map's in EfiConventionalMemory
            {
              PhysicalEnd = Piece->PhysicalStart + (Piece->NumberOfPages << EFI_PAGE_SHIFT); // Get the end of this range for bounds checking (this value might be the start address of the next range)

              if(
                  ((uint8_t*)Global_Memory_Info.MemMap >= (uint8_t*)Piece->PhysicalStart)
                  &&
                  (((uint8_t*)Global_Memory_Info.MemMap + (numpages << EFI_PAGE_SHIFT)) <= (uint8_t*)PhysicalEnd)
                ) // Bounds check
              {
                break; // Found it, Piece holds the spot now. Also, we know it's at the start of Piece->PhysicalStart because we put it there with ActuallyFreeAddress.
              }
            }
            */
          }

          if((uint8_t*)Piece == ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize)) // This will be true if the loop didn't break
          {
            // If this ever happens, it's an emergency situation and everything needs to stop now since the map is gone.
            error_printf("MemMap_Prep: MemMap not found. Something's weird here...\r\n");
            HaCF();
          }
          else
          {
            // Mark the new area as memmap (it's currently EfiConventionalMemory)
            if(Piece->NumberOfPages == numpages) // Trivial case: The new space descriptor is just the right size and needs no splitting; saves a memory descriptor so MemMapSize doesn't need to be increased
            {
              Piece->Type = EfiMaxMemoryType + 3; // Special memmap type
              // Nothng to do for Pad, PhysicalStart, VirtualStart, NumberOfPages, and Attribute
            }
            else // Need to insert a memmap descriptor. Thanks to the way ActuallyFreeAddress works we know the map's new area is at the base of the EfiConventionalMemory descriptor
            {
              // Make a temporary descriptor to hold current piece's values, but modified for memmap
              EFI_MEMORY_DESCRIPTOR new_descriptor_temp;
              new_descriptor_temp.Type = EfiMaxMemoryType + 3; // Special memmap type
              new_descriptor_temp.Pad = Piece->Pad;
              new_descriptor_temp.PhysicalStart = Piece->PhysicalStart;
              new_descriptor_temp.VirtualStart = Piece->VirtualStart;
              new_descriptor_temp.NumberOfPages = numpages;
              new_descriptor_temp.Attribute = Piece->Attribute;

              // Modify this descriptor (shrink it) to reflect its new values
              Piece->PhysicalStart += (numpages << EFI_PAGE_SHIFT);
              Piece->VirtualStart += (numpages << EFI_PAGE_SHIFT);
              Piece->NumberOfPages -= numpages;

              // Move (copy) the whole memmap that's above this piece (including this freshly modified piece) from this piece to one MemMapDescriptorSize over
              AVX_memmove((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize, Piece, ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize) - (uint8_t*)Piece); // Pointer math to get size

              // Insert the new piece (by overwriting the now-duplicated entry with new values)
              // I.e. turn this piece into what was stored in the temporary descriptor above
              Piece->Type = new_descriptor_temp.Type;
              Piece->Pad = new_descriptor_temp.Pad;
              Piece->PhysicalStart = new_descriptor_temp.PhysicalStart;
              Piece->VirtualStart = new_descriptor_temp.VirtualStart;
              Piece->NumberOfPages = new_descriptor_temp.NumberOfPages;
              Piece->Attribute = new_descriptor_temp.Attribute;

              // Update Global_Memory_Info with new MemMap size
              Global_Memory_Info.MemMapSize += Global_Memory_Info.MemMapDescriptorSize;

              // Finally, merge EfiConventionalMemory if possible
              MergeContiguousConventionalMemory();
            }
            // Done modifying new map.
          }
        }

        // Done
      } // End move map conditions
    }

  }
  // Nothing to do otherwise, there's enough space in the last page for another descriptor

  // All done
  return 0;
}

//----------------------------------------------------------------------------------------------------------------------------------
//  pagetable_alloc: Allocate Memory for Page Tables
//----------------------------------------------------------------------------------------------------------------------------------
//
// Returns a 4k-aligned address of a region of size 'pagetable_size' specified for use by page tables
//
// EFI_PHYSICAL_ADDRESS is just a uint64_t.
//

EFI_PHYSICAL_ADDRESS pagetable_alloc(uint64_t pagetables_size)
{
  // All this does is take some EfiConventionalMemory and add one entry to the map

  // First, ensure memmap has enough space for another descriptor
  if(MemMap_Prep(1))
  {
    error_printf("pagetable_alloc: Could not prep memory map...\r\n");
    HaCF();
  }

  EFI_MEMORY_DESCRIPTOR * Piece;
  size_t numpages = EFI_SIZE_TO_PAGES(pagetables_size);

  EFI_PHYSICAL_ADDRESS pagetable_address = ActuallyFreeAddress(numpages, 0); // This will only give addresses at the base of a chunk of EfiConventionalMemory
  if(pagetable_address == ~0ULL)
  {
    error_printf("Not enough space for page tables. Unsafe to continue.\r\n");
    HaCF();
  }
  else
  {
    // Zero out the destination
    AVX_memset((void*)pagetable_address, 0, numpages << EFI_PAGE_SHIFT);

    // Get a pointer for the descriptor corresponding to the address (scan the memory map to find it)
    for(Piece = Global_Memory_Info.MemMap; (uint8_t*)Piece < ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize))
    {
      if(Piece->PhysicalStart == pagetable_address)
      { // Found it, Piece holds the spot now. Also, we know it's at the base of Piece->PhysicalStart of an EfiConventionalMemory area because we put it there with ActuallyFreeAddress.
        break;
      }
    }

    if((uint8_t*)Piece == ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize)) // This will be true if the loop didn't break
    {
      error_printf("Pagetable area not found. Unsafe to continue.\r\n");
      HaCF();
    }
    else
    {
      // Mark the new area as PageTables (it's currently EfiConventionalMemory)
      if(Piece->NumberOfPages == numpages) // Trivial case: The new space descriptor is just the right size and needs no splitting; saves a memory descriptor so MemMapSize doesn't need to be increased
      {
        Piece->Type = EfiMaxMemoryType + 4; // Special PageTables type
        // Nothng to do for Pad, PhysicalStart, VirtualStart, NumberOfPages, and Attribute
      }
      else // Need to insert a memmap descriptor. Thanks to the way ActuallyFreeAddress works we know the area is at the base of the EfiConventionalMemory descriptor
      {
        // Make a temporary descriptor to hold current piece's values, but modified for PageTables
        EFI_MEMORY_DESCRIPTOR new_descriptor_temp;
        new_descriptor_temp.Type = EfiMaxMemoryType + 4; // Special PageTables type
        new_descriptor_temp.Pad = Piece->Pad;
        new_descriptor_temp.PhysicalStart = Piece->PhysicalStart;
        new_descriptor_temp.VirtualStart = Piece->VirtualStart;
        new_descriptor_temp.NumberOfPages = numpages;
        new_descriptor_temp.Attribute = Piece->Attribute;

        // Modify this EfiConventionalMemory descriptor (shrink it) to reflect its new values
        Piece->PhysicalStart += (numpages << EFI_PAGE_SHIFT);
        Piece->VirtualStart += (numpages << EFI_PAGE_SHIFT);
        Piece->NumberOfPages -= numpages;

        // Move (copy) the whole memmap that's above this piece (including this freshly modified piece) from this piece to one MemMapDescriptorSize over
        AVX_memmove((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize, Piece, ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize) - (uint8_t*)Piece); // Pointer math to get size

        // Insert the new piece (by overwriting the now-duplicated entry with new values)
        // I.e. turn this piece into what was stored in the temporary descriptor above
        Piece->Type = new_descriptor_temp.Type;
        Piece->Pad = new_descriptor_temp.Pad;
        Piece->PhysicalStart = new_descriptor_temp.PhysicalStart;
        Piece->VirtualStart = new_descriptor_temp.VirtualStart;
        Piece->NumberOfPages = new_descriptor_temp.NumberOfPages;
        Piece->Attribute = new_descriptor_temp.Attribute;

        // Update Global_Memory_Info with new MemMap size
        Global_Memory_Info.MemMapSize += Global_Memory_Info.MemMapDescriptorSize;
      }
      // Done modifying map.
    }
  }

  return pagetable_address;
}


//----------------------------------------------------------------------------------------------------------------------------------
//  ActuallyFreeAddress: Find A Free Physical Memory Address, Bottom-Up
//----------------------------------------------------------------------------------------------------------------------------------
//
// Returns the next EfiConventionalMemory area that is >= the supplied OldAddress.
//
// pages: number of pages needed
// OldAddress: A baseline address to search bottom-up from
//

EFI_PHYSICAL_ADDRESS ActuallyFreeAddress(size_t pages, EFI_PHYSICAL_ADDRESS OldAddress)
{
  EFI_MEMORY_DESCRIPTOR * Piece;

  // Multiply NumberOfPages by EFI_PAGE_SIZE to get the end address... which should just be the start of the next section.
  // Check for EfiConventionalMemory in the map
  for(Piece = Global_Memory_Info.MemMap; Piece < (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize))
  {
    // Within each compatible EfiConventionalMemory, look for space
    if((Piece->Type == EfiConventionalMemory) && (Piece->NumberOfPages >= pages) && (Piece->PhysicalStart >= OldAddress))
    {
      break;
    }
  }

  // Loop ended without a discovered address
  if(Piece >= (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize))
  {
    // Return address -1
#ifdef MEMORY_CHECK_INFO
    error_printf("No more free physical addresses...\r\n");
#endif
    return ~0ULL;
  }

  return Piece->PhysicalStart;
}

//----------------------------------------------------------------------------------------------------------------------------------
//  ActuallyFreeAddressByPage: Find A Free Physical Memory Address, Bottom-Up, The Hard Way
//----------------------------------------------------------------------------------------------------------------------------------
//
// Returns the next 4kB page address marked as available (EfiConventionalMemory) that is > the supplied OldAddress.
//
// pages: number of pages needed
// OldAddress: A baseline address to search bottom-up from
//

EFI_PHYSICAL_ADDRESS ActuallyFreeAddressByPage(size_t pages, EFI_PHYSICAL_ADDRESS OldAddress)
{
  EFI_MEMORY_DESCRIPTOR * Piece;
  EFI_PHYSICAL_ADDRESS PhysicalEnd;
  EFI_PHYSICAL_ADDRESS DiscoveredAddress = ~0ULL;

  // Multiply NumberOfPages by EFI_PAGE_SIZE to get the end address... which should just be the start of the next section.
  // Check for EfiConventionalMemory in the map
  for(Piece = Global_Memory_Info.MemMap; Piece < (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize))
  {
    // Within each compatible EfiConventionalMemory, look for space
    if((Piece->Type == EfiConventionalMemory) && (Piece->NumberOfPages >= pages))
    {
      PhysicalEnd = Piece->PhysicalStart + (Piece->NumberOfPages << EFI_PAGE_SHIFT) - EFI_PAGE_MASK; // Get the end of this range, and use it to set a bound on the range (define a max returnable address)
      // (pages*EFI_PAGE_SIZE) or (pages << EFI_PAGE_SHIFT) gives the size the kernel would take up in memory
      if((OldAddress >= Piece->PhysicalStart) && ((OldAddress + (pages << EFI_PAGE_SHIFT)) < PhysicalEnd)) // Bounds check on OldAddress
      {
        // Return the next available page's address in the range. We need to go page-by-page for the really buggy systems.
        DiscoveredAddress = OldAddress + EFI_PAGE_SIZE; // Left shift EFI_PAGE_SIZE by 1 or 2 to check every 0x10 (16) or 0x100 (256) pages (must also modify the above PhysicalEnd bound check)
        break;
        // If PhysicalEnd == OldAddress, we need to go to the next EfiConventionalMemory range
      }
      else if(Piece->PhysicalStart > OldAddress) // Try a new range
      {
        DiscoveredAddress = Piece->PhysicalStart;
        break;
      }
    }
  }

#ifdef MEMORY_CHECK_INFO
  // Loop ended without a discovered address
  if(DiscoveredAddress == ~0ULL)
  {
    // Return address -1
    error_printf("No more free physical addresses by %llu-byte page...\r\n", EFI_PAGE_SIZE);
  }
#endif

  return DiscoveredAddress;
}

//----------------------------------------------------------------------------------------------------------------------------------
//  ActuallyAlignedFreeAddress: Find A Free, Aligned Physical Memory Address, Bottom-Up
//----------------------------------------------------------------------------------------------------------------------------------
//
// Returns the next physical address in an EfiConventionalMemory area that is >= the supplied OldAddress and is aligned to a specified boundary.
//
// pages: number of pages needed
// OldAddress: A baseline address to search bottom-up from
// byte_alignment: a desired alignment value in bytes. Valid sizes are power-of-2 multiples (e.g. 2x, 4x, 8x, 16x, 32x, etc.) of the UEFI page size (4096 bytes (4kB) in UEFI 2.x)
//

EFI_PHYSICAL_ADDRESS ActuallyAlignedFreeAddress(size_t pages, EFI_PHYSICAL_ADDRESS OldAddress, uintmax_t byte_alignment)
{
  EFI_MEMORY_DESCRIPTOR * Piece;
  EFI_PHYSICAL_ADDRESS PhysicalEnd;
  EFI_PHYSICAL_ADDRESS NewAddress;
  EFI_PHYSICAL_ADDRESS DiscoveredAddress = ~0ULL;

  if( (byte_alignment & EFI_PAGE_MASK) || (byte_alignment < EFI_PAGE_SIZE) ) // Needs to be a multiple of the memmap page size (4kB)
  {
    error_printf("ActuallyAlignedFreeAddress: Invalid byte alignment value.\r\nMultiple of EFI_PAGE_SIZE (4kB per UEFI 2.x spec) required.\r\n");
    return ~1ULL;
//    byte_alignment &= ~EFI_PAGE_MASK; // Force it to the nearest multiple of 4kB, rounding down such that, e.g., 6kb -> 4kB, 11kB -> 8kB, etc.
  }

  // Verify OldAddress alignment
  if(OldAddress & (byte_alignment - 1))
  {
    // Determine the next aligned address above OldAddress
    NewAddress = (OldAddress & ~(byte_alignment - 1)) + byte_alignment;
    // This is OK instead of erroring out because this is the point of OldAddress--it's just a baseline address to search upwards from
  }
  else
  {
    NewAddress = OldAddress;
  }

  // Multiply NumberOfPages by EFI_PAGE_SIZE to get the end address... which should just be the start of the next section.
  // Check for EfiConventionalMemory in the map
  for(Piece = Global_Memory_Info.MemMap; Piece < (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize))
  {
    // Within each compatible EfiConventionalMemory, look for space
    if((Piece->Type == EfiConventionalMemory) && (Piece->NumberOfPages >= pages) && (Piece->PhysicalStart >= OldAddress))
    {

      PhysicalEnd = Piece->PhysicalStart + (Piece->NumberOfPages << EFI_PAGE_SHIFT); // Get the end of this range (it's really the start of the next, adjacent range)

      if((Piece->PhysicalStart > NewAddress) || (NewAddress > PhysicalEnd)) // Region is > aligned address, or < due to out-of-order entries
      { // This works even if the memory map descriptors are not in order from lowest to highest address.
        // This framework works to keep it so that the map is ordered as handed off by UEFI: sometimes UEFI will stick an out of order
        // entry or two at the end, but those appear to always be EfiReservedMemoryType or EfiMemoryMappedIO, which doesn't pose a problem
        // since only EfiConventionalMemory regions and types that could turn into EfiConventionalMemory regions matter. As far as I've
        // been able to ascertain, these regions of interest are always ordered by UEFI from lowest to highest physical address.

        NewAddress = (Piece->PhysicalStart & ~(byte_alignment - 1)); // Make a new aligned address
        if(NewAddress == Piece->PhysicalStart) // Trivial case
        {
          DiscoveredAddress = NewAddress; // This new region's base address works!
          break;
        }
        else if( (NewAddress < Piece->PhysicalStart) && ((NewAddress + byte_alignment + (pages << EFI_PAGE_SHIFT)) <= PhysicalEnd) )
        {
          DiscoveredAddress = NewAddress + byte_alignment; // This new region has a compatible area!
          break;
        }
      }
      else if((Piece->PhysicalStart <= NewAddress) && ((NewAddress + (pages << EFI_PAGE_SHIFT)) <= PhysicalEnd)) // Is the aligned address in this region?
      {
        DiscoveredAddress = NewAddress; // The aligned address works!
        break;
      }

    }
  }

#ifdef MEMORY_CHECK_INFO
  // Loop ended without a discovered address
  if(DiscoveredAddress == ~0ULL)
  {
    // Return address -1
    error_printf("No more free physical addresses aligned by %llu bytes...\r\n", byte_alignment);
  }
#endif

  return DiscoveredAddress;
}

//----------------------------------------------------------------------------------------------------------------------------------
//  AllocateFreeAddress: Allocate A Free Physical Memory Address, Bottom-Up, Aligned
//----------------------------------------------------------------------------------------------------------------------------------
//
// Returns the next aligned physical address marked as available (in EfiConventionalMemory) that is > the supplied OldAddress.
//
// numbytes: number of bytes needed (NOTE: a size of 0 will return 1 page, not NULL, because 0x0 is a valid address)
// OldAddress: A baseline address to search bottom-up from
// byte_alignment: a desired alignment value in bytes. Valid sizes are power-of-2 multiples (e.g. 2x, 4x, 8x, 16x, 32x, etc.) of the UEFI page size (4096 bytes (4kB) in UEFI 2.x)
//
// UEFI memory maps have a page size of 4kB, which is the minimum allocatable size without resorting to further segmentation via
// memory pooling. This allows dynamically allocated memory to be incorporated right into the main memmap. This has the advantage of
// only requiring free(pointer)to free the descriptor made by this function. No extra sub-mapping, treeing, branching, binning,
// bucketing, carving, slicing, or stressing out over such complexity needed. If finer granularity is absolutely required for some
// reason, just make a struct that describes a 4kB area and typecast or convert the address/pointer returned by
// AllocateFreeAddress/malloc to it.
//
// NOTE: Max size of byte_alignment depends on quantity of installed RAM and how much of it is EfiConventionalMemory. Obviously it
// doesn't make sense to 512GB-align when there's < 512GB RAM, and something like that will just return ~0ULL (indicating no
// sufficient free area found).
//

EFI_PHYSICAL_ADDRESS AllocateFreeAddress(size_t numbytes, EFI_PHYSICAL_ADDRESS OldAddress, uintmax_t byte_alignment)
{
  // All this does is take some EfiConventionalMemory and add entries to the map

  // First, ensure memmap has enough space for 2 more descriptors (the worst-case scenario makes 2)
  uint64_t memmap_check = MemMap_Prep(2);
  if(memmap_check)
  {
    error_printf("AllocateFreeAddress (malloc): Could not prep memory map...\r\n");
    return memmap_check;
  }

  EFI_MEMORY_DESCRIPTOR * Piece;
  EFI_PHYSICAL_ADDRESS PhysicalEnd;
  size_t numpages = EFI_SIZE_TO_PAGES(numbytes);

  // Since NULL (or ((void *) 0), per its typedef) is actually a pointer to address 0x0, well, that's actually a valid return value here.
  // This means we can't return NULL on a size of zero, so instead this implementation rounds all sizes < 4096 bytes (1 UEFI page) to 1 page, including 0.
  if(numpages == 0)
  {
    numpages++;
  }

  EFI_PHYSICAL_ADDRESS alloc_address = ActuallyAlignedFreeAddress(numpages, OldAddress, byte_alignment);
  if(alloc_address == ~0ULL)
  {
    error_printf("Not enough space for AllocateFreeAddress (malloc). Unsafe to continue.\r\n");
    return alloc_address;
  }
  else if(alloc_address == ~1ULL)
  {
    error_printf("AllocateFreeAddress (malloc): Invalid byte alignment.\r\n");
    return alloc_address;
  }

  // Zero out the destination
  AVX_memset((void*)alloc_address, 0, numpages << EFI_PAGE_SHIFT);

  // Get a pointer for the descriptor corresponding to the address (scan the memory map to find it)
  for(Piece = Global_Memory_Info.MemMap; (uint8_t*)Piece < ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize))
  {
    PhysicalEnd = Piece->PhysicalStart + (Piece->NumberOfPages << EFI_PAGE_SHIFT); // Get the end of this range (it's really the start of the next, adjacent range)

    if( (Piece->PhysicalStart <= alloc_address) && ((alloc_address + (numpages << EFI_PAGE_SHIFT)) <= PhysicalEnd) )
    { // Found it, Piece holds the spot now.
      // Need to account for inserting a descriptor that might be for a region at the start of an EfiConventionalMemory chunk, at the end of an EfiConventionalMemory chunk, or somewhere in the middle of an EfiConventionalMemory chunk (requires 2 new descriptors)
      // ...Unless the trivial case is met, in which case 0 descriptors need to be added.

      // Mark the new area as malloc (it's currently EfiConventionalMemory)
      if(Piece->NumberOfPages == numpages) // Trivial case: The new space descriptor is just the right size and needs no splitting; saves a memory descriptor so MemMapSize doesn't need to be increased
      { // Modify 1 map entry, add no entries
        Piece->Type = EfiMaxMemoryType + 1; // Special malloc type
        // Nothng to do for Pad, PhysicalStart, VirtualStart, NumberOfPages, and Attribute
      }
      else if(alloc_address == Piece->PhysicalStart) // Need to insert a descriptor, alloc_address is the start of a region
      { // Modify 1 map entry, add 1 map entry

        // Make a temporary descriptor to hold current piece's values, but modified for malloc
        EFI_MEMORY_DESCRIPTOR new_descriptor_temp;
        new_descriptor_temp.Type = EfiMaxMemoryType + 1; // Special malloc type
        new_descriptor_temp.Pad = Piece->Pad;
        new_descriptor_temp.PhysicalStart = Piece->PhysicalStart;
        new_descriptor_temp.VirtualStart = Piece->VirtualStart;
        new_descriptor_temp.NumberOfPages = numpages;
        new_descriptor_temp.Attribute = Piece->Attribute;

        // Modify this EfiConventionalMemory descriptor (shrink it) to reflect its new values
        Piece->PhysicalStart += (numpages << EFI_PAGE_SHIFT);
        Piece->VirtualStart += (numpages << EFI_PAGE_SHIFT);
        Piece->NumberOfPages -= numpages;

        // Move (copy) the whole memmap that's above this piece (including this freshly modified piece) from this piece to one MemMapDescriptorSize over
        AVX_memmove((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize, Piece, ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize) - (uint8_t*)Piece); // Pointer math to get size

        // Insert the new piece (by overwriting the now-duplicated entry with new values)
        // I.e. turn this piece into what was stored in the temporary descriptor above
        Piece->Type = new_descriptor_temp.Type;
        Piece->Pad = new_descriptor_temp.Pad;
        Piece->PhysicalStart = new_descriptor_temp.PhysicalStart;
        Piece->VirtualStart = new_descriptor_temp.VirtualStart;
        Piece->NumberOfPages = new_descriptor_temp.NumberOfPages;
        Piece->Attribute = new_descriptor_temp.Attribute;

        // Update Global_Memory_Info with new MemMap size
        Global_Memory_Info.MemMapSize += Global_Memory_Info.MemMapDescriptorSize;
      }
      else if( (alloc_address + (numpages << EFI_PAGE_SHIFT)) == PhysicalEnd ) // Need to insert a descriptor, alloc_address + (numpages << EFI_PAGE_SHIFT) is the end of a region
      { // Modify 1 map entry, add 1 map entry

        // Make a temporary descriptor to hold current piece's values, but page size shrunken by page size of malloc area
        EFI_MEMORY_DESCRIPTOR new_descriptor_temp;
        new_descriptor_temp.Type = Piece->Type;
        new_descriptor_temp.Pad = Piece->Pad;
        new_descriptor_temp.PhysicalStart = Piece->PhysicalStart;
        new_descriptor_temp.VirtualStart = Piece->VirtualStart;
        new_descriptor_temp.NumberOfPages = Piece->NumberOfPages - numpages;
        new_descriptor_temp.Attribute = Piece->Attribute;

        // Modify this descriptor to reflect its new values (become the new entry)
        Piece->Type = EfiMaxMemoryType + 1; // Special malloc type
        // Nothing for pad
        Piece->PhysicalStart += (numpages << EFI_PAGE_SHIFT);
        Piece->VirtualStart += (numpages << EFI_PAGE_SHIFT);
        Piece->NumberOfPages = numpages;
        // Nothing for attribute

        // Move (copy) the whole memmap that's above this piece (including this freshly modified piece) from this piece to one MemMapDescriptorSize over
        AVX_memmove((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize, Piece, ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize) - (uint8_t*)Piece); // Pointer math to get size

        // Insert the "new" piece (by overwriting the now-duplicated entry with old values)
        // I.e. turn this piece into what was stored in the temporary descriptor above
        Piece->Type = new_descriptor_temp.Type;
        Piece->Pad = new_descriptor_temp.Pad;
        Piece->PhysicalStart = new_descriptor_temp.PhysicalStart;
        Piece->VirtualStart = new_descriptor_temp.VirtualStart;
        Piece->NumberOfPages = new_descriptor_temp.NumberOfPages;
        Piece->Attribute = new_descriptor_temp.Attribute;

        // Update Global_Memory_Info
        Global_Memory_Info.MemMapSize += Global_Memory_Info.MemMapDescriptorSize; // Only need to update total size here
      }
      else // alloc_address is somewhere in the middle of the entry
      { // Modify 1 map entry, add 2 map entries

        // How many pages do the region's surroundings take?
        size_t below_pages = (alloc_address - Piece->PhysicalStart) >> EFI_PAGE_SHIFT; // This should never lose information because UEFI's memory quanta is 4kB pages.
        size_t above_pages = Piece->NumberOfPages - numpages - below_pages;

        // Make a temporary descriptor to hold "below" segment
        EFI_MEMORY_DESCRIPTOR new_descriptor_temp_below;
        new_descriptor_temp_below.Type = Piece->Type;
        new_descriptor_temp_below.Pad = Piece->Pad;
        new_descriptor_temp_below.PhysicalStart = Piece->PhysicalStart;
        new_descriptor_temp_below.VirtualStart = Piece->VirtualStart;
        new_descriptor_temp_below.NumberOfPages = below_pages;
        new_descriptor_temp_below.Attribute = Piece->Attribute;

        // Make a temporary descriptor to hold "malloc" segment
        EFI_MEMORY_DESCRIPTOR new_descriptor_temp;
        new_descriptor_temp.Type = EfiMaxMemoryType + 1; // Special malloc type
        new_descriptor_temp.Pad = Piece->Pad;
        new_descriptor_temp.PhysicalStart = Piece->PhysicalStart + (below_pages << EFI_PAGE_SHIFT);
        new_descriptor_temp.VirtualStart = Piece->VirtualStart + (below_pages << EFI_PAGE_SHIFT);
        new_descriptor_temp.NumberOfPages = numpages;
        new_descriptor_temp.Attribute = Piece->Attribute;

        // Modify this descriptor's to become the "above" part (higher memory address part)
        // Type stays the same
        // Nothing for pad
        Piece->PhysicalStart += (below_pages + numpages) << EFI_PAGE_SHIFT;
        Piece->VirtualStart += (below_pages + numpages) << EFI_PAGE_SHIFT;
        Piece->NumberOfPages = above_pages;
        // Nothing for attribute

        // Move (copy) the whole memmap that's above this piece (including this freshly modified piece) from this piece to one MemMapDescriptorSize over
        AVX_memmove((uint8_t*)Piece + 2*Global_Memory_Info.MemMapDescriptorSize, Piece, ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize) - (uint8_t*)Piece); // Pointer math to get size

        // Insert the "below" piece into this now-tripled piece
        // I.e. turn this now-tripled piece into what was stored in the "below" temporary descriptor above
        Piece->Type = new_descriptor_temp_below.Type;
        Piece->Pad = new_descriptor_temp_below.Pad;
        Piece->PhysicalStart = new_descriptor_temp_below.PhysicalStart;
        Piece->VirtualStart = new_descriptor_temp_below.VirtualStart;
        Piece->NumberOfPages = new_descriptor_temp_below.NumberOfPages;
        Piece->Attribute = new_descriptor_temp_below.Attribute;

        // Insert the "malloc" piece into the next piece
        EFI_MEMORY_DESCRIPTOR * Next_Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize);

        // I.e. turn this now-duplicate piece, which sits between "above" and "below," into what was stored in the temporary "malloc" descriptor above
        Next_Piece->Type = new_descriptor_temp.Type;
        Next_Piece->Pad = new_descriptor_temp.Pad;
        Next_Piece->PhysicalStart = new_descriptor_temp.PhysicalStart;
        Next_Piece->VirtualStart = new_descriptor_temp.VirtualStart;
        Next_Piece->NumberOfPages = new_descriptor_temp.NumberOfPages;
        Next_Piece->Attribute = new_descriptor_temp.Attribute;

        // Update Global_Memory_Info
        Global_Memory_Info.MemMapSize += 2*Global_Memory_Info.MemMapDescriptorSize;
      }
      // Done modifying map.

      break;
    } // End "found it"
  } // End for

  if((uint8_t*)Piece == ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize)) // This will be true if the loop didn't break
  {
    error_printf("AllocateFreeAddress (malloc) area %#qx not found. Unsafe to continue program.\r\n", alloc_address);
    HaCF();
  }

  return alloc_address;
}

//----------------------------------------------------------------------------------------------------------------------------------
//  VActuallyFreeAddress: Find A Free Virtual Memory Address, Bottom-Up
//----------------------------------------------------------------------------------------------------------------------------------
//
// Returns the next EfiConventionalMemory area that is > the supplied OldAddress.
//
// pages: number of pages needed
// OldAddress: A baseline address to search bottom-up from
//

EFI_VIRTUAL_ADDRESS VActuallyFreeAddress(size_t pages, EFI_VIRTUAL_ADDRESS OldAddress)
{
  EFI_MEMORY_DESCRIPTOR * Piece;

  // Multiply NumberOfPages by EFI_PAGE_SIZE to get the end address... which should just be the start of the next section.
  // Check for EfiConventionalMemory in the map
  for(Piece = Global_Memory_Info.MemMap; Piece < (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize))
  {
    // Within each compatible EfiConventionalMemory, look for space
    if((Piece->Type == EfiConventionalMemory) && (Piece->NumberOfPages >= pages) && (Piece->VirtualStart >= OldAddress))
    {
      break;
    }
  }

  // Loop ended without a discovered address
  if(Piece >= (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize))
  {
    // Return address -1
#ifdef MEMORY_CHECK_INFO
    error_printf("No more free virtual addresses...\r\n");
#endif
    return ~0ULL;
  }

  return Piece->VirtualStart;
}

//----------------------------------------------------------------------------------------------------------------------------------
//  VActuallyFreeAddressByPage: Find A Free Virtual Memory Address, Bottom-Up, The Hard Way
//----------------------------------------------------------------------------------------------------------------------------------
//
// Returns the next 4kB page address marked as available (EfiConventionalMemory) that is > the supplied OldAddress.
//
// pages: number of pages needed
// OldAddress: A baseline address to search bottom-up from
//

EFI_VIRTUAL_ADDRESS VActuallyFreeAddressByPage(size_t pages, EFI_VIRTUAL_ADDRESS OldAddress)
{
  EFI_MEMORY_DESCRIPTOR * Piece;
  EFI_VIRTUAL_ADDRESS VirtualEnd;
  EFI_VIRTUAL_ADDRESS DiscoveredAddress = ~0ULL;

  // Multiply NumberOfPages by EFI_PAGE_SIZE to get the end address... which should just be the start of the next section.
  // Check for EfiConventionalMemory in the map
  for(Piece = Global_Memory_Info.MemMap; Piece < (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize))
  {
    // Within each compatible EfiConventionalMemory, look for space
    if((Piece->Type == EfiConventionalMemory) && (Piece->NumberOfPages >= pages))
    {
      VirtualEnd = Piece->VirtualStart + (Piece->NumberOfPages << EFI_PAGE_SHIFT) - EFI_PAGE_MASK; // Get the end of this range, and use it to set a bound on the range (define a max returnable address)
      // (pages*EFI_PAGE_SIZE) or (pages << EFI_PAGE_SHIFT) gives the size the kernel would take up in memory
      if((OldAddress >= Piece->VirtualStart) && ((OldAddress + (pages << EFI_PAGE_SHIFT)) < VirtualEnd)) // Bounds check on OldAddress
      {
        // Return the next available page's address in the range. We need to go page-by-page for the really buggy systems.
        DiscoveredAddress = OldAddress + EFI_PAGE_SIZE; // Left shift EFI_PAGE_SIZE by 1 or 2 to check every 0x10 (16) or 0x100 (256) pages (must also modify the above VirtualEnd bound check)
        break;
        // If VirtualEnd == OldAddress, we need to go to the next EfiConventionalMemory range
      }
      else if(Piece->VirtualStart > OldAddress) // Try a new range
      {
        DiscoveredAddress = Piece->VirtualStart;
        break;
      }
    }
  }

#ifdef MEMORY_CHECK_INFO
  // Loop ended without a discovered address
  if(DiscoveredAddress == ~0ULL)
  {
    // Return address -1
    error_printf("No more free virtual addresses by 4kB page...\r\n");
  }
#endif

  return DiscoveredAddress;
}

//----------------------------------------------------------------------------------------------------------------------------------
//  VActuallyAlignedFreeAddress: Find A Free, Aligned Virtual Memory Address, Bottom-Up
//----------------------------------------------------------------------------------------------------------------------------------
//
// Returns the next virtual address in an EfiConventionalMemory area that is >= the supplied OldAddress and is aligned to a specified boundary.
//
// pages: number of pages needed
// OldAddress: A baseline address to search bottom-up from
// byte_alignment: a desired alignment value in bytes. Valid sizes are power-of-2 multiples (e.g. 2x, 4x, 8x, 16x, 32x, etc.) of the UEFI page size (4096 bytes (4kB) in UEFI 2.x)
//

EFI_VIRTUAL_ADDRESS VActuallyAlignedFreeAddress(size_t pages, EFI_VIRTUAL_ADDRESS OldAddress, uintmax_t byte_alignment)
{
  EFI_MEMORY_DESCRIPTOR * Piece;
  EFI_VIRTUAL_ADDRESS VirtualEnd;
  EFI_VIRTUAL_ADDRESS NewAddress;
  EFI_VIRTUAL_ADDRESS DiscoveredAddress = ~0ULL;

  if( (byte_alignment & EFI_PAGE_MASK) || (byte_alignment < EFI_PAGE_SIZE) ) // Needs to be a multiple of the memmap page size (4kB)
  {
    error_printf("VActuallyAlignedFreeAddress: Invalid byte alignment value.\r\nMultiple of EFI_PAGE_SIZE (4kB per UEFI 2.x spec) required.\r\n");
    return ~1ULL;
//    byte_alignment &= ~EFI_PAGE_MASK; // Force it to the nearest multiple of 4kB, rounding down such that, e.g., 6kb -> 4kB, 11kB -> 8kB, etc.
  }

  // Verify OldAddress alignment
  if(OldAddress & (byte_alignment - 1))
  {
    // Determine the next aligned address above OldAddress
    NewAddress = (OldAddress & ~(byte_alignment - 1)) + byte_alignment;
    // This is OK instead of erroring out because this is the point of OldAddress--it's just a baseline address to search upwards from
  }
  else
  {
    NewAddress = OldAddress;
  }

  // Multiply NumberOfPages by EFI_PAGE_SIZE to get the end address... which should just be the start of the next section.
  // Check for EfiConventionalMemory in the map
  for(Piece = Global_Memory_Info.MemMap; Piece < (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize))
  {
    // Within each compatible EfiConventionalMemory, look for space
    if((Piece->Type == EfiConventionalMemory) && (Piece->NumberOfPages >= pages) && (Piece->VirtualStart >= OldAddress))
    {

      VirtualEnd = Piece->VirtualStart + (Piece->NumberOfPages << EFI_PAGE_SHIFT); // Get the end of this range (it's really the start of the next, adjacent range)

      if((Piece->VirtualStart > NewAddress) || (NewAddress > VirtualEnd)) // Region is > aligned address, or < due to out-of-order entries
      { // This works even if the memory map descriptors are not in order from lowest to highest address.
        // This framework works to keep it so that the map is ordered as handed off by UEFI: sometimes UEFI will stick an out of order
        // entry or two at the end, but those appear to always be EfiReservedMemoryType or EfiMemoryMappedIO, which doesn't pose a problem
        // since only EfiConventionalMemory regions and types that could turn into EfiConventionalMemory regions matter. As far as I've
        // been able to ascertain, these regions of interest are always ordered by UEFI from lowest to highest physical address.

        NewAddress = (Piece->VirtualStart & ~(byte_alignment - 1)); // Make a new aligned address
        if(NewAddress == Piece->VirtualStart) // Trivial case
        {
          DiscoveredAddress = NewAddress; // This new region's base address works!
          break;
        }
        else if( (NewAddress < Piece->VirtualStart) && ((NewAddress + byte_alignment + (pages << EFI_PAGE_SHIFT)) <= VirtualEnd) )
        {
          DiscoveredAddress = NewAddress + byte_alignment; // This new region has a compatible area!
          break;
        }
      }
      else if((Piece->VirtualStart <= NewAddress) && ((NewAddress + (pages << EFI_PAGE_SHIFT)) <= VirtualEnd)) // Is the aligned address in this region?
      {
        DiscoveredAddress = NewAddress; // The aligned address works!
        break;
      }

    }
  }

#ifdef MEMORY_CHECK_INFO
  // Loop ended without a discovered address
  if(DiscoveredAddress == ~0ULL)
  {
    // Return address -1
    error_printf("No more free virtual addresses aligned by %llu bytes...\r\n", byte_alignment);
  }
#endif

  return DiscoveredAddress;
}

//----------------------------------------------------------------------------------------------------------------------------------
//  VAllocateFreeAddress: Allocate A Free Virtual Memory Address, Bottom-Up, Aligned
//----------------------------------------------------------------------------------------------------------------------------------
//
// Returns the next aligned virtual address marked as available (in EfiConventionalMemory) that is > the supplied OldAddress.
//
// numbytes: number of bytes needed (NOTE: a size of 0 will return 1 page, not NULL, because 0x0 is a valid address)
// OldAddress: A baseline address to search bottom-up from
// byte_alignment: a desired alignment value in bytes. Valid sizes are power-of-2 multiples (e.g. 2x, 4x, 8x, 16x, 32x, etc.) of the UEFI page size (4096 bytes (4kB) in UEFI 2.x)
//
// UEFI memory maps have a page size of 4kB, which is the minimum allocatable size without resorting to further segmentation via
// memory pooling. This allows dynamically allocated memory to be incorporated right into the main memmap. This has the advantage of
// only requiring free(pointer)to free the descriptor made by this function. No extra sub-mapping, treeing, branching, binning,
// bucketing, carving, slicing, or stressing out over such complexity needed. If finer granularity is absolutely required for some
// reason, just make a struct that describes a 4kB area and typecast or convert the address/pointer returned by
// AllocateFreeAddress/malloc to it.
//
// NOTE: Max size of byte_alignment depends on how the virtual address map is set up. 1:1 mapping means the same limits as
// AllocateFreeAddress() apply. An alignment value that is unfeasibly large will just return ~0ULL (indicating no sufficient free area
// found--it wouldn't make sense to try to align to 512GB when there are no 512GB addresses in the map!).
//

EFI_VIRTUAL_ADDRESS VAllocateFreeAddress(size_t numbytes, EFI_VIRTUAL_ADDRESS OldAddress, uintmax_t byte_alignment)
{
  // All this does is take some EfiConventionalMemory and add entries to the map

  // First, ensure memmap has enough space for 2 more descriptors (the worst-case scenario makes 2)
  uint64_t memmap_check = MemMap_Prep(2);
  if(memmap_check)
  {
    error_printf("VAllocateFreeAddress (vmalloc): Could not prep memory map...\r\n");
    return memmap_check;
  }

  EFI_MEMORY_DESCRIPTOR * Piece;
  EFI_VIRTUAL_ADDRESS VirtualEnd;
  size_t numpages = EFI_SIZE_TO_PAGES(numbytes);

  // Since NULL (or ((void *) 0), per its typedef) is actually a pointer to address 0x0, well, that's actually a valid return value here.
  // This means we can't return NULL on a size of zero, so instead this implementation rounds all sizes < 4096 bytes (1 UEFI page) to 1 page, including 0.
  if(numpages == 0)
  {
    numpages++;
  }

  EFI_VIRTUAL_ADDRESS alloc_address = VActuallyAlignedFreeAddress(numpages, OldAddress, byte_alignment);
  if(alloc_address == ~0ULL)
  {
    error_printf("Not enough space for VAllocateFreeAddress (vmalloc). Unsafe to continue.\r\n");
    return alloc_address;
  }
  else if(alloc_address == ~1ULL)
  {
    error_printf("VAllocateFreeAddress (vmalloc): Invalid byte alignment.\r\n");
    return alloc_address;
  }

  // Zero out the destination
  AVX_memset((void*)alloc_address, 0, numpages << EFI_PAGE_SHIFT);

  // Get a pointer for the descriptor corresponding to the address (scan the memory map to find it)
  for(Piece = Global_Memory_Info.MemMap; (uint8_t*)Piece < ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize))
  {
    VirtualEnd = Piece->VirtualStart + (Piece->NumberOfPages << EFI_PAGE_SHIFT); // Get the end of this range (it's really the start of the next, adjacent range)

    if( (Piece->VirtualStart <= alloc_address) && ((alloc_address + (numpages << EFI_PAGE_SHIFT)) <= VirtualEnd) )
    { // Found it, Piece holds the spot now.
      // Need to account for inserting a descriptor that might be for a region at the start of an EfiConventionalMemory chunk, at the end of an EfiConventionalMemory chunk, or somewhere in the middle of an EfiConventionalMemory chunk (requires 2 new descriptors)
      // ...Unless the trivial case is met, in which case 0 descriptors need to be added.

      // Mark the new area as vmalloc (it's currently EfiConventionalMemory)
      if(Piece->NumberOfPages == numpages) // Trivial case: The new space descriptor is just the right size and needs no splitting; saves a memory descriptor so MemMapSize doesn't need to be increased
      { // Modify 1 map entry, add no entries
        Piece->Type = EfiMaxMemoryType + 2; // Special vmalloc type
        // Nothng to do for Pad, PhysicalStart, VirtualStart, NumberOfPages, and Attribute
      }
      else if(alloc_address == Piece->VirtualStart) // Need to insert a descriptor, alloc_address is the start of a region
      { // Modify 1 map entry, add 1 map entry

        // Make a temporary descriptor to hold current piece's values, but modified for vmalloc
        EFI_MEMORY_DESCRIPTOR new_descriptor_temp;
        new_descriptor_temp.Type = EfiMaxMemoryType + 2; // Special vmalloc type
        new_descriptor_temp.Pad = Piece->Pad;
        new_descriptor_temp.PhysicalStart = Piece->PhysicalStart;
        new_descriptor_temp.VirtualStart = Piece->VirtualStart;
        new_descriptor_temp.NumberOfPages = numpages;
        new_descriptor_temp.Attribute = Piece->Attribute;

        // Modify this EfiConventionalMemory descriptor (shrink it) to reflect its new values
        Piece->PhysicalStart += (numpages << EFI_PAGE_SHIFT);
        Piece->VirtualStart += (numpages << EFI_PAGE_SHIFT);
        Piece->NumberOfPages -= numpages;

        // Move (copy) the whole memmap that's above this piece (including this freshly modified piece) from this piece to one MemMapDescriptorSize over
        AVX_memmove((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize, Piece, ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize) - (uint8_t*)Piece); // Pointer math to get size

        // Insert the new piece (by overwriting the now-duplicated entry with new values)
        // I.e. turn this piece into what was stored in the temporary descriptor above
        Piece->Type = new_descriptor_temp.Type;
        Piece->Pad = new_descriptor_temp.Pad;
        Piece->PhysicalStart = new_descriptor_temp.PhysicalStart;
        Piece->VirtualStart = new_descriptor_temp.VirtualStart;
        Piece->NumberOfPages = new_descriptor_temp.NumberOfPages;
        Piece->Attribute = new_descriptor_temp.Attribute;

        // Update Global_Memory_Info with new MemMap size
        Global_Memory_Info.MemMapSize += Global_Memory_Info.MemMapDescriptorSize;
      }
      else if( (alloc_address + (numpages << EFI_PAGE_SHIFT)) == VirtualEnd ) // Need to insert a descriptor, alloc_address + (numpages << EFI_PAGE_SHIFT) is the end of a region
      { // Modify 1 map entry, add 1 map entry

        // Make a temporary descriptor to hold current piece's values, but page size shrunken by page size of vmalloc area
        EFI_MEMORY_DESCRIPTOR new_descriptor_temp;
        new_descriptor_temp.Type = Piece->Type;
        new_descriptor_temp.Pad = Piece->Pad;
        new_descriptor_temp.PhysicalStart = Piece->PhysicalStart;
        new_descriptor_temp.VirtualStart = Piece->VirtualStart;
        new_descriptor_temp.NumberOfPages = Piece->NumberOfPages - numpages;
        new_descriptor_temp.Attribute = Piece->Attribute;

        // Modify this descriptor to reflect its new values (become the new entry)
        Piece->Type = EfiMaxMemoryType + 2; // Special vmalloc type
        // Nothing for pad
        Piece->PhysicalStart += (numpages << EFI_PAGE_SHIFT);
        Piece->VirtualStart += (numpages << EFI_PAGE_SHIFT);
        Piece->NumberOfPages = numpages;
        // Nothing for attribute

        // Move (copy) the whole memmap that's above this piece (including this freshly modified piece) from this piece to one MemMapDescriptorSize over
        AVX_memmove((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize, Piece, ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize) - (uint8_t*)Piece); // Pointer math to get size

        // Insert the "new" piece (by overwriting the now-duplicated entry with old values)
        // I.e. turn this piece into what was stored in the temporary descriptor above
        Piece->Type = new_descriptor_temp.Type;
        Piece->Pad = new_descriptor_temp.Pad;
        Piece->PhysicalStart = new_descriptor_temp.PhysicalStart;
        Piece->VirtualStart = new_descriptor_temp.VirtualStart;
        Piece->NumberOfPages = new_descriptor_temp.NumberOfPages;
        Piece->Attribute = new_descriptor_temp.Attribute;

        // Update Global_Memory_Info
        Global_Memory_Info.MemMapSize += Global_Memory_Info.MemMapDescriptorSize; // Only need to update total size here
      }
      else // alloc_address is somewhere in the middle of the entry
      { // Modify 1 map entry, add 2 map entries

        // How many pages do the region's surroundings take?
        size_t below_pages = (alloc_address - Piece->VirtualStart) >> EFI_PAGE_SHIFT; // This should never lose information because UEFI's memory quanta is 4kB pages.
        size_t above_pages = Piece->NumberOfPages - numpages - below_pages;

        // Make a temporary descriptor to hold "below" segment
        EFI_MEMORY_DESCRIPTOR new_descriptor_temp_below;
        new_descriptor_temp_below.Type = Piece->Type;
        new_descriptor_temp_below.Pad = Piece->Pad;
        new_descriptor_temp_below.PhysicalStart = Piece->PhysicalStart;
        new_descriptor_temp_below.VirtualStart = Piece->VirtualStart;
        new_descriptor_temp_below.NumberOfPages = below_pages;
        new_descriptor_temp_below.Attribute = Piece->Attribute;

        // Make a temporary descriptor to hold "vmalloc" segment
        EFI_MEMORY_DESCRIPTOR new_descriptor_temp;
        new_descriptor_temp.Type = EfiMaxMemoryType + 2; // Special vmalloc type
        new_descriptor_temp.Pad = Piece->Pad;
        new_descriptor_temp.PhysicalStart = Piece->PhysicalStart + (below_pages << EFI_PAGE_SHIFT);
        new_descriptor_temp.VirtualStart = Piece->VirtualStart + (below_pages << EFI_PAGE_SHIFT);
        new_descriptor_temp.NumberOfPages = numpages;
        new_descriptor_temp.Attribute = Piece->Attribute;

        // Modify this descriptor's to become the "above" part (higher memory address part)
        // Type stays the same
        // Nothing for pad
        Piece->PhysicalStart += (below_pages + numpages) << EFI_PAGE_SHIFT;
        Piece->VirtualStart += (below_pages + numpages) << EFI_PAGE_SHIFT;
        Piece->NumberOfPages = above_pages;
        // Nothing for attribute

        // Move (copy) the whole memmap that's above this piece (including this freshly modified piece) from this piece to one MemMapDescriptorSize over
        AVX_memmove((uint8_t*)Piece + 2*Global_Memory_Info.MemMapDescriptorSize, Piece, ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize) - (uint8_t*)Piece); // Pointer math to get size

        // Insert the "below" piece into this now-tripled piece
        // I.e. turn this now-tripled piece into what was stored in the "below" temporary descriptor above
        Piece->Type = new_descriptor_temp_below.Type;
        Piece->Pad = new_descriptor_temp_below.Pad;
        Piece->PhysicalStart = new_descriptor_temp_below.PhysicalStart;
        Piece->VirtualStart = new_descriptor_temp_below.VirtualStart;
        Piece->NumberOfPages = new_descriptor_temp_below.NumberOfPages;
        Piece->Attribute = new_descriptor_temp_below.Attribute;

        // Insert the "vmalloc" piece into the next piece
        EFI_MEMORY_DESCRIPTOR * Next_Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize);

        // I.e. turn this now-duplicate piece, which sits between "above" and "below," into what was stored in the temporary "vmalloc" descriptor above
        Next_Piece->Type = new_descriptor_temp.Type;
        Next_Piece->Pad = new_descriptor_temp.Pad;
        Next_Piece->PhysicalStart = new_descriptor_temp.PhysicalStart;
        Next_Piece->VirtualStart = new_descriptor_temp.VirtualStart;
        Next_Piece->NumberOfPages = new_descriptor_temp.NumberOfPages;
        Next_Piece->Attribute = new_descriptor_temp.Attribute;

        // Update Global_Memory_Info
        Global_Memory_Info.MemMapSize += 2*Global_Memory_Info.MemMapDescriptorSize;
      }
      // Done modifying map.

      break;
    } // End "found it"
  } // End for

  if((uint8_t*)Piece == ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize)) // This will be true if the loop didn't break
  {
    error_printf("VAllocateFreeAddress (vmalloc) area %#qx not found. Unsafe to continue program.\r\n", alloc_address);
    HaCF();
  }

  return alloc_address;
}

//----------------------------------------------------------------------------------------------------------------------------------
//  ReclaimEfiBootServicesMemory: Convert EfiBootServicesCode and EfiBootServicesData to EfiConventionalMemory
//----------------------------------------------------------------------------------------------------------------------------------
//
// After calling ExitBootServices(), EfiBootServicesCode and EfiBootServicesData are supposed to become free memory. This is not
// always the case (see: https://mjg59.dreamwidth.org/11235.html), but this function exists because the UEFI Specification (2.7A)
// states that it really should be free.
//
// Calling this function more than once won't do anything other than just waste some CPU time.
//

void ReclaimEfiBootServicesMemory(void)
{
  EFI_MEMORY_DESCRIPTOR * Piece;

  // Check for Boot Services leftovers in the map
  for(Piece = Global_Memory_Info.MemMap; Piece < (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize))
  {
    if((Piece->Type == EfiBootServicesCode) || (Piece->Type == EfiBootServicesData))
    {
      Piece->Type = EfiConventionalMemory; // Convert to EfiConventionalMemory
    }
  }
  // Done.

  MergeContiguousConventionalMemory();
}

//----------------------------------------------------------------------------------------------------------------------------------
//  ReclaimEfiLoaderCodeMemory: Convert EfiLoaderCode to EfiConventionalMemory
//----------------------------------------------------------------------------------------------------------------------------------
//
// After calling ExitBootServices(), it is up to the OS to decide what to do with EfiLoaderCode (and EfiLoaderData, though that's
// used to store all the Loader Params that this kernel actively uses). This function reclaims that memory as free.
//
// Calling this function more than once won't do anything other than just waste some CPU time.
//

void ReclaimEfiLoaderCodeMemory(void)
{
  EFI_MEMORY_DESCRIPTOR * Piece;

  // Check for Loader Code (the boot loader that booted this kernel) leftovers in the map
  for(Piece = Global_Memory_Info.MemMap; Piece < (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize))
  {
    if(Piece->Type == EfiLoaderCode)
    {
      Piece->Type = EfiConventionalMemory; // Convert to EfiConventionalMemory
    }
  }
  // Done.

  MergeContiguousConventionalMemory();
}

//----------------------------------------------------------------------------------------------------------------------------------
//  MergeContiguousConventionalMemory: Merge Adjacent EfiConventionalMemory Entries
//----------------------------------------------------------------------------------------------------------------------------------
//
// Merge adjacent EfiConventionalMemory locations that are listed as separate entries. This can only work with physical addresses.
// It's main uses are during calls to free() and Setup_MemMap(), where this function acts to clean up the memory map.
//
// This function also contains the logic necessary to shrink the memory map's own descriptor's NumberOfPages to reclaim extra space.
//

void MergeContiguousConventionalMemory(void)
{
  EFI_MEMORY_DESCRIPTOR * Piece;
  EFI_MEMORY_DESCRIPTOR * Piece2;
  EFI_PHYSICAL_ADDRESS PhysicalEnd;
  size_t numpages = 0;

  for(Piece = Global_Memory_Info.MemMap; Piece < (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize))
  {
    // Within each EfiConventionalMemory, check adjacents
    if(Piece->Type == EfiConventionalMemory)
    {
      PhysicalEnd = Piece->PhysicalStart + (Piece->NumberOfPages << EFI_PAGE_SHIFT); // Get the end of this range, which may be the start of another range

      // See if PhysicalEnd matches any PhysicalStart of EfiConventionalMemory

      for(Piece2 = Global_Memory_Info.MemMap; Piece2 < (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Piece2 = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece2 + Global_Memory_Info.MemMapDescriptorSize))
      {
        if( (Piece2->Type == EfiConventionalMemory) && (PhysicalEnd == Piece2->PhysicalStart) )
        {
          // Found one.
          // Add this entry's pages to Piece and delete this entry.
          Piece->NumberOfPages += Piece2-> NumberOfPages;

          // Zero out Piece2
          AVX_memset(Piece2, 0, Global_Memory_Info.MemMapDescriptorSize);
          AVX_memmove(Piece2, (uint8_t*)Piece2 + Global_Memory_Info.MemMapDescriptorSize, ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize) - ((uint8_t*)Piece2 + Global_Memory_Info.MemMapDescriptorSize));

          // Update Global_Memory_Info
          Global_Memory_Info.MemMapSize -= Global_Memory_Info.MemMapDescriptorSize;

          // Zero out the entry that used to be at the end
          AVX_memset((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize, 0, Global_Memory_Info.MemMapDescriptorSize);

          // Decrement Piece2 one descriptor to check this one again, since after modification there may be more to merge
          Piece2 = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece2 - Global_Memory_Info.MemMapDescriptorSize);
          // Refresh PhysicalEnd with the new size
          PhysicalEnd = Piece->PhysicalStart + (Piece->NumberOfPages << EFI_PAGE_SHIFT);
        }
      } // End inner for loop

    }
    else if(Piece->Type == EfiMaxMemoryType + 3)
    { // Get the reported size of the memmap, we'll need it later to see if free space can be reclaimed
      numpages = Piece->NumberOfPages;
    }
  } // End outer for loop

  if(numpages == 0)
  {
    error_printf("Error: MergeContiguousConventionalMemory: MemMap not found. Has it not been set up yet?\r\n");
    HaCF();
  }

  // How much space does the new map take?
  size_t numpages2 = EFI_SIZE_TO_PAGES(Global_Memory_Info.MemMapSize);

  // After all that, maybe some space can be reclaimed. Let's see what we can do...
  if(numpages2 < numpages)
  {
    // Re-find memmap entry, since the memmap layout has changed.
    for(Piece = Global_Memory_Info.MemMap; Piece < (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize))
    {
      if(Piece->Type == EfiMaxMemoryType + 3) // Found it.
      {
        size_t freedpages = numpages - numpages2;

        // Check if there's an EfiConventionalMemory region adjacent in memory to the memmap region
        EFI_MEMORY_DESCRIPTOR * Next_Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize); // Remember... sizeof(EFI_MEMORY_DESCRIPTOR) != MemMapDescriptorSize :/
        PhysicalEnd = Piece->PhysicalStart + (Piece->NumberOfPages << EFI_PAGE_SHIFT); // Get the end of this range, which may be the start of another range

        // Quick check for adjacency that will skip scanning the whole memmap
        // Can provide a little speed boost for ordered memory maps
        if(
            (Next_Piece->PhysicalStart != PhysicalEnd)
            ||
            ((uint8_t*)Next_Piece == ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize))
          ) // This OR check protects against the obscure case of there being a rogue entry adjacent the end of the memory map that describes a valid EfiConventionalMemory entry
        {
          // See if PhysicalEnd matches any PhysicalStart for unordered maps
          for(Next_Piece = Global_Memory_Info.MemMap; Next_Piece < (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Next_Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Next_Piece + Global_Memory_Info.MemMapDescriptorSize))
          {
            if(PhysicalEnd == Next_Piece->PhysicalStart)
            {
              // Found one
              break;
            }
          }
        }

        // Is the next piece an EfiConventionalMemory type?
        if(
            (Next_Piece->Type == EfiConventionalMemory)
            &&
            (uint8_t*)Next_Piece != ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize)
          ) // Check if the loop didn't break (this also covers the case where the memory map is the last valid entry in the map)
        { // Yes, we can reclaim without requiring a new entry

          // Modify MemMap's entry
          Piece->NumberOfPages = numpages2;

          // Modify adjacent EfiConventionalMemory's entry
          Next_Piece->NumberOfPages += freedpages;
          Next_Piece->PhysicalStart -= (freedpages << EFI_PAGE_SHIFT);
          Next_Piece->VirtualStart -= (freedpages << EFI_PAGE_SHIFT);

          // Done. Nice.
        }
        // No, we need a new entry, which will require a new page if the last entry is on a page edge or would spill over a page edge. Better to be safe then sorry!
        // First, maybe there's room for another descriptor in the last page
        else if((Global_Memory_Info.MemMapSize + Global_Memory_Info.MemMapDescriptorSize) <= (numpages2 << EFI_PAGE_SHIFT))
        { // Yes, we can reclaim and fit in another descriptor

          // Make a temporary descriptor to hold current MemMap entry's values
          EFI_MEMORY_DESCRIPTOR new_descriptor_temp;
          new_descriptor_temp.Type = Piece->Type; // Special memmap type
          new_descriptor_temp.Pad = Piece->Pad;
          new_descriptor_temp.PhysicalStart = Piece->PhysicalStart;
          new_descriptor_temp.VirtualStart = Piece->VirtualStart;
          new_descriptor_temp.NumberOfPages = numpages2; // New size of MemMap entry
          new_descriptor_temp.Attribute = Piece->Attribute;

          // Modify the descriptor-to-move
          Piece->Type = EfiConventionalMemory;
          // No pad change
          Piece->PhysicalStart += (numpages2 << EFI_PAGE_SHIFT);
          Piece->VirtualStart += (numpages2 << EFI_PAGE_SHIFT);
          Piece->NumberOfPages = freedpages;
          // No attribute change

          // Move (copy) the whole memmap that's above this piece (including this freshly modified piece) from this piece to one MemMapDescriptorSize over
          AVX_memmove((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize, Piece, ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize) - (uint8_t*)Piece); // Pointer math to get size

          // Insert the new piece (by overwriting the now-duplicated entry with new values)
          // I.e. turn this piece into what was stored in the temporary descriptor above
          Piece->Type = new_descriptor_temp.Type;
          Piece->Pad = new_descriptor_temp.Pad;
          Piece->PhysicalStart = new_descriptor_temp.PhysicalStart;
          Piece->VirtualStart = new_descriptor_temp.VirtualStart;
          Piece->NumberOfPages = new_descriptor_temp.NumberOfPages;
          Piece->Attribute = new_descriptor_temp.Attribute;

          // Update Global_Memory_Info MemMap size
          Global_Memory_Info.MemMapSize += Global_Memory_Info.MemMapDescriptorSize;

          // Done
        }
        // No, it would spill over to a new page
        else // MemMap is always put at the base of an EfiConventionalMemory region after Setup_MemMap()
        {
          // Do we have more than a descriptor's worth of pages reclaimable?
          size_t pages_per_memory_descriptor = EFI_SIZE_TO_PAGES(Global_Memory_Info.MemMapDescriptorSize);

          if((numpages2 + pages_per_memory_descriptor) < numpages)
          { // Yes, so we can hang on to one [set] of them and make a new EfiConventionalMemory entry for the rest.
            freedpages -= pages_per_memory_descriptor;

            // Make a temporary descriptor to hold current MemMap entry's values
            EFI_MEMORY_DESCRIPTOR new_descriptor_temp;
            new_descriptor_temp.Type = Piece->Type; // Special memmap type
            new_descriptor_temp.Pad = Piece->Pad;
            new_descriptor_temp.PhysicalStart = Piece->PhysicalStart;
            new_descriptor_temp.VirtualStart = Piece->VirtualStart;
            new_descriptor_temp.NumberOfPages = numpages2 + pages_per_memory_descriptor; // New size of MemMap entry
            new_descriptor_temp.Attribute = Piece->Attribute;

            // Modify the descriptor-to-move
            Piece->Type = EfiConventionalMemory;
            // No pad change
            Piece->PhysicalStart += ((numpages2 + pages_per_memory_descriptor) << EFI_PAGE_SHIFT);
            Piece->VirtualStart += ((numpages2 + pages_per_memory_descriptor) << EFI_PAGE_SHIFT);
            Piece->NumberOfPages = freedpages;
            // No attribute change

            // Move (copy) the whole memmap that's above this piece (including this freshly modified piece) from this piece to one MemMapDescriptorSize over
            AVX_memmove((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize, Piece, ((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize) - (uint8_t*)Piece); // Pointer math to get size

            // Insert the new piece (by overwriting the now-duplicated entry with new values)
            // I.e. turn this piece into what was stored in the temporary descriptor above
            Piece->Type = new_descriptor_temp.Type;
            Piece->Pad = new_descriptor_temp.Pad;
            Piece->PhysicalStart = new_descriptor_temp.PhysicalStart;
            Piece->VirtualStart = new_descriptor_temp.VirtualStart;
            Piece->NumberOfPages = new_descriptor_temp.NumberOfPages;
            Piece->Attribute = new_descriptor_temp.Attribute;

            // Update Global_Memory_Info MemMap size
            Global_Memory_Info.MemMapSize += Global_Memory_Info.MemMapDescriptorSize;
          }
          // No, only 1 [set of] page(s) was reclaimable and adding another entry would spill over. So don't do anything then and hang on to the extra empty page(s).
        }
        // All done. There's only one MemMap entry so we can break out of the loop now.
        break;
      } // End found it (memmap)
    } // End search for memmap loop
  } // End space reclaim

  // Done
}

//----------------------------------------------------------------------------------------------------------------------------------
//  ZeroAllConventionalMemory: Zero Out ALL EfiConventionalMemory
//----------------------------------------------------------------------------------------------------------------------------------
//
// This function goes through the memory map and zeroes out all EfiConventionalMemory areas. Returns 0 on success, else returns the
// base physical address of the last region that could not be completely zeroed.
//
// USE WITH CAUTION!!
// Firmware bugs like the one described here could really cause problems with this function: https://mjg59.dreamwidth.org/11235.html
//
// Also, buggy firmware that uses Boot Service memory when invoking runtime services will fail to work after this function if
// ReclaimEfiBootServicesMemory() has been used beforehand.
//

EFI_PHYSICAL_ADDRESS ZeroAllConventionalMemory(void)
{
  EFI_MEMORY_DESCRIPTOR * Piece;
  EFI_PHYSICAL_ADDRESS exit_value = 0;

  // Check for EfiConventionalMemory
  for(Piece = Global_Memory_Info.MemMap; Piece < (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Global_Memory_Info.MemMap + Global_Memory_Info.MemMapSize); Piece = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)Piece + Global_Memory_Info.MemMapDescriptorSize))
  {
    if(Piece->Type == EfiConventionalMemory)
    {
      AVX_memset((void *)Piece->PhysicalStart, 0, EFI_PAGES_TO_SIZE(Piece->NumberOfPages));

      if(VerifyZeroMem(EFI_PAGES_TO_SIZE(Piece->NumberOfPages), Piece->PhysicalStart))
      {
        error_printf("Area Not Zeroed! Base Physical Address: %#qx, Pages: %llu\r\n", Piece->PhysicalStart, Piece->NumberOfPages);
        exit_value = Piece->PhysicalStart;
      }
      else
      {
        printf("Zeroed! Base Physical Address: %#qx, Pages: %llu\r\n", Piece->PhysicalStart, Piece->NumberOfPages);
      }
    }
  }
  // Done.
  return exit_value;
}
