#ifndef MONOCYPHER_H
#define MONOCYPHER_H
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void crypto_ed25519_sign(uint8_t signature[64], const uint8_t secret_key[32], const uint8_t public_key[32], const uint8_t *message, size_t message_size);
int crypto_ed25519_check(const uint8_t signature[64], const uint8_t public_key[32], const uint8_t *message, size_t message_size);
void crypto_ed25519_public_key(uint8_t public_key[32], const uint8_t secret_key[32]);

#ifdef __cplusplus
}
#endif
#endif
