/**
 * Integration tests for the wininspect-mcp relay.
 * Uses a mock daemon to verify the full tool lifecycle.
 */
import { describe, it, expect, beforeAll, afterAll } from 'vitest';
import * as net from 'net';
import { DaemonClient } from './daemon-client.js';

// ── Mock Daemon ────────────────────────────────────────────────────────────
// A minimal TCP server that speaks the WinInspect protocol.
// Sends a hello, then responds to each request with a canned response.

const MOCK_PORT = 19185;
let mockServer: net.Server | null = null;

function startMockDaemon(): Promise<void> {
  return new Promise((resolve) => {
    mockServer = net.createServer((socket) => {
      // Send hello
      const hello = JSON.stringify({ type: 'hello', version: '1.0' }) + '\n';
      socket.write(hello);

      let buffer = '';
      socket.on('data', (data) => {
        buffer += data.toString();
        const lines = buffer.split('\n');
        for (let i = 0; i < lines.length - 1; i++) {
          try {
            const req = JSON.parse(lines[i]);
            // Build response based on method
            let result: Record<string, unknown> = {};
            switch (req.method) {
              case 'daemon.identity':
                result = {
                  uuid: 'mock-uuid-1234',
                  name: 'MockDaemon',
                  version: 'v0.4.0',
                  deployment: 'interactive',
                  license: 'noncommercial',
                  os: 'windows 11',
                };
                break;
              case 'daemon.health':
                result = { status: 'ok', uptime_sec: 3600 };
                break;
              case 'daemon.capabilities':
                result = {
                  os: 'windows 11',
                  arch: 'x64',
                  features: { uia: true, clipboard: true, dxgi_capture: true },
                };
                break;
              case 'window.listTop':
                result = {
                  windows: [
                    { hwnd: '0x1234', title: 'Test Window', class: 'TestClass', rect: { x: 0, y: 0, width: 800, height: 600 } },
                    { hwnd: '0x5678', title: 'Another Window', class: 'OtherClass' },
                  ],
                };
                break;
              case 'window.getInfo':
                result = { hwnd: req.params?.hwnd || '0x0', title: 'Info Test', class: 'InfoClass' };
                break;
              case 'process.list':
                result = {
                  processes: [
                    { pid: 1, name: 'System', memory_kb: 1024 },
                    { pid: 100, name: 'wininspectd.exe', memory_kb: 25000 },
                  ],
                };
                break;
              case 'screen.desktopInfo':
                result = { width: 1920, height: 1080, dpi: 96, bits_per_pixel: 32, monitor_count: 2 };
                break;
              case 'input.mouseClick':
              case 'input.hotkey':
              case 'input.text':
              case 'process.execute':
              case 'window.highlight':
              case 'window.ensureVisible':
              case 'window.ensureForeground':
                result = { ok: true };
                break;
              default:
                result = { ok: true, note: `mock response for ${req.method}` };
            }
            const resp = JSON.stringify({ id: req.id, ok: true, result }) + '\n';
            socket.write(resp);
          } catch { /* partial */ }
        }
      });
    });

    mockServer.listen(MOCK_PORT, '127.0.0.1', () => resolve());
  });
}

function stopMockDaemon(): Promise<void> {
  return new Promise((resolve) => {
    if (mockServer) {
      mockServer.close(() => {
        mockServer = null;
        resolve();
      });
      // Force close after 1s if server doesn't close cleanly
      setTimeout(() => { mockServer = null; resolve(); }, 1000);
    } else {
      resolve();
    }
  });
}

// ── Tests ──────────────────────────────────────────────────────────────────

describe('DaemonClient Integration', () => {
  beforeAll(async () => {
    await startMockDaemon();
  }, 5000);

  afterAll(async () => {
    await stopMockDaemon();
  });

  it('should connect to mock daemon', async () => {
    const client = new DaemonClient('127.0.0.1', MOCK_PORT);
    await client.connect(2000);
    client.close();
  });

  it('should get daemon identity', async () => {
    const client = new DaemonClient('127.0.0.1', MOCK_PORT);
    await client.connect(2000);
    const resp = await client.send('daemon.identity');
    expect(resp.ok).toBe(true);
    expect(resp.result).toBeDefined();
    expect((resp.result as Record<string, string>).uuid).toBe('mock-uuid-1234');
    client.close();
  });

  it('should list top windows', async () => {
    const client = new DaemonClient('127.0.0.1', MOCK_PORT);
    await client.connect(2000);
    const resp = await client.send('window.listTop');
    expect(resp.ok).toBe(true);
    const windows = (resp.result as Record<string, unknown[]>).windows;
    expect(windows).toHaveLength(2);
    expect((windows[0] as Record<string, string>).title).toBe('Test Window');
    client.close();
  });

  it('should list processes', async () => {
    const client = new DaemonClient('127.0.0.1', MOCK_PORT);
    await client.connect(2000);
    const resp = await client.send('process.list');
    expect(resp.ok).toBe(true);
    const procs = (resp.result as Record<string, unknown[]>).processes;
    expect(procs).toHaveLength(2);
    expect((procs[0] as Record<string, unknown>).pid).toBe(1);
    client.close();
  });

  it('should get desktop info', async () => {
    const client = new DaemonClient('127.0.0.1', MOCK_PORT);
    await client.connect(2000);
    const resp = await client.send('screen.desktopInfo');
    expect(resp.ok).toBe(true);
    const info = resp.result as Record<string, number>;
    expect(info.width).toBe(1920);
    expect(info.height).toBe(1080);
    client.close();
  });

  it('should execute mutation commands', async () => {
    const client = new DaemonClient('127.0.0.1', MOCK_PORT);
    await client.connect(2000);
    const resp = await client.send('input.mouseClick', { x: 100, y: 200 });
    expect(resp.ok).toBe(true);
    client.close();
  });

  it('should handle unknown methods gracefully', async () => {
    const client = new DaemonClient('127.0.0.1', MOCK_PORT);
    await client.connect(2000);
    const resp = await client.send('nonexistent.method');
    expect(resp.ok).toBe(true); // mock returns ok for unknown
    client.close();
  });

  it('should timeout on slow response', async () => {
    const client = new DaemonClient('127.0.0.1', MOCK_PORT, 1); // 1ms timeout
    await client.connect(2000);
    const resp = await client.send('daemon.identity');
    expect(resp.ok).toBe(false);
    expect(resp.error_message).toContain('timeout');
    client.close();
  });

  it('should handle rapid sequential calls', async () => {
    const client = new DaemonClient('127.0.0.1', MOCK_PORT);
    await client.connect(2000);
    for (let i = 0; i < 10; i++) {
      const resp = await client.send('daemon.health');
      expect(resp.ok).toBe(true);
    }
    client.close();
  });

  it('should handle concurrent calls', async () => {
    const client = new DaemonClient('127.0.0.1', MOCK_PORT);
    await client.connect(2000);
    const results = await Promise.all([
      client.send('daemon.identity'),
      client.send('daemon.health'),
      client.send('window.listTop'),
      client.send('process.list'),
    ]);
    for (const resp of results) {
      expect(resp.ok).toBe(true);
    }
    client.close();
  });
});
