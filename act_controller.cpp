#include "asio.hpp"
#include <asio/serial_port.hpp>
#include <vector>
#include <string>
#include <cstdint>
#include <iostream>
#include <chrono>
#include <thread>
#include <optional>
#include <random> // added
#include <cmath>  // added for std::abs
#include <algorithm> // added for std::clamp

class act_controller {
public:
    act_controller()
        : io_(), port_(io_), connected_(false) {}

    void init() {
        // No-op for now. Reserved for future setup.
    }
    // Hàm này có lỗi. Có thể phải thay bằng hàm m đã viết.
    
    // Auto-connect to the first responsive controller on COM1..COM32.
    // Returns 0 if successful, non-zero otherwise.
    int connect(const std::string& user_com_port = std::string()) {
        // Prefer explicit port (or platform default) before scanning
        if (connected_) return 0;
        bool had_user = !user_com_port.empty();
        std::string requested = user_com_port;
        try {
#ifdef _WIN32
            const std::string preferred = had_user ? user_com_port : std::string("COM7");
#else
            const std::string preferred = had_user ? user_com_port : std::string("/dev/ttyUSB0");
#endif
            open_and_configure(preferred);

            // Probe to ensure it's responsive
            std::vector<uint8_t> probe = hex_to_bytes("01 03 90 00 00 10 69 06");
            write_frame(probe);
            auto rx = read_modbus_response();
            if (rx && validate_crc(*rx)) {
                // Same initialization sequence as below
                {
                    const std::string human_cmd = "01 03 00 0e 00 08 25 cf";
                    const auto cmd_bytes = hex_to_bytes(human_cmd);
                    asio::write(port_, asio::buffer(cmd_bytes.data(), cmd_bytes.size()));
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                {
                    const std::string human_cmd2 = "01 03 00 52 00 02 65 da";
                    const auto cmd_bytes2 = hex_to_bytes(human_cmd2);
                    asio::write(port_, asio::buffer(cmd_bytes2.data(), cmd_bytes2.size()));
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                {
                    auto send_hex = [&](const std::string& s) {
                        const auto b = hex_to_bytes(s);
                        asio::write(port_, asio::buffer(b.data(), b.size()));
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    };
                    const std::vector<std::string> seq = {
                        "01 03 00 00 00 70 44 2e","01 03 00 70 00 70 45 f5","01 03 00 e0 00 70 45 d8",
                        "01 03 01 50 00 30 44 33","01 03 03 80 00 40 45 96","01 03 04 00 00 70 45 1e",
                        "01 03 04 70 00 70 44 c5","01 03 04 e0 00 70 44 e8","01 03 05 50 00 70 44 f3",
                        "01 03 05 c0 00 70 44 de","01 03 06 30 00 70 44 a9","01 03 06 a0 00 70 44 84",
                        "01 03 07 10 00 70 44 9f","01 03 07 80 00 70 44 b2","01 03 07 f0 00 10 45 41",
                        "01 03 90 11 00 02 b9 0e","01 05 00 30 ff 00 8c 35","01 05 00 19 ff 00 5d fd"
                    };
                    for (const auto& s : seq) send_hex(s);
                }
                {
                    char response_buffer[128];
                    asio::error_code ec;
                    std::size_t bytes_read = port_.read_some(asio::buffer(response_buffer), ec);
                    (void)bytes_read;
                }
                connected_ = true;
                port_name_ = preferred;
                if (had_user && preferred != requested) {
                    std::cout << "[port-info] Requested " << requested << " connected as " << preferred << std::endl;
                }
                return 0;
            }
        } catch (...) {
            safe_close();
            connected_ = false;
            port_name_.clear();
            // fall through to the existing scan below
        }
        if (connected_) return 0;
        try {
            // Scan for responsive COM ports
            const std::vector<std::string> candidates = make_port_list();
            std::string name;
            for (const auto& cand : candidates) {
                try {
                    open_and_configure(cand);
                    std::vector<uint8_t> probe = hex_to_bytes("01 03 90 00 00 10 69 06");
                    write_frame(probe);
                    auto rx = read_modbus_response();
                    if (rx && validate_crc(*rx)) { name = cand; break; }
                } catch (...) {
                    // ignore
                }
                safe_close();
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            if (name.empty()) {
                if (had_user)
                    std::cout << "[port-error] Requested " << requested << " not found or unresponsive." << std::endl;
                return 1;
            }
            if (had_user && name != requested) {
                std::cout << "[port-info] Requested " << requested << " not responsive, using " << name << std::endl;
            }
            // ...existing initialization sequence (same as above)...
            {
                const std::string human_cmd = "01 03 00 0e 00 08 25 cf";
                const auto cmd_bytes = hex_to_bytes(human_cmd);
                asio::write(port_, asio::buffer(cmd_bytes.data(), cmd_bytes.size()));
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            // Send another human-readable hex command right after the last human_cmd
            {
                const std::string human_cmd2 = "01 03 00 52 00 02 65 da";
                const auto cmd_bytes2 = hex_to_bytes(human_cmd2);
                asio::write(port_, asio::buffer(cmd_bytes2.data(), cmd_bytes2.size()));
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            // Send a batch of additional human-readable hex commands
            {
                auto send_hex = [&](const std::string& s) {
                    const auto b = hex_to_bytes(s);
                    asio::write(port_, asio::buffer(b.data(), b.size()));
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                };
                const std::vector<std::string> seq = {
                    "01 03 00 00 00 70 44 2e","01 03 00 70 00 70 45 f5","01 03 00 e0 00 70 45 d8",
                    "01 03 01 50 00 30 44 33","01 03 03 80 00 40 45 96","01 03 04 00 00 70 45 1e",
                    "01 03 04 70 00 70 44 c5","01 03 04 e0 00 70 44 e8","01 03 05 50 00 70 44 f3",
                    "01 03 05 c0 00 70 44 de","01 03 06 30 00 70 44 a9","01 03 06 a0 00 70 44 84",
                    "01 03 07 10 00 70 44 9f","01 03 07 80 00 70 44 b2","01 03 07 f0 00 10 45 41",
                    "01 03 90 11 00 02 b9 0e","01 05 00 30 ff 00 8c 35","01 05 00 19 ff 00 5d fd"
                };
                for (const auto& s : seq) send_hex(s);
            }

            // Read a short response
            {
                char response_buffer[128];
                asio::error_code ec;
                (void)port_.read_some(asio::buffer(response_buffer), ec);
            }
            connected_ = true;
            port_name_ = name;
            return 0;
        } catch (...) {
            safe_close();
            connected_ = false;
            port_name_.clear();
            return 1;
        }
    }
        // Reset controller (reverse engineered)
    void reset() {
        if (!connected_) return;
        const std::vector<std::string> seq = {
            // "01 03 00 0e 00 08 25 cf",
            // "01 03 00 52 00 02 65 da",
            // "01 03 00 00 00 70 44 2e",
            // "01 03 00 70 00 70 45 f5",
            // "01 03 00 e0 00 70 45 d8",
            // "01 03 01 50 00 30 44 33",
            // "01 03 03 80 00 40 45 96",
            // "01 03 04 00 00 70 45 1e",
            // "01 03 04 70 00 70 44 c5",
            // "01 03 04 e0 00 70 44 e8",
            // "01 03 05 50 00 70 44 f3",
            // "01 03 05 c0 00 70 44 de",
            // "01 03 06 30 00 70 44 a9",
            // "01 03 06 a0 00 70 44 84",
            // "01 03 07 10 00 70 44 9f",
            // "01 03 07 80 00 70 44 b2",
            // "01 03 07 f0 00 10 45 41",
            // "01 03 90 11 00 02 b9 0e",
            // repeated probe frames
            // "01 03 03 80 00 40 45 96","01 03 03 80 00 40 45 96","01 03 03 80 00 40 45 96",
            // "01 03 03 80 00 40 45 96","01 03 03 80 00 40 45 96","01 03 03 80 00 40 45 96",
            // "01 03 03 80 00 40 45 96","01 03 03 80 00 40 45 96","01 03 03 80 00 40 45 96",
            "01 03 03 80 00 40 45 96",
            // reset command
            "01 05 00 45 ff 00 9d ef",
            // final probe
            // "01 03 03 80 00 40 45 96",
            "01 05 00 1c ff 00 4d fc", // added
            "01 05 00 1c 00 00 0c 0c"  // added
        };
        for (const auto& s : seq) {
            const auto frame = hex_to_bytes(s);
            asio::write(port_, asio::buffer(frame.data(), frame.size()));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        // consume any trailing acks
        asio::steady_timer timer(io_);
        unsigned char buf[32];
        timer.expires_after(std::chrono::milliseconds(150));
        timer.async_wait([&](const asio::error_code&){ port_.cancel(); });
        port_.async_read_some(asio::buffer(buf, sizeof(buf)),
            [&](const asio::error_code&, std::size_t){});
        io_.run();
        io_.restart();
    }


    // Close the serial connection if open.
    void disconnect() {
        safe_close();
        connected_ = false;
        port_name_.clear();
    }

    // Move to origin (placeholder - no specific origin command known; implement when available).
    // For now, just move to absolute position 0.
    void move_to_origin() {
        move_absolute(0);
    }

    // Move relative by magnitude (positive or negative).
    void move_relative(int magnitude, [[maybe_unused]] int move_speed /* [1..30] */ = 10) {
        if (!connected_ || magnitude == 0) return;
        int spd = move_speed;
        if (spd < 1) spd = 1;
        else if (spd > 30) spd = 30;
        // std::cout << "Speed: " << spd << '\n';

        // Big-endian bytes to use in command frames
        unsigned char speed_hi = static_cast<unsigned char>((spd >> 8) & 0xFF);
        unsigned char speed_lo = static_cast<unsigned char>(spd & 0xFF);
        // std::cout << "Speed bytes (hi, lo): 0x" << std::hex
        //       << static_cast<int>(speed_hi) << " 0x" << static_cast<int>(speed_lo)
        //       << std::dec << '\n';
        // Compute hex bytes (big-endian) for use in command array
        if (magnitude > 0) {
            uint16_t val = static_cast<uint16_t>(magnitude * 100);
            unsigned char hi = static_cast<unsigned char>((val >> 8) & 0xFF);
            unsigned char lo = static_cast<unsigned char>(val & 0xFF);

            // Build mutable command without trailing CRC; inject hi/lo; then append computed CRC
            std::vector<unsigned char> command = {
                0x01, 0x10, 0x91, 0x02, 0x00, 0x10, 0x20, 0x00,
                0x02, speed_hi, speed_lo, 0x00, 0x00, hi, lo, 0x03,
                0xe8, 0x03, 0xe8, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x01, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x32
            };
            uint16_t crc = crc16_modbus(command.data(), command.size());
            command.push_back(static_cast<unsigned char>(crc & 0xFF));        // CRC low
            command.push_back(static_cast<unsigned char>((crc >> 8) & 0xFF)); // CRC high

            asio::write(port_, asio::buffer(command.data(), command.size()));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            const unsigned char trigger[] = {0x01, 0x10, 0x91, 0x00, 0x00, 0x01, 0x02, 0x01, 0x00, 0x27, 0x09};
            asio::write(port_, asio::buffer(trigger, sizeof(trigger)));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } else {
            // Negative case: copy controller_minus flow (ff ff hi lo, CRC, trigger, then tail last)
            uint16_t val = static_cast<uint16_t>(magnitude * 100); // two's complement
            unsigned char hi = static_cast<unsigned char>((val >> 8) & 0xFF);
            unsigned char lo = static_cast<unsigned char>(val & 0xFF);

            std::vector<unsigned char> command = {
                0x01, 0x10, 0x91, 0x02, 0x00, 0x10, 0x20, 0x00,
                0x02, speed_hi, speed_lo, 0xFF, 0xFF, hi, lo, 0x03,
                0xE8, 0x03, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x01, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x32
            };
            uint16_t crc = crc16_modbus(command.data(), command.size());
            command.push_back(static_cast<unsigned char>(crc & 0xFF));        // CRC low
            command.push_back(static_cast<unsigned char>((crc >> 8) & 0xFF)); // CRC high

            asio::write(port_, asio::buffer(command.data(), command.size()));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            const unsigned char trigger[] = {0x01, 0x10, 0x91, 0x00, 0x00, 0x01, 0x02, 0x01, 0x00, 0x27, 0x09};
            asio::write(port_, asio::buffer(trigger, sizeof(trigger)));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            const unsigned char tail[] = {0x01, 0x05, 0x00, 0x1a, 0x00, 0x00, 0xec, 0x0d};
            asio::write(port_, asio::buffer(tail, sizeof(tail)));  // send this last
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Timed, non-blocking read to avoid hanging (unchanged)
        std::vector<unsigned char> rb; rb.reserve(32);
        asio::steady_timer timer(io_);
        bool data_received = false;
        asio::error_code ec;

        unsigned char buf[32];
        std::size_t n = 0;

        timer.expires_after(std::chrono::milliseconds(300));
        timer.async_wait([&](const asio::error_code&){ port_.cancel(); });

        port_.async_read_some(asio::buffer(buf, sizeof(buf)), [&](const asio::error_code& error, std::size_t bytes_transferred) {
            if (!error && bytes_transferred > 0) {
                rb.insert(rb.end(), buf, buf + bytes_transferred);
                data_received = true;
            }
        });

        io_.run();
        io_.restart();
    }

    // Move to absolute position (units in same external unit as get_current_position()).
    // Internally scale by 100 to device units (big-endian 16-bit).
    void move_absolute(int position, int speed = 10) {
        if (!connected_) return;
        // clamp speed
        if (speed < 1) speed = 1;
        // else if (speed > 30) speed = 30;
        unsigned char speed_hi = static_cast<unsigned char>((speed >> 8) & 0xFF);
        unsigned char speed_lo = static_cast<unsigned char>(speed & 0xFF);

        int scaled = position * 100;
        if (scaled < 0) scaled = 0;
        if (scaled > 0xFFFF) scaled = 0xFFFF;
        uint16_t abs_mov_u16 = static_cast<uint16_t>(scaled);
        unsigned char abs_hi = static_cast<unsigned char>((abs_mov_u16 >> 8) & 0xFF);
        unsigned char abs_lo = static_cast<unsigned char>(abs_mov_u16 & 0xFF);

        // speed frame: 01 10 04 11 00 01 02 <speed_hi> <speed_lo> CRC(lo,hi)
        std::vector<unsigned char> speed_frame = {
            0x01, 0x10, 0x04, 0x11, 0x00, 0x01, 0x02, speed_hi, speed_lo
        };
        {
            uint16_t crc = crc16_modbus(speed_frame.data(), speed_frame.size());
            speed_frame.push_back(static_cast<unsigned char>(crc & 0xFF));
            speed_frame.push_back(static_cast<unsigned char>((crc >> 8) & 0xFF));
            asio::write(port_, asio::buffer(speed_frame.data(), speed_frame.size()));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // position frame (unchanged logic)
        std::vector<unsigned char> frame = {
            0x01, 0x10, 0x04, 0x12, 0x00, 0x02, 0x04, 0x00, 0x00
        };  
        frame.push_back(abs_hi);
        frame.push_back(abs_lo);
        uint16_t crc = crc16_modbus(frame.data(), frame.size());
        frame.push_back(static_cast<unsigned char>(crc & 0xFF));
        frame.push_back(static_cast<unsigned char>((crc >> 8) & 0xFF));

        asio::write(port_, asio::buffer(frame.data(), frame.size()));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Additional hex command sequence (same as controller_absolute_movement)
        {
            auto send_hex = [&](const std::string& s) {
                const auto b = hex_to_bytes(s);
                asio::write(port_, asio::buffer(b.data(), b.size()));
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            };
            const std::vector<std::string> seq = {
                "01 05 00 1a 00 00 ec 0d",
                "01 0f 00 10 00 08 01 01 fe 96",
                "01 05 00 1a ff 00 ad fd",
                "01 05 00 1a 00 00 ec 0d"
            };
            for (const auto& s : seq) send_hex(s);
        }

        // Timed async read (300ms) to avoid hanging
        std::vector<unsigned char> rb; rb.reserve(32);
        asio::steady_timer timer(io_);
        unsigned char buf[64];

        timer.expires_after(std::chrono::milliseconds(300));
        timer.async_wait([&](const asio::error_code&) { port_.cancel(); });

        port_.async_read_some(asio::buffer(buf, sizeof(buf)),
            [&](const asio::error_code& error, std::size_t bytes_transferred) {
                if (!error && bytes_transferred > 0) {
                    rb.insert(rb.end(), buf, buf + bytes_transferred);
                }
            });

        io_.run();
        io_.restart();
    }

    // Returns true if connected.
    bool is_connected() const {
        return connected_;
    }

    // Returns current position in external unit (scaled down by 100).
    // If unavailable, returns 0.
    int get_current_position() {
        if (!connected_) return 0;
        try {
            // Drain any leftover bytes from previous commands to avoid corrupting this read
            auto drain_incoming = [&](int window_ms) {
                using namespace std::chrono;
                auto deadline = steady_clock::now() + milliseconds(window_ms);
                unsigned char buf[128];
                while (steady_clock::now() < deadline) {
                    asio::steady_timer timer(io_);
                    bool got = false;
                    timer.expires_after(milliseconds(20));
                    timer.async_wait([&](const asio::error_code&) { port_.cancel(); });
                    port_.async_read_some(asio::buffer(buf, sizeof(buf)),
                        [&](const asio::error_code& ec, std::size_t n) {
                            if (!ec && n) got = true; // just consume
                        });
                    io_.run();
                    io_.restart();
                    if (got) {
                        // extend quiet window to keep draining while data arrives
                        deadline = steady_clock::now() + milliseconds(20);
                    }
                }
            };
            drain_incoming(60);

            // TX request (same as controller_get_position.cpp)
            const std::vector<uint8_t> req = hex_to_bytes("01 03 90 00 00 10 69 06");
            asio::write(port_, asio::buffer(req.data(), req.size()));
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            // Read Modbus RTU response: header (3 bytes), then data + CRC (byteCount + 2)
            unsigned char hdr[3];
            asio::read(port_, asio::buffer(hdr, 3));
            const uint8_t byteCount = hdr[2];

            std::vector<unsigned char> tail(byteCount + 2);
            asio::read(port_, asio::buffer(tail.data(), tail.size()));

            // Assemble full frame
            std::vector<unsigned char> rx;
            rx.insert(rx.end(), hdr, hdr + 3);
            rx.insert(rx.end(), tail.begin(), tail.end());

            // CRC check
            if (rx.size() < 5) return 0;
            uint16_t calc_crc = crc16_modbus(rx.data(), rx.size() - 2);
            uint16_t recv_crc = static_cast<uint16_t>(rx[rx.size() - 2]) |
                                (static_cast<uint16_t>(rx[rx.size() - 1]) << 8);
            if (calc_crc != recv_crc) return 0;

            // Use bytes 5/6, interpret as signed 16-bit (scaled *100), then round
            if (rx.size() >= 7) {
                uint16_t raw = (static_cast<uint16_t>(rx[5]) << 8) | static_cast<uint16_t>(rx[6]);
                int16_t signed_raw = static_cast<int16_t>(raw); // sign extension
                // sign-aware rounding toward nearest integer
                int result;
                if (signed_raw >= 0)
                    result = (signed_raw + 50) / 100;
                else
                    result = (signed_raw - 50) / 100;
                return result;
            }
            return 0;
        } catch (...) {
            return 0;
        }
    }

    // Blocking variant: same motion as move_relative, but waits until target reached or timeout (seconds).
    // Returns 0 on success, non-zero on timeout or invalid input; tolerance specifies acceptable position error.
    int move_relative_blocking(int magnitude, int move_speed, int timeout_sec, int tolerance = 1) {
        if (!connected_ || magnitude == 0 || timeout_sec <= 0) return 1;

        // Compute expected target from current position before sending commands
        int start_pos = get_current_position();
        int expected = std::max(0, start_pos + magnitude);
        int spd = move_speed;
        std::cout << "Speed: " << spd << std::endl;
        if (spd < 1) spd = 1; //else if (spd > 30) spd = 30;

        // Build speed bytes (big-endian)
        unsigned char speed_hi = static_cast<unsigned char>((spd >> 8) & 0xFF);
        unsigned char speed_lo = static_cast<unsigned char>(spd & 0xFF);

        // Send the same frames as move_relative (positive vs negative)
        if (magnitude > 0) {
            uint16_t val = static_cast<uint16_t>(magnitude * 100);
            unsigned char hi = static_cast<unsigned char>((val >> 8) & 0xFF);
            unsigned char lo = static_cast<unsigned char>(val & 0xFF);

            std::vector<unsigned char> command = {
                0x01, 0x10, 0x91, 0x02, 0x00, 0x10, 0x20, 0x00,
                0x02, speed_hi, speed_lo, 0x00, 0x00, hi, lo, 0x03,
                0xe8, 0x03, 0xe8, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x01, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x32
            };
            uint16_t crc = crc16_modbus(command.data(), command.size());
            command.push_back(static_cast<unsigned char>(crc & 0xFF));
            command.push_back(static_cast<unsigned char>((crc >> 8) & 0xFF));

            asio::write(port_, asio::buffer(command.data(), command.size()));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            const unsigned char trigger[] = {0x01, 0x10, 0x91, 0x00, 0x00, 0x01, 0x02, 0x01, 0x00, 0x27, 0x09};
            asio::write(port_, asio::buffer(trigger, sizeof(trigger)));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } else {
            uint16_t val = static_cast<uint16_t>(magnitude * 100); // two's complement for negative
            unsigned char hi = static_cast<unsigned char>((val >> 8) & 0xFF);
            unsigned char lo = static_cast<unsigned char>(val & 0xFF);

            std::vector<unsigned char> command = {
                0x01, 0x10, 0x91, 0x02, 0x00, 0x10, 0x20, 0x00,
                0x02, speed_hi, speed_lo, 0xFF, 0xFF, hi, lo, 0x03,
                0xE8, 0x03, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x01, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x32
            };
            uint16_t crc = crc16_modbus(command.data(), command.size());
            command.push_back(static_cast<unsigned char>(crc & 0xFF));
            command.push_back(static_cast<unsigned char>((crc >> 8) & 0xFF));

            asio::write(port_, asio::buffer(command.data(), command.size()));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            const unsigned char trigger[] = {0x01, 0x10, 0x91, 0x00, 0x00, 0x01, 0x02, 0x01, 0x00, 0x27, 0x09};
            asio::write(port_, asio::buffer(trigger, sizeof(trigger)));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            const unsigned char tail[] = {0x01, 0x05, 0x00, 0x1a, 0x00, 0x00, 0xec, 0x0d};
            asio::write(port_, asio::buffer(tail, sizeof(tail)));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Optional short timed read to avoid hanging on unread acks
        {
            std::vector<unsigned char> rb; rb.reserve(32);
            asio::steady_timer timer(io_);
            unsigned char buf[32];
            timer.expires_after(std::chrono::milliseconds(300));
            timer.async_wait([&](const asio::error_code&){ port_.cancel(); });
            port_.async_read_some(asio::buffer(buf, sizeof(buf)),
                [&](const asio::error_code& error, std::size_t bytes_transferred) {
                    if (!error && bytes_transferred > 0) {
                        rb.insert(rb.end(), buf, buf + bytes_transferred);
                    }
                });
            io_.run();
            io_.restart();
        }

        // Poll until target reached or timeout
        using namespace std::chrono;
        const auto deadline = steady_clock::now() + seconds(timeout_sec);
        while (steady_clock::now() < deadline) {
            std::this_thread::sleep_for(milliseconds(120));
            int actual = get_current_position();
            if (std::abs(actual - expected) <= tolerance) return 0; // success within tolerance
        }
        return 1; // timeout
    }

    // Blocking variant: same as move_absolute, but waits until target reached or timeout (seconds).
    // Returns 0 on success, non-zero on timeout/invalid input; tolerance specifies acceptable position error.
    int move_absolute_blocking(int position, int speed, int timeout_sec, int tolerance = 1) {
        if (!connected_ || timeout_sec <= 0) return 1;
        if (speed < 1) speed = 1;
        // else if (speed > 30) speed = 30; // (unclamped upper if intentional)

        // Compute expected target (external units) with same clamp/scale as move_absolute
        int scaled = position * 100;
        if (scaled < 0) scaled = 0;
        if (scaled > 0xFFFF) scaled = 0xFFFF;
        const int expected = static_cast<int>((scaled + 50) / 100); // rounded to nearest ext unit

        move_absolute(position, speed);

        // Poll until target reached or timeout
        using namespace std::chrono;
        const auto deadline = steady_clock::now() + seconds(timeout_sec);
        while (steady_clock::now() < deadline) {
            std::this_thread::sleep_for(milliseconds(120));
            const int actual = get_current_position();
            if (std::abs(actual - expected) <= tolerance) return 0; // success within tolerance
        }
        return 1; // timeout
    }

    const std::string& get_port_name() const { return port_name_; }

private:
    // Helpers
    static std::vector<std::string> make_port_list() {
#ifdef _WIN32
        std::vector<std::string> ports;
        for (int i = 1; i <= 32; ++i) ports.push_back("COM" + std::to_string(i));
        return ports;
#else
        std::vector<std::string> ports;
        for (int i = 0; i < 10; ++i) ports.push_back("/dev/ttyUSB" + std::to_string(i));
        for (int i = 0; i < 32; ++i) ports.push_back("/dev/ttyS" + std::to_string(i));
        for (int i = 0; i < 64; ++i) ports.push_back("/dev/tty" + std::to_string(i));

        return ports;
#endif
    }

    void open_and_configure(const std::string& name) {
        port_.open(name);
        port_.set_option(asio::serial_port_base::baud_rate(38400));
        port_.set_option(asio::serial_port_base::character_size(8));
        port_.set_option(asio::serial_port_base::parity(asio::serial_port_base::parity::none));
        port_.set_option(asio::serial_port_base::stop_bits(asio::serial_port_base::stop_bits::one));
        port_.set_option(asio::serial_port_base::flow_control(asio::serial_port_base::flow_control::none));
    }

    void safe_close() {
        if (port_.is_open()) {
            asio::error_code ec;
            port_.close(ec);
        }
    }

    // CRC16 Modbus (A001 poly), returns crc; wire order is low byte, then high byte.
    static uint16_t crc16_modbus(const uint8_t* data, size_t len) {
        uint16_t crc = 0xFFFF;
        for (size_t i = 0; i < len; ++i) {
            crc ^= static_cast<uint8_t>(data[i]);
            for (int j = 0; j < 8; ++j) {
                if (crc & 0x0001) crc = (crc >> 1) ^ 0xA001;
                else crc >>= 1;
            }
        }
        return crc;
    }

    static void append_crc(std::vector<uint8_t>& frame) {
        uint16_t crc = crc16_modbus(frame.data(), frame.size());
        frame.push_back(static_cast<uint8_t>(crc & 0xFF));        // low
        frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF)); // high
    }

    static bool validate_crc(const std::vector<uint8_t>& frame) {
        if (frame.size() < 4) return false;
        uint16_t calc = crc16_modbus(frame.data(), frame.size() - 2);
        uint16_t recv = static_cast<uint16_t>(frame[frame.size() - 2]) |
                        (static_cast<uint16_t>(frame[frame.size() - 1]) << 8);
        return calc == recv;
    }

    static std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
        std::vector<uint8_t> out;
        out.reserve(hex.size() / 2);
        int hi = -1;
        for (char c : hex) {
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
            int v;
            if (c >= '0' && c <= '9') v = c - '0';
            else if (c >= 'a' && c <= 'f') v = 10 + (c - 'a');
            else if (c >= 'A' && c <= 'F') v = 10 + (c - 'A');
            else continue;
            if (hi < 0) hi = v;
            else {
                out.push_back(static_cast<uint8_t>((hi << 4) | v));
                hi = -1;
            }
        }
        return out;
    }

    void write_frame(const std::vector<uint8_t>& frame) {
        asio::write(port_, asio::buffer(frame.data(), frame.size()));
        // small delay to allow device to respond
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Read Modbus RTU response: first 3 bytes (addr, func, byteCount), then tail (byteCount + 2)
    std::optional<std::vector<uint8_t>> read_modbus_response() {
        try {
            uint8_t hdr[3];
            asio::read(port_, asio::buffer(hdr, 3));
            uint8_t byteCount = hdr[2];
            std::vector<uint8_t> tail(byteCount + 2);
            asio::read(port_, asio::buffer(tail.data(), tail.size()));
            std::vector<uint8_t> rx;
            rx.insert(rx.end(), hdr, hdr + 3);
            rx.insert(rx.end(), tail.begin(), tail.end());
            return rx;
        } catch (...) {
            return std::nullopt;
        }
    }

    // Read exactly n bytes (blocking, minimal error handling)
    bool read_exact(uint8_t* dst, std::size_t n) {
        try {
            std::size_t total = 0;
            while (total < n) {
                total += asio::read(port_, asio::buffer(dst + total, n - total));
            }
            return true;
        } catch (...) { return false; }
    }

private:
    asio::io_context io_;
    asio::serial_port port_;
    bool connected_;
    std::string port_name_;
};

// Randomized stress test for move_relative (+/-) with verification via get_current_position
static void stress_test_move_relative_blocking(act_controller& ctrl,
                                               int iterations,
                                               int min_pos,
                                               int max_pos,
                                               int max_step,
                                               int settle_timeout_ms = 1500,
                                               int tolerance = 1) {
    using namespace std::chrono;
    if (min_pos > max_pos) std::swap(min_pos, max_pos);
    if (min_pos < 0) min_pos = 0;

    std::mt19937 rng(static_cast<unsigned int>(steady_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<int> delta_dist(-max_step, max_step);
    std::uniform_int_distribution<int> spd_dist(1, 30);

    int pass = 0, fail = 0;

    int expected = std::clamp(ctrl.get_current_position(), min_pos, max_pos);

    for (int i = 0; i < iterations; ++i) {
        int before = expected;

        int delta = 0;
        for (int tries = 0; tries < 64 && delta == 0; ++tries) {
            int cand = delta_dist(rng);
            if (cand == 0) continue;
            int target = before + cand;
            if (target >= min_pos && target <= max_pos) delta = cand;
        }
        if (delta == 0) {
            int to_min = before - min_pos;
            int to_max = max_pos - before;
            if (to_max >= to_min) delta = std::min(max_step, to_max);
            else delta = -std::min(max_step, to_min);
            if (delta == 0) delta = (to_max > 0 ? 1 : (to_min > 0 ? -1 : 0));
        }

        int speed = spd_dist(rng);
        int timeout_sec = std::max(1, (settle_timeout_ms + 999) / 1000);

        expected = std::clamp(before + delta, min_pos, max_pos);
        int rc = ctrl.move_relative_blocking(delta, speed, timeout_sec, tolerance);

        // Read back final position and verify within tolerance
        int actual = ctrl.get_current_position();
        bool ok = (rc == 0) && (std::abs(actual - expected) <= tolerance);
        if (ok) ++pass;
        else {
            ++fail;
            std::cout << "[fail] i=" << i
                      << " before=" << before
                      << " delta=" << delta
                      << " speed=" << speed
                      << " expected=" << expected
                      << " got=" << actual
                      << " rc=" << rc << std::endl;
        }

        if ((i + 1) % 100 == 0) {
            std::cout << "[progress] " << (i + 1) << "/" << iterations
                      << " pass=" << pass << " fail=" << fail << std::endl;
        }
    }

    std::cout << "[summary] iterations=" << iterations
              << " pass=" << pass << " fail=" << fail << std::endl;
}

// Randomized stress test for move_relative within a position range [min_pos, max_pos]
static void stress_test_move_relative(act_controller& ctrl,
                                      int iterations,
                                      int min_pos,
                                      int max_pos,
                                      int max_step,
                                      int settle_timeout_ms = 1500,
                                      int tolerance = 1) {
    using namespace std::chrono;
    if (min_pos > max_pos) std::swap(min_pos, max_pos);
    if (min_pos < 0) min_pos = 0;

    std::mt19937 rng(static_cast<unsigned int>(steady_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<int> delta_dist(-max_step, max_step);
    std::uniform_int_distribution<int> spd_dist(1, 30);

    int pass = 0, fail = 0;

    // Initialize expected once and clamp to the allowed range
    int expected = std::clamp(ctrl.get_current_position(), min_pos, max_pos);

    for (int i = 0; i < iterations; ++i) {
        // Base next command off the expected (not the last measured "got")
        int before = expected;

        // Pick a delta that keeps target within [min_pos, max_pos]
        int delta = 0;
        for (int tries = 0; tries < 64 && delta == 0; ++tries) {
            int cand = delta_dist(rng);
            if (cand == 0) continue;
            int target = before + cand;
            if (target >= min_pos && target <= max_pos) delta = cand;
        }
        // If we couldn't find a random delta, nudge toward the nearest bound
        if (delta == 0) {
            if (before < min_pos) delta = std::min(max_step, min_pos - before);
            else if (before > max_pos) delta = -std::min(max_step, before - max_pos);
            else {
                int to_min = before - min_pos;
                int to_max = max_pos - before;
                delta = (to_max >= to_min) ? std::min(max_step, to_max) : -std::min(max_step, to_min);
                if (delta == 0) delta = (to_max > 0) ? 1 : (to_min > 0 ? -1 : 0);
            }
        }

        int speed = spd_dist(rng);
        ctrl.move_relative(delta, speed);

        // Update expected as the commanded/clamped target
        expected = std::clamp(before + delta, min_pos, max_pos);

        // Verify by polling actual until settled or timeout
        int actual = before;
        auto t0 = steady_clock::now();
        while (steady_clock::now() - t0 < milliseconds(settle_timeout_ms)) {
            std::this_thread::sleep_for(milliseconds(120));
            actual = ctrl.get_current_position();
            if (std::abs(actual - expected) <= tolerance) break;
        }

        if (std::abs(actual - expected) <= tolerance) {
            ++pass;
        } else {
            ++fail;
            std::cout << "[fail] i=" << i
                      << " before=" << before
                      << " delta=" << delta
                      << " speed=" << speed
                      << " expected=" << expected
                      << " got=" << actual << std::endl;
        }

        if ((i + 1) % 100 == 0) {
            std::cout << "[progress] " << (i + 1) << "/" << iterations
                      << " pass=" << pass << " fail=" << fail << std::endl;
        }
    }
    std::cout << "[summary] iterations=" << iterations
              << " pass=" << pass << " fail=" << fail << std::endl;
}

// Randomized stress test for absolute movement within a position range [min_pos, max_pos]
// Uses move_absolute_blocking for each target and verifies via get_current_position.
// Reduced tolerance default (0).
static void stress_test_move_absolute_blocking(act_controller& ctrl,
                                      int iterations,
                                      int min_pos,
                                      int max_pos,
                                      int settle_timeout_ms = 1500,
                                      int tolerance = 0) {
    using namespace std::chrono;
    if (min_pos > max_pos) std::swap(min_pos, max_pos);
    if (min_pos < 0) min_pos = 0;

    std::mt19937 rng(static_cast<unsigned int>(steady_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<int> target_dist(min_pos, max_pos);
    std::uniform_int_distribution<int> spd_dist(1, 30); // added

    int pass = 0, fail = 0;
    for (int i = 0; i < iterations; ++i) {
        int target = target_dist(rng);
        int expected = std::clamp(target, min_pos, max_pos);

        // Convert ms to seconds (ceil), minimum 1s
        int timeout_sec = std::max(1, (settle_timeout_ms + 999) / 1000);
        int speed = spd_dist(rng); // added
        int rc = ctrl.move_absolute_blocking(expected, speed, timeout_sec, tolerance);

        // Verify final position
        int actual = ctrl.get_current_position();
        bool ok = (rc == 0) && (std::abs(actual - expected) <= tolerance);

        if (ok) {
            ++pass;
        } else {
            ++fail;
            std::cout << "[abs-fail] i=" << i
                      << " target=" << target
                      << " expected=" << expected
                      << " got=" << actual
                      << " rc=" << rc << std::endl;
        }

        if ((i + 1) % 100 == 0) {
            std::cout << "[abs-progress] " << (i + 1) << "/" << iterations
                      << " pass=" << pass << " fail=" << fail << std::endl;
        }
    }
    std::cout << "[abs-summary] iterations=" << iterations
              << " pass=" << pass << " fail=" << fail << std::endl;
}

// Repeated connect / disconnect stress test
static void stress_test_connect_disconnect(act_controller& ctrl,
                                           int iterations,
                                           int delay_ms = 50,
                                           const std::string& explicit_port = std::string()) {
    if (!explicit_port.empty()) {
        ctrl.disconnect();
        int rc = ctrl.connect(explicit_port);
        if (rc == 0 && ctrl.is_connected()) {
            std::string actual = ctrl.get_port_name();
            if (actual != explicit_port)
                std::cout << "[connect-ok] requested=" << explicit_port << " actual=" << actual << std::endl;
            else
                std::cout << "[connect-ok] " << actual << std::endl;
            ctrl.disconnect();
        } else {
            std::cout << "[connect-fail] " << explicit_port << " rc=" << rc << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }
    int pass = 0, fail = 0;
    for (int i = 0; i < iterations; ++i) {
        // Ensure starting disconnected
        if (ctrl.is_connected()) {
            ctrl.disconnect();
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
        int rc = ctrl.connect();
        bool ok_connect = (rc == 0 && ctrl.is_connected());
        if (!ok_connect) {
            ++fail;
            std::cout << "[conn-fail] i=" << i << " rc=" << rc
                      << " state=" << ctrl.is_connected() << std::endl;
            continue;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        ctrl.disconnect();
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        bool ok_disconnect = !ctrl.is_connected();
        if (ok_disconnect) ++pass;
        else {
            ++fail;
            std::cout << "[disc-fail] i=" << i << " state=" << ctrl.is_connected() << std::endl;
        }
        if ((i + 1) % 100 == 0) {
            std::cout << "[conn-progress] " << (i + 1) << "/" << iterations
                      << " pass=" << pass << " fail=" << fail << std::endl;
        }
    }
    if (iterations > 0) {
        std::cout << "[conn-summary] iterations=" << iterations
                  << " pass=" << pass << " fail=" << fail << std::endl;
    }
}

#ifndef ACT_CONTROLLER_NO_MAIN
int main() {
    act_controller ctrl;
    ctrl.init();
// #ifdef _WIN32
//     stress_test_connect_disconnect(ctrl, 100, 50, "COM7");
// #else
//     stress_test_connect_disconnect(ctrl, 100, 50, "/dev/ttyUSB0");
// #endif
        int rc = ctrl.connect();
    if (rc != 0 || !ctrl.is_connected()) {
        std::cerr << "Failed to connect" << std::endl;
        return 1;
    }
        // ctrl.reset();



    // Run connect/disconnect stress test
    // stress_test_connect_disconnect(ctrl, /*iterations=*/1, /*delay_ms=*/40);

    int p0 = ctrl.get_current_position();
    std::cout << "Current: " << p0 << std::endl;
    
    ctrl.move_absolute_blocking(50, 100, 120, 0);
   // Allow motion to settle before reading (was 100 ms)
   std::this_thread::sleep_for(std::chrono::milliseconds(700));
   int p1 = ctrl.get_current_position();
   std::cout << "After +20: " << p1 << std::endl;

    // 78 là chạm mặt bàn
    // Run automated randomized tests within position range [0, 50], step up to 5 units
    // stress_test_move_relative_blocking(ctrl,
    //                           /*iterations=*/100,
    //                           /*min_pos=*/0,
    //                           /*max_pos=*/70,
    //                           /*max_step=*/70,
    //                           /*settle_timeout_ms=*/120000,
    //                           /*tolerance=*/0);

    // Relative movement stress test

    // Absolute movement stress test with reduced tolerance
    // stress_test_move_absolute_blocking(ctrl,
    //                           /*iterations=*/100,
    //                           /*min_pos=*/0,
    //                           /*max_pos=*/70,
    //                           /*settle_timeout_ms=*/120000,
    //                           /*tolerance=*/0);

    ctrl.disconnect();
    return 0;
}
#endif // ACT_CONTROLLER_NO_MAIN

