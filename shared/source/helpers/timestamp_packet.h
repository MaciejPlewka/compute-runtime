/*
 * Copyright (C) 2018-2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/command_container/command_encoder.h"
#include "shared/source/command_stream/csr_deps.h"
#include "shared/source/execution_environment/root_device_environment.h"
#include "shared/source/helpers/aux_translation.h"
#include "shared/source/helpers/gfx_core_helper.h"
#include "shared/source/helpers/non_copyable_or_moveable.h"
#include "shared/source/helpers/pipe_control_args.h"
#include "shared/source/helpers/string.h"
#include "shared/source/helpers/timestamp_packet_container.h"
#include "shared/source/helpers/timestamp_packet_size_control.h"
#include "shared/source/utilities/tag_allocator.h"

#include <cstdint>

namespace NEO {
class CommandStreamReceiver;
class LinearStream;

#pragma pack(1)
template <typename TSize>
class TimestampPackets : public TagTypeBase {
  protected:
    struct Packet {
        TSize contextStart = 1u;
        TSize globalStart = 1u;
        TSize contextEnd = 1u;
        TSize globalEnd = 1u;
    };

  public:
    static constexpr AllocationType getAllocationType() {
        return AllocationType::TIMESTAMP_PACKET_TAG_BUFFER;
    }

    static constexpr TagNodeType getTagNodeType() { return TagNodeType::TimestampPacket; }

    static constexpr size_t getSinglePacketSize() { return sizeof(Packet); }

    void initialize() {
        for (auto &packet : packets) {
            packet.contextStart = 1u;
            packet.globalStart = 1u;
            packet.contextEnd = 1u;
            packet.globalEnd = 1u;
        }
    }

    void assignDataToAllTimestamps(uint32_t packetIndex, void *source) {
        memcpy_s(&packets[packetIndex], sizeof(Packet), source, sizeof(Packet));
    }

    static constexpr size_t getGlobalStartOffset() { return offsetof(Packet, globalStart); }
    static constexpr size_t getContextStartOffset() { return offsetof(Packet, contextStart); }
    static constexpr size_t getContextEndOffset() { return offsetof(Packet, contextEnd); }
    static constexpr size_t getGlobalEndOffset() { return offsetof(Packet, globalEnd); }

    uint64_t getContextStartValue(uint32_t packetIndex) const { return static_cast<uint64_t>(packets[packetIndex].contextStart); }
    uint64_t getGlobalStartValue(uint32_t packetIndex) const { return static_cast<uint64_t>(packets[packetIndex].globalStart); }
    uint64_t getContextEndValue(uint32_t packetIndex) const { return static_cast<uint64_t>(packets[packetIndex].contextEnd); }
    uint64_t getGlobalEndValue(uint32_t packetIndex) const { return static_cast<uint64_t>(packets[packetIndex].globalEnd); }

    void const *getContextEndAddress(uint32_t packetIndex) const { return static_cast<void const *>(&packets[packetIndex].contextEnd); }
    void const *getContextStartAddress(uint32_t packetIndex) const { return static_cast<void const *>(&packets[packetIndex].contextStart); }

  protected:
    Packet packets[TimestampPacketSizeControl::preferredPacketCount];
};
#pragma pack()

static_assert(((4 * TimestampPacketSizeControl::preferredPacketCount) * sizeof(uint32_t)) == sizeof(TimestampPackets<uint32_t>),
              "This structure is consumed by GPU and has to follow specific restrictions for padding and size");

struct TimestampPacketHelper {
    static uint64_t getContextEndGpuAddress(const TagNodeBase &timestampPacketNode) {
        return timestampPacketNode.getGpuAddress() + timestampPacketNode.getContextEndOffset();
    }
    static uint64_t getContextStartGpuAddress(const TagNodeBase &timestampPacketNode) {
        return timestampPacketNode.getGpuAddress() + timestampPacketNode.getContextStartOffset();
    }
    static uint64_t getGlobalEndGpuAddress(const TagNodeBase &timestampPacketNode) {
        return timestampPacketNode.getGpuAddress() + timestampPacketNode.getGlobalEndOffset();
    }
    static uint64_t getGlobalStartGpuAddress(const TagNodeBase &timestampPacketNode) {
        return timestampPacketNode.getGpuAddress() + timestampPacketNode.getGlobalStartOffset();
    }

    template <typename GfxFamily>
    static void programSemaphore(LinearStream &cmdStream, TagNodeBase &timestampPacketNode) {
        using COMPARE_OPERATION = typename GfxFamily::MI_SEMAPHORE_WAIT::COMPARE_OPERATION;

        auto compareAddress = getContextEndGpuAddress(timestampPacketNode);

        for (uint32_t packetId = 0; packetId < timestampPacketNode.getPacketsUsed(); packetId++) {
            uint64_t compareOffset = packetId * timestampPacketNode.getSinglePacketSize();
            EncodeSemaphore<GfxFamily>::addMiSemaphoreWaitCommand(cmdStream, compareAddress + compareOffset, 1, COMPARE_OPERATION::COMPARE_OPERATION_SAD_NOT_EQUAL_SDD);
        }
    }

    template <typename GfxFamily>
    static void programConditionalBbStartForRelaxedOrdering(LinearStream &cmdStream, TagNodeBase &timestampPacketNode) {
        auto compareAddress = getContextEndGpuAddress(timestampPacketNode);

        for (uint32_t packetId = 0; packetId < timestampPacketNode.getPacketsUsed(); packetId++) {
            uint64_t compareOffset = packetId * timestampPacketNode.getSinglePacketSize();

            EncodeBatchBufferStartOrEnd<GfxFamily>::programConditionalDataMemBatchBufferStart(cmdStream, 0, compareAddress + compareOffset, 1,
                                                                                              NEO::CompareOperation::Equal, true);
        }
    }

    template <typename GfxFamily>
    static void programCsrDependenciesForTimestampPacketContainer(LinearStream &cmdStream, const CsrDependencies &csrDependencies, bool relaxedOrderingEnabled) {
        for (auto timestampPacketContainer : csrDependencies.timestampPacketContainer) {
            for (auto &node : timestampPacketContainer->peekNodes()) {
                if (relaxedOrderingEnabled) {
                    TimestampPacketHelper::programConditionalBbStartForRelaxedOrdering<GfxFamily>(cmdStream, *node);
                } else {
                    TimestampPacketHelper::programSemaphore<GfxFamily>(cmdStream, *node);
                }
            }
        }
    }

    template <typename GfxFamily>
    static void programCsrDependenciesForForMultiRootDeviceSyncContainer(LinearStream &cmdStream, const CsrDependencies &csrDependencies) {
        for (auto timestampPacketContainer : csrDependencies.multiRootTimeStampSyncContainer) {
            for (auto &node : timestampPacketContainer->peekNodes()) {
                TimestampPacketHelper::programSemaphore<GfxFamily>(cmdStream, *node);
            }
        }
    }

    template <typename GfxFamily, AuxTranslationDirection auxTranslationDirection>
    static void programSemaphoreForAuxTranslation(LinearStream &cmdStream,
                                                  const TimestampPacketDependencies *timestampPacketDependencies,
                                                  const RootDeviceEnvironment &rootDeviceEnvironment) {
        auto &container = (auxTranslationDirection == AuxTranslationDirection::AuxToNonAux)
                              ? timestampPacketDependencies->auxToNonAuxNodes
                              : timestampPacketDependencies->nonAuxToAuxNodes;

        // cache flush after NDR, before NonAuxToAux
        if (auxTranslationDirection == AuxTranslationDirection::NonAuxToAux && timestampPacketDependencies->cacheFlushNodes.peekNodes().size() > 0) {
            UNRECOVERABLE_IF(timestampPacketDependencies->cacheFlushNodes.peekNodes().size() != 1);
            auto cacheFlushTimestampPacketGpuAddress = getContextEndGpuAddress(*timestampPacketDependencies->cacheFlushNodes.peekNodes()[0]);

            PipeControlArgs args;
            args.dcFlushEnable = MemorySynchronizationCommands<GfxFamily>::getDcFlushEnable(true, rootDeviceEnvironment);
            MemorySynchronizationCommands<GfxFamily>::addBarrierWithPostSyncOperation(
                cmdStream, PostSyncMode::ImmediateData,
                cacheFlushTimestampPacketGpuAddress, 0, rootDeviceEnvironment, args);
        }

        for (auto &node : container.peekNodes()) {
            TimestampPacketHelper::programSemaphore<GfxFamily>(cmdStream, *node);
        }
    }

    template <typename GfxFamily, AuxTranslationDirection auxTranslationDirection>
    static size_t getRequiredCmdStreamSizeForAuxTranslationNodeDependency(size_t count, const RootDeviceEnvironment &rootDeviceEnvironment, bool cacheFlushForBcsRequired) {
        size_t size = count * TimestampPacketHelper::getRequiredCmdStreamSizeForNodeDependencyWithBlitEnqueue<GfxFamily>();

        if (auxTranslationDirection == AuxTranslationDirection::NonAuxToAux && cacheFlushForBcsRequired) {
            size += MemorySynchronizationCommands<GfxFamily>::getSizeForBarrierWithPostSyncOperation(rootDeviceEnvironment, false);
        }

        return size;
    }

    template <typename GfxFamily>
    static size_t getRequiredCmdStreamSizeForNodeDependencyWithBlitEnqueue() {
        return NEO::EncodeSemaphore<GfxFamily>::getSizeMiSemaphoreWait();
    }

    template <typename GfxFamily>
    static size_t getRequiredCmdStreamSizeForSemaphoreNodeDependency(TagNodeBase &timestampPacketNode) {
        return (timestampPacketNode.getPacketsUsed() * NEO::EncodeSemaphore<GfxFamily>::getSizeMiSemaphoreWait());
    }

    template <typename GfxFamily>
    static size_t getRequiredCmdStreamSizeForRelaxedOrderingNodeDependency(TagNodeBase &timestampPacketNode) {
        return (timestampPacketNode.getPacketsUsed() * EncodeBatchBufferStartOrEnd<GfxFamily>::getCmdSizeConditionalDataMemBatchBufferStart());
    }

    template <typename GfxFamily>
    static size_t getRequiredCmdStreamSize(const CsrDependencies &csrDependencies, bool relaxedOrderingEnabled) {
        size_t totalCommandsSize = 0;
        for (auto timestampPacketContainer : csrDependencies.timestampPacketContainer) {
            for (auto &node : timestampPacketContainer->peekNodes()) {
                if (relaxedOrderingEnabled) {
                    totalCommandsSize += getRequiredCmdStreamSizeForRelaxedOrderingNodeDependency<GfxFamily>(*node);
                } else {
                    totalCommandsSize += getRequiredCmdStreamSizeForSemaphoreNodeDependency<GfxFamily>(*node);
                }
            }
        }

        return totalCommandsSize;
    }

    template <typename GfxFamily>
    static size_t getRequiredCmdStreamSizeForMultiRootDeviceSyncNodesContainer(const CsrDependencies &csrDependencies) {
        return csrDependencies.multiRootTimeStampSyncContainer.size() * NEO::EncodeSemaphore<GfxFamily>::getSizeMiSemaphoreWait();
    }
};

} // namespace NEO
