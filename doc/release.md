# OXQF 1.0.0 release guide

OXQF 1.0.0 is the MVP release baseline. A release candidate is accepted only from a clean checkout whose GitHub Actions `linux`, `windows`, and `release-evidence` jobs all pass for the same commit.

## Supported build baseline

- Linux: GCC and Clang, C++20, CMake 3.23 or newer, Ninja.
- Windows: MSVC/Visual Studio 2022 on the GitHub-hosted `windows-2022` image, C++20 and CMake.
- Development scripts: Node.js 24.x. Installed libraries and `oxq` do not require Node.js.

## Reproduce a release candidate

From a fresh checkout:

```bash
npm ci
npm test
cmake --preset release
cmake --build --preset release
ctest --preset release
cmake --build --preset release --target oxq_writer_fingerprint package
```

On Windows, use the explicit Visual Studio generator presets so the compiler baseline cannot be changed by `PATH` ordering:

```powershell
npm ci
npm test
cmake --preset windows-msvc
cmake --build --preset windows-msvc-release
ctest --preset windows-msvc-release
cmake --build --preset windows-msvc-release --target oxq_writer_fingerprint package
```

Linux produces `OXQF-1.0.0-Linux-<architecture>.tar.gz` and Windows produces `OXQF-1.0.0-Windows-<architecture>.zip`. CPack writes a sibling `.sha256` file. CI retains both archives and compares `writer-fingerprint.txt` across Linux and Windows. Each fingerprint is calculated directly over the canonical OXQ byte arrays generated from the same 12 CBL baselines, with an eight-byte little-endian length before each game; the generator also requires the complete diagnostic semantic snapshot to match the committed baseline.

Run the bounded fuzz regression on Linux with:

```bash
cmake --preset fuzz
cmake --build --preset fuzz --target oxq_core_reader_fuzz
ctest --test-dir build/fuzz -R core.reader-fuzz-smoke --output-on-failure
```

## Archive contents

The archive is generated solely from CMake install rules and contains:

- `oxq`, `oxq-core`, and `oxq-convert`;
- public C++ headers and the `OXQF` CMake package;
- the OXQ v1.0 and CBL adapter specifications;
- the CLI contract, changelog, license, and compatibility record;
- hand-authored OXQ vectors and project-authored CBL golden baselines.

The install-consumer test verifies the public headers, both libraries, CMake package, executable, documentation, and vectors in an isolated prefix. It rejects private paths and assets such as `.codex`, `raw`, secrets, and the Xiangqi Bridge distribution archive. Third-party corpus files are not installed or packaged.

## Publication checklist

1. Require a clean working tree and a successful CI run for the exact commit.
2. Download both release archives and their CPack-generated SHA-256 files.
3. Confirm `release-evidence` compared the Linux and Windows Writer fingerprints successfully.
4. Confirm `oxq --version` prints `oxq 1.0.0` from each extracted archive.
5. Confirm the independent consumer builds against each extracted archive using `find_package(OXQF CONFIG REQUIRED)`.
6. Publish the archives, checksums, `CHANGELOG.md`, and a link to `doc/mvp-acceptance.md` together.
7. Create a signed `v1.0.0` tag only after the artifacts above are approved. Tagging and creating a hosted release are intentionally separate maintainer actions.
