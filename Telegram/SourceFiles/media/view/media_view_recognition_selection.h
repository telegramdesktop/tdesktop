/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_text_recognition.h"

namespace Media::View {

struct RecognitionPosition {
	int item = -1;
	int character = 0;

	friend inline auto operator<=>(
		RecognitionPosition,
		RecognitionPosition) = default;
};

struct RecognitionSpan {
	int item = 0;
	int from = 0;
	int till = 0;
	bool rowStart = false;
};

class RecognitionSelection final {
public:
	using Result = Platform::TextRecognition::Result;

	void setSources(
		not_null<const Result*> result,
		not_null<const QImage*> image);

	[[nodiscard]] RecognitionPosition positionAt(
		QPoint position,
		QRect contentRect,
		int rotation,
		bool allowOutside) const;

	void start(RecognitionPosition position);
	bool updateFocus(RecognitionPosition position);
	[[nodiscard]] bool selecting() const {
		return _selecting;
	}
	void setSelecting(bool value) {
		_selecting = value;
	}
	[[nodiscard]] bool dragged() const {
		return _dragged;
	}
	void setDragged(bool value) {
		_dragged = value;
	}
	bool clear();
	[[nodiscard]] bool hasSelection() const;

	[[nodiscard]] std::vector<RecognitionSpan> spans() const;
	[[nodiscard]] QRect bandFor(int item, int from, int till) const;
	[[nodiscard]] QString selectedText() const;

private:
	[[nodiscard]] const std::vector<int> &charBounds(int index) const;
	[[nodiscard]] std::vector<int> inkBounds(QRect line, int length) const;

	const Result *_result = nullptr;
	const QImage *_image = nullptr;
	RecognitionPosition _anchor;
	RecognitionPosition _focus;
	bool _selecting = false;
	bool _dragged = false;
	mutable std::vector<std::vector<int>> _boundsCache;
	mutable qint64 _boundsKey = 0;

};

} // namespace Media::View
