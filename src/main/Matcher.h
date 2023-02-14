#pragma once
#include "Root.h"
#include "Format.h"

#include "VGMSeq.h"
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMColl.h"


/*#define USE_SIMPLE_MATCHER(fmt_id, id_var)													\
	public:																					\
	virtual void Announce()																	\
	{																						\
		Format* format = pRoot->GetFormat(fmt_id);											\
		SimpleMatcher* matcher = (SimpleMatcher*)format->matcher;							\
		vector<VGMFile*>* files = matcher->AddMatchItem(this, id_var);						\
		if (files)																			\
		{																					\
			format->OnMatch(*files);														\
			delete files;																	\
		}																					\
	}*/

// *******
// Matcher
// *******

class Matcher {
 public:
  Matcher(Format *format);
  virtual ~Matcher();

  //virtual bool Match() = 0;

 public:
  virtual bool OnNewFile(VGMFile *file);
  virtual bool OnCloseFile(VGMFile *file);
  virtual bool MakeCollectionsForFile(VGMFile *file);
 protected:
  virtual bool OnNewSeq(VGMSeq *seq) { return false; }
  virtual bool OnNewInstrSet(VGMInstrSet *instrset) { return false; }
  virtual bool OnNewSampColl(VGMSampColl *sampcoll) { return false; }
  virtual bool OnCloseSeq(VGMSeq *seq) { return false; }
  virtual bool OnCloseInstrSet(VGMInstrSet *instrset) { return false; }
  virtual bool OnCloseSampColl(VGMSampColl *sampcoll) { return false; }

  Format *fmt;
};



// *************
// SimpleMatcher
// *************

//Simple Matcher is used for those collections that use only 1 Instr Set
//and optionally 1 SampColl

template<class IdType>
class SimpleMatcher:
    public Matcher {
 public:

 protected:

  // The following functions should return with the id variable containing the retrieved id of the file.
  // The bool return value is a flag for error: true on success and false on fail.
  virtual bool GetSeqId(VGMSeq *seq, IdType &id) = 0;
  virtual bool GetInstrSetId(VGMInstrSet *instrset, IdType &id) = 0;
  virtual bool GetSampCollId(VGMSampColl *sampcoll, IdType &id) = 0;


  SimpleMatcher(Format *format, bool bUsingSampColl = false)
      : Matcher(format),
        bRequiresSampColl(bUsingSampColl) {
  }

  virtual bool OnNewSeq(VGMSeq *seq) {
    IdType id;
    bool success = this->GetSeqId(seq, id);
    if (!success)
      return false;

    seqs.insert(std::pair<IdType, VGMSeq *>(id, seq));

    VGMInstrSet *matchingInstrSet = NULL;
    matchingInstrSet = instrsets[id];
    if (matchingInstrSet) {
      if (bRequiresSampColl) {
        VGMSampColl *matchingSampColl = sampcolls[id];
        if (matchingSampColl) {
          VGMColl *coll = fmt->NewCollection();
          if (!coll)
            return false;
          coll->SetName(seq->GetName());
          coll->UseSeq(seq);
          coll->AddInstrSet(matchingInstrSet);
          coll->AddSampColl(matchingSampColl);
          if (!coll->Load()) {
            delete coll;
            return false;
          }
        }
      }
      else {
        VGMColl *coll = fmt->NewCollection();
        if (!coll)
          return false;
        coll->SetName(seq->GetName());
        coll->UseSeq(seq);
        coll->AddInstrSet(matchingInstrSet);
        if (!coll->Load()) {
          delete coll;
          return false;
        }
      }
    }

    return true;
  }

  virtual bool OnNewInstrSet(VGMInstrSet *instrset) {
    IdType id;
    bool success = this->GetInstrSetId(instrset, id);
    if (!success)
      return false;
    if (instrsets[id])
      return false;
    instrsets[id] = instrset;

    VGMSampColl *matchingSampColl;
    if (bRequiresSampColl) {
      matchingSampColl = sampcolls[id];
      if (matchingSampColl && matchingSampColl->bLoadOnInstrSetMatch) {
        matchingSampColl->UseInstrSet(instrset);
        if (!matchingSampColl->Load()) {
          OnCloseSampColl(matchingSampColl);
          return false;
        }
        //pRoot->AddVGMFile(matchingSampColl);
      }
    }

    std::pair<typename std::multimap<IdType, VGMSeq *>::iterator, typename std::multimap<IdType, VGMSeq *>::iterator>
        itPair;
    // equal_range(b) returns pair<iterator,iterator> representing the range
    // of element with key b
    itPair = seqs.equal_range(id);
    // Loop through range of maps with id key
    for (typename std::multimap<IdType, VGMSeq *>::iterator it2 = itPair.first;
         it2 != itPair.second;
         ++it2) {
      VGMSeq *matchingSeq = (*it2).second;

      if (bRequiresSampColl) {
        if (matchingSampColl) {
          VGMColl *coll = fmt->NewCollection();
          if (!coll)
            return false;
          coll->SetName(matchingSeq->GetName());
          coll->UseSeq(matchingSeq);
          coll->AddInstrSet(instrset);
          coll->AddSampColl(matchingSampColl);
          coll->Load();
        }
      }
      else {
        VGMColl *coll = fmt->NewCollection();
        if (!coll)
          return false;
        coll->SetName(matchingSeq->GetName());
        coll->UseSeq(matchingSeq);
        coll->AddInstrSet(instrset);
        if (!coll->Load()) {
          delete coll;
          return false;
        }
      }
    }
    return true;
  }

  virtual bool OnNewSampColl(VGMSampColl *sampcoll) {
    if (bRequiresSampColl) {
      IdType id;
      bool success = this->GetSampCollId(sampcoll, id);
      if (!success)
        return false;
      if (sampcolls[id])
        return false;
      sampcolls[id] = sampcoll;

      VGMInstrSet *matchingInstrSet = instrsets[id];

      if (matchingInstrSet) {
        if (sampcoll->bLoadOnInstrSetMatch) {
          sampcoll->UseInstrSet(matchingInstrSet);
          if (!sampcoll->Load()) {
            OnCloseSampColl(sampcoll);
            return false;
          }
          //pRoot->AddVGMFile(sampcoll);
        }
      }

      std::pair<typename std::multimap<IdType, VGMSeq *>::iterator, typename std::multimap<IdType, VGMSeq *>::iterator>
          itPair;
      // equal_range(b) returns pair<iterator,iterator> representing the range
      // of element with key b
      itPair = seqs.equal_range(id);
      // Loop through range of maps with id key
      for (typename std::multimap<IdType, VGMSeq *>::iterator it2 = itPair.first;
           it2 != itPair.second;
           ++it2) {
        VGMSeq *matchingSeq = (*it2).second;

        if (matchingSeq && matchingInstrSet) {
          VGMColl *coll = fmt->NewCollection();
          if (!coll)
            return false;
          coll->SetName(matchingSeq->GetName());
          coll->UseSeq(matchingSeq);
          coll->AddInstrSet(matchingInstrSet);
          coll->AddSampColl(sampcoll);
          if (!coll->Load()) {
            delete coll;
            return false;
          }
        }
      }
      return true;
    }
    return true;
  }

  virtual bool OnCloseSeq(VGMSeq *seq) {
    IdType id;
    bool success = this->GetSeqId(seq, id);
    if (!success)
      return false;

    //Find the first matching key.
    typename std::multimap<IdType, VGMSeq *>::iterator itr = seqs.find(id);
    //Search for the specific seq to remove.
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

  virtual bool OnCloseInstrSet(VGMInstrSet *instrset) {
    IdType id;
    bool success = this->GetInstrSetId(instrset, id);
    if (!success)
      return false;
    instrsets.erase(id);
    return true;
  }

  virtual bool OnCloseSampColl(VGMSampColl *sampcoll) {
    IdType id;
    bool success = this->GetSampCollId(sampcoll, id);
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

class GetIdMatcher:
    public SimpleMatcher<uint32_t> {
 public:
  GetIdMatcher(Format *format, bool bRequiresSampColl = false)
      : SimpleMatcher(format, bRequiresSampColl) { }

  virtual bool GetSeqId(VGMSeq *seq, uint32_t &id) {
    id = seq->GetID();
    return (id != -1);
  }

  virtual bool GetInstrSetId(VGMInstrSet *instrset, uint32_t &id) {
    id = instrset->GetID();
    return (id != -1);
  }

  virtual bool GetSampCollId(VGMSampColl *sampcoll, uint32_t &id) {
    id = sampcoll->GetID();
    return (id != -1);
  }
};


// ***************
// FilenameMatcher
// ***************

class FilenameMatcher:
    public SimpleMatcher<std::wstring> {
 public:
  FilenameMatcher(Format *format, bool bRequiresSampColl = false)
      : SimpleMatcher(format, bRequiresSampColl) { }

  virtual bool GetSeqId(VGMSeq *seq, std::wstring &id) {
    RawFile *rawfile = seq->GetRawFile();
    id = rawfile->GetParRawFileFullPath();
    if (id == L"")        //wonder if empty() is equivalent?
      id = rawfile->GetFullPath();
    return (id != L"");
  }

  virtual bool GetInstrSetId(VGMInstrSet *instrset, std::wstring &id) {
    RawFile *rawfile = instrset->GetRawFile();
    id = rawfile->GetParRawFileFullPath();
    if (id == L"")        //wonder if empty() is equivalent?
      id = rawfile->GetFullPath();
    return (id != L"");
  }

  virtual bool GetSampCollId(VGMSampColl *sampcoll, std::wstring &id) {
    RawFile *rawfile = sampcoll->GetRawFile();
    id = rawfile->GetParRawFileFullPath();
    if (id == L"")        //wonder if empty() is equivalent?
      id = rawfile->GetFullPath();
    return (id != L"");
  }
};




// *************
// FilegroupMatcher
// *************

//Filegroup matcher is sort of a last resort method because it's highly prone to error.  It attempts
//to match based on an assumption of association between files by the fact they were
//loaded from the same source RawFile.  This is necessary for formats that do not use any
//built-in file association between sequences, instrument sets, and sample collections, like the
//standard PS1 format (SEQ/VAB).

//I should probably also program in some routines to allow it to be enabled or disabled based
//on the type of RawFile that was loaded.  PSF files, for example, have an almost certain
//association between the files contained within.  An entire cd image, on the other hand, does not.

class FilegroupMatcher:
    public Matcher {
 public:
  FilegroupMatcher(Format *format);

 protected:
  virtual bool OnNewSeq(VGMSeq *seq);
  virtual bool OnNewInstrSet(VGMInstrSet *instrset);
  virtual bool OnNewSampColl(VGMSampColl *sampcoll);

  virtual bool OnCloseSeq(VGMSeq *seq);
  virtual bool OnCloseInstrSet(VGMInstrSet *instrset);
  virtual bool OnCloseSampColl(VGMSampColl *sampcoll);

  virtual void MakeCollection(VGMInstrSet *instrset, VGMSampColl *sampcoll);
  virtual bool MakeCollectionsForFile(VGMFile *file);

  virtual void LookForMatch();

  template<class T>
  T *GetLargestVGMFileInList(std::list<T *> theList);

 protected:
  std::list<VGMSeq *> seqs;
  std::list<VGMInstrSet *> instrsets;
  std::list<VGMSampColl *> sampcolls;
};
