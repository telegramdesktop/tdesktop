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
#include "history/view/history_view_group_call_tracker.h" // UserpicInRow.
#include "history/history.h"
#include "ui/effects/ripple_animation.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "ui/toast/toast.h"
#include "ui/text/text_utilities.h"
#include "ui/text/text_entity.h"
#include "ui/cached_round_corners.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_channel.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "apiwrap.h"
#include "layout.h"
#include "facades.h"
#include "styles/style_widgets.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"

namespace HistoryView {
namespace {

const auto kPsaTooltipPrefix = "cloud_lng_tooltip_psa_";

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
	Ui::FillRoundRect(p, rect, st::msgServiceBg, Ui::StickerCorners);
	if (howMuchOver > 0) {
		auto o = p.opacity();
		p.setOpacity(o * howMuchOver);
		Ui::FillRoundRect(p, rect, st::msgBotKbOverBgAdd, Ui::BotKbOverCorners);
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
	case Type::CallbackWithPassword:
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

void PaintBubble(Painter &p, QRect rect, int outerWidth, bool selected, bool outbg, RectPart tailSide, RectParts skip) {
	auto &bg = selected ? (outbg ? st::msgOutBgSelected : st::msgInBgSelected) : (outbg ? st::msgOutBg : st::msgInBg);
	auto sh = &(selected ? (outbg ? st::msgOutShadowSelected : st::msgInShadowSelected) : (outbg ? st::msgOutShadow : st::msgInShadow));
	auto cors = selected ? (outbg ? Ui::MessageOutSelectedCorners : Ui::MessageInSelectedCorners) : (outbg ? Ui::MessageOutCorners : Ui::MessageInCorners);
	auto parts = RectPart::None | RectPart::NoTopBottom;
	if (skip & RectPart::Top) {
		if (skip & RectPart::Bottom) {
			p.fillRect(rect, bg);
			return;
		}
		rect.setTop(rect.y() - st::historyMessageRadius);
	} else {
		parts |= RectPart::FullTop;
	}
	if (skip & RectPart::Bottom) {
		rect.setHeight(rect.height() + st::historyMessageRadius);
		sh = nullptr;
		tailSide = RectPart::None;
	} else {
		parts |= RectPart::Bottom;
	}
	if (tailSide == RectPart::Right) {
		parts |= RectPart::BottomLeft;
		p.fillRect(rect.x() + rect.width() - st::historyMessageRadius, rect.y() + rect.height() - st::historyMessageRadius, st::historyMessageRadius, st::historyMessageRadius, bg);
		auto &tail = selected ? st::historyBubbleTailOutRightSelected : st::historyBubbleTailOutRight;
		tail.paint(p, rect.x() + rect.width(), rect.y() + rect.height() - tail.height(), outerWidth);
		p.fillRect(rect.x() + rect.width() - st::historyMessageRadius, rect.y() + rect.height(), st::historyMessageRadius + tail.width(), st::msgShadow, *sh);
	} else if (tailSide == RectPart::Left) {
		parts |= RectPart::BottomRight;
		p.fillRect(rect.x(), rect.y() + rect.height() - st::historyMessageRadius, st::historyMessageRadius, st::historyMessageRadius, bg);
		auto &tail = selected ? (outbg ? st::historyBubbleTailOutLeftSelected : st::historyBubbleTailInLeftSelected) : (outbg ? st::historyBubbleTailOutLeft : st::historyBubbleTailInLeft);
		tail.paint(p, rect.x() - tail.width(), rect.y() + rect.height() - tail.height(), outerWidth);
		p.fillRect(rect.x() - tail.width(), rect.y() + rect.height(), st::historyMessageRadius + tail.width(), st::msgShadow, *sh);
	} else if (!(skip & RectPart::Bottom)) {
		parts |= RectPart::FullBottom;
	}
	Ui::FillRoundRect(p, rect, bg, cors, sh, parts);
}

void PaintBubble(Painter &p, QRect rect, int outerWidth, bool selected, const std::vector<BubbleSelectionInterval> &selection, bool outbg, RectPart tailSide) {
	if (selection.empty()) {
		PaintBubble(
			p,
			rect,
			outerWidth,
			selected,
			outbg,
			tailSide,
			RectPart::None);
		return;
	}
	const auto left = rect.x();
	const auto width = rect.width();
	const auto top = rect.y();
	const auto bottom = top + rect.height();
	auto from = top;
	for (const auto &selected : selection) {
		if (selected.top > from) {
			const auto skip = RectPart::Bottom
				| (from > top ? RectPart::Top : RectPart::None);
			PaintBubble(
				p,
				QRect(left, from, width, selected.top - from),
				outerWidth,
				false,
				outbg,
				tailSide,
				skip);
		}
		const auto skip = ((selected.top > top)
			? RectPart::Top
			: RectPart::None)
			| ((selected.top + selected.height < bottom)
				? RectPart::Bottom
				: RectPart::None);
		PaintBubble(
			p,
			QRect(left, selected.top, width, selected.height),
			outerWidth,
			true,
			outbg,
			tailSide,
			skip);
		from = selected.top + selected.height;
	}
	if (from < bottom) {
		PaintBubble(
			p,
			QRect(left, from, width, bottom - from),
			outerWidth,
			false,
			outbg,
			tailSide,
			RectPart::Top);
	}
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

struct Message::CommentsButton {
	std::unique_ptr<Ui::RippleAnimation> ripple;
	std::vector<UserpicInRow> userpics;
	QImage cachedUserpics;
	ClickHandlerPtr link;
	QPoint lastPoint;

	QString rightActionCountString;
	int rightActionCount = 0;
	int rightActionCountWidth = 0;
};

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
	not_null<HistoryMessage*> data,
	Element *replacing)
: Element(delegate, data, replacing) {
	initLogEntryOriginal();
	initPsa();
}

Message::~Message() {
	if (_comments) {
		_comments = nullptr;
		checkHeavyPart();
	}
}

not_null<HistoryMessage*> Message::message() const {
	return static_cast<HistoryMessage*>(data().get());
}

void Message::refreshRightBadge() {
	const auto text = [&] {
		if (data()->isDiscussionPost()) {
			return (delegate()->elementContext() == Context::Replies)
				? QString()
				: tr::lng_channel_badge(tr::now);
		} else if (data()->author()->isMegagroup()) {
			if (const auto msgsigned = data()->Get<HistoryMessageSigned>()) {
				Assert(msgsigned->isAnonymousRank);
				return msgsigned->author;
			}
		}
		const auto channel = data()->history()->peer->asMegagroup();
		const auto user = data()->author()->asUser();
		if (!channel || !user) {
			return QString();
		}
		const auto info = channel->mgInfo.get();
		const auto i = channel->mgInfo->admins.find(peerToUser(user->id));
		const auto custom = (i != channel->mgInfo->admins.end())
			? i->second
			: (info->creator == user)
			? info->creatorRank
			: QString();
		return !custom.isEmpty()
			? custom
			: (info->creator == user)
			? tr::lng_owner_badge(tr::now)
			: (i != channel->mgInfo->admins.end())
			? tr::lng_admin_badge(tr::now)
			: QString();
	}();
	if (text.isEmpty()) {
		_rightBadge.clear();
	} else {
		_rightBadge.setText(
			st::defaultTextStyle,
			TextUtilities::RemoveEmoji(TextUtilities::SingleLine(text)));
	}
}

void Message::applyGroupAdminChanges(
		const base::flat_set<UserId> &changes) {
	if (!data()->out()
		&& changes.contains(peerToUser(data()->author()->id))) {
		history()->owner().requestViewResize(this);
	}
}

QSize Message::performCountOptimalSize() {
	const auto item = message();
	const auto media = this->media();

	auto maxWidth = 0;
	auto minHeight = 0;

	updateMediaInBubbleState();
	refreshEditedBadge();
	refreshRightBadge();

	auto mediaOnBottom = (logEntryOriginal() != nullptr)
		|| (media && media->isDisplayed() && media->isBubbleBottom());
	if (mediaOnBottom) {
		// remove skip
	} else {
		// add skip
	}

	if (drawBubble()) {
		const auto forwarded = item->Get<HistoryMessageForwarded>();
		const auto reply = displayedReply();
		const auto via = item->Get<HistoryMessageVia>();
		const auto entry = logEntryOriginal();
		const auto views = item->Get<HistoryMessageViews>();
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
		if (context() == Context::Replies && item->isDiscussionPost()) {
			maxWidth = std::max(maxWidth, st::msgMaxWidth);
		}
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
				if (!_rightBadge.isEmpty()) {
					const auto badgeWidth = _rightBadge.maxWidth();
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
				const auto skip1 = forwarded->psaType.isEmpty()
					? 0
					: st::historyPsaIconSkip1;
				auto namew = st::msgPadding.left() + forwarded->text.maxWidth() + skip1 + st::msgPadding.right();
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
		accumulate_max(maxWidth, minWidthForMedia());
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

	auto entry = logEntryOriginal();
	auto mediaDisplayed = media && media->isDisplayed();

	// Entry page is always a bubble bottom.
	auto mediaOnBottom = (mediaDisplayed && media->isBubbleBottom()) || (entry/* && entry->isBubbleBottom()*/);
	auto mediaOnTop = (mediaDisplayed && media->isBubbleTop()) || (entry && entry->isBubbleTop());

	auto mediaSelectionIntervals = (!selected && mediaDisplayed)
		? media->getBubbleSelectionIntervals(selection)
		: std::vector<BubbleSelectionInterval>();
	auto localMediaTop = 0;
	const auto customHighlight = mediaDisplayed && media->customHighlight();
	if (!mediaSelectionIntervals.empty() || customHighlight) {
		auto localMediaBottom = g.top() + g.height();
		if (data()->repliesAreComments() || data()->externalReply()) {
			localMediaBottom -= st::historyCommentsButtonHeight;
		}
		if (!mediaOnBottom) {
			localMediaBottom -= st::msgPadding.bottom();
		}
		if (entry) {
			localMediaBottom -= entry->height();
		}
		localMediaTop = localMediaBottom - media->height();
		for (auto &[top, height] : mediaSelectionIntervals) {
			top += localMediaTop;
		}
	}

	if (customHighlight) {
		media->drawHighlight(p, localMediaTop);
	} else {
		paintHighlight(p, g.height());
	}

	const auto roll = media ? media->bubbleRoll() : Media::BubbleRoll();
	if (roll) {
		p.save();
		p.translate(g.center());
		p.rotate(roll.rotate);
		p.scale(roll.scale, roll.scale);
		p.translate(-g.center());
	}

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

		auto skipTail = isAttachedToNext()
			|| (media && media->skipBubbleTail())
			|| (keyboard != nullptr)
			|| (context() == Context::Replies && data()->isDiscussionPost());
		auto displayTail = skipTail
			? RectPart::None
			: (outbg && !Core::App().settings().chatWide())
			? RectPart::Right
			: RectPart::Left;
		PaintBubble(
			p,
			g,
			width(),
			selected,
			mediaSelectionIntervals,
			outbg,
			displayTail);

		auto inner = g;
		paintCommentsButton(p, inner, selected);

		auto trect = inner.marginsRemoved(st::msgPadding);
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
			auto mediaLeft = inner.left();
			auto mediaTop = (trect.y() + trect.height() - mediaHeight);

			p.translate(mediaLeft, mediaTop);
			media->draw(p, clip.translated(-mediaLeft, -mediaTop), skipTextSelection(selection), ms);
			p.translate(-mediaLeft, -mediaTop);
		}
		if (entry) {
			auto entryLeft = inner.left();
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
			const auto bottomSelected = selected
				|| (!mediaSelectionIntervals.empty()
					&& (mediaSelectionIntervals.back().top
						+ mediaSelectionIntervals.back().height
						>= inner.y() + inner.height()));
			drawInfo(p, inner.left() + inner.width(), inner.top() + inner.height(), 2 * inner.left() + inner.width(), bottomSelected, InfoDisplayType::Default);
			if (g != inner) {
				const auto o = p.opacity();
				p.setOpacity(0.3);
				const auto color = bottomSelected
					? (outbg ? st::msgOutDateFgSelected : st::msgInDateFgSelected)
					: (outbg ? st::msgOutDateFg : st::msgInDateFg);
				p.fillRect(inner.left(), inner.top() + inner.height() - st::lineWidth, inner.width(), st::lineWidth, color);
				p.setOpacity(o);
			}
		}
		if (const auto size = rightActionSize()) {
			const auto fastShareSkip = std::clamp(
				(g.height() - size->height()) / 2,
				0,
				st::historyFastShareBottom);
			const auto fastShareLeft = g.left() + g.width() + st::historyFastShareLeft;
			const auto fastShareTop = g.top() + g.height() - fastShareSkip - size->height();
			drawRightAction(p, fastShareLeft, fastShareTop, width());
		}

		if (media) {
			media->paintBubbleFireworks(p, g, ms);
		}
	} else if (media && media->isDisplayed()) {
		p.translate(g.topLeft());
		media->draw(p, clip.translated(-g.topLeft()), skipTextSelection(selection), ms);
		p.translate(-g.topLeft());
	}

	p.restoreTextPalette();

	if (roll) {
		p.restore();
	}

	if (const auto reply = displayedReply()) {
		if (reply->isNameUpdated()) {
			const_cast<Message*>(this)->setPendingResize();
		}
	}
}

void Message::paintCommentsButton(
		Painter &p,
		QRect &g,
		bool selected) const {
	if (!data()->repliesAreComments() && !data()->externalReply()) {
		return;
	}
	if (!_comments) {
		_comments = std::make_unique<CommentsButton>();
		history()->owner().registerHeavyViewPart(const_cast<Message*>(this));
	}
	const auto outbg = hasOutLayout();
	const auto views = data()->Get<HistoryMessageViews>();

	g.setHeight(g.height() - st::historyCommentsButtonHeight);
	const auto top = g.top() + g.height();
	auto left = g.left();
	auto width = g.width();

	if (_comments->ripple) {
		p.setOpacity(st::historyPollRippleOpacity);
		_comments->ripple->paint(p, left, top, width);
		if (_comments->ripple->empty()) {
			_comments->ripple.reset();
		}
		p.setOpacity(1.);
	}

	left += st::historyCommentsSkipLeft;
	width -= st::historyCommentsSkipLeft
		+ st::historyCommentsSkipRight;

	const auto &open = outbg
		? (selected ? st::historyCommentsOpenOutSelected : st::historyCommentsOpenOut)
		: (selected ? st::historyCommentsOpenInSelected : st::historyCommentsOpenIn);
	open.paint(p,
		left + width - open.width(),
		top + (st::historyCommentsButtonHeight - open.height()) / 2,
		width);

	if (!views || views->recentRepliers.empty()) {
		const auto &icon = outbg
			? (selected ? st::historyCommentsOutSelected : st::historyCommentsOut)
			: (selected ? st::historyCommentsInSelected : st::historyCommentsIn);
		icon.paint(
			p,
			left,
			top + (st::historyCommentsButtonHeight - icon.height()) / 2,
			width);
		left += icon.width();
	} else {
		auto &list = _comments->userpics;
		const auto limit = HistoryMessageViews::kMaxRecentRepliers;
		const auto count = std::min(int(views->recentRepliers.size()), limit);
		const auto single = st::historyCommentsUserpics.size;
		const auto shift = st::historyCommentsUserpics.shift;
		const auto regenerate = [&] {
			if (list.size() != count) {
				return true;
			}
			for (auto i = 0; i != count; ++i) {
				auto &entry = list[i];
				const auto peer = entry.peer;
				auto &view = entry.view;
				const auto wasView = view.get();
				if (views->recentRepliers[i] != peer->id
					|| peer->userpicUniqueKey(view) != entry.uniqueKey
					|| view.get() != wasView) {
					return true;
				}
			}
			return false;
		}();
		if (regenerate) {
			for (auto i = 0; i != count; ++i) {
				const auto peerId = views->recentRepliers[i];
				if (i == list.size()) {
					list.push_back(UserpicInRow{
						history()->owner().peer(peerId)
					});
				} else if (list[i].peer->id != peerId) {
					list[i].peer = history()->owner().peer(peerId);
				}
			}
			while (list.size() > count) {
				list.pop_back();
			}
			GenerateUserpicsInRow(
				_comments->cachedUserpics,
				list,
				st::historyCommentsUserpics,
				limit);
		}
		p.drawImage(
			left,
			top + (st::historyCommentsButtonHeight - single) / 2,
			_comments->cachedUserpics);
		left += single + (count - 1) * (single - shift);
	}

	left += st::historyCommentsSkipText;
	p.setPen(outbg ? (selected ? st::msgFileThumbLinkOutFgSelected : st::msgFileThumbLinkOutFg) : (selected ? st::msgFileThumbLinkInFgSelected : st::msgFileThumbLinkInFg));
	p.setFont(st::semiboldFont);

	const auto textTop = top + (st::historyCommentsButtonHeight - st::semiboldFont->height) / 2;
	p.drawTextLeft(
		left,
		textTop,
		width,
		views ? views->replies.text : tr::lng_replies_view_original(tr::now),
		views ? views->replies.textWidth : -1);

	if (views && data()->areRepliesUnread()) {
		p.setPen(Qt::NoPen);
		p.setBrush(outbg ? (selected ? st::msgFileOutBgSelected : st::msgFileOutBg) : (selected ? st::msgFileInBgSelected : st::msgFileInBg));

		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(style::rtlrect(left + views->replies.textWidth + st::mediaUnreadSkip, textTop + st::mediaUnreadTop, st::mediaUnreadSize, st::mediaUnreadSize, width));
		}
	}
}

void Message::paintFromName(
		Painter &p,
		QRect &trect,
		bool selected) const {
	const auto item = message();
	if (!displayFromName()) {
		return;
	}
	const auto badgeWidth = _rightBadge.isEmpty() ? 0 : _rightBadge.maxWidth();
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
	const auto outbg = hasOutLayout();
	const auto nameText = [&]() -> const Ui::Text::String * {
		const auto from = item->displayFrom();
		if (outbg) {
			p.setPen(selected ? st::msgOutServiceFgSelected : st::msgOutServiceFg);
			return &from->nameText();
		} else if (item->isPost()) {
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
		p.setPen(selected ? (outbg ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (outbg ? st::msgOutServiceFg : st::msgInServiceFg));
		p.drawText(availableLeft, trect.top() + st::msgServiceFont->ascent, via->text);
		auto skipWidth = via->width + st::msgServiceFont->spacew;
		availableLeft += skipWidth;
		availableWidth -= skipWidth;
	}
	if (rightWidth) {
		p.setPen(outbg
			? (selected ? st::msgOutDateFgSelected : st::msgOutDateFg)
			: (selected ? st::msgInDateFgSelected : st::msgInDateFg));
		p.setFont(ClickHandler::showAsActive(_fastReplyLink)
			? st::msgFont->underline()
			: st::msgFont);
		if (replyWidth) {
			p.drawText(
				trect.left() + trect.width() - rightWidth,
				trect.top() + st::msgFont->ascent,
				FastReplyText());
		} else {
			_rightBadge.draw(
				p,
				trect.left() + trect.width() - rightWidth,
				trect.top(),
				rightWidth);
		}
	}
	trect.setY(trect.y() + st::msgNameFont->height);
}

void Message::paintForwardedInfo(Painter &p, QRect &trect, bool selected) const {
	if (displayForwardedFrom()) {
		const auto item = message();
		const auto outbg = hasOutLayout();
		const auto forwarded = item->Get<HistoryMessageForwarded>();

		const auto &serviceFont = st::msgServiceFont;
		const auto &serviceName = st::msgServiceNameFont;
		const auto skip1 = forwarded->psaType.isEmpty()
			? 0
			: st::historyPsaIconSkip1;
		const auto skip2 = forwarded->psaType.isEmpty()
			? 0
			: st::historyPsaIconSkip2;
		const auto fits = (forwarded->text.maxWidth() + skip1 <= trect.width());
		const auto skip = fits ? skip1 : skip2;
		const auto useWidth = trect.width() - skip;
		const auto countedHeight = forwarded->text.countHeight(useWidth);
		const auto breakEverywhere = (countedHeight > 2 * serviceFont->height);
		p.setPen(!forwarded->psaType.isEmpty()
			? st::boxTextFgGood
			: selected
			? (outbg
				? st::msgOutServiceFgSelected
				: st::msgInServiceFgSelected)
			: (outbg
				? st::msgOutServiceFg
				: st::msgInServiceFg));
		p.setFont(serviceFont);
		p.setTextPalette(!forwarded->psaType.isEmpty()
			? st::historyPsaForwardPalette
			: selected
			? (outbg
				? st::outFwdTextPaletteSelected
				: st::inFwdTextPaletteSelected)
			: (outbg
				? st::outFwdTextPalette
				: st::inFwdTextPalette));
		forwarded->text.drawElided(p, trect.x(), trect.y(), useWidth, 2, style::al_left, 0, -1, 0, breakEverywhere);
		p.setTextPalette(selected ? (outbg ? st::outTextPaletteSelected : st::inTextPaletteSelected) : (outbg ? st::outTextPalette : st::inTextPalette));

		if (!forwarded->psaType.isEmpty()) {
			const auto entry = Get<PsaTooltipState>();
			Assert(entry != nullptr);
			const auto shown = entry->buttonVisibleAnimation.value(
				entry->buttonVisible ? 1. : 0.);
			if (shown > 0) {
				const auto &icon = selected
					? (outbg
						? st::historyPsaIconOutSelected
						: st::historyPsaIconInSelected)
					: (outbg ? st::historyPsaIconOut : st::historyPsaIconIn);
				const auto position = fits
					? st::historyPsaIconPosition1
					: st::historyPsaIconPosition2;
				const auto x = trect.x() + trect.width() - position.x() - icon.width();
				const auto y = trect.y() + position.y();
				if (shown == 1) {
					icon.paint(p, x, y, trect.width());
				} else {
					p.save();
					p.translate(x + icon.width() / 2, y + icon.height() / 2);
					p.scale(shown, shown);
					p.setOpacity(shown);
					icon.paint(p, -icon.width() / 2, -icon.height() / 2, width());
					p.restore();
				}
			}
		}

		trect.setY(trect.y() + ((fits ? 1 : 2) * serviceFont->height));
	}
}

void Message::paintReplyInfo(Painter &p, QRect &trect, bool selected) const {
	const auto item = message();
	if (auto reply = displayedReply()) {
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
	auto g = countGeometry();
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

			if (item->repliesAreComments() || item->externalReply()) {
				g.setHeight(g.height() - st::historyCommentsButtonHeight);
			}

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

void Message::clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) {
	Element::clickHandlerPressedChanged(handler, pressed);

	if (!handler || !_comments) {
		return;
	} else if (handler == _comments->link) {
		toggleCommentsButtonRipple(pressed);
	}
}

void Message::toggleCommentsButtonRipple(bool pressed) {
	Expects(_comments != nullptr);

	if (!drawBubble()) {
		return;
	} else if (pressed) {
		const auto g = countGeometry();
		const auto linkWidth = g.width();
		const auto linkHeight = st::historyCommentsButtonHeight;
		if (!_comments->ripple) {
			const auto drawMask = [&](QPainter &p) {
				const auto radius = st::historyMessageRadius;
				p.drawRoundedRect(
					0,
					0,
					linkWidth,
					linkHeight,
					radius,
					radius);
				p.fillRect(0, 0, linkWidth, radius * 2, Qt::white);
			};
			auto mask = Ui::RippleAnimation::maskByDrawer(
				QSize(linkWidth, linkHeight),
				false,
				drawMask);
			_comments->ripple = std::make_unique<Ui::RippleAnimation>(
				(hasOutLayout()
					? st::historyPollRippleOut
					: st::historyPollRippleIn),
				std::move(mask),
				[=] { history()->owner().requestViewRepaint(this); });
		}
		_comments->ripple->add(_comments->lastPoint);
	} else if (_comments->ripple) {
		_comments->ripple->lastStop();
	}
}

bool Message::hasHeavyPart() const {
	return _comments || Element::hasHeavyPart();
}

void Message::unloadHeavyPart() {
	Element::unloadHeavyPart();
	_comments = nullptr;
}

bool Message::showForwardsFromSender() const {
	const auto peer = message()->history()->peer;
	return peer->isSelf() || peer->isRepliesChat();
}

bool Message::hasFromPhoto() const {
	if (isHidden()) {
		return false;
	}
	switch (context()) {
	case Context::AdminLog:
	//case Context::Feed: // #feed
		return true;
	case Context::History:
	case Context::Pinned:
	case Context::Replies: {
		const auto item = message();
		if (item->isPost()
			|| item->isEmpty()
			|| (context() == Context::Replies && data()->isDiscussionPost())) {
			return false;
		} else if (Core::App().settings().chatWide()) {
			return true;
		} else if (showForwardsFromSender()) {
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

		auto bubble = g;
		if (getStateCommentsButton(point, bubble, &result)) {
			return result;
		}

		auto trect = bubble.marginsRemoved(st::msgPadding);
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
			auto entryLeft = bubble.left();
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
				bubble.left() + bubble.width(),
				bubble.top() + bubble.height(),
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
		if (const auto size = rightActionSize()) {
			const auto fastShareSkip = snap(
				(g.height() - size->height()) / 2,
				0,
				st::historyFastShareBottom);
			const auto fastShareLeft = g.left() + g.width() + st::historyFastShareLeft;
			const auto fastShareTop = g.top() + g.height() - fastShareSkip - size->height();
			if (QRect(
				fastShareLeft,
				fastShareTop,
				size->width(),
				size->height()
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

bool Message::getStateCommentsButton(
		QPoint point,
		QRect &g,
		not_null<TextState*> outResult) const {
	if (!_comments) {
		return false;
	}
	g.setHeight(g.height() - st::historyCommentsButtonHeight);
	if (data()->isSending()
		|| !QRect(
			g.left(),
			g.top() + g.height(),
			g.width(),
			st::historyCommentsButtonHeight).contains(point)) {
		return false;
	}
	if (!_comments->link && data()->repliesAreComments()) {
		_comments->link = createGoToCommentsLink();
	} else if (!_comments->link && data()->externalReply()) {
		_comments->link = rightActionLink();
	}
	outResult->link = _comments->link;
	_comments->lastPoint = point - QPoint(g.left(), g.top() + g.height());
	return true;
}

ClickHandlerPtr Message::createGoToCommentsLink() const {
	const auto fullId = data()->fullId();
	return std::make_shared<LambdaClickHandler>([=] {
		if (const auto window = App::wnd()) {
			if (const auto controller = window->sessionController()) {
				if (const auto item = controller->session().data().message(fullId)) {
					const auto history = item->history();
					if (const auto channel = history->peer->asChannel()) {
						if (channel->invitePeekExpires()) {
							Ui::Toast::Show(
								tr::lng_channel_invite_private(tr::now));
							return;
						}
					}
					controller->showRepliesForMessage(history, item->id);
				}
			}
		}
	});
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
		const auto forwarded = item->Get<HistoryMessageForwarded>();
		const auto skip1 = forwarded->psaType.isEmpty()
			? 0
			: st::historyPsaIconSkip1;
		const auto skip2 = forwarded->psaType.isEmpty()
			? 0
			: st::historyPsaIconSkip2;
		const auto fits = (forwarded->text.maxWidth() <= (trect.width() - skip1));
		const auto fwdheight = (fits ? 1 : 2) * st::semiboldFont->height;
		if (point.y() >= trect.top() && point.y() < trect.top() + fwdheight) {
			if (skip1) {
				const auto &icon = st::historyPsaIconIn;
				const auto position = fits
					? st::historyPsaIconPosition1
					: st::historyPsaIconPosition2;
				const auto iconRect = QRect(
					trect.x() + trect.width() - position.x() - icon.width(),
					trect.y() + position.y(),
					icon.width(),
					icon.height());
				if (iconRect.contains(point)) {
					if (const auto link = psaTooltipLink()) {
						outResult->link = link;
						return true;
					}
				}
			}
			const auto useWidth = trect.width() - (fits ? skip1 : skip2);
			const auto breakEverywhere = (forwarded->text.countHeight(useWidth) > 2 * st::semiboldFont->height);
			auto textRequest = request.forText();
			if (breakEverywhere) {
				textRequest.flags |= Ui::Text::StateRequest::Flag::BreakEverywhere;
			}
			*outResult = TextState(item, forwarded->text.getState(
				point - trect.topLeft(),
				useWidth,
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

ClickHandlerPtr Message::psaTooltipLink() const {
	const auto state = Get<PsaTooltipState>();
	if (!state || !state->buttonVisible) {
		return nullptr;
	} else if (state->link) {
		return state->link;
	}
	const auto type = state->type;
	const auto handler = [=] {
		const auto custom = type.isEmpty()
			? QString()
			: Lang::GetNonDefaultValue(kPsaTooltipPrefix + type.toUtf8());
		auto text = Ui::Text::RichLangValue(
			(custom.isEmpty()
				? tr::lng_tooltip_psa_default(tr::now)
				: custom));
		TextUtilities::ParseEntities(text, 0);
		psaTooltipToggled(true);
		delegate()->elementShowTooltip(text, crl::guard(this, [=] {
			psaTooltipToggled(false);
		}));
	};
	state->link = std::make_shared<LambdaClickHandler>(
		crl::guard(this, handler));
	return state->link;
}

void Message::psaTooltipToggled(bool tooltipShown) const {
	const auto visible = !tooltipShown;
	const auto state = Get<PsaTooltipState>();
	if (state->buttonVisible == visible) {
		return;
	}
	state->buttonVisible = visible;
	history()->owner().notifyViewLayoutChange(this);
	state->buttonVisibleAnimation.start(
		[=] { history()->owner().requestViewRepaint(this); },
		visible ? 0. : 1.,
		visible ? 1. : 0.,
		st::fadeWrapDuration);
}

bool Message::getStateReplyInfo(
		QPoint point,
		QRect &trect,
		not_null<TextState*> outResult) const {
	const auto item = message();
	if (auto reply = displayedReply()) {
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
		Ui::FillRoundRect(p, dateX - st::msgDateImgPadding.x(), dateY - st::msgDateImgPadding.y(), dateW, dateH, selected ? st::msgDateImgBgSelected : st::msgDateImgBg, selected ? Ui::DateSelectedCorners : Ui::DateCorners);
	} else if (type == InfoDisplayType::Background) {
		auto dateW = infoW + 2 * st::msgDateImgPadding.x(), dateH = st::msgDateFont->height + 2 * st::msgDateImgPadding.y();
		Ui::FillRoundRect(p, dateX - st::msgDateImgPadding.x(), dateY - st::msgDateImgPadding.y(), dateW, dateH, selected ? st::msgServiceBgSelected : st::msgServiceBg, selected ? Ui::StickerSelectedCorners : Ui::StickerCorners);
	}
	dateX += timeLeft();

	if (const auto msgsigned = item->Get<HistoryMessageSigned>()
		; msgsigned && !msgsigned->isAnonymousRank) {
		msgsigned->signature.drawElided(p, dateX, dateY, item->_timeWidth);
	} else if (const auto edited = displayedEditBadge()) {
		edited->text.drawElided(p, dateX, dateY, item->_timeWidth);
	} else {
		p.drawText(dateX, dateY + st::msgDateFont->ascent, item->_timeText);
	}

	const auto viewIconTop = infoBottom + st::historyViewsTop;
	const auto pinIconTop = infoBottom + st::historyPinTop;
	auto left = infoRight - infoW;
	if (auto views = item->Get<HistoryMessageViews>()) {
		const auto textTop = infoBottom - st::msgDateFont->descent;
		if (views->replies.count > 0
			&& !views->commentsMegagroupId
			&& context() != Context::Replies) {
			auto icon = [&] {
				if (item->id > 0) {
					if (outbg) {
						return &(invertedsprites
							? st::historyRepliesInvertedIcon
							: selected
							? st::historyRepliesOutSelectedIcon
							: st::historyRepliesOutIcon);
					}
					return &(invertedsprites
						? st::historyRepliesInvertedIcon
						: selected
						? st::historyRepliesInSelectedIcon
						: st::historyRepliesInIcon);
				}
				return &(invertedsprites
					? st::historyViewsSendingInvertedIcon
					: st::historyViewsSendingIcon);
			}();
			if (item->id > 0) {
				icon->paint(p, left, viewIconTop, width);
				p.drawText(left + st::historyViewsWidth, textTop, views->replies.text);
			} else if (!outbg && views->views.count < 0) { // sending outbg icon will be painted below
				auto iconSkip = st::historyViewsSpace + views->replies.textWidth;
				icon->paint(p, left + iconSkip, viewIconTop, width);
			}
			left += st::historyViewsSpace
				+ views->replies.textWidth
				+ st::historyViewsWidth;
		}
		if (views->views.count >= 0) {
			auto icon = [&] {
				if (item->id > 0) {
					if (outbg) {
						return &(invertedsprites
							? st::historyViewsInvertedIcon
							: selected
							? st::historyViewsOutSelectedIcon
							: st::historyViewsOutIcon);
					}
					return &(invertedsprites
						? st::historyViewsInvertedIcon
						: selected
						? st::historyViewsInSelectedIcon
						: st::historyViewsInIcon);
				}
				return &(invertedsprites
					? st::historyViewsSendingInvertedIcon
					: st::historyViewsSendingIcon);
			}();
			if (item->id > 0) {
				icon->paint(p, left, viewIconTop, width);
				p.drawText(left + st::historyViewsWidth, textTop, views->views.text);
			} else if (!outbg) { // sending outbg icon will be painted below
				auto iconSkip = st::historyViewsSpace + views->views.textWidth;
				icon->paint(p, left + iconSkip, viewIconTop, width);
			}
			left += st::historyViewsSpace
				+ views->views.textWidth
				+ st::historyViewsWidth;
		}
	} else if (item->id < 0 && item->history()->peer->isSelf() && !outbg) {
		auto icon = &(invertedsprites ? st::historyViewsSendingInvertedIcon : st::historyViewsSendingIcon);
		icon->paint(p, left, viewIconTop, width);
	}
	if (displayPinIcon()) {
		const auto icon = [&] {
			if (outbg) {
				return &(invertedsprites
					? st::historyPinInvertedIcon
					: selected
					? st::historyPinOutSelectedIcon
					: st::historyPinOutIcon);
			}
			return &(invertedsprites
				? st::historyPinInvertedIcon
				: selected
				? st::historyPinInSelectedIcon
				: st::historyPinInIcon);
		}();
		icon->paint(p, left, pinIconTop, width);
		left += st::historyPinWidth;
	}
	if (outbg) {
		auto icon = [&] {
			if (item->id > 0) {
				if (delegate()->elementShownUnread(this)) {
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
		if (views->views.count >= 0) {
			result += st::historyViewsSpace
				+ views->views.textWidth
				+ st::historyViewsWidth;
		}
		if (views->replies.count > 0
			&& !views->commentsMegagroupId
			&& context() != Context::Replies) {
			result += st::historyViewsSpace
				+ views->replies.textWidth
				+ st::historyViewsWidth;
		}
	} else if (item->id < 0 && item->history()->peer->isSelf()) {
		if (!hasOutLayout()) {
			result += st::historySendStateSpace;
		}
	}
	if (displayPinIcon()) {
		result += st::historyPinWidth;
	}

	// When message is scheduled until online, time is not displayed,
	// so message should have less space.
	if (!item->_timeWidth) {
		result += st::historyScheduledUntilOnlineStateSpace;
	} else if (hasOutLayout()) {
		result += st::historySendStateSpace;
	}
	return result;
}

auto Message::verticalRepaintRange() const -> VerticalRepaintRange {
	const auto media = this->media();
	const auto add = media ? media->bubbleRollRepaintMargins() : QMargins();
	return {
		.top = -add.top(),
		.height = height() + add.top() + add.bottom()
	};
}

void Message::refreshDataIdHook() {
	if (base::take(_rightActionLink)) {
		_rightActionLink = rightActionLink();
	}
	if (base::take(_fastReplyLink)) {
		_fastReplyLink = fastReplyLink();
	}
	if (_comments) {
		_comments->link = nullptr;
	}
}

int Message::timeLeft() const {
	const auto item = message();
	auto result = 0;
	if (auto views = item->Get<HistoryMessageViews>()) {
		if (views->views.count >= 0) {
			result += st::historyViewsSpace + views->views.textWidth + st::historyViewsWidth;
		}
		if (views->replies.count > 0
			&& !views->commentsMegagroupId
			&& context() != Context::Replies) {
			result += st::historyViewsSpace + views->replies.textWidth + st::historyViewsWidth;
		}
	} else if (item->id < 0 && item->history()->peer->isSelf()) {
		if (!hasOutLayout()) {
			result += st::historySendStateSpace;
		}
	}
	if (displayPinIcon()) {
		result += st::historyPinWidth;
	}
	return result;
}

int Message::plainMaxWidth() const {
	return st::msgPadding.left()
		+ (hasVisibleText() ? message()->_text.maxWidth() : 0)
		+ st::msgPadding.right();
}

int Message::monospaceMaxWidth() const {
	return st::msgPadding.left()
		+ (hasVisibleText() ? message()->_text.countMaxMonospaceWidth() : 0)
		+ st::msgPadding.right();
}

void Message::initLogEntryOriginal() {
	if (const auto log = message()->Get<HistoryMessageLogEntryOriginal>()) {
		AddComponents(LogEntryOriginal::Bit());
		const auto entry = Get<LogEntryOriginal>();
		entry->page = std::make_unique<WebPage>(this, log->page);
	}
}

void Message::initPsa() {
	if (const auto forwarded = message()->Get<HistoryMessageForwarded>()) {
		if (!forwarded->psaType.isEmpty()) {
			AddComponents(PsaTooltipState::Bit());
			Get<PsaTooltipState>()->type = forwarded->psaType;
		}
	}
}

WebPage *Message::logEntryOriginal() const {
	if (const auto entry = Get<LogEntryOriginal>()) {
		return entry->page.get();
	}
	return nullptr;
}

HistoryMessageReply *Message::displayedReply() const {
	if (const auto reply = data()->Get<HistoryMessageReply>()) {
		return delegate()->elementHideReply(this) ? nullptr : reply;
	}
	return nullptr;
}

bool Message::displayPinIcon() const {
	return data()->isPinned() && !isPinnedContext();
}

bool Message::hasFromName() const {
	switch (context()) {
	case Context::AdminLog:
	//case Context::Feed: // #feed
		return true;
	case Context::History:
	case Context::Pinned:
	case Context::Replies: {
		const auto item = message();
		return (!hasOutLayout() || item->from()->isMegagroup())
			&& (!item->history()->peer->isUser()
				|| showForwardsFromSender());
	} break;
	case Context::ContactPreview:
		return false;
	}
	Unexpected("Context in Message::hasFromPhoto.");
}

bool Message::displayFromName() const {
	if (!hasFromName() || isAttachedToPrevious()) {
		return false;
	}
	return !Has<PsaTooltipState>();
}

bool Message::displayForwardedFrom() const {
	const auto item = message();
	if (showForwardsFromSender()) {
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
	} else if (showForwardsFromSender()) {
		return false;
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

int Message::minWidthForMedia() const {
	auto result = infoWidth() + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x());
	const auto views = data()->Get<HistoryMessageViews>();
	if (data()->repliesAreComments() && !views->replies.text.isEmpty()) {
		const auto limit = HistoryMessageViews::kMaxRecentRepliers;
		const auto single = st::historyCommentsUserpics.size;
		const auto shift = st::historyCommentsUserpics.shift;
		const auto added = single
			+ (limit - 1) * (single - shift)
			+ st::historyCommentsSkipLeft
			+ st::historyCommentsSkipRight
			+ st::historyCommentsSkipText
			+ st::historyCommentsOpenOutSelected.width()
			+ st::historyCommentsSkipRight
			+ st::mediaUnreadSkip
			+ st::mediaUnreadSize;
		accumulate_max(result, added + views->replies.textWidth);
	} else if (data()->externalReply()) {
		const auto added = st::historyCommentsIn.width()
			+ st::historyCommentsSkipLeft
			+ st::historyCommentsSkipRight
			+ st::historyCommentsSkipText
			+ st::historyCommentsOpenOutSelected.width()
			+ st::historyCommentsSkipRight;
		accumulate_max(result, added + st::semiboldFont->width(
			tr::lng_replies_view_original(tr::now)));
	}
	return result;
}

bool Message::hasFastReply() const {
	if (context() == Context::Replies) {
		if (data()->isDiscussionPost()) {
			return false;
		}
	} else if (context() != Context::History) {
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

bool Message::displayRightActionComments() const {
	return !isPinnedContext()
		&& data()->repliesAreComments()
		&& media()
		&& media()->isDisplayed()
		&& !hasBubble();
}

std::optional<QSize> Message::rightActionSize() const {
	if (displayRightActionComments()) {
		const auto views = data()->Get<HistoryMessageViews>();
		Assert(views != nullptr);
		return (views->repliesSmall.textWidth > 0)
			? QSize(
				std::max(
					st::historyFastShareSize,
					2 * st::historyFastShareBottom + views->repliesSmall.textWidth),
				st::historyFastShareSize + st::historyFastShareBottom + st::semiboldFont->height)
			: QSize(st::historyFastShareSize, st::historyFastShareSize);
	}
	return (displayFastShare() || displayGoToOriginal())
		? QSize(st::historyFastShareSize, st::historyFastShareSize)
		: std::optional<QSize>();
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
			return !showForwardsFromSender()
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
	if (isPinnedContext()) {
		return !hasOutLayout();
	}
	const auto item = message();
	if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
		return forwarded->savedFromPeer
			&& forwarded->savedFromMsgId
			&& (!item->externalReply() || !hasBubble())
			&& !(context() == Context::Replies);
	}
	return false;
}

void Message::drawRightAction(
		Painter &p,
		int left,
		int top,
		int outerWidth) const {
	const auto size = rightActionSize();
	p.setPen(Qt::NoPen);
	p.setBrush(st::msgServiceBg);
	{
		PainterHighQualityEnabler hq(p);
		const auto rect = style::rtlrect(
			left,
			top,
			size->width(),
			size->height(),
			outerWidth);
		const auto usual = st::historyFastShareSize;
		if (size->width() == size->height() && size->width() == usual) {
			p.drawEllipse(rect);
		} else {
			p.drawRoundedRect(rect, usual / 2, usual / 2);
		}
	}
	if (displayRightActionComments()) {
		const auto &icon = st::historyFastCommentsIcon;
		icon.paint(
			p,
			left + (size->width() - icon.width()) / 2,
			top + (st::historyFastShareSize - icon.height()) / 2,
			outerWidth);
		const auto views = data()->Get<HistoryMessageViews>();
		Assert(views != nullptr);
		if (views->repliesSmall.textWidth > 0) {
			p.setPen(st::msgServiceFg);
			p.setFont(st::semiboldFont);
			p.drawTextLeft(
				left + (size->width() - views->repliesSmall.textWidth) / 2,
				top + st::historyFastShareSize,
				outerWidth,
				views->repliesSmall.text,
				views->repliesSmall.textWidth);
		}
	} else {
		const auto &icon = (displayFastShare() && !isPinnedContext())
			? st::historyFastShareIcon
			: st::historyGoToOriginalIcon;
		icon.paintInCenter(p, { left, top, size->width(), size->height() });
	}
}

ClickHandlerPtr Message::rightActionLink() const {
	if (!_rightActionLink) {
		if (isPinnedContext()) {
			_rightActionLink = goToMessageClickHandler(data());
			return _rightActionLink;
		} else if (displayRightActionComments()) {
			_rightActionLink = createGoToCommentsLink();
			return _rightActionLink;
		}
		const auto owner = &data()->history()->owner();
		const auto itemId = data()->fullId();
		const auto forwarded = data()->Get<HistoryMessageForwarded>();
		const auto savedFromPeer = forwarded ? forwarded->savedFromPeer : nullptr;
		const auto savedFromMsgId = forwarded ? forwarded->savedFromMsgId : 0;
		const auto showByThread = std::make_shared<FnMut<void()>>();
		const auto showByThreadWeak = std::weak_ptr<FnMut<void()>>(showByThread);
		if (data()->externalReply()) {
			*showByThread = [=, requested = 0]() mutable {
				const auto original = savedFromPeer->owner().message(savedFromPeer->asChannel(), savedFromMsgId);
				if (original && original->replyToTop()) {
					App::wnd()->sessionController()->showRepliesForMessage(
						original->history(),
						original->replyToTop(),
						original->id,
						Window::SectionShow::Way::Forward);
				} else if (!requested) {
					const auto channel = savedFromPeer->asChannel();
					const auto prequested = &requested;
					requested = 1;
					channel->session().api().requestMessageData(channel, savedFromMsgId, [=](ChannelData *gotChannel, MsgId gotId) {
						if (const auto strong = showByThreadWeak.lock()) {
							*prequested = 2;
							(*strong)();
						}
					});
				} else if (requested == 2) {
					App::wnd()->sessionController()->showPeerHistory(
						savedFromPeer,
						Window::SectionShow::Way::Forward,
						savedFromMsgId);
				}
			};
		};
		_rightActionLink = std::make_shared<LambdaClickHandler>([=] {
			if (const auto item = owner->message(itemId)) {
				if (*showByThread) {
					(*showByThread)();
				} else if (savedFromPeer && savedFromMsgId) {
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
				if (const auto main = App::main()) { // multi good
					if (&main->session() == &owner->session()) {
						main->replyToItem(item);
					}
				}
			}
		});
	}
	return _fastReplyLink;
}

bool Message::isPinnedContext() const {
	return context() == Context::Pinned;
}

void Message::updateMediaInBubbleState() {
	const auto item = message();
	const auto media = this->media();

	auto mediaHasSomethingBelow = false;
	auto mediaHasSomethingAbove = false;
	auto getMediaHasSomethingAbove = [&] {
		return displayFromName()
			|| displayForwardedFrom()
			|| displayedReply()
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
	if (!_rightBadge.isEmpty()) {
		const auto badgeWidth = _rightBadge.maxWidth();
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
	if (selection.from == 0xFFFF) {
		return selection;
	}
	return HistoryView::UnshiftItemSelection(selection, message()->_text);
}

TextSelection Message::unskipTextSelection(TextSelection selection) const {
	return HistoryView::ShiftItemSelection(selection, message()->_text);
}

QRect Message::countGeometry() const {
	const auto commentsRoot = (context() == Context::Replies)
		&& data()->isDiscussionPost();
	const auto item = message();
	const auto media = this->media();
	const auto mediaWidth = (media && media->isDisplayed())
		? media->width()
		: width();
	const auto outbg = hasOutLayout();
	const auto availableWidth = width()
		- st::msgMargin.left()
		- (commentsRoot ? st::msgMargin.left() : st::msgMargin.right());
	auto contentLeft = (outbg && !Core::App().settings().chatWide())
		? st::msgMargin.right()
		: st::msgMargin.left();
	auto contentWidth = availableWidth;
	if (hasFromPhoto()) {
		contentLeft += st::msgPhotoSkip;
		if (const auto size = rightActionSize()) {
			contentWidth -= size->width() + (st::msgPhotoSkip - st::historyFastShareSize);
		}
	//} else if (!Adaptive::Wide() && !out() && !fromChannel() && st::msgPhotoSkip - (hmaxwidth - hwidth) > 0) {
	//	contentLeft += st::msgPhotoSkip - (hmaxwidth - hwidth);
	}
	accumulate_min(contentWidth, maxWidth());
	accumulate_min(contentWidth, _bubbleWidthLimit);
	if (mediaWidth < contentWidth) {
		const auto textualWidth = plainMaxWidth();
		if (mediaWidth < textualWidth
			&& (!media || !media->enforceBubbleWidth())) {
			accumulate_min(contentWidth, textualWidth);
		} else {
			contentWidth = mediaWidth;
		}
	}
	if (contentWidth < availableWidth && !Core::App().settings().chatWide()) {
		if (outbg) {
			contentLeft += availableWidth - contentWidth;
		} else if (commentsRoot) {
			contentLeft += (availableWidth - contentWidth) / 2;
		}
	} else if (contentWidth < availableWidth && commentsRoot) {
		contentLeft += ((st::msgMaxWidth + 2 * st::msgPhotoSkip) - contentWidth) / 2;
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
	const auto commentsRoot = (context() == Context::Replies)
		&& data()->isDiscussionPost();
	auto contentWidth = newWidth
		- st::msgMargin.left()
		- (commentsRoot ? st::msgMargin.left() : st::msgMargin.right());
	if (hasFromPhoto()) {
		if (const auto size = rightActionSize()) {
			contentWidth -= size->width() + (st::msgPhotoSkip - st::historyFastShareSize);
		}
	}
	accumulate_min(contentWidth, maxWidth());
	_bubbleWidthLimit = std::max(st::msgMaxWidth, monospaceMaxWidth());
	accumulate_min(contentWidth, _bubbleWidthLimit);
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
		auto reply = displayedReply();
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
			const auto forwarded = item->Get<HistoryMessageForwarded>();
			const auto skip1 = forwarded->psaType.isEmpty()
				? 0
				: st::historyPsaIconSkip1;
			const auto skip2 = forwarded->psaType.isEmpty()
				? 0
				: st::historyPsaIconSkip2;
			const auto fwdheight = ((forwarded->text.maxWidth() > (contentWidth - st::msgPadding.left() - st::msgPadding.right() - skip1)) ? 2 : 1) * st::semiboldFont->height;
			newHeight += fwdheight;
		}

		if (reply) {
			reply->resize(contentWidth - st::msgPadding.left() - st::msgPadding.right());
			newHeight += st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
		}

		if (item->repliesAreComments() || item->externalReply()) {
			newHeight += st::historyCommentsButtonHeight;
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
		if (!msgsigned->isAnonymousRank) {
			const auto text = (!edited || !editDate)
				? dateText
				: edited->text.toString();
			msgsigned->refresh(text);
		}
	}
	initTime();
}

void Message::initTime() {
	const auto item = message();
	if (const auto msgsigned = item->Get<HistoryMessageSigned>()
		; msgsigned && !msgsigned->isAnonymousRank) {
		item->_timeWidth = msgsigned->maxWidth();
	} else if (const auto edited = displayedEditBadge()) {
		item->_timeWidth = edited->maxWidth();
	} else {
		item->_timeText = dateTime().toString(cTimeFormat());
		item->_timeWidth = st::msgDateFont->width(item->_timeText);
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
