/**
 * WinInspect TypeScript SDK — programmatic desktop inspection and control.
 *
 * Usage:
 *   const wi = new WinInspect();
 *   await wi.connect();
 *   const info = await wi.daemonIdentity();
 *   const windows = await wi.listWindows();
 *   await wi.close();
 */
import { DaemonClient } from './client.js';

export interface WindowInfo {
  hwnd: string;
  title: string;
  class: string;
  rect?: { x: number; y: number; width: number; height: number };
}

export interface ProcessInfo {
  pid: number;
  name: string;
  memory_kb?: number;
}

export interface DaemonIdentity {
  uuid: string;
  name: string;
  version: string;
  deployment: string;
  license: string;
  os: string;
}

export interface DaemonStatus {
  version: string;
  daemon_state: string;
  features: Record<string, boolean>;
}

export interface ControlStatus {
  current_controller: string;
  controller_id: string;
  mode: string;
}

export class WinInspect {
  private client: DaemonClient;

  constructor(host = '127.0.0.1', port = 1985) {
    this.client = new DaemonClient(host, port);
  }

  async connect(): Promise<void> {
    await this.client.connect();
  }

  async close(): Promise<void> {
    this.client.close();
  }

  // ── Query Methods ─────────────────────────────────────────────────────

  async listWindows(): Promise<WindowInfo[]> {
    const res = await this.client.send('window.listTop');
    return (res.result?.windows ?? []) as WindowInfo[];
  }

  async getWindowInfo(hwnd: string): Promise<WindowInfo> {
    const res = await this.client.send('window.getInfo', { hwnd });
    return res.result as unknown as WindowInfo;
  }

  async listChildren(hwnd: string): Promise<WindowInfo[]> {
    const res = await this.client.send('window.listChildren', { hwnd });
    return (res.result?.windows ?? []) as WindowInfo[];
  }

  async getWindowTree(hwnd?: string): Promise<Record<string, unknown>> {
    const res = await this.client.send('window.getTree', hwnd ? { hwnd } : {});
    return res.result as Record<string, unknown>;
  }

  async findWindows(titleRegex?: string, classRegex?: string): Promise<WindowInfo[]> {
    const params: Record<string, string> = {};
    if (titleRegex) params.title_regex = titleRegex;
    if (classRegex) params.class_regex = classRegex;
    const res = await this.client.send('window.findRegex', params);
    return (res.result?.matches ?? []) as WindowInfo[];
  }

  async getPixel(x: number, y: number): Promise<{ r: number; g: number; b: number; hex: string }> {
    const res = await this.client.send('screen.getPixel', { x, y });
    return res.result as { r: number; g: number; b: number; hex: string };
  }

  async desktopInfo(): Promise<Record<string, unknown>> {
    const res = await this.client.send('screen.desktopInfo');
    return res.result as Record<string, unknown>;
  }

  async daemonIdentity(): Promise<DaemonIdentity> {
    const res = await this.client.send('daemon.identity');
    return res.result as unknown as DaemonIdentity;
  }

  async daemonHealth(): Promise<Record<string, unknown>> {
    const res = await this.client.send('daemon.health');
    return res.result as Record<string, unknown>;
  }

  async daemonCapabilities(): Promise<Record<string, unknown>> {
    const res = await this.client.send('daemon.capabilities');
    return res.result as Record<string, unknown>;
  }

  async daemonStatus(): Promise<DaemonStatus> {
    const res = await this.client.send('daemon.status');
    return res.result as unknown as DaemonStatus;
  }

  async listProcesses(): Promise<ProcessInfo[]> {
    const res = await this.client.send('process.list');
    return (res.result?.processes ?? []) as ProcessInfo[];
  }

  async getEnv(): Promise<Record<string, string>> {
    const res = await this.client.send('env.get');
    return res.result as Record<string, string>;
  }

  async clipRead(): Promise<string> {
    const res = await this.client.send('clipboard.read');
    return (res.result as Record<string, string>)?.text ?? '';
  }

  async controlStatus(): Promise<ControlStatus> {
    const res = await this.client.send('control.status');
    return res.result as unknown as ControlStatus;
  }

  // ── Mutation Methods ──────────────────────────────────────────────────

  async mouseClick(x: number, y: number, button = 'left'): Promise<boolean> {
    const res = await this.client.send('input.mouseClick', { x, y, button });
    return res.ok;
  }

  async mouseDrag(startX: number, startY: number, endX: number, endY: number, button = 'left'): Promise<boolean> {
    const res = await this.client.send('input.mouseDrag', { start_x: startX, start_y: startY, end_x: endX, end_y: endY, button });
    return res.ok;
  }

  async sendKeys(keys: string): Promise<boolean> {
    const res = await this.client.send('input.hotkey', { keys });
    return res.ok;
  }

  async typeText(text: string): Promise<boolean> {
    const res = await this.client.send('input.text', { text });
    return res.ok;
  }

  async highlightWindow(hwnd: string): Promise<boolean> {
    const res = await this.client.send('window.highlight', { hwnd });
    return res.ok;
  }

  async ensureVisible(hwnd: string, visible = true): Promise<boolean> {
    const res = await this.client.send('window.ensureVisible', { hwnd, visible });
    return res.ok;
  }

  async ensureForeground(hwnd: string): Promise<boolean> {
    const res = await this.client.send('window.ensureForeground', { hwnd });
    return res.ok;
  }

  async executeProcess(path: string, args?: string): Promise<{ pid: number }> {
    const params: Record<string, string> = { path };
    if (args) params.args = args;
    const res = await this.client.send('process.execute', params);
    return res.result as { pid: number };
  }

  async killProcess(pid: number): Promise<boolean> {
    const res = await this.client.send('process.kill', { pid });
    return res.ok;
  }

  // ── Control Methods ───────────────────────────────────────────────────

  async takeControl(type: 'human' | 'agent' | 'script', id = ''): Promise<boolean> {
    const res = await this.client.send('control.take', { controller: type, id });
    return res.ok;
  }

  async releaseControl(): Promise<boolean> {
    const res = await this.client.send('control.release');
    return res.ok;
  }

  // ── Snapshot Methods ──────────────────────────────────────────────────

  async captureSnapshot(): Promise<string> {
    const res = await this.client.send('snapshot.capture');
    return (res.result as Record<string, string>)?.snapshot_id ?? '';
  }

  async captureScreen(left: number, top: number, right: number, bottom: number): Promise<string> {
    const res = await this.client.send('screen.capture', { left, top, right, bottom });
    return (res.result as Record<string, string>)?.data_b64 ?? '';
  }
}
