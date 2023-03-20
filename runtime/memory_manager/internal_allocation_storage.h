/*
 * Copyright (C) 2018-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "runtime/memory_manager/allocations_list.h"

namespace NEO {
class CommandStreamReceiver;

class InternalAllocationStorage {
  public:
    MOCKABLE_VIRTUAL ~InternalAllocationStorage() = default;
    InternalAllocationStorage(CommandStreamReceiver &commandStreamReceiver);
    MOCKABLE_VIRTUAL void cleanAllocationList(TaskCountType waitTaskCount, uint32_t allocationUsage);
    void storeAllocation(std::unique_ptr<GraphicsAllocation> gfxAllocation, uint32_t allocationUsage);
    void storeAllocationWithTaskCount(std::unique_ptr<GraphicsAllocation> gfxAllocation, uint32_t allocationUsage, TaskCountType taskCount);
    std::unique_ptr<GraphicsAllocation> obtainReusableAllocation(size_t requiredSize, GraphicsAllocation::AllocationType allocationType);
    AllocationsList &getTemporaryAllocations() { return temporaryAllocations; }
    AllocationsList &getAllocationsForReuse() { return allocationsForReuse; }

  protected:
    void freeAllocationsList(TaskCountType waitTaskCount, AllocationsList &allocationsList);
    CommandStreamReceiver &commandStreamReceiver;

    AllocationsList temporaryAllocations;
    AllocationsList allocationsForReuse;
};
} // namespace NEO
