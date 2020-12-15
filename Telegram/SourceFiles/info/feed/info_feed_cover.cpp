/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/feed/info_feed_cover.h"

#include "data/data_feed.h"
#include "data/data_session.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "lang/lang_keys.h"
#include "ui/widgets/labels.h"
#include "ui/special_buttons.h"
#include "auth_session.h"
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
, _userpic(
	this,
	controller->parentController(),
	_feed,
	st::infoFeedProfilePhoto)
, _name(this, st::infoProfileNameLabel)
, _status(
		this,
		st::infoProfileMegagroupStatusLabel) {
	_userpic->setPointerCursor(false);
	_name->setSelectable(true);
	_name->setContextCopyText(lang(lng_profile_copy_fullname));
	refreshNameText();
	refreshStatusText();

	initViewers();
	setupChildGeometry();
}

void Cover::setupChildGeometry() {
	widthValue(
	) | rpl::start_with_next([=](int newWidth) {
		_userpic->moveToLeft(
			st::infoProfilePhotoLeft,
			st::infoProfilePhotoTop,
			newWidth);
		refreshNameGeometry(newWidth);
		refreshStatusGeometry(newWidth);
	}, lifetime());
}

void Cover::initViewers() {
	Auth().data().feedUpdated(
	) | rpl::filter([](const Data::FeedUpdate &update) {
		return (update.flag == Data::FeedUpdateFlag::Channels);
	}) | rpl::start_with_next(
		[=] { refreshStatusText(); },
		lifetime());
}

void Cover::refreshNameText() {
	_name->setText(_feed->chatListName());
	refreshNameGeometry(width());
}

void Cover::refreshStatusText() {
	const auto statusText = [&] {
		if (!_feed->channelsLoaded() || _feed->channels().empty()) {
			return QString();
		}
		return lng_feed_channels(lt_count, _feed->channels().size());
	}();
	_status->setRichText(textcmdLink(1, statusText));
	_status->setLink(1, std::make_shared<LambdaClickHandler>([=] {
		_controller->showSection(std::make_unique<Info::Memento>(
			_feed,
			Section::Type::Channels));
	}));
	refreshStatusGeometry(width());
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

Cover::~Cover() = default;

} // namespace FeedProfile
} // namespace Info
