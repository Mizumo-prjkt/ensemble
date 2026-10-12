#pragma once

class McpBridge
{
public:
    // Stdio <-> WebSocket bridge for MCP clients (stdin/stdout)
    static int runBridge();

    // Query and report active MCP server status & loopback reachability
    static int runStatusCheck();
};
