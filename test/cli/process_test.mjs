import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join } from "node:path";

const [jsonDirectory, sourceDirectory] = process.argv.slice(2);

function readJson(name) {
  return JSON.parse(readFileSync(join(jsonDirectory, name), "utf8"));
}

const inspectJson = readJson("inspect-oxq.json");
assert.equal(inspectJson.format, "oxq");
assert.equal(inspectJson.nodes, 4);
assert.equal(inspectJson.reader.canonical, true);

const dumpJson = readJson("dump.json");
assert.equal(dumpJson.uuid, "01980000-0000-7000-8000-000000000001");
assert.equal(dumpJson.metadata.title, null);
assert.equal(dumpJson.move_tree.nodes.length, 1);

assert.equal(readJson("inspect-cbl.json").games, 2);
assert.equal(readJson("validate-valid.json").valid, true);
assert.equal(readJson("validate-invalid.json").codec_error.code, "invalid_magic");
assert.deepEqual(readJson("convert-summary.json"), {
  succeeded: 2,
  skipped: 0,
  warnings: 0,
  losses: 0,
  failed: 0,
});

const cliSource = ["cli.cpp", "json.cpp", "names.cpp"]
  .map((name) => readFileSync(join(sourceDirectory, "src", "cli", name), "utf8"))
  .join("\n");
assert.doesNotMatch(cliSource, /#include\s+["<](?:codec|cbl)\//);
assert.doesNotMatch(cliSource, /0x10440|0x8aa|crc32c/i);
