// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung

#include "wininspect/tls.hpp"
#include "wininspect/logger.hpp"

#ifdef WININSPECT_HAVE_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/rand.h>
#endif

#include <cstring>

namespace wininspect {

#ifdef WININSPECT_HAVE_OPENSSL

  // ── OpenSSL one-time init ───────────────────────────────────────────────────

  namespace {
    struct SslInit
    {
      SslInit()
      {
        SSL_load_error_strings();
        OpenSSL_add_ssl_algorithms();
      }
    };
    static SslInit s_ssl_init;
  } // namespace

  // ── Impl ────────────────────────────────────────────────────────────────────

  struct TlsSession::Impl
  {
    SSL_CTX* ctx = nullptr;
    SSL* ssl = nullptr;
    bool is_server = false;
    bool handshake_done = false;
  };

  TlsSession::TlsSession() : impl_(new Impl) {}

  TlsSession::~TlsSession()
  {
    if (impl_->ssl)
      SSL_free(impl_->ssl);
    if (impl_->ctx)
      SSL_CTX_free(impl_->ctx);
  }

  bool TlsSession::is_initialized() const
  {
    return impl_->handshake_done;
  }

  // ── Self-signed cert generation (EC P-256) ──────────────────────────────────

  bool TlsSession::generate_self_signed_cert(const std::string& subject, std::string& out_cert_pem,
                                             std::string& out_key_pem)
  {
    // Generate EC P-256 key pair using EVP API (OpenSSL 3.0+ compatible)
    EVP_PKEY* pkey = EVP_PKEY_new();
    if (!pkey)
      return false;

    EC_KEY* ec = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    if (!ec || !EC_KEY_generate_key(ec)) {
      EVP_PKEY_free(pkey);
      EC_KEY_free(ec);
      return false;
    }
    // EVP_PKEY_assign_EC_KEY takes ownership of ec on success.
    // On failure (returns 0), both pkey and ec must be freed separately.
    if (!EVP_PKEY_assign_EC_KEY(pkey, ec)) {
      EVP_PKEY_free(pkey);
      EC_KEY_free(ec);
      return false;
    }

    // Create self-signed X.509 certificate
    X509* x509 = X509_new();
    if (!x509) {
      EVP_PKEY_free(pkey);
      return false;
    }

    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
    X509_gmtime_adj(X509_get_notBefore(x509), 0);
    X509_gmtime_adj(X509_get_notAfter(x509), 365 * 24 * 3600); // 1 year

    // OpenSSL 4.x const-corrected X509_get_subject_name return type
    auto* name = const_cast<X509_NAME*>(X509_get_subject_name(x509));
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (const unsigned char*)subject.c_str(), -1,
                               -1, 0);
    X509_set_issuer_name(x509, name);
    X509_set_pubkey(x509, pkey);
    X509_sign(x509, pkey, EVP_sha256());

    // Write cert PEM
    BIO* bio = BIO_new(BIO_s_mem());
    if (!PEM_write_bio_X509(bio, x509)) {
      BIO_free(bio);
      X509_free(x509);
      EVP_PKEY_free(pkey);
      return false;
    }
    char* cert_data = nullptr;
    long cert_len = BIO_get_mem_data(bio, &cert_data);
    out_cert_pem.assign(cert_data, cert_len);
    BIO_free(bio);

    // Write key PEM
    bio = BIO_new(BIO_s_mem());
    if (!PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr)) {
      BIO_free(bio);
      X509_free(x509);
      EVP_PKEY_free(pkey);
      return false;
    }
    char* key_data = nullptr;
    long key_len = BIO_get_mem_data(bio, &key_data);
    out_key_pem.assign(key_data, key_len);
    BIO_free(bio);

    X509_free(x509);
    EVP_PKEY_free(pkey);
    return true;
  }

  // ── Server init ─────────────────────────────────────────────────────────────

  bool TlsSession::init_server(const std::string& cert_pem, const std::string& key_pem)
  {
    impl_->ctx = SSL_CTX_new(TLS_server_method());
    if (!impl_->ctx) {
      LOG_ERROR("TLS: SSL_CTX_new(TLS_server_method) failed");
      return false;
    }

    SSL_CTX_set_min_proto_version(impl_->ctx, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(impl_->ctx, TLS1_3_VERSION);

    // Load certificate from PEM string
    BIO* bio = BIO_new_mem_buf(cert_pem.data(), (int)cert_pem.size());
    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!cert) {
      LOG_ERROR("TLS: failed to parse certificate PEM");
      SSL_CTX_free(impl_->ctx);
      impl_->ctx = nullptr;
      return false;
    }

    // Load private key from PEM string
    bio = BIO_new_mem_buf(key_pem.data(), (int)key_pem.size());
    EVP_PKEY* key = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!key) {
      LOG_ERROR("TLS: failed to parse private key PEM");
      X509_free(cert);
      SSL_CTX_free(impl_->ctx);
      impl_->ctx = nullptr;
      return false;
    }

    if (SSL_CTX_use_certificate(impl_->ctx, cert) != 1) {
      LOG_ERROR("TLS: SSL_CTX_use_certificate failed");
      X509_free(cert);
      EVP_PKEY_free(key);
      SSL_CTX_free(impl_->ctx);
      impl_->ctx = nullptr;
      return false;
    }
    if (SSL_CTX_use_PrivateKey(impl_->ctx, key) != 1) {
      LOG_ERROR("TLS: SSL_CTX_use_PrivateKey failed");
      X509_free(cert);
      EVP_PKEY_free(key);
      SSL_CTX_free(impl_->ctx);
      impl_->ctx = nullptr;
      return false;
    }

    X509_free(cert);
    EVP_PKEY_free(key);
    impl_->is_server = true;
    return true;
  }

  // ── Client init ─────────────────────────────────────────────────────────────

  bool TlsSession::init_client()
  {
    impl_->ctx = SSL_CTX_new(TLS_client_method());
    if (!impl_->ctx) {
      LOG_ERROR("TLS: SSL_CTX_new(TLS_client_method) failed");
      return false;
    }

    SSL_CTX_set_min_proto_version(impl_->ctx, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(impl_->ctx, TLS1_3_VERSION);
    // Accept self-signed certs (LAN deployment scenario)
    SSL_CTX_set_verify(impl_->ctx, SSL_VERIFY_NONE, nullptr);
    return true;
  }

  // ── Handshake ───────────────────────────────────────────────────────────────

  bool TlsSession::handshake(uintptr_t socket)
  {
    if (!impl_->ctx) {
      LOG_ERROR("TLS: handshake called before init");
      return false;
    }

    impl_->ssl = SSL_new(impl_->ctx);
    if (!impl_->ssl) {
      LOG_ERROR("TLS: SSL_new failed");
      return false;
    }

    SSL_set_fd(impl_->ssl, (int)socket);

    int ret = impl_->is_server ? SSL_accept(impl_->ssl) : SSL_connect(impl_->ssl);
    if (ret != 1) {
      int err = SSL_get_error(impl_->ssl, ret);
      char err_buf[256];
      ERR_error_string_n(ERR_get_error(), err_buf, sizeof(err_buf));
      LOG_ERROR("TLS handshake failed (" + std::to_string(err) + "): " + err_buf);
      SSL_free(impl_->ssl);
      impl_->ssl = nullptr;
      return false;
    }

    impl_->handshake_done = true;
    LOG_INFO("TLS 1.3 handshake complete (" + std::string(impl_->is_server ? "server" : "client") +
             ")");
    return true;
  }

  // ── Encrypted send/recv ─────────────────────────────────────────────────────

  bool TlsSession::send(uintptr_t /*socket*/, const std::vector<uint8_t>& data)
  {
    if (!impl_->ssl)
      return false;
    int written = SSL_write(impl_->ssl, data.data(), (int)data.size());
    if (written <= 0) {
      LOG_ERROR("TLS: SSL_write failed");
      return false;
    }
    return written == (int)data.size();
  }

  bool TlsSession::recv(uintptr_t /*socket*/, std::vector<uint8_t>& data)
  {
    if (!impl_->ssl)
      return false;
    uint8_t buf[65536];
    int n = SSL_read(impl_->ssl, buf, sizeof(buf));
    if (n <= 0)
      return false;
    data.assign(buf, buf + n);
    return true;
  }

#else // !WININSPECT_HAVE_OPENSSL

  // ── Stub implementation (no OpenSSL) ────────────────────────────────────────

  struct TlsSession::Impl
  {
  };

  TlsSession::TlsSession() : impl_(new Impl) {}
  TlsSession::~TlsSession() = default;

  bool TlsSession::is_initialized() const
  {
    return false;
  }

  bool TlsSession::generate_self_signed_cert(const std::string&, std::string&, std::string&)
  {
    LOG_ERROR("TLS: not available (build with -DWININSPECT_USE_OPENSSL=ON)");
    return false;
  }

  bool TlsSession::init_server(const std::string&, const std::string&)
  {
    LOG_ERROR("TLS: not available (build with -DWININSPECT_USE_OPENSSL=ON)");
    return false;
  }

  bool TlsSession::init_client()
  {
    LOG_ERROR("TLS: not available (build with -DWININSPECT_USE_OPENSSL=ON)");
    return false;
  }

  bool TlsSession::handshake(uintptr_t)
  {
    return false;
  }
  bool TlsSession::send(uintptr_t, const std::vector<uint8_t>&)
  {
    return false;
  }
  bool TlsSession::recv(uintptr_t, std::vector<uint8_t>&)
  {
    return false;
  }

#endif // WININSPECT_HAVE_OPENSSL

} // namespace wininspect
