import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import test from "node:test";

import { assembleRelease, equalFingerprints } from "../assemble-release.mjs";
import {
  auditReleaseFiles,
  distributionManifest,
  distributionName,
  parseArguments,
  parseFingerprint,
  projectVersion,
  releasePlatform,
} from "../package-release.mjs";

test("release version is read from the CMake project", () => {
  assert.equal(projectVersion("project(\n OXQF\n VERSION 1.2.3\n)"), "1.2.3");
  assert.throws(() => projectVersion("project(other VERSION 1.2.3)"));
});

test("release platform names are stable", () => {
  assert.deepEqual(releasePlatform("linux", "x64"), {
    name: "linux",
    arch: "x86_64",
    compiler: "gcc",
    extension: "tar.gz",
  });
  assert.deepEqual(releasePlatform("win32", "x64"), {
    name: "windows",
    arch: "x86_64",
    compiler: "msvc2022",
    extension: "zip",
  });
  assert.throws(() => releasePlatform("win32", "arm64"), /unsupported Windows/);
  assert.throws(() => releasePlatform("darwin", "x64"), /unsupported/);
});

test("one complete developer distribution is named per platform", () => {
  assert.equal(
    distributionName("1.0.0", releasePlatform("linux", "x64")),
    "oxq-1.0.0-linux-gcc-x86_64",
  );
  assert.equal(
    distributionName("1.0.0", releasePlatform("win32", "x64")),
    "oxq-1.0.0-windows-msvc2022-x86_64",
  );
});

test("release command arguments reject ambiguous input", () => {
  assert.deepEqual(parseArguments(["--check", "--allow-dirty", "--expected-version", "1.0.0"]), {
    check: true,
    allowDirty: true,
    expectedVersion: "1.0.0",
  });
  assert.throws(() => parseArguments(["--expected-version"]), /requires a value/);
  assert.throws(() => parseArguments(["--unknown"]), /unknown argument/);
});

test("Writer fingerprints are normalized across line-ending conventions", () => {
  assert.deepEqual(
    parseFingerprint(`sha256=${"a".repeat(64)}\r\nframed_bytes=42\r\n`),
    { sha256: "a".repeat(64), framed_bytes: "42" },
  );
});

const distributionFiles = [
  "README.md",
  "CHANGELOG.md",
  "LICENSE",
  "share/oxq/manifest.json",
  "share/oxq/doc/cli.md",
  "share/oxq/spec/oxq-v1.md",
  "share/oxq/spec/cbl-adapter-v1.md",
  "share/oxq/test-vectors/oxq-v1/manifest.json",
  "share/oxq/test-vectors/cbl-v3/SHA256SUMS",
  "include/oxq/core/writer.hpp",
  "include/oxq/convert/cbl_writer.hpp",
  "lib/cmake/OXQF/OXQFConfig.cmake",
];

test("complete developer distribution contract is audited", () => {
  const linux = [
    ...distributionFiles,
    "bin/oxq",
    "lib/liboxq-core.a",
    "lib/liboxq-convert.a",
  ];
  assert.doesNotThrow(() => auditReleaseFiles(linux, false));
  assert.doesNotThrow(() => auditReleaseFiles([
    ...distributionFiles,
    "bin/oxq.exe",
    "lib/oxq-core.lib",
    "lib/oxq-convert.lib",
  ], true));
  assert.throws(() => auditReleaseFiles(linux.filter((file) => file !== "bin/oxq"), false), /bin\/oxq/);
  assert.throws(() => auditReleaseFiles([...linux, "raw/private.CBL"], false), /forbidden/);
});

test("internal distribution manifest records ABI and runtime contracts", () => {
  assert.deepEqual(distributionManifest("1.0.0", releasePlatform("linux", "x64")), {
    name: "OXQF",
    version: "1.0.0",
    format_version: "1",
    platform: { os: "linux", arch: "x86_64", toolchain: "gcc" },
    components: ["oxq-cli", "oxq-core", "oxq-convert", "cmake-package", "test-vectors", "documentation"],
    runtime: { cpp_standard: "C++20", glibc_min: "2.35" },
  });
  assert.equal(
    distributionManifest("1.0.0", releasePlatform("win32", "x64")).platform.msvc_runtime,
    "static",
  );
});

test("release assembly requires identical binary Writer evidence", () => {
  assert.equal(equalFingerprints(
    { sha256: "abc", framed_bytes: "42" },
    { sha256: "abc", framed_bytes: "42" },
  ), true);
  assert.equal(equalFingerprints(
    { sha256: "abc", framed_bytes: "42" },
    { sha256: "def", framed_bytes: "42" },
  ), false);
});

test("release assembly verifies assets and writes combined evidence", async (context) => {
  const directory = await mkdtemp(path.join(os.tmpdir(), "oxqf-release-test-"));
  context.after(() => rm(directory, { recursive: true, force: true }));
  const fingerprint = { sha256: "a".repeat(64), framed_bytes: "42" };
  for (const platform of ["linux", "windows"]) {
    const compiler = platform === "linux" ? "gcc" : "msvc2022";
    const extension = platform === "linux" ? "tar.gz" : "zip";
    const assets = [`oxq-1.0.0-${platform}-${compiler}-x86_64.${extension}`];
    const artifacts = [];
    for (const asset of assets) {
      const contents = Buffer.from(asset);
      await writeFile(path.join(directory, asset), contents);
      artifacts.push({
        name: asset,
        bytes: contents.length,
        sha256: createHash("sha256").update(contents).digest("hex"),
      });
    }
    const manifest = {
      schema: "oxqf-release-manifest-v2",
      version: "1.0.0",
      platform,
      architecture: "x86_64",
      compiler,
      writer_fingerprint: fingerprint,
      artifacts,
    };
    await writeFile(
      path.join(directory, `release-manifest-${platform}-x86_64.json`),
      `${JSON.stringify(manifest)}\n`,
    );
  }

  await assembleRelease(directory, "1.0.0");
  const combined = JSON.parse(await readFile(path.join(directory, "combined-release-manifest.json")));
  const checksums = await readFile(path.join(directory, "SHA256SUMS"), "utf8");
  assert.equal(combined.artifacts.length, 2);
  assert.deepEqual(combined.writer_fingerprint, fingerprint);
  assert.match(checksums, /oxq-1\.0\.0-linux-gcc-x86_64\.tar\.gz/);
  assert.match(checksums, /oxq-1\.0\.0-windows-msvc2022-x86_64\.zip/);
  assert.doesNotMatch(checksums, /manifest/);
});
