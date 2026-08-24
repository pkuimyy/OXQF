import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { readFile } from "node:fs/promises";
import path from "node:path";
import test from "node:test";

import {
  IndependentOxqError,
  crc32c,
  readIndependentOxq,
} from "../independent-oxq-reader.mjs";

const vectorDirectory = path.join(process.cwd(), "test", "vectors", "oxq-v1");
const manifest = JSON.parse(
  await readFile(path.join(vectorDirectory, "manifest.json"), "utf8"),
);

test("independent CRC-32C matches the specification check value", () => {
  assert.equal(crc32c(Buffer.from("123456789")), 0xe3069283);
});

for (const vector of manifest.vectors) {
  test(`independent Reader verifies ${vector.name}`, async () => {
    const bytes = await readFile(path.join(vectorDirectory, vector.name));
    assert.equal(bytes.length, vector.size);
    assert.equal(createHash("sha256").update(bytes).digest("hex"), vector.sha256);
    if (vector.expected.valid) {
      assert.deepEqual(readIndependentOxq(bytes), vector.expected);
      return;
    }
    assert.throws(
      () => readIndependentOxq(bytes),
      (error) => error instanceof IndependentOxqError &&
        error.code === vector.expected.error &&
        error.offset === vector.expected.faultOffset,
    );
  });
}

test("independent semantic comparison is sensitive to meaning and ordering", async () => {
  const vector = manifest.vectors.find(({ name }) => name === "variation-zh.oxq");
  const bytes = await readFile(path.join(vectorDirectory, vector.name));
  const actual = readIndependentOxq(bytes);
  assert.equal(actual.metadata.title, "对局示例");
  assert.deepEqual(actual.position.pieces[0], ["red-king", 4]);
  assert.deepEqual(actual.tree[0].children, [1, 3]);

  const wrongMeaning = structuredClone(vector.expected);
  wrongMeaning.position.pieces[0][0] = "black-king";
  wrongMeaning.tree[0].children.reverse();
  assert.notDeepEqual(actual, wrongMeaning);
});

test("independent Reader reports bounded truncation instead of a host exception", async () => {
  const bytes = await readFile(path.join(vectorDirectory, "minimal.oxq"));
  assert.throws(
    () => readIndependentOxq(bytes.subarray(0, 63)),
    (error) => error instanceof IndependentOxqError && error.code === "SIZE_MISMATCH",
  );
});
