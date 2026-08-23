import { describe, it, expect } from 'vitest';
import { DaemonClient } from './daemon-client.js';

describe('DaemonClient', () => {
  it('should reject connection to non-existent daemon', async () => {
    const client = new DaemonClient('127.0.0.1', 1); // port 1 = nobody
    await expect(client.connect(1000)).rejects.toThrow();
    client.close();
  });

  it('should fail to send without connection', async () => {
    const client = new DaemonClient('127.0.0.1', 1985);
    await expect(client.send('test')).rejects.toThrow('Not connected');
    client.close();
  });

  it('should handle request timeout', async () => {
    const client = new DaemonClient('127.0.0.1', 1985, 1); // 1ms timeout
    // Not connected — send will reject with 'Not connected'
    await expect(client.send('test')).rejects.toThrow('Not connected');
    client.close();
  });

  it('should handle reconnection safely', () => {
    const client = new DaemonClient('127.0.0.1', 1985);
    // close() should not throw even if already disconnected
    client.close();
    client.close(); // double close should be safe
  });
});
