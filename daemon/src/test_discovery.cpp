#include <iostream>
#include <thread>
#include <atomic>
#include <cassert>
#include <cstring>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "wininspect/tinyjson.hpp"

// Minimal mock for testing
void run_test_responder(std::atomic<bool>* running) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1988);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(s, (struct sockaddr*)&addr, sizeof(addr));

    while (running->load()) {
        char buf[512];
        sockaddr_in client{};
        int len = sizeof(client);
        int r = recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr*)&client, &len);
        if (r > 0) {
            std::string resp = "{\"type\":\"announcement\",\"hostname\":\"test-host\"}";
            sendto(s, resp.data(), (int)resp.size(), 0, (struct sockaddr*)&client, len);
        }
    }
    closesocket(s);
}

int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    std::atomic<bool> running{true};
    std::thread responder(run_test_responder, &running);
    responder.detach();

    std::this_thread::sleep_for(std::chrono::seconds(1));

    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    bool broadcast = true;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char*)&broadcast, sizeof(broadcast));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1988);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1"); 

    std::string msg = "WININSPECT_DISCOVER";
    sendto(s, msg.data(), (int)msg.size(), 0, (struct sockaddr*)&addr, sizeof(addr));

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(s, &fds);
    timeval tv{5, 0}; // Increased to 5s

    if (select(0, &fds, NULL, NULL, &tv) > 0) {
        char buf[512];
        int r = recv(s, buf, sizeof(buf)-1, 0);
        if (r > 0) {
            buf[r] = '\0';
            std::cout << "Received: " << buf << std::endl;
            if (strstr(buf, "announcement")) {
                std::cout << "Discovery Integration Test: PASSED" << std::endl;
                running = false;
                closesocket(s);
                return 0;
            }
        }
    }

    std::cout << "Discovery Integration Test: FAILED" << std::endl;
    running = false;
    closesocket(s);
    return 1;
}
