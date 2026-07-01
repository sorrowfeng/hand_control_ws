# Default SDK profile for the in-repository development assets.
# Customer delivery packages overwrite this file from packaging/vendors.json.

if(NOT HC_SDK_LIB_NAME)
  set(HC_SDK_LIB_NAME "LHandProLib")
endif()

if(NOT HC_SDK_HEADER)
  set(HC_SDK_HEADER "LHandProLib/LHandProLib.hpp")
endif()

if(NOT HC_SDK_NS)
  set(HC_SDK_NS "lhplib")
endif()

if(NOT HC_SDK_CLASS)
  set(HC_SDK_CLASS "LHandProLib")
endif()
