#pragma once
#include "Scanner.h"

struct FileRange {
  uint32_t offset;
  uint32_t size;
};

class RSAR {
public:
  RSAR(RawFile *file_) : file(file_) {}

  RawFile *file;

  struct RBNK {
    std::string name;

    enum SampCollType {
      WAVE,
      RWAR,
    };
    SampCollType type;

    FileRange instr;
    FileRange wave;
    uint32_t waveDataOffset;
  };

  struct RSEQ {
    std::string name;
    uint32_t rseqOffset;
    uint32_t dataOffset;
    uint32_t bankIdx;
  };

  bool Parse();

  /* Parsed stuff. */
  std::vector<RBNK> rbnks;
  std::vector<RSEQ> rseqs;

private:
  FileRange CheckBlock(uint32_t offs, const char *magic) {
    if (!file->MatchBytes(magic, offs))
      return { 0, 0 };

    uint32_t size = file->GetWordBE(offs + 0x04);
    return { offs + 0x08, size - 0x08 };
  }

  /* RSAR structure internals... */
  struct Sound {
    std::string name;
    uint32_t fileID;

    enum Type {
      SEQ = 1,
      STRM = 2,
      WAVE = 3
    };
    Type type;
    struct {
      uint32_t dataOffset;
      uint32_t bankID;
      uint32_t allocTrack;
    } seq;
  };

  struct Bank {
    std::string name;
    uint32_t fileID;
  };

  struct GroupItem {
    uint32_t fileID;
    FileRange data;
    FileRange waveData;
  };

  struct Group {
    std::string name;
    FileRange data;
    FileRange waveData;
    std::vector<GroupItem> items;
  };

  struct File {
    uint32_t groupID;
    uint32_t index;
  };

  std::vector<std::string> stringTable;
  std::vector<Sound> soundTable;
  std::vector<Bank> bankTable;
  std::vector<File> fileTable;
  std::vector<Group> groupTable;

  std::vector<std::string> ParseSymbBlock(uint32_t blockBase);
  std::vector<Sound> ReadSoundTable(uint32_t infoBlockOffs);
  std::vector<Bank> ReadBankTable(uint32_t infoBlockOffs);
  std::vector<File> ReadFileTable(uint32_t infoBlockOffs);
  std::vector<Group> ReadGroupTable(uint32_t infoBlockOffs);

  RBNK ParseRBNK(Bank *bank);
  RSEQ ParseRSEQ(Sound *sound);
};

class RSARScanner :
  public VGMScanner {
public:
  RSARScanner(void) {
    USE_EXTENSION(L"brsar")
  }
  virtual ~RSARScanner(void) {
  }

  virtual void Scan(RawFile *file, void *info = 0);

private:
  VGMSampColl * LoadBankSampColl(RawFile *file, RSAR::RBNK *rbnk);
};
