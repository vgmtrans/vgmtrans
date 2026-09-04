/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "application/WorkspaceController.h"
#include "models/ValueModels.h"
#include "value/scan/FormatModule.h"
#include "value/scan/ScanResultBuilder.h"

#include <QCoreApplication>
#include <QFile>
#include <QIcon>
#include <QTemporaryDir>

#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {

using namespace vgmtrans::core;
using namespace vgmtrans::ui;

void expect(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

ScanResult scanUiProbe(const ScanInput& input) {
  ScanResultBuilder result(input, "UI Probe");
  if (input.reader.empty() || input.reader.u8At(0) != 0x7f) {
    return result.finish();
  }

  // Like several sequence formats, metadata identifies only a header while the
  // owned annotation graph describes the full decoded source extent.
  const auto misc = result.misc("Probe asset", input.reader.range(0, 1)).payload({0x01, 0x02});
  const auto section =
      result.sourceMap().section("Probe section", input.reader.range(0, 1)).owner(ObjectRefs::asset(misc.id()));
  result.sourceMap()
      .annotation(SourceRole::Field, "Magic", input.reader.range(0, 1))
      .parent(section.id())
      .fieldsAsChildren()
      .field("value", input.reader.range(0, 1), 0x7f, SourceValueDisplay::Hex)
      .derived("decoded", true, SourceValueDisplay::Boolean);
  result.sourceMap()
      .annotation(SourceRole::Payload, "Payload", input.reader.range(1, 2))
      .parent(section.id())
      .field("first", input.reader.range(1, 1), 1);
  result.sourceCollection("Probe collection").misc(misc);
  result.warning("Probe warning", input.reader.range(0, 1));
  return result.finish();
}

void workspacePublishesModelsAndRemovesSourceFamilies() {
  QTemporaryDir directory;
  expect(directory.isValid(), "temporary directory should be available");
  const QString filename = directory.filePath(QStringLiteral("probe.bin"));
  QFile source(filename);
  expect(source.open(QIODevice::WriteOnly), "probe source should open for writing");
  expect(source.write(QByteArray::fromHex("7f0102")) == 3, "probe source should be written");
  source.close();

  WorkspaceController workspace([](Session& session) {
    session.registerFormat(FormatModule{
        .name = "UI Probe",
        .scan = scanUiProbe,
    });
  });
  SourceTableModel sources(workspace);
  AssetTableModel assets(workspace);
  CollectionTableModel collections(workspace);
  CollectionFilterProxyModel collectionFilter(workspace);
  collectionFilter.setSourceModel(&collections);
  CollectionContentsModel contents(workspace);
  DiagnosticTableModel diagnostics(workspace);

#ifdef Q_OS_WIN
  const std::array path{std::filesystem::path(filename.toStdWString())};
#else
  const QByteArray utf8Filename = filename.toUtf8();
  const std::array path{
      std::filesystem::path(utf8Filename.constData(), utf8Filename.constData() + utf8Filename.size())};
#endif
  const OpenResult opened = workspace.openPaths(path);
  expect(opened.opened.size() == 1 && opened.failures.empty(), "workspace should open the probe source");
  expect(sources.rowCount() == 1, "source model should publish the loaded source");
  expect(assets.rowCount() == 1, "asset model should publish the scanned asset");
  expect(assets.columnCount() == 2, "asset contents should be structural rows rather than a summary column");
  expect(collections.rowCount() == 1, "collection model should publish the resolved collection");
  collectionFilter.setFilterFixedString(QStringLiteral("Probe collection"));
  expect(collectionFilter.rowCount() == 1, "collection filtering should retain matching collections");
  collectionFilter.setFilterFixedString(QStringLiteral("no such collection"));
  expect(collectionFilter.rowCount() == 0, "collection filtering should hide non-matching collections");
  collectionFilter.setFilterFixedString({});
  expect(diagnostics.rowCount() == 1, "diagnostic model should publish scan diagnostics");
  expect(sources.index(0, 0).data(IdRole).toUInt() == 0, "source model should expose its stable id");
  expect(assets.index(0, 0).data(Qt::DisplayRole).toString() == QStringLiteral("Probe asset"),
         "asset model should expose value metadata");

  const auto collectionId = CollectionId{collections.index(0, 0).data(IdRole).toUInt()};
  const auto normalCollectionIcon = collections.index(0, 0).data(Qt::DecorationRole).value<QIcon>();
  collections.setPlayingCollection(collectionId);
  const auto playingCollectionIcon = collections.index(0, 0).data(Qt::DecorationRole).value<QIcon>();
  expect(playingCollectionIcon.cacheKey() != normalCollectionIcon.cacheKey(),
         "collection model should decorate the playing collection with a distinct icon");
  collections.setPlayingCollection(std::nullopt);
  expect(collections.index(0, 0).data(Qt::DecorationRole).value<QIcon>().cacheKey() == normalCollectionIcon.cacheKey(),
         "collection model should restore the normal icon when playback stops");
  contents.setCollection(collectionId);
  expect(contents.rowCount() == 2, "collection contents should expose the collection root and resolve its member ids");
  expect(workspace.sourceBytes(SourceId{0}).size() == 3, "workspace should expose source bytes for later inspectors");

  const AssetId assetId{assets.index(0, 0).data(IdRole).toUInt()};
  auto inspector = workspace.inspect(assetId);
  expect(inspector != nullptr, "source inspection should resolve a value asset and its source bytes");
  expect(inspector->bytes().size() == 3 && inspector->bytes()[0] == 0x7f,
         "source inspector should expand a header-only asset range across its owned annotations");
  expect(inspector->roots().size() == 1, "source inspector should preserve the source annotation hierarchy");
  expect(inspector->children(inspector->roots().front()).size() == 2,
         "source inspector should expose child annotations to both panes");
  const auto magic = inspector->annotationAt(0);
  const auto payload = inspector->annotationAt(1);
  expect(magic && inspector->annotation(*magic)->label == "Magic",
         "source inspector hit testing should prefer the most specific annotation");
  expect(payload && inspector->annotation(*payload)->label == "Payload",
         "source inspector hit testing should follow byte ranges");
  const auto magicField = inspector->itemAt(0);
  expect(magicField && magicField->isField() && magicField->annotation == *magic && magicField->field == 0,
         "opted-in source fields should be addressable as transient inspection children");
  expect(inspector->field(*magicField) != nullptr && inspector->field(*magicField)->name == "value" &&
             inspector->range(*magicField) == SourceRange{.source = SourceId{0}, .offset = 0, .size = 1},
         "a transient field child should retain its exact source range for HexView selection");
  const auto payloadItem = inspector->itemAt(1);
  expect(payloadItem && !payloadItem->isField() && payloadItem->annotation == *payload,
         "fields should remain annotation details unless their owner explicitly opts into child projection");

  const std::array assetIds{assetId};
  expect(workspace.removeAssets(assetIds) == 1, "workspace should remove selected detected assets");
  expect(sources.rowCount() == 0, "removing the last detected asset should close its scanned source");
  expect(assets.rowCount() == 0 && collections.rowCount() == 0 && contents.rowCount() == 0,
         "removing a detected asset should update asset and collection models together");
  expect(workspace.snapshot().sourceMap().empty(),
         "removing a detected asset should remove its owned source annotation tree");
  expect(workspace.removeAssets(assetIds) == 0, "removing an already removed asset should be a no-op");
  expect(inspector->bytes().size() == 3 && inspector->annotation(*magic) != nullptr,
         "an open source inspection should remain valid until its tab closes");

  const std::array sourceId{SourceId{0}};
  expect(workspace.removeSources(sourceId) == 0, "the automatically closed source should already be absent");
  expect(sources.rowCount() == 0 && assets.rowCount() == 0 && collections.rowCount() == 0 &&
             diagnostics.rowCount() == 0 && contents.rowCount() == 0,
         "all models should reset to the new immutable snapshot after removal");
  expect(inspector->bytes().size() == 3 && inspector->annotation(*magic) != nullptr,
         "an open source inspection should retain its immutable source data after removal");
}

void workspaceDoesNotPublishEmptyScansAsSources() {
  QTemporaryDir directory;
  expect(directory.isValid(), "temporary directory should be available");
  const QString filename = directory.filePath(QStringLiteral("empty-scan.bin"));
  QFile source(filename);
  expect(source.open(QIODevice::WriteOnly), "empty-scan source should open for writing");
  expect(source.write(QByteArray::fromHex("000102")) == 3, "empty-scan source should be written");
  source.close();

  WorkspaceController workspace([](Session& session) {
    session.registerFormat(FormatModule{
        .name = "UI Probe",
        .scan = scanUiProbe,
    });
  });
  SourceTableModel sources(workspace);

#ifdef Q_OS_WIN
  const std::array path{std::filesystem::path(filename.toStdWString())};
#else
  const QByteArray utf8Filename = filename.toUtf8();
  const std::array path{
      std::filesystem::path(utf8Filename.constData(), utf8Filename.constData() + utf8Filename.size())};
#endif
  const OpenResult opened = workspace.openPaths(path);
  expect(opened.opened.size() == 1 && opened.failures.empty(), "workspace should read and scan the source");
  expect(sources.rowCount() == 0, "a scan with no detected files should not appear in Scanned Files");
  expect(workspace.snapshot().sources().empty(), "the workspace should close a source after an empty scan");
}

}  // namespace

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  try {
    workspacePublishesModelsAndRemovesSourceFamilies();
    workspaceDoesNotPublishEmptyScansAsSources();
    std::cout << "Value Qt model tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Value Qt model tests failed: " << error.what() << '\n';
    return 1;
  }
}
