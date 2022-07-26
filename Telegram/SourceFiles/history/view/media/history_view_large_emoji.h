/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_media_unwrapped.h"
#include "ui/text/text_isolated_emoji.h"

namespace Data {
struct FileOrigin;
} // namespace Data

namespace Stickers {
struct LargeEmojiImage;
} // namespace Stickers

namespace HistoryView {

using LargeEmojiMedia = std::variant<
	v::null_t,
	std::shared_ptr<Stickers::LargeEmojiImage>,
	std::unique_ptr<Ui::Text::CustomEmoji>>;

class LargeEmoji final : public UnwrappedMedia::Content {
public:
	LargeEmoji(
		not_null<Element*> parent,
		const Ui::Text::IsolatedEmoji &emoji);
	~LargeEmoji();

	QSize size() override;
	void draw(
		Painter &p,
		const PaintContext &context,
		const QRect &r) override;

	bool alwaysShowOutTimestamp() override {
		return true;
	}
	bool hasTextForCopy() const override {
		return true;
	}

	bool hasHeavyPart() const override;
	void unloadHeavyPart() override;

private:
	void paintCustom(
		QPainter &p,
		int x,
		int y,
		not_null<Ui::Text::CustomEmoji*> emoji,
		const PaintContext &context,
		bool paused);

	const not_null<Element*> _parent;
	const std::array<LargeEmojiMedia, Ui::Text::kIsolatedEmojiLimit> _images;
	QImage _selectedFrame;
	QSize _size;
	bool _hasHeavyPart = false;

};

} // namespace HistoryView
