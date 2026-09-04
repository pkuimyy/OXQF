#pragma once

#include <oxq/convert/cbl_reader.hpp>
#include <oxq/convert/conversion_report.hpp>
#include <oxq/core/game_model.hpp>
#include <oxq/core/reader.hpp>
#include <oxq/core/validation.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace oxq::cli {

[[nodiscard]] std::string json_string(std::string_view value);
[[nodiscard]] std::string game_json(const core::GameModel& game);
[[nodiscard]] std::string games_json(
    const std::vector<core::GameModel>& games);
[[nodiscard]] std::string reader_diagnostics_json(
    const core::ReaderDiagnostics& diagnostics);
[[nodiscard]] std::string validation_issues_json(
    const std::vector<core::ValidationIssue>& issues);
[[nodiscard]] std::string conversion_report_json(
    const convert::ConversionReport& report);
[[nodiscard]] std::string cbl_library_json(
    const convert::CblLibraryInfo& library);

}  // namespace oxq::cli
