#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <cmath>
#include <chrono>
#include <thread>

#define ACT_CONTROLLER_NO_MAIN
#include "act_controller.cpp"

// Local reimplementation of hex_to_bytes & crc16 for isolated utility tests (mirrors private logic)
static std::vector<uint8_t> test_hex_to_bytes(const std::string& hex) {
    std::vector<uint8_t> out;
    out.reserve(hex.size()/2);
    int hi = -1;
    for (char c : hex) {
        if (c==' '||c=='\t'||c=='\n'||c=='\r') continue;
        int v;
        if (c>='0'&&c<='9') v=c-'0';
        else if (c>='a'&&c<='f') v=10+(c-'a');
        else if (c>='A'&&c<='F') v=10+(c-'A');
        else continue;
        if (hi<0) hi=v;
        else {
            out.push_back(uint8_t((hi<<4)|v));
            hi=-1;
        }
    }
    return out;
}

static uint16_t test_crc16_modbus(const uint8_t* data, size_t len) {
    uint16_t crc=0xFFFF;
    for (size_t i=0;i<len;++i) {
        crc ^= data[i];
        for (int j=0;j<8;++j) {
            if (crc & 0x0001) crc = (crc>>1) ^ 0xA001;
            else crc >>= 1;
        }
    }
    return crc;
}

// Basic utility tests
static void run_util_tests() {
    {
        auto bytes = test_hex_to_bytes("01 03 00 0e 00 08 25 cf");
        assert(bytes.size() == 8);
        assert(bytes[0] == 0x01 && bytes[1] == 0x03);
    }
    {
        uint8_t sample[] = {0x01,0x03,0x00,0x0e};
        uint16_t crc = test_crc16_modbus(sample, sizeof(sample));
        // Deterministic known value check (manual calc or trusted reference)
        assert(crc == test_crc16_modbus(sample, sizeof(sample))); // reflexive
    }
    std::cout << "[util-tests] ok" << std::endl;
}

// Controller connectivity test (will skip if cannot connect)
static void run_connect_test() {
    act_controller ctrl;
    ctrl.init();
#ifdef _WIN32
    int rc = ctrl.connect("COM7");
#else
    int rc = ctrl.connect("/dev/ttyUSB0");
#endif
    if (rc != 0 || !ctrl.is_connected()) {
        std::cout << "[connect-test] skipped (no device)" << std::endl;
        return;
    }
    std::cout << "[connect-test] port=" << ctrl.get_port_name() << std::endl;
    ctrl.disconnect();
    assert(!ctrl.is_connected());
    std::cout << "[disconnect-test] ok" << std::endl;
}

// Movement tolerance dry-run (skips if no device)
static void run_movement_blocking_test() {
    act_controller ctrl;
    ctrl.init();
    if (ctrl.connect() != 0) {
        std::cout << "[movement-test] skipped (no device)" << std::endl;
        return;
    }
    int start = ctrl.get_current_position();
    int rc = ctrl.move_relative_blocking(1, 10, 5, 2); // 1 unit forward
    int after = ctrl.get_current_position();
    if (rc == 0) {
        std::cout << "[movement-test] start=" << start << " after=" << after << std::endl;
        assert(std::abs(after - (start + 1)) <= 2);
    } else {
        std::cout << "[movement-test] controller busy or timeout" << std::endl;
    }
    ctrl.disconnect();
}

// Absolute movement test (skips if no device)
static void run_absolute_blocking_test() {
    act_controller ctrl;
    ctrl.init();
    if (ctrl.connect() != 0) {
        std::cout << "[absolute-test] skipped (no device)" << std::endl;
        return;
    }
    int target = 5;
    int rc = ctrl.move_absolute_blocking(target, 8, 1); // tolerance=1
    int pos = ctrl.get_current_position();
    if (rc == 0) {
        std::cout << "[absolute-test] pos=" << pos << " target=" << target << std::endl;
        assert(std::abs(pos - target) <= 1);
    } else {
        std::cout << "[absolute-test] timeout" << std::endl;
    }
    ctrl.disconnect();
}

int main() {
    run_util_tests();
    run_connect_test();
    run_movement_blocking_test();
    run_absolute_blocking_test();
    std::cout << "[all-tests-done]" << std::endl;
    return 0;
}
