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

class LargeEmoji final : public UnwrappedMedia::Content {
public:
	LargeEmoji(
		not_null<Element*> parent,
		const Ui::Text::IsolatedEmoji &emoji);

	QSize size() override;
	void draw(
		Painter &p,
		const PaintContext &context,
		const QRect &r) override;

	bool alwaysShowOutTimestamp() override {
		return true;
	}

private:
	const not_null<Element*> _parent;
	const std::array<
		std::shared_ptr<Stickers::LargeEmojiImage>,
		Ui::Text::kIsolatedEmojiLimit> _images;
	QSize _size;

};

} // namespace HistoryView
