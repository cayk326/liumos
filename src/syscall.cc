#include "liumos.h"

constexpr uint64_t kSyscallIndex_sys_read = 0;
constexpr uint64_t kSyscallIndex_sys_write = 1;
constexpr uint64_t kSyscallIndex_sys_exit = 60;
constexpr uint64_t kSyscallIndex_arch_prctl = 158;
// constexpr uint64_t kArchSetGS = 0x1001;
constexpr uint64_t kArchSetFS = 0x1002;
// constexpr uint64_t kArchGetFS = 0x1003;
// constexpr uint64_t kArchGetGS = 0x1004;

__attribute__((ms_abi)) extern "C" uint64_t GetCurrentKernelStack(void) {
  ExecutionContext& ctx =
      liumos->scheduler->GetCurrentProcess().GetExecutionContext();
  return ctx.GetKernelRSP();
}

__attribute__((ms_abi)) extern "C" void SyscallHandler(uint64_t* args) {
  // This function is called under exceptions are masked
  // Kernel mode stack is used
  uint64_t idx = args[0];
  if (idx == kSyscallIndex_sys_read) {
    const uint64_t fildes = args[1];
    uint8_t* buf = reinterpret_cast<uint8_t*>(args[2]);
    uint64_t nbyte = args[3];
    if (fildes != 0) {
      PutStringAndHex("fildes", fildes);
      Panic("Only stdin is supported for now.");
    }
    uint16_t keyid;
    if (nbyte < 1)
      return;
    do {
      while ((keyid = liumos->main_console->GetCharWithoutBlocking()) ==
             KeyID::kNoInput) {
        StoreIntFlagAndHalt();
      }
    } while ((keyid & KeyID::kMaskBreak) || (keyid & KeyID::kMaskExtended));
    PutStringAndHex("keyid", keyid);

    buf[0] = keyid & 0xff;
    return;
  }
  if (idx == kSyscallIndex_sys_write) {
    uint64_t t0 = liumos->hpet->ReadMainCounterValue();
    const uint64_t fildes = args[1];
    const uint8_t* buf = reinterpret_cast<uint8_t*>(args[2]);
    uint64_t nbyte = args[3];
    if (fildes != 1) {
      PutStringAndHex("fildes", fildes);
      Panic("Only stdout is supported for now.");
    }
    while (nbyte--) {
      PutChar(*(buf++));
    }
    uint64_t t1 = liumos->hpet->ReadMainCounterValue();
    liumos->scheduler->GetCurrentProcess().AddSysTimeFemtoSec(
        (t1 - t0) * liumos->hpet->GetFemtosecondPerCount());
    return;
  }
  if (idx == kSyscallIndex_sys_exit) {
    const uint64_t exit_code = args[1];
    PutStringAndHex("exit: exit_code", exit_code);
    liumos->scheduler->KillCurrentProcess();
    for (;;) {
      StoreIntFlagAndHalt();
    };
    return;
  }
  if (idx == kSyscallIndex_arch_prctl) {
    Panic("arch_prctl!");
    if (args[1] == kArchSetFS) {
      WriteMSR(MSRIndex::kFSBase, args[2]);
      return;
    }
    PutStringAndHex("arg1", args[1]);
    PutStringAndHex("arg2", args[2]);
    PutStringAndHex("arg3", args[3]);
  }
  PutStringAndHex("idx", idx);
  Panic("syscall handler!");
}

void EnableSyscall() {
  uint64_t star = static_cast<uint64_t>(GDT::kKernelCSSelector) << 32;
  star |= static_cast<uint64_t>(GDT::kUserCS32Selector) << 48;
  WriteMSR(MSRIndex::kSTAR, star);

  uint64_t lstar = reinterpret_cast<uint64_t>(AsmSyscallHandler);
  WriteMSR(MSRIndex::kLSTAR, lstar);

  WriteMSR(MSRIndex::kFMASK, 1ULL << 9);

  uint64_t efer = ReadMSR(MSRIndex::kEFER);
  efer |= 1;  // SCE
  WriteMSR(MSRIndex::kEFER, efer);
}
