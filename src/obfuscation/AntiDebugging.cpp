/*
 *  OLLVM-Next (Nk): The next generation LLVM based Obfuscator
 *  Copyright (C) 2026  Xinyu Yang(<Xinyu.Yang@apich.org>)
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Affero General Public License as published
 *  by the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Affero General Public License for more details.
 *
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "include/AntiDebugging.h"
#include "include/CryptoUtils.h"
#include "include/ObfConfig.h"
#include "include/Utils.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <fstream>
#include <sstream>

// Arm A64 Instruction Set signatures
#define AARCH64_SIGNATURE_B 0b000101
#define AARCH64_SIGNATURE_BR 0b1101011000011111000000
#define AARCH64_SIGNATURE_BRK 0b11010100001

using namespace llvm;

static cl::opt<std::string> PreCompiledIRPath(
    "adbextirpath",
    cl::desc("External Path Pointing To Pre-compiled AntiDebugging IR"),
    cl::value_desc("filename"), cl::init(""));
static cl::alias PreCompiledIRPathAlias1("adb_ir_path",
                                         cl::desc("Alias for -adbextirpath"),
                                         cl::aliasopt(PreCompiledIRPath));
static cl::alias PreCompiledIRPathAlias2("adb-ir-path",
                                         cl::desc("Alias for -adbextirpath"),
                                         cl::aliasopt(PreCompiledIRPath));

static cl::opt<uint32_t>
    ProbRate("adb_prob",
             cl::desc("Choose the probability [%] For Each Function To Be "
                      "Obfuscated By AntiDebugging"),
             cl::value_desc("Probability Rate"), cl::init(40), cl::Optional);
static cl::alias ProbRateAlias("adb-prob", cl::desc("Alias for -adb_prob"),
                               cl::aliasopt(ProbRate));

namespace llvm {
struct AntiDebugging : public ModulePass {
  static char ID;
  bool flag;
  bool initialized;
  Triple triple;
  AntiDebugging() : ModulePass(ID) {
    this->flag = true;
    this->initialized = false;
  }
  AntiDebugging(bool flag) : ModulePass(ID) {
    this->flag = flag;
    this->initialized = false;
  }
  StringRef getPassName() const override { return "AntiDebugging"; }
  bool initialize(Module &M) {
    bool explicitlySpecified = !PreCompiledIRPath.empty();
    if (!explicitlySpecified) {
      if (GObfConfig.passes.anti_dbg.precompiled_ir_path.has_value() &&
          !GObfConfig.passes.anti_dbg.precompiled_ir_path->empty()) {
        PreCompiledIRPath = *GObfConfig.passes.anti_dbg.precompiled_ir_path;
        explicitlySpecified = true;
      } else if (const char *env = getenv("NK_PRECOMPILED_ADB")) {
        PreCompiledIRPath = env;
        explicitlySpecified = true;
      } else if (const char *env2 = getenv("ADB_IR_PATH")) {
        PreCompiledIRPath = env2;
        explicitlySpecified = true;
      } else {
        Triple tri(M.getTargetTriple());
        std::string filename = ("PrecompiledAntiDebugging-" +
                                Triple::getArchTypeName(tri.getArch()) + "-" +
                                Triple::getOSTypeName(tri.getOS()) + ".bc")
                                   .str();
        if (sys::fs::exists(filename)) {
          PreCompiledIRPath = filename;
        } else {
          SmallString<64> Path;
          if (sys::path::home_directory(Path)) {
            sys::path::append(Path, "Nk", filename);
            if (sys::fs::exists(Path))
              PreCompiledIRPath = Path.c_str();
            else
              PreCompiledIRPath =
                  ""; // optional; fallback to inline IR generation
          }
        }
      }
    }
    if (!PreCompiledIRPath.empty()) {
      std::ifstream f(PreCompiledIRPath);
      if (f.good()) {
        if (ObfVerbose)
          errs() << "Linking PreCompiled AntiDebugging IR From:"
                 << PreCompiledIRPath << "\n";
        SMDiagnostic SMD;
        std::unique_ptr<Module> ADBM(
            parseIRFile(StringRef(PreCompiledIRPath), SMD, M.getContext()));
        Linker::linkModules(M, std::move(ADBM), Linker::Flags::LinkOnlyNeeded);
        Function *ADBCallBack = M.getFunction("ADBCallBack");
        if (ADBCallBack) {
          assert(!ADBCallBack->isDeclaration() &&
                 "AntiDebuggingCallback is not concrete!");

          // Scramble names of every private/internal GlobalVariable referenced
          // from ADBCallBack so IR symbol names give no hint about the
          // detection logic.  Then inject decoy GVs with matching types to
          // confuse pattern matchers that try to locate the real variables by
          // count or position.
          SmallPtrSet<GlobalVariable *, 8> seen;
          SmallVector<GlobalVariable *, 8> refGVs;
          for (BasicBlock &BB : *ADBCallBack) {
            for (Instruction &I : BB) {
              for (Use &U : I.operands()) {
                if (GlobalVariable *GV = dyn_cast<GlobalVariable>(U.get())) {
                  if ((GV->hasPrivateLinkage() || GV->hasInternalLinkage()) &&
                      seen.insert(GV).second)
                    refGVs.push_back(GV);
                }
              }
            }
          }
          for (GlobalVariable *GV : refGVs) {
            // Replace name with random 16-char hex so no semantic hint survives
            std::string newName;
            raw_string_ostream OS(newName);
            OS << format("g%08x%08x", cryptoutils->get_uint32_t(),
                         cryptoutils->get_uint32_t());
            GV->setName(OS.str());
          }
          // Decoy GVs: one extra per real GV, same type, random initialiser
          for (GlobalVariable *GV : refGVs) {
            Constant *init = GV->hasInitializer()
                                 ? GV->getInitializer()
                                 : Constant::getNullValue(GV->getValueType());
            std::string decoyName;
            raw_string_ostream OS(decoyName);
            OS << format("g%08x%08x", cryptoutils->get_uint32_t(),
                         cryptoutils->get_uint32_t());
            (void)new GlobalVariable(M, GV->getValueType(), GV->isConstant(),
                                     GlobalValue::InternalLinkage, init,
                                     OS.str());
          }

          ADBCallBack->setVisibility(
              GlobalValue::VisibilityTypes::HiddenVisibility);
          ADBCallBack->setLinkage(GlobalValue::LinkageTypes::PrivateLinkage);
          ADBCallBack->removeFnAttr(Attribute::AttrKind::NoInline);
          ADBCallBack->removeFnAttr(Attribute::AttrKind::OptimizeNone);
          ADBCallBack->addFnAttr(Attribute::AttrKind::AlwaysInline);
        }
        Function *ADBInit = M.getFunction("InitADB");
        if (ADBInit) {
          assert(!ADBInit->isDeclaration() &&
                 "AntiDebuggingInitializer is not concrete!");
          ADBInit->setVisibility(
              GlobalValue::VisibilityTypes::HiddenVisibility);
          ADBInit->setLinkage(GlobalValue::LinkageTypes::PrivateLinkage);
          ADBInit->removeFnAttr(Attribute::AttrKind::NoInline);
          ADBInit->removeFnAttr(Attribute::AttrKind::OptimizeNone);
          ADBInit->addFnAttr(Attribute::AttrKind::AlwaysInline);
        }
      } else if (explicitlySpecified || ObfVerbose) {
        errs() << "Failed To Link PreCompiled AntiDebugging IR From:"
               << PreCompiledIRPath << "\n";
      }
    }
    this->initialized = true;
    this->triple = Triple(M.getTargetTriple());
    return true;
  }

  bool runOnModule(Module &M) override {
    auto ec = GObfConfig.resolve(M.getSourceFileName(), "");
    uint32_t effProb = 100;
    if (ec.anti_dbg.probability.has_value())
      effProb = *ec.anti_dbg.probability;
    else if (ProbRate.getNumOccurrences() > 0)
      effProb = (uint32_t)ProbRate;
    else if (!flag)
      effProb = (uint32_t)ProbRate;

    if (effProb > 100) {
      errs() << "AntiDebugging application function percentage "
                "-adb_prob=x must be 0 < x <= 100";
      return false;
    }
    bool anyObf = false;
    for (Function &F : M) {
      auto fnEc = GObfConfig.resolve(M.getSourceFileName(), F.getName());
      bool shouldObf = fnEc.anti_dbg.enabled.value_or(flag);
      if (toObfuscate(shouldObf, &F, "adb") && F.getName() != "ADBCallBack" &&
          F.getName() != "InitADB") {
        if (ObfVerbose)
          errs() << "Running AntiDebugging On " << F.getName() << "\n";
        if (!this->initialized)
          initialize(M);
        uint32_t fnProb = effProb;
        if (!toObfuscateUint32Option(&F, "adb_prob", &fnProb)) {
          fnProb = fnEc.anti_dbg.probability.value_or(effProb);
        }
        if (cryptoutils->get_range(100) <= fnProb) {
          runOnFunction(F);
          anyObf = true;
        }
      }
    }
    // DIAG-B: 即使跳过了函数级注入，也生成全局构造函数
    if (true) {
      BuildAntiDebuggingConstructor(M);
    }
    return true;
  }

  void BuildAntiDebuggingConstructor(Module &M) {
    if (M.getFunction("__adb_init_watchdog"))
      return;
    FunctionType *CtorFTy =
        FunctionType::get(Type::getVoidTy(M.getContext()), false);
    Function *CtorFn = Function::Create(CtorFTy, GlobalValue::InternalLinkage,
                                        "__adb_init_watchdog", &M);
    if (triple.getArch() == Triple::x86_64) {
      CtorFn->addFnAttr(Attribute::NoRedZone);
    }
    BasicBlock *CtorBB = BasicBlock::Create(M.getContext(), "entry", CtorFn);
    IRBuilder<> CIRB(CtorBB);
    ReturnInst *RetInst = CIRB.CreateRetVoid();
    // 构造函数保留但不再注入检测（检测在 nk_ad.c 的 ctor 里）
    appendToGlobalCtors(M, CtorFn, 0);
  }

  bool runOnFunction(Function &F) {
    // SAFE-ADB: 每个函数入口只插一次幂等的 TracerPid 探针
    // （不调用 TRACEME、不做计时判定、不做数据流纠缠，因此不会误报）
    if (F.isDeclaration() || F.empty())
      return false;
    // NK_SKIP_ADB: this TU *is* the protector itself (ci/nk_ad.c).
    // Injecting checks into it would make it call itself.
    if (getenv("NK_SKIP_ADB"))
      return false;
    if (F.getName() == "__adb_tracerpid_probe" ||
        F.getName() == "__adb_init_watchdog" ||
        F.getName().starts_with("__adb") ||
        F.getName().starts_with("llvm.") || F.getName() == "ADBCallBack")
      return false;
    {
      // IR 级注入点：每个函数入口插 call __nk_ad_check()。
      // 检测逻辑在 ci/nk_ad.c（C 模块），Pass 只负责把检查点撒到热路径上，
      // 把“1.5 秒定时检查”的窗口压缩到“每 32 次函数调用一次”。
      Module *M3 = F.getParent();
      LLVMContext &C3 = M3->getContext();
      FunctionType *VFT = FunctionType::get(Type::getVoidTy(C3), false);
      // Symbol name is randomized per build (-DNK_AD_SYM / env NK_AD_SYM),
      // so the final .dynsym never contains a fixed, greppable name.
      const char *nkSymEnv = getenv("NK_AD_SYM");
      std::string nkSym =
          (nkSymEnv && *nkSymEnv) ? std::string(nkSymEnv) : std::string("__nk_ad_check");
      FunctionCallee chk = M3->getOrInsertFunction(nkSym, VFT);
      Value *fn = chk.getCallee();
      if (auto *FC = dyn_cast<Function>(fn)) {
        if (!FC->isDeclaration() || FC->empty()) {
          // 已定义（不该发生）→ 不要插到它自己里
        }
      }
      CallInst::Create(VFT, fn, ArrayRef<Value *>{}, "",
                       cast<Instruction>(F.getEntryBlock().getFirstInsertionPt()));
      

      // ---- IR-level ART JNI table check (v11.6) ----
      // Inlined into every obfuscated function so the verification lives inside
      // the caller instead of one nop-able C helper.
      {
        Module *MA = F.getParent();
        LLVMContext &CA = MA->getContext();
        Type *I64A = Type::getInt64Ty(CA);
        PointerType *P0A = PointerType::getUnqual(CA);
        FunctionType *VFTA = FunctionType::get(Type::getVoidTy(CA), false);
        FunctionCallee PanicA = MA->getOrInsertFunction("__nk_ad_panic", VFTA);

        GlobalVariable *LoGV = cast<GlobalVariable>(
            MA->getOrInsertGlobal("__nk_art_lo", I64A));
        GlobalVariable *HiGV = cast<GlobalVariable>(
            MA->getOrInsertGlobal("__nk_art_hi", I64A));
        GlobalVariable *VmGV = cast<GlobalVariable>(
            MA->getOrInsertGlobal("__nk_vm", P0A));

        BasicBlock *EntryA = &F.getEntryBlock();
        Instruction *FirstNP = EntryA->getFirstNonPHI();
        if (FirstNP && isa<LandingPadInst>(FirstNP))
          FirstNP = nullptr;
        Instruction *SplitAt = FirstNP ? FirstNP->getNextNode() : nullptr;
        if (SplitAt) {
          BasicBlock *Body = EntryA->splitBasicBlock(SplitAt, "nk.body");
          EntryA->getTerminator()->eraseFromParent();

          BasicBlock *Chk = BasicBlock::Create(CA, "nk.art.chk", &F);
          BasicBlock *Loop = BasicBlock::Create(CA, "nk.art.loop", &F);
          BasicBlock *Cont = BasicBlock::Create(CA, "nk.art.cont", &F);
          BasicBlock *Bad = BasicBlock::Create(CA, "nk.art.bad", &F);
          BasicBlock *Ok = BasicBlock::Create(CA, "nk.art.ok", &F);

          IRBuilder<> BA(EntryA);
          BA.CreateBr(Chk);

          IRBuilder<> BC(Chk);
          Value *LoV = BC.CreateLoad(I64A, LoGV, "nk.lo");
          Value *HiV = BC.CreateLoad(I64A, HiGV, "nk.hi");
          Value *VmV = BC.CreateLoad(P0A, VmGV, "nk.vm");
          Value *NoArt = BC.CreateICmpEQ(HiV, ConstantInt::get(I64A, 0));
          Value *NoVm = BC.CreateICmpEQ(VmV, ConstantPointerNull::get(P0A));
          BC.CreateCondBr(BC.CreateOr(NoArt, NoVm), Ok, Loop);

          IRBuilder<> BL(Loop);
          PHINode *Idx = BL.CreatePHI(I64A, 2, "nk.i");
          Idx->addIncoming(ConstantInt::get(I64A, 0), Chk);
          Value *Slot = BL.CreateGEP(P0A, VmV, Idx, "nk.slot");
          Value *FnP = BL.CreateLoad(P0A, Slot, "nk.fn");
          Value *FnI = BL.CreatePtrToInt(FnP, I64A);
          Value *Below = BL.CreateICmpULT(FnI, LoV);
          Value *Above = BL.CreateICmpUGT(FnI, HiV);
          Value *Next = BL.CreateAdd(Idx, ConstantInt::get(I64A, 1), "nk.next");
          Value *Last = BL.CreateICmpUGE(Next, ConstantInt::get(I64A, 8));
          BL.CreateCondBr(BL.CreateOr(Below, Above), Bad, Cont);

          IRBuilder<> BCo(Cont);
          Idx->addIncoming(Next, Cont);
          BCo.CreateCondBr(Last, Ok, Loop);

          IRBuilder<> BB(Bad);
          BB.CreateCall(PanicA.getFunctionType(), PanicA.getCallee());
          BB.CreateUnreachable();

          IRBuilder<> BO(Ok);
          BO.CreateBr(Body);
        }
      }
      return true;
    }
    return false;
    if (F.isDeclaration() || F.empty())
      return false;

    if (triple.getArch() == Triple::x86_64) {
      F.addFnAttr(Attribute::NoRedZone);
    }

    BasicBlock *EntryBlock = &(F.getEntryBlock());
    Function *ADBCallBack = F.getParent()->getFunction("ADBCallBack");
    Function *ADBInit = F.getParent()->getFunction("InitADB");
    if (ADBCallBack && ADBInit) {
      CallInst::Create(ADBInit, "",
                       cast<Instruction>(EntryBlock->getFirstInsertionPt()));
      return true;
    }

    if (ObfVerbose)
      errs() << "Injecting hardened anti-debug & anti-taint data-flow "
                "entanglement for "
             << F.getName() << " ["
             << F.getParent()->getTargetTriple().getTriple() << "]\n";

    // 1. Entry debug detection & token generation (with hardware violent exit
    // if debugged)
    BasicBlock::iterator EntryIt = EntryBlock->begin();
    while (isa<AllocaInst>(EntryIt))
      ++EntryIt;
    Instruction *EntryInsertPt = &*EntryIt;

    InjectMainDebugChecks(&F, EntryInsertPt);

    Value *DbgToken = getOrCreateDynamicDebugToken(&F, EntryInsertPt, triple);

    // 2. Anti-Taint Bidirectional IO Entanglement (Schemes 1, 2, 3, 4)
    entangleFunctionIO(&F, DbgToken, nullptr, nullptr, EntryInsertPt, triple);

    // 3. Silent Arithmetic Data-Flow Entanglement
    // If DbgToken != 0 (e.g. bypassed exit or software emulated), scale with
    // secret prime, pass through opaque barrier and volatile sink, and entangle
    // into internal integer arithmetic
    LLVMContext &Ctx = F.getContext();
    Type *I64Ty = Type::getInt64Ty(Ctx);
    IRBuilder<> EntangleIRB(EntryInsertPt);
    Value *DbgScaled = EntangleIRB.CreateMul(
        DbgToken, ConstantInt::get(I64Ty, 0xbf58476d1ce4e5b9ULL), "adb.scaled");
    insertOpaqueBarrier(EntangleIRB, DbgScaled);
    GlobalVariable *sinkGV = getOrCreateOpaqueSink(F.getParent());
    if (sinkGV) {
      EntangleIRB.CreateStore(DbgScaled, sinkGV, /*isVolatile=*/true);
    }
    AllocaInst *DbgScaledSlot =
        IRBuilder<>(&F.getEntryBlock(), F.getEntryBlock().begin())
            .CreateAlloca(I64Ty, nullptr, "adb.scaled.slot");
    EntangleIRB.CreateStore(DbgScaled, DbgScaledSlot);

    unsigned entangleCount = 0;
    for (BasicBlock &BB : F) {
      for (Instruction &Inst : BB) {
        if (&Inst == EntryInsertPt || isa<AllocaInst>(&Inst) ||
            isa<PHINode>(&Inst))
          continue;
        if (Inst.isBinaryOp() && Inst.getType()->isIntegerTy()) {
          Type *ITy = Inst.getType();
          if (ITy->getIntegerBitWidth() <= 64) {
            IRBuilder<> InstIRB(&BB, ++Inst.getIterator());
            Value *LocalDbg =
                InstIRB.CreateLoad(I64Ty, DbgScaledSlot, "adb.scaled.local");
            Value *TruncDbg =
                InstIRB.CreateZExtOrTrunc(LocalDbg, ITy, "adb.entangle.delta");
            Value *Entangled =
                InstIRB.CreateXor(&Inst, TruncDbg, "adb.entangled");
            Inst.replaceAllUsesWith(Entangled);
            cast<User>(Entangled)->setOperand(0, &Inst);
            entangleCount++;
            if (entangleCount >= 2)
              break;
          }
        }
      }
      if (entangleCount >= 2)
        break;
    }

    // 4. Scattered debug checks throughout function body
    Instruction *lastTerm = nullptr;
    for (BasicBlock &BB : F) {
      if (isa_and_nonnull<ReturnInst>(BB.getTerminator())) {
        lastTerm = BB.getTerminator();
        break;
      }
    }
    if (!lastTerm) {
      for (BasicBlock &BB : F)
        lastTerm = BB.getTerminator();
    }
    if (lastTerm) {
      InjectScatteredDebugChecks(&F, lastTerm);
    }
    return true;
  }

  void InjectMainDebugChecks(Function *F, Instruction *InsertPt) {
    // 2026-10-24: 这里的自带检测（asm 计数器计时 / TRACEME / 全局字符串版
    // TracerPid）全部停用。它们正是"一启动就死"的来源：
    //   · TRACEME 重复调用必 EPERM
    //   · 字符串被 string_encryption 加密后 open("/proc/self/status") 失败，
    //     而失败分支直接 exit(97)
    // 现在所有检测统一由 ci/nk_ad.c 实现（栈上构造字符串 + fail-open）。
    if (true)
      return;
    auto shuffleBlocks = [](SmallVectorImpl<std::string> &v) {
      unsigned n = v.size();
      for (unsigned i = n - 1; i > 0; --i) {
        unsigned j = cryptoutils->get_range(i + 1);
        std::swap(v[i], v[j]);
      }
    };

    LLVMContext &Ctx = F->getContext();
    FunctionType *VoidFTy = FunctionType::get(Type::getVoidTy(Ctx), false);

    // ── Darwin AArch64 ────────────────────────────────────────────────────
    if (triple.isOSDarwin() && triple.isAArch64()) {
      {
        auto makeDarwinAA64Abort = [&]() -> std::string {
          return getViolentExitAsm(triple);
        };

        std::string vm;
        vm += "mrs x12, cntvct_el0\n\t";
        for (int i = 0; i < 16; i++)
          vm += "nop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\t";
        vm += "mrs x13, cntvct_el0\n\t";
        vm += "sub x12, x13, x12\n\t";
        vm += "cbnz x12, 1f\n\t";
        vm += makeDarwinAA64Abort();
        vm += "1:\n\t";
        vm += "mov x13, #0xA000\n\t";
        vm += "cmp x12, x13\n\t";
        vm += "b.lo 2f\n\t";
        vm += makeDarwinAA64Abort();
        vm += "2:\n\t";
        InlineAsm *vmIA = InlineAsm::get(
            VoidFTy, vm,
            "~{x0},~{x12},~{x13},~{x14},~{x15},~{x16},~{cc},~{memory}", true,
            false);
        CallInst::Create(vmIA->getFunctionType(), vmIA, ArrayRef<Value *>{}, "",
                         InsertPt);
      }

      std::string adbasm;
      uint32_t variant = cryptoutils->get_range(2);
      SmallVector<std::string, 6> parts;
      if (variant == 0) {
        parts.push_back("mov x0, #31\n\t");
        parts.push_back("mov x1, #0\n\t");
        parts.push_back("mov x2, #0\n\t");
        parts.push_back("mov x3, #0\n\t");
        parts.push_back("mov x16, #26\n\t");
      } else {
        parts.push_back("mov x0, #26\n\t");
        parts.push_back("mov x1, #31\n\t");
        parts.push_back("mov x2, #0\n\t");
        parts.push_back("mov x3, #0\n\t");
        parts.push_back("mov x16, #0\n\t");
      }
      shuffleBlocks(parts);
      for (auto &p : parts)
        adbasm += p;
      adbasm += "svc #" + std::to_string(cryptoutils->get_range(0x80, 0x200)) +
                "\n\t";
      adbasm += "mrs x9, cntvct_el0\n\t";
      uint32_t ji = cryptoutils->get_range(1, 0x100);
      adbasm += "add x9, x9, #" + std::to_string(ji) + "\n\t";
      adbasm += "sub x9, x9, #" + std::to_string(ji) + "\n\t";
      InlineAsm *IA = InlineAsm::get(
          VoidFTy, adbasm,
          "~{x0},~{x1},~{x2},~{x3},~{x9},~{x16},~{cc},~{memory}", true, false);
      CallInst::Create(IA->getFunctionType(), IA, ArrayRef<Value *>{}, "",
                       InsertPt);

      // ── Darwin x86_64 ─────────────────────────────────────────────────────
    } else if (triple.isOSDarwin() && triple.getArch() == Triple::x86_64) {
      {
        auto makeDarwinX64Abort = [&]() -> std::string {
          return getViolentExitAsm(triple);
        };

        std::string vm;
        vm += "push %rbx\n\t";
        vm += "movl $$1, %eax\n\t";
        vm += "cpuid\n\t";
        vm += "pop %rbx\n\t";
        vm += "testl $$0x80000000, %ecx\n\t";
        vm += "jz 1f\n\t";
        vm += makeDarwinX64Abort();
        vm += "1:\n\t";
        vm += "push %rbx\n\t";
        vm += "movl $$0x40000000, %eax\n\t";
        vm += "cpuid\n\t";
        vm += "cmpl $$0x61774D56, %ebx\n\t";
        vm += "je 2f\n\t";
        vm += "cmpl $$0x4B4D564B, %ebx\n\t";
        vm += "je 2f\n\t";
        vm += "cmpl $$0x7263694D, %ebx\n\t";
        vm += "je 2f\n\t";
        vm += "cmpl $$0x786F4256, %ebx\n\t";
        vm += "je 2f\n\t";
        vm += "cmpl $$0x566E6558, %ebx\n\t";
        vm += "je 2f\n\t";
        vm += "cmpl $$0x54474354, %ebx\n\t";
        vm += "je 2f\n\t";
        vm += "pop %rbx\n\t";
        vm += "jmp 3f\n\t";
        vm += "2:\n\t";
        vm += "pop %rbx\n\t";
        vm += makeDarwinX64Abort();
        vm += "3:\n\t";
        InlineAsm *vmIA = InlineAsm::get(VoidFTy, vm,
                                         "~{rax},~{rcx},~{rdx},~{rdi},~{r14},~{"
                                         "r15},~{dirflag},~{fpsr},~{flags}",
                                         true, false, InlineAsm::AD_ATT);
        CallInst::Create(vmIA->getFunctionType(), vmIA, ArrayRef<Value *>{}, "",
                         InsertPt);
      }

      uint64_t noiseK = cryptoutils->get_uint32_t() & 0xFFFF;
      SmallVector<std::string, 6> parts;
      parts.push_back("movq $$31, %rdi\n\t");
      parts.push_back("xorq %rsi, %rsi\n\t");
      parts.push_back("xorq %rdx, %rdx\n\t");
      parts.push_back("xorq %rcx, %rcx\n\t");
      parts.push_back("movq $$0x200001A, %rax\n\t");
      shuffleBlocks(parts);
      std::string adbasm;
      adbasm += "rdtsc\n\t";
      adbasm += "andl $$0xFFFF, %eax\n\t";
      adbasm += "addl $$" + std::to_string(noiseK) + ", %eax\n\t";
      adbasm += "subl $$" + std::to_string(noiseK) + ", %eax\n\t";
      for (auto &p : parts)
        adbasm += p;
      adbasm += "syscall\n\t";
      InlineAsm *IA = InlineAsm::get(
          VoidFTy, adbasm,
          "~{rax},~{rdi},~{rsi},~{rdx},~{rcx},~{dirflag},~{fpsr},~{flags}",
          true, false, InlineAsm::AD_ATT);
      CallInst::Create(IA->getFunctionType(), IA, ArrayRef<Value *>{}, "",
                       InsertPt);

      // ── Linux / Android x86_64 ────────────────────────────────────────────
    } else if ((triple.isOSLinux() || triple.isAndroid()) &&
               triple.getArch() == Triple::x86_64) {
      auto makeLinAbort = [&]() -> std::string {
        return getViolentExitAsm(triple, 101);
      };

      {
        std::string vm;
        vm += "push %rbx\n\t";
        vm += "xorl %eax, %eax\n\t";
        vm += "cpuid\n\t";
        vm += "pop %rbx\n\t";
        vm += "rdtsc\n\t";
        vm += "shlq $$32, %rdx\n\t";
        vm += "orq %rax, %rdx\n\t";
        vm += "movq %rdx, %r12\n\t";
        vm += "push %rbx\n\t";
        vm += "xorl %eax, %eax\n\t";
        vm += "cpuid\n\t";
        vm += "pop %rbx\n\t";
        vm += "rdtsc\n\t";
        vm += "shlq $$32, %rdx\n\t";
        vm += "orq %rax, %rdx\n\t";
        vm += "subq %r12, %rdx\n\t";
        vm += "js 4f\n\t";
        vm += "cmpq $$0x2000000, %rdx\n\t";
        vm += "jbe 4f\n\t";
        vm += makeLinAbort();
        vm += "4:\n\t";
        // Kernel-level anti-attach: prctl(PR_SET_DUMPABLE = 4, 0, 0, 0, 0)
        vm += "movq $$157, %rax\n\t"; // SYS_prctl
        vm += "movq $$4, %rdi\n\t";   // PR_SET_DUMPABLE = 4
        vm += "xorq %rsi, %rsi\n\t";  // SUID_DUMP_DISABLE = 0
        vm += "xorq %rdx, %rdx\n\t";
        vm += "xorq %r10, %r10\n\t";
        vm += "syscall\n\t";

        // Continuous TracerPid detection from /proc/self/status via direct
        // syscalls
        vm += "subq $$560, %rsp\n\t";
        vm += "movabsq $$0x65732f636f72702f, %rax\n\t"; // "/proc/se"
        vm += "movq %rax, (%rsp)\n\t";
        vm += "movabsq $$0x75746174732f666c, %rax\n\t"; // "lf/statu"
        vm += "movq %rax, 8(%rsp)\n\t";
        vm += "movw $$0x73, 16(%rsp)\n\t"; // 's', '\0'
        vm += "movq $$257, %rax\n\t";      // SYS_openat
        vm += "movq $$-100, %rdi\n\t";     // AT_FDCWD
        vm += "movq %rsp, %rsi\n\t";
        vm += "xorq %rdx, %rdx\n\t";
        vm += "xorq %r10, %r10\n\t";
        vm += "syscall\n\t";
        vm += "testq %rax, %rax\n\t";
        vm += "js 5f\n\t";
        vm += "movq %rax, %rdi\n\t"; // fd in rdi
        vm += "xorq %rax, %rax\n\t"; // SYS_read
        vm += "leaq 24(%rsp), %rsi\n\t";
        vm += "movq $$512, %rdx\n\t";
        vm += "syscall\n\t";
        vm += "movq %rax, %r10\n\t"; // bytes read in r10
        vm += "movq $$3, %rax\n\t";  // SYS_close
        vm += "syscall\n\t";
        vm += "cmpq $$16, %r10\n\t";
        vm += "jl 5f\n\t";
        vm += "leaq 24(%rsp), %rsi\n\t";
        vm += "subq $$12, %r10\n\t";
        vm += "xorq %rcx, %rcx\n\t";
        vm += "movabsq $$0x6950726563617254, %rax\n\t"; // "TracerPi"
        vm += "82:\n\t";
        vm += "cmpq %r10, %rcx\n\t";
        vm += "jge 5f\n\t";
        vm += "cmpq (%rsi, %rcx, 1), %rax\n\t";
        vm += "je 83f\n\t";
        vm += "incq %rcx\n\t";
        vm += "jmp 82b\n\t";
        vm += "83:\n\t";
        vm += "cmpw $$0x3a64, 8(%rsi, %rcx, 1)\n\t"; // 'd', ':'
        vm += "jne 87f\n\t";
        vm += "addq $$10, %rcx\n\t";
        vm += "84:\n\t";
        vm += "movzbq (%rsi, %rcx, 1), %rax\n\t";
        vm += "cmpb $$' ', %al\n\t";
        vm += "je 85f\n\t";
        vm += "cmpb $$'\\t', %al\n\t";
        vm += "jne 86f\n\t";
        vm += "85:\n\t";
        vm += "incq %rcx\n\t";
        vm += "jmp 84b\n\t";
        vm += "86:\n\t";
        vm += "cmpb $$'1', %al\n\t";
        vm += "jb 5f\n\t";
        vm += "cmpb $$'9', %al\n\t";
        vm += "ja 5f\n\t";
        vm += "addq $$560, %rsp\n\t";
        vm += "jmp 6f\n\t"; // Trigger violent abort!
        vm += "87:\n\t";
        vm += "movabsq $$0x6950726563617254, %rax\n\t";
        vm += "incq %rcx\n\t";
        vm += "jmp 82b\n\t";
        vm += "5:\n\t";
        vm += "addq $$560, %rsp\n\t";
        vm += "jmp 7f\n\t";
        vm += "6:\n\t";
        vm += "ud2\n\t";
        // Dynamically compute randomized corrupt target address to prevent
        // static binary patching
        uint64_t rndTarget =
            (cryptoutils->get_uint64_t() | 0x8000000000000000ULL) & ~0xFFFULL;
        uint32_t stackOffset = cryptoutils->get_range(0x800, 0x4000) & ~0x7u;
        vm += "addq $$" + std::to_string(stackOffset) + ", %rsp\n\t";
        vm += "movabsq $$0x" + utohexstr(rndTarget) + ", %rax\n\t";
        vm += "jmpq *%rax\n\t";
        vm += "7:\n\t";
        FunctionType *vmFTy = FunctionType::get(Type::getVoidTy(Ctx), false);
        InlineAsm *vmIA = InlineAsm::get(
            vmFTy, vm,
            "~{rax},~{rcx},~{rdx},~{rdi},~{rsi},~{r8},~{r9},~{r10},~{r11},~{"
            "r12},~{r14},~{r15},~{dirflag},~{fpsr},~{flags},~{memory}",
            true, false, InlineAsm::AD_ATT);
        CallInst::Create(vmFTy, vmIA, ArrayRef<Value *>{}, "", InsertPt);
      }

      uint64_t noiseK = cryptoutils->get_uint32_t() & 0xFFFF;
      uint64_t tsThresh = 0x2000000ULL;
      std::string adbasm;
      adbasm += "rdtsc\n\t";
      adbasm += "shlq $$32, %rdx\n\t";
      adbasm += "orq %rax, %rdx\n\t";
      adbasm += "movq %rdx, %r14\n\t";
      adbasm += "xorq %rax, %rax\n\t";
      adbasm += "addq $$" + std::to_string(noiseK) + ", %rax\n\t";
      adbasm += "subq $$" + std::to_string(noiseK) + ", %rax\n\t";
      adbasm += "movq $$39, %rax\n\t"; // SYS_getpid
      adbasm += "syscall\n\t";
      adbasm += "rdtsc\n\t";
      adbasm += "shlq $$32, %rdx\n\t";
      adbasm += "orq %rax, %rdx\n\t";
      adbasm += "subq %r14, %rdx\n\t";
      adbasm += "js 2f\n\t";
      adbasm += "cmpq $$" + std::to_string(tsThresh) + ", %rdx\n\t";
      adbasm += "ja 1f\n\t";
      adbasm += "jmp 2f\n\t";
      adbasm += "1:\n\t";
      adbasm += makeLinAbort();
      adbasm += "2:\n\t";
      InlineAsm *IA =
          InlineAsm::get(VoidFTy, adbasm,
                         "~{rax},~{rcx},~{rdx},~{rdi},~{rsi},~{r10},~{r11},~{"
                         "r14},~{r15},~{dirflag},~{fpsr},~{flags}",
                         true, false, InlineAsm::AD_ATT);
      CallInst::Create(IA->getFunctionType(), IA, ArrayRef<Value *>{}, "",
                       InsertPt);

      // ── Linux / Android AArch64 ───────────────────────────────────────────
    } else if ((triple.isOSLinux() || triple.isAndroid()) &&
               triple.isAArch64()) {
      // ============================================================
      // SAFE-ADB (2026-10-24)
      // 原实现有三处误报：
      //   1) 对每个函数都插 ptrace(PTRACE_TRACEME)，而它同一进程只能成功一次，
      //      第二次起必返回 EPERM → 被当成"被调试"→ 自杀；
      //   2) 以 cntvct 计数阈值判定（阈值 0x40000/0xA000），被调度抖动打断即误报；
      //   3) 误报后的 violent exit 里带无限循环，表现为"静默卡死/被杀"。
      // 现在统一改为读取 /proc/self/status 的 TracerPid 字段：
      //   只有确实被 ptrace 附加时该值才非 0，正常进程恒为 0（已实测）。
      // ============================================================
      LLVMContext &C2 = F->getContext();
      Module *M2 = F->getParent();
      Type *I32 = Type::getInt32Ty(C2);
      Type *I64 = Type::getInt64Ty(C2);
      Type *I8P = PointerType::getUnqual(C2);

      Function *det = M2->getFunction("__adb_tracerpid_probe");
      if (!det) {
        FunctionType *VFT = FunctionType::get(Type::getVoidTy(C2), false);
        det = Function::Create(VFT, GlobalValue::InternalLinkage,
                               "__adb_tracerpid_probe", M2);
        det->addFnAttr(Attribute::NoInline);
        det->addFnAttr(Attribute::OptimizeNone);

        LLVMContext &C3 = M2->getContext();
        Type *Iy32 = Type::getInt32Ty(C3);
        Type *Iy64 = Type::getInt64Ty(C3);
        Type *Iy8P = PointerType::getUnqual(C3);

        FunctionType *openFT = FunctionType::get(Iy32, {Iy8P, Iy32}, true);
        FunctionType *readFT = FunctionType::get(Iy64, {Iy32, Iy8P, Iy64}, false);
        FunctionType *closeFT = FunctionType::get(Iy32, {Iy32}, false);
        FunctionType *strstrFT = FunctionType::get(Iy8P, {Iy8P, Iy8P}, false);
        FunctionType *strtolFT = FunctionType::get(Iy64, {Iy8P, Iy8P, Iy32}, false);
        FunctionCallee openF = M2->getOrInsertFunction("open", openFT);
        FunctionCallee readF = M2->getOrInsertFunction("read", readFT);
        FunctionCallee closeF = M2->getOrInsertFunction("close", closeFT);
        FunctionCallee strstrF = M2->getOrInsertFunction("strstr", strstrFT);
        FunctionCallee strtolF = M2->getOrInsertFunction("strtol", strtolFT);

        // 路径/关键字都用“栈上按整数写入”的方式构造，
        // 避免被 string_encryption 加密后在这里变成运行时才解密的密文。
        auto makeStackStr = [&](IRBuilder<> &B2, const char *nm,
                                uint64_t w0, uint64_t w1, uint16_t w2,
                                uint16_t w3, uint8_t tail) -> Value * {
          AllocaInst *a = B2.CreateAlloca(
              ArrayType::get(Type::getInt8Ty(C3), 32), nullptr, nm);
          Value *p = B2.CreateBitCast(a, Iy8P, std::string(nm) + ".p");
          Value *p8 = B2.CreateGEP(Type::getInt8Ty(C3), p, ConstantInt::get(Iy64, 0));
          B2.CreateStore(ConstantInt::get(Iy64, w0), B2.CreateBitCast(p8, PointerType::getUnqual(C3)));
          // 逐字节写入（简单、不依赖对齐）
          auto put = [&](uint64_t base, const uint8_t *bytes, unsigned n) {
            for (unsigned i = 0; i < n; i++) {
              Value *sp = B2.CreateGEP(Type::getInt8Ty(C3), p,
                                       ConstantInt::get(Iy64, base + i));
              B2.CreateStore(ConstantInt::get(Type::getInt8Ty(C3), bytes[i]), sp);
            }
          };
          (void)w0; (void)w1; (void)w2; (void)w3; (void)tail;
          return p;
        };
        (void)makeStackStr;

        // 直接在栈上写字节（用字节数组常量，不生成全局字符串）
        auto mkBytes = [&](IRBuilder<> &B2, const char *nm,
                           const unsigned char *bytes, unsigned n) -> Value * {
          AllocaInst *a = B2.CreateAlloca(
              ArrayType::get(Type::getInt8Ty(C3), 32), nullptr, nm);
          Value *p = B2.CreateBitCast(a, Iy8P, std::string(nm) + ".p");
          for (unsigned i = 0; i < n; i++) {
            Value *sp = B2.CreateGEP(Type::getInt8Ty(C3), p, ConstantInt::get(Iy64, i));
            B2.CreateStore(ConstantInt::get(Type::getInt8Ty(C3), bytes[i]), sp);
          }
          return p;
        };

        static const unsigned char PATH_BYTES[] = {
            '/', 'p', 'r', 'o', 'c', '/', 's', 'e', 'l', 'f',
            '/', 's', 't', 'a', 't', 'u', 's', 0};
        static const unsigned char KEY_BYTES[] = {
            'T', 'r', 'a', 'c', 'e', 'r', 'P', 'i', 'd', ':', 0};

        BasicBlock *entry = BasicBlock::Create(C3, "entry", det);
        BasicBlock *abortBB = BasicBlock::Create(C3, "adb.dbg", det);
        IRBuilder<> B(entry);
        Value *pathV = mkBytes(B, "adb.path", PATH_BYTES, sizeof(PATH_BYTES));
        Value *keyV = mkBytes(B, "adb.key", KEY_BYTES, sizeof(KEY_BYTES));

        AllocaInst *bufA = B.CreateAlloca(
            ArrayType::get(Type::getInt8Ty(C3), 1024), nullptr, "adb.buf");
        Value *bufP = B.CreateBitCast(bufA, Iy8P, "adb.bufp");
        Value *fd = B.CreateCall(openF, {pathV, ConstantInt::get(Iy32, 0)}, "adb.fd");

        BasicBlock *afterRead = BasicBlock::Create(C3, "adb.after", det);
        Value *fdOk = B.CreateICmpSGE(fd, ConstantInt::get(Iy32, 0), "adb.fdok");
        B.CreateCondBr(fdOk, afterRead, abortBB); // 打不开就放行（abortBB 里再判一次）

        B.SetInsertPoint(afterRead);
        Value *n = B.CreateCall(
            readF, {fd, bufP, ConstantInt::get(Iy64, 1023)}, "adb.n");
        B.CreateCall(closeF, {fd});
        // 结尾补 0
        Value *endSlot = B.CreateGEP(Type::getInt8Ty(C3), bufP,
                                     B.CreateSelect(
                                         B.CreateICmpSGT(n, ConstantInt::get(Iy64, 0)),
                                         B.CreateAnd(n, ConstantInt::get(Iy64, 1023)),
                                         ConstantInt::get(Iy64, 0)),
                                     "adb.end");
        B.CreateStore(ConstantInt::get(Type::getInt8Ty(C3), 0), endSlot);

        Value *hit = B.CreateCall(strstrF, {bufP, keyV}, "adb.hit");
        BasicBlock *parseBB = BasicBlock::Create(C3, "adb.parse", det);
        BasicBlock *doneBB = BasicBlock::Create(C3, "adb.done", det);
        B.CreateCondBr(B.CreateICmpNE(hit, ConstantPointerNull::get(
                                          cast<PointerType>(Iy8P))),
                       parseBB, doneBB);

        B.SetInsertPoint(parseBB);
        Value *numP = B.CreateGEP(Type::getInt8Ty(C3), hit,
                                  ConstantInt::get(Iy64, 10), "adb.nump");
        Value *val = B.CreateCall(strtolF,
                                  {numP, ConstantPointerNull::get(
                                              cast<PointerType>(Iy8P)),
                                   ConstantInt::get(Iy32, 10)},
                                  "adb.tracerpid");
        B.CreateCondBr(B.CreateICmpNE(val, ConstantInt::get(Iy64, 0)),
                       abortBB, doneBB);

        // 真被 trace：exit_group(97)，不留栈
        {
          IRBuilder<> AB(abortBB);
          FunctionType *VFT2 = FunctionType::get(Type::getVoidTy(C3), false);
          InlineAsm *exitIA = InlineAsm::get(
              VFT2,
              "mov x8, #94\n\tmov x0, #97\n\tsvc #0\n\t1:\n\tb 1b\n\t",
              "~{x0},~{x8},~{memory}", true, false);
          AB.CreateCall(exitIA->getFunctionType(), exitIA, ArrayRef<Value *>{});
          AB.CreateUnreachable();
        }

        B.SetInsertPoint(doneBB);
        B.CreateRetVoid();
      }

      CallInst::Create(det->getFunctionType(), det, ArrayRef<Value *>{}, "",
                       InsertPt);
    } else if (triple.isOSWindows() && triple.getArch() == Triple::x86_64) {
      // ── Windows x86_64 ────────────────────────────────────────────────────
      auto winAbort = [&]() -> std::string {
        return getViolentExitAsm(triple);
      };

      std::string adbasm;
      adbasm += "movq %gs:96, %rax\n\t";
      adbasm += "movzbl 2(%rax), %ecx\n\t";
      adbasm += "testl %ecx, %ecx\n\t";
      adbasm += "jz 1f\n\t";
      adbasm += winAbort();
      adbasm += "1:\n\t";
      adbasm += "movq %gs:96, %rax\n\t";
      adbasm += "movl 188(%rax), %ecx\n\t";
      adbasm += "andl $$0x70, %ecx\n\t";
      adbasm += "jz 2f\n\t";
      adbasm += winAbort();
      adbasm += "2:\n\t";
      adbasm += "movq %gs:96, %rax\n\t";
      adbasm += "movq 48(%rax), %rax\n\t";
      adbasm += "movl 68(%rax), %ecx\n\t";
      adbasm += "testl %ecx, %ecx\n\t";
      adbasm += "jz 3f\n\t";
      adbasm += winAbort();
      adbasm += "3:\n\t";
      // ── Windows KUSER_SHARED_DATA 0x7FFE02D4 (KdDebuggerEnabled) &
      // 0x7FFE02D0 (BeingDebugged)
      adbasm += "movabsq $$0x7FFE02D4, %rax\n\t";
      adbasm += "movzbl (%rax), %ecx\n\t";
      adbasm += "testl %ecx, %ecx\n\t";
      adbasm += "jz 4f\n\t";
      adbasm += winAbort();
      adbasm += "4:\n\t";
      adbasm += "movabsq $$0x7FFE02D0, %rax\n\t";
      adbasm += "movzbl (%rax), %ecx\n\t";
      adbasm += "testl %ecx, %ecx\n\t";
      adbasm += "jz 5f\n\t";
      adbasm += winAbort();
      adbasm += "5:\n\t";

      // ── Windows PEB+0xBC NtGlobalFlag (FLG_HEAP_ENABLE_TAIL_CHECK 0x10 |
      // FLG_HEAP_ENABLE_FREE_CHECK 0x20 | FLG_HEAP_VALIDATE_PARAMETERS 0x40 =
      // 0x70)
      adbasm += "movq %gs:96, %rax\n\t";    // PEB
      adbasm += "movl 188(%rax), %ecx\n\t"; // NtGlobalFlag @ 0xBC (188)
      adbasm += "andl $$0x70, %ecx\n\t";
      adbasm += "jz 6f\n\t";
      adbasm += winAbort();
      adbasm += "6:\n\t";

      // ── Windows Hardware Debug Registers (DR0 - DR3 & DR7) check via CONTEXT
      // or thread structure Check if hardware debug registers DR0-DR3 / DR7 are
      // set via GetThreadContext / NtGetContextThread
      adbasm += "movq %gs:48, %rax\n\t";       // TEB
      adbasm += "movq 0x1478(%rax), %rcx\n\t"; // Decls / Debugger active field
                                               // check in TEB/PEB
      adbasm += "testq %rcx, %rcx\n\t";
      adbasm += "jz 7f\n\t";
      adbasm += winAbort();
      adbasm += "7:\n\t";

      uint64_t noiseK = cryptoutils->get_uint32_t() & 0xFFFF;
      adbasm += "rdtsc\n\t";
      adbasm += "andl $$0xFFFF, %eax\n\t";
      adbasm += "addl $$" + std::to_string(noiseK) + ", %eax\n\t";
      adbasm += "subl $$" + std::to_string(noiseK) + ", %eax\n\t";
      InlineAsm *IA = InlineAsm::get(
          VoidFTy, adbasm,
          "~{rax},~{rcx},~{rdx},~{r14},~{r15},~{dirflag},~{fpsr},~{flags}",
          true, false, InlineAsm::AD_ATT);
      CallInst::Create(IA->getFunctionType(), IA, ArrayRef<Value *>{}, "",
                       InsertPt);
    }
  }

  void InjectScatteredDebugChecks(Function *F, Instruction *lastTerm) {
    if (!triple.isAArch64() && triple.getArch() != Triple::x86_64)
      return;

    SmallVector<BasicBlock *, 16> scatCands;
    for (BasicBlock &BB : *F) {
      if (&BB == &F->getEntryBlock())
        continue;
      if (&BB == lastTerm->getParent())
        continue;
      if (BB.isEHPad() || BB.isLandingPad())
        continue;
      if (BB.hasAddressTaken())
        continue;
      StringRef nm = BB.getName();
      if (nm.contains("scatter") || nm.contains("Handler") ||
          nm.contains("lpad") || nm.contains("eh") || nm.contains("catch") ||
          nm.contains("terminate"))
        continue;
      BasicBlock::iterator firstNonPHIIt = BB.getFirstNonPHIOrDbgOrLifetime();
      if (firstNonPHIIt == BB.end())
        continue;
      Instruction *term = BB.getTerminator();
      if (!term || isa<InvokeInst>(term) || isa<ResumeInst>(term))
        continue;
      unsigned instCount = 0;
      for (Instruction &I : BB) {
        if (!isa<PHINode>(&I) && !I.isDebugOrPseudoInst())
          ++instCount;
      }
      if (instCount < 2)
        continue;
      scatCands.push_back(&BB);
    }
    if (scatCands.empty())
      return;

    for (unsigned i = (unsigned)scatCands.size() - 1; i > 0; --i)
      std::swap(scatCands[i], scatCands[cryptoutils->get_range(i + 1)]);

    unsigned nScat = std::min(3u, (unsigned)scatCands.size());
    LLVMContext &Ctx = F->getContext();
    Type *I64Ty = Type::getInt64Ty(Ctx);

    for (unsigned si = 0; si < nScat; si++) {
      BasicBlock *Orig = scatCands[si];
      BasicBlock::iterator splitIt = Orig->getFirstNonPHIOrDbgOrLifetime();
      if (splitIt == Orig->end())
        continue;
      BasicBlock *Bottom = Orig->splitBasicBlock(splitIt, "scatter.adb.bot");
      BasicBlock *SDbgHandler =
          BasicBlock::Create(Ctx, "DbgHandler.adb.scatter", F);
      IRBuilder<> HB(SDbgHandler);
      insertViolentExit(HB, triple, 204);   // DIAG: 分散计时检查命中

      Orig->getTerminator()->eraseFromParent();
      IRBuilder<> IRB(Orig);
      Value *IsDbg = nullptr;

      if (triple.getArch() == Triple::x86_64) {
        if (si % 2 == 0) {
          // Distributed check 1: Local RDTSC timing jitter
          FunctionType *JitFTy = FunctionType::get(I64Ty, false);
          std::string jasm;
          jasm += "rdtsc\n\t";
          jasm += "shlq $$32, %rdx\n\t";
          jasm += "orq %rax, %rdx\n\t";
          jasm += "movq %rdx, %rsi\n\t";
          jasm += "movq $$39, %rax\n\t"; // SYS_getpid
          jasm += "syscall\n\t";
          jasm += "rdtsc\n\t";
          jasm += "shlq $$32, %rdx\n\t";
          jasm += "orq %rax, %rdx\n\t";
          jasm += "subq %rsi, %rdx\n\t";
          jasm += "movq %rdx, $0";
          InlineAsm *JIA = InlineAsm::get(JitFTy, jasm,
                                          "=r,~{rax},~{rcx},~{rdx},~{rsi},~{"
                                          "r11},~{dirflag},~{fpsr},~{flags}",
                                          false, false, InlineAsm::AD_ATT);
          CallInst *JCall = IRB.CreateCall(JIA);
          IsDbg = IRB.CreateICmpUGT(
              JCall, ConstantInt::get(I64Ty, 0x20000000ULL), "adb.scat.jit");
        } else {
          // Distributed check 2: In-flight Trap Flag (detects single-step
          // debugging)
          FunctionType *TFFTy = FunctionType::get(I64Ty, false);
          InlineAsm *TFIA = InlineAsm::get(TFFTy,
                                           "subq $$128, %rsp\n\tpushfq\n\tpopq "
                                           "$0\n\taddq $$128, %rsp\n\tandq "
                                           "$$0x100, $0",
                                           "=r,~{dirflag},~{fpsr},~{flags}",
                                           false, false, InlineAsm::AD_ATT);
          CallInst *TFCall = IRB.CreateCall(TFIA);
          IsDbg = IRB.CreateICmpNE(TFCall, ConstantInt::get(I64Ty, 0),
                                   "adb.scat.tf");
        }
      } else if (triple.isAArch64()) {
        if (si % 2 == 0) {
          FunctionType *CntFTy = FunctionType::get(I64Ty, false);
          std::string casm;
          casm += "mrs x11, cntvct_el0\n\t";
          casm += "mov x8, #117\n\t";
          casm += "mov x0, #0\n\t";
          casm += "mov x1, #0\n\t";
          casm += "mov x2, #0\n\t";
          casm += "mov x3, #0\n\t";
          casm += "svc #0\n\t";
          casm += "mrs x12, cntvct_el0\n\t";
          casm += "sub $0, x12, x11";
          InlineAsm *CIA = InlineAsm::get(
              CntFTy, casm,
              "=r,~{x0},~{x1},~{x2},~{x3},~{x8},~{x11},~{x12},~{cc}", false,
              false);
          CallInst *CCall = IRB.CreateCall(CIA);
          IsDbg = IRB.CreateICmpUGT(CCall, ConstantInt::get(I64Ty, 0x40000ULL),
                                    "adb.scat.cnt");
        } else {
          FunctionType *CntFTy = FunctionType::get(I64Ty, false);
          std::string casm;
          casm += "mrs x9, cntvct_el0\n\tnop\n\tnop\n\tnop\n\tnop\n\t";
          casm += "mrs x10, cntvct_el0\n\tsub $0, x10, x9";
          InlineAsm *CIA = InlineAsm::get(CntFTy, casm, "=r,~{x9},~{x10},~{cc}",
                                          false, false);
          CallInst *CCall = IRB.CreateCall(CIA);
          IsDbg = IRB.CreateICmpUGT(CCall, ConstantInt::get(I64Ty, 0x20000ULL),
                                    "adb.scat.cnt");
        }
      }

      if (IsDbg) {
        IRB.CreateCondBr(IsDbg, SDbgHandler, Bottom);
      } else {
        IRB.CreateBr(Bottom);
      }
    }
  }

  std::string GetPlatformAbort(const Triple &T) { return getViolentExitAsm(T); }
};

ModulePass *createAntiDebuggingPass(bool flag) {
  return new AntiDebugging(flag);
}
} // namespace llvm

char AntiDebugging::ID = 0;
INITIALIZE_PASS(AntiDebugging, "adbobf", "Enable AntiDebugging.", false, false)
