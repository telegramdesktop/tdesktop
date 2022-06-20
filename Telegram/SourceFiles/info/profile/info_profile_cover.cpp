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
#include "data/data_changes.h"
#include "editor/photo_editor_layer_widget.h"
#include "info/profile/info_profile_values.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "lang/lang_keys.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/effects/ripple_animation.h"
#include "ui/text/text_utilities.h"
#include "ui/special_buttons.h"
#include "ui/unread_badge.h"
#include "base/unixtime.h"
#include "window/window_session_controller.h"
#include "core/application.h"
#include "main/main_session.h"
#include "settings/settings_premium.h"
#include "apiwrap.h"
#include "api/api_peer_photo.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"

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
	BadgeValue(
		_peer
	) | rpl::start_with_next([=](Badge badge) {
		if (badge == Badge::Premium
			&& !_peer->session().premiumBadgesShown()) {
			badge = Badge::None;
		}
		setBadge(badge);
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

void Cover::setBadge(Badge badge) {
	if (_badge == badge) {
		return;
	}
	_badge = badge;
	_verifiedCheck.destroy();
	_scamFakeBadge.destroy();
	switch (_badge) {
	case Badge::Verified:
	case Badge::Premium: {
		const auto icon = (_badge == Badge::Verified)
			? &st::infoVerifiedCheck
			: &st::infoPremiumStar;
		_verifiedCheck.create(this);
		_verifiedCheck->show();
		_verifiedCheck->resize(icon->size());
		_verifiedCheck->paintRequest(
		) | rpl::start_with_next([icon, check = _verifiedCheck.data()] {
			Painter p(check);
			icon->paint(p, 0, 0, check->width());
		}, _verifiedCheck->lifetime());
		if (_badge == Badge::Premium) {
			const auto userId = peerToUser(_peer->id).bare;
			_verifiedCheck->setClickedCallback([=] {
				::Settings::ShowPremium(
					_controller,
					u"profile__%1"_q.arg(userId));
			});
		} else {
			_verifiedCheck->setAttribute(Qt::WA_TransparentForMouseEvents);
		}
	} break;
	case Badge::Scam:
	case Badge::Fake: {
		const auto fake = (_badge == Badge::Fake);
		const auto size = Ui::ScamBadgeSize(fake);
		const auto skip = st::infoVerifiedCheckPosition.x();
		_scamFakeBadge.create(this);
		_scamFakeBadge->show();
		_scamFakeBadge->resize(
			size.width() + 2 * skip,
			size.height() + 2 * skip);
		_scamFakeBadge->paintRequest(
		) | rpl::start_with_next([=, badge = _scamFakeBadge.data()]{
			Painter p(badge);
			Ui::DrawScamBadge(
				fake,
				p,
				badge->rect().marginsRemoved({ skip, skip, skip, skip }),
				badge->width(),
				st::attentionButtonFg);
			}, _scamFakeBadge->lifetime());
	} break;
	}
	refreshNameGeometry(width());
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
	if (_verifiedCheck) {
		nameWidth -= st::infoVerifiedCheckPosition.x()
			+ _verifiedCheck->width();
	} else if (_scamFakeBadge) {
		nameWidth -= st::infoVerifiedCheckPosition.x()
			+ _scamFakeBadge->width();
	}
	_name->resizeToNaturalWidth(nameWidth);
	_name->moveToLeft(nameLeft, nameTop, newWidth);
	if (_verifiedCheck) {
		const auto checkLeft = nameLeft
			+ _name->width()
			+ st::infoVerifiedCheckPosition.x();
		const auto checkTop = nameTop
			+ st::infoVerifiedCheckPosition.y();
		_verifiedCheck->moveToLeft(checkLeft, checkTop, newWidth);
	} else if (_scamFakeBadge) {
		const auto skip = st::infoVerifiedCheckPosition.x();
		const auto badgeLeft = nameLeft
			+ _name->width()
			+ st::infoVerifiedCheckPosition.x()
			- skip;
		const auto badgeTop = nameTop
			+ (_name->height() - _scamFakeBadge->height()) / 2;
		_scamFakeBadge->moveToLeft(badgeLeft, badgeTop, newWidth);
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
