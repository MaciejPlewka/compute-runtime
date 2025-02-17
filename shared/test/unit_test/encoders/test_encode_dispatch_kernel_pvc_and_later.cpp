/*
 * Copyright (C) 2021-2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/command_stream/stream_properties.h"
#include "shared/source/kernel/grf_config.h"
#include "shared/source/os_interface/product_helper.h"
#include "shared/test/common/cmd_parse/gen_cmd_parse.h"
#include "shared/test/common/helpers/debug_manager_state_restore.h"
#include "shared/test/common/mocks/mock_device.h"
#include "shared/test/common/mocks/mock_execution_environment.h"
#include "shared/test/common/test_macros/hw_test.h"
#include "shared/test/common/test_macros/test.h"
#include "shared/test/unit_test/encoders/test_encode_dispatch_kernel_dg2_and_later.h"
#include "shared/test/unit_test/fixtures/command_container_fixture.h"
#include "shared/test/unit_test/mocks/mock_dispatch_kernel_encoder_interface.h"

using namespace NEO;

using CommandEncodeStatesTestPvcAndLater = Test<CommandEncodeStatesFixture>;

HWTEST2_F(CommandEncodeStatesTestPvcAndLater, givenOverrideSlmTotalSizeDebugVariableWhenDispatchingKernelThenSharedMemorySizeIsSetCorrectly, IsAtLeastXeHpcCore) {
    using INTERFACE_DESCRIPTOR_DATA = typename FamilyType::INTERFACE_DESCRIPTOR_DATA;
    using WALKER_TYPE = typename FamilyType::WALKER_TYPE;
    DebugManagerStateRestore restorer;
    uint32_t dims[] = {2, 1, 1};
    std::unique_ptr<MockDispatchKernelEncoder> dispatchInterface(new MockDispatchKernelEncoder());
    uint32_t slmTotalSize = 0;

    dispatchInterface->getSlmTotalSizeResult = slmTotalSize;

    bool requiresUncachedMocs = false;

    int32_t maxValueToProgram = 0xC;

    for (int32_t valueToProgram = 0x0; valueToProgram < maxValueToProgram; valueToProgram++) {
        DebugManager.flags.OverrideSlmAllocationSize.set(valueToProgram);
        cmdContainer->reset();
        EncodeDispatchKernelArgs dispatchArgs = createDefaultDispatchKernelArgs(pDevice, dispatchInterface.get(), dims, requiresUncachedMocs);

        EncodeDispatchKernel<FamilyType>::encode(*cmdContainer.get(), dispatchArgs, nullptr);

        GenCmdList commands;
        CmdParse<FamilyType>::parseCommandBuffer(commands, ptrOffset(cmdContainer->getCommandStream()->getCpuBase(), 0), cmdContainer->getCommandStream()->getUsed());
        auto itor = find<WALKER_TYPE *>(commands.begin(), commands.end());
        ASSERT_NE(itor, commands.end());
        auto cmd = genCmdCast<WALKER_TYPE *>(*itor);
        auto &idd = cmd->getInterfaceDescriptor();

        EXPECT_EQ(valueToProgram, idd.getSharedLocalMemorySize());
    }
}

HWTEST2_F(CommandEncodeStatesTestPvcAndLater, givenVariousValuesWhenCallingSetBarrierEnableThenCorrectValuesAreSet, IsAtLeastXeHpcCore) {
    using INTERFACE_DESCRIPTOR_DATA = typename FamilyType::INTERFACE_DESCRIPTOR_DATA;
    using BARRIERS = typename INTERFACE_DESCRIPTOR_DATA::NUMBER_OF_BARRIERS;
    INTERFACE_DESCRIPTOR_DATA idd = FamilyType::cmdInitInterfaceDescriptorData;
    MockDevice device;
    auto hwInfo = device.getHardwareInfo();

    struct BarrierCountToBarrierNumEnum {
        uint32_t barrierCount;
        uint32_t numBarriersEncoding;
    };
    constexpr BarrierCountToBarrierNumEnum barriers[8] = {{0, 0},
                                                          {1, 1},
                                                          {2, 2},
                                                          {4, 3},
                                                          {8, 4},
                                                          {16, 5},
                                                          {24, 6},
                                                          {32, 7}};
    for (auto &[barrierCount, numBarriersEnum] : barriers) {
        EncodeDispatchKernel<FamilyType>::programBarrierEnable(idd, barrierCount, hwInfo);
        EXPECT_EQ(numBarriersEnum, idd.getNumberOfBarriers());
    }
}

HWCMDTEST_F(IGFX_XE_HP_CORE, CommandEncodeStatesTestPvcAndLater, givenCommandContainerWhenNumGrfRequiredIsGreaterThanDefaultThenLargeGrfModeEnabled) {
    using PIPELINE_SELECT = typename FamilyType::PIPELINE_SELECT;
    using STATE_COMPUTE_MODE = typename FamilyType::STATE_COMPUTE_MODE;
    MockExecutionEnvironment executionEnvironment{};
    auto &rootDeviceEnvironment = *executionEnvironment.rootDeviceEnvironments[0];
    auto &productHelper = rootDeviceEnvironment.getHelper<ProductHelper>();
    StreamProperties streamProperties{};
    streamProperties.initSupport(rootDeviceEnvironment);
    streamProperties.stateComputeMode.setPropertiesAll(false, GrfConfig::LargeGrfNumber, 0u, PreemptionMode::Disabled);
    EncodeComputeMode<FamilyType>::programComputeModeCommand(*cmdContainer->getCommandStream(), streamProperties.stateComputeMode, rootDeviceEnvironment, nullptr);
    GenCmdList commands;
    CmdParse<FamilyType>::parseCommandBuffer(commands, ptrOffset(cmdContainer->getCommandStream()->getCpuBase(), 0), cmdContainer->getCommandStream()->getUsed());

    auto itorCmd = find<STATE_COMPUTE_MODE *>(commands.begin(), commands.end());
    ASSERT_NE(itorCmd, commands.end());

    auto cmd = genCmdCast<STATE_COMPUTE_MODE *>(*itorCmd);
    EXPECT_EQ(productHelper.isGrfNumReportedWithScm(), cmd->getLargeGrfMode());
}

using CommandEncodeStatesTestHpc = Test<CommandEncodeStatesFixture>;
HWTEST2_F(CommandEncodeStatesTestHpc, GivenVariousSlmTotalSizesAndSettingRevIDToDifferentValuesWhenSetAdditionalInfoIsCalledThenCorrectValuesAreSet, IsPVC) {
    using PREFERRED_SLM_ALLOCATION_SIZE = typename FamilyType::INTERFACE_DESCRIPTOR_DATA::PREFERRED_SLM_ALLOCATION_SIZE;

    const std::vector<PreferredSlmTestValues<FamilyType>> valuesToTest = {
        {0, PREFERRED_SLM_ALLOCATION_SIZE::PREFERRED_SLM_ALLOCATION_SIZE_0K},
        {16 * KB, PREFERRED_SLM_ALLOCATION_SIZE::PREFERRED_SLM_ALLOCATION_SIZE_16K},
        {32 * KB, PREFERRED_SLM_ALLOCATION_SIZE::PREFERRED_SLM_ALLOCATION_SIZE_32K},
        {64 * KB, PREFERRED_SLM_ALLOCATION_SIZE::PREFERRED_SLM_ALLOCATION_SIZE_64K},
        {96 * KB, PREFERRED_SLM_ALLOCATION_SIZE::PREFERRED_SLM_ALLOCATION_SIZE_96K},
        {128 * KB, PREFERRED_SLM_ALLOCATION_SIZE::PREFERRED_SLM_ALLOCATION_SIZE_128K},
    };

    const std::vector<PreferredSlmTestValues<FamilyType>> valuesToTestForPvcAStep = {
        {0, PREFERRED_SLM_ALLOCATION_SIZE::PREFERRED_SLM_ALLOCATION_SIZE_16K},
        {16 * KB, PREFERRED_SLM_ALLOCATION_SIZE::PREFERRED_SLM_ALLOCATION_SIZE_16K},
        {32 * KB, PREFERRED_SLM_ALLOCATION_SIZE::PREFERRED_SLM_ALLOCATION_SIZE_32K},
        {64 * KB, PREFERRED_SLM_ALLOCATION_SIZE::PREFERRED_SLM_ALLOCATION_SIZE_64K},
        {96 * KB, PREFERRED_SLM_ALLOCATION_SIZE::PREFERRED_SLM_ALLOCATION_SIZE_96K},
        {128 * KB, PREFERRED_SLM_ALLOCATION_SIZE::PREFERRED_SLM_ALLOCATION_SIZE_128K},
    };

    const std::array<REVID, 5> revs{REVISION_A0, REVISION_B, REVISION_C, REVISION_D, REVISION_K};
    auto &hwInfo = *pDevice->getRootDeviceEnvironment().getMutableHardwareInfo();
    auto &productHelper = pDevice->getRootDeviceEnvironment().getProductHelper();

    for (auto rev : revs) {
        hwInfo.platform.usRevId = productHelper.getHwRevIdFromStepping(rev, hwInfo);
        if ((hwInfo.platform.eProductFamily == IGFX_PVC) && (rev == REVISION_A0)) {
            verifyPreferredSlmValues<FamilyType>(valuesToTestForPvcAStep, pDevice->getRootDeviceEnvironment());
        } else {
            verifyPreferredSlmValues<FamilyType>(valuesToTest, pDevice->getRootDeviceEnvironment());
        }
    }
}
