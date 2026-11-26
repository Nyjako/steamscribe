#include "util/secret_crypto.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <goblin/appdirs.h>

#define GOBLIN_BASE64_IMPLEMENTATION
#include <goblin/base64.h>

#define SECRET_CRYPTO_NONCE_LEN 12
#define SECRET_CRYPTO_PREFIX "enc1:"

static int secure_random_bytes(uint8_t *out, size_t len)
{
    FILE *fp = fopen("/dev/urandom", "rb");
    if (!fp) return -1;

    size_t n = fread(out, 1, len, fp);
    fclose(fp);
    return (n == len) ? 0 : -1;
}

/* -- ChaCha20 (RFC 8439), stream cipher only -- no Poly1305, see secret_crypto.h. -- */

static uint32_t chacha20_rotl32(uint32_t x, int n)
{
    return (x << n) | (x >> (32 - n));
}

#define CHACHA20_QR(a, b, c, d)             \
    a += b;                                 \
    d ^= a;                                 \
    d = chacha20_rotl32(d, 16);             \
    c += d;                                 \
    b ^= c;                                 \
    b = chacha20_rotl32(b, 12);             \
    a += b;                                 \
    d ^= a;                                 \
    d = chacha20_rotl32(d, 8);              \
    c += d;                                 \
    b ^= c;                                 \
    b = chacha20_rotl32(b, 7);

static void chacha20_block(const uint32_t key[8], uint32_t counter, const uint32_t nonce[3], uint8_t out[64])
{
    uint32_t state[16] = {
        0x61707865, 0x3320646e, 0x79622d32, 0x6b206574,
        key[0], key[1], key[2], key[3], key[4], key[5], key[6], key[7],
        counter, nonce[0], nonce[1], nonce[2],
    };

    uint32_t w[16];
    memcpy(w, state, sizeof(state));

    for (int i = 0; i < 10; i++)
    {
        CHACHA20_QR(w[0], w[4], w[8], w[12]);
        CHACHA20_QR(w[1], w[5], w[9], w[13]);
        CHACHA20_QR(w[2], w[6], w[10], w[14]);
        CHACHA20_QR(w[3], w[7], w[11], w[15]);
        CHACHA20_QR(w[0], w[5], w[10], w[15]);
        CHACHA20_QR(w[1], w[6], w[11], w[12]);
        CHACHA20_QR(w[2], w[7], w[8], w[13]);
        CHACHA20_QR(w[3], w[4], w[9], w[14]);
    }

    for (int i = 0; i < 16; i++) w[i] += state[i];

    for (int i = 0; i < 16; i++)
    {
        out[4 * i + 0] = (uint8_t)(w[i] & 0xff);
        out[4 * i + 1] = (uint8_t)((w[i] >> 8) & 0xff);
        out[4 * i + 2] = (uint8_t)((w[i] >> 16) & 0xff);
        out[4 * i + 3] = (uint8_t)((w[i] >> 24) & 0xff);
    }
}

static void chacha20_xor(const uint8_t key[SECRET_CRYPTO_KEY_LEN], const uint8_t nonce[SECRET_CRYPTO_NONCE_LEN], const uint8_t *in, uint8_t *out, size_t len)
{
    uint32_t key_words[8];
    for (int i = 0; i < 8; i++) key_words[i] = (uint32_t)key[4 * i] | ((uint32_t)key[4 * i + 1] << 8) | ((uint32_t)key[4 * i + 2] << 16) | ((uint32_t)key[4 * i + 3] << 24);

    uint32_t nonce_words[3];
    for (int i = 0; i < 3; i++) nonce_words[i] = (uint32_t)nonce[4 * i] | ((uint32_t)nonce[4 * i + 1] << 8) | ((uint32_t)nonce[4 * i + 2] << 16) | ((uint32_t)nonce[4 * i + 3] << 24);

    uint8_t block[64];
    uint32_t counter = 0;
    size_t offset = 0;
    while (offset < len)
    {
        chacha20_block(key_words, counter, nonce_words, block);
        size_t chunk = (len - offset < 64) ? (len - offset) : 64;
        for (size_t i = 0; i < chunk; i++) out[offset + i] = in[offset + i] ^ block[i];
        offset += chunk;
        counter++;
    }
}

/* -- key file -- */

static char *key_file_path(void)
{
    char *data_dir = goblin_appdir_data_dir("steamscribe");
    if (!data_dir) return NULL;

    if (goblin_appdir_mkdir_p(data_dir) != 0)
    {
        free(data_dir);
        return NULL;
    }

    char *path = goblin_appdir_join(data_dir, "secret.key");
    free(data_dir);
    return path;
}

static int read_key_file(const char *path, uint8_t key_out[SECRET_CRYPTO_KEY_LEN])
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;

    size_t n = fread(key_out, 1, SECRET_CRYPTO_KEY_LEN, fp);
    fclose(fp);
    return (n == SECRET_CRYPTO_KEY_LEN) ? 0 : -1;
}

/* O_EXCL makes this fail rather than overwrite if the file already exists
 * (including a concurrent writer that won the race) -- silently replacing
 * an existing key would make every value it had encrypted undecryptable. */
static int write_key_file_exclusive(const char *path, const uint8_t key[SECRET_CRYPTO_KEY_LEN])
{
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return -1;

    FILE *fp = fdopen(fd, "wb");
    if (!fp)
    {
        close(fd);
        return -1;
    }

    size_t n = fwrite(key, 1, SECRET_CRYPTO_KEY_LEN, fp);
    fclose(fp);
    return (n == SECRET_CRYPTO_KEY_LEN) ? 0 : -1;
}

int secret_crypto_load_or_create_key(uint8_t key_out[SECRET_CRYPTO_KEY_LEN], err_t *err)
{
    char *path = key_file_path();
    if (!path)
    {
        err_set(err, -1, "could not determine where to keep the secret key");
        return -1;
    }

    if (read_key_file(path, key_out) == 0)
    {
        free(path);
        return 0;
    }

    uint8_t new_key[SECRET_CRYPTO_KEY_LEN];
    if (secure_random_bytes(new_key, sizeof(new_key)) != 0)
    {
        free(path);
        err_set(err, -1, "could not read /dev/urandom to generate a secret key");
        return -1;
    }

    if (write_key_file_exclusive(path, new_key) == 0)
    {
        memcpy(key_out, new_key, SECRET_CRYPTO_KEY_LEN);
        free(path);
        return 0;
    }

    /* Lost a race with another process creating it first -- its key wins. */
    if (read_key_file(path, key_out) == 0)
    {
        free(path);
        return 0;
    }

    err_set(err, -1, "could not create secret key file at %s", path);
    free(path);
    return -1;
}

/* -- text blob encode/decode -- */

bool secret_crypto_is_encrypted(const char *text)
{
    return text && strncmp(text, SECRET_CRYPTO_PREFIX, strlen(SECRET_CRYPTO_PREFIX)) == 0;
}

char *secret_crypto_encrypt_to_text(const uint8_t key[SECRET_CRYPTO_KEY_LEN], const char *plaintext)
{
    uint8_t nonce[SECRET_CRYPTO_NONCE_LEN];
    if (secure_random_bytes(nonce, sizeof(nonce)) != 0) return NULL;

    size_t plain_len = strlen(plaintext);
    size_t blob_len = SECRET_CRYPTO_NONCE_LEN + plain_len;
    uint8_t *blob = (uint8_t *)malloc(blob_len);
    if (!blob) return NULL;

    memcpy(blob, nonce, SECRET_CRYPTO_NONCE_LEN);
    chacha20_xor(key, nonce, (const uint8_t *)plaintext, blob + SECRET_CRYPTO_NONCE_LEN, plain_len);

    char *b64 = goblin_base64_encode(blob, blob_len);
    free(blob);
    if (!b64) return NULL;

    size_t prefix_len = strlen(SECRET_CRYPTO_PREFIX);
    char *result = (char *)malloc(prefix_len + strlen(b64) + 1);
    if (!result)
    {
        free(b64);
        return NULL;
    }
    memcpy(result, SECRET_CRYPTO_PREFIX, prefix_len);
    memcpy(result + prefix_len, b64, strlen(b64) + 1);

    free(b64);
    return result;
}

int secret_crypto_decrypt_from_text(arena_t *arena, const uint8_t key[SECRET_CRYPTO_KEY_LEN], const char *text, char **out_plaintext)
{
    size_t prefix_len = strlen(SECRET_CRYPTO_PREFIX);
    if (!secret_crypto_is_encrypted(text)) return -1;

    const char *b64 = text + prefix_len;
    size_t b64_len = strlen(b64);
    if (b64_len == 0 || b64_len % 4 != 0) return -1;

    unsigned char *decoded = (unsigned char *)malloc(3 * b64_len / 4 + 1);
    if (!decoded) return -1;

    size_t decoded_len = 0;
    if (!goblin_base64_decode(b64, decoded, &decoded_len) || decoded_len < SECRET_CRYPTO_NONCE_LEN)
    {
        free(decoded);
        return -1;
    }

    size_t plain_len = decoded_len - SECRET_CRYPTO_NONCE_LEN;
    char *plaintext = (char *)arena_alloc(arena, plain_len + 1);
    chacha20_xor(key, decoded, decoded + SECRET_CRYPTO_NONCE_LEN, (uint8_t *)plaintext, plain_len);
    plaintext[plain_len] = '\0';

    free(decoded);
    *out_plaintext = plaintext;
    return 0;
}
