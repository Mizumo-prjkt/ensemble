#include "McpAgentInstaller.h"
#include "McpServer.h"
#include "McpToolDefinitions.h"
#include "debug/debug.hpp"
#include "utils/AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

static bool writeTextFile(const QString &filePath, const QString &content)
{
    QFileInfo fi(filePath);
    QDir().mkpath(fi.absolutePath());

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    file.write(content.toUtf8());
    file.close();
    return true;
}

static bool writeJsonFile(const QString &filePath, const QJsonObject &obj)
{
    QFileInfo fi(filePath);
    QDir().mkpath(fi.absolutePath());

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool McpAgentInstaller::syncServerConfig(
    const QString &configFilePath,
    const QString &executablePath,
    bool &wasOverwritten,
    bool &wasAdded,
    QString &oldPath)
{
    wasOverwritten = false;
    wasAdded = false;
    oldPath.clear();

    if (configFilePath.isEmpty() || executablePath.isEmpty()) {
        return false;
    }

    QFileInfo fi(configFilePath);
    if (!fi.dir().exists()) {
        return false;
    }

    QJsonObject rootObj;
    if (fi.exists()) {
        QFile file(configFilePath);
        if (file.open(QIODevice::ReadOnly)) {
            QJsonParseError parseErr;
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseErr);
            if (parseErr.error == QJsonParseError::NoError && doc.isObject()) {
                rootObj = doc.object();
            }
            file.close();
        }
    }

    QJsonObject mcpServers = rootObj.value(QStringLiteral("mcpServers")).toObject();
    const bool containsEnsemble = mcpServers.contains(QStringLiteral("ensemble"));

    QJsonArray expectedArgs;
    expectedArgs.append(QStringLiteral("--mcp-bridge"));

    if (containsEnsemble) {
        QJsonObject existingEntry = mcpServers.value(QStringLiteral("ensemble")).toObject();
        const QString existingCmd = existingEntry.value(QStringLiteral("command")).toString();
        const QJsonArray existingArgs = existingEntry.value(QStringLiteral("args")).toArray();

        const bool pathMatches = (existingCmd == executablePath);
        const bool binaryExists = QFile::exists(existingCmd);
        const bool argsMatch = (existingArgs == expectedArgs);

        if (!pathMatches || !binaryExists || !argsMatch) {
            oldPath = existingCmd;
            existingEntry[QStringLiteral("command")] = executablePath;
            existingEntry[QStringLiteral("args")] = expectedArgs;
            mcpServers[QStringLiteral("ensemble")] = existingEntry;
            rootObj[QStringLiteral("mcpServers")] = mcpServers;

            if (writeJsonFile(configFilePath, rootObj)) {
                wasOverwritten = true;
                qCInfo(logMcp) << "Updated / corrected Ensemble MCP entry in" << configFilePath
                               << "(old command:" << oldPath << "-> new command:" << executablePath << ")";
                return true;
            }
            return false;
        }

        // Already correct and pointing to active binary
        return true;
    } else {
        QJsonObject newEntry;
        newEntry[QStringLiteral("command")] = executablePath;
        newEntry[QStringLiteral("args")] = expectedArgs;
        mcpServers[QStringLiteral("ensemble")] = newEntry;
        rootObj[QStringLiteral("mcpServers")] = mcpServers;

        if (writeJsonFile(configFilePath, rootObj)) {
            wasAdded = true;
            qCInfo(logMcp) << "Registered Ensemble MCP entry in" << configFilePath
                           << "-> command:" << executablePath;
            return true;
        }
        return false;
    }
}

bool McpAgentInstaller::checkAndSyncAgentConfigs(
    const QString &executablePath,
    QStringList *modifiedFiles,
    QStringList *overwrittenFiles)
{
    QString exePath = executablePath;
    if (exePath.isEmpty()) {
        exePath = QCoreApplication::applicationFilePath();
    }

    const QString home = QDir::homePath();
    QStringList targets;

    // 1. Antigravity Global configuration
    targets.append(QDir(home).filePath(QStringLiteral(".gemini/config/mcp_config.json")));
    // 2. Antigravity app configuration
    targets.append(QDir(home).filePath(QStringLiteral(".gemini/antigravity/mcp_config.json")));

    // 3. Claude Desktop configuration
#if defined(Q_OS_WIN)
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    targets.append(QDir(appData).filePath(QStringLiteral("Claude/claude_desktop_config.json")));
#elif defined(Q_OS_MAC)
    targets.append(QDir(home).filePath(QStringLiteral("Library/Application Support/Claude/claude_desktop_config.json")));
#else
    targets.append(QDir(home).filePath(QStringLiteral(".config/Claude/claude_desktop_config.json")));
#endif

    // 4. Claude Code CLI configuration
    targets.append(QDir(home).filePath(QStringLiteral(".claude.json")));

    bool anyChanged = false;
    for (const QString &path : targets) {
        bool wasOverwritten = false;
        bool wasAdded = false;
        QString oldPath;
        if (syncServerConfig(path, exePath, wasOverwritten, wasAdded, oldPath)) {
            if (wasOverwritten) {
                anyChanged = true;
                if (overwrittenFiles) {
                    overwrittenFiles->append(QStringLiteral("%1 (was: %2)").arg(path, oldPath.isEmpty() ? QStringLiteral("<empty/missing>") : oldPath));
                }
            }
            if (wasAdded || wasOverwritten) {
                anyChanged = true;
                if (modifiedFiles) {
                    modifiedFiles->append(path);
                }
            }
        }
    }

    return anyChanged;
}

McpInstallReport McpAgentInstaller::installAll(const QString &executablePath, const QString &workspaceDir)
{
    McpInstallReport report;
    report.success = true;

    QString exePath = executablePath;
    if (exePath.isEmpty()) {
        exePath = QCoreApplication::applicationFilePath();
    }

    // 1. Generate & save ENSEMBLE_MCP_MANUAL.md
    const QString manualContent = generateManualMarkdown(exePath);

    // Save in workspace / current dir
    QString targetManualDir = workspaceDir;
    if (targetManualDir.isEmpty() || !QDir(targetManualDir).exists()) {
        targetManualDir = QDir::currentPath();
    }
    const QString manualFile = QDir(targetManualDir).filePath(QStringLiteral("ENSEMBLE_MCP_MANUAL.md"));
    if (writeTextFile(manualFile, manualContent)) {
        report.manualPath = manualFile;
        report.installedFiles.append(manualFile);
    }

    // Also place a copy in appConfigDir
    const QString appConfigManual = QDir(AppPaths::appConfigDir()).filePath(QStringLiteral("ENSEMBLE_MCP_MANUAL.md"));
    if (writeTextFile(appConfigManual, manualContent)) {
        report.installedFiles.append(appConfigManual);
    }

    // Also place a copy in ~/.claude if directory exists
    const QString claudeDir = QDir(QDir::homePath()).filePath(QStringLiteral(".claude"));
    if (QDir(claudeDir).exists()) {
        const QString claudeManual = QDir(claudeDir).filePath(QStringLiteral("ENSEMBLE_MCP_MANUAL.md"));
        if (writeTextFile(claudeManual, manualContent)) {
            report.installedFiles.append(claudeManual);
        }
    }

    // 2. Install into Google Antigravity MCP directory & configs
    QString agyGlobalConfig;
    QString agyToolDir;
    if (installAntigravityMcp(exePath, targetManualDir, report.installedFiles, report.overwrittenConfigs, agyGlobalConfig, agyToolDir)) {
        report.antigravityGlobalPath = agyGlobalConfig;
        report.antigravityMcpDir = agyToolDir;
    }

    // 3. Install / configure Claude Desktop
    QString claudeConfig;
    if (installClaudeDesktopConfig(exePath, claudeConfig, report.installedFiles, report.overwrittenConfigs)) {
        report.claudeConfigPath = claudeConfig;
    }

    // 4. Install / configure Claude Code CLI if ~/.claude.json exists
    QString claudeCodeConfig;
    if (installClaudeCodeConfig(exePath, claudeCodeConfig, report.installedFiles, report.overwrittenConfigs)) {
        report.claudeCodeConfigPath = claudeCodeConfig;
    }

    // Build summary
    QString summary;
    summary += QStringLiteral("========================================\n");
    summary += QStringLiteral(" Ensemble MCP Agent Setup Complete!\n");
    summary += QStringLiteral("========================================\n\n");
    summary += QStringLiteral("• Executable Path: %1\n").arg(exePath);
    summary += QStringLiteral("• MCP Bridge Flag: --mcp-bridge\n\n");
    summary += QStringLiteral("• MCP Agent Manual: %1\n").arg(report.manualPath);
    if (!report.antigravityGlobalPath.isEmpty()) {
        summary += QStringLiteral("• Google Antigravity Global Config: %1\n").arg(report.antigravityGlobalPath);
    }
    if (!report.antigravityMcpDir.isEmpty()) {
        summary += QStringLiteral("• Google Antigravity Tool Schemas: %1\n").arg(report.antigravityMcpDir);
    }
    if (!report.claudeConfigPath.isEmpty()) {
        summary += QStringLiteral("• Claude Desktop Config: %1\n").arg(report.claudeConfigPath);
    }
    if (!report.claudeCodeConfigPath.isEmpty()) {
        summary += QStringLiteral("• Claude Code CLI Config: %1\n").arg(report.claudeCodeConfigPath);
    }

    if (!report.overwrittenConfigs.isEmpty()) {
        summary += QStringLiteral("\n[Path Overwrites / Corrections Applied]:\n");
        for (const QString &item : report.overwrittenConfigs) {
            summary += QStringLiteral("  ✓ %1\n").arg(item);
        }
    }

    summary += QStringLiteral("\nTotal registered/synchronized files: %1\n").arg(report.installedFiles.size());
    summary += QStringLiteral("AI agents (Google Antigravity & Claude) can now automatically discover and interact with Ensemble!\n");

    report.summary = summary;
    qCInfo(logMcp) << "McpAgentInstaller completed successfully. Total files:" << report.installedFiles.size();
    return report;
}

bool McpAgentInstaller::installAntigravityMcp(
    const QString &executablePath,
    const QString &workspaceDir,
    QStringList &installedFiles,
    QStringList &overwrittenFiles,
    QString &outConfigPath,
    QString &outToolDir)
{
    const QString home = QDir::homePath();
    const QString baseAgyMcpDir = QDir(home).filePath(QStringLiteral(".gemini/antigravity/mcp"));
    const QString ensembleMcpDir = QDir(baseAgyMcpDir).filePath(QStringLiteral("ensemble"));
    QDir().mkpath(ensembleMcpDir);
    outToolDir = ensembleMcpDir;

    QStringList targetMcpDirs;
    targetMcpDirs.append(ensembleMcpDir);

    const QString ideMcpDir = QDir(home).filePath(QStringLiteral(".gemini/antigravity-ide/mcp/ensemble"));
    if (QDir(QDir(home).filePath(QStringLiteral(".gemini/antigravity-ide"))).exists()) {
        QDir().mkpath(ideMcpDir);
        targetMcpDirs.append(ideMcpDir);
    }

    const QString cliMcpDir = QDir(home).filePath(QStringLiteral(".gemini/antigravity-cli/mcp/ensemble"));
    if (QDir(QDir(home).filePath(QStringLiteral(".gemini/antigravity-cli"))).exists()) {
        QDir().mkpath(cliMcpDir);
        targetMcpDirs.append(cliMcpDir);
    }

    // 1. Write comprehensive instructions.md and ENSEMBLE_MCP_MANUAL.md in all MCP tool directories
    const QString manualMarkdown = generateManualMarkdown(executablePath);
    const QString instructionsText = QStringLiteral(
        "# Ensemble AO3 Writing Suite MCP Server Instructions\n\n"
        "You are interacting with **Ensemble**, an active AO3 desktop writing suite, work skin composer, and narrative lore mapping environment.\n\n"
        "## Key Capabilities & Workflow Recommendations\n\n"
        "### 1. Project State & Inspection\n"
        "- Before modifying or reading content, check project state using `get_project_summary`.\n"
        "- Inspect chapter list and word counts using `list_chapters`.\n"
        "- Retrieve chapter text or HTML with `get_chapter(chapterIndex, format=\"all\"|\"html\"|\"text\")`.\n"
        "- Inspect lint diagnostics and whitelist violations at any time using `get_problems`.\n\n"
        "### 2. Live Story Drafting & HTML Sanitization\n"
        "- Write story prose using `update_chapter_html(chapterIndex, html)`.\n"
        "  - Standard paragraphs should be formatted in `<p>...</p>`.\n"
        "  - All HTML is automatically sanitized against AO3's official allowed tags and attributes whitelist (`<p>`, `<em>`, `<strong>`, `<a>`, `<span>`, `<blockquote>`, `<ul>`, `<ol>`, `<li>`, `<h1>`-`<h6>`, `<img>`, etc.).\n"
        "  - Script tags, inline styles, or unsafe URL schemes (`javascript:`) are automatically stripped.\n"
        "  - Returns `wordCountDelta`, `strippedTags`, and sanitized HTML.\n"
        "- Append new chapters with `add_chapter(title)`.\n"
        "- Reorder chapters with `reorder_chapters(fromIndex, toIndex)`.\n"
        "- Delete chapters with `delete_chapter(chapterIndex)`.\n"
        "- To embed images in a chapter, use `insert_image_embed` (supports `url`, `alt`, `title`, `width`, `height`, `alignment=\"center\"|\"inline\"|\"left\"|\"right\"`, and `wrapInLink`).\n"
        "- List external images with `list_image_embeds`.\n\n"
        "### 3. Work Skin CSS Authoring\n"
        "- Inspect current stylesheet with `get_workskin_css`.\n"
        "- Update stylesheet with `set_workskin_css(css, allowMalformed=false)`.\n"
        "  - Validates CSS rules and classes. If syntax errors exist, they are rejected with line diagnostics unless `allowMalformed: true` is passed.\n"
        "  - Updates the live Chromium preview pane instantaneously.\n\n"
        "### 4. Story Lore & Worldbuilding Graphs (`.grph`)\n"
        "- Retrieve lore graph syntax with `get_lore_graph(name, includeGrammar=true)`.\n"
        "- Update narrative graphs using `update_lore_graph(dsl, name=\"data\")`.\n"
        "  - Must begin with `\\x89GRPH\\x01\\r\\n` (auto-prepended if missing).\n"
        "  - Graph syntax:\n"
        "    ```text\n"
        "    graph \"Title\" [direction=LR]\n"
        "    node id [label=\"Name\", type=\"character|event|location|faction|concept|lore\", pos(x, y), shape=\"rounded|box|circle|diamond\", color=\"#hex\", notes=\"...\"]\n"
        "    sourceId -> targetId [label=\"rel\", relation=\"causes\", style=\"solid|dashed\", color=\"#hex\"]\n"
        "    sourceId -x targetId [label=\"conflict\", relation=\"conflict\", style=\"dashed\", color=\"#hex\"]\n"
        "    box group [label=\"Box Title\", pos(x, y), size(w, h), color=\"#hex\"] { id1 id2 }\n"
        "    followup id [targetChapter=1, status=\"pending\", note=\"...\"]\n"
        "    ```\n"
        "  - Semantic validation is enforced: connectors must reference existing nodes, types/shapes/colors must be valid.\n"
        "- Use `add_lore_node`, `connect_lore_nodes`, and `move_lore_node` for granular updates without re-transmitting the entire graph.\n\n"
        "### 5. Narrative Lore Data Management\n"
        "- Read structured lore records using `get_lore_data(section=\"entities\"|\"history\"|\"conflicts\"|\"resolved\"|\"plans\"|\"references\"|\"all\")`.\n"
        "- Modify lore records using `manage_lore_data(action, ...)`:\n"
        "  - `add_entity` / `update_entity`: requires `id`, `name`, `type`, `description`, optional `attributes`.\n"
        "  - `add_history` / `update_history`: requires `id`, `summary`, `impact`, optional `timestamp`. (Do not use `description` for history events; use `summary` and `impact`).\n"
        "  - `add_conflict` / `update_conflict`: requires `id`, `title`, `description`.\n"
        "  - `resolve_conflict`: requires `id`, `ruling`.\n"
        "  - `add_plan` / `update_plan`: requires `id`, `title`, `loreThread`, `targetChapter`, `status`.\n"
        "  - `add_reference` / `update_reference`: requires `id`, `source`, `citation`, `notes`.\n"
        "  - `remove_entity`, `remove_history`, `remove_conflict`, `remove_resolved`, `remove_plan`, `remove_reference`: requires `id`.\n"
        "  - `remove_graph`: requires `name` (deleting the default graph resets it to a clean empty graph).\n\n"
        "### 6. Persistence & Version History\n"
        "- Save changes to disk with `save_project(commitMessage, author, filePath, saveCopy=false)`.\n"
        "  - Note: If `filePath` is provided without `saveCopy=true`, the active project switches to that file. Pass `saveCopy: true` to export an archive copy without switching active project.\n"
        "- Revert unsaved edits with `reload_project`.\n"
        "- Inspect git-like commit history with `get_project_history(limit)`.\n"
        "- Commit manual checkpoints with `add_history_commit(message, author)`.\n\n"
        "### 7. Multi-Agent Co-Working & Audit Trail\n"
        "- Discover connected agents with `get_agent_presence`.\n"
        "- Announce your presence and role with `register_agent_presence(agentName, role, workingNote)`.\n"
        "- Broadcast thinking or pacing suggestions live to the user's Agent Thoughts Monitor with `post_agent_thought(thought, category, target)`.\n"
        "- Inspect the agent audit log with `get_agent_history(limit, agentName, action)`.\n"
        "  - Records all chapter edits, lore updates, commits, file operations, and word count deltas with model identity and timestamps.\n"
        "- Store multi-agent shared notes or private agent memory using `read_agentic_file` and `write_agentic_file` (shared cap 5 MB, private cap 20 MB).\n\n"
        "### 8. Diagnostics\n"
        "- If connection drops or socket is unreachable, call `test_firewall_connection` to diagnose local loopback latency.\n"
    );

    for (const QString &mcpDir : targetMcpDirs) {
        const QString instrPath = QDir(mcpDir).filePath(QStringLiteral("instructions.md"));
        if (writeTextFile(instrPath, instructionsText)) {
            installedFiles.append(instrPath);
        }
        const QString manualPath = QDir(mcpDir).filePath(QStringLiteral("ENSEMBLE_MCP_MANUAL.md"));
        if (writeTextFile(manualPath, manualMarkdown)) {
            installedFiles.append(manualPath);
        }
    }

    // 2. Individual tool schema files for all 34 tools
    const QJsonArray tools = getAllToolDefinitions();
    for (const QJsonValue &toolVal : tools) {
        const QJsonObject toolObj = toolVal.toObject();
        const QString toolName = toolObj.value(QStringLiteral("name")).toString();
        const QString desc = toolObj.value(QStringLiteral("description")).toString();
        const QJsonObject inputSchema = toolObj.value(QStringLiteral("inputSchema")).toObject();

        QJsonObject schema;
        schema[QStringLiteral("name")] = toolName;
        schema[QStringLiteral("description")] = desc;
        schema[QStringLiteral("parameters")] = inputSchema;

        for (const QString &mcpDir : targetMcpDirs) {
            const QString schemaPath = QDir(mcpDir).filePath(QStringLiteral("%1.json").arg(toolName));
            if (writeJsonFile(schemaPath, schema)) {
                installedFiles.append(schemaPath);
            }
        }
    }

    // 3. Install Antigravity Plugin & Skill
    const QString pluginDir = QDir(home).filePath(QStringLiteral(".gemini/config/plugins/ensemble"));
    QDir().mkpath(pluginDir);

    QJsonObject pluginObj;
    pluginObj[QStringLiteral("name")] = QStringLiteral("ensemble");
    pluginObj[QStringLiteral("displayName")] = QStringLiteral("Ensemble");
    pluginObj[QStringLiteral("version")] = QStringLiteral("1.2.0");
    pluginObj[QStringLiteral("description")] = QStringLiteral("Desktop writing suite, AO3 work skin composer, narrative lore graph manager, and multi-model agentic collaborative writing environment.");
    pluginObj[QStringLiteral("suggestedPrompts")] = QJsonArray{
        QStringLiteral("Check the active Ensemble story project summary and word count"),
        QStringLiteral("List chapters and read the latest chapter draft in Ensemble"),
        QStringLiteral("Update chapter HTML with automated AO3 tag whitelist sanitization"),
        QStringLiteral("Inspect and tune the custom AO3 workskin CSS in Ensemble"),
        QStringLiteral("Query and update story lore graphs and narrative entities in Ensemble")
    };
    QJsonObject authorObj;
    authorObj[QStringLiteral("name")] = QStringLiteral("Ensemble Team");
    pluginObj[QStringLiteral("author")] = authorObj;
    pluginObj[QStringLiteral("keywords")] = QJsonArray{
        QStringLiteral("ensemble"),
        QStringLiteral("ao3"),
        QStringLiteral("fanfiction"),
        QStringLiteral("writing"),
        QStringLiteral("archiveofourown"),
        QStringLiteral("lore"),
        QStringLiteral("workskin"),
        QStringLiteral("css"),
        QStringLiteral("mcp")
    };

    const QString pluginJsonPath = QDir(pluginDir).filePath(QStringLiteral("plugin.json"));
    if (writeJsonFile(pluginJsonPath, pluginObj)) {
        installedFiles.append(pluginJsonPath);
    }

    const QString skillContent = QStringLiteral(
        "---\n"
        "name: ensemble-mcp\n"
        "description: Comprehensive reference and guide for Ensemble's Model Context Protocol (MCP) server. Use when querying project state, editing chapters, updating work skins, validating AO3 HTML/CSS, managing lore graphs and entities, logging agent audits, or interacting with the live Ensemble AO3 writing suite.\n"
        "---\n\n"
    ) + manualMarkdown;

    QStringList skillPaths;
    skillPaths.append(QDir(pluginDir).filePath(QStringLiteral("skills/ensemble-mcp/SKILL.md")));
    skillPaths.append(QDir(home).filePath(QStringLiteral(".gemini/config/skills/ensemble-mcp/SKILL.md")));
    if (QDir(QDir(home).filePath(QStringLiteral(".gemini/skills"))).exists()) {
        skillPaths.append(QDir(home).filePath(QStringLiteral(".gemini/skills/ensemble-mcp/SKILL.md")));
    }
    if (QDir(QDir(home).filePath(QStringLiteral(".gemini/antigravity"))).exists()) {
        skillPaths.append(QDir(home).filePath(QStringLiteral(".gemini/antigravity/skills/ensemble-mcp/SKILL.md")));
    }
    if (QDir(QDir(home).filePath(QStringLiteral(".gemini/antigravity-cli"))).exists()) {
        skillPaths.append(QDir(home).filePath(QStringLiteral(".gemini/antigravity-cli/skills/ensemble-mcp/SKILL.md")));
    }

    for (const QString &sp : skillPaths) {
        if (writeTextFile(sp, skillContent)) {
            installedFiles.append(sp);
        }
    }

    // 3. Register / update in ~/.gemini/config/mcp_config.json (Global Antigravity config)
    const QString agyGlobalConfig = QDir(home).filePath(QStringLiteral(".gemini/config/mcp_config.json"));
    bool wasOverwritten = false;
    bool wasAdded = false;
    QString oldPath;
    if (syncServerConfig(agyGlobalConfig, executablePath, wasOverwritten, wasAdded, oldPath)) {
        outConfigPath = agyGlobalConfig;
        installedFiles.append(agyGlobalConfig);
        if (wasOverwritten) {
            overwrittenFiles.append(QStringLiteral("Antigravity Global (%1): corrected outdated path '%2'").arg(agyGlobalConfig, oldPath));
        }
    }

    // 4. Register / update in ~/.gemini/antigravity/mcp_config.json
    const QString agyAppConfig = QDir(home).filePath(QStringLiteral(".gemini/antigravity/mcp_config.json"));
    if (syncServerConfig(agyAppConfig, executablePath, wasOverwritten, wasAdded, oldPath)) {
        installedFiles.append(agyAppConfig);
        if (wasOverwritten) {
            overwrittenFiles.append(QStringLiteral("Antigravity App (%1): corrected outdated path '%2'").arg(agyAppConfig, oldPath));
        }
    }

    // 5. Workspace registration if workspace specified
    if (!workspaceDir.isEmpty() && QDir(workspaceDir).exists()) {
        const QString wsAgy = QDir(workspaceDir).filePath(QStringLiteral(".gemini/antigravity/mcp_config.json"));
        if (QDir(QFileInfo(wsAgy).path()).exists()) {
            if (syncServerConfig(wsAgy, executablePath, wasOverwritten, wasAdded, oldPath)) {
                installedFiles.append(wsAgy);
            }
        }
        const QString wsAgents = QDir(workspaceDir).filePath(QStringLiteral(".agents/mcp_config.json"));
        if (QDir(QFileInfo(wsAgents).path()).exists()) {
            if (syncServerConfig(wsAgents, executablePath, wasOverwritten, wasAdded, oldPath)) {
                installedFiles.append(wsAgents);
            }
        }
    }

    return true;
}

bool McpAgentInstaller::installClaudeDesktopConfig(
    const QString &executablePath,
    QString &outConfigPath,
    QStringList &installedFiles,
    QStringList &overwrittenFiles)
{
#if defined(Q_OS_WIN)
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString configDir = QDir(appData).filePath(QStringLiteral("Claude"));
#elif defined(Q_OS_MAC)
    const QString configDir = QDir(QDir::homePath()).filePath(QStringLiteral("Library/Application Support/Claude"));
#else
    const QString configDir = QDir(QDir::homePath()).filePath(QStringLiteral(".config/Claude"));
#endif

    const QString configPath = QDir(configDir).filePath(QStringLiteral("claude_desktop_config.json"));
    outConfigPath = configPath;

    bool wasOverwritten = false;
    bool wasAdded = false;
    QString oldPath;
    if (syncServerConfig(configPath, executablePath, wasOverwritten, wasAdded, oldPath)) {
        installedFiles.append(configPath);
        if (wasOverwritten) {
            overwrittenFiles.append(QStringLiteral("Claude Desktop (%1): corrected outdated path '%2'").arg(configPath, oldPath));
        }
        return true;
    }
    return false;
}

bool McpAgentInstaller::installClaudeCodeConfig(
    const QString &executablePath,
    QString &outConfigPath,
    QStringList &installedFiles,
    QStringList &overwrittenFiles)
{
    const QString configPath = QDir(QDir::homePath()).filePath(QStringLiteral(".claude.json"));
    outConfigPath = configPath;

    if (!QFile::exists(configPath)) {
        return false;
    }

    bool wasOverwritten = false;
    bool wasAdded = false;
    QString oldPath;
    if (syncServerConfig(configPath, executablePath, wasOverwritten, wasAdded, oldPath)) {
        installedFiles.append(configPath);
        if (wasOverwritten) {
            overwrittenFiles.append(QStringLiteral("Claude Code CLI (%1): corrected outdated path '%2'").arg(configPath, oldPath));
        }
        return true;
    }
    return false;
}

QJsonArray McpAgentInstaller::getAllToolDefinitions()
{
    return getMcpAllToolDefinitions();
}

QString McpAgentInstaller::generateManualMarkdown(const QString &executablePath)
{
    const QString discoveryPath = QDir::toNativeSeparators(McpServer::discoveryFilePath());
    const QString jsonEscapedCommand = QString(executablePath).replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    const QString nativeCommand = QDir::toNativeSeparators(executablePath);

    return QStringLiteral(
        "# Ensemble Model Context Protocol (MCP) Manual\n\n"
        "Welcome to the **Ensemble** AI Agent integration guide. This manual describes how autonomous "
        "AI agents (e.g. Gemini Antigravity, Claude Code, Claude Desktop, Cursor) connect to and interact with Ensemble.\n\n"
        "---\n\n"
        "## 1. Overview\n\n"
        "Ensemble is a desktop writing suite and AO3 Work Skin composer. By exposing an embedded Model Context Protocol (MCP) server, "
        "AI agents can inspect the current project, read and write chapters in real-time with automated AO3 HTML sanitization, "
        "tweak custom CSS work skins, manage narrative lore graphs, and run linting diagnostics.\n\n"
        "---\n\n"
        "## 2. Connection Methods\n\n"
        "### A. Direct WebSocket Connection\n"
        "- **Address**: `ws://127.0.0.1:26303` (or auto-assigned port recorded in `mcp.json`)\n"
        "- **Discovery Manifest File**:\n"
        "  - **Windows**: `%LOCALAPPDATA%\\Ensemble\\mcp.json`\n"
        "  - **Linux**: `~/.config/Ensemble/mcp.json`\n"
        "  - **macOS**: `~/Library/Application Support/Ensemble/mcp.json`\n"
        "  - *(Active path on this system: `%1`)*\n"
        "- **Protocol**: JSON-RPC 2.0 (MCP 2024-11-05 specification)\n\n"
        "### B. Stdio CLI Bridge (Claude Desktop / Claude Code / Antigravity)\n"
        "Add Ensemble to your agent's MCP configuration:\n"
        "```json\n"
        "{\n"
        "  \"mcpServers\": {\n"
        "    \"ensemble\": {\n"
        "      \"command\": \"%2\",\n"
        "      \"args\": [\"--mcp-bridge\"]\n"
        "    }\n"
        "  }\n"
        "}\n"
        "```\n\n"
        "Or for Claude Code CLI:\n"
        "- **Windows (PowerShell / Command Prompt)**:\n"
        "  ```powershell\n"
        "  claude mcp add ensemble \"%3\" -- --mcp-bridge\n"
        "  ```\n"
        "- **Linux / macOS (Terminal)**:\n"
        "  ```bash\n"
        "  claude mcp add ensemble \"%3\" -- --mcp-bridge\n"
        "  ```\n\n"
        "---\n\n"
        "## 3. Available Tools\n\n"
        "| Tool | Parameters | Description |\n"
        "| :--- | :--- | :--- |\n"
        "| `get_project_summary` | *(none)* | Returns project title, author, chapter count, word counts, and unsaved changes state. |\n"
        "| `list_chapters` | *(none)* | Lists all chapters with their index, UUID, title, order, and word count. |\n"
        "| `get_chapter` | `chapterIndex` (int), `format` (str, opt) | Retrieves HTML and/or plain text content of a chapter ('all', 'html', or 'text'). |\n"
        "| `update_chapter_html` | `chapterIndex` (int), `html` (str) | Updates chapter body. Automatically sanitized against AO3 tag whitelist; returns sanitized HTML, word count, and stripped tags/attributes. |\n"
        "| `add_chapter` | `title` (str, opt) | Appends a new chapter to the active project. |\n"
        "| `delete_chapter` | `chapterIndex` (int) | Deletes a chapter by index and returns full updated chapter order. |\n"
        "| `reorder_chapters` | `fromIndex` (int), `toIndex` (int) | Reorders chapters and returns full updated chapter order. |\n"
        "| `get_workskin_css` | *(none)* | Retrieves the custom AO3 Work Skin stylesheet. |\n"
        "| `set_workskin_css` | `css` (str), `allowMalformed` (bool, opt) | Updates the Work Skin CSS with syntax validation and triggers an instant Chromium live re-render. |\n"
        "| `get_problems` | *(none)* | Returns current HTML & CSS lint diagnostics with affectedChapters list and lastUpdated timestamp. |\n"
        "| `load_project` | `filePath` (str), `saveCurrent` (bool, opt) | Loads an `.ao3proj` story archive from disk into Ensemble, updating chapters, stylesheet, tags, lore graph, and git history. |\n"
        "| `save_project` | `filePath` (str, opt), `commitMessage` (str, opt), `author` (str, opt), `saveCopy` (bool, opt) | Saves active project to `.ao3proj` on disk. Supports recording a snapshot into `history.cmt` and `history/<hash>.diff`. If `saveCopy: true`, exports a copy without switching active project. |\n"
        "| `get_project_history` | `limit` (int, opt) | Retrieves project snapshot history from `history.cmt`, parsed into structured commit/snapshot objects (`hash`, `previousCommitHash`, `author`, `date`, `localDate`, `message`, `diffAvailable`) and raw log text. |\n"
        "| `add_history_commit` | `message` (str), `author` / `agentId` (str) | Records a new snapshot entry into `history.cmt` and `history/<hash>.diff`. REQUIRED: Agents must identify themselves by providing their agent ID/tag. Returns `hash` and `previousCommitHash`. |\n"
        "| `get_snapshot_diff` | `hash` (str) | Retrieves line-by-line unified diff from `history/<hash>.diff`. If created before snapshots were introduced, safely returns explanation. |\n"
        "| `reload_project` | *(none)* | Discards unsaved changes and reloads active project from disk. |\n"
        "| `get_preview_screenshot` | `chapterIndex` (int, opt) | Captures rendered Chromium PreviewPane as a base64 PNG MCP image block for visual inspection. |\n"
        "| `get_agent_presence` | *(none)* | Inspects all active AI agents currently connected to Ensemble (Antigravity, Claude, etc.) for multi-agent presence and co-working. |\n"
        "| `register_agent_presence` | `agentName` (str, opt), `role` (str, opt), `workingNote` (str, opt) | Announces or updates your agent identity, role (co-author, reviewer, etc.), and current work note for multi-model co-working. |\n"
        "| `get_agentic_summary` | *(none)* | Retrieves storage quotas (5 MB shared, 20 MB localized), current byte usage, index metadata, list of shared files, and registered localized agent directories in .ao3proj agentic-datas. |\n"
        "| `read_agentic_file` | `path` (str), `scope` (str, opt), `encoding` (str, opt) | Reads a file from .ao3proj agentic-datas ('shared/' by default, or an agent's private '<agent>/' memory directory). |\n"
        "| `write_agentic_file` | `path` (str), `content` (str), `scope` (str, opt), `encoding` (str, opt) | Writes or updates a file in .ao3proj agentic-datas ('shared/' by default for multi-model collaboration or '<agent>/' for private memory). Strictly enforces the 5 MB shared cap and 20 MB localized cap. |\n"
        "| `append_agent_history` | `summary` (str), `agentName` (str, opt), `model` (str, opt), `role` (str, opt), `action` (str, opt), `chapterIndex` (int, opt), `wordCountDelta` (int, opt), `details` (str, opt) | Appends a structured interaction entry to agent_model_history.xml in .ao3proj. |\n"
        "| `get_agent_history` | `limit` (int, opt), `agentName` (str, opt), `action` (str, opt) | Retrieves audit trail and recent changes by each model from agent_model_history.xml, including timestamps, actions, deltas, and summaries. |\n"
        "| `post_agent_thought` | `thought` (str), `category` (str, opt), `target` (str, opt) | Broadcasts an AI model's internal reasoning, critique, pacing feedback, or plan to be displayed live in Ensemble's Agent Thoughts Monitor UI. |\n"
        "| `get_lore_graph` | `name` (str, opt), `includeGrammar` (bool, opt) | Retrieves the story lore and narrative graph in custom `.grph` DSL syntax, JSON structure, and complete grammar specification. |\n"
        "| `update_lore_graph` | `dsl` (str), `name` (str, opt), `autoLayout` (bool, opt) | Replaces or updates the `.grph` narrative graph DSL with full syntax and semantic validation. Strictly adheres to unique 8-byte magic header `\\x89GRPH\\x01\\r\\n` and updates live desktop canvas. |\n"
        "| `add_lore_node` | `id` (str), `label` (str), `type` (str, opt), `x` (num, opt), `y` (num, opt), `shape` (str, opt), `color` (str, opt), `notes` (str, opt) | Adds or updates an individual node block in the story's lore graph with position, shape, color, and optional notes. |\n"
        "| `connect_lore_nodes` | `fromNodeId` (str), `toNodeId` (str), `arrow` (str, opt), `label` (str, opt), `style` (str, opt), `color` (str, opt) | Creates or updates a relationship connector between two lore nodes (`->`, `<->`, `--`, `-x` conflict). |\n"
        "| `move_lore_node` | `nodeId` (str), `x` (num), `y` (num), `graphName` (str, opt) | Updates the 2D canvas coordinates `pos(x, y)` of a movable block in the lore graph. |\n"
        "| `get_lore_data` | `section` (str, opt), `query` (str, opt) | Retrieves narrative lore data from the `.ao3proj` lore/ directory files (`lore_data.json`, `history.json`, `conflicts.json`, `resolved.json`, `plans.json`, `references.json`, `metadata.xml`). |\n"
        "| `manage_lore_data` | `action` (str), *payload* | Performs CRUD actions on story lore entities, timeline history, plot conflicts & resolutions, and narrative follow-up plans. |\n"
        "| `update_project_metadata` | `rating` (str), `archiveWarnings` (arr), `categories` (arr), `fandoms` (arr), `relationships` (arr), `characters` (arr), `tags` (arr) | Updates standard AO3 work metadata tags stored in `metadata.xml`. |\n"
        "| `list_image_embeds` | `chapterIndex` (int, opt) | Lists all external image embeds across chapters in the project (or for a specific chapter). |\n"
        "| `insert_image_embed` | `chapterIndex` (int), `url` (str), `alt` (str, opt), `title` (str, opt), `width` (int, opt), `height` (int, opt), `alignment` (str, opt), `wrapInLink` (bool, opt), `position` (str, opt) | Inserts an AO3-compliant external image URL attachment into a chapter with clean `<img>` tags and optional link/alignment wrapping. |\n"
        "| `test_firewall_connection` | *(none)* | Probes the local WebSocket loopback socket and reports latency or firewall blocks. |\n\n"
        "---\n\n"
        "## 4. Lore Graph & Narrative Worldbuilding (`.grph`)\n\n"
        "Ensemble provides an independent narrative graphing engine and DSL format (`.grph`) designed specifically for storyboards, relationship tracking, lore mapping, and narrative follow-ups.\n\n"
        "### Unique File Signature (Magic Bytes)\n"
        "`.grph` files begin with an 8-byte binary signature:\n"
        "`\\x89GRPH\\x01\\r\\n` (`0x89, 0x47, 0x52, 0x50, 0x48, 0x01, 0x0D, 0x0A`).\n\n"
        "### Language Grammar & Syntax\n"
        "```text\n"
        "graph \"Story Lore & Narrative Arc\" [direction=LR]\n\n"
        "# Nodes (Story Blocks with 2D positions)\n"
        "node protagonist [label=\"Hero\", type=\"character\", pos(100, 150), shape=\"rounded\", color=\"#3b82f6\", notes=\"Main character\"]\n"
        "node inciting_incident [label=\"Call to Adventure\", type=\"event\", pos(400, 150), shape=\"diamond\", color=\"#f59e0b\"]\n\n"
        "# Connectors (Relationships & Narrative Flows)\n"
        "protagonist -> inciting_incident [label=\"triggers\", relation=\"causes\", style=\"solid\", color=\"#94a3b8\"]\n"
        "hero -x antagonist [label=\"sworn enemies\", relation=\"conflict\", style=\"dashed\", color=\"#ef4444\"]\n\n"
        "# Boxes (Grouping Containers)\n"
        "box academy [label=\"Royal Knight Academy\", pos(80, 100), size(600, 300), color=\"#8b5cf6\"] {\n"
        "    protagonist\n"
        "    inciting_incident\n"
        "}\n\n"
        "# Follow-up Hooks for Story Chapters\n"
        "followup inciting_incident [targetChapter=2, status=\"pending\", note=\"Establish character stakes and motivation.\"]\n"
        "```\n\n"
        "---\n\n"
        "## 5. Lore Directory Structure in `.ao3proj`\n\n"
        "Inside every `.ao3proj` archive, the `lore/` directory manages narrative continuity:\n"
        "```text\n"
        "lore/\n"
        "├── lore_data.json       (Entities: Characters, Locations, Factions, Artifacts, Concepts)\n"
        "├── history.json         (Backstory events & timeline chronology: summary, impact, timestamp)\n"
        "├── conflicts.json       (Detected canon inconsistencies & plot discrepancies)\n"
        "├── resolved.json        (Resolved conflicts and definitive author canon rulings)\n"
        "├── plans.json           (Story follow-up threads: loreThread, targetChapter, status)\n"
        "├── references.json      (Canon sources, world codex citations: source, citation, notes)\n"
        "├── metadata.xml         (Lore manifest and chapter integration descriptors)\n"
        "└── graphs/\n"
        "    └── data.grph        (Primary narrative lore graph with \\x89GRPH\\x01\\r\\n magic bytes)\n"
        "```\n\n"
        "---\n\n"
        "## 6. Multi-Agent Co-Working & Presence Awareness\n\n"
        "Ensemble supports multiple concurrently connected AI agents (for example, **Gemini Antigravity** and **Claude** running simultaneously). Agents can:\n"
        "1. Discover each other using `get_agent_presence` (or the `ensemble://agents` resource).\n"
        "2. Announce their role and current focus using `register_agent_presence` (e.g. `role: 'co-author'`, `workingNote: 'Drafting chapter 2 dialogue'`).\n"
        "3. Check what other models are working on to divide tasks effectively and prevent conflicting edits.\n\n"
        "---\n\n"
        "## 7. Agentic Data & Coroutine Shared Storage (`agentic-datas`)\n\n"
        "Every `.ao3proj` project container contains a dedicated `agentic-datas` attribute:\n"
        "```text\n"
        ".ao3proj\n"
        "├── metadata.xml (contains <agentic-datas> manifest)\n"
        "├── stylesheet/workskin.css\n"
        "├── chapters/chapter_*.html\n"
        "└── agentic-datas/\n"
        "    ├── index.json                        (Storage catalog, limits, and usage metrics)\n"
        "    ├── agent_model_history.xml           (Structured model interaction & contribution log)\n"
        "    ├── agentifiers.json                  (AES-256-CBC-HMAC-SHA256 encrypted agent identifier registry)\n"
        "    ├── shared/                           (Collaborative coroutine memory shared by all models; max 5 MB cap)\n"
        "    └── <agent>/                          (Private agent memory, e.g. claude/, cursor/, antigravity/; max 20 MB cap)\n"
        "```\n\n"
        "### Storage Caps\n"
        "- **Shared Data (`agentic-datas/shared/`)**: Hard cap of **5 MB** (5,242,880 bytes).\n"
        "- **Localized Agent Data (`agentic-datas/<agent>/`)**: Hard cap of **20 MB** (20,971,520 bytes).\n"
        "Attempts to write files exceeding these caps are automatically rejected with a descriptive error.\n\n"
        "---\n\n"
        "## 8. Model Audit Trail & Contribution History\n\n"
        "All model interactions, edits, and milestones are audited in `agent_model_history.xml`:\n"
        "- Tracked fields: `timestamp`, `agent`, `model`, `role`, `action`, `chapterIndex`, `wordCountDelta`, `strippedTags`, `summary`.\n"
        "- Query audit log via `get_agent_history(limit, agentName, action)`.\n"
        "- Manually record interactions via `append_agent_history(summary, ...)`.\n\n"
        "---\n\n"
        "## 9. Available Resources\n\n"
        "- `ensemble://project` — Current project metadata (JSON)\n"
        "- `ensemble://workskin/css` — Current Work Skin stylesheet (CSS)\n"
        "- `ensemble://problems` — Active problem diagnostics and lint errors (JSON)\n"
        "- `ensemble://agents` — Real-time list of all connected AI agents and presence state (JSON)\n"
        "- `ensemble://agentic/index` — Storage index, quotas (5MB shared / 20MB localized), and file listings (JSON)\n"
        "- `ensemble://agentic/history` — Agent interaction and contribution log (XML)\n"
        "- `ensemble://lore/graph` — Active narrative DSL script and mapping from lore/graphs/data.grph (Text)\n"
        "- `ensemble://lore/data` — Story entities, history, conflicts, plans, and references (JSON)\n\n"
        "---\n\n"
        "## 10. Troubleshooting & Diagnostics\n\n"
        "- **Diagnose Connection via CLI**:\n"
        "  - **Windows (PowerShell / Command Prompt)**: `ensemble.exe --mcp-status`\n"
        "  - **Linux / macOS (Terminal)**: `ensemble --mcp-status`\n\n"
        "- **Firewall Notice**: Ensemble binds strictly to `127.0.0.1` (localhost loopback). If your client cannot connect:\n"
        "  - **Windows**: Ensure `ensemble.exe` is permitted through Windows Defender Firewall for local loopback.\n"
        "  - **Linux**: Verify loopback interface `lo` / `127.0.0.1` is not blocked by `ufw` or `iptables`.\n\n"
        "- **Test in GUI**: Open **Preferences > AI Settings** and click **Test Loopback Connection**.\n"
    ).arg(discoveryPath, jsonEscapedCommand, nativeCommand);
}
