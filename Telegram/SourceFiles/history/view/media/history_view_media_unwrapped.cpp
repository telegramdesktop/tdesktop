/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_media_unwrapped.h"

#include "history/view/media/history_view_media_common.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "layout.h"
#include "styles/style_history.h"

namespace HistoryView {

UnwrappedMedia::Content::~Content() = default;

UnwrappedMedia::UnwrappedMedia(
	not_null<Element*> parent,
	std::unique_ptr<Content> content)
: Media(parent)
, _content(std::move(content)) {
}

QSize UnwrappedMedia::countOptimalSize() {
	_content->refreshLink();
	_contentSize = NonEmptySize(DownscaledSize(
		_content->size(),
		{ st::maxStickerSize, st::maxStickerSize }));
	auto maxWidth = std::max(_contentSize.width(), st::minPhotoSize);
	auto minHeight = std::max(_contentSize.height(), st::minPhotoSize);
	accumulate_max(
		maxWidth,
		_parent->infoWidth() + 2 * st::msgDateImgPadding.x());
	if (_parent->media() == this) {
		maxWidth += additionalWidth();
	}
	return { maxWidth, minHeight };
}

QSize UnwrappedMedia::countCurrentSize(int newWidth) {
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

void UnwrappedMedia::draw(
		Painter &p,
		const QRect &r,
		TextSelection selection,
		crl::time ms) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return;
	}
	bool selected = (selection == FullSelection);

	const auto outbg = _parent->hasOutLayout();
	const auto inWebPage = (_parent->media() != this);

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

	const auto inner = QRect(usex, 0, usew, height());
	_content->draw(p, inner, selected);

	if (!inWebPage) {
		drawSurrounding(p, inner, selected, via, reply);
	}
}

void UnwrappedMedia::drawSurrounding(
		Painter &p,
		const QRect &inner,
		bool selected,
		const HistoryMessageVia *via,
		const HistoryMessageReply *reply) const {
	auto fullRight = inner.x() + inner.width();
	auto fullBottom = inner.y() + inner.height();
	if (needInfoDisplay()) {
		_parent->drawInfo(
			p,
			fullRight,
			fullBottom,
			inner.x() * 2 + inner.width(),
			selected,
			InfoDisplayType::Background);
	}
	if (via || reply) {
		int rectw = width() - inner.width() - st::msgReplyPadding.left();
		int recth = st::msgReplyPadding.top() + st::msgReplyPadding.bottom();
		if (via) {
			recth += st::msgServiceNameFont->height + (reply ? st::msgReplyPadding.top() : 0);
		}
		if (reply) {
			recth += st::msgReplyBarSize.height();
		}
		const auto outbg = _parent->hasOutLayout();
		int rectx = outbg ? 0 : (inner.width() + st::msgReplyPadding.left());
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
		_parent->drawRightAction(p, fastShareLeft, fastShareTop, 2 * inner.x() + inner.width());
	}

}

TextState UnwrappedMedia::textState(QPoint point, StateRequest request) const {
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

	auto pixLeft = usex + (usew - _contentSize.width()) / 2;
	auto pixTop = (minHeight() - _contentSize.height()) / 2;
	if (QRect({ pixLeft, pixTop }, _contentSize).contains(point)) {
		result.link = _content->link();
		return result;
	}
	return result;
}

bool UnwrappedMedia::needInfoDisplay() const {
	return (_parent->data()->id < 0 || _parent->isUnderCursor());
}

int UnwrappedMedia::additionalWidth(const HistoryMessageVia *via, const HistoryMessageReply *reply) const {
	int result = 0;
	if (via) {
		accumulate_max(result, st::msgReplyPadding.left() + st::msgReplyPadding.left() + via->maxWidth + st::msgReplyPadding.left());
	}
	if (reply) {
		accumulate_max(result, st::msgReplyPadding.left() + reply->replyToWidth());
	}
	return result;
}

int UnwrappedMedia::additionalWidth() const {
	const auto item = _parent->data();
	return additionalWidth(
		item->Get<HistoryMessageVia>(),
		item->Get<HistoryMessageReply>());
}

} // namespace HistoryView
