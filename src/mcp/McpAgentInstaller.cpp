#include "McpAgentInstaller.h"
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

    // 1. instructions.md
    const QString instructionsPath = QDir(ensembleMcpDir).filePath(QStringLiteral("instructions.md"));
    const QString instructionsText = QStringLiteral(
        "# Ensemble AO3 Writing Suite MCP Server\n\n"
        "Use this server whenever you need to interact with the running Ensemble writing application.\n\n"
        "## Best Practices:\n"
        "1. Check the active project state first with `get_project_summary` before performing updates.\n"
        "2. To inspect chapter text, use `list_chapters` followed by `get_chapter`.\n"
        "3. When rewriting or drafting story prose, write cleanly formatted HTML or use standard paragraphs `<p>...</p>`.\n"
        "   Call `update_chapter_html` to push changes directly into Ensemble's live editor and preview.\n"
        "4. When authoring or tuning custom work skins, inspect current CSS using `get_workskin_css` and apply modifications with `set_workskin_css`.\n"
        "5. If an edit produces formatting issues, call `get_problems` to review AO3 tag whitelist validation errors.\n"
        "6. If you encounter connection drops, run `test_firewall_connection` to diagnose local socket reachability.\n"
    );
    if (writeTextFile(instructionsPath, instructionsText)) {
        installedFiles.append(instructionsPath);
    }

    // 2. Individual tool schema files
    auto createToolSchema = [&](const QString &toolName, const QString &desc, const QJsonObject &properties, const QJsonArray &required = QJsonArray()) {
        QJsonObject schema;
        schema[QStringLiteral("name")] = toolName;
        schema[QStringLiteral("description")] = desc;
        QJsonObject params;
        params[QStringLiteral("type")] = QStringLiteral("object");
        params[QStringLiteral("properties")] = properties;
        params[QStringLiteral("additionalProperties")] = false;
        if (!required.isEmpty()) {
            params[QStringLiteral("required")] = required;
        }
        schema[QStringLiteral("parameters")] = params;

        const QString schemaPath = QDir(ensembleMcpDir).filePath(QStringLiteral("%1.json").arg(toolName));
        if (writeJsonFile(schemaPath, schema)) {
            installedFiles.append(schemaPath);
        }
    };

    // get_project_summary
    createToolSchema(
        QStringLiteral("get_project_summary"),
        QStringLiteral("Retrieves active project metadata: title, author, chapter count, total word count, and unsaved changes status."),
        QJsonObject()
    );

    // list_chapters
    createToolSchema(
        QStringLiteral("list_chapters"),
        QStringLiteral("Lists all chapters in the active project with index, title, order, and word count."),
        QJsonObject()
    );

    // get_chapter
    {
        QJsonObject props;
        QJsonObject idx;
        idx[QStringLiteral("type")] = QStringLiteral("integer");
        idx[QStringLiteral("description")] = QStringLiteral("0-based index of the chapter to retrieve.");
        props[QStringLiteral("chapterIndex")] = idx;
        QJsonObject format;
        format[QStringLiteral("type")] = QStringLiteral("string");
        format[QStringLiteral("enum")] = QJsonArray{QStringLiteral("all"), QStringLiteral("html"), QStringLiteral("text")};
        format[QStringLiteral("description")] = QStringLiteral("Optional format: 'all' (default), 'html', or 'text'.");
        props[QStringLiteral("format")] = format;
        createToolSchema(
            QStringLiteral("get_chapter"),
            QStringLiteral("Gets HTML and/or plain text content of a specific chapter."),
            props,
            QJsonArray{QStringLiteral("chapterIndex")}
        );
    }

    // update_chapter_html
    {
        QJsonObject props;
        QJsonObject idx;
        idx[QStringLiteral("type")] = QStringLiteral("integer");
        idx[QStringLiteral("description")] = QStringLiteral("0-based index of the chapter to update.");
        props[QStringLiteral("chapterIndex")] = idx;
        QJsonObject html;
        html[QStringLiteral("type")] = QStringLiteral("string");
        html[QStringLiteral("description")] = QStringLiteral("The new HTML content for the chapter.");
        props[QStringLiteral("html")] = html;
        createToolSchema(
            QStringLiteral("update_chapter_html"),
            QStringLiteral("Updates HTML content of a chapter. Content is sanitized against AO3 tag whitelist and the live editor/preview updates immediately."),
            props,
            QJsonArray{QStringLiteral("chapterIndex"), QStringLiteral("html")}
        );
    }

    // add_chapter
    {
        QJsonObject props;
        QJsonObject title;
        title[QStringLiteral("type")] = QStringLiteral("string");
        title[QStringLiteral("description")] = QStringLiteral("Optional title for the new chapter.");
        props[QStringLiteral("title")] = title;
        createToolSchema(
            QStringLiteral("add_chapter"),
            QStringLiteral("Creates and appends a new chapter to the active project."),
            props
        );
    }

    // delete_chapter
    {
        QJsonObject props;
        QJsonObject idx;
        idx[QStringLiteral("type")] = QStringLiteral("integer");
        idx[QStringLiteral("description")] = QStringLiteral("0-based index of the chapter to delete.");
        props[QStringLiteral("chapterIndex")] = idx;
        createToolSchema(
            QStringLiteral("delete_chapter"),
            QStringLiteral("Deletes a chapter from the project by index."),
            props,
            QJsonArray{QStringLiteral("chapterIndex")}
        );
    }

    // reorder_chapters
    {
        QJsonObject props;
        QJsonObject from;
        from[QStringLiteral("type")] = QStringLiteral("integer");
        from[QStringLiteral("description")] = QStringLiteral("Starting 0-based index.");
        props[QStringLiteral("fromIndex")] = from;
        QJsonObject to;
        to[QStringLiteral("type")] = QStringLiteral("integer");
        to[QStringLiteral("description")] = QStringLiteral("Destination 0-based index.");
        props[QStringLiteral("toIndex")] = to;
        createToolSchema(
            QStringLiteral("reorder_chapters"),
            QStringLiteral("Moves a chapter from one index position to another."),
            props,
            QJsonArray{QStringLiteral("fromIndex"), QStringLiteral("toIndex")}
        );
    }

    // get_workskin_css
    createToolSchema(
        QStringLiteral("get_workskin_css"),
        QStringLiteral("Retrieves the custom AO3 Work Skin CSS of the active project."),
        QJsonObject()
    );

    // set_workskin_css
    {
        QJsonObject props;
        QJsonObject css;
        css[QStringLiteral("type")] = QStringLiteral("string");
        css[QStringLiteral("description")] = QStringLiteral("The new CSS rules to apply.");
        props[QStringLiteral("css")] = css;
        createToolSchema(
            QStringLiteral("set_workskin_css"),
            QStringLiteral("Updates the custom AO3 Work Skin CSS for the project and triggers an immediate live Chromium preview re-render."),
            props,
            QJsonArray{QStringLiteral("css")}
        );
    }

    // get_problems
    createToolSchema(
        QStringLiteral("get_problems"),
        QStringLiteral("Returns current diagnostic lint errors and warnings (AO3 tag whitelist violations, CSS validation issues)."),
        QJsonObject()
    );

    // save_project
    createToolSchema(
        QStringLiteral("save_project"),
        QStringLiteral("Saves the active project to its current .ao3proj file on disk."),
        QJsonObject()
    );

    // reload_project
    createToolSchema(
        QStringLiteral("reload_project"),
        QStringLiteral("Reloads active project from disk, discarding unsaved changes and resetting isDirty to false."),
        QJsonObject()
    );

    // get_preview_screenshot
    {
        QJsonObject props;
        QJsonObject idx;
        idx[QStringLiteral("type")] = QStringLiteral("integer");
        idx[QStringLiteral("description")] = QStringLiteral("Optional 0-based index of the chapter to preview.");
        props[QStringLiteral("chapterIndex")] = idx;
        createToolSchema(
            QStringLiteral("get_preview_screenshot"),
            QStringLiteral("Renders a chapter in Chromium with Work Skin CSS and returns a base64-encoded PNG screenshot as an MCP image content block."),
            props
        );
    }

    // test_firewall_connection
    createToolSchema(
        QStringLiteral("test_firewall_connection"),
        QStringLiteral("Executes a loopback connectivity test to verify WebSocket reachability and troubleshoot firewall blocks."),
        QJsonObject()
    );

    // get_agent_presence
    createToolSchema(
        QStringLiteral("get_agent_presence"),
        QStringLiteral("Inspects all active AI agents currently connected to Ensemble (e.g. Gemini Antigravity, Claude Desktop/Code) for multi-agent presence awareness, co-working, and peer synchronization."),
        QJsonObject()
    );

    // register_agent_presence
    {
        QJsonObject props;
        QJsonObject name;
        name[QStringLiteral("type")] = QStringLiteral("string");
        name[QStringLiteral("description")] = QStringLiteral("The name of your AI agent or model (e.g. 'antigravity', 'claude', 'gemini').");
        props[QStringLiteral("agentName")] = name;
        QJsonObject role;
        role[QStringLiteral("type")] = QStringLiteral("string");
        role[QStringLiteral("description")] = QStringLiteral("Your role in the session (e.g. 'co-author', 'reviewer', 'editor', 'proofreader', 'architect'). Defaults to 'co-author'.");
        props[QStringLiteral("role")] = role;
        QJsonObject note;
        note[QStringLiteral("type")] = QStringLiteral("string");
        note[QStringLiteral("description")] = QStringLiteral("A brief note describing what you are currently focusing on or working on.");
        props[QStringLiteral("workingNote")] = note;
        createToolSchema(
            QStringLiteral("register_agent_presence"),
            QStringLiteral("Announces or updates your AI agent presence, identity, role, and current working note so other connected AI models can coordinate and co-work."),
            props
        );
    }

    // get_agentic_summary
    createToolSchema(
        QStringLiteral("get_agentic_summary"),
        QStringLiteral("Retrieves storage quotas (5 MB shared, 20 MB localized), current byte usage, index metadata, list of shared files, and registered localized agent directories in .ao3proj agentic-datas."),
        QJsonObject()
    );

    // read_agentic_file
    {
        QJsonObject props;
        QJsonObject path;
        path[QStringLiteral("type")] = QStringLiteral("string");
        path[QStringLiteral("description")] = QStringLiteral("Relative path of the file to read (e.g. 'notes.md', 'plot_outline.json').");
        props[QStringLiteral("path")] = path;
        QJsonObject scope;
        scope[QStringLiteral("type")] = QStringLiteral("string");
        scope[QStringLiteral("description")] = QStringLiteral("Storage scope: 'shared' (default) or agent name (e.g. 'claude', 'antigravity', 'cursor').");
        props[QStringLiteral("scope")] = scope;
        QJsonObject enc;
        enc[QStringLiteral("type")] = QStringLiteral("string");
        enc[QStringLiteral("enum")] = QJsonArray{QStringLiteral("utf-8"), QStringLiteral("base64")};
        enc[QStringLiteral("description")] = QStringLiteral("Optional encoding: 'utf-8' (default) or 'base64'.");
        props[QStringLiteral("encoding")] = enc;
        createToolSchema(
            QStringLiteral("read_agentic_file"),
            QStringLiteral("Reads a file from the .ao3proj agentic-datas repository (from the coroutine 'shared/' directory or an agent's private '<agent>/' memory directory)."),
            props,
            QJsonArray{QStringLiteral("path")}
        );
    }

    // write_agentic_file
    {
        QJsonObject props;
        QJsonObject path;
        path[QStringLiteral("type")] = QStringLiteral("string");
        path[QStringLiteral("description")] = QStringLiteral("Relative file path (e.g. 'outline.md', 'character_arc.json').");
        props[QStringLiteral("path")] = path;
        QJsonObject content;
        content[QStringLiteral("type")] = QStringLiteral("string");
        content[QStringLiteral("description")] = QStringLiteral("The content of the file to write.");
        props[QStringLiteral("content")] = content;
        QJsonObject scope;
        scope[QStringLiteral("type")] = QStringLiteral("string");
        scope[QStringLiteral("description")] = QStringLiteral("Target storage scope: 'shared' (default) or private agent name (e.g. 'claude', 'antigravity', 'cursor').");
        props[QStringLiteral("scope")] = scope;
        QJsonObject enc;
        enc[QStringLiteral("type")] = QStringLiteral("string");
        enc[QStringLiteral("enum")] = QJsonArray{QStringLiteral("utf-8"), QStringLiteral("base64")};
        enc[QStringLiteral("description")] = QStringLiteral("Optional content encoding: 'utf-8' (default) or 'base64'.");
        props[QStringLiteral("encoding")] = enc;
        createToolSchema(
            QStringLiteral("write_agentic_file"),
            QStringLiteral("Writes or updates a file in .ao3proj agentic-datas ('shared/' for coroutine multi-agent collaboration or '<agent>/' for private memory). Strictly enforces the 5 MB shared cap and 20 MB localized cap."),
            props,
            QJsonArray{QStringLiteral("path"), QStringLiteral("content")}
        );
    }

    // append_agent_history
    {
        QJsonObject props;
        QJsonObject sum;
        sum[QStringLiteral("type")] = QStringLiteral("string");
        sum[QStringLiteral("description")] = QStringLiteral("Detailed summary of the action or contribution performed by the model.");
        props[QStringLiteral("summary")] = sum;
        QJsonObject agent;
        agent[QStringLiteral("type")] = QStringLiteral("string");
        agent[QStringLiteral("description")] = QStringLiteral("Optional agent name (e.g. 'antigravity', 'claude').");
        props[QStringLiteral("agentName")] = agent;
        QJsonObject model;
        model[QStringLiteral("type")] = QStringLiteral("string");
        model[QStringLiteral("description")] = QStringLiteral("Optional model family/version (e.g. 'gemini-2.5-pro', 'claude-3-7-sonnet').");
        props[QStringLiteral("model")] = model;
        QJsonObject role;
        role[QStringLiteral("type")] = QStringLiteral("string");
        role[QStringLiteral("description")] = QStringLiteral("Optional role (e.g. 'co-author', 'reviewer', 'editor').");
        props[QStringLiteral("role")] = role;
        QJsonObject act;
        act[QStringLiteral("type")] = QStringLiteral("string");
        act[QStringLiteral("description")] = QStringLiteral("Optional action type (e.g. 'sync', 'review', 'co-write', 'refactor').");
        props[QStringLiteral("action")] = act;
        createToolSchema(
            QStringLiteral("append_agent_history"),
            QStringLiteral("Appends a structured history entry to agent_model_history.xml in .ao3proj recording an AI model interaction, analysis, suggestion, or co-writing milestone."),
            props,
            QJsonArray{QStringLiteral("summary")}
        );
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

QString McpAgentInstaller::generateManualMarkdown(const QString &executablePath)
{
    return QStringLiteral(
        "# Ensemble Model Context Protocol (MCP) Manual\n\n"
        "Welcome to the **Ensemble** AI Agent integration guide. This manual describes how autonomous "
        "AI agents (e.g. Gemini Antigravity, Claude Code, Claude Desktop, Cursor) connect to and interact with Ensemble.\n\n"
        "---\n\n"
        "## 1. Overview\n\n"
        "Ensemble is a desktop writing suite and AO3 Work Skin composer. By exposing an embedded Model Context Protocol (MCP) server, "
        "AI agents can inspect the current project, read and write chapters in real-time with automated AO3 HTML sanitization, "
        "tweak custom CSS work skins, and run linting diagnostics.\n\n"
        "---\n\n"
        "## 2. Connection Methods\n\n"
        "### A. Direct WebSocket Connection\n"
        "- **Address**: `ws://127.0.0.1:26303` (or auto-assigned port recorded in `mcp.json`)\n"
        "- **Discovery File**: `~/.config/Ensemble/mcp.json`\n"
        "- **Protocol**: JSON-RPC 2.0 (MCP 2024-11-05 specification)\n\n"
        "### B. Stdio CLI Bridge (Claude Desktop / Claude Code / Antigravity)\n"
        "Add Ensemble to your agent's MCP configuration:\n"
        "```json\n"
        "{\n"
        "  \"mcpServers\": {\n"
        "    \"ensemble\": {\n"
        "      \"command\": \"%1\",\n"
        "      \"args\": [\"--mcp-bridge\"]\n"
        "    }\n"
        "  }\n"
        "}\n"
        "```\n\n"
        "Or for Claude Code CLI:\n"
        "```bash\n"
        "claude mcp add ensemble \"%1\" --mcp-bridge\n"
        "```\n\n"
        "---\n\n"
        "## 3. Available Tools\n\n"
        "| Tool | Parameters | Description |\n"
        "| :--- | :--- | :--- |\n"
        "| `get_project_summary` | *(none)* | Returns project title, author, chapter count, word counts, and unsaved changes state. |\n"
        "| `list_chapters` | *(none)* | Lists all chapters with their index, UUID, title, order, and word count. |\n"
        "| `get_chapter` | `chapterIndex` (int), `format` (str, opt) | Retrieves HTML and/or plain text content of a chapter ('all', 'html', or 'text'). |\n"
        "| `update_chapter_html` | `chapterIndex` (int), `html` (str) | Updates chapter body. Automatically sanitized; returns sanitized HTML, word count, and stripped tags/attributes. |\n"
        "| `add_chapter` | `title` (str, opt) | Appends a new chapter to the active project. |\n"
        "| `delete_chapter` | `chapterIndex` (int) | Deletes a chapter by index and returns full updated chapter order. |\n"
        "| `reorder_chapters` | `fromIndex` (int), `toIndex` (int) | Reorders chapters and returns full updated chapter order. |\n"
        "| `get_workskin_css` | *(none)* | Retrieves the custom AO3 Work Skin stylesheet. |\n"
        "| `set_workskin_css` | `css` (str) | Updates the Work Skin CSS and triggers an instant Chromium live re-render. |\n"
        "| `get_problems` | *(none)* | Returns current HTML & CSS lint diagnostics with affectedChapters list and lastUpdated timestamp. |\n"
        "| `save_project` | *(none)* | Saves active project to `.ao3proj` on disk. |\n"
        "| `reload_project` | *(none)* | Discards unsaved changes and reloads active project from disk. |\n"
        "| `get_preview_screenshot` | `chapterIndex` (int, opt) | Captures rendered Chromium PreviewPane as a base64 PNG MCP image block for visual inspection. |\n"
        "| `get_agent_presence` | *(none)* | Inspects all active AI agents currently connected to Ensemble (Antigravity, Claude, etc.) for multi-agent presence and co-working. |\n"
        "| `register_agent_presence` | `agentName` (str, opt), `role` (str, opt), `workingNote` (str, opt) | Announces or updates your agent identity, role (co-author, reviewer, etc.), and current work note for multi-model co-working. |\n"
        "| `get_agentic_summary` | *(none)* | Retrieves storage quotas (5 MB shared, 20 MB localized), current byte usage, index metadata, list of shared files, and registered localized agent directories in .ao3proj agentic-datas. |\n"
        "| `read_agentic_file` | `path` (str), `scope` (str, opt), `encoding` (str, opt) | Reads a file from .ao3proj agentic-datas ('shared/' or an agent's private '<agent>/' memory directory). |\n"
        "| `write_agentic_file` | `path` (str), `content` (str), `scope` (str, opt), `encoding` (str, opt) | Writes or updates a file in .ao3proj agentic-datas ('shared/' for multi-model collaboration or '<agent>/' for private memory). Strictly enforces the 5 MB shared cap and 20 MB localized cap. |\n"
        "| `append_agent_history` | `summary` (str), `agentName` (str, opt), `model` (str, opt), `role` (str, opt), `action` (str, opt) | Appends a structured interaction entry to agent_model_history.xml in .ao3proj. |\n"
        "| `test_firewall_connection` | *(none)* | Probes the local WebSocket loopback socket and reports latency or firewall blocks. |\n\n"
        "---\n\n"
        "## 4. Multi-Agent Co-Working & Presence Awareness\n\n"
        "Ensemble supports multiple concurrently connected AI agents (for example, **Gemini Antigravity** and **Claude** running simultaneously). Agents can:\n"
        "1. Discover each other using `get_agent_presence` (or the `ensemble://agents` resource).\n"
        "2. Announce their role and current focus using `register_agent_presence` (e.g. `role: 'co-author'`, `workingNote: 'Drafting chapter 2 dialogue'`).\n"
        "3. Check what other models are working on to divide tasks effectively and prevent conflicting edits.\n\n"
        "---\n\n"
        "## 5. Agentic Data & Coroutine Shared Storage (`agentic-datas`)\n\n"
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
        "## 6. Available Resources\n\n"
        "- `ensemble://project` — Current project metadata (JSON)\n"
        "- `ensemble://workskin/css` — Current Work Skin stylesheet (CSS)\n"
        "- `ensemble://problems` — Active problem diagnostics and lint errors (JSON)\n"
        "- `ensemble://agents` — Real-time list of all connected AI agents and presence state (JSON)\n"
        "- `ensemble://agentic/index` — Storage index, quotas (5MB shared / 20MB localized), and file listings (JSON)\n"
        "- `ensemble://agentic/history` — Agent interaction and contribution log (XML)\n\n"
        "---\n\n"
        "## 7. Troubleshooting & Diagnostics\n\n"
        "- **Diagnose Connection via CLI**:\n"
        "  ```bash\n"
        "  ensemble --mcp-status\n"
        "  ```\n"
        "- **Firewall Notice**: Ensemble binds strictly to `127.0.0.1` (localhost loopback). If your client cannot connect, ensure local security software is not blocking loopback TCP sockets.\n"
        "- **Test in GUI**: Open **Preferences > AI Settings** and click **Test Loopback Connection**.\n"
    ).arg(executablePath);
}
