/*
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/helpers/completion_stamp.h"
#include "shared/source/helpers/hw_helper.h"

#include "opencl/source/aub/aub_helper_bdw_plus.inl"
#include "opencl/source/aub_mem_dump/aub_alloc_dump.inl"
#include "opencl/source/aub_mem_dump/aub_mem_dump.inl"

#include "aub_mapper.h"
#include "config.h"

namespace AubMemDump {

enum {
    device = DeviceValues::Icllp
};

// Instantiate these common template implementations.
template struct AubDump<Traits<device, 32>>;
template struct AubDump<Traits<device, 48>>;

template struct AubPageTableHelper32<Traits<device, 32>>;
template struct AubPageTableHelper64<Traits<device, 48>>;
} // namespace AubMemDump

namespace NEO {
using Family = ICLFamily;

static const AubMemDump::LrcaHelperRcs rcs(0x002000);
static const AubMemDump::LrcaHelperBcs bcs(0x022000);
static const AubMemDump::LrcaHelperVcs vcs(0x1c0000);
static const AubMemDump::LrcaHelperVecs vecs(0x1c8000);

const AubMemDump::LrcaHelper *const AUBFamilyMapper<Family>::csTraits[aub_stream::NUM_ENGINES] = {
    &rcs,
    &bcs,
    &vcs,
    &vecs};

const MMIOList AUBFamilyMapper<Family>::globalMMIO;

static const MMIOList mmioListRCS = {
    MMIOPair(0x000020d8, 0x00020000),
    MMIOPair(AubMemDump::computeRegisterOffset(rcs.mmioBase, 0x229c), 0xffff8280),
    MMIOPair(0x0000C800, 0x00000009),
    MMIOPair(0x0000C804, 0x00000038),
    MMIOPair(0x0000C808, 0x0000003B),
    MMIOPair(0x0000C80C, 0x00000039),
    MMIOPair(0x0000C810, 0x00000037),
    MMIOPair(0x0000C814, 0x00000039),
    MMIOPair(0x0000C818, 0x00000037),
    MMIOPair(0x0000C81C, 0x0000001B),
    MMIOPair(0x0000C820, 0x00060037),
    MMIOPair(0x0000C824, 0x00000032),
    MMIOPair(0x0000C828, 0x00000033),
    MMIOPair(0x0000C82C, 0x0000003B),
    MMIOPair(0x0000C8C0, 0x00000037),
    MMIOPair(0x0000E18C, 0x00200020),
};

static const MMIOList mmioListBCS = {
    MMIOPair(AubMemDump::computeRegisterOffset(bcs.mmioBase, 0x229c), 0xffff8280),
};

static const MMIOList mmioListVCS = {
    MMIOPair(AubMemDump::computeRegisterOffset(vcs.mmioBase, 0x229c), 0xffff8280),
};

static const MMIOList mmioListVECS = {
    MMIOPair(AubMemDump::computeRegisterOffset(vecs.mmioBase, 0x229c), 0xffff8280),
};

const MMIOList *AUBFamilyMapper<Family>::perEngineMMIO[aub_stream::NUM_ENGINES] = {
    &mmioListRCS,
    &mmioListBCS,
    &mmioListVCS,
    &mmioListVECS};

template class AubHelperHw<Family>;
} // namespace NEO

namespace AubAllocDump {
using namespace NEO;

template SurfaceInfo *getDumpSurfaceInfo<Family>(GraphicsAllocation &gfxAllocation, DumpFormat dumpFormat);

template uint32_t getImageSurfaceTypeFromGmmResourceType<Family>(GMM_RESOURCE_TYPE gmmResourceType);

template void dumpBufferInBinFormat<Family>(GraphicsAllocation &gfxAllocation, AubMemDump::AubFileStream *stream, uint32_t context);

template void dumpImageInBmpFormat<Family>(GraphicsAllocation &gfxAllocation, AubMemDump::AubFileStream *stream, uint32_t context);

template void dumpBufferInTreFormat<Family>(GraphicsAllocation &gfxAllocation, AubMemDump::AubFileStream *stream, uint32_t context);

template void dumpImageInTreFormat<Family>(GraphicsAllocation &gfxAllocation, AubMemDump::AubFileStream *stream, uint32_t context);

template void dumpAllocation<Family>(DumpFormat dumpFormat, GraphicsAllocation &gfxAllocation, AubMemDump::AubFileStream *stream, uint32_t context);
} // namespace AubAllocDump
