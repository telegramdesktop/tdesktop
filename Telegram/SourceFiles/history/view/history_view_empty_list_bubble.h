/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace HistoryView {

class ListWidget;

class EmptyListBubbleWidget : public Ui::RpWidget {
public:
	EmptyListBubbleWidget(
		not_null<ListWidget*> parent,
		const style::margins &padding);

	void setText(const TextWithEntities &textWithEntities);
	void setForceWidth(int width);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void updateGeometry(const QSize &size);

	const style::margins &_padding;
	Ui::Text::String _text;
	int _innerWidth = 0;
	int _forceWidth = 0;

};

} // namespace HistoryView
