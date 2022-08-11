/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_cover.h"

#include "data/data_photo.h"
#include "data/data_peer_values.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_changes.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/stickers/data_custom_emoji.h"
#include "editor/photo_editor_layer_widget.h"
#include "info/profile/info_profile_values.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "lang/lang_keys.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/effects/ripple_animation.h"
#include "ui/text/text_block.h"
#include "ui/text/text_utilities.h"
#include "ui/special_buttons.h"
#include "ui/unread_badge.h"
#include "base/unixtime.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "core/application.h"
#include "main/main_session.h"
#include "settings/settings_premium.h"
#include "apiwrap.h"
#include "mainwindow.h"
#include "api/api_peer_photo.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"
#include "styles/style_chat_helpers.h"

namespace Info {
namespace Profile {
namespace {

auto MembersStatusText(int count) {
	return tr::lng_chat_status_members(tr::now, lt_count_decimal, count);
};

auto OnlineStatusText(int count) {
	return tr::lng_chat_status_online(tr::now, lt_count_decimal, count);
};

auto ChatStatusText(int fullCount, int onlineCount, bool isGroup) {
	if (onlineCount > 1 && onlineCount <= fullCount) {
		return tr::lng_chat_status_members_online(
			tr::now,
			lt_members_count,
			MembersStatusText(fullCount),
			lt_online_count,
			OnlineStatusText(onlineCount));
	} else if (fullCount > 0) {
		return isGroup
			? tr::lng_chat_status_members(
				tr::now,
				lt_count_decimal,
				fullCount)
			: tr::lng_chat_status_subscribers(
				tr::now,
				lt_count_decimal,
				fullCount);
	}
	return isGroup
		? tr::lng_group_status(tr::now)
		: tr::lng_channel_status(tr::now);
};

} // namespace

Cover::Cover(
	QWidget *parent,
	not_null<PeerData*> peer,
	not_null<Window::SessionController*> controller)
: Cover(parent, peer, controller, NameValue(
	peer
) | rpl::map([=](const TextWithEntities &name) {
	return name.text;
})) {
}

Cover::Cover(
	QWidget *parent,
	not_null<PeerData*> peer,
	not_null<Window::SessionController*> controller,
	rpl::producer<QString> title)
: FixedHeightWidget(
	parent,
	st::infoProfilePhotoTop
		+ st::infoProfilePhoto.size.height()
		+ st::infoProfilePhotoBottom)
, _controller(controller)
, _peer(peer)
, _userpic(
	this,
	controller,
	_peer,
	Ui::UserpicButton::Role::OpenPhoto,
	st::infoProfilePhoto)
, _name(this, st::infoProfileNameLabel)
, _status(
	this,
	_peer->isMegagroup()
		? st::infoProfileMegagroupStatusLabel
		: st::infoProfileStatusLabel)
, _refreshStatusTimer([this] { refreshStatusText(); }) {
	_peer->updateFull();

	_name->setSelectable(true);
	_name->setContextCopyText(tr::lng_profile_copy_fullname(tr::now));

	if (!_peer->isMegagroup()) {
		_status->setAttribute(Qt::WA_TransparentForMouseEvents);
	}

	initViewers(std::move(title));
	setupChildGeometry();

	_userpic->uploadPhotoRequests(
	) | rpl::start_with_next([=] {
		_peer->session().api().peerPhoto().upload(
			_peer,
			_userpic->takeResultImage());
	}, _userpic->lifetime());
}

void Cover::setupChildGeometry() {
	widthValue(
	) | rpl::start_with_next([this](int newWidth) {
		_userpic->moveToLeft(
			st::infoProfilePhotoLeft,
			st::infoProfilePhotoTop,
			newWidth);
		refreshNameGeometry(newWidth);
		refreshStatusGeometry(newWidth);
	}, lifetime());
}

Cover *Cover::setOnlineCount(rpl::producer<int> &&count) {
	std::move(
		count
	) | rpl::start_with_next([this](int count) {
		_onlineCount = count;
		refreshStatusText();
	}, lifetime());
	return this;
}

void Cover::initViewers(rpl::producer<QString> title) {
	using Flag = Data::PeerUpdate::Flag;
	std::move(
		title
	) | rpl::start_with_next([=](const QString &title) {
		_name->setText(title);
		refreshNameGeometry(width());
	}, lifetime());

	_peer->session().changes().peerFlagsValue(
		_peer,
		Flag::OnlineStatus | Flag::Members
	) | rpl::start_with_next(
		[=] { refreshStatusText(); },
		lifetime());
	if (!_peer->isUser()) {
		_peer->session().changes().peerFlagsValue(
			_peer,
			Flag::Rights
		) | rpl::start_with_next(
			[=] { refreshUploadPhotoOverlay(); },
			lifetime());
	} else if (_peer->isSelf()) {
		refreshUploadPhotoOverlay();
	}
	rpl::combine(
		BadgeValue(_peer),
		EmojiStatusIdValue(_peer)
	) | rpl::start_with_next([=](Badge badge, DocumentId emojiStatusId) {
		setBadge(badge, emojiStatusId);
	}, lifetime());
}

void Cover::refreshUploadPhotoOverlay() {
	_userpic->switchChangePhotoOverlay([&] {
		if (const auto chat = _peer->asChat()) {
			return chat->canEditInformation();
		} else if (const auto channel = _peer->asChannel()) {
			return channel->canEditInformation();
		}
		return _peer->isSelf();
	}());
}

void Cover::setBadge(Badge badge, DocumentId emojiStatusId) {
	if (!_peer->session().premiumBadgesShown() && badge == Badge::Premium) {
		badge = Badge::None;
	}
	if (badge != Badge::Premium) {
		emojiStatusId = 0;
	}
	if (_badge == badge && _emojiStatusId == emojiStatusId) {
		return;
	}
	_badge = badge;
	_emojiStatusId = emojiStatusId;
	_emojiStatus = nullptr;
	_badgeView.destroy();
	switch (_badge) {
	case Badge::Verified:
	case Badge::Premium: {
		_badgeView.create(this);
		_badgeView->show();
		if (_emojiStatusId) {
			auto &owner = _controller->session().data();
			_emojiStatus = owner.customEmojiManager().create(
				_emojiStatusId,
				[raw = _badgeView.data()]{ raw->update(); },
				Data::CustomEmojiManager::SizeTag::Large);
			const auto size = Ui::Emoji::GetSizeLarge()
				/ style::DevicePixelRatio();
			const auto emoji = Ui::Text::AdjustCustomEmojiSize(size);
			_badgeView->resize(emoji, emoji);
			_badgeView->paintRequest(
			) | rpl::start_with_next([=, check = _badgeView.data()]{
				Painter p(check);
				_emojiStatus->paint(
					p,
					0,
					0,
					crl::now(),
					st::windowBgOver->c,
					_controller->isGifPausedAtLeastFor(
						Window::GifPauseReason::Layer));
			}, _badgeView->lifetime());
		} else {
			const auto icon = (_badge == Badge::Verified)
				? &st::infoVerifiedCheck
				: &st::infoPremiumStar;
			_badgeView->resize(icon->size());
			_badgeView->paintRequest(
			) | rpl::start_with_next([=, check = _badgeView.data()]{
				Painter p(check);
				icon->paint(p, 0, 0, check->width());
			}, _badgeView->lifetime());
		}
	} break;
	case Badge::Scam:
	case Badge::Fake: {
		const auto fake = (_badge == Badge::Fake);
		const auto size = Ui::ScamBadgeSize(fake);
		const auto skip = st::infoVerifiedCheckPosition.x();
		_badgeView.create(this);
		_badgeView->show();
		_badgeView->resize(
			size.width() + 2 * skip,
			size.height() + 2 * skip);
		_badgeView->paintRequest(
		) | rpl::start_with_next([=, badge = _badgeView.data()]{
			Painter p(badge);
			Ui::DrawScamBadge(
				fake,
				p,
				badge->rect().marginsRemoved({ skip, skip, skip, skip }),
				badge->width(),
				st::attentionButtonFg);
			}, _badgeView->lifetime());
	} break;
	}

	if (_badge == Badge::Premium) {
		const auto userId = peerToUser(_peer->id).bare;
		_badgeView->setClickedCallback([=] {
			if (_peer->isSelf()) {
				showEmojiStatusSelector();
			} else {
				::Settings::ShowPremium(
					_controller,
					u"profile__%1"_q.arg(userId));
			}
		});
	} else {
		_badgeView->setAttribute(Qt::WA_TransparentForMouseEvents);
	}

	refreshNameGeometry(width());
}

void Cover::showEmojiStatusSelector() {
	Expects(_badgeView != nullptr);

	if (!_emojiStatusPanel) {
		createEmojiStatusSelector();
	}
	const auto parent = _emojiStatusPanel->parentWidget();
	const auto global = _badgeView->mapToGlobal({ 0, 0 });
	const auto local = parent->mapFromGlobal(global);
	_emojiStatusPanel->moveTopRight(
		local.y() + _badgeView->height(),
		local.x() + _badgeView->width() * 3);
	_emojiStatusPanel->toggleAnimated();
}

void Cover::createEmojiStatusSelector() {
	const auto set = [=](DocumentId id) {
		_controller->session().user()->setEmojiStatus(id);
		_controller->session().api().request(MTPaccount_UpdateEmojiStatus(
			id ? MTP_emojiStatus(MTP_long(id)) : MTP_emojiStatusEmpty()
		)).send();
		_emojiStatusPanel->hideAnimated();
	};
	const auto container = _controller->window().widget()->bodyWidget();
	using Selector = ChatHelpers::TabbedSelector;
	_emojiStatusPanel = base::make_unique_q<ChatHelpers::TabbedPanel>(
		container,
		_controller,
		object_ptr<Selector>(
			nullptr,
			_controller,
			Window::GifPauseReason::Layer,
			ChatHelpers::TabbedSelector::Mode::EmojiStatus));
	_emojiStatusPanel->setDropDown(true);
	_emojiStatusPanel->setDesiredHeightValues(
		1.,
		st::emojiPanMinHeight / 2,
		st::emojiPanMinHeight);
	_emojiStatusPanel->hide();
	_emojiStatusPanel->selector()->setAllowEmojiWithoutPremium(false);
	_emojiStatusPanel->selector()->emojiChosen(
	) | rpl::start_with_next([=] {
		set(0);
	}, _emojiStatusPanel->lifetime());
	_emojiStatusPanel->selector()->customEmojiChosen(
	) | rpl::start_with_next([=](Selector::FileChosen data) {
		set(data.document->id);
	}, _emojiStatusPanel->lifetime());
	_emojiStatusPanel->selector()->showPromoForPremiumEmoji();
}

void Cover::refreshStatusText() {
	auto hasMembersLink = [&] {
		if (auto megagroup = _peer->asMegagroup()) {
			return megagroup->canViewMembers();
		}
		return false;
	}();
	auto statusText = [&]() -> TextWithEntities {
		using namespace Ui::Text;
		auto currentTime = base::unixtime::now();
		if (auto user = _peer->asUser()) {
			const auto result = Data::OnlineTextFull(user, currentTime);
			const auto showOnline = Data::OnlineTextActive(user, currentTime);
			const auto updateIn = Data::OnlineChangeTimeout(user, currentTime);
			if (showOnline) {
				_refreshStatusTimer.callOnce(updateIn);
			}
			return showOnline
				? PlainLink(result)
				: TextWithEntities{ .text = result };
		} else if (auto chat = _peer->asChat()) {
			if (!chat->amIn()) {
				return tr::lng_chat_status_unaccessible({}, WithEntities);
			}
			auto fullCount = std::max(
				chat->count,
				int(chat->participants.size()));
			return { .text = ChatStatusText(fullCount, _onlineCount, true) };
		} else if (auto channel = _peer->asChannel()) {
			auto fullCount = qMax(channel->membersCount(), 1);
			auto result = ChatStatusText(
				fullCount,
				_onlineCount,
				channel->isMegagroup());
			return hasMembersLink
				? PlainLink(result)
				: TextWithEntities{ .text = result };
		}
		return tr::lng_chat_status_unaccessible(tr::now, WithEntities);
	}();
	_status->setMarkedText(statusText);
	if (hasMembersLink) {
		_status->setLink(1, std::make_shared<LambdaClickHandler>([=] {
			_showSection.fire(Section::Type::Members);
		}));
	}
	refreshStatusGeometry(width());
}

Cover::~Cover() {
}

void Cover::refreshNameGeometry(int newWidth) {
	auto nameLeft = st::infoProfileNameLeft;
	auto nameTop = st::infoProfileNameTop;
	auto nameWidth = newWidth
		- nameLeft
		- st::infoProfileNameRight;
	if (_badgeView) {
		nameWidth -= st::infoVerifiedCheckPosition.x() + _badgeView->width();
	}
	_name->resizeToNaturalWidth(nameWidth);
	_name->moveToLeft(nameLeft, nameTop, newWidth);
	if (_badgeView) {
		const auto star = !_emojiStatus
			&& (_badge == Badge::Premium || _badge == Badge::Verified);
		const auto fake = !_emojiStatus && !star;
		const auto skip = fake ? 0 : st::infoVerifiedCheckPosition.x();
		const auto badgeLeft = nameLeft + _name->width() + skip;
		const auto badgeTop = nameTop
			+ (star
				? st::infoVerifiedCheckPosition.y()
				: (_name->height() - _badgeView->height()) / 2);
		_badgeView->moveToLeft(badgeLeft, badgeTop, newWidth);
	}
}

void Cover::refreshStatusGeometry(int newWidth) {
	auto statusWidth = newWidth
		- st::infoProfileStatusLeft
		- st::infoProfileStatusRight;
	_status->resizeToWidth(statusWidth);
	_status->moveToLeft(
		st::infoProfileStatusLeft,
		st::infoProfileStatusTop,
		newWidth);
}

} // namespace Profile
} // namespace Info
