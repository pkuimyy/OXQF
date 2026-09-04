# OXQF MVP acceptance evidence

Version: 1.0.0  
Assessment date: 2026-09-04

This report maps every acceptance criterion in the MVP plan to reproducible automation or an explicit manual interoperability record. Relative test names are CTest names unless a command is shown.

| # | Acceptance criterion | Evidence | Result |
| ---: | --- | --- | --- |
| 1 | Controlled CBL samples cover main line, variations, annotations, and custom position | The 12 project-authored files under `test/gold-baseline`, their MIT distribution statement, `semantic-baseline.json`, and `convert.cbl-semantic-baseline` | Pass |
| 2 | `布局飞刀.CBL` is recognized as an empty/header-only library | Local source SHA-256 `b44b88959fc5d3b7bf0a7c8be60a9cd5fd31e741649e032eaa3310e5d7e4c520`; `oxq inspect --json` reports 0 games, 0 allocated resources, and no diagnostics. The third-party file is not distributed. | Pass |
| 3 | Non-empty CBL game counts match Xiangqi Bridge | Counts and per-game truth recorded in `test/gold-baseline/gold-baseline.md`; `convert.cbl-container`, `convert.cbl-reader`, and `convert.cbl-semantic-baseline`. The optional release corpus baseline also records 1,570 libraries and 322,418 live games. | Pass |
| 4 | Every CBL game can become one independently readable OXQ | `convert.cbl-reader`, `convert.cbl-semantic-baseline`, and `cli.contract-and-e2e` | Pass |
| 5 | Metadata, initial position, move tree, variations, and annotations have no silent loss | Machine-readable full semantic snapshot plus structured diagnostics in `convert.cbl-semantic-baseline`, `convert.cbl-reader`, and conversion-report tests | Pass |
| 6 | `GameModel -> OXQ -> GameModel` is semantically identical | `core.writer`, `core.reader`, codec component tests, and `core.game-model` | Pass |
| 7 | Unmapped CBL fields are reported and strict mode rejects loss | CBL Reader report tests and `cli.contract-and-e2e` strict conversion cases | Pass |
| 8 | Linux and Windows read the same vectors and produce deterministic bytes | Linux GCC/Clang and the pinned `windows-2022` image with an explicit Visual Studio 2022 generator for MSVC run the same suite. Both hash the same length-framed canonical OXQ Writer byte arrays into `writer-fingerprint.txt`; the dependent `release-evidence` CI job requires exact equality. | Pass |
| 9 | Malformed input returns bounded structured failures without crashes | `core.malformed-regression`, `core.deep-malformed`, CBL limit/preflight tests, ASan/UBSan suite, and bounded `core.reader-fuzz-smoke` | Pass |
| 10 | A third party can implement an independent Reader from the specification | `script/independent-oxq-reader.mjs` shares no codec code; its tests verify complete semantics and registered hashes for every public OXQ vector | Pass |
| 11 | OXQ exports new Xiangqi Bridge-compatible CBL preserving expressible semantics | Four deterministic Writer vectors and the completed manual record in `test/writer-compatibility/README.md`; all opened successfully in Xiangqi Bridge 3.0 beta4 (program version 3.0.0.4), which identifies one Windows executable | Pass |
| 12 | OXQ semantics unavailable in CBL are reported; strict mode cannot succeed with loss | CBL Writer preflight, limits, Writer, and CLI strict-mode tests | Pass |

## Release-level gates

- The CI release build starts from `actions/checkout`, builds Release archives independently on Linux and Windows, and runs the complete test suite before upload.
- `package.consumer-smoke` installs to an empty staging prefix, validates the release layout, places the staged executable on `PATH`, and builds a separate consumer using only the installed CMake package.
- `oxq_writer_fingerprint` requires the regenerated semantic baseline to equal the committed snapshot, then hashes the public Writer's length-framed OXQ byte arrays without passing binary evidence through a platform text representation.
- The Linux fuzz smoke test is duration- and timeout-bounded; any crash makes CI fail and leaves its artifact directory available for local minimization before a regression vector is committed.
- CPack archives only installed project files. The package layout check excludes private research material, third-party binaries and corpora, credentials, build trees, and temporary products.

All twelve MVP criteria and all M7 exit conditions have evidence. OXQF 1.0.0 is therefore eligible to be treated as the MVP release baseline after the exact release commit passes the three CI jobs.
