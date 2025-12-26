/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_cover.h"

#include "api/api_user_privacy.h"
#include "base/timer_rpl.h"
#include "data/data_peer_values.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_emoji_statuses.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_changes.h"
#include "data/data_session.h"
#include "data/data_forum_topic.h"
#include "data/stickers/data_custom_emoji.h"
#include "info/profile/info_profile_badge.h"
#include "info/profile/info_profile_badge_tooltip.h"
#include "info/profile/info_profile_emoji_status_panel.h"
#include "info/profile/info_profile_status_label.h"
#include "info/profile/info_profile_values.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "boxes/peers/edit_forum_topic_box.h"
#include "boxes/report_messages_box.h"
#include "history/view/media/history_view_sticker_player.h"
#include "lang/lang_keys.h"
#include "ui/boxes/show_or_premium_box.h"
#include "ui/controls/stars_rating.h"
#include "ui/controls/userpic_button.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/text/text_utilities.h"
#include "ui/basic_click_handlers.h"
#include "ui/ui_utility.h"
#include "ui/painter.h"
#include "base/event_filter.h"
#include "base/unixtime.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "settings/settings_premium.h"
#include "chat_helpers/stickers_lottie.h"
#include "apiwrap.h"
#include "api/api_peer_photo.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"
#include "styles/style_dialogs.h"
#include "styles/style_menu_icons.h"

namespace Info::Profile {
namespace {

constexpr auto kWaitBeforeGiftBadge = crl::time(1000);
constexpr auto kGiftBadgeGlares = 3;
constexpr auto kGlareDurationStep = crl::time(320);
constexpr auto kGlareTimeout = crl::time(1000);

[[nodiscard]] const style::InfoProfileCover &CoverStyle(
		not_null<PeerData*> peer,
		Data::ForumTopic *topic,
		Cover::Role role) {
	return (role == Cover::Role::EditContact)
		? st::infoEditContactCover
		: topic
		? st::infoTopicCover
		: peer->isMegagroup()
		? st::infoProfileMegagroupCover
		: st::infoProfileCover;
}

} // namespace

QMargins LargeCustomEmojiMargins() {
	const auto ratio = style::DevicePixelRatio();
	const auto emoji = Ui::Emoji::GetSizeLarge() / ratio;
	const auto size = Data::FrameSizeFromTag(Data::CustomEmojiSizeTag::Large)
		/ ratio;
	const auto left = (size - emoji) / 2;
	const auto right = size - emoji - left;
	return { left, left, right, right };
}

TopicIconView::TopicIconView(
	not_null<Data::ForumTopic*> topic,
	Fn<bool()> paused,
	Fn<void()> update)
: TopicIconView(
	topic,
	std::move(paused),
	std::move(update),
	st::windowSubTextFg) {
}

TopicIconView::TopicIconView(
	not_null<Data::ForumTopic*> topic,
	Fn<bool()> paused,
	Fn<void()> update,
	const style::color &generalIconFg)
: _topic(topic)
, _generalIconFg(generalIconFg)
, _paused(std::move(paused))
, _update(std::move(update)) {
	setup(topic);
}

void TopicIconView::paintInRect(QPainter &p, QRect rect) {
	const auto paint = [&](const QImage &image) {
		const auto size = image.size() / style::DevicePixelRatio();
		p.drawImage(
			QRect(
				rect.x() + (rect.width() - size.width()) / 2,
				rect.y() + (rect.height() - size.height()) / 2,
				size.width(),
				size.height()),
			image);
	};
	if (_player && _player->ready()) {
		const auto colored = _playerUsesTextColor
			? st::windowFg->c
			: QColor(0, 0, 0, 0);
		paint(_player->frame(
			st::infoTopicCover.photo.size,
			colored,
			false,
			crl::now(),
			_paused()).image);
		_player->markFrameShown();
	} else if (!_topic->iconId() && !_image.isNull()) {
		paint(_image);
	}
}

void TopicIconView::setup(not_null<Data::ForumTopic*> topic) {
	setupPlayer(topic);
	setupImage(topic);
}

void TopicIconView::setupPlayer(not_null<Data::ForumTopic*> topic) {
	IconIdValue(
		topic
	) | rpl::map([=](DocumentId id) -> rpl::producer<DocumentData*> {
		if (!id) {
			return rpl::single((DocumentData*)nullptr);
		}
		return topic->owner().customEmojiManager().resolve(
			id
		) | rpl::map([=](not_null<DocumentData*> document) {
			return document.get();
		}) | rpl::map_error_to_done();
	}) | rpl::flatten_latest(
	) | rpl::map([=](DocumentData *document)
	-> rpl::producer<std::shared_ptr<StickerPlayer>> {
		if (!document) {
			return rpl::single(std::shared_ptr<StickerPlayer>());
		}
		const auto media = document->createMediaView();
		media->checkStickerLarge();
		media->goodThumbnailWanted();

		return rpl::single() | rpl::then(
			document->session().downloaderTaskFinished()
		) | rpl::filter([=] {
			return media->loaded();
		}) | rpl::take(1) | rpl::map([=] {
			auto result = std::shared_ptr<StickerPlayer>();
			const auto sticker = document->sticker();
			if (sticker->isLottie()) {
				result = std::make_shared<HistoryView::LottiePlayer>(
					ChatHelpers::LottiePlayerFromDocument(
						media.get(),
						ChatHelpers::StickerLottieSize::StickerSet,
						st::infoTopicCover.photo.size,
						Lottie::Quality::High));
			} else if (sticker->isWebm()) {
				result = std::make_shared<HistoryView::WebmPlayer>(
					media->owner()->location(),
					media->bytes(),
					st::infoTopicCover.photo.size);
			} else {
				result = std::make_shared<HistoryView::StaticStickerPlayer>(
					media->owner()->location(),
					media->bytes(),
					st::infoTopicCover.photo.size);
			}
			result->setRepaintCallback(_update);
			_playerUsesTextColor = media->owner()->emojiUsesTextColor();
			return result;
		});
	}) | rpl::flatten_latest(
	) | rpl::on_next([=](std::shared_ptr<StickerPlayer> player) {
		_player = std::move(player);
		if (!_player) {
			_update();
		}
	}, _lifetime);
}

void TopicIconView::setupImage(not_null<Data::ForumTopic*> topic) {
	using namespace Data;
	if (topic->isGeneral()) {
		rpl::single(rpl::empty) | rpl::then(
			style::PaletteChanged()
		) | rpl::on_next([=] {
			_image = ForumTopicGeneralIconFrame(
				st::infoForumTopicIcon.size,
				_generalIconFg->c);
			_update();
		}, _lifetime);
		return;
	}
	rpl::combine(
		TitleValue(topic),
		ColorIdValue(topic)
	) | rpl::map([=](const QString &title, int32 colorId) {
		return ForumTopicIconFrame(colorId, title, st::infoForumTopicIcon);
	}) | rpl::on_next([=](QImage &&image) {
		_image = std::move(image);
		_update();
	}, _lifetime);
}

TopicIconButton::TopicIconButton(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<Data::ForumTopic*> topic)
: TopicIconButton(parent, topic, [=] {
	return controller->isGifPausedAtLeastFor(Window::GifPauseReason::Layer);
}) {
}

TopicIconButton::TopicIconButton(
	QWidget *parent,
	not_null<Data::ForumTopic*> topic,
	Fn<bool()> paused)
: AbstractButton(parent)
, _view(topic, paused, [=] { update(); }) {
	resize(st::infoTopicCover.photo.size);
	paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(this);
		_view.paintInRect(p, rect());
	}, lifetime());
}

Cover::Cover(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	Fn<not_null<QWidget*>()> parentForTooltip)
: Cover(
	parent,
	controller,
	peer,
	nullptr,
	Role::Info,
	NameValue(peer),
	parentForTooltip) {
}

Cover::Cover(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<Data::ForumTopic*> topic)
: Cover(
	parent,
	controller,
	topic->peer(),
	topic,
	Role::Info,
	TitleValue(topic),
	nullptr) {
}

Cover::Cover(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	Role role,
	rpl::producer<QString> title)
: Cover(
	parent,
	controller,
	peer,
	nullptr,
	role,
	std::move(title),
	nullptr) {
}

Cover::Cover(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	Data::ForumTopic *topic,
	Role role,
	rpl::producer<QString> title,
	Fn<not_null<QWidget*>()> parentForTooltip)
: FixedHeightWidget(parent, CoverStyle(peer, topic, role).height)
, _st(CoverStyle(peer, topic, role))
, _role(role)
, _controller(controller)
, _peer(peer)
, _emojiStatusPanel(peer->isSelf()
	? std::make_unique<EmojiStatusPanel>()
	: nullptr)
, _botVerify(role == Role::EditContact
	? nullptr
	: std::make_unique<Badge>(
		this,
		st::infoBotVerifyBadge,
		&peer->session(),
		BotVerifyBadgeForPeer(peer),
		nullptr,
		[=] {
			return controller->isGifPausedAtLeastFor(
				Window::GifPauseReason::Layer);
		}))
, _badgeContent(BadgeContentForPeer(peer))
, _badge(role == Role::EditContact
	? nullptr
	: std::make_unique<Badge>(
		this,
		st::infoPeerBadge,
		&peer->session(),
		_badgeContent.value(),
		_emojiStatusPanel.get(),
		[=] {
			return controller->isGifPausedAtLeastFor(
				Window::GifPauseReason::Layer);
		}))
, _verified(role == Role::EditContact
	? nullptr
	: std::make_unique<Badge>(
		this,
		st::infoPeerBadge,
		&peer->session(),
		VerifiedContentForPeer(peer),
		_emojiStatusPanel.get(),
		[=] {
			return controller->isGifPausedAtLeastFor(
				Window::GifPauseReason::Layer);
		}))
, _parentForTooltip(std::move(parentForTooltip))
, _badgeTooltipHide([=] { hideBadgeTooltip(); })
, _userpic(topic
	? nullptr
	: object_ptr<Ui::UserpicButton>(
		this,
		controller,
		_peer->userpicPaintingPeer(),
		Ui::UserpicButton::Role::OpenPhoto,
		Ui::UserpicButton::Source::PeerPhoto,
		_st.photo,
		_peer->userpicShape()))
, _changePersonal((role == Role::Info
	|| role == Role::EditContact
	|| topic
	|| !_peer->isUser()
	|| _peer->isSelf()
	|| _peer->asUser()->isBot())
	? nullptr
	: CreateUploadSubButton(this, _peer->asUser(), controller).get())
, _iconButton(topic
	? object_ptr<TopicIconButton>(this, controller, topic)
	: nullptr)
, _name(this, _st.name)
, _starsRating(_peer->isUser() && _role != Role::EditContact
	? std::make_unique<Ui::StarsRating>(
		this,
		_controller->uiShow(),
		_peer->isSelf() ? QString() : _peer->shortName(),
		Data::StarsRatingValue(_peer),
		(_peer->isSelf()
			? [=] { return _peer->owner().pendingStarsRating(); }
			: Fn<Data::StarsRatingPending()>()))
	: nullptr)
, _status(this, _st.status)
, _statusLabel(std::make_unique<StatusLabel>(_status.data(), _peer))
, _showLastSeen(this, tr::lng_status_lastseen_when(), _st.showLastSeen) {
	_peer->updateFull();
	if (const auto broadcast = _peer->monoforumBroadcast()) {
		broadcast->updateFull();
	}

	_name->setSelectable(true);
	_name->setContextCopyText(tr::lng_profile_copy_fullname(tr::now));

	if (!_peer->isMegagroup()) {
		_status->setAttribute(Qt::WA_TransparentForMouseEvents);
		if (const auto rating = _starsRating.get()) {
			_statusShift = rating->widthValue();
			_statusShift.changes() | rpl::on_next([=] {
				refreshStatusGeometry(width());
			}, _status->lifetime());
			rating->raise();
		}
	}

	setupShowLastSeen();

	if (_badge) {
		_badge->setPremiumClickCallback([=] {
			if (const auto panel = _emojiStatusPanel.get()) {
				panel->show(_controller, _badge->widget(), _badge->sizeTag());
			} else {
				::Settings::ShowEmojiStatusPremium(_controller, _peer);
			}
		});
	}
	auto badgeUpdates = rpl::producer<rpl::empty_value>();
	if (_badge) {
		badgeUpdates = rpl::merge(
			std::move(badgeUpdates),
			_badge->updated());
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
	std::move(badgeUpdates) | rpl::on_next([=] {
		refreshNameGeometry(width());
	}, _name->lifetime());

	initViewers(std::move(title));
	setupChildGeometry();
	setupUniqueBadgeTooltip();

	if (_userpic) {
	} else if (topic->canEdit()) {
		_iconButton->setClickedCallback([=] {
			_controller->show(Box(
				EditForumTopicBox,
				_controller,
				topic->history(),
				topic->rootId()));
		});
	} else {
		_iconButton->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
}

void Cover::setupShowLastSeen() {
	const auto user = _peer->asUser();
	if (_st.showLastSeenVisible
		&& user
		&& !user->isSelf()
		&& !user->isBot()
		&& !user->isServiceUser()
		&& user->session().premiumPossible()) {
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

		_controller->session().api().userPrivacy().value(
			Api::UserPrivacy::Key::LastSeen
		) | rpl::filter([=](Api::UserPrivacy::Rule rule) {
			return (rule.option == Api::UserPrivacy::Option::Everyone);
		}) | rpl::on_next([=] {
			if (user->lastseen().isHiddenByMe()) {
				user->updateFullForced();
			}
		}, _showLastSeen->lifetime());
	} else {
		_showLastSeen->hide();
	}

	using TextTransform = Ui::RoundButton::TextTransform;
	_showLastSeen->setTextTransform(TextTransform::NoTransform);
	_showLastSeen->setFullRadius(true);

	_showLastSeen->setClickedCallback([=] {
		const auto type = Ui::ShowOrPremium::LastSeen;
		auto box = Box(Ui::ShowOrPremiumBox, type, user->shortName(), [=] {
			_controller->session().api().userPrivacy().save(
				::Api::UserPrivacy::Key::LastSeen,
				{});
		}, [=] {
			::Settings::ShowPremium(_controller, u"lastseen_hidden"_q);
		});
		_controller->show(std::move(box));
	});
}

void Cover::setupChildGeometry() {
	widthValue(
	) | rpl::on_next([this](int newWidth) {
		if (_userpic) {
			_userpic->moveToLeft(_st.photoLeft, _st.photoTop, newWidth);
		} else {
			_iconButton->moveToLeft(_st.photoLeft, _st.photoTop, newWidth);
		}
		if (_changePersonal) {
			_changePersonal->moveToLeft(
				(_st.photoLeft
					+ _st.photo.photoSize
					- _changePersonal->width()
					+ st::infoEditContactPersonalLeft),
				(_userpic->y()
					+ _userpic->height()
					- _changePersonal->height()));
		}
		refreshNameGeometry(newWidth);
		refreshStatusGeometry(newWidth);
	}, lifetime());
}

Cover *Cover::setOnlineCount(rpl::producer<int> &&count) {
	std::move(count) | rpl::on_next([=](int value) {
		if (_statusLabel) {
			_statusLabel->setOnlineCount(value);
			refreshStatusGeometry(width());
		}
	}, lifetime());
	return this;
}

std::optional<QImage> Cover::updatedPersonalPhoto() const {
	return _personalChosen;
}

void Cover::initViewers(rpl::producer<QString> title) {
	using Flag = Data::PeerUpdate::Flag;
	std::move(
		title
	) | rpl::on_next([=](const QString &title) {
		_name->setText(title);
		refreshNameGeometry(width());
	}, lifetime());

	_statusLabel->setMembersLinkCallback([=] {
		_showSection.fire(Section::Type::Members);
	});

	_peer->session().changes().peerFlagsValue(
		_peer,
		Flag::OnlineStatus | Flag::Members
	) | rpl::on_next([=] {
		_statusLabel->refresh();
		refreshStatusGeometry(width());
	}, lifetime());

	_peer->session().changes().peerFlagsValue(
		_peer,
		(_peer->isUser() ? Flag::IsContact : Flag::Rights)
	) | rpl::on_next([=] {
		refreshUploadPhotoOverlay();
	}, lifetime());

	setupChangePersonal();
}

void Cover::refreshUploadPhotoOverlay() {
	if (!_userpic) {
		return;
	} else if (_role == Role::EditContact) {
		_userpic->setAttribute(Qt::WA_TransparentForMouseEvents);
		return;
	}

	const auto canChange = [&] {
		if (const auto chat = _peer->asChat()) {
			return chat->canEditInformation();
		} else if (const auto channel = _peer->asChannel()) {
			return channel->canEditInformation()
				&& !channel->isMonoforum();
		} else if (const auto user = _peer->asUser()) {
			return user->isSelf()
				|| (user->isContact()
					&& !user->isInaccessible()
					&& !user->isServiceUser());
		}
		Unexpected("Peer type in Info::Profile::Cover.");
	}();

	_userpic->switchChangePhotoOverlay(canChange, [=](
			Ui::UserpicButton::ChosenImage chosen) {
		using ChosenType = Ui::UserpicButton::ChosenType;
		auto result = Api::PeerPhoto::UserPhoto{
			base::take<QImage>(chosen.image), // Strange MSVC bug with take.
			chosen.markup.documentId,
			chosen.markup.colors,
		};
		switch (chosen.type) {
		case ChosenType::Set:
			_userpic->showCustom(base::duplicate(result.image));
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
	});

	const auto canReport = [=, peer = _peer] {
		if (!peer->hasUserpic()) {
			return false;
		}
		const auto user = peer->asUser();
		if (!user) {
			if (canChange) {
				return false;
			}
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

	const auto contextMenu = _userpic->lifetime()
		.make_state<base::unique_qptr<Ui::PopupMenu>>();
	const auto showMenu = [=, peer = _peer, controller = _controller](
			not_null<Ui::RpWidget*> parent) {
		if (!canReport()) {
			return false;
		}
		*contextMenu = base::make_unique_q<Ui::PopupMenu>(
			parent,
			st::popupMenuWithIcons);
		contextMenu->get()->addAction(tr::lng_profile_report(tr::now), [=] {
			controller->show(
				ReportProfilePhotoBox(
					peer,
					peer->owner().photo(peer->userpicPhotoId())),
				Ui::LayerOption::CloseOther);
		}, &st::menuIconReport);
		contextMenu->get()->popup(QCursor::pos());
		return true;
	};
	base::install_event_filter(_userpic, [showMenu, raw = _userpic.data()](
			not_null<QEvent*> e) {
		return (e->type() == QEvent::ContextMenu && showMenu(raw))
			? base::EventFilterResult::Cancel
			: base::EventFilterResult::Continue;
	});

	if (const auto user = _peer->asUser()) {
		_userpic->resetPersonalRequests(
		) | rpl::on_next([=] {
			user->session().api().peerPhoto().clearPersonal(user);
			_userpic->showSource(Ui::UserpicButton::Source::PeerPhoto);
		}, lifetime());
	}
}

void Cover::setupChangePersonal() {
	if (!_changePersonal) {
		return;
	}

	_changePersonal->chosenImages(
	) | rpl::on_next([=](Ui::UserpicButton::ChosenImage &&chosen) {
		if (chosen.type == Ui::UserpicButton::ChosenType::Suggest) {
			_peer->session().api().peerPhoto().suggest(
				_peer,
				{
					std::move(chosen.image),
					chosen.markup.documentId,
					chosen.markup.colors,
				});
		} else {
			_personalChosen = std::move(chosen.image);
			_userpic->showCustom(base::duplicate(*_personalChosen));
			_changePersonal->overrideHasPersonalPhoto(true);
			_changePersonal->showSource(
				Ui::UserpicButton::Source::NonPersonalIfHasPersonal);
		}
	}, _changePersonal->lifetime());

	_changePersonal->resetPersonalRequests(
	) | rpl::on_next([=] {
		_personalChosen = QImage();
		_userpic->showSource(
			Ui::UserpicButton::Source::NonPersonalPhoto);
		_changePersonal->overrideHasPersonalPhoto(false);
		_changePersonal->showCustom(QImage());
	}, _changePersonal->lifetime());
}



Cover::~Cover() {
	base::take(_badgeTooltip);
	base::take(_badgeOldTooltips);
}

void Cover::refreshNameGeometry(int newWidth) {
	auto nameWidth = newWidth - _st.nameLeft - _st.rightSkip;
	const auto verifiedWidget = _verified ? _verified->widget() : nullptr;
	const auto badgeWidget = _badge ? _badge->widget() : nullptr;
	if (verifiedWidget) {
		nameWidth -= verifiedWidget->width();
	}
	if (badgeWidget) {
		nameWidth -= badgeWidget->width();
	}
	if (verifiedWidget || badgeWidget) {
		nameWidth -= st::infoVerifiedCheckPosition.x();
	}
	auto nameLeft = _st.nameLeft;
	const auto badgeTop = _st.nameTop;
	const auto badgeBottom = _st.nameTop + _name->height();
	const auto margins = LargeCustomEmojiMargins();

	if (_botVerify) {
		_botVerify->move(nameLeft - margins.left(), badgeTop, badgeBottom);
		if (const auto widget = _botVerify->widget()) {
			const auto skip = widget->width()
				+ st::infoVerifiedCheckPosition.x();
			nameLeft += skip;
			nameWidth -= skip;
		}
	}
	_name->resizeToNaturalWidth(nameWidth);
	_name->moveToLeft(nameLeft, _st.nameTop, newWidth);
	const auto badgeLeft = nameLeft + _name->width();
	if (_badge) {
		_badge->move(badgeLeft, badgeTop, badgeBottom);
	}
	if (_verified) {
		_verified->move(
			badgeLeft + (badgeWidget ? badgeWidget->width() : 0),
			badgeTop,
			badgeBottom);
	}
}

void Cover::refreshStatusGeometry(int newWidth) {
	if (const auto rating = _starsRating.get()) {
		rating->moveTo(_st.starsRatingLeft, _st.starsRatingTop);
	}
	const auto statusLeft = _st.statusLeft + _statusShift.current();
	auto statusWidth = newWidth - statusLeft - _st.rightSkip;
	_status->resizeToNaturalWidth(statusWidth);
	_status->moveToLeft(statusLeft, _st.statusTop, newWidth);
	const auto left = statusLeft + _status->textMaxWidth();
	_showLastSeen->moveToLeft(
		left + _st.showLastSeenPosition.x(),
		_st.showLastSeenPosition.y(),
		newWidth);
}

void Cover::hideBadgeTooltip() {
	_badgeTooltipHide.cancel();
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

void Cover::setupUniqueBadgeTooltip() {
	if (!_badge) {
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
		if (!collectible) {
			return;
		}
		const auto parent = _parentForTooltip
			? _parentForTooltip()
			: _controller->window().widget()->bodyWidget();
		_badgeTooltip = std::make_unique<BadgeTooltip>(
			parent,
			collectible,
			widget);
		const auto raw = _badgeTooltip.get();
		raw->fade(true);
		_badgeTooltipHide.callOnce(kGiftBadgeGlares * raw->glarePeriod()
			- st::infoGiftTooltip.duration * 1.5);
	}, lifetime());

	if (const auto raw = _badgeTooltip.get()) {
		raw->finishAnimating();
	}
}

} // namespace Info::Profile
