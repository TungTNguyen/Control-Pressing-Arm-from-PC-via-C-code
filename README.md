# Screw Control Pressing Arm from PC via C++ Code

## Final Goal

Develop a C++ class `act_controller` with the following public interface:

### Core functions

- `init()`

- `connect()`  
  Only needed if connection/setup is required.  
  Automatically connects to an available controller.  
  Returns `0` if successful.

- `disconnect()`  
  Only needed if connection/setup is required.

- `move_to_origin()`

- `move_relative(int magnitude, int speed)`  
  - Moves in the positive direction for positive `magnitude`.  
  - Moves in the negative direction for negative `magnitude`.

- `move_absolute(int position)`  
  - Automatically moves to the specified absolute `position`.

- `bool is_connected()`  
  - Returns `true` if the controller is connected.

- `int get_current_position()`  
  - Returns the current position of the actuator.

### Additional blocking functions

- `move_relative_blocking(int magnitude, int speed, int timeout)`  
  - Moves in the positive direction for positive `magnitude`.  
  - Moves in the negative direction for negative `magnitude`.  
  - The function waits until the actuator reaches the target position before returning.  
  - If the actuator does not reach the target within `timeout` seconds, it returns an error.

- `move_absolute_blocking(int position, int timeout)`  
  - Automatically moves to the specified absolute `position`.  
  - The function waits until the actuator reaches the target position before returning.  
  - If the actuator does not reach the target within `timeout` seconds, it returns an error.

More functions will be added as needed.
# act_controller Design Overview

## Purpose
Encapsulates Modbus-RTU style serial communication with a motion controller on Windows (COM ports) and Linux (/dev/ttyUSB*, /dev/ttyS*). Provides:
- Auto-connect with probe + fallback scan.
- Relative and absolute movement (blocking / non-blocking variants).
- Position polling with CRC validation.
- Stress tests (randomized movement & connect/disconnect).

## Serial Communication (Connection Flow)
1. Preferred port chosen:
   - Windows: explicit user port or default COM7.
   - Linux: explicit user port or default /dev/ttyUSB0.
2. Probe frame: 01 03 90 00 00 10 69 06 sent; response must pass CRC.
3. Initialization sequence: multiple 01 03 and 01 05 frames (read/write setup registers).
4. If preferred fails, scan candidate list (platform-specific) until first valid responsive port.

## Checksum (CRC & Frames)
- CRC16 Modbus (poly 0xA001), appended low-byte then high-byte.
- All motion/parameter frames built in big-endian for 16-bit quantities (position scaled by 100).

## Movement
- Non-blocking:
  - move_relative(int magnitude, int speed)
  - move_absolute(int position)
- Blocking:
  - move_relative_blocking(int magnitude, int speed, int timeout_sec, int tolerance = 1)
  - move_absolute_blocking(int position, int timeout_sec, int tolerance = 1)
- Positive relative uses 0x00 0x00; negative uses 0xFF 0xFF marker bytes before magnitude.
- Trigger frame (start execution) and optional tail frame (reset coil) follow main write frame.

## Position
Request: 01 03 90 00 00 10 69 06
Response parsing:
- Header: addr, func, byteCount
- Data: byteCount bytes + 2 CRC bytes
- Position extracted from bytes[5], bytes[6] (scaled /100 with rounding (+50)/100).

## Timing / I/O Strategy
- Small sleeps (50–120 ms) after writes.
- Async read with cancel timer (300 ms) to avoid indefinite blocking.
- Drain function clears residual bytes before a fresh position read.

## Cross-Platform Port Enumeration
Windows: COM1..COM32
Linux: /dev/ttyUSB[0..9], /dev/ttyS[0..31], /dev/tty[0..63]

## Error Handling
- Exceptions caught broadly, connection resets port state.
- CRC mismatches or malformed frames → ignored, return safe defaults (e.g. position 0).

## Stress Test Facilities
- stress_test_move_relative_blocking
- stress_test_move_relative
- stress_test_move_absolute_blocking
- stress_test_connect_disconnect
Each prints progress and summary (pass/fail counts).

## Extensibility Notes
- Recommend extracting a header (act_controller.hpp) if wider reuse or mocking is required.
- Consider refactoring serial_port into an injectable interface for proper isolated unit tests.
- Logging could be abstracted to allow silent production mode.

## Thread Safety
Instance is not thread-safe; external synchronization required if shared.

## Known Constraints
- Blocking move assumes controller updates position register promptly.
- Negative relative movement relies on device interpreting 0xFF 0xFF as direction.
- No explicit acceleration/deceleration profile handling (speed is direct).

## Minimal Public Interface Snapshot (for reference)
```cpp
class act_controller {
public:
    int connect(const std::string& user_com_port = std::string());
    void disconnect();
    void move_relative(int magnitude, int move_speed = 10);
    void move_absolute(int position);
    int move_relative_blocking(int magnitude, int move_speed, int timeout_sec, int tolerance = 1);
    int move_absolute_blocking(int position, int timeout_sec, int tolerance = 1);
    int get_current_position();
    bool is_connected() const;
    const std::string& get_port_name() const;
};
```





