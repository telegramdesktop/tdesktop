/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/feed/info_feed_cover.h"

#include "data/data_feed.h"
#include "info/info_controller.h"
#include "lang/lang_keys.h"
#include "ui/widgets/labels.h"
#include "styles/style_info.h"

namespace Info {
namespace FeedProfile {

Cover::Cover(
	QWidget *parent,
	not_null<Controller*> controller)
: FixedHeightWidget(
	parent,
	st::infoProfilePhotoTop
		+ st::infoProfilePhoto.size.height()
		+ st::infoProfilePhotoBottom)
, _controller(controller)
, _feed(_controller->key().feed())
//, _userpic(
//	this,
//	controller->parentController(),
//	_peer,
//	Ui::UserpicButton::Role::OpenPhoto,
//	st::infoProfilePhoto)
, _name(this, st::infoProfileNameLabel)
, _status(
	this,
	st::infoProfileMegagroupStatusLabel) {
	_name->setSelectable(true);
	_name->setContextCopyText(lang(lng_profile_copy_fullname));

	initViewers();
	setupChildGeometry();
}

void Cover::setupChildGeometry() {
	using namespace rpl::mappers;
	//
	// Visual Studio 2017 15.5.1 internal compiler error here.
	// See https://developercommunity.visualstudio.com/content/problem/165155/ice-regression-in-1551-after-successfull-build-in.html
	//
	//rpl::combine(
	//	toggleShownValue(),
	//	widthValue(),
	//	_2
	//) | rpl::map([](bool shown, int width) {

	//rpl::combine(
	//	toggleShownValue(),
	//	widthValue()
	//) | rpl::map([](bool shown, int width) {
	//	return width;
	//}) | rpl::start_with_next([this](int newWidth) {
	//	_userpic->moveToLeft(
	//		st::infoProfilePhotoLeft,
	//		st::infoProfilePhotoTop,
	//		newWidth);
	//	refreshNameGeometry(newWidth);
	//	refreshStatusGeometry(newWidth);
	//}, lifetime());
}

void Cover::initViewers() {
	//using Flag = Notify::PeerUpdate::Flag;
	//Notify::PeerUpdateValue(
	//	_peer,
	//	Flag::NameChanged
	//) | rpl::start_with_next(
	//	[this] { refreshNameText(); },
	//	lifetime());
	//Notify::PeerUpdateValue(
	//	_peer,
	//	Flag::UserOnlineChanged | Flag::MembersChanged
	//) | rpl::start_with_next(
	//	[this] { refreshStatusText(); },
	//	lifetime());
	//if (!_peer->isUser()) {
	//	Notify::PeerUpdateValue(
	//		_peer,
	//		Flag::ChannelRightsChanged | Flag::ChatCanEdit
	//	) | rpl::start_with_next(
	//		[this] { refreshUploadPhotoOverlay(); },
	//		lifetime());
	//}
	//VerifiedValue(
	//	_peer
	//) | rpl::start_with_next(
	//	[this](bool verified) { setVerified(verified); },
	//	lifetime());
}

void Cover::refreshUploadPhotoOverlay() {
	//_userpic->switchChangePhotoOverlay([&] {
	//	if (auto chat = _peer->asChat()) {
	//		return chat->canEdit();
	//	} else if (auto channel = _peer->asChannel()) {
	//		return channel->canEditInformation();
	//	}
	//	return false;
	//}());
}

void Cover::refreshNameText() {
	_name->setText(_feed->chatsListName());
	refreshNameGeometry(width());
}

void Cover::refreshStatusText() {
	//auto hasMembersLink = [&] {
	//	if (auto megagroup = _peer->asMegagroup()) {
	//		return megagroup->canViewMembers();
	//	}
	//	return false;
	//}();
	//auto statusText = [&] {
	//	auto currentTime = unixtime();
	//	if (auto user = _peer->asUser()) {
	//		const auto result = Data::OnlineTextFull(user, currentTime);
	//		const auto showOnline = Data::OnlineTextActive(user, currentTime);
	//		const auto updateIn = Data::OnlineChangeTimeout(user, currentTime);
	//		if (showOnline) {
	//			_refreshStatusTimer.callOnce(updateIn);
	//		}
	//		return showOnline
	//			? textcmdLink(1, result)
	//			: result;
	//	} else if (auto chat = _peer->asChat()) {
	//		if (!chat->amIn()) {
	//			return lang(lng_chat_status_unaccessible);
	//		}
	//		auto fullCount = std::max(
	//			chat->count,
	//			int(chat->participants.size()));
	//		return ChatStatusText(fullCount, _onlineCount, true);
	//	} else if (auto channel = _peer->asChannel()) {
	//		auto fullCount = qMax(channel->membersCount(), 1);
	//		auto result = ChatStatusText(
	//			fullCount,
	//			_onlineCount,
	//			channel->isMegagroup());
	//		return hasMembersLink ? textcmdLink(1, result) : result;
	//	}
	//	return lang(lng_chat_status_unaccessible);
	//}();
	//_status->setRichText(statusText);
	//if (hasMembersLink) {
	//	_status->setLink(1, std::make_shared<LambdaClickHandler>([=] {
	//		_controller->showSection(Info::Memento(
	//			_controller->peerId(),
	//			Section::Type::Members));
	//	}));
	//}
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
	_name->resizeToNaturalWidth(nameWidth);
	_name->moveToLeft(nameLeft, nameTop, newWidth);
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

} // namespace FeedProfile
} // namespace Info
