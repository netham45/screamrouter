cmake_minimum_required(VERSION 3.16)

project(screamrouter_audio_engine LANGUAGES CXX)

if(NOT DEFINED SCREAMROUTER_DEPS_PREFIX)
    message(FATAL_ERROR "SCREAMROUTER_DEPS_PREFIX is not defined. Did setup.py pass the dependency install directory?")
endif()

# Where the built Python module should be dropped. setup.py passes an absolute
# path to ensure distutils finds the artifact without an extra install step.
if(NOT DEFINED SCREAMROUTER_MODULE_OUTPUT_DIR)
    set(SCREAMROUTER_MODULE_OUTPUT_DIR "${CMAKE_BINARY_DIR}/python")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    # Match setuptools defaults: /MD runtime, multi-processor compilation, etc.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()

find_package(pybind11 CONFIG REQUIRED)

file(GLOB_RECURSE SCREAMROUTER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# Drop vendored dependency sources from the build.
list(FILTER SCREAMROUTER_SOURCES EXCLUDE REGEX "/deps/")
list(FILTER SCREAMROUTER_SOURCES EXCLUDE REGEX "/build/")

if(NOT SCREAMROUTER_SOURCES)
    message(FATAL_ERROR "No C++ sources found for screamrouter_audio_engine")
endif()

pybind11_add_module(screamrouter_audio_engine MODULE ${SCREAMROUTER_SOURCES})

target_include_directories(screamrouter_audio_engine
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/json/include"
        "${SCREAMROUTER_DEPS_PREFIX}/include"
)

target_compile_definitions(screamrouter_audio_engine
    PRIVATE
        RTC_STATIC
        $<$<PLATFORM_ID:Windows>:_CRT_SECURE_NO_WARNINGS>
)


if(MSVC)
    target_compile_options(screamrouter_audio_engine PRIVATE /O2 /W3 /EHsc /MP)
else()
    target_compile_options(screamrouter_audio_engine PRIVATE -O2 -Wall)
endif()

target_link_directories(screamrouter_audio_engine
    PRIVATE
        "${SCREAMROUTER_DEPS_PREFIX}/lib"
        "${SCREAMROUTER_DEPS_PREFIX}/lib64"
)

target_link_libraries(screamrouter_audio_engine
    PRIVATE
        mp3lame
        opus
        samplerate
        datachannel
        juice
        usrsctp
        srtp2
)

# Always compile function-level instrumentation for this target (non-MSVC).
# Runtime is still gated by SCREAMROUTER_TRACE, so default behavior remains off.
if(NOT MSVC)
    # Optional: enable function-level instrumentation only when requested.
    # Toggle via CMake option SR_ENABLE_FNTRACE or env var SCREAMROUTER_FNTRACE.
    option(SR_ENABLE_FNTRACE "Enable -finstrument-functions for audio_engine" OFF)
    if(DEFINED ENV{SCREAMROUTER_FNTRACE})
        set(SR_ENABLE_FNTRACE ON)
    endif()

    if(SR_ENABLE_FNTRACE)
        message(STATUS "screamrouter_audio_engine: function tracing ENABLED (SR_ENABLE_FNTRACE=ON)")
        # Instrument only this target's object files
        target_compile_options(screamrouter_audio_engine PRIVATE -finstrument-functions)
        # Keep trace implementation itself from being instrumented to avoid recursion
        set_source_files_properties(
            "${CMAKE_CURRENT_SOURCE_DIR}/utils/fntrace.cpp"
            PROPERTIES COMPILE_OPTIONS "-fno-instrument-functions"
        )
        # Ensure the probe function is not inlined so it reliably emits events
        set_source_files_properties(
            "${CMAKE_CURRENT_SOURCE_DIR}/utils/fntrace_probe.cpp"
            PROPERTIES COMPILE_OPTIONS "-fno-inline"
        )
        # Bind instrumentation hook calls locally to this module to avoid
        # runtime interposition by other shared objects in the Python process.
        target_link_options(screamrouter_audio_engine PRIVATE -Wl,-Bsymbolic -Wl,-Bsymbolic-functions)
        target_compile_definitions(screamrouter_audio_engine PRIVATE SR_FNTRACE_BUILD=1)

        # Optional stricter mode to maximize event coverage by reducing inlining/opts.
        # Enable with env var SCREAMROUTER_FNTRACE_STRICT=1
        if(DEFINED ENV{SCREAMROUTER_FNTRACE_STRICT})
            message(STATUS "screamrouter_audio_engine: STRICT tracing (O0, -fno-inline, -fno-optimize-sibling-calls)")
            target_compile_options(screamrouter_audio_engine PRIVATE -O0 -fno-inline -fno-optimize-sibling-calls)
        endif()
    else()
        message(STATUS "screamrouter_audio_engine: function tracing DISABLED (SR_ENABLE_FNTRACE=OFF)")
    endif()
endif()

if(WIN32)
    target_link_libraries(screamrouter_audio_engine
        PRIVATE
            libssl
            libcrypto
            advapi32
            crypt32
            user32
            dwmapi
            shlwapi
            bcrypt
            ws2_32
            iphlpapi
            ole32
            oleaut32
            avrt
            mmdevapi
            uuid
            propsys
            WebView2LoaderStatic
    )
else()
    target_link_libraries(screamrouter_audio_engine PRIVATE ssl crypto pthread dl)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(screamrouter_audio_engine PRIVATE asound)
    endif()
endif()

set(_output_types LIBRARY ARCHIVE RUNTIME)
foreach(_type IN LISTS _output_types)
    set_target_properties(screamrouter_audio_engine PROPERTIES
        "${_type}_OUTPUT_DIRECTORY" "${SCREAMROUTER_MODULE_OUTPUT_DIR}"
    )
endforeach()

if(CMAKE_CONFIGURATION_TYPES)
    foreach(_cfg ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER "${_cfg}" _cfg_upper)
        foreach(_type IN LISTS _output_types)
            set_target_properties(screamrouter_audio_engine PROPERTIES
                "${_type}_OUTPUT_DIRECTORY_${_cfg_upper}" "${SCREAMROUTER_MODULE_OUTPUT_DIR}"
            )
        endforeach()
    endforeach()
endif()

if(UNIX AND NOT APPLE)
    set_target_properties(screamrouter_audio_engine PROPERTIES
        BUILD_WITH_INSTALL_RPATH ON
        INSTALL_RPATH "$ORIGIN"
    )
endif()

install(TARGETS screamrouter_audio_engine
    LIBRARY DESTINATION "${SCREAMROUTER_MODULE_OUTPUT_DIR}"
    ARCHIVE DESTINATION "${SCREAMROUTER_MODULE_OUTPUT_DIR}"
    RUNTIME DESTINATION "${SCREAMROUTER_MODULE_OUTPUT_DIR}"
)
