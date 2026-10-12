#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

struct AgentModelHistoryEntry {
    QString id;
    QDateTime timestamp;
    QString agentName;
    QString model;
    QString role;
    QString action;
    QString summary;
    int chapterIndex = -1;
    int wordCountDelta = 0;
    QString details;

    QString agent() const { return agentName; }
    QJsonObject toJson() const;
};

class AgenticData
{
public:
    // Quota limits requested: 5MB shared, 20MB localized agent data
    static constexpr qint64 kMaxSharedBytes = 5 * 1024 * 1024;      // 5,242,880 bytes (5 MB)
    static constexpr qint64 kMaxLocalizedBytes = 20 * 1024 * 1024;  // 20,971,520 bytes (20 MB)

    AgenticData();

    // Reset to clean initial state
    void clear();

    // Storage consumption metrics
    qint64 totalSharedBytes() const;
    qint64 totalLocalizedBytes() const;
    qint64 agentBytes(const QString &agentName) const;

    // Shared data storage (agentic-datas/shared/...)
    bool setSharedFile(const QString &relativePath, const QByteArray &data, QString *error = nullptr);
    QByteArray sharedFile(const QString &relativePath) const;
    bool hasSharedFile(const QString &relativePath) const;
    bool removeSharedFile(const QString &relativePath);
    QStringList sharedFileList() const;
    const QMap<QString, QByteArray> &sharedFiles() const { return m_sharedFiles; }

    // Localized agent private storage (agentic-datas/<agent>/...)
    bool setAgentFile(const QString &agentName, const QString &relativePath, const QByteArray &data, QString *error = nullptr);
    QByteArray agentFile(const QString &agentName, const QString &relativePath) const;
    bool hasAgentFile(const QString &agentName, const QString &relativePath) const;
    bool removeAgentFile(const QString &agentName, const QString &relativePath);
    QStringList agentNames() const;
    QStringList agentFileList(const QString &agentName) const;
    const QMap<QString, QMap<QString, QByteArray>> &allAgentFiles() const { return m_agentFiles; }

    // agent_model_history.xml
    void appendHistoryEntry(const QString &agentName, const QString &model,
                            const QString &role, const QString &action,
                            const QString &summary, int chapterIndex = -1,
                            int wordCountDelta = 0, const QString &details = QString());
    QList<AgentModelHistoryEntry> historyEntries() const;
    QMap<QString, AgentModelHistoryEntry> lastChangesByModel() const;
    QJsonObject lastChangesByModelJson() const;
    QString rawModelHistoryXml() const;
    void setRawModelHistoryXml(const QString &xml);

    // agentifiers.json (encrypted identifier registry)
    QJsonObject agentifiersJson() const;
    void setAgentifiersJson(const QJsonObject &json);
    void registerAgentIdentifier(const QString &agentName, const QString &modelFamily,
                                 const QString &role, const QString &notes = QString());
    QByteArray exportEncryptedAgentifiers(const QByteArray &passphrase = QByteArray()) const;
    bool importEncryptedAgentifiers(const QByteArray &encryptedEnvelope, const QByteArray &passphrase = QByteArray(), QString *error = nullptr);

    // index.json
    QJsonObject generateIndexJson() const;
    void loadIndexJson(const QJsonObject &indexObj);

    // Archive serialization & deserialization for .ao3proj archive
    void serializeToArchive(QMap<QString, QByteArray> &archiveEntries) const;
    bool deserializeFromArchive(const QMap<QString, QByteArray> &archiveEntries, QString *error = nullptr);
    void ensureDefaultHistory();
    void ensureDefaultAgentifiers();

    // Path sanitization helper
    static QString sanitizePath(const QString &path);
    static QString sanitizeAgentName(const QString &agent);

private:

    QMap<QString, QByteArray> m_sharedFiles;
    QMap<QString, QMap<QString, QByteArray>> m_agentFiles;
    QList<AgentModelHistoryEntry> m_historyEntries;
    QMap<QString, AgentModelHistoryEntry> m_lastChangesByModel;
    QJsonObject m_agentifiers;
    QDateTime m_createdDate;
    QDateTime m_modifiedDate;
    QString m_projectIdentifier;
};
