#include "model/Ao3Project.h"
#include "model/ProjectSerializer.h"
#include "model/ZstdArchive.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QTemporaryDir>
#include <cassert>

#if defined(_WIN32)
#include <crtdbg.h>
#include <stdlib.h>
#endif

int main(int argc, char *argv[]) {
#if defined(_WIN32)
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#if defined(_DEBUG)
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
  _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
#endif

  QCoreApplication app(argc, argv);

  QTemporaryDir tempDir;
  assert(tempDir.isValid());

  const QString projPath =
      tempDir.filePath(QStringLiteral("test_work.ao3proj"));

  // 1. Create a project
  Ao3Project originalProj;
  originalProj.setTitle(QStringLiteral("My Test AO3 Epic"));
  originalProj.setAuthor(QStringLiteral("TestAuthor"));
  originalProj.setWorkSkinCss(
      QStringLiteral(".custom-class { color: #bd93f9; font-weight: bold; }"));
  originalProj.setHistoryLog(QStringLiteral("Initial commit: created story."));

  originalProj.chapters().clear();
  Chapter ch1;
  ch1.setTitle(QStringLiteral("Chapter 1: The Gathering"));
  ch1.setOrder(1);
  ch1.setHtml(QStringLiteral("<p>It was a dark and stormy night...</p>"));
  originalProj.chapters().append(ch1);

  Chapter ch2;
  ch2.setTitle(QStringLiteral("Chapter 2: The Journey"));
  ch2.setOrder(2);
  ch2.setHtml(QStringLiteral("<p>They ventured into the unknown realm.</p>"));
  originalProj.chapters().append(ch2);

  // 2. Save project to .ao3proj archive
  QString saveError;
  bool saveOk = ProjectSerializer::save(projPath, originalProj, &saveError);
  if (!saveOk) {
    qCritical() << "Save failed:" << saveError;
    return 1;
  }
  qDebug() << "Successfully saved project archive to:" << projPath
           << "Size:" << QFile(projPath).size() << "bytes";
  assert(QFile::exists(projPath));
  assert(QFile(projPath).size() > 0);

  // 3. Verify format detection
  assert(ZstdArchive::isZstdArchive(projPath));

  // 4. Load project back using 3-stage validation pipeline
  Ao3Project loadedProj;
  ProjectLoadReport report = ProjectSerializer::loadWithValidation(projPath, loadedProj);
  assert(report.success);
  assert(!report.isLegacy);
  assert(report.failedStage == ValidationStage::None);
  assert(report.formatType == ProjectFormatType::ModernArchiveZip ||
         report.formatType == ProjectFormatType::ModernArchiveZstd);
  assert(!loadedProj.isLegacyFormat());

  // 5. Verify integrity of loaded contents
  assert(loadedProj.title() == originalProj.title());
  assert(loadedProj.author() == originalProj.author());
  assert(loadedProj.workSkinCss() == originalProj.workSkinCss());
  assert(loadedProj.historyLog() == originalProj.historyLog());
  assert(loadedProj.chapters().size() == 2);
  assert(loadedProj.chapters().at(0).title() == ch1.title());
  assert(loadedProj.chapters().at(0).html() == ch1.html());
  assert(loadedProj.chapters().at(1).title() == ch2.title());
  assert(loadedProj.chapters().at(1).html() == ch2.html());
  qDebug() << "✓ Stage 1-3 validation passed for modern .ao3proj archive.";

  // 6. Test loading existing user deflate archive if present
  const QString realFilePath = QStringLiteral("./test/units/test1.ao3proj");
  if (QFile::exists(realFilePath)) {
    Ao3Project realProj;
    ProjectLoadReport realReport = ProjectSerializer::loadWithValidation(realFilePath, realProj);
    assert(realReport.success);
    assert(!realReport.isLegacy);
    qDebug() << "✓ Real-world Deflate project loaded successfully:"
             << realProj.title() << "Chapters:" << realProj.chapters().size();
  }

  // 7. Test Stage 1: Magic Bytes Check on corrupt/invalid file (e.g. PNG / random binary)
  const QString fakePngPath = tempDir.filePath(QStringLiteral("fake_image.ao3proj"));
  {
    QFile fakeFile(fakePngPath);
    assert(fakeFile.open(QIODevice::WriteOnly));
    fakeFile.write("\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDRRandomNotAnArchiveData");
    fakeFile.close();
  }
  {
    Ao3Project dummy;
    ProjectLoadReport invalidReport = ProjectSerializer::loadWithValidation(fakePngPath, dummy);
    assert(!invalidReport.success);
    assert(invalidReport.failedStage == ValidationStage::MagicBytesCheck);
    assert(invalidReport.summaryError.contains(QStringLiteral("Magic Bytes")));
    assert(!invalidReport.detailedExplanation.isEmpty());
    assert(invalidReport.detailedExplanation.contains(QStringLiteral("INVALID_MAGIC_BYTES")));
    assert(invalidReport.headerHex.startsWith(QStringLiteral("89 50 4E 47")));
    qDebug() << "✓ Stage 1: Magic Bytes check correctly rejected invalid header signature.";
  }

  // 8. Test Empty File Check (0 bytes)
  const QString emptyPath = tempDir.filePath(QStringLiteral("empty.ao3proj"));
  {
    QFile emptyFile(emptyPath);
    assert(emptyFile.open(QIODevice::WriteOnly));
    emptyFile.close();
  }
  {
    Ao3Project dummy;
    ProjectLoadReport emptyReport = ProjectSerializer::loadWithValidation(emptyPath, dummy);
    assert(!emptyReport.success);
    assert(emptyReport.failedStage == ValidationStage::MagicBytesCheck);
    assert(emptyReport.summaryError.contains(QStringLiteral("0 bytes")));
    qDebug() << "✓ Stage 1: Zero-byte empty file correctly identified and rejected.";
  }

  // 9. Test Non-existent File (Stage 0: FileAccess)
  {
    Ao3Project dummy;
    ProjectLoadReport missingReport = ProjectSerializer::loadWithValidation(
        tempDir.filePath(QStringLiteral("does_not_exist.ao3proj")), dummy);
    assert(!missingReport.success);
    assert(missingReport.failedStage == ValidationStage::FileAccess);
    assert(missingReport.summaryError.contains(QStringLiteral("not found")));
    qDebug() << "✓ Stage 0: Non-existent file correctly handled with FileAccess stage report.";
  }

  // 10. Test Stage 2: Structural Integrity Check on archive missing metadata.xml
  const QString missingManifestPath = tempDir.filePath(QStringLiteral("missing_manifest.ao3proj"));
  {
    QMap<QString, QByteArray> badEntries;
    badEntries.insert(QStringLiteral("readme.txt"), "This archive has no metadata.xml descriptor");
    badEntries.insert(QStringLiteral("chapter_1.html"), "<p>orphan</p>");
    QString err;
    assert(ZstdArchive::compressArchive(missingManifestPath, badEntries, &err));
  }
  {
    Ao3Project dummy;
    ProjectLoadReport structReport = ProjectSerializer::loadWithValidation(missingManifestPath, dummy);
    assert(!structReport.success);
    assert(structReport.failedStage == ValidationStage::StructureCheck);
    assert(structReport.summaryError.contains(QStringLiteral("metadata.xml")));
    assert(structReport.detailedExplanation.contains(QStringLiteral("MISSING_MANIFEST")));
    assert(structReport.detailedExplanation.contains(QStringLiteral("readme.txt")));
    qDebug() << "✓ Stage 2: Missing manifest detected, generated detailed explanation.";
  }

  // 11. Test Legacy Format: JSON v1 compatibility and subsequent upgrade to .ao3proj
  const QString legacyJsonPath = tempDir.filePath(QStringLiteral("legacy_story.json"));
  {
    QFile legacyFile(legacyJsonPath);
    assert(legacyFile.open(QIODevice::WriteOnly));
    const QByteArray legacyContent =
        "{\n"
        "  \"formatVersion\": 1,\n"
        "  \"title\": \"Legacy Classic Fanfic\",\n"
        "  \"workSkinCss\": \".legacy { font-style: italic; }\",\n"
        "  \"chapters\": [\n"
        "    {\n"
        "      \"id\": \"11111111-2222-3333-4444-555555555555\",\n"
        "      \"title\": \"Legacy Prologue\",\n"
        "      \"order\": 1,\n"
        "      \"html\": \"<p>Old style paragraph</p>\"\n"
        "    }\n"
        "  ]\n"
        "}\n";
    legacyFile.write(legacyContent);
    legacyFile.close();
  }
  {
    Ao3Project legacyProj;
    ProjectLoadReport legacyReport = ProjectSerializer::loadWithValidation(legacyJsonPath, legacyProj);
    assert(legacyReport.success);
    assert(legacyReport.isLegacy);
    assert(legacyReport.formatType == ProjectFormatType::LegacyJsonV1);
    assert(legacyProj.isLegacyFormat());
    assert(legacyProj.title() == QStringLiteral("Legacy Classic Fanfic"));
    assert(legacyProj.chapters().size() == 1);
    assert(legacyProj.chapters().first().title() == QStringLiteral("Legacy Prologue"));
    qDebug() << "✓ Legacy JSON v1 detected and opened in legacy compatibility mode.";

    // Upgrade test: Save legacy project to .ao3proj
    const QString upgradedPath = tempDir.filePath(QStringLiteral("upgraded_story.ao3proj"));
    QString upSaveErr;
    assert(ProjectSerializer::save(upgradedPath, legacyProj, &upSaveErr));

    // Reload upgraded file
    Ao3Project reloadedProj;
    ProjectLoadReport upReport = ProjectSerializer::loadWithValidation(upgradedPath, reloadedProj);
    assert(upReport.success);
    assert(!upReport.isLegacy); // Modern .ao3proj archive
    assert(reloadedProj.title() == QStringLiteral("Legacy Classic Fanfic"));
    assert(reloadedProj.chapters().size() == 1);
    qDebug() << "✓ Legacy project cleanly upgraded and saved as modern .ao3proj archive.";
  }

  // 12. Test Emergency Autosave (.ao3projasve) saving, 3-stage validation, and crash recovery
  {
    Ao3Project autosaveProj;
    autosaveProj.setTitle(QStringLiteral("Unsaved In-Progress Masterpiece"));
    autosaveProj.setAuthor(QStringLiteral("CreativeWriter"));
    autosaveProj.setWorkSkinCss(QStringLiteral(".autosave-style { font-family: monospace; }"));
    autosaveProj.chapters().clear();
    Chapter autoCh;
    autoCh.setTitle(QStringLiteral("Chapter 1: The Crash Event"));
    autoCh.setOrder(1);
    autoCh.setHtml(QStringLiteral("<p>Lines written right before power loss or unexpected shutdown...</p>"));
    autosaveProj.chapters().append(autoCh);

    const QString autosavePath = tempDir.filePath(QStringLiteral("in_progress_work.ao3projasve"));
    QString autoErr;
    assert(ProjectSerializer::save(autosavePath, autosaveProj, &autoErr));
    assert(QFile::exists(autosavePath));

    // Validate that 3-stage validation accepts and cleanly parses .ao3projasve archive
    Ao3Project recoveredProj;
    ProjectLoadReport recReport = ProjectSerializer::loadWithValidation(autosavePath, recoveredProj);
    assert(recReport.success);
    assert(recReport.formatType == ProjectFormatType::ModernArchiveZip ||
           recReport.formatType == ProjectFormatType::ModernArchiveZstd);
    assert(recoveredProj.title() == QStringLiteral("Unsaved In-Progress Masterpiece"));
    assert(recoveredProj.chapters().size() == 1);
    assert(recoveredProj.chapters().first().title() == QStringLiteral("Chapter 1: The Crash Event"));
    assert(recoveredProj.chapters().first().html().contains(QStringLiteral("power loss")));
    assert(recoveredProj.workSkinCss().contains(QStringLiteral(".autosave-style")));
    qDebug() << "✓ .ao3projasve emergency autosave format verified (valid magic bytes & structural recovery).";

    // Test companion recovery path calculation
    const QString mainProjPath = tempDir.filePath(QStringLiteral("my_story.ao3proj"));
    QString companionPath = mainProjPath;
    if (companionPath.endsWith(QStringLiteral(".ao3proj"), Qt::CaseInsensitive)) {
      companionPath.chop(8);
    }
    companionPath += QStringLiteral(".ao3projasve");
    assert(companionPath == tempDir.filePath(QStringLiteral("my_story.ao3projasve")));
    qDebug() << "✓ Companion .ao3projasve file path resolution verified.";
  }

  // 13. Test agentic-datas attribute, encrypted agentifiers, 5MB shared & 20MB localized caps
  {
    Ao3Project proj;
    proj.setTitle(QStringLiteral("Agentic Collaborative Story"));
    proj.setAuthor(QStringLiteral("Antigravity & Claude"));

    AgenticData &agentic = proj.agenticData();

    // A. Test shared storage & 5MB cap
    assert(agentic.totalSharedBytes() == 0);
    const QByteArray smallShared = "Shared co-author plot outline and world lore.";
    QString err;
    assert(agentic.setSharedFile(QStringLiteral("plot_outline.md"), smallShared, &err));
    assert(agentic.hasSharedFile(QStringLiteral("plot_outline.md")));
    assert(agentic.sharedFile(QStringLiteral("plot_outline.md")) == smallShared);
    assert(agentic.totalSharedBytes() == smallShared.size());

    // Try to exceed 5MB shared cap
    const QByteArray oversizedShared(5 * 1024 * 1024 + 1024, 'X');
    QString quotaErr;
    assert(!agentic.setSharedFile(QStringLiteral("huge_data.bin"), oversizedShared, &quotaErr));
    assert(quotaErr.contains(QStringLiteral("quota exceeded"), Qt::CaseInsensitive));
    assert(quotaErr.contains(QStringLiteral("5 MB")));
    qDebug() << "✓ 5 MB shared agent data quota limit verified.";

    // B. Test localized agent storage & 20MB cap
    assert(agentic.totalLocalizedBytes() == 0);
    const QByteArray claudeMemory = "{\"last_review\": \"chapter 1 paced well\", \"notes\": \"expand dialogue\"}";
    assert(agentic.setAgentFile(QStringLiteral("claude"), QStringLiteral("memory.json"), claudeMemory, &err));
    assert(agentic.hasAgentFile(QStringLiteral("claude"), QStringLiteral("memory.json")));
    assert(agentic.agentFile(QStringLiteral("claude"), QStringLiteral("memory.json")) == claudeMemory);

    const QByteArray antigravityMemory = "{\"character_arcs\": {\"hero\": \"transformative\"}}";
    assert(agentic.setAgentFile(QStringLiteral("antigravity"), QStringLiteral("brain.json"), antigravityMemory, &err));
    assert(agentic.hasAgentFile(QStringLiteral("antigravity"), QStringLiteral("brain.json")));
    assert(agentic.agentNames().contains(QStringLiteral("claude")));
    assert(agentic.agentNames().contains(QStringLiteral("antigravity")));

    // Try to exceed 20MB localized cap
    const QByteArray oversizedAgent(20 * 1024 * 1024 + 1024, 'Z');
    QString locQuotaErr;
    assert(!agentic.setAgentFile(QStringLiteral("cursor"), QStringLiteral("big.bin"), oversizedAgent, &locQuotaErr));
    assert(locQuotaErr.contains(QStringLiteral("quota exceeded"), Qt::CaseInsensitive));
    assert(locQuotaErr.contains(QStringLiteral("20 MB")));
    qDebug() << "✓ 20 MB localized agent data quota limit verified.";

    // C. Test agent_model_history.xml
    agentic.appendHistoryEntry(QStringLiteral("antigravity"), QStringLiteral("gemini-2.5-pro"),
                               QStringLiteral("co-author"), QStringLiteral("init"),
                               QStringLiteral("Drafted chapter 1 premise."));
    agentic.appendHistoryEntry(QStringLiteral("claude"), QStringLiteral("claude-3-7-sonnet"),
                               QStringLiteral("reviewer"), QStringLiteral("review"),
                               QStringLiteral("Reviewed chapter 1 pacing and character voice."));
    const QString historyXml = agentic.rawModelHistoryXml();
    assert(historyXml.contains(QStringLiteral("<agent-model-history")));
    assert(historyXml.contains(QStringLiteral("gemini-2.5-pro")));
    assert(historyXml.contains(QStringLiteral("claude-3-7-sonnet")));
    assert(agentic.historyEntries().size() >= 3);
    qDebug() << "✓ agent_model_history.xml generation and parsing verified.";

    // D. Test encrypted agentifiers.json
    agentic.registerAgentIdentifier(QStringLiteral("antigravity"), QStringLiteral("gemini"), QStringLiteral("co-author"), QStringLiteral("Primary architect"));
    agentic.registerAgentIdentifier(QStringLiteral("claude"), QStringLiteral("anthropic"), QStringLiteral("reviewer"), QStringLiteral("Style editor"));
    const QByteArray encryptedAgentifiers = agentic.exportEncryptedAgentifiers();
    assert(!encryptedAgentifiers.isEmpty());
    assert(encryptedAgentifiers.contains("aes-256-cbc-hmac-sha256"));
    assert(!encryptedAgentifiers.contains("Primary architect")); // Must not be cleartext!

    // Verify decryption roundtrip
    AgenticData decryptTarget;
    QString decErr;
    assert(decryptTarget.importEncryptedAgentifiers(encryptedAgentifiers, QByteArray(), &decErr));
    QJsonObject decObj = decryptTarget.agentifiersJson();
    assert(decObj.value(QStringLiteral("format")).toString() == QStringLiteral("ensemble-agentifiers-v1"));
    assert(decObj.value(QStringLiteral("agents")).toObject().contains(QStringLiteral("antigravity")));
    assert(decObj.value(QStringLiteral("agents")).toObject().contains(QStringLiteral("claude")));
    qDebug() << "✓ agentifiers.json AES-256 authenticated encryption & decryption verified.";

    // E. Save full project archive to disk and verify all agentic container entries
    const QString agenticProjPath = tempDir.filePath(QStringLiteral("agentic_story.ao3proj"));
    QString saveErr;
    assert(ProjectSerializer::save(agenticProjPath, proj, &saveErr));
    assert(QFile::exists(agenticProjPath));

    // Decompress and check archive files directly
    QMap<QString, QByteArray> rawEntries;
    QString decArchErr;
    assert(ZstdArchive::decompressArchive(agenticProjPath, rawEntries, &decArchErr));
    assert(rawEntries.contains(QStringLiteral("agentic-datas/index.json")));
    assert(rawEntries.contains(QStringLiteral("agentic-datas/agent_model_history.xml")));
    assert(rawEntries.contains(QStringLiteral("agentic-datas/agentifiers.json")));
    assert(rawEntries.contains(QStringLiteral("agentic-datas/shared/plot_outline.md")));
    assert(rawEntries.contains(QStringLiteral("agentic-datas/claude/memory.json")));
    assert(rawEntries.contains(QStringLiteral("agentic-datas/antigravity/brain.json")));
    assert(rawEntries.value(QStringLiteral("agentic-datas/shared/plot_outline.md")) == smallShared);
    assert(rawEntries.value(QStringLiteral("agentic-datas/claude/memory.json")) == claudeMemory);
    assert(rawEntries.value(QStringLiteral("agentic-datas/antigravity/brain.json")) == antigravityMemory);

    // Verify metadata.xml contains <agentic-datas> tag
    assert(rawEntries.contains(QStringLiteral("metadata.xml")));
    assert(rawEntries.value(QStringLiteral("metadata.xml")).contains("<agentic-datas"));
    qDebug() << "✓ .ao3proj archive container validated with all agentic-datas contents & structure.";

    // F. Load with validation pipeline (Stages 1-3)
    Ao3Project reloadedProj;
    ProjectLoadReport loadReport = ProjectSerializer::loadWithValidation(agenticProjPath, reloadedProj);
    assert(loadReport.success);
    assert(reloadedProj.title() == QStringLiteral("Agentic Collaborative Story"));
    assert(reloadedProj.agenticData().hasSharedFile(QStringLiteral("plot_outline.md")));
    assert(reloadedProj.agenticData().sharedFile(QStringLiteral("plot_outline.md")) == smallShared);
    assert(reloadedProj.agenticData().hasAgentFile(QStringLiteral("claude"), QStringLiteral("memory.json")));
    assert(reloadedProj.agenticData().agentFile(QStringLiteral("claude"), QStringLiteral("memory.json")) == claudeMemory);
    assert(reloadedProj.agenticData().hasAgentFile(QStringLiteral("antigravity"), QStringLiteral("brain.json")));
    assert(reloadedProj.agenticData().agentFile(QStringLiteral("antigravity"), QStringLiteral("brain.json")) == antigravityMemory);
    assert(reloadedProj.agenticData().historyEntries().size() >= 3);
    qDebug() << "✓ ProjectSerializer::loadWithValidation successfully restored all agentic data.";
  }

  // 14. Test Lore Graph, unique .grph magic bytes, AO3 tags, and lore/ directory structure
  {
    Ao3Project loreProj;
    loreProj.setTitle(QStringLiteral("Lore & Worldbuilding Masterpiece"));
    loreProj.setAuthor(QStringLiteral("Author & Co-Authors"));
    loreProj.setRating(QStringLiteral("Teen And Up Audiences"));
    loreProj.archiveWarnings() = QStringList{QStringLiteral("Creator Chose Not To Use Archive Warnings")};
    loreProj.categories() = QStringList{QStringLiteral("Gen"), QStringLiteral("M/M")};
    loreProj.addFandom(QStringLiteral("Original Fiction"));
    loreProj.addRelationship(QStringLiteral("Hero/Rival"));
    loreProj.addCharacter(QStringLiteral("Hero"));
    loreProj.addCharacter(QStringLiteral("Rival"));
    loreProj.addTag(QStringLiteral("Worldbuilding"));
    loreProj.addTag(QStringLiteral("Plot Twists"));

    LoreManager &lm = loreProj.lore();
    lm.ensureDefaults();

    // A. Verify .grph Unique Magic Bytes
    const QByteArray grphMagic = LoreGraph::magicBytes();
    assert(grphMagic.size() == 8);
    assert(static_cast<unsigned char>(grphMagic[0]) == 0x89);
    assert(grphMagic[1] == 'G' && grphMagic[2] == 'R' && grphMagic[3] == 'P' && grphMagic[4] == 'H');
    assert(grphMagic[5] == 0x01);
    assert(grphMagic[6] == '\r' && grphMagic[7] == '\n');
    assert(LoreGraph::hasValidMagic(grphMagic));

    // Invalid magic tests
    assert(!LoreGraph::hasValidMagic(QByteArray("MERMAID;")));
    assert(!LoreGraph::hasValidMagic(QByteArray("\x89PNG\r\n\x1a\n")));
    qDebug() << "✓ .grph unique 8-byte magic header (\\x89GRPH\\x01\\r\\n) verified.";

    // B. Test .grph DSL composition & parsing
    LoreGraph testGraph;
    testGraph.setTitle(QStringLiteral("Epic Narrative Arc"));
    testGraph.setDirection(QStringLiteral("LR"));

    LoreGraphNode n1;
    n1.id = QStringLiteral("hero");
    n1.label = QStringLiteral("The Chosen Hero");
    n1.type = QStringLiteral("character");
    n1.x = 100.0;
    n1.y = 150.0;
    n1.color = QStringLiteral("#3b82f6");
    n1.shape = QStringLiteral("rounded");
    n1.loreId = QStringLiteral("char_hero");
    testGraph.addOrUpdateNode(n1);

    LoreGraphNode n2;
    n2.id = QStringLiteral("rival");
    n2.label = QStringLiteral("The Dark Rival");
    n2.type = QStringLiteral("character");
    n2.x = 500.0;
    n2.y = 150.0;
    n2.color = QStringLiteral("#ef4444");
    n2.shape = QStringLiteral("diamond");
    testGraph.addOrUpdateNode(n2);

    testGraph.connectNodes(QStringLiteral("hero"), QStringLiteral("rival"),
                          QStringLiteral("-x"), QStringLiteral("rivalry & conflict"),
                          QStringLiteral("dashed"), QStringLiteral("#ef4444"));

    LoreGraphBox factionBox;
    factionBox.id = QStringLiteral("academy_box");
    factionBox.label = QStringLiteral("Royal Knight Academy");
    factionBox.x = 80.0;
    factionBox.y = 100.0;
    factionBox.width = 600.0;
    factionBox.height = 300.0;
    factionBox.color = QStringLiteral("#8b5cf6");
    factionBox.memberNodeIds = QStringList{QStringLiteral("hero"), QStringLiteral("rival")};
    testGraph.addOrUpdateBox(factionBox);

    LoreGraphFollowup fu;
    fu.nodeId = QStringLiteral("rival");
    fu.targetChapter = 3;
    fu.status = QStringLiteral("pending");
    fu.note = QStringLiteral("Reveal hidden backstory in tournament arc.");
    testGraph.addOrUpdateFollowup(fu);

    const QString dsl = testGraph.toDsl();
    assert(dsl.contains(QStringLiteral("graph \"Epic Narrative Arc\"")));
    assert(dsl.contains(QStringLiteral("node hero")));
    assert(dsl.contains(QStringLiteral("pos(100, 150)")));
    assert(dsl.contains(QStringLiteral("hero -x rival")));
    assert(dsl.contains(QStringLiteral("box academy_box")));
    assert(dsl.contains(QStringLiteral("followup rival")));

    // Test roundtrip parsing from DSL
    LoreGraph parsedGraph;
    QString parseErr;
    assert(parsedGraph.fromDsl(dsl, &parseErr));
    assert(parsedGraph.title() == QStringLiteral("Epic Narrative Arc"));
    assert(parsedGraph.nodes().size() == 2);
    assert(parsedGraph.connectors().size() == 1);
    assert(parsedGraph.connectors().first().arrowType == QStringLiteral("-x"));
    assert(parsedGraph.boxes().size() == 1);
    assert(parsedGraph.followups().size() == 1);
    assert(parsedGraph.findNode(QStringLiteral("hero")) != nullptr);
    assert(parsedGraph.findNode(QStringLiteral("hero"))->x == 100.0);
    assert(parsedGraph.findNode(QStringLiteral("hero"))->y == 150.0);
    qDebug() << "✓ .grph custom DSL parser & serializer roundtrip verified.";

    // C. Test binary .grph file bytes (magic header + DSL)
    const QByteArray grphBytes = testGraph.toGrphBytes();
    assert(grphBytes.startsWith(grphMagic));
    LoreGraph fromBytesGraph;
    QString bytesErr;
    assert(fromBytesGraph.fromGrphBytes(grphBytes, &bytesErr));
    assert(fromBytesGraph.nodes().size() == 2);
    qDebug() << "✓ .grph binary packaging with magic bytes validated.";

    // D. Test moving blocks updates pos(x, y)
    parsedGraph.updateNodePosition(QStringLiteral("hero"), 250.0, 350.0);
    assert(parsedGraph.findNode(QStringLiteral("hero"))->x == 250.0);
    assert(parsedGraph.findNode(QStringLiteral("hero"))->y == 350.0);
    const QString movedDsl = parsedGraph.toDsl();
    assert(movedDsl.contains(QStringLiteral("pos(250, 350)")));
    qDebug() << "✓ Movable node block coordinates update confirmed.";

    // E. Populate Lore Manager with entities, history, conflicts, plans, references
    LoreEntity ent;
    ent.id = QStringLiteral("char_hero");
    ent.name = QStringLiteral("The Chosen Hero");
    ent.category = QStringLiteral("Character");
    ent.description = QStringLiteral("Protagonist seeking redemption.");
    ent.tags = QStringList{QStringLiteral("canon"), QStringLiteral("magic")};
    lm.addOrUpdateEntity(ent);

    LoreHistoryEvent hist;
    hist.id = QStringLiteral("hist_cataclysm");
    hist.title = QStringLiteral("The Great Cataclysm");
    hist.eraOrDate = QStringLiteral("100 years prior");
    hist.summary = QStringLiteral("Shattered the ancient empire into seven kingdoms.");
    lm.addOrUpdateHistoryEvent(hist);

    LoreConflict conf;
    conf.id = QStringLiteral("conf_magic_system");
    conf.title = QStringLiteral("Mana Consumption Inconsistency");
    conf.severity = QStringLiteral("medium");
    conf.chapterA = 1;
    conf.chapterB = 4;
    conf.description = QStringLiteral("Chapter 1 says spells require gems, Chapter 4 uses spoken runes.");
    lm.addOrUpdateConflict(conf);

    lm.resolveConflict(QStringLiteral("conf_magic_system"),
                       QStringLiteral("Gems amplify spoken runes; both are canonically valid."),
                       QStringLiteral("Dual-channel spellcasting system"));

    LorePlan plan;
    plan.id = QStringLiteral("plan_betrayal");
    plan.title = QStringLiteral("Mentor's Betrayal");
    plan.targetChapter = 7;
    plan.status = QStringLiteral("planned");
    plan.description = QStringLiteral("Foreshadow betrayal in Chapter 5, execute in Chapter 7.");
    lm.addOrUpdatePlan(plan);

    LoreReference ref;
    ref.id = QStringLiteral("ref_magic_rules");
    ref.title = QStringLiteral("World Codex Vol 1");
    ref.source = QStringLiteral("Author Notes");
    ref.citation = QStringLiteral("Page 42");
    lm.addOrUpdateReference(ref);

    lm.defaultGraph() = testGraph;

    // F. Save project to .ao3proj archive
    const QString loreProjPath = tempDir.filePath(QStringLiteral("lore_story.ao3proj"));
    QString saveErr;
    assert(ProjectSerializer::save(loreProjPath, loreProj, &saveErr));
    assert(QFile::exists(loreProjPath));

    // G. Verify all 7 required files in lore/ directory plus lore/graphs/data.grph
    QMap<QString, QByteArray> rawEntries;
    QString archErr;
    assert(ZstdArchive::decompressArchive(loreProjPath, rawEntries, &archErr));

    assert(rawEntries.contains(QStringLiteral("lore/lore_data.json")));
    assert(rawEntries.contains(QStringLiteral("lore/history.json")));
    assert(rawEntries.contains(QStringLiteral("lore/conflicts.json")));
    assert(rawEntries.contains(QStringLiteral("lore/resolved.json")));
    assert(rawEntries.contains(QStringLiteral("lore/plans.json")));
    assert(rawEntries.contains(QStringLiteral("lore/references.json")));
    assert(rawEntries.contains(QStringLiteral("lore/metadata.xml")));
    assert(rawEntries.contains(QStringLiteral("lore/graphs/data.grph")));

    // Verify .grph in archive starts with valid magic bytes
    const QByteArray archivedGrphBytes = rawEntries.value(QStringLiteral("lore/graphs/data.grph"));
    assert(LoreGraph::hasValidMagic(archivedGrphBytes));

    // Verify metadata.xml contains AO3 tags and lore manifest
    const QByteArray metaBytes = rawEntries.value(QStringLiteral("metadata.xml"));
    assert(metaBytes.contains("<rating>Teen And Up Audiences</rating>"));
    assert(metaBytes.contains("<warning>Creator Chose Not To Use Archive Warnings</warning>"));
    assert(metaBytes.contains("<category>Gen</category>"));
    assert(metaBytes.contains("<category>M/M</category>"));
    assert(metaBytes.contains("<fandom>Original Fiction</fandom>"));
    assert(metaBytes.contains("<relationship>Hero/Rival</relationship>"));
    assert(metaBytes.contains("<character>Hero</character>"));
    assert(metaBytes.contains("<character>Rival</character>"));
    assert(metaBytes.contains("<tag>Worldbuilding</tag>"));
    assert(metaBytes.contains("<lore enabled=\"true\""));
    qDebug() << "✓ .ao3proj container verified with all 7 lore files, .grph graph, and AO3 metadata tags.";

    // H. Reload project via 3-stage validation pipeline
    Ao3Project reloaded;
    ProjectLoadReport rep = ProjectSerializer::loadWithValidation(loreProjPath, reloaded);
    assert(rep.success);
    assert(reloaded.title() == QStringLiteral("Lore & Worldbuilding Masterpiece"));
    assert(reloaded.rating() == QStringLiteral("Teen And Up Audiences"));
    assert(reloaded.archiveWarnings().contains(QStringLiteral("Creator Chose Not To Use Archive Warnings")));
    assert(reloaded.categories().contains(QStringLiteral("Gen")));
    assert(reloaded.fandoms().contains(QStringLiteral("Original Fiction")));
    assert(reloaded.relationships().contains(QStringLiteral("Hero/Rival")));
    assert(reloaded.characters().contains(QStringLiteral("Hero")));
    assert(reloaded.tags().contains(QStringLiteral("Worldbuilding")));

    // Check restored LoreManager state
    const LoreManager &reloadedLore = reloaded.lore();
    assert(reloadedLore.entities().size() >= 1);
    assert(reloadedLore.findEntity(QStringLiteral("char_hero")) != nullptr);
    assert(reloadedLore.historyEvents().size() >= 1);
    assert(reloadedLore.resolvedConflicts().size() >= 1);
    assert(reloadedLore.plans().size() >= 1);
    assert(reloadedLore.references().size() >= 1);
    assert(reloadedLore.defaultGraph().nodes().size() == 2);
    assert(reloadedLore.defaultGraph().connectors().size() == 1);
    assert(reloadedLore.defaultGraph().boxes().size() == 1);
    assert(reloadedLore.defaultGraph().followups().size() == 1);
    qDebug() << "✓ ProjectSerializer successfully roundtripped complete lore repository & graph.";
  }

  // 12. Test loading legacy/older .ao3proj archive created before modern features
  {
    const QString legacyArchiveProjPath = tempDir.filePath(QStringLiteral("legacy_bare_archive.ao3proj"));
    QMap<QString, QByteArray> bareEntries;
    bareEntries.insert(QStringLiteral("metadata.xml"), QByteArray(
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<project>\n"
      "  <title>Old Archive Story</title>\n"
      "  <author>Retro Author</author>\n"
      "  <chapter id=\"1\" order=\"0\" file=\"chapters/ch1.html\">Beginning</chapter>\n"
      "</project>\n"
    ));
    bareEntries.insert(QStringLiteral("chapters/ch1.html"), QByteArray("<p>Chapter content from old archive.</p>"));
    // Notice: NO history.cmt, NO agentic-datas/, NO lore/
    QString compErr;
    assert(ZstdArchive::compressArchive(legacyArchiveProjPath, bareEntries, &compErr));
    assert(QFile::exists(legacyArchiveProjPath));

    Ao3Project legacyProj;
    ProjectLoadReport rep = ProjectSerializer::loadWithValidation(legacyArchiveProjPath, legacyProj);
    assert(rep.success);
    assert(legacyProj.title() == QStringLiteral("Old Archive Story"));
    assert(legacyProj.author() == QStringLiteral("Retro Author"));
    assert(legacyProj.chapters().size() == 1);
    assert(legacyProj.chapters().at(0).title() == QStringLiteral("Beginning"));
    assert(legacyProj.chapters().at(0).html() == QStringLiteral("<p>Chapter content from old archive.</p>"));
    // Baseline commit created automatically
    assert(!legacyProj.historyLog().trimmed().isEmpty());
    assert(legacyProj.historyLog().contains(QStringLiteral("Initial project import / baseline commit")));
    // Lore defaults initialized
    assert(!legacyProj.lore().defaultGraph().title().isEmpty());
    // Agentic data defaults initialized
    assert(!legacyProj.agenticData().historyEntries().isEmpty());
    qDebug() << "✓ Successfully loaded older bare .ao3proj archive lacking modern lore/agentic data with automatic baseline defaults.";
  }

  // 17. Test Snapshots: history/ directory in .ao3proj, diff generation, and author tagging
  {
    Ao3Project proj;
    proj.resetToNew();
    proj.setTitle(QStringLiteral("Snapshot Test Story"));
    proj.setAuthor(QStringLiteral("TestAuthor"));
    assert(!proj.snapshotDiffs().isEmpty());

    // Modify chapter 1 and add chapter 2
    proj.chapters()[0].setHtml(QStringLiteral("<p>Modified chapter line 1</p>\n<p>Added line 2</p>"));
    proj.addChapter(QStringLiteral("Chapter 2"));
    proj.chapters()[1].setHtml(QStringLiteral("<p>Brand new chapter 2 content</p>"));

    // Create snapshot
    const QString newHash = proj.appendHistoryCommit(QString(), QStringLiteral("Add chapter 2 and modify chapter 1"));
    assert(!newHash.isEmpty());
    assert(proj.snapshotDiffs().contains(newHash));
    const QString diff = proj.snapshotDiff(newHash);
    assert(diff.contains(QStringLiteral("--- a/chapters/")));
    assert(diff.contains(QStringLiteral("+++ b/chapters/")));
    assert(diff.contains(QStringLiteral("+<p>Added line 2</p>")));
    assert(diff.contains(QStringLiteral("# Snapshot:")));
    assert(diff.contains(QStringLiteral("@"))); // Author tag contains host tag

    // Save project archive and verify history/<hash>.diff is saved
    const QString snapProjPath = tempDir.filePath(QStringLiteral("snap_test.ao3proj"));
    QString saveErr;
    assert(ProjectSerializer::save(snapProjPath, proj, &saveErr));
    assert(QFile::exists(snapProjPath));

    // Inspect archive entries
    QMap<QString, QByteArray> entries;
    QString decErr;
    assert(ZstdArchive::decompressArchive(snapProjPath, entries, &decErr));
    assert(entries.contains(QStringLiteral("history.cmt")));
    assert(entries.contains(QStringLiteral("history/%1.diff").arg(newHash)));

    // Reload archive into fresh project
    Ao3Project reloadedProj;
    ProjectLoadReport rep = ProjectSerializer::loadWithValidation(snapProjPath, reloadedProj);
    assert(rep.success);
    assert(reloadedProj.snapshotDiffs().contains(newHash));
    assert(reloadedProj.snapshotDiff(newHash) == diff);

    // Verify safe fallback on non-existent hash
    assert(reloadedProj.snapshotDiff(QStringLiteral("nonexistent12")).isEmpty());

    qDebug() << "✓ Verified history/ diff snapshots, author tags, and persistence in .ao3proj archive.";
  }

  qDebug() << "✓ All 3-Stage validation, magic byte verification, structural integrity, "
              "legacy compatibility, .ao3projasve recovery, agentic-datas, and Lore Graph tests passed successfully!";
  return 0;
}
