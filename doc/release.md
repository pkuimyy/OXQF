# OXQF 1.0.0 release guide

OXQF uses one repository-owned packaging implementation for local builds and GitHub Releases. GitHub Actions invokes the same Node.js and CMake commands; it does not maintain a second archive layout.

## Supported binary baseline

- Linux CLI and SDK: GCC on Ubuntu 22.04 x86-64, C++20, glibc 2.35 or newer.
- Windows CLI and SDK: MSVC/Visual Studio 2022 on Windows Server 2022 x86-64, C++20.
- The Windows Release build uses the static MSVC runtime so the CLI does not require a separately installed Visual C++ Redistributable. The SDK archive is consequently labelled `msvc2022` and uses the same runtime choice.
- Packaging scripts require Node.js 24.x and CMake 3.23 or newer. Installed products do not require Node.js.

## Local packaging

Run from a clean checkout:

```bash
npm ci
npm run release:check
npm run release:package
```

Both commands start with a fresh `build/release-package` tree and perform:

1. CMake and npm version agreement checks;
2. a Release configure and complete build;
3. all CTest tests, including the isolated installed consumer;
4. canonical OXQ Writer fingerprint generation;
5. separate CMake component installs for the CLI and SDK;
6. required-file and forbidden-private-path audits.

`release:check` stops after auditing both staging trees. `release:package` additionally recreates `out/release/` and writes the two platform archives, a machine-readable manifest, and a platform SHA-256 file. A dirty Git working tree is rejected; `--allow-dirty` exists only for developing the packaging code itself.

Linux output:

```text
oxq-1.0.0-linux-x86_64.tar.gz
oxq-sdk-1.0.0-linux-gcc-x86_64.tar.gz
release-manifest-linux-x86_64.json
SHA256SUMS-linux-x86_64
```

Windows output:

```text
oxq-1.0.0-windows-x86_64.zip
oxq-sdk-1.0.0-windows-msvc2022-x86_64.zip
release-manifest-windows-x86_64.json
SHA256SUMS-windows-x86_64
```

## Archive boundaries

The CLI archive contains only the `oxq` executable, README, changelog, license, specifications, CLI contract, acceptance report, and compatibility record.

The SDK archive contains `oxq-core`, `oxq-convert`, public C++ headers, the relocatable `OXQF` CMake package, the same documentation, and project-authored OXQ/CBL test vectors. It intentionally does not contain the CLI executable.

Both archive audits reject private paths and assets such as `.codex`, `raw`, build trees, dependency trees, credentials, and the Xiangqi Bridge distribution archive. Third-party corpus files are never installed or packaged.

## GitHub Release publication

`.github/workflows/release.yml` accepts only an existing `vMAJOR.MINOR.PATCH` tag. The tag version must exactly match `PROJECT_VERSION` and `package.json`.

For each tag, the workflow:

1. checks out the exact tag independently on Ubuntu 22.04 and Windows 2022;
2. runs `npm run release:package` on both platforms;
3. creates GitHub build-provenance attestations for all four binary archives;
4. verifies archive hashes, versions, and cross-platform Writer fingerprints;
5. creates a combined `release-manifest.json` and `SHA256SUMS`;
6. publishes one GitHub Release containing four binary archives, both platform manifests, the combined manifest, and checksums.

GitHub automatically exposes source ZIP and tar.gz downloads for the tag; they remain secondary to the explicit CLI and SDK assets. The workflow refuses to replace an existing release and never creates a tag implicitly.

Maintainer sequence:

```bash
git status --short
git tag -s v1.0.0 -m "OXQF 1.0.0"
git push origin v1.0.0
```

If signed tags are not configured, use an annotated tag only after explicitly accepting that reduced provenance. The workflow may also be manually dispatched for an existing tag; it still verifies that tag and refuses a version mismatch.

After download, verify an archive with:

```bash
sha256sum --check SHA256SUMS
gh attestation verify oxq-1.0.0-linux-x86_64.tar.gz --repo pkuimyy/OXQF
```

## Publication checklist

1. Require M7 CI success for the exact commit.
2. Confirm the version appears in `CMakeLists.txt`, `package.json`, and `CHANGELOG.md`.
3. Create and push a signed version tag.
4. Require both release build jobs and the publish job to pass.
5. Download the CLI archives and run `oxq --version` on both platforms.
6. Verify `SHA256SUMS` and the GitHub attestations.
7. Enable GitHub immutable releases in repository settings when operationally appropriate.
