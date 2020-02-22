/*
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "core/helpers/preamble.h"
#include "core/memory_manager/graphics_allocation.h"
#include "core/memory_manager/memory_constants.h"
#include "opencl/source/built_ins/built_ins.h"
#include "opencl/source/sharings/gl/gl_buffer.h"

#include "command_queue/enqueue_fixture.h"
#include "fixtures/hello_world_fixture.h"
#include "fixtures/memory_management_fixture.h"
#include "helpers/hw_parse.h"
#include "mocks/gl/windows/mock_gl_sharing_windows.h"
#include "mocks/mock_buffer.h"
#include "mocks/mock_csr.h"
#include "mocks/mock_submissions_aggregator.h"

using namespace NEO;

typedef HelloWorldFixture<HelloWorldFixtureFactory> EnqueueKernelFixture;
typedef Test<EnqueueKernelFixture> EnqueueKernelTest;

TEST_F(EnqueueKernelTest, givenKernelWithSharedObjArgsWhenEnqueueIsCalledThenResetPatchAddress) {
    auto nonSharedBuffer = new MockBuffer;
    MockGlSharing glSharing;
    glSharing.uploadDataToBufferInfo(1, 0);
    pContext->setSharingFunctions(glSharing.sharingFunctions.release());
    auto retVal = CL_SUCCESS;
    auto sharedBuffer = GlBuffer::createSharedGlBuffer(pContext, CL_MEM_READ_WRITE, 1, &retVal);
    auto sharedMem = static_cast<cl_mem>(sharedBuffer);
    auto nonSharedMem = static_cast<cl_mem>(nonSharedBuffer);

    pKernel->setArg(0, sizeof(cl_mem *), &sharedMem);
    pKernel->setArg(1, sizeof(cl_mem *), &nonSharedMem);
    EXPECT_TRUE(pKernel->isUsingSharedObjArgs());
    auto &kernelInfo = pKernel->getKernelInfo();

    auto pKernelArg =
        (uint32_t *)(pKernel->getCrossThreadData() + kernelInfo.kernelArgInfo[0].kernelArgPatchInfoVector[0].crossthreadOffset);

    auto address1 = static_cast<uint64_t>(*pKernelArg);
    auto sharedBufferGpuAddress =
        pKernel->isBuiltIn ? sharedBuffer->getGraphicsAllocation()->getGpuAddress()
                           : sharedBuffer->getGraphicsAllocation()->getGpuAddressToPatch();
    EXPECT_EQ(sharedBufferGpuAddress, address1);

    // update address
    glSharing.uploadDataToBufferInfo(1, 1);
    pCmdQ->enqueueAcquireSharedObjects(1, &sharedMem, 0, nullptr, nullptr, CL_COMMAND_ACQUIRE_GL_OBJECTS);

    callOneWorkItemNDRKernel();

    auto address2 = static_cast<uint64_t>(*pKernelArg);
    EXPECT_NE(address1, address2);
    sharedBufferGpuAddress =
        pKernel->isBuiltIn ? sharedBuffer->getGraphicsAllocation()->getGpuAddress()
                           : sharedBuffer->getGraphicsAllocation()->getGpuAddressToPatch();
    EXPECT_EQ(sharedBufferGpuAddress, address2);

    delete sharedBuffer;
    delete nonSharedBuffer;
}
