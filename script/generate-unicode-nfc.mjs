import { createHash } from "node:crypto";
import { readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import process from "node:process";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const unicodeDataInput = process.argv[2];
const exclusionsInput = process.argv[3];
const output = process.argv[4] ??
  path.join(root, "src/core/src/generated/unicode_nfc_data.hpp");
if (unicodeDataInput === undefined || exclusionsInput === undefined) {
  throw new Error(
    "usage: node script/generate-unicode-nfc.mjs " +
      "UnicodeData.txt CompositionExclusions.txt [output.hpp]",
  );
}

const source = await readFile(unicodeDataInput, "utf8");
const exclusionsSource = await readFile(exclusionsInput, "utf8");
const exclusions = new Set(
  exclusionsSource
    .split("\n")
    .map((line) => line.split("#", 1)[0].trim())
    .filter((line) => line !== "")
    .map((value) => Number.parseInt(value, 16)),
);
const combining = [];
const combiningByCodePoint = new Map();
const decompositions = [];
const composition = [];
const decompositionData = [];
const records = source.trimEnd().split("\n").map((line) =>
  line.replace(/\r$/, "").split(";"),
);

for (const fields of records) {
  const codePoint = Number.parseInt(fields[0], 16);
  const combiningClass = Number.parseInt(fields[3], 10);
  if (combiningClass !== 0) {
    combining.push([codePoint, combiningClass]);
    combiningByCodePoint.set(codePoint, combiningClass);
  }
}

for (const fields of records) {
  const codePoint = Number.parseInt(fields[0], 16);
  const raw = fields[5];
  if (raw === "" || raw.startsWith("<")) {
    continue;
  }
  const values = raw.split(" ").map((value) => Number.parseInt(value, 16));
  const offset = decompositionData.length;
  decompositionData.push(...values);
  decompositions.push([codePoint, offset, values.length]);
  if (values.length === 2 && !exclusions.has(codePoint) &&
      !combiningByCodePoint.has(values[0])) {
    composition.push([values[0], values[1], codePoint]);
  }
}
composition.sort((left, right) => left[0] - right[0] || left[1] - right[1]);

const hex = (value) => `0x${value.toString(16)}U`;
const rows = (values, render) => values.map((value) => `    ${render(value)},`).join("\n");
const sha256 = createHash("sha256").update(source).digest("hex");
const exclusionsSha256 = createHash("sha256").update(exclusionsSource).digest("hex");
const generated = `// Generated from Unicode 17.0.0 UnicodeData.txt.
// Source SHA-256: ${sha256}
// CompositionExclusions.txt SHA-256: ${exclusionsSha256}
// Regenerate with script/generate-unicode-nfc.mjs; do not edit by hand.
#pragma once

#include <array>
#include <cstdint>

namespace oxq::core::detail::unicode_data {

struct CombiningClassEntry {
  std::uint32_t code_point;
  std::uint8_t value;
};

struct DecompositionEntry {
  std::uint32_t code_point;
  std::uint32_t offset;
  std::uint8_t length;
};

struct CompositionEntry {
  std::uint32_t first;
  std::uint32_t second;
  std::uint32_t composite;
};

inline constexpr std::array<CombiningClassEntry, ${combining.length}> combining_classes{{
${rows(combining, ([codePoint, value]) => `{${hex(codePoint)}, ${value}}`)}
}};

inline constexpr std::array<DecompositionEntry, ${decompositions.length}> decomposition_entries{{
${rows(decompositions, ([codePoint, offset, length]) =>
  `{${hex(codePoint)}, ${offset}U, ${length}}`)}
}};

inline constexpr std::array<std::uint32_t, ${decompositionData.length}> decomposition_data{
${rows(decompositionData, (value) => hex(value))}
};

inline constexpr std::array<CompositionEntry, ${composition.length}> composition_entries{{
${rows(composition, ([first, second, composite]) =>
  `{${hex(first)}, ${hex(second)}, ${hex(composite)}}`)}
}};

}  // namespace oxq::core::detail::unicode_data
`;

await writeFile(output, generated);
console.log(
  `Wrote ${output}: ${combining.length} CCC, ${decompositions.length} decompositions, ` +
    `${composition.length} compositions.`,
);
