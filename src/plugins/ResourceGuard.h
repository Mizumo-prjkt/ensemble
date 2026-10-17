#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace EnsemblePlugin {

/**
 * @brief Exception thrown when a script violates sandboxing limits or performs an invalid operation.
 */
class ScriptTrap : public std::runtime_error {
public:
    explicit ScriptTrap(const std::string &message, int line = -1, int column = -1);

    int line() const noexcept { return m_line; }
    int column() const noexcept { return m_column; }
    const std::string &rawMessage() const noexcept { return m_rawMessage; }

private:
    int m_line = -1;
    int m_column = -1;
    std::string m_rawMessage;
};

/**
 * @brief Configurable execution and resource limits for a plugin sandbox.
 */
struct ResourceLimits {
    size_t maxMemoryBytes = 8 * 1024 * 1024;       ///< Default: 8 MB memory cap
    uint64_t maxAstSteps = 150000;                 ///< Default: 150,000 AST steps per hook
    int64_t maxExecutionTimeMs = 250;              ///< Default: 250 ms wall-clock timeout per hook
    int maxCallDepth = 64;                         ///< Default: 64 stack frames
    size_t maxDiskStorageBytes = 5 * 1024 * 1024;   ///< Default: 5 MB storage quota
};

/**
 * @brief Guard class that tracks memory consumption, instruction steps,
 * recursion depth, and execution timeouts for a plugin instance.
 */
class ResourceGuard {
public:
    explicit ResourceGuard(const ResourceLimits &limits = ResourceLimits());

    void resetForHook();

    void tick(int line = -1, int column = -1);

    void alloc(size_t bytes, int line = -1, int column = -1);
    void free(size_t bytes) noexcept;

    void enterFunction(int line = -1, int column = -1);
    void leaveFunction() noexcept;

    size_t currentMemory() const noexcept { return m_currentMemoryBytes; }
    uint64_t currentSteps() const noexcept { return m_currentAstSteps; }
    int currentCallDepth() const noexcept { return m_currentCallDepth; }
    const ResourceLimits &limits() const noexcept { return m_limits; }
    void setLimits(const ResourceLimits &limits) { m_limits = limits; }

private:
    ResourceLimits m_limits;
    size_t m_currentMemoryBytes = 0;
    uint64_t m_currentAstSteps = 0;
    int m_currentCallDepth = 0;
    std::chrono::steady_clock::time_point m_startTime;
    bool m_watchdogActive = false;
};

/**
 * @brief RAII helper for tracking call stack depth.
 */
class CallFrameGuard {
public:
    CallFrameGuard(ResourceGuard &guard, int line = -1, int column = -1)
        : m_guard(guard) {
        m_guard.enterFunction(line, column);
    }
    ~CallFrameGuard() {
        m_guard.leaveFunction();
    }

    CallFrameGuard(const CallFrameGuard &) = delete;
    CallFrameGuard &operator=(const CallFrameGuard &) = delete;

private:
    ResourceGuard &m_guard;
};

} // namespace EnsemblePlugin
