set(document_root "${stage_dir}/${OXQF_INSTALL_DATADIR}/oxq/doc")
set(specification_root "${stage_dir}/${OXQF_INSTALL_DATADIR}/oxq/spec")
set(vector_root "${stage_dir}/${OXQF_INSTALL_DATADIR}/oxq/test-vectors")

set(required_files
  "${stage_dir}/${OXQF_INSTALL_INCLUDEDIR}/oxq/core/game_model.hpp"
  "${stage_dir}/${OXQF_INSTALL_INCLUDEDIR}/oxq/core/reader.hpp"
  "${stage_dir}/${OXQF_INSTALL_INCLUDEDIR}/oxq/core/writer.hpp"
  "${stage_dir}/${OXQF_INSTALL_INCLUDEDIR}/oxq/convert/cbl_reader.hpp"
  "${stage_dir}/${OXQF_INSTALL_INCLUDEDIR}/oxq/convert/cbl_writer.hpp"
  "${stage_dir}/${OXQF_INSTALL_LIBDIR}/cmake/OXQF/OXQFConfig.cmake"
  "${stage_dir}/README.md"
  "${stage_dir}/CHANGELOG.md"
  "${stage_dir}/LICENSE"
  "${document_root}/cli.md"
  "${document_root}/mvp-acceptance.md"
  "${document_root}/release.md"
  "${specification_root}/oxq-v1.md"
  "${specification_root}/cbl-adapter-v1.md"
  "${document_root}/writer-compatibility/README.md"
  "${vector_root}/oxq-v1/manifest.json"
  "${vector_root}/oxq-v1/minimal.oxq"
  "${vector_root}/cbl-v3/SHA256SUMS"
  "${vector_root}/cbl-v3/semantic-baseline.json"
)
foreach(required_file IN LISTS required_files)
  if(NOT EXISTS "${required_file}")
    message(FATAL_ERROR "Installed release is missing: ${required_file}")
  endif()
endforeach()

file(GLOB core_libraries "${stage_dir}/${OXQF_INSTALL_LIBDIR}/*oxq-core*")
file(GLOB convert_libraries "${stage_dir}/${OXQF_INSTALL_LIBDIR}/*oxq-convert*")
if(NOT core_libraries OR NOT convert_libraries)
  message(FATAL_ERROR "Installed release is missing oxq-core or oxq-convert")
endif()

file(GLOB_RECURSE installed_paths RELATIVE "${stage_dir}" "${stage_dir}/*")
foreach(installed_path IN LISTS installed_paths)
  if(installed_path MATCHES "(^|/)(raw|\\.codex|secrets)(/|\\.|$)" OR
     installed_path MATCHES "CCBridge\\.rar$")
    message(FATAL_ERROR "Forbidden private asset in installed release: ${installed_path}")
  endif()
endforeach()
