#include "doctest/doctest.h"
#include "wininspect/crypto.hpp"

#ifdef _WIN32
using namespace wininspect::crypto;

DOCTEST_TEST_CASE("CryptoSession handshake and encryption") {
    CryptoSession server;
    CryptoSession client;

    // 1. Generate keys
    auto server_pub = server.generate_local_key();
    auto client_pub = client.generate_local_key();

    DOCTEST_REQUIRE(!server_pub.empty());
    DOCTEST_REQUIRE(!client_pub.empty());

    // 2. Exchange and compute secret
    DOCTEST_REQUIRE(server.compute_shared_secret(client_pub));
    DOCTEST_REQUIRE(client.compute_shared_secret(server_pub));

    // 3. Encrypt/Decrypt roundtrip
    std::string message = "Secret Window Title";
    auto encrypted = client.encrypt(message);
    DOCTEST_REQUIRE(!encrypted.empty());
    DOCTEST_REQUIRE(encrypted.size() > message.size());

    std::string decrypted = server.decrypt(encrypted);
    DOCTEST_REQUIRE_EQ(message, decrypted);
}
#endif
