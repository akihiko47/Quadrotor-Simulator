cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")
include(FetchContent)
set(NAME QuadrotorSimulator)
project(${NAME})

if (NOT IS_DIRECTORY $ENV{VULKAN_SDK})
    message(WARNING "Vulkan SDK not detected, if installed please set VULKAN_SDK env var or manually provide libraries")
else()
    message(STATUS "Found Vulkan SDK in $ENV{VULKAN_SDK}")
endif()

# Volk, VMA, glm and Slang are taken from the SDK
include_directories($ENV{VULKAN_SDK}/include)
# These are not part of the SDK
include_directories(external/tinyobj)
include_directories(external/imgui)
include_directories(external/implot)
include_directories(external/ktx/include)
include_directories(external/ktx/other_include)
include_directories(src)
include_directories(src/models)
include_directories(src/integrators)
include_directories(src/control_system)
include_directories(src/networking)

find_package (Vulkan REQUIRED)

if (NOT WIN32)
    FetchContent_Declare(GLM
        GIT_REPOSITORY https://github.com/g-truc/glm
        GIT_TAG 1.0
        GIT_SHALLOW ON
        EXCLUDE_FROM_ALL
        SYSTEM)
    FetchContent_MakeAvailable(GLM)
endif()

FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz)
FetchContent_MakeAvailable(json)

set(KTX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/ktx)
set(KTX_SOURCES
    ${KTX_DIR}/lib/texture.c
    ${KTX_DIR}/lib/hashlist.c
    ${KTX_DIR}/lib/checkheader.c
    ${KTX_DIR}/lib/swap.c
    ${KTX_DIR}/lib/memstream.c
    ${KTX_DIR}/lib/filestream.c
    ${KTX_DIR}/lib/vkloader.c)
add_library(ktx STATIC ${KTX_SOURCES})

if (NOT WIN32)
    target_link_libraries(ktx PUBLIC Vulkan::Vulkan)
endif()

# SDL3 is taken from sdk on Windows and fetchet from git on Linux
if(WIN32)
    find_library(SDL3_LIB_PATH NAMES SDL3 HINTS "$ENV{VULKAN_SDK}/lib" REQUIRED)
    add_library(SDL3::SDL3 UNKNOWN IMPORTED)
    set_target_properties(SDL3::SDL3 PROPERTIES
        IMPORTED_LOCATION "${SDL3_LIB_PATH}"
        INTERFACE_INCLUDE_DIRECTORIES "$ENV{VULKAN_SDK}/include"
    )
else()
    find_package(SDL3 CONFIG QUIET)
    if(NOT SDL3_FOUND)
        message(STATUS "SDL3 not found, fetching from GitHub...")
        FetchContent_Declare(
            SDL3
            GIT_REPOSITORY  https://github.com/libsdl-org/SDL.git
            GIT_TAG         release-3.4.8
            GIT_SHALLOW     ON
            EXCLUDE_FROM_ALL
            SYSTEM
        )
        FetchContent_MakeAvailable(SDL3)
    endif()
endif()

find_library(Slang_LIBRARY NAMES slang slang-compiler HINTS "$ENV{VULKAN_SDK}/lib" REQUIRED)

if(WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_WIN32_KHR")
elseif(LINUX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_WAYLAND_KHR")
endif()
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNOMINMAX -D_USE_MATH_DEFINES")
if(MSVC)
	SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc")
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/")

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.h
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imconfig.h
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/imgui_impl_sdl3.h
    ${IMGUI_DIR}/imgui_impl_vulkan.cpp
    ${IMGUI_DIR}/imgui_impl_vulkan.h
    ${IMGUI_DIR}/imgui_internal.h
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imstb_rectpack.h
    ${IMGUI_DIR}/imstb_textedit.h
    ${IMGUI_DIR}/imstb_truetype.h
)

set(IMPLOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/implot)
set(IMPLOT_SOURCES
    ${IMPLOT_DIR}/implot.h
    ${IMPLOT_DIR}/implot.cpp
    ${IMPLOT_DIR}/implot_internal.h
    ${IMPLOT_DIR}/implot_items.cpp
    ${IMPLOT_DIR}/implot_demo.cpp
)

set(MODEL_SOURCES
    src/models/dynamic_model.hpp
    src/models/test_model.hpp
    src/models/quadrotor_model.hpp
)

set(INTEGRATOR_SOURCES
    src/integrators/integrators.hpp
)

set(CONTROL_SYSTEM_SOURCES
    src/control_system/mixer.hpp
    src/control_system/pid.hpp
    src/control_system/rates.hpp
)

set(SHADER_SOURCES
    assets/shader.slang
    assets/shader-skybox.slang
)

set(NETWORKING_SOURCES
    src/networking/udp_socket.hpp
)

set(SOURCE_FILES
    src/main.cpp
    ${SHADER_SOURCES}
    ${NETWORKING_SOURCES}
    ${MODEL_SOURCES}
    ${INTEGRATOR_SOURCES}
    ${CONTROL_SYSTEM_SOURCES}
    ${IMGUI_SOURCES}
    ${IMPLOT_SOURCES}
)

add_executable(${NAME} ${SOURCE_FILES})

# Copy assets to build folder
add_custom_command(TARGET ${NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:${NAME}>/assets
)

# VS default start-up project
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${NAME})

# VS folders organization
source_group("Header Files\\models" FILES ${MODEL_SOURCES})
source_group("Header Files\\networking" FILES ${NETWORKING_SOURCES})
source_group("Header Files\\integrators" FILES ${INTEGRATOR_SOURCES})
source_group("Header Files\\control system" FILES ${CONTROL_SYSTEM_SOURCES})
source_group("Source Files" FILES src/main.cpp)
source_group("Shader Files" FILES ${SHADER_SOURCES})
source_group("Imgui Files" FILES ${IMGUI_SOURCES})

target_compile_definitions(${NAME} PRIVATE VK_NO_PROTOTYPES)
if (WIN32)
    target_compile_definitions(${NAME} PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
set_target_properties(${NAME} PROPERTIES DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(${NAME} PRIVATE cxx_std_20)
target_link_libraries(${NAME} PRIVATE SDL3::SDL3 ktx ${Slang_LIBRARY})
target_link_libraries(${NAME} PRIVATE nlohmann_json::nlohmann_json)
if (NOT WIN32)
    target_include_directories(${NAME} PRIVATE ${glm_SOURCE_DIR})
endif()

set_target_properties(${NAME} PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)