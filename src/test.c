/**
 * test.c - libtpm public API contract tests.
 * Summary: Validates each exported libtpm function through one dedicated test case.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "tpm.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int signal_count;
static int signal_count_b;
static kc_tpm_t *signal_ctx_seen;

/**
 * Stores one observed signal callback.
 * @param tpm Context passed by the library.
 * @return None.
 */
static void count_signal(kc_tpm_t *tpm) {
    signal_count++;
    signal_ctx_seen = tpm;
}

/**
 * Stores one observed replacement signal callback.
 * @param tpm Context passed by the library.
 * @return None.
 */
static void count_signal_b(kc_tpm_t *tpm) {
    (void)tpm;
    signal_count_b++;
}

/**
 * Verifies one integer result.
 * @param name Check description.
 * @param expected Expected value.
 * @param actual Actual value.
 * @return 0 on success, 1 on failure.
 */
static int expect_int(const char *name, int expected, int actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected %d, got %d\n", name, expected, actual);
        return 1;
    }
    return 0;
}

/**
 * Verifies one boolean condition.
 * @param name Check description.
 * @param condition Non-zero when the check passed.
 * @return 0 on success, 1 on failure.
 */
static int expect_true(const char *name, int condition) {
    if (!condition) {
        fprintf(stderr, "%s\n", name);
        return 1;
    }
    return 0;
}

/**
 * Verifies one score is within the public score range.
 * @param name Check description.
 * @param score Score returned by the library.
 * @return 0 on success, 1 on failure.
 */
static int expect_score_range(const char *name, double score) {
    return expect_true(name, score >= 0.0 && score <= 1.0);
}

/**
 * Allocates a repeated byte string.
 * @param byte Byte to repeat.
 * @param count Number of bytes.
 * @return Allocated string, or NULL on failure.
 */
static char *repeat_byte(char byte, size_t count) {
    char *text;

    text = (char *)malloc(count + 1);
    if (text == NULL) return NULL;
    memset(text, byte, count);
    text[count] = '\0';
    return text;
}

/**
 * Tests kc_tpm_options_default.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpm_options_default(void) {
    kc_tpm_options_t opts;

    opts = kc_tpm_options_default();
    return expect_int("options_default initializes reserved", 0, opts.reserved);
}

/**
 * Tests kc_tpm_options_load_env.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpm_options_load_env(void) {
    kc_tpm_options_t opts;
    int rc;

    rc = 0;
    opts = kc_tpm_options_default();
    opts.reserved = 9;
    kc_tpm_options_load_env(&opts);
    rc += expect_int("kc_tpm_options_load_env preserves unmapped options", 9, opts.reserved);
    kc_tpm_options_load_env(NULL);
    rc += expect_true("kc_tpm_options_load_env accepts NULL", 1);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_tpm_options_free.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpm_options_free(void) {
    kc_tpm_options_t opts;
    int rc;

    rc = 0;
    opts = kc_tpm_options_default();
    opts.reserved = 11;
    kc_tpm_options_free(&opts);
    rc += expect_int("kc_tpm_options_free preserves plain options", 11, opts.reserved);
    kc_tpm_options_free(NULL);
    rc += expect_true("kc_tpm_options_free accepts NULL", 1);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_tpm_on_signal.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpm_on_signal(void) {
    kc_tpm_options_t opts;
    kc_tpm_t *tpm;
    int rc;
    int i;

    rc = 0;
    opts = kc_tpm_options_default();
    tpm = NULL;
    signal_count = 0;
    signal_count_b = 0;
    signal_ctx_seen = NULL;
    rc += expect_int("on_signal(NULL) returns ERROR", KC_TPM_ERROR,
        kc_tpm_on_signal(NULL, 1, count_signal));
    rc += expect_int("open returns OK", KC_TPM_OK, kc_tpm_open(&tpm, &opts));
    rc += expect_int("remove missing handler returns OK", KC_TPM_OK,
        kc_tpm_on_signal(tpm, 1, NULL));
    rc += expect_int("register signal handler returns OK", KC_TPM_OK,
        kc_tpm_on_signal(tpm, 1, count_signal));
    rc += expect_int("replace signal handler returns OK", KC_TPM_OK,
        kc_tpm_on_signal(tpm, 1, count_signal_b));
    rc += expect_int("raise replaced signal returns OK", KC_TPM_OK,
        kc_tpm_raise_signal(tpm, 1));
    rc += expect_int("old callback was not invoked", 0, signal_count);
    rc += expect_int("replacement callback was invoked", 1, signal_count_b);
    rc += expect_int("remove signal handler returns OK", KC_TPM_OK,
        kc_tpm_on_signal(tpm, 1, NULL));
    for (i = 0; i < 8; i++) {
        rc += expect_int("register growth handler returns OK", KC_TPM_OK,
            kc_tpm_on_signal(tpm, 200 + i, count_signal));
    }
    signal_count = 0;
    rc += expect_int("raise growth handler returns OK", KC_TPM_OK,
        kc_tpm_raise_signal(tpm, 207));
    rc += expect_int("growth callback was invoked", 1, signal_count);
    rc += expect_int("close(ctx) returns OK", KC_TPM_OK, kc_tpm_close(tpm));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_tpm_raise_signal.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpm_raise_signal(void) {
    kc_tpm_options_t opts;
    kc_tpm_t *tpm;
    int rc;

    rc = 0;
    opts = kc_tpm_options_default();
    tpm = NULL;
    signal_count = 0;
    signal_ctx_seen = NULL;
    rc += expect_int("raise_signal(NULL) returns ERROR", KC_TPM_ERROR,
        kc_tpm_raise_signal(NULL, 1));
    rc += expect_int("open returns OK", KC_TPM_OK, kc_tpm_open(&tpm, &opts));
    rc += expect_int("raise unhandled signal returns ERROR", KC_TPM_ERROR,
        kc_tpm_raise_signal(tpm, 1));
    rc += expect_int("register signal handler returns OK", KC_TPM_OK,
        kc_tpm_on_signal(tpm, 1, count_signal));
    rc += expect_int("raise handled signal returns OK", KC_TPM_OK,
        kc_tpm_raise_signal(tpm, 1));
    rc += expect_int("signal callback was invoked", 1, signal_count);
    rc += expect_true("signal callback received context", signal_ctx_seen == tpm);
    rc += expect_int("close(ctx) returns OK", KC_TPM_OK, kc_tpm_close(tpm));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_tpm_stop.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpm_stop(void) {
    kc_tpm_options_t opts;
    kc_tpm_t *tpm;
    int rc;

    rc = 0;
    opts = kc_tpm_options_default();
    tpm = NULL;
    rc += expect_int("stop(NULL) returns ERROR", KC_TPM_ERROR, kc_tpm_stop(NULL));
    rc += expect_int("open returns OK", KC_TPM_OK, kc_tpm_open(&tpm, &opts));
    rc += expect_int("stop(ctx) returns OK", KC_TPM_OK, kc_tpm_stop(tpm));
    rc += expect_int("stop(ctx) second call returns OK", KC_TPM_OK, kc_tpm_stop(tpm));
    rc += expect_int("build after stop returns OK", KC_TPM_OK,
        kc_tpm_build(tpm, "test data", 2));
    rc += expect_score_range("score after stop remains valid", kc_tpm_score(tpm, "test"));
    rc += expect_int("close(ctx) returns OK", KC_TPM_OK, kc_tpm_close(tpm));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_tpm_listen_signals.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpm_listen_signals(void) {
    kc_tpm_options_t opts;
    kc_tpm_t *tpm;
    int rc;

    rc = 0;
    opts = kc_tpm_options_default();
    tpm = NULL;
    rc += expect_int("listen_signals(NULL) returns ERROR", KC_TPM_ERROR,
        kc_tpm_listen_signals(NULL));
    rc += expect_int("open returns OK", KC_TPM_OK, kc_tpm_open(&tpm, &opts));
    rc += expect_int("listen_signals(ctx) returns OK", KC_TPM_OK,
        kc_tpm_listen_signals(tpm));
    rc += expect_int("close(ctx) returns OK", KC_TPM_OK, kc_tpm_close(tpm));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_tpm_listen_signal.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpm_listen_signal(void) {
    kc_tpm_options_t opts;
    kc_tpm_t *tpm;
    int rc;

    rc = 0;
    opts = kc_tpm_options_default();
    tpm = NULL;
    rc += expect_int("listen_signal(NULL) returns ERROR", KC_TPM_ERROR,
        kc_tpm_listen_signal(NULL, 1));
    rc += expect_int("open returns OK", KC_TPM_OK, kc_tpm_open(&tpm, &opts));
#ifdef _WIN32
    rc += expect_int("listen_signal(ctx, 2) returns OK", KC_TPM_OK,
        kc_tpm_listen_signal(tpm, 2));
#else
    rc += expect_int("listen_signal(ctx, SIGUSR1) returns OK", KC_TPM_OK,
        kc_tpm_listen_signal(tpm, SIGUSR1));
#endif
    rc += expect_int("close(ctx) returns OK", KC_TPM_OK, kc_tpm_close(tpm));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_tpm_signal_listener.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpm_signal_listener(void) {
    kc_tpm_options_t opts;
    kc_tpm_t *tpm;
    int rc;

    rc = 0;
    opts = kc_tpm_options_default();
    tpm = NULL;
    signal_count = 0;
    signal_ctx_seen = NULL;
    rc += expect_int("open returns OK", KC_TPM_OK, kc_tpm_open(&tpm, &opts));
    rc += expect_int("register listener handler returns OK", KC_TPM_OK,
        kc_tpm_on_signal(tpm, 44, count_signal));
    rc += expect_int("listen_signals(ctx) returns OK", KC_TPM_OK,
        kc_tpm_listen_signals(tpm));
    kc_tpm_signal_listener(44);
    rc += expect_int("signal_listener dispatches callback", 1, signal_count);
    rc += expect_true("signal_listener dispatches correct context", signal_ctx_seen == tpm);
    rc += expect_int("close(ctx) returns OK", KC_TPM_OK, kc_tpm_close(tpm));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_tpm_open.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpm_open(void) {
    kc_tpm_options_t opts;
    kc_tpm_t *tpm;
    int rc;

    rc = 0;
    opts = kc_tpm_options_default();
    tpm = NULL;
    rc += expect_int("kc_tpm_open rejects NULL out", KC_TPM_ERROR,
        kc_tpm_open(NULL, &opts));
    rc += expect_int("kc_tpm_open rejects NULL opts", KC_TPM_ERROR,
        kc_tpm_open(&tpm, NULL));
    rc += expect_true("kc_tpm_open leaves output unchanged on error", tpm == NULL);
    rc += expect_int("kc_tpm_open creates context", KC_TPM_OK,
        kc_tpm_open(&tpm, &opts));
    rc += expect_true("kc_tpm_open sets output", tpm != NULL);
    if (tpm != NULL) rc += expect_int("close opened context", KC_TPM_OK, kc_tpm_close(tpm));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_tpm_build.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpm_build(void) {
    kc_tpm_options_t opts;
    kc_tpm_t *tpm;
    char *large_text;
    int rc;

    rc = 0;
    opts = kc_tpm_options_default();
    tpm = NULL;
    large_text = NULL;
    rc += expect_int("kc_tpm_build rejects NULL ctx", KC_TPM_ERROR,
        kc_tpm_build(NULL, "abc", 2));
    rc += expect_int("open context for build", KC_TPM_OK, kc_tpm_open(&tpm, &opts));
    rc += expect_int("kc_tpm_build rejects NULL text", KC_TPM_ERROR,
        kc_tpm_build(tpm, NULL, 2));
    rc += expect_int("kc_tpm_build rejects n below range", KC_TPM_ERROR,
        kc_tpm_build(tpm, "abc", 0));
    rc += expect_int("kc_tpm_build rejects n above range", KC_TPM_ERROR,
        kc_tpm_build(tpm, "abc", 9));
    rc += expect_int("kc_tpm_build accepts n=1", KC_TPM_OK,
        kc_tpm_build(tpm, "abc abc", 1));
    rc += expect_int("kc_tpm_build accepts n=8", KC_TPM_OK,
        kc_tpm_build(tpm, "abcdefgh abcdefgh", 8));
    rc += expect_int("kc_tpm_build accepts empty profile", KC_TPM_OK,
        kc_tpm_build(tpm, "", 2));
    large_text = repeat_byte('a', 16385);
    rc += expect_true("allocate overflow text", large_text != NULL);
    if (large_text != NULL) {
        rc += expect_int("kc_tpm_build reports raw gram overflow", KC_TPM_ERROR,
            kc_tpm_build(tpm, large_text, 1));
    }
    free(large_text);
    rc += expect_int("close build context", KC_TPM_OK, kc_tpm_close(tpm));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_tpm_score.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpm_score(void) {
    kc_tpm_options_t opts;
    kc_tpm_t *tpm;
    double matching_score;
    double mismatching_score;
    double normalized_score;
    double plain_score;
    int rc;

    rc = 0;
    opts = kc_tpm_options_default();
    tpm = NULL;
    rc += expect_true("kc_tpm_score NULL ctx returns zero", kc_tpm_score(NULL, "abc") == 0.0);
    rc += expect_int("open context for score", KC_TPM_OK, kc_tpm_open(&tpm, &opts));
    rc += expect_true("kc_tpm_score before build returns zero", kc_tpm_score(tpm, "abc") == 0.0);
    rc += expect_int("build scoring profile", KC_TPM_OK,
        kc_tpm_build(tpm, "hello world hello world english text", 3));
    matching_score = kc_tpm_score(tpm, "hello world english text");
    mismatching_score = kc_tpm_score(tpm, "zzzz qqqq xxxx yyyy");
    rc += expect_score_range("matching score is in range", matching_score);
    rc += expect_score_range("mismatching score is in range", mismatching_score);
    rc += expect_true("kc_tpm_score ranks matching text higher", matching_score > mismatching_score);
    rc += expect_true("kc_tpm_score NULL input returns zero", kc_tpm_score(tpm, NULL) == 0.0);
    rc += expect_int("build normalization profile", KC_TPM_OK,
        kc_tpm_build(tpm, "  Hello\tWORLD\nhello   world  ", 3));
    normalized_score = kc_tpm_score(tpm, "hello world");
    plain_score = kc_tpm_score(tpm, "HELLO\tWORLD");
    rc += expect_true("kc_tpm_score normalizes case and whitespace", plain_score == normalized_score);
    rc += expect_int("close score context", KC_TPM_OK, kc_tpm_close(tpm));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_tpm_close.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpm_close(void) {
    kc_tpm_options_t opts;
    kc_tpm_t *tpm;
    int rc;

    rc = 0;
    opts = kc_tpm_options_default();
    tpm = NULL;
    rc += expect_int("kc_tpm_close rejects NULL", KC_TPM_ERROR, kc_tpm_close(NULL));
    rc += expect_int("open context for close", KC_TPM_OK, kc_tpm_open(&tpm, &opts));
    rc += expect_int("kc_tpm_close releases context", KC_TPM_OK, kc_tpm_close(tpm));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_tpm_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpm_version(void) {
    return expect_true("version returns non-zero build timestamp", kc_tpm_version() != 0U);
}

/**
 * Runs one libtpm contract test case.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success, 1 or 2 on failure.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (strcmp(argv[1], "kc_tpm_options_default") == 0) return case_kc_tpm_options_default();
    if (strcmp(argv[1], "kc_tpm_options_load_env") == 0) return case_kc_tpm_options_load_env();
    if (strcmp(argv[1], "kc_tpm_options_free") == 0) return case_kc_tpm_options_free();
    if (strcmp(argv[1], "kc_tpm_on_signal") == 0) return case_kc_tpm_on_signal();
    if (strcmp(argv[1], "kc_tpm_raise_signal") == 0) return case_kc_tpm_raise_signal();
    if (strcmp(argv[1], "kc_tpm_stop") == 0) return case_kc_tpm_stop();
    if (strcmp(argv[1], "kc_tpm_listen_signals") == 0) return case_kc_tpm_listen_signals();
    if (strcmp(argv[1], "kc_tpm_listen_signal") == 0) return case_kc_tpm_listen_signal();
    if (strcmp(argv[1], "kc_tpm_signal_listener") == 0) return case_kc_tpm_signal_listener();
    if (strcmp(argv[1], "kc_tpm_open") == 0) return case_kc_tpm_open();
    if (strcmp(argv[1], "kc_tpm_build") == 0) return case_kc_tpm_build();
    if (strcmp(argv[1], "kc_tpm_score") == 0) return case_kc_tpm_score();
    if (strcmp(argv[1], "kc_tpm_close") == 0) return case_kc_tpm_close();
    if (strcmp(argv[1], "kc_tpm_version") == 0) return case_kc_tpm_version();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
