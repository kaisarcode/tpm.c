# AGENTS.md

## Project Context

`tpm.c` is a small C library and one-shot CLI for scoring input text against one
reference text profile.

It builds an in-memory byte n-gram profile from representative text and returns
a heuristic score from 0 to 1. It is not machine learning infrastructure,
search, classification orchestration, or a model service.

Read `README.md` and `DESIGN.md` before modifying the project.

## Core Invariants

Preserve these properties unless explicitly instructed otherwise:

- one context owns one built reference profile;
- map input is ordinary representative text;
- n-gram size is explicitly 1 through 8;
- normalization lowercases ASCII and collapses whitespace only;
- n-grams are contiguous byte windows, not Unicode characters or tokens;
- raw grams are sorted and counted before edge spaces are stripped;
- separately counted raw grams may remain duplicate stripped profile entries;
- profile capacity is 8192 entries;
- raw window capacity is 16384;
- scoring uses the built profile and returns a bounded heuristic value;
- a score is not a probability or guarantee;
- the CLI reads one map file and complete stdin, then prints one score;
- no network, external model, database, or persistent runtime state is required;
- the implementation remains portable C11 and inspectable.

## Matching Boundary

`tpm.c` measures similarity to one supplied text profile. It does not assign
labels, compare many profiles, choose thresholds, explain matches, detect
language, parse syntax, or establish semantic meaning.

Callers may run multiple independent contexts or CLI invocations when comparing
profiles. Do not absorb catalog, ranking, index, or classifier responsibilities
into this library by default.

Scores are shaped by smoothed n-gram likelihood and a logistic mapping. They are
useful for relative comparisons under similar conditions, not calibrated
confidence percentages.

## Profile Compatibility

The raw-sort-then-trim behavior intentionally mirrors the historical text
pipeline. Leading and trailing spaces are removed after raw grams are grouped,
so distinct raw grams can produce duplicate stripped names with separate
counts.

Do not deduplicate or reinterpret these entries as cleanup without exact score
compatibility tests. Changing normalization, n-gram extraction, smoothing, or
the logistic constants changes all scores.

## Public API and Ownership

Treat `src/libtpm.h` as a compatibility boundary.

Contexts own fixed profile storage, scalar options, and stop state.
Map and input strings are borrowed only during calls. `kc_tpm_build()` replaces
the current profile. `kc_tpm_score()` returns zero for invalid, unbuilt, empty,
or overflow cases rather than allocating an error object.

Do not add hidden persistent profiles or global matching state.

## Source Layout

Preserve the existing `src/` structure:

- `src/tpm.c` contains the CLI;
- `src/libtpm.c` contains profile building and scoring;
- `src/libtpm.h` contains the public contract;
- `src/test.c` contains all tests.

Do not create additional source, header, profile, or test files. Keep every new
test in `test.c`; do not add `test_accuracy.c`, `profile.c`, generated model
sources, or per-domain fixtures.

## Forbidden Default Recommendations

Do not recommend or implement without explicit instruction:

- machine-learning models or embeddings;
- vector databases or search indexes;
- multi-profile catalogs or classifiers;
- training pipelines or corpus collection;
- persisted binary model formats;
- automatic threshold selection;
- Unicode tokenization frameworks;
- explainability or feature dashboards;
- batch services, queues, or parallel workers;
- remote APIs, SaaS, or cloud inference;
- telemetry or analytics;
- plugins or generic metrics frameworks;
- a resident control socket.

Do not justify changes through AI trends, benchmark competition, enterprise
scale, or hypothetical datasets.

## Change Evaluation

Every scoring change must name map text, input text, n-gram size, old score, and
expected score relationship. Check empty and short text, whitespace,
case-folding, capacity boundaries, duplicate stripped grams, unrelated input,
and repeated builds on one context.

Reject speculative model abstractions. Prefer explicit fixed behavior.

## Resource Model

Profiles and raw gram workspaces have fixed capacities. Inputs that generate
more than 16384 raw windows or 8192 profile entries fail profile construction;
scoring overflow returns zero.

The CLI buffers the complete map file and stdin. There is no streaming score,
hidden truncation, cache, database, network, or background work.

Do not replace visible limits with unbounded allocations by default.

## Concurrency

Profile build and scoring do not promise concurrent mutation of one context.

Do not expand lifecycle support into process supervision.

## Testing

Behavioral changes require exact or bounded score tests for identical,
representative, unrelated, empty, short, mixed-case, and whitespace-varied
texts; all n-gram sizes; capacity overflow; rebuilding; invalid contexts; stop
state; and CLI output formatting.

All tests remain in `src/test.c`.

## Build and Completion

For documentation-only changes, run `kcs AGENTS.md DESIGN.md`. For source
changes, use `README.md` build and test commands. Do not run `make clean`
without authorization.

A change is complete when score behavior is explicit and tested, fixed limits
and ownership remain clear, documentation matches implementation, and no
unrelated AI or classification platform was introduced.

The goal is one small text-profile comparison primitive.
