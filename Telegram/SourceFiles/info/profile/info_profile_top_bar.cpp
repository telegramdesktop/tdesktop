/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_top_bar.h"

#include "api/api_peer_colors.h"
#include "api/api_peer_photo.h"
#include "api/api_user_privacy.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "base/timer_rpl.h"
#include "base/timer.h"
#include "base/unixtime.h"
#include "boxes/peers/edit_peer_info_box.h" // EditPeerInfoBox::Available.
#include "boxes/peers/edit_forum_topic_box.h"
#include "boxes/moderate_messages_box.h"
#include "boxes/report_messages_box.h"
#include "boxes/star_gift_box.h"
#include "calls/calls_instance.h"
#include "chat_helpers/stickers_lottie.h"
#include "core/application.h"
#include "core/shortcuts.h"
#include "data/components/recent_shared_media_gifts.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_document_media.h"
#include "data/data_document.h"
#include "data/data_emoji_statuses.h"
#include "data/data_forum_topic.h"
#include "data/data_forum.h"
#include "data/data_peer_values.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "data/data_star_gift.h"
#include "data/data_stories.h"
#include "data/data_user.h"
#include "data/notify/data_notify_settings.h"
#include "data/notify/data_peer_notify_settings.h"
#include "data/stickers/data_custom_emoji.h"
#include "editor/photo_editor_common.h"
#include "editor/photo_editor_layer_widget.h"
#include "history/history.h"
#include "info/info_memento.h"
#include "info/profile/info_profile_badge_tooltip.h"
#include "info/profile/info_profile_badge.h"
#include "info/profile/info_profile_cover.h" // LargeCustomEmojiMargins
#include "info/profile/info_profile_status_label.h"
#include "info/profile/info_profile_top_bar_action_button.h"
#include "info/profile/info_profile_values.h"
#include "info/userpic/info_userpic_emoji_builder_common.h"
#include "info/userpic/info_userpic_emoji_builder_common.h"
#include "info/userpic/info_userpic_emoji_builder_menu_item.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_animation.h"
#include "lottie/lottie_multi_player.h"
#include "main/main_session.h"
#include "menu/menu_mute.h"
#include "settings/settings_credits_graphics.h"
#include "settings/settings_information.h"
#include "settings/settings_premium.h"
#include "ui/boxes/show_or_premium_box.h"
#include "ui/color_contrast.h"
#include "ui/controls/stars_rating.h"
#include "ui/controls/userpic_button.h"
#include "ui/effects/animations.h"
#include "ui/effects/outline_segments.h"
#include "ui/effects/round_checkbox.h"
#include "ui/empty_userpic.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/peer/video_userpic_player.h"
#include "ui/rect.h"
#include "ui/text/text_utilities.h"
#include "ui/top_background_gradient.h"
#include "ui/ui_utility.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/horizontal_fit_container.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/tooltip.h"
#include "ui/wrap/fade_wrap.h"
#include "window/themes/window_theme.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "boxes/sticker_set_box.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_chat.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QGraphicsOpacityEffect>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

namespace Info::Profile {
namespace {

class Userpic final
	: public Ui::AbstractButton
	, public Ui::AbstractTooltipShower {
public:
	Userpic(QWidget *parent, Fn<bool()> hasStories)
	: Ui::AbstractButton(parent)
	, _hasStories(std::move(hasStories)) {
		installEventFilter(this);
	}

	QString tooltipText() const override {
		return _hasStories() ? tr::lng_view_button_story(tr::now) : QString();
	}

	QPoint tooltipPos() const override {
		return QCursor::pos();
	}

	bool tooltipWindowActive() const override {
		return Ui::AppInFocus() && Ui::InFocusChain(window());
	}

protected:
	bool eventFilter(QObject *obj, QEvent *e) override {
		if (obj == this && e->type() == QEvent::Enter && _hasStories()) {
			Ui::Tooltip::Show(1000, this);
		}
		return Ui::AbstractButton::eventFilter(obj, e);
	}

private:
	Fn<bool()> _hasStories;

};

constexpr auto kWaitBeforeGiftBadge = crl::time(1000);
constexpr auto kGiftBadgeGlares = 3;
constexpr auto kMinPatternRadius = 8;
constexpr auto kMinContrast = 5.5;
constexpr auto kStoryOutlineFadeEnd = 0.4;
constexpr auto kStoryOutlineFadeRange = 1. - kStoryOutlineFadeEnd;

using AnimatedPatternPoint = TopBar::AnimatedPatternPoint;

struct PatternColors {
	QColor patternColor;
	bool useOverlayBlend = false;
};

[[nodiscard]] PatternColors CalculatePatternColors(
		const std::optional<Data::ColorProfileSet> &colorProfile,
		const std::shared_ptr<Data::EmojiStatusCollectible> &collectible,
		const std::optional<QColor> &edgeColor,
		bool isDark) {
	if (collectible && collectible->patternColor.isValid()) {
		auto blended = Ui::BlendColors(
			collectible->patternColor,
			Qt::black,
			isDark ? (140. / 255) : (160. / 255));
		auto result = !edgeColor
			? std::move(blended)
			: (Ui::CountContrast(blended, *edgeColor)
				> Ui::CountContrast(collectible->patternColor, *edgeColor))
			? std::move(blended)
			: collectible->patternColor;
		return {
			.patternColor = std::move(result),
			// .patternColor = collectible->patternColor.lighter(isDark
			// 	? 140
			// 	: 160),
			.useOverlayBlend = false
		};
	}
	if (colorProfile && !colorProfile->bg.empty()) {
		return {
			.patternColor = QColor(0, 0, 0, int(0.6 * 255)),
			.useOverlayBlend = true
		};
	}
	const auto baseWhite = isDark ? 0.5 : 0.3;
	return {
		.patternColor = QColor::fromRgbF(
			baseWhite,
			baseWhite,
			baseWhite,
			0.6),
		.useOverlayBlend = false
	};
}

[[nodiscard]] std::vector<AnimatedPatternPoint> GenerateAnimatedPattern(
		const QRect &userpicRect) {
	auto points = std::vector<TopBar::AnimatedPatternPoint>();
	points.reserve(18); // 6 + 6 + 4 + 2.
	const auto ax = float64(userpicRect.x());
	const auto ay = float64(userpicRect.y());
	const auto aw = float64(userpicRect.width());
	const auto ah = float64(userpicRect.height());
	const auto acx = ax + aw / 2.;
	const auto acy = ay + ah / 2.;

	constexpr auto kPaddingScale = 0.8;

	const auto padding24 = style::ConvertFloatScale(24. * kPaddingScale);
	const auto padding16 = style::ConvertFloatScale(16. * kPaddingScale);
	const auto padding8 = style::ConvertFloatScale(8. * kPaddingScale);
	const auto padding12 = style::ConvertFloatScale(12. * kPaddingScale);
	const auto padding48 = style::ConvertFloatScale(48. * kPaddingScale);
	const auto padding96 = style::ConvertFloatScale(96. * kPaddingScale);
	static const auto kCos120 = std::cos(M_PI * 120. / 180.);
	static const auto kCos160 = std::cos(M_PI * 160. / 180.);
	const auto r48Cos120 = (padding48 + aw / 2.) * kCos120;
	const auto r16Cos160 = (padding16 + ah / 2.) * kCos160;

	// First ring.
	points.push_back({ { acx, ay - padding24 }, 20, 0.02, 0.42 });
	points.push_back({ { acx, ay + ah + padding24 }, 20, 0.00, 0.32 });
	points.push_back({ { ax - padding16, acy - ah / 4 - padding8 }, 23, 0.00, 0.40 });
	points.push_back({ { ax + aw + padding16, acy - ah / 4 - padding8 }, 18, 0.00, 0.40 });
	points.push_back({ { ax - padding16, acy + ah / 4 + padding8 }, 24, 0.00, 0.40 });
	points.push_back({ { ax + aw + padding16 - 4, acy + ah / 4 + padding8 }, 24, 0.00, 0.40 });

	// Second ring.
	points.push_back({ { ax - padding48, acy }, 19, 0.14, 0.60 });
	points.push_back({ { ax + aw + padding48, acy }, 19, 0.16, 0.64 });
	points.push_back({ { acx + r48Cos120, ay - padding48 + padding12 }, 17, 0.14, 0.70 });
	points.push_back({ { acx - r48Cos120, ay - padding48 + padding12 }, 17, 0.14, 0.90 });
	points.push_back({ { acx + r48Cos120, ay + ah + padding48 - padding12 }, 20, 0.20, 0.75 });
	points.push_back({ { acx - r48Cos120, ay + ah + padding48 - padding12 }, 20, 0.20, 0.85 });

	// Third ring.
	points.push_back({ { ax - padding48 - padding8, acy + r16Cos160 }, 20, 0.09, 0.45 });
	points.push_back({ { ax + aw + padding48 + padding8, acy + r16Cos160 }, 19, 0.09, 0.45 });
	points.push_back({ { ax - padding48 - padding8, acy - r16Cos160 }, 21, 0.09, 0.45 });
	points.push_back({ { ax + aw + padding48 + padding8, acy - r16Cos160 }, 18, 0.11, 0.45 });

	// Fourth ring.
	points.push_back({ { ax - padding96, acy }, 19, 0.14, 0.75 });
	points.push_back({ { ax + aw + padding96, acy }, 19, 0.20, 0.80 });

	return points;
}

} // namespace

TopBar::TopBar(
	not_null<Ui::RpWidget*> parent,
	Descriptor descriptor)
: RpWidget(parent)
, _peer(descriptor.peer
	? descriptor.peer
	: descriptor.key.peer())
, _topic(descriptor.key.topic())
, _key(descriptor.key)
, _wrap(std::move(descriptor.wrap))
, _st(st::infoTopBar)
, _source(descriptor.source)
, _badgeTooltipHide(
	std::make_unique<base::Timer>([=] { hideBadgeTooltip(); }))
, _botVerify(std::make_unique<Badge>(
	this,
	st::infoBotVerifyBadge,
	&_peer->session(),
	BotVerifyBadgeForPeer(_peer),
	nullptr,
	Fn<bool()>([=, controller = descriptor.controller] {
		return controller->isGifPausedAtLeastFor(
			Window::GifPauseReason::Layer);
	})))
, _badgeContent(BadgeContentForPeer(_peer))
, _gifPausedChecker([=, controller = descriptor.controller] {
	return controller->isGifPausedAtLeastFor(Window::GifPauseReason::Layer);
})
, _badge(std::make_unique<Badge>(
	this,
	st::infoPeerBadge,
	&_peer->session(),
	_badgeContent.value(),
	nullptr,
	_gifPausedChecker))
, _verified(std::make_unique<Badge>(
	this,
	st::infoPeerBadge,
	&_peer->session(),
	VerifiedContentForPeer(_peer),
	nullptr,
	_gifPausedChecker))
, _hasActions(descriptor.source != Source::Stories
	&& descriptor.source != Source::Preview
	&& (_wrap.current() != Wrap::Side || !_peer->isNotificationsUser()))
, _minForProgress([&] {
	QWidget::setMinimumHeight(st::infoLayerTopBarHeight);
	QWidget::setMaximumHeight(_hasActions
		? st::infoProfileTopBarHeightMax
		: st::infoProfileTopBarNoActionsHeightMax);
	return QWidget::minimumHeight()
		+ (!_hasActions
			? 0
			: st::infoProfileTopBarActionButtonsHeight);
}())
, _title(this, nameValue(), _st.title)
, _starsRating(_peer->isUser()
	? std::make_unique<Ui::StarsRating>(
		this,
		descriptor.controller->uiShow(),
		_peer->isSelf() ? QString() : _peer->shortName(),
		Data::StarsRatingValue(_peer),
		(_peer->isSelf()
			? [=] { return _peer->owner().pendingStarsRating(); }
			: Fn<Data::StarsRatingPending()>()))
	: nullptr)
, _status(this, QString(), statusStyle())
, _statusLabel(std::make_unique<StatusLabel>(_status.data(), _peer))
, _showLastSeen(
	this,
	tr::lng_status_lastseen_when(),
	st::infoProfileTopBarShowLastSeen)
, _forumButton([&, controller = descriptor.controller] {
	const auto topic = _key.topic();
	if (!topic) {
		return object_ptr<Ui::RoundButton>{ nullptr };
	}
	auto owned = object_ptr<Ui::RoundButton>(
		this,
		rpl::single(QString()),
		st::infoProfileTopBarTopicStatusButton);
	owned->setText(Info::Profile::NameValue(
		_peer
	) | rpl::map([=](const QString &name) {
		return TextWithEntities(name)
			.append(' ')
			.append(Ui::Text::IconEmoji(&st::textMoreIconEmoji, QString()));
	}));
	owned->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	owned->setClickedCallback([=, peer = _peer] {
		if (const auto forum = peer->forum()) {
			if (peer->useSubsectionTabs()) {
				controller->searchInChat(forum->history());
			} else if (controller->adaptive().isOneColumn()) {
				controller->showForum(forum);
			} else {
				controller->showPeerHistory(peer->id);
			}
		} else {
			controller->showPeerHistory(peer->id);
		}
	});
	return owned;
}())
, _backToggles(std::move(descriptor.backToggles)) {
	_peer->updateFull();
	if (const auto broadcast = _peer->monoforumBroadcast()) {
		broadcast->updateFull();
	}
	const auto controller = descriptor.controller;

	if (_peer->isMegagroup() || _peer->isChat()) {
		_statusLabel->setMembersLinkCallback([=, peer = _peer] {
			const auto topic = _key.topic();
			const auto sublist = _key.sublist();
			const auto shown = sublist
				? sublist->sublistPeer().get()
				: peer.get();
			const auto section = Section::Type::Members;
			controller->showSection(topic
				? std::make_shared<Info::Memento>(topic, section)
				: std::make_shared<Info::Memento>(shown, section));
		});
	}
	if (!_peer->isMegagroup() && !_topic) {
		setupStatusWithRating();
	}

	if (!_topic) {
		setupShowLastSeen(controller);
	}

	_peer->session().changes().peerFlagsValue(
		_peer,
		Data::PeerUpdate::Flag::OnlineStatus | Data::PeerUpdate::Flag::Members
	) | rpl::on_next([=] {
		_statusLabel->refresh();
	}, lifetime());

	_title->setSelectable(true);
	_title->setContextCopyText(tr::lng_profile_copy_fullname(tr::now));

	auto badgeUpdates = rpl::producer<rpl::empty_value>();
	if (_badge) {
		badgeUpdates = rpl::merge(
			std::move(badgeUpdates),
			_badge->updated());

		_badge->setPremiumClickCallback([controller, peer = _peer] {
			::Settings::ShowEmojiStatusPremium(controller, peer);
		});
	}
	if (_verified) {
		badgeUpdates = rpl::merge(
			std::move(badgeUpdates),
			_verified->updated());
	}
	if (_botVerify) {
		badgeUpdates = rpl::merge(
			std::move(badgeUpdates),
			_botVerify->updated());
	}
	_title->naturalWidthValue() | rpl::on_next([=](int w) {
		_title->resizeToWidth(w);
	}, _title->lifetime());
	badgeUpdates = rpl::merge(
		std::move(badgeUpdates),
		nameValue() | rpl::to_empty,
		_backToggles.value() | rpl::to_empty);
	std::move(badgeUpdates) | rpl::on_next([=] {
		updateLabelsPosition();
	}, _title->lifetime());

	setupUniqueBadgeTooltip();
	setupButtons(controller, descriptor.source);
	setupUserpicButton(controller);
	if (_hasActions) {
		_peer->session().changes().peerFlagsValue(
			_peer,
			Data::PeerUpdate::Flag::FullInfo
				| Data::PeerUpdate::Flag::ChannelAmIn
		) | rpl::on_next([=] {
			setupActions(controller);
		}, lifetime());
	}
	setupStoryOutline();
	if (_topic) {
		_topicIconView = std::make_unique<TopicIconView>(
			_topic,
			_gifPausedChecker,
			[=] { update(); });
	} else {
		updateVideoUserpic();
	}

	rpl::merge(
		style::PaletteChanged(),
		_peer->session().data().giftUpdates() | rpl::filter([=](
				const Data::GiftUpdate &update) {
			if (update.action == Data::GiftUpdate::Action::Pin) {
				if (_peer->isSelf() && update.id.isUser()) {
					return true;
				}
				if (_peer == update.id.chat()) {
					return true;
				}
			}
			if (update.action == Data::GiftUpdate::Action::Unpin) {
				for (const auto &gift : _pinnedToTopGifts) {
					if (gift.manageId == update.id) {
						return true;
					}
				}
			}
			return false;
		}) | rpl::to_empty,
		_peer->session().changes().peerFlagsValue(
			_peer,
			Data::PeerUpdate::Flag::EmojiStatus
				| Data::PeerUpdate::Flag::ColorProfile) | rpl::to_empty
	) | rpl::on_next([=] {
		if (_pinnedToTopGiftsFirstTimeShowed) {
			_peer->session().recentSharedGifts().clearLastRequestTime(_peer);
			setupPinnedToTopGifts(controller);
		} else {
			updateCollectibleStatus();
		}
	}, lifetime());

	std::move(
		descriptor.showFinished
	) | rpl::take(1) | rpl::on_next([=] {
		setupPinnedToTopGifts(controller);
	}, lifetime());

	if (_forumButton) {
		_forumButton->show();
	}
}

void TopBar::adjustColors(const std::optional<QColor> &edgeColor) {
	constexpr auto kMinContrast = 5.5;
	const auto shouldOverride = [&](const style::color &color) {
		return edgeColor
			&& (kMinContrast > Ui::CountContrast(color->c, *edgeColor));
	};
	const auto collectible = effectiveCollectible();
	const auto shouldOverrideTitle = shouldOverride(_title->st().textFg);
	const auto shouldOverrideStatus = shouldOverrideTitle
		|| shouldOverride(_status->st().textFg);
	_title->setTextColorOverride(collectible
		? collectible->textColor
		: shouldOverrideTitle
		? std::optional<QColor>(st::groupCallMembersFg->c)
		: std::nullopt);
	if (!_showLastSeen->isHidden()) {
		if (shouldOverrideTitle) {
			const auto st = mapActionStyle(edgeColor);
			_showLastSeen->setBrushOverride(st.bgColor);
			_showLastSeen->setTextFgOverride(st.fgColor);
		} else {
			_showLastSeen->setBrushOverride(std::nullopt);
			_showLastSeen->setTextFgOverride(std::nullopt);
		}
	}
	{
		const auto membersLinkCallback = _statusLabel->membersLinkCallback();
		{
			_statusLabel = nullptr;
			delete _status.release();
		}
		if (shouldOverrideStatus) {
			const auto copySt = [&](const style::FlatLabel &st) {
				auto result = std::make_unique<style::FlatLabel>(
					base::duplicate(st));
				result->palette.linkFg = st::groupCallVideoSubTextFg;
				return result;
			};
			_statusSt = copySt(statusStyle());
			_status.create(this, QString(), *(_statusSt.get()));
		} else {
			_status.create(this, QString(), statusStyle());
		}
		_status->show();
		if (!_peer->isMegagroup() && !_topic) {
			setupStatusWithRating();
		}
		_status->widthValue() | rpl::on_next([=] {
			updateStatusPosition(_progress.current());
		}, _status->lifetime());
		_statusLabel = std::make_unique<StatusLabel>(_status.data(), _peer);
		_statusLabel->setMembersLinkCallback(membersLinkCallback);
		_status->setTextColorOverride(collectible
			? collectible->textColor
			: shouldOverrideStatus
			? std::optional<QColor>(st::groupCallVideoSubTextFg->c)
			: std::nullopt);
		_statusLabel->setColorized(!shouldOverrideStatus);
	}

	const auto shouldOverrideBadges = shouldOverride(
		st::infoBotVerifyBadge.premiumFg);
	_botVerify->setOverrideStyle(shouldOverrideBadges
		? _botVerifySt
		? _botVerifySt.get()
		: &st::infoColoredBotVerifyBadge
		: nullptr);
	_badge->setOverrideStyle(shouldOverrideBadges
		? _badgeSt
		? _badgeSt.get()
		: &st::infoColoredPeerBadge
		: nullptr);
	_verified->setOverrideStyle(shouldOverrideBadges
		? _verifiedSt
		? _verifiedSt.get()
		: &st::infoColoredPeerBadge
		: nullptr);

	if (_starsRating) {
		const auto shouldOverrideRating = shouldOverride(st::windowBgActive);
		_starsRating->setCustomColors(
			shouldOverrideRating
				? edgeColor
				: std::nullopt,
			shouldOverrideRating
				? std::make_optional<QColor>(st::windowFgActive->c)
				: std::nullopt);
	}

	_edgeColor = edgeColor;
}

void TopBar::updateCollectibleStatus() {
	const auto collectible = effectiveCollectible();
	const auto colorProfile = effectiveColorProfile();
	_hasGradientBg = (collectible != nullptr)
		|| (colorProfile && colorProfile->bg.size() > 1);
	_solidBg = (colorProfile && colorProfile->bg.size() == 1)
		? std::make_optional(colorProfile->bg.front())
		: std::nullopt;
	_cachedClipPath = QPainterPath();
	_cachedGradient = QImage();
	_basePatternImage = QImage();
	_lastUserpicRect = QRect();
	const auto patternEmojiId = _localPatternEmojiId
		? *_localPatternEmojiId
		: collectible && collectible->patternDocumentId
		? collectible->patternDocumentId
		: _peer->profileBackgroundEmojiId();
	if (patternEmojiId) {
		const auto document = _peer->owner().document(patternEmojiId);
		if (!_patternEmoji
			|| _patternEmoji->entityData()
				!= Data::SerializeCustomEmojiId(document)) {
			_patternEmoji = document->owner().customEmojiManager().create(
				document,
				[=] { update(); },
				Data::CustomEmojiSizeTag::Normal);
		}
	} else {
		_patternEmoji = nullptr;
	}
	if (collectible || _localPatternEmojiId) {
		setupAnimatedPattern();
	} else {
		_animatedPoints.clear();
		_pinnedToTopGifts.clear();
	}
	const auto verifiedFg = [&]() -> std::optional<QColor> {
		if (collectible) {
			return Ui::BlendColors(
				collectible->edgeColor,
				collectible->centerColor,
				0.2);
		}
		if (colorProfile && !colorProfile->palette.empty()) {
			return Ui::BlendColors(
				colorProfile->palette.back(),
				colorProfile->palette.size() == 1 ? Qt::white : Qt::black,
				0.2);
		}
		return std::nullopt;
	}();
	if (verifiedFg) {
		const auto copyStVerified = [&](const style::InfoPeerBadge &st) {
			auto result = std::make_unique<style::InfoPeerBadge>(
				base::duplicate(st));
			auto fg = std::make_shared<style::owned_color>(*verifiedFg);
			result->premiumFg = fg->color();
			return std::shared_ptr<style::InfoPeerBadge>(
				result.release(),
				[fg](style::InfoPeerBadge *ptr) { delete ptr; });
			return std::shared_ptr<style::InfoPeerBadge>(result.release());
		};
		const auto copySt = [&](const style::InfoPeerBadge &st) {
			auto result = std::make_unique<style::InfoPeerBadge>(
				base::duplicate(st));
			result->premiumFg = st::groupCallVideoSubTextFg;
			return std::shared_ptr<style::InfoPeerBadge>(result.release());
		};
		_botVerifySt = copySt(st::infoColoredBotVerifyBadge);
		_badgeSt = copySt(st::infoColoredPeerBadge);
		_verifiedSt = copyStVerified(st::infoColoredPeerBadge);
	} else {
		_botVerifySt = nullptr;
		_badgeSt = nullptr;
		_verifiedSt = nullptr;
	}
	update();
	adjustColors(collectible
		? std::optional<QColor>(collectible->edgeColor)
		: (colorProfile && !colorProfile->bg.empty())
		? std::optional<QColor>(colorProfile->bg.front())
		: std::nullopt);
}

void TopBar::setupActions(not_null<Window::SessionController*> controller) {
	const auto peer = _peer;
	const auto user = peer->asUser();
	const auto channel = peer->asChannel();
	const auto chat = peer->asChat();
	const auto topic = _key.topic();
	const auto sublist = _key.sublist();
	const auto isSide = (_wrap.current() == Wrap::Side);

	auto buttons = std::vector<not_null<TopBarActionButton*>>();
	_actions = base::make_unique_q<Ui::HorizontalFitContainer>(
		this,
		st::infoProfileTopBarActionButtonsSpace);
	const auto chechMax = [&, max = 3] {
		return buttons.size() >= max;
	};
	const auto addMore = [&] {
		if ([&]() -> bool {
			if (isSide) {
				return false;
			}
			const auto guard = gsl::finally([&] { _peerMenu = nullptr; });
			showTopBarMenu(controller, true);
			return _peerMenu;
		}()) {
			const auto moreButton = Ui::CreateChild<TopBarActionButton>(
				this,
				tr::lng_profile_action_short_more(tr::now),
				st::infoProfileTopBarActionMore);
			moreButton->setClickedCallback([=] {
				showTopBarMenu(controller, false);
			});
			_actionMore = moreButton;
			_actions->add(moreButton);
			buttons.push_back(moreButton);
		}
	};
	const auto guard = gsl::finally([&] {
		addMore();
		style::PaletteChanged(
		) | rpl::on_next([=] {
			const auto current = _edgeColor.current();
			_edgeColor.force_assign(current);
		}, _actions->lifetime());
		_edgeColor.value() | rpl::map([=](std::optional<QColor> c) {
			return mapActionStyle(c);
		}) | rpl::on_next([=](
				TopBarActionButtonStyle st) {
			for (const auto &button : buttons) {
				button->setStyle(st);
			}
		}, _actions->lifetime());
		const auto padding = st::infoProfileTopBarActionButtonsPadding;
		sizeValue() | rpl::on_next([=](const QSize &size) {
			const auto ratio = float64(size.height())
				/ (st::infoProfileTopBarActionButtonsHeight
					+ st::infoLayerTopBarHeight);
			const auto h = st::infoProfileTopBarActionButtonSize;
			const auto resultHeight = (ratio >= 1.)
				? h
				: (ratio <= 0.5)
				? 0
				: int(h * (ratio - 0.5) / 0.5);
			_actions->setGeometry(
				padding.left(),
				size.height() - resultHeight - padding.bottom(),
				size.width() - rect::m::sum::h(padding),
				resultHeight);
		}, _actions->lifetime());
		_actions->show();
		_actions->raise();
	});
	if (user) {
		const auto message = Ui::CreateChild<TopBarActionButton>(
			this,
			tr::lng_profile_action_short_message(tr::now),
			st::infoProfileTopBarActionMessage);
		message->setClickedCallback([=, window = controller] {
			window->showPeerHistory(
				peer->id,
				Window::SectionShow::Way::Forward);
		});
		buttons.push_back(message);
		_actions->add(message);
	}
	if (!topic && channel && !channel->amIn()) {
		const auto join = Ui::CreateChild<TopBarActionButton>(
			this,
			tr::lng_profile_action_short_join(tr::now),
			st::infoProfileTopBarActionJoin);
		join->setClickedCallback([=] {
			channel->session().api().joinChannel(channel);
		});
		buttons.push_back(join);
		_actions->add(join);
	} else if (const auto channel = peer->monoforumBroadcast()) {
		const auto message = Ui::CreateChild<TopBarActionButton>(
			this,
			tr::lng_profile_action_short_channel(tr::now),
			st::infoProfileTopBarActionMessage);
		message->setClickedCallback([=, window = controller] {
			window->showPeerHistory(
				channel,
				Window::SectionShow::Way::Forward);
		});
		buttons.push_back(message);
		_actions->add(message);
	}
	{
		const auto notifications = Ui::CreateChild<TopBarActionButton>(
			this,
			tr::lng_profile_action_short_mute(tr::now),
			st::infoProfileTopBarActionMessage);
		notifications->convertToToggle(
			st::infoProfileTopBarActionUnmute,
			st::infoProfileTopBarActionMute,
			u"profile_muting"_q,
			u"profile_unmuting"_q);

		const auto topicRootId = topic ? topic->rootId() : MsgId();
		const auto makeThread = [=] {
			return topicRootId
				? static_cast<Data::Thread*>(peer->forumTopicFor(topicRootId))
				: peer->owner().history(peer).get();
		};
		(topic
			? NotificationsEnabledValue(topic)
			: NotificationsEnabledValue(peer)
		) | rpl::on_next([=](bool enabled) {
			notifications->toggle(enabled);
			notifications->setText(enabled
				? tr::lng_profile_action_short_mute(tr::now)
				: tr::lng_profile_action_short_unmute(tr::now));
		}, notifications->lifetime());
		notifications->finishAnimating();

		notifications->setAcceptBoth();
		const auto notifySettings = &peer->owner().notifySettings();
			MuteMenu::SetupMuteMenu(
				notifications,
				notifications->clicks(
				) | rpl::filter([=](Qt::MouseButton button) {
					if (button == Qt::RightButton) {
						return true;
					}
					const auto topic = topicRootId
						? peer->forumTopicFor(topicRootId)
						: nullptr;
					Assert(!topicRootId || topic != nullptr);
					const auto is = topic
						? notifySettings->isMuted(topic)
						: notifySettings->isMuted(peer);
					if (is) {
						if (topic) {
							notifySettings->update(topic, { .unmute = true });
						} else {
							notifySettings->update(peer, { .unmute = true });
						}
						return false;
					} else {
						return true;
					}
				}) | rpl::to_empty,
				makeThread,
				controller->uiShow(),
				[=, skip = st::infoProfileTopBarActionMenuSkip] {
					return notifications->mapToGlobal(
						QPoint(0, notifications->height() + skip));
				});
		buttons.push_back(notifications);
		_actions->add(notifications);
		_edgeColor.value() | rpl::on_next([=](
				std::optional<QColor> c) {
			notifications->setLottieColor(c
				? (const style::color*)(nullptr)
				: &st::windowBoldFg);
		}, notifications->lifetime());
	}
	if (chechMax()) {
		return;
	}
	if (!isSide
		&& user
		&& !user->sharedMediaInfo()
		&& !user->isInaccessible()
		&& user->callsStatus() != UserData::CallsStatus::Disabled) {
		const auto call = Ui::CreateChild<TopBarActionButton>(
			this,
			tr::lng_profile_action_short_call(tr::now),
			st::infoProfileTopBarActionCall);
		call->setClickedCallback([=] {
			Core::App().calls().startOutgoingCall(user, false);
		});
		buttons.push_back(call);
		_actions->add(call);
	}
	if (chechMax()) {
		return;
	}
	if (const auto chat = channel ? channel->discussionLink() : nullptr;
			chat && chat->isMegagroup()) {
		const auto discuss = Ui::CreateChild<TopBarActionButton>(
			this,
			tr::lng_profile_action_short_discuss(tr::now),
			st::infoProfileTopBarActionMessage);
		discuss->setClickedCallback([=] {
			if (channel->invitePeekExpires()) {
				controller->showToast(
					tr::lng_channel_invite_private(tr::now));
				return;
			}
			controller->showPeerHistory(
				chat,
				Window::SectionShow::Way::Forward);
		});
		_actions->add(discuss);
		buttons.push_back(discuss);
	}
	if (chechMax()) {
		return;
	}
	if ((topic && topic->canEdit()) || EditPeerInfoBox::Available(peer)) {
		const auto manage = Ui::CreateChild<TopBarActionButton>(
			this,
			tr::lng_profile_action_short_manage(tr::now),
			st::infoProfileTopBarActionManage);
		manage->setClickedCallback([=, window = controller] {
			if (topic) {
				window->show(Box(
					EditForumTopicBox,
					window,
					peer->owner().history(peer),
					topic->rootId()));
			} else {
				window->showEditPeerBox(peer);
			}
		});
		buttons.push_back(manage);
		_actions->add(manage);
	}
	if (chechMax()) {
		return;
	}
	{
		const auto channel = peer->asBroadcast();
		if (!user && !channel) {
		} else if (user
			&& (user->isInaccessible()
				|| user->isSelf()
				|| user->isBot()
				|| user->isServiceUser()
				|| user->isNotificationsUser()
				|| user->isRepliesChat()
				|| user->isVerifyCodes()
				|| !user->session().premiumCanBuy())) {
		} else if (channel
			&& (channel->isForbidden() || !channel->stargiftsAvailable())) {
		} else {
			const auto giftButton = Ui::CreateChild<TopBarActionButton>(
				this,
				tr::lng_profile_action_short_gift(tr::now),
				st::infoProfileTopBarActionGift);
			giftButton->setClickedCallback([=] {
				Ui::ShowStarGiftBox(controller, peer);
			});
			_actions->add(giftButton);
			buttons.push_back(giftButton);
		}
	}
	if (chechMax()) {
		return;
	}
	if (!topic
		&& ((chat && !chat->amCreator())
			|| (channel && !channel->amCreator()))) {
		const auto show = controller->uiShow();
		const auto reportButton = Ui::CreateChild<TopBarActionButton>(
			this,
			tr::lng_profile_action_short_report(tr::now),
			st::infoProfileTopBarActionReport);
		reportButton->setClickedCallback([=] {
			ShowReportMessageBox(show, peer, {}, {});
		});
		_actions->add(reportButton);
		buttons.push_back(reportButton);
	}
	if (chechMax()) {
		return;
	}
	if (!topic && !sublist && channel && channel->amIn()) {
		const auto leaveButton = Ui::CreateChild<TopBarActionButton>(
			this,
			tr::lng_profile_action_short_leave(tr::now),
			st::infoProfileTopBarActionLeave);
		leaveButton->setClickedCallback([=] {
			if (!controller->showFrozenError()) {
				controller->show(Box(DeleteChatBox, peer));
			}
		});
		_actions->add(leaveButton);
		buttons.push_back(leaveButton);
	}
}

void TopBar::setupUserpicButton(
		not_null<Window::SessionController*> controller) {
	_userpicButton = base::make_unique_q<Userpic>(
		this,
		[=] { return _hasStories; });

	const auto openPhoto = [=, peer = _peer] {
		if (const auto id = peer->userpicPhotoId()) {
			if (const auto photo = peer->owner().photo(id); photo->date()) {
				controller->openPhoto(photo, peer);
			}
		}
	};

	_userpicButton->setAcceptBoth(true);

	const auto menu = _userpicButton->lifetime().make_state<
		base::unique_qptr<Ui::PopupMenu>
	>();
	const auto canReport = [=, peer = _peer] {
		if (!peer->hasUserpic()) {
			return false;
		}
		const auto user = peer->asUser();
		if (!user) {
			return false;
		} else if (user->hasPersonalPhoto()
				|| user->isSelf()
				|| user->isInaccessible()
				|| user->isRepliesChat()
				|| user->isVerifyCodes()
				|| (user->botInfo && user->botInfo->canEditInformation)
				|| user->isServiceUser()) {
			return false;
		}
		return true;
	};

	const auto canChangePhoto = [=, peer = _peer] {
		if (_topicIconView) {
			return false;
		}
		if (const auto user = peer->asUser()) {
			return user->isContact()
				&& !user->isSelf()
				&& !user->isInaccessible()
				&& !user->isServiceUser();
		}
		if (const auto chat = peer->asChat()) {
			return chat->canEditInformation();
		}
		if (const auto channel = peer->asChannel()) {
			return channel->canEditInformation();
		}
		return false;
	};

	const auto canSuggestPhoto = [=, peer = _peer] {
		if (const auto user = peer->asUser()) {
			return !user->isSelf()
				&& !user->isBot()
				&& !user->starsPerMessageChecked()
				&& user->owner().history(user)->lastServerMessage();
		}
		return false;
	};

	const auto hasMenu = [=] {
		if (canChangePhoto()) {
			return true;
		}
		if (canSuggestPhoto()) {
			return true;
		}
		if (_hasStories || canReport()) {
			return !!_peer->userpicPhotoId();
		}
		return false;
	};

	const auto invalidate = [=] {
		_userpicUniqueKey = InMemoryKey();
		const auto hasLeftButton = _peer->userpicPhotoId() || _hasStories;
		_userpicButton->setAttribute(
			Qt::WA_TransparentForMouseEvents,
			!hasLeftButton && !hasMenu());
		_userpicButton->setPointerCursor(hasLeftButton);
		updateVideoUserpic();
		_peer->session().downloaderTaskFinished(
		) | rpl::filter([=] {
			return !Ui::PeerUserpicLoading(_userpicView);
		}) | rpl::on_next([=] {
			update();
			_userpicLoadingLifetime.destroy();
		}, _userpicLoadingLifetime);
		Ui::PostponeCall(this, [=] {
			update();
		});
	};

	rpl::single(
		rpl::empty_value()
	) | rpl::then(
		_peer->session().changes().peerFlagsValue(
			_peer,
			Data::PeerUpdate::Flag::Photo
				| Data::PeerUpdate::Flag::FullInfo) | rpl::to_empty
	) | rpl::on_next(invalidate, lifetime());

	if (const auto broadcast = _peer->monoforumBroadcast()) {
		_peer->session().changes().peerFlagsValue(
			broadcast,
			Data::PeerUpdate::Flag::Photo
				| Data::PeerUpdate::Flag::FullInfo
		) | rpl::to_empty | rpl::on_next(invalidate, lifetime());
	}

	using ChosenType = Ui::UserpicButton::ChosenType;

	const auto choosePhotoCallback = [=](ChosenType type) {
		return [=](QImage &&image) {
			auto result = Api::PeerPhoto::UserPhoto{
				std::move(image),
				0,
				std::vector<QColor>(),
			};
			switch (type) {
			case ChosenType::Set:
				_peer->session().api().peerPhoto().upload(
					_peer,
					std::move(result));
				break;
			case ChosenType::Suggest:
				_peer->session().api().peerPhoto().suggest(
					_peer,
					std::move(result));
				break;
			}
		};
	};

	const auto editorData = [=](ChosenType type) {
		const auto user = _peer->asUser();
		const auto name = (user && !user->firstName.isEmpty())
			? user->firstName
			: _peer->name();
		const auto phrase = (type == ChosenType::Suggest)
			? &tr::lng_profile_suggest_sure
			: &tr::lng_profile_set_personal_sure;
		return Editor::EditorData{
			.about = (*phrase)(
				tr::now,
				lt_user,
				tr::bold(name),
				tr::marked),
			.confirm = ((type == ChosenType::Suggest)
				? tr::lng_profile_suggest_button(tr::now)
				: tr::lng_profile_set_photo_button(tr::now)),
			.cropType = Editor::EditorData::CropType::Ellipse,
			.keepAspectRatio = true,
		};
	};

	const auto chooseFile = [=](ChosenType type) {
		base::call_delayed(
			st::defaultRippleAnimation.hideDuration,
			crl::guard(this, [=] {
				Editor::PrepareProfilePhotoFromFile(
					this,
					&controller->window(),
					editorData(type),
					choosePhotoCallback(type));
			}));
	};

	const auto addFromClipboard = [=](
			Ui::PopupMenu *menu,
			ChosenType type,
			tr::phrase<> text) {
		if (const auto data = QGuiApplication::clipboard()->mimeData()) {
			if (data->hasImage()) {
				auto openEditor = crl::guard(this, [=] {
					Editor::PrepareProfilePhoto(
						this,
						&controller->window(),
						editorData(type),
						choosePhotoCallback(type),
						qvariant_cast<QImage>(data->imageData()));
				});
				menu->addAction(
					std::move(text)(tr::now),
					std::move(openEditor),
					&st::menuIconPhoto);
			}
		}
	};

	_userpicButton->clicks() | rpl::on_next([=](
			Qt::MouseButton button) {
		if (button == Qt::RightButton && hasMenu()) {
			*menu = base::make_unique_q<Ui::PopupMenu>(
				this,
				st::popupMenuWithIcons);

			if (_hasStories) {
				(*menu)->addAction(
					tr::lng_profile_open_photo(tr::now),
					openPhoto,
					&st::menuIconPhoto);
			}

			if (canReport()) {
				(*menu)->addAction(
					tr::lng_profile_report(tr::now),
					[=] {
						controller->show(
							ReportProfilePhotoBox(
								_peer,
								_peer->owner().photo(
									_peer->userpicPhotoId())));
					},
					&st::menuIconReport);
			}

			if (canChangePhoto()) {
				if (!(*menu)->empty()) {
					(*menu)->addSeparator(&st::expandedMenuSeparator);
				}
				const auto isUser = _peer->asUser();
				if (isUser) {
					(*menu)->addAction(
						tr::lng_profile_set_photo_for(tr::now),
						[=] { chooseFile(ChosenType::Set); },
						&st::menuIconPhotoSet);
					addFromClipboard(
						menu->get(),
						ChosenType::Set,
						tr::lng_profile_set_photo_for_from_clipboard);
					if (canSuggestPhoto()) {
						(*menu)->addAction(
							tr::lng_profile_suggest_photo(tr::now),
							[=] {
								chooseFile(
									ChosenType::Suggest);
							},
							&st::menuIconPhotoSuggest);
						addFromClipboard(
								menu->get(),
								ChosenType::Suggest,
								tr::lng_profile_suggest_photo_from_clipboard);
					}
				} else {
					const auto channel = _peer->asChannel();
					const auto isChannel = channel && !channel->isMegagroup();
					(*menu)->addAction(
						isChannel
							? tr::lng_profile_set_photo_for_channel(tr::now)
							: tr::lng_profile_set_photo_for_group(tr::now),
						[=] { chooseFile(ChosenType::Set); },
						&st::menuIconPhotoSet);
					addFromClipboard(
						menu->get(),
						ChosenType::Set,
						tr::lng_profile_set_photo_for_from_clipboard);
				}
				if (controller && isUser) {
					const auto done = [=](UserpicBuilder::Result data) {
						auto result = Api::PeerPhoto::UserPhoto{
							base::take(data.image),
							data.id,
							std::move(data.colors),
						};
						_peer->session().api().peerPhoto().upload(
							_peer,
							std::move(result));
					};
					UserpicBuilder::AddEmojiBuilderAction(
						controller,
						menu->get(),
						_peer->session().api().peerPhoto().emojiListValue(
							Api::PeerPhoto::EmojiListType::Profile),
						done,
						false);
				}
			}
			if (!(*menu)->empty()) {
				(*menu)->popup(QCursor::pos());
			}
		} else if (button == Qt::LeftButton) {
			if (_topicIconView && _topic && _topic->iconId()) {
				const auto document = _peer->owner().document(
					_topic->iconId());
				if (const auto sticker = document->sticker()) {
					const auto packName
						= _peer->owner().customEmojiManager().lookupSetName(
							sticker->set.id);
					if (!packName.isEmpty()) {
						const auto text = tr::lng_profile_topic_toast(
							tr::now,
							lt_name,
							tr::link(packName, u"internal:"_q),
							tr::marked);
						const auto weak = base::make_weak(controller);
						controller->showToast(Ui::Toast::Config{
							.text = text,
							.filter = [=, set = sticker->set](
									const ClickHandlerPtr &handler,
									Qt::MouseButton) {
								if (const auto strong = weak.get()) {
									strong->show(
										Box<StickerSetBox>(
											strong->uiShow(),
											set,
											Data::StickersType::Emoji));
								}
								return false;
							},
							.duration = crl::time(3000),
						});
					}
				}
			} else if (_hasStories) {
				controller->openPeerStories(_peer->id);
			} else {
				openPhoto();
			}
		}
	}, _userpicButton->lifetime());
}

void TopBar::setupUniqueBadgeTooltip() {
	if (!_badge || _source == Source::Preview) {
		return;
	}
	base::timer_once(kWaitBeforeGiftBadge) | rpl::then(
		_badge->updated()
	) | rpl::on_next([=] {
		const auto widget = _badge->widget();
		const auto &content = _badgeContent.current();
		const auto &collectible = content.emojiStatusId.collectible;
		const auto premium = (content.badge == BadgeType::Premium);
		const auto id = (collectible && widget && premium)
			? collectible->id
			: uint64();
		if (_badgeCollectibleId == id) {
			return;
		}
		hideBadgeTooltip();
		if (!collectible || _localCollectible) {
			return;
		}
		const auto parent = window();
		_badgeTooltip = std::make_unique<BadgeTooltip>(
			parent,
			collectible,
			widget);
		const auto raw = _badgeTooltip.get();
		raw->fade(true);
		_badgeTooltipHide->callOnce(kGiftBadgeGlares * raw->glarePeriod()
			- st::infoGiftTooltip.duration * 1.5);
		raw->setOpacity(_progress.current());
	}, lifetime());

	if (const auto raw = _badgeTooltip.get()) {
		raw->finishAnimating();
	}
}

void TopBar::hideBadgeTooltip() {
	_badgeTooltipHide->cancel();
	if (auto old = base::take(_badgeTooltip)) {
		const auto raw = old.get();
		_badgeOldTooltips.push_back(std::move(old));

		raw->fade(false);
		raw->shownValue(
		) | rpl::filter(
			!rpl::mappers::_1
		) | rpl::on_next([=] {
			const auto i = ranges::find(
				_badgeOldTooltips,
				raw,
				&std::unique_ptr<BadgeTooltip>::get);
			if (i != end(_badgeOldTooltips)) {
				_badgeOldTooltips.erase(i);
			}
		}, raw->lifetime());
	}
}

TopBar::~TopBar() {
	base::take(_badgeTooltip);
	base::take(_badgeOldTooltips);
}

rpl::producer<> TopBar::backRequest() const {
	return _backClicks.events();
}

void TopBar::setOnlineCount(rpl::producer<int> &&count) {
	std::move(count) | rpl::on_next([=](int v) {
		if (_statusLabel) {
			_statusLabel->setOnlineCount(v);
		}
	}, lifetime());
}

void TopBar::setRoundEdges(bool value) {
	_roundEdges = value;
	update();
}

void TopBar::setLottieSingleLoop(bool value) {
	_lottieSingleLoop = value;
}

void TopBar::setColorProfileIndex(std::optional<uint8> index) {
	_localColorProfileIndex = index;
	updateCollectibleStatus();
}

void TopBar::setPatternEmojiId(std::optional<DocumentId> patternEmojiId) {
	_localPatternEmojiId = patternEmojiId;
	updateCollectibleStatus();
}

void TopBar::setLocalEmojiStatusId(EmojiStatusId emojiStatusId) {
	_localCollectible = emojiStatusId.collectible;
	if (!emojiStatusId.collectible) {
		_badgeContent = Badge::Content{ BadgeType::Premium, emojiStatusId };
	} else {
		_badgeContent = BadgeContentForPeer(_peer);
	}
	updateCollectibleStatus();
}

std::optional<Data::ColorProfileSet> TopBar::effectiveColorProfile() const {
	return _localColorProfileIndex
		? _peer->session().api().peerColors().colorProfileFor(
			*_localColorProfileIndex)
		: _source == Source::Preview
		? std::nullopt
		: _peer->session().api().peerColors().colorProfileFor(_peer);
}

auto TopBar::effectiveCollectible() const
-> std::shared_ptr<Data::EmojiStatusCollectible> {
	return _localCollectible
		? _localCollectible
		: _localColorProfileIndex
		? nullptr
		: _peer->emojiStatusId().collectible;
}

void TopBar::paintEdges(QPainter &p, const QBrush &brush) const {
	const auto r = rect();
	if (_roundEdges) {
		auto hq = PainterHighQualityEnabler(p);
		const auto radius = st::boxRadius;
		p.setPen(Qt::NoPen);
		p.setBrush(brush);
		p.drawRoundedRect(
			r + QMargins{ 0, 0, 0, radius + 1 },
			radius,
			radius);
	} else {
		p.fillRect(r, brush);
	}
}

void TopBar::paintEdges(QPainter &p) const {
	if (!_solidBg) {
		paintEdges(p, st::boxDividerBg);
	} else {
		paintEdges(p, *_solidBg);
	}
}

int TopBar::titleMostLeft() const {
	return (_back && _back->toggled())
		? _back->width()
		: _st.titleWithSubtitlePosition.x();
}

int TopBar::statusMostLeft() const {
	return (_back && _back->toggled())
		? _back->width()
		: _st.subtitlePosition.x();
}

int TopBar::calculateRightButtonsWidth() const {
	auto width = 0;
	if (_close) {
		width += _close->width();
	}
	if (_topBarButton) {
		width += _topBarButton->width();
	}
	return width;
}

void TopBar::updateLabelsPosition() {
	if (width() <= 0) {
		return;
	}
	_progress = [&] {
		const auto max = QWidget::maximumHeight();
		const auto min = _minForProgress;
		const auto p = (max > min)
			? ((height() - min) / float64(max - min))
			: 1.;
		return std::clamp(p, 0., 1.);
	}();
	const auto progressCurrent = _progress.current();

	const auto rightButtonsWidth = calculateRightButtonsWidth();

	const auto reservedRight = anim::interpolate(
		0,
		rightButtonsWidth,
		1. - progressCurrent);
	const auto titleMostLeft = TopBar::titleMostLeft();
	const auto interpolatedPadding = anim::interpolate(
		titleMostLeft,
		rect::m::sum::h(st::boxRowPadding),
		progressCurrent);
	const auto verifiedWidget = _verified ? _verified->widget() : nullptr;
	const auto badgeWidget = _badge ? _badge->widget() : nullptr;
	const auto botVerifyWidget = _botVerify ? _botVerify->widget() : nullptr;
	auto badgesWidth = 0;
	if (verifiedWidget) {
		badgesWidth += verifiedWidget->width();
	}
	if (badgeWidget) {
		badgesWidth += badgeWidget->width();
	}
	if (botVerifyWidget) {
		badgesWidth += botVerifyWidget->width();
	}
	if (verifiedWidget || badgeWidget) {
		badgesWidth += st::infoVerifiedCheckPosition.x();
	}
	const auto titleWidth = width()
		- interpolatedPadding
		- reservedRight
		- badgesWidth;

	if (titleWidth > 0 && _title->textMaxWidth() > titleWidth) {
		_title->resizeToWidth(titleWidth);
	}

	const auto titleTop = anim::interpolate(
		_st.titleWithSubtitlePosition.y(),
		st::infoProfileTopBarTitleTop,
		progressCurrent);

	const auto badgeTop = titleTop;
	const auto badgeBottom = titleTop + _title->height();
	const auto margins = LargeCustomEmojiMargins();

	auto totalElementsWidth = _title->width();
	const auto botVerifySkip = botVerifyWidget
		? botVerifyWidget->width() + st::infoVerifiedCheckPosition.x()
		: 0;
	if (verifiedWidget) {
		totalElementsWidth += verifiedWidget->width();
	}
	if (badgeWidget) {
		totalElementsWidth += badgeWidget->width();
	}
	if (verifiedWidget || badgeWidget) {
		totalElementsWidth += st::infoVerifiedCheckPosition.x();
	}
	totalElementsWidth += botVerifySkip;

	auto titleLeft = anim::interpolate(
		titleMostLeft,
		(width() - totalElementsWidth) / 2,
		progressCurrent);

	if (_botVerify) {
		_botVerify->move(
			titleLeft,
			badgeTop,
			badgeBottom);
		titleLeft += margins.left() + botVerifySkip;
	}

	_title->moveToLeft(titleLeft, titleTop);
	const auto badgeLeft = titleLeft + _title->width();
	if (_badge) {
		_badge->move(badgeLeft, badgeTop, badgeBottom);
	}
	if (_verified) {
		_verified->move(
			badgeLeft + (badgeWidget ? badgeWidget->width() : 0),
			badgeTop,
			badgeBottom);
	}

	updateStatusPosition(progressCurrent);

	if (_badgeTooltip) {
		_badgeTooltip->setOpacity(progressCurrent);
	}

	{
		const auto userpicRect = userpicGeometry();
		if (_userpicButton) {
			_userpicButton->setGeometry(userpicGeometry());
		}

		updateGiftButtonsGeometry(progressCurrent, userpicRect);
	}
}

void TopBar::updateStatusPosition(float64 progressCurrent) {
	if (width() <= 0) {
		return;
	}
	if (_forumButton) {
		const auto buttonTop = anim::interpolate(
			_st.subtitlePosition.y(),
			st::infoProfileTopBarStatusTop,
			progressCurrent);
		const auto mostLeft = statusMostLeft();
		const auto buttonMostLeft = anim::interpolate(
			mostLeft,
			st::infoProfileTopBarActionButtonsPadding.left(),
			progressCurrent);
		const auto buttonMostRight = anim::interpolate(
			calculateRightButtonsWidth(),
			st::infoProfileTopBarActionButtonsPadding.right(),
			progressCurrent);
		const auto maxWidth = width() - buttonMostLeft - buttonMostRight;
		if (_forumButton->contentWidth() > maxWidth) {
			_forumButton->setFullWidth(maxWidth);
		}
		const auto buttonLeft = anim::interpolate(
			mostLeft,
			(width() - _forumButton->width()) / 2,
			progressCurrent);
		_forumButton->moveToLeft(buttonLeft, buttonTop);
		_forumButton->setVisible(true);

		_status->hide();
		// _starsRating->hide();
		_showLastSeen->hide();
		return;
	}

	const auto statusTop = anim::interpolate(
		_st.subtitlePosition.y(),
		st::infoProfileTopBarStatusTop,
		progressCurrent);
	const auto totalElementsWidth = _status->width()
		+ (_starsRating ? _starsRating->width() : 0)
		+ (!_showLastSeen->isHidden() ? _showLastSeen->width() : 0);
	const auto statusLeft = anim::interpolate(
		statusMostLeft(),
		(width() - totalElementsWidth) / 2,
		progressCurrent);

	if (const auto rating = _starsRating.get()) {
		rating->moveTo(statusLeft, statusTop - st::lineWidth);
		rating->setOpacity(progressCurrent);
	}
	const auto statusShift = _statusShift.current()
		* std::clamp((progressCurrent) / 0.15, 0., 1.);

	_status->moveToLeft(statusLeft + statusShift, statusTop);

	if (!_showLastSeen->isHidden()) {
		_showLastSeen->moveToLeft(
			statusLeft
				+ statusShift
				+ _status->textMaxWidth()
				+ st::infoProfileTopBarLastSeenSkip.x(),
			statusTop + st::infoProfileTopBarLastSeenSkip.y());
		if (_showLastSeenOpacity) {
			_showLastSeenOpacity->setOpacity(progressCurrent);
		}
		_showLastSeen->setAttribute(
			Qt::WA_TransparentForMouseEvents,
			!progressCurrent);
	}
}

void TopBar::resizeEvent(QResizeEvent *e) {
	_cachedClipPath = QPainterPath();
	const auto collectible = effectiveCollectible();
	if (collectible && !_animatedPoints.empty()) {
		setupAnimatedPattern();
	}
	if (_hasGradientBg && e->oldSize().width() != e->size().width()) {
		_cachedClipPath = QPainterPath();
		_cachedGradient = QImage();
	}
	updateLabelsPosition();
	RpWidget::resizeEvent(e);
}

QRect TopBar::userpicGeometry() const {
	constexpr auto kMinScale = 0.25;
	const auto progressCurrent = _progress.current();
	const auto fullSize = st::infoProfileTopBarPhotoSize;
	const auto minSize = fullSize * kMinScale;
	const auto size = anim::interpolate(minSize, fullSize, progressCurrent);
	const auto x = (width() - size) / 2;
	const auto minY = -minSize;
	const auto maxY = st::infoProfileTopBarPhotoTop;
	const auto y = anim::interpolate(minY, maxY, progressCurrent);
	return QRect(x, y, size, size);
}

void TopBar::updateGiftButtonsGeometry(
		float64 progressCurrent,
		const QRect &userpicRect) {
	if (width() <= 0) {
		return;
	}
	const auto sz = st::infoProfileTopBarGiftSize;
	const auto halfSz = sz / 2.;
	for (const auto &gift : _pinnedToTopGifts) {
		if (gift.button) {
			const auto giftPos = calculateGiftPosition(
				gift.position,
				progressCurrent,
				userpicRect);
			const auto buttonRect = QRect(
				QPoint(giftPos.x() - halfSz, giftPos.y() - halfSz),
				Size(sz));
			gift.button->setGeometry(buttonRect);
		}
	}
}

void TopBar::paintUserpic(QPainter &p, const QRect &geometry) {
	if (_topicIconView) {
		_topicIconView->paintInRect(p, geometry);
		return;
	}
	if (_videoUserpicPlayer && _videoUserpicPlayer->ready()) {
		const auto size = st::infoProfileTopBarPhotoSize;
		const auto frame = _videoUserpicPlayer->frame(Size(size), _peer);
		if (!frame.isNull()) {
			p.drawImage(geometry, frame);
			update();
			return;
		}
	}
	const auto key = _peer->userpicUniqueKey(_userpicView);
	if (_userpicUniqueKey != key) {
		_userpicUniqueKey = key;
		const auto fullSize = st::infoProfileTopBarPhotoSize;
		const auto scaled = fullSize * style::DevicePixelRatio();
		auto image = QImage();
		if (const auto broadcast = _peer->monoforumBroadcast()) {
			image = PeerData::GenerateUserpicImage(
				broadcast,
				_userpicView,
				scaled,
				0);
			if (_monoforumMask.isNull()) {
				_monoforumMask = Ui::MonoforumShapeMask(Size(scaled));
			}
			constexpr auto kFormat = QImage::Format_ARGB32_Premultiplied;
			if (image.format() != kFormat) {
				image = std::move(image).convertToFormat(kFormat);
			}
			auto q = QPainter(&image);
			q.setCompositionMode(QPainter::CompositionMode_DestinationIn);
			q.drawImage(
				Rect(image.size() / image.devicePixelRatio()),
				_monoforumMask);
			q.end();
		} else {
			image = PeerData::GenerateUserpicImage(
				_peer,
				_userpicView,
				scaled,
				std::nullopt);
		}
		_cachedUserpic = std::move(image);
		_cachedUserpic.setDevicePixelRatio(style::DevicePixelRatio());
	}
	p.drawImage(geometry, _cachedUserpic);
}

void TopBar::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto geometry = userpicGeometry();

	if (_hasGradientBg && _cachedGradient.isNull()) {
		const auto collectible = effectiveCollectible();
		const auto colorProfile = effectiveColorProfile();
		const auto offset = QPoint(
			0,
			_hasActions
				? -st::infoProfileTopBarPhotoBgShift
				: -st::infoProfileTopBarPhotoBgNoActionsShift);
		if (collectible) {
			_cachedGradient = Ui::CreateTopBgGradient(
				QSize(width(), maximumHeight()),
				collectible->centerColor,
				collectible->edgeColor,
				false,
				offset);
		} else if (colorProfile && colorProfile->bg.size() > 1) {
			_cachedGradient = Ui::CreateTopBgGradient(
				QSize(width(), maximumHeight()),
				colorProfile->bg[1],
				colorProfile->bg[0],
				false,
				offset);
		}
	}
	if (!_hasGradientBg) {
		paintEdges(p);
	} else {
		const auto x = (width()
			- _cachedGradient.width() / style::DevicePixelRatio())
				/ 2;
		const auto y = (height()
			- _cachedGradient.height() / style::DevicePixelRatio())
				/ 2;
		if (_roundEdges) {
			if (_cachedClipPath.isEmpty()) {
				const auto radius = st::boxRadius;
				_cachedClipPath.addRoundedRect(
					rect() + QMargins{ 0, 0, 0, radius + 1 },
					radius,
					radius);
			}
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setClipPath(_cachedClipPath);
			p.drawImage(x, y, _cachedGradient);
		} else {
			p.drawImage(x, y, _cachedGradient);
		}
	}
	if (_patternEmoji && _patternEmoji->ready()) {
		paintAnimatedPattern(p, rect(), geometry);
	}

	const auto clipBounds = e->region().boundingRect();
	if (clipBounds.bottom() >= geometry.top()
		&& clipBounds.top() <= geometry.bottom()) {
		paintPinnedToTopGifts(p, rect(), geometry);
	}

	if (clipBounds.intersects(geometry)) {
		paintUserpic(p, geometry);
		paintStoryOutline(p, geometry);
	}
}

void TopBar::setupButtons(
		not_null<Window::SessionController*> controller,
		Source source) {
	if (source == Source::Preview) {
		setRoundEdges(false);
		return;
	}
	rpl::combine(
		_wrap.value(),
		_edgeColor.value()
	) | rpl::on_next([=](
			Wrap wrap,
			std::optional<QColor> edgeColor) mutable {
		const auto isLayer = (wrap == Wrap::Layer);
		const auto isSide = (wrap == Wrap::Side);
		setRoundEdges(isLayer);
		setLottieSingleLoop(wrap == Wrap::Side);

		const auto shouldUseColored = edgeColor
			&& (kMinContrast > Ui::CountContrast(
				st::boxTitleCloseFg->c,
				*edgeColor));
		_back = base::make_unique_q<Ui::FadeWrap<Ui::IconButton>>(
			this,
			object_ptr<Ui::IconButton>(
				this,
				(isLayer
					? (shouldUseColored
						? st::infoTopBarColoredBack
						: st::infoTopBarBlackBack)
					: (shouldUseColored
						? st::infoLayerTopBarColoredBack
						: st::infoLayerTopBarBlackBack))),
			st::infoTopBarScale);
		_back->QWidget::show();
		_back->setDuration(0);
		_back->toggleOn(isLayer || isSide
			? (_backToggles.value() | rpl::type_erased)
			: rpl::single(wrap == Wrap::Narrow));
		_back->entity()->clicks() | rpl::to_empty | rpl::start_to_stream(
			_backClicks,
			_back->lifetime());

		if (!isLayer && !isSide) {
			_close = nullptr;
		} else {
			_close = base::make_unique_q<Ui::IconButton>(
				this,
				shouldUseColored
					? st::infoTopBarColoredClose
					: st::infoTopBarBlackClose);
			_close->show();
			_close->addClickHandler(isSide
				? Fn<void()> ([=] { controller->closeThirdSection(); })
				: Fn<void()> ([=] {
					controller->hideLayer();
					controller->hideSpecialLayer();
				}));
			widthValue() | rpl::on_next([=] {
				_close->moveToRight(0, 0);
			}, _close->lifetime());
		}

		if (wrap != Wrap::Side) {
			if (source == Source::Stories) {
				addTopBarEditButton(controller, wrap, shouldUseColored);
			}
		}
	}, lifetime());
}

void TopBar::addTopBarEditButton(
		not_null<Window::SessionController*> controller,
		Wrap wrap,
		bool shouldUseColored) {
	_topBarButton = base::make_unique_q<Ui::IconButton>(
		this,
		((wrap == Wrap::Layer)
			? (shouldUseColored
				? st::infoLayerTopBarColoredEdit
				: st::infoLayerTopBarBlackEdit)
			: (shouldUseColored
				? st::infoTopBarColoredEdit
				: st::infoTopBarBlackEdit)));
	_topBarButton->show();
	_topBarButton->addClickHandler([=] {
		controller->showSettings(::Settings::Information::Id());
	});

	widthValue() | rpl::on_next([=] {
		if (_close) {
			_topBarButton->moveToRight(_close->width(), 0);
		} else {
			_topBarButton->moveToRight(0, 0);
		}
	}, _topBarButton->lifetime());
}

void TopBar::showTopBarMenu(
		not_null<Window::SessionController*> controller,
		bool check) {
	if (_peerMenu) {
		_peerMenu->hideMenu(true);
		return;
	}
	_peerMenu = base::make_unique_q<Ui::PopupMenu>(
		QWidget::window(),
		st::popupMenuExpandedSeparator);

	_peerMenu->setDestroyedCallback([this] {
		InvokeQueued(this, [this] { _peerMenu = nullptr; });
		// if (auto toggle = _topBarMenuToggle.get()) {
		// 	toggle->setForceRippled(false);
		// }
	});

	fillTopBarMenu(
		controller,
		Ui::Menu::CreateAddActionCallback(_peerMenu));
	if (_peerMenu->empty()) {
		_peerMenu = nullptr;
		return;
	} else if (check) {
		return;
	}
	_peerMenu->setForcedOrigin(Ui::PanelAnimation::Origin::TopRight);
	_peerMenu->popup(_actionMore
		? _actionMore->mapToGlobal(
			QPoint(
				_actionMore->width(),
				_actionMore->height() + st::infoProfileTopBarActionMenuSkip))
		: QCursor::pos());
}

void TopBar::fillTopBarMenu(
		not_null<Window::SessionController*> controller,
		const Ui::Menu::MenuCallback &addAction) {
	const auto peer = _peer;
	const auto topic = _key.topic();
	const auto sublist = _key.sublist();
	if (!peer && !topic) {
		return;
	}

	Window::FillDialogsEntryMenu(
		controller,
		Dialogs::EntryState{
			.key = (topic
				? Dialogs::Key{ topic }
				: sublist
				? Dialogs::Key{ sublist }
				: Dialogs::Key{ peer->owner().history(peer) }),
			.section = Dialogs::EntryState::Section::Profile,
		},
		addAction);
}

void TopBar::updateVideoUserpic() {
	if (width() <= 0) {
		return;
	}
	const auto id = _peer->userpicPhotoId();
	if (!id) {
		_videoUserpicPlayer = nullptr;
		return;
	}
	const auto photo = _peer->owner().photo(id);
	if (!photo->date() || !photo->videoCanBePlayed()) {
		_videoUserpicPlayer = nullptr;
		return;
	}
	if (!_videoUserpicPlayer) {
		_videoUserpicPlayer = std::make_unique<Ui::VideoUserpicPlayer>();
	}
	_videoUserpicPlayer->setup(_peer, photo);
}

void TopBar::setupShowLastSeen(
		not_null<Window::SessionController*> controller) {
	const auto user = _peer->asUser();
	if (!user
		|| user->isSelf()
		|| user->isBot()
		|| user->isServiceUser()
		|| !user->session().premiumPossible()) {
		_showLastSeen->hide();
		return;
	}

	if (user->session().premium()) {
		if (user->lastseen().isHiddenByMe()) {
			user->updateFullForced();
		}
		_showLastSeen->hide();
		return;
	}

	rpl::combine(
		user->session().changes().peerFlagsValue(
			user,
			Data::PeerUpdate::Flag::OnlineStatus),
		Data::AmPremiumValue(&user->session())
	) | rpl::on_next([=](auto, bool premium) {
		const auto wasShown = !_showLastSeen->isHidden();
		const auto hiddenByMe = user->lastseen().isHiddenByMe();
		const auto shown = hiddenByMe
			&& !user->lastseen().isOnline(base::unixtime::now())
			&& !premium
			&& user->session().premiumPossible();
		_showLastSeen->setVisible(shown);
		if (wasShown && premium && hiddenByMe) {
			user->updateFullForced();
		}
	}, _showLastSeen->lifetime());

	controller->session().api().userPrivacy().value(
		Api::UserPrivacy::Key::LastSeen
	) | rpl::filter([=](Api::UserPrivacy::Rule rule) {
		return (rule.option == Api::UserPrivacy::Option::Everyone);
	}) | rpl::on_next([=] {
		if (user->lastseen().isHiddenByMe()) {
			user->updateFullForced();
		}
	}, _showLastSeen->lifetime());

	_showLastSeenOpacity = Ui::CreateChild<QGraphicsOpacityEffect>(
		_showLastSeen.get());
	_showLastSeen->setGraphicsEffect(_showLastSeenOpacity);
	_showLastSeenOpacity->setOpacity(0.);

	using TextTransform = Ui::RoundButton::TextTransform;
	_showLastSeen->setTextTransform(TextTransform::NoTransform);
	_showLastSeen->setFullRadius(true);

	_showLastSeen->setClickedCallback([=] {
		const auto type = Ui::ShowOrPremium::LastSeen;
		controller->show(Box(
			Ui::ShowOrPremiumBox,
			type,
			user->shortName(),
			[=] {
				controller->session().api().userPrivacy().save(
					::Api::UserPrivacy::Key::LastSeen,
					{});
			},
			[=] {
				::Settings::ShowPremium(controller, u"lastseen_hidden"_q);
			}));
	});
}

void TopBar::setupAnimatedPattern(const QRect &userpicGeometry) {
	_animatedPoints = GenerateAnimatedPattern(userpicGeometry.isNull()
		? TopBar::userpicGeometry()
		: userpicGeometry);
}

void TopBar::paintAnimatedPattern(
		QPainter &p,
		const QRect &rect,
		const QRect &userpicGeometry) {
	if (!_patternEmoji || !_patternEmoji->ready()) {
		return;
	}

	{
		// TODO make it better.
		if (_lastUserpicRect != userpicGeometry) {
			_lastUserpicRect = userpicGeometry;
			setupAnimatedPattern(userpicGeometry);
		}
	}

	if (_basePatternImage.isNull()) {
		auto patternColors = CalculatePatternColors(
			effectiveColorProfile(),
			effectiveCollectible(),
			_edgeColor.current(),
			Window::Theme::IsNightMode());
		const auto ratio = style::DevicePixelRatio();
		const auto scale = 0.910;
		const auto size = st::emojiSize;
		_basePatternImage = QImage(
			QSize(size, size) * ratio,
			QImage::Format_ARGB32_Premultiplied);
		_basePatternImage.setDevicePixelRatio(ratio);
		_basePatternImage.fill(Qt::transparent);
		auto painter = QPainter(&_basePatternImage);
		auto hq = PainterHighQualityEnabler(painter);
		// const auto contentSize = size * scale;
		// const auto offset = (size - contentSize) / 2.;
		// painter.translate(offset, offset);
		painter.scale(scale, scale);
		_patternEmoji->paint(painter, { .textColor = Qt::white });
		painter.resetTransform();

		if (patternColors.useOverlayBlend) {
			painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
			painter.fillRect(
				Rect(Size(size)),
				QColor(0, 0, 0, int(0.8 * 255)));
		} else {
			painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
			painter.fillRect(Rect(Size(size)), patternColors.patternColor);
		}
	}

	const auto progress = _progress.current();
	// const auto collapseDiff = progress >= 0.85 ? 1. : (progress / 0.85);
	// const auto collapse = std::clamp((collapseDiff - 0.2) / 0.8, 0., 1.);
	const auto collapse = progress;

	const auto userpicCenter = rect::center(userpicGeometry);
	const auto yOffset = 12 * (1. - progress);
	const auto imageSize = _basePatternImage.size()
		/ style::DevicePixelRatio();
	const auto halfImageWidth = imageSize.width() * 0.5;
	const auto halfImageHeight = imageSize.height() * 0.5;

	for (const auto &point : _animatedPoints) {
		const auto timeRange = point.endTime - point.startTime;
		const auto collapseProgress = (1. - collapse <= point.startTime)
			? 1.
			: 1.
				- std::clamp(
					(1. - collapse - point.startTime) / timeRange,
					0.,
					1.);

		if (collapseProgress <= 0.) {
			continue;
		}

		auto x = point.basePosition.x();
		auto y = point.basePosition.y() - yOffset;
		auto r = point.size * 0.5;

		if (collapseProgress < 1.) {
			const auto dx = x - userpicCenter.x();
			const auto dy = y - userpicCenter.y();
			x = userpicCenter.x() + dx * collapseProgress;
			y = userpicCenter.y() + dy * collapseProgress;
			r = kMinPatternRadius
				+ (r - kMinPatternRadius) * collapseProgress;
		}

		const auto scale = r / (point.size * 0.5);
		const auto scaledHalfWidth = halfImageWidth * scale;
		const auto scaledHalfHeight = halfImageHeight * scale;

		// Distance-based alpha calculation.
		const auto userpicRect = _lastUserpicRect;
		const auto acx = userpicRect.x() + userpicRect.width() / 2.;
		const auto acy = userpicRect.y() + userpicRect.height() / 2.;
		const auto aw = userpicRect.width();
		const auto ah = userpicRect.height();
		const auto distance = std::sqrt((x - acx) * (x - acx)
			+ (y - acy) * (y - acy));
		const auto normalizedDistance = std::clamp(
			distance / (aw * 2.),
			0.,
			1.);
		const auto distanceAlpha = 1. - normalizedDistance;

		// Bottom alpha calculation.
		const auto bottomThreshold = userpicRect.y() + ah + kMinPatternRadius;
		const auto bottomAlpha = (y > bottomThreshold)
			? 1. - std::clamp((y - bottomThreshold) / 56., 0., 1.)
			: 1.;

		auto alpha = progress * distanceAlpha * 0.5 * bottomAlpha;
		if (collapseProgress < 1.) {
			alpha = alpha * collapseProgress;
		}

		p.setOpacity(alpha);
		p.drawImage(
			QRectF(
				x - scaledHalfWidth,
				y - scaledHalfHeight,
				scaledHalfWidth * 2,
				scaledHalfHeight * 2),
			_basePatternImage);
	}
	p.setOpacity(1.);
}

void TopBar::setupPinnedToTopGifts(
		not_null<Window::SessionController*> controller) {
	const auto requestDone = crl::guard(this, [=](
			std::vector<Data::SavedStarGift> gifts) {
		const auto shouldHideFirst = _pinnedToTopGiftsFirstTimeShowed
			&& !_pinnedToTopGifts.empty();

		if (shouldHideFirst) {
			_giftsHiding = std::make_unique<Ui::Animations::Simple>();
			_giftsHiding->start([=](float64 value) {
				update();
				if (value <= 0.) {
					_giftsHiding = nullptr;
					_pinnedToTopGifts.clear();
					_giftsLoadingLifetime.destroy();
					updateCollectibleStatus();
					setupNewGifts(controller, gifts);
				}
			}, 1., 0., 300, anim::linear);
			return;
		}

		_pinnedToTopGifts.clear();
		_giftsLoadingLifetime.destroy();

		updateCollectibleStatus();
		setupNewGifts(controller, gifts);
	});
	_peer->session().recentSharedGifts().request(_peer, requestDone, true);
}

void TopBar::setupNewGifts(
		not_null<Window::SessionController*> controller,
		const std::vector<Data::SavedStarGift> &gifts) {
	const auto emojiStatusId = _peer->emojiStatusId().collectible
		? _peer->emojiStatusId().collectible->id
		: CollectibleId(0);
	auto filteredGifts = std::vector<Data::SavedStarGift>();
	const auto subtract = emojiStatusId ? 1 : 0;
	filteredGifts.reserve((gifts.size() > subtract)
		? (gifts.size() - subtract)
		: 0);
	for (const auto &gift : gifts) {
		if (const auto &unique = gift.info.unique) {
			if (unique->id != emojiStatusId) {
				filteredGifts.push_back(gift);
			}
		}
	}

	_pinnedToTopGifts.reserve(filteredGifts.size());
	if (filteredGifts.empty()) {
		_giftsAppearing = nullptr;
		_lottiePlayer = nullptr;
		_pinnedToTopGiftsFirstTimeShowed = true;
	} else if (!_lottiePlayer) {
		_lottiePlayer = std::make_unique<Lottie::MultiPlayer>(
			Lottie::Quality::Default);
		_lottiePlayer->updates() | rpl::on_next([=] {
			update();
		}, lifetime());
	}

	_giftsAppearing = std::make_unique<Ui::Animations::Simple>();

	constexpr auto kMaxPinnedToTopGifts = 6;

	auto positions = ranges::views::iota(
		0,
		kMaxPinnedToTopGifts) | ranges::to_vector;
	ranges::shuffle(positions);

	for (auto i = 0;
		i < filteredGifts.size() && i < kMaxPinnedToTopGifts;
		++i) {
		const auto &gift = filteredGifts[i];
		const auto document = _peer->owner().document(
			gift.info.document->id);
		auto entry = PinnedToTopGiftEntry();
		entry.manageId = gift.manageId;
		entry.media = document->createMediaView();
		entry.media->checkStickerSmall();
		if (const auto &unique = gift.info.unique) {
			if (unique->backdrop.centerColor.isValid()
				&& unique->backdrop.edgeColor.isValid()) {
				entry.bg = Ui::CreateTopBgGradient(
					Size(st::infoProfileTopBarGiftSize * 2),
					unique->backdrop.centerColor,
					anim::with_alpha(unique->backdrop.edgeColor, 0.0),
					false);
			}
		}
		entry.position = positions[i];
		entry.button = base::make_unique_q<Ui::AbstractButton>(this);
		entry.button->show();

		entry.button->setClickedCallback([=, giftData = gift, peer = _peer] {
			::Settings::ShowSavedStarGiftBox(controller, peer, giftData);
		});

		_pinnedToTopGifts.push_back(std::move(entry));
	}
	updateGiftButtonsGeometry(_progress.current(), userpicGeometry());

	using namespace ChatHelpers;

	rpl::single(
		rpl::empty_value()
	) | rpl::then(
		_peer->session().downloaderTaskFinished()
	) | rpl::on_next([=] {
		auto allLoaded = true;
		for (auto &entry : _pinnedToTopGifts) {
			if (!entry.animation && !entry.lastFrame.isNull()) {
				continue;
			}
			if (!entry.animation && entry.media->loaded()) {
				entry.animation = LottieAnimationFromDocument(
					_lottiePlayer.get(),
					entry.media.get(),
					StickerLottieSize::PinnedProfileUniqueGiftSize,
					Size(st::infoProfileTopBarGiftSize)
						* style::DevicePixelRatio());
			} else if (!entry.media->loaded()) {
				allLoaded = false;
			}
		}
		if (allLoaded) {
			_giftsLoadingLifetime.destroy();
			_giftsAppearing->stop();
			_giftsAppearing->start([=](float64 value) {
				update();
				if (value >= 1.) {
					_giftsAppearing = nullptr;
					_pinnedToTopGiftsFirstTimeShowed = true;
					if (_lottieSingleLoop) {
						auto allFramesCaptured = true;
						for (const auto &entry : _pinnedToTopGifts) {
							if (entry.animation || entry.lastFrame.isNull()) {
								allFramesCaptured = false;
								break;
							}
						}
						if (allFramesCaptured) {
							_lottiePlayer = nullptr;
						}
					}
				}
			}, 0., 1., 400, anim::easeOutQuint);
		}
	}, _giftsLoadingLifetime);
}

QPointF TopBar::calculateGiftPosition(
		int position,
		float64 progress,
		const QRect &userpicRect) const {
	const auto acx = userpicRect.x() + userpicRect.width() / 2.;
	const auto acy = userpicRect.y() + userpicRect.height() / 2.;
	const auto aw = userpicRect.width();
	const auto ah = userpicRect.height();

	auto giftPos = QPointF();
	auto delayValue = 0.;
	switch (position) {
	case 0: // Left.
		giftPos = QPointF(
			acx / 2. - st::infoProfileTopBarGiftLeft.x(),
			acy - st::infoProfileTopBarGiftLeft.y());
		delayValue = 1.6;
		break;
	case 1: // Top left.
		giftPos = QPointF(
			acx * 2. / 3. - st::infoProfileTopBarGiftTopLeft.x(),
			userpicRect.y() - st::infoProfileTopBarGiftTopLeft.y());
		delayValue = 0.;
		break;
	case 2: // Bottom left.
		giftPos = QPointF(
			acx * 2. / 3. - st::infoProfileTopBarGiftBottomLeft.x(),
			userpicRect.y() + ah - st::infoProfileTopBarGiftBottomLeft.y());
		delayValue = 0.9;
		break;
	case 3: // Right.
		giftPos = QPointF(
			acx + aw / 2. + st::infoProfileTopBarGiftRight.x(),
			acy - st::infoProfileTopBarGiftRight.y());
		delayValue = 1.6;
		break;
	case 4: // Top right.
		giftPos = QPointF(
			acx + aw / 3. + st::infoProfileTopBarGiftTopRight.x(),
			userpicRect.y() - st::infoProfileTopBarGiftTopRight.y());
		delayValue = 0.9;
		break;
	default: // Bottom right.
		giftPos = QPointF(
			acx + aw / 3. + st::infoProfileTopBarGiftBottomRight.x(),
			userpicRect.y() + ah - st::infoProfileTopBarGiftBottomRight.y());
		delayValue = 0.;
		break;
	}

	const auto delayFraction = 0.2;
	const auto maxDelayFraction = 1.6 * delayFraction;
	const auto intervalFraction = 1. - maxDelayFraction;
	const auto delay = delayValue * delayFraction;
	const auto collapse = (progress >= 1. - delay)
		? 1.
		: std::clamp((progress - maxDelayFraction + delay)
			/ intervalFraction, 0., 1.);

	if (collapse < 1.) {
		const auto collapseX = 1. - std::pow(1. - collapse, 2.);
		giftPos = QPointF(
			acx + (giftPos.x() - acx) * collapseX,
			acy + (giftPos.y() - acy) * collapse);
	}

	return giftPos;
}

void TopBar::paintPinnedToTopGifts(
		QPainter &p,
		const QRect &rect,
		const QRect &userpicRect) {
	if (_pinnedToTopGifts.empty() || _source == Source::Preview) {
		return;
	}

	const auto progress = _giftsHiding
		? _progress.current() * _giftsHiding->value(1.)
		: (_giftsAppearing
			? _progress.current() * _giftsAppearing->value(0.)
			: _progress.current());

	for (auto &gift : _pinnedToTopGifts) {
		if (!gift.animation
			&& (_lottieSingleLoop ? gift.lastFrame.isNull() : true)) {
			continue;
		}

		const auto giftPos = calculateGiftPosition(
			gift.position,
			progress,
			userpicRect);

		const auto alpha = progress;
		if (alpha <= 0.) {
			continue;
		}

		p.setOpacity(alpha);
		auto frameToRender = QImage();
		if (_lottieSingleLoop && !gift.lastFrame.isNull()) {
			frameToRender = gift.lastFrame;
		} else if (gift.animation && gift.animation->ready()) {
			frameToRender = gift.animation->frame();
			frameToRender.setDevicePixelRatio(style::DevicePixelRatio());
			if (_lottiePlayer) {
				_lottiePlayer->markFrameShown();
			}
			if (_lottieSingleLoop && gift.animation->framesCount() > 0) {
				const auto currentFrame = gift.animation->frameIndex();
				const auto totalFrames = gift.animation->framesCount();
				if (currentFrame >= totalFrames - 1) {
					gift.lastFrame = frameToRender;
					gift.animation = nullptr;

					auto allDone = true;
					for (const auto &entry : _pinnedToTopGifts) {
						if (entry.animation) {
							allDone = false;
							break;
						}
					}
					if (allDone) {
						_lottiePlayer = nullptr;
					}
				}
			}
		}
		if (!frameToRender.isNull()) {
			const auto frameSize = frameToRender.width()
				/ style::DevicePixelRatio();
			const auto halfFrameSize = frameSize / 2.;
			const auto resultPos = QPointF(
				giftPos.x() - halfFrameSize,
				giftPos.y() - halfFrameSize);
			if (!gift.bg.isNull()) {
				const auto bgSize = gift.bg.width()
					/ style::DevicePixelRatio();
				const auto bgPos = QPointF(
					resultPos.x() + (frameSize - bgSize) / 2.,
					resultPos.y() + (frameSize - bgSize) / 2.);
				p.drawImage(bgPos, gift.bg);
			}
			p.drawImage(resultPos, frameToRender);
		}
	}
	p.setOpacity(1.);
}

void TopBar::setupStoryOutline(const QRect &geometry) {
	const auto user = _peer->asUser();
	const auto channel = _peer->asChannel();
	if (!user && !channel) {
		return;
	}

	rpl::combine(
		_edgeColor.value(),
		rpl::merge(
			rpl::single(rpl::empty_value()),
			style::PaletteChanged(),
			_peer->session().changes().peerUpdates(
				Data::PeerUpdate::Flag::StoriesState
					| Data::PeerUpdate::Flag::ColorProfile
			) | rpl::filter([=](const Data::PeerUpdate &update) {
				return update.peer == _peer;
			}) | rpl::to_empty)
	) | rpl::on_next([=](
			std::optional<QColor> edgeColor,
			rpl::empty_value) {
		const auto geometry = QRectF(userpicGeometry());
		const auto colorProfile
			= _peer->session().api().peerColors().colorProfileFor(_peer);
		const auto hasProfileColor = colorProfile
			&& colorProfile->story.size() > 1;
		if (hasProfileColor) {
			edgeColor = std::nullopt;
		}
		_storyOutlineBrush = hasProfileColor
			? Ui::UnreadStoryOutlineGradient(
				geometry,
				colorProfile->story[0],
				colorProfile->story[1])
			: Ui::UnreadStoryOutlineGradient(geometry);
		updateStoryOutline(edgeColor);
	}, lifetime());
}

void TopBar::updateStoryOutline(std::optional<QColor> edgeColor) {
	if (width() <= 0) {
		return;
	}
	const auto user = _peer->asUser();
	const auto channel = _peer->asChannel();
	if (!user && !channel) {
		return;
	}

	const auto hasActiveStories = (_source == Source::Preview)
		? true
		: (user ? user->hasActiveStories() : channel->hasActiveStories());
	const auto hasLiveStories = (_source == Source::Preview)
		? false
		: (user ? user->hasActiveVideoStream() : false);

	if (_hasStories != hasActiveStories
		|| _hasLiveStories != hasLiveStories) {
		_hasStories = hasActiveStories;
		_hasLiveStories = hasLiveStories;
		update();
	}

	if (!hasActiveStories) {
		_storySegments.clear();
		return;
	}
	const auto widthBig = style::ConvertFloatScale(3.0);

	_storySegments.clear();

	if (_source == Source::Preview) {
		const auto colorProfile = effectiveColorProfile();
		const auto hasProfileColor = colorProfile
			&& colorProfile->story.size() > 1;
		const auto previewBrush = hasProfileColor
			? Ui::UnreadStoryOutlineGradient(
				QRectF(userpicGeometry()),
				colorProfile->story[0],
				colorProfile->story[1])
			: _localCollectible
			? Ui::UnreadStoryOutlineGradient(
				QRectF(userpicGeometry()),
				Ui::BlendColors(_localCollectible->edgeColor, Qt::white, .5),
				Ui::BlendColors(_localCollectible->edgeColor, Qt::white, .5))
			: Ui::UnreadStoryOutlineGradient(QRectF(userpicGeometry()));
		_storySegments.push_back({
			.brush = QBrush(previewBrush),
			.width = widthBig,
		});
		return;
	}

	const auto &stories = _peer->owner().stories();
	const auto source = stories.source(_peer->id);
	if (!source) {
		return;
	}

	const auto baseColor = edgeColor
		? Ui::BlendColors(*edgeColor, Qt::white, .5)
		: _storyOutlineBrush.color();
	const auto unreadBrush = _hasLiveStories
		? st::attentionButtonFg->b
		: edgeColor
		? QBrush(baseColor)
		: _storyOutlineBrush;
	const auto readBrush = edgeColor
		? QBrush(anim::with_alpha(baseColor, 0.5))
		: QBrush(st::dialogsUnreadBgMuted->b);

	if (_hasLiveStories) {
		_storySegments.push_back({
			.brush = unreadBrush,
			.width = widthBig,
		});
	} else {
		const auto readTill = source->readTill;
		const auto widthSmall = widthBig / 2.;
		for (const auto &storyIdDates : source->ids) {
			const auto isUnread = (storyIdDates.id > readTill);
			_storySegments.push_back({
				.brush = isUnread ? unreadBrush : readBrush,
				.width = !isUnread ? widthSmall : widthBig,
			});
		}
	}
}

void TopBar::paintStoryOutline(QPainter &p, const QRect &geometry) {
	if (!_hasStories || _storySegments.empty()) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);

	const auto progress = _progress.current();
	const auto alpha = std::clamp(
		(progress - kStoryOutlineFadeEnd) / kStoryOutlineFadeRange,
		0.,
		1.);
	if (alpha <= 0.) {
		return;
	}

	p.setOpacity(alpha);
	const auto outlineWidth = style::ConvertFloatScale(4.0);
	const auto padding = style::ConvertFloatScale(3.0);
	const auto outlineRect = QRectF(geometry).adjusted(
		-padding - outlineWidth / 2,
		-padding - outlineWidth / 2,
		padding + outlineWidth / 2,
		padding + outlineWidth / 2);

	Ui::PaintOutlineSegments(p, outlineRect, _storySegments);

	if (_hasLiveStories) {
		const auto outline = _edgeColor.current().value_or(
			_solidBg.value_or(st::boxDividerBg->c));
		Ui::PaintLiveBadge(
			p,
			geometry.x(),
			geometry.y() + outlineWidth + padding,
			geometry.width(),
			outline);
	}
}

void TopBar::setupStatusWithRating() {
	_status->setAttribute(Qt::WA_TransparentForMouseEvents);
	if (const auto rating = _starsRating.get()) {
		_statusShift = rating->widthValue();
		_statusShift.changes() | rpl::on_next([=] {
			updateLabelsPosition();
		}, _status->lifetime());
		rating->raise();
	}
}

rpl::producer<std::optional<QColor>> TopBar::edgeColor() const {
	return _edgeColor.value();
}

const style::FlatLabel &TopBar::statusStyle() const {
	return _peer->isMegagroup()
		? st::infoProfileMegagroupCover.status
		: st::infoProfileCover.status;
}

rpl::producer<QString> TopBar::nameValue() const {
	if (const auto topic = _key.topic()) {
		return Info::Profile::TitleValue(topic);
	}
	return Info::Profile::NameValue(_peer);
}

TopBarActionButtonStyle TopBar::mapActionStyle(
		std::optional<QColor> c) const {
	if (c) {
		return TopBarActionButtonStyle{
			.bgColor = Ui::BlendColors(
				*c,
				Qt::black,
				st::infoProfileTopBarActionButtonBgOpacity),
			.fgColor = std::make_optional(st::premiumButtonFg->c),
			.shadowColor = std::nullopt,
		};
	} else {
		return TopBarActionButtonStyle{
			.bgColor = anim::with_alpha(
				st::boxBg->c,
				1. - st::infoProfileTopBarActionButtonBgOpacity),
			.fgColor = std::nullopt,
			.shadowColor = std::make_optional(st::windowShadowFgFallback->c),
		};
	}
}

} // namespace Info::Profile
