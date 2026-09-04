# `oxq` command-line contract

Status: MVP v1

## Commands

```text
oxq convert [--strict] [--json] --output PATH INPUT...
oxq inspect [--json] INPUT
oxq validate [--json] INPUT
oxq dump INPUT
```

Input format is selected case-insensitively from `.cbl` or `.oxq`. `convert`
accepts either one or more CBL inputs and an output directory, or one or more
OXQ inputs and one new CBL output file. Mixing input formats is a usage error.

CBL games are written as `<game-uuid>.oxq`. A duplicate UUID or an existing
target is skipped and never overwritten. OXQ inputs retain command-line order
inside a generated CBL. Output parents are created as needed. Every new file is
fully written to a sibling temporary file and then renamed into place; failed
writes remove that temporary file. An externally interrupted process can leave
a `.tmp.N` sibling, but the requested target remains unpublished and a later
run selects another temporary name.

Human-readable business output uses stdout and diagnostics use stderr. With
`--json`, stdout contains one stable compact JSON value and no color or progress
control sequences. `dump` is always JSON. JSON output is UTF-8 with object keys
in the order documented by the implementation tests.

## Exit codes

| Code | Meaning |
| ---: | --- |
| 0 | All requested work succeeded |
| 2 | Invalid command or arguments |
| 3 | Input file could not be read or decoded |
| 4 | Output directory/file operation failed or every target was skipped |
| 5 | OXQ validation failed |
| 6 | Strict conversion was rejected because of semantic loss |
| 7 | Batch completed partially; successful independent outputs remain |

`inspect` and `dump` decode their input and return 3 for malformed data.
`validate` returns 5 for either a structural codec error or state-validation
errors. `convert` reports a summary containing `succeeded`, `skipped`,
`warnings`, `losses`, and `failed`; any mixed successful/failed or
successful/skipped batch returns 7. Warning and loss counts are the
corresponding severities from `ConversionReport`, not affected-file counts.
