#!/usr/bin/env python3
import os
import sys
import subprocess
import platform
import json

def run_cmd(cmd):
    try:
        result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
        return result.stdout.strip() if result.returncode == 0 else None
    except:
        return None

def generate_keys():
    # Use wg command if available, otherwise instructions
    priv = run_cmd("wg genkey")
    if not priv:
        print("Error: 'wg' command not found. Please install WireGuard tools.")
        return None, None
    pub = run_cmd(f"echo {priv} | wg pubkey")
    return priv, pub

def get_config_template(is_host, priv_key, peer_pub, endpoint, local_ip, peer_ip):
    conf = f"[Interface]
PrivateKey = {priv_key}
Address = {local_ip}/24
"
    if is_host:
        conf += "ListenPort = 51820
"
    
    conf += f"
[Peer]
PublicKey = {peer_pub}
"
    if endpoint:
        conf += f"Endpoint = {endpoint}:51820
"
    conf += f"AllowedIPs = {peer_ip}/32
"
    conf += "PersistentKeepalive = 25
"
    return conf

def main():
    if len(sys.argv) < 2:
        print("Usage: wi-tunnel.py <init | up | down | status>")
        return

    cmd = sys.argv[1]
    is_windows = platform.system() == "Windows"
    conf_path = "wi-vpn.conf"

    if cmd == "init":
        print("--- WinInspect WireGuard Setup ---")
        role = input("Is this the [H]ost (Linux) or [T]arget (Windows/Wine)? ").lower()
        endpoint = ""
        if role == "h":
            endpoint = input("Enter Target physical IP address: ")
            local_ip, peer_ip = "10.0.0.1", "10.0.0.2"
        else:
            local_ip, peer_ip = "10.0.0.2", "10.0.0.1"

        priv, pub = generate_keys()
        if not priv: return

        print(f"
Your Public Key: {pub}")
        peer_pub = input("Enter Peer's Public Key: ")

        config = get_config_template(role == "h", priv, peer_pub, endpoint, local_ip, peer_ip)
        with open(conf_path, "w") as f:
            f.write(config)
        print(f"
Config saved to {conf_path}")

    elif cmd == "up":
        if is_windows:
            print(f"On Windows, please import '{conf_path}' into the WireGuard GUI and click Activate.")
        else:
            print("Bringing up tunnel on Linux...")
            subprocess.run(["sudo", "wg-quick", "up", f"./{conf_path}"])

    elif cmd == "down":
        if is_windows:
            print("On Windows, please Deactivate via the WireGuard GUI.")
        else:
            subprocess.run(["sudo", "wg-quick", "down", f"./{conf_path}"])

    elif cmd == "status":
        if is_windows:
            subprocess.run(["wireguard.exe", "/status"])
        else:
            subprocess.run(["sudo", "wg", "show"])

if __name__ == "__main__":
    main()
