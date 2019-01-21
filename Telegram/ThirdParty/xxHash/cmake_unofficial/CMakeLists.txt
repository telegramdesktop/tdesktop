# To the extent possible under law, the author(s) have dedicated all
# copyright and related and neighboring rights to this software to
# the public domain worldwide. This software is distributed without
# any warranty.
#
# For details, see <http://creativecommons.org/publicdomain/zero/1.0/>.

set(XXHASH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")

file(STRINGS "${XXHASH_DIR}/xxhash.h" XXHASH_VERSION_MAJOR REGEX "^#define XXH_VERSION_MAJOR +([0-9]+) *$")
string(REGEX REPLACE "^#define XXH_VERSION_MAJOR +([0-9]+) *$" "\\1" XXHASH_VERSION_MAJOR "${XXHASH_VERSION_MAJOR}")
file(STRINGS "${XXHASH_DIR}/xxhash.h" XXHASH_VERSION_MINOR REGEX "^#define XXH_VERSION_MINOR +([0-9]+) *$")
string(REGEX REPLACE "^#define XXH_VERSION_MINOR +([0-9]+) *$" "\\1" XXHASH_VERSION_MINOR "${XXHASH_VERSION_MINOR}")
file(STRINGS "${XXHASH_DIR}/xxhash.h" XXHASH_VERSION_RELEASE REGEX "^#define XXH_VERSION_RELEASE +([0-9]+) *$")
string(REGEX REPLACE "^#define XXH_VERSION_RELEASE +([0-9]+) *$" "\\1" XXHASH_VERSION_RELEASE "${XXHASH_VERSION_RELEASE}")
set(XXHASH_VERSION_STRING "${XXHASH_VERSION_MAJOR}.${XXHASH_VERSION_MINOR}.${XXHASH_VERSION_RELEASE}")
set(XXHASH_LIB_VERSION ${XXHASH_VERSION_STRING})
set(XXHASH_LIB_SOVERSION "${XXHASH_VERSION_MAJOR}")
mark_as_advanced(XXHASH_VERSION_MAJOR XXHASH_VERSION_MINOR XXHASH_VERSION_RELEASE XXHASH_VERSION_STRING XXHASH_LIB_VERSION XXHASH_LIB_SOVERSION)

option(BUILD_XXHSUM "Build the xxhsum binary" ON)
option(BUILD_SHARED_LIBS "Build shared library" ON)

if("${CMAKE_VERSION}" VERSION_LESS "3.0")
  project(XXHASH C)
else()
  cmake_policy (SET CMP0048 NEW)
  project(XXHASH
    VERSION ${XXHASH_VERSION_STRING}
    LANGUAGES C)
endif()

cmake_minimum_required (VERSION 2.8.12)

# If XXHASH is being bundled in another project, we don't want to
# install anything.  However, we want to let people override this, so
# we'll use the XXHASH_BUNDLED_MODE variable to let them do that; just
# set it to OFF in your project before you add_subdirectory(xxhash/contrib/cmake_unofficial).
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL "${CMAKE_SOURCE_DIR}")
  # Bundled mode hasn't been set one way or the other, set the default
  # depending on whether or not we are the top-level project.
  if("${XXHASH_PARENT_DIRECTORY}" STREQUAL "")
    set(XXHASH_BUNDLED_MODE OFF)
  else()
    set(XXHASH_BUNDLED_MODE ON)
  endif()
endif()
mark_as_advanced(XXHASH_BUNDLED_MODE)

# Allow people to choose whether to build shared or static libraries
# via the BUILD_SHARED_LIBS option unless we are in bundled mode, in
# which case we always use static libraries.
include(CMakeDependentOption)
CMAKE_DEPENDENT_OPTION(BUILD_SHARED_LIBS "Build shared libraries" ON "NOT XXHASH_BUNDLED_MODE" OFF)

include_directories("${XXHASH_DIR}")

# libxxhash
add_library(xxhash "${XXHASH_DIR}/xxhash.c")
set_target_properties(xxhash PROPERTIES
  SOVERSION "${XXHASH_VERSION_STRING}"
  VERSION "${XXHASH_VERSION_STRING}")

# xxhsum
add_executable(xxhsum "${XXHASH_DIR}/xxhsum.c")
target_link_libraries(xxhsum xxhash)

# Extra warning flags
include (CheckCCompilerFlag)
foreach (flag
    -Wall -Wextra -Wcast-qual -Wcast-align -Wshadow
    -Wstrict-aliasing=1 -Wswitch-enum -Wdeclaration-after-statement
    -Wstrict-prototypes -Wundef)
  # Because https://gcc.gnu.org/wiki/FAQ#wnowarning
  string(REGEX REPLACE "\\-Wno\\-(.+)" "-W\\1" flag_to_test "${flag}")
  string(REGEX REPLACE "[^a-zA-Z0-9]+" "_" test_name "CFLAG_${flag_to_test}")

  check_c_compiler_flag("${ADD_COMPILER_FLAGS_PREPEND} ${flag_to_test}" ${test_name})

  if(${test_name})
    set(CMAKE_C_FLAGS "${flag} ${CMAKE_C_FLAGS}")
  endif()

  unset(test_name)
  unset(flag_to_test)
endforeach (flag)

if(NOT XXHASH_BUNDLED_MODE)
  include(GNUInstallDirs)

  install(TARGETS xxhsum
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
  install(TARGETS xxhash
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
  install(FILES "${XXHASH_DIR}/xxhash.h"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
  install(FILES "${XXHASH_DIR}/xxhsum.1"
    DESTINATION "${CMAKE_INSTALL_MANDIR}/man1")
endif(NOT XXHASH_BUNDLED_MODE)
