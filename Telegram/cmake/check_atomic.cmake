# Modified from https://github.com/llvm/llvm-project/blob/main/llvm/cmake/modules/CheckAtomic.cmake

include(CheckCXXSourceCompiles)
include(CheckLibraryExists)

# Sometimes linking against libatomic is required for atomic ops, if
# the platform doesn't support lock-free atomics.

function(check_working_cxx_atomics varname)
  set(OLD_CMAKE_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS})
  set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -std=c++11")
  CHECK_CXX_SOURCE_COMPILES("
#include <atomic>
std::atomic<int> x;
std::atomic<short> y;
std::atomic<char> z;
std::atomic<uint64_t> v;
int main() {
  ++z;
  ++y;
  uint64_t i = v.load(std::memory_order_relaxed);
  (void)i;
  return ++x;
}
" ${varname})
  set(CMAKE_REQUIRED_FLAGS ${OLD_CMAKE_REQUIRED_FLAGS})
endfunction(check_working_cxx_atomics)

# Check for (non-64-bit) atomic operations.
if(MSVC)
  set(HAVE_CXX_ATOMICS_WITHOUT_LIB True)
elseif(LLVM_COMPILER_IS_GCC_COMPATIBLE OR CMAKE_CXX_COMPILER_ID MATCHES "XL")
  # First check if atomics work without the library.
  check_working_cxx_atomics(HAVE_CXX_ATOMICS_WITHOUT_LIB)
  # If not, check if the library exists, and atomics work with it.
  if(NOT HAVE_CXX_ATOMICS_WITHOUT_LIB)
    list(APPEND CMAKE_REQUIRED_LIBRARIES "atomic")
    check_working_cxx_atomics(HAVE_CXX_ATOMICS_WITH_LIB)
    if (NOT HAVE_CXX_ATOMICS_WITH_LIB)
      message(FATAL_ERROR "Host compiler must support std::atomic!")
    endif()
  endif()
endif()
