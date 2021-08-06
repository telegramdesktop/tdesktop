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
	Service(
		not_null<ElementDelegate*> delegate,
		not_null<HistoryService*> data,
		Element *replacing);

	int marginTop() const override;
	int marginBottom() const override;
	bool isHidden() const override;
	void draw(
		Painter &p,
		QRect clip,
		TextSelection selection,
		crl::time ms) const override;
	PointState pointState(QPoint point) const override;
	TextState textState(
		QPoint point,
		StateRequest request) const override;
	void updatePressed(QPoint point) override;
	TextForMimeData selectedText(TextSelection selection) const override;
	TextSelection adjustSelection(
		TextSelection selection,
		TextSelectType type) const override;

private:
	not_null<HistoryService*> message() const;

	QRect countGeometry() const;

	QSize performCountOptimalSize() override;
	QSize performCountCurrentSize(int newWidth) override;

};

int WideChatWidth();

struct PaintContext {
	PaintContext(crl::time ms, const QRect &clip, TextSelection selection)
		: ms(ms)
		, clip(clip)
		, selection(selection) {
	}
	crl::time ms;
	const QRect &clip;
	TextSelection selection;
};

class ServiceMessagePainter {
public:
	static void paintDate(
		Painter &p,
		const QDateTime &date,
		int y,
		int w,
		bool chatWide,
		const style::color &bg = st::msgServiceBg,
		const style::color &fg = st::msgServiceFg);
	static void paintDate(
		Painter &p,
		const QString &dateText,
		int y,
		int w,
		bool chatWide,
		const style::color &bg = st::msgServiceBg,
		const style::color &fg = st::msgServiceFg);
	static void paintDate(
		Painter &p,
		const QString &dateText,
		int dateTextWidth,
		int y,
		int w,
		bool chatWide,
		const style::color &bg = st::msgServiceBg,
		const style::color &fg = st::msgServiceFg);

	static void paintBubble(
		Painter &p,
		int x,
		int y,
		int w,
		int h,
		const style::color &bg = st::msgServiceBg);

	static void paintComplexBubble(
		Painter &p,
		int left,
		int width,
		const Ui::Text::String &text,
		const QRect &textRect,
		const style::color &bg = st::msgServiceBg);

private:
	static QVector<int> countLineWidths(const Ui::Text::String &text, const QRect &textRect);

};

class EmptyPainter {
public:
	explicit EmptyPainter(not_null<History*> history);

	void paint(Painter &p, int width, int height);

private:
	void fillAboutGroup();

	not_null<History*> _history;
	Ui::Text::String _header = { st::msgMinWidth };
	Ui::Text::String _text = { st::msgMinWidth };
	std::vector<Ui::Text::String> _phrases;

};

} // namespace HistoryView
