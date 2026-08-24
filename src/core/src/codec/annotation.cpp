#include "codec/annotation.hpp"

#include "codec/binary.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace oxq::core::detail {
namespace {

constexpr std::uint32_t kAnnotationSectionType = 4;
constexpr std::size_t kHeaderSize = 16;
constexpr std::size_t kRecordSize = 24;

[[nodiscard]] CodecError error(CodecErrorCode code, std::size_t offset, std::string field,
                               std::string message, std::optional<std::uint64_t> expected = {},
                               std::optional<std::uint64_t> actual = {}) {
  return {code, offset, kAnnotationSectionType, std::move(field), std::move(message), expected,
          actual};
}

}  // namespace

AnnotationResult read_annotations(std::span<const std::byte> input, const ContainerView& container,
                                  const StringPoolView& strings,
                                  const AnnotationLimits& limits) {
  const auto section = std::ranges::find(container.sections, kAnnotationSectionType,
                                         &SectionView::type);
  if (section == container.sections.end()) {
    return error(CodecErrorCode::invalid_section_table, 0, "section_type",
                 "ANNOTATION Section is missing");
  }
  if (!contains(input, section->offset, section->size)) {
    return error(CodecErrorCode::section_out_of_range, section->offset, "section_range",
                 "ANNOTATION Section extends beyond input");
  }
  const auto payload = input.subspan(section->offset, section->size);
  if (payload.size() < kHeaderSize) {
    return error(CodecErrorCode::invalid_annotation, section->offset, "header",
                 "ANNOTATION is shorter than its 16-byte header", kHeaderSize, payload.size());
  }

  const auto version = read_u16(payload, 0);
  if (version != 1) {
    return error(CodecErrorCode::unsupported_version, section->offset, "section_version",
                 "unsupported ANNOTATION Section version", 1, version);
  }
  const auto header_size = read_u16(payload, 2);
  if (header_size != kHeaderSize) {
    return error(CodecErrorCode::invalid_annotation, section->offset + 2, "header_size",
                 "ANNOTATION header_size must be 16", kHeaderSize, header_size);
  }
  const auto record_size = read_u16(payload, 4);
  if (record_size != kRecordSize) {
    return error(CodecErrorCode::invalid_annotation, section->offset + 4, "record_size",
                 "ANNOTATION record_size must be 24", kRecordSize, record_size);
  }
  const auto annotation_flags = read_u16(payload, 6);
  if (annotation_flags != 0) {
    return error(CodecErrorCode::invalid_annotation, section->offset + 6, "annotation_flags",
                 "ANNOTATION flags must be zero in v1", 0, annotation_flags);
  }
  const auto count = static_cast<std::size_t>(read_u32(payload, 8));
  if (count > limits.max_annotations) {
    return error(CodecErrorCode::resource_limit, section->offset + 8, "annotation_count",
                 "annotation count limit exceeded", limits.max_annotations, count);
  }
  const auto reserved = read_u32(payload, 12);
  if (reserved != 0) {
    return error(CodecErrorCode::invalid_annotation, section->offset + 12, "reserved",
                 "ANNOTATION reserved bytes must be zero", 0, reserved);
  }

  std::size_t records_size = 0;
  std::size_t expected_size = 0;
  if (!checked_multiply(count, kRecordSize, records_size) ||
      !checked_add(kHeaderSize, records_size, expected_size)) {
    return error(CodecErrorCode::integer_overflow, section->offset + 8, "annotation_count",
                 "ANNOTATION size calculation overflowed", {}, count);
  }
  if (payload.size() != expected_size) {
    return error(CodecErrorCode::invalid_annotation, section->offset + 8, "annotation_count",
                 "ANNOTATION size does not match annotation_count", expected_size,
                 payload.size());
  }

  AnnotationView result;
  result.records.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    const std::size_t cursor = kHeaderSize + index * kRecordSize;
    const auto annotation_id = read_u32(payload, cursor);
    const auto expected_id = static_cast<std::uint64_t>(index) + 1U;
    if (annotation_id != expected_id) {
      return error(CodecErrorCode::invalid_annotation, section->offset + cursor,
                   "annotation_id", "annotation_id must equal its one-based record index",
                   expected_id, annotation_id);
    }
    const auto next_id = read_u32(payload, cursor + 4);
    if (next_id > count) {
      return error(CodecErrorCode::invalid_annotation, section->offset + cursor + 4,
                   "next_annotation_id", "next_annotation_id is outside the Annotation table",
                   count, next_id);
    }
    const auto kind = read_u16(payload, cursor + 8);
    if (kind < 1 || kind > 2) {
      return error(CodecErrorCode::invalid_annotation, section->offset + cursor + 8, "kind",
                   "Annotation kind is outside the v1 domain", {}, kind);
    }
    const auto flags = read_u16(payload, cursor + 10);
    if ((flags & ~1U) != 0) {
      return error(CodecErrorCode::invalid_annotation, section->offset + cursor + 10, "flags",
                   "Annotation flags contain reserved bits", 1, flags);
    }

    const auto text_ref = read_u32(payload, cursor + 12);
    const auto author_ref = read_u32(payload, cursor + 16);
    const auto language_ref = read_u32(payload, cursor + 20);
    const auto text = strings.find(text_ref);
    if (text_ref == 0 || !text.has_value()) {
      return error(CodecErrorCode::invalid_string_ref, section->offset + cursor + 12, "text_ref",
                   "Annotation text_ref must point to a String Record", {}, text_ref);
    }

    Annotation value;
    value.kind = kind == 1 ? AnnotationKind::comment : AnnotationKind::source_note;
    value.before_move = (flags & 1U) != 0;
    value.text = std::string{*text};
    if (author_ref != 0) {
      const auto author = strings.find(author_ref);
      if (!author.has_value()) {
        return error(CodecErrorCode::invalid_string_ref, section->offset + cursor + 16,
                     "author_ref", "Annotation author_ref does not point to a String Record", {},
                     author_ref);
      }
      value.author = std::string{*author};
    }
    if (language_ref != 0) {
      const auto language = strings.find(language_ref);
      if (!language.has_value()) {
        return error(CodecErrorCode::invalid_string_ref, section->offset + cursor + 20,
                     "language_ref",
                     "Annotation language_ref does not point to a String Record", {},
                     language_ref);
      }
      value.language = std::string{*language};
    }
    result.records.push_back({next_id, std::move(value)});
  }
  return result;
}

}  // namespace oxq::core::detail
