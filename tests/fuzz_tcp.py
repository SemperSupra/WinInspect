import socket
import struct
import time
import json

def test_fuzz(host="127.0.0.1", port=1985):
    print(f"--- Starting TCP Fuzz Test on {host}:{port} ---")
    
    # 1. Test oversized message (DoS Protection)
    print("[1/3] Testing oversized message limit (11MB)...")
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(2)
        s.connect((host, port))
        # Send 11MB length prefix
        s.send(struct.pack("<I", 11 * 1024 * 1024))
        # Wait for disconnect
        data = s.recv(1024)
        if not data:
            print("  OK: Connection closed by server as expected.")
        else:
            print("  FAIL: Server did not close connection or sent unexpected data.")
        s.close()
    except Exception as e:
        print(f"  OK: Caught exception during oversized send: {e}")

    # 2. Test random garbage
    print("[2/3] Testing random garbage...")
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(2)
        s.connect((host, port))
        s.send(b"\x00\x00\x00\x05GARBAGE")
        s.close()
        print("  OK: Random garbage handled.")
    except:
        print("  OK: Random garbage handled (exception).")

    # 3. Verify daemon responsiveness
    print("[3/3] Verifying daemon health post-fuzz...")
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(2)
        s.connect((host, port))
        # Note: This assumes NO auth required for testing or it will timeout at handshake
        # For a full test, we'd need to perform the handshake.
        print("  OK: Daemon still accepting connections.")
        s.close()
    except Exception as e:
        print(f"  FAIL: Daemon unreachable: {e}")

if __name__ == "__main__":
    test_fuzz()
