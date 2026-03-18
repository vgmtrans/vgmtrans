/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "PianoRollFrameData.h"
#include "PianoRollSequenceCache.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class VGMItem;

// Owns PianoRoll selection and preview state independently from widget and input code.
class PianoRollSelectionModel {
public:
  using SelectableNote = PianoRollSequenceCache::SelectableNote;

  struct PreviewSelection {
    VGMItem* item = nullptr;
    int key = -1;
    uint32_t startTick = 0;
  };

  struct SelectionUpdate {
    bool selectionChanged = false;
    bool primaryChanged = false;
  };

  enum class PreviewSignal {
    None,
    Requested,
    Stopped,
  };

  struct PreviewUpdate {
    PreviewSignal signal = PreviewSignal::None;
    int previewTick = -1;
    std::vector<PreviewSelection> previewNotes;
  };

  PianoRollSelectionModel();

  void clear();

  [[nodiscard]] const std::vector<size_t>& selectedNoteIndices() const { return m_selectedNoteIndices; }
  [[nodiscard]] VGMItem* primarySelectedItem() const { return m_primarySelectedItem; }
  [[nodiscard]] const std::shared_ptr<const std::vector<PianoRollFrame::Note>>& selectedNotes() const {
    return m_selectedNotes;
  }

  [[nodiscard]] std::vector<VGMItem*> uniqueItemsForSelectedNoteIndices(
      const std::vector<SelectableNote>& notes) const;

  [[nodiscard]] SelectionUpdate applySelectedNoteIndices(
      std::vector<size_t> indices,
      const std::vector<SelectableNote>& notes,
      const std::function<bool(int trackIndex)>& isTrackEnabled,
      VGMItem* preferredPrimary);

  [[nodiscard]] PreviewUpdate applyPreviewNoteIndices(
      std::vector<size_t> indices,
      int previewTick,
      const std::vector<SelectableNote>& notes);

private:
  void normalizeNoteIndices(std::vector<size_t>& indices, size_t noteCount) const;
  [[nodiscard]] bool isSelectedItem(const std::vector<SelectableNote>& notes, VGMItem* item) const;
  void rebuildSelectedNotesCache(const std::vector<SelectableNote>& notes);

  std::shared_ptr<const std::vector<PianoRollFrame::Note>> m_selectedNotes;
  std::vector<size_t> m_selectedNoteIndices;
  std::vector<size_t> m_previewNoteIndices;
  VGMItem* m_primarySelectedItem = nullptr;
  int m_previewTick = -1;
};
