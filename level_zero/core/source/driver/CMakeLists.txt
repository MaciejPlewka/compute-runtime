#
# Copyright (C) 2020-2023 Intel Corporation
#
# SPDX-License-Identifier: MIT
#

target_sources(${L0_STATIC_LIB_NAME}
               PRIVATE
               ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
               ${CMAKE_CURRENT_SOURCE_DIR}/driver_handle.h
               ${CMAKE_CURRENT_SOURCE_DIR}/driver_handle_imp.cpp
               ${CMAKE_CURRENT_SOURCE_DIR}/driver_handle_imp.h
               ${CMAKE_CURRENT_SOURCE_DIR}/driver.cpp
               ${CMAKE_CURRENT_SOURCE_DIR}/driver.h
               ${CMAKE_CURRENT_SOURCE_DIR}/driver_imp.h
               ${CMAKE_CURRENT_SOURCE_DIR}/host_pointer_manager.cpp
               ${CMAKE_CURRENT_SOURCE_DIR}/host_pointer_manager.h
)

if(NOT DEFINED L0_DRIVER_VERSION)
  set(L0_DRIVER_VERSION 1)
endif()

configure_file(driver_version.h.in ${CMAKE_BINARY_DIR}/driver_version_l0.h) # Put Driver version into define
