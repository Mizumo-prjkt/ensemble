#pragma once

#include <QJsonArray>
#include <QString>
#include <QStringList>

struct McpInstallReport {
    bool success = false;
    QString manualPath;
    QString antigravityGlobalPath;
    QString antigravityMcpDir;
    QString claudeConfigPath;
    QString claudeCodeConfigPath;
    QString localLlmConfigPath;
    QString localLlmScriptPath;
    QStringList installedFiles;
    QStringList overwrittenConfigs;
    QString summary;
};

class McpAgentInstaller
{
public:
    static McpInstallReport installAll(const QString &executablePath = QString(), const QString &workspaceDir = QString());
    static QString generateManualMarkdown(const QString &executablePath);
    static QString generateLocalLlmMcpConfigJson(const QString &executablePath = QString());
    static QString generateLocalLlmAgentScript(const QString &executablePath = QString());

    // Returns authoritative list of all 34 MCP tool definitions with schemas
    static QJsonArray getAllToolDefinitions();

    // Checks agent configuration files (Antigravity, Claude Desktop, Claude Code, Local LLMs).
    // If an existing 'ensemble' entry has an invalid, stale, or different binary path, overwrites it with the correct path.
    // If missing, registers it. Returns true if any configurations were added or overwritten.
    static bool checkAndSyncAgentConfigs(const QString &executablePath = QString(), QStringList *modifiedFiles = nullptr, QStringList *overwrittenFiles = nullptr);

    // Updates or registers the "ensemble" entry in a specified JSON config file (e.g. mcp_config.json or claude_desktop_config.json).
    static bool syncServerConfig(const QString &configFilePath, const QString &executablePath, bool &wasOverwritten, bool &wasAdded, QString &oldPath);

private:
    static bool installAntigravityMcp(const QString &executablePath, const QString &workspaceDir, QStringList &installedFiles, QStringList &overwrittenFiles, QString &outConfigPath, QString &outToolDir);
    static bool installClaudeDesktopConfig(const QString &executablePath, QString &outConfigPath, QStringList &installedFiles, QStringList &overwrittenFiles);
    static bool installClaudeCodeConfig(const QString &executablePath, QString &outConfigPath, QStringList &installedFiles, QStringList &overwrittenFiles);
    static bool installLocalLlmConfig(const QString &executablePath, const QString &targetDir, QString &outConfigPath, QString &outScriptPath, QStringList &installedFiles, QStringList &overwrittenFiles);
};
