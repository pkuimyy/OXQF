# Open Xiangqi Format

OXQF is the reference implementation workspace for the OXQ single-game format. The MVP currently contains three product boundaries:

- `oxq-core`: the format-independent game model and OXQ codec library;
- `oxq-convert`: external-format adapters and conversion reports;
- `oxq-cli`: the thin `oxq` command-line application.

The current source tree is an engineering skeleton. The frozen OXQ v1.0 RC1 specification is published at [spec/oxq-v1.md](spec/oxq-v1.md); implementation design documents are maintained under `.codex` during development.

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

## License

OXQF is distributed under the [MIT License](LICENSE).
