/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/media/history_media_sticker.h"

#include "layout.h"
#include "boxes/sticker_set_box.h"
#include "history/history_item_components.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "ui/image/image.h"
#include "ui/emoji_config.h"
#include "data/data_document.h"
#include "styles/style_history.h"

namespace {

using TextState = HistoryView::TextState;

} // namespace

HistorySticker::HistorySticker(
	not_null<Element*> parent,
	not_null<DocumentData*> document)
: HistoryMedia(parent)
, _data(document)
, _emoji(_data->sticker()->alt) {
	_data->thumb->load(parent->data()->fullId());
	if (auto emoji = Ui::Emoji::Find(_emoji)) {
		_emoji = emoji->text();
	}
}

QSize HistorySticker::countOptimalSize() {
	auto sticker = _data->sticker();

	if (!_packLink && sticker && sticker->set.type() != mtpc_inputStickerSetEmpty) {
		_packLink = std::make_shared<LambdaClickHandler>([document = _data] {
			StickerSetBox::Show(document);
		});
	}
	_pixw = _data->dimensions.width();
	_pixh = _data->dimensions.height();
	if (_pixw > st::maxStickerSize) {
		_pixh = (st::maxStickerSize * _pixh) / _pixw;
		_pixw = st::maxStickerSize;
	}
	if (_pixh > st::maxStickerSize) {
		_pixw = (st::maxStickerSize * _pixw) / _pixh;
		_pixh = st::maxStickerSize;
	}
	if (_pixw < 1) _pixw = 1;
	if (_pixh < 1) _pixh = 1;
	auto maxWidth = qMax(_pixw, st::minPhotoSize);
	auto minHeight = qMax(_pixh, st::minPhotoSize);
	accumulate_max(
		maxWidth,
		_parent->infoWidth() + 2 * st::msgDateImgPadding.x());
	if (_parent->media() == this) {
		maxWidth += additionalWidth();
	}
	return { maxWidth, minHeight };
}

QSize HistorySticker::countCurrentSize(int newWidth) {
	const auto item = _parent->data();
	accumulate_min(newWidth, maxWidth());
	if (_parent->media() == this) {
		auto via = item->Get<HistoryMessageVia>();
		auto reply = item->Get<HistoryMessageReply>();
		if (via || reply) {
			int usew = maxWidth() - additionalWidth(via, reply);
			int availw = newWidth - usew - st::msgReplyPadding.left() - st::msgReplyPadding.left() - st::msgReplyPadding.left();
			if (via) {
				via->resize(availw);
			}
			if (reply) {
				reply->resize(availw);
			}
		}
	}
	return { newWidth, minHeight() };
}

void HistorySticker::draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const {
	auto sticker = _data->sticker();
	if (!sticker) return;

	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	_data->checkSticker();
	bool loaded = _data->loaded();
	bool selected = (selection == FullSelection);

	auto outbg = _parent->hasOutLayout();
	auto inWebPage = (_parent->media() != this);

	const auto item = _parent->data();
	int usew = maxWidth(), usex = 0;
	auto via = inWebPage ? nullptr : item->Get<HistoryMessageVia>();
	auto reply = inWebPage ? nullptr : item->Get<HistoryMessageReply>();
	if (via || reply) {
		usew -= additionalWidth(via, reply);
		if (outbg) {
			usex = width() - usew;
		}
	}
	if (rtl()) usex = width() - usex - usew;

	const auto &pixmap = [&]() -> const QPixmap & {
		const auto o = item->fullId();
		const auto w = _pixw;
		const auto h = _pixh;
		const auto &c = st::msgStickerOverlay;
		if (const auto image = _data->getStickerImage()) {
			return selected
				? image->pixColored(o, c, w, h)
				: image->pix(o, w, h);
		}
		return selected
			? _data->thumb->pixBlurredColored(o, c, w, h)
			: _data->thumb->pixBlurred(o, w, h);
	}();
	p.drawPixmap(
		QPoint{ usex + (usew - _pixw) / 2, (minHeight() - _pixh) / 2 },
		pixmap);

	if (!inWebPage) {
		auto fullRight = usex + usew;
		auto fullBottom = height();
		if (needInfoDisplay()) {
			_parent->drawInfo(p, fullRight, fullBottom, usex * 2 + usew, selected, InfoDisplayType::Background);
		}
		if (via || reply) {
			int rectw = width() - usew - st::msgReplyPadding.left();
			int recth = st::msgReplyPadding.top() + st::msgReplyPadding.bottom();
			if (via) {
				recth += st::msgServiceNameFont->height + (reply ? st::msgReplyPadding.top() : 0);
			}
			if (reply) {
				recth += st::msgReplyBarSize.height();
			}
			int rectx = outbg ? 0 : (usew + st::msgReplyPadding.left());
			int recty = st::msgDateImgDelta;
			if (rtl()) rectx = width() - rectx - rectw;

			App::roundRect(p, rectx, recty, rectw, recth, selected ? st::msgServiceBgSelected : st::msgServiceBg, selected ? StickerSelectedCorners : StickerCorners);
			p.setPen(st::msgServiceFg);
			rectx += st::msgReplyPadding.left();
			rectw -= st::msgReplyPadding.left() + st::msgReplyPadding.right();
			if (via) {
				p.setFont(st::msgDateFont);
				p.drawTextLeft(rectx, recty + st::msgReplyPadding.top(), 2 * rectx + rectw, via->text);
				int skip = st::msgServiceNameFont->height + (reply ? st::msgReplyPadding.top() : 0);
				recty += skip;
			}
			if (reply) {
				HistoryMessageReply::PaintFlags flags = 0;
				if (selected) {
					flags |= HistoryMessageReply::PaintFlag::Selected;
				}
				reply->paint(p, _parent, rectx, recty, rectw, flags);
			}
		}
		if (_parent->displayRightAction()) {
			auto fastShareLeft = (fullRight + st::historyFastShareLeft);
			auto fastShareTop = (fullBottom - st::historyFastShareBottom - st::historyFastShareSize);
			_parent->drawRightAction(p, fastShareLeft, fastShareTop, 2 * usex + usew);
		}
	}
}

TextState HistorySticker::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return result;
	}

	auto outbg = _parent->hasOutLayout();
	auto inWebPage = (_parent->media() != this);

	const auto item = _parent->data();
	int usew = maxWidth(), usex = 0;
	auto via = inWebPage ? nullptr : item->Get<HistoryMessageVia>();
	auto reply = inWebPage ? nullptr : item->Get<HistoryMessageReply>();
	if (via || reply) {
		usew -= additionalWidth(via, reply);
		if (outbg) {
			usex = width() - usew;
		}
	}
	if (rtl()) usex = width() - usex - usew;

	if (via || reply) {
		int rectw = width() - usew - st::msgReplyPadding.left();
		int recth = st::msgReplyPadding.top() + st::msgReplyPadding.bottom();
		if (via) {
			recth += st::msgServiceNameFont->height + (reply ? st::msgReplyPadding.top() : 0);
		}
		if (reply) {
			recth += st::msgReplyBarSize.height();
		}
		int rectx = outbg ? 0 : (usew + st::msgReplyPadding.left());
		int recty = st::msgDateImgDelta;
		if (rtl()) rectx = width() - rectx - rectw;

		if (via) {
			int viah = st::msgReplyPadding.top() + st::msgServiceNameFont->height + (reply ? 0 : st::msgReplyPadding.bottom());
			if (QRect(rectx, recty, rectw, viah).contains(point)) {
				result.link = via->link;
				return result;
			}
			int skip = st::msgServiceNameFont->height + (reply ? 2 * st::msgReplyPadding.top() : 0);
			recty += skip;
			recth -= skip;
		}
		if (reply) {
			if (QRect(rectx, recty, rectw, recth).contains(point)) {
				result.link = reply->replyToLink();
				return result;
			}
		}
	}
	if (_parent->media() == this) {
		auto fullRight = usex + usew;
		auto fullBottom = height();
		if (_parent->pointInTime(fullRight, fullBottom, point, InfoDisplayType::Image)) {
			result.cursor = CursorState::Date;
		}
		if (_parent->displayRightAction()) {
			auto fastShareLeft = (fullRight + st::historyFastShareLeft);
			auto fastShareTop = (fullBottom - st::historyFastShareBottom - st::historyFastShareSize);
			if (QRect(fastShareLeft, fastShareTop, st::historyFastShareSize, st::historyFastShareSize).contains(point)) {
				result.link = _parent->rightActionLink();
			}
		}
	}

	auto pixLeft = usex + (usew - _pixw) / 2;
	auto pixTop = (minHeight() - _pixh) / 2;
	if (QRect(pixLeft, pixTop, _pixw, _pixh).contains(point)) {
		result.link = _packLink;
		return result;
	}
	return result;
}

bool HistorySticker::needInfoDisplay() const {
	return (_parent->data()->id < 0 || _parent->isUnderCursor());
}

int HistorySticker::additionalWidth(const HistoryMessageVia *via, const HistoryMessageReply *reply) const {
	int result = 0;
	if (via) {
		accumulate_max(result, st::msgReplyPadding.left() + st::msgReplyPadding.left() + via->maxWidth + st::msgReplyPadding.left());
	}
	if (reply) {
		accumulate_max(result, st::msgReplyPadding.left() + reply->replyToWidth());
	}
	return result;
}

int HistorySticker::additionalWidth() const {
	const auto item = _parent->data();
	return additionalWidth(
		item->Get<HistoryMessageVia>(),
		item->Get<HistoryMessageReply>());
}
