# tpm.c — Text Profile Matcher

`tpm.c` is a C library and CLI for scoring input text against a reference text profile using character n-gram matching. Given a map file of representative text samples, it builds an n-gram frequency profile and computes a similarity score in [0.0, 1.0] for arbitrary input text.

Use cases include language identification, programming language detection, authorship attribution, or any domain where text samples define a category.

---

## CLI

### Examples

Create a map file with representative samples:

```bash
cat > es.map <<'EOF'
hola mis amigos como estan
buenos dias a todos
este proyecto compara texto corto
el zorro marron salta sobre el perro perezoso
EOF
```

Score input against it:

```bash
echo "El gato esta debajo de la mesa" | ./bin/x86_64/linux/tpm es.map
0.692323
```

Use a different n-gram size:

```bash
echo "def foo(): pass" | ./bin/x86_64/linux/tpm -n 4 python.map
```

### Parameters

| Flag | Description |
| :--- | :--- |
| `-n <size>` | N-gram size (default 3, max 8) |
| `-h`, `--help` | Show help and usage |
| `-v`, `--version` | Show version |

### Input

Map file: one or more lines of representative text. Stdin: text to score.

### Output

A single line with the score formatted to six decimal places:

```
0.763264
```

### Exit codes

| Code | Meaning |
| :--- | :------ |
| 0 | Success |
| 1 | Error (missing map, invalid args, I/O failure, profile build error) |

---

## Public API

### Types

```c
typedef struct kc_tpm kc_tpm_t;
```

### Status codes

| Symbol | Value |
| :----- | :---- |
| `KC_TPM_OK` | 0 |
| `KC_TPM_ERROR` | -1 |

### Functions

| Function | Returns | Description |
| :------- | :------ | :---------- |
| `kc_tpm_open(void)` | `kc_tpm_t *` | Allocate a new context. Returns NULL on failure. |
| `kc_tpm_build(tpm, map_text, ngram_size)` | `int` | Build an n-gram profile from map text. `ngram_size` must be 1–8. |
| `kc_tpm_score(tpm, input_text)` | `double` | Score input text against the built profile. Returns 0.0–1.0. |
| `kc_tpm_close(tpm)` | `void` | Free the context. Safe on NULL. |

### Lifecycle

```c
kc_tpm_t *t = kc_tpm_open();
kc_tpm_build(t, map_text, 3);
double score = kc_tpm_score(t, input_text);
kc_tpm_close(t);
```

---

## Build

| Target | Description |
| :----- | :---------- |
| `make` (default) | Build for native host arch/platform |
| `make all` | Build all cross-compilation targets |
| `make <arch>/<platform>` | Build specific target (e.g. `x86_64/linux`, `x86_64/windows`) |
| `make test` | Run `sh test.sh` |
| `make clean` | Remove `.build/` |

Artifacts at `bin/{arch}/{platform}/`.

Requires CMake 3.14+ and Ninja.

---

## Algorithm

1. **Normalize**: lowercase A–Z, collapse whitespace runs to single spaces, trim.
2. **Extract**: slide a window of `n` bytes over the text to collect n-grams.
3. **Profile**: count each distinct n-gram (deduplicated frequency table).
4. **Score**: for each input n-gram, compute a smoothed log-probability against the map profile, average them, and apply a sigmoid squash to [0, 1].

---

## License

GNU General Public License v3.0
