include_guard(GLOBAL)

# Configure a CLAP module as a macOS CFBundle or a single-file plugin elsewhere.
# Call after creating the MODULE target and setting any RETRO_CLAP_VERSION property.
function(retro_configure_clap target output_name bundle_id)
  if(NOT TARGET "${target}")
    message(FATAL_ERROR "retro_configure_clap: unknown target '${target}'")
  endif()
  get_target_property(target_type "${target}" TYPE)
  if(NOT target_type STREQUAL "MODULE_LIBRARY")
    message(FATAL_ERROR "retro_configure_clap requires a MODULE library")
  endif()
  set_target_properties("${target}" PROPERTIES
    PREFIX ""
    OUTPUT_NAME "${output_name}"
    C_VISIBILITY_PRESET hidden)

  if(APPLE)
    set(RETRO_CLAP_EXECUTABLE "${output_name}")
    set(RETRO_CLAP_BUNDLE_ID "${bundle_id}")
    set(RETRO_CLAP_BUNDLE_NAME "${output_name}")
    get_target_property(RETRO_CLAP_VERSION "${target}" RETRO_CLAP_VERSION)
    if(NOT RETRO_CLAP_VERSION)
      set(RETRO_CLAP_VERSION "${PROJECT_VERSION}")
    endif()
    if(NOT RETRO_CLAP_VERSION)
      set(RETRO_CLAP_VERSION "0.1.0")
    endif()
    set(plist "${CMAKE_CURRENT_BINARY_DIR}/${target}-Info.plist")
    configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ClapInfo.plist.in" "${plist}" @ONLY)
    # CMake's CFBundle generator uses this template for MODULE targets too.
    set_target_properties("${target}" PROPERTIES
      BUNDLE TRUE
      BUNDLE_EXTENSION "clap"
      SUFFIX ""
      MACOSX_BUNDLE_INFO_PLIST "${plist}")
  else()
    set_target_properties("${target}" PROPERTIES SUFFIX ".clap")
  endif()
endfunction()
