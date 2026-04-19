---
name: binary-protocol-database
description: "Use when designing, implementing, or porting a lightweight UART binary protocol with parameter database (DF_) and live data (LD_) mapping. Covers frame format, parser state machine, checksum, typed register addressing, read/write handlers, host/firmware contract, and migration checklist. Keywords: binary protocol, UART frame, database, DF, LD, checksum, register map, embedded communication, command handler, packet parser."
---

# Binary Protocol + Database Pattern

Use this skill when you need a reusable embedded communication architecture based on:

- A compact UART binary frame
- Typed register map (parameter + live data)
- Host/firmware read-write contract
- Clear portability rules for new projects

This pattern is based on practical code structure in this workspace and is intended to be copied to other MCU projects.

## Goals

- Define a stable, testable binary packet format.
- Keep parsing robust under noisy UART streams.
- Unify parameter and live data access via one register model.
- Separate protocol transport from business logic.
- Make host app integration predictable.

## Core Architecture

### 1) Layers

- Transport Layer:
  - UART RX/TX ring buffer
  - byte stream read/write
- Protocol Layer:
  - frame detection
  - length check
  - checksum
  - function-code dispatch
- Database Layer:
  - typed register addressing
  - DF_ (persistent parameter) read/write
  - LD_ (runtime live data) read-only/read-mostly
- Application Layer:
  - state machine updates LD_
  - command handlers consume DF_/LD_

### 2) Data Domains

- DF_:
  - Non-volatile configuration parameters (EEPROM/Flash backed)
  - Example: timeout, PWM limits, thresholds
- LD_:
  - Runtime values (RAM)
  - Example: state, position, error code, counters

## Frame Contract

Use a compact frame structure (example pattern):

- Byte0: Start prefix (for example 0x02)
- Byte1: Function code (FC)
- Byte2: Transaction/Request ID
- Byte3: Payload length (N)
- Byte4..Byte(4+N-1): Payload
- Byte(4+N): Checksum
- Byte(5+N): End suffix (for example 0x03)

Total size = N + 6

### Checksum Rule (recommended)

- checksum = bitwise NOT of sum over FC..payload bytes
- Verify checksum before handler dispatch.
- On checksum mismatch: drop frame, do not mutate database.

## Function Code Contract

Recommended function-code partition:

- 0x10: Dataflash/Parameter channel (DF_)
- 0x20: Livedata channel (LD_)
- 0x30: Command channel (optional)
- 0x40: Raw/bulk data channel (optional)

Keep FC handling centralized in one dispatcher.

## Typed Register Addressing

Use a single 16-bit register address with encoded type + index.

### Recommended encoding model

- High bits: type bucket (u8/u16/u32/i8/i16/i32/f32)
- Low bits: index
- Optional flag bit: live-data mask (LD vs DF)

Example conceptual helpers:

- PARAM_INDEX(addr)
- PARAM_TYPE(addr)
- LIVE_DATA_MASK

### Why this model works

- One parser path for all numeric types
- Host and firmware share one register namespace
- Easy bounds check per type bucket

## Database API Contract

Define a small stable API between protocol and database:

- db_read_dataflash(addr, out_bytes) -> byte_count
- db_write_dataflash(addr, in_bytes) -> byte_count
- db_read_livedata(addr, out_bytes) -> byte_count
- db_set_live(ld_id, value)
- db_get_live(ld_id)
- db_get_param(df_id)

Rules:

- Return 0 on invalid address/type/index.
- Enforce per-type max count and range.
- Serialize multi-byte values in one fixed endianness (document it).

## Parser State Machine Pattern

Use a streaming parser with these states:

1. Waiting for start prefix
2. Collecting bytes
3. Length reached -> validate checksum/suffix
4. Dispatch or drop
5. Reset state safely

Robustness requirements:

- Hard cap frame buffer length
- Reset on overflow
- Re-sync by searching next start prefix
- Never block protocol task on malformed packet

## Read/Write Flow (Reference)

### Host Read Sequence

1. Host sends FC + addr + read-count
2. Firmware validates
3. Firmware reads DB (DF or LD)
4. Firmware replies with same request ID + payload

### Host Write Sequence (DF)

1. Host sends FC + addr + value bytes
2. Firmware validates type/index/range
3. Firmware writes non-volatile storage
4. Firmware optionally reads back and replies

## Porting Checklist (For New Project)

When migrating this pattern to another project:

1. Define new DF_/LD_ enums and ownership
2. Freeze register encoding and endianness
3. Implement db_* API against target storage (Flash/EEPROM/RAM)
4. Implement protocol parser and FC dispatcher
5. Add range validation table for all DF_
6. Add host-side ID map and formatter
7. Add protocol regression tests (golden packets)
8. Add negative tests (bad checksum, bad length, out-of-range)

## Validation Matrix

Minimum tests before release:

- Frame parser:
  - valid frame accepted
  - wrong checksum rejected
  - wrong suffix rejected
  - oversized payload rejected
- DF channel:
  - read returns expected bytes
  - write updates storage and survives reboot
  - out-of-range write rejected
- LD channel:
  - values update with runtime state
  - read latency within budget
- Compatibility:
  - host and firmware agree on IDs, type sizes, endianness

## Common Failure Modes

- Host and firmware enum mismatch (IDs drift)
- Incorrect length field calculation
- Missing bounds checks on type bucket
- Hidden endianness mismatch for 16/32-bit values
- Using blocking delays in parser path
- No transaction ID echo in response

## Suggested File Layout

- protocol/
  - bin_protocol_lite.h
  - bin_protocol_lite.c
- database/
  - database.h
  - database.c
- app/
  - command_handlers.c
  - state_machine.c

## Reuse Notes

For new repositories, copy this folder:

- .github/skills/binary-protocol-database/

Then adjust only:

- description keywords (to fit new domain terms)
- FC assignment if protocol already exists
- DF_/LD_ tables and ranges

Keep parser and database contracts stable to minimize host rework.
