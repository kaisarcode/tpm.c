# CHANGELOG

## v1.1.0

- Added data-driven configuration lifecycle through `kc_tpm_options_t`.
- Added `kc_tpm_options_default()`, `kc_tpm_options_load_env()`, and `kc_tpm_options_free()` to the public API.
- Refactored `kc_tpm_open()` to take `kc_tpm_options_t` and return error codes.
- Changed `kc_tpm_close()` to return `int` for consistent error reporting.
- Added signal listener lifecycle: `kc_tpm_on_signal()`, `kc_tpm_raise_signal()`, `kc_tpm_listen_signals()`, `kc_tpm_listen_signal()`, and `kc_tpm_signal_listener()`.

## v1.0.0

- Published the stable baseline release.
- Provided n-gram text profile matching from stdin or positional input.
- Supported configurable n-gram sizes through the CLI and public C API.
