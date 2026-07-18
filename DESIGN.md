# tpm.c Design

## Purpose

`tpm.c` builds one reference n-gram profile from representative text and scores
another text against it.

The operator supplies the category meaning through the map text. The library
only computes similarity; it does not manage labels or decisions.

## Normalization

Input is copied, ASCII `A-Z` is lowercased, whitespace runs collapse to one
space, and leading or trailing spaces are removed.

Other bytes are preserved. There is no punctuation removal, Unicode case
folding, normalization form, locale, or tokenization.

N-grams are byte windows over this normalized string.

## Profile Construction

`kc_tpm_build()` accepts n-gram sizes 1 through 8 and resets the context profile.

All raw windows are copied into a fixed 16384-entry workspace, sorted by their
raw bytes, and grouped into counts. Leading and trailing spaces are then removed
from each grouped gram before storing up to 8192 profile entries.

Because grouping precedes stripping, different raw windows can become equal
stored strings with separate counts. This preserves historical pipeline
behavior and is part of score compatibility.

The context stores profile entries, total raw windows, and selected n-gram size.

## Scoring

Input text is normalized and profiled with the same process. Each input gram
looks up the corresponding reference count. Add-one smoothing produces a log
likelihood contribution weighted by the input count.

The average log value is mapped with a fixed logistic function and clamped to
`[0, 1]`.

The result is a heuristic similarity score. It is not a probability and scores
from different map construction methods or n-gram sizes are not inherently
calibrated against each other.

Invalid, unbuilt, empty, or over-capacity scoring returns `0.0`.

## Context and Ownership

`kc_tpm_open()` allocates a context containing the complete fixed profile.
`kc_tpm_build()` borrows map text for the call. `kc_tpm_score()` borrows input
text. Neither string is retained.

Options currently reserve the common lifecycle shape but have no effective
configuration fields. Contexts own callbacks and stop state.

## CLI

The CLI reads one complete map file and complete stdin into dynamic buffers,
builds one profile, scores stdin, and prints exactly six decimal places.

The default n-gram size is 3. `-n` accepts 1 through 8. Empty stdin prints
`0.000000`. The CLI is one-shot and has no resident request protocol.

## Resource Model

The context profile is fixed at 8192 entries. Profile construction and scoring
use fixed raw and profile workspaces, while normalization allocates proportional
to input byte length.

The CLI buffers complete files and stdin. There is no persistent model file,
cache, index, network dependency, or background work.

## Portability

The implementation is portable C11 and uses libm for logarithmic and logistic
scoring. Scores should remain materially stable across supported platforms,
subject to normal floating-point formatting differences.

## Non-Goals

`tpm.c` does not provide multi-label classification, profile catalogs, search,
embeddings, machine learning, Unicode linguistic processing, persisted binary
models, automatic thresholds, distributed scoring, remote APIs, telemetry, or
a control plane.

## Change Criteria

A matching change must demonstrate concrete map and input texts, preserve or
explicitly revise historical score behavior, retain visible capacity limits,
and test normalization, raw grouping, stripped duplicates, smoothing, and
score range.

Changes justified mainly by AI trends, enterprise datasets, or generic model
extensibility should be rejected.

## Core Invariants

The project is defined by one in-memory reference profile, byte n-grams of size
1 through 8, minimal ASCII normalization, fixed capacities, historical
raw-grouping compatibility, heuristic bounded scoring, one-shot local
composition, and no external model infrastructure.
