cmake_minimum_required(VERSION 3.24)

project(CatGatekeeperNext VERSION 0.1.0 LANGUAGES C CXX)

include(GNUInstallDirs)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTORCC ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(LayerShellQt REQUIRED)

set(CAT_GATEKEEPER_BUNDLED_ASSETS
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/processed/manifest.conf"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/processed/neko1.webm"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/processed/neko2.webm"
)

foreach(asset IN LISTS CAT_GATEKEEPER_BUNDLED_ASSETS)
  if(NOT EXISTS "${asset}")
    message(FATAL_ERROR "Missing bundled asset ${asset}; run ./tools/preprocess-assets.sh")
  endif()
endforeach()

add_executable(cat-gatekeeperd
  src/main.c
  src/config.c
  src/control.c
  src/logind.c
  src/reminder.c
  src/idle.c
)

target_include_directories(cat-gatekeeperd PRIVATE src)
target_compile_definitions(cat-gatekeeperd PRIVATE
  _POSIX_C_SOURCE=200809L
)
target_link_libraries(cat-gatekeeperd PRIVATE PkgConfig::SYSTEMD)
target_compile_options(cat-gatekeeperd PRIVATE -Wall -Wextra -Wpedantic)

add_executable(cat-gatekeeperctl
  src/ctl.c
  src/control.c
)

target_include_directories(cat-gatekeeperctl PRIVATE src)
target_compile_definitions(cat-gatekeeperctl PRIVATE
  _POSIX_C_SOURCE=200809L
)
target_compile_options(cat-gatekeeperctl PRIVATE -Wall -Wextra -Wpedantic)

add_executable(cat-gatekeeper-overlay
  overlay/main.cpp
  overlay/OverlayWindow.cpp
  overlay/FrameSequence.cpp
  overlay/VideoFrameDecoder.cpp
  overlay/ProcessedAssets.cpp
  overlay/processed-assets.qrc
)

target_include_directories(cat-gatekeeper-overlay PRIVATE overlay)
target_link_libraries(cat-gatekeeper-overlay PRIVATE Qt6::Widgets LayerShellQt::Interface PkgConfig::LIBAV)
target_compile_options(cat-gatekeeper-overlay PRIVATE -Wall -Wextra -Wpedantic)

configure_file(
  systemd/cat-gatekeeper.service.in
  systemd/cat-gatekeeper.service
  @ONLY
)

install(TARGETS cat-gatekeeperd cat-gatekeeperctl cat-gatekeeper-overlay
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/systemd/cat-gatekeeper.service
  DESTINATION ${CMAKE_INSTALL_DATADIR}/cat-gatekeeper/systemd
)
