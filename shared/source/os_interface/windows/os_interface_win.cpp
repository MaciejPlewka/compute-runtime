/*
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/execution_environment/root_device_environment.h"
#include "shared/source/helpers/debug_helpers.h"
#include "shared/source/os_interface/os_interface.h"
#include "shared/source/os_interface/windows/wddm/wddm.h"
#include "shared/source/os_interface/windows/wddm_memory_operations_handler.h"

namespace NEO {

bool OSInterface::osEnabled64kbPages = true;
bool OSInterface::newResourceImplicitFlush = false;
bool OSInterface::gpuIdleImplicitFlush = false;
bool OSInterface::requiresSupportForWddmTrimNotification = true;

bool OSInterface::isDebugAttachAvailable() const {
    return false;
}

bool RootDeviceEnvironment::initOsInterface(std::unique_ptr<HwDeviceId> &&hwDeviceId, uint32_t rootDeviceIndex) {
    UNRECOVERABLE_IF(hwDeviceId->getDriverModelType() != DriverModelType::WDDM);
    auto hwDeviceIdWddm = std::unique_ptr<HwDeviceIdWddm>(reinterpret_cast<HwDeviceIdWddm *>(hwDeviceId.release()));
    auto wddm(Wddm::createWddm(std::move(hwDeviceIdWddm), *this));
    if (!wddm->init()) {
        return false;
    }
    memoryOperationsInterface = std::make_unique<WddmMemoryOperationsHandler>(wddm);
    return true;
}
} // namespace NEO
