# CLI tests

This directory contains the command contract, JSON, state-validation, failure,
and bidirectional end-to-end tests for `oxq`. The suite exercises the CLI
dispatch layer directly, parses emitted JSON independently with Node.js, and
checks that the command implementation does not include private codec/CBL
layout headers or known physical-layout constants.

The package consumer smoke test separately installs `oxq`, prepends the staged
`bin` directory to `PATH`, and executes `oxq --version` outside the source tree.
