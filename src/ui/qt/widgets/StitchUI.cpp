/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "widgets/StitchUI.h"

#include "application/WorkspaceController.h"
#include "ColorHelpers.h"
#include "models/ValueModels.h"
#include "util/UIHelpers.h"
#include "value/export/CollectionStitch.h"
#include "widgets/EmptyStateWidget.h"
#include "workarea/MdiArea.h"
#include "workarea/CollectionListView.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScreen>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

namespace {

constexpr int RoleCollectionId = Qt::UserRole;
constexpr QSize kQueueIconSize(16, 16);
constexpr QSize kActionIconSize(16, 16);
constexpr int kQueueRowHeight = 28;
constexpr int kBalloonMinWidth = 420;
constexpr int kBalloonMinHeight = 320;
constexpr int kAnchorVerticalGap = 6;

std::vector<vgmtrans::core::CollectionId> dedupeCollections(
    std::span<const vgmtrans::core::CollectionId> collections) {
  std::vector<vgmtrans::core::CollectionId> deduped;
  deduped.reserve(collections.size());
  std::unordered_set<u32> seen;

  for (const auto collection : collections) {
    if (collection.valid() && seen.insert(collection.value).second) {
      deduped.push_back(collection);
    }
  }

  return deduped;
}

bool collectionExists(const vgmtrans::ui::WorkspaceController& workspace, vgmtrans::core::CollectionId collection) {
  return workspace.snapshot().collection(collection) != nullptr;
}

std::string buildExportSuccessMessage(const std::filesystem::path& midiPath, const std::filesystem::path& sf2Path,
                                      const vgmtrans::core::CollectionStitchResult& result,
                                      const vgmtrans::core::SessionSnapshot& snapshot) {
  std::string message = "Stitched export created:\n";
  message += "MIDI: " + midiPath.string() + "\n";
  message += "SF2: " + sf2Path.string();

  if (result.parts.empty()) {
    return message;
  }

  message += "\n\nParts:\n";
  for (size_t i = 0; i < result.parts.size(); ++i) {
    const auto& part = result.parts[i];
    const auto* collection = snapshot.collection(part.collection);
    message += collection != nullptr ? collection->name : "(unknown collection)";
    message += " - tick ";
    message += std::to_string(part.startTick);
    message += ", banks ";
    for (size_t bank = 0; bank < part.banks.size(); ++bank) {
      if (bank != 0) {
        message += ", ";
      }
      message += std::to_string(part.banks[bank].source) + "->" + std::to_string(part.banks[bank].target);
    }
    if (i + 1 < result.parts.size()) {
      message += "\n";
    }
  }
  return message;
}

std::string diagnosticText(const vgmtrans::core::CollectionStitchResult& result) {
  std::string text;
  std::unordered_set<std::string> seen;
  const auto add = [&](const std::vector<vgmtrans::core::Diagnostic>& diagnostics) {
    for (const auto& diagnostic : diagnostics) {
      if (!diagnostic.message.empty() && seen.insert(diagnostic.message).second) {
        if (!text.empty()) {
          text += '\n';
        }
        text += diagnostic.message;
      }
    }
  };
  add(result.midi.diagnostics);
  add(result.soundFont.diagnostics);
  return text;
}

bool writeArtifact(const std::filesystem::path& path, const vgmtrans::core::Artifact& artifact, std::string& error) {
  QSaveFile file(QString::fromStdWString(path.wstring()));
  if (!file.open(QIODevice::WriteOnly)) {
    error = file.errorString().toStdString();
    return false;
  }
  const auto size = static_cast<qsizetype>(artifact.bytes.size());
  if (file.write(reinterpret_cast<const char*>(artifact.bytes.data()), size) != size || !file.commit()) {
    error = file.errorString().toStdString();
    return false;
  }
  return true;
}

bool exportStitchedCollections(vgmtrans::ui::WorkspaceController& workspace,
                               const vgmtrans::core::ExportRequest& request,
                               const std::vector<vgmtrans::core::CollectionId>& orderedCollections,
                               const stitchui::ShowToast& showToast) {
  if (orderedCollections.size() < 2) {
    showToast(QStringLiteral("Select at least two collections to stitch."), ToastType::Info, 3000);
    return false;
  }

  for (const auto id : orderedCollections) {
    const auto* collection = workspace.snapshot().collection(id);
    if (collection == nullptr || !collection->members.sequence) {
      showToast(QStringLiteral("Each selected collection must contain a sequence for stitched export."),
                ToastType::Error, 15000);
      return false;
    }
  }

  const auto* first = workspace.snapshot().collection(orderedCollections.front());
  QString suggestedFileName =
      QString::fromStdString(first != nullptr ? first->name : std::string("stitched-collections"));
  suggestedFileName.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")), QStringLiteral("_"));
  if (suggestedFileName.isEmpty()) {
    suggestedFileName = QStringLiteral("stitched-collections");
  }
  suggestedFileName += QStringLiteral(".mid");

  std::filesystem::path midiPath = openSaveFileDialog(std::filesystem::path(suggestedFileName.toStdWString()), "mid");
  if (midiPath.empty()) {
    return false;
  }
  if (!midiPath.has_extension()) {
    midiPath.replace_extension("mid");
  }

  auto sf2Path = midiPath;
  sf2Path.replace_extension("sf2");

  vgmtrans::core::CollectionStitchResult result;
  try {
    result = workspace.stitchCollections(orderedCollections, request);
  } catch (const std::exception& error) {
    showToast(QString::fromUtf8(error.what()), ToastType::Error, 15000);
    return false;
  }
  if (!result.complete()) {
    const std::string details = diagnosticText(result);
    showToast(QString::fromStdString(details.empty() ? "Failed to stitch selected collections." : details),
              ToastType::Error, 15000);
    return false;
  }

  std::string error;
  if (!writeArtifact(midiPath, result.midi, error) || !writeArtifact(sf2Path, result.soundFont, error)) {
    showToast(QString::fromStdString("Failed to write stitched export: " + error), ToastType::Error, 15000);
    return false;
  }

  showToast(QString::fromStdString(buildExportSuccessMessage(midiPath, sf2Path, result, workspace.snapshot())),
            ToastType::Success, 10000);
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

QString collectionLabel(const vgmtrans::ui::WorkspaceController& workspace,
                        vgmtrans::core::CollectionId collectionId) {
  const auto* collection = workspace.snapshot().collection(collectionId);
  QString label = collection != nullptr ? QString::fromStdString(collection->name)
                                        : QStringLiteral("(unknown collection)");
  if (collection != nullptr && collection->members.sequence) {
    const auto* sequence = workspace.snapshot().asset(*collection->members.sequence);
    label += QStringLiteral(" - ");
    label += sequence != nullptr ? QString::fromStdString(vgmtrans::core::metadata(*sequence).name)
                                 : QStringLiteral("(missing sequence)");
  }
  return label;
}

QListWidgetItem* makeCollectionItem(const vgmtrans::ui::WorkspaceController& workspace,
                                    vgmtrans::core::CollectionId collection) {
  auto* item = new QListWidgetItem(collectionLabel(workspace, collection));
  item->setData(RoleCollectionId, collection.value);
  item->setSizeHint(QSize(0, kQueueRowHeight));
  return item;
}

std::optional<vgmtrans::core::CollectionId> collectionFromItem(const QListWidgetItem* item) {
  return item != nullptr ? std::optional{vgmtrans::core::CollectionId{item->data(RoleCollectionId).toUInt()}}
                         : std::nullopt;
}

std::vector<vgmtrans::core::CollectionId> selectedCollections(const CollectionListView& list) {
  std::vector<vgmtrans::core::CollectionId> collections;
  if (list.selectionModel() == nullptr) {
    return collections;
  }
  for (const auto& index : list.selectionModel()->selectedRows()) {
    collections.push_back(vgmtrans::core::CollectionId{index.data(vgmtrans::ui::IdRole).toUInt()});
  }
  return collections;
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
  using QWidget::QWidget;

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
  using ExternalDropHandler = std::function<void(const std::vector<vgmtrans::core::CollectionId>&, int)>;
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
    if (qobject_cast<CollectionListView*>(event->source())) {
      event->acceptProposedAction();
      return;
    }
    QListWidget::dragEnterEvent(event);
  }

  void dragMoveEvent(QDragMoveEvent* event) override {
    if (qobject_cast<CollectionListView*>(event->source())) {
      event->acceptProposedAction();
      return;
    }
    QListWidget::dragMoveEvent(event);
  }

  void dropEvent(QDropEvent* event) override {
    if (auto* sourceList = qobject_cast<CollectionListView*>(event->source())) {
      if (m_externalDropHandler) {
        int insertionRow = count();
        const QModelIndex hoveredIndex = indexAt(event->position().toPoint());
        if (hoveredIndex.isValid()) {
          insertionRow = hoveredIndex.row();
          if (dropIndicatorPosition() == QAbstractItemView::BelowItem) {
            ++insertionRow;
          }
        }

        m_externalDropHandler(selectedCollections(*sourceList), insertionRow);
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
  StitchExportBalloon(vgmtrans::ui::WorkspaceController& workspace, QWidget* parent = nullptr)
      : QFrame(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint), workspace_(workspace) {
    setObjectName(QStringLiteral("stitchExportBalloon"));
    setFrameShape(QFrame::NoFrame);
    setFrameShadow(QFrame::Plain);
    const QColor borderColor = blendColors(
      palette().color(QPalette::Window),
      palette().color(QPalette::Text),
      0.88);
    setStyleSheet(QString(
        "QFrame#stitchExportBalloon {"
        "  background-color: palette(window);"
        "  border: 1px solid %1;"
        "}")
        .arg(cssColor(borderColor)));
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

    auto* heading = new QLabel(QStringLiteral("Collection Stitcher"), m_headingDragHandle);
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
                                     QStringLiteral("Drop collections here")),
        m_queueList, this);
    m_emptyState->setBodyText(
        QStringLiteral("Reorder, then export as joined MIDI + SF2"));
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

    m_queueList->setExternalDropHandler([this](const std::vector<vgmtrans::core::CollectionId>& collections, int row) {
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
      if (exportStitchedCollections(workspace_, request_, orderedCollections(), callbacks_.showToast)) {
        hide();
      }
    });
    connect(m_closeButton, &QToolButton::clicked, this, &QWidget::hide);

    connect(m_queueList, &QListWidget::itemSelectionChanged, this, [this]() { refreshUi(); });
    connect(m_queueList->model(), &QAbstractItemModel::rowsInserted, this, [this]() { refreshUi(); });
    connect(m_queueList->model(), &QAbstractItemModel::rowsRemoved, this, [this]() { refreshUi(); });
    connect(m_queueList->model(), &QAbstractItemModel::rowsMoved, this, [this]() { refreshUi(); });
    connect(&workspace_, &vgmtrans::ui::WorkspaceController::snapshotChanged, this,
            [this]() { pruneMissingQueueCollections(); });

    refreshUi();
  }

  void configure(vgmtrans::core::ExportRequest request, stitchui::Callbacks callbacks) {
    request_ = std::move(request);
    callbacks_ = std::move(callbacks);
  }

  void openForCollections(const std::vector<vgmtrans::core::CollectionId>& initialCollections, QWidget* anchor,
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
    if (callbacks_.visibilityChanged) {
      callbacks_.visibilityChanged(true);
    }
    QFrame::showEvent(event);
  }

  void hideEvent(QHideEvent* event) override {
    if (m_toggleButton) {
      m_toggleButton->setChecked(false);
    }
    if (callbacks_.visibilityChanged) {
      callbacks_.visibilityChanged(false);
    }
    if (callbacks_.planChanged) {
      callbacks_.planChanged(std::span<const vgmtrans::core::CollectionId>{});
    }
    QFrame::hideEvent(event);
  }

  bool containsCollection(vgmtrans::core::CollectionId collection) const {
    for (int row = 0; row < m_queueList->count(); ++row) {
      if (collectionFromItem(m_queueList->item(row)) == collection) {
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
      const auto collection = collectionFromItem(m_queueList->item(row));
      if (!collection || !collectionExists(workspace_, *collection)) {
        delete m_queueList->takeItem(row);
        removedAny = true;
      }
    }

    if (removedAny) {
      refreshUi();
    }
  }

  void addCollections(const std::vector<vgmtrans::core::CollectionId>& collections, int insertionRow) {
    if (collections.empty()) {
      return;
    }

    const auto deduped = dedupeCollections(collections);
    int targetRow = std::clamp(insertionRow, 0, m_queueList->count());

    for (const auto collection : deduped) {
      if (!collectionExists(workspace_, collection) || containsCollection(collection)) {
        continue;
      }

      m_queueList->insertItem(targetRow++, makeCollectionItem(workspace_, collection));
    }

    refreshUi();
  }

  std::vector<vgmtrans::core::CollectionId> orderedCollections() const {
    std::vector<vgmtrans::core::CollectionId> ordered;
    ordered.reserve(static_cast<size_t>(m_queueList->count()));

    for (int row = 0; row < m_queueList->count(); ++row) {
      const auto collection = collectionFromItem(m_queueList->item(row));
      if (collection && collectionExists(workspace_, *collection)) {
        ordered.push_back(*collection);
      }
    }

    return ordered;
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

    refreshButtonIcon(m_removeButton, QStringLiteral(":/icons/minus-circle-outline.svg"));
    refreshButtonIcon(m_clearButton, QStringLiteral(":/icons/close-circle.svg"));
    refreshButtonIcon(m_exportButton, QStringLiteral(":/icons/export.svg"));
    refreshStencilToolButton(m_closeButton, QStringLiteral(":/icons/toast_close.svg"), palette);
    if (callbacks_.planChanged) {
      const auto collections = orderedCollections();
      callbacks_.planChanged(collections);
    }
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
  vgmtrans::ui::WorkspaceController& workspace_;
  vgmtrans::core::ExportRequest request_;
  stitchui::Callbacks callbacks_;
};

QPointer<StitchExportBalloon> g_stitchExportBalloon;

StitchExportBalloon* ensureStitchExportBalloon(vgmtrans::ui::WorkspaceController& workspace, QWidget* owner) {
  StitchExportBalloon* existingBalloon = g_stitchExportBalloon.data();
  if (!existingBalloon || existingBalloon->parentWidget() != owner) {
    if (existingBalloon) {
      existingBalloon->deleteLater();
    }
    g_stitchExportBalloon = new StitchExportBalloon(workspace, owner);
  }
  return g_stitchExportBalloon.data();
}

}  // namespace

namespace stitchui {

void openCollectionStitchBalloon(vgmtrans::ui::WorkspaceController& workspace,
                                 std::span<const vgmtrans::core::CollectionId> initialCollections,
                                 const vgmtrans::core::ExportRequest& request, Callbacks callbacks, QWidget* parent,
                                 QWidget* anchor, QAbstractButton* toggleButton) {
  QWidget* owner = parent ? parent : QApplication::activeWindow();
  StitchExportBalloon* balloon = ensureStitchExportBalloon(workspace, owner);
  balloon->configure(request, std::move(callbacks));
  balloon->openForCollections(dedupeCollections(initialCollections), anchor ? anchor : owner, toggleButton);
}

bool toggleCollectionStitchBalloon(vgmtrans::ui::WorkspaceController& workspace,
                                   std::span<const vgmtrans::core::CollectionId> initialCollections,
                                   const vgmtrans::core::ExportRequest& request, Callbacks callbacks, QWidget* parent,
                                   QWidget* anchor, QAbstractButton* toggleButton) {
  QWidget* owner = parent ? parent : QApplication::activeWindow();
  StitchExportBalloon* balloon = ensureStitchExportBalloon(workspace, owner);
  balloon->configure(request, std::move(callbacks));
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
