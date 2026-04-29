cmake_minimum_required(VERSION 3.24)

project(CatGatekeeperNext VERSION 0.1.0 LANGUAGES C CXX)

include(GNUInstallDirs)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CAT_GATEKEEPER_BUILD_OVERLAY "Build the Qt/FFmpeg overlay executable" ON)

if(NOT WIN32 OR CAT_GATEKEEPER_BUILD_OVERLAY)
  find_package(PkgConfig REQUIRED)
endif()

if(CAT_GATEKEEPER_BUILD_OVERLAY)
  pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale)
  find_package(Qt6 REQUIRED COMPONENTS Widgets)
endif()
if(NOT WIN32)
  pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
  if(CAT_GATEKEEPER_BUILD_OVERLAY)
    find_package(LayerShellQt REQUIRED)
  endif()
endif()

function(cat_gatekeeper_enable_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

set(CAT_GATEKEEPER_BUNDLED_ASSETS
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/manifest.conf"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/neko1.webm"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/neko2.webm"
)

foreach(asset IN LISTS CAT_GATEKEEPER_BUNDLED_ASSETS)
  if(NOT EXISTS "${asset}")
    message(FATAL_ERROR "Missing bundled asset ${asset}")
  endif()
endforeach()

set(CAT_GATEKEEPER_DAEMON_SOURCES
  src/main.c
  src/config.c
  src/control.c
  src/logind.c
  src/reminder.c
  src/idle.c
  src/log.c
)

add_executable(cat-gatekeeperd ${CAT_GATEKEEPER_DAEMON_SOURCES})
target_include_directories(cat-gatekeeperd PRIVATE src)
if(WIN32)
  target_compile_definitions(cat-gatekeeperd PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS _WIN32_WINNT=0x0601)
  target_link_libraries(cat-gatekeeperd PRIVATE advapi32 user32)
else()
  target_compile_definitions(cat-gatekeeperd PRIVATE _POSIX_C_SOURCE=200809L)
  target_link_libraries(cat-gatekeeperd PRIVATE PkgConfig::SYSTEMD)
endif()
cat_gatekeeper_enable_warnings(cat-gatekeeperd)

add_executable(cat-gatekeeperctl
  src/ctl.c
  src/control.c
  src/log.c
)

target_include_directories(cat-gatekeeperctl PRIVATE src)
if(WIN32)
  target_compile_definitions(cat-gatekeeperctl PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS _WIN32_WINNT=0x0601)
  target_link_libraries(cat-gatekeeperctl PRIVATE advapi32)
else()
  target_compile_definitions(cat-gatekeeperctl PRIVATE _POSIX_C_SOURCE=200809L)
endif()
cat_gatekeeper_enable_warnings(cat-gatekeeperctl)

set(CAT_GATEKEEPER_INSTALL_TARGETS
  cat-gatekeeperd
  cat-gatekeeperctl
)

if(CAT_GATEKEEPER_BUILD_OVERLAY)
  add_executable(cat-gatekeeper-overlay
    overlay/main.cpp
    overlay/OverlayWindow.cpp
    overlay/FrameSequence.cpp
    overlay/VideoFrameDecoder.cpp
    overlay/ProcessedAssets.cpp
    overlay/processed-assets.qrc
  )

  set_target_properties(cat-gatekeeper-overlay PROPERTIES AUTORCC ON)
  target_include_directories(cat-gatekeeper-overlay PRIVATE overlay)
  target_link_libraries(cat-gatekeeper-overlay PRIVATE Qt6::Widgets PkgConfig::LIBAV)
  if(WIN32)
    target_compile_definitions(cat-gatekeeper-overlay PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0601)
    target_link_libraries(cat-gatekeeper-overlay PRIVATE user32)
    set_target_properties(cat-gatekeeper-overlay PROPERTIES WIN32_EXECUTABLE TRUE)
  else()
    target_link_libraries(cat-gatekeeper-overlay PRIVATE LayerShellQt::Interface)
  endif()
  cat_gatekeeper_enable_warnings(cat-gatekeeper-overlay)
  list(APPEND CAT_GATEKEEPER_INSTALL_TARGETS cat-gatekeeper-overlay)
endif()

if(NOT WIN32)
  configure_file(
    systemd/cat-gatekeeper.service.in
    systemd/cat-gatekeeper.service
    @ONLY
  )
endif()

install(TARGETS ${CAT_GATEKEEPER_INSTALL_TARGETS}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(NOT WIN32)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/systemd/cat-gatekeeper.service
    DESTINATION ${CMAKE_INSTALL_DATADIR}/cat-gatekeeper/systemd
  )
endif()
