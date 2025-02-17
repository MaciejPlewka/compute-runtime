/*
 * Copyright (C) 2018-2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/os_interface/linux/os_context_linux.h"

#include "shared/source/debug_settings/debug_settings_manager.h"
#include "shared/source/execution_environment/execution_environment.h"
#include "shared/source/execution_environment/root_device_environment.h"
#include "shared/source/helpers/engine_node_helper.h"
#include "shared/source/helpers/hw_info.h"
#include "shared/source/os_interface/linux/drm_neo.h"
#include "shared/source/os_interface/linux/ioctl_helper.h"
#include "shared/source/os_interface/os_context.h"
#include "shared/source/os_interface/os_interface.h"
#include "shared/source/os_interface/product_helper.h"

namespace NEO {

OsContext *OsContextLinux::create(OSInterface *osInterface, uint32_t rootDeviceIndex, uint32_t contextId, const EngineDescriptor &engineDescriptor) {
    if (osInterface) {
        return new OsContextLinux(*osInterface->getDriverModel()->as<Drm>(), rootDeviceIndex, contextId, engineDescriptor);
    }
    return new OsContext(rootDeviceIndex, contextId, engineDescriptor);
}

OsContextLinux::OsContextLinux(Drm &drm, uint32_t rootDeviceIndex, uint32_t contextId, const EngineDescriptor &engineDescriptor)
    : OsContext(rootDeviceIndex, contextId, engineDescriptor),
      drm(drm) {}

bool OsContextLinux::initializeContext() {
    auto hwInfo = drm.getRootDeviceEnvironment().getHardwareInfo();
    auto defaultEngineType = getChosenEngineType(*hwInfo);

    if (engineType == defaultEngineType && !isLowPriority() && !isInternalEngine()) {
        this->setDefaultContext(true);
    }

    bool submitDirect = false;
    this->isDirectSubmissionAvailable(*drm.getRootDeviceEnvironment().getHardwareInfo(), submitDirect);

    if (drm.isPerContextVMRequired()) {
        this->drmVmIds.resize(deviceBitfield.size(), 0);
    }

    for (auto deviceIndex = 0u; deviceIndex < deviceBitfield.size(); deviceIndex++) {
        if (deviceBitfield.test(deviceIndex)) {
            auto drmVmId = drm.getVirtualMemoryAddressSpace(deviceIndex);
            auto drmContextId = drm.getIoctlHelper()->createDrmContext(drm, *this, drmVmId, deviceIndex);
            if (drmContextId < 0) {
                return false;
            }

            this->drmContextIds.push_back(drmContextId);

            if (drm.isPerContextVMRequired()) {
                [[maybe_unused]] auto ret = drm.queryVmId(drmContextId, drmVmId);
                DEBUG_BREAK_IF(drmVmId == 0);
                DEBUG_BREAK_IF(ret != 0);

                UNRECOVERABLE_IF(this->drmVmIds.size() <= deviceIndex);
                this->drmVmIds[deviceIndex] = drmVmId;
            }
        }
    }
    return true;
}

bool OsContextLinux::isDirectSubmissionSupported(const HardwareInfo &hwInfo) const {
    auto &productHelper = this->getDrm().getRootDeviceEnvironment().getHelper<ProductHelper>();

    return this->getDrm().isVmBindAvailable() && productHelper.isDirectSubmissionSupported(hwInfo);
}

Drm &OsContextLinux::getDrm() const {
    return this->drm;
}

void OsContextLinux::waitForPagingFence() {
    for (auto drmIterator = 0u; drmIterator < this->deviceBitfield.size(); drmIterator++) {
        if (this->deviceBitfield.test(drmIterator)) {
            drm.waitForBind(drmIterator);
        }
    }
}

void OsContextLinux::reInitializeContext() {}

OsContextLinux::~OsContextLinux() {
    if (contextInitialized) {
        for (auto drmContextId : drmContextIds) {
            drm.destroyDrmContext(drmContextId);
        }
    }
    drmContextIds.clear();
    drmVmIds.clear();
}
} // namespace NEO
