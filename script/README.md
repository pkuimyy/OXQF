# Development scripts

This directory contains deterministic Node.js tooling used during development and testing. It is not part of the runtime dependency set of `oxq-core`, `oxq-convert`, or `oxq-cli`.

Run the local checks with:

```bash
npm ci
npm test
```

Release packaging is implemented here rather than in GitHub-specific shell commands:

```bash
npm run release:check
npm run release:package
```

`package-release.mjs` performs a clean Release configure, build, full CTest run,
Writer fingerprint, component installs, and archive-content audit. It writes a
CLI archive, a compiler-labelled SDK archive, a platform manifest, and checksums
to `out/release/`. `assemble-release.mjs` verifies the independently built Linux
and Windows manifests and creates the combined manifest and `SHA256SUMS` used by
the tag-driven GitHub Release workflow.

Future OXQ anchor-vector generators belong here and must not call the production `oxq-core` writer.

`independent-oxq-reader.mjs` is a separate acceptance implementation derived
directly from `spec/oxq-v1.md`. It must not import `oxq-core` or the anchor-vector
generator. Its tests compare independently decoded semantics with
`test/vectors/oxq-v1/manifest.json` so matching Reader/Writer mistakes cannot
pass only through byte round trips.
