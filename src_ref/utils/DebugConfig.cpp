// Author: SeungJae Lee
// DebugConfig: lightweight toggle for runtime-controlled debug logging.

#include "DebugConfig.h"

#include <atomic>

namespace
{
std::atomic<bool> g_debugLoggingEnabled{false};
}

void DebugConfig::setDebugLoggingEnabled(bool enabled)
{
    g_debugLoggingEnabled.store(enabled, std::memory_order_relaxed);
}

bool DebugConfig::isDebugLoggingEnabled()
{
    return g_debugLoggingEnabled.load(std::memory_order_relaxed);
}
