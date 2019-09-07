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
	auto maxWidth = _contentSize.width();
	const auto minimal = st::largeEmojiSize + 2 * st::largeEmojiOutline;
	auto minHeight = std::max(_contentSize.height(), minimal);
	if (_parent->media() == this) {
		const auto item = _parent->data();
		const auto via = item->Get<HistoryMessageVia>();
		const auto reply = item->Get<HistoryMessageReply>();
		maxWidth += additionalWidth(via, reply);
		if (const auto surrounding = surroundingHeight(via, reply)) {
			const auto infoHeight = st::msgDateImgPadding.y() * 2
				+ st::msgDateFont->height;
			const auto minimal = surrounding
				+ st::msgDateImgDelta
				+ infoHeight;
			minHeight = std::max(minHeight, minimal);
		}
	}
	return { maxWidth, minHeight };
}

QSize UnwrappedMedia::countCurrentSize(int newWidth) {
	const auto item = _parent->data();
	accumulate_min(newWidth, maxWidth());
	if (_parent->media() == this) {
		const auto infoWidth = _parent->infoWidth() + 2 * st::msgDateImgPadding.x();
		const auto via = item->Get<HistoryMessageVia>();
		const auto reply = item->Get<HistoryMessageReply>();
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
	auto newHeight = minHeight();
	if (_parent->hasOutLayout() && !Adaptive::ChatWide()) {
		// Add some height to isolated emoji for the timestamp info.
		const auto infoHeight = st::msgDateImgPadding.y() * 2
			+ st::msgDateFont->height;
		const auto minimal = st::largeEmojiSize
			+ 2 * st::largeEmojiOutline
			+ (st::msgDateImgDelta + infoHeight);
		accumulate_max(newHeight, minimal);
	}
	return { newWidth, newHeight };
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

	const auto rightAligned = _parent->hasOutLayout() && !Adaptive::ChatWide();
	const auto inWebPage = (_parent->media() != this);
	const auto item = _parent->data();
	const auto via = inWebPage ? nullptr : item->Get<HistoryMessageVia>();
	const auto reply = inWebPage ? nullptr : item->Get<HistoryMessageReply>();
	auto usex = 0;
	auto usew = maxWidth();
	if (!inWebPage) {
		usew -= additionalWidth(via, reply);
		if (rightAligned) {
			usex = width() - usew;
		}
	}
	if (rtl()) {
		usex = width() - usex - usew;
	}

	const auto usey = rightAligned ? 0 : (height() - _contentSize.height());
	const auto useh = rightAligned
		? std::max(
			_contentSize.height(),
			height() - st::msgDateImgPadding.y() * 2 - st::msgDateFont->height)
		: _contentSize.height();
	const auto inner = QRect(usex, usey, usew, useh);
	_content->draw(p, inner, selected);

	if (!inWebPage) {
		drawSurrounding(p, inner, selected, via, reply);
	}
}

int UnwrappedMedia::surroundingHeight(
		const HistoryMessageVia *via,
		const HistoryMessageReply *reply) const {
	if (!via && !reply) {
		return 0;
	}
	auto result = st::msgReplyPadding.top() + st::msgReplyPadding.bottom();
	if (via) {
		result += st::msgServiceNameFont->height
			+ (reply ? st::msgReplyPadding.top() : 0);
	}
	if (reply) {
		result += st::msgReplyBarSize.height();
	}
	return result;
}

void UnwrappedMedia::drawSurrounding(
		Painter &p,
		const QRect &inner,
		bool selected,
		const HistoryMessageVia *via,
		const HistoryMessageReply *reply) const {
	const auto rightAligned = _parent->hasOutLayout() && !Adaptive::ChatWide();
	const auto rightAction = _parent->displayRightAction();
	const auto fullRight = calculateFullRight(inner);
	auto fullBottom = height();
	if (needInfoDisplay()) {
		_parent->drawInfo(
			p,
			fullRight,
			fullBottom,
			inner.x() * 2 + inner.width(),
			selected,
			InfoDisplayType::Background);
	}
	auto replyRight = 0;
	if (const auto recth = surroundingHeight(via, reply)) {
		int rectw = width() - inner.width() - st::msgReplyPadding.left();
		int rectx = rightAligned ? 0 : (inner.width() + st::msgReplyPadding.left());
		int recty = 0;
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
		replyRight = rectx + rectw;
	}
	if (rightAction) {
		const auto position = calculateFastActionPosition(
			fullBottom,
			replyRight,
			fullRight);
		const auto outer = 2 * inner.x() + inner.width();
		_parent->drawRightAction(p, position.x(), position.y(), outer);
	}
}

PointState UnwrappedMedia::pointState(QPoint point) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return PointState::Outside;
	}

	const auto rightAligned = _parent->hasOutLayout() && !Adaptive::ChatWide();
	const auto inWebPage = (_parent->media() != this);
	const auto item = _parent->data();
	const auto via = inWebPage ? nullptr : item->Get<HistoryMessageVia>();
	const auto reply = inWebPage ? nullptr : item->Get<HistoryMessageReply>();
	auto usex = 0;
	auto usew = maxWidth();
	if (!inWebPage) {
		usew -= additionalWidth(via, reply);
		if (rightAligned) {
			usex = width() - usew;
		}
	}
	if (rtl()) {
		usex = width() - usex - usew;
	}

	const auto datey = height() - st::msgDateImgPadding.y() * 2
		- st::msgDateFont->height;
	const auto usey = rightAligned ? 0 : (height() - _contentSize.height());
	const auto useh = rightAligned
		? std::max(_contentSize.height(), datey)
		: _contentSize.height();
	const auto inner = QRect(usex, usey, usew, useh);

	// Rectangle of date bubble.
	if (point.x() < calculateFullRight(inner) && point.y() > datey) {
		return PointState::Inside;
	}

	return inner.contains(point) ? PointState::Inside : PointState::Outside;
}

TextState UnwrappedMedia::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return result;
	}

	const auto rightAligned = _parent->hasOutLayout() && !Adaptive::ChatWide();
	const auto inWebPage = (_parent->media() != this);
	const auto item = _parent->data();
	const auto via = inWebPage ? nullptr : item->Get<HistoryMessageVia>();
	const auto reply = inWebPage ? nullptr : item->Get<HistoryMessageReply>();
	auto usex = 0;
	auto usew = maxWidth();
	if (!inWebPage) {
		usew -= additionalWidth(via, reply);
		if (rightAligned) {
			usex = width() - usew;
		}
	}
	if (rtl()) {
		usex = width() - usex - usew;
	}

	const auto usey = rightAligned ? 0 : (height() - _contentSize.height());
	const auto useh = rightAligned
		? std::max(
			_contentSize.height(),
			height() - st::msgDateImgPadding.y() * 2 - st::msgDateFont->height)
		: _contentSize.height();
	const auto inner = QRect(usex, usey, usew, useh);

	if (_parent->media() == this) {
		auto replyRight = 0;
		if (auto recth = surroundingHeight(via, reply)) {
			int rectw = width() - inner.width() - st::msgReplyPadding.left();
			int rectx = rightAligned ? 0 : (inner.width() + st::msgReplyPadding.left());
			int recty = 0;
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
			replyRight = rectx + rectw - st::msgReplyPadding.right();
		}
		const auto fullRight = calculateFullRight(inner);
		const auto rightAction = _parent->displayRightAction();
		auto fullBottom = height();
		if (_parent->pointInTime(fullRight, fullBottom, point, InfoDisplayType::Background)) {
			result.cursor = CursorState::Date;
		}
		if (rightAction) {
			const auto size = st::historyFastShareSize;
			const auto position = calculateFastActionPosition(
				fullBottom,
				replyRight,
				fullRight);
			if (QRect(position.x(), position.y(), size, size).contains(point)) {
				result.link = _parent->rightActionLink();
				return result;
			}
		}
	}

	auto pixLeft = usex + (usew - _contentSize.width()) / 2;
	auto pixTop = (minHeight() - _contentSize.height()) / 2;
	// Link of content can be nullptr (e.g. sticker without stickerpack).
	// So we have to process it to avoid overriding the previous result.
	if (_content->link()
		&& QRect({ pixLeft, pixTop }, _contentSize).contains(point)) {
		result.link = _content->link();
		return result;
	}
	return result;
}

int UnwrappedMedia::calculateFullRight(const QRect &inner) const {
	const auto rightAligned = _parent->hasOutLayout() && !Adaptive::ChatWide();
	const auto infoWidth = _parent->infoWidth()
		+ st::msgDateImgPadding.x() * 2
		+ st::msgReplyPadding.left();
	const auto rightActionWidth = _parent->displayRightAction()
		? (st::historyFastShareLeft * 2
			+ st::historyFastShareSize
			+ st::msgPadding.left()
			+ (_parent->hasFromPhoto()
				? st::msgMargin.right()
				: st::msgPadding.right()))
		: 0;
	auto fullRight = inner.x()
		+ inner.width()
		+ (rightAligned ? 0 : infoWidth);
	if (fullRight + rightActionWidth > _parent->width()) {
		fullRight = _parent->width() - rightActionWidth;
	}
	return fullRight;
}

QPoint UnwrappedMedia::calculateFastActionPosition(
	int fullBottom,
	int replyRight,
	int fullRight) const {
	const auto size = st::historyFastShareSize;
	const auto fastShareTop = (fullBottom
		- st::historyFastShareBottom
		- size);
	const auto doesRightActionHitReply = replyRight && (fastShareTop <
		st::msgReplyBarSize.height()
		+ st::msgReplyPadding.top()
		+ st::msgReplyPadding.bottom());
	const auto fastShareLeft = ((doesRightActionHitReply
		? replyRight
		: fullRight) + st::historyFastShareLeft);
	return QPoint(fastShareLeft, fastShareTop);
}

bool UnwrappedMedia::needInfoDisplay() const {
	return (_parent->data()->id < 0)
		|| (_parent->isUnderCursor())
		|| (_parent->displayRightAction())
		|| (_parent->hasOutLayout()
			&& !Adaptive::ChatWide()
			&& _content->alwaysShowOutTimestamp());
}

int UnwrappedMedia::additionalWidth(const HistoryMessageVia *via, const HistoryMessageReply *reply) const {
	auto result = st::msgReplyPadding.left() + _parent->infoWidth() + 2 * st::msgDateImgPadding.x();
	if (via) {
		accumulate_max(result, st::msgReplyPadding.left() + st::msgReplyPadding.left() + via->maxWidth + st::msgReplyPadding.left());
	}
	if (reply) {
		accumulate_max(result, st::msgReplyPadding.left() + reply->replyToWidth());
	}
	return result;
}

} // namespace HistoryView
