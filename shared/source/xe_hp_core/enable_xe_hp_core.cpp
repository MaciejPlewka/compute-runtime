/*
 * Copyright (C) 2021-2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/helpers/cache_policy_bdw_and_later.inl"
#include "shared/source/helpers/enable_product.inl"
#include "shared/source/os_interface/product_helper.h"
#include "shared/source/xe_hp_core/hw_cmds.h"

namespace NEO {

#ifdef SUPPORT_XE_HP_SDV
template struct L1CachePolicyHelper<IGFX_XE_HP_SDV>;
static EnableGfxProductHw<IGFX_XE_HP_SDV> enableGfxProductHwXEHP;

#endif
} // namespace NEO
