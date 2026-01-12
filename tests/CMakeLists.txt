# CMakeLists.txt for ScreamRouter Audio Engine Tests
cmake_minimum_required(VERSION 3.14)
project(screamrouter_audio_engine_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Option to skip GoogleTest integration (for bare-metal tests like the existing one)
option(SCREAMROUTER_USE_GTEST "Use GoogleTest framework" ON)

# --- Fetch GoogleTest ---
if(SCREAMROUTER_USE_GTEST)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    enable_testing()
    include(GoogleTest)
endif()

# --- Project Paths ---
set(AUDIO_ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../src/audio_engine")

# --- Common Include Directories ---
set(AUDIO_ENGINE_INCLUDE_DIRS
    ${AUDIO_ENGINE_ROOT}
    ${AUDIO_ENGINE_ROOT}/json/include
)

# --- Core Audio Engine Sources (Subset for Testing) ---
# We compile a minimal subset of the engine as a library to link against.
# This avoids needing the full Python/Pybind build and all dependencies.
# Add sources as needed for each test target.

# Example: Sources for RTP-related testing
set(RTP_SOURCES
    ${AUDIO_ENGINE_ROOT}/receivers/rtp/rtp_reordering_buffer.cpp
)

# --- Test Targets ---

# 1. Legacy Test (no GoogleTest, uses assert)
add_executable(test_rtp_interpolation_legacy
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_rtp_interpolation.cpp
    ${RTP_SOURCES}
)
target_include_directories(test_rtp_interpolation_legacy PRIVATE ${AUDIO_ENGINE_INCLUDE_DIRS})
target_compile_definitions(test_rtp_interpolation_legacy PRIVATE
    SCREAMROUTER_TESTING # Can be used to stub out logging
)

# 2. GoogleTest-based Tests
if(SCREAMROUTER_USE_GTEST)
    # --- Phase 2: Utility Tests (Header-only, no additional sources needed) ---
    
    # ByteRingBuffer tests
    add_executable(test_byte_ring_buffer
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_byte_ring_buffer.cpp
    )
    target_include_directories(test_byte_ring_buffer PRIVATE ${AUDIO_ENGINE_INCLUDE_DIRS})
    target_compile_definitions(test_byte_ring_buffer PRIVATE SCREAMROUTER_TESTING)
    target_link_libraries(test_byte_ring_buffer GTest::gtest_main)
    gtest_discover_tests(test_byte_ring_buffer)
    
    # ThreadSafeQueue tests
    add_executable(test_thread_safe_queue
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_thread_safe_queue.cpp
    )
    target_include_directories(test_thread_safe_queue PRIVATE ${AUDIO_ENGINE_INCLUDE_DIRS})
    target_compile_definitions(test_thread_safe_queue PRIVATE SCREAMROUTER_TESTING)
    target_link_libraries(test_thread_safe_queue GTest::gtest_main)
    gtest_discover_tests(test_thread_safe_queue)
    
    # PacketRing tests
    add_executable(test_packet_ring
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_packet_ring.cpp
    )
    target_include_directories(test_packet_ring PRIVATE ${AUDIO_ENGINE_INCLUDE_DIRS})
    target_compile_definitions(test_packet_ring PRIVATE SCREAMROUTER_TESTING)
    target_link_libraries(test_packet_ring GTest::gtest_main)
    gtest_discover_tests(test_packet_ring)
    
    # --- Phase 4: RTP Reordering Buffer (GTest version) ---
    add_executable(test_rtp_reordering_buffer
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_rtp_reordering_buffer.cpp
        ${RTP_SOURCES}
    )
    target_include_directories(test_rtp_reordering_buffer PRIVATE ${AUDIO_ENGINE_INCLUDE_DIRS})
    target_compile_definitions(test_rtp_reordering_buffer PRIVATE SCREAMROUTER_TESTING)
    target_link_libraries(test_rtp_reordering_buffer GTest::gtest_main)
    gtest_discover_tests(test_rtp_reordering_buffer)
    
    # --- Phase 3/6: Audio Channel Layout Tests ---
    add_executable(test_audio_channel_layout
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_audio_channel_layout.cpp
        ${AUDIO_ENGINE_ROOT}/audio_channel_layout.cpp
    )
    target_include_directories(test_audio_channel_layout PRIVATE ${AUDIO_ENGINE_INCLUDE_DIRS})
    target_compile_definitions(test_audio_channel_layout PRIVATE SCREAMROUTER_TESTING)
    target_link_libraries(test_audio_channel_layout GTest::gtest_main)
    gtest_discover_tests(test_audio_channel_layout)
    
    # --- Phase 5: StreamClock (Kalman filter) Tests ---
    add_executable(test_stream_clock
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_stream_clock.cpp
        ${AUDIO_ENGINE_ROOT}/input_processor/stream_clock.cpp
    )
    target_include_directories(test_stream_clock PRIVATE ${AUDIO_ENGINE_INCLUDE_DIRS})
    target_compile_definitions(test_stream_clock PRIVATE SCREAMROUTER_TESTING)
    target_link_libraries(test_stream_clock GTest::gtest_main)
    gtest_discover_tests(test_stream_clock)
    
    # --- Phase 6: Biquad Filter Tests ---
    add_executable(test_biquad
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_biquad.cpp
        ${AUDIO_ENGINE_ROOT}/audio_processor/biquad/biquad.cpp
    )
    target_include_directories(test_biquad PRIVATE ${AUDIO_ENGINE_INCLUDE_DIRS})
    target_compile_definitions(test_biquad PRIVATE SCREAMROUTER_TESTING)
    target_link_libraries(test_biquad GTest::gtest_main)
    gtest_discover_tests(test_biquad)
    
    # --- Phase 8: GlobalSynchronizationClock Tests ---
    add_executable(test_global_sync_clock
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_global_sync_clock.cpp
        ${AUDIO_ENGINE_ROOT}/synchronization/global_synchronization_clock.cpp
    )
    target_include_directories(test_global_sync_clock PRIVATE ${AUDIO_ENGINE_INCLUDE_DIRS})
    target_compile_definitions(test_global_sync_clock PRIVATE SCREAMROUTER_TESTING)
    target_link_libraries(test_global_sync_clock GTest::gtest_main pthread)
    gtest_discover_tests(test_global_sync_clock)
    
    # --- Phase 7: Audio Mixing Logic Tests ---
    add_executable(test_audio_mixing
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_audio_mixing.cpp
    )
    target_include_directories(test_audio_mixing PRIVATE ${AUDIO_ENGINE_INCLUDE_DIRS})
    target_compile_definitions(test_audio_mixing PRIVATE SCREAMROUTER_TESTING)
    target_link_libraries(test_audio_mixing GTest::gtest_main)
    gtest_discover_tests(test_audio_mixing)
    
    # --- Phase 9: Integration Tests ---
    # Source files needed for SourceInputProcessor integration test
    set(SIP_INTEGRATION_SOURCES
        ${AUDIO_ENGINE_ROOT}/input_processor/source_input_processor.cpp
        ${AUDIO_ENGINE_ROOT}/audio_processor/audio_processor.cpp
        ${AUDIO_ENGINE_ROOT}/audio_processor/biquad/biquad.cpp
        ${AUDIO_ENGINE_ROOT}/audio_channel_layout.cpp
        ${AUDIO_ENGINE_ROOT}/utils/profiler.cpp
    )
    
    add_executable(test_source_processor_integration
        ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_source_processor_integration.cpp
        ${SIP_INTEGRATION_SOURCES}
    )
    target_include_directories(test_source_processor_integration PRIVATE 
        ${AUDIO_ENGINE_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/mocks
        ${CMAKE_CURRENT_SOURCE_DIR}/../build/deps/include
    )
    target_compile_definitions(test_source_processor_integration PRIVATE SCREAMROUTER_TESTING)
    # Link project-local libsamplerate static library
    target_link_libraries(test_source_processor_integration 
        GTest::gtest_main 
        pthread
        ${CMAKE_CURRENT_SOURCE_DIR}/../build/deps/lib/libsamplerate.a
    )
    gtest_discover_tests(test_source_processor_integration)
    
    # --- Pipeline Integration Tests ---
    # Tests the core pipeline: TimeshiftManager → SourceInputProcessor
    # This avoids heavy deps like MP3/WebRTC/ALSA
    set(PIPELINE_SOURCES
        ${AUDIO_ENGINE_ROOT}/input_processor/source_input_processor.cpp
        ${AUDIO_ENGINE_ROOT}/input_processor/timeshift_manager.cpp
        ${AUDIO_ENGINE_ROOT}/input_processor/stream_clock.cpp
        ${AUDIO_ENGINE_ROOT}/audio_processor/audio_processor.cpp
        ${AUDIO_ENGINE_ROOT}/audio_processor/biquad/biquad.cpp
        ${AUDIO_ENGINE_ROOT}/utils/profiler.cpp
        ${AUDIO_ENGINE_ROOT}/audio_channel_layout.cpp
    )
    
    add_executable(test_pipeline
        ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_pipeline.cpp
        ${PIPELINE_SOURCES}
    )
    target_include_directories(test_pipeline PRIVATE 
        ${AUDIO_ENGINE_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/../build/deps/include
    )
    target_compile_definitions(test_pipeline PRIVATE SCREAMROUTER_TESTING)
    target_link_libraries(test_pipeline 
        GTest::gtest_main 
        pthread
        ${CMAKE_CURRENT_SOURCE_DIR}/../build/deps/lib/libsamplerate.a
    )
    gtest_discover_tests(test_pipeline)
    
    # --- Full Audio Graph Integration Tests ---
    # Tests with ALL dependencies compiled in
    file(GLOB_RECURSE AUDIO_ENGINE_ALL_SOURCES
        "${AUDIO_ENGINE_ROOT}/*.cpp"
    )
    # Exclude bindings.cpp (pybind11 main) and deps examples
    list(FILTER AUDIO_ENGINE_ALL_SOURCES EXCLUDE REGEX "bindings\\.cpp$")
    list(FILTER AUDIO_ENGINE_ALL_SOURCES EXCLUDE REGEX "deps/.*")
    
    add_executable(test_audio_graph
        ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_audio_graph.cpp
        ${AUDIO_ENGINE_ALL_SOURCES}
    )
    target_include_directories(test_audio_graph PRIVATE 
        ${AUDIO_ENGINE_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/../build/deps/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio_engine/deps/libdatachannel/include
    )
    target_compile_definitions(test_audio_graph PRIVATE SCREAMROUTER_TESTING)
    target_link_libraries(test_audio_graph 
        GTest::gtest_main 
        pthread
        ${CMAKE_CURRENT_SOURCE_DIR}/../build/deps/lib/libsamplerate.a
        ${CMAKE_CURRENT_SOURCE_DIR}/../build/deps/lib/libmp3lame.a
        ${CMAKE_CURRENT_SOURCE_DIR}/../build/deps/lib/libopus.a
        ${CMAKE_CURRENT_SOURCE_DIR}/../build/deps/lib/libdatachannel.a
        ${CMAKE_CURRENT_SOURCE_DIR}/../build/deps/lib/libjuice.a
        ${CMAKE_CURRENT_SOURCE_DIR}/../build/deps/lib/libsrtp2.a
        ${CMAKE_CURRENT_SOURCE_DIR}/../build/deps/lib/libusrsctp.a
        asound
        ssl
        crypto
    )
    gtest_discover_tests(test_audio_graph)
    
endif()

# --- CTest Integration ---
# The legacy test can also be run via CTest.
add_test(NAME RtpInterpolationLegacyTest COMMAND test_rtp_interpolation_legacy)
