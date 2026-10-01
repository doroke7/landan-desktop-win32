cmake_minimum_required(VERSION 3.15)
project(CameraPicker VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 靜態連結 C++ 執行階段，使用者不需另外安裝 VC++ Redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(CameraPicker WIN32 main.cpp)
target_compile_definitions(CameraPicker PRIVATE UNICODE _UNICODE)
if(MSVC)
  target_compile_options(CameraPicker PRIVATE /utf-8 /W3)
endif()
target_link_libraries(CameraPicker PRIVATE
  mf mfplat mfreadwrite mfuuids ole32 user32 gdi32)

# ---- 安裝與打包 ----
install(TARGETS CameraPicker RUNTIME DESTINATION .)

set(CPACK_PACKAGE_NAME "CameraPicker")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_GENERATOR "ZIP")
# 若已安裝 NSIS，可改成 "ZIP;NSIS" 同時產生安裝程式
include(CPack)
