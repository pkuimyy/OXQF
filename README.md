# Open Xiangqi Format

OXQF is the reference implementation workspace for the OXQ single-game format. The MVP currently contains three product boundaries:

- `oxq-core`: the format-independent game model and OXQ codec library;
- `oxq-convert`: external-format adapters and conversion reports;
- `oxq-cli`: the thin `oxq` command-line application.

The source tree implements the frozen OXQ v1.0 codec, CBL v3 Reader/Writer, and the `oxq` command-line application. The format specification is published at [spec/oxq-v1.md](spec/oxq-v1.md), and the stable CLI contract is documented at [doc/cli.md](doc/cli.md).

## Development prerequisites

- CMake 3.23 or newer;
- a C++20 compiler (GCC, Clang, or MSVC);
- Ninja;
- Node.js 24.x and npm.

On the supported WSL/Ubuntu development environment, install the native tools from the Ubuntu repositories:

```bash
sudo apt-get update
sudo apt-get install --yes build-essential cmake ninja-build clang
```

Node.js is used only for development and test scripts. It is not a runtime dependency of any C++ product.

## Build and test

```bash
npm ci
npm test
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

To validate the sanitizer build:

```bash
cmake --preset clang-sanitize
cmake --build --preset clang-sanitize
ctest --preset clang-sanitize
```

Run the complete WSL quality gate, including GCC and Clang builds, with:

```bash
npm run ci
```

The CTest suite installs the project into an isolated staging prefix and builds a separate consumer with `find_package(OXQF CONFIG REQUIRED)`.

## Install

```bash
cmake --install build/dev --prefix build/install
build/install/bin/oxq --version
```

## Command-line examples

```bash
# Split one CBL library into UUID-named OXQ files.
oxq convert source.CBL --output converted/

# Combine ordered OXQ inputs into one new CBL library.
oxq convert game-1.oxq game-2.oxq --output exported.CBL

# Inspect, validate, or emit canonical diagnostic JSON.
oxq inspect --json source.CBL
oxq validate --json game-1.oxq
oxq dump game-1.oxq
```

`oxq convert` never overwrites an existing target. Add `--strict` to reject
any conversion with semantic loss. See the CLI contract for exit codes,
machine-readable output fields, and batch behavior.

## License

OXQF is distributed under the [MIT License](LICENSE).
