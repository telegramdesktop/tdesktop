/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
struct ChatPaintContext;
} // namespace Ui

namespace HistoryView {

using PaintContext = Ui::ChatPaintContext;

class TranscribeButton final {
public:
	explicit TranscribeButton(not_null<HistoryItem*> item);

	[[nodiscard]] QSize size() const;

	void paint(QPainter &p, int x, int y, const PaintContext &context);

	[[nodiscard]] ClickHandlerPtr link();

private:
	const not_null<HistoryItem*> _item;

	ClickHandlerPtr _link;
	QString _text;
	bool _loaded = false;
	bool _loading = false;

};

} // namespace HistoryView
