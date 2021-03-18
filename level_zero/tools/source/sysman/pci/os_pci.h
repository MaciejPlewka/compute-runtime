/*
 * Copyright (C) 2019-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "level_zero/tools/source/sysman/os_sysman.h"
#include <level_zero/zet_api.h>

#include <string>
#include <vector>

namespace L0 {
int64_t convertPcieSpeedFromGTsToBs(double maxLinkSpeedInGt);
int32_t convertLinkSpeedToPciGen(double speed);
double convertPciGenToLinkSpeed(uint32_t gen);
class OsPci {
  public:
    virtual ze_result_t getPciBdf(std::string &bdf) = 0;
    virtual ze_result_t getMaxLinkSpeed(double &maxLinkSpeed) = 0;
    virtual ze_result_t getMaxLinkWidth(int32_t &maxLinkWidth) = 0;
    virtual ze_result_t getState(zes_pci_state_t *state) = 0;
    virtual ze_result_t getProperties(zes_pci_properties_t *properties) = 0;
    virtual ze_result_t initializeBarProperties(std::vector<zes_pci_bar_properties_t *> &pBarProperties) = 0;
    static OsPci *create(OsSysman *pOsSysman);
    virtual ~OsPci() = default;
};

} // namespace L0
