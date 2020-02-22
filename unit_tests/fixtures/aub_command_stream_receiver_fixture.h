/*
 * Copyright (C) 2019-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "fixtures/device_fixture.h"
#include "fixtures/mock_aub_center_fixture.h"

namespace NEO {
struct AubCommandStreamReceiverFixture : public DeviceFixture, MockAubCenterFixture {
    void SetUp() {
        DeviceFixture::SetUp();
        MockAubCenterFixture::SetUp();
        setMockAubCenter(*pDevice->getExecutionEnvironment()->rootDeviceEnvironments[0]);
    }
    void TearDown() {
        MockAubCenterFixture::TearDown();
        DeviceFixture::TearDown();
    }
};
} // namespace NEO
