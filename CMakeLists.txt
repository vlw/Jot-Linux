cmake_minimum_required(VERSION 3.21)
project(jot-linux VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# layer-shell-qt built against this Qt (scripts/build.sh does it).
set(JOT_THIRD_PARTY_PREFIX "${CMAKE_SOURCE_DIR}/third_party/install")
list(PREPEND CMAKE_PREFIX_PATH "${JOT_THIRD_PARTY_PREFIX}")

find_package(Qt6 6.12 REQUIRED COMPONENTS LinguistTools
    Core Gui Widgets Qml Quick QuickControls2 Multimedia Network WebSockets DBus Sql Svg)
find_package(LayerShellQt REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(EVDEV REQUIRED IMPORTED_TARGET libevdev)
pkg_check_modules(FLAC REQUIRED IMPORTED_TARGET flac)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

qt_standard_project_setup(REQUIRES 6.12)

# ---- Wayland protocol code
set(PROTO_OUT "${CMAKE_BINARY_DIR}/protocols")
file(MAKE_DIRECTORY "${PROTO_OUT}")
set(PROTO_SOURCES)
foreach(proto ext-data-control-v1 fake-input)
    set(xml "${CMAKE_SOURCE_DIR}/protocols/${proto}.xml")
    add_custom_command(
        OUTPUT "${PROTO_OUT}/${proto}-client-protocol.h" "${PROTO_OUT}/${proto}-protocol.c"
        COMMAND ${WAYLAND_SCANNER} client-header "${xml}" "${PROTO_OUT}/${proto}-client-protocol.h"
        COMMAND ${WAYLAND_SCANNER} private-code "${xml}" "${PROTO_OUT}/${proto}-protocol.c"
        DEPENDS "${xml}")
    list(APPEND PROTO_SOURCES "${PROTO_OUT}/${proto}-client-protocol.h" "${PROTO_OUT}/${proto}-protocol.c")
endforeach()

file(GLOB ICON_FILES CONFIGURE_DEPENDS RELATIVE "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/resources/icons/breeze/*.svg")
file(GLOB QML_FILES CONFIGURE_DEPENDS RELATIVE "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/resources/qml/*.qml")

qt_add_executable(jot-linux
    src/main.cpp
    src/core/Types.h
    src/core/DictationState.cpp
    src/core/HotkeyProcessor.cpp
    src/core/Settings.cpp
    src/core/SecretStore.cpp
    src/core/TextPipeline.cpp
    src/core/DictionaryStore.cpp
    src/core/FileLayout.cpp
    src/core/SessionMeta.cpp
    src/core/HistoryStore.cpp
    src/net/GeminiClient.cpp
    src/net/TranscriptionService.cpp
    src/net/LiveSession.cpp
    src/audio/AudioCapture.cpp
    src/platform/HotkeyEngine.cpp
    src/platform/WaylandIntegration.cpp
    src/platform/ActiveWindow.cpp
    src/platform/Inserter.cpp
    src/platform/MediaPause.cpp
    src/app/Earcons.cpp
    src/app/Language.cpp
    src/app/DictationCoordinator.cpp
    src/app/RetryQueue.cpp
    src/app/DictationController.cpp
    src/ui/PillHUD.cpp
    src/ui/TrayController.cpp
    src/ui/Backend.cpp
    src/ui/IconProvider.cpp
    ${PROTO_SOURCES}
)

qt_add_resources(jot-linux "assets"
    PREFIX "/"
    BASE "resources"
    FILES
        resources/sounds/start.wav resources/sounds/stop.wav resources/sounds/lock.wav
        resources/sounds/success.wav resources/sounds/cancel.wav resources/sounds/error.wav
        resources/sounds/celebration.wav
        resources/fonts/GoogleSansFlex-Regular.ttf resources/fonts/GoogleSansCode-VF.ttf
        resources/icons/jot.png
        ${ICON_FILES}
        ${QML_FILES}
)

# Translations: i18n/jot_ru.ts → :/i18n/jot_ru.qm (refresh sources with scripts/update-translations.sh)
qt_add_lrelease(jot-linux TS_FILES i18n/jot_ru.ts QM_FILES_OUTPUT_VARIABLE JOT_QM_FILES)
qt_add_resources(jot-linux "translations" PREFIX "/i18n" BASE "${CMAKE_CURRENT_BINARY_DIR}" FILES ${JOT_QM_FILES})

target_include_directories(jot-linux PRIVATE "${PROTO_OUT}" src)
target_compile_definitions(jot-linux PRIVATE
    JOT_VERSION="${PROJECT_VERSION}"
    JOT_PLUGIN_DIR="${CMAKE_BINARY_DIR}/plugins")
target_link_libraries(jot-linux PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia
    Qt6::Network Qt6::WebSockets Qt6::DBus Qt6::Sql Qt6::Svg
    LayerShellQt::Interface
    PkgConfig::WAYLAND PkgConfig::EVDEV PkgConfig::FLAC)

set_target_properties(jot-linux PROPERTIES
    BUILD_RPATH "\$ORIGIN/lib;${JOT_THIRD_PARTY_PREFIX}/lib/x86_64-linux-gnu;${Qt6_DIR}/../../")

# Ship the layer-shell shell-integration plugin next to the binary.
add_custom_command(TARGET jot-linux POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/plugins/wayland-shell-integration"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${JOT_THIRD_PARTY_PREFIX}/lib/x86_64-linux-gnu/plugins/wayland-shell-integration/liblayer-shell.so"
        "${CMAKE_BINARY_DIR}/plugins/wayland-shell-integration/liblayer-shell.so")
