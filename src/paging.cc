#include "liumos.h"

template <>
void IA_PDT::Print() {
  for (int i = 0; i < kNumOfEntries; i++) {
    if (!entries[i].IsPresent())
      continue;
    PutString("  PDT[");
    PutHex64(i);
    PutString("]:");
    if (entries[i].IsPage()) {
      PutString(" 2MB Page\n");
      continue;
    }
    PutStringAndHex(" addr", entries[i].GetTableAddr());
  }
}

template <>
void IA_PDPT::Print() {
  for (int i = 0; i < kNumOfEntries; i++) {
    if (!entries[i].IsPresent())
      continue;
    PutString(" PDPT[");
    PutHex64(i);
    PutString("]:");
    if (entries[i].IsPage()) {
      PutString("  1GB Page\n");
      continue;
    }
    PutString("\n");
    IA_PDT* pdt = entries[i].GetTableAddr();
    pdt->Print();
  }
}

template <>
void IA_PML4::Print() {
  for (int i = 0; i < kNumOfEntries; i++) {
    if (!entries[i].IsPresent())
      continue;
    PutString(" PML4[");
    PutHex64(i);
    PutString("]:");
    PutString("\n");
  }
}
template <>
void IA_PT::DebugPrintEntryForAddr(uint64_t vaddr) {
  IA_PTE& e = GetEntryForAddr(vaddr);
  PutString("PTE@");
  PutHex64ZeroFilled(this);
  PutString("[");
  PutHex64(addr2index(vaddr));
  PutString("]: ");
  PutHex64ZeroFilled(e.data);
  PutString("\n");
}
template <>
void IA_PDT::DebugPrintEntryForAddr(uint64_t vaddr) {
  IA_PDE& e = GetEntryForAddr(vaddr);
  PutString("PDTE@");
  PutHex64ZeroFilled(this);
  PutString("[");
  PutHex64(addr2index(vaddr));
  PutString("]: ");
  PutHex64ZeroFilled(e.data);
  PutString("\n");
  if (e.IsPage())
    return;
  e.GetTableAddr()->DebugPrintEntryForAddr(vaddr);
}

template <>
void IA_PDPT::DebugPrintEntryForAddr(uint64_t vaddr) {
  IA_PDPTE& e = GetEntryForAddr(vaddr);
  PutString("PDPT@");
  PutHex64ZeroFilled(this);
  PutString("[");
  PutHex64(addr2index(vaddr));
  PutString("]: ");
  PutHex64ZeroFilled(e.data);
  PutString("\n");
  if (e.IsPage())
    return;
  e.GetTableAddr()->DebugPrintEntryForAddr(vaddr);
}

template <>
void IA_PML4::DebugPrintEntryForAddr(uint64_t vaddr) {
  IA_PML4E& e = GetEntryForAddr(vaddr);
  PutString("PML4@");
  PutHex64ZeroFilled(this);
  PutString("[");
  PutHex64(addr2index(vaddr));
  PutString("]: ");
  PutHex64ZeroFilled(e.data);
  PutString("\n");
  if (e.IsPage())
    return;
  e.GetTableAddr()->DebugPrintEntryForAddr(vaddr);
}

IA_PML4& CreatePageTable() {
  IA_PML4* pml4 = liumos->dram_allocator->AllocPages<IA_PML4*>(1);
  assert(pml4);
  pml4->ClearMapping();
  for (int i = IA_PML4::kNumOfEntries / 2; i < IA_PML4::kNumOfEntries; i++) {
    pml4->entries[i] = liumos->kernel_pml4->entries[i];
  }
  return *pml4;
}

void InitPaging() {
  IA32_EFER efer;
  efer.data = ReadMSR(MSRIndex::kEFER);
  if (!efer.bits.LME)
    Panic("IA32_EFER.LME not enabled.");
  PutString("4-level paging enabled.\n");
  // Even if 4-level paging is supported,
  // whether 1GB pages are supported or not is determined by
  // CPUID.80000001H:EDX.Page1GB [bit 26] = 1.
  const EFI::MemoryDescriptor* loader_code_desc = nullptr;
  uint64_t direct_mapping_end = 0xffff'ffffULL;
  EFI::MemoryMap& map = *liumos->efi_memory_map;
  for (int i = 0; i < map.GetNumberOfEntries(); i++) {
    const EFI::MemoryDescriptor* desc = map.GetDescriptor(i);
    uint64_t map_end_addr =
        desc->physical_start + (desc->number_of_pages << 12);
    if (map_end_addr > direct_mapping_end)
      direct_mapping_end = map_end_addr;
    if (desc->type != EFI::MemoryType::kLoaderCode)
      continue;
    assert(!loader_code_desc);
    loader_code_desc = desc;
  }
  loader_code_desc->Print();
  PutChar('\n');

  // Adjust direct_mapping_end here
  // since VRAM region is not appeared in EFIMemoryMap
  {
    uint64_t map_end_addr =
        reinterpret_cast<uint64_t>(liumos->screen_sheet->GetBuf()) +
        liumos->screen_sheet->GetBufSize();
    if (map_end_addr > direct_mapping_end)
      direct_mapping_end = map_end_addr;
  }

  // PMEM address area may not be shown in UEFI memory map. (ex. QEMU)
  // So we should check NFIT to determine direct_mapping_end.
  if (liumos->acpi.nfit) {
    ACPI::NFIT& nfit = *liumos->acpi.nfit;
    for (auto& it : nfit) {
      using namespace ACPI;
      if (it.type != NFIT::Entry::kTypeSPARangeStructure)
        continue;
      NFIT::SPARange* spa_range = reinterpret_cast<NFIT::SPARange*>(&it);
      uint64_t map_end_addr = spa_range->system_physical_address_range_base +
                              spa_range->system_physical_address_range_length;
      if (map_end_addr > direct_mapping_end)
        direct_mapping_end = map_end_addr;
    }
  }

  assert(loader_code_desc->number_of_pages < (1 << 9));
  PutStringAndHex("map_end_addr", direct_mapping_end);
  uint64_t direct_map_1gb_pages = (direct_mapping_end + (1ULL << 30) - 1) >> 30;
  PutStringAndHex("direct map 1gb pages", direct_map_1gb_pages);

  IA_PML4* kernel_pml4 = liumos->dram_allocator->AllocPages<IA_PML4*>(1);
  kernel_pml4->ClearMapping();

  // mapping pages for real memory & memory mapped IOs
  uint64_t phys_page_attr =
      kPageAttrPresent | kPageAttrWritable | kPageAttrUser;
  for (uint64_t addr = 0; addr < direct_mapping_end;) {
    if (addr >= direct_mapping_end)
      break;
    IA_PDPT* pdpt = liumos->dram_allocator->AllocPages<IA_PDPT*>(1);
    pdpt->ClearMapping();
    kernel_pml4->SetTableBaseForAddr(addr, pdpt, phys_page_attr);
    for (int i = 0; i < IA_PDPT::kNumOfEntries; i++) {
      if (addr >= direct_mapping_end)
        break;
      IA_PDT* pdt = liumos->dram_allocator->AllocPages<IA_PDT*>(1);
      pdt->ClearMapping();
      pdpt->SetTableBaseForAddr(addr, pdt, phys_page_attr);
      for (int i = 0; i < IA_PDT::kNumOfEntries; i++) {
        if (addr >= direct_mapping_end)
          break;
        pdt->SetPageBaseForAddr(addr, addr, phys_page_attr);
        addr += 2 * 1024 * 1024;
      }
    }
  }

  WriteCR3(reinterpret_cast<uint64_t>(kernel_pml4));
  PutStringAndHex("Paging enabled. Kernel CR3", ReadCR3());
  liumos->kernel_pml4 = kernel_pml4;
}

IA_PML4& GetKernelPML4(void) {
  return *liumos->kernel_pml4;
}
