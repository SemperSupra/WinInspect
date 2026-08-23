import { describe, it, expect } from 'vitest';
import { TOOL_DEFINITIONS, SafetyTier } from './tools.js';

describe('Tool Definitions', () => {
  it('should have unique tool names', () => {
    const names = TOOL_DEFINITIONS.map((t) => t.name);
    expect(new Set(names).size).toBe(names.length);
  });

  it('should map to valid daemon methods', () => {
    for (const tool of TOOL_DEFINITIONS) {
      expect(tool.daemonMethod).toMatch(/^[a-z_]+\.[a-zA-Z]+$/);
      expect(tool.name).toMatch(/^[a-z_]+$/);
    }
  });

  it('should have valid safety tiers', () => {
    for (const tool of TOOL_DEFINITIONS) {
      expect([SafetyTier.AlwaysAvailable, SafetyTier.RequiresConfirmation]).toContain(tool.tier);
    }
  });

  it('should require hwnd for window-specific tools', () => {
    const windowTools = TOOL_DEFINITIONS.filter(
      (t) =>
        (t.name.startsWith('get_window_') && t.name !== 'get_window_tree') ||
        t.name === 'highlight_window' ||
        t.name === 'ensure_visible' ||
        t.name === 'ensure_foreground' ||
        t.name === 'get_window_zorder'
    );
    for (const tool of windowTools) {
      expect(tool.inputSchema.required, `${tool.name} should require hwnd`).toContain('hwnd');
    }
  });
});
