cmake_minimum_required(VERSION 3.21)
project(MkPCApp LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "MkPCApp targets Windows only (Win32/DirectX/registry APIs).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# --- Dear ImGui (vendored submodule) ---------------------------------------
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

# --- ImPlot (vendored submodule) --------------------------------------------
set(IMPLOT_DIR ${CMAKE_SOURCE_DIR}/third_party/implot)
set(IMPLOT_SOURCES
    ${IMPLOT_DIR}/implot.cpp
    ${IMPLOT_DIR}/implot_items.cpp
)

add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)

add_library(implot STATIC ${IMPLOT_SOURCES})
target_include_directories(implot PUBLIC ${IMPLOT_DIR})
target_link_libraries(implot PUBLIC imgui)

# --- MkPCApp.exe -------------------------------------------------------------
set(APP_SOURCES
    src/main.cpp
    src/app/Application.cpp
    src/app/TrayIcon.cpp
    src/app/BridgeProcess.cpp
    src/app/AutoStart.cpp
    src/sensors/NativeSensors.cpp
    src/platform/MiniJson.cpp
    src/profiles/ProfileJson.cpp
    src/profiles/ProfileStore.cpp
    src/profiles/ProfileManager.cpp
    src/profiles/AutomationEngine.cpp
    src/profiles/SystemControl/PowerPlan.cpp
    src/profiles/SystemControl/PowerTimeouts.cpp
    src/profiles/SystemControl/DisplayBrightness.cpp
    src/profiles/SystemControl/Volume.cpp
    src/profiles/SystemControl/ScreenControl.cpp
    src/groups/GroupJson.cpp
    src/groups/GroupStore.cpp
    src/groups/GroupManager.cpp
    src/groups/ProcessLifecycle.cpp
    src/groups/GroupProcessTracker.cpp
    src/startup/RegistryStartupControl.cpp
    src/startup/ShortcutStartupControl.cpp
    src/startup/SignatureVerifier.cpp
    src/startup/IconExtractor.cpp
    src/startup/AppInfoReader.cpp
    src/startup/StartupScanner.cpp
    src/ui/TabManager.cpp
    src/ui/HardwareMonitorTab.cpp
    src/ui/PerfilesTab.cpp
    src/ui/ProfileEditorDialog.cpp
    src/ui/RuleEditorDialog.cpp
    src/ui/CardWidgets.cpp
    src/ui/StartupTab.cpp
    src/ui/AddStartupEntryDialog.cpp
    src/ui/ConfirmDeleteDialog.cpp
    src/ui/Theme.cpp
    src/platform/DX11Renderer.cpp
    src/platform/IconTextureCache.cpp
    src/platform/WndProc.cpp
    resources/AppIcon.rc
)

add_executable(MkPCApp WIN32 ${APP_SOURCES})
target_include_directories(MkPCApp PRIVATE src resources)
target_link_libraries(MkPCApp PRIVATE imgui implot d3d11 dxgi shell32 iphlpapi advapi32
                                        PowrProf dxva2 ole32 oleaut32 wbemuuid
                                        wintrust crypt32 shlwapi gdi32 uuid version bcrypt)
target_compile_definitions(MkPCApp PRIVATE UNICODE _UNICODE NOMINMAX)

# Always request administrator elevation: full sensor access (CPU temperature,
# fan speeds) needs it. This was tried once before, appeared to break those
# same sensors, and was reverted — the actual cause was LibreHardwareMonitorLib
# 0.9.6 needing a separate driver (PawnIO) that isn't installed, not this
# manifest (see docs/PROJECT_STATUS.md). Now re-added on top of the library
# pinned back to 0.9.4. Windows still shows the UAC prompt once per launch;
# this is explicit user-consented elevation, not a silent one.
if(MSVC)
    target_link_options(MkPCApp PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
endif()

# --- Sensor bridge (.NET, built via dotnet publish, not part of this target's
#     compile graph) -- copied into build/bin/sensor-bridge/ so the native app
#     finds it next to its own executable at runtime. -----------------------
set(SENSOR_BRIDGE_PROJECT ${CMAKE_SOURCE_DIR}/sensor-bridge/MkPCApp.SensorBridge/MkPCApp.SensorBridge.csproj)
set(SENSOR_BRIDGE_OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin/sensor-bridge)

add_custom_target(sensor_bridge ALL
    COMMAND dotnet publish ${SENSOR_BRIDGE_PROJECT}
            -c Release -r win-x64 --self-contained
            -p:PublishSingleFile=true
            -o ${SENSOR_BRIDGE_OUTPUT_DIR}
    COMMENT "Publishing MkPCApp.SensorBridge (.NET, self-contained win-x64)"
    VERBATIM
)
add_dependencies(MkPCApp sensor_bridge)
