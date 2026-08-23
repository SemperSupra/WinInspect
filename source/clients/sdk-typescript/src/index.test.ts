import { describe, it, expect } from 'vitest';
import { WinInspect } from './index.js';

describe('WinInspect SDK', () => {
  it('should create instance with defaults', () => {
    const wi = new WinInspect();
    expect(wi).toBeDefined();
  });

  it('should create instance with custom host/port', () => {
    const wi = new WinInspect('10.0.0.1', 1986);
    expect(wi).toBeDefined();
  });

  it('should fail to connect to non-existent daemon', async () => {
    const wi = new WinInspect('127.0.0.1', 1);
    await expect(wi.connect()).rejects.toThrow();
  });

  it('should fail gracefully before connect', async () => {
    const wi = new WinInspect();
    await expect(wi.listWindows()).rejects.toThrow('Not connected');
  });
});
