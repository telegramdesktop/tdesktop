/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "info/info_profile_inner_widget.h"

#include <rpl/combine.h>
#include "boxes/abstract_box.h"
#include "boxes/add_contact_box.h"
#include "mainwidget.h"
#include "info/info_profile_widget.h"
#include "info/info_profile_lines.h"
#include "window/window_controller.h"
#include "lang/lang_keys.h"
#include "styles/style_info.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"

namespace Info {
namespace Profile {

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<Window::Controller*> controller,
	not_null<PeerData*> peer)
: RpWidget(parent)
, _controller(controller)
, _peer(peer)
, _content(this) {
	setupContent();
}

void InnerWidget::setupContent() {
	auto hideDetails = (_peer->isChat() || _peer->isMegagroup());
	auto cover = _content->add(object_ptr<CoverLine>(this, _peer));
	if (hideDetails) {
		auto hiddenDetailsContent = setupDetailsContent(_content);
		auto hiddenDetails = _content->add(object_ptr<Ui::SlideWrap<>>(
			this,
			std::move(hiddenDetailsContent)));
		cover->setHasToggle(true);
		cover->toggled()
		| rpl::start([=](bool expanded) {
			hiddenDetails->toggleAnimated(expanded);
		}, _lifetime);
		hiddenDetails->hideFast();
	} else {
		_content->add(setupDetailsContent(_content));
	}
	_content->add(object_ptr<BoxContentDivider>(this));

	_content->heightValue()
		| rpl::start([this](int height) {
			TWidget::resizeToWidth(width());
		}, _lifetime);
}

object_ptr<Ui::RpWidget> InnerWidget::setupDetailsContent(
		RpWidget *parent) const {
	auto result = object_ptr<Ui::VerticalLayout>(parent);

	result->add(object_ptr<BoxContentDivider>(result));

	auto skipPadding = QMargins(0, 0, 0, st::infoProfileSkip);
	result->add(object_ptr<Ui::PaddingWrap<>>(result, skipPadding));

	result->add(setupInfoLines(result));
	result->add(setupMuteToggle(result));
	if (auto user = _peer->asUser()) {
		setupMainUserButtons(result, user);
	}

	result->add(object_ptr<Ui::PaddingWrap<>>(result, skipPadding));

	return std::move(result);
}

object_ptr<Ui::RpWidget> InnerWidget::setupMuteToggle(
		RpWidget *parent) const {
	auto result = object_ptr<Ui::VerticalLayout>(parent);
	auto button = result->add(object_ptr<Button>(
		result,
		Lang::Viewer(lng_profile_enable_notifications),
		st::infoNotificationsButton));
	NotificationsEnabledViewer(_peer)
		| rpl::start([button](bool enabled) {
			button->setToggled(enabled);
		}, button->lifetime());
	button->clicks()
		| rpl::start([this](auto) {
			App::main()->updateNotifySetting(
				_peer,
				_peer->isMuted()
					? NotifySettingSetNotify
					: NotifySettingSetMuted);
		}, button->lifetime());

	object_ptr<FloatingIcon>(
		result,
		st::infoIconNotifications);
	return std::move(result);
}

void InnerWidget::setupMainUserButtons(
		Ui::VerticalLayout *wrap,
		not_null<UserData*> user) const {
	auto sendMessage = wrap->add(object_ptr<Ui::SlideWrap<Button>>(
		wrap,
		object_ptr<Button>(
			wrap,
			Lang::Viewer(lng_profile_send_message) | ToUpperValue(),
			st::infoMainButton)));
	_controller->historyPeer.value()
		| rpl::map([user](PeerData *peer) { return peer == user; })
		| rpl::start([sendMessage](bool peerHistoryShown) {
			sendMessage->toggleAnimated(!peerHistoryShown);
		}, sendMessage->lifetime());
	sendMessage->entity()->clicks()
		| rpl::start([this, user](auto&&) {
			_controller->showPeerHistory(
				user,
				Ui::ShowWay::Forward);
		}, sendMessage->lifetime());
	sendMessage->finishAnimations();

	auto addContact = wrap->add(object_ptr<Ui::SlideWrap<Button>>(
		wrap,
		object_ptr<Button>(
			wrap,
			Lang::Viewer(lng_info_add_as_contact) | ToUpperValue(),
			st::infoMainButton)));
	CanAddContactViewer(user)
		| rpl::start([addContact](bool canAdd) {
			addContact->toggleAnimated(canAdd);
		}, addContact->lifetime());
	addContact->finishAnimations();
	addContact->entity()->clicks()
		| rpl::start([user](auto&&) {
			auto firstName = user->firstName;
			auto lastName = user->lastName;
			auto phone = user->phone().isEmpty()
				? App::phoneFromSharedContact(user->bareId())
				: user->phone();
			Ui::show(Box<AddContactBox>(firstName, lastName, phone));
		}, addContact->lifetime());
}

object_ptr<Ui::RpWidget> InnerWidget::setupInfoLines(
		RpWidget *parent) const {
	auto result = object_ptr<Ui::VerticalLayout>(parent);
	auto infoPartsShown = std::vector<rpl::producer<bool>>();
	auto addInfoLine = [&](
			LangKey label,
			rpl::producer<TextWithEntities> &&text,
			bool selectByDoubleClick = false,
			const style::FlatLabel &textSt = st::infoLabeled) {
		auto line = result->add(object_ptr<LabeledLine>(
			result,
			Lang::Viewer(label) | WithEmptyEntities(),
			std::move(text),
			textSt,
			st::infoProfileLabeledPadding,
			selectByDoubleClick));
		infoPartsShown.push_back(line->shownValue());
		return line;
	};
	auto addInfoOneLine = [&](
			LangKey label,
			rpl::producer<TextWithEntities> &&text) {
		addInfoLine(
			label,
			std::move(text),
			true,
			st::infoLabeledOneLine);
	};
	if (auto user = _peer->asUser()) {
		addInfoOneLine(lng_info_mobile_label, PhoneViewer(user));
		addInfoLine(lng_info_bio_label, BioViewer(user));
		addInfoOneLine(lng_info_username_label, UsernameViewer(user));
	} else {
		addInfoOneLine(lng_info_link_label, LinkViewer(_peer));
		addInfoLine(lng_info_about_label, AboutViewer(_peer));
	}
	auto separator = result->add(object_ptr<Ui::SlideWrap<>>(
		result,
		object_ptr<Ui::PlainShadow>(result, st::shadowFg),
		st::infoProfileSeparatorPadding));
	rpl::combine(std::move(infoPartsShown),	[](const auto &values) {
			return base::find(values, true) != values.end();
		})
		| rpl::distinct_until_changed()
		| rpl::start([separator](bool someShown) {
			separator->toggleAnimated(someShown);
		}, separator->lifetime());
	separator->finishAnimations();

	object_ptr<FloatingIcon>(result, st::infoIconInformation);

	return std::move(result);
}

void InnerWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;
}

void InnerWidget::saveState(not_null<Memento*> memento) {
}

void InnerWidget::restoreState(not_null<Memento*> memento) {
}

int InnerWidget::resizeGetHeight(int newWidth) {
	_content->resizeToWidth(newWidth);
	return qMax(_content->height(), _minHeight);
}

} // namespace Profile
} // namespace Info
