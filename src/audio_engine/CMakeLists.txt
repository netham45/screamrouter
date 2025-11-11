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

# Windows resource script for tray icon
if(WIN32)
    set(WEBVIEW2_VERSION "1.0.2846.51")
    set(SCREAMROUTER_RESOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/windows/resources/screamrouter.rc"
    )
    if(NOT DEFINED WEBVIEW2_SDK_DIR AND DEFINED ENV{WEBVIEW2_SDK_DIR})
        set(WEBVIEW2_SDK_DIR "$ENV{WEBVIEW2_SDK_DIR}")
    endif()
    if(NOT DEFINED WEBVIEW2_SDK_DIR)
        set(_webview2_package_root "${CMAKE_BINARY_DIR}/_deps/webview2")
        set(WEBVIEW2_SDK_DIR "${_webview2_package_root}/Microsoft.Web.WebView2.${WEBVIEW2_VERSION}")
        if(NOT EXISTS "${WEBVIEW2_SDK_DIR}")
            if(DEFINED ENV{NUGET_EXE})
                set(_nuget_hint "$ENV{NUGET_EXE}")
            else()
                set(_nuget_hint "")
            endif()
            find_program(NUGET_EXE nuget
                HINTS
                    "${_nuget_hint}"
                    "$ENV{ProgramFiles(x86)}/NuGet"
                    "$ENV{ProgramFiles}/NuGet"
            )
            if(NOT NUGET_EXE AND _nuget_hint)
                if(EXISTS "${_nuget_hint}")
                    set(NUGET_EXE "${_nuget_hint}")
                endif()
            endif()
            if(NOT NUGET_EXE)
                message(FATAL_ERROR
                    "WebView2 SDK not found. Install nuget.exe or set WEBVIEW2_SDK_DIR to an existing Microsoft.Web.WebView2 package.")
            endif()
            file(MAKE_DIRECTORY "${_webview2_package_root}")
            message(STATUS "Downloading Microsoft.Web.WebView2.${WEBVIEW2_VERSION} with nuget")
            execute_process(
                COMMAND "${NUGET_EXE}" install Microsoft.Web.WebView2 -Version ${WEBVIEW2_VERSION} -OutputDirectory "${_webview2_package_root}"
                RESULT_VARIABLE _nuget_result
                OUTPUT_QUIET
                ERROR_VARIABLE _nuget_err
            )
            if(_nuget_result)
                message(FATAL_ERROR "nuget failed to download WebView2 (${_nuget_result}): ${_nuget_err}")
            endif()
        endif()
    endif()
    if(NOT EXISTS "${WEBVIEW2_SDK_DIR}")
        message(FATAL_ERROR "WEBVIEW2_SDK_DIR does not exist: ${WEBVIEW2_SDK_DIR}")
    endif()
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_webview2_arch "x64")
        set(_webview2_runtime "win-x64")
    else()
        set(_webview2_arch "x86")
        set(_webview2_runtime "win-x86")
    endif()
    set(WEBVIEW2_INCLUDE_DIR "${WEBVIEW2_SDK_DIR}/build/native/include")
    set(WEBVIEW2_LOADER_LIB "${WEBVIEW2_SDK_DIR}/build/native/${_webview2_arch}/WebView2LoaderStatic.lib")
    set(WEBVIEW2_LOADER_DLL "${WEBVIEW2_SDK_DIR}/runtimes/${_webview2_runtime}/native/WebView2Loader.dll")
    if(NOT EXISTS "${WEBVIEW2_INCLUDE_DIR}/WebView2.h")
        message(FATAL_ERROR "WebView2.h not found under ${WEBVIEW2_INCLUDE_DIR}")
    endif()
    if(NOT EXISTS "${WEBVIEW2_LOADER_LIB}")
        message(FATAL_ERROR "WebView2LoaderStatic.lib not found (${WEBVIEW2_LOADER_LIB})")
    endif()
else()
    set(SCREAMROUTER_RESOURCES "")
    set(WEBVIEW2_INCLUDE_DIR "")
endif()

# Drop vendored dependency sources from the build.
list(FILTER SCREAMROUTER_SOURCES EXCLUDE REGEX "/deps/")
list(FILTER SCREAMROUTER_SOURCES EXCLUDE REGEX "/build/")

if(NOT SCREAMROUTER_SOURCES)
    message(FATAL_ERROR "No C++ sources found for screamrouter_audio_engine")
endif()

pybind11_add_module(screamrouter_audio_engine MODULE ${SCREAMROUTER_SOURCES} ${SCREAMROUTER_RESOURCES})

target_include_directories(screamrouter_audio_engine
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/json/include"
        "${SCREAMROUTER_DEPS_PREFIX}/include"
        $<$<PLATFORM_ID:Windows>:${WEBVIEW2_INCLUDE_DIR}>
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
            "${WEBVIEW2_LOADER_LIB}"
    )
    if(EXISTS "${WEBVIEW2_LOADER_DLL}")
        add_custom_command(TARGET screamrouter_audio_engine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${WEBVIEW2_LOADER_DLL}" "$<TARGET_FILE_DIR:screamrouter_audio_engine>"
            COMMENT "Copy WebView2Loader.dll"
        )
    else()
        message(WARNING "WebView2Loader.dll not found at ${WEBVIEW2_LOADER_DLL}; runtime overlay may fail to load WebView2")
    endif()
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
