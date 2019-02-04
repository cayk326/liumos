#pragma once

#include "acpi.h"
#include "asm.h"
#include "efi.h"
#include "generic.h"
#include "guid.h"
#include "interrupt.h"
#include "keyid.h"
#include "paging.h"

constexpr uint64_t kKernelBaseAddr = 0xFFFF'FFFF'0000'0000;

// @apic.cc
class LocalAPIC {
 public:
  uint8_t GetID() { return id_; }
  void Init(void);
  enum class RegisterOffset : uint64_t {
    kLocalAPICID = 0x20,
    kSVR = 0xF0,
  };
  uint32_t ReadRegister(RegisterOffset offset);
  void WriteRegister(RegisterOffset offset, uint32_t value) {
    *reinterpret_cast<uint32_t *>(base_addr_ + static_cast<uint64_t>(offset)) = value;
  }

 private:
  uint64_t base_addr_;
  uint8_t id_;
};

void SendEndOfInterruptToLocalAPIC();
void InitIOAPIC(uint64_t local_apic_id);

// @console.c
void ResetCursorPosition();
void EnableVideoModeForConsole();
void PutChar(char c);
void PutString(const char* s);
void PutChars(const char* s, int n);
void PutHex64(uint64_t value);
void PutHex64ZeroFilled(uint64_t value);
void PutStringAndHex(const char* s, uint64_t value);
void PutStringAndHex(const char* s, void* value);

// @console_command.cc
namespace ConsoleCommand {
void ShowNFIT(void);
void ShowMADT(void);
void ShowEFIMemoryMap(void);
void Free(void);
}  // namespace ConsoleCommand

// @elf.cc
class File;
void ParseELFFile(File& logo_file);

// @file.cc
class File {
 public:
  void LoadFromEFISimpleFS(const wchar_t* file_name);
  const uint8_t* GetBuf() { return buf_pages_; }
  uint64_t GetFileSize() { return file_size_; }

 private:
  static constexpr int kFileNameSize = 16;
  char file_name_[kFileNameSize + 1];
  uint64_t file_size_;
  uint8_t* buf_pages_;
};

// @font.gen.c
extern uint8_t font[0x100][16];

// @gdt.c
class GDT {
 public:
  static constexpr uint64_t kDescBitTypeData = 0b10ULL << 43;
  static constexpr uint64_t kDescBitTypeCode = 0b11ULL << 43;

  static constexpr uint64_t kDescBitForSystemSoftware = 1ULL << 52;
  static constexpr uint64_t kDescBitPresent = 1ULL << 47;
  static constexpr uint64_t kDescBitOfsDPL = 45;
  static constexpr uint64_t kDescBitMaskDPL = 0b11ULL << kDescBitOfsDPL;
  static constexpr uint64_t kDescBitAccessed = 1ULL << 40;

  static constexpr uint64_t kCSDescBitLongMode = 1ULL << 53;
  static constexpr uint64_t kCSDescBitReadable = 1ULL << 41;

  static constexpr uint64_t kDSDescBitWritable = 1ULL << 41;

  static constexpr uint64_t kKernelCSIndex = 1;
  static constexpr uint64_t kKernelDSIndex = 2;
  static constexpr uint64_t kTSS64Index = 3;

  static constexpr uint64_t kKernelCSSelector = kKernelCSIndex << 3;
  static constexpr uint64_t kKernelDSSelector = kKernelDSIndex << 3;
  static constexpr uint64_t kTSS64Selector = kTSS64Index << 3;

  void Init(void);
  void Print(void);

 private:
  GDTR gdtr_;
  packed_struct GDTDescriptors{
    uint64_t null_segment;
    uint64_t kernel_code_segment;
    uint64_t kernel_data_segment;
    packed_struct TSS64 {
      uint16_t limit_low;
      uint16_t base_low;
      uint8_t base_mid_low;
      uint16_t attr;
      uint8_t base_mid_high;
      uint32_t base_high;
      uint32_t reserved;
    } task_state_segment;
    static_assert(sizeof(TSS64) == 16);
  } descriptors_;
};

// @generic.h

[[noreturn]] void Panic(const char* s);
inline void* operator new(size_t, void* where) {
  return where;
}

// @graphics.cc
extern int xsize;
extern int ysize;
extern int pixels_per_scan_line;
extern uint8_t* vram;
void InitGraphics();
void DrawCharacter(char c, int px, int py);
void DrawRect(int px, int py, int w, int h, uint32_t col);
void BlockTransfer(int to_x, int to_y, int from_x, int from_y, int w, int h);

// @keyboard.cc
constexpr uint16_t kIOPortKeyboardData = 0x0060;

// @libfunc.cc
int strncmp(const char* s1, const char* s2, size_t n);
void* memcpy(void* dst, const void* src, size_t n);

// @liumos.c
constexpr uint64_t kPageSizeExponent = 12;
constexpr uint64_t kPageSize = 1 << kPageSizeExponent;
inline uint64_t ByteSizeToPageSize(uint64_t byte_size) {
  return (byte_size + kPageSize - 1) >> kPageSizeExponent;
}

class PhysicalPageAllocator;

extern EFI::MemoryMap efi_memory_map;
extern PhysicalPageAllocator* page_allocator;
extern int kMaxPhyAddr;

void MainForBootProcessor(void* image_handle, EFI::SystemTable* system_table);

// @phys_page_allocator.cc
class PhysicalPageAllocator {
 public:
  PhysicalPageAllocator() : head_(nullptr) {}
  void FreePages(void* phys_addr, uint64_t num_of_pages);
  void* AllocPages(int num_of_pages);
  void Print();

 private:
  class FreeInfo {
   public:
    FreeInfo(uint64_t num_of_pages, FreeInfo* next)
        : num_of_pages_(num_of_pages), next_(next) {}
    FreeInfo* GetNext() const { return next_; }
    void* ProvidePages(int num_of_req_pages);
    void Print();

   private:
    bool CanProvidePages(uint64_t num_of_req_pages) const {
      return num_of_req_pages < num_of_pages_;
    }

    uint64_t num_of_pages_;
    FreeInfo* next_;
  };

  FreeInfo* head_;
};
