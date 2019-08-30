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
#include "history/view/media/history_view_media.h"
#include "history/view/media/history_view_web_page.h"
#include "history/history.h"
#include "ui/toast/toast.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_channel.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
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
	item->history()->owner().requestItemRepaint(item);
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
	using Type = HistoryMessageMarkupButton::Type;
	const auto getIcon = [](Type type) -> const style::icon* {
		switch (type) {
		case Type::Url:
		case Type::Auth: return &st::msgBotKbUrlIcon;
		case Type::SwitchInlineSame:
		case Type::SwitchInline: return &st::msgBotKbSwitchPmIcon;
		}
		return nullptr;
	};
	if (const auto icon = getIcon(type)) {
		icon->paint(p, rect.x() + rect.width() - icon->width() - st::msgBotKbIconPadding, rect.y() + st::msgBotKbIconPadding, outerWidth);
	}
}

void KeyboardStyle::paintButtonLoading(Painter &p, const QRect &rect) const {
	auto icon = &st::historySendingInvertedIcon;
	icon->paint(p, rect.x() + rect.width() - icon->width() - st::msgBotKbIconPadding, rect.y() + rect.height() - icon->height() - st::msgBotKbIconPadding, rect.x() * 2 + rect.width());
}

int KeyboardStyle::minButtonWidth(
		HistoryMessageMarkupButton::Type type) const {
	using Type = HistoryMessageMarkupButton::Type;
	int result = 2 * buttonPadding(), iconWidth = 0;
	switch (type) {
	case Type::Url:
	case Type::Auth: iconWidth = st::msgBotKbUrlIcon.width(); break;
	case Type::SwitchInlineSame:
	case Type::SwitchInline: iconWidth = st::msgBotKbSwitchPmIcon.width(); break;
	case Type::Callback:
	case Type::Game: iconWidth = st::historySendingInvertedIcon.width(); break;
	}
	if (iconWidth > 0) {
		result = std::max(result, 2 * iconWidth + 4 * int(st::msgBotKbIconPadding));
	}
	return result;
}

QString FastReplyText() {
	return tr::lng_fast_reply(tr::now);
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

style::color FromNameFg(PeerId peerId, bool selected) {
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
		return colors[Data::PeerColorIndex(peerId)];
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
		return colors[Data::PeerColorIndex(peerId)];
	}
}

} // namespace

LogEntryOriginal::LogEntryOriginal() = default;

LogEntryOriginal::LogEntryOriginal(LogEntryOriginal &&other)
: page(std::move(other.page)) {
}

LogEntryOriginal &LogEntryOriginal::operator=(LogEntryOriginal &&other) {
	page = std::move(other.page);
	return *this;
}

LogEntryOriginal::~LogEntryOriginal() = default;

Message::Message(
	not_null<ElementDelegate*> delegate,
	not_null<HistoryMessage*> data)
: Element(delegate, data) {
	initLogEntryOriginal();
}

not_null<HistoryMessage*> Message::message() const {
	return static_cast<HistoryMessage*>(data().get());
}

QSize Message::performCountOptimalSize() {
	const auto item = message();
	const auto media = this->media();

	auto maxWidth = 0;
	auto minHeight = 0;

	updateMediaInBubbleState();
	refreshEditedBadge();

	auto mediaOnBottom = (logEntryOriginal() != nullptr)
		|| (media && media->isDisplayed() && media->isBubbleBottom());
	if (mediaOnBottom) {
		// remove skip
	} else {
		// add skip
	}

	if (drawBubble()) {
		auto forwarded = item->Get<HistoryMessageForwarded>();
		auto reply = item->Get<HistoryMessageReply>();
		auto via = item->Get<HistoryMessageVia>();
		auto entry = logEntryOriginal();
		if (forwarded) {
			forwarded->create(via);
		}
		if (reply) {
			reply->updateName();
		}

		auto mediaDisplayed = false;
		if (media) {
			mediaDisplayed = media->isDisplayed();
			media->initDimensions();
		}
		if (entry) {
			entry->initDimensions();
		}

		// Entry page is always a bubble bottom.
		auto mediaOnBottom = (mediaDisplayed && media->isBubbleBottom()) || (entry/* && entry->isBubbleBottom()*/);
		auto mediaOnTop = (mediaDisplayed && media->isBubbleTop()) || (entry && entry->isBubbleTop());

		if (mediaOnBottom) {
			if (item->_text.removeSkipBlock()) {
				item->_textWidth = -1;
				item->_textHeight = 0;
			}
		} else if (item->_text.updateSkipBlock(skipBlockWidth(), skipBlockHeight())) {
			item->_textWidth = -1;
			item->_textHeight = 0;
		}

		maxWidth = plainMaxWidth();
		minHeight = hasVisibleText() ? item->_text.minHeight() : 0;
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
			if (media->enforceBubbleWidth()) {
				maxWidth = media->maxWidth();
				if (hasVisibleText() && maxWidth < plainMaxWidth()) {
					minHeight -= item->_text.minHeight();
					minHeight += item->_text.countHeight(maxWidth - st::msgPadding.left() - st::msgPadding.right());
				}
			} else {
				accumulate_max(maxWidth, media->maxWidth());
			}
			minHeight += media->minHeight();
		} else {
			// Count parts in maxWidth(), don't count them in minHeight().
			// They will be added in resizeGetHeight() anyway.
			if (displayFromName()) {
				const auto from = item->displayFrom();
				const auto &name = from
					? from->nameText()
					: item->hiddenForwardedInfo()->nameText;
				auto namew = st::msgPadding.left()
					+ name.maxWidth()
					+ st::msgPadding.right();
				if (via && !displayForwardedFrom()) {
					namew += st::msgServiceFont->spacew + via->maxWidth;
				}
				const auto replyWidth = hasFastReply()
					? st::msgFont->width(FastReplyText())
					: 0;
				if (item->hasMessageBadge()) {
					const auto badgeWidth = item->messageBadge().maxWidth();
					namew += st::msgPadding.right()
						+ std::max(badgeWidth, replyWidth);
				} else if (replyWidth) {
					namew += st::msgPadding.right() + replyWidth;
				}
				accumulate_max(maxWidth, namew);
			} else if (via && !displayForwardedFrom()) {
				accumulate_max(maxWidth, st::msgPadding.left() + via->maxWidth + st::msgPadding.right());
			}
			if (displayForwardedFrom()) {
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
				accumulate_max(maxWidth, entry->maxWidth());
				minHeight += entry->minHeight();
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
		if (hasVisibleText()) {
			accumulate_max(maxWidth, markup->inlineKeyboard->naturalWidth());
		}
	}
	return QSize(maxWidth, minHeight);
}

int Message::marginTop() const {
	auto result = 0;
	if (!isHidden()) {
		if (isAttachedToPrevious()) {
			result += st::msgMarginTopAttached;
		} else {
			result += st::msgMargin.top();
		}
	}
	result += displayedDateHeight();
	if (const auto bar = Get<UnreadBar>()) {
		result += bar->height();
	}
	return result;
}

int Message::marginBottom() const {
	return isHidden() ? 0 : st::msgMargin.bottom();
}

void Message::draw(
		Painter &p,
		QRect clip,
		TextSelection selection,
		crl::time ms) const {
	auto g = countGeometry();
	if (g.width() < 1) {
		return;
	}

	const auto item = message();
	const auto media = this->media();

	const auto outbg = hasOutLayout();
	const auto bubble = drawBubble();
	const auto selected = (selection == FullSelection);

	auto dateh = 0;
	if (const auto date = Get<DateBadge>()) {
		dateh = date->height();
	}
	if (const auto bar = Get<UnreadBar>()) {
		auto unreadbarh = bar->height();
		if (clip.intersects(QRect(0, dateh, width(), unreadbarh))) {
			p.translate(0, dateh);
			bar->paint(p, 0, width());
			p.translate(0, -dateh);
		}
	}

	if (isHidden()) {
		return;
	}

	paintHighlight(p, g.height());

	p.setTextPalette(selected
		? (outbg ? st::outTextPaletteSelected : st::inTextPaletteSelected)
		: (outbg ? st::outTextPalette : st::inTextPalette));

	auto keyboard = item->inlineReplyKeyboard();
	if (keyboard) {
		auto keyboardHeight = st::msgBotKbButton.margin + keyboard->naturalHeight();
		g.setHeight(g.height() - keyboardHeight);
		auto keyboardPosition = QPoint(g.left(), g.top() + g.height() + st::msgBotKbButton.margin);
		p.translate(keyboardPosition);
		keyboard->paint(p, g.width(), clip.translated(-keyboardPosition));
		p.translate(-keyboardPosition);
	}

	if (bubble) {
		if (displayFromName()
			&& item->displayFrom()
			&& item->displayFrom()->nameVersion > item->_fromNameVersion) {
			fromNameUpdated(g.width());
		}

		auto entry = logEntryOriginal();
		auto mediaDisplayed = media && media->isDisplayed();

		auto skipTail = isAttachedToNext()
			|| (media && media->skipBubbleTail())
			|| (keyboard != nullptr);
		auto displayTail = skipTail ? RectPart::None : (outbg && !Adaptive::ChatWide()) ? RectPart::Right : RectPart::Left;
		PaintBubble(p, g, width(), selected, outbg, displayTail);

		// Entry page is always a bubble bottom.
		auto mediaOnBottom = (mediaDisplayed && media->isBubbleBottom()) || (entry/* && entry->isBubbleBottom()*/);
		auto mediaOnTop = (mediaDisplayed && media->isBubbleTop()) || (entry && entry->isBubbleTop());

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
			trect.setHeight(trect.height() - entry->height());
		}
		paintText(p, trect, selection);
		if (mediaDisplayed) {
			auto mediaHeight = media->height();
			auto mediaLeft = g.left();
			auto mediaTop = (trect.y() + trect.height() - mediaHeight);

			p.translate(mediaLeft, mediaTop);
			media->draw(p, clip.translated(-mediaLeft, -mediaTop), skipTextSelection(selection), ms);
			p.translate(-mediaLeft, -mediaTop);
		}
		if (entry) {
			auto entryLeft = g.left();
			auto entryTop = trect.y() + trect.height();
			p.translate(entryLeft, entryTop);
			auto entrySelection = skipTextSelection(selection);
			if (mediaDisplayed) {
				entrySelection = media->skipSelection(entrySelection);
			}
			entry->draw(p, clip.translated(-entryLeft, -entryTop), entrySelection, ms);
			p.translate(-entryLeft, -entryTop);
		}
		const auto needDrawInfo = entry
			? !entry->customInfoLayout()
			: (mediaDisplayed
				? !media->customInfoLayout()
				: true);
		if (needDrawInfo) {
			drawInfo(p, g.left() + g.width(), g.top() + g.height(), 2 * g.left() + g.width(), selected, InfoDisplayType::Default);
		}
		if (displayRightAction()) {
			const auto fastShareSkip = snap(
				(g.height() - st::historyFastShareSize) / 2,
				0,
				st::historyFastShareBottom);
			const auto fastShareLeft = g.left() + g.width() + st::historyFastShareLeft;
			const auto fastShareTop = g.top() + g.height() - fastShareSkip - st::historyFastShareSize;
			drawRightAction(p, fastShareLeft, fastShareTop, width());
		}
	} else if (media && media->isDisplayed()) {
		p.translate(g.topLeft());
		media->draw(p, clip.translated(-g.topLeft()), skipTextSelection(selection), ms);
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
			if (item->hasMessageBadge()) {
				return item->messageBadge().maxWidth();
			}
			return 0;
		}();
		const auto replyWidth = [&] {
			if (isUnderCursor() && displayFastReply()) {
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
		const auto nameText = [&]() -> const Ui::Text::String * {
			const auto from = item->displayFrom();
			if (item->isPost()) {
				p.setPen(selected ? st::msgInServiceFgSelected : st::msgInServiceFg);
				return &from->nameText();
			} else if (from) {
				p.setPen(FromNameFg(from->id, selected));
				return &from->nameText();
			} else if (const auto info = item->hiddenForwardedInfo()) {
				p.setPen(FromNameFg(info->colorPeerId, selected));
				return &info->nameText;
			} else {
				Unexpected("Corrupt forwarded information in message.");
			}
		}();
		nameText->drawElided(p, availableLeft, trect.top(), availableWidth);
		const auto skipWidth = nameText->maxWidth() + st::msgServiceFont->spacew;
		availableLeft += skipWidth;
		availableWidth -= skipWidth;

		auto via = item->Get<HistoryMessageVia>();
		if (via && !displayForwardedFrom() && availableWidth > 0) {
			const auto outbg = hasOutLayout();
			p.setPen(selected ? (outbg ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (outbg ? st::msgOutServiceFg : st::msgInServiceFg));
			p.drawText(availableLeft, trect.top() + st::msgServiceFont->ascent, via->text);
			auto skipWidth = via->width + st::msgServiceFont->spacew;
			availableLeft += skipWidth;
			availableWidth -= skipWidth;
		}
		if (rightWidth) {
			p.setPen(selected ? st::msgInDateFgSelected : st::msgInDateFg);
			p.setFont(ClickHandler::showAsActive(_fastReplyLink)
				? st::msgFont->underline()
				: st::msgFont);
			if (replyWidth) {
				p.drawText(
					trect.left() + trect.width() - rightWidth,
					trect.top() + st::msgFont->ascent,
					FastReplyText());
			} else {
				item->messageBadge().draw(
					p,
					trect.left() + trect.width() - rightWidth,
					trect.top(),
					rightWidth);
			}
		}
		trect.setY(trect.y() + st::msgNameFont->height);
	}
}

void Message::paintForwardedInfo(Painter &p, QRect &trect, bool selected) const {
	if (displayForwardedFrom()) {
		const auto &serviceFont = st::msgServiceFont;
		const auto &serviceName = st::msgServiceNameFont;

		const auto item = message();
		const auto outbg = hasOutLayout();
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
		reply->paint(p, this, trect.x(), trect.y(), trect.width(), flags);

		trect.setY(trect.y() + h);
	}
}

void Message::paintViaBotIdInfo(Painter &p, QRect &trect, bool selected) const {
	const auto item = message();
	if (!displayFromName() && !displayForwardedFrom()) {
		if (auto via = item->Get<HistoryMessageVia>()) {
			const auto outbg = hasOutLayout();
			p.setFont(st::msgServiceNameFont);
			p.setPen(selected ? (outbg ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (outbg ? st::msgOutServiceFg : st::msgInServiceFg));
			p.drawTextLeft(trect.left(), trect.top(), width(), via->text);
			trect.setY(trect.y() + st::msgServiceNameFont->height);
		}
	}
}

void Message::paintText(Painter &p, QRect &trect, TextSelection selection) const {
	if (!hasVisibleText()) {
		return;
	}
	const auto item = message();

	const auto outbg = hasOutLayout();
	auto selected = (selection == FullSelection);
	p.setPen(outbg ? (selected ? st::historyTextOutFgSelected : st::historyTextOutFg) : (selected ? st::historyTextInFgSelected : st::historyTextInFg));
	p.setFont(st::msgFont);
	item->_text.draw(p, trect.x(), trect.y(), trect.width(), style::al_left, 0, -1, selection);
}

PointState Message::pointState(QPoint point) const {
	const auto g = countGeometry();
	if (g.width() < 1 || isHidden()) {
		return PointState::Outside;
	}

	const auto media = this->media();
	const auto item = message();
	if (drawBubble()) {
		if (!g.contains(point)) {
			return PointState::Outside;
		}
		if (const auto mediaDisplayed = media && media->isDisplayed()) {
			// Hack for grouped media point state.
			auto entry = logEntryOriginal();

			// Entry page is always a bubble bottom.
			auto mediaOnBottom = (mediaDisplayed && media->isBubbleBottom()) || (entry/* && entry->isBubbleBottom()*/);
			auto mediaOnTop = (mediaDisplayed && media->isBubbleTop()) || (entry && entry->isBubbleTop());

			auto trect = g.marginsRemoved(st::msgPadding);
			if (mediaOnBottom) {
				trect.setHeight(trect.height() + st::msgPadding.bottom());
			}
			//if (mediaOnTop) {
			//	trect.setY(trect.y() - st::msgPadding.top());
			//} else {
			//	if (getStateFromName(point, trect, &result)) return result;
			//	if (getStateForwardedInfo(point, trect, &result, request)) return result;
			//	if (getStateReplyInfo(point, trect, &result)) return result;
			//	if (getStateViaBotIdInfo(point, trect, &result)) return result;
			//}
			if (entry) {
				auto entryHeight = entry->height();
				trect.setHeight(trect.height() - entryHeight);
			}

			auto mediaHeight = media->height();
			auto mediaLeft = trect.x() - st::msgPadding.left();
			auto mediaTop = (trect.y() + trect.height() - mediaHeight);

			if (point.y() >= mediaTop && point.y() < mediaTop + mediaHeight) {
				return media->pointState(point - QPoint(mediaLeft, mediaTop));
			}
		}
		return PointState::Inside;
	} else if (media) {
		return media->pointState(point - g.topLeft());
	}
	return PointState::Outside;
}

bool Message::displayFromPhoto() const {
	return hasFromPhoto() && !isAttachedToNext();
}

bool Message::hasFromPhoto() const {
	if (isHidden()) {
		return false;
	}
	switch (context()) {
	case Context::AdminLog:
	//case Context::Feed: // #feed
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
	case Context::ContactPreview:
		return false;
	}
	Unexpected("Context in Message::hasFromPhoto.");
}

TextState Message::textState(
		QPoint point,
		StateRequest request) const {
	const auto item = message();
	const auto media = this->media();

	auto result = TextState(item);

	auto g = countGeometry();
	if (g.width() < 1 || isHidden()) {
		return result;
	}

	auto keyboard = item->inlineReplyKeyboard();
	auto keyboardHeight = 0;
	if (keyboard) {
		keyboardHeight = keyboard->naturalHeight();
		g.setHeight(g.height() - st::msgBotKbButton.margin - keyboardHeight);
	}

	if (drawBubble()) {
		const auto inBubble = g.contains(point);
		auto entry = logEntryOriginal();
		auto mediaDisplayed = media && media->isDisplayed();

		// Entry page is always a bubble bottom.
		auto mediaOnBottom = (mediaDisplayed && media->isBubbleBottom()) || (entry/* && entry->isBubbleBottom()*/);
		auto mediaOnTop = (mediaDisplayed && media->isBubbleTop()) || (entry && entry->isBubbleTop());

		auto trect = g.marginsRemoved(st::msgPadding);
		if (mediaOnBottom) {
			trect.setHeight(trect.height() + st::msgPadding.bottom());
		}
		if (mediaOnTop) {
			trect.setY(trect.y() - st::msgPadding.top());
		} else if (inBubble) {
			if (getStateFromName(point, trect, &result)) {
				return result;
			}
			if (getStateForwardedInfo(point, trect, &result, request)) {
				return result;
			}
			if (getStateReplyInfo(point, trect, &result)) {
				return result;
			}
			if (getStateViaBotIdInfo(point, trect, &result)) {
				return result;
			}
		}
		if (entry) {
			auto entryHeight = entry->height();
			trect.setHeight(trect.height() - entryHeight);
			auto entryLeft = g.left();
			auto entryTop = trect.y() + trect.height();
			if (point.y() >= entryTop && point.y() < entryTop + entryHeight) {
				result = entry->textState(
					point - QPoint(entryLeft, entryTop),
					request);
				result.symbol += item->_text.length() + (mediaDisplayed ? media->fullSelectionLength() : 0);
			}
		}

		auto checkForPointInTime = [&] {
			if (mediaOnBottom && (entry || media->customInfoLayout())) {
				return;
			}
			const auto inDate = pointInTime(
				g.left() + g.width(),
				g.top() + g.height(),
				point,
				InfoDisplayType::Default);
			if (inDate) {
				result.cursor = CursorState::Date;
			}
		};
		if (inBubble) {
			if (mediaDisplayed) {
				auto mediaHeight = media->height();
				auto mediaLeft = trect.x() - st::msgPadding.left();
				auto mediaTop = (trect.y() + trect.height() - mediaHeight);

				if (point.y() >= mediaTop && point.y() < mediaTop + mediaHeight) {
					result = media->textState(point - QPoint(mediaLeft, mediaTop), request);
					result.symbol += item->_text.length();
				} else if (getStateText(point, trect, &result, request)) {
					checkForPointInTime();
					return result;
				} else if (point.y() >= trect.y() + trect.height()) {
					result.symbol = item->_text.length();
				}
			} else if (getStateText(point, trect, &result, request)) {
				checkForPointInTime();
				return result;
			} else if (point.y() >= trect.y() + trect.height()) {
				result.symbol = item->_text.length();
			}
		}
		checkForPointInTime();
		if (displayRightAction()) {
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
				result.link = rightActionLink();
			}
		}
	} else if (media && media->isDisplayed()) {
		result = media->textState(point - g.topLeft(), request);
		result.symbol += item->_text.length();
	}

	if (keyboard && item->isHistoryEntry()) {
		auto keyboardTop = g.top() + g.height() + st::msgBotKbButton.margin;
		if (QRect(g.left(), keyboardTop, g.width(), keyboardHeight).contains(point)) {
			result.link = keyboard->getLink(point - QPoint(g.left(), keyboardTop));
			return result;
		}
	}

	return result;
}

bool Message::getStateFromName(
		QPoint point,
		QRect &trect,
		not_null<TextState*> outResult) const {
	const auto item = message();
	if (displayFromName()) {
		const auto replyWidth = [&] {
			if (isUnderCursor() && displayFastReply()) {
				return st::msgFont->width(FastReplyText());
			}
			return 0;
		}();
		if (replyWidth
			&& point.x() >= trect.left() + trect.width() - replyWidth
			&& point.x() < trect.left() + trect.width() + st::msgPadding.right()
			&& point.y() >= trect.top() - st::msgPadding.top()
			&& point.y() < trect.top() + st::msgServiceFont->height) {
			outResult->link = fastReplyLink();
			return true;
		}
		if (point.y() >= trect.top() && point.y() < trect.top() + st::msgNameFont->height) {
			auto availableLeft = trect.left();
			auto availableWidth = trect.width();
			if (replyWidth) {
				availableWidth -= st::msgPadding.right() + replyWidth;
			}
			const auto from = item->displayFrom();
			const auto nameText = [&]() -> const Ui::Text::String * {
				if (from) {
					return &from->nameText();
				} else if (const auto info = item->hiddenForwardedInfo()) {
					return &info->nameText;
				} else {
					Unexpected("Corrupt forwarded information in message.");
				}
			}();
			if (point.x() >= availableLeft
				&& point.x() < availableLeft + availableWidth
				&& point.x() < availableLeft + nameText->maxWidth()) {
				static const auto hidden = std::make_shared<LambdaClickHandler>([] {
					Ui::Toast::Show(tr::lng_forwarded_hidden(tr::now));
				});
				outResult->link = from ? from->openLink() : hidden;
				return true;
			}
			auto via = item->Get<HistoryMessageVia>();
			if (via
				&& !displayForwardedFrom()
				&& point.x() >= availableLeft + nameText->maxWidth() + st::msgServiceFont->spacew
				&& point.x() < availableLeft + availableWidth
				&& point.x() < availableLeft + nameText->maxWidth() + st::msgServiceFont->spacew + via->width) {
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
		not_null<TextState*> outResult,
		StateRequest request) const {
	if (displayForwardedFrom()) {
		const auto item = message();
		auto forwarded = item->Get<HistoryMessageForwarded>();
		auto fwdheight = ((forwarded->text.maxWidth() > trect.width()) ? 2 : 1) * st::semiboldFont->height;
		if (point.y() >= trect.top() && point.y() < trect.top() + fwdheight) {
			auto breakEverywhere = (forwarded->text.countHeight(trect.width()) > 2 * st::semiboldFont->height);
			auto textRequest = request.forText();
			if (breakEverywhere) {
				textRequest.flags |= Ui::Text::StateRequest::Flag::BreakEverywhere;
			}
			*outResult = TextState(item, forwarded->text.getState(
				point - trect.topLeft(),
				trect.width(),
				textRequest));
			outResult->symbol = 0;
			outResult->afterSymbol = false;
			if (breakEverywhere) {
				outResult->cursor = CursorState::Forwarded;
			} else {
				outResult->cursor = CursorState::None;
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
		not_null<TextState*> outResult) const {
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
		not_null<TextState*> outResult) const {
	const auto item = message();
	if (const auto via = item->Get<HistoryMessageVia>()) {
		if (!displayFromName() && !displayForwardedFrom()) {
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
		not_null<TextState*> outResult,
		StateRequest request) const {
	if (!hasVisibleText()) {
		return false;
	}
	const auto item = message();
	if (base::in_range(point.y(), trect.y(), trect.y() + trect.height())) {
		*outResult = TextState(item, item->_text.getState(
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
	const auto media = this->media();
	if (!media) return;

	auto g = countGeometry();
	auto keyboard = item->inlineReplyKeyboard();
	if (keyboard) {
		auto keyboardHeight = st::msgBotKbButton.margin + keyboard->naturalHeight();
		g.setHeight(g.height() - keyboardHeight);
	}

	if (drawBubble()) {
		auto mediaDisplayed = media && media->isDisplayed();
		auto top = marginTop();
		auto trect = g.marginsAdded(-st::msgPadding);
		if (mediaDisplayed && media->isBubbleTop()) {
			trect.setY(trect.y() - st::msgPadding.top());
		} else {
			if (displayFromName()) {
				trect.setTop(trect.top() + st::msgNameFont->height);
			}
			if (displayForwardedFrom()) {
				auto forwarded = item->Get<HistoryMessageForwarded>();
				auto fwdheight = ((forwarded->text.maxWidth() > trect.width()) ? 2 : 1) * st::semiboldFont->height;
				trect.setTop(trect.top() + fwdheight);
			}
			if (item->Get<HistoryMessageReply>()) {
				auto h = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
				trect.setTop(trect.top() + h);
			}
			if (const auto via = item->Get<HistoryMessageVia>()) {
				if (!displayFromName() && !displayForwardedFrom()) {
					trect.setTop(trect.top() + st::msgNameFont->height);
				}
			}
		}
		if (mediaDisplayed && media->isBubbleBottom()) {
			trect.setHeight(trect.height() + st::msgPadding.bottom());
		}

		auto needDateCheck = true;
		if (mediaDisplayed) {
			auto mediaHeight = media->height();
			auto mediaLeft = trect.x() - st::msgPadding.left();
			auto mediaTop = (trect.y() + trect.height() - mediaHeight);
			media->updatePressed(point - QPoint(mediaLeft, mediaTop));
		}
	} else {
		media->updatePressed(point - g.topLeft());
	}
}

TextForMimeData Message::selectedText(TextSelection selection) const {
	const auto item = message();
	const auto media = this->media();

	auto logEntryOriginalResult = TextForMimeData();
	auto textResult = item->_text.toTextForMimeData(selection);
	auto skipped = skipTextSelection(selection);
	auto mediaDisplayed = (media && media->isDisplayed());
	auto mediaResult = (mediaDisplayed || isHiddenByGroup())
		? media->selectedText(skipped)
		: TextForMimeData();
	if (auto entry = logEntryOriginal()) {
		const auto originalSelection = mediaDisplayed
			? media->skipSelection(skipped)
			: skipped;
		logEntryOriginalResult = entry->selectedText(originalSelection);
	}
	auto result = textResult;
	if (result.empty()) {
		result = std::move(mediaResult);
	} else if (!mediaResult.empty()) {
		result.append(qstr("\n\n")).append(std::move(mediaResult));
	}
	if (result.empty()) {
		result = std::move(logEntryOriginalResult);
	} else if (!logEntryOriginalResult.empty()) {
		result.append(qstr("\n\n")).append(std::move(logEntryOriginalResult));
	}
	return result;
}

TextSelection Message::adjustSelection(
		TextSelection selection,
		TextSelectType type) const {
	const auto item = message();
	const auto media = this->media();

	auto result = item->_text.adjustSelection(selection, type);
	auto beforeMediaLength = item->_text.length();
	if (selection.to <= beforeMediaLength) {
		return result;
	}
	auto mediaDisplayed = media && media->isDisplayed();
	if (mediaDisplayed) {
		auto mediaSelection = unskipTextSelection(
			media->adjustSelection(skipTextSelection(selection), type));
		if (selection.from >= beforeMediaLength) {
			result = mediaSelection;
		} else {
			result.to = mediaSelection.to;
		}
	}
	auto beforeEntryLength = beforeMediaLength
		+ (mediaDisplayed ? media->fullSelectionLength() : 0);
	if (selection.to <= beforeEntryLength) {
		return result;
	}
	if (const auto entry = logEntryOriginal()) {
		auto entrySelection = mediaDisplayed
			? media->skipSelection(skipTextSelection(selection))
			: skipTextSelection(selection);
		auto logEntryOriginalSelection = entry->adjustSelection(entrySelection, type);
		if (mediaDisplayed) {
			logEntryOriginalSelection = media->unskipSelection(logEntryOriginalSelection);
		}
		logEntryOriginalSelection = unskipTextSelection(logEntryOriginalSelection);
		if (selection.from >= beforeEntryLength) {
			result = logEntryOriginalSelection;
		} else {
			result.to = logEntryOriginalSelection.to;
		}
	}
	return result;
}

void Message::drawInfo(
		Painter &p,
		int right,
		int bottom,
		int width,
		bool selected,
		InfoDisplayType type) const {
	p.setFont(st::msgDateFont);

	bool outbg = hasOutLayout();
	bool invertedsprites = (type == InfoDisplayType::Image)
		|| (type == InfoDisplayType::Background);
	int32 infoRight = right, infoBottom = bottom;
	switch (type) {
	case InfoDisplayType::Default:
		infoRight -= st::msgPadding.right() - st::msgDateDelta.x();
		infoBottom -= st::msgPadding.bottom() - st::msgDateDelta.y();
		p.setPen(selected
			? (outbg ? st::msgOutDateFgSelected : st::msgInDateFgSelected)
			: (outbg ? st::msgOutDateFg : st::msgInDateFg));
	break;
	case InfoDisplayType::Image:
		infoRight -= st::msgDateImgDelta + st::msgDateImgPadding.x();
		infoBottom -= st::msgDateImgDelta + st::msgDateImgPadding.y();
		p.setPen(st::msgDateImgFg);
	break;
	case InfoDisplayType::Background:
		infoRight -= st::msgDateImgPadding.x();
		infoBottom -= st::msgDateImgPadding.y();
		p.setPen(st::msgServiceFg);
	break;
	}

	const auto item = message();
	auto infoW = infoWidth();
	if (rtl()) infoRight = width - infoRight + infoW;

	auto dateX = infoRight - infoW;
	auto dateY = infoBottom - st::msgDateFont->height;
	if (type == InfoDisplayType::Image) {
		auto dateW = infoW + 2 * st::msgDateImgPadding.x(), dateH = st::msgDateFont->height + 2 * st::msgDateImgPadding.y();
		App::roundRect(p, dateX - st::msgDateImgPadding.x(), dateY - st::msgDateImgPadding.y(), dateW, dateH, selected ? st::msgDateImgBgSelected : st::msgDateImgBg, selected ? DateSelectedCorners : DateCorners);
	} else if (type == InfoDisplayType::Background) {
		auto dateW = infoW + 2 * st::msgDateImgPadding.x(), dateH = st::msgDateFont->height + 2 * st::msgDateImgPadding.y();
		App::roundRect(p, dateX - st::msgDateImgPadding.x(), dateY - st::msgDateImgPadding.y(), dateW, dateH, selected ? st::msgServiceBgSelected : st::msgServiceBg, selected ? StickerSelectedCorners : StickerCorners);
	}
	dateX += timeLeft();

	if (const auto msgsigned = item->Get<HistoryMessageSigned>()) {
		msgsigned->signature.drawElided(p, dateX, dateY, item->_timeWidth);
	} else if (const auto edited = displayedEditBadge()) {
		edited->text.drawElided(p, dateX, dateY, item->_timeWidth);
	} else {
		p.drawText(dateX, dateY + st::msgDateFont->ascent, item->_timeText);
	}

	if (auto views = item->Get<HistoryMessageViews>()) {
		auto icon = [&] {
			if (item->id > 0) {
				if (outbg) {
					return &(invertedsprites ? st::historyViewsInvertedIcon : (selected ? st::historyViewsOutSelectedIcon : st::historyViewsOutIcon));
				}
				return &(invertedsprites ? st::historyViewsInvertedIcon : (selected ? st::historyViewsInSelectedIcon : st::historyViewsInIcon));
			}
			return &(invertedsprites ? st::historyViewsSendingInvertedIcon : st::historyViewsSendingIcon);
		}();
		if (item->id > 0) {
			icon->paint(p, infoRight - infoW, infoBottom + st::historyViewsTop, width);
			p.drawText(infoRight - infoW + st::historyViewsWidth, infoBottom - st::msgDateFont->descent, views->_viewsText);
		} else if (!outbg) { // sending outbg icon will be painted below
			auto iconSkip = st::historyViewsSpace + views->_viewsWidth;
			icon->paint(p, infoRight - infoW + iconSkip, infoBottom + st::historyViewsTop, width);
		}
	} else if (item->id < 0 && item->history()->peer->isSelf() && !outbg) {
		auto icon = &(invertedsprites ? st::historyViewsSendingInvertedIcon : st::historyViewsSendingIcon);
		icon->paint(p, infoRight - infoW, infoBottom + st::historyViewsTop, width);
	}
	if (outbg) {
		auto icon = [&] {
			if (item->id > 0) {
				if (item->unread()) {
					return &(invertedsprites ? st::historySentInvertedIcon : (selected ? st::historySentSelectedIcon : st::historySentIcon));
				}
				return &(invertedsprites ? st::historyReceivedInvertedIcon : (selected ? st::historyReceivedSelectedIcon : st::historyReceivedIcon));
			}
			return &(invertedsprites ? st::historySendingInvertedIcon : st::historySendingIcon);
		}();
		icon->paint(p, QPoint(infoRight, infoBottom) + st::historySendStatePosition, width);
	}
}

bool Message::pointInTime(
		int right,
		int bottom,
		QPoint point,
		InfoDisplayType type) const {
	auto infoRight = right;
	auto infoBottom = bottom;
	switch (type) {
	case InfoDisplayType::Default:
		infoRight -= st::msgPadding.right() - st::msgDateDelta.x();
		infoBottom -= st::msgPadding.bottom() - st::msgDateDelta.y();
		break;
	case InfoDisplayType::Image:
		infoRight -= st::msgDateImgDelta + st::msgDateImgPadding.x();
		infoBottom -= st::msgDateImgDelta + st::msgDateImgPadding.y();
		break;
	case InfoDisplayType::Background:
		infoRight -= st::msgDateImgPadding.x();
		infoBottom -= st::msgDateImgPadding.y();
		break;
	}
	const auto item = message();
	auto dateX = infoRight - infoWidth() + timeLeft();
	auto dateY = infoBottom - st::msgDateFont->height;
	return QRect(
		dateX,
		dateY,
		item->_timeWidth,
		st::msgDateFont->height).contains(point);
}

int Message::infoWidth() const {
	const auto item = message();
	auto result = item->_timeWidth;
	if (auto views = item->Get<HistoryMessageViews>()) {
		result += st::historyViewsSpace
			+ views->_viewsWidth
			+ st::historyViewsWidth;
	} else if (item->id < 0 && item->history()->peer->isSelf()) {
		if (!hasOutLayout()) {
			result += st::historySendStateSpace;
		}
	}
	if (hasOutLayout()) {
		result += st::historySendStateSpace;
	}
	return result;
}

void Message::refreshDataIdHook() {
	if (base::take(_rightActionLink)) {
		_rightActionLink = rightActionLink();
	}
	if (base::take(_fastReplyLink)) {
		_fastReplyLink = fastReplyLink();
	}
}

int Message::timeLeft() const {
	const auto item = message();
	auto result = 0;
	if (auto views = item->Get<HistoryMessageViews>()) {
		result += st::historyViewsSpace + views->_viewsWidth + st::historyViewsWidth;
	} else if (item->id < 0 && item->history()->peer->isSelf()) {
		if (!hasOutLayout()) {
			result += st::historySendStateSpace;
		}
	}
	return result;
}

int Message::plainMaxWidth() const {
	return st::msgPadding.left()
		+ (hasVisibleText() ? message()->_text.maxWidth() : 0)
		+ st::msgPadding.right();
}

void Message::initLogEntryOriginal() {
	if (const auto log = message()->Get<HistoryMessageLogEntryOriginal>()) {
		AddComponents(LogEntryOriginal::Bit());
		const auto entry = Get<LogEntryOriginal>();
		entry->page = std::make_unique<WebPage>(this, log->page);
	}
}

WebPage *Message::logEntryOriginal() const {
	if (const auto entry = Get<LogEntryOriginal>()) {
		return entry->page.get();
	}
	return nullptr;
}

bool Message::hasFromName() const {
	switch (context()) {
	case Context::AdminLog:
	//case Context::Feed: // #feed
		return true;
	case Context::History: {
		const auto item = message();
		return !hasOutLayout()
			&& (!item->history()->peer->isUser()
				|| item->history()->peer->isSelf());
	} break;
	case Context::ContactPreview:
		return false;
	}
	Unexpected("Context in Message::hasFromPhoto.");
}

bool Message::displayFromName() const {
	if (!hasFromName()) return false;
	if (isAttachedToPrevious()) return false;
	return true;
}

bool Message::displayForwardedFrom() const {
	const auto item = message();
	if (item->history()->peer->isSelf()) {
		return false;
	}
	if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
		if (const auto sender = item->discussionPostOriginalSender()) {
			if (sender == forwarded->originalSender) {
				return false;
			}
		}
		const auto media = this->media();
		return item->Has<HistoryMessageVia>()
			|| !media
			|| !media->isDisplayed()
			|| !media->hideForwardedFrom()
			|| (forwarded->originalSender
				&& forwarded->originalSender->isChannel());
	}
	return false;
}

bool Message::hasOutLayout() const {
	const auto item = message();
	if (item->history()->peer->isSelf()) {
		return !item->Has<HistoryMessageForwarded>();
	}
	return item->out() && !item->isPost();
}

bool Message::drawBubble() const {
	const auto item = message();
	if (isHidden()) {
		return false;
	} else if (logEntryOriginal()) {
		return true;
	}
	const auto media = this->media();
	return media
		? (hasVisibleText() || media->needsBubble())
		: !item->isEmpty();
}

bool Message::hasBubble() const {
	return drawBubble();
}

bool Message::hasFastReply() const {
	if (context() != Context::History) {
		return false;
	}
	const auto peer = data()->history()->peer;
	return !hasOutLayout() && (peer->isChat() || peer->isMegagroup());
}

bool Message::displayFastReply() const {
	return hasFastReply()
		&& IsServerMsgId(data()->id)
		&& data()->history()->peer->canWrite()
		&& !delegate()->elementInSelectionMode();
}

bool Message::displayRightAction() const {
	return displayFastShare() || displayGoToOriginal();
}

bool Message::displayFastShare() const {
	const auto item = message();
	const auto peer = item->history()->peer;
	if (!IsServerMsgId(item->id)) {
		return false;
	} else if (peer->isChannel()) {
		return !peer->isMegagroup();
	} else if (const auto user = peer->asUser()) {
		if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
			return !peer->isSelf()
				&& !item->out()
				&& forwarded->originalSender
				&& forwarded->originalSender->isChannel()
				&& !forwarded->originalSender->isMegagroup();
		} else if (user->isBot() && !item->out()) {
			if (const auto media = this->media()) {
				return media->allowsFastShare();
			}
		}
	}
	return false;
}

bool Message::displayGoToOriginal() const {
	const auto item = message();
	if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
		return forwarded->savedFromPeer && forwarded->savedFromMsgId;
	}
	return false;
}

void Message::drawRightAction(
		Painter &p,
		int left,
		int top,
		int outerWidth) const {
	p.setPen(Qt::NoPen);
	p.setBrush(st::msgServiceBg);
	{
		PainterHighQualityEnabler hq(p);
		p.drawEllipse(rtlrect(
			left,
			top,
			st::historyFastShareSize,
			st::historyFastShareSize,
			outerWidth));
	}
	if (displayFastShare()) {
		st::historyFastShareIcon.paint(p, left, top, outerWidth);
	} else {
		st::historyGoToOriginalIcon.paint(p, left, top, outerWidth);
	}
}

ClickHandlerPtr Message::rightActionLink() const {
	if (!_rightActionLink) {
		const auto owner = &data()->history()->owner();
		const auto itemId = data()->fullId();
		const auto forwarded = data()->Get<HistoryMessageForwarded>();
		const auto savedFromPeer = forwarded ? forwarded->savedFromPeer : nullptr;
		const auto savedFromMsgId = forwarded ? forwarded->savedFromMsgId : 0;
		_rightActionLink = std::make_shared<LambdaClickHandler>([=] {
			if (const auto item = owner->message(itemId)) {
				if (savedFromPeer && savedFromMsgId) {
					App::wnd()->sessionController()->showPeerHistory(
						savedFromPeer,
						Window::SectionShow::Way::Forward,
						savedFromMsgId);
				} else {
					FastShareMessage(item);
				}
			}
		});
	}
	return _rightActionLink;
}

ClickHandlerPtr Message::fastReplyLink() const {
	if (!_fastReplyLink) {
		const auto owner = &data()->history()->owner();
		const auto itemId = data()->fullId();
		_fastReplyLink = std::make_shared<LambdaClickHandler>([=] {
			if (const auto item = owner->message(itemId)) {
				if (const auto main = App::main()) {
					main->replyToItem(item);
				}
			}
		});
	}
	return _fastReplyLink;
}

void Message::updateMediaInBubbleState() {
	const auto item = message();
	const auto media = this->media();

	auto mediaHasSomethingBelow = false;
	auto mediaHasSomethingAbove = false;
	auto getMediaHasSomethingAbove = [&] {
		return displayFromName()
			|| displayForwardedFrom()
			|| item->Has<HistoryMessageReply>()
			|| item->Has<HistoryMessageVia>();
	};
	auto entry = logEntryOriginal();
	if (entry) {
		mediaHasSomethingBelow = true;
		mediaHasSomethingAbove = getMediaHasSomethingAbove();
		auto entryState = (mediaHasSomethingAbove
			|| hasVisibleText()
			|| (media && media->isDisplayed()))
			? MediaInBubbleState::Bottom
			: MediaInBubbleState::None;
		entry->setInBubbleState(entryState);
	}
	if (!media) {
		return;
	}

	media->updateNeedBubbleState();
	if (!drawBubble()) {
		media->setInBubbleState(MediaInBubbleState::None);
		return;
	}

	if (!entry) {
		mediaHasSomethingAbove = getMediaHasSomethingAbove();
	}
	if (hasVisibleText()) {
		mediaHasSomethingAbove = true;
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
	const auto replyWidth = hasFastReply()
		? st::msgFont->width(FastReplyText())
		: 0;
	if (item->hasMessageBadge()) {
		const auto badgeWidth = item->messageBadge().maxWidth();
		width -= st::msgPadding.right() + std::max(badgeWidth, replyWidth);
	} else if (replyWidth) {
		width -= st::msgPadding.right() + replyWidth;
	}
	const auto from = item->displayFrom();
	item->_fromNameVersion = from ? from->nameVersion : 1;
	if (const auto via = item->Get<HistoryMessageVia>()) {
		if (!displayForwardedFrom()) {
			const auto nameText = [&]() -> const Ui::Text::String * {
				if (from) {
					return &from->nameText();
				} else if (const auto info = item->hiddenForwardedInfo()) {
					return &info->nameText;
				} else {
					Unexpected("Corrupted forwarded information in message.");
				}
			}();
			via->resize(width
				- st::msgPadding.left()
				- st::msgPadding.right()
				- nameText->maxWidth()
				- st::msgServiceFont->spacew);
		}
	}
}

TextSelection Message::skipTextSelection(TextSelection selection) const {
	return HistoryView::UnshiftItemSelection(selection, message()->_text);
}

TextSelection Message::unskipTextSelection(TextSelection selection) const {
	return HistoryView::ShiftItemSelection(selection, message()->_text);
}

QRect Message::countGeometry() const {
	const auto item = message();
	const auto media = this->media();
	const auto mediaWidth = (media && media->isDisplayed())
		? media->width()
		: width();
	const auto outbg = hasOutLayout();
	const auto availableWidth = width()
		- st::msgMargin.left()
		- st::msgMargin.right();
	auto contentLeft = (outbg && !Adaptive::ChatWide())
		? st::msgMargin.right()
		: st::msgMargin.left();
	auto contentWidth = availableWidth;
	if (hasFromPhoto()) {
		contentLeft += st::msgPhotoSkip;
		if (displayRightAction()) {
			contentWidth -= st::msgPhotoSkip;
		}
	//} else if (!Adaptive::Wide() && !out() && !fromChannel() && st::msgPhotoSkip - (hmaxwidth - hwidth) > 0) {
	//	contentLeft += st::msgPhotoSkip - (hmaxwidth - hwidth);
	}
	accumulate_min(contentWidth, maxWidth());
	accumulate_min(contentWidth, st::msgMaxWidth);
	if (mediaWidth < contentWidth) {
		const auto textualWidth = plainMaxWidth();
		if (mediaWidth < textualWidth
			&& (!media || !media->enforceBubbleWidth())) {
			accumulate_min(contentWidth, textualWidth);
		} else {
			contentWidth = mediaWidth;
		}
	}
	if (contentWidth < availableWidth && outbg && !Adaptive::ChatWide()) {
		contentLeft += availableWidth - contentWidth;
	}

	const auto contentTop = marginTop();
	return QRect(
		contentLeft,
		contentTop,
		contentWidth,
		height() - contentTop - marginBottom());
}

int Message::resizeContentGetHeight(int newWidth) {
	if (isHidden()) {
		return marginTop() + marginBottom();
	} else if (newWidth < st::msgMinWidth) {
		return height();
	}

	auto newHeight = minHeight();

	const auto item = message();
	const auto media = this->media();
	const auto mediaDisplayed = media ? media->isDisplayed() : false;
	const auto bubble = drawBubble();

	// This code duplicates countGeometry() but also resizes media.
	auto contentWidth = newWidth - (st::msgMargin.left() + st::msgMargin.right());
	if (hasFromPhoto() && displayRightAction()) {
		contentWidth -= st::msgPhotoSkip;
	}
	accumulate_min(contentWidth, maxWidth());
	accumulate_min(contentWidth, st::msgMaxWidth);
	if (mediaDisplayed) {
		media->resizeGetHeight(contentWidth);
		if (media->width() < contentWidth) {
			const auto textualWidth = plainMaxWidth();
			if (media->width() < textualWidth
				&& !media->enforceBubbleWidth()) {
				accumulate_min(contentWidth, textualWidth);
			} else {
				contentWidth = media->width();
			}
		}
	}

	if (bubble) {
		auto reply = item->Get<HistoryMessageReply>();
		auto via = item->Get<HistoryMessageVia>();
		auto entry = logEntryOriginal();

		// Entry page is always a bubble bottom.
		auto mediaOnBottom = (mediaDisplayed && media->isBubbleBottom()) || (entry/* && entry->isBubbleBottom()*/);
		auto mediaOnTop = (mediaDisplayed && media->isBubbleTop()) || (entry && entry->isBubbleTop());

		if (contentWidth == maxWidth()) {
			if (mediaDisplayed) {
				if (entry) {
					newHeight += entry->resizeGetHeight(contentWidth);
				}
			} else if (entry) {
				// In case of text-only message it is counted in minHeight already.
				entry->resizeGetHeight(contentWidth);
			}
		} else {
			if (hasVisibleText()) {
				auto textWidth = qMax(contentWidth - st::msgPadding.left() - st::msgPadding.right(), 1);
				if (textWidth != item->_textWidth) {
					item->_textWidth = textWidth;
					item->_textHeight = item->_text.countHeight(textWidth);
				}
				newHeight = item->_textHeight;
			} else {
				newHeight = 0;
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
				newHeight += media->height();
				if (entry) {
					newHeight += entry->resizeGetHeight(contentWidth);
				}
			} else if (entry) {
				newHeight += entry->resizeGetHeight(contentWidth);
			}
		}

		if (displayFromName()) {
			fromNameUpdated(contentWidth);
			newHeight += st::msgNameFont->height;
		} else if (via && !displayForwardedFrom()) {
			via->resize(contentWidth - st::msgPadding.left() - st::msgPadding.right());
			newHeight += st::msgNameFont->height;
		}

		if (displayForwardedFrom()) {
			auto forwarded = item->Get<HistoryMessageForwarded>();
			auto fwdheight = ((forwarded->text.maxWidth() > (contentWidth - st::msgPadding.left() - st::msgPadding.right())) ? 2 : 1) * st::semiboldFont->height;
			newHeight += fwdheight;
		}

		if (reply) {
			reply->resize(contentWidth - st::msgPadding.left() - st::msgPadding.right());
			newHeight += st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
		}
	} else if (mediaDisplayed) {
		newHeight = media->height();
	} else {
		newHeight = 0;
	}
	if (const auto keyboard = item->inlineReplyKeyboard()) {
		const auto keyboardHeight = st::msgBotKbButton.margin + keyboard->naturalHeight();
		newHeight += keyboardHeight;
		keyboard->resize(contentWidth, keyboardHeight - st::msgBotKbButton.margin);
	}

	newHeight += marginTop() + marginBottom();
	return newHeight;
}

bool Message::hasVisibleText() const {
	if (message()->emptyText()) {
		return false;
	}
	const auto media = this->media();
	return !media || !media->hideMessageText();
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

void Message::refreshEditedBadge() {
	const auto item = message();
	const auto edited = displayedEditBadge();
	const auto editDate = displayedEditDate();
	const auto dateText = dateTime().toString(cTimeFormat());
	if (edited) {
		edited->refresh(dateText, editDate != 0);
	}
	if (const auto msgsigned = item->Get<HistoryMessageSigned>()) {
		const auto text = (!edited || !editDate)
			? dateText
			: edited->text.toString();
		msgsigned->refresh(text);
	}
	initTime();
}

void Message::initTime() {
	const auto item = message();
	if (const auto msgsigned = item->Get<HistoryMessageSigned>()) {
		item->_timeWidth = msgsigned->maxWidth();
	} else if (const auto edited = displayedEditBadge()) {
		item->_timeWidth = edited->maxWidth();
	} else {
		item->_timeText = dateTime().toString(cTimeFormat());
		item->_timeWidth = st::msgDateFont->width(item->_timeText);
	}
	if (const auto views = item->Get<HistoryMessageViews>()) {
		views->_viewsText = (views->_views > 0)
			? Lang::FormatCountToShort(views->_views).string
			: QString("1");
		views->_viewsWidth = views->_viewsText.isEmpty()
			? 0
			: st::msgDateFont->width(views->_viewsText);
	}
	if (item->_text.hasSkipBlock()) {
		if (item->_text.updateSkipBlock(skipBlockWidth(), skipBlockHeight())) {
			item->_textWidth = -1;
			item->_textHeight = 0;
		}
	}
}

bool Message::displayEditedBadge() const {
	return (displayedEditDate() != TimeId(0));
}

TimeId Message::displayedEditDate() const {
	const auto item = message();
	if (item->hideEditedBadge()) {
		return TimeId(0);
	} else if (const auto edited = displayedEditBadge()) {
		return edited->date;
	}
	return TimeId(0);
}

HistoryMessageEdited *Message::displayedEditBadge() {
	if (const auto media = this->media()) {
		if (media->overrideEditedDate()) {
			return media->displayedEditBadge();
		}
	}
	return message()->Get<HistoryMessageEdited>();
}

const HistoryMessageEdited *Message::displayedEditBadge() const {
	if (const auto media = this->media()) {
		if (media->overrideEditedDate()) {
			return media->displayedEditBadge();
		}
	}
	return message()->Get<HistoryMessageEdited>();
}

} // namespace HistoryView
