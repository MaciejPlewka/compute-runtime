/*
 * Copyright (C) 2020-2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/helpers/gfx_core_helper.h"
#include "shared/source/os_interface/linux/i915.h"
#include "shared/source/os_interface/linux/memory_info.h"

#include "level_zero/sysman/source/linux/os_sysman_imp.h"
#include "level_zero/sysman/source/memory/linux/os_memory_imp.h"

namespace L0 {
namespace Sysman {

LinuxMemoryImp::LinuxMemoryImp(OsSysman *pOsSysman, ze_bool_t onSubdevice, uint32_t subdeviceId) : isSubdevice(onSubdevice), subdeviceId(subdeviceId) {
    pLinuxSysmanImp = static_cast<LinuxSysmanImp *>(pOsSysman);
    pDrm = pLinuxSysmanImp->getDrm();
    pDevice = pLinuxSysmanImp->getSysmanDeviceImp();
}

bool LinuxMemoryImp::isMemoryModuleSupported() {
    auto &gfxCoreHelper = pDevice->getRootDeviceEnvironment().getHelper<NEO::GfxCoreHelper>();
    return gfxCoreHelper.getEnableLocalMemory(pDevice->getHardwareInfo());
}

ze_result_t LinuxMemoryImp::getProperties(zes_mem_properties_t *pProperties) {
    pProperties->location = ZES_MEM_LOC_DEVICE;
    pProperties->type = ZES_MEM_TYPE_DDR;
    pProperties->onSubdevice = isSubdevice;
    pProperties->subdeviceId = subdeviceId;
    pProperties->busWidth = -1;
    pProperties->numChannels = -1;
    pProperties->physicalSize = 0;

    return ZE_RESULT_SUCCESS;
}

ze_result_t LinuxMemoryImp::getBandwidth(zes_mem_bandwidth_t *pBandwidth) {
    return ZE_RESULT_ERROR_UNSUPPORTED_FEATURE;
}

ze_result_t LinuxMemoryImp::getState(zes_mem_state_t *pState) {
    std::vector<NEO::MemoryRegion> deviceRegions;
    auto hwDeviceId = pLinuxSysmanImp->getSysmanHwDeviceId();
    hwDeviceId->openFileDescriptor();
    auto status = pDrm->queryMemoryInfo();
    hwDeviceId->closeFileDescriptor();

    if (status == false) {
        return ZE_RESULT_ERROR_UNSUPPORTED_FEATURE;
    }

    auto memoryInfo = pDrm->getMemoryInfo();
    if (!memoryInfo) {
        return ZE_RESULT_ERROR_UNSUPPORTED_FEATURE;
    }
    for (auto region : memoryInfo->getDrmRegionInfos()) {
        if (region.region.memoryClass == drm_i915_gem_memory_class::I915_MEMORY_CLASS_DEVICE) {
            deviceRegions.push_back(region);
        }
    }
    pState->free = deviceRegions[subdeviceId].unallocatedSize;
    pState->size = deviceRegions[subdeviceId].probedSize;
    pState->health = ZES_MEM_HEALTH_OK;

    return ZE_RESULT_SUCCESS;
}

OsMemory *OsMemory::create(OsSysman *pOsSysman, ze_bool_t onSubdevice, uint32_t subdeviceId) {
    LinuxMemoryImp *pLinuxMemoryImp = new LinuxMemoryImp(pOsSysman, onSubdevice, subdeviceId);
    return static_cast<OsMemory *>(pLinuxMemoryImp);
}

} // namespace Sysman
} // namespace L0
