// Independent OXQ v1 acceptance reader.
// Source of truth: spec/oxq-v1.md. This module must not depend on oxq-core or
// script/build-oxq-vectors.mjs; it exists to detect shared Codec assumptions.

const MAGIC = Buffer.from([0x89, 0x4f, 0x58, 0x51, 0x0d, 0x0a, 0x1a, 0x0a]);
const HEADER_SIZE = 64;
const SECTION_ENTRY_SIZE = 40;
const ABSENT_INDEX = 0xffffffff;
const STANDARD_SECTIONS = new Set([1, 2, 3, 4, 5]);
const utf8Decoder = new TextDecoder("utf-8", { fatal: true, ignoreBOM: true });

const DEFAULT_LIMITS = Object.freeze({
  maxFileSize: 1024 * 1024 * 1024,
  maxSections: 1024,
  maxStrings: 10_000_000,
  maxStringBytes: 16 * 1024 * 1024,
  maxMetadataFields: 65_536,
  maxNodes: 10_000_000,
  maxAnnotations: 10_000_000,
});

export class IndependentOxqError extends Error {
  constructor(code, offset, message, sectionType = undefined) {
    super(message);
    this.name = "IndependentOxqError";
    this.code = code;
    this.offset = offset;
    this.sectionType = sectionType;
  }
}

function fail(code, offset, message, sectionType = undefined) {
  throw new IndependentOxqError(code, offset, message, sectionType);
}

function ensureRange(bytes, offset, size, code = "SIZE_MISMATCH", sectionType = undefined) {
  if (!Number.isSafeInteger(offset) || !Number.isSafeInteger(size) || offset < 0 || size < 0 ||
      offset > bytes.length || size > bytes.length - offset) {
    fail(code, Math.max(0, offset), "byte range is outside input", sectionType);
  }
}

function u8(bytes, offset, sectionType = undefined) {
  ensureRange(bytes, offset, 1, "SIZE_MISMATCH", sectionType);
  return bytes[offset];
}

function u16(bytes, offset, sectionType = undefined) {
  ensureRange(bytes, offset, 2, "SIZE_MISMATCH", sectionType);
  return bytes.readUInt16LE(offset);
}

function u32(bytes, offset, sectionType = undefined) {
  ensureRange(bytes, offset, 4, "SIZE_MISMATCH", sectionType);
  return bytes.readUInt32LE(offset);
}

function i32(bytes, offset, sectionType = undefined) {
  ensureRange(bytes, offset, 4, "SIZE_MISMATCH", sectionType);
  return bytes.readInt32LE(offset);
}

function safeU64(bytes, offset, sectionType = undefined) {
  ensureRange(bytes, offset, 8, "SIZE_MISMATCH", sectionType);
  const value = bytes.readBigUInt64LE(offset);
  if (value > BigInt(Number.MAX_SAFE_INTEGER)) {
    fail("RESOURCE_LIMIT", offset, "u64 exceeds the independent reader safe range", sectionType);
  }
  return Number(value);
}

function requireEqual(actual, expected, code, offset, field, sectionType = undefined) {
  if (actual !== expected) {
    fail(code, offset, `${field}: expected ${expected}, got ${actual}`, sectionType);
  }
}

function requireZero(bytes, begin, end, code, sectionType = undefined) {
  ensureRange(bytes, begin, end - begin, code, sectionType);
  for (let offset = begin; offset < end; ++offset) {
    if (bytes[offset] !== 0) {
      fail(code, offset, "reserved or padding byte must be zero", sectionType);
    }
  }
}

export function crc32c(input) {
  const bytes = Buffer.isBuffer(input) ? input : Buffer.from(input);
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; ++bit) {
      crc = (crc >>> 1) ^ ((crc & 1) === 0 ? 0 : 0x82f63b78);
    }
  }
  return (crc ^ 0xffffffff) >>> 0;
}

function decodeUtf8(bytes, absoluteOffset, sectionType) {
  try {
    return utf8Decoder.decode(bytes);
  } catch {
    fail("INVALID_UTF8", absoluteOffset, "string is not strict UTF-8", sectionType);
  }
}

function formatUuid(bytes) {
  const hex = bytes.toString("hex");
  return `${hex.slice(0, 8)}-${hex.slice(8, 12)}-${hex.slice(12, 16)}-` +
    `${hex.slice(16, 20)}-${hex.slice(20)}`;
}

function parseContainer(bytes, limits) {
  if (bytes.length > limits.maxFileSize) {
    fail("RESOURCE_LIMIT", 0, "file size limit exceeded");
  }
  ensureRange(bytes, 0, HEADER_SIZE);
  for (let index = 0; index < MAGIC.length; ++index) {
    if (bytes[index] !== MAGIC[index]) {
      fail("INVALID_MAGIC", index, "OXQ Magic byte differs");
    }
  }
  requireEqual(u16(bytes, 0x08), 1, "UNSUPPORTED_VERSION", 0x08, "version_major");
  requireEqual(u16(bytes, 0x0a), 0, "UNSUPPORTED_VERSION", 0x0a, "version_minor");
  requireEqual(u32(bytes, 0x0c), HEADER_SIZE, "INVALID_HEADER", 0x0c, "header_size");
  requireEqual(safeU64(bytes, 0x10), bytes.length, "SIZE_MISMATCH", 0x10, "file_size");
  const tableOffset = safeU64(bytes, 0x18);
  if (tableOffset < HEADER_SIZE) {
    fail("INVALID_SECTION_TABLE", 0x18, "Section Table overlaps Header");
  }
  const sectionCount = u32(bytes, 0x20);
  if (sectionCount < 5 || sectionCount > limits.maxSections) {
    fail("RESOURCE_LIMIT", 0x20, "section_count is outside supported bounds");
  }
  requireEqual(u32(bytes, 0x24), 0, "INVALID_HEADER", 0x24, "file_flags");
  const uuidBytes = bytes.subarray(0x28, 0x38);
  if (uuidBytes.every((byte) => byte === 0)) {
    fail("INVALID_HEADER", 0x28, "game_uuid must not be nil");
  }
  requireEqual(crc32c(bytes.subarray(0, 0x3c)), u32(bytes, 0x3c),
    "CRC_MISMATCH", 0x3c, "header_crc32c");

  const tableSize = sectionCount * SECTION_ENTRY_SIZE;
  ensureRange(bytes, tableOffset, tableSize, "INVALID_SECTION_TABLE");
  requireEqual(crc32c(bytes.subarray(tableOffset, tableOffset + tableSize)), u32(bytes, 0x38),
    "CRC_MISMATCH", 0x38, "section_table_crc32c");

  const entries = [];
  const types = new Set();
  for (let index = 0; index < sectionCount; ++index) {
    const entryOffset = tableOffset + index * SECTION_ENTRY_SIZE;
    const type = u32(bytes, entryOffset);
    const flags = u32(bytes, entryOffset + 4);
    const offset = safeU64(bytes, entryOffset + 8);
    const storedSize = safeU64(bytes, entryOffset + 16);
    const logicalSize = safeU64(bytes, entryOffset + 24);
    const payloadCrc = u32(bytes, entryOffset + 32);
    requireEqual(u32(bytes, entryOffset + 36), 0, "INVALID_SECTION_TABLE",
      entryOffset + 36, "entry.reserved");
    if (type === 0 || types.has(type)) {
      fail("INVALID_SECTION_TABLE", entryOffset, "Section type is zero or duplicated");
    }
    types.add(type);
    if ((flags & ~1) !== 0) {
      fail("UNSUPPORTED_VERSION", entryOffset + 4, "unsupported Section flags", type);
    }
    if (STANDARD_SECTIONS.has(type)) {
      requireEqual(flags, 1, "INVALID_SECTION_TABLE", entryOffset + 4,
        "standard Section flags", type);
    } else if ((flags & 1) !== 0) {
      fail("UNKNOWN_CRITICAL_SECTION", entryOffset, "unknown critical Section", type);
    }
    if (offset % 8 !== 0) {
      fail("INVALID_SECTION_TABLE", entryOffset + 8, "Section offset is not 8-byte aligned", type);
    }
    requireEqual(storedSize, logicalSize, "UNSUPPORTED_VERSION", entryOffset + 24,
      "logical_size", type);
    ensureRange(bytes, offset, storedSize, "SECTION_OUT_OF_RANGE", type);
    requireEqual(crc32c(bytes.subarray(offset, offset + storedSize)), payloadCrc,
      "CRC_MISMATCH", entryOffset + 32, "payload_crc32c", type);
    entries.push({ type, flags, offset, size: storedSize, entryOffset });
  }

  for (const type of STANDARD_SECTIONS) {
    if (!types.has(type)) {
      fail("INVALID_SECTION_TABLE", tableOffset, `required Section ${type} is missing`);
    }
  }

  const ranges = [
    { begin: 0, end: HEADER_SIZE, type: undefined },
    { begin: tableOffset, end: tableOffset + tableSize, type: undefined },
    ...entries.map((entry) => ({
      begin: entry.offset,
      end: entry.offset + entry.size,
      type: entry.type,
    })),
  ].sort((left, right) => left.begin - right.begin || left.end - right.end);
  let coveredEnd = 0;
  for (const range of ranges) {
    if (range.begin < coveredEnd) {
      fail("SECTION_OVERLAP", range.begin, "OXQ regions overlap", range.type);
    }
    requireZero(bytes, coveredEnd, range.begin, "INVALID_SECTION_TABLE");
    coveredEnd = range.end;
  }
  requireZero(bytes, coveredEnd, bytes.length, "INVALID_SECTION_TABLE");

  return {
    uuid: formatUuid(uuidBytes),
    entries,
    section(type) {
      const entry = entries.find((item) => item.type === type);
      return { entry, payload: bytes.subarray(entry.offset, entry.offset + entry.size) };
    },
  };
}

function parseStringPool(container, limits) {
  const { entry, payload } = container.section(5);
  requireEqual(u16(payload, 0, 5), 1, "UNSUPPORTED_VERSION", entry.offset,
    "STRING_POOL.section_version", 5);
  requireEqual(u16(payload, 2, 5), 8, "UNSUPPORTED_VERSION", entry.offset + 2,
    "STRING_POOL.header_size", 5);
  const count = u32(payload, 4, 5);
  if (count > limits.maxStrings) {
    fail("RESOURCE_LIMIT", entry.offset + 4, "string_count limit exceeded", 5);
  }
  const strings = new Map();
  let cursor = 8;
  for (let index = 0; index < count; ++index) {
    const reference = cursor;
    const length = u32(payload, cursor, 5);
    if (length > limits.maxStringBytes) {
      fail("RESOURCE_LIMIT", entry.offset + cursor, "string length limit exceeded", 5);
    }
    const dataOffset = cursor + 4;
    ensureRange(payload, dataOffset, length, "INVALID_STRING_REF", 5);
    const value = decodeUtf8(payload.subarray(dataOffset, dataOffset + length),
      entry.offset + dataOffset, 5);
    const end = dataOffset + length;
    const recordEnd = end + ((4 - (end % 4)) % 4);
    ensureRange(payload, end, recordEnd - end, "INVALID_STRING_REF", 5);
    requireZero(payload, end, recordEnd, "INVALID_STRING_REF", 5);
    strings.set(reference, value);
    cursor = recordEnd;
  }
  requireEqual(cursor, payload.length, "INVALID_STRING_REF", entry.offset + cursor,
    "STRING_POOL payload length", 5);
  return {
    require(reference, absoluteOffset, allowMissing = false) {
      if (reference === 0 && allowMissing) {
        return undefined;
      }
      if (reference === 0 || !strings.has(reference)) {
        fail("INVALID_STRING_REF", absoluteOffset, "reference is not a String Record start", 5);
      }
      return strings.get(reference);
    },
  };
}

// Standard Metadata tags from spec section 8.4. Paths define the independent
// JSON acceptance contract; absent fields are omitted.
const STRING_METADATA = new Map([
  [0x0001, ["redPlayer", "name"]], [0x0002, ["blackPlayer", "name"]],
  [0x0003, ["redPlayer", "id"]], [0x0004, ["blackPlayer", "id"]],
  [0x0005, ["redPlayer", "country"]], [0x0006, ["blackPlayer", "country"]],
  [0x0009, ["redPlayer", "title"]], [0x000a, ["blackPlayer", "title"]],
  [0x000b, ["redPlayer", "team"]], [0x000c, ["blackPlayer", "team"]],
  [0x000d, ["redPlayer", "timeUsed"]], [0x000e, ["blackPlayer", "timeUsed"]],
  [0x0010, ["event", "name"]], [0x0011, ["event", "id"]],
  [0x0012, ["event", "location"]], [0x0013, ["event", "organizer"]],
  [0x0014, ["event", "round"]], [0x0015, ["event", "type"]],
  [0x0016, ["event", "group"]], [0x0017, ["event", "boardNumber"]],
  [0x0018, ["event", "timeControl"]], [0x0020, ["event", "startTime"]],
  [0x0021, ["event", "endTime"]], [0x0031, ["resultText"]],
  [0x0040, ["opening", "name"]], [0x0041, ["opening", "code"]],
  [0x0042, ["opening", "id"]], [0x0050, ["title"]],
  [0x0052, ["gameType"]], [0x0060, ["referee"]], [0x0061, ["recorder"]],
  [0x0062, ["commentator"]], [0x0063, ["commentatorUri"]],
  [0x0064, ["creator"]], [0x0065, ["creatorUri"]],
  [0x0066, ["recordCreatedAt"]], [0x0067, ["recordModifiedAt"]],
  [0x0100, ["provenance", "sourceFormat"]],
  [0x0101, ["provenance", "sourceRecordId"]],
  [0x0102, ["provenance", "sourceUri"]],
  [0x0103, ["provenance", "importNote"]],
  [0x0104, ["provenance", "sourceFormatVersion"]],
  [0x0105, ["provenance", "sourceLibraryId"]],
  [0x0106, ["provenance", "sourceLibraryName"]],
  [0x0107, ["provenance", "sourceCategory"]],
]);

function setPath(target, path, value) {
  let parent = target;
  for (const component of path.slice(0, -1)) {
    parent[component] ??= {};
    parent = parent[component];
  }
  parent[path.at(-1)] = value;
}

function parseMetadata(container, stringPool, limits) {
  const { entry, payload } = container.section(1);
  requireEqual(u16(payload, 0, 1), 1, "UNSUPPORTED_VERSION", entry.offset,
    "GAME_METADATA.section_version", 1);
  requireEqual(u16(payload, 2, 1), 8, "UNSUPPORTED_VERSION", entry.offset + 2,
    "GAME_METADATA.header_size", 1);
  const count = u32(payload, 4, 1);
  if (count > limits.maxMetadataFields) {
    fail("RESOURCE_LIMIT", entry.offset + 4, "metadata field limit exceeded", 1);
  }
  const metadata = {};
  const seen = new Set();
  let cursor = 8;
  for (let index = 0; index < count; ++index) {
    ensureRange(payload, cursor, 8, "INVALID_METADATA", 1);
    const fieldOffset = entry.offset + cursor;
    const tag = u16(payload, cursor, 1);
    const valueType = u8(payload, cursor + 2, 1);
    const flags = u8(payload, cursor + 3, 1);
    const length = u32(payload, cursor + 4, 1);
    const valueOffset = cursor + 8;
    ensureRange(payload, valueOffset, length, "INVALID_METADATA", 1);
    const end = valueOffset + length;
    const fieldEnd = end + ((4 - (end % 4)) % 4);
    ensureRange(payload, end, fieldEnd - end, "INVALID_METADATA", 1);
    requireZero(payload, end, fieldEnd, "INVALID_METADATA", 1);
    if ((flags & ~3) !== 0) {
      fail("INVALID_METADATA", fieldOffset + 3, "reserved Metadata flags are nonzero", 1);
    }

    const isTag = tag === 0x0051;
    const isString = STRING_METADATA.has(tag) || isTag || tag === 0x7fff;
    const isRating = tag === 0x0007 || tag === 0x0008;
    const isEnum = tag === 0x0022 || tag === 0x0030;
    const known = isString || isRating || isEnum;
    if (known) {
      requireEqual(flags, isTag ? 2 : 0, "INVALID_METADATA", fieldOffset + 3,
        "Metadata flags", 1);
      requireEqual(length, 4, "INVALID_METADATA", fieldOffset + 4,
        "Metadata value_length", 1);
      requireEqual(valueType, isRating ? 2 : isEnum ? 1 : 5,
        "INVALID_METADATA", fieldOffset + 2, "Metadata value_type", 1);
      if (!isTag && seen.has(tag)) {
        fail("INVALID_METADATA", fieldOffset, "standard Metadata tag is duplicated", 1);
      }
      seen.add(tag);
    } else if ((flags & 1) !== 0) {
      fail("INVALID_METADATA", fieldOffset, "unknown critical Metadata tag", 1);
    }

    if (STRING_METADATA.has(tag)) {
      setPath(metadata, STRING_METADATA.get(tag),
        stringPool.require(u32(payload, valueOffset, 1), entry.offset + valueOffset));
    } else if (isTag) {
      metadata.tags ??= [];
      metadata.tags.push(stringPool.require(u32(payload, valueOffset, 1),
        entry.offset + valueOffset));
    } else if (isRating) {
      setPath(metadata, tag === 0x0007 ? ["redPlayer", "rating"] : ["blackPlayer", "rating"],
        i32(payload, valueOffset, 1));
    } else if (tag === 0x0022) {
      const names = ["UNKNOWN", "YEAR", "MONTH", "DAY", "MINUTE", "SECOND", "SUBSECOND"];
      const value = u32(payload, valueOffset, 1);
      if (value >= names.length) {
        fail("INVALID_METADATA", entry.offset + valueOffset, "invalid DATE_PRECISION", 1);
      }
      setPath(metadata, ["event", "datePrecision"], names[value]);
    } else if (tag === 0x0030) {
      const names = ["UNKNOWN", "RED_WIN", "BLACK_WIN", "DRAW", "UNFINISHED", "ABORTED"];
      const value = u32(payload, valueOffset, 1);
      if (value >= names.length) {
        fail("INVALID_METADATA", entry.offset + valueOffset, "invalid RESULT", 1);
      }
      metadata.result = names[value];
    } else if (tag === 0x7fff) {
      const value = stringPool.require(u32(payload, valueOffset, 1), entry.offset + valueOffset);
      try {
        metadata.extensions = JSON.parse(value);
      } catch {
        fail("INVALID_METADATA", entry.offset + valueOffset, "invalid extended Metadata JSON", 1);
      }
    } else if (valueType === 5 && length === 4) {
      stringPool.require(u32(payload, valueOffset, 1), entry.offset + valueOffset, true);
    }
    cursor = fieldEnd;
  }
  requireEqual(cursor, payload.length, "INVALID_METADATA", entry.offset + cursor,
    "GAME_METADATA payload length", 1);
  return metadata;
}

const PIECE_NAMES = [undefined, "king", "advisor", "elephant", "horse", "rook", "cannon", "pawn"];

function parsePosition(container) {
  const { entry, payload } = container.section(2);
  requireEqual(u16(payload, 0, 2), 1, "UNSUPPORTED_VERSION", entry.offset,
    "POSITION.section_version", 2);
  requireEqual(u16(payload, 2, 2), 16, "UNSUPPORTED_VERSION", entry.offset + 2,
    "POSITION.header_size", 2);
  requireEqual(u16(payload, 4, 2), 1, "INVALID_POSITION", entry.offset + 4,
    "POSITION.variant", 2);
  const side = u8(payload, 6, 2);
  if (side > 1) {
    fail("INVALID_POSITION", entry.offset + 6, "invalid side_to_move", 2);
  }
  requireEqual(u8(payload, 7, 2), 0, "INVALID_POSITION", entry.offset + 7,
    "POSITION.flags", 2);
  const fullmoveNumber = u16(payload, 8, 2);
  if (fullmoveNumber === 0) {
    fail("INVALID_POSITION", entry.offset + 8, "fullmove_number must be positive", 2);
  }
  const count = u16(payload, 10, 2);
  if (count > 32) {
    fail("INVALID_POSITION", entry.offset + 10, "piece_count exceeds 32", 2);
  }
  requireEqual(u32(payload, 12, 2), 0, "INVALID_POSITION", entry.offset + 12,
    "POSITION.reserved", 2);
  requireEqual(payload.length, 16 + count * 4, "INVALID_POSITION", entry.offset,
    "POSITION payload length", 2);
  const occupied = new Set();
  const pieces = [];
  for (let index = 0; index < count; ++index) {
    const offset = 16 + index * 4;
    const code = u8(payload, offset, 2);
    const square = u8(payload, offset + 1, 2);
    const kind = code & 0x0f;
    if ((code & 0x70) !== 0 || PIECE_NAMES[kind] === undefined || square > 89 ||
        occupied.has(square)) {
      fail("INVALID_POSITION", entry.offset + offset, "invalid or duplicate Piece Record", 2);
    }
    requireEqual(u16(payload, offset + 2, 2), 0, "INVALID_POSITION",
      entry.offset + offset + 2, "piece_flags", 2);
    occupied.add(square);
    pieces.push([`${(code & 0x80) === 0 ? "red" : "black"}-${PIECE_NAMES[kind]}`, square]);
  }
  pieces.sort((left, right) => left[1] - right[1]);
  return { sideToMove: side === 0 ? "red" : "black", fullmoveNumber, pieces };
}

function parseAnnotations(container, stringPool, limits) {
  const { entry, payload } = container.section(4);
  requireEqual(u16(payload, 0, 4), 1, "UNSUPPORTED_VERSION", entry.offset,
    "ANNOTATION.section_version", 4);
  requireEqual(u16(payload, 2, 4), 16, "UNSUPPORTED_VERSION", entry.offset + 2,
    "ANNOTATION.header_size", 4);
  requireEqual(u16(payload, 4, 4), 24, "UNSUPPORTED_VERSION", entry.offset + 4,
    "ANNOTATION.record_size", 4);
  requireEqual(u16(payload, 6, 4), 0, "INVALID_ANNOTATION", entry.offset + 6,
    "ANNOTATION.flags", 4);
  const count = u32(payload, 8, 4);
  if (count > limits.maxAnnotations) {
    fail("RESOURCE_LIMIT", entry.offset + 8, "annotation_count limit exceeded", 4);
  }
  requireEqual(u32(payload, 12, 4), 0, "INVALID_ANNOTATION", entry.offset + 12,
    "ANNOTATION.reserved", 4);
  requireEqual(payload.length, 16 + count * 24, "INVALID_ANNOTATION", entry.offset,
    "ANNOTATION payload length", 4);
  const annotations = [];
  for (let index = 0; index < count; ++index) {
    const offset = 16 + index * 24;
    const id = u32(payload, offset, 4);
    const next = u32(payload, offset + 4, 4);
    const kind = u16(payload, offset + 8, 4);
    const flags = u16(payload, offset + 10, 4);
    requireEqual(id, index + 1, "INVALID_ANNOTATION", entry.offset + offset,
      "annotation_id", 4);
    if (next > count || kind < 1 || kind > 2 || (flags & ~1) !== 0) {
      fail("INVALID_ANNOTATION", entry.offset + offset + 4,
        "invalid Annotation link, kind, or flags", 4);
    }
    const text = stringPool.require(u32(payload, offset + 12, 4), entry.offset + offset + 12);
    const author = stringPool.require(u32(payload, offset + 16, 4),
      entry.offset + offset + 16, true);
    const language = stringPool.require(u32(payload, offset + 20, 4),
      entry.offset + offset + 20, true);
    annotations.push({ id, next, kind: kind === 1 ? "COMMENT" : "SOURCE_NOTE",
      beforeMove: (flags & 1) !== 0, text, author, language });
  }
  return annotations;
}

function parseMoveTree(container, annotationRecords, limits) {
  const { entry, payload } = container.section(3);
  requireEqual(u16(payload, 0, 3), 1, "UNSUPPORTED_VERSION", entry.offset,
    "MOVE_TREE.section_version", 3);
  requireEqual(u16(payload, 2, 3), 16, "UNSUPPORTED_VERSION", entry.offset + 2,
    "MOVE_TREE.header_size", 3);
  requireEqual(u16(payload, 4, 3), 32, "UNSUPPORTED_VERSION", entry.offset + 4,
    "MOVE_TREE.node_record_size", 3);
  requireEqual(u16(payload, 6, 3), 0, "INVALID_TREE", entry.offset + 6,
    "MOVE_TREE.flags", 3);
  const count = u32(payload, 8, 3);
  if (count < 1 || count > limits.maxNodes) {
    fail("RESOURCE_LIMIT", entry.offset + 8, "node_count is outside supported bounds", 3);
  }
  requireEqual(u32(payload, 12, 3), 0, "INVALID_TREE", entry.offset + 12,
    "MOVE_TREE.root_index", 3);
  requireEqual(payload.length, 16 + count * 32, "INVALID_TREE", entry.offset,
    "MOVE_TREE payload length", 3);

  const rawNodes = [];
  for (let index = 0; index < count; ++index) {
    const offset = 16 + index * 32;
    const parent = u32(payload, offset, 3);
    const firstChild = u32(payload, offset + 4, 3);
    const nextSibling = u32(payload, offset + 8, 3);
    const firstAnnotation = u32(payload, offset + 12, 3);
    const moveBytes = [...payload.subarray(offset + 16, offset + 20)];
    const ply = u32(payload, offset + 20, 3);
    requireEqual(u32(payload, offset + 24, 3), 0, "INVALID_TREE",
      entry.offset + offset + 24, "node_flags", 3);
    requireEqual(u32(payload, offset + 28, 3), 0, "INVALID_TREE",
      entry.offset + offset + 28, "node.reserved", 3);
    for (const [value, field] of [[parent, "parent"], [firstChild, "first_child"],
      [nextSibling, "next_sibling"]]) {
      if (value !== ABSENT_INDEX && value >= count) {
        fail("INVALID_TREE", entry.offset + offset, `${field} index is invalid`, 3);
      }
    }
    if (firstAnnotation > annotationRecords.length) {
      fail("INVALID_TREE", entry.offset + offset + 12, "annotation ID is invalid", 3);
    }
    if (index === 0) {
      if (parent !== ABSENT_INDEX || ply !== 0 || moveBytes.some((byte) => byte !== 0xff)) {
        fail("INVALID_TREE", entry.offset + offset, "invalid root node", 3);
      }
    } else {
      if (parent === ABSENT_INDEX || moveBytes[0] > 89 || moveBytes[1] > 89 ||
          moveBytes[2] !== 0 || moveBytes[3] !== 0) {
        fail("INVALID_MOVE", entry.offset + offset + 16, "invalid non-root Move", 3);
      }
    }
    rawNodes.push({ parent, firstChild, nextSibling, firstAnnotation, moveBytes, ply });
  }

  const owners = new Array(count).fill(0);
  const childrenByNode = [];
  for (let parent = 0; parent < count; ++parent) {
    const children = [];
    const local = new Set();
    let child = rawNodes[parent].firstChild;
    while (child !== ABSENT_INDEX) {
      if (local.has(child)) {
        fail("INVALID_TREE", entry.offset, "child/sibling chain contains a cycle", 3);
      }
      local.add(child);
      if (rawNodes[child].parent !== parent || rawNodes[child].ply !== rawNodes[parent].ply + 1) {
        fail("INVALID_TREE", entry.offset, "parent or ply disagrees with child chain", 3);
      }
      ++owners[child];
      children.push(child);
      child = rawNodes[child].nextSibling;
    }
    childrenByNode.push(children);
  }
  if (rawNodes[0].nextSibling !== ABSENT_INDEX || owners[0] !== 0 ||
      owners.slice(1).some((countValue) => countValue !== 1)) {
    fail("INVALID_TREE", entry.offset, "tree is not uniquely connected to the root", 3);
  }

  const reachable = new Set();
  const stack = [0];
  while (stack.length !== 0) {
    const node = stack.pop();
    if (reachable.has(node)) {
      fail("INVALID_TREE", entry.offset, "tree traversal revisited a node", 3);
    }
    reachable.add(node);
    stack.push(...childrenByNode[node]);
  }
  if (reachable.size !== count) {
    fail("INVALID_TREE", entry.offset, "tree contains unreachable nodes", 3);
  }

  const annotationOwners = new Set();
  const annotations = [];
  for (let nodeIndex = 0; nodeIndex < count; ++nodeIndex) {
    const local = new Set();
    let annotationId = rawNodes[nodeIndex].firstAnnotation;
    while (annotationId !== 0) {
      if (local.has(annotationId) || annotationOwners.has(annotationId)) {
        fail("INVALID_ANNOTATION", entry.offset, "Annotation chain cycles or is shared", 4);
      }
      local.add(annotationId);
      annotationOwners.add(annotationId);
      const source = annotationRecords[annotationId - 1];
      if (nodeIndex === 0 && source.beforeMove) {
        fail("INVALID_ANNOTATION", entry.offset, "root Annotation cannot be BEFORE_MOVE", 4);
      }
      const projected = { node: nodeIndex, kind: source.kind, text: source.text };
      if (source.beforeMove) projected.beforeMove = true;
      if (source.author !== undefined) projected.author = source.author;
      if (source.language !== undefined) projected.language = source.language;
      annotations.push(projected);
      annotationId = source.next;
    }
  }
  if (annotationOwners.size !== annotationRecords.length) {
    fail("INVALID_ANNOTATION", entry.offset, "unreachable Annotation record", 4);
  }

  const tree = rawNodes.map((node, index) => ({
    index,
    parent: node.parent === ABSENT_INDEX ? null : node.parent,
    move: index === 0 ? null : node.moveBytes.slice(0, 2),
    children: childrenByNode[index],
  }));
  return { tree, annotations };
}

export function readIndependentOxq(input, options = {}) {
  const bytes = Buffer.isBuffer(input)
    ? input
    : Buffer.from(input.buffer, input.byteOffset, input.byteLength);
  const limits = { ...DEFAULT_LIMITS, ...options };
  const container = parseContainer(bytes, limits);
  const stringPool = parseStringPool(container, limits);
  const metadata = parseMetadata(container, stringPool, limits);
  const position = parsePosition(container);
  const annotationRecords = parseAnnotations(container, stringPool, limits);
  const { tree, annotations } = parseMoveTree(container, annotationRecords, limits);
  const unknownSections = container.entries
    .filter(({ type }) => !STANDARD_SECTIONS.has(type))
    .map(({ type, flags, offset, size }) => ({
      type,
      critical: (flags & 1) !== 0,
      payloadHex: bytes.subarray(offset, offset + size).toString("hex"),
    }));
  return {
    valid: true,
    uuid: container.uuid,
    metadata,
    position,
    tree,
    annotations,
    unknownSections,
  };
}
