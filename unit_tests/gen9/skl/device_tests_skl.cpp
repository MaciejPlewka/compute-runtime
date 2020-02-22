/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "test.h"

#include "fixtures/device_fixture.h"

using namespace NEO;

typedef Test<DeviceFixture> DeviceTest;

SKLTEST_F(DeviceTest, getSupportedClVersion21Device) {
    auto version = pDevice->getSupportedClVersion();
    EXPECT_EQ(21u, version);
}

SKLTEST_F(DeviceTest, givenSklDeviceWhenAskedForProflingTimerResolutionThen83IsReturned) {
    auto resolution = pDevice->getProfilingTimerResolution();
    EXPECT_DOUBLE_EQ(83.333, resolution);
}
