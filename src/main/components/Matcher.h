/*
 * VGMTrans (c) 2002-2014
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include <variant>
#include <map>
#include "Format.h"

#include "VGMSeq.h"
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMColl.h"

class VGMMiscFile;

// *******
// Matcher
// *******

class Matcher {
public:
  explicit Matcher(Format *format);
  virtual ~Matcher() = default;

  virtual void onFinishedScan(RawFile *rawfile) {}

  virtual bool onNewFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file);
  virtual bool onCloseFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file);

protected:
  virtual bool onNewSeq(VGMSeq *) { return false; }
  virtual bool onNewInstrSet(VGMInstrSet *) { return false; }
  virtual bool onNewSampColl(VGMSampColl *) { return false; }
  virtual bool onCloseSeq(VGMSeq *) { return false; }
  virtual bool onCloseInstrSet(VGMInstrSet *) { return false; }
  virtual bool onCloseSampColl(VGMSampColl *) { return false; }

  Format *fmt;
};

// *************
// SimpleMatcher
// *************

// Simple Matcher is used for those collections that use only 1 Instr Set
// and optionally 1 SampColl

template <class IdType>
class SimpleMatcher : public Matcher {
protected:
  // The following functions should return with the id variable containing the retrieved id of the
  // file. The bool return value is a flag for error: true on success and false on fail.
  virtual bool seqId(VGMSeq *seq, IdType &id) = 0;
  virtual bool instrSetId(VGMInstrSet *instrset, IdType &id) = 0;
  virtual bool sampCollId(VGMSampColl *sampcoll, IdType &id) = 0;

  explicit SimpleMatcher(Format *format, bool bUsingSampColl = false)
      : Matcher(format), bRequiresSampColl(bUsingSampColl) {}

  bool onNewSeq(VGMSeq *seq) override {
    IdType id;
    bool success = this->seqId(seq, id);
    if (!success)
      return false;

    seqs.insert(std::pair<IdType, VGMSeq *>(id, seq));

    if (VGMInstrSet *matchingInstrSet = instrsets[id]) {
      if (bRequiresSampColl) {
        if (VGMSampColl *matchingSampColl = sampcolls[id]) {
          VGMColl *coll = fmt->newCollection();
          if (!coll)
            return false;
          coll->setName(seq->name());
          coll->useSeq(seq);
          coll->addInstrSet(matchingInstrSet);
          coll->addSampColl(matchingSampColl);
          if (!coll->load()) {
            delete coll;
            return false;
          }
        }
      } else {
        VGMColl *coll = fmt->newCollection();
        if (!coll)
          return false;
        coll->setName(seq->name());
        coll->useSeq(seq);
        coll->addInstrSet(matchingInstrSet);
        if (!coll->load()) {
          delete coll;
          return false;
        }
      }
    }

    return true;
  }

  bool onNewInstrSet(VGMInstrSet *instrset) override {
    IdType id;
    bool success = this->instrSetId(instrset, id);
    if (!success)
      return false;
    if (instrsets[id])
      return false;
    instrsets[id] = instrset;

    VGMSampColl *matchingSampColl = nullptr;
    if (bRequiresSampColl) {
      matchingSampColl = sampcolls[id];
      if (matchingSampColl && matchingSampColl->shouldLoadOnInstrSetMatch()) {
        matchingSampColl->useInstrSet(instrset);
        if (!matchingSampColl->load()) {
          onCloseSampColl(matchingSampColl);
          return false;
        }
        // pRoot->AddVGMFile(matchingSampColl);
      }
    }

    std::pair<typename std::multimap<IdType, VGMSeq *>::iterator,
              typename std::multimap<IdType, VGMSeq *>::iterator>
        itPair;
    // equal_range(b) returns pair<iterator,iterator> representing the range
    // of element with key b
    itPair = seqs.equal_range(id);
    // Loop through range of maps with id key
    for (auto it2 = itPair.first; it2 != itPair.second; ++it2) {
      VGMSeq *matchingSeq = (*it2).second;

      if (bRequiresSampColl) {
        if (matchingSampColl) {
          VGMColl *coll = fmt->newCollection();
          if (!coll)
            return false;
          coll->setName(matchingSeq->name());
          coll->useSeq(matchingSeq);
          coll->addInstrSet(instrset);
          coll->addSampColl(matchingSampColl);
          coll->load();
        }
      } else {
        VGMColl *coll = fmt->newCollection();
        if (!coll)
          return false;
        coll->setName(matchingSeq->name());
        coll->useSeq(matchingSeq);
        coll->addInstrSet(instrset);
        if (!coll->load()) {
          delete coll;
          return false;
        }
      }
    }
    return true;
  }

  bool onNewSampColl(VGMSampColl *sampcoll) override {
    if (bRequiresSampColl) {
      IdType id;
      bool success = this->sampCollId(sampcoll, id);
      if (!success)
        return false;
      if (sampcolls[id])
        return false;
      sampcolls[id] = sampcoll;

      VGMInstrSet *matchingInstrSet = instrsets[id];

      if (matchingInstrSet) {
        if (sampcoll->shouldLoadOnInstrSetMatch()) {
          sampcoll->useInstrSet(matchingInstrSet);
          if (!sampcoll->load()) {
            onCloseSampColl(sampcoll);
            return false;
          }
          // pRoot->AddVGMFile(sampcoll);
        }
      }

      std::pair<typename std::multimap<IdType, VGMSeq *>::iterator,
                typename std::multimap<IdType, VGMSeq *>::iterator>
          itPair;
      // equal_range(b) returns pair<iterator,iterator> representing the range
      // of element with key b
      itPair = seqs.equal_range(id);
      // Loop through range of maps with id key
      for (auto it2 = itPair.first; it2 != itPair.second; ++it2) {
        VGMSeq *matchingSeq = (*it2).second;

        if (matchingSeq && matchingInstrSet) {
          VGMColl *coll = fmt->newCollection();
          if (!coll)
            return false;
          coll->setName(matchingSeq->name());
          coll->useSeq(matchingSeq);
          coll->addInstrSet(matchingInstrSet);
          coll->addSampColl(sampcoll);
          if (!coll->load()) {
            delete coll;
            return false;
          }
        }
      }
      return true;
    }
    return true;
  }

  bool onCloseSeq(VGMSeq *seq) override {
    IdType id;
    bool success = this->seqId(seq, id);
    if (!success)
      return false;

    // Find the first matching key.
    auto itr = seqs.find(id);
    // Search for the specific seq to remove.
    if (itr != seqs.end()) {
      do {
        if (itr->second == seq) {
          seqs.erase(itr);
          break;
        }

        ++itr;
      } while (itr != seqs.upper_bound(id));
    }
    return true;
  }

  bool onCloseInstrSet(VGMInstrSet *instrset) override {
    IdType id;
    bool success = this->instrSetId(instrset, id);
    if (!success)
      return false;
    instrsets.erase(id);
    return true;
  }

  bool onCloseSampColl(VGMSampColl *sampcoll) override {
    IdType id;
    bool success = this->sampCollId(sampcoll, id);
    if (!success)
      return false;
    sampcolls.erase(id);
    return true;
  }

private:
  bool bRequiresSampColl;

  std::multimap<IdType, VGMSeq *> seqs;
  std::map<IdType, VGMInstrSet *> instrsets;
  std::map<IdType, VGMSampColl *> sampcolls;
};

// ************
// GetIdMatcher
// ************

class GetIdMatcher : public SimpleMatcher<uint32_t> {
public:
  explicit GetIdMatcher(Format *format, bool bRequiresSampColl = false)
      : SimpleMatcher(format, bRequiresSampColl) {}

  bool seqId(VGMSeq *seq, uint32_t &id) override {
    id = seq->id();
    return (id != -1u);
  }

  bool instrSetId(VGMInstrSet *instrset, uint32_t &id) override {
    id = instrset->id();
    return (id != -1u);
  }

  bool sampCollId(VGMSampColl *sampcoll, uint32_t &id) override {
    id = sampcoll->id();
    return (id != -1u);
  }
};

// ***************
// FilenameMatcher
// ***************

class FilenameMatcher : public SimpleMatcher<std::string> {
public:
  explicit FilenameMatcher(Format *format, bool bRequiresSampColl = false)
      : SimpleMatcher(format, bRequiresSampColl) {}

  bool seqId(VGMSeq *seq, std::string &id) override {
    RawFile *rawfile = seq->rawFile();
    id = rawfile->path().string();
    return !id.empty();
  }

  bool instrSetId(VGMInstrSet *instrset, std::string &id) override {
    RawFile *rawfile = instrset->rawFile();
    id = rawfile->path().string();
    return !id.empty();
  }

  bool sampCollId(VGMSampColl *sampcoll, std::string &id) override {
    RawFile *rawfile = sampcoll->rawFile();
    id = rawfile->path().string();
    return !id.empty();
  }
};

// ****************
// FilegroupMatcher
// ****************

// FilegroupMatcher handles formats where the only method of associating sequences, instrument sets,
// and sample collections is that they are loaded together within the same RawFile. When loading is
// complete, FilegroupMatcher processes the VGMFiles in the order they were added and groups them
// into collections: thus, it assumes association by order. FilegroupMatcher does not retain
// state between loads, so it will never create a collection that spans multiple RawFiles, with the
// exception that FilegroupMatcher processes a psf file and all of its psflib dependencies together
// as a single load.

class FilegroupMatcher : public Matcher {
public:
  explicit FilegroupMatcher(Format *format);

  void onFinishedScan(RawFile *rawfile) override;

protected:
  bool onNewSeq(VGMSeq *seq) override;
  bool onNewInstrSet(VGMInstrSet *instrset) override;
  bool onNewSampColl(VGMSampColl *sampcoll) override;

  bool onCloseSeq(VGMSeq *seq) override;
  bool onCloseInstrSet(VGMInstrSet *instrset) override;
  bool onCloseSampColl(VGMSampColl *sampcoll) override;

  virtual void lookForMatch();

protected:
  std::list<VGMSeq *> seqs;
  std::list<VGMInstrSet *> instrsets;
  std::list<VGMSampColl *> sampcolls;
};
