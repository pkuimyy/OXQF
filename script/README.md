# Development scripts

This directory contains deterministic Node.js tooling used during development and testing. It is not part of the runtime dependency set of `oxq-core`, `oxq-convert`, or `oxq-cli`.

Run the local checks with:

```bash
npm ci
npm test
```

Future OXQ anchor-vector generators belong here and must not call the production `oxq-core` writer.

`independent-oxq-reader.mjs` is a separate acceptance implementation derived
directly from `spec/oxq-v1.md`. It must not import `oxq-core` or the anchor-vector
generator. Its tests compare independently decoded semantics with
`test/vectors/oxq-v1/manifest.json` so matching Reader/Writer mistakes cannot
pass only through byte round trips.
