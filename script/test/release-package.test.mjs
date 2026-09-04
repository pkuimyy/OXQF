import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import test from "node:test";

import { assembleRelease, equalFingerprints } from "../assemble-release.mjs";
import {
  auditReleaseFiles,
  parseArguments,
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

test("release command arguments reject ambiguous input", () => {
  assert.deepEqual(parseArguments(["--check", "--allow-dirty", "--expected-version", "1.0.0"]), {
    check: true,
    allowDirty: true,
    expectedVersion: "1.0.0",
  });
  assert.throws(() => parseArguments(["--expected-version"]), /requires a value/);
  assert.throws(() => parseArguments(["--unknown"]), /unknown argument/);
});

const documentation = [
  "share/doc/OXQF/LICENSE",
  "share/doc/OXQF/README.md",
  "share/doc/OXQF/cli.md",
];

test("CLI and SDK archive contracts are independently audited", () => {
  assert.doesNotThrow(() => auditReleaseFiles("cli", [...documentation, "bin/oxq"], false));
  assert.doesNotThrow(() => auditReleaseFiles("cli", [...documentation, "bin/oxq.exe"], true));
  const sdk = [
    ...documentation,
    "include/oxq/core/writer.hpp",
    "include/oxq/convert/cbl_writer.hpp",
    "lib/cmake/OXQF/OXQFConfig.cmake",
    "lib/liboxq-core.a",
    "lib/liboxq-convert.a",
    "share/OXQF/vectors/oxq-v1/manifest.json",
    "share/OXQF/vectors/cbl-v3/SHA256SUMS",
  ];
  assert.doesNotThrow(() => auditReleaseFiles("sdk", sdk, false));
  assert.throws(() => auditReleaseFiles("cli", [...documentation, "bin/oxq", "lib/a"]), /SDK/);
  assert.throws(() => auditReleaseFiles("sdk", [...sdk, "raw/private.CBL"], false), /forbidden/);
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
    const assets = [
      `oxq-1.0.0-${platform}-x86_64.${extension}`,
      `oxq-sdk-1.0.0-${platform}-${compiler}-x86_64.${extension}`,
    ];
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
      schema: "oxqf-release-manifest-v1",
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
    await writeFile(path.join(directory, `SHA256SUMS-${platform}-x86_64`), "superseded\n");
  }

  await assembleRelease(directory, "1.0.0");
  const combined = JSON.parse(await readFile(path.join(directory, "release-manifest.json")));
  const checksums = await readFile(path.join(directory, "SHA256SUMS"), "utf8");
  assert.equal(combined.artifacts.length, 4);
  assert.deepEqual(combined.writer_fingerprint, fingerprint);
  assert.match(checksums, /release-manifest\.json/);
  assert.doesNotMatch(checksums, /SHA256SUMS-/);
});
