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

namespace style {
struct DialogRow;
struct DialogsMiniIcon;
} // namespace style

namespace Ui {
class SpoilerAnimation;
} // namespace Ui

namespace Data {
class Forum;
} // namespace Data

namespace HistoryView {
struct ToPreviewOptions;
struct ItemPreviewImage;
struct ItemPreview;
} // namespace HistoryView

namespace Dialogs::Ui {

using namespace ::Ui;

struct PaintContext;
struct TopicJumpCache;
class TopicsView;

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

	[[nodiscard]] bool prepared(
		not_null<const HistoryItem*> item,
		Data::Forum *forum) const;
	void prepare(
		not_null<const HistoryItem*> item,
		Data::Forum *forum,
		Fn<void()> customEmojiRepaint,
		ToPreviewOptions options);

	void paint(
		Painter &p,
		const QRect &geometry,
		const PaintContext &context) const;

	[[nodiscard]] bool isInTopicJump(int x, int y) const;
	void addTopicJumpRipple(
		QPoint origin,
		not_null<TopicJumpCache*> topicJumpCache,
		Fn<void()> updateCallback);
	void stopLastRipple();
	void clearRipple();

private:
	struct LoadingContext;

	[[nodiscard]] int countWidth() const;
	void paintJumpToLast(
		Painter &p,
		const QRect &rect,
		const PaintContext &context,
		int width1) const;

	mutable const HistoryItem *_textCachedFor = nullptr;
	mutable Text::String _senderCache;
	mutable std::unique_ptr<TopicsView> _topics;
	mutable Text::String _textCache;
	mutable std::vector<ItemPreviewImage> _imagesCache;
	mutable std::unique_ptr<SpoilerAnimation> _spoiler;
	mutable std::unique_ptr<LoadingContext> _loadingContext;
	mutable const style::DialogsMiniIcon *_leftIcon = nullptr;
	mutable bool _hasPlainLinkAtBegin = false;

};

[[nodiscard]] HistoryView::ItemPreview PreviewWithSender(
	HistoryView::ItemPreview &&preview,
	const QString &sender,
	TextWithEntities topic);

} // namespace Dialogs::Ui
