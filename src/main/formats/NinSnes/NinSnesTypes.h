#pragma once

enum NinSnesVersion {
  NINSNES_NONE = 0,           // Not Supported
  NINSNES_UNKNOWN,            // Unknown version (only header support)
  NINSNES_EARLIER,            // Eariler version (Super Mario World, Pilotwings)
  NINSNES_STANDARD,           // Common version with voice commands $e0-fa (e.g. Yoshi Island)

                              // derived formats
  NINSNES_RD1,                // Nintendo RD1 (e.g. Super Metroid, Earthbound)
  NINSNES_RD2,                // Nintendo RD2 (e.g. Marvelous)
  NINSNES_HAL,                // HAL Laboratory games (e.g. Kirby series)
  NINSNES_KONAMI,             // Old Konami games (e.g. Gradius III, Castlevania IV, Legend of the Mystical Ninja)
  NINSNES_LEMMINGS,           // Lemmings
  NINSNES_INTELLI_FE3,        // Fire Emblem 3
  NINSNES_INTELLI_TA,         // Tetris Attack
  NINSNES_INTELLI_FE4,        // Fire Emblem 4
  NINSNES_HUMAN,              // Human games (e.g. Clock Tower, Firemen, Septentrion)
  NINSNES_TOSE,               // TOSE games (e.g. Yoshi's Safari, Dragon Ball Z: Super Butouden 2)
  NINSNES_QUINTET_ACTR,       // ActRaiser, Soul Blazer
  NINSNES_QUINTET_ACTR2,      // ActRaiser 2
  NINSNES_QUINTET_IOG,        // Illusion of Gaia, Robotrek
  NINSNES_QUINTET_TS,         // Terranigma (Tenchi Souzou)
  NINSNES_FALCOM_YS4,         // Ys IV
};

enum class NinSnesSignatureId {
  None = 0,
  Standard,
  Earlier,
  Konami,
  Intelligent,
  Human,
  Tose,
  Quintet,
  FalcomYs4,
};

enum class NinSnesProfileId {
  Unknown = 0,
  Earlier,
  Standard,
  Rd1,
  Rd2,
  Hal,
  Konami,
  Lemmings,
  IntelliFe3,
  IntelliTa,
  IntelliFe4,
  Human,
  Tose,
  QuintetActR,
  QuintetActR2,
  QuintetIog,
  QuintetTs,
  FalcomYs4,
};

enum class NinSnesBaseProfileId {
  Unknown = 0,
  Earlier,
  Standard,
  Intelli,
};

enum class NinSnesAddressModelId {
  Direct = 0,
  KonamiBase,
  FalcomBaseOffset,
};

enum class NinSnesPlaylistModelId {
  Unknown = 0,
  Standard,
  Tose,
};

enum class NinSnesNoteParamModelId {
  Standard = 0,
  Lemmings,
  IntelliTable,
};

enum class NinSnesProgramResolverId {
  Direct = 0,
  StandardPercussion,
  QuintetActRBase,
  QuintetLookup,
  IntelliTaOverride,
};

enum class NinSnesPanModelId {
  StandardTable = 0,
  HalTable,
  ToseLinear,
};

enum class NinSnesInstrumentLayoutId {
  Earlier5Byte = 0,
  Standard6Byte,
  KonamiTuningTable,
};

enum class NinSnesIntelliModeId {
  None = 0,
  Fe3,
  Ta,
  Fe4,
};
