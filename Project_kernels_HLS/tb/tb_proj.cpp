#include "krnl_proj.h"
#include "../src/patterns.h"
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <algorithm>

using std::cout;
using std::dec;
using std::endl;
using std::hex;
using std::string;
using std::vector;

// ============================================================================
// Configuration
// ============================================================================

static const int EVENT_W = 128;
static const int EVENT_BYTES = EVENT_W / 8;  // 16
static const int KEEP_W = DATA_WIDTH_BYTES;  // 64

// ============================================================================
// Data Structures
// ============================================================================

struct MatchResult {
    uint64_t byte_index;  // Global byte index in stream
    uint16_t pattern_id;
    uint8_t lane;
    int packet_id;        // Which packet this event belongs to
};

struct TestStats {
    int packets_sent;
    int packets_received_payload;
    int packets_received_events;
    int bytes_sent;
    int bytes_received_payload;
    int bytes_received_events;
    int events_found;
    int payload_mismatches;
    int missing_events;
    int false_positives;
    bool deadlock_detected;
    
    void print() {
        cout << "\n========== TEST STATISTICS ==========" << endl;
        cout << "Packets sent:               " << packets_sent << endl;
        cout << "Packets recv (payload):     " << packets_received_payload << endl;
        cout << "Packets recv (events):      " << packets_received_events << endl;
        cout << "Bytes sent:                 " << bytes_sent << endl;
        cout << "Bytes recv (payload):       " << bytes_received_payload << endl;
        cout << "Bytes recv (events):        " << bytes_received_events << endl;
        cout << "Events found:               " << events_found << endl;
        cout << "Payload mismatches:         " << payload_mismatches << endl;
        cout << "Missing events:             " << missing_events << endl;
        cout << "False positives:            " << false_positives << endl;
        cout << "Deadlock detected:          " << (deadlock_detected ? "YES ❌" : "NO ✅") << endl;
        cout << "=====================================" << endl;
    }
    
    bool is_pass() {
        return (packets_sent == packets_received_payload) &&
               (payload_mismatches == 0) &&
               (missing_events == 0) &&
               (!deadlock_detected);
    }
};

// ============================================================================
// MM2S Simulator (Same as before)
// ============================================================================

class MM2S_Simulator {
private:
    static const int MAX_PACKET_BYTES = 1408;
    
public:
    int send_data(hls::stream<pkt> &n2k,
                  const unsigned char *data,
                  int total_bytes) {
        int packets_sent = 0;
        int bytes_sent = 0;
        
        cout << "[MM2S] Sending " << total_bytes << " bytes" << endl;
        
        while (bytes_sent < total_bytes) {
            int packet_bytes = std::min(MAX_PACKET_BYTES, total_bytes - bytes_sent);
            int beats = (packet_bytes + DATA_WIDTH_BYTES - 1) / DATA_WIDTH_BYTES;
            
            for (int b = 0; b < beats; ++b) {
                pkt p;
                p.data = 0;
                p.keep = 0;
                p.dest = 0;
                p.user = 0;
                p.id = 0;
                
                int beat_offset = bytes_sent + b * DATA_WIDTH_BYTES;
                int beat_bytes = std::min(DATA_WIDTH_BYTES, total_bytes - beat_offset);
                
                for (int i = 0; i < beat_bytes; ++i) {
                    p.data(i * 8 + 7, i * 8) = data[beat_offset + i];
                    p.keep[i] = 1;
                }
                
                p.last = (b == beats - 1) ? 1 : 0;
                n2k.write(p);
            }
            
            bytes_sent += packet_bytes;
            packets_sent++;
        }
        
        return packets_sent;
    }
};

const int MM2S_Simulator::MAX_PACKET_BYTES;

// ============================================================================
// Dual S2MM Simulator - Receives BOTH payload and events in parallel
// ============================================================================

class DualS2MM_Simulator {
private:
    unsigned char *payload_buffer;
    unsigned char *event_buffer;
    int payload_max_size;
    int event_max_size;
    int payload_write_pos;
    int event_write_pos;
    int payload_packets_received;
    int event_packets_received;
    
public:
    DualS2MM_Simulator(unsigned char *payload_buf, int payload_size,
                       unsigned char *event_buf, int event_size)
        : payload_buffer(payload_buf), event_buffer(event_buf),
          payload_max_size(payload_size), event_max_size(event_size),
          payload_write_pos(0), event_write_pos(0),
          payload_packets_received(0), event_packets_received(0) {}
    
    // Receive payload and events in PARALLEL (critical for avoiding deadlock)
    bool receive_both_streams(hls::stream<pkt> &k2n_payload,
                             hls::stream<pkt> &k2n_events,
                             int expected_payload_packets) {
        cout << "[S2MM] Receiving payload and events in parallel..." << endl;
        cout << "[S2MM] Expecting " << expected_payload_packets << " payload packets" << endl;
        
        bool payload_done = false;
        bool events_done = false;
        int stall_count = 0;
        const int MAX_STALL = 1000;  // Deadlock detection
        
        while (!payload_done || !events_done) {
            bool made_progress = false;
            
            // Try to read payload stream
            if (!payload_done && !k2n_payload.empty()) {
                pkt v = k2n_payload.read();
                made_progress = true;
                
                // Write to buffer
                for (int i = 0; i < DATA_WIDTH_BYTES; ++i) {
                    if (v.keep[i] && payload_write_pos < payload_max_size) {
                        payload_buffer[payload_write_pos++] = 
                            (unsigned char)v.data(i * 8 + 7, i * 8);
                    }
                }
                
                if (v.last) {
                    payload_packets_received++;
                    if (payload_packets_received >= expected_payload_packets) {
                        payload_done = true;
                        cout << "[S2MM] Payload stream complete: " 
                             << payload_packets_received << " packets, "
                             << payload_write_pos << " bytes" << endl;
                    }
                }
            }
            
            // Try to read event stream (may be empty for some packets)
            if (!events_done && !k2n_events.empty()) {
                pkt v = k2n_events.read();
                made_progress = true;
                
                // Write to buffer
                for (int i = 0; i < DATA_WIDTH_BYTES; ++i) {
                    if (event_write_pos < event_max_size) {
                        event_buffer[event_write_pos++] = 
                            (unsigned char)v.data(i * 8 + 7, i * 8);
                    }
                }
                
                if (v.last) {
                    event_packets_received++;
                }
            }
            
            // Check if event stream is done (heuristic: payload done + no more events)
            if (payload_done && k2n_events.empty()) {
                events_done = true;
                cout << "[S2MM] Event stream complete: " 
                     << event_packets_received << " event packets, "
                     << event_write_pos << " bytes" << endl;
            }
            
            // Deadlock detection
            if (!made_progress) {
                stall_count++;
                if (stall_count >= MAX_STALL) {
                    cout << "[S2MM] ❌ DEADLOCK DETECTED after " << stall_count 
                         << " stall cycles!" << endl;
                    cout << "[S2MM] Payload done: " << payload_done 
                         << ", Events done: " << events_done << endl;
                    cout << "[S2MM] Payload empty: " << k2n_payload.empty()
                         << ", Events empty: " << k2n_events.empty() << endl;
                    return false;
                }
            } else {
                stall_count = 0;
            }
        }
        
        return true;
    }
    
    int get_payload_bytes() const { return payload_write_pos; }
    int get_event_bytes() const { return event_write_pos; }
    int get_payload_packets() const { return payload_packets_received; }
    int get_event_packets() const { return event_packets_received; }
};

// ============================================================================
// Event Parser - Extract events from event buffer
// ============================================================================

class EventParser {
public:
    static vector<MatchResult> parse_events(const unsigned char *event_buffer, 
                                           int total_bytes) {
        vector<MatchResult> results;
        
        cout << "[PARSER] Parsing " << total_bytes << " bytes of events" << endl;
        
        int pos = 0;
        while (pos + EVENT_BYTES <= total_bytes) {
            // Check for end marker (0xEE in first byte)
            if (event_buffer[pos] == 0xEE) {
                pos += EVENT_BYTES;
                continue;
            }
            
            uint64_t byte_index = 0;
            uint16_t pattern_id = 0;
            uint8_t lane = 0;
            
            // Parse 128-bit event format from pack_event:
            // [127:64] = byte_index (bytes 15-8, little-endian)
            // [63:48] = pattern_id (bytes 7-6, little-endian)
            // [47:40] = lane (byte 5)
            for (int i = 0; i < 8; ++i) {
                byte_index |= ((uint64_t)event_buffer[pos + 8 + i]) << (i * 8);
            }
            pattern_id = event_buffer[pos + 6] | (event_buffer[pos + 7] << 8);
            lane = event_buffer[pos + 5];
            
            // Valid event has non-zero pattern_id
            if (pattern_id > 0 && pattern_id <= NUM_PATTERNS) {
                results.push_back({byte_index, pattern_id, lane, -1});
            }
            
            pos += EVENT_BYTES;
        }
        
        cout << "[PARSER] Found " << results.size() << " valid events" << endl;
        return results;
    }
    
    static void print_events(const vector<MatchResult> &events, int max_print = 20) {
        if (events.empty()) {
            cout << "  No events" << endl;
            return;
        }
        
        int n = std::min((int)events.size(), max_print);
        for (int i = 0; i < n; ++i) {
            cout << "  Event " << i << ": Pattern " << events[i].pattern_id
                 << " at byte " << events[i].byte_index
                 << " (lane " << (int)events[i].lane << ")" << endl;
        }
        
        if ((int)events.size() > max_print) {
            cout << "  ... +" << (events.size() - max_print) << " more" << endl;
        }
    }
};

// ============================================================================
// Payload Validator - Byte-by-byte comparison
// ============================================================================

class PayloadValidator {
public:
    static int validate_payload(const unsigned char *input,
                               const unsigned char *output,
                               int size) {
        int mismatches = 0;
        
        cout << "[VALIDATOR] Comparing " << size << " bytes..." << endl;
        
        for (int i = 0; i < size; ++i) {
            if (input[i] != output[i]) {
                if (mismatches < 10) {  // Print first 10 mismatches
                    cout << "  ❌ Mismatch at byte " << i << ": "
                         << "input=0x" << hex << (int)input[i]
                         << " output=0x" << (int)output[i] << dec << endl;
                }
                mismatches++;
            }
        }
        
        if (mismatches == 0) {
            cout << "  ✅ Payload matches perfectly!" << endl;
        } else {
            cout << "  ❌ Total mismatches: " << mismatches << endl;
        }
        
        return mismatches;
    }
};

// ============================================================================
// Pattern Injector - Insert known patterns for testing
// ============================================================================

class PatternInjector {
public:
    struct Injection {
        int position;
        int pattern_id;
        string pattern_text;
    };
    
    static vector<Injection> inject_patterns(unsigned char *buffer, int size,
                                            int num_injections = 3) {
        vector<Injection> injections;
        
        if (NUM_PATTERNS < 1) {
            cout << "[INJECTOR] No patterns defined" << endl;
            return injections;
        }
        
        cout << "[INJECTOR] Injecting patterns..." << endl;
        
        for (int idx = 0; idx < num_injections && idx < NUM_PATTERNS; ++idx) {
            // Build pattern string
            string pat;
            for (int i = 0; i < rules[idx].len; ++i) {
                pat += (char)used_bytes[rules[idx].byte_index[i]];
            }
            
            if (pat.size() < 2 || pat.size() > 32) {
                continue;
            }
            
            // Calculate position (spread across buffer)
            int pos = (idx + 1) * (size / (num_injections + 2));
            
            if (pos + (int)pat.size() < size) {
                for (int i = 0; i < (int)pat.size(); ++i) {
                    buffer[pos + i] = pat[i];
                }
                
                injections.push_back({pos, idx + 1, pat});
                
                cout << "  Pattern " << (idx + 1) << " \"" << pat 
                     << "\" at byte " << pos << endl;
            }
        }
        
        return injections;
    }
    
    // Inject pattern across beat/packet boundary
    static Injection inject_boundary_pattern(unsigned char *buffer, int size,
                                            int boundary_byte, int pattern_idx = 0) {
        if (NUM_PATTERNS < 1 || pattern_idx >= NUM_PATTERNS) {
            return {-1, -1, ""};
        }
        
        string pat;
        for (int i = 0; i < rules[pattern_idx].len; ++i) {
            pat += (char)used_bytes[rules[pattern_idx].byte_index[i]];
        }
        
        if (pat.size() < 2) {
            return {-1, -1, ""};
        }
        
        // Place pattern so it crosses the boundary
        int start_pos = boundary_byte - (int)pat.size() / 2;
        
        if (start_pos >= 0 && start_pos + (int)pat.size() < size) {
            for (int i = 0; i < (int)pat.size(); ++i) {
                buffer[start_pos + i] = pat[i];
            }
            
            cout << "[INJECTOR] Boundary pattern at bytes [" << start_pos
                 << "-" << (start_pos + pat.size() - 1)
                 << "] crossing boundary " << boundary_byte << endl;
            
            return {start_pos, pattern_idx + 1, pat};
        }
        
        return {-1, -1, ""};
    }
};

// ============================================================================
// Event Validator - Check if expected events are found
// ============================================================================

class EventValidator {
public:
    static void validate_events(const vector<MatchResult> &events,
                               const vector<PatternInjector::Injection> &expected,
                               TestStats &stats) {
        cout << "[VALIDATOR] Checking " << expected.size() 
             << " expected patterns..." << endl;
        
        stats.missing_events = 0;
        stats.false_positives = 0;
        
        for (const auto &exp : expected) {
            bool found = false;
            int end_pos = exp.position + exp.pattern_text.size() - 1;
            
            for (const auto &evt : events) {
                if (evt.pattern_id == exp.pattern_id) {
                    int evt_end = evt.byte_index;
                    
                    // Check if event is within pattern range
                    if ((int)evt.byte_index >= exp.position && 
                        (int)evt.byte_index <= end_pos) {
                        found = true;
                        cout << "  ✅ Found pattern " << exp.pattern_id 
                             << " at byte " << evt.byte_index << endl;
                        break;
                    }
                }
            }
            
            if (!found) {
                cout << "  ❌ Missing pattern " << exp.pattern_id 
                     << " expected at byte " << exp.position << endl;
                stats.missing_events++;
            }
        }
        
        // Check for unexpected events (false positives)
        // This is tricky - we'd need to scan the entire input
        // For now, just report if we got more events than expected
        if ((int)events.size() > (int)expected.size()) {
            cout << "  ⚠️  Got " << events.size() << " events but expected only "
                 << expected.size() << " (possible false positives)" << endl;
            stats.false_positives = events.size() - expected.size();
        }
    }
};

// ============================================================================
// Test Cases
// ============================================================================

bool test_basic_passthrough() {
    cout << "\n============================================" << endl;
    cout << "TEST 1: Basic Payload Passthrough" << endl;
    cout << "============================================" << endl;
    
    const int INPUT_SIZE = 1024;
    unsigned char input_data[INPUT_SIZE];
    unsigned char payload_output[INPUT_SIZE];
    unsigned char event_output[4096];
    
    // Fill with known pattern
    for (int i = 0; i < INPUT_SIZE; ++i) {
        input_data[i] = (unsigned char)(i % 256);
    }
    
    memset(payload_output, 0, INPUT_SIZE);
    memset(event_output, 0, 4096);
    
    // Create streams
    hls::stream<pkt> n2k("n2k");
    hls::stream<pkt> k2n_payload("k2n_payload");
    hls::stream<pkt> k2n_events("k2n_events");
    
    // MM2S
    MM2S_Simulator mm2s;
    int packets_sent = mm2s.send_data(n2k, input_data, INPUT_SIZE);
    
    // DUT
    cout << "[DUT] Processing " << packets_sent << " packets..." << endl;
    krnl_proj(n2k, k2n_payload, k2n_events, 0, 1, packets_sent);
    
    // Dual S2MM
    DualS2MM_Simulator s2mm(payload_output, INPUT_SIZE, 
                            event_output, 4096);
    bool no_deadlock = s2mm.receive_both_streams(k2n_payload, k2n_events, 
                                                  packets_sent);
    
    // Validate payload
    TestStats stats = {0};
    stats.packets_sent = packets_sent;
    stats.packets_received_payload = s2mm.get_payload_packets();
    stats.bytes_sent = INPUT_SIZE;
    stats.bytes_received_payload = s2mm.get_payload_bytes();
    stats.deadlock_detected = !no_deadlock;
    
    stats.payload_mismatches = PayloadValidator::validate_payload(
        input_data, payload_output, INPUT_SIZE);
    
    auto events = EventParser::parse_events(event_output, 
                                            s2mm.get_event_bytes());
    stats.events_found = events.size();
    
    stats.print();
    return stats.is_pass();
}

bool test_pattern_detection() {
    cout << "\n============================================" << endl;
    cout << "TEST 2: Pattern Detection Accuracy" << endl;
    cout << "============================================" << endl;
    
    const int INPUT_SIZE = 4096;
    unsigned char input_data[INPUT_SIZE];
    unsigned char payload_output[INPUT_SIZE];
    unsigned char event_output[8192];
    
    // Fill with random data
    srand(12345);
    for (int i = 0; i < INPUT_SIZE; ++i) {
        input_data[i] = (unsigned char)(rand() % 256);
    }
    
    // Inject known patterns
    auto injections = PatternInjector::inject_patterns(input_data, INPUT_SIZE, 5);
    
    memset(payload_output, 0, INPUT_SIZE);
    memset(event_output, 0, 8192);
    
    // Streams
    hls::stream<pkt> n2k("n2k");
    hls::stream<pkt> k2n_payload("k2n_payload");
    hls::stream<pkt> k2n_events("k2n_events");
    
    // MM2S
    MM2S_Simulator mm2s;
    int packets_sent = mm2s.send_data(n2k, input_data, INPUT_SIZE);
    
    // DUT
    krnl_proj(n2k, k2n_payload, k2n_events, 0, 1, packets_sent);
    
    // S2MM
    DualS2MM_Simulator s2mm(payload_output, INPUT_SIZE, 
                            event_output, 8192);
    bool no_deadlock = s2mm.receive_both_streams(k2n_payload, k2n_events, 
                                                  packets_sent);
    
    // Validate
    TestStats stats = {0};
    stats.packets_sent = packets_sent;
    stats.packets_received_payload = s2mm.get_payload_packets();
    stats.deadlock_detected = !no_deadlock;
    
    stats.payload_mismatches = PayloadValidator::validate_payload(
        input_data, payload_output, INPUT_SIZE);
    
    auto events = EventParser::parse_events(event_output, 
                                            s2mm.get_event_bytes());
    stats.events_found = events.size();
    
    EventParser::print_events(events);
    EventValidator::validate_events(events, injections, stats);
    
    stats.print();
    return stats.is_pass();
}

bool test_boundary_crossing() {
    cout << "\n============================================" << endl;
    cout << "TEST 3: Pattern Across Beat Boundary" << endl;
    cout << "============================================" << endl;
    
    const int INPUT_SIZE = 256;
    unsigned char input_data[INPUT_SIZE];
    unsigned char payload_output[INPUT_SIZE];
    unsigned char event_output[2048];
    
    // Fill with spaces
    memset(input_data, ' ', INPUT_SIZE);
    
    // Inject pattern across beat boundary (at byte 64)
    auto injection = PatternInjector::inject_boundary_pattern(
        input_data, INPUT_SIZE, 64, 0);
    
    if (injection.position == -1) {
        cout << "⚠️  SKIP: Pattern not suitable for boundary test" << endl;
        return true;
    }
    
    vector<PatternInjector::Injection> injections;
    injections.push_back(injection);
    
    memset(payload_output, 0, INPUT_SIZE);
    memset(event_output, 0, 2048);
    
    // Streams
    hls::stream<pkt> n2k("n2k");
    hls::stream<pkt> k2n_payload("k2n_payload");
    hls::stream<pkt> k2n_events("k2n_events");
    
    // MM2S
    MM2S_Simulator mm2s;
    int packets_sent = mm2s.send_data(n2k, input_data, INPUT_SIZE);
    
    // DUT
    krnl_proj(n2k, k2n_payload, k2n_events, 0, 1, packets_sent);
    
    // S2MM
    DualS2MM_Simulator s2mm(payload_output, INPUT_SIZE, 
                            event_output, 2048);
    bool no_deadlock = s2mm.receive_both_streams(k2n_payload, k2n_events, 
                                                  packets_sent);
    
    // Validate
    TestStats stats = {0};
    stats.packets_sent = packets_sent;
    stats.packets_received_payload = s2mm.get_payload_packets();
    stats.deadlock_detected = !no_deadlock;
    
    stats.payload_mismatches = PayloadValidator::validate_payload(
        input_data, payload_output, INPUT_SIZE);
    
    auto events = EventParser::parse_events(event_output, 
                                            s2mm.get_event_bytes());
    stats.events_found = events.size();
    
    EventParser::print_events(events);
    EventValidator::validate_events(events, injections, stats);
    
    stats.print();
    return stats.is_pass();
}

bool test_stress_large_data() {
    cout << "\n============================================" << endl;
    cout << "TEST 4: Stress Test (Large Data)" << endl;
    cout << "============================================" << endl;
    
    const int INPUT_SIZE = 16384;  // 16KB
    unsigned char *input_data = new unsigned char[INPUT_SIZE];
    unsigned char *payload_output = new unsigned char[INPUT_SIZE];
    unsigned char *event_output = new unsigned char[32768];  // 32KB for events
    
    // Random data
    srand(99999);
    for (int i = 0; i < INPUT_SIZE; ++i) {
        input_data[i] = (unsigned char)(rand() % 256);
    }
    
    // Inject patterns
    auto injections = PatternInjector::inject_patterns(input_data, INPUT_SIZE, 10);
    
    memset(payload_output, 0, INPUT_SIZE);
    memset(event_output, 0, 32768);
    
    // Streams
    hls::stream<pkt> n2k("n2k");
    hls::stream<pkt> k2n_payload("k2n_payload");
    hls::stream<pkt> k2n_events("k2n_events");
    
    // MM2S
    MM2S_Simulator mm2s;
    int packets_sent = mm2s.send_data(n2k, input_data, INPUT_SIZE);
    
    cout << "[DUT] Processing " << packets_sent << " packets..." << endl;
    
    // DUT
    krnl_proj(n2k, k2n_payload, k2n_events, 0, 1, packets_sent);
    
    // S2MM
    DualS2MM_Simulator s2mm(payload_output, INPUT_SIZE, 
                            event_output, 32768);
    bool no_deadlock = s2mm.receive_both_streams(k2n_payload, k2n_events, 
                                                  packets_sent);
    
    // Validate
    TestStats stats = {0};
    stats.packets_sent = packets_sent;
    stats.packets_received_payload = s2mm.get_payload_packets();
    stats.deadlock_detected = !no_deadlock;
    stats.bytes_sent = INPUT_SIZE;
    stats.bytes_received_payload = s2mm.get_payload_bytes();
    
    stats.payload_mismatches = PayloadValidator::validate_payload(
        input_data, payload_output, INPUT_SIZE);
    
    auto events = EventParser::parse_events(event_output, 
                                            s2mm.get_event_bytes());
    stats.events_found = events.size();
    
    EventParser::print_events(events, 10);
    EventValidator::validate_events(events, injections, stats);
    
    stats.print();
    
    bool pass = stats.is_pass();
    
    delete[] input_data;
    delete[] payload_output;
    delete[] event_output;
    
    return pass;
}

bool test_multi_packet_continuous() {
    cout << "\n============================================" << endl;
    cout << "TEST 5: Multi-Packet Continuous Stream" << endl;
    cout << "============================================" << endl;
    
    const int INPUT_SIZE = 10000;  // Will create multiple packets
    unsigned char *input_data = new unsigned char[INPUT_SIZE];
    unsigned char *payload_output = new unsigned char[INPUT_SIZE];
    unsigned char *event_output = new unsigned char[32768];
    
    // Generate continuous stream with patterns
    srand(55555);
    for (int i = 0; i < INPUT_SIZE; ++i) {
        input_data[i] = (unsigned char)((rand() % 64) + 32);  // Printable ASCII
    }
    
    // Inject patterns at regular intervals
    auto injections = PatternInjector::inject_patterns(input_data, INPUT_SIZE, 8);
    
    memset(payload_output, 0, INPUT_SIZE);
    memset(event_output, 0, 32768);
    
    // Streams
    hls::stream<pkt> n2k("n2k");
    hls::stream<pkt> k2n_payload("k2n_payload");
    hls::stream<pkt> k2n_events("k2n_events");
    
    // MM2S
    MM2S_Simulator mm2s;
    int packets_sent = mm2s.send_data(n2k, input_data, INPUT_SIZE);
    
    cout << "[INFO] Will generate ~" << packets_sent << " packets" << endl;
    
    // DUT
    krnl_proj(n2k, k2n_payload, k2n_events, 0, 1, packets_sent);
    
    // S2MM
    DualS2MM_Simulator s2mm(payload_output, INPUT_SIZE, 
                            event_output, 32768);
    bool no_deadlock = s2mm.receive_both_streams(k2n_payload, k2n_events, 
                                                  packets_sent);
    
    // Validate
    TestStats stats = {0};
    stats.packets_sent = packets_sent;
    stats.packets_received_payload = s2mm.get_payload_packets();
    stats.deadlock_detected = !no_deadlock;
    stats.bytes_sent = INPUT_SIZE;
    stats.bytes_received_payload = s2mm.get_payload_bytes();
    
    stats.payload_mismatches = PayloadValidator::validate_payload(
        input_data, payload_output, INPUT_SIZE);
    
    auto events = EventParser::parse_events(event_output, 
                                            s2mm.get_event_bytes());
    stats.events_found = events.size();
    
    EventParser::print_events(events, 15);
    EventValidator::validate_events(events, injections, stats);
    
    stats.print();
    
    bool pass = stats.is_pass();
    
    delete[] input_data;
    delete[] payload_output;
    delete[] event_output;
    
    return pass;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    cout << "======================================================" << endl;
    cout << "  Dual-Output DCAM Testbench" << endl;
    cout << "  Architecture: Payload + Events (Separate Streams)" << endl;
    cout << "  DWIDTH = " << DWIDTH << endl;
    cout << "  DATA_WIDTH_BYTES = " << DATA_WIDTH_BYTES << endl;
    cout << "  NUM_PATTERNS = " << NUM_PATTERNS << endl;
    cout << "======================================================" << endl;
    
    bool all_pass = true;
    
    all_pass &= test_basic_passthrough();
    all_pass &= test_pattern_detection();
    all_pass &= test_boundary_crossing();
    all_pass &= test_stress_large_data();
    all_pass &= test_multi_packet_continuous();
    
    cout << "\n======================================================" << endl;
    if (all_pass) {
        cout << "  ✅✅✅ ALL TESTS PASSED ✅✅✅" << endl;
    } else {
        cout << "  ❌❌❌ SOME TESTS FAILED ❌❌❌" << endl;
    }
    cout << "======================================================" << endl;
    
    return all_pass ? 0 : 1;
}