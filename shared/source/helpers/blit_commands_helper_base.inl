/*
 * Copyright (C) 2019-2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/command_container/command_encoder.h"
#include "shared/source/gmm_helper/gmm_helper.h"
#include "shared/source/helpers/blit_properties.h"
#include "shared/source/helpers/gfx_core_helper.h"
#include "shared/source/helpers/register_offsets.h"
#include "shared/source/helpers/timestamp_packet.h"

#include <cmath>

namespace NEO {

template <typename GfxFamily>
uint64_t BlitCommandsHelper<GfxFamily>::getMaxBlitWidth(const RootDeviceEnvironment &rootDeviceEnvironment) {
    if (DebugManager.flags.LimitBlitterMaxWidth.get() != -1) {
        return static_cast<uint64_t>(DebugManager.flags.LimitBlitterMaxWidth.get());
    }
    auto maxBlitWidthOverride = getMaxBlitWidthOverride(rootDeviceEnvironment);
    if (maxBlitWidthOverride > 0) {
        return maxBlitWidthOverride;
    }
    return BlitterConstants::maxBlitWidth;
}

template <typename GfxFamily>
uint64_t BlitCommandsHelper<GfxFamily>::getMaxBlitHeight(const RootDeviceEnvironment &rootDeviceEnvironment, bool isSystemMemoryPoolUsed) {
    if (DebugManager.flags.LimitBlitterMaxHeight.get() != -1) {
        return static_cast<uint64_t>(DebugManager.flags.LimitBlitterMaxHeight.get());
    }
    auto maxBlitHeightOverride = getMaxBlitHeightOverride(rootDeviceEnvironment, isSystemMemoryPoolUsed);
    if (maxBlitHeightOverride > 0) {
        return maxBlitHeightOverride;
    }
    return BlitterConstants::maxBlitHeight;
}

template <typename GfxFamily>
void BlitCommandsHelper<GfxFamily>::dispatchPreBlitCommand(LinearStream &linearStream, RootDeviceEnvironment &rootDeviceEnvironment) {
    if (BlitCommandsHelper<GfxFamily>::preBlitCommandWARequired()) {
        NEO::EncodeDummyBlitWaArgs waArgs{false, const_cast<RootDeviceEnvironment *>(&rootDeviceEnvironment)};
        MiFlushArgs args{waArgs};

        EncodeMiFlushDW<GfxFamily>::programWithWa(linearStream, 0, 0, args);
    }
}

template <typename GfxFamily>
size_t BlitCommandsHelper<GfxFamily>::estimatePreBlitCommandSize(const RootDeviceEnvironment &rootDeviceEnvironment) {
    if (BlitCommandsHelper<GfxFamily>::preBlitCommandWARequired()) {
        EncodeDummyBlitWaArgs waArgs{false, const_cast<RootDeviceEnvironment *>(&rootDeviceEnvironment)};
        return EncodeMiFlushDW<GfxFamily>::getCommandSizeWithWa(waArgs);
    }

    return 0u;
}

template <typename GfxFamily>
void BlitCommandsHelper<GfxFamily>::dispatchPostBlitCommand(LinearStream &linearStream, EncodeDummyBlitWaArgs &waArgs) {
    MiFlushArgs args{waArgs};
    if (DebugManager.flags.PostBlitCommand.get() != BlitterConstants::PostBlitMode::Default) {
        switch (DebugManager.flags.PostBlitCommand.get()) {
        case BlitterConstants::PostBlitMode::MiArbCheck:
            EncodeMiArbCheck<GfxFamily>::programWithWa(linearStream, std::nullopt, waArgs);
            return;
        case BlitterConstants::PostBlitMode::MiFlush:
            EncodeMiFlushDW<GfxFamily>::programWithWa(linearStream, 0, 0, args);
            return;
        default:
            return;
        }
    }

    if (BlitCommandsHelper<GfxFamily>::miArbCheckWaRequired()) {
        EncodeMiFlushDW<GfxFamily>::programWithWa(linearStream, 0, 0, args);
        args.waArgs.isWaRequired = false;
    }

    EncodeMiArbCheck<GfxFamily>::programWithWa(linearStream, std::nullopt, waArgs);
}

template <typename GfxFamily>
size_t BlitCommandsHelper<GfxFamily>::estimatePostBlitCommandSize(const RootDeviceEnvironment &rootDeviceEnvironment) {
    EncodeDummyBlitWaArgs waArgs{true, const_cast<RootDeviceEnvironment *>(&rootDeviceEnvironment)};

    if (DebugManager.flags.PostBlitCommand.get() != BlitterConstants::PostBlitMode::Default) {
        switch (DebugManager.flags.PostBlitCommand.get()) {
        case BlitterConstants::PostBlitMode::MiArbCheck:
            return EncodeMiArbCheck<GfxFamily>::getCommandSizeWithWa(waArgs);
        case BlitterConstants::PostBlitMode::MiFlush:
            return EncodeMiFlushDW<GfxFamily>::getCommandSizeWithWa(waArgs);
        default:
            return 0;
        }
    }
    size_t size = 0u;
    if (BlitCommandsHelper<GfxFamily>::miArbCheckWaRequired()) {
        size += EncodeMiFlushDW<GfxFamily>::getCommandSizeWithWa(waArgs);
        waArgs.isWaRequired = false;
    }
    size += EncodeMiArbCheck<GfxFamily>::getCommandSizeWithWa(waArgs);
    return size;
}

template <typename GfxFamily>
size_t BlitCommandsHelper<GfxFamily>::estimateBlitCommandSize(const Vec3<size_t> &copySize, const CsrDependencies &csrDependencies, bool updateTimestampPacket, bool profilingEnabled,
                                                              bool isImage, const RootDeviceEnvironment &rootDeviceEnvironment, bool isSystemMemoryPoolUsed, bool relaxedOrderingEnabled) {
    size_t timestampCmdSize = 0;
    if (updateTimestampPacket) {
        EncodeDummyBlitWaArgs waArgs{false, const_cast<RootDeviceEnvironment *>(&rootDeviceEnvironment)};
        timestampCmdSize += EncodeMiFlushDW<GfxFamily>::getCommandSizeWithWa(waArgs);
        if (profilingEnabled) {
            timestampCmdSize += getProfilingMmioCmdsSize();
        }
    }

    size_t nBlits = 0u;
    size_t sizePerBlit = 0u;

    if (isImage) {
        nBlits = getNumberOfBlitsForCopyRegion(copySize, rootDeviceEnvironment, isSystemMemoryPoolUsed);
        sizePerBlit = sizeof(typename GfxFamily::XY_BLOCK_COPY_BLT);
    } else {
        nBlits = std::min(getNumberOfBlitsForCopyRegion(copySize, rootDeviceEnvironment, isSystemMemoryPoolUsed),
                          getNumberOfBlitsForCopyPerRow(copySize, rootDeviceEnvironment, isSystemMemoryPoolUsed));
        sizePerBlit = sizeof(typename GfxFamily::XY_COPY_BLT);
    }

    sizePerBlit += estimatePostBlitCommandSize(rootDeviceEnvironment);
    return TimestampPacketHelper::getRequiredCmdStreamSize<GfxFamily>(csrDependencies, relaxedOrderingEnabled) +
           TimestampPacketHelper::getRequiredCmdStreamSizeForMultiRootDeviceSyncNodesContainer<GfxFamily>(csrDependencies) +
           (sizePerBlit * nBlits) +
           timestampCmdSize +
           estimatePreBlitCommandSize(rootDeviceEnvironment);
}

template <typename GfxFamily>
size_t BlitCommandsHelper<GfxFamily>::estimateBlitCommandsSize(const BlitPropertiesContainer &blitPropertiesContainer,
                                                               bool profilingEnabled, bool debugPauseEnabled, bool blitterDirectSubmission,
                                                               bool relaxedOrderingEnabled, const RootDeviceEnvironment &rootDeviceEnvironment) {
    size_t size = 0;
    EncodeDummyBlitWaArgs waArgs{false, const_cast<RootDeviceEnvironment *>(&rootDeviceEnvironment)};
    for (auto &blitProperties : blitPropertiesContainer) {
        auto updateTimestampPacket = blitProperties.outputTimestampPacket != nullptr;
        auto isImage = blitProperties.isImageOperation();
        size += BlitCommandsHelper<GfxFamily>::estimateBlitCommandSize(blitProperties.copySize, blitProperties.csrDependencies, updateTimestampPacket,
                                                                       profilingEnabled, isImage, rootDeviceEnvironment, blitProperties.isSystemMemoryPoolUsed, relaxedOrderingEnabled);
        if (blitProperties.multiRootDeviceEventSync != nullptr) {
            size += EncodeMiFlushDW<GfxFamily>::getCommandSizeWithWa(waArgs);
        }
    }
    size += BlitCommandsHelper<GfxFamily>::getWaCmdsSize(blitPropertiesContainer);
    size += 2 * MemorySynchronizationCommands<GfxFamily>::getSizeForAdditonalSynchronization(rootDeviceEnvironment);
    size += EncodeMiFlushDW<GfxFamily>::getCommandSizeWithWa(waArgs);
    size += blitterDirectSubmission ? sizeof(typename GfxFamily::MI_BATCH_BUFFER_START) : sizeof(typename GfxFamily::MI_BATCH_BUFFER_END);

    if (debugPauseEnabled) {
        size += BlitCommandsHelper<GfxFamily>::getSizeForDebugPauseCommands(rootDeviceEnvironment);
    }

    size += BlitCommandsHelper<GfxFamily>::getSizeForGlobalSequencerFlush();

    if (relaxedOrderingEnabled) {
        size += 2 * EncodeSetMMIO<GfxFamily>::sizeREG;
    }

    return alignUp(size, MemoryConstants::cacheLineSize);
}

template <typename GfxFamily>
uint64_t BlitCommandsHelper<GfxFamily>::calculateBlitCommandDestinationBaseAddress(const BlitProperties &blitProperties, uint64_t offset, uint64_t row, uint64_t slice) {
    return blitProperties.dstGpuAddress + blitProperties.dstOffset.x * blitProperties.bytesPerPixel + offset +
           blitProperties.dstOffset.y * blitProperties.dstRowPitch +
           blitProperties.dstOffset.z * blitProperties.dstSlicePitch +
           row * blitProperties.dstRowPitch +
           slice * blitProperties.dstSlicePitch;
}

template <typename GfxFamily>
uint64_t BlitCommandsHelper<GfxFamily>::calculateBlitCommandSourceBaseAddress(const BlitProperties &blitProperties, uint64_t offset, uint64_t row, uint64_t slice) {
    return blitProperties.srcGpuAddress + blitProperties.srcOffset.x * blitProperties.bytesPerPixel + offset +
           blitProperties.srcOffset.y * blitProperties.srcRowPitch +
           blitProperties.srcOffset.z * blitProperties.srcSlicePitch +
           row * blitProperties.srcRowPitch +
           slice * blitProperties.srcSlicePitch;
}

template <typename GfxFamily>
void BlitCommandsHelper<GfxFamily>::dispatchBlitCommandsForBufferPerRow(const BlitProperties &blitProperties, LinearStream &linearStream, EncodeDummyBlitWaArgs &waArgs) {
    uint64_t width = 1;
    uint64_t height = 1;

    PRINT_DEBUG_STRING(DebugManager.flags.PrintBlitDispatchDetails.get(), stdout,
                       "\nBlit dispatch with AuxTranslationDirection %u ", static_cast<uint32_t>(blitProperties.auxTranslationDirection));

    dispatchPreBlitCommand(linearStream, *waArgs.rootDeviceEnvironment);
    auto bltCmd = GfxFamily::cmdInitXyCopyBlt;
    appendColorDepth(blitProperties, bltCmd);

    for (uint64_t slice = 0; slice < blitProperties.copySize.z; slice++) {
        for (uint64_t row = 0; row < blitProperties.copySize.y; row++) {
            uint64_t offset = 0;
            uint64_t sizeToBlit = blitProperties.copySize.x;
            while (sizeToBlit != 0) {
                if (sizeToBlit > getMaxBlitWidth(*waArgs.rootDeviceEnvironment)) {
                    // dispatch 2D blit: maxBlitWidth x (1 .. maxBlitHeight)
                    width = getMaxBlitWidth(*waArgs.rootDeviceEnvironment);
                    height = std::min((sizeToBlit / width), getMaxBlitHeight(*waArgs.rootDeviceEnvironment, blitProperties.isSystemMemoryPoolUsed));
                } else {
                    // dispatch 1D blt: (1 .. maxBlitWidth) x 1
                    width = sizeToBlit;
                    height = 1;
                }

                bltCmd.setDestinationX2CoordinateRight(static_cast<uint32_t>(width));
                bltCmd.setDestinationY2CoordinateBottom(static_cast<uint32_t>(height));
                bltCmd.setDestinationPitch(static_cast<uint32_t>(width));
                bltCmd.setSourcePitch(static_cast<uint32_t>(width));

                auto dstAddr = calculateBlitCommandDestinationBaseAddress(blitProperties, offset, row, slice);
                auto srcAddr = calculateBlitCommandSourceBaseAddress(blitProperties, offset, row, slice);

                PRINT_DEBUG_STRING(DebugManager.flags.PrintBlitDispatchDetails.get(), stdout,
                                   "\nBlit command. width: %u, height: %u, srcAddr: %#llx, dstAddr: %#llx ", width, height, srcAddr, dstAddr);

                bltCmd.setDestinationBaseAddress(dstAddr);
                bltCmd.setSourceBaseAddress(srcAddr);

                appendBlitCommandsForBuffer(blitProperties, bltCmd, *waArgs.rootDeviceEnvironment);

                auto bltStream = linearStream.getSpaceForCmd<typename GfxFamily::XY_COPY_BLT>();
                *bltStream = bltCmd;

                waArgs.isWaRequired = true;
                dispatchPostBlitCommand(linearStream, waArgs);

                auto blitSize = width * height;
                sizeToBlit -= blitSize;
                offset += blitSize;
            }
        }
    }
}

template <typename GfxFamily>
template <size_t patternSize>
void BlitCommandsHelper<GfxFamily>::dispatchBlitMemoryFill(NEO::GraphicsAllocation *dstAlloc, uint64_t offset, uint32_t *pattern, LinearStream &linearStream, size_t size, EncodeDummyBlitWaArgs &waArgs, COLOR_DEPTH depth) {
    using XY_COLOR_BLT = typename GfxFamily::XY_COLOR_BLT;
    auto blitCmd = GfxFamily::cmdInitXyColorBlt;
    auto &rootDeviceEnvironment = *waArgs.rootDeviceEnvironment;

    blitCmd.setFillColor(pattern);
    blitCmd.setColorDepth(depth);

    uint64_t sizeToFill = size / patternSize;
    while (sizeToFill != 0) {
        auto tmpCmd = blitCmd;
        tmpCmd.setDestinationBaseAddress(ptrOffset(dstAlloc->getGpuAddress(), static_cast<size_t>(offset)));
        uint64_t height = 0;
        uint64_t width = 0;
        if (sizeToFill <= getMaxBlitWidth(rootDeviceEnvironment)) {
            width = sizeToFill;
            height = 1;
        } else {
            width = getMaxBlitWidth(rootDeviceEnvironment);
            height = std::min((sizeToFill / width), getMaxBlitHeight(rootDeviceEnvironment, true));
            if (height > 1) {
                appendTilingEnable(tmpCmd);
            }
        }
        tmpCmd.setDestinationX2CoordinateRight(static_cast<uint32_t>(width));
        tmpCmd.setDestinationY2CoordinateBottom(static_cast<uint32_t>(height));
        tmpCmd.setDestinationPitch(static_cast<uint32_t>(width * patternSize));

        appendBlitCommandsForFillBuffer(dstAlloc, tmpCmd, rootDeviceEnvironment);

        auto cmd = linearStream.getSpaceForCmd<XY_COLOR_BLT>();
        *cmd = tmpCmd;
        waArgs.isWaRequired = true;
        auto blitSize = width * height;
        offset += (blitSize * patternSize);
        sizeToFill -= blitSize;
    }
}

template <typename GfxFamily>
void BlitCommandsHelper<GfxFamily>::dispatchBlitCommandsForImageRegion(const BlitProperties &blitProperties, LinearStream &linearStream, EncodeDummyBlitWaArgs &waArgs) {

    auto srcSlicePitch = static_cast<uint32_t>(blitProperties.srcSlicePitch);
    auto dstSlicePitch = static_cast<uint32_t>(blitProperties.dstSlicePitch);

    UNRECOVERABLE_IF(blitProperties.copySize.x > BlitterConstants::maxBlitWidth || blitProperties.copySize.y > BlitterConstants::maxBlitHeight);
    auto bltCmd = GfxFamily::cmdInitXyBlockCopyBlt;

    bltCmd.setSourceBaseAddress(blitProperties.srcGpuAddress);
    bltCmd.setDestinationBaseAddress(blitProperties.dstGpuAddress);

    bltCmd.setDestinationX1CoordinateLeft(static_cast<uint32_t>(blitProperties.dstOffset.x));
    bltCmd.setDestinationY1CoordinateTop(static_cast<uint32_t>(blitProperties.dstOffset.y));
    bltCmd.setDestinationX2CoordinateRight(static_cast<uint32_t>(blitProperties.dstOffset.x + blitProperties.copySize.x));
    bltCmd.setDestinationY2CoordinateBottom(static_cast<uint32_t>(blitProperties.dstOffset.y + blitProperties.copySize.y));

    bltCmd.setSourceX1CoordinateLeft(static_cast<uint32_t>(blitProperties.srcOffset.x));
    bltCmd.setSourceY1CoordinateTop(static_cast<uint32_t>(blitProperties.srcOffset.y));

    appendBlitCommandsBlockCopy(blitProperties, bltCmd, *(waArgs.rootDeviceEnvironment));
    appendBlitCommandsForImages(blitProperties, bltCmd, *(waArgs.rootDeviceEnvironment), srcSlicePitch, dstSlicePitch);
    appendColorDepth(blitProperties, bltCmd);
    appendSurfaceType(blitProperties, bltCmd);

    dispatchPreBlitCommand(linearStream, *(waArgs.rootDeviceEnvironment));
    for (uint32_t i = 0; i < blitProperties.copySize.z; i++) {
        appendSliceOffsets(blitProperties, bltCmd, i, *(waArgs.rootDeviceEnvironment), srcSlicePitch, dstSlicePitch);

        if (DebugManager.flags.PrintImageBlitBlockCopyCmdDetails.get()) {
            printImageBlitBlockCopyCommand(bltCmd, i);
        }

        auto cmd = linearStream.getSpaceForCmd<typename GfxFamily::XY_BLOCK_COPY_BLT>();
        *cmd = bltCmd;
        waArgs.isWaRequired = true;
        dispatchPostBlitCommand(linearStream, waArgs);
    }
}

template <typename GfxFamily>
void BlitCommandsHelper<GfxFamily>::dispatchDebugPauseCommands(LinearStream &commandStream, uint64_t debugPauseStateGPUAddress,
                                                               DebugPauseState confirmationTrigger, DebugPauseState waitCondition,
                                                               RootDeviceEnvironment &rootDeviceEnvironment) {
    using COMPARE_OPERATION = typename GfxFamily::MI_SEMAPHORE_WAIT::COMPARE_OPERATION;

    NEO::EncodeDummyBlitWaArgs waArgs{false, &rootDeviceEnvironment};
    MiFlushArgs args{waArgs};
    args.commandWithPostSync = true;

    EncodeMiFlushDW<GfxFamily>::programWithWa(commandStream, debugPauseStateGPUAddress, static_cast<uint32_t>(confirmationTrigger),
                                              args);

    EncodeSemaphore<GfxFamily>::addMiSemaphoreWaitCommand(commandStream, debugPauseStateGPUAddress, static_cast<uint32_t>(waitCondition), COMPARE_OPERATION::COMPARE_OPERATION_SAD_EQUAL_SDD);
}

template <typename GfxFamily>
size_t BlitCommandsHelper<GfxFamily>::getSizeForDebugPauseCommands(const RootDeviceEnvironment &rootDeviceEnvironment) {
    EncodeDummyBlitWaArgs waArgs{false, const_cast<RootDeviceEnvironment *>(&rootDeviceEnvironment)};
    return (EncodeMiFlushDW<GfxFamily>::getCommandSizeWithWa(waArgs) + EncodeSemaphore<GfxFamily>::getSizeMiSemaphoreWait()) * 2;
}

template <typename GfxFamily>
uint32_t BlitCommandsHelper<GfxFamily>::getAvailableBytesPerPixel(size_t copySize, uint32_t srcOrigin, uint32_t dstOrigin, size_t srcSize, size_t dstSize) {
    uint32_t bytesPerPixel = BlitterConstants::maxBytesPerPixel;
    while (bytesPerPixel > 1) {
        if (copySize % bytesPerPixel == 0 && srcSize % bytesPerPixel == 0 && dstSize % bytesPerPixel == 0) {
            if ((srcOrigin ? (srcOrigin % bytesPerPixel == 0) : true) && (dstOrigin ? (dstOrigin % bytesPerPixel == 0) : true)) {
                break;
            }
        }
        bytesPerPixel >>= 1;
    }
    return bytesPerPixel;
}

template <typename GfxFamily>
void BlitCommandsHelper<GfxFamily>::dispatchBlitCommands(const BlitProperties &blitProperties, LinearStream &linearStream, EncodeDummyBlitWaArgs &waArgs) {
    if (blitProperties.isImageOperation()) {
        dispatchBlitCommandsForImageRegion(blitProperties, linearStream, waArgs);
    } else {
        bool preferCopyBufferRegion = isCopyRegionPreferred(blitProperties.copySize, *waArgs.rootDeviceEnvironment, blitProperties.isSystemMemoryPoolUsed);
        preferCopyBufferRegion ? dispatchBlitCommandsForBufferRegion(blitProperties, linearStream, waArgs)
                               : dispatchBlitCommandsForBufferPerRow(blitProperties, linearStream, waArgs);
    }
}

template <typename GfxFamily>
uint64_t BlitCommandsHelper<GfxFamily>::calculateBlitCommandSourceBaseAddressCopyRegion(const BlitProperties &blitProperties, size_t slice) {
    return blitProperties.srcGpuAddress + blitProperties.srcOffset.x * blitProperties.bytesPerPixel +
           (blitProperties.srcOffset.y * blitProperties.srcRowPitch) +
           (blitProperties.srcSlicePitch * (slice + blitProperties.srcOffset.z));
}

template <typename GfxFamily>
uint64_t BlitCommandsHelper<GfxFamily>::calculateBlitCommandDestinationBaseAddressCopyRegion(const BlitProperties &blitProperties, size_t slice) {
    return blitProperties.dstGpuAddress + blitProperties.dstOffset.x * blitProperties.bytesPerPixel +
           (blitProperties.dstOffset.y * blitProperties.dstRowPitch) +
           (blitProperties.dstSlicePitch * (slice + blitProperties.dstOffset.z));
}

template <typename GfxFamily>
template <typename T>
void BlitCommandsHelper<GfxFamily>::appendBlitCommandsForBuffer(const BlitProperties &blitProperties, T &blitCmd, const RootDeviceEnvironment &rootDeviceEnvironment) {
    appendBlitCommandsBlockCopy(blitProperties, blitCmd, rootDeviceEnvironment);
}

template <typename GfxFamily>
void BlitCommandsHelper<GfxFamily>::appendBlitCommandsMemCopy(const BlitProperties &blitProperites, typename GfxFamily::XY_COPY_BLT &blitCmd,
                                                              const RootDeviceEnvironment &rootDeviceEnvironment) {
}

template <typename GfxFamily>
void BlitCommandsHelper<GfxFamily>::dispatchBlitCommandsForBufferRegion(const BlitProperties &blitProperties, LinearStream &linearStream, EncodeDummyBlitWaArgs &waArgs) {

    const auto maxWidthToCopy = getMaxBlitWidth(*waArgs.rootDeviceEnvironment);
    const auto maxHeightToCopy = getMaxBlitHeight(*waArgs.rootDeviceEnvironment, blitProperties.isSystemMemoryPoolUsed);

    dispatchPreBlitCommand(linearStream, *waArgs.rootDeviceEnvironment);

    auto bltCmd = GfxFamily::cmdInitXyCopyBlt;
    bltCmd.setSourcePitch(static_cast<uint32_t>(blitProperties.srcRowPitch));
    bltCmd.setDestinationPitch(static_cast<uint32_t>(blitProperties.dstRowPitch));
    appendColorDepth(blitProperties, bltCmd);

    for (size_t slice = 0u; slice < blitProperties.copySize.z; ++slice) {
        auto srcAddress = calculateBlitCommandSourceBaseAddressCopyRegion(blitProperties, slice);
        auto dstAddress = calculateBlitCommandDestinationBaseAddressCopyRegion(blitProperties, slice);
        auto heightToCopy = blitProperties.copySize.y;

        while (heightToCopy > 0) {
            auto height = static_cast<uint32_t>(std::min(heightToCopy, static_cast<size_t>(maxHeightToCopy)));
            auto widthToCopy = blitProperties.copySize.x;

            while (widthToCopy > 0) {
                auto width = static_cast<uint32_t>(std::min(widthToCopy, static_cast<size_t>(maxWidthToCopy)));

                bltCmd.setSourceBaseAddress(srcAddress);
                bltCmd.setDestinationBaseAddress(dstAddress);
                bltCmd.setDestinationX2CoordinateRight(width);
                bltCmd.setDestinationY2CoordinateBottom(height);

                appendBlitCommandsForBuffer(blitProperties, bltCmd, *waArgs.rootDeviceEnvironment);

                auto cmd = linearStream.getSpaceForCmd<typename GfxFamily::XY_COPY_BLT>();
                *cmd = bltCmd;
                waArgs.isWaRequired = true;

                dispatchPostBlitCommand(linearStream, waArgs);

                srcAddress += width;
                dstAddress += width;
                widthToCopy -= width;
            }

            heightToCopy -= height;
            srcAddress += (blitProperties.srcRowPitch - blitProperties.copySize.x);
            srcAddress += (blitProperties.srcRowPitch * (height - 1));
            dstAddress += (blitProperties.dstRowPitch - blitProperties.copySize.x);
            dstAddress += (blitProperties.dstRowPitch * (height - 1));
        }
    }
}

template <typename GfxFamily>
bool BlitCommandsHelper<GfxFamily>::isCopyRegionPreferred(const Vec3<size_t> &copySize, const RootDeviceEnvironment &rootDeviceEnvironment, bool isSystemMemoryPoolUsed) {
    bool preferCopyRegion = getNumberOfBlitsForCopyRegion(copySize, rootDeviceEnvironment, isSystemMemoryPoolUsed) < getNumberOfBlitsForCopyPerRow(copySize, rootDeviceEnvironment, isSystemMemoryPoolUsed);
    return preferCopyRegion;
}

template <typename GfxFamily>
size_t BlitCommandsHelper<GfxFamily>::getNumberOfBlitsForCopyRegion(const Vec3<size_t> &copySize, const RootDeviceEnvironment &rootDeviceEnvironment, bool isSystemMemoryPoolUsed) {
    auto maxWidthToCopy = getMaxBlitWidth(rootDeviceEnvironment);
    auto maxHeightToCopy = getMaxBlitHeight(rootDeviceEnvironment, isSystemMemoryPoolUsed);
    auto xBlits = static_cast<size_t>(std::ceil(copySize.x / static_cast<double>(maxWidthToCopy)));
    auto yBlits = static_cast<size_t>(std::ceil(copySize.y / static_cast<double>(maxHeightToCopy)));
    auto zBlits = static_cast<size_t>(copySize.z);
    auto nBlits = xBlits * yBlits * zBlits;

    return nBlits;
}

template <typename GfxFamily>
size_t BlitCommandsHelper<GfxFamily>::getNumberOfBlitsForCopyPerRow(const Vec3<size_t> &copySize, const RootDeviceEnvironment &rootDeviceEnvironment, bool isSystemMemoryPoolUsed) {
    size_t xBlits = 0u;
    uint64_t width = 1;
    uint64_t height = 1;
    uint64_t sizeToBlit = copySize.x;

    while (sizeToBlit != 0) {
        if (sizeToBlit > getMaxBlitWidth(rootDeviceEnvironment)) {
            // dispatch 2D blit: maxBlitWidth x (1 .. maxBlitHeight)
            width = getMaxBlitWidth(rootDeviceEnvironment);
            height = std::min((sizeToBlit / width), getMaxBlitHeight(rootDeviceEnvironment, isSystemMemoryPoolUsed));

        } else {
            // dispatch 1D blt: (1 .. maxBlitWidth) x 1
            width = sizeToBlit;
            height = 1;
        }
        sizeToBlit -= (width * height);
        xBlits++;
    }

    auto yBlits = copySize.y;
    auto zBlits = copySize.z;
    auto nBlits = xBlits * yBlits * zBlits;

    return nBlits;
}

template <typename GfxFamily>
bool BlitCommandsHelper<GfxFamily>::preBlitCommandWARequired() {
    return false;
}

template <typename GfxFamily>
void BlitCommandsHelper<GfxFamily>::appendExtraMemoryProperties(typename GfxFamily::XY_BLOCK_COPY_BLT &blitCmd, const RootDeviceEnvironment &rootDeviceEnvironment) {}

template <typename GfxFamily>
void BlitCommandsHelper<GfxFamily>::appendExtraMemoryProperties(typename GfxFamily::XY_COLOR_BLT &blitCmd, const RootDeviceEnvironment &rootDeviceEnvironment) {}

template <typename GfxFamily>
void BlitCommandsHelper<GfxFamily>::encodeProfilingStartMmios(LinearStream &cmdStream, const TagNodeBase &timestampPacketNode) {
    auto timestampContextStartGpuAddress = TimestampPacketHelper::getContextStartGpuAddress(timestampPacketNode);
    auto timestampGlobalStartAddress = TimestampPacketHelper::getGlobalStartGpuAddress(timestampPacketNode);

    EncodeStoreMMIO<GfxFamily>::encode(cmdStream, GP_THREAD_TIME_REG_ADDRESS_OFFSET_LOW, timestampContextStartGpuAddress, false);
    EncodeStoreMMIO<GfxFamily>::encode(cmdStream, REG_GLOBAL_TIMESTAMP_LDW, timestampGlobalStartAddress, false);
}

template <typename GfxFamily>
void BlitCommandsHelper<GfxFamily>::encodeProfilingEndMmios(LinearStream &cmdStream, const TagNodeBase &timestampPacketNode) {
    auto timestampContextEndGpuAddress = TimestampPacketHelper::getContextEndGpuAddress(timestampPacketNode);
    auto timestampGlobalEndAddress = TimestampPacketHelper::getGlobalEndGpuAddress(timestampPacketNode);

    EncodeStoreMMIO<GfxFamily>::encode(cmdStream, GP_THREAD_TIME_REG_ADDRESS_OFFSET_LOW, timestampContextEndGpuAddress, false);
    EncodeStoreMMIO<GfxFamily>::encode(cmdStream, REG_GLOBAL_TIMESTAMP_LDW, timestampGlobalEndAddress, false);
}

template <typename GfxFamily>
size_t BlitCommandsHelper<GfxFamily>::getProfilingMmioCmdsSize() {
    return 4 * sizeof(typename GfxFamily::MI_STORE_REGISTER_MEM);
}

template <typename GfxFamily>
void BlitCommandsHelper<GfxFamily>::appendBaseAddressOffset(const BlitProperties &blitProperties, typename GfxFamily::XY_BLOCK_COPY_BLT &blitCmd, const uint32_t originalSliceIndex, const bool isSource) {}

template <typename GfxFamily>
void BlitCommandsHelper<GfxFamily>::encodeWa(LinearStream &cmdStream, const BlitProperties &blitProperties, uint32_t &latestSentBcsWaValue) {
}

template <typename GfxFamily>
size_t BlitCommandsHelper<GfxFamily>::getWaCmdsSize(const BlitPropertiesContainer &blitPropertiesContainer) {
    return 0;
}

} // namespace NEO
