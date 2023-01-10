/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_message.h"

#include "core/click_handler_types.h" // ClickHandlerContext
#include "core/ui_integration.h"
#include "history/view/history_view_cursor_state.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/history_item_helpers.h"
#include "history/view/media/history_view_media.h"
#include "history/view/media/history_view_web_page.h"
#include "history/view/reactions/history_view_reactions.h"
#include "history/view/reactions/history_view_reactions_button.h"
#include "history/view/history_view_group_call_bar.h" // UserpicInRow.
#include "history/view/history_view_view_button.h" // ViewButton.
#include "history/history.h"
#include "boxes/share_box.h"
#include "ui/effects/ripple_animation.h"
#include "ui/effects/reaction_fly_animation.h"
#include "ui/chat/message_bubble.h"
#include "ui/chat/chat_style.h"
#include "ui/toast/toast.h"
#include "ui/text/text_utilities.h"
#include "ui/text/text_entity.h"
#include "ui/cached_round_corners.h"
#include "base/unixtime.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_channel.h"
#include "data/data_forum_topic.h"
#include "data/data_message_reactions.h"
#include "data/data_sponsored_messages.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "main/main_session.h"
#include "ui/text/text_options.h"
#include "ui/painter.h"
#include "window/window_session_controller.h"
#include "apiwrap.h"
#include "styles/style_widgets.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"

namespace HistoryView {
namespace {

constexpr auto kPlayStatusLimit = 2;
const auto kPsaTooltipPrefix = "cloud_lng_tooltip_psa_";

std::optional<Window::SessionController*> ExtractController(
		const ClickContext &context) {
	const auto my = context.other.value<ClickHandlerContext>();
	if (const auto controller = my.sessionWindow.get()) {
		return controller;
	}
	return std::nullopt;
}

class KeyboardStyle : public ReplyKeyboard::Style {
public:
	using ReplyKeyboard::Style::Style;

	Images::CornersMaskRef buttonRounding(
		Ui::BubbleRounding outer,
		RectParts sides) const override;

	void startPaint(
		QPainter &p,
		const Ui::ChatStyle *st) const override;
	const style::TextStyle &textStyle() const override;
	void repaint(not_null<const HistoryItem*> item) const override;

protected:
	void paintButtonBg(
		QPainter &p,
		const Ui::ChatStyle *st,
		const QRect &rect,
		Ui::BubbleRounding rounding,
		float64 howMuchOver) const override;
	void paintButtonIcon(
		QPainter &p,
		const Ui::ChatStyle *st,
		const QRect &rect,
		int outerWidth,
		HistoryMessageMarkupButton::Type type) const override;
	void paintButtonLoading(
		QPainter &p,
		const Ui::ChatStyle *st,
		const QRect &rect) const override;
	int minButtonWidth(HistoryMessageMarkupButton::Type type) const override;

};

void KeyboardStyle::startPaint(
		QPainter &p,
		const Ui::ChatStyle *st) const {
	Expects(st != nullptr);

	p.setPen(st->msgServiceFg());
}

const style::TextStyle &KeyboardStyle::textStyle() const {
	return st::serviceTextStyle;
}

void KeyboardStyle::repaint(not_null<const HistoryItem*> item) const {
	item->history()->owner().requestItemRepaint(item);
}

Images::CornersMaskRef KeyboardStyle::buttonRounding(
		Ui::BubbleRounding outer,
		RectParts sides) const {
	using namespace Images;
	using namespace Ui;
	using Radius = CachedCornerRadius;
	using Corner = BubbleCornerRounding;
	auto result = CornersMaskRef(CachedCornersMasks(Radius::BubbleSmall));
	if (sides & RectPart::Bottom) {
		const auto &large = CachedCornersMasks(Radius::BubbleLarge);
		auto round = [&](RectPart side, int index) {
			if ((sides & side) && (outer[index] == Corner::Large)) {
				result.p[index] = &large[index];
			}
		};
		round(RectPart::Left, kBottomLeft);
		round(RectPart::Right, kBottomRight);
	}
	return result;
}

void KeyboardStyle::paintButtonBg(
		QPainter &p,
		const Ui::ChatStyle *st,
		const QRect &rect,
		Ui::BubbleRounding rounding,
		float64 howMuchOver) const {
	Expects(st != nullptr);

	const auto sti = &st->imageStyle(false);
	const auto &small = sti->msgServiceBgCornersSmall;
	const auto &large = sti->msgServiceBgCornersLarge;
	auto corners = Ui::CornersPixmaps();
	using Corner = Ui::BubbleCornerRounding;
	for (auto i = 0; i != 4; ++i) {
		corners.p[i] = (rounding[i] == Corner::Large ? large : small).p[i];
	}
	Ui::FillRoundRect(p, rect, sti->msgServiceBg, corners);
	if (howMuchOver > 0) {
		auto o = p.opacity();
		p.setOpacity(o * howMuchOver);
		const auto &small = st->msgBotKbOverBgAddCornersSmall();
		const auto &large = st->msgBotKbOverBgAddCornersLarge();
		auto over = Ui::CornersPixmaps();
		for (auto i = 0; i != 4; ++i) {
			over.p[i] = (rounding[i] == Corner::Large ? large : small).p[i];
		}
		Ui::FillRoundRect(p, rect, st->msgBotKbOverBgAdd(), over);
		p.setOpacity(o);
	}
}

void KeyboardStyle::paintButtonIcon(
		QPainter &p,
		const Ui::ChatStyle *st,
		const QRect &rect,
		int outerWidth,
		HistoryMessageMarkupButton::Type type) const {
	Expects(st != nullptr);

	using Type = HistoryMessageMarkupButton::Type;
	const auto icon = [&]() -> const style::icon* {
		switch (type) {
		case Type::Url:
		case Type::Auth: return &st->msgBotKbUrlIcon();
		case Type::Buy: return &st->msgBotKbPaymentIcon();
		case Type::SwitchInlineSame:
		case Type::SwitchInline: return &st->msgBotKbSwitchPmIcon();
		case Type::WebView:
		case Type::SimpleWebView: return &st->msgBotKbWebviewIcon();
		}
		return nullptr;
	}();
	if (icon) {
		icon->paint(p, rect.x() + rect.width() - icon->width() - st::msgBotKbIconPadding, rect.y() + st::msgBotKbIconPadding, outerWidth);
	}
}

void KeyboardStyle::paintButtonLoading(
		QPainter &p,
		const Ui::ChatStyle *st,
		const QRect &rect) const {
	Expects(st != nullptr);

	const auto &icon = st->historySendingInvertedIcon();
	icon.paint(p, rect.x() + rect.width() - icon.width() - st::msgBotKbIconPadding, rect.y() + rect.height() - icon.height() - st::msgBotKbIconPadding, rect.x() * 2 + rect.width());
}

int KeyboardStyle::minButtonWidth(
		HistoryMessageMarkupButton::Type type) const {
	using Type = HistoryMessageMarkupButton::Type;
	int result = 2 * buttonPadding(), iconWidth = 0;
	switch (type) {
	case Type::Url:
	case Type::Auth: iconWidth = st::msgBotKbUrlIcon.width(); break;
	case Type::Buy: iconWidth = st::msgBotKbPaymentIcon.width(); break;
	case Type::SwitchInlineSame:
	case Type::SwitchInline: iconWidth = st::msgBotKbSwitchPmIcon.width(); break;
	case Type::Callback:
	case Type::CallbackWithPassword:
	case Type::Game: iconWidth = st::historySendingInvertedIcon.width(); break;
	case Type::WebView:
	case Type::SimpleWebView: iconWidth = st::msgBotKbWebviewIcon.width(); break;
	}
	if (iconWidth > 0) {
		result = std::max(result, 2 * iconWidth + 4 * int(st::msgBotKbIconPadding));
	}
	return result;
}

QString FastReplyText() {
	return tr::lng_fast_reply(tr::now);
}

[[nodiscard]] ClickHandlerPtr MakeTopicButtonLink(
		not_null<Data::ForumTopic*> topic,
		MsgId messageId) {
	const auto weak = base::make_weak(topic);
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		if (const auto controller = my.sessionWindow.get()) {
			if (const auto strong = weak.get()) {
				controller->showTopic(
					strong,
					messageId,
					Window::SectionShow::Way::Forward);
			}
		}
	});
}

} // namespace

style::color FromNameFg(
		const Ui::ChatPaintContext &context,
		PeerId peerId) {
	const auto st = context.st;
	if (context.selected()) {
		const style::color colors[] = {
			st->historyPeer1NameFgSelected(),
			st->historyPeer2NameFgSelected(),
			st->historyPeer3NameFgSelected(),
			st->historyPeer4NameFgSelected(),
			st->historyPeer5NameFgSelected(),
			st->historyPeer6NameFgSelected(),
			st->historyPeer7NameFgSelected(),
			st->historyPeer8NameFgSelected(),
		};
		return colors[Data::PeerColorIndex(peerId)];
	} else {
		const style::color colors[] = {
			st->historyPeer1NameFg(),
			st->historyPeer2NameFg(),
			st->historyPeer3NameFg(),
			st->historyPeer4NameFg(),
			st->historyPeer5NameFg(),
			st->historyPeer6NameFg(),
			st->historyPeer7NameFg(),
			st->historyPeer8NameFg(),
		};
		return colors[Data::PeerColorIndex(peerId)];
	}
}

struct Message::CommentsButton {
	std::unique_ptr<Ui::RippleAnimation> ripple;
	std::vector<UserpicInRow> userpics;
	QImage cachedUserpics;
	ClickHandlerPtr link;
	QPoint lastPoint;
	int rippleShift = 0;
};

struct Message::FromNameStatus {
	DocumentId id = 0;
	std::unique_ptr<Ui::Text::CustomEmoji> custom;
	int skip = 0;
};

struct Message::RightAction {
	std::unique_ptr<Ui::RippleAnimation> ripple;
	ClickHandlerPtr link;
	QPoint lastPoint;
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
	not_null<HistoryItem*> data,
	Element *replacing)
: Element(delegate, data, replacing, Flag(0))
, _bottomInfo(
		&data->history()->owner().reactions(),
		BottomInfoDataFromMessage(this)) {
	initLogEntryOriginal();
	initPsa();
	refreshReactions();
	auto animations = replacing
		? replacing->takeReactionAnimations()
		: base::flat_map<
			Data::ReactionId,
			std::unique_ptr<Ui::ReactionFlyAnimation>>();
	if (!animations.empty()) {
		const auto repainter = [=] { repaint(); };
		for (const auto &[id, animation] : animations) {
			animation->setRepaintCallback(repainter);
		}
		if (_reactions) {
			_reactions->continueAnimations(std::move(animations));
		} else {
			_bottomInfo.continueReactionAnimations(std::move(animations));
		}
	}
}

Message::~Message() {
	if (_comments || (_fromNameStatus && _fromNameStatus->custom)) {
		_comments = nullptr;
		_fromNameStatus = nullptr;
		checkHeavyPart();
	}
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
		const auto i = info->admins.find(peerToUser(user->id));
		const auto custom = (i != info->admins.end())
			? i->second
			: (info->creator == user)
			? info->creatorRank
			: QString();
		return !custom.isEmpty()
			? custom
			: (info->creator == user)
			? tr::lng_owner_badge(tr::now)
			: (i != info->admins.end())
			? tr::lng_admin_badge(tr::now)
			: QString();
	}();
	const auto badge = text.isEmpty()
		? delegate()->elementAuthorRank(this)
		: TextUtilities::RemoveEmoji(TextUtilities::SingleLine(text));
	if (badge.isEmpty()) {
		_rightBadge.clear();
	} else {
		_rightBadge.setText(st::defaultTextStyle, badge);
	}
}

void Message::applyGroupAdminChanges(
		const base::flat_set<UserId> &changes) {
	if (!data()->out()
		&& changes.contains(peerToUser(data()->author()->id))) {
		history()->owner().requestViewResize(this);
	}
}

void Message::animateReaction(Ui::ReactionFlyAnimationArgs &&args) {
	const auto item = data();
	const auto media = this->media();

	auto g = countGeometry();
	if (g.width() < 1 || isHidden()) {
		return;
	}
	const auto repainter = [=] { repaint(); };

	const auto bubble = drawBubble();
	const auto reactionsInBubble = _reactions && embedReactionsInBubble();
	const auto mediaDisplayed = media && media->isDisplayed();
	const auto keyboard = item->inlineReplyKeyboard();
	auto keyboardHeight = 0;
	if (keyboard) {
		keyboardHeight = keyboard->naturalHeight();
		g.setHeight(g.height() - st::msgBotKbButton.margin - keyboardHeight);
	}

	if (_reactions && !reactionsInBubble) {
		const auto reactionsHeight = st::mediaInBubbleSkip + _reactions->height();
		const auto reactionsLeft = (!bubble && mediaDisplayed)
			? media->contentRectForReactions().x()
			: 0;
		g.setHeight(g.height() - reactionsHeight);
		const auto reactionsPosition = QPoint(reactionsLeft + g.left(), g.top() + g.height() + st::mediaInBubbleSkip);
		_reactions->animate(args.translated(-reactionsPosition), repainter);
		return;
	}

	const auto animateInBottomInfo = [&](QPoint bottomRight) {
		_bottomInfo.animateReaction(args.translated(-bottomRight), repainter);
	};
	if (bubble) {
		auto entry = logEntryOriginal();

		// Entry page is always a bubble bottom.
		auto mediaOnBottom = (mediaDisplayed && media->isBubbleBottom()) || (entry/* && entry->isBubbleBottom()*/);
		auto mediaOnTop = (mediaDisplayed && media->isBubbleTop()) || (entry && entry->isBubbleTop());

		auto inner = g;
		if (_comments) {
			inner.setHeight(inner.height() - st::historyCommentsButtonHeight);
		}
		auto trect = inner.marginsRemoved(st::msgPadding);
		const auto reactionsTop = (reactionsInBubble && !_viewButton)
			? st::mediaInBubbleSkip
			: 0;
		const auto reactionsHeight = reactionsInBubble
			? (reactionsTop + _reactions->height())
			: 0;
		if (reactionsInBubble) {
			trect.setHeight(trect.height() - reactionsHeight);
			const auto reactionsPosition = QPoint(trect.left(), trect.top() + trect.height() + reactionsTop);
			_reactions->animate(args.translated(-reactionsPosition), repainter);
			return;
		}
		if (_viewButton) {
			const auto belowInfo = _viewButton->belowMessageInfo();
			const auto infoHeight = reactionsInBubble
				? (reactionsHeight + 2 * st::mediaInBubbleSkip)
				: _bottomInfo.height();
			const auto heightMargins = QMargins(0, 0, 0, infoHeight);
			if (belowInfo) {
				inner -= heightMargins;
			}
			trect.setHeight(trect.height() - _viewButton->height());
			if (reactionsInBubble) {
				trect.setHeight(trect.height() - st::mediaInBubbleSkip + st::msgPadding.bottom());
			} else if (mediaDisplayed) {
				trect.setHeight(trect.height() - st::mediaInBubbleSkip);
			}
		}
		if (mediaOnBottom) {
			trect.setHeight(trect.height()
				+ st::msgPadding.bottom()
				- viewButtonHeight());
		}
		if (mediaOnTop) {
			trect.setY(trect.y() - st::msgPadding.top());
		}
		if (mediaDisplayed && mediaOnBottom && media->customInfoLayout()) {
			auto mediaHeight = media->height();
			auto mediaLeft = trect.x() - st::msgPadding.left();
			auto mediaTop = (trect.y() + trect.height() - mediaHeight);
			animateInBottomInfo(QPoint(mediaLeft, mediaTop) + media->resolveCustomInfoRightBottom());
		} else {
			animateInBottomInfo({
				inner.left() + inner.width() - (st::msgPadding.right() - st::msgDateDelta.x()),
				inner.top() + inner.height() - (st::msgPadding.bottom() - st::msgDateDelta.y()),
			});
		}
	} else if (mediaDisplayed) {
		animateInBottomInfo(g.topLeft() + media->resolveCustomInfoRightBottom());
	}
}

auto Message::takeReactionAnimations()
-> base::flat_map<
		Data::ReactionId,
		std::unique_ptr<Ui::ReactionFlyAnimation>> {
	return _reactions
		? _reactions->takeAnimations()
		: _bottomInfo.takeReactionAnimations();
}

QSize Message::performCountOptimalSize() {
	const auto item = data();
	const auto markup = item->inlineReplyMarkup();
	const auto reactionsKey = [&] {
		return embedReactionsInBottomInfo()
			? 0
			: embedReactionsInBubble()
			? 1
			: 2;
	};
	const auto oldKey = reactionsKey();
	refreshIsTopicRootReply();
	validateText();
	validateInlineKeyboard(markup);
	updateViewButtonExistence();
	refreshTopicButton();
	updateMediaInBubbleState();
	if (oldKey != reactionsKey()) {
		refreshReactions();
	}
	refreshRightBadge();
	refreshInfoSkipBlock();

	const auto media = this->media();

	auto maxWidth = 0;
	auto minHeight = 0;

	const auto reactionsInBubble = _reactions && embedReactionsInBubble();
	if (_reactions) {
		_reactions->initDimensions();
	}
	if (drawBubble()) {
		const auto forwarded = item->Get<HistoryMessageForwarded>();
		const auto reply = displayedReply();
		const auto via = item->Get<HistoryMessageVia>();
		const auto entry = logEntryOriginal();
		if (forwarded) {
			forwarded->create(via);
		}
		if (reply) {
			reply->updateName(item);
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
		maxWidth = plainMaxWidth();
		if (context() == Context::Replies && item->isDiscussionPost()) {
			maxWidth = std::max(maxWidth, st::msgMaxWidth);
		}
		minHeight = hasVisibleText() ? text().minHeight() : 0;
		if (reactionsInBubble) {
			const auto reactionsMaxWidth = st::msgPadding.left()
				+ _reactions->maxWidth()
				+ st::msgPadding.right();
			accumulate_max(
				maxWidth,
				std::min(st::msgMaxWidth, reactionsMaxWidth));
			if (!mediaDisplayed || _viewButton) {
				minHeight += st::mediaInBubbleSkip;
			}
			if (maxWidth >= reactionsMaxWidth) {
				minHeight += _reactions->minHeight();
			} else {
				const auto widthForReactions = maxWidth
					- st::msgPadding.left()
					- st::msgPadding.right();
				minHeight += _reactions->resizeGetHeight(widthForReactions);
			}
		}
		if (!mediaOnBottom && (!_viewButton || !reactionsInBubble)) {
			minHeight += st::msgPadding.bottom();
			if (mediaDisplayed) {
				minHeight += st::mediaInBubbleSkip;
			}
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
				const auto innerWidth = maxWidth
					- st::msgPadding.left()
					- st::msgPadding.right();
				if (hasVisibleText() && maxWidth < plainMaxWidth()) {
					minHeight -= text().minHeight();
					minHeight += text().countHeight(innerWidth);
				}
				if (reactionsInBubble) {
					minHeight -= _reactions->minHeight();
					minHeight
						+= _reactions->countCurrentSize(innerWidth).height();
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
				validateFromNameText(from);
				const auto &name = from
					? _fromName
					: item->hiddenSenderInfo()->nameText();
				auto namew = st::msgPadding.left()
					+ name.maxWidth()
					+ (_fromNameStatus ? st::dialogsPremiumIcon.width() : 0)
					+ st::msgPadding.right();
				if (via && !displayForwardedFrom()) {
					namew += st::msgServiceFont->spacew + via->maxWidth
						+ (_fromNameStatus ? st::msgServiceFont->spacew : 0);
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
			if (displayedTopicButton()) {
				const auto padding = st::msgPadding + st::topicButtonPadding;
				accumulate_max(
					maxWidth,
					(padding.left()
						+ _topicButton->name.maxWidth()
						+ st::topicButtonArrowSkip
						+ padding.right()));
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
	// if we have a text bubble we can resize it to fit the keyboard
	// but if we have only media we don't do that
	if (markup && markup->inlineKeyboard && hasVisibleText()) {
		accumulate_max(maxWidth, markup->inlineKeyboard->naturalWidth());
	}
	return QSize(maxWidth, minHeight);
}

void Message::refreshTopicButton() {
	const auto item = data();
	if (isAttachedToPrevious() || context() != Context::History) {
		_topicButton = nullptr;
	} else if (const auto topic = item->topic()) {
		if (!_topicButton) {
			_topicButton = std::make_unique<TopicButton>();
		}
		const auto jumpToId = IsServerMsgId(item->id) ? item->id : MsgId();
		_topicButton->link = MakeTopicButtonLink(topic, jumpToId);
		if (_topicButton->nameVersion != topic->titleVersion()) {
			_topicButton->nameVersion = topic->titleVersion();
			const auto context = Core::MarkedTextContext{
				.session = &history()->session(),
				.customEmojiRepaint = [=] { customEmojiRepaint(); },
				.customEmojiLoopLimit = 1,
			};
			_topicButton->name.setMarkedText(
				st::fwdTextStyle,
				topic->titleWithIcon(),
				kMarkupTextOptions,
				context);
		}
	} else {
		_topicButton = nullptr;
	}
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

void Message::draw(Painter &p, const PaintContext &context) const {
	auto g = countGeometry();
	if (g.width() < 1) {
		return;
	}

	const auto item = data();
	const auto media = this->media();

	const auto stm = context.messageStyle();
	const auto bubble = drawBubble();

	if (const auto bar = Get<UnreadBar>()) {
		auto unreadbarh = bar->height();
		auto dateh = 0;
		if (const auto date = Get<DateBadge>()) {
			dateh = date->height();
		}
		if (context.clip.intersects(QRect(0, dateh, width(), unreadbarh))) {
			p.translate(0, dateh);
			bar->paint(
				p,
				context,
				0,
				width(),
				delegate()->elementIsChatWide());
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

	const auto displayInfo = needInfoDisplay();
	const auto reactionsInBubble = _reactions && embedReactionsInBubble();

	auto mediaSelectionIntervals = (!context.selected() && mediaDisplayed)
		? media->getBubbleSelectionIntervals(context.selection)
		: std::vector<Ui::BubbleSelectionInterval>();
	auto localMediaTop = 0;
	const auto customHighlight = mediaDisplayed && media->customHighlight();
	if (!mediaSelectionIntervals.empty() || customHighlight) {
		auto localMediaBottom = g.top() + g.height();
		if (data()->repliesAreComments() || data()->externalReply()) {
			localMediaBottom -= st::historyCommentsButtonHeight;
		}
		if (_viewButton) {
			localMediaBottom -= st::mediaInBubbleSkip + _viewButton->height();
		}
		if (reactionsInBubble) {
			localMediaBottom -= st::mediaInBubbleSkip + _reactions->height();
		}
		if (!mediaOnBottom && (!_viewButton || !reactionsInBubble)) {
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
		media->drawHighlight(p, context, localMediaTop);
	} else {
		paintHighlight(p, context, g.height());
	}

	const auto roll = media ? media->bubbleRoll() : Media::BubbleRoll();
	if (roll) {
		p.save();
		p.translate(g.center());
		p.rotate(roll.rotate);
		p.scale(roll.scale, roll.scale);
		p.translate(-g.center());
	}

	p.setTextPalette(stm->textPalette);

	const auto keyboard = item->inlineReplyKeyboard();
	const auto messageRounding = countMessageRounding();
	if (keyboard) {
		const auto keyboardHeight = st::msgBotKbButton.margin + keyboard->naturalHeight();
		g.setHeight(g.height() - keyboardHeight);
		const auto keyboardPosition = QPoint(g.left(), g.top() + g.height() + st::msgBotKbButton.margin);
		p.translate(keyboardPosition);
		keyboard->paint(
			p,
			context.st,
			messageRounding,
			g.width(),
			context.clip.translated(-keyboardPosition));
		p.translate(-keyboardPosition);
	}

	if (_reactions && !reactionsInBubble) {
		const auto reactionsHeight = st::mediaInBubbleSkip + _reactions->height();
		const auto reactionsLeft = (!bubble && mediaDisplayed)
			? media->contentRectForReactions().x()
			: 0;
		g.setHeight(g.height() - reactionsHeight);
		const auto reactionsPosition = QPoint(reactionsLeft + g.left(), g.top() + g.height() + st::mediaInBubbleSkip);
		p.translate(reactionsPosition);
		prepareCustomEmojiPaint(p, context, *_reactions);
		_reactions->paint(p, context, g.width(), context.clip.translated(-reactionsPosition));
		if (context.reactionInfo) {
			context.reactionInfo->position = reactionsPosition;
		}
		p.translate(-reactionsPosition);
	}

	if (bubble) {
		if (displayFromName()
			&& item->displayFrom()
			&& (_fromNameVersion < item->displayFrom()->nameVersion())) {
			fromNameUpdated(g.width());
		}
		Ui::PaintBubble(
			p,
			Ui::ComplexBubble{
				.simple = Ui::SimpleBubble{
					.st = context.st,
					.geometry = g,
					.pattern = context.bubblesPattern,
					.patternViewport = context.viewport,
					.outerWidth = width(),
					.selected = context.selected(),
					.outbg = context.outbg,
					.rounding = countBubbleRounding(messageRounding),
				},
				.selection = mediaSelectionIntervals,
			});

		auto inner = g;
		paintCommentsButton(p, inner, context);

		auto trect = inner.marginsRemoved(st::msgPadding);

		const auto reactionsTop = (reactionsInBubble && !_viewButton)
			? st::mediaInBubbleSkip
			: 0;
		const auto reactionsHeight = reactionsInBubble
			? (reactionsTop + _reactions->height())
			: 0;
		if (reactionsInBubble) {
			trect.setHeight(trect.height() - reactionsHeight);
			const auto reactionsPosition = QPoint(trect.left(), trect.top() + trect.height() + reactionsTop);
			p.translate(reactionsPosition);
			prepareCustomEmojiPaint(p, context, *_reactions);
			_reactions->paint(p, context, g.width(), context.clip.translated(-reactionsPosition));
			if (context.reactionInfo) {
				context.reactionInfo->position = reactionsPosition;
			}
			p.translate(-reactionsPosition);
		}

		if (_viewButton) {
			const auto belowInfo = _viewButton->belowMessageInfo();
			const auto infoHeight = reactionsInBubble
				? (reactionsHeight + 2 * st::mediaInBubbleSkip)
				: _bottomInfo.height();
			const auto heightMargins = QMargins(0, 0, 0, infoHeight);
			_viewButton->draw(
				p,
				_viewButton->countRect(belowInfo
					? inner
					: inner - heightMargins),
				context);
			if (belowInfo) {
				inner.setHeight(inner.height() - _viewButton->height());
			}
			trect.setHeight(trect.height() - _viewButton->height());
			if (reactionsInBubble) {
				trect.setHeight(trect.height() - st::mediaInBubbleSkip + st::msgPadding.bottom());
			} else if (mediaDisplayed) {
				trect.setHeight(trect.height() - st::mediaInBubbleSkip);
			}
		}

		if (mediaOnBottom) {
			trect.setHeight(trect.height() + st::msgPadding.bottom());
		}
		if (mediaOnTop) {
			trect.setY(trect.y() - st::msgPadding.top());
		} else {
			paintFromName(p, trect, context);
			paintTopicButton(p, trect, context);
			paintForwardedInfo(p, trect, context);
			paintReplyInfo(p, trect, context);
			paintViaBotIdInfo(p, trect, context);
		}
		if (entry) {
			trect.setHeight(trect.height() - entry->height());
		}
		if (displayInfo) {
			trect.setHeight(trect.height()
				- (_bottomInfo.height() - st::msgDateFont->height));
		}
		paintText(p, trect, context);
		if (mediaDisplayed) {
			auto mediaHeight = media->height();
			auto mediaPosition = QPoint(
				inner.left(),
				trect.y() + trect.height() - mediaHeight);
			p.translate(mediaPosition);
			media->draw(p, context.translated(
				-mediaPosition
			).withSelection(skipTextSelection(context.selection)));
			if (context.reactionInfo && !displayInfo && !_reactions) {
				const auto add = QPoint(0, mediaHeight);
				context.reactionInfo->position = mediaPosition + add;
				if (context.reactionInfo->effectPaint) {
					context.reactionInfo->effectOffset -= add;
				}
			}
			p.translate(-mediaPosition);
		}
		if (entry) {
			auto entryLeft = inner.left();
			auto entryTop = trect.y() + trect.height();
			p.translate(entryLeft, entryTop);
			auto entryContext = context.translated(-entryLeft, -entryTop);
			entryContext.selection = skipTextSelection(context.selection);
			if (mediaDisplayed) {
				entryContext.selection = media->skipSelection(
					entryContext.selection);
			}
			entry->draw(p, entryContext);
			p.translate(-entryLeft, -entryTop);
		}
		if (displayInfo) {
			const auto bottomSelected = context.selected()
				|| (!mediaSelectionIntervals.empty()
					&& (mediaSelectionIntervals.back().top
						+ mediaSelectionIntervals.back().height
						>= inner.y() + inner.height()));
			drawInfo(
				p,
				context.withSelection(
					bottomSelected ? FullSelection : TextSelection()),
				inner.left() + inner.width(),
				inner.top() + inner.height(),
				2 * inner.left() + inner.width(),
				InfoDisplayType::Default);
			if (context.reactionInfo && !_reactions) {
				const auto add = QPoint(0, inner.top() + inner.height());
				context.reactionInfo->position = add;
				if (context.reactionInfo->effectPaint) {
					context.reactionInfo->effectOffset -= add;
				}
			}
			if (_comments) {
				const auto o = p.opacity();
				p.setOpacity(0.3);
				p.fillRect(g.left(), g.top() + g.height() - st::historyCommentsButtonHeight - st::lineWidth, g.width(), st::lineWidth, stm->msgDateFg);
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
			drawRightAction(p, context, fastShareLeft, fastShareTop, width());
		}

		if (media) {
			media->paintBubbleFireworks(p, g, context.now);
		}
	} else if (media && media->isDisplayed()) {
		p.translate(g.topLeft());
		media->draw(p, context.translated(
			-g.topLeft()
		).withSelection(skipTextSelection(context.selection)));
		if (context.reactionInfo && !_reactions) {
			const auto add = QPoint(0, g.height());
			context.reactionInfo->position = g.topLeft() + add;
			if (context.reactionInfo->effectPaint) {
				context.reactionInfo->effectOffset -= add;
			}
		}
		p.translate(-g.topLeft());
	}

	p.restoreTextPalette();

	if (roll) {
		p.restore();
	}

	if (const auto reply = displayedReply()) {
		if (reply->isNameUpdated(data())) {
			const_cast<Message*>(this)->setPendingResize();
		}
	}
}

void Message::paintCommentsButton(
		Painter &p,
		QRect &g,
		const PaintContext &context) const {
	if (!data()->repliesAreComments() && !data()->externalReply()) {
		return;
	}
	if (!_comments) {
		_comments = std::make_unique<CommentsButton>();
		history()->owner().registerHeavyViewPart(const_cast<Message*>(this));
	}
	const auto stm = context.messageStyle();
	const auto views = data()->Get<HistoryMessageViews>();

	g.setHeight(g.height() - st::historyCommentsButtonHeight);
	const auto top = g.top() + g.height();
	auto left = g.left();
	auto width = g.width();

	if (_comments->ripple) {
		p.setOpacity(st::historyPollRippleOpacity);
		const auto colorOverride = &stm->msgWaveformInactive->c;
		_comments->ripple->paint(
			p,
			left - _comments->rippleShift,
			top,
			width,
			colorOverride);
		if (_comments->ripple->empty()) {
			_comments->ripple.reset();
		}
		p.setOpacity(1.);
	}

	left += st::historyCommentsSkipLeft;
	width -= st::historyCommentsSkipLeft
		+ st::historyCommentsSkipRight;

	const auto &open = stm->historyCommentsOpen;
	open.paint(p,
		left + width - open.width(),
		top + (st::historyCommentsButtonHeight - open.height()) / 2,
		width);

	if (!views || views->recentRepliers.empty()) {
		const auto &icon = stm->historyComments;
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
				const auto wasView = view.cloud.get();
				if (views->recentRepliers[i] != peer->id
					|| peer->userpicUniqueKey(view) != entry.uniqueKey
					|| view.cloud.get() != wasView) {
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
	p.setPen(stm->msgFileThumbLinkFg);
	p.setFont(st::semiboldFont);

	const auto textTop = top + (st::historyCommentsButtonHeight - st::semiboldFont->height) / 2;
	p.drawTextLeft(
		left,
		textTop,
		width,
		views ? views->replies.text : tr::lng_replies_view_original(tr::now),
		views ? views->replies.textWidth : -1);

	if (views && data()->areCommentsUnread()) {
		p.setPen(Qt::NoPen);
		p.setBrush(stm->msgFileBg);

		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(style::rtlrect(left + views->replies.textWidth + st::mediaUnreadSkip, textTop + st::mediaUnreadTop, st::mediaUnreadSize, st::mediaUnreadSize, width));
		}
	}
}

void Message::paintFromName(
		Painter &p,
		QRect &trect,
		const PaintContext &context) const {
	const auto item = data();
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

	const auto stm = context.messageStyle();
	const auto from = item->displayFrom();
	const auto info = from ? nullptr : item->hiddenSenderInfo();
	Assert(from || info);
	const auto service = (context.outbg || item->isPost());
	const auto st = context.st;
	const auto nameFg = !service
		? FromNameFg(context, from ? from->id : info->colorPeerId)
		: item->isSponsored()
		? st->boxTextFgGood()
		: stm->msgServiceFg;
	const auto nameText = [&] {
		if (from) {
			validateFromNameText(from);
			return static_cast<const Ui::Text::String*>(&_fromName);
		}
		return &info->nameText();
	}();
	const auto statusWidth = _fromNameStatus
		? st::dialogsPremiumIcon.width()
		: 0;
	if (statusWidth && availableWidth > statusWidth) {
		const auto x = availableLeft
			+ std::min(availableWidth - statusWidth, nameText->maxWidth());
		const auto y = trect.top();
		const auto color = QColor(
			nameFg->c.red(),
			nameFg->c.green(),
			nameFg->c.blue(),
			nameFg->c.alpha() * 115 / 255);
		const auto user = from->asUser();
		const auto id = user ? user->emojiStatusId() : 0;
		if (_fromNameStatus->id != id) {
			const auto that = const_cast<Message*>(this);
			_fromNameStatus->custom = id
				? std::make_unique<Ui::Text::LimitedLoopsEmoji>(
					user->owner().customEmojiManager().create(
						id,
						[=] { that->customEmojiRepaint(); }),
					kPlayStatusLimit)
				: nullptr;
			if (id && !_fromNameStatus->id) {
				history()->owner().registerHeavyViewPart(that);
			} else if (!id && _fromNameStatus->id) {
				that->checkHeavyPart();
			}
			_fromNameStatus->id = id;
		}
		if (_fromNameStatus->custom) {
			clearCustomEmojiRepaint();
			_fromNameStatus->custom->paint(p, {
				.textColor = color,
				.now = context.now,
				.position = QPoint(
					x - 2 * _fromNameStatus->skip,
					y + _fromNameStatus->skip),
				.paused = context.paused,
			});
		} else {
			st::dialogsPremiumIcon.paint(p, x, y, width(), color);
		}
		availableWidth -= statusWidth;
	}
	p.setFont(st::msgNameFont);
	p.setPen(nameFg);
	nameText->drawElided(p, availableLeft, trect.top(), availableWidth);
	const auto skipWidth = nameText->maxWidth()
		+ (_fromNameStatus
			? (st::dialogsPremiumIcon.width() + st::msgServiceFont->spacew)
			: 0)
		+ st::msgServiceFont->spacew;
	availableLeft += skipWidth;
	availableWidth -= skipWidth;

	auto via = item->Get<HistoryMessageVia>();
	if (via && !displayForwardedFrom() && availableWidth > 0) {
		p.setPen(stm->msgServiceFg);
		p.drawText(availableLeft, trect.top() + st::msgServiceFont->ascent, via->text);
		auto skipWidth = via->width + st::msgServiceFont->spacew;
		availableLeft += skipWidth;
		availableWidth -= skipWidth;
	}
	if (rightWidth) {
		p.setPen(stm->msgDateFg);
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

void Message::paintTopicButton(
		Painter &p,
		QRect &trect,
		const PaintContext &context) const {
	const auto button = displayedTopicButton();
	if (!button) {
		return;
	}
	trect.setTop(trect.top() + st::topicButtonSkip);
	const auto padding = st::topicButtonPadding;
	const auto availableWidth = trect.width();
	const auto height = padding.top()
		+ st::msgNameFont->height
		+ padding.bottom();
	const auto width = std::max(
		std::min(
			availableWidth,
			(padding.left()
				+ button->name.maxWidth()
				+ st::topicButtonArrowSkip
				+ padding.right())),
		height);
	const auto rect = QRect(trect.x(), trect.y(), width, height);

	const auto stm = context.messageStyle();
	const auto skip = padding.right() + st::topicButtonArrowSkip;
	auto color = stm->msgServiceFg->c;
	color.setAlpha(color.alpha() / 8);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	{
		auto hq = PainterHighQualityEnabler(p);
		p.drawRoundedRect(rect, height / 2, height / 2);
	}
	if (button->ripple) {
		button->ripple->paint(
			p,
			rect.x(),
			rect.y(),
			this->width(),
			&color);
		if (button->ripple->empty()) {
			button->ripple.reset();
		}
	}
	clearCustomEmojiRepaint();
	p.setPen(stm->msgServiceFg);
	p.setTextPalette(stm->fwdTextPalette);
	button->name.drawElided(
		p,
		trect.x() + padding.left(),
		trect.y() + padding.top(),
		width - padding.left() - skip);

	const auto &icon = st::topicButtonArrow;
	icon.paint(
		p,
		rect.x() + rect.width() - skip + st::topicButtonArrowPosition.x(),
		rect.y() + padding.top() + st::topicButtonArrowPosition.y(),
		this->width(),
		stm->msgServiceFg->c);

	trect.setY(trect.y() + height + st::topicButtonSkip);
}

void Message::paintForwardedInfo(
		Painter &p,
		QRect &trect,
		const PaintContext &context) const {
	if (displayForwardedFrom()) {
		const auto item = data();
		const auto st = context.st;
		const auto stm = context.messageStyle();
		const auto forwarded = item->Get<HistoryMessageForwarded>();

		const auto &serviceFont = st::msgServiceFont;
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
			? st->boxTextFgGood()
			: stm->msgServiceFg);
		p.setFont(serviceFont);
		p.setTextPalette(!forwarded->psaType.isEmpty()
			? st->historyPsaForwardPalette()
			: stm->fwdTextPalette);
		forwarded->text.drawElided(p, trect.x(), trect.y(), useWidth, 2, style::al_left, 0, -1, 0, breakEverywhere);
		p.setTextPalette(stm->textPalette);

		if (!forwarded->psaType.isEmpty()) {
			const auto entry = Get<PsaTooltipState>();
			Assert(entry != nullptr);
			const auto shown = entry->buttonVisibleAnimation.value(
				entry->buttonVisible ? 1. : 0.);
			if (shown > 0) {
				const auto &icon = stm->historyPsaIcon;
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

void Message::paintReplyInfo(
		Painter &p,
		QRect &trect,
		const PaintContext &context) const {
	if (const auto reply = displayedReply()) {
		int32 h = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
		reply->paint(p, this, context, trect.x(), trect.y(), trect.width(), true);
		trect.setY(trect.y() + h);
	}
}

void Message::paintViaBotIdInfo(
		Painter &p,
		QRect &trect,
		const PaintContext &context) const {
	const auto item = data();
	if (!displayFromName() && !displayForwardedFrom()) {
		if (auto via = item->Get<HistoryMessageVia>()) {
			const auto stm = context.messageStyle();
			p.setFont(st::msgServiceNameFont);
			p.setPen(stm->msgServiceFg);
			p.drawTextLeft(trect.left(), trect.top(), width(), via->text);
			trect.setY(trect.y() + st::msgServiceNameFont->height);
		}
	}
}

void Message::paintText(
		Painter &p,
		QRect &trect,
		const PaintContext &context) const {
	if (!hasVisibleText()) {
		return;
	}
	const auto stm = context.messageStyle();
	p.setPen(stm->historyTextFg);
	p.setFont(st::msgFont);
	prepareCustomEmojiPaint(p, context, text());
	text().draw(p, {
		.position = trect.topLeft(),
		.availableWidth = trect.width(),
		.palette = &stm->textPalette,
		.spoiler = Ui::Text::DefaultSpoilerCache(),
		.now = context.now,
		.paused = context.paused,
		.selection = context.selection,
	});
}

PointState Message::pointState(QPoint point) const {
	auto g = countGeometry();
	if (g.width() < 1 || isHidden()) {
		return PointState::Outside;
	}

	const auto media = this->media();
	const auto item = data();
	const auto reactionsInBubble = _reactions && embedReactionsInBubble();
	if (drawBubble()) {
		if (!g.contains(point)) {
			return PointState::Outside;
		}
		if (const auto mediaDisplayed = media && media->isDisplayed()) {
			// Hack for grouped media point state.
			auto entry = logEntryOriginal();

			// Entry page is always a bubble bottom.
			auto mediaOnBottom = (mediaDisplayed && media->isBubbleBottom()) || (entry/* && entry->isBubbleBottom()*/);

			if (item->repliesAreComments() || item->externalReply()) {
				g.setHeight(g.height() - st::historyCommentsButtonHeight);
			}

			auto trect = g.marginsRemoved(st::msgPadding);
			if (reactionsInBubble) {
				const auto reactionsHeight = (_viewButton ? 0 : st::mediaInBubbleSkip)
					+ _reactions->height();
				trect.setHeight(trect.height() - reactionsHeight);
			}
			if (_viewButton) {
				trect.setHeight(trect.height() - _viewButton->height());
				if (reactionsInBubble) {
					trect.setHeight(trect.height() + st::msgPadding.bottom());
				} else if (mediaDisplayed) {
					trect.setHeight(trect.height() - st::mediaInBubbleSkip);
				}
			}
			if (mediaOnBottom) {
				trect.setHeight(trect.height() + st::msgPadding.bottom());
			}
			//if (mediaOnTop) {
			//	trect.setY(trect.y() - st::msgPadding.top());
			//} else {
			//	if (getStateFromName(point, trect, &result)) return result;
			//	if (getStateTopicButton(point, trect, &result)) return result;
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
	if (const auto markup = data()->Get<HistoryMessageReplyMarkup>()) {
		if (const auto keyboard = markup->inlineKeyboard.get()) {
			keyboard->clickHandlerPressedChanged(
				handler,
				pressed,
				countMessageRounding());
		}
	}
	Element::clickHandlerPressedChanged(handler, pressed);
	if (!handler) {
		return;
	} else if (_rightAction && (handler == _rightAction->link)) {
		toggleRightActionRipple(pressed);
	} else if (_comments && (handler == _comments->link)) {
		toggleCommentsButtonRipple(pressed);
	} else if (_topicButton && (handler == _topicButton->link)) {
		toggleTopicButtonRipple(pressed);
	} else if (_viewButton) {
		_viewButton->checkLink(handler, pressed);
	}
}

void Message::toggleCommentsButtonRipple(bool pressed) {
	Expects(_comments != nullptr);

	if (!drawBubble()) {
		return;
	} else if (pressed) {
		if (!_comments->ripple) {
			createCommentsButtonRipple();
		}
		_comments->ripple->add(_comments->lastPoint
			+ QPoint(_comments->rippleShift, 0));
	} else if (_comments->ripple) {
		_comments->ripple->lastStop();
	}
}

void Message::toggleRightActionRipple(bool pressed) {
	Expects(_rightAction != nullptr);

	const auto size = rightActionSize();
	Assert(size != std::nullopt);

	if (pressed) {
		if (!_rightAction->ripple) {
			// Create a ripple.
			_rightAction->ripple = std::make_unique<Ui::RippleAnimation>(
				st::defaultRippleAnimation,
				Ui::RippleAnimation::RoundRectMask(*size, size->width() / 2),
				[=] { repaint(); });
		}
		_rightAction->ripple->add(_rightAction->lastPoint);
	} else if (_rightAction->ripple) {
		_rightAction->ripple->lastStop();
	}
}

BottomRippleMask Message::bottomRippleMask(int buttonHeight) const {
	using namespace Ui;
	using namespace Images;
	using Radius = CachedCornerRadius;
	using Corner = BubbleCornerRounding;
	const auto g = countGeometry();
	const auto buttonWidth = g.width();
	const auto &large = CachedCornersMasks(Radius::BubbleLarge);
	const auto &small = CachedCornersMasks(Radius::BubbleSmall);
	const auto rounding = countBubbleRounding();
	const auto icon = (rounding.bottomLeft == Corner::Tail)
		? &st::historyBubbleTailInLeft
		: (rounding.bottomRight == Corner::Tail)
		? &st::historyBubbleTailInRight
		: nullptr;
	const auto shift = (rounding.bottomLeft == Corner::Tail)
		? icon->width()
		: 0;
	const auto added = shift ? shift : icon ? icon->width() : 0;
	auto corners = CornersMaskRef();
	const auto set = [&](int index) {
		corners.p[index] = (rounding[index] == Corner::Large)
			? &large[index]
			: (rounding[index] == Corner::Small)
			? &small[index]
			: nullptr;
	};
	set(kBottomLeft);
	set(kBottomRight);
	const auto drawer = [&](QPainter &p) {
		p.setCompositionMode(QPainter::CompositionMode_Source);
		const auto ratio = style::DevicePixelRatio();
		const auto corner = [&](int index, bool right) {
			if (const auto image = corners.p[index]) {
				const auto width = image->width() / ratio;
				const auto height = image->height() / ratio;
				p.drawImage(
					QRect(
						shift + (right ? (buttonWidth - width) : 0),
						buttonHeight - height,
						width,
						height),
					*image);
			}
		};
		corner(kBottomLeft, false);
		corner(kBottomRight, true);
		if (icon) {
			const auto left = shift ? 0 : buttonWidth;
			p.fillRect(
				QRect{ left, 0, added, buttonHeight },
				Qt::transparent);
			icon->paint(
				p,
				left,
				buttonHeight - icon->height(),
				buttonWidth + shift,
				Qt::white);
		}
	};
	return {
		RippleAnimation::MaskByDrawer(
			QSize(buttonWidth + added, buttonHeight),
			true,
			drawer),
		shift,
	};
}

void Message::createCommentsButtonRipple() {
	auto mask = bottomRippleMask(st::historyCommentsButtonHeight);
	_comments->ripple = std::make_unique<Ui::RippleAnimation>(
		st::defaultRippleAnimation,
		std::move(mask.image),
		[=] { repaint(); });
	_comments->rippleShift = mask.shift;
}

void Message::toggleTopicButtonRipple(bool pressed) {
	Expects(_topicButton != nullptr);

	if (!drawBubble()) {
		return;
	} else if (pressed) {
		if (!_topicButton->ripple) {
			createTopicButtonRipple();
		}
		_topicButton->ripple->add(_topicButton->lastPoint);
	} else if (_topicButton->ripple) {
		_topicButton->ripple->lastStop();
	}
}

void Message::createTopicButtonRipple() {
	const auto geometry = countGeometry().marginsRemoved(st::msgPadding);
	const auto availableWidth = geometry.width();
	const auto padding = st::topicButtonPadding;
	const auto height = padding.top()
		+ st::msgNameFont->height
		+ padding.bottom();
	const auto width = std::max(
		std::min(
			availableWidth,
			(padding.left()
				+ _topicButton->name.maxWidth()
				+ st::topicButtonArrowSkip
				+ padding.right())),
		height);
	auto mask = Ui::RippleAnimation::RoundRectMask(
		{ width, height },
		height / 2);
	_topicButton->ripple = std::make_unique<Ui::RippleAnimation>(
		st::defaultRippleAnimation,
		std::move(mask),
		[=] { repaint(); });
}

bool Message::hasHeavyPart() const {
	return _comments
		|| (_fromNameStatus && _fromNameStatus->custom)
		|| Element::hasHeavyPart();
}

void Message::unloadHeavyPart() {
	Element::unloadHeavyPart();
	if (_reactions) {
		_reactions->unloadCustomEmoji();
	}
	_comments = nullptr;
	if (_fromNameStatus) {
		_fromNameStatus->custom = nullptr;
		_fromNameStatus->id = 0;
	}
}

bool Message::showForwardsFromSender(
		not_null<HistoryMessageForwarded*> forwarded) const {
	const auto peer = data()->history()->peer;
	return peer->isSelf()
		|| peer->isRepliesChat()
		|| forwarded->imported;
}

bool Message::hasFromPhoto() const {
	if (isHidden()) {
		return false;
	}
	switch (context()) {
	case Context::AdminLog:
		return true;
	case Context::History:
	case Context::Pinned:
	case Context::Replies: {
		const auto item = data();
		if (item->isPost()) {
			if (item->isSponsored()) {
				if (item->history()->peer->isMegagroup()) {
					return true;
				}
				if (const auto info = item->Get<HistoryMessageSponsored>()) {
					return info->isForceUserpicDisplay;
				}
			}
			return false;
		}
		if (item->isEmpty()
			|| (context() == Context::Replies && item->isDiscussionPost())) {
			return false;
		} else if (delegate()->elementIsChatWide()) {
			return true;
		} else if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
			const auto peer = item->history()->peer;
			if (peer->isSelf() || peer->isRepliesChat()) {
				return true;
			}
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
	const auto item = data();
	const auto media = this->media();

	auto result = TextState(item);

	auto g = countGeometry();
	if (g.width() < 1 || isHidden()) {
		return result;
	}

	const auto bubble = drawBubble();
	const auto reactionsInBubble = _reactions && embedReactionsInBubble();
	const auto mediaDisplayed = media && media->isDisplayed();
	auto keyboard = item->inlineReplyKeyboard();
	auto keyboardHeight = 0;
	if (keyboard) {
		keyboardHeight = keyboard->naturalHeight();
		g.setHeight(g.height() - st::msgBotKbButton.margin - keyboardHeight);
	}

	if (_reactions && !reactionsInBubble) {
		const auto reactionsHeight = st::mediaInBubbleSkip + _reactions->height();
		const auto reactionsLeft = (!bubble && mediaDisplayed)
			? media->contentRectForReactions().x()
			: 0;
		g.setHeight(g.height() - reactionsHeight);
		const auto reactionsPosition = QPoint(reactionsLeft + g.left(), g.top() + g.height() + st::mediaInBubbleSkip);
		if (_reactions->getState(point - reactionsPosition, &result)) {
			return result;
		}
	}

	if (bubble) {
		const auto inBubble = g.contains(point);
		auto entry = logEntryOriginal();

		// Entry page is always a bubble bottom.
		auto mediaOnBottom = (mediaDisplayed && media->isBubbleBottom()) || (entry/* && entry->isBubbleBottom()*/);
		auto mediaOnTop = (mediaDisplayed && media->isBubbleTop()) || (entry && entry->isBubbleTop());

		auto inner = g;
		if (getStateCommentsButton(point, inner, &result)) {
			return result;
		}
		auto trect = inner.marginsRemoved(st::msgPadding);
		const auto reactionsTop = (reactionsInBubble && !_viewButton)
			? st::mediaInBubbleSkip
			: 0;
		const auto reactionsHeight = reactionsInBubble
			? (reactionsTop + _reactions->height())
			: 0;
		if (reactionsInBubble) {
			trect.setHeight(trect.height() - reactionsHeight);
			const auto reactionsPosition = QPoint(trect.left(), trect.top() + trect.height() + reactionsTop);
			if (_reactions->getState(point - reactionsPosition, &result)) {
				return result;
			}
		}
		if (_viewButton) {
			const auto belowInfo = _viewButton->belowMessageInfo();
			const auto infoHeight = reactionsInBubble
				? (reactionsHeight + 2 * st::mediaInBubbleSkip)
				: _bottomInfo.height();
			const auto heightMargins = QMargins(0, 0, 0, infoHeight);
			if (_viewButton->getState(
					point,
					_viewButton->countRect(belowInfo
						? inner
						: inner - heightMargins),
					&result)) {
				return result;
			}
			if (belowInfo) {
				inner.setHeight(inner.height() - _viewButton->height());
			}
			trect.setHeight(trect.height() - _viewButton->height());
			if (reactionsInBubble) {
				trect.setHeight(trect.height() - st::mediaInBubbleSkip + st::msgPadding.bottom());
			} else if (mediaDisplayed) {
				trect.setHeight(trect.height() - st::mediaInBubbleSkip);
			}
		}
		if (mediaOnBottom) {
			trect.setHeight(trect.height() + st::msgPadding.bottom());
		}
		if (mediaOnTop) {
			trect.setY(trect.y() - st::msgPadding.top());
		} else if (inBubble) {
			if (getStateFromName(point, trect, &result)) {
				return result;
			}
			if (getStateTopicButton(point, trect, &result)) {
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
			auto entryLeft = inner.left();
			auto entryTop = trect.y() + trect.height();
			if (point.y() >= entryTop && point.y() < entryTop + entryHeight) {
				result = entry->textState(
					point - QPoint(entryLeft, entryTop),
					request);
				result.symbol += visibleTextLength()
					+ visibleMediaTextLength();
			}
		}

		auto checkBottomInfoState = [&] {
			if (mediaOnBottom && (entry || media->customInfoLayout())) {
				return;
			}
			const auto bottomInfoResult = bottomInfoTextState(
				inner.left() + inner.width(),
				inner.top() + inner.height(),
				point,
				InfoDisplayType::Default);
			if (bottomInfoResult.link
				|| bottomInfoResult.cursor != CursorState::None
				|| bottomInfoResult.customTooltip) {
				result = bottomInfoResult;
			}
		};
		if (!result.symbol && inBubble) {
			if (mediaDisplayed) {
				auto mediaHeight = media->height();
				auto mediaLeft = trect.x() - st::msgPadding.left();
				auto mediaTop = (trect.y() + trect.height() - mediaHeight);

				if (point.y() >= mediaTop && point.y() < mediaTop + mediaHeight) {
					result = media->textState(point - QPoint(mediaLeft, mediaTop), request);
					result.symbol += visibleTextLength();
				} else if (getStateText(point, trect, &result, request)) {
					checkBottomInfoState();
					return result;
				} else if (point.y() >= trect.y() + trect.height()) {
					result.symbol = visibleTextLength();
				}
			} else if (getStateText(point, trect, &result, request)) {
				checkBottomInfoState();
				return result;
			} else if (point.y() >= trect.y() + trect.height()) {
				result.symbol = visibleTextLength();
			}
		}
		checkBottomInfoState();
		if (const auto size = rightActionSize(); size && _rightAction) {
			const auto fastShareSkip = std::clamp(
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
				result.link = rightActionLink(point
					- QPoint(fastShareLeft, fastShareTop));
			}
		}
	} else if (media && media->isDisplayed()) {
		result = media->textState(point - g.topLeft(), request);
		result.symbol += visibleTextLength();
	}

	if (keyboard && item->isHistoryEntry()) {
		const auto keyboardTop = g.top()
			+ g.height()
			+ st::msgBotKbButton.margin
			+ ((_reactions && !reactionsInBubble)
				? (st::mediaInBubbleSkip + _reactions->height())
				: 0);
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
		_comments->link = prepareRightActionLink();
	}
	outResult->link = _comments->link;
	_comments->lastPoint = point - QPoint(g.left(), g.top() + g.height());
	return true;
}

ClickHandlerPtr Message::createGoToCommentsLink() const {
	const auto fullId = data()->fullId();
	const auto sessionId = data()->history()->session().uniqueId();
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto controller = ExtractController(context).value_or(nullptr);
		if (!controller) {
			return;
		}
		if (controller->session().uniqueId() != sessionId) {
			return;
		}
		if (const auto item = controller->session().data().message(fullId)) {
			const auto history = item->history();
			if (const auto channel = history->peer->asChannel()) {
				if (channel->invitePeekExpires()) {
					Ui::Toast::Show(
						Window::Show(controller).toastParent(),
						tr::lng_channel_invite_private(tr::now));
					return;
				}
			}
			controller->showRepliesForMessage(history, item->id);
		}
	});
}

bool Message::getStateFromName(
		QPoint point,
		QRect &trect,
		not_null<TextState*> outResult) const {
	if (!displayFromName()) {
		return false;
	}
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
		const auto item = data();
		const auto from = item->displayFrom();
		const auto nameText = [&]() -> const Ui::Text::String * {
			if (from) {
				validateFromNameText(from);
				return &_fromName;
			} else if (const auto info = item->hiddenSenderInfo()) {
				return &info->nameText();
			} else {
				Unexpected("Corrupt forwarded information in message.");
			}
		}();
		if (point.x() >= availableLeft
			&& point.x() < availableLeft + availableWidth
			&& point.x() < availableLeft + nameText->maxWidth()) {
			outResult->link = fromLink();
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
	return false;
}

bool Message::getStateTopicButton(
		QPoint point,
		QRect &trect,
		not_null<TextState*> outResult) const {
	if (!displayedTopicButton()) {
		return false;
	}
	trect.setTop(trect.top() + st::topicButtonSkip);
	const auto padding = st::topicButtonPadding;
	const auto availableWidth = trect.width();
	const auto height = padding.top()
		+ st::msgNameFont->height
		+ padding.bottom();
	const auto width = std::max(
		std::min(
			availableWidth,
			(padding.left()
				+ _topicButton->name.maxWidth()
				+ st::topicButtonArrowSkip
				+ padding.right())),
		height);
	const auto rect = QRect(trect.x(), trect.y(), width, height);
	if (rect.contains(point)) {
		outResult->link = _topicButton->link;
		_topicButton->lastPoint = point - rect.topLeft();
		return true;
	}
	trect.setY(trect.y() + height + st::topicButtonSkip);
	return false;
}

bool Message::getStateForwardedInfo(
		QPoint point,
		QRect &trect,
		not_null<TextState*> outResult,
		StateRequest request) const {
	if (!displayForwardedFrom()) {
		return false;
	}
	const auto item = data();
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
		[=] { repaint(); },
		visible ? 0. : 1.,
		visible ? 1. : 0.,
		st::fadeWrapDuration);
}

bool Message::getStateReplyInfo(
		QPoint point,
		QRect &trect,
		not_null<TextState*> outResult) const {
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
	const auto item = data();
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
	const auto item = data();
	if (base::in_range(point.y(), trect.y(), trect.y() + trect.height())) {
		*outResult = TextState(item, text().getState(
			point - trect.topLeft(),
			trect.width(),
			request.forText()));
		return true;
	}
	return false;
}

// Forward to media.
void Message::updatePressed(QPoint point) {
	const auto item = data();
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
		auto trect = g.marginsAdded(-st::msgPadding);
		if (mediaDisplayed && media->isBubbleTop()) {
			trect.setY(trect.y() - st::msgPadding.top());
		} else {
			if (displayFromName()) {
				trect.setTop(trect.top() + st::msgNameFont->height);
			}
			if (displayedTopicButton()) {
				trect.setTop(trect.top()
					+ st::topicButtonSkip
					+ st::topicButtonPadding.top()
					+ st::msgNameFont->height
					+ st::topicButtonPadding.bottom()
					+ st::topicButtonSkip);
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
	const auto media = this->media();
	auto logEntryOriginalResult = TextForMimeData();
	auto textResult = hasVisibleText()
		? text().toTextForMimeData(selection)
		: TextForMimeData();
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
		result.append(u"\n\n"_q).append(std::move(mediaResult));
	}
	if (result.empty()) {
		result = std::move(logEntryOriginalResult);
	} else if (!logEntryOriginalResult.empty()) {
		result.append(u"\n\n"_q).append(std::move(logEntryOriginalResult));
	}
	return result;
}

TextSelection Message::adjustSelection(
		TextSelection selection,
		TextSelectType type) const {
	const auto media = this->media();

	auto result = hasVisibleText()
		? text().adjustSelection(selection, type)
		: selection;
	auto beforeMediaLength = visibleTextLength();
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
	auto beforeEntryLength = beforeMediaLength + visibleMediaTextLength();
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

Reactions::ButtonParameters Message::reactionButtonParameters(
		QPoint position,
		const TextState &reactionState) const {
	using namespace Reactions;
	auto result = ButtonParameters{ .context = data()->fullId() };
	const auto outbg = hasOutLayout();
	const auto outsideBubble = (!_comments && !embedReactionsInBubble());
	const auto geometry = countGeometry();
	result.pointer = position;
	const auto onTheLeft = (outbg && !delegate()->elementIsChatWide());

	const auto keyboard = data()->inlineReplyKeyboard();
	const auto keyboardHeight = keyboard
		? (st::msgBotKbButton.margin + keyboard->naturalHeight())
		: 0;
	const auto reactionsHeight = (_reactions && !embedReactionsInBubble())
		? (st::mediaInBubbleSkip + _reactions->height())
		: 0;
	const auto innerHeight = geometry.height()
		- keyboardHeight
		- reactionsHeight;
	const auto maybeRelativeCenter = outsideBubble
		? media()->reactionButtonCenterOverride()
		: std::nullopt;
	const auto addOnTheRight = [&] {
		return (maybeRelativeCenter
			|| !(displayFastShare() || displayGoToOriginal()))
			? st::reactionCornerCenter.x()
			: 0;
	};
	const auto relativeCenter = QPoint(
		maybeRelativeCenter.value_or(onTheLeft
			? -st::reactionCornerCenter.x()
			: (geometry.width() + addOnTheRight())),
		innerHeight + st::reactionCornerCenter.y());
	result.center = geometry.topLeft() + relativeCenter;
	if (reactionState.itemId != result.context
		&& !geometry.contains(position)) {
		result.outside = true;
	}
	const auto minSkip = (st::reactionCornerShadow.left()
		+ st::reactionCornerSize.width()
		+ st::reactionCornerShadow.right()) / 2;
	result.center = QPoint(
		std::min(std::max(result.center.x(), minSkip), width() - minSkip),
		result.center.y());
	return result;
}

int Message::reactionsOptimalWidth() const {
	return _reactions ? _reactions->countNiceWidth() : 0;
}

void Message::drawInfo(
		Painter &p,
		const PaintContext &context,
		int right,
		int bottom,
		int width,
		InfoDisplayType type) const {
	p.setFont(st::msgDateFont);

	const auto st = context.st;
	const auto sti = context.imageStyle();
	const auto stm = context.messageStyle();
	bool invertedsprites = (type == InfoDisplayType::Image)
		|| (type == InfoDisplayType::Background);
	int32 infoRight = right, infoBottom = bottom;
	switch (type) {
	case InfoDisplayType::Default:
		infoRight -= st::msgPadding.right() - st::msgDateDelta.x();
		infoBottom -= st::msgPadding.bottom() - st::msgDateDelta.y();
		p.setPen(stm->msgDateFg);
	break;
	case InfoDisplayType::Image:
		infoRight -= st::msgDateImgDelta + st::msgDateImgPadding.x();
		infoBottom -= st::msgDateImgDelta + st::msgDateImgPadding.y();
		p.setPen(st->msgDateImgFg());
	break;
	case InfoDisplayType::Background:
		infoRight -= st::msgDateImgPadding.x();
		infoBottom -= st::msgDateImgPadding.y();
		p.setPen(st->msgServiceFg());
	break;
	}

	const auto size = _bottomInfo.currentSize();
	const auto dateX = infoRight - size.width();
	const auto dateY = infoBottom - size.height();
	if (type == InfoDisplayType::Image) {
		const auto dateW = size.width() + 2 * st::msgDateImgPadding.x();
		const auto dateH = size.height() + 2 * st::msgDateImgPadding.y();
		Ui::FillRoundRect(p, dateX - st::msgDateImgPadding.x(), dateY - st::msgDateImgPadding.y(), dateW, dateH, sti->msgDateImgBg, sti->msgDateImgBgCorners);
	} else if (type == InfoDisplayType::Background) {
		const auto dateW = size.width() + 2 * st::msgDateImgPadding.x();
		const auto dateH = size.height() + 2 * st::msgDateImgPadding.y();
		Ui::FillRoundRect(p, dateX - st::msgDateImgPadding.x(), dateY - st::msgDateImgPadding.y(), dateW, dateH, sti->msgServiceBg, sti->msgServiceBgCornersSmall);
	}
	_bottomInfo.paint(
		p,
		{ dateX, dateY },
		width,
		delegate()->elementShownUnread(this),
		invertedsprites,
		context);
}

TextState Message::bottomInfoTextState(
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
	const auto size = _bottomInfo.currentSize();
	const auto infoLeft = infoRight - size.width();
	const auto infoTop = infoBottom - size.height();
	return _bottomInfo.textState(
		data(),
		point - QPoint{ infoLeft, infoTop });
}

int Message::infoWidth() const {
	return _bottomInfo.optimalSize().width();
}

int Message::bottomInfoFirstLineWidth() const {
	return _bottomInfo.firstLineWidth();
}

bool Message::bottomInfoIsWide() const {
	if (_reactions && embedReactionsInBubble()) {
		return false;
	}
	return _bottomInfo.isWide();
}

bool Message::isSignedAuthorElided() const {
	return _bottomInfo.isSignedAuthorElided();
}

bool Message::embedReactionsInBottomInfo() const {
	const auto item = data();
	const auto user = item->history()->peer->asUser();
	if (!user || user->isPremium() || user->session().premium()) {
		// Only in messages of a non premium user with a non premium user.
		return false;
	}
	auto seenMy = false;
	auto seenHis = false;
	for (const auto &reaction : item->reactions()) {
		if (reaction.id.custom()) {
			// Only in messages without any custom emoji reactions.
			return false;
		}
		// Only in messages without two reactions from the same person.
		if (reaction.my) {
			if (seenMy) {
				return false;
			}
			seenMy = true;
		}
		if (!reaction.my || (reaction.count > 1)) {
			if (seenHis) {
				return false;
			}
			seenHis = true;
		}
	}
	return true;
}

bool Message::embedReactionsInBubble() const {
	return needInfoDisplay();
}

void Message::refreshReactions() {
	const auto item = data();
	const auto &list = item->reactions();
	if (list.empty() || embedReactionsInBottomInfo()) {
		_reactions = nullptr;
		return;
	}
	using namespace Reactions;
	auto reactionsData = InlineListDataFromMessage(this);
	if (!_reactions) {
		const auto handlerFactory = [=](ReactionId id) {
			const auto weak = base::make_weak(this);
			return std::make_shared<LambdaClickHandler>([=] {
				if (const auto strong = weak.get()) {
					strong->data()->toggleReaction(
						id,
						HistoryItem::ReactionSource::Existing);
					if (const auto now = weak.get()) {
						const auto chosen = now->data()->chosenReactions();
						if (ranges::contains(chosen, id)) {
							now->animateReaction({
								.id = id,
							});
						}
					}
				}
			});
		};
		_reactions = std::make_unique<InlineList>(
			&item->history()->owner().reactions(),
			handlerFactory,
			[=] { customEmojiRepaint(); },
			std::move(reactionsData));
	} else {
		_reactions->update(std::move(reactionsData), width());
	}
}

void Message::validateInlineKeyboard(HistoryMessageReplyMarkup *markup) {
	if (!markup
		|| markup->inlineKeyboard
		|| markup->hiddenBy(data()->media())) {
		return;
	}
	markup->inlineKeyboard = std::make_unique<ReplyKeyboard>(
		data(),
		std::make_unique<KeyboardStyle>(st::msgBotKbButton));
}

void Message::validateFromNameText(PeerData *from) const {
	if (!from) {
		if (_fromNameStatus) {
			_fromNameStatus = nullptr;
		}
		return;
	}
	const auto version = from->nameVersion();
	if (_fromNameVersion < version) {
		_fromNameVersion = version;
		_fromName.setText(
			st::msgNameStyle,
			from->name(),
			Ui::NameTextOptions());
	}
	if (from->isPremium()) {
		if (!_fromNameStatus) {
			_fromNameStatus = std::make_unique<FromNameStatus>();
			const auto size = st::emojiSize;
			const auto emoji = Ui::Text::AdjustCustomEmojiSize(size);
			_fromNameStatus->skip = (size - emoji) / 2;
		}
	} else if (_fromNameStatus) {
		_fromNameStatus = nullptr;
	}
}

void Message::itemDataChanged() {
	const auto wasInfo = _bottomInfo.currentSize();
	const auto wasReactions = _reactions
		? _reactions->currentSize()
		: QSize();
	refreshReactions();
	_bottomInfo.update(BottomInfoDataFromMessage(this), width());
	const auto nowInfo = _bottomInfo.currentSize();
	const auto nowReactions = _reactions
		? _reactions->currentSize()
		: QSize();
	if (wasInfo != nowInfo || wasReactions != nowReactions) {
		history()->owner().requestViewResize(this);
	} else {
		repaint();
	}
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
	if (_rightAction && base::take(_rightAction->link)) {
		_rightAction->link = rightActionLink(_rightAction->lastPoint);
	}
	if (base::take(_fastReplyLink)) {
		_fastReplyLink = fastReplyLink();
	}
	if (_comments) {
		_comments->link = nullptr;
	}
}

int Message::plainMaxWidth() const {
	return st::msgPadding.left()
		+ (hasVisibleText() ? text().maxWidth() : 0)
		+ st::msgPadding.right();
}

int Message::monospaceMaxWidth() const {
	return st::msgPadding.left()
		+ (hasVisibleText() ? text().countMaxMonospaceWidth() : 0)
		+ st::msgPadding.right();
}

int Message::viewButtonHeight() const {
	return _viewButton ? _viewButton->height() : 0;
}

void Message::updateViewButtonExistence() {
	const auto item = data();
	const auto sponsored = item->Get<HistoryMessageSponsored>();
	const auto media = sponsored ? nullptr : item->media();
	const auto has = sponsored
		|| (media && ViewButton::MediaHasViewButton(media));
	if (!has) {
		_viewButton = nullptr;
		return;
	} else if (_viewButton) {
		return;
	}
	auto repainter = [=] { repaint(); };
	_viewButton = sponsored
		? std::make_unique<ViewButton>(sponsored, std::move(repainter))
		: std::make_unique<ViewButton>(media, std::move(repainter));
}

void Message::initLogEntryOriginal() {
	if (const auto log = data()->Get<HistoryMessageLogEntryOriginal>()) {
		AddComponents(LogEntryOriginal::Bit());
		const auto entry = Get<LogEntryOriginal>();
		entry->page = std::make_unique<WebPage>(this, log->page);
	}
}

void Message::initPsa() {
	if (const auto forwarded = data()->Get<HistoryMessageForwarded>()) {
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

bool Message::toggleSelectionByHandlerClick(
		const ClickHandlerPtr &handler) const {
	if (_comments && _comments->link == handler) {
		return true;
	} else if (_viewButton && _viewButton->link() == handler) {
		return true;
	} else if (const auto media = this->media()) {
		if (media->toggleSelectionByHandlerClick(handler)) {
			return true;
		}
	}
	return false;
}

bool Message::hasFromName() const {
	switch (context()) {
	case Context::AdminLog:
		return true;
	case Context::History:
	case Context::Pinned:
	case Context::Replies: {
		const auto item = data();
		const auto peer = item->history()->peer;
		if (hasOutLayout() && !item->from()->isChannel()) {
			return false;
		} else if (!peer->isUser()) {
			return true;
		}
		if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
			if (forwarded->imported
				&& peer.get() == forwarded->originalSender) {
				return false;
			} else if (showForwardsFromSender(forwarded)) {
				return true;
			}
		}
		return false;
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
	const auto item = data();
	if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
		if (showForwardsFromSender(forwarded)) {
			return false;
		}
		if (const auto sender = item->discussionPostOriginalSender()) {
			if (sender == forwarded->originalSender) {
				return false;
			}
		}
		const auto media = item->media();
		return !media || !media->dropForwardedInfo();
	}
	return false;
}

bool Message::hasOutLayout() const {
	const auto item = data();
	if (item->history()->peer->isSelf()) {
		return !item->Has<HistoryMessageForwarded>();
	} else if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
		if (!forwarded->imported
			|| !forwarded->originalSender
			|| !forwarded->originalSender->isSelf()) {
			if (showForwardsFromSender(forwarded)) {
				return false;
			}
		}
	}
	return item->out() && !item->isPost();
}

bool Message::drawBubble() const {
	const auto item = data();
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

TopicButton *Message::displayedTopicButton() const {
	return _topicButton.get();
}

bool Message::unwrapped() const {
	const auto item = data();
	if (isHidden()) {
		return true;
	} else if (logEntryOriginal()) {
		return false;
	}
	const auto media = this->media();
	return media
		? (!hasVisibleText() && media->unwrapped())
		: item->isEmpty();
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
	const auto canWrite = [&] {
		const auto item = data();
		const auto peer = item->history()->peer;
		const auto topic = item->topic();
		return topic ? topic->canWrite() : peer->canWrite();
	};

	return hasFastReply()
		&& data()->isRegular()
		&& canWrite()
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
	const auto item = data();
	const auto peer = item->history()->peer;
	if (!item->allowsForward()) {
		return false;
	} else if (peer->isChannel()) {
		return !peer->isMegagroup();
	} else if (const auto user = peer->asUser()) {
		if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
			return !showForwardsFromSender(forwarded)
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
	const auto item = data();
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
		const PaintContext &context,
		int left,
		int top,
		int outerWidth) const {
	ensureRightAction();

	const auto size = rightActionSize();
	const auto st = context.st;

	if (_rightAction->ripple) {
		const auto &stm = context.messageStyle();
		const auto colorOverride = &stm->msgWaveformInactive->c;
		_rightAction->ripple->paint(
			p,
			left,
			top,
			size->width(),
			colorOverride);
		if (_rightAction->ripple->empty()) {
			_rightAction->ripple.reset();
		}
	}

	p.setPen(Qt::NoPen);
	p.setBrush(st->msgServiceBg());
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
		const auto &icon = st->historyFastCommentsIcon();
		icon.paint(
			p,
			left + (size->width() - icon.width()) / 2,
			top + (st::historyFastShareSize - icon.height()) / 2,
			outerWidth);
		const auto views = data()->Get<HistoryMessageViews>();
		Assert(views != nullptr);
		if (views->repliesSmall.textWidth > 0) {
			p.setPen(st->msgServiceFg());
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
			? st->historyFastShareIcon()
			: st->historyGoToOriginalIcon();
		icon.paintInCenter(p, { left, top, size->width(), size->height() });
	}
}

ClickHandlerPtr Message::rightActionLink(
		std::optional<QPoint> pressPoint) const {
	ensureRightAction();
	if (!_rightAction->link) {
		_rightAction->link = prepareRightActionLink();
	}
	if (pressPoint) {
		_rightAction->lastPoint = *pressPoint;
	}
	return _rightAction->link;
}

void Message::ensureRightAction() const {
	if (_rightAction) {
		return;
	}
	Assert(rightActionSize().has_value());
	_rightAction = std::make_unique<RightAction>();
}

ClickHandlerPtr Message::prepareRightActionLink() const {
	if (isPinnedContext()) {
		return JumpToMessageClickHandler(data());
	} else if (displayRightActionComments()) {
		return createGoToCommentsLink();
	}
	const auto sessionId = data()->history()->session().uniqueId();
	const auto owner = &data()->history()->owner();
	const auto itemId = data()->fullId();
	const auto forwarded = data()->Get<HistoryMessageForwarded>();
	const auto savedFromPeer = forwarded
		? forwarded->savedFromPeer
		: nullptr;
	const auto savedFromMsgId = forwarded ? forwarded->savedFromMsgId : 0;

	using Callback = FnMut<void(not_null<Window::SessionController*>)>;
	const auto showByThread = std::make_shared<Callback>();
	const auto showByThreadWeak = std::weak_ptr<Callback>(showByThread);
	if (data()->externalReply()) {
		*showByThread = [=, requested = 0](
				not_null<Window::SessionController*> controller) mutable {
			const auto original = savedFromPeer->owner().message(
				savedFromPeer,
				savedFromMsgId);
			if (original && original->replyToTop()) {
				controller->showRepliesForMessage(
					original->history(),
					original->replyToTop(),
					original->id,
					Window::SectionShow::Way::Forward);
			} else if (!requested) {
				const auto prequested = &requested;
				requested = 1;
				savedFromPeer->session().api().requestMessageData(
					savedFromPeer,
					savedFromMsgId,
					[=, weak = base::make_weak(controller)] {
						if (const auto strong = showByThreadWeak.lock()) {
							if (const auto strongController = weak.get()) {
								*prequested = 2;
								(*strong)(strongController);
							}
						}
					});
			} else if (requested == 2) {
				controller->showPeerHistory(
					savedFromPeer,
					Window::SectionShow::Way::Forward,
					savedFromMsgId);
			}
		};
	};
	return std::make_shared<LambdaClickHandler>([=](
			ClickContext context) {
		const auto controller = ExtractController(context).value_or(nullptr);
		if (!controller) {
			return;
		}
		if (controller->session().uniqueId() != sessionId) {
			return;
		}

		if (const auto item = owner->message(itemId)) {
			if (*showByThread) {
				(*showByThread)(controller);
			} else if (savedFromPeer && savedFromMsgId) {
				controller->showPeerHistory(
					savedFromPeer,
					Window::SectionShow::Way::Forward,
					savedFromMsgId);
			} else {
				FastShareMessage(controller, item);
			}
		}
	});
}

ClickHandlerPtr Message::fastReplyLink() const {
	if (_fastReplyLink) {
		return _fastReplyLink;
	}
	const auto itemId = data()->fullId();
	_fastReplyLink = std::make_shared<LambdaClickHandler>([=] {
		delegate()->elementReplyTo(itemId);
	});
	return _fastReplyLink;
}

bool Message::isPinnedContext() const {
	return context() == Context::Pinned;
}

void Message::updateMediaInBubbleState() {
	const auto item = data();
	const auto media = this->media();

	if (media) {
		media->updateNeedBubbleState();
	}
	const auto reactionsInBubble = (_reactions && embedReactionsInBubble());
	auto mediaHasSomethingBelow = (_viewButton != nullptr)
		|| reactionsInBubble;
	auto mediaHasSomethingAbove = false;
	auto getMediaHasSomethingAbove = [&] {
		return displayFromName()
			|| displayedTopicButton()
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
		if (!media) {
			entry->setBubbleRounding(countBubbleRounding());
			return;
		}
	} else if (!media) {
		return;
	}

	const auto guard = gsl::finally([&] {
		media->setBubbleRounding(countBubbleRounding());
	});
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
	const auto item = data();
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
	validateFromNameText(from);
	if (const auto via = item->Get<HistoryMessageVia>()) {
		if (!displayForwardedFrom()) {
			const auto nameText = [&]() -> const Ui::Text::String * {
				if (from) {
					return &_fromName;
				} else if (const auto info	= item->hiddenSenderInfo()) {
					return &info->nameText();
				} else {
					Unexpected("Corrupted forwarded information in message.");
				}
			}();
			via->resize(width
				- st::msgPadding.left()
				- st::msgPadding.right()
				- nameText->maxWidth()
				+ (_fromNameStatus
					? (st::dialogsPremiumIcon.width()
						+ st::msgServiceFont->spacew)
					: 0)
				- st::msgServiceFont->spacew);
		}
	}
}

TextSelection Message::skipTextSelection(TextSelection selection) const {
	if (selection.from == 0xFFFF || !hasVisibleText()) {
		return selection;
	}
	return HistoryView::UnshiftItemSelection(selection, text());
}

TextSelection Message::unskipTextSelection(TextSelection selection) const {
	if (!hasVisibleText()) {
		return selection;
	}
	return HistoryView::ShiftItemSelection(selection, text());
}

QRect Message::innerGeometry() const {
	auto result = countGeometry();
	if (!hasOutLayout()) {
		const auto w = std::max(
			(media() ? media()->resolveCustomInfoRightBottom().x() : 0),
			result.width());
		result.setWidth(std::min(
			w + rightActionSize().value_or(QSize(0, 0)).width() * 2,
			width()));
	}
	if (hasBubble()) {
		result.translate(0, st::msgPadding.top() + st::mediaInBubbleSkip);

		if (displayFromName()) {
			// See paintFromName().
			result.translate(0, st::msgNameFont->height);
		}
		if (displayedTopicButton()) {
			result.translate(0, st::topicButtonSkip
				+ st::topicButtonPadding.top()
				+ st::msgNameFont->height
				+ st::topicButtonPadding.bottom()
				+ st::topicButtonSkip);
		}
		// Skip displayForwardedFrom() until there are no animations for it.
		if (displayedReply()) {
			// See paintReplyInfo().
			result.translate(
				0,
				st::msgReplyPadding.top()
					+ st::msgReplyBarSize.height()
					+ st::msgReplyPadding.bottom());
		}
		if (!displayFromName() && !displayForwardedFrom()) {
			// See paintViaBotIdInfo().
			if (data()->Has<HistoryMessageVia>()) {
				result.translate(0, st::msgServiceNameFont->height);
			}
		}
	}
	return result;
}

QRect Message::countGeometry() const {
	const auto commentsRoot = (context() == Context::Replies)
		&& data()->isDiscussionPost();
	const auto media = this->media();
	const auto mediaWidth = (media && media->isDisplayed())
		? media->width()
		: width();
	const auto outbg = hasOutLayout();
	const auto availableWidth = width()
		- st::msgMargin.left()
		- (commentsRoot ? st::msgMargin.left() : st::msgMargin.right());
	auto contentLeft = (outbg && !delegate()->elementIsChatWide())
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
	if (contentWidth < availableWidth && !delegate()->elementIsChatWide()) {
		if (outbg) {
			contentLeft += availableWidth - contentWidth;
		} else if (commentsRoot) {
			contentLeft += (availableWidth - contentWidth) / 2;
		}
	} else if (contentWidth < availableWidth && commentsRoot) {
		contentLeft += std::max(
			((st::msgMaxWidth + 2 * st::msgPhotoSkip) - contentWidth) / 2,
			0);
	}

	const auto contentTop = marginTop();
	return QRect(
		contentLeft,
		contentTop,
		contentWidth,
		height() - contentTop - marginBottom());
}

Ui::BubbleRounding Message::countMessageRounding() const {
	const auto smallTop = isBubbleAttachedToPrevious();
	const auto smallBottom = isBubbleAttachedToNext();
	const auto media = smallBottom ? nullptr : this->media();
	const auto keyboard = data()->inlineReplyKeyboard();
	const auto skipTail = smallBottom
		|| (media && media->skipBubbleTail())
		|| (keyboard != nullptr)
		|| (context() == Context::Replies && data()->isDiscussionPost());
	const auto right = !delegate()->elementIsChatWide() && hasOutLayout();
	using Corner = Ui::BubbleCornerRounding;
	return Ui::BubbleRounding{
		.topLeft = (smallTop && !right) ? Corner::Small : Corner::Large,
		.topRight = (smallTop && right) ? Corner::Small : Corner::Large,
		.bottomLeft = ((smallBottom && !right)
			? Corner::Small
			: (!skipTail && !right)
			? Corner::Tail
			: Corner::Large),
		.bottomRight = ((smallBottom && right)
			? Corner::Small
			: (!skipTail && right)
			? Corner::Tail
			: Corner::Large),
	};
}

Ui::BubbleRounding Message::countBubbleRounding(
		Ui::BubbleRounding messageRounding) const {
	if (const auto keyboard = data()->inlineReplyKeyboard()) {
		messageRounding.bottomLeft
			= messageRounding.bottomRight
			= Ui::BubbleCornerRounding::Small;
	}
	return messageRounding;
}

Ui::BubbleRounding Message::countBubbleRounding() const {
	return countBubbleRounding(countMessageRounding());
}

int Message::resizeContentGetHeight(int newWidth) {
	if (isHidden()) {
		return marginTop() + marginBottom();
	} else if (newWidth < st::msgMinWidth) {
		return height();
	}

	auto newHeight = minHeight();

	const auto item = data();
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
	const auto textWidth = qMax(contentWidth - st::msgPadding.left() - st::msgPadding.right(), 1);
	const auto reactionsInBubble = _reactions && embedReactionsInBubble();
	const auto bottomInfoHeight = _bottomInfo.resizeGetHeight(
		std::min(
			_bottomInfo.optimalSize().width(),
			textWidth - 2 * st::msgDateDelta.x()));

	if (bubble) {
		auto reply = displayedReply();
		auto via = item->Get<HistoryMessageVia>();
		auto entry = logEntryOriginal();

		// Entry page is always a bubble bottom.
		auto mediaOnBottom = (mediaDisplayed && media->isBubbleBottom()) || (entry/* && entry->isBubbleBottom()*/);
		auto mediaOnTop = (mediaDisplayed && media->isBubbleTop()) || (entry && entry->isBubbleTop());

		if (reactionsInBubble) {
			_reactions->resizeGetHeight(textWidth);
		}

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
			newHeight = hasVisibleText() ? textHeightFor(textWidth) : 0;
			if (!mediaOnBottom && (!_viewButton || !reactionsInBubble)) {
				newHeight += st::msgPadding.bottom();
				if (mediaDisplayed) {
					newHeight += st::mediaInBubbleSkip;
				}
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
			if (reactionsInBubble) {
				if (!mediaDisplayed || _viewButton) {
					newHeight += st::mediaInBubbleSkip;
				}
				newHeight += _reactions->height();
			}
		}

		if (displayFromName()) {
			fromNameUpdated(contentWidth);
			newHeight += st::msgNameFont->height;
		} else if (via && !displayForwardedFrom()) {
			via->resize(contentWidth - st::msgPadding.left() - st::msgPadding.right());
			newHeight += st::msgNameFont->height;
		}

		if (displayedTopicButton()) {
			newHeight += st::topicButtonSkip
				+ st::topicButtonPadding.top()
				+ st::msgNameFont->height
				+ st::topicButtonPadding.bottom()
				+ st::topicButtonSkip;
		}

		if (displayForwardedFrom()) {
			const auto forwarded = item->Get<HistoryMessageForwarded>();
			const auto skip1 = forwarded->psaType.isEmpty()
				? 0
				: st::historyPsaIconSkip1;
			const auto fwdheight = ((forwarded->text.maxWidth() > (contentWidth - st::msgPadding.left() - st::msgPadding.right() - skip1)) ? 2 : 1) * st::semiboldFont->height;
			newHeight += fwdheight;
		}

		if (reply) {
			reply->resize(contentWidth - st::msgPadding.left() - st::msgPadding.right());
			newHeight += st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
		}
		if (needInfoDisplay()) {
			newHeight += (bottomInfoHeight - st::msgDateFont->height);
		}

		if (item->repliesAreComments() || item->externalReply()) {
			newHeight += st::historyCommentsButtonHeight;
		} else if (_comments) {
			_comments = nullptr;
			checkHeavyPart();
		}
		newHeight += viewButtonHeight();
	} else if (mediaDisplayed) {
		newHeight = media->height();
	} else {
		newHeight = 0;
	}
	if (_reactions && !reactionsInBubble) {
		const auto reactionsWidth = (!bubble && mediaDisplayed)
			? media->contentRectForReactions().width()
			: contentWidth;
		newHeight += st::mediaInBubbleSkip
			+ _reactions->resizeGetHeight(reactionsWidth);
		if (hasOutLayout() && !delegate()->elementIsChatWide()) {
			_reactions->flipToRight();
		}
	}

	if (const auto keyboard = item->inlineReplyKeyboard()) {
		const auto keyboardHeight = st::msgBotKbButton.margin + keyboard->naturalHeight();
		newHeight += keyboardHeight;
		keyboard->resize(contentWidth, keyboardHeight - st::msgBotKbButton.margin);
	}

	newHeight += marginTop() + marginBottom();
	return newHeight;
}

bool Message::needInfoDisplay() const {
	const auto media = this->media();
	const auto mediaDisplayed = media ? media->isDisplayed() : false;
	const auto entry = logEntryOriginal();
	return entry
		? !entry->customInfoLayout()
		: (mediaDisplayed
			? !media->customInfoLayout()
			: true);
}

bool Message::hasVisibleText() const {
	if (data()->emptyText()) {
		return false;
	}
	const auto media = this->media();
	return !media || !media->hideMessageText();
}

int Message::visibleTextLength() const {
	return hasVisibleText() ? text().length() : 0;
}

int Message::visibleMediaTextLength() const {
	const auto media = this->media();
	return (media && media->isDisplayed())
		? media->fullSelectionLength()
		: 0;
}

QSize Message::performCountCurrentSize(int newWidth) {
	const auto newHeight = resizeContentGetHeight(newWidth);

	return { newWidth, newHeight };
}

void Message::refreshInfoSkipBlock() {
	const auto item = data();
	const auto media = this->media();
	const auto hasTextSkipBlock = [&] {
		if (item->_text.empty()) {
			return false;
		} else if (item->Has<HistoryMessageLogEntryOriginal>()) {
			return false;
		} else if (media && media->isDisplayed()) {
			return false;
		} else if (_reactions) {
			return false;
		}
		return true;
	}();
	const auto skipWidth = skipBlockWidth();
	const auto skipHeight = skipBlockHeight();
	if (_reactions) {
		if (needInfoDisplay()) {
			_reactions->updateSkipBlock(skipWidth, skipHeight);
		} else {
			_reactions->removeSkipBlock();
		}
	}
	validateTextSkipBlock(hasTextSkipBlock, skipWidth, skipHeight);
}

TimeId Message::displayedEditDate() const {
	const auto item = data();
	const auto overrided = media() && media()->overrideEditedDate();
	if (item->hideEditedBadge() && !overrided) {
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
	return data()->Get<HistoryMessageEdited>();
}

const HistoryMessageEdited *Message::displayedEditBadge() const {
	if (const auto media = this->media()) {
		if (media->overrideEditedDate()) {
			return media->displayedEditBadge();
		}
	}
	return data()->Get<HistoryMessageEdited>();
}

} // namespace HistoryView
