# OXQF 1.0.0 release guide

OXQF publishes one complete developer distribution for each supported platform and
toolchain. The same repository-owned packaging implementation is used locally,
in CI, and by the tag-driven GitHub Release workflow.

## Supported binary baseline

- Linux: GCC on Ubuntu 22.04 x86-64, C++20, glibc 2.35 or newer.
- Windows: MSVC/Visual Studio 2022 on Windows Server 2022 x86-64, C++20, with
  the static MSVC runtime.
- The installed `OXQF` CMake package supplies the matching static runtime as a
  default to MSVC consumer targets created after `find_package(OXQF)`, unless a
  consuming project explicitly chose a runtime first.
- Packaging requires Node.js 24.x and CMake 3.23 or newer. Installed products
  do not require Node.js.

## One distribution per platform

Release assets are deliberately limited to:

```text
oxq-1.0.0-linux-gcc-x86_64.tar.gz
oxq-1.0.0-windows-msvc2022-x86_64.zip
SHA256SUMS
```

Each archive contains the `oxq` CLI, `oxq-core` and `oxq-convert` static
libraries, public headers, the relocatable `OXQF` CMake package, documentation,
specifications, project-authored test vectors, and an internal manifest. There
is no separate CLI or SDK archive.

```text
oxq-1.0.0-linux-gcc-x86_64/
├── bin/oxq
├── include/oxq/
├── lib/
│   └── cmake/OXQF/
├── share/oxq/
│   ├── doc/
│   ├── spec/
│   ├── test-vectors/
│   └── manifest.json
├── README.md
├── CHANGELOG.md
└── LICENSE
```

The Windows archive has the same layout, with `bin/oxq.exe` and `.lib`
libraries. `share/oxq/manifest.json` records the distribution version,
toolchain, included components, and runtime contract. It never contains the
hash of its enclosing archive.

## Local packaging

Run from a clean checkout:

```bash
npm ci
npm run release:check
npm run release:package
ls out/release/
```

The scripts check CMake/npm version agreement, configure and build Release,
run the complete CTest suite including the installed CMake consumer, generate
the canonical Writer fingerprint, install all four internal components into one
staging tree, write the internal manifest, and audit the result. The archive
audit requires the CLI, headers, libraries, CMake package, docs, specs, vectors,
and root license files. It rejects source control, CI, build, dependency,
temporary, private-corpus, credential, and Xiangqi Bridge distribution paths.

`release:check` stops after the staging audit. `release:package` additionally
writes one archive and a platform build manifest to `out/release/`. The platform
manifest is CI evidence, not a GitHub Release asset.

## Publication and verification

`.github/workflows/release.yml` accepts only an existing `vMAJOR.MINOR.PATCH`
tag whose version matches both `PROJECT_VERSION` and `package.json`. It builds
and tests the complete distribution on Ubuntu 22.04 and Windows 2022, attests
both archives, verifies archive hashes and cross-platform Writer fingerprints,
then produces `SHA256SUMS` for exactly the two user-visible archives.

The platform manifests and a combined manifest remain CI-internal data. GitHub
automatically provides source ZIP and tar.gz downloads for the tag; they remain
secondary to the two explicit binary distributions.

Maintainer sequence:

```bash
git status --short
git tag -s v1.0.0 -m "OXQF 1.0.0"
git push origin v1.0.0
```

If signed tags are unavailable, create an annotated tag only after explicitly
accepting the reduced provenance. The workflow neither creates tags nor replaces
an existing release.

After download, verify an archive with:

```bash
sha256sum --check SHA256SUMS
gh attestation verify oxq-1.0.0-linux-gcc-x86_64.tar.gz --repo pkuimyy/OXQF
```
