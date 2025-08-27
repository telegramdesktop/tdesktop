/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_media_unwrapped.h"

#include "data/data_session.h"
#include "history/history.h"
#include "history/view/media/history_view_media_common.h"
#include "history/view/media/history_view_sticker.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_reply.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "lottie/lottie_single_player.h"
#include "ui/cached_round_corners.h"
#include "ui/chat/chat_style.h"
#include "ui/painter.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

constexpr auto kMaxForwardedBarLines = 4;

} // namespace

std::unique_ptr<StickerPlayer> UnwrappedMedia::Content::stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) {
	return nullptr;
}

QSize UnwrappedMedia::Content::countCurrentSize(int newWidth) {
	return countOptimalSize();
}

UnwrappedMedia::UnwrappedMedia(
	not_null<Element*> parent,
	std::unique_ptr<Content> content)
: Media(parent)
, _content(std::move(content)) {
}

QSize UnwrappedMedia::countOptimalSize() {
	_content->refreshLink();
	const auto optimal = _content->countOptimalSize();
	auto maxWidth = optimal.width();
	const auto minimal = std::max(st::emojiSize, st::msgPhotoSize);
	auto minHeight = std::max(optimal.height(), minimal);
	if (_parent->media() == this) {
		const auto item = _parent->data();
		const auto via = item->Get<HistoryMessageVia>();
		const auto reply = _parent->Get<Reply>();
		const auto topic = _parent->displayedTopicButton();
		const auto forwarded = getDisplayedForwardedInfo();
		if (forwarded) {
			forwarded->create(via, item);
		}
		maxWidth += additionalWidth(topic, reply, via, forwarded);
		accumulate_max(maxWidth, _parent->reactionsOptimalWidth());
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
	_contentSize = _content->countCurrentSize(newWidth);
	auto newHeight = std::max(minHeight(), _contentSize.height());
	_additionalOnTop = false;
	if (_parent->media() != this) {
		return { newWidth, newHeight };
	}
	if (_parent->hasRightLayout()) {
		// Add some height to isolated emoji for the timestamp info.
		const auto infoHeight = st::msgDateImgPadding.y() * 2
			+ st::msgDateFont->height;
		const auto minimal = std::min(
			st::largeEmojiSize + 2 * st::largeEmojiOutline,
			_contentSize.height());
		accumulate_max(newHeight, minimal + st::msgDateImgDelta + infoHeight);
	}
	accumulate_max(newWidth, _parent->reactionsOptimalWidth());
	_topAdded = 0;
	const auto via = item->Get<HistoryMessageVia>();
	const auto reply = _parent->Get<Reply>();
	const auto topic = _parent->displayedTopicButton();
	const auto forwarded = getDisplayedForwardedInfo();
	if (topic || via || reply || forwarded) {
		const auto additional = additionalWidth(topic, reply, via, forwarded);
		const auto optimalw = maxWidth() - additional;
		const auto additionalMinWidth = std::min(additional, st::msgReplyPadding.left() + st::msgMinWidth / 2);
		_additionalOnTop = (optimalw + additionalMinWidth) > newWidth;
		const auto surroundingWidth = _additionalOnTop
			? std::min(newWidth - st::msgReplyPadding.left(), additional)
			: (newWidth - _contentSize.width() - st::msgReplyPadding.left());
		if (reply) {
			[[maybe_unused]] auto h = reply->resizeToWidth(surroundingWidth);
		}
		const auto surrounding = surroundingInfo(topic, reply, via, forwarded, surroundingWidth);
		if (_additionalOnTop) {
			_topAdded = surrounding.height + st::msgMargin.bottom();
			newHeight += _topAdded;
		} else {
			const auto infoHeight = st::msgDateImgPadding.y() * 2
				+ st::msgDateFont->height;
			const auto minimal = surrounding.height
				+ st::msgDateImgDelta
				+ infoHeight;
			newHeight = std::max(newHeight, minimal);
		}
		const auto availw = newWidth
			- (_additionalOnTop ? 0 : optimalw + st::msgReplyPadding.left())
			- 2 * st::msgReplyPadding.left();
		if (via) {
			via->resize(availw);
		}
	}
	return { newWidth, newHeight };
}

void UnwrappedMedia::draw(Painter &p, const PaintContext &context) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return;
	}
	const auto rightAligned = _parent->hasRightLayout();
	const auto inWebPage = (_parent->media() != this);
	const auto item = _parent->data();
	auto usex = 0;
	auto usew = _contentSize.width();
	if (!inWebPage && rightAligned) {
		usex = width() - usew;
	}
	if (rtl()) {
		usex = width() - usex - usew;
	}

	const auto usey = rightAligned ? _topAdded : (height() - _contentSize.height());
	const auto useh = rightAligned
		? std::max(
			_contentSize.height(),
			(height()
				- _topAdded
				- st::msgDateImgPadding.y() * 2
				- st::msgDateFont->height))
		: _contentSize.height();
	const auto inner = QRect(usex, usey, usew, useh);
	if (context.skipDrawingParts != PaintContext::SkipDrawingParts::Content) {
		_content->draw(p, context, inner);
	}

	if (!inWebPage && (context.skipDrawingParts
			!= PaintContext::SkipDrawingParts::Surrounding)) {
		const auto via = inWebPage ? nullptr : item->Get<HistoryMessageVia>();
		const auto reply = inWebPage ? nullptr : _parent->Get<Reply>();
		const auto topic = inWebPage ? nullptr : _parent->displayedTopicButton();
		const auto forwarded = inWebPage ? nullptr : getDisplayedForwardedInfo();
		drawSurrounding(p, inner, context, topic, reply, via, forwarded);
	}
}

UnwrappedMedia::SurroundingInfo UnwrappedMedia::surroundingInfo(
		const TopicButton *topic,
		const Reply *reply,
		const HistoryMessageVia *via,
		const HistoryMessageForwarded *forwarded,
		int outerw) const {
	if (!topic && !via && !reply && !forwarded) {
		return {};
	}
	const auto innerw = outerw - st::msgReplyPadding.left() - st::msgReplyPadding.right();

	auto topicSize = QSize();
	if (topic) {
		const auto padding = st::topicButtonPadding;
		const auto height = padding.top()
			+ st::msgNameFont->height
			+ padding.bottom();
		const auto width = std::max(
			std::min(
				outerw,
				(st::msgReplyPadding.left()
					+ topic->name.maxWidth()
					+ st::topicButtonArrowSkip
					+ st::topicButtonPadding.right())),
			height);
		topicSize = { width, height };
	}
	auto panelHeight = 0;
	auto forwardedHeightReal = forwarded
		? forwarded->text.countHeight(innerw)
		: 0;
	auto forwardedHeight = std::min(
		forwardedHeightReal,
		kMaxForwardedBarLines * st::msgServiceNameFont->height);
	const auto breakEverywhere = (forwardedHeightReal > forwardedHeight);
	if (forwarded) {
		panelHeight += forwardedHeight;
	} else if (via) {
		panelHeight += st::msgServiceNameFont->height
			+ (reply ? st::msgReplyPadding.top() : 0);
	}
	if (panelHeight) {
		panelHeight += st::msgReplyPadding.top();
	}
	if (reply) {
		const auto replyMargins = reply->margins();
		panelHeight += reply->height()
			- ((forwarded || via) ? 0 : replyMargins.top())
			- replyMargins.bottom();
	} else {
		panelHeight += st::msgReplyPadding.bottom();
	}
	const auto total = (topicSize.isEmpty() ? 0 : topicSize.height())
		+ ((panelHeight || !topicSize.height()) ? st::topicButtonSkip : 0)
		+ panelHeight;
	return {
		.topicSize = topicSize,
		.height = total,
		.panelHeight = panelHeight,
		.forwardedHeight = forwardedHeight,
		.forwardedBreakEverywhere = breakEverywhere,
	};
}

void UnwrappedMedia::drawSurrounding(
		Painter &p,
		const QRect &inner,
		const PaintContext &context,
		const TopicButton *topic,
		const Reply *reply,
		const HistoryMessageVia *via,
		const HistoryMessageForwarded *forwarded) const {
	const auto st = context.st;
	const auto sti = context.imageStyle();
	const auto rightAligned = _parent->hasRightLayout();
	const auto rightActionSize = _parent->rightActionSize();
	const auto fullRight = calculateFullRight(inner);
	auto fullBottom = height();
	if (needInfoDisplay()) {
		_parent->drawInfo(
			p,
			context,
			fullRight,
			fullBottom,
			inner.x() * 2 + inner.width(),
			InfoDisplayType::Background);
	}
	auto replyLeft = 0;
	auto replyRight = 0;
	auto rectw = _additionalOnTop
		? std::min(width() - st::msgReplyPadding.left(), additionalWidth(topic, reply, via, forwarded))
		: (width() - inner.width() - st::msgReplyPadding.left());
	if (const auto surrounding = surroundingInfo(topic, reply, via, forwarded, rectw)) {
		auto recth = surrounding.panelHeight;
		if (!surrounding.topicSize.isEmpty()) {
			auto rectw = surrounding.topicSize.width();
			int rectx = _additionalOnTop
				? (rightAligned ? (inner.x() + inner.width() - rectw) : 0)
				: (rightAligned ? 0 : (inner.width() + st::msgReplyPadding.left()));
			int recty = 0;
			if (rtl()) rectx = width() - rectx - rectw;

			{
				auto hq = PainterHighQualityEnabler(p);
				p.setPen(Qt::NoPen);
				p.setBrush(sti->msgServiceBg);
				const auto recth = surrounding.topicSize.height();
				p.drawRoundedRect(
					QRect{ rectx, recty, rectw, recth },
					recth / 2,
					recth / 2);
			}

			p.setPen(st->msgServiceFg());
			rectx += st::msgReplyPadding.left();
			recty += st::topicButtonPadding.top();
			rectw -= st::msgReplyPadding.left() + st::topicButtonPadding.right() + st::topicButtonArrowSkip;
			p.setTextPalette(st->serviceTextPalette());
			topic->name.drawElided(p, rectx, recty, rectw);
			p.restoreTextPalette();

			const auto &icon = st::topicButtonArrow;
			icon.paint(
				p,
				rectx + rectw + st::topicButtonArrowPosition.x(),
				recty + st::topicButtonArrowPosition.y(),
				width(),
				st->msgServiceFg()->c);
		}
		if (recth) {
			int rectx = _additionalOnTop
				? (rightAligned ? (inner.x() + inner.width() - rectw) : 0)
				: (rightAligned ? 0 : (inner.width() + st::msgReplyPadding.left()));
			int recty = surrounding.height - recth;
			if (rtl()) rectx = width() - rectx - rectw;

			Ui::FillRoundRect(p, rectx, recty, rectw, recth, sti->msgServiceBg, sti->msgServiceBgCornersSmall);
			p.setPen(st->msgServiceFg());
			const auto textx = rectx + st::msgReplyPadding.left();
			const auto textw = rectw - st::msgReplyPadding.left() - st::msgReplyPadding.right();
			if (forwarded) {
				p.setTextPalette(st->serviceTextPalette());
				forwarded->text.drawElided(p, textx, recty + st::msgReplyPadding.top(), textw, kMaxForwardedBarLines, style::al_left, 0, -1, 0, surrounding.forwardedBreakEverywhere);
				p.restoreTextPalette();

				const auto skip = std::min(
					forwarded->text.countHeight(textw),
					kMaxForwardedBarLines * st::msgServiceNameFont->height);
				recty += skip;
			} else if (via) {
				p.setFont(st::msgDateFont);
				p.drawTextLeft(textx, recty + st::msgReplyPadding.top(), 2 * textx + textw, via->text);

				const auto skip = st::msgServiceNameFont->height
					+ (reply ? st::msgReplyPadding.top() : 0);
				recty += skip;
			}
			if (reply) {
				if (forwarded || via) {
					recty += st::msgReplyPadding.top();
					recth -= st::msgReplyPadding.top();
				} else {
					recty -= reply->margins().top();
				}
				reply->paint(p, _parent, context, rectx, recty, rectw, false);
			}
			replyLeft = rectx;
			replyRight = rectx + rectw;
		}
	}
	if (rightActionSize) {
		const auto position = calculateFastActionPosition(
			inner,
			rightAligned,
			replyLeft,
			replyRight,
			reply ? reply->height() : 0,
			fullBottom,
			fullRight,
			*rightActionSize);
		const auto outer = 2 * inner.x() + inner.width();
		_parent->drawRightAction(p, context, position.x(), position.y(), outer);
	}
}

PointState UnwrappedMedia::pointState(QPoint point) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return PointState::Outside;
	}

	const auto rightAligned = _parent->hasRightLayout();
	const auto inWebPage = (_parent->media() != this);
	auto usex = 0;
	auto usew = _contentSize.width();
	if (!inWebPage && rightAligned) {
		usex = width() - usew;
	}
	if (rtl()) {
		usex = width() - usex - usew;
	}

	const auto datey = height() - st::msgDateImgPadding.y() * 2
		- st::msgDateFont->height;
	const auto usey = rightAligned ? _topAdded : (height() - _contentSize.height());
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

	const auto rightAligned = _parent->hasRightLayout();
	const auto inWebPage = (_parent->media() != this);
	const auto item = _parent->data();
	auto usex = 0;
	auto usew = _contentSize.width();
	if (!inWebPage && rightAligned) {
		usex = width() - usew;
	}
	if (rtl()) {
		usex = width() - usex - usew;
	}

	const auto usey = rightAligned ? _topAdded : (height() - _contentSize.height());
	const auto useh = rightAligned
		? std::max(
			_contentSize.height(),
			height() - st::msgDateImgPadding.y() * 2 - st::msgDateFont->height)
		: _contentSize.height();
	const auto inner = QRect(usex, usey, usew, useh);

	if (_parent->media() == this) {
		const auto via = inWebPage ? nullptr : item->Get<HistoryMessageVia>();
		const auto reply = inWebPage ? nullptr : _parent->Get<Reply>();
		const auto topic = inWebPage ? nullptr : _parent->displayedTopicButton();
		const auto forwarded = inWebPage ? nullptr : getDisplayedForwardedInfo();
		auto replyLeft = 0;
		auto replyRight = 0;
		auto rectw = _additionalOnTop
			? std::min(width() - st::msgReplyPadding.left(), additionalWidth(topic, reply, via, forwarded))
			: (width() - inner.width() - st::msgReplyPadding.left());
		if (const auto surrounding = surroundingInfo(topic, reply, via, forwarded, rectw)) {
			auto recth = surrounding.panelHeight;
			if (!surrounding.topicSize.isEmpty()) {
				auto rectw = surrounding.topicSize.width();
				int rectx = _additionalOnTop
					? (rightAligned ? (inner.x() + inner.width() - rectw) : 0)
					: (rightAligned ? 0 : (inner.width() + st::msgReplyPadding.left()));
				int recty = 0;
				if (rtl()) rectx = width() - rectx - rectw;
				if (QRect(QPoint(rectx, recty), surrounding.topicSize).contains(point)) {
					result.link = topic->link;
					return result;
				}
			}
			if (recth) {
				int rectx = _additionalOnTop
					? (rightAligned ? (inner.x() + inner.width() - rectw) : 0)
					: (rightAligned ? 0 : (inner.width() + st::msgReplyPadding.left()));
				int recty = surrounding.height - recth;
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
					if (forwarded || via) {
						recty += st::msgReplyPadding.top();
						recth -= st::msgReplyPadding.top() + reply->margins().top();
					} else {
						recty -= reply->margins().top();
					}
					const auto replyRect = QRect(rectx, recty, rectw, recth);
					if (replyRect.contains(point)) {
						result.link = reply->link();
						reply->saveRipplePoint(point - replyRect.topLeft());
						reply->createRippleAnimation(_parent, replyRect.size());
					}
				}
				replyLeft = rectx;
				replyRight = rectx + rectw;
			}
		}
		const auto fullRight = calculateFullRight(inner);
		const auto rightActionSize = _parent->rightActionSize();
		auto fullBottom = height();
		const auto bottomInfoResult = _parent->bottomInfoTextState(
			fullRight,
			fullBottom,
			point,
			InfoDisplayType::Background);
		if (bottomInfoResult.link
			|| bottomInfoResult.cursor != CursorState::None
			|| bottomInfoResult.customTooltip) {
			return bottomInfoResult;
		}
		if (rightActionSize) {
			const auto position = calculateFastActionPosition(
				inner,
				rightAligned,
				replyLeft,
				replyRight,
				reply ? reply->height() : 0,
				fullBottom,
				fullRight,
				*rightActionSize);
			if (QRect(position.x(), position.y(), rightActionSize->width(), rightActionSize->height()).contains(point)) {
				result.link = _parent->rightActionLink(point - position);
				return result;
			}
		}
	}

	// Link of content can be nullptr (e.g. sticker without stickerpack).
	// So we have to process it to avoid overriding the previous result.
	if (_content->link() && inner.contains(point)) {
		result.link = _content->link();
		return result;
	}
	return result;
}

bool UnwrappedMedia::hasTextForCopy() const {
	return _content->hasTextForCopy();
}

bool UnwrappedMedia::dragItemByHandler(const ClickHandlerPtr &p) const {
	const auto reply = _parent->Get<Reply>();
	return !reply || (reply->link() != p);
}

QRect UnwrappedMedia::contentRectForReactions() const {
	const auto inWebPage = (_parent->media() != this);
	if (inWebPage) {
		return QRect(0, 0, width(), height());
	}
	const auto rightAligned = _parent->hasRightLayout();
	auto usex = 0;
	auto usew = _contentSize.width();
	accumulate_max(usew, _parent->reactionsOptimalWidth());
	if (rightAligned) {
		usex = width() - usew;
	}
	if (rtl()) {
		usex = width() - usex - usew;
	}
	const auto usey = rightAligned ? _topAdded : (height() - _contentSize.height());
	const auto useh = rightAligned
		? std::max(
			_contentSize.height(),
			height() - st::msgDateImgPadding.y() * 2 - st::msgDateFont->height)
		: _contentSize.height();
	return QRect(usex, usey, usew, useh);
}

std::optional<int> UnwrappedMedia::reactionButtonCenterOverride() const {
	const auto fullRight = calculateFullRight(contentRectForReactions());
	const auto right = fullRight
		- _parent->infoWidth()
		- st::msgDateImgPadding.x() * 2
		- st::msgReplyPadding.left();
	return right - st::reactionCornerSize.width() / 2;
}

QPoint UnwrappedMedia::resolveCustomInfoRightBottom() const {
	const auto inner = contentRectForReactions();
	const auto fullBottom = inner.y() + inner.height();
	const auto fullRight = calculateFullRight(inner);
	const auto skipx = st::msgDateImgPadding.x();
	const auto skipy = st::msgDateImgPadding.y();
	return QPoint(fullRight - skipx, fullBottom - skipy);
}

std::unique_ptr<StickerPlayer> UnwrappedMedia::stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) {
	return _content->stickerTakePlayer(data, replacements);
}

int UnwrappedMedia::calculateFullRight(const QRect &inner) const {
	const auto rightAligned = _parent->hasRightLayout();
	const auto infoWidth = _parent->infoWidth()
		+ st::msgDateImgPadding.x() * 2
		+ st::msgReplyPadding.left();
	const auto rightActionSize = _parent->rightActionSize();
	const auto rightSkip = st::msgPadding.left()
		+ (_parent->hasFromPhoto()
			? st::msgMargin.right()
			: st::msgPadding.right());
	const auto rightActionWidth = rightActionSize
		? (st::historyFastShareLeft * 2
			+ rightActionSize->width())
		: 0;
	auto fullRight = inner.x()
		+ inner.width()
		+ (rightAligned ? 0 : infoWidth);
	const auto rightActionSkip = rightAligned ? 0 : rightActionWidth;
	if (fullRight + rightActionSkip + rightSkip > _parent->width()) {
		fullRight = _parent->width()
			- (rightAligned ? 0 : rightActionSkip)
			- rightSkip;
	}
	return fullRight;
}

QPoint UnwrappedMedia::calculateFastActionPosition(
		QRect inner,
		bool rightAligned,
		int replyLeft,
		int replyRight,
		int replyHeight,
		int fullBottom,
		int fullRight,
		QSize size) const {
	const auto fastShareTop = (fullBottom
		- st::historyFastShareBottom
		- size.height());
	const auto doesRightActionHitReply = replyRight
		&& (fastShareTop < replyHeight);
	const auto fastShareLeft = rightAligned
		? ((doesRightActionHitReply ? replyLeft : inner.x())
			- size.width()
			- st::historyFastShareLeft)
		: ((doesRightActionHitReply ? replyRight : fullRight)
			+ st::historyFastShareLeft);
	return QPoint(fastShareLeft, fastShareTop);
}

bool UnwrappedMedia::needInfoDisplay() const {
	return _parent->data()->isSending()
		|| _parent->data()->hasFailed()
		|| _parent->isUnderCursor()
		|| _parent->rightActionSize()
		|| _parent->isLastAndSelfMessage()
		|| (_parent->delegate()->elementContext() == Context::ChatPreview)
		|| (_parent->hasRightLayout()
			&& _content->alwaysShowOutTimestamp());
}

int UnwrappedMedia::additionalWidth(
		const TopicButton *topic,
		const Reply *reply,
		const HistoryMessageVia *via,
		const HistoryMessageForwarded *forwarded) const {
	auto result = st::msgReplyPadding.left() + _parent->infoWidth() + 2 * st::msgDateImgPadding.x();
	if (topic) {
		accumulate_max(result, 2 * st::msgReplyPadding.left() + topic->name.maxWidth() + st::topicButtonArrowSkip + st::topicButtonPadding.right());
	}
	if (forwarded) {
		accumulate_max(result, 2 * st::msgReplyPadding.left() + forwarded->text.maxWidth() + st::msgReplyPadding.right());
	} else if (via) {
		accumulate_max(result, 2 * st::msgReplyPadding.left() + via->maxWidth + st::msgReplyPadding.right());
	}
	if (reply) {
		accumulate_max(result, reply->maxWidth());
	}
	return result;
}

auto UnwrappedMedia::getDisplayedForwardedInfo() const
-> const HistoryMessageForwarded * {
	return _parent->displayForwardedFrom()
		? _parent->data()->Get<HistoryMessageForwarded>()
		: nullptr;
}

} // namespace HistoryView
