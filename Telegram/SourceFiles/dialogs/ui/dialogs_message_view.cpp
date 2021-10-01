/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/dialogs_message_view.h"

#include "history/history_item.h"
#include "ui/text/text_options.h"
#include "styles/style_dialogs.h"

namespace Dialogs::Ui {
namespace {

} // namespace

MessageView::MessageView()
: _textCache(st::dialogsTextWidthMin) {
}

MessageView::~MessageView() = default;

void MessageView::itemInvalidated(not_null<const HistoryItem*> item) {
	if (_textCachedFor == item.get()) {
		_textCachedFor = nullptr;
	}
}

bool MessageView::dependsOn(not_null<const HistoryItem*> item) const {
	return (_textCachedFor == item.get());
}

void MessageView::paint(
		Painter &p,
		not_null<const HistoryItem*> item,
		const QRect &geometry,
		bool active,
		bool selected,
		ToPreviewOptions options) const {
	if (geometry.isEmpty()) {
		return;
	}
	if (_textCachedFor != item.get()) {
		const auto preview = item->toPreview(options);
		_textCache.setText(
			st::dialogsTextStyle,
			preview.text,
			DialogTextOptions());
		_textCachedFor = item;
	}
	p.setTextPalette(active
		? st::dialogsTextPaletteActive
		: selected
		? st::dialogsTextPaletteOver
		: st::dialogsTextPalette);
	p.setFont(st::dialogsTextFont);
	p.setPen(active ? st::dialogsTextFgActive : (selected ? st::dialogsTextFgOver : st::dialogsTextFg));
	_textCache.drawElided(
		p,
		geometry.left(),
		geometry.top(),
		geometry.width(),
		geometry.height() / st::dialogsTextFont->height);
	p.restoreTextPalette();
}

} // namespace Dialogs::Ui
