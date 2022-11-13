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
} // namespace style

namespace Ui {
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
class TopicsView;
struct TopicJumpCorners;

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
	mutable std::unique_ptr<LoadingContext> _loadingContext;

};

[[nodiscard]] HistoryView::ItemPreview PreviewWithSender(
	HistoryView::ItemPreview &&preview,
	const QString &sender,
	TextWithEntities topic);

struct JumpToLastBg {
	not_null<const style::DialogRow*> st;
	not_null<TopicJumpCorners*> corners;
	QRect geometry;
	const style::color &bg;
	int width1 = 0;
	int width2 = 0;
};
void FillJumpToLastBg(QPainter &p, JumpToLastBg context);

} // namespace Dialogs::Ui
