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

const ProtocolVersion = "0.0.1"

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
	fmt.Println("Commands: capture, top, tree, info, health, ps, reg-read, clip-read, find-regex, etc.")
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
		"capture":        "snapshot.capture",
		"top":            "window.listTop",
		"info":           "window.getInfo",
		"children":       "window.listChildren",
		"tree":           "window.getTree",
		"pick":           "window.pickAtPoint",
		"status":         "daemon.status",
		"health":         "daemon.health",
		"highlight":      "window.highlight",
		"set-prop":       "window.setProperty",
		"control-click":  "window.controlClick",
		"control-send":   "window.controlSend",
		"get-pixel":      "screen.getPixel",
		"pixel-search":   "screen.pixelSearch",
		"capture-region": "screen.capture",
		"ps":             "process.list",
		"kill":           "process.kill",
		"file-info":      "file.getInfo",
		"file-read":      "file.read",
		"find-regex":     "window.findRegex",
		"reg-read":       "reg.read",
		"reg-write":      "reg.write",
		"reg-delete":     "reg.delete",
		"clip-read":      "clipboard.read",
		"clip-write":     "clipboard.write",
		"svc-list":       "service.list",
		"svc-status":     "service.status",
		"svc-control":    "service.control",
		"env-get":        "env.get",
		"env-set":        "env.set",
		"wine-drives":    "wine.drives",
		"wine-overrides": "wine.overrides",
		"mutex-check":    "sync.checkMutex",
		"mutex-create":   "sync.createMutex",
		"mem-read":       "mem.read",
		"mem-write":      "mem.write",
	}[method]

	if reqMethod == "" {
		return fmt.Errorf("unknown command: %s", method)
	}

	params := make(map[string]interface{})
	params["canonical"] = true

	// Argument Mapping
	switch method {
	case "info", "children", "tree", "highlight":
		if len(args) > 0 {
			params["hwnd"] = args[0]
		}
	case "pick", "get-pixel":
		if len(args) < 2 {
			return fmt.Errorf("missing x y")
		}
		params["x"] = args[0]
		params["y"] = args[1]
	case "set-prop":
		if len(args) < 3 {
			return fmt.Errorf("missing hwnd name value")
		}
		params["hwnd"] = args[0]
		params["name"] = args[1]
		params["value"] = args[2]
	case "control-click":
		if len(args) < 3 {
			return fmt.Errorf("missing hwnd x y")
		}
		params["hwnd"] = args[0]
		params["x"] = args[1]
		params["y"] = args[2]
	case "control-send":
		if len(args) < 2 {
			return fmt.Errorf("missing hwnd text")
		}
		params["hwnd"] = args[0]
		params["text"] = args[1]
	case "reg-read", "file-info", "file-read", "svc-status", "mutex-check", "mutex-create":
		if len(args) < 1 {
			return fmt.Errorf("missing argument")
		}
		params["path"] = args[0]
		params["name"] = args[0]
	case "reg-write":
		if len(args) < 4 {
			return fmt.Errorf("missing path name type data")
		}
		params["path"] = args[0]
		params["name"] = args[1]
		params["type"] = args[2]
		params["data"] = args[3]
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
	// 1. Recv Hello/Challenge
	helloData, err := recvRaw(conn)
	if err != nil {
		return nil, err
	}
	var hello map[string]interface{}
	json.Unmarshal(helloData, &hello)

	if hello["version"] != ProtocolVersion {
		return nil, fmt.Errorf("version mismatch: server %v, client %v", hello["version"], ProtocolVersion)
	}

	// 2. Decide if Auth is needed
	// If server didn't send a nonce, it likely doesn't require auth
	if hello["nonce"] == nil {
		// Initialize Dummy Session (matches crypto_windows.cpp placeholder)
		block, _ := aes.NewCipher(make([]byte, 32))
		gcm, _ := cipher.NewGCM(block)
		return &CryptoSession{aesGCM: gcm}, nil
	}

	// 3. Perform Auth & Key Exchange
	resp := map[string]interface{}{
		"version":   ProtocolVersion,
		"identity":  "portable-client",
		"signature": "SSHSIG_STUB", // In real version, sign hello["nonce"]
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
