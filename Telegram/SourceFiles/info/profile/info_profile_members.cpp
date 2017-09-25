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
#include "info/profile/info_profile_members.h"

#include <rpl/combine.h>
#include "info/profile/info_profile_values.h"
#include "info/profile/info_profile_icon.h"
#include "info/profile/info_profile_values.h"
#include "info/info_memento.h"
#include "profile/profile_block_group_members.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "styles/style_info.h"
#include "lang/lang_keys.h"
#include "boxes/confirm_box.h"
#include "boxes/peer_list_controllers.h"

namespace Info {
namespace Profile {
namespace {

constexpr auto kEnableSearchMembersAfterCount = 50;

} // namespace

Members::Members(
	QWidget *parent,
	rpl::producer<Wrap> &&wrapValue,
	not_null<PeerData*> peer)
: RpWidget(parent)
, _peer(peer)
, _labelWrap(this)
, _label(setupHeader())
, _addMember(this, st::infoMembersAddMember)
, _searchField(
	this,
	st::infoMembersSearchField,
	langFactory(lng_participant_filter))
, _search(this, st::infoMembersSearch)
, _cancelSearch(this, st::infoMembersCancelSearch)
, _list(setupList(this)) {
	setupButtons();
	std::move(wrapValue)
		| rpl::start([this](Wrap wrap) {
			_wrap = wrap;
			updateSearchOverrides();
		}, lifetime());
}

object_ptr<Ui::FlatLabel> Members::setupHeader() {
	auto result = object_ptr<Ui::FlatLabel>(
		_labelWrap,
		MembersCountValue(_peer)
			| rpl::map([](int count) {
				return lng_chat_status_members(lt_count, count);
			})
			| ToUpperValue(),
		st::infoBlockHeaderLabel);
	result->setAttribute(Qt::WA_TransparentForMouseEvents);
	return result;
}

void Members::setupButtons() {
	using namespace rpl::mappers;

	_searchField->hide();
	_cancelSearch->hideFast();

	auto addMemberShown = CanAddMemberValue(_peer)
		| rpl::start_spawning(lifetime());
	widthValue()
		| rpl::start([button = _addMember.data()](int newWidth) {
			button->moveToRight(
				st::infoMembersButtonPosition.x(),
				st::infoMembersButtonPosition.y(),
				newWidth);
		}, _addMember->lifetime());
	_addMember->showOn(rpl::duplicate(addMemberShown));
	_addMember->clicks() // TODO throttle(ripple duration)
		| rpl::start([this](auto&&) {
			this->addMember();
		}, _addMember->lifetime());

	auto searchShown = MembersCountValue(_peer)
		| rpl::map($1 >= kEnableSearchMembersAfterCount)
		| rpl::distinct_until_changed()
		| rpl::start_spawning(lifetime());
	_search->showOn(rpl::duplicate(searchShown));
	_search->clicks()
		| rpl::start([this](auto&&) {
			this->showSearch();
		}, _search->lifetime());
	_cancelSearch->clicks()
		| rpl::start([this](auto&&) {
			this->cancelSearch();
		}, _cancelSearch->lifetime());

	rpl::combine(
		std::move(addMemberShown),
		std::move(searchShown))
		| rpl::start([this](auto&&) {
			this->resizeToWidth(width());
		}, lifetime());

	object_ptr<FloatingIcon>(
		this,
		st::infoIconMembers,
		st::infoMembersIconPosition)->lower();
}

object_ptr<Members::ListWidget> Members::setupList(
		RpWidget *parent) const {
	auto result = object_ptr<ListWidget>(
		parent,
		_peer,
		::Profile::GroupMembersWidget::TitleVisibility::Hidden,
		st::infoMembersItem);
	result->moveToLeft(0, st::infoMembersHeader);
	parent->widthValue()
		| rpl::start([list = result.data()](int newWidth) {
			list->resizeToWidth(newWidth);
		}, result->lifetime());
	result->heightValue()
		| rpl::start([parent](int listHeight) {
			auto newHeight = (listHeight > 0)
				? (st::infoMembersHeader + listHeight)
				: 0;
			parent->resize(parent->width(), newHeight);
		}, result->lifetime());
	return result;
}

int Members::resizeGetHeight(int newWidth) {
	auto availableWidth = newWidth
		- st::infoMembersButtonPosition.x();
	if (!_addMember->isHidden()) {
		availableWidth -= st::infoMembersAddMember.width;
	}

	auto cancelLeft = availableWidth - _cancelSearch->width();
	_cancelSearch->moveToLeft(
		cancelLeft,
		st::infoMembersButtonPosition.y());

	auto searchShownLeft = st::infoMembersIconPosition.x()
		- st::infoMembersSearch.iconPosition.x();
	auto searchHiddenLeft = availableWidth - _search->width();
	auto searchShown = _searchShownAnimation.current(_searchShown ? 1. : 0.);
	auto searchCurrentLeft = anim::interpolate(
		searchHiddenLeft,
		searchShownLeft,
		searchShown);
	_search->moveToLeft(
		searchCurrentLeft,
		st::infoMembersButtonPosition.y());

	auto fieldLeft = anim::interpolate(
		cancelLeft,
		st::infoBlockHeaderPosition.x(),
		searchShown);
	_searchField->setGeometryToLeft(
		fieldLeft,
		st::infoMembersSearchTop,
		cancelLeft - fieldLeft,
		_searchField->height());

	_labelWrap->resize(
		searchCurrentLeft - st::infoBlockHeaderPosition.x(),
		_label->height());
	_labelWrap->moveToLeft(
		st::infoBlockHeaderPosition.x(),
		st::infoBlockHeaderPosition.y(),
		newWidth);

	_label->resizeToWidth(searchHiddenLeft);
	_label->moveToLeft(0, 0);

	return heightNoMargins();
}

void Members::addMember() {
	if (auto chat = _peer->asChat()) {
		if (chat->count >= Global::ChatSizeMax() && chat->amCreator()) {
			Ui::show(Box<ConvertToSupergroupBox>(chat));
		} else {
			AddParticipantsBoxController::Start(chat);
		}
	} else if (auto channel = _peer->asChannel()) {
		if (channel->mgInfo) {
			auto &participants = channel->mgInfo->lastParticipants;
			AddParticipantsBoxController::Start(channel, { participants.cbegin(), participants.cend() });
		}
	}
}

void Members::showSearch() {
	if (!_searchShown) {
		toggleSearch();
	}
}

void Members::toggleSearch() {
	_searchShown = !_searchShown;
	_cancelSearch->toggleAnimated(_searchShown);
	_searchShownAnimation.start(
		[this] { searchAnimationCallback(); },
		_searchShown ? 0. : 1.,
		_searchShown ? 1. : 0.,
		st::slideWrapDuration);
	_search->setDisabled(_searchShown);
	if (_searchShown) {
		_searchField->show();
		_searchField->setFocus();
	} else {
		setFocus();
	}
}

void Members::searchAnimationCallback() {
	if (!_searchShownAnimation.animating()) {
		_searchField->setVisible(_searchShown);
		updateSearchOverrides();
		_search->setPointerCursor(!_searchShown);
	}
	resizeToWidth(width());
}

void Members::updateSearchOverrides() {
	auto iconOverride = !_searchShown
		? nullptr
		: (_wrap == Wrap::Layer)
		? &st::infoMembersSearchActiveLayer
		: &st::infoMembersSearchActive;
	_search->setIconOverride(iconOverride, iconOverride);
}

void Members::cancelSearch() {
	if (_searchShown) {
		if (!_searchField->getLastText().isEmpty()) {
			_searchField->setText(QString());
			_searchField->updatePlaceholder();
			_searchField->setFocus();
			applySearch();
		} else {
			toggleSearch();
		}
	}
}

void Members::applySearch() {

}

void Members::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	setChildVisibleTopBottom(_list, visibleTop, visibleBottom);
}

} // namespace Profile
} // namespace Info

