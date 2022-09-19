/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <any>

class Image;
class HistoryItem;
enum class ImageRoundRadius;

namespace Ui {
} // namespace Ui

namespace HistoryView {
struct ToPreviewOptions;
struct ItemPreviewImage;
struct ItemPreview;
} // namespace HistoryView

namespace Dialogs::Ui {

using namespace ::Ui;

[[nodiscard]] TextWithEntities DialogsPreviewText(TextWithEntities text);

class MessageView final {
public:
	MessageView();
	~MessageView();

	using ToPreviewOptions = HistoryView::ToPreviewOptions;
	using ItemPreviewImage = HistoryView::ItemPreviewImage;
	using ItemPreview = HistoryView::ItemPreview;

	void itemInvalidated(not_null<const HistoryItem*> item);
	[[nodiscard]] bool dependsOn(not_null<const HistoryItem*> item) const;

	[[nodiscard]] bool prepared(not_null<const HistoryItem*> item) const;
	void prepare(
		not_null<const HistoryItem*> item,
		Fn<void()> customEmojiRepaint,
		ToPreviewOptions options);
	void paint(
		Painter &p,
		const QRect &geometry,
		bool active,
		bool selected) const;

private:
	struct LoadingContext;

	mutable const HistoryItem *_textCachedFor = nullptr;
	mutable Ui::Text::String _senderCache;
	mutable Ui::Text::String _textCache;
	mutable std::vector<ItemPreviewImage> _imagesCache;
	mutable std::unique_ptr<LoadingContext> _loadingContext;

};

[[nodiscard]] HistoryView::ItemPreview PreviewWithSender(
	HistoryView::ItemPreview &&preview,
	const TextWithEntities &sender);

} // namespace Dialogs::Ui
