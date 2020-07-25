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
  // Another Mind
  // Chocobo Dungeon 2
  // Final Fantasy 8
  // Chocobo Racing
  // SaGa Frontier 2
  // Racing Lagoon
  VERSION_3,

  // Slight breaking changes on opcode mapping:
  // Legend of Mana
  // Front Mission 3
  // Chrono Cross
  // Vagrant Story
  // Final Fantasy 9
  // Final Fantasy Origins - Final Fantasy 2
  VERSION_3_1
};
