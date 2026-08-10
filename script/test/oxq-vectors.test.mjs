import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { readFile } from "node:fs/promises";
import { spawnSync } from "node:child_process";
import path from "node:path";
import test from "node:test";

const directory = path.join(process.cwd(), "test", "vectors", "oxq-v1");
const magic = Buffer.from([0x89, 0x4f, 0x58, 0x51, 0x0d, 0x0a, 0x1a, 0x0a]);

function crc32c(input) {
  let crc = 0xffffffff;
  for (const byte of input) {
    crc ^= byte;
    for (let bit = 0; bit < 8; ++bit) {
      crc = (crc >>> 1) ^ ((crc & 1) === 0 ? 0 : 0x82f63b78);
    }
  }
  return (crc ^ 0xffffffff) >>> 0;
}

function inspectStructure(bytes) {
  assert(bytes.subarray(0, 8).equals(magic));
  assert.equal(bytes.readUInt16LE(8), 1);
  assert.equal(bytes.readUInt16LE(10), 0);
  assert.equal(bytes.readUInt32LE(12), 64);
  assert.equal(Number(bytes.readBigUInt64LE(16)), bytes.length);
  assert.equal(Number(bytes.readBigUInt64LE(24)), 64);
  assert.equal(bytes.readUInt32LE(60), crc32c(bytes.subarray(0, 60)));

  const count = bytes.readUInt32LE(32);
  const table = bytes.subarray(64, 64 + count * 40);
  assert.equal(bytes.readUInt32LE(56), crc32c(table));
  const types = [];
  let previousEnd = 64 + count * 40;
  for (let index = 0; index < count; ++index) {
    const entry = 64 + index * 40;
    const type = bytes.readUInt32LE(entry);
    const flags = bytes.readUInt32LE(entry + 4);
    const offset = Number(bytes.readBigUInt64LE(entry + 8));
    const storedSize = Number(bytes.readBigUInt64LE(entry + 16));
    const logicalSize = Number(bytes.readBigUInt64LE(entry + 24));
    assert(type > (types.at(-1) ?? 0));
    assert.equal(offset % 8, 0);
    assert(offset >= previousEnd);
    assert(offset + storedSize <= bytes.length);
    assert.equal(storedSize, logicalSize);
    assert.equal(bytes.readUInt32LE(entry + 32), crc32c(bytes.subarray(offset, offset + storedSize)));
    assert.equal(bytes.readUInt32LE(entry + 36), 0);
    assert.equal(type <= 5 ? flags : flags & 1, type <= 5 ? 1 : 0);
    types.push(type);
    previousEnd = offset + storedSize;
  }
  assert.deepEqual(types.slice(0, 5), [1, 2, 3, 4, 5]);
}

test("hand-authored OXQ vectors are reproducible and structurally self-consistent", async () => {
  const result = spawnSync(process.execPath, ["script/build-oxq-vectors.mjs"], {
    cwd: process.cwd(),
    encoding: "utf8",
  });
  assert.equal(result.status, 0, result.stderr);

  const manifest = JSON.parse(await readFile(path.join(directory, "manifest.json"), "utf8"));
  assert.deepEqual(
    manifest.vectors.map((vector) => vector.name),
    [
      "minimal.oxq",
      "variation-zh.oxq",
      "unknown-noncritical.oxq",
      "invalid-magic-high-bit.oxq",
      "invalid-magic-crlf.oxq",
      "invalid-magic-eof.oxq",
    ],
  );

  const files = new Map();
  for (const vector of manifest.vectors) {
    const bytes = await readFile(path.join(directory, vector.name));
    files.set(vector.name, bytes);
    assert.equal(bytes.length, vector.size);
    assert.equal(createHash("sha256").update(bytes).digest("hex"), vector.sha256);
    if (vector.expected.valid) {
      inspectStructure(bytes);
    }
  }

  const minimal = files.get("minimal.oxq");
  for (const vector of manifest.vectors.filter((item) => !item.expected.valid)) {
    const bytes = files.get(vector.name);
    assert(!bytes.subarray(0, 8).equals(magic));
    assert.equal(bytes.readUInt32LE(60), crc32c(bytes.subarray(0, 60)));
    const normalized = Buffer.from(bytes);
    normalized[vector.expected.faultOffset] = minimal[vector.expected.faultOffset];
    normalized.writeUInt32LE(minimal.readUInt32LE(60), 60);
    assert(normalized.equals(minimal), `${vector.name} must contain exactly one Magic fault`);
  }
});
