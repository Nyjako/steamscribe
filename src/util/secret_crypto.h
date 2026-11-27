#ifndef STEAMSCRIBE_UTIL_SECRET_CRYPTO_H
#define STEAMSCRIBE_UTIL_SECRET_CRYPTO_H

#include <stdbool.h>
#include <stdint.h>

#include "util/arena.h"
#include "util/error.h"

#define SECRET_CRYPTO_KEY_LEN 32

/* This is deliberately *not* a defense against someone with access to this
 * machine's filesystem -- the key below sits right next to the config file
 * it decrypts, unprotected by any passphrase. Its only job is to keep a
 * secret like the Steam Web API key from sitting in config.ini as
 * plaintext, so it isn't exposed by a casual `grep`, a pasted bug report,
 * or an accidental `git add` of a dotfiles repo. */

/* Reads the machine-local 256-bit key from <data dir>/secret.key (created
 * with 0600 permissions on first use, under goblin_appdir_data_dir()), or
 * generates and persists a new one if it doesn't exist yet. The same key
 * is reused for as long as the file exists -- losing it makes every
 * previously encrypted value undecryptable, which is why this never
 * regenerates an existing file. Returns 0 on success. */
int secret_crypto_load_or_create_key(uint8_t key_out[SECRET_CRYPTO_KEY_LEN], err_t *err);

/* True if `text` is one of this module's own encrypted blobs (starts with
 * its "enc1:" prefix) rather than plaintext. NULL is not. */
bool secret_crypto_is_encrypted(const char *text);

/* Encrypts `plaintext` (ChaCha20, confidentiality only -- no integrity
 * check) under a fresh random nonce and returns a storable, printable
 * blob ("enc1:" + base64 of nonce||ciphertext), malloc'd -- the caller
 * frees it. Returns NULL if a secure random nonce couldn't be obtained. */
char *secret_crypto_encrypt_to_text(const uint8_t key[SECRET_CRYPTO_KEY_LEN], const char *plaintext);

/* Reverses secret_crypto_encrypt_to_text(). Returns 0 and an
 * arena-allocated, NUL-terminated plaintext in *out_plaintext on success;
 * -1 if `text` doesn't have the "enc1:" prefix or is malformed. */
int secret_crypto_decrypt_from_text(arena_t *arena, const uint8_t key[SECRET_CRYPTO_KEY_LEN], const char *text, char **out_plaintext);

#endif
