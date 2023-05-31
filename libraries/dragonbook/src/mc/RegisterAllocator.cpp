
#include "RegisterAllocator.h"

void RegisterAllocator::run(IRContext* context, MachineFunction* func)
{
	RegisterAllocator ra(context, func);
	ra.run();
}

void RegisterAllocator::run()
{
	createRegisterInfo();
	runLiveAnalysis();

#ifdef WIN32
	setupArgsWin64();
#else
	setupArgsUnix64();
#endif

	allocStackVars();

	for (size_t i = 0; i < func->basicBlocks.size(); i++)
	{
		MachineBasicBlock* bb = func->basicBlocks[i];
		for (MachineInst* inst : bb->code)
		{
			for (MachineOperand& operand : inst->operands)
			{
				if (operand.type == MachineOperandType::reg)
				{
					useRegister(operand);
				}
			}

			//std::string comment;
			for (MachineOperand& operand : inst->operands)
			{
				if (operand.type == MachineOperandType::reg)
				{
					int pregIndex = operand.registerIndex;
					int vregIndex = reginfo[pregIndex].vreg;
					if (vregIndex != -1)
					{
						// If vreg is only used by one BB we can safely kill it once we get to the last inst using it
						auto& liveref = reginfo[vregIndex].liveReferences;
						if (liveref && liveref->bb == bb && !liveref->next)
						{
							liveref->refcount--;
							if (liveref->refcount == 0)
							{
								killVirtRegister(vregIndex);

								/*if (comment.empty())
									comment = "kill vreg ";
								else
									comment += ", ";
								comment += getVRegName(vregIndex);*/
							}
						}
					}
				}
			}
			//if (!comment.empty())
			//	inst->comment = comment;

			if ((inst->opcode >= MachineInstOpcode::idiv64 && inst->opcode <= MachineInstOpcode::idiv8) ||
				(inst->opcode >= MachineInstOpcode::div64 && inst->opcode <= MachineInstOpcode::div8))
			{
				usedRegs.insert(RegisterName::rax);
				usedRegs.insert(RegisterName::rdx);
			}

			updateModifiedStatus(inst);

			if (inst->opcode == MachineInstOpcode::call)
			{
				for (RegisterName regName : volatileRegs)
				{
					int pregIndex = (int)regName;
					if (reginfo[pregIndex].vreg != -1)
					{
						assignVirt2StackSlot(reginfo[pregIndex].vreg);
						setAsLeastRecentlyUsed(pregIndex);
					}
				}

				// To avoid mov rax,rax or mov xmm0,xmm0
				setAsMostRecentlyUsed((int)RegisterName::rax);
				setAsMostRecentlyUsed((int)RegisterName::xmm0);
			}

			if (inst->opcode == MachineInstOpcode::je || inst->opcode == MachineInstOpcode::jne)
			{
				assignAllToStack();
			}
			else if (inst->opcode == MachineInstOpcode::jmp)
			{
				if (inst->operands[0].bb == func->epilog)
				{
					setAllToStack();
				}
				else
				{
					assignAllToStack();
				}
			}

			if (inst->opcode == MachineInstOpcode::jmp && i + 1 < func->basicBlocks.size() && func->basicBlocks[i + 1] == inst->operands[0].bb)
				continue;

			emittedInstructions.push_back(inst);
		}

		bb->code.swap(emittedInstructions);
		emittedInstructions.clear();
	}

	for (RegisterName reg : volatileRegs)
		usedRegs.erase(reg);

	usedRegs.erase(RegisterName::rsp);
	if (func->dynamicStackAllocations)
		usedRegs.insert(RegisterName::rbp);

	std::vector<RegisterName> savedRegs, savedXmmRegs;
	for (RegisterName reg : usedRegs)
	{
		if (reg < RegisterName::xmm0)
		{
			savedRegs.push_back(reg);
		}
		else
		{
			savedXmmRegs.push_back(reg);
		}
	}

#if 0 // Force save all non-volatile registers (for debugging)
	savedRegs =
	{
		RegisterName::rbx,
		RegisterName::rsp, RegisterName::rbp, RegisterName::rsi, RegisterName::rdi,
		RegisterName::r12, RegisterName::r13, RegisterName::r14, RegisterName::r15
	};

	savedXmmRegs =
	{
		RegisterName::xmm8, RegisterName::xmm9, RegisterName::xmm10, RegisterName::xmm11,
		RegisterName::xmm12, RegisterName::xmm13, RegisterName::xmm14, RegisterName::xmm15
	};
#endif

	// [funcargs] [retaddr] <framebase> [save/spill area] <spillbase> [dsa/alloca area] [callargs] <rsp>

	int callReturnAddr = 8;
	int pushStackAdjustment = (int)savedRegs.size() * 8;
	int spillAreaSize = (int)savedXmmRegs.size() * 16 + nextSpillOffset;
	int callargsSize = (func->maxCallArgsSize + 15) / 16 * 16;

	if ((savedRegs.size() & 1) == 0) // xmm must be 16-byte aligned
		spillAreaSize += 8;

	// rsp is 16-byte aligned + 8 bytes (for the call return address) when entering the prolog.
	// Alloca needs to be 16-byte aligned, so the sum of the return address, the pushed registers and spill area needs to end at a 16-byte boundary
	int allocaStart = (callReturnAddr + pushStackAdjustment + spillAreaSize + 15) / 16 * 16;
	spillAreaSize = allocaStart - pushStackAdjustment - callReturnAddr;

	int frameSize = pushStackAdjustment + spillAreaSize + callargsSize;
	int frameStackAdjustment = frameSize - pushStackAdjustment;

	func->frameBaseOffset = frameSize;
	func->spillBaseOffset = callargsSize;

	emitProlog(savedRegs, savedXmmRegs, frameStackAdjustment, func->dynamicStackAllocations);
	emitEpilog(savedRegs, savedXmmRegs, frameStackAdjustment, func->dynamicStackAllocations);
}

void RegisterAllocator::updateModifiedStatus(MachineInst* inst)
{
	if (inst->operands.empty() || inst->operands[0].type != MachineOperandType::reg) return;

	MachineInstOpcode opcode = inst->opcode;
	if (opcode == MachineInstOpcode::nop ||
		(opcode >= MachineInstOpcode::storess && opcode <= MachineInstOpcode::store8) ||
		(opcode >= MachineInstOpcode::idiv64 && opcode <= MachineInstOpcode::idiv8) ||
		(opcode >= MachineInstOpcode::div64 && opcode <= MachineInstOpcode::div8) ||
		(opcode >= MachineInstOpcode::ucomisd && opcode <= MachineInstOpcode::cmp8) ||
		(opcode >= MachineInstOpcode::jmp && opcode <= MachineInstOpcode::pop))
	{
		return;
	}

	int pregIndex = inst->operands[0].registerIndex;
	int vregIndex = reginfo[pregIndex].vreg;
	if (vregIndex != -1)
		reginfo[vregIndex].modified = true;
}

void RegisterAllocator::emitProlog(const std::vector<RegisterName>& savedRegs, const std::vector<RegisterName>& savedXmmRegs, int stackAdjustment, bool dsa)
{
	for (RegisterName physReg : savedRegs)
	{
		MachineOperand src;
		src.type = MachineOperandType::reg;
		src.registerIndex = (int)physReg;

		auto inst = context->newMachineInst();
		inst->opcode = MachineInstOpcode::push;
		inst->operands.push_back(src);
		inst->unwindHint = MachineUnwindHint::PushNonvolatile;
		func->prolog->code.push_back(inst);
	}

	// sub rsp, stacksize
	if (stackAdjustment > 0)
	{
		MachineOperand dst;
		dst.type = MachineOperandType::reg;
		dst.registerIndex = (int)RegisterName::rsp;

		MachineOperand src;
		src.type = MachineOperandType::imm;
		src.immvalue = stackAdjustment;

		auto inst = context->newMachineInst();
		inst->opcode = MachineInstOpcode::sub64;
		inst->operands.push_back(dst);
		inst->operands.push_back(src);
		inst->unwindHint = MachineUnwindHint::StackAdjustment;
		func->prolog->code.push_back(inst);
	}

	int xmmStartOffset = -(int)savedRegs.size() * 8;
	if ((savedRegs.size() & 1) == 0) // xmm must be 16-byte aligned
		xmmStartOffset -= 8;

	int i = 1;
	for (RegisterName physReg : savedXmmRegs)
	{
		MachineOperand dst;
		dst.type = MachineOperandType::frameOffset;
		dst.frameOffset = xmmStartOffset - i * 16;

		MachineOperand src;
		src.type = MachineOperandType::reg;
		src.registerIndex = (int)physReg;

		auto inst = context->newMachineInst();
		inst->opcode = MachineInstOpcode::movdqa;
		inst->operands.push_back(dst);
		inst->operands.push_back(src);
		inst->unwindHint = MachineUnwindHint::RegisterStackLocation;
		func->prolog->code.push_back(inst);
		i++;
	}

	if (dsa)
	{
		MachineOperand dst;
		dst.type = MachineOperandType::reg;
		dst.registerIndex = (int)RegisterName::rbp;

		MachineOperand src;
		src.type = MachineOperandType::reg;
		src.registerIndex = (int)RegisterName::rsp;

		auto inst = context->newMachineInst();
		inst->opcode = MachineInstOpcode::mov64;
		inst->operands.push_back(dst);
		inst->operands.push_back(src);
		inst->unwindHint = MachineUnwindHint::SaveFrameRegister;
		func->prolog->code.push_back(inst);
	}
}

void RegisterAllocator::emitEpilog(const std::vector<RegisterName>& savedRegs, const std::vector<RegisterName>& savedXmmRegs, int stackAdjustment, bool dsa)
{
	if (dsa)
	{
		MachineOperand dst;
		dst.type = MachineOperandType::reg;
		dst.registerIndex = (int)RegisterName::rsp;

		MachineOperand src;
		src.type = MachineOperandType::reg;
		src.registerIndex = (int)RegisterName::rbp;

		auto inst = context->newMachineInst();
		inst->opcode = MachineInstOpcode::mov64;
		inst->operands.push_back(dst);
		inst->operands.push_back(src);
		func->epilog->code.push_back(inst);
	}

	int xmmStartOffset = -(int)savedRegs.size() * 8;
	if ((savedRegs.size() & 1) == 0) // xmm must be 16-byte aligned
		xmmStartOffset -= 8;

	int i = 1;
	for (RegisterName physReg : savedXmmRegs)
	{
		MachineOperand dst;
		dst.type = MachineOperandType::reg;
		dst.registerIndex = (int)physReg;

		MachineOperand src;
		src.type = MachineOperandType::frameOffset;
		src.frameOffset = xmmStartOffset - i * 16;

		auto inst = context->newMachineInst();
		inst->opcode = MachineInstOpcode::movdqa;
		inst->operands.push_back(dst);
		inst->operands.push_back(src);
		func->epilog->code.push_back(inst);
		i++;
	}

	// add rsp, stacksize
	if (stackAdjustment > 0)
	{
		MachineOperand dst;
		dst.type = MachineOperandType::reg;
		dst.registerIndex = (int)RegisterName::rsp;

		MachineOperand src;
		src.type = MachineOperandType::imm;
		src.immvalue = stackAdjustment;

		auto inst = context->newMachineInst();
		inst->opcode = MachineInstOpcode::add64;
		inst->operands.push_back(dst);
		inst->operands.push_back(src);
		func->epilog->code.push_back(inst);
	}

	for (auto it = savedRegs.rbegin(); it != savedRegs.rend(); ++it)
	{
		RegisterName physReg = *it;

		MachineOperand dst;
		dst.type = MachineOperandType::reg;
		dst.registerIndex = (int)physReg;

		auto inst = context->newMachineInst();
		inst->opcode = MachineInstOpcode::pop;
		inst->operands.push_back(dst);
		func->epilog->code.push_back(inst);
	}

	{
		auto inst = context->newMachineInst();
		inst->opcode = MachineInstOpcode::ret;
		func->epilog->code.push_back(inst);
	}
}

void RegisterAllocator::createRegisterInfo()
{
	reginfo.resize(func->registers.size());
	for (size_t i = 0; i < func->registers.size(); i++)
	{
		reginfo[i].cls = func->registers[i].cls;
	}

	for (int i = 0; i < (int)RegisterName::vregstart; i++)
	{
		if (reginfo[i].cls != MachineRegClass::reserved)
		{
			RARegisterClass& cls = regclass[(int)reginfo[i].cls];
			cls.mruIt[i] = cls.mru.insert(cls.mru.end(), i);
		}
	}
}

void RegisterAllocator::allocStackVars()
{
	for (const MachineStackAlloc& stackvar : func->stackvars)
	{
		auto& vreg = reginfo[stackvar.registerIndex];
		vreg.stacklocation.spillOffset = nextSpillOffset;
		vreg.stackvar = true;
		vreg.name = &stackvar.name;
		nextSpillOffset += (int)stackvar.size;
	}
}

void RegisterAllocator::setAllToStack()
{
	for (RARegisterInfo &entry : reginfo)
	{
		if (entry.physreg != -1)
		{
			reginfo[entry.physreg].vreg = -1;
			entry.physreg = -1;
		}
	}
}

void RegisterAllocator::assignAllToStack()
{
	for (size_t i = (int)RegisterName::vregstart; i < reginfo.size(); i++)
	{
		int vregIndex = (int)i;
		assignVirt2StackSlot(vregIndex);
	}
}

void RegisterAllocator::useRegister(MachineOperand& operand)
{
	if (operand.registerIndex < (int)RegisterName::vregstart)
	{
		int pregIndex = operand.registerIndex;

		auto& physreg = reginfo[pregIndex];
		if (physreg.vreg != -1)
		{
			assignVirt2StackSlot(physreg.vreg);
		}

		setAsMostRecentlyUsed(pregIndex);
	}
	else
	{
		int vregIndex = operand.registerIndex;
		auto& vreg = reginfo[vregIndex];
		if (vreg.physreg == -1)
		{
			int pregIndex = getLeastRecentlyUsed(vreg.cls);
			assignVirt2Phys(vregIndex, pregIndex);
		}

		setAsMostRecentlyUsed(vreg.physreg);
		operand.registerIndex = vreg.physreg;
	}
}

void RegisterAllocator::killVirtRegister(int vregIndex)
{
	auto& vreg = reginfo[vregIndex];
	if (vreg.physreg != -1)
	{
		int pregIndex = vreg.physreg;
		auto& physreg = reginfo[pregIndex];

		physreg.vreg = -1;
		vreg.physreg = -1;

		setAsLeastRecentlyUsed(pregIndex);
	}

	if (vreg.stacklocation.type == MachineOperandType::spillOffset && vreg.stacklocation.spillOffset != -1)
		freeSpillOffsets.push_back(vreg.stacklocation.spillOffset);
}

void RegisterAllocator::assignVirt2Phys(int vregIndex, int pregIndex)
{
	auto& vreg = reginfo[vregIndex];
	auto& physreg = reginfo[pregIndex];

	if (physreg.vreg == vregIndex) // Already assigned to physreg
		return;

	if (physreg.vreg != -1) // Already in use. Make it available.
		assignVirt2StackSlot(physreg.vreg);

	usedRegs.insert((RegisterName)pregIndex);

	if (vreg.physreg == -1 && vreg.stacklocation.spillOffset == -1) // first time vreg is used
	{
		vreg.physreg = pregIndex;
		physreg.vreg = vregIndex;
	}
	else if (vreg.stackvar) // ptr to stack location
	{
		MachineOperand dest;
		dest.type = MachineOperandType::reg;
		dest.registerIndex = pregIndex;

		MachineOperand src = vreg.stacklocation;

		auto inst = context->newMachineInst();
		inst->opcode = MachineInstOpcode::lea;
		inst->operands.push_back(dest);
		inst->operands.push_back(src);
		inst->comment = "ptr " + getVRegName(vregIndex);
		emittedInstructions.push_back(inst);

		physreg.vreg = vregIndex;
		vreg.physreg = pregIndex;
	}
	else if (vreg.physreg == -1) // move from stack
	{
		MachineOperand dest;
		dest.type = MachineOperandType::reg;
		dest.registerIndex = pregIndex;

		MachineOperand src = vreg.stacklocation;

		auto inst = context->newMachineInst();
		inst->opcode = vreg.cls == MachineRegClass::gp ? MachineInstOpcode::load64 : MachineInstOpcode::loadsd;
		inst->operands.push_back(dest);
		inst->operands.push_back(src);
		inst->comment = "load " + getVRegName(vregIndex);
		emittedInstructions.push_back(inst);

		physreg.vreg = vregIndex;
		vreg.physreg = pregIndex;
	}
	else // move from other register
	{
		MachineOperand dest;
		dest.type = MachineOperandType::reg;
		dest.registerIndex = pregIndex;

		MachineOperand src;
		src.type = MachineOperandType::reg;
		src.registerIndex = vreg.physreg;

		auto inst = context->newMachineInst();
		inst->opcode = vreg.cls == MachineRegClass::gp ? MachineInstOpcode::mov64 : MachineInstOpcode::movsd;
		inst->operands.push_back(dest);
		inst->operands.push_back(src);
		inst->comment = "move " + getVRegName(vregIndex);
		emittedInstructions.push_back(inst);

		reginfo[vreg.physreg].vreg = -1;
		physreg.vreg = vregIndex;
		vreg.physreg = pregIndex;
	}
}

void RegisterAllocator::assignVirt2StackSlot(int vregIndex)
{
	auto& vreg = reginfo[vregIndex];
	if (vreg.physreg == -1)
		return;

	auto& physreg = reginfo[vreg.physreg];

	if (vreg.modified)
	{
		if (vreg.stacklocation.spillOffset == -1)
		{
			if (freeSpillOffsets.empty())
			{
				vreg.stacklocation.spillOffset = nextSpillOffset;
				nextSpillOffset += 8;
			}
			else
			{
				vreg.stacklocation.spillOffset = freeSpillOffsets.back();
				freeSpillOffsets.pop_back();
			}
		}

		// emit move from register to stack:

		MachineOperand dest = vreg.stacklocation;

		MachineOperand src;
		src.type = MachineOperandType::reg;
		src.registerIndex = vreg.physreg;

		auto inst = context->newMachineInst();
		inst->opcode = vreg.cls == MachineRegClass::gp ? MachineInstOpcode::store64 : MachineInstOpcode::storesd;
		inst->operands.push_back(dest);
		inst->operands.push_back(src);
		inst->comment = "save " + getVRegName(vregIndex);
		emittedInstructions.push_back(inst);

		vreg.modified = false;
	}

	physreg.vreg = -1;
	vreg.physreg = -1;
}

void RegisterAllocator::setAsMostRecentlyUsed(int pregIndex)
{
	if (reginfo[pregIndex].cls != MachineRegClass::reserved)
	{
		RARegisterClass& cls = regclass[(int)reginfo[pregIndex].cls];
		cls.mru.erase(cls.mruIt[pregIndex]);
		cls.mruIt[pregIndex] = cls.mru.insert(cls.mru.end(), pregIndex);
	}
}

void RegisterAllocator::setAsLeastRecentlyUsed(int pregIndex)
{
	if (reginfo[pregIndex].cls != MachineRegClass::reserved)
	{
		RARegisterClass& cls = regclass[(int)reginfo[pregIndex].cls];
		cls.mru.erase(cls.mruIt[pregIndex]);
		cls.mruIt[pregIndex] = cls.mru.insert(cls.mru.begin(), pregIndex);
	}
}

int RegisterAllocator::getLeastRecentlyUsed(MachineRegClass cls)
{
	return regclass[(int)cls].mru.front();
}

void RegisterAllocator::runLiveAnalysis()
{
	for (MachineBasicBlock* bb : func->basicBlocks)
	{
		for (MachineInst* inst : bb->code)
		{
			for (const MachineOperand& operand : inst->operands)
			{
				if (operand.type == MachineOperandType::reg)
				{
					addLiveReference(operand.registerIndex, bb);
				}
			}
		}
	}
}

void RegisterAllocator::addLiveReference(size_t vregIndex, MachineBasicBlock* bb)
{
	RARegisterInfo& vreg = reginfo[vregIndex];
	if (vreg.liveReferences)
	{
		RARegisterLiveReference* ref = vreg.liveReferences.get();
		while (ref->bb != bb && ref->next)
			ref = ref->next.get();

		if (ref->bb == bb)
		{
			ref->refcount++;
		}
		else
		{
			ref->next = std::make_unique<RARegisterLiveReference>(bb);
		}
	}
	else
	{
		vreg.liveReferences = std::make_unique<RARegisterLiveReference>(bb);
	}
}

void RegisterAllocator::setupArgsWin64()
{
	static const RegisterName registerArgs[] = { RegisterName::rcx, RegisterName::rdx, RegisterName::r8, RegisterName::r9 };
	const int numRegisterArgs = 4;

	MachineOperand stacklocation;
	stacklocation.type = MachineOperandType::frameOffset;
	stacklocation.frameOffset = 8;

	IRFunctionType* functype = dynamic_cast<IRFunctionType*>(func->type);
	for (int i = 0; i < (int)functype->args.size(); i++)
	{
		int vregindex = (int)RegisterName::vregstart + i;

		reginfo[vregindex].stacklocation = stacklocation;
		stacklocation.frameOffset += 8;

		if (i < numRegisterArgs)
		{
			int pregIndex;
			if (reginfo[vregindex].cls == MachineRegClass::gp)
				pregIndex = (int)registerArgs[i];
			else
				pregIndex = (int)RegisterName::xmm0 + (int)i;

			reginfo[vregindex].modified = true;
			reginfo[vregindex].physreg = pregIndex;
			reginfo[pregIndex].vreg = vregindex;

			setAsMostRecentlyUsed(pregIndex);
		}
	}

	volatileRegs =
	{
		RegisterName::rax, RegisterName::rcx, RegisterName::rdx, RegisterName::r8, RegisterName::r9, RegisterName::r10, RegisterName::r11,
		RegisterName::xmm0, RegisterName::xmm1, RegisterName::xmm2, RegisterName::xmm3, RegisterName::xmm4, RegisterName::xmm5
	};
}

void RegisterAllocator::setupArgsUnix64()
{
	static const RegisterName registerArgs[] = { RegisterName::rdi, RegisterName::rsi, RegisterName::rdx, RegisterName::rcx, RegisterName::r8, RegisterName::r9 };
	const int numRegisterArgs = 6;
	const int numXmmRegisterArgs = 8;
	int nextRegisterArg = 0;
	int nextXmmRegisterArg = 0;

	MachineOperand stacklocation;
	stacklocation.type = MachineOperandType::frameOffset;
	stacklocation.frameOffset = 8;

	IRFunctionType* functype = dynamic_cast<IRFunctionType*>(func->type);
	for (int i = 0; i < (int)functype->args.size(); i++)
	{
		int vregindex = (int)RegisterName::vregstart + i;

		if (reginfo[vregindex].cls == MachineRegClass::gp)
		{
			if (nextRegisterArg < numRegisterArgs)
			{
				int pregIndex = (int)registerArgs[nextRegisterArg++];
				reginfo[vregindex].physreg = pregIndex;
				reginfo[pregIndex].vreg = vregindex;
				setAsMostRecentlyUsed(pregIndex);
			}
			else
			{
				reginfo[vregindex].stacklocation = stacklocation;
				stacklocation.frameOffset += 8;
			}
		}
		else
		{
			if (nextXmmRegisterArg < numXmmRegisterArgs)
			{
				int pregIndex = (int)RegisterName::xmm0 + (int)(nextXmmRegisterArg++);
				reginfo[vregindex].physreg = pregIndex;
				reginfo[pregIndex].vreg = vregindex;
				setAsMostRecentlyUsed(pregIndex);
			}
			else
			{
				reginfo[vregindex].stacklocation = stacklocation;
				stacklocation.frameOffset += 8;
			}
		}
	}

	volatileRegs =
	{
		RegisterName::rax, RegisterName::rcx, RegisterName::rdx, RegisterName::rdi, RegisterName::rsi,
		RegisterName::xmm0, RegisterName::xmm1, RegisterName::xmm2, RegisterName::xmm3, RegisterName::xmm4, RegisterName::xmm5, RegisterName::xmm6, RegisterName::xmm7,
		RegisterName::xmm8, RegisterName::xmm9, RegisterName::xmm10, RegisterName::xmm11, RegisterName::xmm12, RegisterName::xmm13, RegisterName::xmm14, RegisterName::xmm15
	};
}

std::string RegisterAllocator::getVRegName(size_t vregIndex)
{
	if (reginfo[vregIndex].name)
		return *reginfo[vregIndex].name;
	else
		return "vreg" + std::to_string(vregIndex);
}
