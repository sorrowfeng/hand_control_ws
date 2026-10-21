cmake_minimum_required(VERSION 3.8)
project(hand_control_service)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra) # -Wpedantic
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(hand_control_interfaces REQUIRED)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  # the following line skips the linter which checks for copyrights
  # comment the line when a copyright and license is added to all source files
  set(ament_cmake_copyright_FOUND TRUE)
  # the following line skips cpplint (only works in a git repo)
  # comment the line when this package is in a git repo and when
  # a copyright and license is added to all source files
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()


set(HAND_CONTROL_THIRDPARTY_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty"
  CACHE PATH "Path to the selected customer thirdparty SDK directory"
)
set(HAND_CONTROL_SDK_PROFILE
  "${CMAKE_CURRENT_SOURCE_DIR}/config/sdk_profile.cmake"
  CACHE FILEPATH "Path to the selected customer SDK profile"
)

set(HC_SDK_LIB_NAME "" CACHE STRING "SDK library name to link")
set(HC_SDK_HEADER "" CACHE STRING "SDK C++ header included by the service wrapper")
set(HC_SDK_NS "" CACHE STRING "SDK C++ namespace used by the service wrapper")
set(HC_SDK_CLASS "" CACHE STRING "SDK C++ class used by the service wrapper")

if(EXISTS "${HAND_CONTROL_SDK_PROFILE}")
  include("${HAND_CONTROL_SDK_PROFILE}")
endif()

foreach(required_sdk_setting
    HC_SDK_LIB_NAME
    HC_SDK_HEADER
    HC_SDK_NS
    HC_SDK_CLASS)
  if(NOT ${required_sdk_setting})
    message(FATAL_ERROR
      "${required_sdk_setting} is not set. Provide ${HAND_CONTROL_SDK_PROFILE} "
      "or pass -D${required_sdk_setting}=...")
  endif()
endforeach()

set(THIRDPARTY_DIR "${HAND_CONTROL_THIRDPARTY_DIR}")

find_library(HAND_CONTROL_SDK_LIB NAMES ${HC_SDK_LIB_NAME}
  PATHS ${THIRDPARTY_DIR}/lib
  REQUIRED
  NO_DEFAULT_PATH
)
# ---------------------------------------------------------------------------
# CANFD backend selection (default: gsusb-canfd; see src/CANFDMaster.h).
#
# Only two backends are supported:
#   - gsusb-canfd: userspace libusb gs_usb, default and cross-platform
#   - SocketCAN:   Linux kernel CAN interface, opt-in
#
# Selecting SocketCAN automatically releases the default gsusb slot. The header
# macros in CANFDMaster.h remain authoritative when editing the file directly.
# ---------------------------------------------------------------------------
option(HAND_CONTROL_USE_GSUSB_CANFD "CANFD backend: gsusb-canfd (userspace libusb gs_usb), default" ON)
option(HAND_CONTROL_USE_SOCKETCAN   "CANFD backend: Linux SocketCAN" OFF)

if(HAND_CONTROL_USE_SOCKETCAN AND HAND_CONTROL_USE_GSUSB_CANFD)
  message(STATUS "CANFD: SocketCAN selected, disabling default HAND_CONTROL_USE_GSUSB_CANFD")
  # 同步写回 cache，避免 CMakeCache 里两个后端都显示 ON 造成误判。
  set(HAND_CONTROL_USE_GSUSB_CANFD OFF CACHE BOOL
      "CANFD backend: gsusb-canfd (userspace libusb gs_usb), default" FORCE)
endif()

if(NOT HAND_CONTROL_USE_GSUSB_CANFD AND NOT HAND_CONTROL_USE_SOCKETCAN)
  message(FATAL_ERROR
    "No CANFD backend available: keep HAND_CONTROL_USE_GSUSB_CANFD=ON "
    "or enable HAND_CONTROL_USE_SOCKETCAN (Linux only)")
endif()
if(HAND_CONTROL_USE_SOCKETCAN AND (WIN32 OR APPLE))
  message(FATAL_ERROR "HAND_CONTROL_USE_SOCKETCAN is Linux-only; use gsusb-canfd on Windows/macOS")
endif()

# gsusb-canfd: build its static library only, no tools/tests/examples.
if(HAND_CONTROL_USE_GSUSB_CANFD)
  set(CANFD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(CANFD_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(CANFD_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(CANFD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  add_subdirectory(thirdparty/gsusb-canfd)
endif()

# 导出该库给其他包使用
ament_export_libraries(${HAND_CONTROL_SDK_LIB})
ament_export_include_directories(${THIRDPARTY_DIR}/include)

set(SDK_ADAPTER_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
configure_file(
  cmake/sdk_adapter.hpp.in
  ${SDK_ADAPTER_INCLUDE_DIR}/sdk_adapter.hpp
  @ONLY
)


add_subdirectory(thirdparty/SOEM)

# 添加执行文件
add_executable(hand_control_service
  src/service_node.cpp
  src/hand_control_service.hpp
  src/hand_control_service.cpp
  src/EthercatMaster.cpp
  src/CANFDMaster.cpp
  src/SerialPort.cpp
)

ament_target_dependencies(hand_control_service rclcpp hand_control_interfaces)

# 导出头文件路径
set(HAND_CONTROL_CANFD_LINK_LIBS)
if(HAND_CONTROL_USE_GSUSB_CANFD)
  list(APPEND HAND_CONTROL_CANFD_LINK_LIBS canfd)
endif()
target_link_libraries(hand_control_service ${HAND_CONTROL_SDK_LIB} soem
  ${HAND_CONTROL_CANFD_LINK_LIBS})
target_include_directories(hand_control_service PUBLIC
    $<BUILD_INTERFACE:${THIRDPARTY_DIR}/include>
)
target_include_directories(hand_control_service PRIVATE
    ${SDK_ADAPTER_INCLUDE_DIR}
)
target_compile_definitions(hand_control_service PRIVATE
  "HC_SDK_NS=${HC_SDK_NS}"
  "HC_SDK_CLASS=${HC_SDK_CLASS}"
)

if(HAND_CONTROL_USE_GSUSB_CANFD)
  target_compile_definitions(hand_control_service PRIVATE HAND_CONTROL_CANFD_USE_GSUSB=1)
elseif(HAND_CONTROL_USE_SOCKETCAN)
  target_compile_definitions(hand_control_service PRIVATE HAND_CONTROL_CANFD_USE_SOCKETCAN=1)
endif()

# 安装可执行文件
install(TARGETS 
  hand_control_service
  DESTINATION lib/${PROJECT_NAME}
)

# 安装 .so 文件
install(
  DIRECTORY ${THIRDPARTY_DIR}/lib/
  DESTINATION lib
  USE_SOURCE_PERMISSIONS  # 保持文件权限
  PATTERN "*.so*"  # 匹配所有 .so 文件
)

# 安装 launch 文件
install(DIRECTORY
  launch/
  DESTINATION share/${PROJECT_NAME}/launch
)

install(DIRECTORY
  config/
  DESTINATION share/${PROJECT_NAME}/config
)

ament_package()
