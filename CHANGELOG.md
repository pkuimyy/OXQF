# Changelog

## Unreleased

- Introduce the `oxq-format` library name, `OXQF::format` CMake target,
  `oxq/format/*` headers, and the `oxq::format` source namespace facade.
- Keep `OXQF::core` and `oxq/core/*` as compatibility entry points backed by
  the same `oxq-format` binary until the next major version.
- Add build-tree and installed-package smoke coverage for both the new format
  API and the legacy core API.

## 1.0.0 — 2026-09-04

- Freeze and publish the independently implementable OXQ v1.0 specification and test vectors.
- Add the C++20 `oxq-core` model, Reader, Writer, Validator, deterministic serialization, structured failures, and resource limits.
- Add the `oxq-convert` CBL v3 Reader and new-file Writer with structured loss reporting and strict mode.
- Add the `oxq` CLI with `convert`, `inspect`, `validate`, and `dump` commands.
- Add independent-reader, malformed-input, sanitizer, bounded-fuzz, install-consumer, Linux GCC/Clang, Windows MSVC, and cross-platform Writer-byte gates.
- Record successful CBL Writer interoperability with Xiangqi Bridge 3.0 beta4 (program version 3.0.0.4).
- Automate binary-first CLI and compiler-labelled SDK archives, release manifests, checksums, provenance attestations, and tag-driven GitHub Releases.
- Replace separate CLI and SDK release archives with one complete developer distribution per supported platform and toolchain.
- Place a self-describing distribution manifest inside each archive and retain platform manifests only as CI evidence.
