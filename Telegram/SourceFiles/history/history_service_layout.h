/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
