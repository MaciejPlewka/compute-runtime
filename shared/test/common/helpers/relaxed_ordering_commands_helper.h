/*
 * Copyright (C) 2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/command_container/command_encoder.h"

namespace NEO {
namespace RelaxedOrderingCommandsHelper {
template <typename FamilyType>
bool verifyMiPredicate(void *miPredicateCmd, MiPredicateType predicateType);

template <typename FamilyType>
bool verifyAlu(typename FamilyType::MI_MATH_ALU_INST_INLINE *miAluCmd, AluRegisters opcode, AluRegisters operand1, AluRegisters operand2);

template <typename FamilyType>
bool verifyLri(typename FamilyType::MI_LOAD_REGISTER_IMM *lriCmd, uint32_t registerOffset, uint32_t data);

template <typename FamilyType>
bool verifyLrr(typename FamilyType::MI_LOAD_REGISTER_REG *lrrCmd, uint32_t dstOffset, uint32_t srcOffset);

template <typename FamilyType>
bool verifyIncrementOrDecrement(void *cmds, AluRegisters aluRegister, bool increment);

template <typename FamilyType>
bool verifyConditionalDataRegBbStart(void *cmd, uint64_t startAddress, uint32_t compareReg, uint32_t compareData, CompareOperation compareOperation, bool indirect);

template <typename FamilyType>
bool verifyConditionalDataMemBbStart(void *cmd, uint64_t startAddress, uint64_t compareAddress, uint32_t compareData, CompareOperation compareOperation, bool indirect);

template <typename FamilyType>
bool verifyConditionalRegRegBbStart(void *cmd, uint64_t startAddress, AluRegisters compareReg0, AluRegisters compareReg1, CompareOperation compareOperation, bool indirect);

template <typename FamilyType>
bool verifyBaseConditionalBbStart(void *cmd, CompareOperation compareOperation, uint64_t startAddress, bool indirect, AluRegisters regA, AluRegisters regB);

template <typename FamilyType>
bool verifyBbStart(typename FamilyType::MI_BATCH_BUFFER_START *cmd, uint64_t startAddress, bool indirect, bool predicate);

template <typename FamilyType>
bool verifyMiPredicate(void *miPredicateCmd, MiPredicateType predicateType) {
    if constexpr (FamilyType::isUsingMiSetPredicate) {
        using MI_SET_PREDICATE = typename FamilyType::MI_SET_PREDICATE;
        using PREDICATE_ENABLE = typename MI_SET_PREDICATE::PREDICATE_ENABLE;

        auto miSetPredicate = reinterpret_cast<MI_SET_PREDICATE *>(miPredicateCmd);
        if (static_cast<PREDICATE_ENABLE>(predicateType) != miSetPredicate->getPredicateEnable()) {
            return false;
        }

        return true;
    }
    return false;
}

template <typename FamilyType>
bool verifyLri(typename FamilyType::MI_LOAD_REGISTER_IMM *lriCmd, uint32_t registerOffset, uint32_t data) {
    if ((lriCmd->getRegisterOffset() != registerOffset) || (lriCmd->getDataDword() != data)) {
        return false;
    }

    return true;
}

template <typename FamilyType>
bool verifyLrr(typename FamilyType::MI_LOAD_REGISTER_REG *lrrCmd, uint32_t dstOffset, uint32_t srcOffset) {
    if ((dstOffset != lrrCmd->getDestinationRegisterAddress()) || (srcOffset != lrrCmd->getSourceRegisterAddress())) {
        return false;
    }
    return true;
}

template <typename FamilyType>
bool verifyIncrementOrDecrement(void *cmds, AluRegisters aluRegister, bool increment) {
    using MI_LOAD_REGISTER_IMM = typename FamilyType::MI_LOAD_REGISTER_IMM;
    using MI_MATH_ALU_INST_INLINE = typename FamilyType::MI_MATH_ALU_INST_INLINE;
    using MI_MATH = typename FamilyType::MI_MATH;

    auto lriCmd = reinterpret_cast<MI_LOAD_REGISTER_IMM *>(cmds);
    if (!verifyLri<FamilyType>(lriCmd, CS_GPR_R7, 1)) {
        return false;
    }

    lriCmd++;
    if (!verifyLri<FamilyType>(lriCmd, CS_GPR_R7 + 4, 0)) {
        return false;
    }

    auto miMathCmd = reinterpret_cast<MI_MATH *>(++lriCmd);
    if (miMathCmd->DW0.BitField.DwordLength != 3) {
        return false;
    }

    auto miAluCmd = reinterpret_cast<MI_MATH_ALU_INST_INLINE *>(++miMathCmd);
    if (!verifyAlu<FamilyType>(miAluCmd, AluRegisters::OPCODE_LOAD, AluRegisters::R_SRCA, aluRegister)) {
        return false;
    }

    miAluCmd++;
    if (!verifyAlu<FamilyType>(miAluCmd, AluRegisters::OPCODE_LOAD, AluRegisters::R_SRCB, AluRegisters::R_7)) {
        return false;
    }

    miAluCmd++;

    if (increment && !verifyAlu<FamilyType>(miAluCmd, AluRegisters::OPCODE_ADD, AluRegisters::OPCODE_NONE, AluRegisters::OPCODE_NONE)) {
        return false;
    }

    if (!increment && !verifyAlu<FamilyType>(miAluCmd, AluRegisters::OPCODE_SUB, AluRegisters::OPCODE_NONE, AluRegisters::OPCODE_NONE)) {
        return false;
    }

    miAluCmd++;
    if (!verifyAlu<FamilyType>(miAluCmd, AluRegisters::OPCODE_STORE, aluRegister, AluRegisters::R_ACCU)) {
        return false;
    }

    return true;
}

template <typename FamilyType>
bool verifyAlu(typename FamilyType::MI_MATH_ALU_INST_INLINE *miAluCmd, AluRegisters opcode, AluRegisters operand1, AluRegisters operand2) {
    if ((static_cast<uint32_t>(opcode) != miAluCmd->DW0.BitField.ALUOpcode) ||
        (static_cast<uint32_t>(operand1) != miAluCmd->DW0.BitField.Operand1) ||
        (static_cast<uint32_t>(operand2) != miAluCmd->DW0.BitField.Operand2)) {
        return false;
    }

    return true;
}

template <typename FamilyType>
bool verifyBbStart(typename FamilyType::MI_BATCH_BUFFER_START *bbStartCmd, uint64_t startAddress, bool indirect, bool predicate) {
    if constexpr (FamilyType::isUsingMiSetPredicate) {
        if ((predicate != !!bbStartCmd->getPredicationEnable()) ||
            (indirect != !!bbStartCmd->getIndirectAddressEnable())) {
            return false;
        }
    }

    if (!indirect && startAddress != bbStartCmd->getBatchBufferStartAddress()) {
        return false;
    }

    return true;
}

template <typename FamilyType>
bool verifyBaseConditionalBbStart(void *cmd, CompareOperation compareOperation, uint64_t startAddress, bool indirect, AluRegisters regA, AluRegisters regB) {
    using MI_BATCH_BUFFER_START = typename FamilyType::MI_BATCH_BUFFER_START;
    using MI_SET_PREDICATE = typename FamilyType::MI_SET_PREDICATE;
    using MI_LOAD_REGISTER_REG = typename FamilyType::MI_LOAD_REGISTER_REG;
    using MI_MATH_ALU_INST_INLINE = typename FamilyType::MI_MATH_ALU_INST_INLINE;
    using MI_MATH = typename FamilyType::MI_MATH;

    auto miMathCmd = reinterpret_cast<MI_MATH *>(cmd);
    if (miMathCmd->DW0.BitField.DwordLength != 3) {
        return false;
    }

    auto miAluCmd = reinterpret_cast<MI_MATH_ALU_INST_INLINE *>(++miMathCmd);
    if (!verifyAlu<FamilyType>(miAluCmd, AluRegisters::OPCODE_LOAD, AluRegisters::R_SRCA, regA)) {
        return false;
    }

    miAluCmd++;
    if (!verifyAlu<FamilyType>(miAluCmd, AluRegisters::OPCODE_LOAD, AluRegisters::R_SRCB, regB)) {
        return false;
    }

    miAluCmd++;
    if (!verifyAlu<FamilyType>(miAluCmd, AluRegisters::OPCODE_SUB, AluRegisters::OPCODE_NONE, AluRegisters::OPCODE_NONE)) {
        return false;
    }

    miAluCmd++;

    if (compareOperation == CompareOperation::Equal || compareOperation == CompareOperation::NotEqual) {
        if (!verifyAlu<FamilyType>(miAluCmd, AluRegisters::OPCODE_STORE, AluRegisters::R_7, AluRegisters::R_ZF)) {
            return false;
        }
    } else {
        if (!verifyAlu<FamilyType>(miAluCmd, AluRegisters::OPCODE_STORE, AluRegisters::R_7, AluRegisters::R_CF)) {
            return false;
        }
    }

    auto lrrCmd = reinterpret_cast<MI_LOAD_REGISTER_REG *>(++miAluCmd);
    if (!verifyLrr<FamilyType>(lrrCmd, CS_PREDICATE_RESULT_2, CS_GPR_R7)) {
        return false;
    }

    auto predicateCmd = reinterpret_cast<MI_SET_PREDICATE *>(++lrrCmd);
    if (compareOperation == CompareOperation::Equal) {
        if (!verifyMiPredicate<FamilyType>(predicateCmd, MiPredicateType::NoopOnResult2Clear)) {
            return false;
        }
    } else {
        if (!verifyMiPredicate<FamilyType>(predicateCmd, MiPredicateType::NoopOnResult2Set)) {
            return false;
        }
    }

    auto bbStartCmd = reinterpret_cast<MI_BATCH_BUFFER_START *>(++predicateCmd);
    if (!verifyBbStart<FamilyType>(bbStartCmd, startAddress, indirect, true)) {
        return false;
    }

    predicateCmd = reinterpret_cast<MI_SET_PREDICATE *>(++bbStartCmd);
    if (!verifyMiPredicate<FamilyType>(predicateCmd, MiPredicateType::Disable)) {
        return false;
    }

    return true;
}

template <typename FamilyType>
bool verifyConditionalRegRegBbStart(void *cmd, uint64_t startAddress, AluRegisters compareReg0, AluRegisters compareReg1, CompareOperation compareOperation, bool indirect) {
    return verifyBaseConditionalBbStart<FamilyType>(cmd, compareOperation, startAddress, indirect, compareReg0, compareReg1);
}

template <typename FamilyType>
bool verifyConditionalDataMemBbStart(void *cmd, uint64_t startAddress, uint64_t compareAddress, uint32_t compareData, CompareOperation compareOperation, bool indirect) {
    using MI_LOAD_REGISTER_MEM = typename FamilyType::MI_LOAD_REGISTER_MEM;
    using MI_LOAD_REGISTER_IMM = typename FamilyType::MI_LOAD_REGISTER_IMM;

    auto lrmCmd = reinterpret_cast<MI_LOAD_REGISTER_MEM *>(cmd);
    if ((lrmCmd->getRegisterAddress() != CS_GPR_R7) || (lrmCmd->getMemoryAddress() != compareAddress)) {
        return false;
    }

    auto lriCmd = reinterpret_cast<MI_LOAD_REGISTER_IMM *>(++lrmCmd);
    if (!verifyLri<FamilyType>(lriCmd, CS_GPR_R7 + 4, 0)) {
        return false;
    }

    if (!verifyLri<FamilyType>(++lriCmd, CS_GPR_R8, compareData)) {
        return false;
    }

    if (!verifyLri<FamilyType>(++lriCmd, CS_GPR_R8 + 4, 0)) {
        return false;
    }

    return verifyBaseConditionalBbStart<FamilyType>(++lriCmd, compareOperation, startAddress, indirect, AluRegisters::R_7, AluRegisters::R_8);
}

template <typename FamilyType>
bool verifyConditionalDataRegBbStart(void *cmds, uint64_t startAddress, uint32_t compareReg, uint32_t compareData,
                                     CompareOperation compareOperation, bool indirect) {
    using MI_LOAD_REGISTER_REG = typename FamilyType::MI_LOAD_REGISTER_REG;
    using MI_LOAD_REGISTER_IMM = typename FamilyType::MI_LOAD_REGISTER_IMM;

    auto lrrCmd = reinterpret_cast<MI_LOAD_REGISTER_REG *>(cmds);
    if (!verifyLrr<FamilyType>(lrrCmd, CS_GPR_R7, compareReg)) {
        return false;
    }

    auto lriCmd = reinterpret_cast<MI_LOAD_REGISTER_IMM *>(++lrrCmd);
    if (!verifyLri<FamilyType>(lriCmd, CS_GPR_R7 + 4, 0)) {
        return false;
    }

    lriCmd++;
    if (!verifyLri<FamilyType>(lriCmd, CS_GPR_R8, compareData)) {
        return false;
    }

    lriCmd++;
    if (!verifyLri<FamilyType>(lriCmd, CS_GPR_R8 + 4, 0)) {
        return false;
    }

    return verifyBaseConditionalBbStart<FamilyType>(++lriCmd, compareOperation, startAddress, indirect, AluRegisters::R_7, AluRegisters::R_8);
}

} // namespace RelaxedOrderingCommandsHelper
} // namespace NEO
