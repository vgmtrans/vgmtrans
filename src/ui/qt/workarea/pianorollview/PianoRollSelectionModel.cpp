/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "PianoRollSelectionModel.h"

#include <algorithm>
#include <unordered_set>

PianoRollSelectionModel::PianoRollSelectionModel() {
  clear();
}

void PianoRollSelectionModel::clear() {
  m_selectedNotes = std::make_shared<std::vector<PianoRollFrame::Note>>();
  m_selectedNoteIndices.clear();
  m_previewNoteIndices.clear();
  m_primarySelectedItem = nullptr;
  m_previewTick = -1;
}

std::vector<VGMItem*> PianoRollSelectionModel::uniqueItemsForSelectedNoteIndices(
    const std::vector<SelectableNote>& notes) const {
  std::vector<VGMItem*> items;
  items.reserve(m_selectedNoteIndices.size());

  std::unordered_set<VGMItem*> seenItems;
  seenItems.reserve(m_selectedNoteIndices.size() * 2 + 1);

  for (size_t index : m_selectedNoteIndices) {
    if (index >= notes.size()) {
      continue;
    }
    VGMItem* item = notes[index].item;
    if (!item || !seenItems.insert(item).second) {
      continue;
    }
    items.push_back(item);
  }

  return items;
}

PianoRollSelectionModel::SelectionUpdate PianoRollSelectionModel::applySelectedNoteIndices(
    std::vector<size_t> indices,
    const std::vector<SelectableNote>& notes,
    const std::function<bool(int trackIndex)>& isTrackEnabled,
    VGMItem* preferredPrimary) {
  normalizeNoteIndices(indices, notes.size());

  size_t filteredCount = 0;
  for (size_t index : indices) {
    if (index >= notes.size()) {
      continue;
    }
    if (isTrackEnabled && !isTrackEnabled(notes[index].trackIndex)) {
      continue;
    }
    indices[filteredCount++] = index;
  }
  indices.resize(filteredCount);

  const bool selectionChanged = (indices != m_selectedNoteIndices);
  m_selectedNoteIndices = std::move(indices);
  rebuildSelectedNotesCache(notes);

  VGMItem* nextPrimary = nullptr;
  if (isSelectedItem(notes, preferredPrimary)) {
    nextPrimary = preferredPrimary;
  } else if (isSelectedItem(notes, m_primarySelectedItem)) {
    nextPrimary = m_primarySelectedItem;
  } else if (!m_selectedNoteIndices.empty()) {
    nextPrimary = notes[m_selectedNoteIndices.front()].item;
  }

  const bool primaryChanged = (nextPrimary != m_primarySelectedItem);
  m_primarySelectedItem = nextPrimary;
  return {.selectionChanged = selectionChanged, .primaryChanged = primaryChanged};
}

PianoRollSelectionModel::PreviewUpdate PianoRollSelectionModel::applyPreviewNoteIndices(
    std::vector<size_t> indices,
    int previewTick,
    const std::vector<SelectableNote>& notes) {
  normalizeNoteIndices(indices, notes.size());
  const int clampedPreviewTick = std::max(0, previewTick);

  if (indices.empty()) {
    if (m_previewNoteIndices.empty() && m_previewTick < 0) {
      return {};
    }
    m_previewNoteIndices.clear();
    m_previewTick = -1;
    return {.signal = PreviewSignal::Stopped};
  }

  if (indices == m_previewNoteIndices && clampedPreviewTick == m_previewTick) {
    return {};
  }

  m_previewNoteIndices = std::move(indices);
  m_previewTick = clampedPreviewTick;

  std::vector<PreviewSelection> previewNotes;
  previewNotes.reserve(m_previewNoteIndices.size());
  for (size_t index : m_previewNoteIndices) {
    const auto& selected = notes[index];
    if (!selected.item) {
      continue;
    }
    previewNotes.push_back({selected.item, selected.key, selected.startTick});
  }

  if (previewNotes.empty()) {
    m_previewNoteIndices.clear();
    m_previewTick = -1;
    return {.signal = PreviewSignal::Stopped};
  }

  return {
      .signal = PreviewSignal::Requested,
      .previewTick = clampedPreviewTick,
      .previewNotes = std::move(previewNotes),
  };
}

void PianoRollSelectionModel::normalizeNoteIndices(std::vector<size_t>& indices, size_t noteCount) const {
  indices.erase(std::remove_if(indices.begin(),
                               indices.end(),
                               [noteCount](size_t index) {
                                 return index >= noteCount;
                               }),
                indices.end());
  std::sort(indices.begin(), indices.end());
  indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
}

bool PianoRollSelectionModel::isSelectedItem(const std::vector<SelectableNote>& notes, VGMItem* item) const {
  if (!item) {
    return false;
  }

  for (size_t selectedIndex : m_selectedNoteIndices) {
    if (selectedIndex < notes.size() && notes[selectedIndex].item == item) {
      return true;
    }
  }
  return false;
}

void PianoRollSelectionModel::rebuildSelectedNotesCache(const std::vector<SelectableNote>& notes) {
  std::vector<PianoRollFrame::Note> selectedNotes;
  selectedNotes.reserve(m_selectedNoteIndices.size());
  for (size_t selectedIndex : m_selectedNoteIndices) {
    if (selectedIndex >= notes.size()) {
      continue;
    }
    const auto& selected = notes[selectedIndex];
    selectedNotes.push_back({
        selected.startTick,
        selected.duration,
        selected.key,
        selected.trackIndex,
    });
  }
  m_selectedNotes = std::make_shared<std::vector<PianoRollFrame::Note>>(std::move(selectedNotes));
}
