#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>

struct AgentActivityEvent {
    enum class Type {
        Thought,          // Explicit model reasoning, critique, analysis, or reflection
        DataInspection,   // Reading chapters, HTML, CSS, lint problems, or agentic files
        DataModification, // Modifying chapters, CSS, or writing agentic shared files
        PresenceChange,   // Registering or updating presence and working notes
        ToolExecution     // General tool execution
    };

    QString id;
    QDateTime timestamp;
    QString agentName;
    QString role;
    Type type = Type::ToolExecution;
    QString title;         // Short action headline (e.g. "Reviewing Chapter 1 Pacing", "Read 'plot_notes.md'")
    QString thought;       // Detailed reasoning, internal critique, or rationale
    QJsonObject inputData; // Complete JSON arguments / input payload received from agent
    QJsonObject resultData;// Response result / summary
    int durationMs = 0;
    bool isError = false;

    QString typeString() const {
        switch (type) {
            case Type::Thought: return QStringLiteral("Thought");
            case Type::DataInspection: return QStringLiteral("Read Data");
            case Type::DataModification: return QStringLiteral("Modify Data");
            case Type::PresenceChange: return QStringLiteral("Presence");
            case Type::ToolExecution: return QStringLiteral("Tool");
        }
        return QStringLiteral("Activity");
    }

    QString iconSymbol() const {
        switch (type) {
            case Type::Thought: return QStringLiteral("💭");
            case Type::DataInspection: return QStringLiteral("📖");
            case Type::DataModification: return QStringLiteral("✏️");
            case Type::PresenceChange: return QStringLiteral("👤");
            case Type::ToolExecution: return QStringLiteral("⚙️");
        }
        return QStringLiteral("🔹");
    }
};
