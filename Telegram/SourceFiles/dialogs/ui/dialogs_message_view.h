/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;

namespace Ui {

} // namespace Ui

namespace HistoryView {
struct ToPreviewOptions;
struct ItemPreview;
} // namespace HistoryView

namespace Dialogs::Ui {

using namespace ::Ui;

class MessageView final {
public:
	MessageView();
	~MessageView();

	using ToPreviewOptions = HistoryView::ToPreviewOptions;
	using ItemPreview = HistoryView::ItemPreview;

	void itemInvalidated(not_null<const HistoryItem*> item);
	[[nodiscard]] bool dependsOn(not_null<const HistoryItem*> item) const;

	void paint(
		Painter &p,
		not_null<const HistoryItem*> item,
		const QRect &geometry,
		bool active,
		bool selected,
		ToPreviewOptions options) const;

private:
	mutable const HistoryItem *_textCachedFor = nullptr;
	mutable Ui::Text::String _textCache;
	mutable std::vector<QImage> _imagesCache;

};

} // namespace Dialogs::Ui
