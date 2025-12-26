/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Platform {
namespace TextRecognition {

struct RectWithText {
	QString text;
	QRect rect;
};

struct Result {
	std::vector<RectWithText> items;
	bool success = false;

	inline operator bool() const {
		return success;
	}
};

[[nodiscard]] bool IsAvailable();
[[nodiscard]] Result RecognizeText(const QImage &image);

} // namespace TextRecognition
} // namespace Platform

// Platform dependent implementations.

#if defined Q_OS_WINRT || defined Q_OS_WIN
#include "platform/win/text_recognition_win.h"
#elif defined Q_OS_MAC // Q_OS_WINRT || Q_OS_WIN
#include "platform/mac/text_recognition_mac.h"
#else // Q_OS_WINRT || Q_OS_WIN || Q_OS_MAC
#include "platform/linux/text_recognition_linux.h"
#endif // else for Q_OS_WINRT || Q_OS_WIN || Q_OS_MAC