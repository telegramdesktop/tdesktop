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
#include "history/history_item_components.h"
#include "history/history_item_helpers.h"
#include "history/view/media/history_view_web_page.h"
#include "history/view/reactions/history_view_reactions.h"
#include "history/view/reactions/history_view_reactions_button.h"
#include "history/view/history_view_group_call_bar.h" // UserpicInRow.
#include "history/view/history_view_reply.h"
#include "history/view/history_view_view_button.h" // ViewButton.
#include "history/history.h"
#include "boxes/premium_preview_box.h"
#include "boxes/share_box.h"
#include "ui/effects/glare.h"
#include "ui/effects/reaction_fly_animation.h"
#include "ui/rect.h"
#include "ui/round_rect.h"
#include "ui/text/text_utilities.h"
#include "ui/text/text_extended_data.h"
#include "ui/power_saving.h"
#include "data/components/factchecks.h"
#include "data/components/sponsored_messages.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_channel.h"
#include "data/data_forum_topic.h"
#include "data/data_message_reactions.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "main/main_session.h"
#include "settings/settings_premium.h"
#include "ui/text/text_options.h"
#include "ui/painter.h"
#include "window/themes/window_theme.h" // IsNightMode.
#include "window/window_session_controller.h"
#include "apiwrap.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_dialogs.h"

namespace HistoryView {
namespace {

constexpr auto kPlayStatusLimit = 2;
const auto kPsaTooltipPrefix = "cloud_lng_tooltip_psa_";

class KeyboardStyle : public ReplyKeyboard::Style {
public:
	KeyboardStyle(const style::BotKeyboardButton &st, Fn<void()> repaint);

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
		const QRect &rect,
		int outerWidth,
		Ui::BubbleRounding rounding) const override;
	int minButtonWidth(HistoryMessageMarkupButton::Type type) const override;

private:
	using BubbleRoundingKey = uchar;
	mutable base::flat_map<BubbleRoundingKey, QImage> _cachedBg;
	mutable base::flat_map<BubbleRoundingKey, QPainterPath> _cachedOutline;
	mutable std::unique_ptr<Ui::GlareEffect> _glare;
	Fn<void()> _repaint;
	rpl::lifetime _lifetime;

};

KeyboardStyle::KeyboardStyle(
	const style::BotKeyboardButton &st,
	Fn<void()> repaint)
: ReplyKeyboard::Style(st)
, _repaint(std::move(repaint)) {
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		_cachedBg = {};
		_cachedOutline = {};
	}, _lifetime);
}

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

	using Corner = Ui::BubbleCornerRounding;
	auto &cachedBg = _cachedBg[rounding.key()];

	if (cachedBg.isNull()
		|| cachedBg.width() != (rect.width() * style::DevicePixelRatio())) {
		cachedBg = QImage(
			rect.size() * style::DevicePixelRatio(),
			QImage::Format_ARGB32_Premultiplied);
		cachedBg.setDevicePixelRatio(style::DevicePixelRatio());
		cachedBg.fill(Qt::transparent);
		{
			auto painter = QPainter(&cachedBg);

			const auto sti = &st->imageStyle(false);
			const auto &small = sti->msgServiceBgCornersSmall;
			const auto &large = sti->msgServiceBgCornersLarge;
			auto corners = Ui::CornersPixmaps();
			int radiuses[4];
			for (auto i = 0; i != 4; ++i) {
				const auto isLarge = (rounding[i] == Corner::Large);
				corners.p[i] = (isLarge ? large : small).p[i];
				radiuses[i] = Ui::CachedCornerRadiusValue(isLarge
					? Ui::CachedCornerRadius::BubbleLarge
					: Ui::CachedCornerRadius::BubbleSmall);
			}
			const auto r = Rect(rect.size());
			_cachedOutline[rounding.key()] = Ui::ComplexRoundedRectPath(
				r - Margins(st::lineWidth),
				radiuses[0],
				radiuses[1],
				radiuses[2],
				radiuses[3]);
			Ui::FillRoundRect(painter, r, sti->msgServiceBg, corners);
		}
	}
	p.drawImage(rect.topLeft(), cachedBg);
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
		case Type::CopyText: return &st->msgBotKbCopyIcon();
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
		const QRect &rect,
		int outerWidth,
		Ui::BubbleRounding rounding) const {
	Expects(st != nullptr);

	if (anim::Disabled()) {
		const auto &icon = st->historySendingInvertedIcon();
		icon.paint(
			p,
			rect::right(rect) - icon.width() - st::msgBotKbIconPadding,
			rect::bottom(rect) - icon.height() - st::msgBotKbIconPadding,
			rect.x() * 2 + rect.width());
		return;
	}

	const auto cacheKey = rounding.key();
	auto &cachedBg = _cachedBg[cacheKey];
	if (!cachedBg.isNull()) {
		if (_glare && _glare->glare.birthTime) {
			const auto progress = _glare->progress(crl::now());
			const auto w = _glare->width;
			const auto h = rect.height();
			const auto x = (-w) + (w * 2) * progress;

			auto frame = cachedBg;
			frame.fill(Qt::transparent);
			{
				auto painter = QPainter(&frame);
				auto hq = PainterHighQualityEnabler(painter);
				painter.setPen(Qt::NoPen);
				painter.drawTiledPixmap(x, 0, w, h, _glare->pixmap, 0, 0);

				auto path = QPainterPath();
				path.addRect(Rect(rect.size()));
				path -= _cachedOutline[cacheKey];

				constexpr auto kBgOutlineAlpha = 0.5;
				constexpr auto kFgOutlineAlpha = 0.8;
				const auto &c = st::premiumButtonFg->c;
				painter.setPen(Qt::NoPen);
				painter.setBrush(c);
				painter.setOpacity(kBgOutlineAlpha);
				painter.drawPath(path);
				auto gradient = QLinearGradient(-w, 0, w * 2, 0);
				{
					constexpr auto kShiftLeft = 0.01;
					constexpr auto kShiftRight = 0.99;
					auto stops = _glare->computeGradient(c).stops();
					stops[1] = {
						std::clamp(progress, kShiftLeft, kShiftRight),
						QColor(c.red(), c.green(), c.blue(), kFgOutlineAlpha),
					};
					gradient.setStops(std::move(stops));
				}
				painter.setBrush(QBrush(gradient));
				painter.setOpacity(1);
				painter.drawPath(path);

				painter.setCompositionMode(
					QPainter::CompositionMode_DestinationIn);
				painter.drawImage(0, 0, cachedBg);
			}
			p.drawImage(rect.x(), rect.y(), frame);
		} else {
			_glare = std::make_unique<Ui::GlareEffect>();
			_glare->width = outerWidth;

			constexpr auto kTimeout = crl::time(0);
			constexpr auto kDuration = crl::time(1100);
			const auto color = st::premiumButtonFg->c;
			_glare->validate(color, _repaint, kTimeout, kDuration);
		}
	}
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
	case Type::CopyText: return st::msgBotKbCopyIcon.width(); break;
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

struct SecondRightAction final {
	std::unique_ptr<Ui::RippleAnimation> ripple;
	ClickHandlerPtr link;
};

} // namespace

struct Message::CommentsButton {
	std::unique_ptr<Ui::RippleAnimation> ripple;
	std::vector<UserpicInRow> userpics;
	QImage cachedUserpics;
	ClickHandlerPtr link;
	QPoint lastPoint;
	int rippleShift = 0;
};

struct Message::FromNameStatus {
	EmojiStatusId id;
	std::unique_ptr<Ui::Text::CustomEmoji> custom;
	ClickHandlerPtr link;
	int skip = 0;
};

struct Message::RightAction {
	std::unique_ptr<Ui::RippleAnimation> ripple;
	ClickHandlerPtr link;
	QPoint lastPoint;
	std::unique_ptr<SecondRightAction> second;
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
, _hideReply(delegate->elementHideReply(this))
, _postShowingAuthor(data->isPostShowingAuthor() ? 1 : 0)
, _bottomInfo(
		&data->history()->owner().reactions(),
		BottomInfoDataFromMessage(this)) {
	if (const auto media = data->media()) {
		if (media->giveawayResults()) {
			_hideReply = 1;
		}
	}
	initLogEntryOriginal();
	initPsa();
	setupReactions(replacing);
	auto animation = replacing ? replacing->takeEffectAnimation() : nullptr;
	if (animation) {
		_bottomInfo.continueEffectAnimation(std::move(animation));
	}
	if (data->isSponsored()) {
		const auto &session = data->history()->session();
		const auto details = session.sponsoredMessages().lookupDetails(
			data->fullId());
		if (details.canReport) {
			_rightAction = std::make_unique<RightAction>();
			_rightAction->second = std::make_unique<SecondRightAction>();

			_rightAction->second->link = ReportSponsoredClickHandler(data);
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
	const auto item = data();
	const auto text = [&] {
		if (item->isDiscussionPost()) {
			return (delegate()->elementContext() == Context::Replies)
				? QString()
				: tr::lng_channel_badge(tr::now);
		} else if (item->author()->isMegagroup()) {
			if (const auto msgsigned = item->Get<HistoryMessageSigned>()) {
				if (!msgsigned->viaBusinessBot) {
					Assert(msgsigned->isAnonymousRank);
					return msgsigned->author;
				}
			}
		}
		const auto channel = item->history()->peer->asMegagroup();
		const auto user = item->author()->asUser();
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
	auto badge = TextWithEntities{
		(text.isEmpty()
			? delegate()->elementAuthorRank(this)
			: TextUtilities::RemoveEmoji(TextUtilities::SingleLine(text)))
	};
	_rightBadgeHasBoosts = 0;
	if (const auto boosts = item->boostsApplied()) {
		_rightBadgeHasBoosts = 1;

		const auto many = (boosts > 1);
		const auto &icon = many
			? st::boostsMessageIcon
			: st::boostMessageIcon;
		const auto padding = many
			? st::boostsMessageIconPadding
			: st::boostMessageIconPadding;
		const auto owner = &item->history()->owner();
		auto added = Ui::Text::SingleCustomEmoji(
			owner->customEmojiManager().registerInternalEmoji(icon, padding)
		).append(many ? QString::number(boosts) : QString());
		badge.append(' ').append(Ui::Text::Colorized(added, 1));
	}
	if (badge.empty()) {
		_rightBadge.clear();
	} else {
		const auto context = Core::MarkedTextContext{
			.session = &item->history()->session(),
			.customEmojiRepaint = [] {},
			.customEmojiLoopLimit = 1,
		};
		_rightBadge.setMarkedText(
			st::defaultTextStyle,
			badge,
			Ui::NameTextOptions(),
			context);
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

	if (bubble) {
		// Entry page is always a bubble bottom.
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
	}
}

void Message::animateEffect(Ui::ReactionFlyAnimationArgs &&args) {
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

	const auto animateInBottomInfo = [&](QPoint bottomRight) {
		_bottomInfo.animateEffect(args.translated(-bottomRight), repainter);
	};
	if (bubble) {
		const auto entry = logEntryOriginal();
		const auto check = factcheckBlock();

		// Entry page is always a bubble bottom.
		auto mediaOnBottom = (mediaDisplayed && media->isBubbleBottom()) || check || (entry/* && entry->isBubbleBottom()*/);
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

auto Message::takeEffectAnimation()
-> std::unique_ptr<Ui::ReactionFlyAnimation> {
	return _bottomInfo.takeEffectAnimation();
}

QRect Message::effectIconGeometry() const {
	const auto item = data();
	const auto media = this->media();

	auto g = countGeometry();
	if (g.width() < 1 || isHidden()) {
		return {};
	}
	const auto bubble = drawBubble();
	const auto reactionsInBubble = _reactions && embedReactionsInBubble();
	const auto mediaDisplayed = media && media->isDisplayed();
	const auto keyboard = item->inlineReplyKeyboard();
	auto keyboardHeight = 0;
	if (keyboard) {
		keyboardHeight = keyboard->naturalHeight();
		g.setHeight(g.height() - st::msgBotKbButton.margin - keyboardHeight);
	}

	const auto fromBottomInfo = [&](QPoint bottomRight) {
		const auto size = _bottomInfo.currentSize();
		return _bottomInfo.effectIconGeometry().translated(
			bottomRight - QPoint(size.width(), size.height()));
	};
	if (bubble) {
		const auto entry = logEntryOriginal();
		const auto check = factcheckBlock();

		// Entry page is always a bubble bottom.
		auto mediaOnBottom = (mediaDisplayed && media->isBubbleBottom()) || check || (entry/* && entry->isBubbleBottom()*/);
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
			return fromBottomInfo(QPoint(mediaLeft, mediaTop) + media->resolveCustomInfoRightBottom());
		} else {
			return fromBottomInfo({
				inner.left() + inner.width() - (st::msgPadding.right() - st::msgDateDelta.x()),
				inner.top() + inner.height() - (st::msgPadding.bottom() - st::msgDateDelta.y()),
			});
		}
	} else if (mediaDisplayed) {
		return fromBottomInfo(g.topLeft() + media->resolveCustomInfoRightBottom());
	}
	return {};
}

QSize Message::performCountOptimalSize() {
	const auto item = data();

	const auto replyData = item->Get<HistoryMessageReply>();
	if (replyData && !_hideReply) {
		AddComponents(Reply::Bit());
	} else {
		RemoveComponents(Reply::Bit());
	}

	const auto factcheck = item->Get<HistoryMessageFactcheck>();
	if (factcheck && !factcheck->data.text.empty()) {
		AddComponents(Factcheck::Bit());
		Get<Factcheck>()->page = history()->session().factchecks().makeMedia(
			this,
			factcheck);
	} else {
		RemoveComponents(Factcheck::Bit());
	}

	const auto markup = item->inlineReplyMarkup();
	const auto reactionsKey = [&] {
		return embedReactionsInBubble() ? 0 : 1;
	};
	const auto oldKey = reactionsKey();
	validateText();
	validateInlineKeyboard(markup);
	updateViewButtonExistence();
	refreshTopicButton();

	const auto media = this->media();
	const auto textItem = this->textItem();
	const auto defaultInvert = media && media->aboveTextByDefault();
	const auto invertDefault = textItem
		&& textItem->invertMedia()
		&& !textItem->emptyText();
	_invertMedia = invertDefault ? !defaultInvert : defaultInvert;

	updateMediaInBubbleState();
	if (oldKey != reactionsKey()) {
		refreshReactions();
	}
	refreshRightBadge();
	refreshInfoSkipBlock(textItem);

	const auto botTop = item->isFakeAboutView()
		? Get<FakeBotAboutTop>()
		: nullptr;
	if (botTop) {
		botTop->init();
	}

	auto maxWidth = 0;
	auto minHeight = 0;

	const auto reactionsInBubble = _reactions && embedReactionsInBubble();
	if (_reactions) {
		_reactions->initDimensions();
	}

	const auto reply = Get<Reply>();
	if (reply) {
		reply->update(this, replyData);
	}

	if (drawBubble()) {
		const auto forwarded = item->Get<HistoryMessageForwarded>();
		const auto via = item->Get<HistoryMessageVia>();
		const auto entry = logEntryOriginal();
		const auto check = factcheckBlock();
		if (forwarded) {
			forwarded->create(via, item);
		}

		auto mediaDisplayed = false;
		if (media) {
			mediaDisplayed = media->isDisplayed();
			media->initDimensions();
		}
		if (check) {
			check->initDimensions();
		}
		if (entry) {
			entry->initDimensions();
		}

		// Entry page is always a bubble bottom.
		const auto withVisibleText = hasVisibleText();
		const auto textualWidth = textualMaxWidth();
		auto mediaOnBottom = (mediaDisplayed && media->isBubbleBottom()) || check || (entry/* && entry->isBubbleBottom()*/);
		auto mediaOnTop = (mediaDisplayed && media->isBubbleTop()) || (entry && entry->isBubbleTop());
		maxWidth = textualWidth;
		if (context() == Context::Replies && item->isDiscussionPost()) {
			maxWidth = std::max(maxWidth, st::msgMaxWidth);
		}
		minHeight = withVisibleText ? text().minHeight() : 0;
		if (reactionsInBubble) {
			const auto reactionsMaxWidth = st::msgPadding.left()
				+ _reactions->maxWidth()
				+ st::msgPadding.right();
			accumulate_max(
				maxWidth,
				std::min(st::msgMaxWidth, reactionsMaxWidth));
			if (mediaDisplayed
				&& !media->additionalInfoString().isEmpty()) {
				// In round videos in a web page status text is painted
				// in the bottom left corner, reactions should be below.
				minHeight += st::msgDateFont->height;
			} else {
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
		if (check) minHeight += st::mediaInBubbleSkip;
		if (mediaDisplayed) {
			// Parts don't participate in maxWidth() in case of media message.
			if (media->enforceBubbleWidth()) {
				maxWidth = media->maxWidth();
				const auto innerWidth = maxWidth
					- st::msgPadding.left()
					- st::msgPadding.right();
				if (withVisibleText) {
					if (maxWidth < textualWidth) {
						minHeight -= text().minHeight();
						minHeight += text().countHeight(innerWidth);
					}
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
					: item->displayHiddenSenderInfo()->nameText();
				auto namew = st::msgPadding.left()
					+ name.maxWidth()
					+ (_fromNameStatus
						? st::dialogsPremiumIcon.icon.width()
						: 0)
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
				const auto replyw = st::msgPadding.left()
					+ reply->maxWidth()
					+ st::msgPadding.right();
				accumulate_max(maxWidth, replyw);
			}
			if (check) {
				accumulate_max(maxWidth, check->maxWidth());
				minHeight += check->minHeight();
			}
			if (entry) {
				accumulate_max(maxWidth, entry->maxWidth());
				minHeight += entry->minHeight();
			}
		}
		if (withVisibleText && botTop) {
			accumulate_max(maxWidth, botTop->maxWidth);
			minHeight += botTop->height;
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
	if (isAttachedToPrevious()
		|| delegate()->elementHideTopicButton(this)) {
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
	if (const auto service = Get<ServicePreMessage>()) {
		result += service->height;
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

	const auto hasGesture = context.gestureHorizontal.translation
		&& (context.gestureHorizontal.msgBareId == item->fullId().msg.bare);
	if (hasGesture) {
		p.translate(context.gestureHorizontal.translation, 0);
	}
	const auto selectionModeResult = delegate()->elementInSelectionMode(this);
	const auto selectionTranslation = (selectionModeResult.progress > 0)
		? (selectionModeResult.progress
			* AdditionalSpaceForSelectionCheckbox(this, g))
		: 0;
	if (selectionTranslation) {
		p.translate(selectionTranslation, 0);
	}

	if (item->hasUnrequestedFactcheck()) {
		item->history()->session().factchecks().requestFor(item);
	}

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

	if (const auto service = Get<ServicePreMessage>()) {
		service->paint(p, context, g, delegate()->elementIsChatWide());
	}

	if (isHidden()) {
		return;
	}

	const auto entry = logEntryOriginal();
	const auto check = factcheckBlock();
	auto mediaDisplayed = media && media->isDisplayed();

	// Entry page is always a bubble bottom.
	auto mediaOnBottom = (mediaDisplayed && media->isBubbleBottom()) || check || (entry/* && entry->isBubbleBottom()*/);
	auto mediaOnTop = (mediaDisplayed && media->isBubbleTop()) || (entry && entry->isBubbleTop());

	const auto displayInfo = needInfoDisplay();
	const auto reactionsInBubble = _reactions && embedReactionsInBubble();

	const auto keyboard = item->inlineReplyKeyboard();
	const auto fullGeometry = g;
	if (keyboard) {
		// We need to count geometry without keyboard for bubble selection
		// intervals counting below.
		const auto keyboardHeight = st::msgBotKbButton.margin + keyboard->naturalHeight();
		g.setHeight(g.height() - keyboardHeight);
	}

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
		if (check) {
			localMediaBottom -= check->height();
		}
		if (entry) {
			localMediaBottom -= entry->height();
		}
		localMediaTop = localMediaBottom - media->height();
		for (auto &[top, height] : mediaSelectionIntervals) {
			top += localMediaTop;
		}
	}

	{
		if (selectionTranslation) {
			p.translate(-selectionTranslation, 0);
		}
		if (customHighlight) {
			media->drawHighlight(p, context, localMediaTop);
		} else {
			paintHighlight(p, context, fullGeometry.height());
		}
		if (selectionTranslation) {
			p.translate(selectionTranslation, 0);
		}
	}

	const auto roll = media ? media->bubbleRoll() : Media::BubbleRoll();
	if (roll) {
		p.save();
		p.translate(fullGeometry.center());
		p.rotate(roll.rotate);
		p.scale(roll.scale, roll.scale);
		p.translate(-fullGeometry.center());
	}

	p.setTextPalette(stm->textPalette);

	const auto messageRounding = countMessageRounding();
	if (keyboard) {
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

	if (context.highlightPathCache) {
		context.highlightInterpolateTo = g;
		context.highlightPathCache->clear();
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

		const auto additionalInfoSkip = (mediaDisplayed
			&& !media->additionalInfoString().isEmpty())
			? st::msgDateFont->height
			: 0;
		const auto reactionsTop = (reactionsInBubble && !_viewButton)
			? (additionalInfoSkip + st::mediaInBubbleSkip)
			: additionalInfoSkip;
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
			paintViaBotIdInfo(p, trect, context);
			paintReplyInfo(p, trect, context);
		}
		if (entry) {
			trect.setHeight(trect.height() - entry->height());
		}
		if (check) {
			trect.setHeight(trect.height() - check->height() - st::mediaInBubbleSkip);
		}
		if (displayInfo) {
			trect.setHeight(trect.height()
				- (_bottomInfo.height() - st::msgDateFont->height));
		}
		auto textSelection = context.selection;
		auto highlightRange = context.highlight.range;
		const auto mediaHeight = mediaDisplayed ? media->height() : 0;
		const auto paintMedia = [&](int top) {
			if (!mediaDisplayed) {
				return;
			}
			const auto mediaSelection = _invertMedia
				? context.selection
				: skipTextSelection(context.selection);
			const auto maybeMediaHighlight = context.highlightPathCache
				&& context.highlightPathCache->isEmpty();
			auto mediaPosition = QPoint(inner.left(), top);
			p.translate(mediaPosition);
			media->draw(p, context.translated(
				-mediaPosition
			).withSelection(mediaSelection));
			if (context.reactionInfo && !displayInfo && !_reactions) {
				const auto add = QPoint(0, mediaHeight);
				context.reactionInfo->position = mediaPosition + add;
				if (context.reactionInfo->effectPaint) {
					context.reactionInfo->effectOffset -= add;
				}
			}
			if (maybeMediaHighlight
				&& !context.highlightPathCache->isEmpty()) {
				context.highlightPathCache->translate(mediaPosition);
			}
			p.translate(-mediaPosition);
		};
		if (mediaDisplayed && _invertMedia) {
			if (!mediaOnTop) {
				trect.setY(trect.y() + st::mediaInBubbleSkip);
			}
			paintMedia(trect.y());
			trect.setY(trect.y()
				+ mediaHeight
				+ (mediaOnBottom ? 0 : st::mediaInBubbleSkip));
			textSelection = media->skipSelection(textSelection);
			highlightRange = media->skipSelection(highlightRange);
		}
		auto copy = context;
		copy.selection = textSelection;
		copy.highlight.range = highlightRange;
		paintText(p, trect, copy);
		if (mediaDisplayed && !_invertMedia) {
			paintMedia(trect.y() + trect.height() - mediaHeight);
			if (context.reactionInfo && !displayInfo && !_reactions) {
				context.reactionInfo->position
					= QPoint(inner.left(), trect.y() + trect.height());
				if (context.reactionInfo->effectPaint) {
					context.reactionInfo->effectOffset -= QPoint(0, mediaHeight);
				}
			}
		}
		if (check) {
			auto checkLeft = inner.left();
			auto checkTop = trect.y() + trect.height() + st::mediaInBubbleSkip;
			p.translate(checkLeft, checkTop);
			auto checkContext = context.translated(checkLeft, -checkTop);
			checkContext.selection = skipTextSelection(context.selection);
			if (mediaDisplayed) {
				checkContext.selection = media->skipSelection(
					checkContext.selection);
			}
			check->draw(p, checkContext);
			p.translate(-checkLeft, -checkTop);
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
			const auto fastShareLeft = hasRightLayout()
				? (g.left() - size->width() - st::historyFastShareLeft)
				: (g.left() + g.width() + st::historyFastShareLeft);
			const auto fastShareTop = data()->isSponsored()
				? g.top() + fastShareSkip
				: g.top() + g.height() - fastShareSkip - size->height();
			const auto o = p.opacity();
			if (selectionModeResult.progress > 0) {
				p.setOpacity(1. - selectionModeResult.progress);
			}
			drawRightAction(p, context, fastShareLeft, fastShareTop, width());
			if (selectionModeResult.progress > 0) {
				p.setOpacity(o);
			}
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

	if (context.highlightPathCache
		&& !context.highlightPathCache->isEmpty()) {
		const auto alpha = int(0.25
			* context.highlight.collapsion
			* context.highlight.opacity
			* 255);
		if (alpha > 0) {
			context.highlightPathCache->setFillRule(Qt::WindingFill);
			auto color = context.messageStyle()->textPalette.linkFg->c;
			color.setAlpha(alpha);
			p.fillPath(*context.highlightPathCache, color);
		}
	}

	if (roll) {
		p.restore();
	}

	if (const auto reply = Get<Reply>()) {
		if (const auto replyData = item->Get<HistoryMessageReply>()) {
			if (reply->isNameUpdated(this, replyData)) {
				const_cast<Message*>(this)->setPendingResize();
			}
		}
	}
	if (hasGesture) {
		p.translate(-context.gestureHorizontal.translation, 0);

		constexpr auto kShiftRatio = 1.5;
		constexpr auto kBouncePart = 0.25;
		constexpr auto kMaxHeightRatio = 3.5;
		constexpr auto kStrokeWidth = 2.;
		constexpr auto kWaveWidth = 10.;
		const auto isLeftSize = (!context.outbg)
			|| delegate()->elementIsChatWide();
		const auto ratio = std::min(context.gestureHorizontal.ratio, 1.);
		const auto reachRatio = context.gestureHorizontal.reachRatio;
		const auto size = st::historyFastShareSize;
		const auto outerWidth = st::historySwipeIconSkip
			+ (isLeftSize ? rect::right(g) : width())
			+ ((g.height() < size * kMaxHeightRatio)
				? rightActionSize().value_or(QSize()).width()
				: 0);
		const auto shift = std::min(
			(size * kShiftRatio * context.gestureHorizontal.ratio),
			-1. * context.gestureHorizontal.translation
		) + (st::historySwipeIconSkip * ratio * (isLeftSize ? .7 : 1.));
		const auto rect = QRectF(
			outerWidth - shift,
			g.y() + (g.height() - size) / 2,
			size,
			size);
		const auto center = rect::center(rect);
		const auto spanAngle = ratio * arc::kFullLength;
		const auto strokeWidth = style::ConvertFloatScale(kStrokeWidth);

		const auto reachScale = std::clamp(
			(reachRatio > kBouncePart)
				? (kBouncePart * 2 - reachRatio)
				: reachRatio,
			0.,
			1.);
		auto pen = Window::Theme::IsNightMode()
			? QPen(anim::with_alpha(context.st->msgServiceFg()->c, 0.3))
			: QPen(context.st->msgServiceBg());
		pen.setWidthF(strokeWidth - (1. * (reachScale / kBouncePart)));
		const auto arcRect = rect - Margins(strokeWidth);
		p.save();
		{
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(context.st->msgServiceBg());
			p.setOpacity(ratio);
			p.translate(center);
			if (reachScale) {
				p.scale(-(1. + 1. * reachScale), (1. + 1. * reachScale));
			} else {
				p.scale(-1., 1.);
			}
			p.translate(-center);
			// All the next draws are mirrored.
			p.drawEllipse(rect);
			context.st->historyFastShareIcon().paintInCenter(p, rect);
			p.setPen(pen);
			p.setBrush(Qt::NoBrush);
			p.drawArc(arcRect, arc::kQuarterLength, spanAngle);
			// p.drawArc(arcRect, arc::kQuarterLength, spanAngle);
			if (reachRatio) {
				const auto w = style::ConvertFloatScale(kWaveWidth);
				p.setOpacity(ratio - reachRatio);
				p.drawArc(
					arcRect + Margins(reachRatio * reachRatio * w),
					arc::kQuarterLength,
					spanAngle);
			}
		}
		p.restore();
	}
	if (selectionTranslation) {
		p.translate(-selectionTranslation, 0);
	}
	if (selectionModeResult.progress) {
		const auto progress = selectionModeResult.progress;
		if (progress <= 1.) {
			if (context.selected()) {
				if (!_selectionRoundCheckbox) {
					_selectionRoundCheckbox
						= std::make_unique<Ui::RoundCheckbox>(
							st::msgSelectionCheck,
							[this] { repaint(); });
				}
			}
			if (_selectionRoundCheckbox) {
				_selectionRoundCheckbox->setChecked(
					context.selected(),
					anim::type::normal);
			}
			const auto o = ScopedPainterOpacity(p, progress);
			const auto &st = st::msgSelectionCheck;
			const auto right = delegate()->elementIsChatWide()
				? std::min(
					int(_bubbleWidthLimit
						+ st::msgPhotoSkip
						+ st::msgSelectionOffset
						+ st::msgPadding.left()
						+ st.size),
					width())
				: width();
			const auto pos = QPoint(
				(right
					- (st::msgSelectionOffset * progress - st.size) / 2
					- st::msgPadding.right() / 2
					- st.size
					- st::historyScroll.deltax),
				rect::bottom(g) - st.size - st::msgSelectionBottomSkip);
			{
				p.setPen(QPen(st.border, st.width));
				p.setBrush(context.st->msgServiceBg());
				auto hq = PainterHighQualityEnabler(p);
				p.drawEllipse(QRect(pos, Size(st.size)));
			}
			if (_selectionRoundCheckbox) {
				_selectionRoundCheckbox->paint(p, pos.x(), pos.y(), width());
			}
		} else {
			_selectionRoundCheckbox = nullptr;
		}
	} else {
		_selectionRoundCheckbox = nullptr;
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
	const auto info = from ? nullptr : item->displayHiddenSenderInfo();
	Assert(from || info);
	const auto nameFg = !context.outbg
		? FromNameFg(context, colorIndex())
		: stm->msgServiceFg->c;
	const auto nameText = [&] {
		if (from) {
			validateFromNameText(from);
			return static_cast<const Ui::Text::String*>(&_fromName);
		}
		return &info->nameText();
	}();
	const auto statusWidth = _fromNameStatus
		? st::dialogsPremiumIcon.icon.width()
		: 0;
	if (statusWidth && availableWidth > statusWidth) {
		const auto x = availableLeft
			+ std::min(availableWidth - statusWidth, nameText->maxWidth());
		const auto y = trect.top();
		auto color = nameFg;
		color.setAlpha(115);
		const auto id = from ? from->emojiStatusId() : EmojiStatusId();
		if (_fromNameStatus->id != id) {
			const auto that = const_cast<Message*>(this);
			_fromNameStatus->custom = id
				? std::make_unique<Ui::Text::LimitedLoopsEmoji>(
					history()->owner().customEmojiManager().create(
						Data::EmojiStatusCustomId(id),
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
				.paused = context.paused || On(PowerSaving::kEmojiStatus),
			});
		} else {
			st::dialogsPremiumIcon.icon.paint(p, x, y, width(), color);
		}
		availableWidth -= statusWidth;
	}
	p.setFont(st::msgNameFont);
	p.setPen(nameFg);
	nameText->draw(p, {
		.position = { availableLeft, trect.top() },
		.availableWidth = availableWidth,
		.elisionLines = 1,
	});
	const auto skipWidth = nameText->maxWidth()
		+ (_fromNameStatus
			? (st::dialogsPremiumIcon.icon.width()
				+ st::msgServiceFont->spacew)
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
		if (replyWidth) {
			p.setFont(ClickHandler::showAsActive(_fastReplyLink)
				? st::msgFont->underline()
				: st::msgFont);
			p.drawText(
				trect.left() + trect.width() - rightWidth,
				trect.top() + st::msgFont->ascent,
				FastReplyText());
		} else {
			const auto shift = QPoint(trect.width() - rightWidth, 0);
			const auto pen = !_rightBadgeHasBoosts
				? QPen()
				: !context.outbg
				? QPen(FromNameFg(context, colorIndex()))
				: stm->msgServiceFg->p;
			auto colored = std::array<Ui::Text::SpecialColor, 1>{
				{ { &pen, &pen } },
			};
			_rightBadge.draw(p, {
				.position = trect.topLeft() + shift,
				.availableWidth = rightWidth,
				.colors = colored,
				.now = context.now,
			});
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
	if (const auto reply = Get<Reply>()) {
		reply->paint(
			p,
			this,
			context,
			trect.x(),
			trect.y(),
			trect.width(),
			true);
		trect.setY(trect.y() + reply->height());
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
	if (const auto botTop = Get<FakeBotAboutTop>()) {
		botTop->text.drawLeftElided(
			p,
			trect.x(),
			trect.y(),
			trect.width(),
			width());
		trect.setY(trect.y() + botTop->height);
	}
	auto highlightRequest = context.computeHighlightCache();
	text().draw(p, {
		.position = trect.topLeft(),
		.availableWidth = trect.width(),
		.palette = &stm->textPalette,
		.pre = stm->preCache.get(),
		.blockquote = context.quoteCache(contentColorIndex()),
		.colors = context.st->highlightColors(),
		.spoiler = Ui::Text::DefaultSpoilerCache(),
		.now = context.now,
		.pausedEmoji = context.paused || On(PowerSaving::kEmojiChat),
		.pausedSpoiler = context.paused || On(PowerSaving::kChatSpoiler),
		.selection = context.selection,
		.highlight = highlightRequest ? &*highlightRequest : nullptr,
		.useFullWidth = true,
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
			const auto entry = logEntryOriginal();
			const auto check = factcheckBlock();

			// Entry page is always a bubble bottom.
			auto mediaOnBottom = (mediaDisplayed && media->isBubbleBottom()) || check || (entry/* && entry->isBubbleBottom()*/);
			auto mediaOnTop = (mediaDisplayed && media->isBubbleTop()) || (entry && entry->isBubbleTop());

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
			if (check) {
				auto checkHeight = check->height();
				trect.setHeight(trect.height() - checkHeight - st::mediaInBubbleSkip);
			}
			if (entry) {
				auto entryHeight = entry->height();
				trect.setHeight(trect.height() - entryHeight);
			}

			const auto mediaHeight = mediaDisplayed ? media->height() : 0;
			const auto mediaLeft = trect.x() - st::msgPadding.left();
			const auto mediaTop = (!mediaDisplayed || _invertMedia)
				? (trect.y() + (mediaOnTop ? 0 : st::mediaInBubbleSkip))
				: (trect.y() + trect.height() - mediaHeight);
			if (mediaDisplayed && _invertMedia) {
				trect.setY(mediaTop
					+ mediaHeight
					+ (mediaOnBottom ? 0 : st::mediaInBubbleSkip));
			}
			if (point.y() >= mediaTop
				&& point.y() < mediaTop + mediaHeight) {
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
	if (const auto check = factcheckBlock()) {
		check->clickHandlerPressedChanged(handler, pressed);
	}
	if (!handler) {
		return;
	} else if (_rightAction && (handler == _rightAction->link)) {
		toggleRightActionRipple(pressed);
	} else if (_rightAction
		&& _rightAction->second
		&& (handler == _rightAction->second->link)) {
		const auto rightSize = rightActionSize();
		Assert(rightSize != std::nullopt);
		if (pressed) {
			if (!_rightAction->second->ripple) {
				// Create a ripple.
				_rightAction->second->ripple
					= std::make_unique<Ui::RippleAnimation>(
						st::defaultRippleAnimation,
						Ui::RippleAnimation::RoundRectMask(
							Size(rightSize->width()),
							rightSize->width() / 2),
						[=] { repaint(); });
			}
			_rightAction->second->ripple->add(_rightAction->lastPoint);
		} else if (_rightAction->second->ripple) {
			_rightAction->second->ripple->lastStop();
		}
	} else if (_comments && (handler == _comments->link)) {
		toggleCommentsButtonRipple(pressed);
	} else if (_topicButton && (handler == _topicButton->link)) {
		toggleTopicButtonRipple(pressed);
	} else if (_viewButton) {
		_viewButton->checkLink(handler, pressed);
	} else if (const auto reply = Get<Reply>()
		; reply && (handler == reply->link())) {
		toggleReplyRipple(pressed);
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

	const auto rightSize = rightActionSize();
	Assert(rightSize != std::nullopt);

	if (pressed) {
		if (!_rightAction->ripple) {
			// Create a ripple.
			const auto size = _rightAction->second
				? Size(rightSize->width())
				: *rightSize;
			_rightAction->ripple = std::make_unique<Ui::RippleAnimation>(
				st::defaultRippleAnimation,
				Ui::RippleAnimation::RoundRectMask(size, size.width() / 2),
				[=] { repaint(); });
		}
		_rightAction->ripple->add(_rightAction->lastPoint);
	} else if (_rightAction->ripple) {
		_rightAction->ripple->lastStop();
	}
}

void Message::toggleReplyRipple(bool pressed) {
	const auto reply = Get<Reply>();
	if (!reply) {
		return;
	}

	if (pressed) {
		if (!unwrapped()) {
			const auto &padding = st::msgPadding;
			const auto geometry = countGeometry();
			const auto margins = reply->margins();
			const auto size = QSize(
				geometry.width() - padding.left() - padding.right(),
				reply->height() - margins.top() - margins.bottom());
			reply->createRippleAnimation(this, size);
		}
		reply->addRipple();
	} else {
		reply->stopLastRipple();
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
	_comments = nullptr;
	if (_fromNameStatus) {
		_fromNameStatus->custom = nullptr;
		_fromNameStatus->id = EmojiStatusId();
	}
}

bool Message::hasFromPhoto() const {
	if (isHidden()) {
		return false;
	}
	switch (context()) {
	case Context::AdminLog:
		return true;
	case Context::History:
	case Context::ChatPreview:
	case Context::TTLViewer:
	case Context::Pinned:
	case Context::Replies:
	case Context::SavedSublist:
	case Context::ScheduledTopic: {
		const auto item = data();
		if (item->isSponsored()) {
			return false;
		} else if (item->isPostHidingAuthor()) {
			return false;
		} else if (item->isPost()) {
			return true;
		} else if (item->isEmpty()
			|| item->isFakeAboutView()
			|| (context() == Context::Replies && item->isDiscussionPost())) {
			return false;
		} else if (delegate()->elementIsChatWide()) {
			return true;
		} else if (item->history()->peer->isVerifyCodes()) {
			return !hasOutLayout();
		} else if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
			const auto peer = item->history()->peer;
			if (peer->isSelf() || peer->isRepliesChat()) {
				return !hasOutLayout();
			}
		}
		return !item->out() && !item->history()->peer->isUser();
	} break;
	case Context::ContactPreview:
	case Context::ShortcutMessages:
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
	const auto visibleMediaTextLen = visibleMediaTextLength();
	const auto visibleTextLen = visibleTextLength();
	const auto minSymbol = (_invertMedia && request.onlyMessageText)
		? visibleMediaTextLen
		: 0;
	result.symbol = minSymbol;

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
			result.symbol += visibleMediaTextLen + visibleTextLen;
			return result;
		}
	}

	if (bubble) {
		const auto inBubble = g.contains(point);
		const auto check = factcheckBlock();
		const auto entry = logEntryOriginal();

		// Entry page is always a bubble bottom.
		auto mediaOnBottom = (mediaDisplayed && media->isBubbleBottom()) || check || (entry/* && entry->isBubbleBottom()*/);
		auto mediaOnTop = (mediaDisplayed && media->isBubbleTop()) || (entry && entry->isBubbleTop());

		auto inner = g;
		if (getStateCommentsButton(point, inner, &result)) {
			result.symbol += visibleMediaTextLen + visibleTextLen;
			return result;
		}
		auto trect = inner.marginsRemoved(st::msgPadding);
		const auto additionalInfoSkip = (mediaDisplayed
			&& !media->additionalInfoString().isEmpty())
			? st::msgDateFont->height
			: 0;
		const auto reactionsTop = (reactionsInBubble && !_viewButton)
			? (additionalInfoSkip + st::mediaInBubbleSkip)
			: additionalInfoSkip;
		const auto reactionsHeight = reactionsInBubble
			? (reactionsTop + _reactions->height())
			: 0;
		if (reactionsInBubble) {
			trect.setHeight(trect.height() - reactionsHeight);
			const auto reactionsPosition = QPoint(trect.left(), trect.top() + trect.height() + reactionsTop);
			if (_reactions->getState(point - reactionsPosition, &result)) {
				result.symbol += visibleMediaTextLen + visibleTextLen;
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
				result.symbol += visibleMediaTextLen + visibleTextLen;
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
			if (getStateViaBotIdInfo(point, trect, &result)) {
				return result;
			}
			if (getStateReplyInfo(point, trect, &result)) {
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
		if (check) {
			auto checkHeight = check->height();
			trect.setHeight(trect.height() - checkHeight - st::mediaInBubbleSkip);
			auto checkLeft = inner.left();
			auto checkTop = trect.y() + trect.height() + st::mediaInBubbleSkip;
			if (point.y() >= checkTop && point.y() < checkTop + checkHeight) {
				result = check->textState(
					point - QPoint(checkLeft, checkTop),
					request);
				result.symbol += visibleTextLength()
					+ visibleMediaTextLength();
			}
		}

		auto checkBottomInfoState = [&] {
			if (mediaOnBottom
				&& (check || entry || media->customInfoLayout())) {
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
		if (!inBubble) {
			if (point.y() >= g.y() + g.height()) {
				result.symbol += visibleTextLen + visibleMediaTextLen;
			}
		} else if (result.symbol <= minSymbol) {
			const auto mediaHeight = mediaDisplayed ? media->height() : 0;
			const auto mediaLeft = trect.x() - st::msgPadding.left();
			const auto mediaTop = (!mediaDisplayed || _invertMedia)
				? (trect.y() + (mediaOnTop ? 0 : st::mediaInBubbleSkip))
				: (trect.y() + trect.height() - mediaHeight);
			if (mediaDisplayed && _invertMedia) {
				trect.setY(mediaTop
					+ mediaHeight
					+ (mediaOnBottom ? 0 : st::mediaInBubbleSkip));
			}
			if (point.y() >= mediaTop
				&& point.y() < mediaTop + mediaHeight) {
				result = media->textState(
					point - QPoint(mediaLeft, mediaTop),
					request);
				if (_invertMedia) {
					if (request.onlyMessageText) {
						result.symbol = minSymbol;
						result.afterSymbol = false;
						result.cursor = CursorState::None;
					}
				} else if (request.onlyMessageText) {
					result.symbol = visibleTextLen;
					result.afterSymbol = false;
					result.cursor = CursorState::None;
				} else {
					result.symbol += visibleTextLen;
				}
			} else if (getStateText(point, trect, &result, request)) {
				if (_invertMedia) {
					result.symbol += visibleMediaTextLen;
				}
				result.overMessageText = true;
				checkBottomInfoState();
				return result;
			} else if (point.y() >= trect.y() + trect.height()) {
				result.symbol = visibleTextLen + visibleMediaTextLen;
			}
		}
		checkBottomInfoState();
		if (const auto size = rightActionSize(); size && _rightAction) {
			const auto fastShareSkip = std::clamp(
				(g.height() - size->height()) / 2,
				0,
				st::historyFastShareBottom);
			const auto fastShareLeft = hasRightLayout()
				? (g.left() - size->width() - st::historyFastShareLeft)
				: (g.left() + g.width() + st::historyFastShareLeft);
			const auto fastShareTop = data()->isSponsored()
				? g.top() + fastShareSkip
				: g.top() + g.height() - fastShareSkip - size->height();
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
		if (request.onlyMessageText) {
			result.symbol = 0;
			result.afterSymbol = false;
			result.cursor = CursorState::None;
		}
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
		const auto controller = ExtractController(context);
		if (!controller || controller->session().uniqueId() != sessionId) {
			return;
		}
		if (const auto item = controller->session().data().message(fullId)) {
			const auto history = item->history();
			if (const auto channel = history->peer->asChannel()) {
				if (channel->invitePeekExpires()) {
					controller->showToast(
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
			} else if (const auto info = item->displayHiddenSenderInfo()) {
				return &info->nameText();
			} else {
				Unexpected("Corrupt forwarded information in message.");
			}
		}();

		const auto statusWidth = (from && _fromNameStatus)
			? st::dialogsPremiumIcon.icon.width()
			: 0;
		if (statusWidth && availableWidth > statusWidth) {
			const auto x = availableLeft + std::min(
				availableWidth - statusWidth,
				nameText->maxWidth()
			) - (_fromNameStatus->custom ? (2 * _fromNameStatus->skip) : 0);
			const auto checkWidth = _fromNameStatus->custom
				? (st::emojiSize - 2 * _fromNameStatus->skip)
				: statusWidth;
			if (point.x() >= x && point.x() < x + checkWidth) {
				ensureFromNameStatusLink(from);
				outResult->link = _fromNameStatus->link;
				return true;
			}
			availableWidth -= statusWidth;
		}
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

void Message::ensureFromNameStatusLink(not_null<PeerData*> peer) const {
	Expects(_fromNameStatus != nullptr);

	if (_fromNameStatus->link) {
		return;
	}
	_fromNameStatus->link = std::make_shared<LambdaClickHandler>([=](
			ClickContext context) {
		const auto controller = ExtractController(context);
		if (controller) {
			Settings::ShowEmojiStatusPremium(controller, peer);
		}
	});
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
	if (const auto reply = Get<Reply>()) {
		const auto margins = reply->margins();
		const auto height = reply->height();
		if (point.y() >= trect.top() && point.y() < trect.top() + height) {
			const auto g = QRect(
				trect.x(),
				trect.y() + margins.top(),
				trect.width(),
				height - margins.top() - margins.bottom());
			if (g.contains(point)) {
				if (const auto link = reply->link()) {
					outResult->link = reply->link();
					reply->saveRipplePoint(point - g.topLeft());
				}
			}
			return true;
		}
		trect.setTop(trect.top() + height);
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
	} else if (const auto botTop = Get<FakeBotAboutTop>()) {
		trect.setY(trect.y() + botTop->height);
	}
	const auto item = this->textItem();
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
	if (!media) {
		return;
	}

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
			if (const auto reply = Get<Reply>()) {
				trect.setTop(trect.top() + reply->height());
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
	auto factcheckResult = TextForMimeData();
	const auto mediaDisplayed = (media && media->isDisplayed());
	const auto mediaBefore = mediaDisplayed && invertMedia();
	const auto textSelection = mediaBefore
		? media->skipSelection(selection)
		: selection;
	const auto mediaSelection = !invertMedia()
		? skipTextSelection(selection)
		: selection;
	auto textResult = hasVisibleText()
		? text().toTextForMimeData(textSelection)
		: TextForMimeData();
	auto mediaResult = (mediaDisplayed || isHiddenByGroup())
		? media->selectedText(mediaSelection)
		: TextForMimeData();
	if (const auto check = factcheckBlock()) {
		const auto checkSelection = mediaBefore
			? skipTextSelection(textSelection)
			: mediaDisplayed
			? media->skipSelection(mediaSelection)
			: skipTextSelection(selection);
		factcheckResult = check->selectedText(checkSelection);
	}
	if (const auto entry = logEntryOriginal()) {
		const auto originalSelection = mediaBefore
			? skipTextSelection(textSelection)
			: mediaDisplayed
			? media->skipSelection(mediaSelection)
			: skipTextSelection(selection);
		logEntryOriginalResult = entry->selectedText(originalSelection);
	}
	auto &first = mediaBefore ? mediaResult : textResult;
	auto &second = mediaBefore ? textResult : mediaResult;
	auto result = first;
	if (result.empty()) {
		result = std::move(second);
	} else if (!second.empty()) {
		result.append(u"\n\n"_q).append(std::move(second));
	}
	if (result.empty()) {
		result = std::move(factcheckResult);
	} else if (!factcheckResult.empty()) {
		result.append(u"\n\n"_q).append(std::move(factcheckResult));
	}
	if (result.empty()) {
		result = std::move(logEntryOriginalResult);
	} else if (!logEntryOriginalResult.empty()) {
		result.append(u"\n\n"_q).append(std::move(logEntryOriginalResult));
	}
	return result;
}

SelectedQuote Message::selectedQuote(TextSelection selection) const {
	const auto textItem = this->textItem();
	const auto item = textItem ? textItem : data().get();
	const auto &translated = item->translatedText();
	const auto &original = item->originalText();
	if (&translated != &original
		|| selection.empty()
		|| selection == FullSelection) {
		return {};
	} else if (hasVisibleText()) {
		const auto media = this->media();
		const auto mediaDisplayed = media && media->isDisplayed();
		const auto mediaBefore = mediaDisplayed && invertMedia();
		const auto textSelection = mediaBefore
			? media->skipSelection(selection)
			: selection;
		return FindSelectedQuote(text(), textSelection, item);
	} else if (const auto media = this->media()) {
		if (media->isDisplayed() || isHiddenByGroup()) {
			return media->selectedQuote(selection);
		}
	}
	return {};
}

TextSelection Message::selectionFromQuote(
		const SelectedQuote &quote) const {
	Expects(quote.item != nullptr);

	if (quote.text.empty()) {
		return {};
	}
	const auto item = quote.item;
	const auto &translated = item->translatedText();
	const auto &original = item->originalText();
	if (&translated != &original) {
		return {};
	} else if (hasVisibleText()) {
		const auto media = this->media();
		const auto mediaDisplayed = media && media->isDisplayed();
		const auto mediaBefore = mediaDisplayed && invertMedia();
		const auto result = FindSelectionFromQuote(text(), quote);
		return mediaBefore ? media->unskipSelection(result) : result;
	} else if (const auto media = this->media()) {
		if (media->isDisplayed() || isHiddenByGroup()) {
			return media->selectionFromQuote(quote);
		}
	}
	return {};
}

TextSelection Message::adjustSelection(
		TextSelection selection,
		TextSelectType type) const {
	const auto media = this->media();
	const auto mediaDisplayed = media && media->isDisplayed();
	const auto mediaBefore = mediaDisplayed && invertMedia();
	const auto textSelection = mediaBefore
		? media->skipSelection(selection)
		: selection;
	const auto useSelection = [](TextSelection selection, bool skipped) {
		return !skipped || (selection != TextSelection(uint16(), uint16()));
	};
	auto textAdjusted = (hasVisibleText()
		&& useSelection(textSelection, mediaBefore))
		? text().adjustSelection(textSelection, type)
		: textSelection;
	auto textResult = mediaBefore
		? media->unskipSelection(textAdjusted)
		: textAdjusted;
	auto mediaResult = TextSelection();
	auto mediaSelection = mediaBefore
		? selection
		: skipTextSelection(selection);
	if (mediaDisplayed) {
		auto mediaAdjusted = useSelection(mediaSelection, !mediaBefore)
			? media->adjustSelection(mediaSelection, type)
			: mediaSelection;
		mediaResult = mediaBefore
			? mediaAdjusted
			: unskipTextSelection(mediaAdjusted);
	}
	auto checkResult = TextSelection();
	if (const auto check = factcheckBlock()) {
		auto checkSelection = !mediaDisplayed
			? skipTextSelection(selection)
			: mediaBefore
			? skipTextSelection(textSelection)
			: media->skipSelection(mediaSelection);
		auto checkAdjusted = useSelection(checkSelection, true)
			? check->adjustSelection(checkSelection, type)
			: checkSelection;
		checkResult = unskipTextSelection(checkAdjusted);
		if (mediaDisplayed) {
			checkResult = media->unskipSelection(checkResult);
		}
	}
	auto entryResult = TextSelection();
	if (const auto entry = logEntryOriginal()) {
		auto entrySelection = !mediaDisplayed
			? skipTextSelection(selection)
			: mediaBefore
			? skipTextSelection(textSelection)
			: media->skipSelection(mediaSelection);
		auto entryAdjusted = useSelection(entrySelection, true)
			? entry->adjustSelection(entrySelection, type)
			: entrySelection;
		entryResult = unskipTextSelection(entryAdjusted);
		if (mediaDisplayed) {
			entryResult = media->unskipSelection(entryResult);
		}
	}
	auto result = textResult;
	if (!mediaResult.empty()) {
		result = result.empty() ? mediaResult : TextSelection{
			std::min(result.from, mediaResult.from),
			std::max(result.to, mediaResult.to),
		};
	}
	if (!checkResult.empty()) {
		result = result.empty() ? checkResult : TextSelection{
			std::min(result.from, checkResult.from),
			std::max(result.to, checkResult.to),
		};
	}
	if (!entryResult.empty()) {
		result = result.empty() ? entryResult : TextSelection{
			std::min(result.from, entryResult.from),
			std::max(result.to, entryResult.to),
		};
	}
	return result;
}

Reactions::ButtonParameters Message::reactionButtonParameters(
		QPoint position,
		const TextState &reactionState) const {
	using namespace Reactions;
	auto result = ButtonParameters{ .context = data()->fullId() };
	const auto outsideBubble = (!_comments && !embedReactionsInBubble());
	const auto geometry = countGeometry();
	result.pointer = position;
	const auto onTheLeft = hasRightLayout();

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
		this,
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

bool Message::embedReactionsInBubble() const {
	return needInfoDisplay();
}

void Message::validateInlineKeyboard(HistoryMessageReplyMarkup *markup) {
	if (!markup
		|| markup->inlineKeyboard
		|| markup->hiddenBy(data()->media())) {
		return;
	}
	const auto item = data();
	markup->inlineKeyboard = std::make_unique<ReplyKeyboard>(
		item,
		std::make_unique<KeyboardStyle>(
			st::msgBotKbButton,
			[=] { item->history()->owner().requestItemRepaint(item); }));
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
	if (from->isPremium()
		|| (from->isChannel()
			&& from->emojiStatusId()
			&& from != history()->peer)) {
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

bool Message::updateBottomInfo() {
	const auto wasInfo = _bottomInfo.currentSize();
	_bottomInfo.update(BottomInfoDataFromMessage(this), width());
	return (_bottomInfo.currentSize() != wasInfo);
}

void Message::itemDataChanged() {
	const auto infoChanged = updateBottomInfo();
	const auto reactionsChanged = updateReactions();

	if (infoChanged || reactionsChanged) {
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
	if (_viewButton) {
		_viewButton = nullptr;
		updateViewButtonExistence();
	}
	if (_comments) {
		_comments->link = nullptr;
	}
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
	const auto media = item->media();
	const auto has = (media && ViewButton::MediaHasViewButton(media));
	if (!has) {
		_viewButton = nullptr;
		return;
	} else if (_viewButton) {
		return;
	}
	auto make = [=](auto &&from) {
		return std::make_unique<ViewButton>(
			std::forward<decltype(from)>(from),
			colorIndex(),
			[=] { repaint(); });
	};
	_viewButton = make(media);
}

void Message::initLogEntryOriginal() {
	if (const auto log = data()->Get<HistoryMessageLogEntryOriginal>()) {
		AddComponents(LogEntryOriginal::Bit());
		const auto entry = Get<LogEntryOriginal>();
		using Flags = MediaWebPageFlags;
		entry->page = std::make_unique<WebPage>(this, log->page, Flags());
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

WebPage *Message::factcheckBlock() const {
	if (const auto entry = Get<Factcheck>()) {
		return entry->page.get();
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

bool Message::allowTextSelectionByHandler(
		const ClickHandlerPtr &handler) const {
	if (const auto media = this->media()) {
		if (media->allowTextSelectionByHandler(handler)) {
			return true;
		}
	}
	if (dynamic_cast<Ui::Text::BlockquoteClickHandler*>(handler.get())) {
		return true;
	}
	return false;
}

bool Message::hasFromName() const {
	switch (context()) {
	case Context::AdminLog:
		return true;
	case Context::History:
	case Context::ChatPreview:
	case Context::TTLViewer:
	case Context::Pinned:
	case Context::Replies:
	case Context::SavedSublist:
	case Context::ScheduledTopic: {
		const auto item = data();
		const auto peer = item->history()->peer;
		if (hasOutLayout() && !item->from()->isChannel()) {
			if (peer->isSelf()) {
				if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
					return forwarded->savedFromSender
						&& forwarded->savedFromSender->isChannel();
				}
			}
			return false;
		} else if (!peer->isUser()) {
			if (const auto media = this->media()) {
				return !media->hideFromName();
			}
			return true;
		}
		if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
			if (forwarded->imported
				&& peer.get() == forwarded->originalSender) {
				return false;
			} else if (item->showForwardsFromSender(forwarded)) {
				return true;
			}
		}
		return false;
	} break;
	case Context::ContactPreview:
	case Context::ShortcutMessages:
		return false;
	}
	Unexpected("Context in Message::hasFromName.");
}

bool Message::displayFromName() const {
	if (!hasFromName() || isAttachedToPrevious() || data()->isSponsored()) {
		return false;
	}
	return !Has<PsaTooltipState>();
}

bool Message::displayForwardedFrom() const {
	const auto item = data();
	if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
		if (forwarded->story) {
			return true;
		} else if (item->showForwardsFromSender(forwarded)) {
			return forwarded->savedFromHiddenSenderInfo
				|| (forwarded->savedFromSender
					&& (forwarded->savedFromSender
						!= forwarded->originalSender));
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
		if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
			if (context() == Context::ShortcutMessages) {
				return true;
			}
			return (context() == Context::SavedSublist)
				&& (!forwarded->forwardOfForward()
					? (forwarded->originalSender
						&& forwarded->originalSender->isSelf())
					: ((forwarded->savedFromSender
						&& forwarded->savedFromSender->isSelf())
						|| forwarded->savedFromOutgoing));
		}
		return true;
	} else if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
		if (!forwarded->imported
			|| !forwarded->originalSender
			|| !forwarded->originalSender->isSelf()) {
			if (item->showForwardsFromSender(forwarded)) {
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
	} else if (logEntryOriginal()
		|| factcheckBlock()
		|| item->isFakeAboutView()) {
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
	} else if (logEntryOriginal() || factcheckBlock()) {
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
	const auto canSendAnything = [&] {
		const auto item = data();
		const auto peer = item->history()->peer;
		const auto topic = item->topic();
		return topic
			? Data::CanSendAnything(topic)
			: Data::CanSendAnything(peer);
	};

	return hasFastReply()
		&& data()->isRegular()
		&& canSendAnything()
		&& !delegate()->elementInSelectionMode(this).inSelectionMode;
}

bool Message::displayRightActionComments() const {
	return !isPinnedContext()
		&& (context() != Context::SavedSublist)
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
	return data()->isSponsored()
		? ((_rightAction && _rightAction->second)
			? QSize(st::historyFastCloseSize, st::historyFastCloseSize * 2)
			: QSize(st::historyFastCloseSize, st::historyFastCloseSize))
		: (displayFastShare() || displayGoToOriginal())
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
			return !item->out()
				&& forwarded->originalSender
				&& forwarded->originalSender->isBroadcast()
				&& !item->showForwardsFromSender(forwarded);
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
			&& (context() != Context::Replies);
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
	if (_rightAction->second && _rightAction->second->ripple) {
		const auto &stm = context.messageStyle();
		const auto colorOverride = &stm->msgWaveformInactive->c;
		_rightAction->second->ripple->paint(
			p,
			left,
			top + st::historyFastCloseSize,
			size->width(),
			colorOverride);
		if (_rightAction->second->ripple->empty()) {
			_rightAction->second->ripple.reset();
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
	} else if (_rightAction->second) {
		st->historyFastCloseIcon().paintInCenter(
			p,
			QRect(left, top, size->width(), size->width()));
		st->historyFastMoreIcon().paintInCenter(
			p,
			QRect(left, size->width() + top, size->width(), size->width()));
	} else {
		const auto &icon = data()->isSponsored()
			? st->historyFastCloseIcon()
			: (displayFastShare()
				&& !isPinnedContext()
				&& this->context() != Context::SavedSublist)
			? st->historyFastShareIcon()
			: st->historyGoToOriginalIcon();
		icon.paintInCenter(p, Rect(left, top, *size));
	}
}

ClickHandlerPtr Message::rightActionLink(
		std::optional<QPoint> pressPoint) const {
	if (delegate()->elementInSelectionMode(this).progress > 0) {
		return nullptr;
	}
	ensureRightAction();
	if (!_rightAction->link) {
		_rightAction->link = prepareRightActionLink();
	}
	if (pressPoint) {
		_rightAction->lastPoint = *pressPoint;
	}
	if (_rightAction->second
		&& (_rightAction->lastPoint.y() > st::historyFastCloseSize)) {
		return _rightAction->second->link;
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
	if (data()->isSponsored()) {
		return HideSponsoredClickHandler();
	} else if (isPinnedContext()) {
		return JumpToMessageClickHandler(data());
	} else if ((context() != Context::SavedSublist)
		&& displayRightActionComments()) {
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
		const auto controller = ExtractController(context);
		if (!controller || controller->session().uniqueId() != sessionId) {
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
		delegate()->elementReplyTo({ itemId });
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
		|| reactionsInBubble
		|| (invertMedia() && hasVisibleText());
	auto mediaHasSomethingAbove = false;
	auto getMediaHasSomethingAbove = [&] {
		return displayFromName()
			|| displayedTopicButton()
			|| displayForwardedFrom()
			|| Has<Reply>()
			|| item->Has<HistoryMessageVia>();
	};
	const auto entry = logEntryOriginal();
	const auto check = factcheckBlock();
	if (check) {
		mediaHasSomethingBelow = true;
		mediaHasSomethingAbove = getMediaHasSomethingAbove();
		auto checkState = (mediaHasSomethingAbove
			|| hasVisibleText()
			|| (media && media->isDisplayed()))
			? MediaInBubbleState::Bottom
			: MediaInBubbleState::None;
		check->setInBubbleState(checkState);
		if (!media) {
			check->setBubbleRounding(countBubbleRounding());
			return;
		}
	} else if (entry) {
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

	if (!check && !entry) {
		mediaHasSomethingAbove = getMediaHasSomethingAbove();
	}
	if (!invertMedia() && hasVisibleText()) {
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
				} else if (const auto info = item->originalHiddenSenderInfo()) {
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
					? (st::dialogsPremiumIcon.icon.width()
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
		const auto cut = [&](int amount) {
			amount = std::min(amount, result.height());
			result.setTop(result.top() + amount);
		};
		cut(st::msgPadding.top() + st::mediaInBubbleSkip);

		if (displayFromName()) {
			// See paintFromName().
			cut(st::msgNameFont->height);
		}
		if (displayedTopicButton()) {
			cut(st::topicButtonSkip
				+ st::topicButtonPadding.top()
				+ st::msgNameFont->height
				+ st::topicButtonPadding.bottom()
				+ st::topicButtonSkip);
		}
		if (!displayFromName() && !displayForwardedFrom()) {
			// See paintViaBotIdInfo().
			if (data()->Has<HistoryMessageVia>()) {
				cut(st::msgServiceNameFont->height);
			}
		}
		// Skip displayForwardedFrom() until there are no animations for it.
		if (const auto reply = Get<Reply>()) {
			// See paintReplyInfo().
			cut(reply->height());
		}
	}
	return result;
}

QRect Message::countGeometry() const {
	const auto item = data();
	const auto centeredView = item->isFakeAboutView()
		|| (context() == Context::Replies && item->isDiscussionPost());
	const auto media = this->media();
	const auto mediaWidth = (media && media->isDisplayed())
		? media->width()
		: width();
	const auto outbg = hasOutLayout();
	const auto availableWidth = width()
		- st::msgMargin.left()
		- (centeredView ? st::msgMargin.left() : st::msgMargin.right());
	auto contentLeft = hasRightLayout()
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
	accumulate_min(contentWidth, int(_bubbleWidthLimit));
	if (mediaWidth < contentWidth) {
		const auto textualWidth = textualMaxWidth();
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
		} else if (centeredView) {
			contentLeft += (availableWidth - contentWidth) / 2;
		}
	} else if (contentWidth < availableWidth && centeredView) {
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
	const auto item = data();
	const auto keyboard = item->inlineReplyKeyboard();
	const auto skipTail = smallBottom
		|| (media && media->skipBubbleTail())
		|| (keyboard != nullptr)
		|| item->isFakeAboutView()
		|| (context() == Context::Replies && item->isDiscussionPost());
	const auto right = hasRightLayout();
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

	const auto item = data();
	const auto postShowingAuthor = item->isPostShowingAuthor() ? 1 : 0;
	if (_postShowingAuthor != postShowingAuthor) {
		_postShowingAuthor = postShowingAuthor;
		_fromNameVersion = -1;
		previousInBlocksChanged();

		const auto size = _bottomInfo.currentSize();
		_bottomInfo.update(BottomInfoDataFromMessage(this), newWidth);
		if (size != _bottomInfo.currentSize()) {
			// maxWidth may have changed, full recount required.
			setPendingResize();
			return resizeGetHeight(newWidth);
		}
	}

	auto newHeight = minHeight();

	if (const auto service = Get<ServicePreMessage>()) {
		service->resizeToWidth(newWidth, delegate()->elementIsChatWide());
	}

	const auto botTop = item->isFakeAboutView()
		? Get<FakeBotAboutTop>()
		: nullptr;
	const auto media = this->media();
	const auto mediaDisplayed = media ? media->isDisplayed() : false;
	const auto bubble = drawBubble();

	item->resolveDependent();

	// This code duplicates countGeometry() but also resizes media.
	const auto centeredView = item->isFakeAboutView()
		|| (context() == Context::Replies && item->isDiscussionPost());
	auto contentWidth = newWidth
		- st::msgMargin.left()
		- (centeredView ? st::msgMargin.left() : st::msgMargin.right());
	if (hasFromPhoto()) {
		if (const auto size = rightActionSize()) {
			contentWidth -= size->width() + (st::msgPhotoSkip - st::historyFastShareSize);
		}
	}
	accumulate_min(contentWidth, maxWidth());
	_bubbleWidthLimit = std::max(st::msgMaxWidth, monospaceMaxWidth());
	accumulate_min(contentWidth, int(_bubbleWidthLimit));
	if (mediaDisplayed) {
		media->resizeGetHeight(contentWidth);
		if (media->width() < contentWidth) {
			const auto textualWidth = textualMaxWidth();
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
		auto reply = Get<Reply>();
		auto via = item->Get<HistoryMessageVia>();
		const auto check = factcheckBlock();
		const auto entry = logEntryOriginal();

		// Entry page is always a bubble bottom.
		auto mediaOnBottom = (mediaDisplayed && media->isBubbleBottom()) || check || (entry/* && entry->isBubbleBottom()*/);
		auto mediaOnTop = (mediaDisplayed && media->isBubbleTop()) || (entry && entry->isBubbleTop());

		if (reactionsInBubble) {
			_reactions->resizeGetHeight(textWidth);
		}

		if (contentWidth == maxWidth()) {
			if (mediaDisplayed) {
				if (check) {
					newHeight += check->resizeGetHeight(contentWidth) + st::mediaInBubbleSkip;
				}
				if (entry) {
					newHeight += entry->resizeGetHeight(contentWidth);
				}
			} else {
				if (check) {
					check->resizeGetHeight(contentWidth);
				}
				if (entry) {
					// In case of text-only message it is counted in minHeight already.
					entry->resizeGetHeight(contentWidth);
				}
			}
		} else {
			const auto withVisibleText = hasVisibleText();
			newHeight = 0;
			if (withVisibleText) {
				if (botTop) {
					newHeight += botTop->height;
				}
				newHeight += textHeightFor(textWidth);
			}
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
			}
			if (check) {
				newHeight += check->resizeGetHeight(contentWidth) + st::mediaInBubbleSkip;
			}
			if (entry) {
				newHeight += entry->resizeGetHeight(contentWidth);
			}
			if (reactionsInBubble) {
				if (mediaDisplayed
					&& !media->additionalInfoString().isEmpty()) {
					// In round videos in a web page status text is painted
					// in the bottom left corner, reactions should be below.
					newHeight += st::msgDateFont->height;
				} else {
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
			newHeight += reply->resizeToWidth(contentWidth
				- st::msgPadding.left()
				- st::msgPadding.right());
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
		if (hasRightLayout()) {
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
	const auto check = factcheckBlock();
	const auto entry = logEntryOriginal();
	return entry
		? !entry->customInfoLayout()
		: check
		? !check->customInfoLayout()
		: ((mediaDisplayed && media->isBubbleBottom())
			? !media->customInfoLayout()
			: true);
}

bool Message::invertMedia() const {
	return _invertMedia;
}

bool Message::hasVisibleText() const {
	const auto textItem = this->textItem();
	if (!textItem) {
		return false;
	} else if (textItem->emptyText()) {
		if (const auto media = textItem->media()) {
			return media->storyExpired();
		}
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

void Message::refreshInfoSkipBlock(HistoryItem *textItem) {
	const auto media = this->media();
	const auto hasTextSkipBlock = [&] {
		if (!textItem || textItem->_text.empty()) {
			if (const auto media = data()->media()) {
				return media->storyExpired();
			}
			return false;
		} else if (factcheckBlock()
			|| data()->Has<HistoryMessageLogEntryOriginal>()) {
			return false;
		} else if (media && media->isDisplayed() && !_invertMedia) {
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
