/*
 * Copyright (C) 2020-2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "level_zero/core/source/fence/fence.h"

#include "shared/source/command_stream/command_stream_receiver.h"

#include "level_zero/core/source/cmdqueue/cmdqueue_imp.h"

namespace L0 {

Fence *Fence::create(CommandQueueImp *cmdQueue, const ze_fence_desc_t *desc) {
    auto fence = new Fence(cmdQueue);
    UNRECOVERABLE_IF(fence == nullptr);
    fence->reset(!!(desc->flags & ZE_FENCE_FLAG_SIGNALED));
    return fence;
}

ze_result_t Fence::queryStatus() {
    auto csr = cmdQueue->getCsr();
    csr->downloadAllocations();

    auto *hostAddr = csr->getTagAddress();

    return csr->testTaskCountReady(hostAddr, taskCount) ? ZE_RESULT_SUCCESS : ZE_RESULT_NOT_READY;
}

ze_result_t Fence::assignTaskCountFromCsr() {
    auto csr = cmdQueue->getCsr();
    taskCount = csr->peekTaskCount() + 1;
    return ZE_RESULT_SUCCESS;
}

ze_result_t Fence::reset(bool signaled) {
    if (signaled) {
        taskCount = 0;
    } else {
        taskCount = std::numeric_limits<uint32_t>::max();
    }
    return ZE_RESULT_SUCCESS;
}

ze_result_t Fence::hostSynchronize(uint64_t timeout) {
    std::chrono::high_resolution_clock::time_point waitStartTime, lastHangCheckTime, currentTime;
    uint64_t timeDiff = 0;
    ze_result_t ret = ZE_RESULT_NOT_READY;
    const auto csr = cmdQueue->getCsr();

    if (csr->getType() == NEO::CommandStreamReceiverType::CSR_AUB) {
        return ZE_RESULT_SUCCESS;
    }

    if (std::numeric_limits<uint32_t>::max() == taskCount) {
        return ZE_RESULT_NOT_READY;
    }

    waitStartTime = std::chrono::high_resolution_clock::now();
    lastHangCheckTime = waitStartTime;
    do {
        ret = queryStatus();
        if (ret == ZE_RESULT_SUCCESS) {
            cmdQueue->printKernelsPrintfOutput(false);
            cmdQueue->checkAssert();
            return ZE_RESULT_SUCCESS;
        }

        currentTime = std::chrono::high_resolution_clock::now();
        if (csr->checkGpuHangDetected(currentTime, lastHangCheckTime)) {
            cmdQueue->printKernelsPrintfOutput(true);
            cmdQueue->checkAssert();
            return ZE_RESULT_ERROR_DEVICE_LOST;
        }

        if (timeout == std::numeric_limits<uint64_t>::max()) {
            continue;
        } else if (timeout == 0) {
            break;
        }

        timeDiff = std::chrono::duration_cast<std::chrono::nanoseconds>(currentTime - waitStartTime).count();
    } while (timeDiff < timeout);

    return ret;
}

} // namespace L0
