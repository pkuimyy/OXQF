import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { readdir, readFile } from "node:fs/promises";
import path from "node:path";
import test from "node:test";

const baselineDirectory = path.join("test", "gold-baseline");

test("gold baseline files match SHA256SUMS", async () => {
  const manifest = await readFile(
    path.join(baselineDirectory, "SHA256SUMS"),
    "utf8",
  );
  const entries = manifest.trim().split("\n").map((line) => {
    const match = /^([0-9a-f]{64})  ([^/]+)$/.exec(line);
    assert.ok(match, `invalid SHA256SUMS line: ${line}`);
    return { expectedHash: match[1], fileName: match[2] };
  });

  const directoryEntries = await readdir(baselineDirectory);
  const expectedFiles = directoryEntries
    .filter((name) => /^cbl_.*\.CBL$/.test(name) || name === "GAME_TEST_09.txt")
    .sort();
  const manifestFiles = entries.map(({ fileName }) => fileName).sort();
  assert.deepEqual(manifestFiles, expectedFiles);

  for (const { expectedHash, fileName } of entries) {
    const bytes = await readFile(path.join(baselineDirectory, fileName));
    const actualHash = createHash("sha256").update(bytes).digest("hex");
    assert.equal(actualHash, expectedHash, `${fileName} hash mismatch`);
  }
});
