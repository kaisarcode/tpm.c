/**
 * test.c - libtpm portable contract tests.
 * Summary: Validates exported libtpm behavior through the public C API.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "tpm.h"

#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int signal_count = 0;
static kc_tpm_t *signal_ctx_seen = NULL;

/**
 * Stores one observed signal callback.
 * @param tpm Context passed by the library.
 * @return None.
 */
static void count_signal(kc_tpm_t *tpm) {
    if (tpm != NULL) {
        signal_count++;
        signal_ctx_seen = tpm;
    }
}

static int signal_count_b = 0;

/**
 * Stores one observed replacement signal callback.
 * @param tpm Context passed by the library.
 * @return None.
 */
static void count_signal_b(kc_tpm_t *tpm) {
    if (tpm != NULL) {
        signal_count_b++;
    }
}

/**
 * Verifies one integer result and prints a descriptive pass/fail line.
 * @param name Check description.
 * @param expected Expected value.
 * @param actual Actual value.
 * @return 0 on success, 1 on failure.
 */
static int expect_int(const char *name, int expected, int actual) {
    if (expected != actual) {
        printf("\033[31m[FAIL]\033[0m %s: expected %d, got %d\n", name, expected, actual);
        return 1;
    }
    printf("\033[32m[PASS]\033[0m %s\n", name);
    return 0;
}

/**
 * Verifies one boolean condition and prints a descriptive pass/fail line.
 * @param name Check description.
 * @param condition Non-zero when the check passed.
 * @return 0 on success, 1 on failure.
 */
static int expect_true(const char *name, int condition) {
    if (!condition) {
        printf("\033[31m[FAIL]\033[0m %s\n", name);
        return 1;
    }
    printf("\033[32m[PASS]\033[0m %s\n", name);
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
 * Verifies kc_tpm_options_default, kc_tpm_options_load_env, and
 * kc_tpm_options_free in meaningful paths.
 * @return 0 on success, 1 on failure.
 */
static int case_options(void) {
    kc_tpm_options_t opts;
    int rc;

    rc = 0;
    opts = kc_tpm_options_default();
    rc += expect_int("options_default initializes reserved to zero", 0, opts.reserved);

    opts.reserved = 7;
    kc_tpm_options_load_env(&opts);
    rc += expect_int("load_env leaves reserved unchanged when no env mappings exist", 7, opts.reserved);

    kc_tpm_options_load_env(NULL);
    rc += expect_true("load_env(NULL) does not crash", 1);
    kc_tpm_options_free(&opts);
    rc += expect_int("options_free keeps reserved value", 7, opts.reserved);
    kc_tpm_options_free(NULL);
    rc += expect_true("options_free(NULL) does not crash", 1);

    return rc == 0 ? 0 : 1;
}

/**
 * Verifies kc_tpm_open and kc_tpm_close in meaningful paths.
 * @return 0 on success, 1 on failure.
 */
static int case_open_close(void) {
    kc_tpm_options_t opts;
    kc_tpm_t *tpm;
    int rc;

    rc = 0;
    opts = kc_tpm_options_default();
    tpm = NULL;

    rc += expect_int("open(NULL, opts) returns ERROR", KC_TPM_ERROR,
        kc_tpm_open(NULL, &opts));
    rc += expect_int("open(out, NULL) returns ERROR", KC_TPM_ERROR,
        kc_tpm_open(&tpm, NULL));
    rc += expect_true("open with NULL args leaves out as NULL", tpm == NULL);
    rc += expect_int("open(out, opts) returns OK", KC_TPM_OK,
        kc_tpm_open(&tpm, &opts));
    rc += expect_true("open sets context", tpm != NULL);
    rc += expect_int("close(NULL) returns ERROR", KC_TPM_ERROR,
        kc_tpm_close(NULL));
    rc += expect_int("close(ctx) returns OK", KC_TPM_OK, kc_tpm_close(tpm));

    return rc == 0 ? 0 : 1;
}

/**
 * Verifies kc_tpm_build and kc_tpm_score for normal, edge, and error paths.
 * @return 0 on success, 1 on failure.
 */
static int case_profile_score(void) {
    kc_tpm_options_t opts;
    kc_tpm_t *tpm;
    char *large_text;
    double matching_score;
    double mismatching_score;
    int rc;

    rc = 0;
    opts = kc_tpm_options_default();
    tpm = NULL;
    large_text = NULL;

    rc += expect_int("build(NULL, text, n) returns ERROR", KC_TPM_ERROR,
        kc_tpm_build(NULL, "abc", 2));
    rc += expect_score_range("score(NULL, text) returns score range",
        kc_tpm_score(NULL, "abc"));

    rc += expect_int("open returns OK", KC_TPM_OK, kc_tpm_open(&tpm, &opts));
    rc += expect_score_range("score before build returns score range",
        kc_tpm_score(tpm, "abc"));
    rc += expect_true("score before build returns zero", kc_tpm_score(tpm, "abc") == 0.0);
    rc += expect_int("build(ctx, NULL, n) returns ERROR", KC_TPM_ERROR,
        kc_tpm_build(tpm, NULL, 2));
    rc += expect_int("build(ctx, text, n=0) returns ERROR", KC_TPM_ERROR,
        kc_tpm_build(tpm, "abc", 0));
    rc += expect_int("build(ctx, text, n=9) returns ERROR", KC_TPM_ERROR,
        kc_tpm_build(tpm, "abc", 9));
    rc += expect_int("build(ctx, empty, n) returns OK", KC_TPM_OK,
        kc_tpm_build(tpm, "", 2));
    rc += expect_true("score after empty profile returns zero",
        kc_tpm_score(tpm, "abc") == 0.0);

    rc += expect_int("build normal profile returns OK", KC_TPM_OK,
        kc_tpm_build(tpm,
            "hello world hello world text profile matcher english words",
            3));
    matching_score = kc_tpm_score(tpm, "hello world english text");
    mismatching_score = kc_tpm_score(tpm, "zzzz qqqq xxxx yyyy");
    rc += expect_score_range("matching score is in range", matching_score);
    rc += expect_score_range("mismatching score is in range", mismatching_score);
    rc += expect_true("matching text scores higher than mismatching text",
        matching_score > mismatching_score);
    rc += expect_true("score(NULL input) returns zero", kc_tpm_score(tpm, NULL) == 0.0);

    rc += expect_int("build n=1 profile returns OK", KC_TPM_OK,
        kc_tpm_build(tpm, "abc abc", 1));
    rc += expect_score_range("n=1 score is in range", kc_tpm_score(tpm, "abc"));
    rc += expect_int("build n=8 profile returns OK", KC_TPM_OK,
        kc_tpm_build(tpm, "abcdefgh abcdefgh", 8));
    rc += expect_score_range("n=8 score is in range", kc_tpm_score(tpm, "abcdefgh"));

    large_text = repeat_byte('a', 16385);
    rc += expect_true("large overflow input allocation succeeds", large_text != NULL);
    if (large_text != NULL) {
        rc += expect_int("build over raw gram limit returns ERROR", KC_TPM_ERROR,
            kc_tpm_build(tpm, large_text, 1));
    }

    free(large_text);
    rc += expect_int("close(ctx) returns OK", KC_TPM_OK, kc_tpm_close(tpm));

    return rc == 0 ? 0 : 1;
}

/**
 * Verifies kc_tpm_stop in meaningful paths.
 * @return 0 on success, 1 on failure.
 */
static int case_stop(void) {
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
 * Verifies signal registration, replacement, removal, listener registration,
 * callback routing, and dynamic handler growth.
 * @return 0 on success, 1 on failure.
 */
static int case_signals(void) {
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
    rc += expect_int("raise_signal(NULL) returns ERROR", KC_TPM_ERROR,
        kc_tpm_raise_signal(NULL, 1));
    rc += expect_int("listen_signals(NULL) returns ERROR", KC_TPM_ERROR,
        kc_tpm_listen_signals(NULL));
    rc += expect_int("listen_signal(NULL) returns ERROR", KC_TPM_ERROR,
        kc_tpm_listen_signal(NULL, 1));

    rc += expect_int("open returns OK", KC_TPM_OK, kc_tpm_open(&tpm, &opts));
    rc += expect_int("raise unhandled signal returns ERROR", KC_TPM_ERROR,
        kc_tpm_raise_signal(tpm, 1));
    rc += expect_int("remove missing handler returns OK", KC_TPM_OK,
        kc_tpm_on_signal(tpm, 1, NULL));
    rc += expect_int("register signal handler returns OK", KC_TPM_OK,
        kc_tpm_on_signal(tpm, 1, count_signal));
    rc += expect_int("raise handled signal returns OK", KC_TPM_OK,
        kc_tpm_raise_signal(tpm, 1));
    rc += expect_int("signal callback was invoked", 1, signal_count);
    rc += expect_true("signal callback received context", signal_ctx_seen == tpm);

    rc += expect_int("replace signal handler returns OK", KC_TPM_OK,
        kc_tpm_on_signal(tpm, 1, count_signal_b));
    signal_count = 0;
    signal_count_b = 0;
    rc += expect_int("raise replaced signal returns OK", KC_TPM_OK,
        kc_tpm_raise_signal(tpm, 1));
    rc += expect_int("old callback was not invoked", 0, signal_count);
    rc += expect_int("replacement callback was invoked", 1, signal_count_b);
    rc += expect_int("remove signal handler returns OK", KC_TPM_OK,
        kc_tpm_on_signal(tpm, 1, NULL));
    rc += expect_int("raise removed signal returns ERROR", KC_TPM_ERROR,
        kc_tpm_raise_signal(tpm, 1));

    for (i = 0; i < 8; i++) {
        rc += expect_int("register growth handler returns OK", KC_TPM_OK,
            kc_tpm_on_signal(tpm, 200 + i, count_signal));
    }
    signal_count = 0;
    rc += expect_int("raise growth handler returns OK", KC_TPM_OK,
        kc_tpm_raise_signal(tpm, 207));
    rc += expect_int("growth callback was invoked", 1, signal_count);

    rc += expect_int("register listener handler returns OK", KC_TPM_OK,
        kc_tpm_on_signal(tpm, 44, count_signal));
    rc += expect_int("listen_signals(ctx) returns OK", KC_TPM_OK,
        kc_tpm_listen_signals(tpm));
    signal_count = 0;
    signal_ctx_seen = NULL;
    kc_tpm_signal_listener(44);
    rc += expect_int("signal_listener dispatches callback", 1, signal_count);
    rc += expect_true("signal_listener dispatches correct context", signal_ctx_seen == tpm);

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
 * Verifies two contexts coexist and one stopped context does not break another.
 * @return 0 on success, 1 on failure.
 */
static int case_multictx(void) {
    kc_tpm_options_t opts;
    kc_tpm_t *first;
    kc_tpm_t *second;
    int rc;

    rc = 0;
    opts = kc_tpm_options_default();
    first = NULL;
    second = NULL;

    rc += expect_int("open first context returns OK", KC_TPM_OK,
        kc_tpm_open(&first, &opts));
    rc += expect_int("open second context returns OK", KC_TPM_OK,
        kc_tpm_open(&second, &opts));
    rc += expect_int("stop first context returns OK", KC_TPM_OK,
        kc_tpm_stop(first));
    rc += expect_int("build second context after first stopped returns OK", KC_TPM_OK,
        kc_tpm_build(second, "test data", 2));
    rc += expect_score_range("score second context after first stopped is valid",
        kc_tpm_score(second, "test"));
    rc += expect_int("listen_signals(first) returns OK", KC_TPM_OK,
        kc_tpm_listen_signals(first));
    rc += expect_int("listen_signals(second) returns OK", KC_TPM_OK,
        kc_tpm_listen_signals(second));
    rc += expect_int("close first context returns OK", KC_TPM_OK,
        kc_tpm_close(first));
    rc += expect_int("close second context returns OK", KC_TPM_OK,
        kc_tpm_close(second));

    return rc == 0 ? 0 : 1;
}

/**
 * Verifies kc_tpm_version returns a non-zero build timestamp.
 * @return 0 on success, 1 on failure.
 */
static int case_version(void) {
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
    if (strcmp(argv[1], "version") == 0) return case_version();
    if (strcmp(argv[1], "options") == 0) return case_options();
    if (strcmp(argv[1], "open-close") == 0) return case_open_close();
    if (strcmp(argv[1], "profile-score") == 0) return case_profile_score();
    if (strcmp(argv[1], "stop") == 0) return case_stop();
    if (strcmp(argv[1], "signals") == 0) return case_signals();
    if (strcmp(argv[1], "multictx") == 0) return case_multictx();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
