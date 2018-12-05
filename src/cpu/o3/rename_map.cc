/*
 * Copyright (c) 2016 ARM Limited
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder. You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2004-2005 The Regents of The University of Michigan
 * Copyright (c) 2013 Advanced Micro Devices, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Kevin Lim
 */

#include "cpu/o3/rename_map.hh"

#include <vector>

#include "config/the_isa.hh"
#include "cpu/reg_class_impl.hh"
#include "debug/Rename.hh"

using namespace std;

/**** SimpleRenameMap methods ****/

SimpleRenameMap::SimpleRenameMap()
    : freeList(NULL), zeroReg(IntRegClass,0)
{
}


void
SimpleRenameMap::init(unsigned size, SimpleFreeList *_freeList,
                      RegIndex _zeroReg)
{
    assert(freeList == NULL);
    assert(map.empty());

    map.resize(size);
    freeList = _freeList;
    zeroReg = RegId(IntRegClass, _zeroReg);
}

SimpleRenameMap::RenameInfo
SimpleRenameMap::rename(const RegId& arch_reg)
{
    PhysRegIdPtr renamed_reg;
    // Record the current physical register that is renamed to the
    // requested architected register.
    PhysRegIdPtr prev_reg = map[arch_reg.index()];

    // If it's not referencing the zero register, then rename the
    // register.
    if (arch_reg != zeroReg) {
        renamed_reg = freeList->getReg();
		// 如果处理的目标不是0寄存器，则获得一个可用的物理寄存器作为
		// 重命名映射的目标物理寄存器
        map[arch_reg.index()] = renamed_reg;
		// 记录新映射的目标寄存器
    } else {
        // Otherwise return the zero register so nothing bad happens.
        /*
		if (curTick() > 133000 && arch_reg.classValue() == IntRegClass){
			printf("~~ Rename for Int Reg %d from %d!\n", arch_reg.index(), map[arch_reg.index()]->index());
		}
		*/
		assert(prev_reg->isZeroReg());
        renamed_reg = prev_reg;
		// 对于0寄存器，映射不会发生变化
    }

    DPRINTF(Rename, "Renamed reg %d to physical reg %d (%d) old mapping was"
            " %d (%d)\n",
            arch_reg, renamed_reg->index(), renamed_reg->flatIndex(),
            prev_reg->index(), prev_reg->flatIndex());

    return RenameInfo(renamed_reg, prev_reg);
}

void
SimpleRenameMap::dumpInsts()
{
	int index, endIdx = map.size();
	for(index = 0; index < endIdx; index++) {
		printf("  Arch reg %d -> Physical reg %d;\n", index, map[index]->index());
	}
	printf("  Zero reg %d\n", zeroReg.index());
}

/**** UnifiedRenameMap methods ****/

void
UnifiedRenameMap::init(PhysRegFile *_regFile,
                       RegIndex _intZeroReg,
                       RegIndex _floatZeroReg,
                       UnifiedFreeList *freeList,
                       VecMode _mode)
{
	//printf(">> Rename map Init: intZero %d; floatZero %d;\n", _intZeroReg, _floatZeroReg);
    regFile = _regFile;
    vecMode = _mode;

    intMap.init(TheISA::NumIntRegs, &(freeList->intList), _intZeroReg);

    floatMap.init(TheISA::NumFloatRegs, &(freeList->floatList), _floatZeroReg);

    vecMap.init(TheISA::NumVecRegs, &(freeList->vecList), (RegIndex)-1);

    vecElemMap.init(TheISA::NumVecRegs * NVecElems,
            &(freeList->vecElemList), (RegIndex)-1);

    ccMap.init(TheISA::NumCCRegs, &(freeList->ccList), (RegIndex)-1);

}

void
UnifiedRenameMap::dumpInsts()
{
	printf(">> Rename map for integer register:\n");
	intMap.dumpInsts();
	printf(">> Rename map for float register:\n");
	floatMap.dumpInsts();
}

void
UnifiedRenameMap::switchMode(VecMode newVecMode, UnifiedFreeList* freeList)
{
    if (newVecMode == Enums::Elem && vecMode == Enums::Full) {
        /* Switch to vector element rename mode. */
        /* The free list should currently be tracking full registers. */
		
		// 这里尝试从向量寄存器整体引用变换到向量寄存器按照元素引用的模式
        panic_if(freeList->hasFreeVecElems(),
                "The free list is already tracking Vec elems");
        panic_if(freeList->numFreeVecRegs() !=
                regFile->numVecPhysRegs() - TheISA::NumVecRegs,
                "The free list has lost vector registers");
		
		// 这里对freeList的正确性进行相应的检查
        /* Split the mapping of each arch reg. */
        int reg = 0;
        for (auto &e: vecMap) {
            PhysRegFile::IdRange range = this->regFile->getRegElemIds(e);
            uint32_t i;
            for (i = 0; range.first != range.second; i++, range.first++) {
                vecElemMap.setEntry(RegId(VecElemClass, reg, i),
                                    &(*range.first));
			// 将原本整个向量寄存器的映射拆分成单个向量元素的映射
            }
            panic_if(i != NVecElems,
                "Wrong name of elems: expecting %u, got %d\n",
                TheISA::NumVecElemPerVecReg, i);
            reg++;
        }
        /* Split the free regs. */
        while (freeList->hasFreeVecRegs()) {
            auto vr = freeList->getVecReg();
            auto range = this->regFile->getRegElemIds(vr);
            freeList->addRegs(range.first, range.second);
        }
		// 将拆分后的向量寄存器写入freeList，并将整体映射的寄存器实体删除
        vecMode = Enums::Elem;
    } else if (newVecMode == Enums::Full && vecMode == Enums::Elem) {
        /* Switch to full vector register rename mode. */
        /* The free list should currently be tracking register elems. */
		// 由向量寄存器元素索引模式切换到捆绑索引的模式
        panic_if(freeList->hasFreeVecRegs(),
                "The free list is already tracking full Vec");
        panic_if(freeList->numFreeVecRegs() !=
                regFile->numVecElemPhysRegs() - TheISA::NumFloatRegs,
                "The free list has lost vector register elements");
        /* To rebuild the arch regs we take the easy road:
         *  1.- Stitch the elems together into vectors.
         *  2.- Replace the contents of the register file with the vectors
         *  3.- Set the remaining registers as free
         */
        TheISA::VecRegContainer new_RF[TheISA::NumVecRegs];
        for (uint32_t i = 0; i < TheISA::NumVecRegs; i++) {
            VecReg dst = new_RF[i].as<TheISA::VecElem>();
            for (uint32_t l = 0; l < NVecElems; l++) {
                RegId s_rid(VecElemClass, i, l);
                PhysRegIdPtr s_prid = vecElemMap.lookup(s_rid);
                dst[l] = regFile->readVecElem(s_prid);
            }
        }
		// 将原本分散映射向量寄存器的元素中的数据提取到一个向量数组new_RF中

        for (uint32_t i = 0; i < TheISA::NumVecRegs; i++) {
            PhysRegId pregId(VecRegClass, i, 0);
            regFile->setVecReg(regFile->getTrueId(&pregId), new_RF[i]);
        }
		// 将上面生成的向量数组作为新的存储实体

        auto range = regFile->getRegIds(VecRegClass);
        freeList->addRegs(range.first + TheISA::NumVecRegs, range.second);

        /* We remove the elems from the free list. */
        while (freeList->hasFreeVecElems())
            freeList->getVecElem();
		// 将按照元素引用的寄存器从freeList中删除
        vecMode = Enums::Full;
    }
}

