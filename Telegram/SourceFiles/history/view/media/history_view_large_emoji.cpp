/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_large_emoji.h"

#include "main/main_session.h"
#include "chat_helpers/stickers_emoji_pack.h"
#include "history/view/history_view_element.h"
#include "history/history_item.h"
#include "history/history.h"
#include "ui/image/image.h"
#include "data/data_file_origin.h"
#include "layout.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

using EmojiImage = Stickers::LargeEmojiImage;

auto ResolveImages(
	not_null<Main::Session*> session,
	const Ui::Text::IsolatedEmoji &emoji)
-> std::array<std::shared_ptr<EmojiImage>, Ui::Text::kIsolatedEmojiLimit> {
	const auto single = [&](EmojiPtr emoji) {
		return emoji ? session->emojiStickersPack().image(emoji) : nullptr;
	};
	return { {
		single(emoji.items[0]),
		single(emoji.items[1]),
		single(emoji.items[2]) } };
}

auto NonEmpty(const std::array<std::shared_ptr<EmojiImage>, Ui::Text::kIsolatedEmojiLimit> &images) {
	using namespace rpl::mappers;

	return images | ranges::view::filter(_1 != nullptr);
}

} // namespace

LargeEmoji::LargeEmoji(
	not_null<Element*> parent,
	const Ui::Text::IsolatedEmoji &emoji)
: _parent(parent)
, _images(ResolveImages(&parent->data()->history()->session(), emoji)) {
}

QSize LargeEmoji::size() {
	using namespace rpl::mappers;

	const auto count = ranges::distance(NonEmpty(_images));
	Assert(count > 0);

	const auto single = EmojiImage::Size() / cIntRetinaFactor();
	const auto skip = st::largeEmojiSkip - 2 * st::largeEmojiOutline;
	const auto inner = count * single.width() + (count - 1) * skip;
	const auto &padding = st::largeEmojiPadding;
	_size = QSize(
		padding.left() + inner + padding.right(),
		padding.top() + single.height() + padding.bottom());
	return _size;
}

void LargeEmoji::draw(Painter &p, const QRect &r, bool selected) {
	auto &&images = NonEmpty(_images);
	const auto &padding = st::largeEmojiPadding;
	auto x = r.x() + (r.width() - _size.width()) / 2 + padding.left();
	const auto y = r.y() + (r.height() - _size.height()) / 2 + padding.top();
	const auto skip = st::largeEmojiSkip - 2 * st::largeEmojiOutline;
	const auto size = EmojiImage::Size() / cIntRetinaFactor();
	for (const auto &image : images) {
		const auto w = size.width();
		if (const auto &prepared = image->image) {
			const auto h = size.height();
			const auto &c = st::msgStickerOverlay;
			const auto pixmap = selected
				? prepared->pixColored(c, w, h)
				: prepared->pix(w, h);
			p.drawPixmap(x, y, pixmap);
		} else if (image->load) {
			image->load();
		}
		x += w + skip;
	}
}

} // namespace HistoryView
