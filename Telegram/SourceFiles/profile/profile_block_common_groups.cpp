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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "profile/profile_block_common_groups.h"

#include "profile/profile_section_memento.h"
#include "styles/style_widgets.h"
#include "observer_peer.h"
#include "apiwrap.h"
#include "lang.h"

namespace Profile {
namespace {

constexpr int kCommonGroupsPerPage = 20;

} // namespace

CommonGroupsWidget::CommonGroupsWidget(QWidget *parent, PeerData *peer)
: PeerListWidget(parent, peer, lang(lng_profile_common_groups_section)) {
	refreshVisibility();

	auto observeEvents = Notify::PeerUpdate::Flag::MembersChanged;
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(observeEvents, [this](const Notify::PeerUpdate &update) {
		notifyPeerUpdated(update);
	}));

	setSelectedCallback([this](PeerData *selectedPeer) {
		Ui::showPeerHistory(selectedPeer, ShowAtUnreadMsgId, Ui::ShowWay::Forward);
	});
	setPreloadMoreCallback([this] {
		preloadMore();
	});
}

void CommonGroupsWidget::notifyPeerUpdated(const Notify::PeerUpdate &update) {
	for_const (auto item, items()) {
		if (item->peer == update.peer) {
			updateStatusText(item);
			this->update();
			return;
		}
	}
}

int CommonGroupsWidget::resizeGetHeight(int newWidth) {
	auto result = PeerListWidget::resizeGetHeight(newWidth);
	return qRound(_height.current(result));
}

void CommonGroupsWidget::paintContents(Painter &p) {
	_height.animating(getms());
	return PeerListWidget::paintContents(p);
}

void CommonGroupsWidget::saveState(SectionMemento *memento) const {
	if (auto count = itemsCount()) {
		QList<PeerData*> groups;
		groups.reserve(count);
		for_const (auto item, items()) {
			groups.push_back(item->peer);
		}
		memento->setCommonGroups(groups);
	}
}

void CommonGroupsWidget::restoreState(const SectionMemento *memento) {
	CommonGroupsEvent event;
	event.groups = memento->getCommonGroups();
	if (!event.groups.empty()) {
		onShowCommonGroups(event);
	}
}

void CommonGroupsWidget::onShowCommonGroups(const CommonGroupsEvent &event) {
	for_const (auto group, event.groups) {
		addItem(computeItem(group));
		_preloadGroupId = group->bareId();
	}
	refreshVisibility();
	if (event.initialHeight >= 0) {
		_height.start([this] { contentSizeUpdated(); }, event.initialHeight, resizeGetHeight(width()), st::widgetSlideDuration);
	}
	contentSizeUpdated();
	update();
}

void CommonGroupsWidget::preloadMore() {
	if (_preloadRequestId || !_preloadGroupId) {
		return;
	}
	auto user = peer()->asUser();
	t_assert(user != nullptr);
	auto request = MTPmessages_GetCommonChats(user->inputUser, MTP_int(_preloadGroupId), MTP_int(kCommonGroupsPerPage));
	_preloadRequestId = MTP::send(request, ::rpcDone(base::lambda_guarded(this, [this](const MTPmessages_Chats &result) {
		_preloadRequestId = 0;
		_preloadGroupId = 0;

		if (auto chats = Api::getChatsFromMessagesChats(result)) {
			auto &list = chats->c_vector().v;
			if (!list.empty()) {
				reserveItemsForSize(itemsCount() + list.size());
				for_const (auto &chatData, list) {
					if (auto chat = App::feedChat(chatData)) {
						addItem(computeItem(chat));
						_preloadGroupId = chat->bareId();
					}
				}
				contentSizeUpdated();
			}
		}
	})));
}

void CommonGroupsWidget::updateStatusText(Item *item) {
	auto group = item->peer;
	if (auto chat = group->asChat()) {
		auto count = qMax(chat->count, chat->participants.size());
		item->statusText = count ? lng_chat_status_members(lt_count, count) : lang(lng_group_status);
	} else if (auto megagroup = group->asMegagroup()) {
		auto count = megagroup->membersCount();
		item->statusText = (count > 0) ? lng_chat_status_members(lt_count, count) : lang(lng_group_status);

		// Request members count.
		if (!megagroup->wasFullUpdated()) App::api()->requestFullPeer(megagroup);
	} else if (auto channel = group->asChannel()) {
		auto count = channel->membersCount();
		item->statusText = (count > 0) ? lng_chat_status_members(lt_count, count) : lang(lng_channel_status);

		// Request members count.
		if (!channel->wasFullUpdated()) App::api()->requestFullPeer(channel);
	} else {
		t_assert(!"Users should not get to CommonGroupsWidget::updateStatusText()");
	}
}

CommonGroupsWidget::Item *CommonGroupsWidget::computeItem(PeerData *group) {
	// Skip groups that migrated to supergroups.
	if (group->migrateTo()) {
		return nullptr;
	}

	auto it = _dataMap.constFind(group);
	if (it == _dataMap.cend()) {
		it = _dataMap.insert(group, new Item(group));
		updateStatusText(it.value());
	}
	return it.value();
}

CommonGroupsWidget::~CommonGroupsWidget() {
	for (auto item : base::take(_dataMap)) {
		delete item;
	}
}

} // namespace Profile
