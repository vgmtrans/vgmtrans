#pragma once

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

enum class NinSnesInstrTableAddressModelId {
  Standard = 0,
  Human,
  Tose,
};

enum class NinSnesIntelliModeId {
  None = 0,
  Fe3,
  Ta,
  Fe4,
};
