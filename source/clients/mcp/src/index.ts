/**
 * WinInspect MCP Relay — main entry point.
 *
 * Connects to the WinInspect daemon and exposes methods as MCP tools.
 * Uses the official @modelcontextprotocol/sdk.
 */
import { Server } from '@modelcontextprotocol/sdk/server/index.js';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import {
  CallToolRequestSchema,
  ListToolsRequestSchema,
  McpError,
  ErrorCode,
} from '@modelcontextprotocol/sdk/types.js';
import { DaemonClient } from './daemon-client.js';
import { TOOL_DEFINITIONS, SafetyTier, ToolDefinition } from './tools.js';

const DAEMON_HOST = process.env.WININSPECT_HOST || '127.0.0.1';
const DAEMON_PORT = parseInt(process.env.WININSPECT_PORT || '1985', 10);

class WinInspectMcpServer {
  private server: Server;
  private daemon: DaemonClient;

  constructor() {
    this.server = new Server(
      { name: 'wininspect-mcp', version: '0.1.0' },
      { capabilities: { tools: {} } }
    );

    this.daemon = new DaemonClient(DAEMON_HOST, DAEMON_PORT);

    this.setupToolHandlers();
    this.setupErrorHandling();
  }

  private setupErrorHandling(): void {
    this.server.onerror = (error) => console.error('[MCP Error]', error);
    process.on('SIGINT', async () => {
      await this.daemon.close();
      await this.server.close();
      process.exit(0);
    });
  }

  private setupToolHandlers(): void {
    // List available tools
    this.server.setRequestHandler(ListToolsRequestSchema, async () => ({
      tools: TOOL_DEFINITIONS.map((t) => ({
        name: t.name,
        description: t.description,
        inputSchema: t.inputSchema,
        annotations: t.tier === SafetyTier.RequiresConfirmation
          ? { dangerous: true }
          : undefined,
      })),
    }));

    // Call a tool
    this.server.setRequestHandler(CallToolRequestSchema, async (request) => {
      const toolName = request.params.name;
      const args = request.params.arguments ?? {};

      const toolDef = TOOL_DEFINITIONS.find((t) => t.name === toolName);
      if (!toolDef) {
        throw new McpError(ErrorCode.MethodNotFound, `Unknown tool: ${toolName}`);
      }

      // Check safety tier — dangerous tools are flagged via annotations
      // The MCP host (Claude Desktop) shows a confirmation dialog based on
      // the `dangerous` annotation. We also notify the daemon for audit.
      if (toolDef.tier === SafetyTier.RequiresConfirmation) {
        console.error(`[AUDIT] Executing Tier 2 (dangerous) tool: ${toolName}`, JSON.stringify(args));
        // Notify the daemon (best-effort, don't block the tool call)
        this.daemon.send('daemon.notify', {
          title: `Agent: ${toolName}`,
          message: `Args: ${JSON.stringify(args)}`,
        }).catch(() => {});
      }

      return await this.executeTool(toolDef, args as Record<string, unknown>);
    });
  }

  private async executeTool(
    tool: ToolDefinition,
    args: Record<string, unknown>
  ): Promise<{ content: Array<{ type: string; text: string }> }> {
    try {
      const response = await this.daemon.send(tool.daemonMethod, args);

      if (!response.ok) {
        return {
          content: [
            {
              type: 'text',
              text: `Error: ${response.error_message || 'unknown error'}`,
            },
          ],
        };
      }

      return {
        content: [
          {
            type: 'text',
            text: JSON.stringify(response.result ?? {}, null, 2),
          },
        ],
      };
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      return {
        content: [{ type: 'text', text: `Error: ${message}` }],
      };
    }
  }

  async run(): Promise<void> {
    // Connect to daemon
    try {
      await this.daemon.connect();
      console.error(`[INFO] Connected to WinInspect daemon at ${DAEMON_HOST}:${DAEMON_PORT}`);
    } catch (error) {
      console.error('[FATAL] Could not connect to daemon:', error instanceof Error ? error.message : String(error));
      process.exit(1);
    }

    // Start MCP server via stdio
    const transport = new StdioServerTransport();
    await this.server.connect(transport);
    console.error('[INFO] WinInspect MCP relay running on stdio');
  }
}

const server = new WinInspectMcpServer();
server.run().catch(console.error);
