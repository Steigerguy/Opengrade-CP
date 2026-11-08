// Seed blockage monitor - CAN protocol shared by tower nodes and the cab.
// Keep this file identical in tower_node/ and cab_monitor/.
//
// Bus: 250 kbit/s, standard 11-bit IDs.
// Each tower sends 3 frames once a second:
//
//   ID = 0x100 + (tower << 4) + part      tower = 1..15
//
//   part 0: runs 1-4   uint16 little-endian hits/second each
//   part 1: runs 5-8   uint16 little-endian hits/second each
//   part 2: byte 0-1  run 9 hits/second (0 on 8-run towers)
//           byte 2    number of runs on this tower (8 or 9)
//           byte 3    sequence number (increments every second)
//           byte 4    flags (bit 0 = tune mode, counts not valid)
//           byte 5    firmware version
//           byte 6-7  reserved (0)
//
// Example: tower 3 sends IDs 0x130, 0x131, 0x132.

#pragma once

#define SBM_CAN_BITRATE    250000UL
#define SBM_ID_BASE        0x100
#define SBM_MAX_TOWERS     15
#define SBM_MAX_RUNS       9
#define SBM_FW_VERSION     1
#define SBM_FLAG_TUNE      0x01

static inline uint32_t sbm_can_id(uint8_t tower, uint8_t part) {
  return SBM_ID_BASE + ((uint32_t)tower << 4) + part;
}

// Returns true and fills tower/part if id is one of ours.
static inline bool sbm_parse_id(uint32_t id, uint8_t *tower, uint8_t *part) {
  if (id < SBM_ID_BASE + 0x10 || id > SBM_ID_BASE + (SBM_MAX_TOWERS << 4) + 2) return false;
  uint32_t off = id - SBM_ID_BASE;
  *tower = off >> 4;
  *part = off & 0x0F;
  return *part <= 2;
}
