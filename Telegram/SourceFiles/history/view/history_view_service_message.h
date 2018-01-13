/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/history_view_element.h"

class HistoryService;

namespace HistoryView {

class Service : public Element {
public:
	Service(not_null<HistoryService*> data, Context context);

	void draw(
		Painter &p,
		QRect clip,
		TextSelection selection,
		TimeMs ms) const override;
	bool hasPoint(QPoint point) const override;
	HistoryTextState getState(
		QPoint point,
		HistoryStateRequest request) const override;
	void updatePressed(QPoint point) override;

private:
	not_null<HistoryService*> message() const;

	QRect countGeometry() const;

	QSize performCountOptimalSize() override;
	QSize performCountCurrentSize(int newWidth) override;

};

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
	static void paintDate(Painter &p, const QDateTime &date, int y, int w);
	static void paintDate(Painter &p, const QString &dateText, int dateTextWidth, int y, int w);

	static void paintBubble(Painter &p, int x, int y, int w, int h);

	static void paintComplexBubble(Painter &p, int left, int width, const Text &text, const QRect &textRect);

private:
	static QVector<int> countLineWidths(const Text &text, const QRect &textRect);

};

void paintEmpty(Painter &p, int width, int height);

void serviceColorsUpdated();

} // namespace HistoryView
