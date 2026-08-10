import { createHash } from "node:crypto";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import path from "node:path";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const outputDirectory = path.join(root, "test", "vectors", "oxq-v1");
const writeMode = process.argv.includes("--write");

class Bytes {
  constructor() {
    this.parts = [];
    this.length = 0;
  }

  append(buffer) {
    this.parts.push(buffer);
    this.length += buffer.length;
    return this;
  }

  u8(value) {
    return this.append(Buffer.from([value]));
  }

  u16(value) {
    const result = Buffer.alloc(2);
    result.writeUInt16LE(value);
    return this.append(result);
  }

  u32(value) {
    const result = Buffer.alloc(4);
    result.writeUInt32LE(value);
    return this.append(result);
  }

  u64(value) {
    const result = Buffer.alloc(8);
    result.writeBigUInt64LE(BigInt(value));
    return this.append(result);
  }

  pad(alignment) {
    const size = (alignment - (this.length % alignment)) % alignment;
    return this.append(Buffer.alloc(size));
  }

  build() {
    return Buffer.concat(this.parts, this.length);
  }
}

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

function uuidBytes(text) {
  return Buffer.from(text.replaceAll("-", ""), "hex");
}

function stringPool(values) {
  const unique = [...new Set(values)].sort((left, right) =>
    Buffer.compare(Buffer.from(left), Buffer.from(right)),
  );
  const body = new Bytes().u16(1).u16(8).u32(unique.length);
  const references = new Map();
  for (const value of unique) {
    const encoded = Buffer.from(value, "utf8");
    references.set(value, body.length);
    body.u32(encoded.length).append(encoded).pad(4);
  }
  return { payload: body.build(), references };
}

function metadata(fields) {
  const body = new Bytes().u16(1).u16(8).u32(fields.length);
  for (const field of fields) {
    body
      .u16(field.tag)
      .u8(field.valueType)
      .u8(field.flags ?? 0)
      .u32(field.value.length)
      .append(field.value)
      .pad(4);
  }
  return body.build();
}

function position(pieces = []) {
  const body = new Bytes()
    .u16(1)
    .u16(16)
    .u16(1)
    .u8(0)
    .u8(0)
    .u16(1)
    .u16(pieces.length)
    .u32(0);
  for (const piece of [...pieces].sort((left, right) => left.square - right.square)) {
    body.u8(piece.code).u8(piece.square).u16(0);
  }
  return body.build();
}

function moveTree(nodes) {
  const none = 0xffffffff;
  const body = new Bytes().u16(1).u16(16).u16(32).u16(0).u32(nodes.length).u32(0);
  for (const node of nodes) {
    const move = node.move ?? [0xff, 0xff, 0xff, 0xff];
    body
      .u32(node.parent ?? none)
      .u32(node.firstChild ?? none)
      .u32(node.nextSibling ?? none)
      .u32(node.firstAnnotation ?? 0)
      .append(Buffer.from(move))
      .u32(node.ply)
      .u32(0)
      .u32(0);
  }
  return body.build();
}

function annotations(records = []) {
  const body = new Bytes().u16(1).u16(16).u16(24).u16(0).u32(records.length).u32(0);
  for (const record of records) {
    body
      .u32(record.id)
      .u32(record.next ?? 0)
      .u16(record.kind ?? 1)
      .u16(record.flags ?? 0)
      .u32(record.textRef)
      .u32(record.authorRef ?? 0)
      .u32(record.languageRef ?? 0);
  }
  return body.build();
}

function buildOxq({ uuid, sections }) {
  const entrySize = 40;
  const headerSize = 64;
  const sorted = [...sections].sort((left, right) => left.type - right.type);
  let nextOffset = headerSize + sorted.length * entrySize;
  nextOffset += (8 - (nextOffset % 8)) % 8;

  const entries = [];
  for (const section of sorted) {
    entries.push({
      ...section,
      offset: nextOffset,
      crc32c: crc32c(section.payload),
    });
    nextOffset += section.payload.length;
    nextOffset += (8 - (nextOffset % 8)) % 8;
  }

  const table = new Bytes();
  for (const entry of entries) {
    table
      .u32(entry.type)
      .u32(entry.flags ?? 1)
      .u64(entry.offset)
      .u64(entry.payload.length)
      .u64(entry.payload.length)
      .u32(entry.crc32c)
      .u32(0);
  }
  const tableBytes = table.build();

  const header = Buffer.alloc(headerSize);
  Buffer.from([0x89, 0x4f, 0x58, 0x51, 0x0d, 0x0a, 0x1a, 0x0a]).copy(header, 0);
  header.writeUInt16LE(1, 0x08);
  header.writeUInt16LE(0, 0x0a);
  header.writeUInt32LE(headerSize, 0x0c);
  header.writeBigUInt64LE(BigInt(nextOffset), 0x10);
  header.writeBigUInt64LE(BigInt(headerSize), 0x18);
  header.writeUInt32LE(entries.length, 0x20);
  uuidBytes(uuid).copy(header, 0x28);
  header.writeUInt32LE(crc32c(tableBytes), 0x38);
  header.writeUInt32LE(crc32c(header.subarray(0, 0x3c)), 0x3c);

  const file = Buffer.alloc(nextOffset);
  header.copy(file, 0);
  tableBytes.copy(file, headerSize);
  for (const entry of entries) {
    entry.payload.copy(file, entry.offset);
  }

  return {
    file,
    layout: entries.map((entry) => ({
      type: entry.type,
      offset: entry.offset,
      size: entry.payload.length,
      crc32c: entry.crc32c.toString(16).padStart(8, "0"),
    })),
  };
}

function minimalSections(extra = []) {
  return [
    { type: 1, payload: metadata([]) },
    { type: 2, payload: position() },
    {
      type: 3,
      payload: moveTree([{ ply: 0 }]),
    },
    { type: 4, payload: annotations() },
    { type: 5, payload: stringPool([]).payload },
    ...extra,
  ];
}

const minimal = buildOxq({
  uuid: "01980000-0000-7000-8000-000000000001",
  sections: minimalSections(),
});

const text = {
  language: "zh-Hans",
  title: "对局示例",
  comment: "根注释\n第二行",
};
const pool = stringPool(Object.values(text));
const titleValue = Buffer.alloc(4);
titleValue.writeUInt32LE(pool.references.get(text.title));

const variation = buildOxq({
  uuid: "01980000-0000-7000-8000-000000000002",
  sections: [
    { type: 1, payload: metadata([{ tag: 0x0050, valueType: 5, value: titleValue }]) },
    {
      type: 2,
      payload: position([
        { code: 0x01, square: 4 },
        { code: 0x06, square: 19 },
        { code: 0x05, square: 27 },
        { code: 0x85, square: 63 },
        { code: 0x81, square: 85 },
      ]),
    },
    {
      type: 3,
      payload: moveTree([
        { firstChild: 1, firstAnnotation: 1, ply: 0 },
        { parent: 0, firstChild: 2, nextSibling: 3, move: [27, 36, 0, 0], ply: 1 },
        { parent: 1, move: [63, 54, 0, 0], ply: 2 },
        { parent: 0, move: [19, 22, 0, 0], ply: 1 },
      ]),
    },
    {
      type: 4,
      payload: annotations([
        {
          id: 1,
          textRef: pool.references.get(text.comment),
          languageRef: pool.references.get(text.language),
        },
      ]),
    },
    { type: 5, payload: pool.payload },
  ],
});

const unknown = buildOxq({
  uuid: "01980000-0000-7000-8000-000000000003",
  sections: minimalSections([
    { type: 0x80000001, flags: 0, payload: Buffer.from("deadbeef", "hex") },
  ]),
});

function corruptedMagic(source, offset, value) {
  const result = Buffer.from(source);
  result[offset] = value;
  result.writeUInt32LE(crc32c(result.subarray(0, 0x3c)), 0x3c);
  return result;
}

const vectors = [
  {
    name: "minimal.oxq",
    bytes: minimal.file,
    expected: {
      valid: true,
      uuid: "01980000-0000-7000-8000-000000000001",
      metadata: {},
      position: { sideToMove: "red", fullmoveNumber: 1, pieces: [] },
      tree: [{ index: 0, parent: null, move: null, children: [] }],
      annotations: [],
      unknownSections: [],
    },
    layout: minimal.layout,
  },
  {
    name: "variation-zh.oxq",
    bytes: variation.file,
    expected: {
      valid: true,
      uuid: "01980000-0000-7000-8000-000000000002",
      metadata: { title: text.title },
      position: {
        sideToMove: "red",
        fullmoveNumber: 1,
        pieces: [
          ["red-king", 4],
          ["red-cannon", 19],
          ["red-rook", 27],
          ["black-rook", 63],
          ["black-king", 85],
        ],
      },
      tree: [
        { index: 0, parent: null, move: null, children: [1, 3] },
        { index: 1, parent: 0, move: [27, 36], children: [2] },
        { index: 2, parent: 1, move: [63, 54], children: [] },
        { index: 3, parent: 0, move: [19, 22], children: [] },
      ],
      annotations: [{ node: 0, kind: "COMMENT", text: text.comment, language: text.language }],
      unknownSections: [],
    },
    layout: variation.layout,
  },
  {
    name: "unknown-noncritical.oxq",
    bytes: unknown.file,
    expected: {
      valid: true,
      uuid: "01980000-0000-7000-8000-000000000003",
      metadata: {},
      position: { sideToMove: "red", fullmoveNumber: 1, pieces: [] },
      tree: [{ index: 0, parent: null, move: null, children: [] }],
      annotations: [],
      unknownSections: [{ type: 2147483649, critical: false, payloadHex: "deadbeef" }],
    },
    layout: unknown.layout,
  },
  {
    name: "invalid-magic-high-bit.oxq",
    bytes: corruptedMagic(minimal.file, 0, 0x09),
    expected: { valid: false, error: "INVALID_MAGIC", faultOffset: 0 },
  },
  {
    name: "invalid-magic-crlf.oxq",
    bytes: corruptedMagic(minimal.file, 4, 0x0a),
    expected: { valid: false, error: "INVALID_MAGIC", faultOffset: 4 },
  },
  {
    name: "invalid-magic-eof.oxq",
    bytes: corruptedMagic(minimal.file, 6, 0x00),
    expected: { valid: false, error: "INVALID_MAGIC", faultOffset: 6 },
  },
];

const manifest = {
  format: "OXQ",
  version: "1.0",
  provenance: "Hand-authored from spec/oxq-v1.md; never generated by oxq-core.",
  vectors: vectors.map(({ name, bytes, expected, layout }) => ({
    name,
    size: bytes.length,
    sha256: createHash("sha256").update(bytes).digest("hex"),
    expected,
    ...(layout === undefined ? {} : { layout }),
  })),
};
const manifestBytes = Buffer.from(`${JSON.stringify(manifest, null, 2)}\n`);

async function compare(file, expected) {
  let actual;
  try {
    actual = await readFile(file);
  } catch (error) {
    if (error.code === "ENOENT") {
      throw new Error(`Missing generated vector: ${path.relative(root, file)}`);
    }
    throw error;
  }
  if (!actual.equals(expected)) {
    throw new Error(`Generated vector is stale: ${path.relative(root, file)}`);
  }
}

await mkdir(outputDirectory, { recursive: true });
if (writeMode) {
  for (const vector of vectors) {
    await writeFile(path.join(outputDirectory, vector.name), vector.bytes);
  }
  await writeFile(path.join(outputDirectory, "manifest.json"), manifestBytes);
  console.log(`Wrote ${vectors.length} OXQ v1 vectors.`);
} else {
  for (const vector of vectors) {
    await compare(path.join(outputDirectory, vector.name), vector.bytes);
  }
  await compare(path.join(outputDirectory, "manifest.json"), manifestBytes);
  console.log(`Verified ${vectors.length} OXQ v1 vectors.`);
}
