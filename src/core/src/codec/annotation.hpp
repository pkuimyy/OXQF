#pragma once

#include "codec/container.hpp"
#include "codec/string_pool.hpp"

#include <oxq/core/codec_error.hpp>
#include <oxq/core/game_model.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

namespace oxq::core::detail {

struct AnnotationLimits {
  std::size_t max_annotations{10'000'000};
};

struct AnnotationRecordView {
  std::uint32_t next_annotation_id{};
  Annotation value;
};

struct AnnotationView {
  std::vector<AnnotationRecordView> records;
};

using AnnotationResult = std::variant<AnnotationView, CodecError>;

[[nodiscard]] AnnotationResult read_annotations(
    std::span<const std::byte> input,
    const ContainerView& container,
    const StringPoolView& strings,
    const AnnotationLimits& limits = {});

}  // namespace oxq::core::detail
