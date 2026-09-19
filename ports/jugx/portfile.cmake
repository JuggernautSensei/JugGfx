# JugX 는 STATIC 라이브러리만 만든다.
set(VCPKG_LIBRARY_LINKAGE static)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO JuggernautSensei/JugX
    REF 76472b779e3a6ff8faea6bd02349b7ae981b030c
    SHA512 515e287587a76e0627f63eca5cb655fba5829c4b0ce9a70d85785a8fe99eb7794238451dda0c2a0906dce83c80eb95c88e6cc56105369a39eaf870002bb6d6c8
    HEAD_REF main
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DJUGX_INSTALL=ON
        -DJUGX_BUILD_TESTS=OFF
        -DJUGX_BUILD_BENCHMARKS=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME jugx CONFIG_PATH share/jugx)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/README.md")
