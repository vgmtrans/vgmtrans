#include "pch.h"
#include "Matcher.h"

using namespace std;

Matcher::Matcher(Format *format) {
  fmt = format;
}

Matcher::~Matcher(void) {
}

bool Matcher::OnNewFile(VGMFile *file) {
  switch (file->GetFileType()) {
    case FILETYPE_SEQ:
      return OnNewSeq((VGMSeq *) file);
    case FILETYPE_INSTRSET:
      return OnNewInstrSet((VGMInstrSet *) file);
    case FILETYPE_SAMPCOLL:
      return OnNewSampColl((VGMSampColl *) file);
  }
  return false;
}

bool Matcher::OnCloseFile(VGMFile *file) {
  switch (file->GetFileType()) {
    case FILETYPE_SEQ:
      return OnCloseSeq((VGMSeq *) file);
    case FILETYPE_INSTRSET:
      return OnCloseInstrSet((VGMInstrSet *) file);
    case FILETYPE_SAMPCOLL:
      return OnCloseSampColl((VGMSampColl *) file);
  }
  return false;
}

bool Matcher::MakeCollectionsForFile(VGMFile *file) {
  return false;
}



/*
AddItem(ITEM_TYPE type, uint32_t id)
{

}*/

// *************
// SimpleMatcher
// *************

//template <class IdType>
//SimpleMatcher::SimpleMatcher(Format* format, bool bUsingSampColl)
//: Matcher(format), bRequiresSampColl(bUsingSampColl)
//{
//}
//
//template <class IdType>
//bool SimpleMatcher::OnNewSeq(VGMSeq* seq)
//{
//	//uint32_t id = seq->GetID();
//	IdType id;
//	bool success = this->GetSeqId(seq, id);
//	if (!success)
//		return false;
//	//if (!id)
//	//	return false;
//	if (seqs[id])
//		return false;
//	seqs[id] = seq;
//
//	/*if (sampcolls[id])
//	{
//		VGMColl* coll = fmt->NewCollection();
//		coll->SetName(seq->GetName());
//		coll->UseSeq(seq);
//		coll->AddInstrSet(
//	}*/
//
//	VGMInstrSet* matchingInstrSet = NULL;
//	matchingInstrSet = instrsets[id];
//	if (matchingInstrSet)
//	{
//		if (bRequiresSampColl)
//		{
//			VGMSampColl* matchingSampColl = sampcolls[id];
//			if (matchingSampColl)
//			{
//				VGMColl* coll = fmt->NewCollection();
//				coll->SetName(seq->GetName());
//				coll->UseSeq(seq);
//				coll->AddInstrSet(matchingInstrSet);
//				coll->AddSampColl(matchingSampColl);
//				coll->Load(); // needs error check
//			}
//		}
//		else
//		{
//			VGMColl* coll = fmt->NewCollection();
//			coll->SetName(seq->GetName());
//			coll->UseSeq(seq);
//			coll->AddInstrSet(matchingInstrSet);
//			coll->Load(); // needs error check
//		}
//	}
//
//	return true;
//}
//
//template <class IdType>
//bool SimpleMatcher::OnNewInstrSet(VGMInstrSet* instrset)
//{
//	IdType id;
//	bool success = this->GetInstrSetId(instrset, id);
//	if (!success)
//		return false;
//	//uint32_t id = instrset->GetID();
//	//if (!id)						//for the time being, 0 isn't a valid value of id
//	//	return false;
//	if (instrsets[id])
//		return false;
//	instrsets[id] = instrset;
//
//	VGMSeq* matchingSeq = seqs[id];
//	if (matchingSeq)
//	{
//		if (bRequiresSampColl)
//		{
//			VGMSampColl* matchingSampColl = sampcolls[id];
//			if (matchingSampColl)
//			{
//				VGMColl* coll = fmt->NewCollection();
//				coll->SetName(matchingSeq->GetName());
//				coll->UseSeq(matchingSeq);
//				coll->AddInstrSet(instrset);
//				coll->AddSampColl(matchingSampColl);
//				coll->Load(); // needs error check
//			}
//		}
////		if (bUsingSampColl)
////		{
////			VGMSampColl* matchingSampColl = sampcolls[
//		VGMColl* coll = fmt->NewCollection();
//		coll->SetName(matchingSeq->GetName());
//		coll->UseSeq(matchingSeq);
//		coll->AddInstrSet(instrset);
//		coll->Load(); // needs error check
//	}
//	return true;
//}
//
//template <class IdType>
//bool SimpleMatcher::OnNewSampColl(VGMSampColl* sampcoll)
//{
//	if (bRequiresSampColl)
//	{
//		IdType id;
//		bool success = this->GetSampCollId(sampcoll, id);
//		if (!success)
//			return false;
//		//uint32_t id = sampcoll->GetID();
//		//if (!id)
//		//	return false;
//		if (sampcolls[id])
//			return false;
//		sampcolls[id] = sampcoll;
//
//		VGMInstrSet* matchingInstrSet = instrsets[id];
//		VGMSeq* matchingSeq = seqs[id];
//		if (matchingSeq && matchingInstrSet)
//		{
//			VGMColl* coll = fmt->NewCollection();
//			coll->SetName(matchingSeq->GetName());
//			coll->UseSeq(matchingSeq);
//			coll->AddInstrSet(matchingInstrSet);
//			coll->AddSampColl(sampcoll);
//			coll->Load(); // needs error check
//		}
//		return true;
//	}
//}
//
//template <class IdType>
//bool SimpleMatcher::OnCloseSeq(VGMSeq* seq)
//{
//	IdType id;
//	bool success = this->GetSeqId(seq, id);
//	if (!success)
//		return false;
//	seqs.erase(id);
//	//seqs.erase(seq->GetID());
//	return true;
//}
//
//template <class IdType>
//bool SimpleMatcher::OnCloseInstrSet(VGMInstrSet* instrset)
//{
//	IdType id;
//	bool success = this->GetInstrSetId(instrset, id);
//	if (!success)
//		return false;
//	instrsets.erase(id);
//	//instrsets.erase(instrset->GetID());
//	return true;
//}
//
//template <class IdType>
//bool SimpleMatcher::OnCloseSampColl(VGMSampColl* sampcoll)
//{
//	IdType id;
//	bool success = this->GetSampCollId(sampcoll, id);
//	if (!success)
//		return false;
//	sampcolls.erase(id);
//	//sampcolls.erase(sampcoll->GetID());
//	return true;
//}





// ****************
// FilegroupMatcher
// ****************


FilegroupMatcher::FilegroupMatcher(Format *format)
    : Matcher(format) {
}

bool FilegroupMatcher::OnNewSeq(VGMSeq *seq) {
  seqs.push_back(seq);
  LookForMatch();
  return true;
}

bool FilegroupMatcher::OnNewInstrSet(VGMInstrSet *instrset) {
  instrsets.push_back(instrset);
  LookForMatch();
  return true;
}

bool FilegroupMatcher::OnNewSampColl(VGMSampColl *sampcoll) {
  sampcolls.push_back(sampcoll);
  LookForMatch();
  return true;
}

bool FilegroupMatcher::OnCloseSeq(VGMSeq *seq) {
  std::list<VGMSeq *>::iterator iterator = std::find(seqs.begin(), seqs.end(), seq);
  if (iterator != seqs.end()) {
    seqs.erase(iterator);
  }
  return true;
}

bool FilegroupMatcher::OnCloseInstrSet(VGMInstrSet *instrset) {
  std::list<VGMInstrSet *>::iterator iterator = std::find(instrsets.begin(), instrsets.end(), instrset);
  if (iterator != instrsets.end()) {
    instrsets.erase(iterator);
  }
  return true;
}

bool FilegroupMatcher::OnCloseSampColl(VGMSampColl *sampcoll) {
  std::list<VGMSampColl *>::iterator iterator = std::find(sampcolls.begin(), sampcolls.end(), sampcoll);
  if (iterator != sampcolls.end()) {
    sampcolls.erase(iterator);
  }
  return true;
}

void FilegroupMatcher::MakeCollection(VGMInstrSet *instrset, VGMSampColl *sampcoll) {
  if (seqs.size() >= 1) {
    for(VGMSeq *seq : seqs) {
      VGMColl *coll = fmt->NewCollection();
      coll->SetName(seq->GetName());
      coll->UseSeq(seq);
      coll->AddInstrSet(instrset);
      coll->AddSampColl(sampcoll);
      if (!coll->Load()) {
        delete coll;
      }
    }
  }
  else {
    VGMColl *coll = fmt->NewCollection();
    coll->SetName(instrset->GetName());
    coll->UseSeq(NULL);
    coll->AddInstrSet(instrset);
    coll->AddSampColl(sampcoll);
    if (!coll->Load()) {
      delete coll;
    }
  }
}

void FilegroupMatcher::LookForMatch() {

  if (g_isCliMode) {
    // in CLI mode, do not update collections dynamically
    return;
  }

  if (instrsets.size() == 1 && sampcolls.size() == 1) {
    VGMInstrSet* instrset = instrsets.front();
    VGMSampColl* sampcoll = sampcolls.front();
    MakeCollection(instrset, sampcoll);
    seqs.clear();
    instrsets.clear();
    sampcolls.clear();
  }
}

bool FilegroupMatcher::MakeCollectionsForFile(VGMFile *file) {

  if (instrsets.size() >= 1 && sampcolls.size() >= 1) {

    // make one VGMCollection for each sequence

    // NOTE: this only uses the first instrument set & sample collection
    // VGMInstrSet *instrset = instrsets.front();
    // VGMSampColl *sampcoll = sampcolls.front();
    // MakeCollection(instrset, sampcoll);

    // alternatively, we can combine all instrument sets & sample collections
    // into the same VGMCollection
    for(VGMSeq *seq : seqs) {
      VGMColl *coll = fmt->NewCollection();
      coll->SetName(seq->GetName());
      coll->UseSeq(seq);
      for (VGMInstrSet* instrset : instrsets) {
        for (VGMSampColl* sampcoll : sampcolls) {
          coll->AddInstrSet(instrset);
          coll->AddSampColl(sampcoll);
        }
      }
      if (!coll->Load()) {
        delete coll;
      }
    }
  }

  seqs.clear();
  instrsets.clear();
  sampcolls.clear();
  return true;
}

template<class T>
T *FilegroupMatcher::GetLargestVGMFileInList(list<T *> theList) {
  uint32_t s = 0;
  T *curWinner = NULL;
  for (typename list<T *>::iterator iter = theList.begin(); iter != theList.end(); iter++) {
    if ((*iter)->unLength > s) {
      s = (*iter)->unLength;
      curWinner = *iter;
    }
  }
  return curWinner;
}