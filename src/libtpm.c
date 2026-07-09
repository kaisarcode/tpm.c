/**
 * libtpm.c - Text profile matcher.
 * Summary: Core implementation for the tpm library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#include <unistd.h>
#endif
#include <signal.h>

#include "libtpm.h"

#if !defined(KC_TPM_BUILD_VERSION) || KC_TPM_BUILD_VERSION + 0 == 0
#undef KC_TPM_BUILD_VERSION
#define KC_TPM_BUILD_VERSION 0ULL
#endif

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#ifndef _WIN32
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#endif

#define KC_TPM_MAX_GRAMS 8192
#define KC_TPM_RAW_MAX   16384
#define KC_TPM_NG_MAX    8

typedef enum {
    KC_ENV_TYPE_INT,
    KC_ENV_TYPE_FLOAT,
    KC_ENV_TYPE_STR
} kc_env_type_t;

typedef struct {
    const char *env_var;
    size_t offset;
    kc_env_type_t type;
} kc_env_map_t;

static const kc_env_map_t env_config_table[] = {
    { "KC_TPM_CTRL", offsetof(kc_tpm_options_t, ctrl_path), KC_ENV_TYPE_STR },
};
static const int env_config_table_n = sizeof(env_config_table) / sizeof(env_config_table[0]);

typedef struct {
    int sig;
    kc_tpm_signal_callback_t cb;
} kc_tpm_signal_entry_t;

typedef struct {
    char *cmd;
    kc_tpm_ctrl_callback_t cb;
} kc_tpm_ctrl_entry_t;

typedef struct {
    int fd;
    char *buf;
    size_t used;
    size_t cap;
} kc_tpm_ctrl_conn_t;

static kc_tpm_t **g_signal_ctx_list = NULL;
static int g_signal_ctx_cap = 0;
static int g_signal_ctx_count = 0;

typedef struct {
    char gram[12];
    int count;
} kc_tpm_gram_t;

struct kc_tpm {
    kc_tpm_gram_t profile[KC_TPM_MAX_GRAMS];
    int profile_size;
    long total;
    int ngram_size;

    kc_tpm_options_t opts;
    kc_tpm_signal_entry_t *signal_handlers;
    int n_signal_handlers;
    int signal_handlers_capacity;
    volatile sig_atomic_t stop_requested;
    int ctrl_fd;
    char *ctrl_path;
    kc_tpm_ctrl_entry_t *ctrl_handlers;
    int n_ctrl_handlers;
    int ctrl_handlers_cap;
    kc_tpm_ctrl_conn_t *ctrl_conns;
    int n_ctrl_conns;
    int ctrl_conns_cap;
};

/**
 * Closes the control socket and active connections.
 * @param ctx Context handle.
 * @return KC_TPM_OK on success, or KC_TPM_ERROR on failure.
 */
int kc_tpm_ctrl_close(kc_tpm_t *ctx);

/**
 * Normalizes text: lowercase A-Z, collapse whitespace runs to single space,
 * trim leading/trailing spaces.
 * @param input Input text to normalize.
 * @param out_len Output length (bytes).
 * @return Allocated string the caller must free, or NULL on error.
 */
static char *kc_tpm_norm(const char *input, size_t *out_len) {
    size_t i, j;
    size_t len;
    char *out;
    int in_space;

    if (!input || !out_len) {
        return NULL;
    }

    len = strlen(input);
    out = (char *)malloc(len + 1);
    if (!out) {
        return NULL;
    }

    in_space = 1;
    j = 0;

    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)input[i];
        if (isspace((int)c)) {
            if (!in_space) {
                out[j++] = ' ';
                in_space = 1;
            }
        } else {
            if (c >= 'A' && c <= 'Z') {
                out[j++] = (char)(c + ('a' - 'A'));
            } else {
                out[j++] = (char)c;
            }
            in_space = 0;
        }
    }

    if (j > 0 && out[j - 1] == ' ') {
        j--;
    }

    out[j] = '\0';
    *out_len = j;
    return out;
}

/**
 * Compare two raw gram strings for qsort (memcmp by first 8 bytes).
 * @param a Left element.
 * @param b Right element.
 * @return Negative, zero, or positive.
 */
static int kc_tpm_raw_cmp(const void *a, const void *b) {
    return memcmp(a, b, 8);
}

/**
 * Build an n-gram profile: extract raw n-grams, sort, uniq count,
 * strip leading/trailing spaces.
 *
 * This replicates sort|uniq -c which groups by RAW bytes, then strips spaces
 * (as the shell's read -r strips IFS whitespace), potentially producing
 * duplicate entries for the same stripped gram (e.g. " el" and "el " both
 * become "el" with separate counts).
 *
 * @param text Normalized text.
 * @param len Length of text.
 * @param n N-gram size.
 * @param profile Output profile array.
 * @param out_size Number of profile entries written.
 * @param out_total Total number of n-gram windows.
 * @return KC_TPM_OK on success, KC_TPM_ERROR on overflow.
 */
static int kc_tpm_grams(
    const char *text,
    size_t len,
    int n,
    kc_tpm_gram_t *profile,
    int *out_size,
    long *out_total
) {
    char raw[KC_TPM_RAW_MAX][12];
    int raw_count;
    size_t i;
    int size;
    long total;
    int idx;

    raw_count = 0;

    for (i = 0; i + (size_t)n <= len; i++) {
        if (raw_count >= KC_TPM_RAW_MAX) {
            return KC_TPM_ERROR;
        }
        memcpy(raw[raw_count], text + i, (size_t)n);
        raw[raw_count][n] = '\0';
        raw_count++;
    }

    total = (long)raw_count;

    qsort(raw, (size_t)raw_count, sizeof(raw[0]), kc_tpm_raw_cmp);

    size = 0;
    idx = 0;
    while (idx < raw_count) {
        int dup_count;
        char *gram_raw;
        int start;
        int end;
        int gram_len;

        gram_raw = raw[idx];
        dup_count = 1;
        idx++;

        while (idx < raw_count &&
            memcmp(gram_raw, raw[idx], (size_t)n + 1) == 0) {
            dup_count++;
            idx++;
        }

        start = 0;
        while (start < n && gram_raw[start] == ' ') {
            start++;
        }

        end = n - 1;
        while (end >= start && gram_raw[end] == ' ') {
            end--;
        }

        if (start > end) {
            continue;
        }

        gram_len = end - start + 1;

        if (size >= KC_TPM_MAX_GRAMS) {
            return KC_TPM_ERROR;
        }

        memcpy(profile[size].gram, gram_raw + start, (size_t)gram_len);
        profile[size].gram[gram_len] = '\0';
        profile[size].count = dup_count;
        size++;
    }

    *out_size = size;
    *out_total = total;
    return KC_TPM_OK;
}

/**
 * Allocate and initialize a new tpm context.
 * Prepares one inference context.
 * @param out Pointer to receive the context pointer.
 * @param opts Options.
 * @return KC_TPM_OK on success, or KC_TPM_ERROR on failure.
 */
int kc_tpm_open(kc_tpm_t **out, const kc_tpm_options_t *opts) {
    kc_tpm_t *tpm;
    if (!out || !opts) return KC_TPM_ERROR;
    tpm = (kc_tpm_t *)calloc(1, sizeof(kc_tpm_t));
    if (!tpm) return KC_TPM_ERROR;
    tpm->opts = *opts;
    tpm->opts.ctrl_path = opts->ctrl_path ? strdup(opts->ctrl_path) : NULL;
    if (opts->ctrl_path && !tpm->opts.ctrl_path) {
        free(tpm);
        return KC_TPM_ERROR;
    }
    tpm->ctrl_fd = -1;
    *out = tpm;
    return KC_TPM_OK;
}

/**
 * Build an n-gram profile from map text.
 * @param tpm Context pointer.
 * @param map_text Text to build profile from.
 * @param ngram_size N-gram size.
 * @return KC_TPM_OK on success, KC_TPM_ERROR on failure.
 */
int kc_tpm_build(kc_tpm_t *tpm, const char *map_text, int ngram_size) {
    char *norm;
    size_t len;
    int rc;

    if (!tpm || !map_text || ngram_size < 1 || ngram_size > KC_TPM_NG_MAX) {
        return KC_TPM_ERROR;
    }

    tpm->profile_size = 0;
    tpm->total = 0;
    tpm->ngram_size = ngram_size;

    norm = kc_tpm_norm(map_text, &len);
    if (!norm) {
        return KC_TPM_ERROR;
    }

    rc = kc_tpm_grams(
        norm, len, ngram_size,
        tpm->profile, &tpm->profile_size, &tpm->total
    );

    free(norm);
    return rc;
}

/**
 * Score input text against the built profile.
 * @param tpm Context pointer with built profile.
 * @param input_text Text to score.
 * @return Score in [0.0, 1.0].
 */
double kc_tpm_score(kc_tpm_t *tpm, const char *input_text) {
    char *norm;
    size_t len;
    kc_tpm_gram_t input_profile[KC_TPM_MAX_GRAMS];
    int input_size;
    long input_total;
    int i;
    double log_sum;

    if (!tpm || !input_text || tpm->profile_size <= 0 || tpm->total <= 0) {
        return 0.0;
    }

    norm = kc_tpm_norm(input_text, &len);
    if (!norm) {
        return 0.0;
    }

    if (kc_tpm_grams(norm, len, tpm->ngram_size,
            input_profile, &input_size, &input_total) != KC_TPM_OK) {
        free(norm);
        return 0.0;
    }

    free(norm);

    if (input_total <= 0) {
        return 0.0;
    }

    log_sum = 0.0;
    for (i = 0; i < input_size; i++) {
        int map_count;
        int j;
        double numerator;
        double denominator;

        map_count = 0;
        for (j = 0; j < tpm->profile_size; j++) {
            if (strcmp(input_profile[i].gram, tpm->profile[j].gram) == 0) {
                map_count = tpm->profile[j].count;
            }
        }

        numerator = (double)map_count + 1.0;
        denominator = (double)tpm->total + (double)tpm->profile_size;
        log_sum += (double)input_profile[i].count *
            log(numerator / denominator);
    }

    if (log_sum == 0.0 && input_total > 0) {
        double denominator = (double)tpm->total + (double)tpm->profile_size;
        log_sum = (double)input_total * log(1.0 / denominator);
    }

    double avg_log = log_sum / (double)input_total;
    double score = 1.0 / (1.0 + exp(-8.0 * (avg_log + 5.25)));

    if (score < 0.0) score = 0.0;
    if (score > 1.0) score = 1.0;

    return score;
}

/**
 * Release a tpm context.
 * @param tpm Context pointer.
 * @return KC_TPM_OK on success, or KC_TPM_ERROR on failure.
 */
int kc_tpm_close(kc_tpm_t *tpm) {
    int i;
    if (!tpm) return KC_TPM_ERROR;
    for (i = 0; i < g_signal_ctx_count; i++) {
        if (g_signal_ctx_list[i] == tpm) {
            g_signal_ctx_list[i] = g_signal_ctx_list[--g_signal_ctx_count];
            break;
        }
    }
    kc_tpm_ctrl_close(tpm);
    kc_tpm_options_free(&tpm->opts);
    free(tpm->signal_handlers);
    for (i = 0; i < tpm->n_ctrl_handlers; i++) {
        free(tpm->ctrl_handlers[i].cmd);
    }
    free(tpm->ctrl_handlers);
    free(tpm->ctrl_conns);
    free(tpm);
    return KC_TPM_OK;
}

/**
 * Create an options struct initialized with default values.
 * @param none Unused.
 * @return Default-initialized options.
 */
kc_tpm_options_t kc_tpm_options_default(void) {
    kc_tpm_options_t opts;
    memset(&opts, 0, sizeof(opts));
    return opts;
}

/**
 * Load configuration from environment variables.
 * @param opts Options to update.
 * @return None.
 */
void kc_tpm_options_load_env(kc_tpm_options_t *opts) {
    int i;
    if (!opts) return;
    for (i = 0; i < env_config_table_n; i++) {
        const char *val = getenv(env_config_table[i].env_var);
        char *end;
        if (!val) continue;
        switch (env_config_table[i].type) {
            case KC_ENV_TYPE_INT: {
                long v = strtol(val, &end, 10);
                if (end != val && *end == '\0') {
                    *(int *)((char *)opts + env_config_table[i].offset) = (int)v;
                }
                break;
            }
            case KC_ENV_TYPE_FLOAT: {
                float v = strtof(val, &end);
                if (end != val && *end == '\0') {
                    *(float *)((char *)opts + env_config_table[i].offset) = v;
                }
                break;
            }
            case KC_ENV_TYPE_STR: {
                char **p = (char **)((char *)opts + env_config_table[i].offset);
                free(*p);
                *p = strdup(val);
                break;
            }
        }
    }
}

/**
 * Free dynamically allocated resources within an options struct.
 * @param opts Options to clean up.
 * @return None.
 */
void kc_tpm_options_free(kc_tpm_options_t *opts) {
    if (!opts) return;
    free(opts->ctrl_path);
    opts->ctrl_path = NULL;
}

/**
 * Request stop for a specific tpm context.
 * @param tpm Context pointer.
 * @return KC_TPM_OK on success, or KC_TPM_ERROR on failure.
 */
int kc_tpm_stop(kc_tpm_t *tpm) {
    if (!tpm) return KC_TPM_ERROR;
    tpm->stop_requested = 1;
    return KC_TPM_OK;
}

/**
 * Returns whether stop was requested on a specific tpm context.
 * @param tpm Context pointer.
 * @return 1 if stop was requested, or 0 otherwise.
 */
int kc_tpm_stop_requested(kc_tpm_t *tpm) {
    if (!tpm) return 0;
    return tpm->stop_requested ? 1 : 0;
}

/**
 * Register a handler for a library-level signal number.
 * @param tpm Context pointer.
 * @param sig Application-defined signal number.
 * @param cb Callback to invoke.
 * @return KC_TPM_OK on success, or KC_TPM_ERROR on failure.
 */
int kc_tpm_on_signal(kc_tpm_t *tpm, int sig, kc_tpm_signal_callback_t cb) {
    int i;
    if (!tpm) return KC_TPM_ERROR;
    for (i = 0; i < tpm->n_signal_handlers; i++) {
        if (tpm->signal_handlers[i].sig == sig) {
            if (cb) {
                tpm->signal_handlers[i].cb = cb;
            } else {
                int tail = tpm->n_signal_handlers - i - 1;
                if (tail > 0) {
                    memmove(&tpm->signal_handlers[i],
                            &tpm->signal_handlers[i + 1],
                            (size_t)tail * sizeof(kc_tpm_signal_entry_t));
                }
                tpm->n_signal_handlers--;
            }
            return KC_TPM_OK;
        }
    }
    if (!cb) return KC_TPM_OK;
    if (tpm->n_signal_handlers >= tpm->signal_handlers_capacity) {
        int new_cap = tpm->signal_handlers_capacity ? tpm->signal_handlers_capacity * 2 : 4;
        kc_tpm_signal_entry_t *p = (kc_tpm_signal_entry_t *)realloc(tpm->signal_handlers,
            (size_t)new_cap * sizeof(kc_tpm_signal_entry_t));
        if (!p) return KC_TPM_ERROR;
        tpm->signal_handlers = p;
        tpm->signal_handlers_capacity = new_cap;
    }
    tpm->signal_handlers[tpm->n_signal_handlers].sig = sig;
    tpm->signal_handlers[tpm->n_signal_handlers].cb = cb;
    tpm->n_signal_handlers++;
    return KC_TPM_OK;
}

/**
 * Raise a library-level signal.
 * @param tpm Context pointer.
 * @param sig Signal number to raise.
 * @return KC_TPM_OK if handled, or KC_TPM_ERROR if no handler.
 */
int kc_tpm_raise_signal(kc_tpm_t *tpm, int sig) {
    int i;
    if (!tpm) return KC_TPM_ERROR;
    for (i = 0; i < tpm->n_signal_handlers; i++) {
        if (tpm->signal_handlers[i].sig == sig) {
            tpm->signal_handlers[i].cb(tpm);
            return KC_TPM_OK;
        }
    }
    return KC_TPM_ERROR;
}

/**
 * Set the internal signal-listener context.
 * @param tpm Context pointer.
 * @return KC_TPM_OK on success, or KC_TPM_ERROR if tpm is NULL.
 */
int kc_tpm_listen_signals(kc_tpm_t *tpm) {
    if (!tpm) return KC_TPM_ERROR;
    if (g_signal_ctx_count >= g_signal_ctx_cap) {
        int new_cap = g_signal_ctx_cap ? g_signal_ctx_cap * 2 : 4;
        kc_tpm_t **new_list = (kc_tpm_t **)realloc(g_signal_ctx_list,
            (size_t)new_cap * sizeof(kc_tpm_t *));
        if (!new_list) return KC_TPM_ERROR;
        g_signal_ctx_list = new_list;
        g_signal_ctx_cap = new_cap;
    }
    g_signal_ctx_list[g_signal_ctx_count++] = tpm;
    return KC_TPM_OK;
}

/**
 * Wire an OS signal to the library signal listener.
 * @param tpm Context pointer.
 * @param sig_id OS signal number.
 * @return KC_TPM_OK on success, or KC_TPM_ERROR on failure.
 */
int kc_tpm_listen_signal(kc_tpm_t *tpm, int sig_id) {
    if (!tpm) return KC_TPM_ERROR;
    if (g_signal_ctx_count >= g_signal_ctx_cap) {
        int new_cap = g_signal_ctx_cap ? g_signal_ctx_cap * 2 : 4;
        kc_tpm_t **new_list = (kc_tpm_t **)realloc(g_signal_ctx_list,
            (size_t)new_cap * sizeof(kc_tpm_t *));
        if (!new_list) return KC_TPM_ERROR;
        g_signal_ctx_list = new_list;
        g_signal_ctx_cap = new_cap;
    }
    g_signal_ctx_list[g_signal_ctx_count++] = tpm;
#ifdef _WIN32
    (void)sig_id;
#else
    signal(sig_id, kc_tpm_signal_listener);
#endif
    return KC_TPM_OK;
}

/**
 * Generic signal-listener compatible with signal() / sigaction().
 * @param sig OS signal number.
 * @return None.
 */
void kc_tpm_signal_listener(int sig) {
    int i;
    for (i = 0; i < g_signal_ctx_count; i++) {
        if (g_signal_ctx_list[i] &&
            kc_tpm_raise_signal(g_signal_ctx_list[i], sig) == 0)
            return;
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

/**
 * Retrieves the library build version as a Unix timestamp.
 * @return Build version timestamp.
 */
uint64_t kc_tpm_version(void) {
    return (uint64_t)KC_TPM_BUILD_VERSION;
}

#ifndef _WIN32

/**
 * Sends a text message to a control connection.
 * @param fd File descriptor.
 * @param msg Message string.
 * @return KC_TPM_OK on success, or KC_TPM_ERROR on failure.
 */
static int kc_tpm_ctrl_send(int fd, const char *msg) {
    size_t len;

    if (!msg) return KC_TPM_ERROR;
    len = strlen(msg);
    return (size_t)write(fd, msg, len) == len ? KC_TPM_OK : KC_TPM_ERROR;
}

/**
 * Handles the HELP control command.
 * @param ctx Context handle.
 * @param fd Control connection file descriptor.
 * @param argc Number of arguments.
 * @param argv Argument vector.
 * @return KC_TPM_OK on success.
 */
static int kc_tpm_ctrl_default_help(kc_tpm_t *ctx, int fd, int argc, char **argv) {
    int i;
    char tmp[4096];
    size_t pos;

    (void)argc;
    (void)argv;
    pos = 0;
    for (i = 0; i < ctx->n_ctrl_handlers; i++) {
        size_t len = strlen(ctx->ctrl_handlers[i].cmd);
        if (pos + len + 2 > sizeof(tmp)) break;
        if (pos > 0) {
            tmp[pos] = ' ';
            pos++;
        }
        memcpy(tmp + pos, ctx->ctrl_handlers[i].cmd, len);
        pos += len;
    }
    if (pos + 1 > sizeof(tmp)) pos = sizeof(tmp) - 1;
    tmp[pos] = '\n';
    kc_tpm_ctrl_send(fd, "OK ");
    write(fd, tmp, pos + 1);
    return KC_TPM_OK;
}

/**
 * Handles the STOP control command.
 * @param ctx Context handle.
 * @param fd Control connection file descriptor.
 * @param argc Number of arguments.
 * @param argv Argument vector.
 * @return KC_TPM_OK on success.
 */
static int kc_tpm_ctrl_default_stop(kc_tpm_t *ctx, int fd, int argc, char **argv) {
    (void)argc;
    (void)argv;
    if (kc_tpm_stop(ctx) == KC_TPM_OK) {
        kc_tpm_ctrl_send(fd, "OK\n");
    } else {
        kc_tpm_ctrl_send(fd, "ERR\n");
    }
    return KC_TPM_OK;
}

/**
 * Handles the GET control command.
 * @param ctx Context handle.
 * @param fd Control connection file descriptor.
 * @param argc Number of arguments.
 * @param argv Argument vector.
 * @return KC_TPM_OK on success.
 */
static int kc_tpm_ctrl_default_get(kc_tpm_t *ctx, int fd, int argc, char **argv) {
    char tmp[256];
    const char *ctrl_value;

    if (argc < 2) {
        kc_tpm_ctrl_send(fd, "ERR missing key\n");
        return KC_TPM_OK;
    }

    if (strcmp(argv[1], "ctrl_path") == 0) {
        ctrl_value = ctx->ctrl_path ? ctx->ctrl_path : ctx->opts.ctrl_path;
        snprintf(tmp, sizeof(tmp), "OK %s\n", ctrl_value ? ctrl_value : "");
        write(fd, tmp, strlen(tmp));
        return KC_TPM_OK;
    }

    if (strcmp(argv[1], "reserved") == 0) {
        snprintf(tmp, sizeof(tmp), "OK %d\n", ctx->opts.reserved);
        write(fd, tmp, strlen(tmp));
        return KC_TPM_OK;
    }

    kc_tpm_ctrl_send(fd, "ERR unknown key\n");
    return KC_TPM_OK;
}

/**
 * Handles the SET control command.
 * @param ctx Context handle.
 * @param fd Control connection file descriptor.
 * @param argc Number of arguments.
 * @param argv Argument vector.
 * @return KC_TPM_OK on success.
 */
static int kc_tpm_ctrl_default_set(kc_tpm_t *ctx, int fd, int argc, char **argv) {
    (void)ctx;
    (void)argv;
    if (argc < 3) {
        kc_tpm_ctrl_send(fd, "ERR missing value\n");
        return KC_TPM_OK;
    }
    kc_tpm_ctrl_send(fd, "ERR unknown key\n");
    return KC_TPM_OK;
}

/**
 * Parses and dispatches one control command line.
 * @param ctx Context handle.
 * @param fd Control connection file descriptor.
 * @param line Command line.
 * @return KC_TPM_OK on success, or KC_TPM_ERROR on failure.
 */
static int kc_tpm_ctrl_dispatch(kc_tpm_t *ctx, int fd, const char *line) {
    char *copy;
    char *argv[64];
    int argc;
    int i;

    if (!ctx || !line) return KC_TPM_ERROR;

    copy = strdup(line);
    if (!copy) return KC_TPM_ERROR;

    argc = 0;
    argv[argc] = strtok(copy, " \t\r\n");
    if (argv[argc]) {
        argc++;
        while (argc < 64 && (argv[argc] = strtok(NULL, " \t\r\n")) != NULL) {
            argc++;
        }
    }

    if (argc == 0) {
        free(copy);
        return KC_TPM_OK;
    }

    for (i = 0; i < ctx->n_ctrl_handlers; i++) {
        if (strcmp(ctx->ctrl_handlers[i].cmd, argv[0]) == 0) {
            ctx->ctrl_handlers[i].cb(ctx, fd, argc, argv);
            free(copy);
            return KC_TPM_OK;
        }
    }

    kc_tpm_ctrl_send(fd, "ERR unknown command\n");
    free(copy);
    return KC_TPM_OK;
}

#endif

/**
 * Register a control command handler.
 * @param ctx Context handle.
 * @param cmd Command name.
 * @param cb Callback function, or NULL to remove.
 * @return KC_TPM_OK on success, or KC_TPM_ERROR on failure.
 */
int kc_tpm_ctrl_on(kc_tpm_t *ctx, const char *cmd, kc_tpm_ctrl_callback_t cb) {
    int i;

    if (!ctx || !cmd) return KC_TPM_ERROR;

    for (i = 0; i < ctx->n_ctrl_handlers; i++) {
        if (strcmp(ctx->ctrl_handlers[i].cmd, cmd) == 0) {
            if (cb) {
                ctx->ctrl_handlers[i].cb = cb;
            } else {
                int tail = ctx->n_ctrl_handlers - i - 1;
                free(ctx->ctrl_handlers[i].cmd);
                if (tail > 0) {
                    memmove(&ctx->ctrl_handlers[i],
                        &ctx->ctrl_handlers[i + 1],
                        (size_t)tail * sizeof(kc_tpm_ctrl_entry_t));
                }
                ctx->n_ctrl_handlers--;
            }
            return KC_TPM_OK;
        }
    }

    if (!cb) return KC_TPM_OK;

    if (ctx->n_ctrl_handlers >= ctx->ctrl_handlers_cap) {
        int new_cap = ctx->ctrl_handlers_cap ? ctx->ctrl_handlers_cap * 2 : 4;
        kc_tpm_ctrl_entry_t *p = (kc_tpm_ctrl_entry_t *)realloc(ctx->ctrl_handlers,
            (size_t)new_cap * sizeof(kc_tpm_ctrl_entry_t));
        if (!p) return KC_TPM_ERROR;
        ctx->ctrl_handlers = p;
        ctx->ctrl_handlers_cap = new_cap;
    }

    ctx->ctrl_handlers[ctx->n_ctrl_handlers].cmd = strdup(cmd);
    if (!ctx->ctrl_handlers[ctx->n_ctrl_handlers].cmd) return KC_TPM_ERROR;
    ctx->ctrl_handlers[ctx->n_ctrl_handlers].cb = cb;
    ctx->n_ctrl_handlers++;
    return KC_TPM_OK;
}

/**
 * Remove a control command handler.
 * @param ctx Context handle.
 * @param cmd Command name.
 * @return KC_TPM_OK on success, or KC_TPM_ERROR on failure.
 */
int kc_tpm_ctrl_off(kc_tpm_t *ctx, const char *cmd) {
    return kc_tpm_ctrl_on(ctx, cmd, NULL);
}

/**
 * Open a Unix domain socket for control commands.
 * @param ctx Context handle.
 * @param path Socket path.
 * @return KC_TPM_OK on success, or KC_TPM_ERROR on failure.
 */
int kc_tpm_ctrl_open(kc_tpm_t *ctx, const char *path) {
#ifndef _WIN32
    struct sockaddr_un addr;
    int fd;
    int flags;

    if (!ctx || !path || ctx->ctrl_fd >= 0) return KC_TPM_ERROR;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return KC_TPM_ERROR;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    unlink(path);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return KC_TPM_ERROR;
    }

    if (listen(fd, 4) < 0) {
        close(fd);
        unlink(path);
        return KC_TPM_ERROR;
    }

    flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    ctx->ctrl_fd = fd;
    ctx->ctrl_path = strdup(path);
    if (!ctx->ctrl_path) {
        close(fd);
        unlink(path);
        ctx->ctrl_fd = -1;
        return KC_TPM_ERROR;
    }

    if (kc_tpm_ctrl_on(ctx, "HELP", kc_tpm_ctrl_default_help) != KC_TPM_OK ||
        kc_tpm_ctrl_on(ctx, "STOP", kc_tpm_ctrl_default_stop) != KC_TPM_OK ||
        kc_tpm_ctrl_on(ctx, "GET", kc_tpm_ctrl_default_get) != KC_TPM_OK ||
        kc_tpm_ctrl_on(ctx, "SET", kc_tpm_ctrl_default_set) != KC_TPM_OK) {
        kc_tpm_ctrl_close(ctx);
        return KC_TPM_ERROR;
    }

    return KC_TPM_OK;
#else
    (void)ctx;
    (void)path;
    return KC_TPM_ERROR;
#endif
}

/**
 * Closes the control socket and active connections.
 * @param ctx Context handle.
 * @return KC_TPM_OK on success, or KC_TPM_ERROR on failure.
 */
int kc_tpm_ctrl_close(kc_tpm_t *ctx) {
#ifndef _WIN32
    int i;

    if (!ctx) return KC_TPM_OK;

    for (i = 0; i < ctx->n_ctrl_conns; i++) {
        if (ctx->ctrl_conns[i].fd >= 0) {
            close(ctx->ctrl_conns[i].fd);
        }
        free(ctx->ctrl_conns[i].buf);
        ctx->ctrl_conns[i].buf = NULL;
    }
    ctx->n_ctrl_conns = 0;

    if (ctx->ctrl_fd >= 0) {
        close(ctx->ctrl_fd);
        ctx->ctrl_fd = -1;
    }

    if (ctx->ctrl_path) {
        unlink(ctx->ctrl_path);
        free(ctx->ctrl_path);
        ctx->ctrl_path = NULL;
    }

    return KC_TPM_OK;
#else
    (void)ctx;
    return KC_TPM_OK;
#endif
}

/**
 * Poll the control socket without blocking.
 * @param ctx Context handle.
 * @return Number of handled commands, or KC_TPM_ERROR on failure.
 */
int kc_tpm_ctrl_poll(kc_tpm_t *ctx) {
#ifndef _WIN32
    struct pollfd pfds[64];
    int nfds;
    int i;
    int handled;

    if (!ctx || ctx->ctrl_fd < 0) return 0;

    handled = 0;
    nfds = 0;
    pfds[nfds].fd = ctx->ctrl_fd;
    pfds[nfds].events = POLLIN;
    pfds[nfds].revents = 0;
    nfds++;

    for (i = 0; i < ctx->n_ctrl_conns && nfds < 64; i++) {
        if (ctx->ctrl_conns[i].fd >= 0) {
            pfds[nfds].fd = ctx->ctrl_conns[i].fd;
            pfds[nfds].events = POLLIN;
            pfds[nfds].revents = 0;
            nfds++;
        }
    }

    if (poll(pfds, (nfds_t)nfds, 0) < 0) return KC_TPM_ERROR;

    if (pfds[0].revents & POLLIN) {
        int conn_fd = accept(ctx->ctrl_fd, NULL, NULL);
        if (conn_fd >= 0) {
            int conn_flags = fcntl(conn_fd, F_GETFL, 0);
            if (conn_flags >= 0) {
                fcntl(conn_fd, F_SETFL, conn_flags | O_NONBLOCK);
            }

            if (ctx->n_ctrl_conns >= ctx->ctrl_conns_cap) {
                int new_cap = ctx->ctrl_conns_cap ? ctx->ctrl_conns_cap * 2 : 4;
                kc_tpm_ctrl_conn_t *p = (kc_tpm_ctrl_conn_t *)realloc(ctx->ctrl_conns,
                    (size_t)new_cap * sizeof(kc_tpm_ctrl_conn_t));
                if (p) {
                    ctx->ctrl_conns = p;
                    ctx->ctrl_conns_cap = new_cap;
                }
            }

            if (ctx->n_ctrl_conns < ctx->ctrl_conns_cap) {
                ctx->ctrl_conns[ctx->n_ctrl_conns].fd = conn_fd;
                ctx->ctrl_conns[ctx->n_ctrl_conns].buf = NULL;
                ctx->ctrl_conns[ctx->n_ctrl_conns].used = 0;
                ctx->ctrl_conns[ctx->n_ctrl_conns].cap = 0;
                ctx->n_ctrl_conns++;
            } else {
                close(conn_fd);
            }
        }
    }

    for (i = 0; i < ctx->n_ctrl_conns; i++) {
        int pidx;
        int j;

        pidx = -1;
        for (j = 1; j < nfds; j++) {
            if (pfds[j].fd == ctx->ctrl_conns[i].fd) {
                pidx = j;
                break;
            }
        }
        if (pidx < 0 || !(pfds[pidx].revents & POLLIN)) continue;

        for (;;) {
            char chunk[256];
            ssize_t n = read(ctx->ctrl_conns[i].fd, chunk, sizeof(chunk));
            if (n < 0) break;
            if (n == 0) {
                close(ctx->ctrl_conns[i].fd);
                ctx->ctrl_conns[i].fd = -1;
                free(ctx->ctrl_conns[i].buf);
                ctx->ctrl_conns[i].buf = NULL;
                ctx->ctrl_conns[i].used = 0;
                ctx->ctrl_conns[i].cap = 0;
                break;
            }

            {
                size_t offset = 0;
                while ((size_t)n > offset) {
                    char *nl = (char *)memchr(chunk + offset, '\n', (size_t)n - offset);
                    if (!nl) {
                        size_t avail = (size_t)n - offset;
                        if (ctx->ctrl_conns[i].used + avail + 1 > ctx->ctrl_conns[i].cap) {
                            size_t new_cap = ctx->ctrl_conns[i].cap ? ctx->ctrl_conns[i].cap * 2 : 256;
                            while (new_cap < ctx->ctrl_conns[i].used + avail + 1) new_cap *= 2;
                            {
                                char *p = (char *)realloc(ctx->ctrl_conns[i].buf, new_cap);
                                if (!p) break;
                                ctx->ctrl_conns[i].buf = p;
                                ctx->ctrl_conns[i].cap = new_cap;
                            }
                        }
                        memcpy(ctx->ctrl_conns[i].buf + ctx->ctrl_conns[i].used, chunk + offset, avail);
                        ctx->ctrl_conns[i].used += avail;
                        ctx->ctrl_conns[i].buf[ctx->ctrl_conns[i].used] = '\0';
                        offset = (size_t)n;
                    } else {
                        size_t line_len = (size_t)(nl - (chunk + offset));
                        size_t total = ctx->ctrl_conns[i].used + line_len;

                        if (total + 1 > ctx->ctrl_conns[i].cap) {
                            size_t new_cap = ctx->ctrl_conns[i].cap ? ctx->ctrl_conns[i].cap * 2 : 256;
                            while (new_cap < total + 1) new_cap *= 2;
                            {
                                char *p = (char *)realloc(ctx->ctrl_conns[i].buf, new_cap);
                                if (!p) break;
                                ctx->ctrl_conns[i].buf = p;
                                ctx->ctrl_conns[i].cap = new_cap;
                            }
                        }

                        if (line_len > 0) {
                            memcpy(ctx->ctrl_conns[i].buf + ctx->ctrl_conns[i].used,
                                chunk + offset, line_len);
                        }
                        ctx->ctrl_conns[i].buf[total] = '\0';
                        kc_tpm_ctrl_dispatch(ctx, ctx->ctrl_conns[i].fd, ctx->ctrl_conns[i].buf);
                        handled++;
                        ctx->ctrl_conns[i].used = 0;
                        offset += line_len + 1;
                    }
                }
            }
        }
    }

    {
        int write_idx = 0;
        for (i = 0; i < ctx->n_ctrl_conns; i++) {
            if (ctx->ctrl_conns[i].fd >= 0) {
                if (write_idx != i) {
                    ctx->ctrl_conns[write_idx] = ctx->ctrl_conns[i];
                }
                write_idx++;
            }
        }
        ctx->n_ctrl_conns = write_idx;
    }

    return handled;
#else
    (void)ctx;
    return 0;
#endif
}
