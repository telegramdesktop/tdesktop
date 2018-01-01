/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

class HistoryService;

namespace HistoryLayout {

int WideChatWidth();

struct PaintContext {
	PaintContext(TimeMs ms, const QRect &clip, TextSelection selection)
		: ms(ms)
		, clip(clip)
		, selection(selection) {
	}
	TimeMs ms;
	const QRect &clip;
	TextSelection selection;
};

class ServiceMessagePainter {
public:
	static void paint(
		Painter &p,
		not_null<const HistoryService*> message,
		const PaintContext &context,
		int height);

	static void paintDate(Painter &p, const QDateTime &date, int y, int w);
	static void paintDate(Painter &p, const QString &dateText, int dateTextWidth, int y, int w);

	static void paintBubble(Painter &p, int x, int y, int w, int h);

private:
	static void paintComplexBubble(Painter &p, int left, int width, const Text &text, const QRect &textRect);
	static QVector<int> countLineWidths(const Text &text, const QRect &textRect);

};

void paintEmpty(Painter &p, int width, int height);

void serviceColorsUpdated();

void paintBubble(Painter &p, QRect rect, int outerWidth, bool selected, bool outbg, RectPart tailSide);

} // namespace HistoryLayout
