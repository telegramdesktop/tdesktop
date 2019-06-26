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
	QSize box;
	std::optional<QColor> colored;

	[[nodiscard]] bool empty() const {
		return box.isEmpty();
	}
	[[nodiscard]] QSize size(const QSize &original) const {
		Expects(!box.isEmpty());

		const auto result = original.scaled(box, Qt::KeepAspectRatio);
		return QSize(
			std::max(result.width(), 1),
			std::max(result.height(), 1));
	}

	[[nodiscard]] bool operator==(const FrameRequest &other) const {
		return (box == other.box)
			&& (colored == other.colored);
	}
	[[nodiscard]] bool operator!=(const FrameRequest &other) const {
		return !(*this == other);
	}
};

} // namespace Lottie
