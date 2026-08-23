/**
 * Low-level TCP client for the WinInspect daemon protocol.
 * Handles connection, auth (if needed), and JSON-RPC-style messaging.
 */
import * as net from 'net';
import { createHash, randomBytes } from 'crypto';

export interface DaemonResponse {
  ok: boolean;
  result?: Record<string, unknown>;
  error_message?: string;
}

export class DaemonClient {
  private socket: net.Socket | null = null;
  private buffer = '';

  constructor(
    private host: string = '127.0.0.1',
    private port: number = 1985
  ) {}

  async connect(timeoutMs = 5000): Promise<void> {
    return new Promise((resolve, reject) => {
      const sock = net.createConnection({ host: this.host, port: this.port }, () => {
        sock.once('data', () => resolve()); // consume hello
      });
      sock.once('error', reject);
      sock.setTimeout(timeoutMs, () => reject(new Error('Connection timeout')));
      this.socket = sock;
    });
  }

  async send(method: string, params: Record<string, unknown> = {}): Promise<DaemonResponse> {
    if (!this.socket) throw new Error('Not connected. Call connect() first.');

    return new Promise((resolve) => {
      const id = `wi-${Date.now()}-${randomBytes(4).toString('hex')}`;
      const request = JSON.stringify({
        id,
        method,
        params: { ...params, protocol_version: '1.0' },
      });

      const sock = this.socket!;
      const handler = (data: Buffer) => {
        this.buffer += data.toString('utf-8');
        const lines = this.buffer.split('\n');
        for (let i = 0; i < lines.length - 1; i++) {
          try {
            const parsed = JSON.parse(lines[i]);
            if (parsed.id === id) {
              this.buffer = lines.slice(i + 1).join('\n');
              sock.removeListener('data', handler);
              resolve({
                ok: parsed.ok ?? false,
                result: parsed.result,
                error_message: parsed.error_message,
              });
              return;
            }
          } catch { /* incomplete */ }
        }
      };

      sock.on('data', handler);
      sock.write(request + '\n', 'utf-8');
    });
  }

  close(): void {
    this.socket?.destroy();
    this.socket = null;
  }
}
