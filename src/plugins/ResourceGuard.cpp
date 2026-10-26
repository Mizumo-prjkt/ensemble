#include "ResourceGuard.h"
#include <sstream>

namespace EnsemblePlugin {

static std::string formatTrapMessage(const std::string &msg, int line, int col) {
    std::ostringstream ss;
    if (line > 0) {
        ss << "[line " << line;
        if (col > 0) {
            ss << ":" << col;
        }
        ss << "] ScriptTrap: ";
    } else {
        ss << "ScriptTrap: ";
    }
    ss << msg;
    return ss.str();
}

ScriptTrap::ScriptTrap(const std::string &message, int line, int column)
    : std::runtime_error(formatTrapMessage(message, line, column))
    , m_line(line)
    , m_column(column)
    , m_rawMessage(message)
{
}

ResourceGuard::ResourceGuard(const ResourceLimits &limits)
    : m_limits(limits)
    , m_startTime(std::chrono::steady_clock::now())
{
}

void ResourceGuard::resetForHook() {
    m_currentAstSteps = 0;
    m_currentCallDepth = 0;
    m_startTime = std::chrono::steady_clock::now();
    m_watchdogActive = true;
}

void ResourceGuard::tick(int line, int column) {
    ++m_currentAstSteps;
    if (m_currentAstSteps > m_limits.maxAstSteps) {
        throw ScriptTrap("Instruction step limit exceeded (" + std::to_string(m_limits.maxAstSteps) + " steps)", line, column);
    }

    // Check timeout periodically (every 128 ticks) to minimize clock query overhead
    if ((m_currentAstSteps & 0x7F) == 0 && m_watchdogActive) {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_startTime).count();
        if (elapsedMs > m_limits.maxExecutionTimeMs) {
            throw ScriptTrap("Execution timeout / infinite loop (" + std::to_string(m_limits.maxExecutionTimeMs) + "ms exceeded)", line, column);
        }
    }
}

void ResourceGuard::alloc(size_t bytes, int line, int column) {
    if (bytes == 0) return;

    size_t curr = m_currentMemoryBytes.load(std::memory_order_relaxed);
    if (curr + bytes > m_limits.maxMemoryBytes) {
        throw ScriptTrap("Memory limit exceeded (requested " + std::to_string(bytes) + " bytes, limit is " +
                         std::to_string(m_limits.maxMemoryBytes / (1024 * 1024)) + " MB)", line, column);
    }
    m_currentMemoryBytes.fetch_add(bytes, std::memory_order_relaxed);
}

void ResourceGuard::free(size_t bytes) noexcept {
    if (bytes == 0) return;
    size_t curr = m_currentMemoryBytes.load(std::memory_order_relaxed);
    if (bytes >= curr) {
        m_currentMemoryBytes.store(0, std::memory_order_relaxed);
    } else {
        m_currentMemoryBytes.fetch_sub(bytes, std::memory_order_relaxed);
    }
}

void ResourceGuard::enterFunction(int line, int column) {
    ++m_currentCallDepth;
    if (m_currentCallDepth > m_limits.maxCallDepth) {
        throw ScriptTrap("Maximum recursion depth exceeded (" + std::to_string(m_limits.maxCallDepth) + " frames)", line, column);
    }
}

void ResourceGuard::leaveFunction() noexcept {
    if (m_currentCallDepth > 0) {
        --m_currentCallDepth;
    }
}

} // namespace EnsemblePlugin
