/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/history_view_element.h"

class HistoryService;

namespace Ui {
class ChatStyle;
struct CornersPixmaps;
} // namespace Ui

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
	void draw(Painter &p, const PaintContext &context) const override;
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

class ServiceMessagePainter {
public:
	static void PaintDate(
		Painter &p,
		not_null<const Ui::ChatStyle*> st,
		const QDateTime &date,
		int y,
		int w,
		bool chatWide);
	static void PaintDate(
		Painter &p,
		not_null<const Ui::ChatStyle*> st,
		const QString &dateText,
		int y,
		int w,
		bool chatWide);
	static void PaintDate(
		Painter &p,
		not_null<const Ui::ChatStyle*> st,
		const QString &dateText,
		int dateTextWidth,
		int y,
		int w,
		bool chatWide);
	static void PaintDate(
		Painter &p,
		const style::color &bg,
		const Ui::CornersPixmaps &corners,
		const style::color &fg,
		const QString &dateText,
		int dateTextWidth,
		int y,
		int w,
		bool chatWide);

	static void PaintBubble(
		Painter &p,
		not_null<const Ui::ChatStyle*> st,
		QRect rect);
	static void PaintBubble(
		Painter &p,
		const style::color &bg,
		const Ui::CornersPixmaps &corners,
		QRect rect);

	static void PaintComplexBubble(
		Painter &p,
		not_null<const Ui::ChatStyle*> st,
		int left,
		int width,
		const Ui::Text::String &text,
		const QRect &textRect);

private:
	static QVector<int> CountLineWidths(
		const Ui::Text::String &text,
		const QRect &textRect);

};

class EmptyPainter {
public:
	explicit EmptyPainter(not_null<History*> history);

	void paint(
		Painter &p,
		not_null<const Ui::ChatStyle*> st,
		int width,
		int height);

private:
	void fillAboutGroup();

	not_null<History*> _history;
	Ui::Text::String _header = { st::msgMinWidth };
	Ui::Text::String _text = { st::msgMinWidth };
	std::vector<Ui::Text::String> _phrases;

};

} // namespace HistoryView
