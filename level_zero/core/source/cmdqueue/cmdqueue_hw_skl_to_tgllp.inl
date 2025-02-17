/*
 * Copyright (C) 2020-2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "shared/source/command_container/cmdcontainer.h"
#include "shared/source/command_container/encode_surface_state.h"
#include "shared/source/command_stream/linear_stream.h"
#include "shared/source/device/device.h"
#include "shared/source/helpers/api_specific_config.h"
#include "shared/source/helpers/cache_policy.h"
#include "shared/source/helpers/gfx_core_helper.h"
#include "shared/source/helpers/state_base_address.h"
#include "shared/source/helpers/state_base_address_bdw_and_later.inl"

#include "level_zero/core/source/cmdlist/cmdlist.h"
#include "level_zero/core/source/cmdqueue/cmdqueue_hw.h"
#include "level_zero/core/source/device/device.h"
#include "level_zero/tools/source/metrics/metric.h"

namespace L0 {

template <GFXCORE_FAMILY gfxCoreFamily>
void CommandQueueHw<gfxCoreFamily>::programStateBaseAddress(uint64_t gsba, bool useLocalMemoryForIndirectHeap, NEO::LinearStream &commandStream, bool cachedMOCSAllowed, NEO::StreamProperties *streamProperties) {
    using STATE_BASE_ADDRESS = typename GfxFamily::STATE_BASE_ADDRESS;

    NEO::Device *neoDevice = device->getNEODevice();
    auto csr = this->getCsr();
    bool isRcs = csr->isRcs();
    auto &rootDeviceEnvironment = neoDevice->getRootDeviceEnvironment();

    bool useGlobalSshAndDsh = false;
    bool isDebuggerActive = neoDevice->isDebuggerActive() || neoDevice->getDebugger() != nullptr;

    uint64_t globalHeapsBase = 0;
    uint64_t indirectObjectHeapBaseAddress = 0;

    NEO::StateBaseAddressProperties *sbaProperties = nullptr;

    if (streamProperties != nullptr) {
        sbaProperties = &streamProperties->stateBaseAddress;
    } else {
        useGlobalSshAndDsh = NEO::ApiSpecificConfig::getBindlessConfiguration();
        if (useGlobalSshAndDsh) {
            globalHeapsBase = neoDevice->getBindlessHeapsHelper()->getGlobalHeapsBase();
        }

        indirectObjectHeapBaseAddress = neoDevice->getMemoryManager()->getInternalHeapBaseAddress(device->getRootDeviceIndex(), useLocalMemoryForIndirectHeap);
    }

    uint64_t instructionHeapBaseAddress = neoDevice->getMemoryManager()->getInternalHeapBaseAddress(device->getRootDeviceIndex(), neoDevice->getMemoryManager()->isLocalMemoryUsedForIsa(neoDevice->getRootDeviceIndex()));

    STATE_BASE_ADDRESS sbaCmd;

    NEO::EncodeWA<GfxFamily>::addPipeControlBeforeStateBaseAddress(commandStream, rootDeviceEnvironment, isRcs, csr->getDcFlushSupport());
    NEO::EncodeWA<GfxFamily>::encodeAdditionalPipelineSelect(commandStream, {}, true, rootDeviceEnvironment, isRcs);
    auto l1CachePolicyData = csr->getStoredL1CachePolicy();

    NEO::StateBaseAddressHelperArgs<GfxFamily> stateBaseAddressHelperArgs = {
        gsba,                                             // generalStateBaseAddress
        indirectObjectHeapBaseAddress,                    // indirectObjectHeapBaseAddress
        instructionHeapBaseAddress,                       // instructionHeapBaseAddress
        globalHeapsBase,                                  // globalHeapsBaseAddress
        0,                                                // surfaceStateBaseAddress
        &sbaCmd,                                          // stateBaseAddressCmd
        sbaProperties,                                    // sbaProperties
        nullptr,                                          // dsh
        nullptr,                                          // ioh
        nullptr,                                          // ssh
        neoDevice->getGmmHelper(),                        // gmmHelper
        (device->getMOCS(cachedMOCSAllowed, false) >> 1), // statelessMocsIndex
        l1CachePolicyData->getL1CacheValue(false),        // l1CachePolicy
        l1CachePolicyData->getL1CacheValue(true),         // l1CachePolicyDebuggerActive
        NEO::MemoryCompressionState::NotApplicable,       // memoryCompressionState
        true,                                             // setInstructionStateBaseAddress
        true,                                             // setGeneralStateBaseAddress
        useGlobalSshAndDsh,                               // useGlobalHeapsBaseAddress
        false,                                            // isMultiOsContextCapable
        false,                                            // useGlobalAtomics
        false,                                            // areMultipleSubDevicesInContext
        false,                                            // overrideSurfaceStateBaseAddress
        isDebuggerActive,                                 // isDebuggerActive
        this->doubleSbaWa                                 // doubleSbaWa
    };

    NEO::StateBaseAddressHelper<GfxFamily>::programStateBaseAddressIntoCommandStream(stateBaseAddressHelperArgs, commandStream);

    bool sbaTrackingEnabled = (NEO::Debugger::isDebugEnabled(this->internalUsage) && device->getL0Debugger());
    NEO::EncodeStateBaseAddress<GfxFamily>::setSbaTrackingForL0DebuggerIfEnabled(sbaTrackingEnabled, *neoDevice, commandStream, sbaCmd, true);

    NEO::EncodeWA<GfxFamily>::encodeAdditionalPipelineSelect(commandStream, {}, false, rootDeviceEnvironment, isRcs);

    csr->setGSBAStateDirty(false);
}

template <GFXCORE_FAMILY gfxCoreFamily>
inline size_t CommandQueueHw<gfxCoreFamily>::estimateStateBaseAddressCmdDispatchSize(bool bindingTableBaseAddress) {
    using STATE_BASE_ADDRESS = typename GfxFamily::STATE_BASE_ADDRESS;

    size_t size = sizeof(STATE_BASE_ADDRESS) + NEO::MemorySynchronizationCommands<GfxFamily>::getSizeForSingleBarrier(false) +
                  NEO::EncodeWA<GfxFamily>::getAdditionalPipelineSelectSize(*device->getNEODevice(), this->csr->isRcs());

    size += estimateStateBaseAddressDebugTracking();
    return size;
}

template <GFXCORE_FAMILY gfxCoreFamily>
size_t CommandQueueHw<gfxCoreFamily>::estimateStateBaseAddressCmdSize() {
    return estimateStateBaseAddressCmdDispatchSize(true);
}

template <GFXCORE_FAMILY gfxCoreFamily>
void CommandQueueHw<gfxCoreFamily>::handleScratchSpace(NEO::HeapContainer &heapContainer,
                                                       NEO::ScratchSpaceController *scratchController,
                                                       bool &gsbaState, bool &frontEndState,
                                                       uint32_t perThreadScratchSpaceSize, uint32_t perThreadPrivateScratchSize) {

    if (perThreadScratchSpaceSize > 0) {
        scratchController->setRequiredScratchSpace(nullptr, 0u, perThreadScratchSpaceSize, 0u, csr->peekTaskCount(),
                                                   csr->getOsContext(), gsbaState, frontEndState);
        auto scratchAllocation = scratchController->getScratchSpaceAllocation();
        csr->makeResident(*scratchAllocation);
    }
}

template <GFXCORE_FAMILY gfxCoreFamily>
void CommandQueueHw<gfxCoreFamily>::patchCommands(CommandList &commandList, uint64_t scratchAddress) {
    using MI_SEMAPHORE_WAIT = typename GfxFamily::MI_SEMAPHORE_WAIT;
    using COMPARE_OPERATION = typename GfxFamily::MI_SEMAPHORE_WAIT::COMPARE_OPERATION;

    auto &commandsToPatch = commandList.getCommandsToPatch();
    for (auto &commandToPatch : commandsToPatch) {
        switch (commandToPatch.type) {
        case CommandList::CommandToPatch::FrontEndState: {
            UNRECOVERABLE_IF(true);
            break;
        }
        case CommandList::CommandToPatch::PauseOnEnqueueSemaphoreStart: {
            NEO::EncodeSemaphore<GfxFamily>::programMiSemaphoreWait(reinterpret_cast<MI_SEMAPHORE_WAIT *>(commandToPatch.pCommand),
                                                                    csr->getDebugPauseStateGPUAddress(),
                                                                    static_cast<uint32_t>(NEO::DebugPauseState::hasUserStartConfirmation),
                                                                    COMPARE_OPERATION::COMPARE_OPERATION_SAD_EQUAL_SDD,
                                                                    false, true);
            break;
        }
        case CommandList::CommandToPatch::PauseOnEnqueueSemaphoreEnd: {
            NEO::EncodeSemaphore<GfxFamily>::programMiSemaphoreWait(reinterpret_cast<MI_SEMAPHORE_WAIT *>(commandToPatch.pCommand),
                                                                    csr->getDebugPauseStateGPUAddress(),
                                                                    static_cast<uint32_t>(NEO::DebugPauseState::hasUserEndConfirmation),
                                                                    COMPARE_OPERATION::COMPARE_OPERATION_SAD_EQUAL_SDD,
                                                                    false, true);
            break;
        }
        case CommandList::CommandToPatch::PauseOnEnqueuePipeControlStart: {

            NEO::PipeControlArgs args;
            args.dcFlushEnable = csr->getDcFlushSupport();

            auto command = reinterpret_cast<void *>(commandToPatch.pCommand);
            NEO::MemorySynchronizationCommands<GfxFamily>::setBarrierWithPostSyncOperation(
                command,
                NEO::PostSyncMode::ImmediateData,
                csr->getDebugPauseStateGPUAddress(),
                static_cast<uint64_t>(NEO::DebugPauseState::waitingForUserStartConfirmation),
                device->getNEODevice()->getRootDeviceEnvironment(),
                args);
            break;
        }
        case CommandList::CommandToPatch::PauseOnEnqueuePipeControlEnd: {

            NEO::PipeControlArgs args;
            args.dcFlushEnable = csr->getDcFlushSupport();

            auto command = reinterpret_cast<void *>(commandToPatch.pCommand);
            NEO::MemorySynchronizationCommands<GfxFamily>::setBarrierWithPostSyncOperation(
                command,
                NEO::PostSyncMode::ImmediateData,
                csr->getDebugPauseStateGPUAddress(),
                static_cast<uint64_t>(NEO::DebugPauseState::waitingForUserEndConfirmation),
                device->getNEODevice()->getRootDeviceEnvironment(),
                args);
            break;
        }
        default: {
            UNRECOVERABLE_IF(true);
        }
        }
    }
}

} // namespace L0
