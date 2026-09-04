#pragma once

#include <oxq/convert/cbl_reader.hpp>
#include <oxq/convert/cbl_writer.hpp>
#include <oxq/convert/conversion_report.hpp>
#include <oxq/core/codec_error.hpp>
#include <oxq/core/validation.hpp>

#include <string_view>

namespace oxq::cli {

[[nodiscard]] std::string_view name(core::CodecErrorCode code) noexcept;
[[nodiscard]] std::string_view name(core::ValidationCode code) noexcept;
[[nodiscard]] std::string_view name(convert::CblErrorCode code) noexcept;
[[nodiscard]] std::string_view name(convert::CblWriteErrorCode code) noexcept;
[[nodiscard]] std::string_view name(convert::ConversionCode code) noexcept;

}  // namespace oxq::cli
