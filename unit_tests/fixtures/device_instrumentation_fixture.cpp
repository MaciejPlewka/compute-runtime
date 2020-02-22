/*
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "fixtures/device_instrumentation_fixture.h"

#include "helpers/execution_environment_helper.h"
#include "mocks/mock_device.h"

namespace NEO {
void DeviceInstrumentationFixture::SetUp(bool instrumentation) {
    ExecutionEnvironment *executionEnvironment = getExecutionEnvironmentImpl(hwInfo, 1);
    hwInfo->capabilityTable.instrumentationEnabled = instrumentation;
    device = std::make_unique<ClDevice>(*Device::create<RootDevice>(executionEnvironment, 0), platform());
}

} // namespace NEO
