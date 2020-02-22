/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "core/os_interface/linux/os_interface.h"
#include "opencl/source/command_queue/command_queue_hw.h"
#include "test.h"

#include "fixtures/ult_command_stream_receiver_fixture.h"
#include "mocks/linux/mock_drm_command_stream_receiver.h"
#include "mocks/linux/mock_drm_memory_manager.h"
#include "mocks/mock_context.h"
#include "os_interface/linux/drm_mock.h"

using namespace NEO;

struct clCreateCommandQueueWithPropertiesLinux : public UltCommandStreamReceiverTest {
    void SetUp() override {
        UltCommandStreamReceiverTest::SetUp();
        ExecutionEnvironment *executionEnvironment = new MockExecutionEnvironment();
        executionEnvironment->prepareRootDeviceEnvironments(1);
        auto osInterface = new OSInterface();
        osInterface->get()->setDrm(drm);
        executionEnvironment->rootDeviceEnvironments[0]->osInterface.reset(osInterface);
        executionEnvironment->memoryManager.reset(new TestedDrmMemoryManager(*executionEnvironment));
        mdevice = std::make_unique<MockClDevice>(MockDevice::create<MockDevice>(executionEnvironment, 0u));

        clDevice = mdevice.get();
        retVal = CL_SUCCESS;
        context = std::unique_ptr<Context>(Context::create<MockContext>(nullptr, ClDeviceVector(&clDevice, 1), nullptr, nullptr, retVal));
    }
    void TearDown() override {
        UltCommandStreamReceiverTest::TearDown();
    }
    DrmMock *drm = new DrmMock();
    std::unique_ptr<MockClDevice> mdevice = nullptr;
    std::unique_ptr<Context> context;
    cl_device_id clDevice;
    cl_int retVal;
};

namespace ULT {

TEST_F(clCreateCommandQueueWithPropertiesLinux, givenUnPossiblePropertiesWithClQueueSliceCountWhenCreateCommandQueueThenQueueNotCreated) {
    uint64_t newSliceCount = 1;
    size_t maxSliceCount;
    clGetDeviceInfo(clDevice, CL_DEVICE_SLICE_COUNT_INTEL, sizeof(size_t), &maxSliceCount, nullptr);

    newSliceCount = maxSliceCount + 1;

    cl_queue_properties properties[] = {CL_QUEUE_SLICE_COUNT_INTEL, newSliceCount, 0};

    cl_command_queue cmdQ = clCreateCommandQueueWithProperties(context.get(), clDevice, properties, &retVal);

    EXPECT_EQ(nullptr, cmdQ);
    EXPECT_EQ(CL_INVALID_QUEUE_PROPERTIES, retVal);
}

TEST_F(clCreateCommandQueueWithPropertiesLinux, givenZeroWithClQueueSliceCountWhenCreateCommandQueueThenSliceCountEqualDefaultSliceCount) {

    uint64_t newSliceCount = 0;

    cl_queue_properties properties[] = {CL_QUEUE_SLICE_COUNT_INTEL, newSliceCount, 0};

    cl_command_queue cmdQ = clCreateCommandQueueWithProperties(context.get(), clDevice, properties, &retVal);

    ASSERT_NE(nullptr, cmdQ);
    ASSERT_EQ(CL_SUCCESS, retVal);

    auto commandQueue = castToObject<CommandQueue>(cmdQ);
    EXPECT_EQ(commandQueue->getSliceCount(), QueueSliceCount::defaultSliceCount);

    retVal = clReleaseCommandQueue(cmdQ);
    EXPECT_EQ(CL_SUCCESS, retVal);
}

TEST_F(clCreateCommandQueueWithPropertiesLinux, givenPossiblePropertiesWithClQueueSliceCountWhenCreateCommandQueueThenSliceCountIsSet) {

    uint64_t newSliceCount = 1;
    size_t maxSliceCount;
    clGetDeviceInfo(clDevice, CL_DEVICE_SLICE_COUNT_INTEL, sizeof(size_t), &maxSliceCount, nullptr);
    if (maxSliceCount > 1) {
        newSliceCount = maxSliceCount - 1;
    }

    cl_queue_properties properties[] = {CL_QUEUE_SLICE_COUNT_INTEL, newSliceCount, 0};

    cl_command_queue cmdQ = clCreateCommandQueueWithProperties(context.get(), clDevice, properties, &retVal);

    ASSERT_NE(nullptr, cmdQ);
    ASSERT_EQ(CL_SUCCESS, retVal);

    auto commandQueue = castToObject<CommandQueue>(cmdQ);
    EXPECT_EQ(commandQueue->getSliceCount(), newSliceCount);

    retVal = clReleaseCommandQueue(cmdQ);
    EXPECT_EQ(CL_SUCCESS, retVal);
}

HWTEST_F(clCreateCommandQueueWithPropertiesLinux, givenPropertiesWithClQueueSliceCountWhenCreateCommandQueueThenCallFlushTaskAndSliceCountIsSet) {
    uint64_t newSliceCount = 1;
    size_t maxSliceCount;
    clGetDeviceInfo(clDevice, CL_DEVICE_SLICE_COUNT_INTEL, sizeof(size_t), &maxSliceCount, nullptr);
    if (maxSliceCount > 1) {
        newSliceCount = maxSliceCount - 1;
    }

    cl_queue_properties properties[] = {CL_QUEUE_SLICE_COUNT_INTEL, newSliceCount, 0};

    auto mockCsr = new TestedDrmCommandStreamReceiver<FamilyType>(*mdevice->executionEnvironment);
    mdevice->resetCommandStreamReceiver(mockCsr);

    cl_command_queue cmdQ = clCreateCommandQueueWithProperties(context.get(), clDevice, properties, &retVal);

    ASSERT_NE(nullptr, cmdQ);
    ASSERT_EQ(CL_SUCCESS, retVal);

    auto commandQueue = castToObject<CommandQueueHw<FamilyType>>(cmdQ);
    auto &commandStream = commandQueue->getCS(1024u);

    DispatchFlags dispatchFlags = DispatchFlagsHelper::createDefaultDispatchFlags();
    dispatchFlags.sliceCount = commandQueue->getSliceCount();

    mockCsr->flushTask(commandStream,
                       0u,
                       dsh,
                       ioh,
                       ssh,
                       taskLevel,
                       dispatchFlags,
                       mdevice->getDevice());
    auto expectedSliceMask = drm->getSliceMask(newSliceCount);
    EXPECT_EQ(expectedSliceMask, drm->storedParamSseu);
    drm_i915_gem_context_param_sseu sseu = {};
    EXPECT_EQ(0, drm->getQueueSliceCount(&sseu));
    EXPECT_EQ(expectedSliceMask, sseu.slice_mask);
    EXPECT_EQ(newSliceCount, mockCsr->lastSentSliceCount);

    retVal = clReleaseCommandQueue(cmdQ);
    EXPECT_EQ(CL_SUCCESS, retVal);
}

HWTEST_F(clCreateCommandQueueWithPropertiesLinux, givenSameSliceCountAsRecentlySetWhenCreateCommandQueueThenSetQueueSliceCountNotCalled) {
    uint64_t newSliceCount = 1;
    size_t maxSliceCount;

    clGetDeviceInfo(clDevice, CL_DEVICE_SLICE_COUNT_INTEL, sizeof(size_t), &maxSliceCount, nullptr);
    if (maxSliceCount > 1) {
        newSliceCount = maxSliceCount - 1;
    }

    cl_queue_properties properties[] = {CL_QUEUE_SLICE_COUNT_INTEL, newSliceCount, 0};

    auto mockCsr = new TestedDrmCommandStreamReceiver<FamilyType>(*mdevice->executionEnvironment);
    mdevice->resetCommandStreamReceiver(mockCsr);

    cl_command_queue cmdQ = clCreateCommandQueueWithProperties(context.get(), clDevice, properties, &retVal);

    ASSERT_NE(nullptr, cmdQ);
    ASSERT_EQ(CL_SUCCESS, retVal);

    auto commandQueue = castToObject<CommandQueueHw<FamilyType>>(cmdQ);
    auto &commandStream = commandQueue->getCS(1024u);

    DispatchFlags dispatchFlags = DispatchFlagsHelper::createDefaultDispatchFlags();
    dispatchFlags.sliceCount = commandQueue->getSliceCount();

    mockCsr->lastSentSliceCount = newSliceCount;
    mockCsr->flushTask(commandStream,
                       0u,
                       dsh,
                       ioh,
                       ssh,
                       taskLevel,
                       dispatchFlags,
                       mdevice->getDevice());
    auto expectedSliceMask = drm->getSliceMask(newSliceCount);
    EXPECT_NE(expectedSliceMask, drm->storedParamSseu);
    drm_i915_gem_context_param_sseu sseu = {};
    EXPECT_EQ(0, drm->getQueueSliceCount(&sseu));
    EXPECT_NE(expectedSliceMask, sseu.slice_mask);

    retVal = clReleaseCommandQueue(cmdQ);
    EXPECT_EQ(CL_SUCCESS, retVal);
}

HWTEST_F(clCreateCommandQueueWithPropertiesLinux, givenPropertiesWithClQueueSliceCountWhenCreateCommandQueueThenSetReturnFalseAndLastSliceCountNotModify) {
    uint64_t newSliceCount = 1;
    size_t maxSliceCount;
    clGetDeviceInfo(clDevice, CL_DEVICE_SLICE_COUNT_INTEL, sizeof(size_t), &maxSliceCount, nullptr);
    if (maxSliceCount > 1) {
        newSliceCount = maxSliceCount - 1;
    }

    cl_queue_properties properties[] = {CL_QUEUE_SLICE_COUNT_INTEL, newSliceCount, 0};

    auto mockCsr = new TestedDrmCommandStreamReceiver<FamilyType>(*mdevice->executionEnvironment);
    mdevice->resetCommandStreamReceiver(mockCsr);

    cl_command_queue cmdQ = clCreateCommandQueueWithProperties(context.get(), clDevice, properties, &retVal);

    ASSERT_NE(nullptr, cmdQ);
    ASSERT_EQ(CL_SUCCESS, retVal);

    auto commandQueue = castToObject<CommandQueueHw<FamilyType>>(cmdQ);
    auto &commandStream = commandQueue->getCS(1024u);

    DispatchFlags dispatchFlags = DispatchFlagsHelper::createDefaultDispatchFlags();
    dispatchFlags.sliceCount = commandQueue->getSliceCount();
    drm->StoredRetValForSetSSEU = -1;

    auto lastSliceCountBeforeFlushTask = mockCsr->lastSentSliceCount;
    mockCsr->flushTask(commandStream,
                       0u,
                       dsh,
                       ioh,
                       ssh,
                       taskLevel,
                       dispatchFlags,
                       mdevice->getDevice());

    EXPECT_NE(newSliceCount, mockCsr->lastSentSliceCount);
    EXPECT_EQ(lastSliceCountBeforeFlushTask, mockCsr->lastSentSliceCount);

    retVal = clReleaseCommandQueue(cmdQ);
    EXPECT_EQ(CL_SUCCESS, retVal);
}

} // namespace ULT
