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
#include "lottie/lottie_single_player.h"
#include "ui/cached_round_corners.h"
#include "layout.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

constexpr auto kMaxForwardedBarLines = 4;

} // namespace

auto UnwrappedMedia::Content::stickerTakeLottie(
	not_null<DocumentData*> data,
	const Lottie::ColorReplacements *replacements)
-> std::unique_ptr<Lottie::SinglePlayer> {
	return nullptr;
}

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
		const auto reply = _parent->displayedReply();
		const auto forwarded = getDisplayedForwardedInfo();
		if (forwarded) {
			forwarded->create(via);
		}
		const auto additional = additionalWidth(via, reply, forwarded);
		maxWidth += additional;
		if (const auto surrounding = surroundingInfo(via, reply, forwarded, additional - st::msgReplyPadding.left())) {
			const auto infoHeight = st::msgDateImgPadding.y() * 2
				+ st::msgDateFont->height;
			const auto minimal = surrounding.height
				+ st::msgDateImgDelta
				+ infoHeight;
			minHeight = std::max(minHeight, minimal);
		}
		if (const auto size = _parent->rightActionSize()) {
			minHeight = std::max(
				minHeight,
				st::historyFastShareBottom + size->height());
		}
	}
	return { maxWidth, minHeight };
}

QSize UnwrappedMedia::countCurrentSize(int newWidth) {
	const auto item = _parent->data();
	accumulate_min(newWidth, maxWidth());
	if (_parent->media() == this) {
		const auto via = item->Get<HistoryMessageVia>();
		const auto reply = _parent->displayedReply();
		const auto forwarded = getDisplayedForwardedInfo();
		if (via || reply || forwarded) {
			int usew = maxWidth() - additionalWidth(via, reply, forwarded);
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
	if (_parent->hasOutLayout()
			&& !_parent->delegate()->elementIsChatWide()) {
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

	const auto rightAligned = _parent->hasOutLayout()
		&& !_parent->delegate()->elementIsChatWide();
	const auto inWebPage = (_parent->media() != this);
	const auto item = _parent->data();
	const auto via = inWebPage ? nullptr : item->Get<HistoryMessageVia>();
	const auto reply = inWebPage ? nullptr : _parent->displayedReply();
	const auto forwarded = inWebPage ? nullptr : getDisplayedForwardedInfo();
	auto usex = 0;
	auto usew = maxWidth();
	if (!inWebPage) {
		usew -= additionalWidth(via, reply, forwarded);
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
		drawSurrounding(p, inner, selected, via, reply, forwarded);
	}
}

UnwrappedMedia::SurroundingInfo UnwrappedMedia::surroundingInfo(
		const HistoryMessageVia *via,
		const HistoryMessageReply *reply,
		const HistoryMessageForwarded *forwarded,
		int outerw) const {
	if (!via && !reply && !forwarded) {
		return {};
	}
	auto height = st::msgReplyPadding.top() + st::msgReplyPadding.bottom();
	const auto innerw = outerw - st::msgReplyPadding.left() - st::msgReplyPadding.right();
	auto forwardedHeightReal = forwarded
		? forwarded->text.countHeight(innerw)
		: 0;
	auto forwardedHeight = std::min(
		forwardedHeightReal,
		kMaxForwardedBarLines * st::msgServiceNameFont->height);
	const auto breakEverywhere = (forwardedHeightReal > forwardedHeight);
	if (forwarded) {
		height += forwardedHeight;
	} else if (via) {
		height += st::msgServiceNameFont->height
			+ (reply ? st::msgReplyPadding.top() : 0);
	}
	if (reply) {
		height += st::msgReplyBarSize.height();
	}
	return { height, forwardedHeight, breakEverywhere };
}

void UnwrappedMedia::drawSurrounding(
		Painter &p,
		const QRect &inner,
		bool selected,
		const HistoryMessageVia *via,
		const HistoryMessageReply *reply,
		const HistoryMessageForwarded *forwarded) const {
	const auto rightAligned = _parent->hasOutLayout()
		&& !_parent->delegate()->elementIsChatWide();
	const auto rightActionSize = _parent->rightActionSize();
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
	auto rectw = width() - inner.width() - st::msgReplyPadding.left();
	if (const auto surrounding = surroundingInfo(via, reply, forwarded, rectw)) {
		auto recth = surrounding.height;
		int rectx = rightAligned ? 0 : (inner.width() + st::msgReplyPadding.left());
		int recty = 0;
		if (rtl()) rectx = width() - rectx - rectw;

		Ui::FillRoundRect(p, rectx, recty, rectw, recth, selected ? st::msgServiceBgSelected : st::msgServiceBg, selected ? Ui::StickerSelectedCorners : Ui::StickerCorners);
		p.setPen(st::msgServiceFg);
		rectx += st::msgReplyPadding.left();
		rectw -= st::msgReplyPadding.left() + st::msgReplyPadding.right();
		if (forwarded) {
			p.setTextPalette(st::serviceTextPalette);
			forwarded->text.drawElided(p, rectx, recty + st::msgReplyPadding.top(), rectw, kMaxForwardedBarLines, style::al_left, 0, -1, 0, surrounding.forwardedBreakEverywhere);
			p.restoreTextPalette();
		} else if (via) {
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
	if (rightActionSize) {
		const auto position = calculateFastActionPosition(
			fullBottom,
			replyRight,
			fullRight,
			*rightActionSize);
		const auto outer = 2 * inner.x() + inner.width();
		_parent->drawRightAction(p, position.x(), position.y(), outer);
	}
}

PointState UnwrappedMedia::pointState(QPoint point) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return PointState::Outside;
	}

	const auto rightAligned = _parent->hasOutLayout()
		&& !_parent->delegate()->elementIsChatWide();
	const auto inWebPage = (_parent->media() != this);
	const auto item = _parent->data();
	const auto via = inWebPage ? nullptr : item->Get<HistoryMessageVia>();
	const auto reply = inWebPage ? nullptr : _parent->displayedReply();
	const auto forwarded = inWebPage ? nullptr : getDisplayedForwardedInfo();
	auto usex = 0;
	auto usew = maxWidth();
	if (!inWebPage) {
		usew -= additionalWidth(via, reply, forwarded);
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

	const auto rightAligned = _parent->hasOutLayout()
		&& !_parent->delegate()->elementIsChatWide();
	const auto inWebPage = (_parent->media() != this);
	const auto item = _parent->data();
	const auto via = inWebPage ? nullptr : item->Get<HistoryMessageVia>();
	const auto reply = inWebPage ? nullptr : _parent->displayedReply();
	const auto forwarded = inWebPage ? nullptr : getDisplayedForwardedInfo();
	auto usex = 0;
	auto usew = maxWidth();
	if (!inWebPage) {
		usew -= additionalWidth(via, reply, forwarded);
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
		auto rectw = width() - inner.width() - st::msgReplyPadding.left();
		if (auto surrounding = surroundingInfo(via, reply, forwarded, rectw)) {
			auto recth = surrounding.height;
			int rectx = rightAligned ? 0 : (inner.width() + st::msgReplyPadding.left());
			int recty = 0;
			if (rtl()) rectx = width() - rectx - rectw;

			if (forwarded) {
				if (QRect(rectx, recty, rectw, st::msgReplyPadding.top() + surrounding.forwardedHeight).contains(point)) {
					auto textRequest = request.forText();
					if (surrounding.forwardedBreakEverywhere) {
						textRequest.flags |= Ui::Text::StateRequest::Flag::BreakEverywhere;
					}
					const auto innerw = rectw - st::msgReplyPadding.left() - st::msgReplyPadding.right();
					result = TextState(_parent, forwarded->text.getState(
						point - QPoint(rectx + st::msgReplyPadding.left(), recty + st::msgReplyPadding.top()),
						innerw,
						textRequest));
					result.symbol = 0;
					result.afterSymbol = false;
					if (surrounding.forwardedBreakEverywhere) {
						result.cursor = CursorState::Forwarded;
					} else {
						result.cursor = CursorState::None;
					}
					return result;
				}
				recty += surrounding.forwardedHeight;
				recth -= surrounding.forwardedHeight;
			} else if (via) {
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
		const auto rightActionSize = _parent->rightActionSize();
		auto fullBottom = height();
		if (_parent->pointInTime(fullRight, fullBottom, point, InfoDisplayType::Background)) {
			result.cursor = CursorState::Date;
		}
		if (rightActionSize) {
			const auto position = calculateFastActionPosition(
				fullBottom,
				replyRight,
				fullRight,
				*rightActionSize);
			if (QRect(position.x(), position.y(), rightActionSize->width(), rightActionSize->height()).contains(point)) {
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

std::unique_ptr<Lottie::SinglePlayer> UnwrappedMedia::stickerTakeLottie(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) {
	return _content->stickerTakeLottie(data, replacements);
}

int UnwrappedMedia::calculateFullRight(const QRect &inner) const {
	const auto rightAligned = _parent->hasOutLayout()
		&& !_parent->delegate()->elementIsChatWide();
	const auto infoWidth = _parent->infoWidth()
		+ st::msgDateImgPadding.x() * 2
		+ st::msgReplyPadding.left();
	const auto rightActionSize = _parent->rightActionSize();
	const auto rightActionWidth = rightActionSize
		? (st::historyFastShareLeft * 2
			+ rightActionSize->width()
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
		int fullRight,
		QSize size) const {
	const auto fastShareTop = (fullBottom
		- st::historyFastShareBottom
		- size.height());
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
		|| (_parent->rightActionSize())
		|| (_parent->isLastAndSelfMessage())
		|| (_parent->hasOutLayout()
			&& !_parent->delegate()->elementIsChatWide()
			&& _content->alwaysShowOutTimestamp());
}

int UnwrappedMedia::additionalWidth(
		const HistoryMessageVia *via,
		const HistoryMessageReply *reply,
		const HistoryMessageForwarded *forwarded) const {
	auto result = st::msgReplyPadding.left() + _parent->infoWidth() + 2 * st::msgDateImgPadding.x();
	if (forwarded) {
		accumulate_max(result, st::msgReplyPadding.left() + st::msgReplyPadding.left() + forwarded->text.maxWidth() + st::msgReplyPadding.right());
	} else if (via) {
		accumulate_max(result, st::msgReplyPadding.left() + st::msgReplyPadding.left() + via->maxWidth + st::msgReplyPadding.left());
	}
	if (reply) {
		accumulate_max(result, st::msgReplyPadding.left() + reply->replyToWidth());
	}
	return result;
}

auto UnwrappedMedia::getDisplayedForwardedInfo() const
-> const HistoryMessageForwarded * {
	return _content->hidesForwardedInfo()
		? nullptr
		: _parent->data()->Get<HistoryMessageForwarded>();
}

} // namespace HistoryView
