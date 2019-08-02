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
#include "styles/style_history.h"

namespace HistoryView {
namespace {

std::shared_ptr<Image> ResolveImage(
		not_null<Main::Session*> session,
		const Ui::Text::IsolatedEmoji &emoji) {
	return session->emojiStickersPack().image(emoji);
}

} // namespace

LargeEmoji::LargeEmoji(
	not_null<Element*> parent,
	Ui::Text::IsolatedEmoji emoji)
: _parent(parent)
, _emoji(emoji)
, _image(ResolveImage(&parent->data()->history()->session(), emoji)) {
}

QSize LargeEmoji::size() {
	const auto size = _image->size() / cIntRetinaFactor();
	const auto &padding = st::largeEmojiPadding;
	_size = QSize(
		padding.left() + size.width() + padding.right(),
		padding.top() + size.height() + padding.bottom());
	return _size;
}

void LargeEmoji::draw(Painter &p, const QRect &r, bool selected) {
	_image->load(Data::FileOrigin());
	if (!_image->loaded()) {
		return;
	}
	const auto &padding = st::largeEmojiPadding;
	const auto o = Data::FileOrigin();
	const auto w = _size.width() - padding.left() - padding.right();
	const auto h = _size.height() - padding.top() - padding.bottom();
	const auto &c = st::msgStickerOverlay;
	const auto pixmap = selected
		? _image->pixColored(o, c, w, h)
		: _image->pix(o, w, h);
	p.drawPixmap(
		QPoint(
			r.x() + (r.width() - _size.width()) / 2,
			r.y() + (r.height() - _size.height()) / 2),
		pixmap);
}

} // namespace HistoryView
