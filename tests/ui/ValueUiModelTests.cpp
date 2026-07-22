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

  const SourceRange wholeSource = input.reader.range(0, input.reader.size());
  const auto misc = result.misc("Probe asset", wholeSource).payload({0x01, 0x02});
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
  expect(collections.rowCount() == 1, "collection model should publish the resolved collection");
  collectionFilter.setFilterFixedString(QStringLiteral("Probe collection"));
  expect(collectionFilter.rowCount() == 1,
         "collection filtering should retain matching collections");
  collectionFilter.setFilterFixedString(QStringLiteral("no such collection"));
  expect(collectionFilter.rowCount() == 0,
         "collection filtering should hide non-matching collections");
  collectionFilter.setFilterFixedString({});
  expect(diagnostics.rowCount() == 1, "diagnostic model should publish scan diagnostics");
  expect(sources.index(0, 0).data(IdRole).toUInt() == 0, "source model should expose its stable id");
  expect(assets.index(0, 0).data(Qt::DisplayRole).toString() == QStringLiteral("Probe asset"),
         "asset model should expose value metadata");

  const auto collectionId = CollectionId{collections.index(0, 0).data(IdRole).toUInt()};
  contents.setCollection(collectionId);
  expect(contents.rowCount() == 2,
         "collection contents should expose the collection root and resolve its member ids");
  expect(workspace.sourceBytes(SourceId{0}).size() == 3, "workspace should expose source bytes for later inspectors");

  const std::array sourceId{SourceId{0}};
  expect(workspace.removeSources(sourceId) == 1, "workspace should remove the selected source family");
  expect(sources.rowCount() == 0 && assets.rowCount() == 0 && collections.rowCount() == 0 &&
             diagnostics.rowCount() == 0 && contents.rowCount() == 0,
         "all models should reset to the new immutable snapshot after removal");
}

}  // namespace

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  try {
    workspacePublishesModelsAndRemovesSourceFamilies();
    std::cout << "Value Qt model tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Value Qt model tests failed: " << error.what() << '\n';
    return 1;
  }
}
