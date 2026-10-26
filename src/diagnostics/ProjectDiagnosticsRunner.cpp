#include "ProjectDiagnosticsRunner.h"
#include "export/Ao3HtmlSanitizer.h"
#include "export/Ao3TagWhitelist.h"
#include "ui/CssEditorPane.h"

#include <QCoreApplication>
#include <QDebug>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>

#if defined(Q_OS_LINUX)
#include <pthread.h>
#include <sys/resource.h>
#include <unistd.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#endif

// -------------------------------------------------------------
// DiagnosticsWorker implementation (lives on background thread)
// -------------------------------------------------------------
DiagnosticsWorker::DiagnosticsWorker(QObject *parent)
    : QObject(parent)
{
}

void DiagnosticsWorker::processJob(const DiagnosticsJob &job)
{
    QElapsedTimer timer;
    timer.start();

    QList<ProblemItem> problems;
    int errorCount = 0;
    int warningCount = 0;

    // 1. Check Work Skin CSS syntax & structure
    QStringList cssErrors;
    CssEditorPane::validateCss(job.workSkinCss, &cssErrors);
    for (const QString &err : cssErrors) {
        if (QThread::currentThread()->isInterruptionRequested()) return;

        ProblemItem item;
        item.severity = ProblemItem::Error;
        item.category = QStringLiteral("Work Skin CSS");
        item.description = err;
        item.chapterIndex = -1;
        item.affectedChapters.clear();
        problems.append(item);
        errorCount++;
    }

    const QSet<QString> cssClassSet(job.cssClasses.begin(), job.cssClasses.end());

    // Check duplicate CSS classes
    QMap<QString, int> classCounts;
    for (const QString &cls : job.cssClasses) {
        classCounts[cls]++;
    }

    for (auto iter = classCounts.constBegin(); iter != classCounts.constEnd(); ++iter) {
        if (QThread::currentThread()->isInterruptionRequested()) return;

        if (iter.value() > 1) {
            ProblemItem item;
            item.severity = ProblemItem::Warning;
            item.category = QStringLiteral("Work Skin CSS");
            item.description = QStringLiteral("Duplicate CSS class '.%1' defined %2 times in Work Skin.").arg(iter.key()).arg(iter.value());
            item.chapterIndex = -1;
            item.affectedChapters.clear();
            problems.append(item);
            warningCount++;
        }
    }

    // 2. Check chapter structural issues & missing CSS classes
    static const QRegularExpression classAttrRe(R"re(class\s*=\s*"([^"]+)")re", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tagExtractRe(R"(<\/?([a-zA-Z][\w:-]*))");
    static const QRegularExpression styleAttrRe(R"(\bstyle\s*=\s*(?:"[^"]*"|'[^']*'|[^\s>]+))", QRegularExpression::CaseInsensitiveOption);
    const QSet<QString> allowedTags = Ao3TagWhitelist::allowedTags();
    QMap<QString, QList<int>> missingClassChapters;

    for (const auto &ch : job.chapters) {
        if (QThread::currentThread()->isInterruptionRequested()) return;

        const int chIdx = ch.index;
        const QString &html = ch.html;
        const QString &title = ch.title;

        if (title.trimmed().isEmpty()) {
            ProblemItem item;
            item.severity = ProblemItem::Warning;
            item.category = QStringLiteral("Chapter Structure");
            item.description = QStringLiteral("Chapter %1 has an empty title.").arg(chIdx + 1);
            item.chapterIndex = chIdx;
            item.affectedChapters = { chIdx };
            problems.append(item);
            warningCount++;
        }
        if (html.trimmed().isEmpty()) {
            ProblemItem item;
            item.severity = ProblemItem::Warning;
            item.category = QStringLiteral("Chapter Content");
            item.description = QStringLiteral("Chapter %1 (\"%2\") has no body content.").arg(chIdx + 1).arg(title);
            item.chapterIndex = chIdx;
            item.affectedChapters = { chIdx };
            problems.append(item);
            warningCount++;
        }

        // Check HTML parse errors via QXmlStreamReader
        if (!html.trimmed().isEmpty()) {
            const QString wrapped = QStringLiteral("<root>%1</root>").arg(html);
            QXmlStreamReader reader(wrapped);
            while (!reader.atEnd()) {
                if (QThread::currentThread()->isInterruptionRequested()) return;
                reader.readNext();
                if (reader.hasError()) {
                    const QString err = reader.errorString();
                    const qint64 line = qMax<qint64>(1, reader.lineNumber() - 1);

                    ProblemItem item;
                    item.severity = ProblemItem::Error; // HTML tag mismatch is an Error
                    item.category = QStringLiteral("HTML Syntax");
                    item.description = QStringLiteral("HTML parse error in Chapter %1 (\"%2\") near line %3: %4")
                                           .arg(chIdx + 1)
                                           .arg(title)
                                           .arg(line)
                                           .arg(err);
                    item.chapterIndex = chIdx;
                    item.affectedChapters = { chIdx };
                    problems.append(item);
                    errorCount++;
                    break;
                }
            }
        }

        // Check for inline style= attributes
        if (html.contains(styleAttrRe)) {
            ProblemItem item;
            item.severity = ProblemItem::Warning;
            item.category = QStringLiteral("AO3 Compliance");
            item.description = QStringLiteral("Inline style= attributes detected in Chapter %1 (\"%2\"). AO3 strips inline styles upon posting.").arg(chIdx + 1).arg(title);
            item.chapterIndex = chIdx;
            item.affectedChapters = { chIdx };
            problems.append(item);
            warningCount++;
        }

        // Scan for disallowed HTML tags according to AO3 whitelist
        QSet<QString> disallowedInChapter;
        QRegularExpressionMatchIterator tagIt = tagExtractRe.globalMatch(html);
        while (tagIt.hasNext()) {
            if (QThread::currentThread()->isInterruptionRequested()) return;
            const QString tagName = tagIt.next().captured(1).toLower();
            if (!allowedTags.contains(tagName) && !disallowedInChapter.contains(tagName)) {
                disallowedInChapter.insert(tagName);
                ProblemItem item;
                item.severity = ProblemItem::Warning;
                item.category = QStringLiteral("AO3 Compliance");
                item.description = QStringLiteral("Disallowed HTML tag <%1> detected in Chapter %2 (\"%3\"). AO3 will strip this tag upon posting.")
                                       .arg(tagName)
                                       .arg(chIdx + 1)
                                       .arg(title);
                item.chapterIndex = chIdx;
                item.affectedChapters = { chIdx };
                problems.append(item);
                warningCount++;
            }
        }

        // Scan chapter HTML for used CSS classes missing from Work Skin CSS
        QRegularExpressionMatchIterator it = classAttrRe.globalMatch(html);
        while (it.hasNext()) {
            if (QThread::currentThread()->isInterruptionRequested()) return;
            QRegularExpressionMatch m = it.next();
            const QStringList classes = m.captured(1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
            for (const QString &cls : classes) {
                if (!cssClassSet.contains(cls)) {
                    if (!missingClassChapters[cls].contains(chIdx)) {
                        missingClassChapters[cls].append(chIdx);
                    }
                }
            }
        }
    }

    // Add missing/removed CSS class warnings
    for (auto iter = missingClassChapters.constBegin(); iter != missingClassChapters.constEnd(); ++iter) {
        if (QThread::currentThread()->isInterruptionRequested()) return;
        const QString &cls = iter.key();
        const QList<int> &chList = iter.value();

        ProblemItem item;
        item.severity = ProblemItem::Warning;
        item.category = QStringLiteral("Work Skin CSS");
        if (chList.size() <= 2) {
            QStringList chNames;
            for (int idx : chList) {
                chNames << QStringLiteral("Chapter %1").arg(idx + 1);
            }
            item.description = QStringLiteral("CSS class '.%1' is used in %2, but is missing from Work Skin.").arg(cls, chNames.join(QStringLiteral(", ")));
        } else {
            item.description = QStringLiteral("CSS class '.%1' is used across %2 chapters, but is missing from Work Skin.").arg(cls).arg(chList.size());
        }
        item.chapterIndex = chList.first();
        item.affectedChapters = chList;
        problems.append(item);
        warningCount++;
    }

    emit jobFinished(job.jobId, problems, errorCount, warningCount, timer.elapsed());
}

// -------------------------------------------------------------
// ProjectDiagnosticsRunner implementation (manager on GUI thread)
// -------------------------------------------------------------
ProjectDiagnosticsRunner &ProjectDiagnosticsRunner::instance()
{
    static ProjectDiagnosticsRunner s_instance;
    return s_instance;
}

ProjectDiagnosticsRunner::ProjectDiagnosticsRunner(QObject *parent)
    : QObject(parent)
{
    m_watchdogTimer = new QTimer(this);
    m_watchdogTimer->setSingleShot(true);
    connect(m_watchdogTimer, &QTimer::timeout, this, &ProjectDiagnosticsRunner::onWatchdogTimeout);

    setupWorkerThread();
}

ProjectDiagnosticsRunner::~ProjectDiagnosticsRunner()
{
    if (m_watchdogTimer) {
        m_watchdogTimer->stop();
    }
    if (m_thread) {
        m_thread->quit();
        m_thread->wait(500);
        delete m_thread;
        m_thread = nullptr;
    }
}

void ProjectDiagnosticsRunner::setupWorkerThread()
{
    m_thread = new QThread(this);
    m_worker = new DiagnosticsWorker();
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &DiagnosticsWorker::jobFinished, this, &ProjectDiagnosticsRunner::onJobFinished, Qt::QueuedConnection);

    // Platform-specific process / thread optimization
    connect(m_thread, &QThread::started, [this]() {
#if defined(Q_OS_LINUX)
        pthread_setname_np(pthread_self(), "diag-worker");
        // Lower scheduling priority so main GUI thread is always favored
        setpriority(PRIO_PROCESS, 0, 10);
#elif defined(Q_OS_WIN)
        SetThreadDescription(GetCurrentThread(), L"Ensemble Diagnostics Worker");
#endif
    });

    m_thread->start(QThread::LowPriority);
}

void ProjectDiagnosticsRunner::requestDiagnostics(const QString &workSkinCss,
                                                  const QStringList &cssClasses,
                                                  const QList<DiagnosticsChapterSnapshot> &chapters)
{
    m_activeJobId = m_nextJobId++;
    m_running.store(true);

    DiagnosticsJob job;
    job.jobId = m_activeJobId;
    job.workSkinCss = workSkinCss;
    job.cssClasses = cssClasses;
    job.chapters = chapters;

    // Arm watchdog for 8 seconds
    if (m_watchdogTimer) {
        m_watchdogTimer->start(8000);
    }

    QMetaObject::invokeMethod(m_worker, "processJob", Qt::QueuedConnection,
                              Q_ARG(DiagnosticsJob, job));
}

void ProjectDiagnosticsRunner::onJobFinished(quint64 jobId, const QList<ProblemItem> &problems, int errorCount, int warningCount, qint64 elapsedMs)
{
    if (jobId != m_activeJobId) {
        return; // Ignore stale job result
    }

    if (m_watchdogTimer) {
        m_watchdogTimer->stop();
    }

    m_running.store(false);
    m_lastRunMs.store(elapsedMs);
    m_totalRuns.fetch_add(1);

    emit diagnosticsReady(problems, errorCount, warningCount);
}

void ProjectDiagnosticsRunner::onWatchdogTimeout()
{
    qWarning() << "[DiagnosticsWatchdog] Background diagnostics worker stuck in loop (> 8s timeout). Forcibly terminating thread.";
    m_terminationsCount.fetch_add(1);

    killAndRestartWorkerThread(QStringLiteral("Execution exceeded 8-second watchdog timeout (infinite loop or catastrophic backtracking)"));

    m_running.store(false);

    // Alert user that core component had to be terminated
    ProblemItem alert;
    alert.severity = ProblemItem::Error;
    alert.category = QStringLiteral("Core Watchdog Alert");
    alert.description = QStringLiteral("⚠️ Terminated problematic core component: 'Project Diagnostics Worker' was stuck in an execution loop (>8s) and was terminated to prevent application lockup.");
    alert.chapterIndex = -1;

    emit componentTerminated(QStringLiteral("Core Component (Project Diagnostics Engine)"), false,
                             QStringLiteral("Exceeded 8s timeout (stuck loop or parsing deadlock)"));

    emit diagnosticsReady({ alert }, 1, 0);
}

void ProjectDiagnosticsRunner::killAndRestartWorkerThread(const QString &reason)
{
    Q_UNUSED(reason);

    if (m_thread) {
        m_thread->requestInterruption();
        if (!m_thread->wait(300)) {
            m_thread->terminate();
            m_thread->wait(200);
        }
        delete m_thread;
        m_thread = nullptr;
        m_worker = nullptr;
    }

    setupWorkerThread();
}
