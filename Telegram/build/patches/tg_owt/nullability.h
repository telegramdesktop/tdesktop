#pragma once

#if __has_include(<third_party/abseil-cpp/absl/base/nullability.h>)
#include <third_party/abseil-cpp/absl/base/nullability.h>
#endif  // __has_include

#if !defined(ABSL_BASE_NULLABILITY_H_)
#define TDESKTOP_NULLABILITY_FALLBACK 1

namespace absl {

template <typename T>
using Nonnull = T;

template <typename T>
using Nullable = T;

template <typename T>
using NullabilityUnknown = T;

}  // namespace absl

#endif  // !defined(ABSL_BASE_NULLABILITY_H_)
