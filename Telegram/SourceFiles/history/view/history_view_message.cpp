/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_message.h"

#include "history/view/history_view_cursor_state.h"
#include "history/history_item_components.h"
#include "history/history_message.h"
#include "history/history_media_types.h"
#include "history/history_media.h"
#include "history/history.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "auth_session.h"
#include "layout.h"
#include "styles/style_widgets.h"
#include "styles/style_history.h"
#include "styles/style_dialogs.h"

namespace HistoryView {
namespace {

class KeyboardStyle : public ReplyKeyboard::Style {
public:
	using ReplyKeyboard::Style::Style;

	int buttonRadius() const override;

	void startPaint(Painter &p) const override;
	const style::TextStyle &textStyle() const override;
	void repaint(not_null<const HistoryItem*> item) const override;

protected:
	void paintButtonBg(
		Painter &p,
		const QRect &rect,
		float64 howMuchOver) const override;
	void paintButtonIcon(Painter &p, const QRect &rect, int outerWidth, HistoryMessageMarkupButton::Type type) const override;
	void paintButtonLoading(Painter &p, const QRect &rect) const override;
	int minButtonWidth(HistoryMessageMarkupButton::Type type) const override;

};

void KeyboardStyle::startPaint(Painter &p) const {
	p.setPen(st::msgServiceFg);
}

const style::TextStyle &KeyboardStyle::textStyle() const {
	return st::serviceTextStyle;
}

void KeyboardStyle::repaint(not_null<const HistoryItem*> item) const {
	Auth().data().requestItemRepaint(item);
}

int KeyboardStyle::buttonRadius() const {
	return st::dateRadius;
}

void KeyboardStyle::paintButtonBg(
		Painter &p,
		const QRect &rect,
		float64 howMuchOver) const {
	App::roundRect(p, rect, st::msgServiceBg, StickerCorners);
	if (howMuchOver > 0) {
		auto o = p.opacity();
		p.setOpacity(o * howMuchOver);
		App::roundRect(p, rect, st::msgBotKbOverBgAdd, BotKbOverCorners);
		p.setOpacity(o);
	}
}

void KeyboardStyle::paintButtonIcon(
		Painter &p,
		const QRect &rect,
		int outerWidth,
		HistoryMessageMarkupButton::Type type) const {
	using Button = HistoryMessageMarkupButton;
	auto getIcon = [](Button::Type type) -> const style::icon* {
		switch (type) {
		case Button::Type::Url: return &st::msgBotKbUrlIcon;
		case Button::Type::SwitchInlineSame:
		case Button::Type::SwitchInline: return &st::msgBotKbSwitchPmIcon;
		}
		return nullptr;
	};
	if (auto icon = getIcon(type)) {
		icon->paint(p, rect.x() + rect.width() - icon->width() - st::msgBotKbIconPadding, rect.y() + st::msgBotKbIconPadding, outerWidth);
	}
}

void KeyboardStyle::paintButtonLoading(Painter &p, const QRect &rect) const {
	auto icon = &st::historySendingInvertedIcon;
	icon->paint(p, rect.x() + rect.width() - icon->width() - st::msgBotKbIconPadding, rect.y() + rect.height() - icon->height() - st::msgBotKbIconPadding, rect.x() * 2 + rect.width());
}

int KeyboardStyle::minButtonWidth(
		HistoryMessageMarkupButton::Type type) const {
	using Button = HistoryMessageMarkupButton;
	int result = 2 * buttonPadding(), iconWidth = 0;
	switch (type) {
	case Button::Type::Url: iconWidth = st::msgBotKbUrlIcon.width(); break;
	case Button::Type::SwitchInlineSame:
	case Button::Type::SwitchInline: iconWidth = st::msgBotKbSwitchPmIcon.width(); break;
	case Button::Type::Callback:
	case Button::Type::Game: iconWidth = st::historySendingInvertedIcon.width(); break;
	}
	if (iconWidth > 0) {
		result = std::max(result, 2 * iconWidth + 4 * int(st::msgBotKbIconPadding));
	}
	return result;
}

QString AdminBadgeText() {
	return lang(lng_admin_badge);
}

QString FastReplyText() {
	return lang(lng_fast_reply);
}

void PaintBubble(Painter &p, QRect rect, int outerWidth, bool selected, bool outbg, RectPart tailSide) {
	auto &bg = selected ? (outbg ? st::msgOutBgSelected : st::msgInBgSelected) : (outbg ? st::msgOutBg : st::msgInBg);
	auto &sh = selected ? (outbg ? st::msgOutShadowSelected : st::msgInShadowSelected) : (outbg ? st::msgOutShadow : st::msgInShadow);
	auto cors = selected ? (outbg ? MessageOutSelectedCorners : MessageInSelectedCorners) : (outbg ? MessageOutCorners : MessageInCorners);
	auto parts = RectPart::FullTop | RectPart::NoTopBottom | RectPart::Bottom;
	if (tailSide == RectPart::Right) {
		parts |= RectPart::BottomLeft;
		p.fillRect(rect.x() + rect.width() - st::historyMessageRadius, rect.y() + rect.height() - st::historyMessageRadius, st::historyMessageRadius, st::historyMessageRadius, bg);
		auto &tail = selected ? st::historyBubbleTailOutRightSelected : st::historyBubbleTailOutRight;
		tail.paint(p, rect.x() + rect.width(), rect.y() + rect.height() - tail.height(), outerWidth);
		p.fillRect(rect.x() + rect.width() - st::historyMessageRadius, rect.y() + rect.height(), st::historyMessageRadius + tail.width(), st::msgShadow, sh);
	} else if (tailSide == RectPart::Left) {
		parts |= RectPart::BottomRight;
		p.fillRect(rect.x(), rect.y() + rect.height() - st::historyMessageRadius, st::historyMessageRadius, st::historyMessageRadius, bg);
		auto &tail = selected ? (outbg ? st::historyBubbleTailOutLeftSelected : st::historyBubbleTailInLeftSelected) : (outbg ? st::historyBubbleTailOutLeft : st::historyBubbleTailInLeft);
		tail.paint(p, rect.x() - tail.width(), rect.y() + rect.height() - tail.height(), outerWidth);
		p.fillRect(rect.x() - tail.width(), rect.y() + rect.height(), st::historyMessageRadius + tail.width(), st::msgShadow, sh);
	} else {
		parts |= RectPart::FullBottom;
	}
	App::roundRect(p, rect, bg, cors, &sh, parts);
}

style::color FromNameFg(not_null<PeerData*> peer, bool selected) {
	if (selected) {
		const style::color colors[] = {
			st::historyPeer1NameFgSelected,
			st::historyPeer2NameFgSelected,
			st::historyPeer3NameFgSelected,
			st::historyPeer4NameFgSelected,
			st::historyPeer5NameFgSelected,
			st::historyPeer6NameFgSelected,
			st::historyPeer7NameFgSelected,
			st::historyPeer8NameFgSelected,
		};
		return colors[Data::PeerColorIndex(peer->id)];
	} else {
		const style::color colors[] = {
			st::historyPeer1NameFg,
			st::historyPeer2NameFg,
			st::historyPeer3NameFg,
			st::historyPeer4NameFg,
			st::historyPeer5NameFg,
			st::historyPeer6NameFg,
			st::historyPeer7NameFg,
			st::historyPeer8NameFg,
		};
		return colors[Data::PeerColorIndex(peer->id)];
	}
}

} // namespace

Message::Message(not_null<HistoryMessage*> data, Context context)
: Element(data, context) {
}

not_null<HistoryMessage*> Message::message() const {
	return static_cast<HistoryMessage*>(data().get());
}

QSize Message::performCountOptimalSize() {
	const auto item = message();
	const auto media = item->getMedia();

	auto maxWidth = 0;
	auto minHeight = 0;

	updateMediaInBubbleState();
	item->refreshEditedBadge();
	if (item->drawBubble()) {
		auto forwarded = item->Get<HistoryMessageForwarded>();
		auto reply = item->Get<HistoryMessageReply>();
		auto via = item->Get<HistoryMessageVia>();
		auto entry = item->Get<HistoryMessageLogEntryOriginal>();
		if (forwarded) {
			forwarded->create(via);
		}
		if (reply) {
			reply->updateName();
		}
		if (displayFromName()) {
			item->updateAdminBadgeState();
		}

		auto mediaDisplayed = false;
		if (media) {
			mediaDisplayed = media->isDisplayed();
			media->initDimensions();
		}
		if (entry) {
			entry->_page->initDimensions();
		}

		// Entry page is always a bubble bottom.
		auto mediaOnBottom = (mediaDisplayed && media->isBubbleBottom()) || (entry/* && entry->_page->isBubbleBottom()*/);
		auto mediaOnTop = (mediaDisplayed && media->isBubbleTop()) || (entry && entry->_page->isBubbleTop());

		if (mediaOnBottom) {
			if (item->_text.hasSkipBlock()) {
				item->_text.removeSkipBlock();
				item->_textWidth = -1;
				item->_textHeight = 0;
			}
		} else if (!item->_text.hasSkipBlock()) {
			item->_text.setSkipBlock(item->skipBlockWidth(), item->skipBlockHeight());
			item->_textWidth = -1;
			item->_textHeight = 0;
		}

		maxWidth = item->plainMaxWidth();
		minHeight = item->emptyText() ? 0 : item->_text.minHeight();
		if (!mediaOnBottom) {
			minHeight += st::msgPadding.bottom();
			if (mediaDisplayed) minHeight += st::mediaInBubbleSkip;
		}
		if (!mediaOnTop) {
			minHeight += st::msgPadding.top();
			if (mediaDisplayed) minHeight += st::mediaInBubbleSkip;
			if (entry) minHeight += st::mediaInBubbleSkip;
		}
		if (mediaDisplayed) {
			// Parts don't participate in maxWidth() in case of media message.
			accumulate_max(maxWidth, media->maxWidth());
			minHeight += media->minHeight();
		} else {
			// Count parts in maxWidth(), don't count them in minHeight().
			// They will be added in resizeGetHeight() anyway.
			if (displayFromName()) {
				auto namew = st::msgPadding.left()
					+ item->displayFrom()->nameText.maxWidth()
					+ st::msgPadding.right();
				if (via && !forwarded) {
					namew += st::msgServiceFont->spacew + via->maxWidth;
				}
				const auto replyWidth = item->hasFastReply()
					? st::msgFont->width(FastReplyText())
					: 0;
				if (item->hasAdminBadge()) {
					const auto badgeWidth = st::msgFont->width(
						AdminBadgeText());
					namew += st::msgPadding.right()
						+ std::max(badgeWidth, replyWidth);
				} else if (replyWidth) {
					namew += st::msgPadding.right() + replyWidth;
				}
				accumulate_max(maxWidth, namew);
			} else if (via && !forwarded) {
				accumulate_max(maxWidth, st::msgPadding.left() + via->maxWidth + st::msgPadding.right());
			}
			if (forwarded) {
				auto namew = st::msgPadding.left() + forwarded->text.maxWidth() + st::msgPadding.right();
				if (via) {
					namew += st::msgServiceFont->spacew + via->maxWidth;
				}
				accumulate_max(maxWidth, namew);
			}
			if (reply) {
				auto replyw = st::msgPadding.left() + reply->maxReplyWidth - st::msgReplyPadding.left() - st::msgReplyPadding.right() + st::msgPadding.right();
				if (reply->replyToVia) {
					replyw += st::msgServiceFont->spacew + reply->replyToVia->maxWidth;
				}
				accumulate_max(maxWidth, replyw);
			}
			if (entry) {
				accumulate_max(maxWidth, entry->_page->maxWidth());
				minHeight += entry->_page->minHeight();
			}
		}
	} else if (media) {
		media->initDimensions();
		maxWidth = media->maxWidth();
		minHeight = media->isDisplayed() ? media->minHeight() : 0;
	} else {
		maxWidth = st::msgMinWidth;
		minHeight = 0;
	}
	if (const auto markup = item->inlineReplyMarkup()) {
		if (!markup->inlineKeyboard) {
			markup->inlineKeyboard = std::make_unique<ReplyKeyboard>(
				item,
				std::make_unique<KeyboardStyle>(st::msgBotKbButton));
		}

		// if we have a text bubble we can resize it to fit the keyboard
		// but if we have only media we don't do that
		if (!item->emptyText()) {
			accumulate_max(maxWidth, markup->inlineKeyboard->naturalWidth());
		}
	}
	return QSize(maxWidth, minHeight);
}

void Message::draw(
		Painter &p,
		QRect clip,
		TextSelection selection,
		TimeMs ms) const {
	const auto item = message();
	const auto media = item->getMedia();

	auto outbg = item->hasOutLayout();
	auto bubble = item->drawBubble();
	auto selected = (selection == FullSelection);

	auto g = countGeometry();
	if (g.width() < 1) {
		return;
	}

	auto dateh = 0;
	if (auto date = item->Get<HistoryMessageDate>()) {
		dateh = date->height();
	}
	if (auto unreadbar = item->Get<HistoryMessageUnreadBar>()) {
		auto unreadbarh = unreadbar->height();
		if (clip.intersects(QRect(0, dateh, width(), unreadbarh))) {
			p.translate(0, dateh);
			unreadbar->paint(p, 0, width());
			p.translate(0, -dateh);
		}
	}

	auto fullAnimMs = App::main() ? App::main()->highlightStartTime(item) : 0LL;
	if (fullAnimMs > 0 && fullAnimMs <= ms) {
		auto animms = ms - fullAnimMs;
		if (animms < st::activeFadeInDuration + st::activeFadeOutDuration) {
			auto top = marginTop();
			auto bottom = marginBottom();
			auto fill = qMin(top, bottom);
			auto skiptop = top - fill;
			auto fillheight = fill + g.height() + fill;

			auto dt = (animms > st::activeFadeInDuration) ? (1. - (animms - st::activeFadeInDuration) / float64(st::activeFadeOutDuration)) : (animms / float64(st::activeFadeInDuration));
			auto o = p.opacity();
			p.setOpacity(o * dt);
			p.fillRect(0, skiptop, width(), fillheight, st::defaultTextPalette.selectOverlay);
			p.setOpacity(o);
		}
	}

	p.setTextPalette(selected ? (outbg ? st::outTextPaletteSelected : st::inTextPaletteSelected) : (outbg ? st::outTextPalette : st::inTextPalette));

	auto keyboard = item->inlineReplyKeyboard();
	if (keyboard) {
		auto keyboardHeight = st::msgBotKbButton.margin + keyboard->naturalHeight();
		g.setHeight(g.height() - keyboardHeight);
		auto keyboardPosition = QPoint(g.left(), g.top() + g.height() + st::msgBotKbButton.margin);
		p.translate(keyboardPosition);
		keyboard->paint(p, g.width(), clip.translated(-keyboardPosition), ms);
		p.translate(-keyboardPosition);
	}

	if (bubble) {
		if (displayFromName() && item->displayFrom()->nameVersion > item->_fromNameVersion) {
			fromNameUpdated(g.width());
		}

		auto entry = item->Get<HistoryMessageLogEntryOriginal>();
		auto mediaDisplayed = media && media->isDisplayed();

		auto skipTail = isAttachedToNext()
			|| (media && media->skipBubbleTail())
			|| (keyboard != nullptr);
		auto displayTail = skipTail ? RectPart::None : (outbg && !Adaptive::ChatWide()) ? RectPart::Right : RectPart::Left;
		PaintBubble(p, g, width(), selected, outbg, displayTail);

		// Entry page is always a bubble bottom.
		auto mediaOnBottom = (mediaDisplayed && media->isBubbleBottom()) || (entry/* && entry->_page->isBubbleBottom()*/);
		auto mediaOnTop = (mediaDisplayed && media->isBubbleTop()) || (entry && entry->_page->isBubbleTop());

		auto trect = g.marginsRemoved(st::msgPadding);
		if (mediaOnBottom) {
			trect.setHeight(trect.height() + st::msgPadding.bottom());
		}
		if (mediaOnTop) {
			trect.setY(trect.y() - st::msgPadding.top());
		} else {
			paintFromName(p, trect, selected);
			paintForwardedInfo(p, trect, selected);
			paintReplyInfo(p, trect, selected);
			paintViaBotIdInfo(p, trect, selected);
		}
		if (entry) {
			trect.setHeight(trect.height() - entry->_page->height());
		}
		auto needDrawInfo = mediaOnBottom ? !(entry ? entry->_page->customInfoLayout() : media->customInfoLayout()) : true;
		if (mediaDisplayed) {
			auto mediaAboveText = media->isAboveMessage();
			auto mediaHeight = media->height();
			auto mediaLeft = g.left();
			auto mediaTop = mediaAboveText ? trect.y() : (trect.y() + trect.height() - mediaHeight);
			if (!mediaAboveText) {
				paintText(p, trect, selection);
			}
			p.translate(mediaLeft, mediaTop);
			media->draw(p, clip.translated(-mediaLeft, -mediaTop), item->skipTextSelection(selection), ms);
			p.translate(-mediaLeft, -mediaTop);

			if (mediaAboveText) {
				trect.setY(trect.y() + mediaHeight);
				paintText(p, trect, selection);
			} else {
				needDrawInfo = !media->customInfoLayout();
			}
		} else {
			paintText(p, trect, selection);
		}
		if (entry) {
			auto entryLeft = g.left();
			auto entryTop = trect.y() + trect.height();
			p.translate(entryLeft, entryTop);
			auto entrySelection = item->skipTextSelection(selection);
			if (mediaDisplayed) {
				entrySelection = media->skipSelection(entrySelection);
			}
			entry->_page->draw(p, clip.translated(-entryLeft, -entryTop), entrySelection, ms);
			p.translate(-entryLeft, -entryTop);
		}
		if (needDrawInfo) {
			item->HistoryMessage::drawInfo(p, g.left() + g.width(), g.top() + g.height(), 2 * g.left() + g.width(), selected, InfoDisplayDefault);
		}
		if (item->displayRightAction()) {
			const auto fastShareSkip = snap(
				(g.height() - st::historyFastShareSize) / 2,
				0,
				st::historyFastShareBottom);
			const auto fastShareLeft = g.left() + g.width() + st::historyFastShareLeft;
			const auto fastShareTop = g.top() + g.height() - fastShareSkip - st::historyFastShareSize;
			item->drawRightAction(p, fastShareLeft, fastShareTop, width());
		}
	} else if (media && media->isDisplayed()) {
		p.translate(g.topLeft());
		media->draw(p, clip.translated(-g.topLeft()), item->skipTextSelection(selection), ms);
		p.translate(-g.topLeft());
	}

	p.restoreTextPalette();

	const auto reply = item->Get<HistoryMessageReply>();
	if (reply && reply->isNameUpdated()) {
		const_cast<Message*>(this)->setPendingResize();
	}
}

void Message::paintFromName(
		Painter &p,
		QRect &trect,
		bool selected) const {
	const auto item = message();
	if (displayFromName()) {
		const auto badgeWidth = [&] {
			if (item->hasAdminBadge()) {
				return st::msgFont->width(AdminBadgeText());
			}
			return 0;
		}();
		const auto replyWidth = [&] {
			if (item->isUnderCursor() && item->displayFastReply()) {
				return st::msgFont->width(FastReplyText());
			}
			return 0;
		}();
		const auto rightWidth = replyWidth ? replyWidth : badgeWidth;
		auto availableLeft = trect.left();
		auto availableWidth = trect.width();
		if (rightWidth) {
			availableWidth -= st::msgPadding.right() + rightWidth;
		}

		p.setFont(st::msgNameFont);
		if (item->isPost()) {
			p.setPen(selected ? st::msgInServiceFgSelected : st::msgInServiceFg);
		} else {
			p.setPen(FromNameFg(item->author(), selected));
		}
		item->displayFrom()->nameText.drawElided(p, availableLeft, trect.top(), availableWidth);
		auto skipWidth = item->author()->nameText.maxWidth() + st::msgServiceFont->spacew;
		availableLeft += skipWidth;
		availableWidth -= skipWidth;

		auto forwarded = item->Get<HistoryMessageForwarded>();
		auto via = item->Get<HistoryMessageVia>();
		if (via && !forwarded && availableWidth > 0) {
			auto outbg = item->hasOutLayout();
			p.setPen(selected ? (outbg ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (outbg ? st::msgOutServiceFg : st::msgInServiceFg));
			p.drawText(availableLeft, trect.top() + st::msgServiceFont->ascent, via->text);
			auto skipWidth = via->width + st::msgServiceFont->spacew;
			availableLeft += skipWidth;
			availableWidth -= skipWidth;
		}
		if (rightWidth) {
			p.setPen(selected ? st::msgInDateFgSelected : st::msgInDateFg);
			p.setFont(ClickHandler::showAsActive(item->_fastReplyLink)
				? st::msgFont->underline()
				: st::msgFont);
			p.drawText(
				trect.left() + trect.width() - rightWidth,
				trect.top() + st::msgFont->ascent,
				replyWidth ? FastReplyText() : AdminBadgeText());
		}
		trect.setY(trect.y() + st::msgNameFont->height);
	}
}

void Message::paintForwardedInfo(Painter &p, QRect &trect, bool selected) const {
	const auto item = message();
	if (item->displayForwardedFrom()) {
		style::font serviceFont(st::msgServiceFont), serviceName(st::msgServiceNameFont);

		auto outbg = item->hasOutLayout();
		p.setPen(selected ? (outbg ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (outbg ? st::msgOutServiceFg : st::msgInServiceFg));
		p.setFont(serviceFont);

		auto forwarded = item->Get<HistoryMessageForwarded>();
		auto breakEverywhere = (forwarded->text.countHeight(trect.width()) > 2 * serviceFont->height);
		p.setTextPalette(selected ? (outbg ? st::outFwdTextPaletteSelected : st::inFwdTextPaletteSelected) : (outbg ? st::outFwdTextPalette : st::inFwdTextPalette));
		forwarded->text.drawElided(p, trect.x(), trect.y(), trect.width(), 2, style::al_left, 0, -1, 0, breakEverywhere);
		p.setTextPalette(selected ? (outbg ? st::outTextPaletteSelected : st::inTextPaletteSelected) : (outbg ? st::outTextPalette : st::inTextPalette));

		trect.setY(trect.y() + (((forwarded->text.maxWidth() > trect.width()) ? 2 : 1) * serviceFont->height));
	}
}

void Message::paintReplyInfo(Painter &p, QRect &trect, bool selected) const {
	const auto item = message();
	if (auto reply = item->Get<HistoryMessageReply>()) {
		int32 h = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();

		auto flags = HistoryMessageReply::PaintFlag::InBubble | 0;
		if (selected) {
			flags |= HistoryMessageReply::PaintFlag::Selected;
		}
		reply->paint(p, item, trect.x(), trect.y(), trect.width(), flags);

		trect.setY(trect.y() + h);
	}
}

void Message::paintViaBotIdInfo(Painter &p, QRect &trect, bool selected) const {
	const auto item = message();
	if (!displayFromName() && !item->Has<HistoryMessageForwarded>()) {
		if (auto via = item->Get<HistoryMessageVia>()) {
			p.setFont(st::msgServiceNameFont);
			p.setPen(selected ? (item->hasOutLayout() ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (item->hasOutLayout() ? st::msgOutServiceFg : st::msgInServiceFg));
			p.drawTextLeft(trect.left(), trect.top(), width(), via->text);
			trect.setY(trect.y() + st::msgServiceNameFont->height);
		}
	}
}

void Message::paintText(Painter &p, QRect &trect, TextSelection selection) const {
	const auto item = message();

	auto outbg = item->hasOutLayout();
	auto selected = (selection == FullSelection);
	p.setPen(outbg ? (selected ? st::historyTextOutFgSelected : st::historyTextOutFg) : (selected ? st::historyTextInFgSelected : st::historyTextInFg));
	p.setFont(st::msgFont);
	item->_text.draw(p, trect.x(), trect.y(), trect.width(), style::al_left, 0, -1, selection);
}

bool Message::hasPoint(QPoint point) const {
	const auto g = countGeometry();
	if (g.width() < 1) {
		return false;
	}

	const auto item = message();
	if (item->drawBubble()) {
		return g.contains(point);
	} else if (const auto media = item->getMedia()) {
		return media->hasPoint(point - g.topLeft());
	} else {
		return false;
	}
}

bool Message::displayFromPhoto() const {
	return hasFromPhoto() && !isAttachedToNext();
}

bool Message::hasFromPhoto() const {
	switch (context()) {
	case Context::AdminLog:
	case Context::Feed:
		return true;
	case Context::History: {
		const auto item = message();
		if (item->isPost() || item->isEmpty()) {
			return false;
		} else if (Adaptive::ChatWide()) {
			return true;
		} else if (item->history()->peer->isSelf()) {
			return item->Has<HistoryMessageForwarded>();
		}
		return !item->out() && !item->history()->peer->isUser();
	} break;
	}
	Unexpected("Context in Message::hasFromPhoto.");
}

HistoryTextState Message::getState(
		QPoint point,
		HistoryStateRequest request) const {
	const auto item = message();
	const auto media = item->getMedia();

	auto result = HistoryTextState(item);

	auto g = countGeometry();
	if (g.width() < 1) {
		return result;
	}

	auto keyboard = item->inlineReplyKeyboard();
	auto keyboardHeight = 0;
	if (keyboard) {
		keyboardHeight = keyboard->naturalHeight();
		g.setHeight(g.height() - st::msgBotKbButton.margin - keyboardHeight);
	}

	if (item->drawBubble()) {
		auto entry = item->Get<HistoryMessageLogEntryOriginal>();
		auto mediaDisplayed = media && media->isDisplayed();

		// Entry page is always a bubble bottom.
		auto mediaOnBottom = (mediaDisplayed && media->isBubbleBottom()) || (entry/* && entry->_page->isBubbleBottom()*/);
		auto mediaOnTop = (mediaDisplayed && media->isBubbleTop()) || (entry && entry->_page->isBubbleTop());

		auto trect = g.marginsRemoved(st::msgPadding);
		if (mediaOnBottom) {
			trect.setHeight(trect.height() + st::msgPadding.bottom());
		}
		if (mediaOnTop) {
			trect.setY(trect.y() - st::msgPadding.top());
		} else {
			if (getStateFromName(point, trect, &result)) return result;
			if (getStateForwardedInfo(point, trect, &result, request)) return result;
			if (getStateReplyInfo(point, trect, &result)) return result;
			if (getStateViaBotIdInfo(point, trect, &result)) return result;
		}
		if (entry) {
			auto entryHeight = entry->_page->height();
			trect.setHeight(trect.height() - entryHeight);
			auto entryLeft = g.left();
			auto entryTop = trect.y() + trect.height();
			if (point.y() >= entryTop && point.y() < entryTop + entryHeight) {
				result = entry->_page->getState(
					point - QPoint(entryLeft, entryTop),
					request);
				result.symbol += item->_text.length() + (mediaDisplayed ? media->fullSelectionLength() : 0);
			}
		}

		auto needDateCheck = mediaOnBottom ? !(entry ? entry->_page->customInfoLayout() : media->customInfoLayout()) : true;
		if (mediaDisplayed) {
			auto mediaAboveText = media->isAboveMessage();
			auto mediaHeight = media->height();
			auto mediaLeft = trect.x() - st::msgPadding.left();
			auto mediaTop = mediaAboveText ? trect.y() : (trect.y() + trect.height() - mediaHeight);

			if (point.y() >= mediaTop && point.y() < mediaTop + mediaHeight) {
				result = media->getState(point - QPoint(mediaLeft, mediaTop), request);
				result.symbol += item->_text.length();
			} else {
				if (mediaAboveText) {
					trect.setY(trect.y() + mediaHeight);
				}
				if (trect.contains(point)) {
					getStateText(point, trect, &result, request);
				}
			}
		} else if (trect.contains(point)) {
			getStateText(point, trect, &result, request);
		}
		if (needDateCheck) {
			if (item->HistoryMessage::pointInTime(g.left() + g.width(), g.top() + g.height(), point, InfoDisplayDefault)) {
				result.cursor = HistoryInDateCursorState;
			}
		}
		if (item->displayRightAction()) {
			const auto fastShareSkip = snap(
				(g.height() - st::historyFastShareSize) / 2,
				0,
				st::historyFastShareBottom);
			const auto fastShareLeft = g.left() + g.width() + st::historyFastShareLeft;
			const auto fastShareTop = g.top() + g.height() - fastShareSkip - st::historyFastShareSize;
			if (QRect(
				fastShareLeft,
				fastShareTop,
				st::historyFastShareSize,
				st::historyFastShareSize
			).contains(point)) {
				result.link = item->rightActionLink();
			}
		}
	} else if (media && media->isDisplayed()) {
		result = media->getState(point - g.topLeft(), request);
		result.symbol += item->_text.length();
	}

	if (keyboard && !item->isLogEntry()) {
		auto keyboardTop = g.top() + g.height() + st::msgBotKbButton.margin;
		if (QRect(g.left(), keyboardTop, g.width(), keyboardHeight).contains(point)) {
			result.link = keyboard->getState(point - QPoint(g.left(), keyboardTop));
			return result;
		}
	}

	return result;
}

bool Message::getStateFromName(
		QPoint point,
		QRect &trect,
		not_null<HistoryTextState*> outResult) const {
	const auto item = message();
	if (displayFromName()) {
		const auto replyWidth = [&] {
			if (item->isUnderCursor() && item->displayFastReply()) {
				return st::msgFont->width(FastReplyText());
			}
			return 0;
		}();
		if (replyWidth
			&& point.x() >= trect.left() + trect.width() - replyWidth
			&& point.x() < trect.left() + trect.width() + st::msgPadding.right()
			&& point.y() >= trect.top() - st::msgPadding.top()
			&& point.y() < trect.top() + st::msgServiceFont->height) {
			outResult->link = item->fastReplyLink();
			return true;
		}
		if (point.y() >= trect.top() && point.y() < trect.top() + st::msgNameFont->height) {
			auto availableLeft = trect.left();
			auto availableWidth = trect.width();
			if (replyWidth) {
				availableWidth -= st::msgPadding.right() + replyWidth;
			}
			auto user = item->displayFrom();
			if (point.x() >= availableLeft
				&& point.x() < availableLeft + availableWidth
				&& point.x() < availableLeft + user->nameText.maxWidth()) {
				outResult->link = user->openLink();
				return true;
			}
			auto forwarded = item->Get<HistoryMessageForwarded>();
			auto via = item->Get<HistoryMessageVia>();
			if (via
				&& !forwarded
				&& point.x() >= availableLeft + item->author()->nameText.maxWidth() + st::msgServiceFont->spacew
				&& point.x() < availableLeft + availableWidth
				&& point.x() < availableLeft + user->nameText.maxWidth() + st::msgServiceFont->spacew + via->width) {
				outResult->link = via->link;
				return true;
			}
		}
		trect.setTop(trect.top() + st::msgNameFont->height);
	}
	return false;
}

bool Message::getStateForwardedInfo(
		QPoint point,
		QRect &trect,
		not_null<HistoryTextState*> outResult,
		HistoryStateRequest request) const {
	const auto item = message();
	if (item->displayForwardedFrom()) {
		auto forwarded = item->Get<HistoryMessageForwarded>();
		auto fwdheight = ((forwarded->text.maxWidth() > trect.width()) ? 2 : 1) * st::semiboldFont->height;
		if (point.y() >= trect.top() && point.y() < trect.top() + fwdheight) {
			auto breakEverywhere = (forwarded->text.countHeight(trect.width()) > 2 * st::semiboldFont->height);
			auto textRequest = request.forText();
			if (breakEverywhere) {
				textRequest.flags |= Text::StateRequest::Flag::BreakEverywhere;
			}
			*outResult = HistoryTextState(item, forwarded->text.getState(
				point - trect.topLeft(),
				trect.width(),
				textRequest));
			outResult->symbol = 0;
			outResult->afterSymbol = false;
			if (breakEverywhere) {
				outResult->cursor = HistoryInForwardedCursorState;
			} else {
				outResult->cursor = HistoryDefaultCursorState;
			}
			return true;
		}
		trect.setTop(trect.top() + fwdheight);
	}
	return false;
}

bool Message::getStateReplyInfo(
		QPoint point,
		QRect &trect,
		not_null<HistoryTextState*> outResult) const {
	const auto item = message();
	if (auto reply = item->Get<HistoryMessageReply>()) {
		int32 h = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
		if (point.y() >= trect.top() && point.y() < trect.top() + h) {
			if (reply->replyToMsg && QRect(trect.x(), trect.y() + st::msgReplyPadding.top(), trect.width(), st::msgReplyBarSize.height()).contains(point)) {
				outResult->link = reply->replyToLink();
			}
			return true;
		}
		trect.setTop(trect.top() + h);
	}
	return false;
}

bool Message::getStateViaBotIdInfo(
		QPoint point,
		QRect &trect,
		not_null<HistoryTextState*> outResult) const {
	const auto item = message();
	if (!displayFromName() && !item->Has<HistoryMessageForwarded>()) {
		if (auto via = item->Get<HistoryMessageVia>()) {
			if (QRect(trect.x(), trect.y(), via->width, st::msgNameFont->height).contains(point)) {
				outResult->link = via->link;
				return true;
			}
			trect.setTop(trect.top() + st::msgNameFont->height);
		}
	}
	return false;
}

bool Message::getStateText(
		QPoint point,
		QRect &trect,
		not_null<HistoryTextState*> outResult,
		HistoryStateRequest request) const {
	const auto item = message();
	if (trect.contains(point)) {
		*outResult = HistoryTextState(item, item->_text.getState(
			point - trect.topLeft(),
			trect.width(),
			request.forText()));
		return true;
	}
	return false;
}

// Forward to media.
void Message::updatePressed(QPoint point) {
	const auto item = message();
	const auto media = item->getMedia();
	if (!media) return;

	auto g = countGeometry();
	auto keyboard = item->inlineReplyKeyboard();
	if (keyboard) {
		auto keyboardHeight = st::msgBotKbButton.margin + keyboard->naturalHeight();
		g.setHeight(g.height() - keyboardHeight);
	}

	if (item->drawBubble()) {
		auto mediaDisplayed = media && media->isDisplayed();
		auto top = marginTop();
		auto trect = g.marginsAdded(-st::msgPadding);
		if (mediaDisplayed && media->isBubbleTop()) {
			trect.setY(trect.y() - st::msgPadding.top());
		} else {
			if (displayFromName()) trect.setTop(trect.top() + st::msgNameFont->height);
			if (item->displayForwardedFrom()) {
				auto forwarded = item->Get<HistoryMessageForwarded>();
				auto fwdheight = ((forwarded->text.maxWidth() > trect.width()) ? 2 : 1) * st::semiboldFont->height;
				trect.setTop(trect.top() + fwdheight);
			}
			if (item->Get<HistoryMessageReply>()) {
				auto h = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
				trect.setTop(trect.top() + h);
			}
			if (!displayFromName() && !item->Has<HistoryMessageForwarded>()) {
				if (auto via = item->Get<HistoryMessageVia>()) {
					trect.setTop(trect.top() + st::msgNameFont->height);
				}
			}
		}
		if (mediaDisplayed && media->isBubbleBottom()) {
			trect.setHeight(trect.height() + st::msgPadding.bottom());
		}

		auto needDateCheck = true;
		if (mediaDisplayed) {
			auto mediaAboveText = media->isAboveMessage();
			auto mediaHeight = media->height();
			auto mediaLeft = trect.x() - st::msgPadding.left();
			auto mediaTop = mediaAboveText ? trect.y() : (trect.y() + trect.height() - mediaHeight);
			media->updatePressed(point - QPoint(mediaLeft, mediaTop));
		}
	} else {
		media->updatePressed(point - g.topLeft());
	}
}

bool Message::hasFromName() const {
	switch (context()) {
	case Context::AdminLog:
	case Context::Feed:
		return true;
	case Context::History: {
		const auto item = message();
		return !item->hasOutLayout()
			&& (!item->history()->peer->isUser()
				|| item->history()->peer->isSelf());
	} break;
	}
	Unexpected("Context in Message::hasFromPhoto.");
}

bool Message::displayFromName() const {
	if (!hasFromName()) return false;
	if (isAttachedToPrevious()) return false;
	return true;
}

void Message::updateMediaInBubbleState() {
	const auto item = message();
	const auto media = item->getMedia();

	auto mediaHasSomethingBelow = false;
	auto mediaHasSomethingAbove = false;
	auto getMediaHasSomethingAbove = [&] {
		return displayFromName()
			|| item->displayForwardedFrom()
			|| item->Has<HistoryMessageReply>()
			|| item->Has<HistoryMessageVia>();
	};
	auto entry = item->Get<HistoryMessageLogEntryOriginal>();
	if (entry) {
		mediaHasSomethingBelow = true;
		mediaHasSomethingAbove = getMediaHasSomethingAbove();
		auto entryState = (mediaHasSomethingAbove
			|| !item->emptyText()
			|| (media && media->isDisplayed()))
			? MediaInBubbleState::Bottom
			: MediaInBubbleState::None;
		entry->_page->setInBubbleState(entryState);
	}
	if (!media) {
		return;
	}

	media->updateNeedBubbleState();
	if (!item->drawBubble()) {
		media->setInBubbleState(MediaInBubbleState::None);
		return;
	}

	if (!entry) {
		mediaHasSomethingAbove = getMediaHasSomethingAbove();
	}
	if (!item->emptyText()) {
		if (media->isAboveMessage()) {
			mediaHasSomethingBelow = true;
		} else {
			mediaHasSomethingAbove = true;
		}
	}
	const auto state = [&] {
		if (mediaHasSomethingAbove) {
			if (mediaHasSomethingBelow) {
				return MediaInBubbleState::Middle;
			}
			return MediaInBubbleState::Bottom;
		} else if (mediaHasSomethingBelow) {
			return MediaInBubbleState::Top;
		}
		return MediaInBubbleState::None;
	}();
	media->setInBubbleState(state);
}

void Message::fromNameUpdated(int width) const {
	const auto item = message();
	const auto replyWidth = item->hasFastReply()
		? st::msgFont->width(FastReplyText())
		: 0;
	if (item->hasAdminBadge()) {
		const auto badgeWidth = st::msgFont->width(AdminBadgeText());
		width -= st::msgPadding.right() + std::max(badgeWidth, replyWidth);
	} else if (replyWidth) {
		width -= st::msgPadding.right() + replyWidth;
	}
	item->_fromNameVersion = item->displayFrom()->nameVersion;
	if (!item->Has<HistoryMessageForwarded>()) {
		if (auto via = item->Get<HistoryMessageVia>()) {
			via->resize(width
				- st::msgPadding.left()
				- st::msgPadding.right()
				- item->author()->nameText.maxWidth()
				- st::msgServiceFont->spacew);
		}
	}
}

QRect Message::countGeometry() const {
	const auto item = message();
	const auto media = item->getMedia();

	auto maxwidth = qMin(st::msgMaxWidth, maxWidth());
	if (media && media->width() < maxwidth) {
		maxwidth = qMax(media->width(), qMin(maxwidth, item->plainMaxWidth()));
	}

	const auto outLayout = item->hasOutLayout();
	auto contentLeft = (outLayout && !Adaptive::ChatWide())
		? st::msgMargin.right()
		: st::msgMargin.left();
	if (hasFromPhoto()) {
		contentLeft += st::msgPhotoSkip;
	//} else if (!Adaptive::Wide() && !out() && !fromChannel() && st::msgPhotoSkip - (hmaxwidth - hwidth) > 0) {
	//	contentLeft += st::msgPhotoSkip - (hmaxwidth - hwidth);
	}

	auto contentWidth = width() - st::msgMargin.left() - st::msgMargin.right();
	if (item->history()->peer->isSelf() && !outLayout) {
		contentWidth -= st::msgPhotoSkip;
	}
	if (contentWidth > maxwidth) {
		if (outLayout && !Adaptive::ChatWide()) {
			contentLeft += contentWidth - maxwidth;
		}
		contentWidth = maxwidth;
	}

	const auto contentTop = marginTop();
	return QRect(
		contentLeft,
		contentTop,
		contentWidth,
		height() - contentTop - marginBottom());
}

int Message::resizeContentGetHeight(int newWidth) {
	const auto item = message();
	const auto media = item->getMedia();

	if (newWidth < st::msgMinWidth) {
		return height();
	}

	auto newHeight = minHeight();
	auto contentWidth = newWidth - (st::msgMargin.left() + st::msgMargin.right());
	if (item->history()->peer->isSelf() && !item->hasOutLayout()) {
		contentWidth -= st::msgPhotoSkip;
	}
	if (contentWidth < st::msgPadding.left() + st::msgPadding.right() + 1) {
		contentWidth = st::msgPadding.left() + st::msgPadding.right() + 1;
	} else if (contentWidth > st::msgMaxWidth) {
		contentWidth = st::msgMaxWidth;
	}
	if (item->drawBubble()) {
		auto forwarded = item->Get<HistoryMessageForwarded>();
		auto reply = item->Get<HistoryMessageReply>();
		auto via = item->Get<HistoryMessageVia>();
		auto entry = item->Get<HistoryMessageLogEntryOriginal>();

		auto mediaDisplayed = false;
		if (media) {
			mediaDisplayed = media->isDisplayed();
		}

		// Entry page is always a bubble bottom.
		auto mediaOnBottom = (mediaDisplayed && media->isBubbleBottom()) || (entry/* && entry->_page->isBubbleBottom()*/);
		auto mediaOnTop = (mediaDisplayed && media->isBubbleTop()) || (entry && entry->_page->isBubbleTop());

		if (contentWidth >= maxWidth()) {
			if (mediaDisplayed) {
				media->resizeGetHeight(maxWidth());
				if (entry) {
					newHeight += entry->_page->resizeGetHeight(countGeometry().width());
				}
			} else if (entry) {
				// In case of text-only message it is counted in minHeight already.
				entry->_page->resizeGetHeight(countGeometry().width());
			}
		} else {
			if (item->emptyText()) {
				newHeight = 0;
			} else {
				auto textWidth = qMax(contentWidth - st::msgPadding.left() - st::msgPadding.right(), 1);
				if (textWidth != item->_textWidth) {
					item->_textWidth = textWidth;
					item->_textHeight = item->_text.countHeight(textWidth);
				}
				newHeight = item->_textHeight;
			}
			if (!mediaOnBottom) {
				newHeight += st::msgPadding.bottom();
				if (mediaDisplayed) newHeight += st::mediaInBubbleSkip;
			}
			if (!mediaOnTop) {
				newHeight += st::msgPadding.top();
				if (mediaDisplayed) newHeight += st::mediaInBubbleSkip;
				if (entry) newHeight += st::mediaInBubbleSkip;
			}
			if (mediaDisplayed) {
				newHeight += media->resizeGetHeight(contentWidth);
				if (entry) {
					newHeight += entry->_page->resizeGetHeight(countGeometry().width());
				}
			} else if (entry) {
				newHeight += entry->_page->resizeGetHeight(contentWidth);
			}
		}

		if (displayFromName()) {
			fromNameUpdated(countGeometry().width());
			newHeight += st::msgNameFont->height;
		} else if (via && !forwarded) {
			via->resize(countGeometry().width() - st::msgPadding.left() - st::msgPadding.right());
			newHeight += st::msgNameFont->height;
		}

		if (item->displayForwardedFrom()) {
			auto fwdheight = ((forwarded->text.maxWidth() > (countGeometry().width() - st::msgPadding.left() - st::msgPadding.right())) ? 2 : 1) * st::semiboldFont->height;
			newHeight += fwdheight;
		}

		if (reply) {
			reply->resize(countGeometry().width() - st::msgPadding.left() - st::msgPadding.right());
			newHeight += st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
		}
	} else if (media && media->isDisplayed()) {
		newHeight = media->resizeGetHeight(contentWidth);
	} else {
		newHeight = 0;
	}
	if (const auto keyboard = item->inlineReplyKeyboard()) {
		const auto g = countGeometry();
		const auto keyboardHeight = st::msgBotKbButton.margin + keyboard->naturalHeight();
		newHeight += keyboardHeight;
		keyboard->resize(g.width(), keyboardHeight - st::msgBotKbButton.margin);
	}

	newHeight += marginTop() + marginBottom();
	return newHeight;
}

QSize Message::performCountCurrentSize(int newWidth) {
	const auto item = message();
	const auto newHeight = resizeContentGetHeight(newWidth);

	const auto keyboard = item->inlineReplyKeyboard();
	if (const auto markup = item->Get<HistoryMessageReplyMarkup>()) {
		const auto oldTop = markup->oldTop;
		if (oldTop >= 0) {
			markup->oldTop = -1;
			if (keyboard) {
				const auto height = st::msgBotKbButton.margin + keyboard->naturalHeight();
				const auto keyboardTop = newHeight - height + st::msgBotKbButton.margin - marginBottom();
				if (keyboardTop != oldTop) {
					Notify::inlineKeyboardMoved(item, oldTop, keyboardTop);
				}
			}
		}
	}
	return { newWidth, newHeight };
}

} // namespace HistoryView
