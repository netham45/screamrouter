#include <iostream>
#include <vector>
#include <cassert>
#include <cstring>
#include "receivers/rtp/rtp_reordering_buffer.h"
#include "receivers/rtp/sap_listener/sap_types.h"

// Mock Logging
namespace screamrouter::audio::logging {
    enum class LogLevel { DEBUG, INFO, WARNING, ERR };
    void log_message(LogLevel level, const char* file, int line, const char* format, ...) {
        // No-op or print to stdout for debugging
        // va_list args; ...
    }
    const char* get_base_filename(const char* path) { return path; }
}

#define LOG_CPP_DEBUG(...)
#define LOG_CPP_WARNING(...)
#define LOG_CPP_INFO(...)

int main() {
    using namespace screamrouter::audio;
    
    std::cout << "Starting RTP Interpolation Test..." << std::endl;

    auto make_props = [](int bits, Endianness end) {
        StreamProperties p;
        p.codec = StreamCodec::PCM;
        p.bit_depth = bits;
        p.channels = 2;
        p.endianness = end;
        return p;
    };

    {
        std::cout << "--- Test 1. Normal Flow ---" << std::endl;
        RtpReorderingBuffer buffer(std::chrono::milliseconds(10), 100);
        buffer.set_properties(make_props(16, Endianness::BIG));

        // 1. Initial Packet (Seq 100)
        RtpPacketData p1;
        p1.sequence_number = 100;
        p1.rtp_timestamp = 1000;
        p1.received_time = std::chrono::steady_clock::now() - std::chrono::milliseconds(20); // Old
        p1.payload.resize(4); 
        p1.payload[0] = 0x03; p1.payload[1] = 0xE8; 
        p1.payload[2] = 0x07; p1.payload[3] = 0xD0;
        
        buffer.add_packet(std::move(p1));
        
        auto ready = buffer.get_ready_packets();
        assert(ready.size() == 1);
        assert(ready[0].sequence_number == 100);
        std::cout << "Passed: Initial packet retrieval." << std::endl;

        // 2. Next Packet with GAP (Seq 102). Missing 101.
        RtpPacketData p2;
        p2.sequence_number = 102;
        p2.rtp_timestamp = 1020; 
        p2.received_time = std::chrono::steady_clock::now(); 
        p2.payload.resize(4);
        p2.payload[0] = 0x0B; p2.payload[1] = 0xB8;
        p2.payload[2] = 0x0F; p2.payload[3] = 0xA0;

        buffer.add_packet(std::move(p2));
        ready = buffer.get_ready_packets();
        assert(ready.empty()); 
        std::cout << "Passed: Buffering gap." << std::endl;
    }

    {
        std::cout << "--- Test 2. Interpolation (New Buffer) ---" << std::endl;
        RtpReorderingBuffer buffer(std::chrono::milliseconds(10), 100);
        buffer.set_properties(make_props(16, Endianness::BIG));
        
        // Re-add P1 (consumed)
        RtpPacketData p1;
        p1.sequence_number = 100;
        p1.rtp_timestamp = 1000;
        p1.received_time = std::chrono::steady_clock::now() - std::chrono::milliseconds(20);
        p1.payload.resize(4);
        p1.payload[0] = 0x03; p1.payload[1] = 0xE8; p1.payload[2] = 0x07; p1.payload[3] = 0xD0; 
        
        std::cout << "DEBUG: Adding P1..." << std::endl;
        buffer.add_packet(std::move(p1));
        std::cout << "DEBUG: Getting P1..." << std::endl;
        buffer.get_ready_packets();

        // Re-add P2 (Older)
        RtpPacketData p2;
        p2.sequence_number = 102;
        p2.rtp_timestamp = 1020;
        p2.received_time = std::chrono::steady_clock::now() - std::chrono::milliseconds(20);
        p2.payload.resize(4);
        p2.payload[0] = 0x0B; p2.payload[1] = 0xB8; p2.payload[2] = 0x0F; p2.payload[3] = 0xA0;
        
        std::cout << "DEBUG: Adding P2..." << std::endl;
        buffer.add_packet(std::move(p2));
        
        // Should trigger timeout and interpolate Seq 101.
        std::cout << "DEBUG: Final get_ready_packets..." << std::endl;
        auto ready = buffer.get_ready_packets();
        
        assert(ready.size() == 2); // 101 (interpolated), 102 (original)
        assert(ready[0].sequence_number == 101);
        assert(ready[1].sequence_number == 102);
        
        auto& load = ready[0].payload;
        int16_t s0 = (load[0] << 8) | load[1];
        int16_t s1 = (load[2] << 8) | load[3];
        
        std::cout << "Interpolated S0: " << s0 << " (Expected 1000)" << std::endl;
        std::cout << "Interpolated S1: " << s1 << " (Expected 2500)" << std::endl;
        
        assert(s0 == 1000);
        assert(s1 == 2500);

        std::cout << "Passed: Interpolation Logic." << std::endl;
    }
    
    std::cout << "All Tests Passed!" << std::endl;
    return 0;
}
