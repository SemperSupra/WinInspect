package main

import (
	"crypto/aes"
	"crypto/cipher"
	"encoding/binary"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"net"
	"os"
	"path/filepath"
	"strings"
)

const ProtocolVersion = "1.0.0"

type Request struct {
	ID     string      `json:"id"`
	Method string      `json:"method"`
	Params interface{} `json:"params"`
}

type Response struct {
	ID     string          `json:"id"`
	OK     bool            `json:"ok"`
	Result json.RawMessage `json:"result,omitempty"`
	Error  *ErrorDetail    `json:"error,omitempty"`
}

type ErrorDetail struct {
	Code    string `json:"code"`
	Message string `json:"message"`
}

type CryptoSession struct {
	aesGCM cipher.AEAD
	nonce  uint64
}

func main() {
	tcpAddr := flag.String("tcp", "127.0.0.1:1985", "Daemon TCP address")
	keyPath := flag.String("key", "", "Path to private key for auth")
	flag.Parse()

	args := flag.Args()
	if len(args) == 0 {
		usage()
		return
	}

	cmd := args[0]
	if cmd == "config" {
		if len(args) < 3 || args[1] != "--key" {
			usage()
			return
		}
		saveConfig(args[2])
		fmt.Printf("Key path saved: %s\n", args[2])
		return
	}

	// Load stored key if not provided via flag
	if *keyPath == "" {
		*keyPath = loadConfig()
	}

	err := runCommand(*tcpAddr, *keyPath, cmd, args[1:])
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}
}

func usage() {
	fmt.Println("Usage: wi-portable [flags] <command> [args]")
	fmt.Println("Commands: capture, top, info <hwnd>, children <hwnd>, pick <x> <y>, status, config --key <path>")
}

func runCommand(addr, keyPath, method string, args []string) error {
	conn, err := net.Dial("tcp", addr)
	if err != nil {
		return fmt.Errorf("failed to connect: %v", err)
	}
	defer conn.Close()

	// 1. Handshake & Auth
	session, err := handshake(conn, keyPath)
	if err != nil {
		return fmt.Errorf("handshake failed: %v", err)
	}

	// 2. Build Request
	reqMethod := map[string]string{
		"capture":  "snapshot.capture",
		"top":      "window.listTop",
		"info":     "window.getInfo",
		"children": "window.listChildren",
		"pick":     "window.pickAtPoint",
		"status":   "daemon.status",
	}[method]

	if reqMethod == "" {
		return fmt.Errorf("unknown command: %s", method)
	}

	params := make(map[string]interface{})
	if method == "info" || method == "children" {
		if len(args) < 1 {
			return fmt.Errorf("missing hwnd")
		}
		params["hwnd"] = args[0]
	} else if method == "pick" {
		if len(args) < 2 {
			return fmt.Errorf("missing x y")
		}
		params["x"] = args[0]
		params["y"] = args[1]
	}

	req := Request{ID: "p-1", Method: reqMethod, Params: params}
	reqData, _ := json.Marshal(req)

	// 3. Encrypted Send/Recv
	err = sendEncrypted(conn, session, reqData)
	if err != nil {
		return err
	}

	respData, err := recvEncrypted(conn, session)
	if err != nil {
		return err
	}

	fmt.Println(string(respData))
	return nil
}

func handshake(conn net.Conn, keyPath string) (*CryptoSession, error) {
	// Recv Hello
	helloData, err := recvRaw(conn)
	if err != nil {
		return nil, err
	}
	var hello map[string]interface{}
	json.Unmarshal(helloData, &hello)

	if hello["version"] != ProtocolVersion {
		return nil, fmt.Errorf("version mismatch: server %v, client %v", hello["version"], ProtocolVersion)
	}

	// Auth & Key Exchange (Simplified for this scaffold)
	// In real version, we'd use ed25519 to sign the nonce
	resp := map[string]interface{}{
		"version":   ProtocolVersion,
		"identity":  "portable-client",
		"signature": "SSHSIG_STUB",
	}
	respData, _ := json.Marshal(resp)
	if err := sendRaw(conn, respData); err != nil {
		return nil, err
	}

	authStatus, err := recvRaw(conn)
	if err != nil {
		return nil, err
	}
	if !strings.Contains(string(authStatus), "\"ok\":true") {
		return nil, fmt.Errorf("auth failed")
	}

	// Initialize Dummy Session (Matches crypto_windows.cpp placeholder)
	block, _ := aes.NewCipher(make([]byte, 32))
	gcm, _ := cipher.NewGCM(block)
	return &CryptoSession{aesGCM: gcm}, nil
}

func sendRaw(conn net.Conn, data []byte) error {
	lenBuf := make([]byte, 4)
	binary.LittleEndian.PutUint32(lenBuf, uint32(len(data)))
	conn.Write(lenBuf)
	_, err := conn.Write(data)
	return err
}

func recvRaw(conn net.Conn) ([]byte, error) {
	lenBuf := make([]byte, 4)
	_, err := io.ReadFull(conn, lenBuf)
	if err != nil {
		return nil, err
	}
	length := binary.LittleEndian.Uint32(lenBuf)
	data := make([]byte, length)
	_, err = io.ReadFull(conn, data)
	return data, err
}

func sendEncrypted(conn net.Conn, s *CryptoSession, data []byte) error {
	nonce := make([]byte, 12) // In real version, increment s.nonce
	ciphertext := s.aesGCM.Seal(nil, nonce, data, nil)

	// Matches our C++ logic: [Nonce(12)][Tag(16)][Ciphertext(N)]
	// Note: Seal returns [Ciphertext][Tag], we may need to reorder
	return sendRaw(conn, ciphertext)
}

func recvEncrypted(conn net.Conn, s *CryptoSession) ([]byte, error) {
	data, err := recvRaw(conn)
	if err != nil {
		return nil, err
	}
	// In this scaffold, decrypt is just returning the slice past 28 bytes
	if len(data) < 28 {
		return nil, fmt.Errorf("malformed packet")
	}
	return data[28:], nil
}

func saveConfig(path string) {
	home, _ := os.UserHomeDir()
	os.WriteFile(filepath.Join(home, ".wininspect_portable"), []byte(path), 0600)
}

func loadConfig() string {
	home, _ := os.UserHomeDir()
	data, _ := os.ReadFile(filepath.Join(home, ".wininspect_portable"))
	return string(data)
}
