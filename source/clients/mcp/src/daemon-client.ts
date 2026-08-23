/**
 * TCP client for communicating with the WinInspect daemon.
 * Sends/receives JSON-RPC-like messages over a TCP socket.
 * Supports request timeout and automatic reconnection.
 */
import * as net from 'net';

export interface DaemonResponse {
  ok: boolean;
  result?: Record<string, unknown>;
  error_message?: string;
}

export class DaemonClient {
  private socket: net.Socket | null = null;
  private buffer = '';
  private closed = false;

  constructor(
    private host: string,
    private port: number,
    private requestTimeoutMs = 10000
  ) {}

  async connect(timeoutMs = 5000): Promise<void> {
    this.closed = false;
    return new Promise((resolve, reject) => {
      const sock = net.createConnection({ host: this.host, port: this.port }, () => {
        sock.once('data', () => resolve());
      });
      sock.once('error', reject);
      sock.once('timeout', () => reject(new Error('Connection timeout')));
      sock.setTimeout(timeoutMs);
      sock.on('close', () => {
        if (!this.closed) {
          // Unexpected disconnect — schedule reconnect
          console.error('[WARN] Daemon disconnected, will retry in 5s...');
          setTimeout(() => this.reconnect(), 5000);
        }
      });
      this.socket = sock;
    });
  }

  private async reconnect(): Promise<void> {
    while (!this.closed) {
      try {
        await this.connect(5000);
        console.error('[INFO] Reconnected to daemon');
        return;
      } catch {
        console.error('[WARN] Reconnect failed, retrying in 10s...');
        await new Promise((r) => setTimeout(r, 10000));
      }
    }
  }

  async send(
    method: string,
    params: Record<string, unknown> = {}
  ): Promise<DaemonResponse> {
    if (!this.socket) throw new Error('Not connected');

    return new Promise((resolve, reject) => {
      const id = `mcp-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`;
      const request = JSON.stringify({
        id,
        method,
        params: { ...params, protocol_version: '1.0' },
      });

      const sock = this.socket!;
      let done = false;
      const timer = setTimeout(() => {
        if (!done) {
          done = true;
          sock.removeListener('data', onData);
          resolve({
            ok: false,
            error_message: `Request timeout (${this.requestTimeoutMs}ms): ${method}`,
          });
        }
      }, this.requestTimeoutMs);

      const onData = (data: Buffer) => {
        this.buffer += data.toString('utf-8');
        const lines = this.buffer.split('\n');
        for (let i = 0; i < lines.length - 1; i++) {
          try {
            const parsed = JSON.parse(lines[i]);
            if (parsed.id === id) {
              this.buffer = lines.slice(i + 1).join('\n');
              sock.removeListener('data', onData);
              clearTimeout(timer);
              done = true;
              resolve({
                ok: parsed.ok ?? false,
                result: parsed.result,
                error_message: parsed.error_message,
              });
              return;
            }
          } catch { /* partial line */ }
        }
      };

      sock.on('data', onData);
      sock.write(request + '\n', 'utf-8');
    });
  }

  close(): void {
    this.closed = true;
    this.socket?.destroy();
    this.socket = null;
  }
}
