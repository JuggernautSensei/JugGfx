#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "OpenImageIO::OpenImageIO_Util" for configuration "Debug"
set_property(TARGET OpenImageIO::OpenImageIO_Util APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(OpenImageIO::OpenImageIO_Util PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/debug/lib/OpenImageIO_Util_d.lib"
  IMPORTED_LINK_DEPENDENT_LIBRARIES_DEBUG "Imath::Imath"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/debug/bin/OpenImageIO_Util_d.dll"
  )

list(APPEND _cmake_import_check_targets OpenImageIO::OpenImageIO_Util )
list(APPEND _cmake_import_check_files_for_OpenImageIO::OpenImageIO_Util "${_IMPORT_PREFIX}/debug/lib/OpenImageIO_Util_d.lib" "${_IMPORT_PREFIX}/debug/bin/OpenImageIO_Util_d.dll" )

# Import target "OpenImageIO::OpenImageIO" for configuration "Debug"
set_property(TARGET OpenImageIO::OpenImageIO APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(OpenImageIO::OpenImageIO PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/debug/lib/OpenImageIO_d.lib"
  IMPORTED_LINK_DEPENDENT_LIBRARIES_DEBUG "Imath::Imath;OpenEXR::OpenEXR;libjpeg-turbo::jpeg;OpenEXR::OpenEXRCore;OpenColorIO::OpenColorIO"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/debug/bin/OpenImageIO_d.dll"
  )

list(APPEND _cmake_import_check_targets OpenImageIO::OpenImageIO )
list(APPEND _cmake_import_check_files_for_OpenImageIO::OpenImageIO "${_IMPORT_PREFIX}/debug/lib/OpenImageIO_d.lib" "${_IMPORT_PREFIX}/debug/bin/OpenImageIO_d.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
