/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "widgets/StitchUI.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <unordered_set>
#include <vector>

#include <QAbstractItemView>
#include <QAbstractButton>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFrame>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

#include "common.h"
#include "Root.h"
#include "QtVGMRoot.h"
#include "StitchExport.h"
#include "VGMColl.h"
#include "VGMSeq.h"
#include "services/NotificationCenter.h"
#include "util/UIHelpers.h"
#include "widgets/EmptyStateWidget.h"
#include "workarea/VGMCollListView.h"
#include "workarea/MdiArea.h"

namespace {

constexpr int RoleCollectionPointer = Qt::UserRole;
constexpr QSize kQueueIconSize(16, 16);
constexpr QSize kActionIconSize(16, 16);
constexpr int kQueueRowHeight = 28;
constexpr int kBalloonMinWidth = 420;
constexpr int kBalloonMinHeight = 320;
constexpr int kAnchorVerticalGap = 6;

std::vector<VGMColl*> dedupeCollections(const std::vector<VGMColl*>& collections) {
  std::vector<VGMColl*> deduped;
  deduped.reserve(collections.size());
  std::unordered_set<VGMColl*> seen;

  for (VGMColl* coll : collections) {
    if (!coll) {
      continue;
    }
    if (seen.insert(coll).second) {
      deduped.push_back(coll);
    }
  }

  return deduped;
}

bool collectionExists(VGMColl* coll) {
  if (!coll) {
    return false;
  }

  const auto& allCollections = qtVGMRoot.vgmColls();
  return std::find(allCollections.begin(), allCollections.end(), coll) != allCollections.end();
}

std::string buildExportSuccessMessage(const std::filesystem::path &midiPath,
                                      const std::filesystem::path &sf2Path,
                                      const conversion::MidiMergeResult &mergeResult,
                                      const std::vector<conversion::MidiMergeEntry> &entries,
                                      const std::vector<uint8_t> &bankOffsets) {
  std::string message = "Stitched export created:\n";
  message += "MIDI: " + midiPath.string() + "\n";
  message += "SF2: " + sf2Path.string();

  const bool hasPartDetails =
      mergeResult.startTimes.size() == entries.size() &&
      bankOffsets.size() == entries.size();
  if (!hasPartDetails) {
    return message;
  }

  message += "\n\nParts:\n";
  for (size_t i = 0; i < entries.size(); ++i) {
    message += entries[i].label;
    message += " - tick ";
    message += std::to_string(mergeResult.startTimes[i]);
    message += ", bank +";
    message += std::to_string(bankOffsets[i]);
    if (i + 1 < entries.size()) {
      message += "\n";
    }
  }
  return message;
}

bool exportStitchedCollections(const std::vector<VGMColl*> &orderedCollections) {
  if (orderedCollections.size() < 2) {
    pRoot->UI_toast("Select at least two collections to stitch.", ToastType::Info);
    return false;
  }

  std::vector<conversion::MidiMergeEntry> entries;
  entries.reserve(orderedCollections.size());

  for (VGMColl *coll : orderedCollections) {
    if (!coll) {
      continue;
    }

    auto *seq = coll->seq();
    if (!seq) {
      pRoot->UI_toast("Each selected collection must contain a sequence for stitched export.",
                      ToastType::Error, 15000);
      return false;
    }

    entries.push_back({coll, coll->name()});
  }

  if (entries.size() < 2) {
    pRoot->UI_toast("Select at least two collections containing sequences.",
                    ToastType::Info);
    return false;
  }

  std::filesystem::path suggestedFileName = makeSafeFileName(entries.front().label);
  if (suggestedFileName.empty()) {
    suggestedFileName = "stitched-collections";
  }
  suggestedFileName.replace_extension("mid");

  const std::filesystem::path midiPath = pRoot->UI_getSaveFilePath(
      suggestedFileName.string(),
      "mid");
  if (midiPath.empty()) {
    return false;
  }

  auto sf2Path = midiPath;
  sf2Path.replace_extension("sf2");

  conversion::StitchedExportResult exportResult;
  if (!conversion::exportStitchedMidiAndSf2(entries, midiPath, sf2Path,
                                            &exportResult)) {
    pRoot->UI_toast("Failed to stitch selected collections. Check logs for details.",
                    ToastType::Error, 15000);
    return false;
  }

  pRoot->UI_toast(
      buildExportSuccessMessage(midiPath, sf2Path,
                                exportResult.mergeResult,
                                entries,
                                exportResult.bankOffsets),
      ToastType::Success,
      10000);
  return true;
}

InstructionHint defaultEmptyStateHeadingHint(const QString &iconPath,
                                             const QString &headingText) {
  InstructionHint hint;
  hint.iconPath = iconPath;
  hint.text = headingText;
  hint.fontScale = 1.28;
  hint.iconScale = 1.9;
  hint.spacingScale = 0.24;
  hint.fontWeight = QFont::DemiBold;
  hint.minPointSize = 13;
  hint.fontFamily.clear();
  return hint;
}

QString collectionLabel(const VGMColl *coll) {
  QString label = coll ? QString::fromStdString(coll->name())
                       : QStringLiteral("(unknown collection)");
  if (coll && coll->seq()) {
    label += QStringLiteral(" - ");
    label += QString::fromStdString(coll->seq()->name());
  }
  return label;
}

QListWidgetItem *makeCollectionItem(VGMColl *coll) {
  auto *item = new QListWidgetItem(collectionLabel(coll));
  item->setData(RoleCollectionPointer,
                static_cast<qulonglong>(reinterpret_cast<uintptr_t>(coll)));
  item->setSizeHint(QSize(0, kQueueRowHeight));
  return item;
}

VGMColl* collFromItem(const QListWidgetItem* item) {
  return item
             ? reinterpret_cast<VGMColl*>(
                   static_cast<uintptr_t>(item->data(RoleCollectionPointer).toULongLong()))
             : nullptr;
}

void configureActionButton(QPushButton* button, const QString& text, const QString& toolTip) {
  if (!button) {
    return;
  }

  button->setText(text);
  button->setToolTip(toolTip);
  button->setFocusPolicy(Qt::NoFocus);
  button->setCursor(Qt::ArrowCursor);
  button->setIconSize(kActionIconSize);
}

class StitchDragHandleWidget final : public QWidget {
public:
  explicit StitchDragHandleWidget(QWidget* parent = nullptr) : QWidget(parent) {
    setCursor(Qt::OpenHandCursor);
  }

protected:
  void mousePressEvent(QMouseEvent* event) override {
    if (event && event->button() == Qt::LeftButton) {
      if (QWidget* topLevel = window(); topLevel && topLevel->windowHandle()
          && topLevel->windowHandle()->startSystemMove()) {
        event->accept();
        return;
      }
    }
    QWidget::mousePressEvent(event);
  }
};

class StitchQueueListWidget final : public QListWidget {
public:
  using ExternalDropHandler = std::function<void(const std::vector<VGMColl*>&, int)>;
  using RemoveSelectionHandler = std::function<void()>;

  explicit StitchQueueListWidget(QWidget* parent = nullptr) : QListWidget(parent) {
  }

  void setExternalDropHandler(ExternalDropHandler handler) {
    m_externalDropHandler = std::move(handler);
  }

  void setRemoveSelectionHandler(RemoveSelectionHandler handler) {
    m_removeSelectionHandler = std::move(handler);
  }

protected:
  bool event(QEvent* event) override {
    if (event && event->type() == QEvent::ShortcutOverride) {
      auto* keyEvent = static_cast<QKeyEvent*>(event);
      if (keyEvent && (keyEvent->key() == Qt::Key_Delete || keyEvent->key() == Qt::Key_Backspace)) {
        event->accept();
        return true;
      }
    }

    return QListWidget::event(event);
  }

  void keyPressEvent(QKeyEvent* event) override {
    if (event && (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)) {
      if (m_removeSelectionHandler) {
        m_removeSelectionHandler();
      }
      event->accept();
      return;
    }

    QListWidget::keyPressEvent(event);
  }

  void dragEnterEvent(QDragEnterEvent* event) override {
    if (qobject_cast<VGMCollListView*>(event->source())) {
      event->acceptProposedAction();
      return;
    }
    QListWidget::dragEnterEvent(event);
  }

  void dragMoveEvent(QDragMoveEvent* event) override {
    if (qobject_cast<VGMCollListView*>(event->source())) {
      event->acceptProposedAction();
      return;
    }
    QListWidget::dragMoveEvent(event);
  }

  void dropEvent(QDropEvent* event) override {
    if (auto* sourceList = qobject_cast<VGMCollListView*>(event->source())) {
      if (m_externalDropHandler) {
        int insertionRow = count();
        const QModelIndex hoveredIndex = indexAt(event->position().toPoint());
        if (hoveredIndex.isValid()) {
          insertionRow = hoveredIndex.row();
          if (dropIndicatorPosition() == QAbstractItemView::BelowItem) {
            ++insertionRow;
          }
        }

        m_externalDropHandler(sourceList->selectedCollections(), insertionRow);
      }

      event->acceptProposedAction();
      return;
    }

    QListWidget::dropEvent(event);
  }

private:
  ExternalDropHandler m_externalDropHandler;
  RemoveSelectionHandler m_removeSelectionHandler;
};

class StitchExportBalloon final : public QFrame {
public:
  explicit StitchExportBalloon(QWidget* parent = nullptr)
  : QFrame(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint) {
    setObjectName(QStringLiteral("stitchExportBalloon"));
    setFrameShape(QFrame::NoFrame);
    setFrameShadow(QFrame::Plain);
    setMinimumSize(kBalloonMinWidth, kBalloonMinHeight);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    setAttribute(Qt::WA_DeleteOnClose, false);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(10, 0, 10, 10);
    rootLayout->setSpacing(0);

    m_headingDragHandle = new StitchDragHandleWidget(this);

    auto* headingRow = new QHBoxLayout(m_headingDragHandle);
    headingRow->setContentsMargins(0, 10, 0, 8);
    headingRow->setSpacing(4);

    auto* heading = new QLabel(QStringLiteral("Stitch Queue"), m_headingDragHandle);
    heading->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    heading->setFont(headingFont);

    m_closeButton = new QToolButton(m_headingDragHandle);
    configureToolButton(m_closeButton, QStringLiteral("Close stitch queue"),
                        QSize(22, 20), QSize(14, 14));

    headingRow->addWidget(heading);
    headingRow->addStretch(1);
    headingRow->addWidget(m_closeButton);
    rootLayout->addWidget(m_headingDragHandle);

    m_queueList = new StitchQueueListWidget(this);
    m_queueList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_queueList->setAlternatingRowColors(true);
    m_queueList->setIconSize(kQueueIconSize);
    m_queueList->setDragEnabled(true);
    m_queueList->setAcceptDrops(true);
    m_queueList->setDropIndicatorShown(true);
    m_queueList->setDragDropMode(QAbstractItemView::DragDrop);
    m_queueList->setDefaultDropAction(Qt::MoveAction);

    m_emptyState = new EmptyStateWidget(
        defaultEmptyStateHeadingHint(QStringLiteral(":/icons/stitch.svg"),
                                     QStringLiteral("Drop collections to stitch")),
        m_queueList, this);
    m_emptyState->setBodyText(
        QStringLiteral("Drag from Collections, reorder, then export MIDI + SF2."));
    rootLayout->addWidget(m_emptyState, 1);

    auto* actionRow = new QHBoxLayout();
    actionRow->setContentsMargins(0, 8, 0, 0);
    actionRow->setSpacing(6);

    m_removeButton = new QPushButton(this);
    m_clearButton = new QPushButton(this);
    m_exportButton = new QPushButton(this);

    configureActionButton(m_removeButton, QStringLiteral("Remove"),
                          QStringLiteral("Remove selected from stitch queue"));
    configureActionButton(m_clearButton, QStringLiteral("Clear"),
                          QStringLiteral("Clear stitch queue"));
    configureActionButton(m_exportButton, QStringLiteral("Export"),
                          QStringLiteral("Export stitched MIDI + SF2"));

    actionRow->addWidget(m_removeButton);
    actionRow->addWidget(m_clearButton);
    actionRow->addStretch(1);
    actionRow->addWidget(m_exportButton);
    rootLayout->addLayout(actionRow);

    m_queueList->setExternalDropHandler([this](const std::vector<VGMColl*>& collections, int row) {
      addCollections(collections, row);
    });
    m_queueList->setRemoveSelectionHandler([this]() {
      removeSelectedQueueItems();
    });

    connect(m_removeButton, &QPushButton::clicked, this, [this]() {
      removeSelectedQueueItems();
    });

    connect(m_clearButton, &QPushButton::clicked, this, [this]() {
      m_queueList->clear();
      refreshUi();
    });

    connect(m_exportButton, &QPushButton::clicked, this, [this]() {
      if (exportStitchedCollections(orderedCollections())) {
        hide();
      }
    });
    connect(m_closeButton, &QToolButton::clicked, this, &QWidget::hide);

    connect(m_queueList, &QListWidget::itemSelectionChanged, this, [this]() { refreshUi(); });
    connect(m_queueList->model(), &QAbstractItemModel::rowsInserted, this, [this]() { refreshUi(); });
    connect(m_queueList->model(), &QAbstractItemModel::rowsRemoved, this, [this]() { refreshUi(); });
    connect(m_queueList->model(), &QAbstractItemModel::rowsMoved, this, [this]() { refreshUi(); });
    connect(&qtVGMRoot, &QtVGMRoot::UI_endRemoveVGMColls, this, [this]() {
      pruneMissingQueueCollections();
    });

    refreshUi();
  }

  void openForCollections(const std::vector<VGMColl*>& initialCollections, QWidget* anchor,
                          QAbstractButton* toggleButton = nullptr) {
    if (toggleButton) {
      setToggleButton(toggleButton);
    }

    m_queueList->clear();
    addCollections(initialCollections, 0);
    refreshUi();

    adjustSize();
    if (width() < kBalloonMinWidth) {
      resize(kBalloonMinWidth, height());
    }
    if (height() < kBalloonMinHeight) {
      resize(width(), kBalloonMinHeight);
    }

    const QRect mdiRect(MdiArea::the()->mapToGlobal(QPoint(0, 0)), MdiArea::the()->size());
    move(mdiRect.left(), mdiRect.bottom() - height() + 1);
    move(mdiRect.left() + 20, mdiRect.bottom() - height() - 20);
    show();
    raise();
    activateWindow();
  }

  void setToggleButton(QAbstractButton* toggleButton) {
    m_toggleButton = toggleButton;
    if (m_toggleButton) {
      m_toggleButton->setChecked(isVisible());
    }
  }

private:
  void showEvent(QShowEvent* event) override {
    if (m_toggleButton) {
      m_toggleButton->setChecked(true);
    }
    NotificationCenter::the()->setCollectionStitchUiVisible(true);
    QFrame::showEvent(event);
  }

  void hideEvent(QHideEvent* event) override {
    if (m_toggleButton) {
      m_toggleButton->setChecked(false);
    }
    NotificationCenter::the()->setCollectionStitchUiVisible(false);
    NotificationCenter::the()->setStitchPlanCollections({});
    QFrame::hideEvent(event);
  }

  bool containsCollection(VGMColl* coll) const {
    if (!coll) {
      return false;
    }

    for (int row = 0; row < m_queueList->count(); ++row) {
      if (collFromItem(m_queueList->item(row)) == coll) {
        return true;
      }
    }
    return false;
  }

  void removeSelectedQueueItems() {
    const QList<QListWidgetItem*> selectedItems = m_queueList->selectedItems();
    for (QListWidgetItem* item : selectedItems) {
      delete m_queueList->takeItem(m_queueList->row(item));
    }
    refreshUi();
  }

  void pruneMissingQueueCollections() {
    if (!isVisible()) {
      return;
    }

    bool removedAny = false;
    QSignalBlocker modelSignalBlocker(m_queueList->model());

    for (int row = m_queueList->count() - 1; row >= 0; --row) {
      if (!collectionExists(collFromItem(m_queueList->item(row)))) {
        delete m_queueList->takeItem(row);
        removedAny = true;
      }
    }

    if (removedAny) {
      refreshUi();
    }
  }

  void addCollections(const std::vector<VGMColl*>& collections, int insertionRow) {
    if (collections.empty()) {
      return;
    }

    const std::vector<VGMColl*> deduped = dedupeCollections(collections);
    int targetRow = std::clamp(insertionRow, 0, m_queueList->count());

    for (VGMColl* coll : deduped) {
      if (!collectionExists(coll) || containsCollection(coll)) {
        continue;
      }

      m_queueList->insertItem(targetRow++, makeCollectionItem(coll));
    }

    refreshUi();
  }

  std::vector<VGMColl*> orderedCollections() const {
    std::vector<VGMColl*> ordered;
    ordered.reserve(static_cast<size_t>(m_queueList->count()));

    for (int row = 0; row < m_queueList->count(); ++row) {
      if (VGMColl* coll = collFromItem(m_queueList->item(row)); coll && collectionExists(coll)) {
        ordered.push_back(coll);
      }
    }

    return ordered;
  }

  void publishStitchPlanCollections() const {
    QList<VGMColl*> ordered;
    const std::vector<VGMColl*> orderedVector = orderedCollections();
    ordered.reserve(static_cast<qsizetype>(orderedVector.size()));
    for (VGMColl* coll : orderedVector) {
      ordered.append(coll);
    }
    NotificationCenter::the()->setStitchPlanCollections(ordered);
  }

  void refreshUi() {
    m_emptyState->setEmptyStateFrom(*m_queueList);

    const bool hasSelection = !m_queueList->selectedItems().isEmpty();
    const bool hasItems = m_queueList->count() > 0;
    m_removeButton->setEnabled(hasSelection);
    m_clearButton->setEnabled(hasItems);
    m_exportButton->setEnabled(m_queueList->count() >= 2);

    const QPalette palette = this->palette();
    const auto refreshButtonIcon = [&](QPushButton* button, const QString& iconPath) {
      if (!button) {
        return;
      }
      const QColor iconColor = button->isEnabled()
                                   ? palette.color(QPalette::ButtonText)
                                   : palette.color(QPalette::Disabled, QPalette::ButtonText);
      button->setIcon(stencilSvgIcon(iconPath, iconColor));
    };

    refreshButtonIcon(m_removeButton, QStringLiteral(":/icons/transfer-left.svg"));
    refreshButtonIcon(m_clearButton, QStringLiteral(":/icons/clear.svg"));
    refreshButtonIcon(m_exportButton, QStringLiteral(":/icons/export.svg"));
    refreshStencilToolButton(m_closeButton, QStringLiteral(":/icons/toast_close.svg"), palette);

    publishStitchPlanCollections();
  }

  void positionRelativeTo(QWidget* anchor) {
    QRect anchorRect;
    if (anchor) {
      anchorRect = QRect(anchor->mapToGlobal(QPoint(0, 0)), anchor->size());
    } else if (QWidget* activeWindow = QApplication::activeWindow()) {
      anchorRect = activeWindow->geometry();
    }

    if (!anchorRect.isValid()) {
      return;
    }

    QScreen* screen = nullptr;
    if (anchor) {
      screen = anchor->screen();
    }
    if (!screen) {
      screen = QApplication::screenAt(anchorRect.center());
    }
    if (!screen) {
      screen = QApplication::primaryScreen();
    }

    const QRect bounds = screen ? screen->availableGeometry() : QRect();

    QPoint desiredPos;
    if (anchor) {
      desiredPos = QPoint(anchorRect.center().x() - width() / 2,
                          anchorRect.bottom() + kAnchorVerticalGap);
    } else {
      desiredPos = QPoint(anchorRect.center().x() - width() / 2,
                          anchorRect.center().y() - height() / 2);
    }

    if (bounds.isValid()) {
      if (desiredPos.x() + width() > bounds.right()) {
        desiredPos.setX(bounds.right() - width());
      }
      if (desiredPos.x() < bounds.left()) {
        desiredPos.setX(bounds.left());
      }

      if (anchor && desiredPos.y() + height() > bounds.bottom()) {
        desiredPos.setY(anchorRect.top() - height() - kAnchorVerticalGap);
      }
      if (desiredPos.y() + height() > bounds.bottom()) {
        desiredPos.setY(bounds.bottom() - height());
      }
      if (desiredPos.y() < bounds.top()) {
        desiredPos.setY(bounds.top());
      }
    }

    move(desiredPos);
  }

  StitchQueueListWidget* m_queueList = nullptr;
  EmptyStateWidget* m_emptyState = nullptr;
  QPushButton* m_removeButton = nullptr;
  QPushButton* m_clearButton = nullptr;
  QPushButton* m_exportButton = nullptr;
  QWidget* m_headingDragHandle = nullptr;
  QToolButton* m_closeButton = nullptr;
  QPointer<QAbstractButton> m_toggleButton;
};

QPointer<StitchExportBalloon> g_stitchExportBalloon;

StitchExportBalloon* ensureStitchExportBalloon(QWidget* owner) {
  if (!g_stitchExportBalloon || g_stitchExportBalloon->parentWidget() != owner) {
    if (g_stitchExportBalloon) {
      if (g_stitchExportBalloon->isVisible()) {
        NotificationCenter::the()->setCollectionStitchUiVisible(false);
      }
      g_stitchExportBalloon->deleteLater();
    }
    g_stitchExportBalloon = new StitchExportBalloon(owner);
  }
  return g_stitchExportBalloon;
}

}  // namespace

namespace stitchui {

void openCollectionStitchBalloon(const std::vector<VGMColl*>& initialCollections,
                                 QWidget* parent,
                                 QWidget* anchor,
                                 QAbstractButton* toggleButton) {
  QWidget* owner = parent ? parent : QApplication::activeWindow();
  StitchExportBalloon* balloon = ensureStitchExportBalloon(owner);
  balloon->openForCollections(dedupeCollections(initialCollections), anchor ? anchor : owner, toggleButton);
}

bool toggleCollectionStitchBalloon(const std::vector<VGMColl*>& initialCollections,
                                   QWidget* parent,
                                   QWidget* anchor,
                                   QAbstractButton* toggleButton) {
  QWidget* owner = parent ? parent : QApplication::activeWindow();
  StitchExportBalloon* balloon = ensureStitchExportBalloon(owner);
  if (toggleButton) {
    balloon->setToggleButton(toggleButton);
  }

  if (balloon->isVisible()) {
    balloon->hide();
    return false;
  }

  balloon->openForCollections(dedupeCollections(initialCollections),
                              anchor ? anchor : owner,
                              toggleButton);
  return true;
}

}  // namespace stitchui

