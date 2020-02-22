#
# Copyright (C) 2019-2020 Intel Corporation
#
# SPDX-License-Identifier: MIT
#

target_sources(${NEO_STATIC_LIB_NAME} PRIVATE ${NEO_SOURCE_DIR}/opencl/source/core_files.cmake)

append_sources_from_properties(NEO_CORE_SOURCES
  NEO_CORE_BUILT_INS
  NEO_CORE_COMMAND_STREAM
  NEO_CORE_COMMANDS
  NEO_CORE_DEVICE
  NEO_CORE_COMMAND_ENCODERS
  NEO_CORE_GMM_HELPER
  NEO_CORE_KERNEL
  NEO_CORE_MEMORY_MANAGER
  NEO_CORE_OS_INTERFACE
  NEO_CORE_PAGE_FAULT_MANAGER
  NEO_CORE_PROGRAM
  NEO_CORE_IMAGE
)

append_sources_from_properties(NEO_CORE_SOURCES_WINDOWS
  NEO_CORE_GMM_HELPER_WINDOWS
  NEO_CORE_OS_INTERFACE_WINDOWS
  NEO_CORE_PAGE_FAULT_MANAGER_WINDOWS
  NEO_CORE_SKU_INFO_WINDOWS
  NEO_CORE_SRCS_HELPERS_WINDOWS
  NEO_CORE_UTILITIES_WINDOWS
)

append_sources_from_properties(NEO_CORE_SOURCES_LINUX
  NEO_CORE_OS_INTERFACE_LINUX
  NEO_CORE_PAGE_FAULT_MANAGER_LINUX
  NEO_CORE_UTILITIES_LINUX
)

set_property(GLOBAL PROPERTY NEO_CORE_SOURCES ${NEO_CORE_SOURCES})
set_property(GLOBAL PROPERTY NEO_CORE_SOURCES_WINDOWS ${NEO_CORE_SOURCES_WINDOWS})
set_property(GLOBAL PROPERTY NEO_CORE_SOURCES_LINUX ${NEO_CORE_SOURCES_LINUX})

target_sources(${NEO_STATIC_LIB_NAME} PRIVATE ${NEO_CORE_SOURCES})
if(WIN32)
  target_sources(${NEO_STATIC_LIB_NAME} PRIVATE ${NEO_CORE_SOURCES_WINDOWS})
else()
  target_sources(${NEO_STATIC_LIB_NAME} PRIVATE ${NEO_CORE_SOURCES_LINUX})
endif()
