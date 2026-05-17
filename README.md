# tpm.c - Text Profile Matcher

`tpm.c` tells you how similar a piece of text is to a reference text. Create a
profile from representative samples, then score any input from 0.0 (dissimilar)
to 1.0 (very similar).

---

## CLI

### Example

Create a map file with Python code:

```bash
cat > python.map <<'EOF'
def hello():
    print("hello world")
```
```

Score Python input against it:

```bash
echo "def foo(): pass" | ./bin/x86_64/linux/tpm python.map
0.999992
```

### Demo: use cases

Create profiles for different domains and compare how inputs score against
matching vs mismatching profiles.

**Natural language - English vs Spanish:**

```
cat > english.map <<'EOF'
The quick brown fox jumps over the lazy dog.
Pack my box with five dozen liquor jugs.
The five boxing wizards jump quickly.
EOF

cat > spanish.map <<'EOF'
El zorro marron salta sobre el perro perezoso.
Los exploradores descubrieron una nueva especie.
Las civilizaciones antiguas construyeron estructuras magnificas.
EOF
```

| Input | vs english.map | vs spanish.map |
| :---- | :------------: | :------------: |
| "The quick brown fox jumps over the lazy dog near the river bank." | 0.445819 | 0.000334 |
| "El zorro marron salta sobre el perro perezoso cerca del arroyo." | 0.001779 | 0.131115 |

**Programming language - Python vs JavaScript:**

Tells Python and JS syntax apart. Python input scores higher on the Python
profile than on the JS profile.

| Input | vs python.map | vs javascript.map |
| :---- | :-----------: | :---------------: |
| `def add(a, b): return a + b` | 0.164708 | 0.048800 |
| `function add(a, b) { return a + b; }` | 0.055775 | 0.298180 |

**Log format - Apache vs Syslog:**

Distinguishes HTTP access logs from system logs by their line structure.

| Input | vs apache.map | vs syslog.map |
| :---- | :-----------: | :-----------: |
| '127.0.0.1 - admin [12/May/2026:08:30:00 +0000] "GET /dashboard HTTP/1.1" 200 4567' | 0.390322 | 0.000875 |
| 'May 12 08:30:00 laptop kernel: PCI device enabled for power management' | 0.001641 | 0.016910 |

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
