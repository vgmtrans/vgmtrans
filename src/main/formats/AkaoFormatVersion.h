#pragma once

enum class AkaoPs1Version : uint8_t {
  UNKNOWN = 0,

  // Final Fantasy 7
  VERSION_1_0,

  // Headered AKAO samples, 0xFC opcode is used for specifying articulation zones
  // SaGa Frontier
  VERSION_1_1,

  // 0xFC xx meta event introduced:
  // Front Mission 2
  // Chocobo's Mysterious Dungeon
  VERSION_1_2,

  // Header size increased to 0x20 bytes:
  // Parasite Eve
  VERSION_2,

  // Header size increased to 0x40 bytes:
  VERSION_3
};
