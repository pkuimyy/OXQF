# Development scripts

This directory contains deterministic Node.js tooling used during development and testing. It is not part of the runtime dependency set of `oxq-core`, `oxq-convert`, or `oxq-cli`.

Run the local checks with:

```bash
npm ci
npm test
```

Future OXQ anchor-vector generators belong here and must not call the production `oxq-core` writer.
