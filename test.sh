#!/bin/sh
# Summary: Validation suite for tpm functionality.
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: https://www.gnu.org/licenses/gpl-3.0.html

# Prints one failure line.
# @param $1 Failure message.
# @return 1 on failure.
kc_test_fail() {
    printf '\033[31m[FAIL]\033[0m %s\n' "$1"
    return 1
}

# Prints one success line.
# @param $1 Success message.
# @return 0 on success.
kc_test_pass() {
    printf '\033[32m[PASS]\033[0m %s\n' "$1"
    return 0
}

# Detects the artifact architecture for the current machine.
# @return Architecture name on stdout.
kc_test_arch() {
    case "$(uname -m)" in
        x86_64 | amd64)
            printf '%s\n' "x86_64"
            ;;
        aarch64 | arm64)
            printf '%s\n' "aarch64"
            ;;
        armv7l | armv7)
            printf '%s\n' "armv7"
            ;;
        i386 | i486 | i586 | i686)
            printf '%s\n' "i686"
            ;;
        ppc64le | powerpc64le)
            printf '%s\n' "powerpc64le"
            ;;
        *)
            uname -m
            ;;
    esac
}

# Detects the artifact platform for the current machine.
# @return Platform name on stdout.
kc_test_platform() {
    case "$(uname -s)" in
        Linux)
            printf '%s\n' "linux"
            ;;
        *)
            uname -s | tr '[:upper:]' '[:lower:]'
            ;;
    esac
}

# Returns the CLI path for the current architecture and platform.
# @return CLI path on stdout.
kc_test_binary_path() {
    printf './bin/%s/%s/tpm\n' "$(kc_test_arch)" "$(kc_test_platform)"
}

# Verifies the binary exists and is executable.
# @return 0 on success, 1 on failure.
kc_test_check_binary() {
    if [ ! -x "$BIN" ]; then
        kc_test_fail "binary not found: $BIN"
        return 1
    fi
    return 0
}

# Tests help flag.
# @return 0 on success, 1 on failure.
kc_test_help() {
    if "$BIN" -h >/dev/null 2>&1 && "$BIN" --help >/dev/null 2>&1; then
        kc_test_pass "help flag"
    else
        kc_test_fail "help flag"
        return 1
    fi
}

# Tests version flag.
# @return 0 on success, 1 on failure.
kc_test_version() {
    if "$BIN" -v >/dev/null 2>&1 && "$BIN" --version >/dev/null 2>&1; then
        kc_test_pass "version flag"
    else
        kc_test_fail "version flag"
        return 1
    fi
}

# Tests unknown flag.
# @return 0 on success, 1 on failure.
kc_test_unknown_flag() {
    if "$BIN" --bogus >/dev/null 2>&1; then
        kc_test_fail "unknown flag should fail"
        return 1
    fi
    kc_test_pass "unknown flag"
}

# Tests missing map file.
# @return 0 on success, 1 on failure.
kc_test_missing_map() {
    if "$BIN" >/dev/null 2>&1; then
        kc_test_fail "missing map should fail"
        return 1
    fi
    kc_test_pass "missing map"
}

# Tests invalid map path.
# @return 0 on success, 1 on failure.
kc_test_invalid_map() {
    if "$BIN" /nonexistent/map.file >/dev/null 2>&1; then
        kc_test_fail "invalid map should fail"
        return 1
    fi
    kc_test_pass "invalid map"
}

# Tests missing value for -n.
# @return 0 on success, 1 on failure.
kc_test_missing_n() {
    if "$BIN" -n map.file >/dev/null 2>&1; then
        kc_test_fail "missing -n value should fail"
        return 1
    fi
    kc_test_pass "missing -n value"
}

# Tests scoring with a known map.
# @return 0 on success, 1 on failure.
kc_test_scoring() {
    printf 'hola mis amigos como estan\nbuenos dias a todos\neste proyecto compara texto corto\nel zorro marron salta sobre el perro perezoso' > "${TMP}/es.map"
    printf 'hello my friends how are you\ngood morning everyone\nthis project matches short text\nthe quick brown fox jumps over the lazy dog' > "${TMP}/en.map"

    result_en=$(printf 'The cat is under the table' | "$BIN" "${TMP}/en.map" 2>/dev/null)
    result_es=$(printf 'El gato esta debajo de la mesa' | "$BIN" "${TMP}/es.map" 2>/dev/null)

    if [ -z "$result_en" ] || [ -z "$result_es" ]; then
        kc_test_fail "scoring returned empty"
        return 1
    fi

    kc_test_pass "scoring produces output"
}

# Tests empty input.
# @return 0 on success, 1 on failure.
kc_test_empty_input() {
    printf 'some text' > "${TMP}/empty.map"
    result=$(printf '' | "$BIN" "${TMP}/empty.map" 2>/dev/null)
    if [ "$result" = "0.000000" ]; then
        kc_test_pass "empty input"
    else
        kc_test_fail "empty input should give 0.000000"
        return 1
    fi
}

# Tests with different n-gram sizes.
# @return 0 on success, 1 on failure.
kc_test_ngram_size() {
    printf 'test data for ngram profile building' > "${TMP}/ngram.map"
    if printf 'test' | "$BIN" -n 2 "${TMP}/ngram.map" >/dev/null 2>&1 && \
    printf 'test' | "$BIN" -n 4 "${TMP}/ngram.map" >/dev/null 2>&1; then
        kc_test_pass "different n-gram sizes"
    else
        kc_test_fail "different n-gram sizes"
        return 1
    fi
}

# Runs the full validation suite.
# @return 0 on success, 1 on failure.
kc_test_main() {
    failed=0
    TMP=$(mktemp -d)
    BIN=$(kc_test_binary_path)

    kc_test_check_binary || exit 1

    kc_test_help           || failed=$((failed + 1))
    kc_test_version        || failed=$((failed + 1))
    kc_test_unknown_flag   || failed=$((failed + 1))
    kc_test_missing_map    || failed=$((failed + 1))
    kc_test_invalid_map    || failed=$((failed + 1))
    kc_test_missing_n      || failed=$((failed + 1))
    kc_test_scoring        || failed=$((failed + 1))
    kc_test_empty_input    || failed=$((failed + 1))
    kc_test_ngram_size     || failed=$((failed + 1))

    rm -rf "${TMP}"
    return $failed
}

kc_test_main
