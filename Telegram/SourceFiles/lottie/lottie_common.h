/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "base/variant.h"

#include <QSize>
#include <QColor>
#include <crl/crl_time.h>

namespace Lottie {

inline constexpr auto kTimeUnknown = std::numeric_limits<crl::time>::min();
inline constexpr auto kMaxFileSize = 2 * 1024 * 1024;

class Animation;

struct Information {
	int frameRate = 0;
	int framesCount = 0;
	QSize size;
};

enum class Error {
	ParseFailed,
	NotSupported,
};

struct FrameRequest {
	QSize box;
	std::optional<QColor> colored;

	[[nodiscard]] bool empty() const {
		return box.isEmpty();
	}
	[[nodiscard]] QSize size(const QSize &original) const;

	[[nodiscard]] bool operator==(const FrameRequest &other) const {
		return (box == other.box)
			&& (colored == other.colored);
	}
	[[nodiscard]] bool operator!=(const FrameRequest &other) const {
		return !(*this == other);
	}
};

enum class Quality : char {
	Default,
	High,
};

QByteArray ReadContent(const QByteArray &data, const QString &filepath);

} // namespace Lottie
