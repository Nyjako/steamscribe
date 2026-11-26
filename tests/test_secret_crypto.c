#include "test_assert.h"

#include "util/arena.h"
#include "util/error.h"
#include "util/secret_crypto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fixed_key(uint8_t key[SECRET_CRYPTO_KEY_LEN], uint8_t fill)
{
    memset(key, fill, SECRET_CRYPTO_KEY_LEN);
}

static int test_is_encrypted(void)
{
    TEST_ASSERT(secret_crypto_is_encrypted("enc1:AAAA") == true);
    TEST_ASSERT(secret_crypto_is_encrypted("plain-text-key") == false);
    TEST_ASSERT(secret_crypto_is_encrypted("") == false);
    TEST_ASSERT(secret_crypto_is_encrypted(NULL) == false);

    return 0;
}

static int test_encrypt_then_decrypt_round_trips(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    uint8_t key[SECRET_CRYPTO_KEY_LEN];
    fixed_key(key, 0x42);

    char *encrypted = secret_crypto_encrypt_to_text(key, "AC1234-MY-STEAM-KEY");
    TEST_ASSERT(encrypted != NULL);
    TEST_ASSERT(secret_crypto_is_encrypted(encrypted) == true);
    TEST_ASSERT(strstr(encrypted, "AC1234-MY-STEAM-KEY") == NULL); /* never sits in the blob as plaintext */

    char *decrypted = NULL;
    int rc = secret_crypto_decrypt_from_text(&arena, key, encrypted, &decrypted);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(strcmp(decrypted, "AC1234-MY-STEAM-KEY") == 0);

    free(encrypted);
    arena_free(&arena);
    return 0;
}

static int test_encrypt_empty_string_round_trips(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    uint8_t key[SECRET_CRYPTO_KEY_LEN];
    fixed_key(key, 0x7a);

    char *encrypted = secret_crypto_encrypt_to_text(key, "");
    TEST_ASSERT(encrypted != NULL);

    char *decrypted = NULL;
    int rc = secret_crypto_decrypt_from_text(&arena, key, encrypted, &decrypted);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(strcmp(decrypted, "") == 0);

    free(encrypted);
    arena_free(&arena);
    return 0;
}

static int test_two_encryptions_of_the_same_plaintext_differ(void)
{
    uint8_t key[SECRET_CRYPTO_KEY_LEN];
    fixed_key(key, 0x11);

    char *a = secret_crypto_encrypt_to_text(key, "same-plaintext");
    char *b = secret_crypto_encrypt_to_text(key, "same-plaintext");
    TEST_ASSERT(a != NULL && b != NULL);
    TEST_ASSERT(strcmp(a, b) != 0); /* fresh random nonce each time */

    free(a);
    free(b);
    return 0;
}

static int test_decrypt_rejects_plaintext(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    uint8_t key[SECRET_CRYPTO_KEY_LEN];
    fixed_key(key, 0x99);

    char *decrypted = NULL;
    int rc = secret_crypto_decrypt_from_text(&arena, key, "just-a-plain-key", &decrypted);
    TEST_ASSERT(rc != 0);

    arena_free(&arena);
    return 0;
}

static int test_decrypt_wrong_key_does_not_recover_plaintext(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    uint8_t key_a[SECRET_CRYPTO_KEY_LEN];
    uint8_t key_b[SECRET_CRYPTO_KEY_LEN];
    fixed_key(key_a, 0x01);
    fixed_key(key_b, 0x02);

    char *encrypted = secret_crypto_encrypt_to_text(key_a, "AC1234-MY-STEAM-KEY");
    TEST_ASSERT(encrypted != NULL);

    char *decrypted = NULL;
    int rc = secret_crypto_decrypt_from_text(&arena, key_b, encrypted, &decrypted);
    /* Confidentiality-only cipher: decrypting under the wrong key still
     * "succeeds" (no integrity check), it just doesn't recover the
     * original plaintext -- that's the one thing this asserts. */
    if (rc == 0) TEST_ASSERT(strcmp(decrypted, "AC1234-MY-STEAM-KEY") != 0);

    free(encrypted);
    arena_free(&arena);
    return 0;
}

static int test_load_or_create_key_is_idempotent(void)
{
    setenv("XDG_DATA_HOME", TEST_TMP_DIR "/data_crypto_test", 1);

    uint8_t key1[SECRET_CRYPTO_KEY_LEN];
    uint8_t key2[SECRET_CRYPTO_KEY_LEN];
    err_t err;
    err_clear(&err);

    TEST_ASSERT(secret_crypto_load_or_create_key(key1, &err) == 0);
    err_clear(&err);
    TEST_ASSERT(secret_crypto_load_or_create_key(key2, &err) == 0);

    TEST_ASSERT(memcmp(key1, key2, SECRET_CRYPTO_KEY_LEN) == 0);

    return 0;
}

int main(void)
{
    if (test_is_encrypted()) return 1;
    if (test_encrypt_then_decrypt_round_trips()) return 1;
    if (test_encrypt_empty_string_round_trips()) return 1;
    if (test_two_encryptions_of_the_same_plaintext_differ()) return 1;
    if (test_decrypt_rejects_plaintext()) return 1;
    if (test_decrypt_wrong_key_does_not_recover_plaintext()) return 1;
    if (test_load_or_create_key_is_idempotent()) return 1;

    printf("all tests passed\n");
    return 0;
}
