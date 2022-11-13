// llvm/lib/CodeGen/StackCopyStats.cpp

#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/InitializePasses.h"
#include <algorithm>

using namespace llvm;

typedef DenseMap<MachineBasicBlock *, DenseSet<unsigned>> StackRegMap;

namespace {

class StackCopyStats : public MachineFunctionPass {
  raw_ostream &OS;

  const TargetRegisterInfo *TRI = nullptr;
  const TargetInstrInfo *TII = nullptr;

  bool HeaderPrinted;

  struct Stats {
    unsigned StackToStackCopies;
    unsigned OtherToStackCopies;
    unsigned TotalInstrs;

    Stats() : StackToStackCopies(0), OtherToStackCopies(0), TotalInstrs(0) {}
  };

  bool computeStackRegs(MachineBasicBlock &MBB, StackRegMap &Map);
  void accumulateStats(MachineBasicBlock &MBB, StackRegMap &Map, Stats &Stats);

public:
  static char ID;

  explicit StackCopyStats(raw_ostream &OS = errs())
      : MachineFunctionPass(ID), OS(OS), HeaderPrinted(false) {}

  bool runOnMachineFunction(MachineFunction &MF) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

} // end anonymous namespace

#if 0
static bool setsAreIdentical(DenseSet<unsigned> &A, DenseSet<unsigned> &B) {
  return std::all_of(A.begin(), A.end(),
                     [&](auto AElem) { return B.count(AElem); }) &&
         std::all_of(B.begin(), B.end(),
                     [&](auto BElem) { return A.count(BElem); });
}
#endif

bool StackCopyStats::computeStackRegs(MachineBasicBlock &MBB,
                                      StackRegMap &InMap) {
  DenseSet<unsigned> NewOut = InMap[&MBB];
  /*for (auto Pred : MBB.predecessors()) {
    // FIXME: This may over-count...
    NewSet.insert(Map[Pred].begin(), Map[Pred].end());
  }*/

  for (auto &MI : MBB) {
    int FI;
    if (unsigned Reg = TII->isLoadFromStackSlotPostExpansion(MI, FI)) {
      NewOut.insert(Reg);
    } else {
      for (auto &MO : MI.operands()) {
        // TODO: Doesn't deal with overlapping registers correctly.
        if (MO.isReg() && MO.isDef()) {
          if (Register MOReg = MO.getReg())
            NewOut.erase(unsigned(MOReg));
        }
      }
    }
  }

  bool Changed = false;
  for (MachineBasicBlock *Succ : MBB.successors()) {
    DenseSet<unsigned> &SuccIn = InMap[Succ];
    for (unsigned Reg : NewOut) {
      if (!SuccIn.count(Reg)) {
        Changed = true;
        SuccIn.insert(Reg);
      }
    }
  }

  return Changed;
}

void StackCopyStats::accumulateStats(MachineBasicBlock &MBB, StackRegMap &InMap,
                                     StackCopyStats::Stats &S) {
  DenseSet<unsigned> Set = InMap[&MBB];

  for (auto &MI : MBB) {
    S.TotalInstrs++;
    int FI;
    if (unsigned Reg = TII->isLoadFromStackSlotPostExpansion(MI, FI)) {
      Set.insert(Reg);
    } else if (unsigned Reg = TII->isStoreToStackSlotPostExpansion(MI, FI)) {
      if (Set.count(Reg))
        S.StackToStackCopies++;
      else
        S.OtherToStackCopies++;
    } else {
      for (auto &MO : MI.operands()) {
        // TODO: Doesn't deal with overlapping registers correctly.
        if (MO.isReg() && MO.isDef()) {
          if (Register MOReg = MO.getReg())
            Set.erase(unsigned(MOReg));
        }
      }
    }
  }
}

bool StackCopyStats::runOnMachineFunction(MachineFunction &MF) {
  TRI = MF.getRegInfo().getTargetRegisterInfo();
  TII = MF.getSubtarget().getInstrInfo();

  StackRegMap InMap;
  for (auto &MBB : MF)
    InMap.insert({&MBB, {}});

  bool Changed = true;
  do {
    Changed = false;
    for (auto &MBB : MF) {
      if (computeStackRegs(MBB, InMap))
        Changed = true;
    }
  } while (Changed);

  StackCopyStats::Stats S;
  for (auto &MBB : MF)
    accumulateStats(MBB, InMap, S);

  if (!HeaderPrinted) {
    OS << "Function,Stack to Stack,Other to Stack,Total Instrs\n";
    HeaderPrinted = true;
  }

  OS << MF.getName() << "," << S.StackToStackCopies << ","
     << S.OtherToStackCopies << "," << S.TotalInstrs << "\n";

  return false;
}

void StackCopyStats::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<MachineModuleInfoWrapperPass>();
}

char StackCopyStats::ID = 0;
INITIALIZE_PASS(StackCopyStats, "stack-copy-stats",
                "Dump statistics about the number of stack copies", false,
                false)

MachineFunctionPass *llvm::createStackCopyStatsPass(raw_ostream &OS) {
  return new StackCopyStats(OS);
}
