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

constexpr auto kTimeUnknown = std::numeric_limits<crl::time>::min();

class Animation;

struct PlaybackOptions {
	float64 speed = 1.;
	bool loop = true;
};

struct Information {
	int frameRate = 0;
	int framesCount = 0;
	QSize size;
};

struct DisplayFrameRequest {
	crl::time time = 0;
};

struct Update {
	base::variant<
		Information,
		DisplayFrameRequest> data;
};

enum class Error {
	ParseFailed,
	NotSupported,
};

struct FrameRequest {
	QSize resize;
	std::optional<QColor> colored;
	bool strict = true;

	static FrameRequest NonStrict() {
		auto result = FrameRequest();
		result.strict = false;
		return result;
	}

	bool empty() const {
		return resize.isEmpty();
	}

	bool operator==(const FrameRequest &other) const {
		return (resize == other.resize)
			&& (colored == other.colored);
	}
	bool operator!=(const FrameRequest &other) const {
		return !(*this == other);
	}
};

} // namespace Lottie
