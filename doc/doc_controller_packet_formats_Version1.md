# Controller Packet Formats (Reverse-Engineered from `act_controller.cpp`)

This document captures the packet formats used by the screw controller as
seen in [`act_controller.cpp`](../act_controller.cpp). The goal is to
prevent protocol details from being lost and to make future maintenance easier.

## Conventions

- All multi-byte numbers are shown in hex; for registers, bytes are:
  - `(Hi, Lo)` = big-endian 16-bit value (high byte first, low byte second).
- CRC is **Modbus RTU CRC16** (poly 0xA001), appended as:
  - `[CRC-Lo][CRC-Hi]`
- General Modbus RTU frame layout is:

  ```text
  [Addr][Func][Data ...][CRC-Lo][CRC-Hi]
  ```

  Where:
  - `Addr` = slave address (here always `01`).
  - `Func` = function code (`03`, `05`, `0F`, `10`, etc.).
  - `Data` = function-specific payload.

---

## 1. Read Holding Registers (Function 0x03)

Used heavily for:
- Probing / verifying connection.
- Querying various blocks of device state.
- Getting current position.

### 1.1 General Request Format

```text
01 03 AddrHi AddrLo QtyHi QtyLo CRC-Lo CRC-Hi
^^ ^^ ^^^^^  ^^^^^  ^^^^^ ^^^^^ ^^^^^^^^^^^^^
|  |   |      |      |     |        |
|  |   |      |      |     |        └─ CRC16 (over bytes 0..5)
|  |   |      |      |     └─ Quantity of registers
|  |   |      |      └─ Quantity of registers (Hi)
|  |   |      └─ Start address (Lo)
|  |   └─ Start address (Hi)
|  └─ Function 0x03
└─ Slave address 0x01
```

Example (probe frame used in `connect()` and `get_current_position()`):

```text
01 03 90 00 00 10 69 06
|  |  |  |  |  |  |  └─ CRC-Hi
|  |  |  |  |  |  └─ CRC-Lo
|  |  |  |  |  └─ QtyLo = 0x10 (16 regs)
|  |  |  |  └─ QtyHi = 0x00
|  |  |  └─ AddrLo = 0x00
|  |  └─ AddrHi = 0x90
|  └─ Func = 0x03
└─ Addr = 0x01
```

### 1.2 Response Format (as parsed)

In `get_current_position()`:

```text
[0]   [1]   [2]      [3..(2+ByteCount)] [Last-1] [Last]
 Addr Func ByteCnt   Data bytes ...      CRC-Lo   CRC-Hi
 01   03   N         D0 D1 D2 ...        xx       yy
```

Position is interpreted from `Data[2]` and `Data[3]` (i.e. `rx[5]` and `rx[6]`):

```c++
// rx[5] = high byte, rx[6] = low byte
uint16_t raw = (rx[5] << 8) | rx[6];
int16_t signed_raw = static_cast<int16_t>(raw);
// external units ~= signed_raw / 100, with sign-aware rounding
```

So:

- Device position register = signed 16-bit value, scaled by 100.

---

## 2. Relative Move Parameter Block (Function 0x10, Address 0x9102)

Used in:
- `move_relative()` (positive and negative)
- `move_relative_blocking()` (positive and negative)

This writes a 32-byte parameter block to starting register `0x9102`.

### 2.1 Base Layout

Without CRC:

```text
01 10 91 02 00 10 20
00 02 [SpeedHi][SpeedLo] [SignHi][SignLo]
[DeltaHi][DeltaLo] 03 e8 03 e8
00 00 00 00 00 01 00 64
00 00 00 00 00 00 00 00
00 00 00 00 00 00 32
CRC-Lo CRC-Hi
```

Field notes:

- `01`          : slave address.
- `10`          : function 0x10 (Write Multiple Registers).
- `91 02`       : starting register address = 0x9102.
- `00 10`       : quantity of registers = 0x0010 (16 regs).
- `20`          : byte count = 0x20 (32 data bytes).
- `00 02`       : unknown; constant header word.
- `[SpeedHi][SpeedLo]`:
  - 16‑bit speed value (big-endian), from `move_speed` or `speed`.
- `[SignHi][SignLo]`:
  - For positive movement: `00 00`
  - For negative movement: `FF FF`
- `[DeltaHi][DeltaLo]`:
  - magnitude × 100, cast to `uint16_t`:

    ```c++
    uint16_t val = static_cast<uint16_t>(magnitude * 100);
    ```

- `03 e8 03 e8`:
  - two registers of value `0x03E8 = 1000` (likely timing/limits).
- `00 00 00 00 00 01 00 64`:
  - some fixed configuration values; 0x0064 = 100 decimal.
- Remaining 0x00 bytes and trailing `0x32` are constant.

### 2.2 Positive vs Negative Examples

**Positive relative move (magnitude > 0)**

```text
... 00 02 [SpeedHi][SpeedLo] 00 00 [DeltaHi][DeltaLo] 03 e8 03 e8 ...
                                     ^ sign = 0x0000 (positive)
```

**Negative relative move (magnitude < 0)**

```text
... 00 02 [SpeedHi][SpeedLo] ff ff [DeltaHi][DeltaLo] 03 e8 03 e8 ...
                                     ^ sign = 0xFFFF (negative)
```

Then CRC16 is appended:

```text
[... data ...] CRC-Lo CRC-Hi
```

---

## 3. Relative Move Trigger (Function 0x10, Address 0x9100)

After writing the parameter block, a small “trigger” frame is sent in:

- `move_relative()`
- `move_relative_blocking()`

```text
01 10 91 00 00 01 02 01 00 27 09
^^ ^^ ^^^^^ ^^^^^ ^^ ^^ ^^ ^^^^^^
|  |   |      |   |  |  |   └─ CRC-Hi
|  |   |      |   |  |  └─ Data[1] = 0x00
|  |   |      |   |  └─ Data[0] = 0x01
|  |   |      |   └─ Byte count = 0x02 (one register)
|  |   |      └─ Quantity of registers = 0x0001
|  |   └─ Start address = 0x9100
|  └─ Func = 0x10
└─ Addr = 0x01
```

In semantic terms:

- Write 1 register at `0x9100` with value `0x0100`.
- Interpretation: likely “start motion” or “execute parameter block”.

---

## 4. Tail Frame for Negative Relative Move (Function 0x05, Coil 0x001A)

Only used for negative relative motion:

- `move_relative()` (negative branch)
- `move_relative_blocking()` (negative branch)

```text
01 05 00 1a 00 00 ec 0d
^^ ^^ ^^^^^ ^^^^^ ^^^^^
|  |   |      |    └─ CRC (Modbus RTU)
|  |   |      └─ Coil value = 0x0000 (OFF)
|  |   └─ Coil address = 0x001A
|  └─ Func = 0x05 (Write Single Coil)
└─ Addr = 0x01
```

Used as a final “tail” command after trigger. Semantics unknown but
likely disables or clears some negative‑motion latch/flag.

---

## 5. Absolute Move Speed Frame (Function 0x10, Address 0x0411)

Used in:

- `move_absolute()`

Before sending the target position, a speed register is written:

```text
01 10 04 11 00 01 02 [SpeedHi][SpeedLo] CRC-Lo CRC-Hi
^^ ^^ ^^^^^ ^^^^^ ^^ ^^ ^^^^^^^^ ^^^^^^^^^^^^^^^^^^^^
|  |   |      |   |  |   |         |
|  |   |      |   |  |   |         └─ CRC16
|  |   |      |   |  |   └─ data: 1 register = speed
|  |   |      |   |  └─ Byte count = 0x02
|  |   |      |   └─ Quantity of registers = 0x0001
|  |   |      └─ Start address = 0x0411
|  |   └─ Func = 0x10
|  └─ Addr = 0x01
└─
```

Where:

- `[SpeedHi][SpeedLo]` = `speed` (clamped, at least 1) as big‑endian 16‑bit.

---

## 6. Absolute Move Position Frame (Function 0x10, Address 0x0412)

Also in `move_absolute()`:

```text
01 10 04 12 00 02 04 00 00 [PosHi][PosLo] CRC-Lo CRC-Hi
^^ ^^ ^^^^^ ^^^^^ ^^ ^^ ^^ ^^^^^^^^ ^^^^^^^^^^^^^^^^^^^
|  |   |      |   |  |  |    |         |
|  |   |      |   |  |  |    |         └─ CRC16
|  |   |      |   |  |  |    └─ scaled absolute position (big-endian)
|  |   |      |   |  |  └─ Byte count = 0x04 (2 regs)
|  |   |      |   |  └─ Quantity of registers = 0x0002
|  |   |      |   └─ Start address = 0x0412
|  |   └─ Func = 0x10
|  └─ Addr = 0x01
└─
```

Internal handling:

```c++
int scaled = position * 100;
scaled = clamp(scaled, 0, 0xFFFF);
uint16_t abs_mov_u16 = static_cast<uint16_t>(scaled);
PosHi = abs_mov_u16 >> 8;
PosLo = abs_mov_u16 & 0xFF;
```

So device position is **unsigned** and scaled by 100, clamped to `[0, 65535]`.

The two extra zero bytes (`00 00`) before `[PosHi][PosLo]` are a leading register of unknown purpose.

---

## 7. Absolute Move Post-Sequence (Coils and Multi-Coil Write)

After speed and position are written, `move_absolute()` sends a fixed series
of frames:

```text
01 05 00 1a 00 00 ec 0d   // Coil 0x001A = 0 (OFF)
01 0f 00 10 00 08 01 01 fe 96
01 05 00 1a ff 00 ad fd   // Coil 0x001A = 0xFF00 (ON)
01 05 00 1a 00 00 ec 0d   // Coil 0x001A = 0 (OFF)
```

### 7.1 Single Coil Writes (0x05, Coil 0x001A)

Format:

```text
01 05 00 1a VV VV CRC-Lo CRC-Hi
```

- `VV VV = 00 00` — coil OFF.
- `VV VV = ff 00` — coil ON.

This coil seems to be used as some kind of pulse/enable around motion.

### 7.2 Multi-Coil Write (0x0F, Starting Coil 0x0010)

```text
01 0f 00 10 00 08 01 01 fe 96
^^ ^^ ^^^^^ ^^^^^ ^^ ^^ ^^ ^^^^
|  |   |      |   |  |  |  └─ CRC16
|  |   |      |   |  |  └─ coil data byte
|  |   |      |   |  └─ byte count
|  |   |      |   └─ quantity of coils = 0x0008
|  |   |      └─ start coil address = 0x0010
|  |   └─ Func = 0x0F (Write Multiple Coils)
|  └─ Addr = 0x01
└─
```

Exact semantics of these coils remain unknown; they appear to be
part of the required motion‑start sequence.

---

## 8. Reset Sequence Frames

The `reset()` function sends:

```text
01 03 03 80 00 40 45 96
01 05 00 45 ff 00 9d ef
01 05 00 1c ff 00 4d fc
01 05 00 1c 00 00 0c 0c
```

Breakdown:

1. **Read registers** (`0x03`):

   ```text
   01 03 03 80 00 40 CRC
   ```

   - Start address: `0x0380`
   - Quantity: `0x0040` (64 regs)

2. **Write coil 0x0045 = ON**:

   ```text
   01 05 00 45 ff 00 CRC
   ```

3. **Write coil 0x001C = ON**:

   ```text
   01 05 00 1c ff 00 CRC
   ```

4. **Write coil 0x001C = OFF**:

   ```text
   01 05 00 1c 00 00 CRC
   ```

Hypothesis: this sequence toggles specific coils that reset the motion
controller into a known state.

---

## 9. Connect / Initialization Sequences

In `connect()`, after a positive probe, a pre-defined series of
initialization commands is sent; all are standard Modbus frames and
should be preserved exactly:

Examples:

```text
01 03 00 0e 00 08 25 cf
01 03 00 52 00 02 65 da
01 03 00 00 00 70 44 2e
01 03 00 70 00 70 45 f5
01 03 00 e0 00 70 45 d8
01 03 01 50 00 30 44 33
01 03 03 80 00 40 45 96
01 03 04 00 00 70 45 1e
01 03 04 70 00 70 44 c5
01 03 04 e0 00 70 44 e8
01 03 05 50 00 70 44 f3
01 03 05 c0 00 70 44 de
01 03 06 30 00 70 44 a9
01 03 06 a0 00 70 44 84
01 03 07 10 00 70 44 9f
01 03 07 80 00 70 44 b2
01 03 07 f0 00 10 45 41
01 03 90 11 00 02 b9 0e
01 05 00 30 ff 00 8c 35
01 05 00 19 ff 00 5d fd
```

All of them use:
- `01 03 AddrHi AddrLo QtyHi QtyLo CRC` for reads.
- `01 05 CoilHi CoilLo ValHi ValLo CRC` for single coil writes.

**Important:** even though their semantics are not yet fully decoded,
they appear necessary for correct startup and should not be removed
or arbitrarily changed.

---

## 10. Quick Reference Table

| Purpose                               | Func | Addr / Coil        | Shape (simplified)                                                |
|---------------------------------------|------|--------------------|--------------------------------------------------------------------|
| Connection probe / position block     | 0x03 | 0x9000             | `01 03 90 00 00 10 CRC`                                           |
| Generic register read                 | 0x03 | various            | `01 03 AddrHi AddrLo QtyHi QtyLo CRC`                             |
| Relative move param block             | 0x10 | 0x9102             | `01 10 91 02 00 10 20 ... 32 data bytes ... CRC`                  |
| Relative move trigger                 | 0x10 | 0x9100             | `01 10 91 00 00 01 02 01 00 CRC`                                  |
| Negative move tail                    | 0x05 | coil 0x001A        | `01 05 00 1a 00 00 CRC`                                           |
| Abs move speed                        | 0x10 | 0x0411             | `01 10 04 11 00 01 02 SpeedHi SpeedLo CRC`                        |
| Abs move position                     | 0x10 | 0x0412             | `01 10 04 12 00 02 04 00 00 PosHi PosLo CRC`                      |
| Abs move coil pulse                   | 0x05 | coil 0x001A        | `01 05 00 1a VV VV CRC` (`VV VV = 00 00` or `ff 00`)              |
| Abs move multi-coil config            | 0x0F | coil 0x0010        | `01 0f 00 10 00 08 01 01 CRC`                                     |
| Reset coils                           | 0x05 | coils 0x0045,0x001C| `01 05 00 45 ff 00 CRC`, `01 05 00 1c ff 00 / 00 00 CRC`          |
| Reset / init block read               | 0x03 | 0x0380, others     | `01 03 03 80 00 40 CRC` and similar                               |

---

## 11. Implementation Notes

- All CRCs are calculated with the helper:

  ```c++
  uint16_t crc16_modbus(const uint8_t* data, size_t len)
  ```

- CRC bytes are always appended `[lo, hi]`.
- Positions and distances are in “device units” = **external units × 100**.
- Relative moves rely on signed values, using C++ cast to `uint16_t` for
  two’s complement representation.
- Absolute moves clamp positions to `[0, 0xFFFF]` and treat them as
  **unsigned**.

When adding new commands, document them in this file with the same
diagram style so future reverse engineering work is not lost.