/**
 * MCP tool definitions for the WinInspect daemon.
 *
 * Follows the safety tier classification from docs/mcp-control-hierarchy.md:
 *   Tier 1: Always available
 *   Tier 2: Requires confirmation
 *   Tier 3: Excluded (not exposed)
 */

export enum SafetyTier {
  AlwaysAvailable = 1,
  RequiresConfirmation = 2,
}

export interface ToolDefinition {
  name: string;
  description: string;
  daemonMethod: string;
  tier: SafetyTier;
  inputSchema: {
    type: 'object';
    properties: Record<string, unknown>;
    required?: string[];
  };
}

export const TOOL_DEFINITIONS: ToolDefinition[] = [
  // ── Tier 1: Always Available ────────────────────────────────────────────
  {
    name: 'list_windows',
    description: 'List all top-level windows on the desktop',
    daemonMethod: 'window.listTop',
    tier: SafetyTier.AlwaysAvailable,
    inputSchema: {
      type: 'object',
      properties: {},
    },
  },
  {
    name: 'get_window_info',
    description: 'Get detailed information about a specific window',
    daemonMethod: 'window.getInfo',
    tier: SafetyTier.AlwaysAvailable,
    inputSchema: {
      type: 'object',
      properties: {
        hwnd: { type: 'string', description: 'Window handle (hex format, e.g. 0x1234)' },
      },
      required: ['hwnd'],
    },
  },
  {
    name: 'get_window_children',
    description: 'List child windows of a given window',
    daemonMethod: 'window.listChildren',
    tier: SafetyTier.AlwaysAvailable,
    inputSchema: {
      type: 'object',
      properties: {
        hwnd: { type: 'string', description: 'Parent window handle' },
      },
      required: ['hwnd'],
    },
  },
  {
    name: 'get_window_tree',
    description: 'Get the full window tree rooted at a given window or the desktop',
    daemonMethod: 'window.getTree',
    tier: SafetyTier.AlwaysAvailable,
    inputSchema: {
      type: 'object',
      properties: {
        hwnd: { type: 'string', description: 'Root window handle (optional, defaults to desktop)' },
      },
    },
  },
  {
    name: 'find_windows',
    description: 'Find windows matching a title or class regex pattern',
    daemonMethod: 'window.findRegex',
    tier: SafetyTier.AlwaysAvailable,
    inputSchema: {
      type: 'object',
      properties: {
        title_regex: { type: 'string', description: 'Regex pattern to match window titles' },
        class_regex: { type: 'string', description: 'Regex pattern to match window class names' },
      },
    },
  },
  {
    name: 'screen_info',
    description: 'Get desktop information including resolution, DPI, and monitor count',
    daemonMethod: 'screen.desktopInfo',
    tier: SafetyTier.AlwaysAvailable,
    inputSchema: {
      type: 'object',
      properties: {},
    },
  },
  {
    name: 'get_pixel',
    description: 'Get the color of a pixel at specific screen coordinates',
    daemonMethod: 'screen.getPixel',
    tier: SafetyTier.AlwaysAvailable,
    inputSchema: {
      type: 'object',
      properties: {
        x: { type: 'number', description: 'X coordinate' },
        y: { type: 'number', description: 'Y coordinate' },
      },
      required: ['x', 'y'],
    },
  },
  {
    name: 'daemon_identity',
    description: 'Get the daemon identity including version, name, and license info',
    daemonMethod: 'daemon.identity',
    tier: SafetyTier.AlwaysAvailable,
    inputSchema: {
      type: 'object',
      properties: {},
    },
  },
  {
    name: 'daemon_health',
    description: 'Get daemon health status and capabilities',
    daemonMethod: 'daemon.health',
    tier: SafetyTier.AlwaysAvailable,
    inputSchema: {
      type: 'object',
      properties: {},
    },
  },
  {
    name: 'daemon_capabilities',
    description: 'Get daemon capabilities (features, OS, hardware info)',
    daemonMethod: 'daemon.capabilities',
    tier: SafetyTier.AlwaysAvailable,
    inputSchema: {
      type: 'object',
      properties: {},
    },
  },
  {
    name: 'process_list',
    description: 'List all running processes',
    daemonMethod: 'process.list',
    tier: SafetyTier.AlwaysAvailable,
    inputSchema: {
      type: 'object',
      properties: {},
    },
  },
  {
    name: 'get_window_zorder',
    description: 'Get the Z-order of a window',
    daemonMethod: 'window.getZOrder',
    tier: SafetyTier.AlwaysAvailable,
    inputSchema: {
      type: 'object',
      properties: {
        hwnd: { type: 'string', description: 'Window handle' },
      },
      required: ['hwnd'],
    },
  },
  {
    name: 'daemon_status',
    description: 'Get daemon status including running state and feature availability',
    daemonMethod: 'daemon.status',
    tier: SafetyTier.AlwaysAvailable,
    inputSchema: {
      type: 'object',
      properties: {},
    },
  },

  // ── Tier 2: Requires Confirmation ───────────────────────────────────────
  {
    name: 'mouse_click',
    description: 'Click the mouse at specified screen coordinates',
    daemonMethod: 'input.mouseClick',
    tier: SafetyTier.RequiresConfirmation,
    inputSchema: {
      type: 'object',
      properties: {
        x: { type: 'number', description: 'X coordinate' },
        y: { type: 'number', description: 'Y coordinate' },
        button: { type: 'string', description: 'Mouse button (left, right, middle)', enum: ['left', 'right', 'middle'] },
      },
      required: ['x', 'y'],
    },
  },
  {
    name: 'type_text',
    description: 'Type text into the currently focused window',
    daemonMethod: 'input.text',
    tier: SafetyTier.RequiresConfirmation,
    inputSchema: {
      type: 'object',
      properties: {
        text: { type: 'string', description: 'Text to type' },
      },
      required: ['text'],
    },
  },
  {
    name: 'send_keys',
    description: 'Send a keyboard shortcut (e.g. Ctrl+C, Alt+Tab)',
    daemonMethod: 'input.hotkey',
    tier: SafetyTier.RequiresConfirmation,
    inputSchema: {
      type: 'object',
      properties: {
        keys: { type: 'string', description: 'Key combination (e.g. "Ctrl+C", "Alt+Tab")' },
      },
      required: ['keys'],
    },
  },
  {
    name: 'mouse_drag',
    description: 'Drag the mouse from one point to another',
    daemonMethod: 'input.mouseDrag',
    tier: SafetyTier.RequiresConfirmation,
    inputSchema: {
      type: 'object',
      properties: {
        start_x: { type: 'number', description: 'Starting X coordinate' },
        start_y: { type: 'number', description: 'Starting Y coordinate' },
        end_x: { type: 'number', description: 'Ending X coordinate' },
        end_y: { type: 'number', description: 'Ending Y coordinate' },
        button: { type: 'string', description: 'Mouse button', enum: ['left', 'right', 'middle'] },
      },
      required: ['start_x', 'start_y', 'end_x', 'end_y'],
    },
  },
  {
    name: 'highlight_window',
    description: 'Visually highlight a window on the screen',
    daemonMethod: 'window.highlight',
    tier: SafetyTier.RequiresConfirmation,
    inputSchema: {
      type: 'object',
      properties: {
        hwnd: { type: 'string', description: 'Window handle to highlight' },
      },
      required: ['hwnd'],
    },
  },
  {
    name: 'ensure_visible',
    description: 'Ensure a window is visible (scroll into view if needed)',
    daemonMethod: 'window.ensureVisible',
    tier: SafetyTier.RequiresConfirmation,
    inputSchema: {
      type: 'object',
      properties: {
        hwnd: { type: 'string', description: 'Window handle' },
      },
      required: ['hwnd'],
    },
  },
  {
    name: 'ensure_foreground',
    description: 'Bring a window to the foreground',
    daemonMethod: 'window.ensureForeground',
    tier: SafetyTier.RequiresConfirmation,
    inputSchema: {
      type: 'object',
      properties: {
        hwnd: { type: 'string', description: 'Window handle' },
      },
      required: ['hwnd'],
    },
  },
  {
    name: 'process_execute',
    description: 'Execute a program or command',
    daemonMethod: 'process.execute',
    tier: SafetyTier.RequiresConfirmation,
    inputSchema: {
      type: 'object',
      properties: {
        path: { type: 'string', description: 'Path to executable' },
        args: { type: 'string', description: 'Command line arguments' },
      },
      required: ['path'],
    },
  },
  {
    name: 'image_find',
    description: 'Find an image on the screen by matching a base64-encoded bitmap',
    daemonMethod: 'image.match',
    tier: SafetyTier.RequiresConfirmation,
    inputSchema: {
      type: 'object',
      properties: {
        sub_image_b64: { type: 'string', description: 'Base64-encoded bitmap to search for' },
      },
      required: ['sub_image_b64'],
    },
  },
];
