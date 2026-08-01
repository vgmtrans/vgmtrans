/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include "MainWindow.h"

#include "About.h"
#include "Logger.h"
#include "MainWindowDockLayout.h"
#include "MenuBar.h"
#include "PlaybackControls.h"
#include "ReportDialog.h"
#include "SequencePlayer.h"
#include "application/WorkspaceController.h"
#include "models/ValueModels.h"
#include "services/Settings.h"
#include "StatusBarContent.h"
#include "TitleBar.h"
#include "util/ColorHelpers.h"
#include "util/UIHelpers.h"
#include <version.h>
#include "widgets/FixedHeightListDelegate.h"
#include "widgets/ItemViewDensity.h"
#include "widgets/TableView.h"
#include "widgets/ToastHost.h"
#include "widgets/WindowBar.h"
#include "workarea/CollectionListView.h"
#include "workarea/hexview/HexViewInput.h"
#include "workarea/MdiArea.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <limits>
#include <optional>
#include <variant>
#include <vector>

#include <QAction>
#include <QAbstractItemView>
#include <QApplication>
#include <QCloseEvent>
#if defined(Q_OS_LINUX)
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QVariantMap>
#endif
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QShortcut>
#include <QStringList>
#include <QSortFilterProxyModel>
#include <QStandardPaths>
#include <QStyleOptionViewItem>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWKWidgets/widgetwindowagent.h>

namespace {
constexpr auto MIME_PORTAL_FILETRANSFER = "application/vnd.portal.filetransfer";
constexpr int kCollectionTitleControlSpacing = 20;

bool isDockSeparatorCursor(Qt::CursorShape shape) {
  return shape == Qt::SplitHCursor || shape == Qt::SplitVCursor;
}

bool hasPortalFileTransferMime(const QMimeData* mimeData) {
  if (!mimeData) {
    return false;
  }

  for (const QString& format : mimeData->formats()) {
    if (format == MIME_PORTAL_FILETRANSFER || format.startsWith(MIME_PORTAL_FILETRANSFER)) {
      return true;
    }
  }
  return false;
}

bool hasLocalFileUrls(const QMimeData* mimeData) {
  if (!mimeData || !mimeData->hasUrls()) {
    return false;
  }

  const QList<QUrl> urls = mimeData->urls();
  for (const QUrl& url : urls) {
    if (url.isLocalFile()) {
      return true;
    }
  }
  return false;
}

bool isFileDropMime(const QMimeData* mimeData) {
  return hasPortalFileTransferMime(mimeData) || hasLocalFileUrls(mimeData);
}

QStringList retrievePortalDroppedFiles([[maybe_unused]] const QMimeData* mimeData) {
#if defined(VGMTRANS_HAVE_DBUS) && defined(Q_OS_LINUX)
  if (!mimeData) {
    return {};
  }

  QString portalMimeFormat;
  for (const QString& format : mimeData->formats()) {
    if (format == MIME_PORTAL_FILETRANSFER || format.startsWith(MIME_PORTAL_FILETRANSFER)) {
      portalMimeFormat = format;
      break;
    }
  }
  if (portalMimeFormat.isEmpty()) {
    return {};
  }

  QByteArray keyBytes = mimeData->data(portalMimeFormat);
  if (const qsizetype nulPos = keyBytes.indexOf('\0'); nulPos >= 0) {
    keyBytes.truncate(nulPos);
  }

  const QString key = QString::fromUtf8(keyBytes.trimmed());
  if (key.isEmpty()) {
    return {};
  }

  QDBusMessage msg = QDBusMessage::createMethodCall(
      QStringLiteral("org.freedesktop.portal.Documents"),
      QStringLiteral("/org/freedesktop/portal/documents"),
      QStringLiteral("org.freedesktop.portal.FileTransfer"),
      QStringLiteral("RetrieveFiles"));
  QVariantMap options;
  msg << key << options;

  QDBusReply<QStringList> reply = QDBusConnection::sessionBus().call(msg);
  if (!reply.isValid()) {
    return {};
  }

  return reply.value();
#else
  return {};
#endif
}

std::filesystem::path filePath(const QString& path) {
#ifdef Q_OS_WIN
  return std::filesystem::path(path.toStdWString());
#else
  const QByteArray utf8 = path.toUtf8();
  return std::filesystem::path(utf8.constData(), utf8.constData() + utf8.size());
#endif
}

QString pathText(const std::filesystem::path& path) {
#ifdef Q_OS_WIN
  return QString::fromStdWString(path.wstring());
#else
  const std::string native = path.string();
  return QString::fromUtf8(native.data(), static_cast<qsizetype>(native.size()));
#endif
}

std::filesystem::path safeFileName(QString name) {
  const QString invalid = QStringLiteral("<>:\"/\\|?*");
  for (qsizetype index = 0; index < name.size(); ++index) {
    if (name[index].unicode() < 32 || invalid.contains(name[index])) {
      name[index] = QLatin1Char('_');
    }
  }
  return filePath(name.isEmpty() ? QStringLiteral("unnamed") : name);
}

void configureTableView(TableView* view) {
  view->setIconSize(QSize(16, 16));
  view->setSelectionMode(QAbstractItemView::ExtendedSelection);
  view->setSelectionBehavior(QAbstractItemView::SelectRows);
}

class CollectionTreeDelegate final : public FixedHeightListDelegate {
public:
  explicit CollectionTreeDelegate(int itemHeight, QObject* parent = nullptr)
      : FixedHeightListDelegate(itemHeight, parent) {}

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override {
    QStyleOptionViewItem itemOption(option);
    initStyleOption(&itemOption, index);
    if (index.data(vgmtrans::ui::IsCollectionRole).toBool()) {
      QStyledItemDelegate::paint(painter, itemOption, index);
      return;
    }

    const int indent = itemOption.decorationSize.width() + 12;
    const QRect rowRect = itemOption.rect;
    itemOption.rect.adjust(indent, 0, 0, 0);
    QStyledItemDelegate::paint(painter, itemOption, index);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    QPen pen(option.palette.color(QPalette::Text), 1);
    pen.setCosmetic(true);
    painter->setPen(pen);
    const int branchX = rowRect.left() + indent / 2;
    const int centerY = rowRect.center().y();
    const int branchEndX = branchX +
        (itemOption.rect.left() + itemOption.decorationSize.width() / 2 - branchX) / 1.5;
    const int trunkEndY = index.data(vgmtrans::ui::IsLastItemRole).toBool()
                              ? centerY
                              : rowRect.bottom();
    painter->drawLine(branchX, rowRect.top(), branchX, trunkEndY);
    painter->drawLine(branchX, centerY, branchEndX, centerY);
    painter->restore();
  }
};

MenuBar::Context contextForAsset(vgmtrans::ui::WorkspaceController& workspace,
                                 const QModelIndex& index) {
  if (!index.isValid()) {
    return MenuBar::Context::None;
  }
  const auto* asset = workspace.snapshot().asset(
      vgmtrans::core::AssetId{index.data(vgmtrans::ui::IdRole).toUInt()});
  if (asset == nullptr) {
    return MenuBar::Context::None;
  }
  if (std::holds_alternative<vgmtrans::core::SequenceProgramAsset>(*asset)) {
    return MenuBar::Context::Sequence;
  }
  if (std::holds_alternative<vgmtrans::core::InstrumentSetAsset>(*asset)) {
    return MenuBar::Context::InstrumentSet;
  }
  if (std::holds_alternative<vgmtrans::core::SampleCollectionAsset>(*asset)) {
    return MenuBar::Context::SampleCollection;
  }
  return MenuBar::Context::Misc;
}

QModelIndex indexForId(const QAbstractItemView* view, u32 id, bool assetOnly) {
  if (view == nullptr || view->model() == nullptr) {
    return {};
  }
  for (int row = 0; row < view->model()->rowCount(); ++row) {
    const QModelIndex index = view->model()->index(row, 0);
    if (assetOnly && index.data(vgmtrans::ui::IsCollectionRole).toBool()) {
      continue;
    }
    if (index.data(vgmtrans::ui::IdRole).toUInt() != id) {
      continue;
    }
    return index;
  }
  return {};
}

bool selectIdInView(QAbstractItemView* view, u32 id, bool assetOnly,
                    bool clearWhenMissing) {
  if (view == nullptr || view->selectionModel() == nullptr) {
    return false;
  }
  const QModelIndex index = indexForId(view, id, assetOnly);
  if (index.isValid()) {
    const QSignalBlocker blocker(view->selectionModel());
    view->selectionModel()->setCurrentIndex(
        index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    view->scrollTo(index, QAbstractItemView::EnsureVisible);
    return true;
  }
  if (clearWhenMissing) {
    const QSignalBlocker blocker(view->selectionModel());
    view->selectionModel()->clearSelection();
  }
  return false;
}

QString diagnosticMessages(const std::vector<vgmtrans::core::Diagnostic>& diagnostics,
                           const QString& fallback) {
  QStringList messages;
  for (const auto& diagnostic : diagnostics) {
    const QString message = QString::fromStdString(diagnostic.message);
    if (!message.isEmpty() && !messages.contains(message)) {
      messages.push_back(message);
    }
  }
  return messages.isEmpty() ? fallback : messages.join(QLatin1Char('\n'));
}

void applySequenceRenderSettings(vgmtrans::core::SequenceRenderOptions& options) {
  options.sequenceLoops = static_cast<u32>(Settings::the()->conversion.numSequenceLoops());
  options.midi.skipChannel10 = Settings::the()->conversion.skipChannel10();
  options.midi.bankSelectStyle = Settings::the()->conversion.bankSelectStyle() == BankSelectStyle::MMA
                                     ? vgmtrans::core::MidiBankSelectStyle::MsbAndLsb
                                     : vgmtrans::core::MidiBankSelectStyle::MsbOnly;
}

void applyMidiExportSettings(vgmtrans::core::SequenceRenderOptions& options) {
  applySequenceRenderSettings(options);
  options.midi.pitchTransitions = Settings::the()->conversion.pitchTransitionRendering();
}

void applyCollectionExportSettings(vgmtrans::core::ExportRequest& request) {
  applyMidiExportSettings(request.sequence);
  request.modulationConversion = Settings::the()->conversion.modulationConversion();
  request.dynamicEnvelopes = Settings::the()->conversion.dynamicEnvelopeConversion();
  request.exportOnlyUsedInstruments = Settings::the()->conversion.exportOnlyUsedInstruments();
}
}  // namespace

MainWindow::MainWindow(vgmtrans::ui::WorkspaceController& workspace)
    : QMainWindow(nullptr), m_workspace(workspace) {
  setWindowTitle("VGMTrans");
  setWindowIcon(QIcon(":/vgmtrans.png"));
  setAttribute(Qt::WA_DontCreateNativeAncestors);
  setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea, false);
  setAcceptDrops(true);
  setContextMenuPolicy(Qt::NoContextMenu);
  setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks);

  m_windowAgent = new QWK::WidgetWindowAgent(this);
  m_windowAgent->setup(this);

  createElements();
  m_dockLayout = new MainWindowDockLayout(this,
                                          {
                                              .rawFiles = m_rawfile_dock,
                                              .vgmFiles = m_vgmfile_dock,
                                              .collections = m_coll_dock,
                                              .collectionContents = m_coll_view_dock,
                                              .logs = m_logger,
                                              .collectionListView = m_coll_listview,
                                          });
  m_dockLayout->restoreWindowGeometry();
  configureWindowAgent();
  routeSignals();
  qApp->installEventFilter(this);

  m_dragOverlay = new QWidget(this);
  m_dragOverlay->setObjectName(QStringLiteral("dragOverlay"));
  m_dragOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
  m_dragOverlay->setAcceptDrops(false);
  m_dragOverlay->hide();
  updateDragOverlayAppearance();
  updateDragOverlayGeometry();

  qInfo("Running %s (%s, %s), Qt %s", VGMTRANS_VERSION, VGMTRANS_REVISION,
        VGMTRANS_BRANCH, qVersion());
}

void MainWindow::createElements() {
  setDocumentMode(true);
  setTabPosition(Qt::BottomDockWidgetArea, QTabWidget::North);
  setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
  setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
  setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);

  const auto installTitleBar = [](QDockWidget *dock, const QString& title,
                                      TitleBar::Buttons buttons,
                                      const QString& newToolTip = QString()) {
    auto *titleBar = new TitleBar(title, buttons, dock, newToolTip);
    connect(titleBar, &TitleBar::hideRequested, dock, &QDockWidget::hide);
    dock->setTitleBarWidget(titleBar);
    return titleBar;
  };

  m_rawfile_dock = new QDockWidget("Scanned Files");
  m_rawfile_dock->setObjectName(QStringLiteral("rawFileListDock"));
  m_rawfile_dock->setAllowedAreas(Qt::LeftDockWidgetArea);
  m_rawfile_listview = new TableView();
  configureTableView(m_rawfile_listview);
  m_rawfile_listview->setModel(new vgmtrans::ui::SourceTableModel(m_workspace, m_rawfile_listview));
  m_rawfile_dock->setWidget(m_rawfile_listview);
  m_rawfile_dock->setContentsMargins(0, 0, 0, 0);
  installTitleBar(m_rawfile_dock, "Scanned Files", TitleBar::HideButton);

  m_vgmfile_dock = new QDockWidget("Detected Files");
  m_vgmfile_dock->setObjectName(QStringLiteral("vgmFileListDock"));
  m_vgmfile_dock->setAllowedAreas(Qt::LeftDockWidgetArea);
  m_vgmfile_listview = new TableView();
  configureTableView(m_vgmfile_listview);
  m_vgmfile_listview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  m_vgmfile_listview->setModel(new vgmtrans::ui::AssetTableModel(m_workspace, m_vgmfile_listview));
  m_vgmfile_dock->setWidget(m_vgmfile_listview);
  m_vgmfile_dock->setContentsMargins(0, 0, 0, 0);
  installTitleBar(m_vgmfile_dock, "Detected Files", TitleBar::HideButton);

  m_coll_listview = new CollectionListView();
  m_collection_model = new vgmtrans::ui::CollectionTableModel(m_workspace, m_coll_listview);
  m_collection_filter =
      new vgmtrans::ui::CollectionFilterProxyModel(m_workspace, m_coll_listview);
  m_collection_filter->setSourceModel(m_collection_model);
  m_coll_listview->setModel(m_collection_filter);

  m_coll_view = new QListView();
  m_coll_view->setAttribute(Qt::WA_MacShowFocusRect, false);
  m_coll_view->setIconSize(QSize(16, 16));
  ItemViewDensity::apply(m_coll_view);
  m_coll_view->setSpacing(0);
  m_coll_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_collection_contents_model =
      new vgmtrans::ui::CollectionContentsModel(m_workspace, m_coll_view);
  m_coll_view->setModel(m_collection_contents_model);
  m_coll_view->setItemDelegate(new CollectionTreeDelegate(
      ItemViewDensity::listItemHeight(m_coll_view) + ItemViewDensity::listSpacing(m_coll_view),
      m_coll_view));
  m_playback_controls = new PlaybackControls();
  m_sequence_player = new SequencePlayer(this);

  auto *central_wrapper = new QWidget(this);
  auto *central_layout = new QVBoxLayout();
  central_layout->setContentsMargins(0, 0, 0, 0);
  central_layout->setSpacing(0);
  MdiArea::the()->setWorkspace(&m_workspace);
  central_layout->addWidget(MdiArea::the(), 1);
  central_wrapper->setLayout(central_layout);
  setCentralWidget(central_wrapper);

  m_coll_dock = new QDockWidget("Collections");
  m_coll_dock->setObjectName(QStringLiteral("collectionListDock"));
  m_coll_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::BottomDockWidgetArea);
  m_coll_dock->setWidget(m_coll_listview);
  m_coll_dock->setContentsMargins(0, 0, 0, 0);
  addDockWidget(Qt::BottomDockWidgetArea, m_coll_dock);
  TitleBar *collTitleBar = installTitleBar(
      m_coll_dock, "Collections", TitleBar::HideButton | TitleBar::NewButton,
      QStringLiteral("New collection"));
  connect(collTitleBar, &TitleBar::newRequested, this,
          &MainWindow::manualCollectionRequested);

  auto* collLeadingControls = new QWidget(collTitleBar);
  auto* collLeadingLayout = new QHBoxLayout(collLeadingControls);
  collLeadingLayout->setContentsMargins(0, 0, 0, 0);
  collLeadingLayout->setSpacing(kCollectionTitleControlSpacing);

  m_stitchButton = new QToolButton(collLeadingControls);
  configureToolButton(m_stitchButton, QStringLiteral("Stitch collections"),
                      QSize(22, 20), QSize(16, 16));
  m_stitchButton->setCheckable(true);
  m_stitchButton->setChecked(false);

  auto* collSearchEdit = new QLineEdit(collLeadingControls);
  collSearchEdit->setPlaceholderText(QStringLiteral("Search"));
  collSearchEdit->setClearButtonEnabled(true);
  collSearchEdit->setFixedWidth(180);
  collSearchEdit->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  QFont collSearchFont = collSearchEdit->font();
  collSearchFont.setPointSizeF(collSearchFont.pointSizeF() - 1.0);
  collSearchEdit->setFont(collSearchFont);
#ifdef Q_OS_MAC
  collSearchEdit->setAttribute(Qt::WA_MacShowFocusRect, false);
#endif
  const int searchControlHeight = m_stitchButton->height();
  collSearchEdit->setFixedHeight(searchControlHeight);
  QAction* collSearchIconAction =
      collSearchEdit->addAction(QIcon(), QLineEdit::LeadingPosition);

  collLeadingLayout->addWidget(m_stitchButton);
  collLeadingLayout->addWidget(collSearchEdit);
  collTitleBar->addLeadingWidget(collLeadingControls);

  const auto refreshCollectionTitleControls =
      [collTitleBar, this, collSearchEdit, collSearchIconAction]() {
    const QPalette titleBarPalette = collTitleBar->palette();
    const QColor titleBarBackground = titleBarPalette.color(QPalette::Window);
    const QColor borderColor = blendColors(titleBarPalette.color(QPalette::Text), titleBarBackground, 0.08);
    const QColor focusBorderColor = blendColors(titleBarPalette.color(QPalette::Highlight), titleBarBackground, 0.84);
    const QColor focusBackground = blendColors(titleBarBackground, titleBarPalette.color(QPalette::Text), 0.96);
    collSearchEdit->setStyleSheet(QStringLiteral(
        "QLineEdit {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 5px;"
        "  padding: 0px 6px 0px 0px;"
        "}"
        "QLineEdit:focus {"
        "  border: 2px solid %3;"
        "  background-color: %4;"
        "}")
                                      .arg(cssColor(titleBarBackground))
                                      .arg(cssColor(borderColor))
                                      .arg(cssColor(focusBorderColor))
                                      .arg(cssColor(focusBackground)));
    refreshStencilToolButton(m_stitchButton, QStringLiteral(":/icons/stitch.svg"),
                             titleBarPalette, true);
    collSearchIconAction->setIcon(stencilSvgIcon(QStringLiteral(":/icons/magnify.svg"),
                                                 toolBarButtonIconColor(titleBarPalette)));
  };
  refreshCollectionTitleControls();
  connect(collTitleBar, &TitleBar::appearanceChanged, this, refreshCollectionTitleControls);
  connect(collSearchEdit, &QLineEdit::textChanged, m_coll_listview,
          &CollectionListView::setFilterText);
  connect(m_stitchButton, &QToolButton::clicked, this,
          &MainWindow::collectionStitchRequested);
  m_stitchButton->setEnabled(false);

  m_coll_view_dock = new QDockWidget("Collection Contents");
  m_coll_view_dock->setObjectName(QStringLiteral("collectionContentDock"));
  m_coll_view_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::BottomDockWidgetArea);
  m_coll_view_dock->setWidget(m_coll_view);
  m_coll_view_dock->setContentsMargins(0, 0, 0, 0);
  installTitleBar(m_coll_view_dock, "Collection Contents", TitleBar::HideButton);

  addDockWidget(Qt::LeftDockWidgetArea, m_rawfile_dock);
  splitDockWidget(m_rawfile_dock, m_vgmfile_dock, Qt::Orientation::Vertical);
  m_vgmfile_dock->setFocus();

  m_logger = new Logger(m_workspace);
  m_logger->setObjectName(QStringLiteral("loggerDock"));
  m_logger->setWindowTitle("Logs");
  m_logger->setAllowedAreas(Qt::BottomDockWidgetArea);
  m_logger->setContentsMargins(0, 0, 0, 0);
  TitleBar* loggerTitleBar = installTitleBar(m_logger, "Logs", TitleBar::HideButton);
  m_logger->installTitleBarControls(loggerTitleBar);

  addDockWidget(Qt::BottomDockWidgetArea, m_coll_view_dock);
  // Keep the bottom docks in a side-by-side layout so each dock preserves its own width.
  splitDockWidget(m_coll_view_dock, m_coll_dock, Qt::Horizontal);
  splitDockWidget(m_coll_dock, m_logger, Qt::Horizontal);

  const QList<QDockWidget *> viewMenuDocks{
      m_vgmfile_dock, m_coll_dock, m_coll_view_dock, m_rawfile_dock, m_logger,
  };
  m_windowBar = new WindowBar(this);

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  m_menu_bar = new MenuBar(nullptr, viewMenuDocks);
  m_menu_bar->setNativeMenuBar(true);
#else
  m_menu_bar = new MenuBar(nullptr, viewMenuDocks);
  m_menu_bar->setNativeMenuBar(false);
  m_windowBar->setMenuBarWidget(m_menu_bar);
#endif
  m_menu_bar->setShortcutHost(this);
  m_windowBar->setCenterWidget(m_playback_controls);
  m_windowBar->setDockToggleButtons({
      {m_vgmfile_dock->toggleViewAction(), QStringLiteral(":/icons/music-box-outline.svg")},
      {m_coll_dock->toggleViewAction(), QStringLiteral(":/icons/music-box-multiple-outline.svg")},
      {m_coll_view_dock->toggleViewAction(), QStringLiteral(":/icons/package-variant.svg")},
      {m_rawfile_dock->toggleViewAction(), QStringLiteral(":/icons/file-search-outline.svg")},
      {m_logger->toggleViewAction(), QStringLiteral(":/icons/book-open-variant-outline.svg")},
  });
  createStatusBar();
  m_toastHost = new ToastHost(this, MdiArea::the(), ToastHost::defaultMode());
}

void MainWindow::configureWindowAgent() {
  m_windowAgent->setTitleBar(m_windowBar);
  m_windowAgent->setHitTestVisible(m_windowBar->dockControls(), true);
  if (QWidget *menuBarWidget = m_windowBar->menuBarWidget()) {
    m_windowAgent->setHitTestVisible(menuBarWidget, true);
  }
  if (QWidget *centerWidget = m_windowBar->centerWidget()) {
    for (QWidget *child : centerWidget->findChildren<QWidget *>(Qt::FindDirectChildrenOnly)) {
      m_windowAgent->setHitTestVisible(child, true);
    }
  }

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  m_windowAgent->setWindowAttribute(QStringLiteral("no-system-buttons"), false);
  setMenuWidget(m_windowBar);
  m_windowAgent->setSystemButtonArea(m_windowBar->systemButtonArea());
#else
  m_windowAgent->setSystemButton(QWK::WindowAgentBase::WindowIcon, m_windowBar->windowIconButton());
  m_windowAgent->setSystemButton(QWK::WindowAgentBase::Minimize, m_windowBar->minimizeButton());
  m_windowAgent->setSystemButton(QWK::WindowAgentBase::Maximize, m_windowBar->maximizeButton());
  m_windowAgent->setSystemButton(QWK::WindowAgentBase::Close, m_windowBar->closeButton());
  setMenuWidget(m_windowBar);
#endif
}

void MainWindow::createStatusBar() {
  statusBarContent = new StatusBarContent;
  statusBar()->setSizeGripEnabled(false);
  statusBar()->setMaximumHeight(statusBarContent->maximumHeight());
  statusBar()->addPermanentWidget(statusBarContent, 1);
}

void MainWindow::showEvent(QShowEvent* event) {
  QMainWindow::showEvent(event);
  m_dockLayout->initializeAfterFirstShow();

  updateDragOverlayGeometry();

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  QTimer::singleShot(0, this, [this]() {
    // QMainWindow repositions the menu widget during startup, so rebind the
    // traffic-light anchor once after show to pick up the final rect.
    m_windowAgent->setSystemButtonArea(nullptr);
    m_windowAgent->setSystemButtonArea(m_windowBar->systemButtonArea());
  });
#endif
}

void MainWindow::removeSelectedSources() {
  const QModelIndexList rows = m_rawfile_listview->selectionModel()->selectedRows();
  std::vector<vgmtrans::core::SourceId> sources;
  sources.reserve(static_cast<size_t>(rows.size()));
  for (const QModelIndex& row : rows) {
    sources.push_back(vgmtrans::core::SourceId{
        row.data(vgmtrans::ui::IdRole).toUInt()});
  }
  static_cast<void>(m_workspace.removeSources(sources));
}

void MainWindow::removeSelectedAssets() {
  const QModelIndexList rows = m_vgmfile_listview->selectionModel()->selectedRows();
  std::vector<vgmtrans::core::AssetId> assets;
  assets.reserve(static_cast<size_t>(rows.size()));
  for (const QModelIndex& row : rows) {
    assets.push_back(vgmtrans::core::AssetId{
        row.data(vgmtrans::ui::IdRole).toUInt()});
  }
  static_cast<void>(m_workspace.removeAssets(assets));
}

void MainWindow::saveOriginal(QAbstractItemView* view, OriginalItemKind kind) {
  if (view == nullptr || view->selectionModel() == nullptr) {
    return;
  }
  const QModelIndexList rows = view->selectionModel()->selectedRows();
  if (rows.isEmpty()) {
    return;
  }

  const bool single = rows.size() == 1;
  const std::filesystem::path destination =
      single ? openSaveFileDialog(safeFileName(rows.front().data(Qt::DisplayRole).toString()), "")
             : openSaveDirDialog();
  if (destination.empty()) {
    return;
  }

  QStringList errors;
  size_t written = 0;
  for (const QModelIndex& row : rows) {
    const u32 id = row.data(vgmtrans::ui::IdRole).toUInt();
    const auto inspection = kind == OriginalItemKind::Asset
                                ? m_workspace.inspect(vgmtrans::core::AssetId{id})
                                : nullptr;
    if (kind == OriginalItemKind::Asset && inspection == nullptr) {
      errors.push_back(tr("%1 could not be inspected.").arg(row.data(Qt::DisplayRole).toString()));
      continue;
    }
    const std::span<const u8> bytes = inspection != nullptr
                                         ? inspection->bytes()
                                         : m_workspace.sourceBytes(vgmtrans::core::SourceId{id});
    const std::filesystem::path path =
        single ? destination : destination / safeFileName(row.data(Qt::DisplayRole).toString());
    QSaveFile file(pathText(path));
    const auto size = static_cast<qsizetype>(bytes.size());
    if (!file.open(QIODevice::WriteOnly) ||
        (!bytes.empty() && file.write(reinterpret_cast<const char*>(bytes.data()), size) != size) ||
        !file.commit()) {
      errors.push_back(file.errorString());
      continue;
    }
    ++written;
  }

  const QString title = rows.size() == 1
                            ? rows.front().data(Qt::DisplayRole).toString()
                            : tr("%1 items").arg(rows.size());
  statusBarContent->setStatus(title, tr("Wrote %1 file(s)").arg(static_cast<qulonglong>(written)));
  if (!errors.isEmpty()) {
    showToast(errors.join(QLatin1Char('\n')), ToastType::Error, 15000);
  }
}

void MainWindow::saveArtifact(const QModelIndex& index, vgmtrans::core::Artifact artifact,
                              const QString& failureMessage, const char* extension) {
  const QString title = index.data(Qt::DisplayRole).toString();
  if (artifact.bytes.empty()) {
    const QString message = diagnosticMessages(artifact.diagnostics, failureMessage);
    statusBarContent->setStatus(title, message);
    showToast(message, ToastType::Error, 15000);
    return;
  }

  const std::filesystem::path path = openSaveFileDialog(artifact.filename, extension);
  if (path.empty()) {
    return;
  }

  QSaveFile file(pathText(path));
  const auto size = static_cast<qsizetype>(artifact.bytes.size());
  if (!file.open(QIODevice::WriteOnly) ||
      file.write(reinterpret_cast<const char*>(artifact.bytes.data()), size) != size ||
      !file.commit()) {
    const QString message = file.errorString();
    statusBarContent->setStatus(title, message);
    showToast(message, ToastType::Error, 15000);
    return;
  }
  statusBarContent->setStatus(title, tr("Wrote %1").arg(pathText(path)));
}

QAbstractItemView* MainWindow::activeAssetView() const {
  const QWidget* focused = QApplication::focusWidget();
  return focused != nullptr && (focused == m_coll_view || m_coll_view->isAncestorOf(focused))
             ? static_cast<QAbstractItemView*>(m_coll_view)
             : static_cast<QAbstractItemView*>(m_vgmfile_listview);
}

void MainWindow::exportSequenceMidi(const QModelIndex& index) {
  if (!index.isValid()) {
    return;
  }

  vgmtrans::core::SequenceExportRequest request;
  applyMidiExportSettings(request);

  try {
    saveArtifact(index,
                 m_workspace.exportSequenceMidi(
                     vgmtrans::core::AssetId{index.data(vgmtrans::ui::IdRole).toUInt()}, request),
                 tr("The sequence could not be exported as MIDI."), "mid");
  } catch (const std::exception& error) {
    const QString message = QString::fromUtf8(error.what());
    statusBarContent->setStatus(index.data(Qt::DisplayRole).toString(), message);
    showToast(message, ToastType::Error, 15000);
  }
}

void MainWindow::exportInstrumentSet(const QModelIndex& index, vgmtrans::core::SynthExportFormat format) {
  if (!index.isValid()) {
    return;
  }

  const auto instrumentSet =
      vgmtrans::core::AssetId{index.data(vgmtrans::ui::IdRole).toUInt()};
  vgmtrans::core::ExportRequest request;
  const bool fromDetectedFiles = index.model() == m_vgmfile_listview->model();
  if (fromDetectedFiles) {
    applyCollectionExportSettings(request);
  } else {
    applySequenceRenderSettings(request.sequence);
  }
  const bool ambiguousCollection =
      fromDetectedFiles && request.exportOnlyUsedInstruments &&
      m_workspace.snapshot().countCollectionsContaining(instrumentSet) > 1;
  const bool soundFont = format == vgmtrans::core::SynthExportFormat::SoundFont2;
  try {
    auto artifact = m_workspace.exportInstrumentSet(instrumentSet, format, request);
    const bool exportable = !artifact.bytes.empty();
    saveArtifact(index, std::move(artifact),
                 soundFont ? tr("The instrument set could not be exported as SF2.")
                           : tr("The instrument set could not be exported as DLS."),
                 soundFont ? "sf2" : "dls");
    if (ambiguousCollection && exportable) {
      showToast(tr("No instrument data was excluded because this instrument set is used by multiple collections."
                   "\n\nExport directly from a collection to resolve this ambiguity."),
                ToastType::Info, 10000);
    }
  } catch (const std::exception& error) {
    const QString message = QString::fromUtf8(error.what());
    statusBarContent->setStatus(index.data(Qt::DisplayRole).toString(), message);
    showToast(message, ToastType::Error, 10000);
  }
}

void MainWindow::togglePlayback() {
  if (!m_playback_controls->hasPlayableTarget()) {
    m_playback_controls->showPlayInfo();
    return;
  }

  const QModelIndex current = m_coll_listview->currentIndex();
  if (!current.isValid()) {
    m_sequence_player->toggle();
    return;
  }

  const auto collection = vgmtrans::core::CollectionId{
      current.data(vgmtrans::ui::IdRole).toUInt()};
  if (m_sequence_player->activeCollection() == collection) {
    m_sequence_player->toggle();
    return;
  }

  vgmtrans::core::SequenceRenderOptions options;
  applySequenceRenderSettings(options);
  auto request = m_sequence_player->playbackRequest(std::move(options));
  request.dynamicEnvelopes = Settings::the()->conversion.dynamicEnvelopeConversion();

  try {
    auto playback = m_workspace.preparePlayback(collection, request);
    if (!playback.playable()) {
      const QString message = diagnosticMessages(
          playback.diagnostics, tr("The collection could not be prepared for playback."));
      statusBarContent->setStatus(current.data(Qt::DisplayRole).toString(), message);
      showToast(message, ToastType::Error, 15000);
      return;
    }
    if (m_sequence_player->load(std::move(playback))) {
      MdiArea::the()->setPlaybackSequence(m_sequence_player->activeSequence(),
                                          m_sequence_player->activeSourceSpans());
    }
  } catch (const std::exception& error) {
    const QString message = QString::fromUtf8(error.what());
    statusBarContent->setStatus(current.data(Qt::DisplayRole).toString(), message);
    showToast(message, ToastType::Error, 15000);
  }
}

void MainWindow::routeSignals() {
  auto* mdiArea = MdiArea::the();
  connect(m_menu_bar, &MenuBar::openFile, this, &MainWindow::openFile);
  connect(m_menu_bar, &MenuBar::openRecentFile, this, &MainWindow::openFileInternal);
  connect(m_menu_bar, &MenuBar::exit, this, &MainWindow::close);
  connect(m_menu_bar, &MenuBar::showAbout, [this]() {
    About about(this);
    about.exec();
  });
  connect(m_menu_bar, &MenuBar::reportBugRequested, this, [this]() {
    auto* dialog = new ReportDialog(m_workspace, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
  });
  connect(m_menu_bar, &MenuBar::showToastRequested, this,
          &MainWindow::showToast);
  connect(m_menu_bar, &MenuBar::resetDockLayout, m_dockLayout, &MainWindowDockLayout::resetToDefault);
  connect(m_menu_bar, &MenuBar::increaseHexFontRequested, mdiArea,
          &MdiArea::increaseActiveHexFont);
  connect(m_menu_bar, &MenuBar::decreaseHexFontRequested, mdiArea,
          &MdiArea::decreaseActiveHexFont);
  connect(m_menu_bar, &MenuBar::resetHexFontRequested, mdiArea,
          &MdiArea::resetActiveHexFont);
  connect(mdiArea, &MdiArea::hexViewAvailableChanged, m_menu_bar,
          &MenuBar::setHexViewAvailable);
  connect(this, &MainWindow::seekModifierActiveChanged, mdiArea,
          &MdiArea::setSeekModifierActive);
  connect(mdiArea, &MdiArea::playbackSeekRequested,
          m_sequence_player, &SequencePlayer::seek);
  connect(m_sequence_player, &SequencePlayer::stateChanged,
          m_playback_controls, &PlaybackControls::setPlaybackState);
  connect(m_sequence_player, &SequencePlayer::stateChanged, m_collection_model,
          [this](bool playing, bool hasActiveCollection) {
            m_collection_model->setPlayingCollection(
                playing && hasActiveCollection
                    ? std::optional{m_sequence_player->activeCollection()}
                    : std::nullopt);
          });
  connect(m_sequence_player, &SequencePlayer::stateChanged, mdiArea,
          [mdiArea](bool, bool hasActiveCollection) {
            if (!hasActiveCollection) {
              mdiArea->clearPlayback();
            }
          });
  connect(m_sequence_player, &SequencePlayer::positionChanged,
          m_playback_controls, &PlaybackControls::setPlaybackPosition);
  connect(m_sequence_player, &SequencePlayer::positionChanged,
          mdiArea, &MdiArea::setPlaybackPosition);
  connect(m_sequence_player, &SequencePlayer::errorOccurred, this,
          [this](const QString& message) {
            showToast(message, ToastType::Error, 15000);
          });
  connect(mdiArea, &MdiArea::inspectorStatusChanged, this,
          [this](const QString& name, const CapsuleText& description, const QIcon& icon,
                 int offset, int size) {
            statusBarContent->setInspectorStatus(
                name, description, icon.isNull() ? nullptr : &icon, offset, size);
          });

  const auto synchronizeAssetSelection =
      [this](vgmtrans::core::AssetId assetId, QWidget* caller) {
    const auto& snapshot = m_workspace.snapshot();
    const auto* asset = snapshot.asset(assetId);
    if (asset == nullptr) {
      return;
    }

    if (std::holds_alternative<vgmtrans::core::SequenceProgramAsset>(*asset)) {
      if (const auto* collection = snapshot.firstCollectionContaining(assetId)) {
        const QModelIndex index = indexForId(m_coll_listview, collection->id.value, false);
        if (index.isValid()) {
          m_coll_listview->selectionModel()->setCurrentIndex(
              index, QItemSelectionModel::ClearAndSelect);
          m_coll_listview->scrollTo(index, QAbstractItemView::EnsureVisible);
        } else {
          m_coll_listview->selectionModel()->clearSelection();
          m_coll_listview->selectionModel()->setCurrentIndex({}, QItemSelectionModel::NoUpdate);
        }
      }
    }

    if (caller != m_vgmfile_listview) {
      selectIdInView(m_vgmfile_listview, assetId.value, false, false);
    }
    if (caller != m_coll_view) {
      selectIdInView(m_coll_view, assetId.value, true, true);
    }

    vgmtrans::core::SourceId source = vgmtrans::core::metadata(*asset).range.source;
    while (const auto* value = snapshot.source(source)) {
      if (!value->parent) {
        break;
      }
      source = *value->parent;
    }
    if (caller != m_rawfile_listview && source.valid()) {
      selectIdInView(m_rawfile_listview, source.value, false, false);
    }
  };

  connect(m_playback_controls, &PlaybackControls::playToggle, this,
          &MainWindow::togglePlayback);
  connect(m_playback_controls, &PlaybackControls::stopPressed,
          m_sequence_player, &SequencePlayer::stop);
  connect(m_playback_controls, &PlaybackControls::seekingTo,
          m_sequence_player, &SequencePlayer::seek);
  auto* playShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
  playShortcut->setContext(Qt::WindowShortcut);
  connect(playShortcut, &QShortcut::activated, this, &MainWindow::togglePlayback);

  for (const QKeySequence& key : {QKeySequence(Qt::Key_Return),
                                  QKeySequence(Qt::Key_Enter)}) {
    auto* selectionPlayShortcut = new QShortcut(key, m_coll_listview);
    selectionPlayShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(selectionPlayShortcut, &QShortcut::activated, this,
            &MainWindow::togglePlayback);
  }
  auto* selectionStopShortcut =
      new QShortcut(QKeySequence(Qt::Key_Escape), m_coll_listview);
  selectionStopShortcut->setContext(Qt::WidgetWithChildrenShortcut);
  connect(selectionStopShortcut, &QShortcut::activated,
          m_sequence_player, &SequencePlayer::stop);

  const auto closeSelectedSources = [this] { removeSelectedSources(); };
  const auto removeSelectedAssets = [this] { this->removeSelectedAssets(); };
  connect(m_menu_bar, &MenuBar::closeSelectedSources, this, closeSelectedSources);
  connect(m_menu_bar, &MenuBar::removeSelectedAssets, this, removeSelectedAssets);
  connect(m_menu_bar, &MenuBar::saveSelectedSourceOriginal, this,
          [this] { saveOriginal(m_rawfile_listview, OriginalItemKind::Source); });
  connect(m_menu_bar, &MenuBar::saveSelectedAssetOriginal, this, [this] {
    saveOriginal(activeAssetView(), OriginalItemKind::Asset);
  });

  const auto exportSelectedCollection = [this](int choice) {
    const QModelIndex current = m_coll_listview->currentIndex();
    if (!current.isValid()) {
      return;
    }

    vgmtrans::core::ExportRequest request;
    if (choice == 0) {
      request.kinds = {vgmtrans::core::ExportKind::Midi,
                       vgmtrans::core::ExportKind::SoundFont2};
    } else if (choice == 1) {
      request.kinds = {vgmtrans::core::ExportKind::Midi,
                       vgmtrans::core::ExportKind::Dls};
    } else if (choice == 2) {
      request.kinds = {vgmtrans::core::ExportKind::Midi,
                       vgmtrans::core::ExportKind::SoundFont2,
                       vgmtrans::core::ExportKind::Dls};
    } else {
      return;
    }

    const std::filesystem::path directory = openSaveDirDialog();
    if (directory.empty()) {
      return;
    }
    applyCollectionExportSettings(request);

    const auto collection = vgmtrans::core::CollectionId{
        current.data(vgmtrans::ui::IdRole).toUInt()};
    try {
      const auto artifacts = m_workspace.exportCollection(collection, request);
      size_t written = 0;
      for (const auto& artifact : artifacts) {
        if (artifact.bytes.empty()) {
          continue;
        }
        QSaveFile file(pathText(directory / artifact.filename));
        if (!file.open(QIODevice::WriteOnly)) {
          continue;
        }
        const auto size = static_cast<qsizetype>(artifact.bytes.size());
        if (file.write(reinterpret_cast<const char*>(artifact.bytes.data()), size) == size &&
            file.commit()) {
          ++written;
        }
      }
      statusBarContent->setStatus(
          current.data(Qt::DisplayRole).toString(),
          tr("Wrote %1 file(s)").arg(static_cast<qulonglong>(written)));
    } catch (const std::exception& error) {
      statusBarContent->setStatus(current.data(Qt::DisplayRole).toString(),
                                  QString::fromUtf8(error.what()));
    }
  };
  connect(m_menu_bar, &MenuBar::exportSelectedCollection, this,
          exportSelectedCollection);
  connect(m_menu_bar, &MenuBar::exportSelectedSequenceMidi, this, [this] {
    exportSequenceMidi(activeAssetView()->currentIndex());
  });
  connect(m_menu_bar, &MenuBar::exportSelectedInstrumentSetSf2, this, [this] {
    exportInstrumentSet(activeAssetView()->currentIndex(),
                        vgmtrans::core::SynthExportFormat::SoundFont2);
  });
  connect(m_menu_bar, &MenuBar::exportSelectedInstrumentSetDls, this, [this] {
    exportInstrumentSet(activeAssetView()->currentIndex(), vgmtrans::core::SynthExportFormat::Dls);
  });
  connect(m_menu_bar, &MenuBar::openSelectedAsset, this, [this] {
    const QModelIndex current = activeAssetView()->currentIndex();
    if (current.isValid() && !current.data(vgmtrans::ui::IsCollectionRole).toBool()) {
      MdiArea::the()->newView(
          vgmtrans::core::AssetId{current.data(vgmtrans::ui::IdRole).toUInt()});
    }
  });

  connect(m_coll_listview->selectionModel(), &QItemSelectionModel::currentChanged, this,
          [this](const QModelIndex& current) {
            if (!current.isValid()) {
              m_playback_controls->setCollectionSelected(false);
              m_collection_contents_model->setCollection(std::nullopt);
              m_menu_bar->setContext(MenuBar::Context::None);
              updateSelectionStatus({}, SelectionStatusKind::Collection);
              return;
            }
            m_collection_contents_model->setCollection(
                vgmtrans::core::CollectionId{current.data(vgmtrans::ui::IdRole).toUInt()});
            m_playback_controls->setCollectionSelected(true);
            m_menu_bar->setContext(MenuBar::Context::Collection);
            updateSelectionStatus(current, SelectionStatusKind::Collection);
          });
  connect(m_rawfile_listview->selectionModel(), &QItemSelectionModel::currentChanged, this,
          [this](const QModelIndex& current) {
            m_menu_bar->setContext(current.isValid() ? MenuBar::Context::Source
                                                     : MenuBar::Context::None);
            updateSelectionStatus(current, SelectionStatusKind::Source);
          });
  connect(m_vgmfile_listview->selectionModel(), &QItemSelectionModel::currentChanged, this,
          [this, synchronizeAssetSelection](const QModelIndex& current) {
            m_menu_bar->setContext(contextForAsset(m_workspace, current));
            updateSelectionStatus(current, SelectionStatusKind::Asset);
            if (current.isValid()) {
              const auto asset = vgmtrans::core::AssetId{
                  current.data(vgmtrans::ui::IdRole).toUInt()};
              synchronizeAssetSelection(asset, m_vgmfile_listview);
              MdiArea::the()->selectAsset(asset, m_vgmfile_listview);
            }
          });
  connect(m_coll_view->selectionModel(), &QItemSelectionModel::currentChanged, this,
          [this, synchronizeAssetSelection](const QModelIndex& current) {
            m_menu_bar->setContext(
                current.data(vgmtrans::ui::IsCollectionRole).toBool()
                    ? MenuBar::Context::Collection
                    : contextForAsset(m_workspace, current));
            updateSelectionStatus(current, SelectionStatusKind::CollectionContents);
            if (current.isValid() &&
                !current.data(vgmtrans::ui::IsCollectionRole).toBool()) {
              const auto asset = vgmtrans::core::AssetId{
                  current.data(vgmtrans::ui::IdRole).toUInt()};
              synchronizeAssetSelection(asset, m_coll_view);
              MdiArea::the()->selectAsset(asset, m_coll_view);
            }
          });

  connect(m_coll_listview, &QAbstractItemView::doubleClicked, this,
          [this](const QModelIndex& index) {
            if (index.isValid()) {
              togglePlayback();
            }
          });

  connect(m_vgmfile_listview, &QAbstractItemView::doubleClicked, this,
          [](const QModelIndex& index) {
            if (index.isValid()) {
              MdiArea::the()->newView(
                  vgmtrans::core::AssetId{index.data(vgmtrans::ui::IdRole).toUInt()});
            }
          });
  connect(m_coll_view, &QAbstractItemView::doubleClicked, this,
          [](const QModelIndex& index) {
            if (index.isValid() && !index.data(vgmtrans::ui::IsCollectionRole).toBool()) {
              MdiArea::the()->newView(
                  vgmtrans::core::AssetId{index.data(vgmtrans::ui::IdRole).toUInt()});
            }
          });

  m_rawfile_listview->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_rawfile_listview, &QWidget::customContextMenuRequested, this,
          [this, closeSelectedSources](const QPoint& position) {
            const QModelIndexList rows = m_rawfile_listview->selectionModel()->selectedRows();
            if (rows.isEmpty()) {
              return;
            }
            QMenu menu(m_rawfile_listview);
            QAction* saveOriginal = menu.addAction(tr("Save as Original Format"));
            menu.addSeparator();
            QAction* close = menu.addAction(tr("Close"));
            close->setShortcuts({Qt::Key_Backspace, Qt::Key_Delete});
            close->setShortcutVisibleInContextMenu(true);
            const QAction* chosen = menu.exec(m_rawfile_listview->viewport()->mapToGlobal(position));
            if (chosen == saveOriginal) {
              this->saveOriginal(m_rawfile_listview, OriginalItemKind::Source);
            } else if (chosen == close) {
              closeSelectedSources();
            }
          });

  const auto showAssetContextMenu = [this, removeSelectedAssets](QAbstractItemView* view,
                                                                const QModelIndex& current,
                                                                const QPoint& position) {
    const MenuBar::Context context = contextForAsset(m_workspace, current);
    if (context == MenuBar::Context::None) {
      return;
    }
    const auto addDisabled = [](QMenu& menu, const QString& label) {
      QAction* action = menu.addAction(label);
      action->setEnabled(false);
    };

    QMenu menu(view);
    QAction* open = menu.addAction(tr("Open Analysis"));
    open->setShortcut(Qt::Key_Return);
    open->setShortcutVisibleInContextMenu(true);
    menu.addSeparator();
    QAction* saveMidi = nullptr;
    QAction* saveSf2 = nullptr;
    QAction* saveDls = nullptr;
    QAction* saveOriginalAction = nullptr;
    if (context == MenuBar::Context::Sequence) {
      saveMidi = menu.addAction(tr("Save as MIDI"));
      saveOriginalAction = menu.addAction(tr("Save as Original Format"));
      menu.addSeparator();
      addDisabled(menu, tr("Stitch"));
    } else if (context == MenuBar::Context::InstrumentSet) {
      saveSf2 = menu.addAction(tr("Save as SF2"));
      saveDls = menu.addAction(tr("Save as DLS"));
      saveOriginalAction = menu.addAction(tr("Save as Original Format"));
    } else if (context == MenuBar::Context::SampleCollection) {
      addDisabled(menu, tr("Save all samples as WAV"));
      saveOriginalAction = menu.addAction(tr("Save as Original Format"));
    } else {
      saveOriginalAction = menu.addAction(tr("Save as Original Format"));
    }
    menu.addSeparator();
    QAction* remove = menu.addAction(tr("Remove"));
    remove->setShortcuts({Qt::Key_Backspace, Qt::Key_Delete});
    remove->setShortcutVisibleInContextMenu(true);

    const QAction* chosen = menu.exec(view->viewport()->mapToGlobal(position));
    if (chosen == open) {
      MdiArea::the()->newView(
          vgmtrans::core::AssetId{current.data(vgmtrans::ui::IdRole).toUInt()});
    } else if (saveMidi != nullptr && chosen == saveMidi) {
      exportSequenceMidi(current);
    } else if (saveSf2 != nullptr && chosen == saveSf2) {
      exportInstrumentSet(current, vgmtrans::core::SynthExportFormat::SoundFont2);
    } else if (saveDls != nullptr && chosen == saveDls) {
      exportInstrumentSet(current, vgmtrans::core::SynthExportFormat::Dls);
    } else if (chosen == saveOriginalAction) {
      saveOriginal(view, OriginalItemKind::Asset);
    } else if (chosen == remove) {
      removeSelectedAssets();
    }
  };

  m_vgmfile_listview->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_vgmfile_listview, &QWidget::customContextMenuRequested, this,
          [this, showAssetContextMenu](const QPoint& position) {
            showAssetContextMenu(m_vgmfile_listview, m_vgmfile_listview->currentIndex(),
                                 position);
          });

  m_coll_listview->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_coll_listview, &QWidget::customContextMenuRequested, this,
          [this, exportSelectedCollection](const QPoint& position) {
            const QModelIndex current = m_coll_listview->currentIndex();
            if (!current.isValid()) {
              return;
            }

            QMenu menu(m_coll_listview);
            QAction* play = menu.addAction(tr("Play / Pause"));
            play->setShortcut(Qt::Key_Return);
            play->setShortcutVisibleInContextMenu(true);
            menu.addSeparator();
            QAction* midiSf2 = menu.addAction(tr("Export as MIDI and SF2"));
            QAction* midiDls = menu.addAction(tr("Export as MIDI and DLS"));
            QAction* all = menu.addAction(tr("Export as MIDI, SF2, and DLS"));
            menu.addSeparator();
            QAction* stitch = menu.addAction(tr("Stitch"));
            stitch->setEnabled(false);
            QAction* chosen = menu.exec(m_coll_listview->viewport()->mapToGlobal(position));
            if (chosen == play) {
              togglePlayback();
            } else if (chosen == midiSf2) {
              exportSelectedCollection(0);
            } else if (chosen == midiDls) {
              exportSelectedCollection(1);
            } else if (chosen == all) {
              exportSelectedCollection(2);
            }
          });

  m_coll_view->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_coll_view, &QWidget::customContextMenuRequested, this,
          [this, exportSelectedCollection, showAssetContextMenu](const QPoint& position) {
            const QModelIndex current = m_coll_view->currentIndex();
            if (!current.isValid()) {
              return;
            }
            if (!current.data(vgmtrans::ui::IsCollectionRole).toBool()) {
              showAssetContextMenu(m_coll_view, current, position);
              return;
            }

            QMenu menu(m_coll_view);
            QAction* play = menu.addAction(tr("Play / Pause"));
            play->setShortcut(Qt::Key_Return);
            play->setShortcutVisibleInContextMenu(true);
            menu.addSeparator();
            QAction* midiSf2 = menu.addAction(tr("Export as MIDI and SF2"));
            QAction* midiDls = menu.addAction(tr("Export as MIDI and DLS"));
            QAction* all = menu.addAction(tr("Export as MIDI, SF2, and DLS"));
            menu.addSeparator();
            QAction* stitch = menu.addAction(tr("Stitch"));
            stitch->setEnabled(false);
            QAction* chosen = menu.exec(m_coll_view->viewport()->mapToGlobal(position));
            if (chosen == play) {
              togglePlayback();
            } else if (chosen == midiSf2) {
              exportSelectedCollection(0);
            } else if (chosen == midiDls) {
              exportSelectedCollection(1);
            } else if (chosen == all) {
              exportSelectedCollection(2);
            }
          });

  connect(&m_workspace, &vgmtrans::ui::WorkspaceController::snapshotChanged,
          MdiArea::the(), &MdiArea::workspaceChanged);
  connect(&m_workspace, &vgmtrans::ui::WorkspaceController::snapshotChanged,
          this, [this] {
            if (!m_sequence_player->hasActiveCollection()) {
              return;
            }
            const auto* collection = m_workspace.snapshot().collection(
                m_sequence_player->activeCollection());
            std::vector<vgmtrans::core::AssetId> assets;
            if (collection != nullptr && collection->members.sequence) {
              assets.push_back(*collection->members.sequence);
              assets.insert(assets.end(), collection->members.instrumentSets.begin(),
                            collection->members.instrumentSets.end());
              assets.insert(assets.end(), collection->members.sampleCollections.begin(),
                            collection->members.sampleCollections.end());
            }
            const auto activeAssets = m_sequence_player->activeAssets();
            const bool assetsChanged =
                !std::ranges::equal(assets, activeAssets) ||
                std::ranges::any_of(activeAssets, [this](const auto asset) {
                  return m_workspace.snapshot().asset(asset) == nullptr;
                });
            if (collection == nullptr || assetsChanged) {
              m_sequence_player->stop();
            }
          });
  connect(MdiArea::the(), &MdiArea::assetSelected, this,
          [synchronizeAssetSelection](vgmtrans::core::AssetId asset, QWidget* caller) {
            synchronizeAssetSelection(asset, caller);
          });
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
  if (event->type() == QEvent::MouseButtonPress) {
    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    auto *widget = qobject_cast<QWidget *>(obj);
    if (mouseEvent->button() == Qt::LeftButton && widget && (widget == this || isAncestorOf(widget)) &&
        isDockSeparatorCursor(cursor().shape())) {
      m_dockLayout->beginSeparatorDrag();
    }
  } else if (event->type() == QEvent::MouseMove) {
    m_dockLayout->handleSeparatorMouseMove();
  } else if (event->type() == QEvent::MouseButtonRelease) {
    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    if (mouseEvent->button() == Qt::LeftButton) {
      m_dockLayout->endSeparatorDrag();
    }
  }

  if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) {
    auto* keyEvent = static_cast<QKeyEvent*>(event);
    const bool removeKey = keyEvent->key() == Qt::Key_Backspace || keyEvent->key() == Qt::Key_Delete;
    const auto commandModifiers = Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;
    auto* widget = qobject_cast<QWidget*>(obj);
    const auto targets = [widget](const QWidget* view) {
      return widget != nullptr && view != nullptr && (widget == view || view->isAncestorOf(widget));
    };
    const bool targetsSources = targets(m_rawfile_listview);
    const bool targetsAssets = targets(m_vgmfile_listview);
    if (removeKey && !(keyEvent->modifiers() & commandModifiers) && (targetsSources || targetsAssets)) {
      event->accept();
      if (event->type() == QEvent::KeyPress && !keyEvent->isAutoRepeat()) {
        if (targetsSources) {
          removeSelectedSources();
        } else {
          removeSelectedAssets();
        }
      }
      return true;
    }
  }

  if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
    auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (!keyEvent->isAutoRepeat() && keyEvent->key() == HexViewInput::kModifierKey) {
      emit seekModifierActiveChanged(event->type() == QEvent::KeyPress);
    }
  } else if (event->type() == QEvent::ApplicationDeactivate) {
    m_dockLayout->cancelInteraction();
    emit seekModifierActiveChanged(false);
  }
  return QMainWindow::eventFilter(obj, event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
  if (!event) {
    return;
  }

  if (!isFileDropMime(event->mimeData())) {
    event->ignore();
    hideDragOverlay();
    return;
  }
  event->acceptProposedAction();
  showDragOverlay();
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event) {
  if (!event) {
    return;
  }

  if (!isFileDropMime(event->mimeData())) {
    event->ignore();
    hideDragOverlay();
    return;
  }
  event->acceptProposedAction();
  showDragOverlay();
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent *event) {
  event->accept();
  hideDragOverlay();
}

void MainWindow::dropEvent(QDropEvent *event) {
  hideDragOverlay();
  if (!event) {
    return;
  }

  if (!isFileDropMime(event->mimeData())) {
    event->ignore();
    return;
  }

  const QMimeData* mimeData = event->mimeData();
  const QStringList portalFiles = retrievePortalDroppedFiles(mimeData);
  if (!portalFiles.isEmpty()) {
    for (const QString& filePath : portalFiles) {
      if (!filePath.isEmpty()) {
        openFileInternal(filePath);
      }
    }
    event->acceptProposedAction();
    return;
  }

  handleDroppedUrls(mimeData->urls());
  event->acceptProposedAction();
}

void MainWindow::closeEvent(QCloseEvent *event) {
  m_dockLayout->saveOnClose();
  QMainWindow::closeEvent(event);
}

void MainWindow::showDragOverlay() {
  updateDragOverlayGeometry();
  if (!m_dragOverlay->isVisible()) {
    m_dragOverlay->show();
  }
  m_dragOverlay->raise();
}

void MainWindow::hideDragOverlay() {
  m_dragOverlay->hide();
}

void MainWindow::handleDroppedUrls(const QList<QUrl>& urls) {
  hideDragOverlay();

  if (urls.isEmpty()) {
    return;
  }

  for (const auto &url : urls) {
    if (!url.isLocalFile()) {
      continue;
    }

    const QString localFile = url.toLocalFile();
    if (!localFile.isEmpty()) {
      openFileInternal(localFile);
    }
  }
}

void MainWindow::openFile() {
  auto filenames = QFileDialog::getOpenFileNames(
      this, "Select a file...", QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
      "All files (*)");

  if (filenames.isEmpty())
    return;

  for (QString &filename : filenames) {
    openFileInternal(filename);
  }
}

void MainWindow::openPaths(std::span<const std::filesystem::path> paths) {
  if (paths.empty()) {
    return;
  }

  const vgmtrans::ui::OpenResult result = m_workspace.openPaths(paths);
  for (const auto& path : result.opened) {
    Settings::the()->recentFiles.add(pathText(path));
  }
  if (!result.opened.empty()) {
    m_menu_bar->updateRecentFilesMenu();
    statusBarContent->setStatus(
        tr("Scanned %1 file(s)").arg(static_cast<qulonglong>(result.opened.size())), {});
  }
  if (!result.failures.empty()) {
    const auto& failure = result.failures.front();
    statusBarContent->setStatus(QString::fromStdString(failure.path.filename().string()),
                                QString::fromStdString(failure.message));
  }
}

void MainWindow::openFileInternal(const QString& filename) {
  static QString UNSUPPORTED_RAW_IMAGE_WARNING{
      "'%1' is a raw image file. Data is unlikely to be read correctly, do you wish "
      "to continue anyway?"};

  static QString UNSUPPORTED_RAW_IMAGE_DESCRIPTION{
      "If this is a dump of a CD or DVD (e.g. PlayStation), please "
      "convert it to '.iso'. The program cannot read raw dumps from optical media."};

  auto file_info = QFileInfo(filename);
  if (file_info.completeSuffix().contains("img")) {
    QMessageBox user_choice(QMessageBox::Icon::Warning, "File format might be unsopported",
                            UNSUPPORTED_RAW_IMAGE_WARNING.arg(file_info.fileName()),
                            QMessageBox::StandardButton::Yes | QMessageBox::StandardButton::No,
                            this);
    user_choice.setInformativeText(UNSUPPORTED_RAW_IMAGE_DESCRIPTION);
    user_choice.setWindowModality(Qt::WindowModal);
    user_choice.exec();

    if (user_choice.result() != QMessageBox::StandardButton::Yes) {
      return;
    }
  }

  const std::array path{filePath(filename)};
  openPaths(path);
}

void MainWindow::updateSelectionStatus(const QModelIndex& index,
                                       SelectionStatusKind kind) {
  if (!index.isValid()) {
    statusBarContent->setStatus({}, {});
    return;
  }

  QIcon icon = index.siblingAtColumn(0).data(Qt::DecorationRole).value<QIcon>();
  const QIcon* iconPointer = icon.isNull() ? nullptr : &icon;
  const QString displayName =
      index.siblingAtColumn(0).data(Qt::DisplayRole).toString();

  if (kind == SelectionStatusKind::Source) {
    const auto* source = m_workspace.snapshot().source(vgmtrans::core::SourceId{
        index.data(vgmtrans::ui::IdRole).toUInt()});
    const int size = source != nullptr
        ? static_cast<int>(std::min<u64>(source->size,
                                        std::numeric_limits<int>::max()))
        : -1;
    statusBarContent->setStatus(displayName, {}, iconPointer, -1, size);
    return;
  }

  const bool isCollection = kind == SelectionStatusKind::Collection ||
      (kind == SelectionStatusKind::CollectionContents &&
       index.data(vgmtrans::ui::IsCollectionRole).toBool());
  if (isCollection) {
    statusBarContent->setStatus(
        QStringLiteral("<b>%1</b>").arg(displayName), {}, iconPointer);
    return;
  }

  const auto* asset = m_workspace.snapshot().asset(vgmtrans::core::AssetId{
      index.data(vgmtrans::ui::IdRole).toUInt()});
  if (asset == nullptr) {
    statusBarContent->setStatus(displayName, {}, iconPointer);
    return;
  }
  const auto& metadata = vgmtrans::core::metadata(*asset);
  const int offset = static_cast<int>(std::min<u64>(
      metadata.range.offset, std::numeric_limits<int>::max()));
  const int size = static_cast<int>(std::min<u64>(
      metadata.range.size, std::numeric_limits<int>::max()));
  statusBarContent->setStatus(
      QStringLiteral("<b>%1</b>").arg(displayName),
      QString::fromStdString(metadata.format), iconPointer, offset, size);
}

void MainWindow::setCollectionStitchAvailable(bool available) {
  m_stitchButton->setEnabled(available);
  if (!available) {
    m_stitchButton->setChecked(false);
  }
}

void MainWindow::setCollectionStitchOpen(bool open) {
  m_stitchButton->setChecked(open);
}

void MainWindow::showToast(const QString& message, ToastType type, int durationMs) {
  m_toastHost->showToast(message, type, durationMs);
}

void MainWindow::resizeEvent(QResizeEvent *event) {
  QMainWindow::resizeEvent(event);
  updateDragOverlayGeometry();
  m_dockLayout->handleResize(event->oldSize(), event->size());
}
void MainWindow::updateDragOverlayAppearance() {
  m_dragOverlay->setStyleSheet(QStringLiteral("background-color: rgba(0, 0, 0, 102);"));
}

void MainWindow::updateDragOverlayGeometry() {
  m_dragOverlay->setGeometry(rect());
  m_dragOverlay->raise();
}
