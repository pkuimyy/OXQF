import assert from "node:assert/strict";
import test from "node:test";

import {
  checkNodeVersion,
  requiredNodeMajor,
} from "../check-environment.mjs";

test("the current Node.js version satisfies the development baseline", () => {
  const result = checkNodeVersion();
  assert.equal(result.ok, true, result.message);
});

test("a different major version is rejected", () => {
  const result = checkNodeVersion(`${requiredNodeMajor - 1}.0.0`);
  assert.equal(result.ok, false);
  assert.match(result.message, /is required/);
});
