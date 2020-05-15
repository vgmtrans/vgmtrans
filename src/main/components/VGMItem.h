/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <algorithm>
#include "common.h"

enum EventColors {
    CLR_UNKNOWN,
    CLR_UNRECOGNIZED,
    CLR_HEADER,
    CLR_MISC,
    CLR_MARKER,
    CLR_TIMESIG,
    CLR_TEMPO,
    CLR_PROGCHANGE,
    CLR_TRANSPOSE,
    CLR_PRIORITY,
    CLR_VOLUME,
    CLR_EXPRESSION,
    CLR_PAN,
    CLR_NOTEON,
    CLR_NOTEOFF,
    CLR_DURNOTE,
    CLR_TIE,
    CLR_REST,
    CLR_PITCHBEND,
    CLR_PITCHBENDRANGE,
    CLR_MODULATION,
    CLR_PORTAMENTO,
    CLR_PORTAMENTOTIME,
    CLR_CHANGESTATE,
    CLR_ADSR,
    CLR_LFO,
    CLR_REVERB,
    CLR_SUSTAIN,
    CLR_LOOP,
    CLR_LOOPFOREVER,
    CLR_TRACKEND,
};

class RawFile;
class VGMFile;
class VGMHeader;

class VGMItem {
public:
    enum Icon {
        ICON_SEQ,
        ICON_INSTRSET,
        ICON_SAMPCOLL,
        ICON_UNKNOWN,
        ICON_NOTE,
        ICON_TRACK,
        ICON_REST,
        ICON_CONTROL,
        ICON_STARTREP,
        ICON_ENDREP,
        ICON_TIMESIG,
        ICON_TEMPO,
        ICON_PROGCHANGE,
        ICON_TRACKEND,
        ICON_COLL,
        ICON_INSTR,
        ICON_SAMP,
        ICON_BINARY,
        ICON_MAX
    };

    VGMItem(VGMFile *thevgmfile, uint32_t theOffset, uint32_t theLength = 0,
            std::string theName = "", uint8_t color = 0);
    virtual ~VGMItem();

    friend bool operator>(VGMItem &item1, VGMItem &item2);
    friend bool operator<=(VGMItem &item1, VGMItem &item2);
    friend bool operator<(VGMItem &item1, VGMItem &item2);
    friend bool operator>=(VGMItem &item1, VGMItem &item2);

    virtual bool IsItemAtOffset(uint32_t offset, bool includeContainer = true,
                                bool matchStartOffset = false);
    virtual VGMItem *GetItemFromOffset(uint32_t offset, bool includeContainer = true,
                                       bool matchStartOffset = false);

    virtual uint32_t GuessLength();
    virtual void SetGuessedLength();
    [[nodiscard]] RawFile *GetRawFile() const;
    virtual std::string GetDescription() { return name; }
    virtual Icon GetIcon() { return ICON_BINARY; }
    virtual void AddToUI(VGMItem *parent, void *UI_specific);

protected:
    // TODO make inline
    uint32_t GetBytes(uint32_t nIndex, uint32_t nCount, void *pBuffer) const;

    [[nodiscard]] uint8_t GetByte(uint32_t offset) const;
    [[nodiscard]] uint16_t GetShort(uint32_t offset) const;
    [[nodiscard]] uint32_t GetWord(uint32_t offset) const;
    [[nodiscard]] uint16_t GetShortBE(uint32_t offset) const;
    [[nodiscard]] uint32_t GetWordBE(uint32_t offset) const;
    [[nodiscard]] bool IsValidOffset(uint32_t offset) const;

public:
    uint8_t color;
    VGMFile *vgmfile;
    std::string name;
    uint32_t dwOffset;  // offset in the pDoc data buffer
    uint32_t unLength;  // num of bytes the event engulfs
};

class VGMContainerItem : public VGMItem {
public:
    VGMContainerItem(VGMFile *thevgmfile, uint32_t theOffset, uint32_t theLength = 0,
                     const std::string &theName = "", uint8_t color = CLR_HEADER);
    ~VGMContainerItem() override;

    VGMItem *GetItemFromOffset(uint32_t offset, bool includeContainer = true,
                               bool matchStartOffset = false) override;

    uint32_t GuessLength() override;
    void SetGuessedLength() override;

    void AddToUI(VGMItem *parent, void *UI_specific) override;
    VGMHeader *AddHeader(uint32_t offset, uint32_t length, const std::string &name = "Header");
    void AddItem(VGMItem *item);
    void AddSimpleItem(uint32_t offset, uint32_t length, const std::string &theName);
    void AddUnknownItem(uint32_t offset, uint32_t length);

    template<class T>
    void AddContainer(std::vector<T *> &container) {
        containers.push_back(reinterpret_cast<std::vector<VGMItem *> *>(&container));
    }

    template<class T>
    bool RemoveContainer(std::vector<T *> &container) {
        auto iter = std::find(containers.begin(), containers.end(),
                              reinterpret_cast<std::vector<VGMItem *> *>(&container));
        if (iter != containers.end()) {
            containers.erase(iter);
            return true;
        } else
            return false;
    }

public:
    std::vector<VGMHeader *> headers;
    std::vector<std::vector<VGMItem *> *> containers;
    std::vector<VGMItem *> localitems;
};
