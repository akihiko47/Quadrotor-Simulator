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

# SDL3, Volk, VMA, glm and Slang are taken from the SDK
include_directories($ENV{VULKAN_SDK}/include)
# These are not part of the SDK
include_directories(external/tinyobj)
include_directories(external/ktx/include)
include_directories(external/ktx/other_include)
include_directories(src)
include_directories(src/models)
include_directories(src/integrators)

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
    target_link_libraries(ktx PUBLIC $ENV{VULKAN_SDK}/lib/libvulkan.so)
endif()

find_library(SDL_LIBRARY NAMES SDL3 HINTS "$ENV{VULKAN_SDK}/lib" REQUIRED)
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


set(MODEL_SOURCES
    src/models/dynamic_model.cpp
    src/models/test_model.cpp
    src/models/quadrotor_model.cpp
)

set(INTEGRATOR_SOURCES
    src/integrators/integrators.cpp
)


set(SHADER_SOURCES
    assets/shader.slang
    assets/shader-skybox.slang
)

set(SOURCE_FILES
    src/main.cpp
    ${SHADER_SOURCES}
    ${MODEL_SOURCES}
    ${INTEGRATOR_SOURCES}
)

add_executable(${NAME} ${SOURCE_FILES})

# VS default start-up project
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${NAME})

# VS folders organization
source_group("Source Files\\models" FILES ${MODEL_SOURCES})
source_group("Source Files\\integrators" FILES ${INTEGRATOR_SOURCES})
source_group("Source Files" FILES src/main.cpp)
source_group("Shader Files" FILES ${SHADER_SOURCES})

target_compile_definitions(${NAME} PRIVATE VK_NO_PROTOTYPES)
if (WIN32)
    target_compile_definitions(${NAME} PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
set_target_properties(${NAME} PROPERTIES DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(${NAME} PRIVATE cxx_std_20)
target_link_libraries(${NAME} PRIVATE ${SDL_LIBRARY} ktx ${Slang_LIBRARY})
if (NOT WIN32)
    target_link_libraries(${NAME} PRIVATE glm::glm-header-only)
endif()

set_target_properties(${NAME} PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)