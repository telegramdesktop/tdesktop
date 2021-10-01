/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/dialogs_message_view.h"

#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "ui/text/text_options.h"
#include "ui/image/image.h"
#include "styles/style_dialogs.h"

namespace Dialogs::Ui {
namespace {

} // namespace

struct MessageView::LoadingContext {
	std::any context;
	rpl::lifetime lifetime;
};

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
		auto preview = item->toPreview(options);
		_textCache.setText(
			st::dialogsTextStyle,
			preview.text,
			DialogTextOptions());
		_textCachedFor = item;
		_imagesCache = std::move(preview.images);
		if (preview.loadingContext.has_value()) {
			if (!_loadingContext) {
				_loadingContext = std::make_unique<LoadingContext>();
				item->history()->session().downloaderTaskFinished(
				) | rpl::start_with_next([=] {
					_textCachedFor = nullptr;
				}, _loadingContext->lifetime);
			}
			_loadingContext->context = std::move(preview.loadingContext);
		} else {
			_loadingContext = nullptr;
		}
	}
	auto rect = geometry;
	for (const auto &image : _imagesCache) {
		if (rect.width() < st::dialogsMiniPreview) {
			break;
		}
		p.drawImage(rect.topLeft(), image);
		rect.setLeft(rect.x()
			+ st::dialogsMiniPreview
			+ st::dialogsMiniPreviewSkip);
	}
	if (!_imagesCache.empty()) {
		rect.setLeft(rect.x() + st::dialogsMiniPreviewRight);
	}
	if (rect.isEmpty()) {
		return;
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
		rect.left(),
		rect.top(),
		rect.width(),
		rect.height() / st::dialogsTextFont->height);
	p.restoreTextPalette();
}

} // namespace Dialogs::Ui
