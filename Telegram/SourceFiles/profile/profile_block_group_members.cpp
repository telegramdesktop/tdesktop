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
#include "profile/profile_block_group_members.h"

#include "styles/style_profile.h"
#include "ui/widgets/labels.h"
#include "boxes/confirm_box.h"
#include "ui/widgets/popup_menu.h"
#include "mainwidget.h"
#include "apiwrap.h"
#include "observer_peer.h"
#include "auth_session.h"
#include "lang.h"

namespace Profile {

using UpdateFlag = Notify::PeerUpdate::Flag;

GroupMembersWidget::GroupMembersWidget(QWidget *parent, PeerData *peer, TitleVisibility titleVisibility, const style::ProfilePeerListItem &st)
: PeerListWidget(parent
	, peer
	, (titleVisibility == TitleVisibility::Visible) ? lang(lng_profile_participants_section) : QString()
	, st
	, lang(lng_profile_kick)) {
	_updateOnlineTimer.setSingleShot(true);
	connect(&_updateOnlineTimer, SIGNAL(timeout()), this, SLOT(onUpdateOnlineDisplay()));

	auto observeEvents = UpdateFlag::AdminsChanged
		| UpdateFlag::MembersChanged
		| UpdateFlag::UserOnlineChanged;
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(observeEvents, [this](const Notify::PeerUpdate &update) {
		notifyPeerUpdated(update);
	}));

	setRemovedCallback([this, peer](PeerData *selectedPeer) {
		removePeer(selectedPeer);
	});
	setSelectedCallback([this](PeerData *selectedPeer) {
		Ui::showPeerProfile(selectedPeer);
	});
	setUpdateItemCallback([this](Item *item) {
		updateItemStatusText(item);
	});
	setPreloadMoreCallback([this] {
		preloadMore();
	});

	refreshMembers();
}

void GroupMembersWidget::addAdmin(PeerData *selectedPeer) {
	auto user = selectedPeer->asUser();
	auto text = lng_channel_admin_sure(lt_user, user->firstName);
	Ui::show(Box<ConfirmBox>(text, base::lambda_guarded(this, [this, user] {
		Ui::hideLayer();
		if (auto chat = peer()->asChat()) {
			// not supported
		} else if (auto channel = peer()->asMegagroup()) {
			MTP::send(MTPchannels_EditAdmin(channel->inputChannel, user->inputUser, MTP_channelRoleEditor()), rpcDone(base::lambda_guarded(this, [this, channel, user](const MTPUpdates &result) {
				if (App::main()) App::main()->sentUpdatesReceived(result);
				channel->mgInfo->lastAdmins.insert(user);
				channel->setAdminsCount(channel->adminsCount() + 1);
				if (App::main()) emit App::main()->peerUpdated(channel);
				Notify::peerUpdatedDelayed(channel, Notify::PeerUpdate::Flag::AdminsChanged);
			})));
		}
	})));
}

void GroupMembersWidget::removeAdmin(PeerData *selectedPeer) {
	auto user = selectedPeer->asUser();
	auto text = lng_profile_sure_kick_admin(lt_user, user->firstName);
	Ui::show(Box<ConfirmBox>(text, lang(lng_box_remove), base::lambda_guarded(this, [this, user] {
		Ui::hideLayer();
		if (auto chat = peer()->asChat()) {
			// not supported
		} else if (auto channel = peer()->asMegagroup()) {
			MTP::send(MTPchannels_EditAdmin(channel->inputChannel, user->inputUser, MTP_channelRoleEmpty()), rpcDone(base::lambda_guarded(this, [this, channel, user](const MTPUpdates &result) {
				if (App::main()) App::main()->sentUpdatesReceived(result);
				channel->mgInfo->lastAdmins.remove(user);
				if (channel->adminsCount() > 1) {
					channel->setAdminsCount(channel->adminsCount() - 1);
					if (App::main()) emit App::main()->peerUpdated(channel);
				}
				Notify::peerUpdatedDelayed(channel, Notify::PeerUpdate::Flag::AdminsChanged);
			})));
		}
	})));
}

void GroupMembersWidget::removePeer(PeerData *selectedPeer) {
	auto user = selectedPeer->asUser();
	auto text = lng_profile_sure_kick(lt_user, user->firstName);
	Ui::show(Box<ConfirmBox>(text, lang(lng_box_remove), base::lambda_guarded(this, [user, peer = peer()] {
		Ui::hideLayer();
		if (auto chat = peer->asChat()) {
			if (App::main()) App::main()->kickParticipant(chat, user);
		} else if (auto channel = peer->asChannel()) {
			if (App::api()) App::api()->kickParticipant(channel, user);
		}
	})));
}

void GroupMembersWidget::notifyPeerUpdated(const Notify::PeerUpdate &update) {
	if (update.peer != peer()) {
		if (update.flags & UpdateFlag::UserOnlineChanged) {
			if (auto user = update.peer->asUser()) {
				refreshUserOnline(user);
			}
		}
		return;
	}

	if (update.flags & UpdateFlag::MembersChanged) {
		refreshMembers();
		contentSizeUpdated();
	} else if (update.flags & UpdateFlag::AdminsChanged) {
		if (auto chat = peer()->asChat()) {
			for_const (auto item, items()) {
				setItemFlags(getMember(item), chat);
			}
		} else if (auto megagroup = peer()->asMegagroup()) {
			for_const (auto item, items()) {
				setItemFlags(getMember(item), megagroup);
			}
		}
	}
	this->update();
}

void GroupMembersWidget::refreshUserOnline(UserData *user) {
	auto it = _membersByUser.find(user);
	if (it == _membersByUser.cend()) return;

	_now = unixtime();

	auto member = getMember(it.value());
	member->statusHasOnlineColor = !user->botInfo && App::onlineColorUse(user->onlineTill, _now);
	member->onlineTill = user->onlineTill;
	member->onlineForSort = user->isSelf() ? INT_MAX : App::onlineForSort(user, _now);
	member->statusText = QString();

	sortMembers();
	update();
}

void GroupMembersWidget::preloadMore() {
	if (auto megagroup = peer()->asMegagroup()) {
		auto megagroupInfo = megagroup->mgInfo;
		if (!megagroupInfo->lastParticipants.isEmpty() && megagroupInfo->lastParticipants.size() < megagroup->membersCount()) {
			App::api()->requestLastParticipants(megagroup, false);
		}
	}
}

int GroupMembersWidget::resizeGetHeight(int newWidth) {
	if (_limitReachedInfo) {
		int limitReachedInfoWidth = newWidth - getListLeft();
		accumulate_min(limitReachedInfoWidth, st::profileBlockWideWidthMax);

		_limitReachedInfo->resizeToWidth(limitReachedInfoWidth);
		_limitReachedInfo->moveToLeft(getListLeft(), contentTop());
	}
	return PeerListWidget::resizeGetHeight(newWidth);
}

void GroupMembersWidget::paintContents(Painter &p) {
	int left = getListLeft();
	int top = getListTop();
	int memberRowWidth = width() - left;
	accumulate_min(memberRowWidth, st::profileBlockWideWidthMax);
	if (_limitReachedInfo) {
		int infoTop = contentTop();
		int infoHeight = top - infoTop - st::profileLimitReachedSkip;
		paintOutlinedRect(p, left, infoTop, memberRowWidth, infoHeight);
	}

	_now = unixtime();
	PeerListWidget::paintContents(p);
}

Ui::PopupMenu *GroupMembersWidget::fillPeerMenu(PeerData *selectedPeer) {
	if (emptyTitle()) {
		return nullptr;
	}
	auto result = new Ui::PopupMenu(nullptr);
	result->addAction(lang(lng_context_view_profile), [selectedPeer] {
		Ui::showPeerProfile(selectedPeer);
	});
	auto chat = peer()->asChat();
	auto channel = peer()->asMegagroup();
	for_const (auto item, items()) {
		if (item->peer == selectedPeer) {
			auto canRemoveAdmin = [item, chat, channel] {
				if (item->hasAdminStar && !item->peer->isSelf()) {
					if (chat) {
						// Adding of admins from context menu of chat participants
						// is not supported, so the removing is also disabled.
						return false;//chat->amCreator();
					} else if (channel) {
						return channel->amCreator();
					}
				}
				return false;
			};
			if (channel && channel->amCreator() && !item->hasAdminStar) {
				result->addAction(lang(lng_context_promote_admin), base::lambda_guarded(this, [this, selectedPeer] {
					addAdmin(selectedPeer);
				}));
			} else if (canRemoveAdmin()) {
				result->addAction(lang(lng_context_remove_admin), base::lambda_guarded(this, [this, selectedPeer] {
					removeAdmin(selectedPeer);
				}));
			}
			if (item->hasRemoveLink) {
				result->addAction(lang(lng_context_remove_from_group), base::lambda_guarded(this, [this, selectedPeer] {
					removePeer(selectedPeer);
				}));
			}
		}
	}
	return result;
}

void GroupMembersWidget::updateItemStatusText(Item *item) {
	auto member = getMember(item);
	auto user = member->user();
	if (member->statusText.isEmpty() || (member->onlineTextTill <= _now)) {
		if (user->botInfo) {
			bool seesAllMessages = (user->botInfo->readsAllHistory || member->hasAdminStar);
			member->statusText = lang(seesAllMessages ? lng_status_bot_reads_all : lng_status_bot_not_reads_all);
			member->onlineTextTill = _now + 86400;
		} else {
			member->statusHasOnlineColor = App::onlineColorUse(member->onlineTill, _now);
			member->statusText = App::onlineText(member->onlineTill, _now);
			member->onlineTextTill = _now + App::onlineWillChangeIn(member->onlineTill, _now);
		}
	}
	if (_updateOnlineAt <= _now || _updateOnlineAt > member->onlineTextTill) {
		_updateOnlineAt = member->onlineTextTill;
		_updateOnlineTimer.start((_updateOnlineAt - _now + 1) * 1000);
	}
}

int GroupMembersWidget::getListTop() const {
	int result = contentTop();
	if (_limitReachedInfo) {
		result += _limitReachedInfo->height();
		result += st::profileLimitReachedSkip;
	}
	return result;
}

void GroupMembersWidget::refreshMembers() {
	_now = unixtime();
	if (auto chat = peer()->asChat()) {
		checkSelfAdmin(chat);
		if (chat->noParticipantInfo()) {
			App::api()->requestFullPeer(chat);
		}
		fillChatMembers(chat);
		refreshLimitReached();
	} else if (auto megagroup = peer()->asMegagroup()) {
		checkSelfAdmin(megagroup);
		auto megagroupInfo = megagroup->mgInfo;
		if (megagroupInfo->lastParticipants.isEmpty() || megagroup->lastParticipantsCountOutdated()) {
			App::api()->requestLastParticipants(megagroup);
		}
		fillMegagroupMembers(megagroup);
	}
	sortMembers();

	refreshVisibility();
}

void GroupMembersWidget::refreshLimitReached() {
	auto chat = peer()->asChat();
	if (!chat) return;

	bool limitReachedShown = (itemsCount() >= Global::ChatSizeMax()) && chat->amCreator() && !emptyTitle();
	if (limitReachedShown && !_limitReachedInfo) {
		_limitReachedInfo.create(this, st::profileLimitReachedLabel);
		QString title = textRichPrepare(lng_profile_migrate_reached(lt_count, Global::ChatSizeMax()));
		QString body = textRichPrepare(lang(lng_profile_migrate_body));
		QString link = textRichPrepare(lang(lng_profile_migrate_learn_more));
		QString text = qsl("%1%2%3\n%4 [a href=\"https://telegram.org/blog/supergroups5k\"]%5[/a]").arg(textcmdStartSemibold()).arg(title).arg(textcmdStopSemibold()).arg(body).arg(link);
		_limitReachedInfo->setRichText(text);
		_limitReachedInfo->setClickHandlerHook([this](const ClickHandlerPtr &handler, Qt::MouseButton button) {
			Ui::show(Box<ConvertToSupergroupBox>(peer()->asChat()));
			return false;
		});
	} else if (!limitReachedShown && _limitReachedInfo) {
		_limitReachedInfo.destroy();
	}
}

void GroupMembersWidget::checkSelfAdmin(ChatData *chat) {
	if (chat->participants.isEmpty()) return;

	auto self = App::self();
	if (chat->amAdmin() && !chat->admins.contains(self)) {
		chat->admins.insert(self);
	} else if (!chat->amAdmin() && chat->admins.contains(self)) {
		chat->admins.remove(self);
	}
}

void GroupMembersWidget::checkSelfAdmin(ChannelData *megagroup) {
	if (megagroup->mgInfo->lastParticipants.isEmpty()) return;

	bool amAdmin = (megagroup->amCreator() || megagroup->amEditor());
	auto self = App::self();
	if (amAdmin && !megagroup->mgInfo->lastAdmins.contains(self)) {
		megagroup->mgInfo->lastAdmins.insert(self);
	} else if (!amAdmin && megagroup->mgInfo->lastAdmins.contains(self)) {
		megagroup->mgInfo->lastAdmins.remove(self);
	}
}

void GroupMembersWidget::sortMembers() {
	if (!_sortByOnline || !itemsCount()) return;

	sortItems([this](Item *a, Item *b) {
		return getMember(a)->onlineForSort > getMember(b)->onlineForSort;
	});

	updateOnlineCount();
}

void GroupMembersWidget::updateOnlineCount() {
	bool onlyMe = true;
	int newOnlineCount = 0;
	for_const (auto item, items()) {
		auto member = getMember(item);
		auto user = member->user();
		auto isOnline = !user->botInfo && App::onlineColorUse(member->onlineTill, _now);
		if (member->statusHasOnlineColor != isOnline) {
			member->statusHasOnlineColor = isOnline;
			member->statusText = QString();
		}
		if (member->statusHasOnlineColor) {
			++newOnlineCount;
			if (!user->isSelf()) {
				onlyMe = false;
			}
		}
	}
	if (newOnlineCount == 1 && onlyMe) {
		newOnlineCount = 0;
	}
	if (_onlineCount != newOnlineCount) {
		_onlineCount = newOnlineCount;
		emit onlineCountUpdated(_onlineCount);
	}
}

GroupMembersWidget::Member *GroupMembersWidget::addUser(ChatData *chat, UserData *user) {
	auto member = computeMember(user);
	setItemFlags(member, chat);
	addItem(member);
	return member;
}

void GroupMembersWidget::fillChatMembers(ChatData *chat) {
	if (chat->participants.isEmpty()) return;

	clearItems();
	if (!chat->amIn()) return;

	_sortByOnline = true;

	reserveItemsForSize(chat->participants.size());
	addUser(chat, App::self())->onlineForSort = INT_MAX; // Put me on the first place.
	for (auto i = chat->participants.cbegin(), e = chat->participants.cend(); i != e; ++i) {
		auto user = i.key();
		if (!user->isSelf()) {
			addUser(chat, user);
		}
	}
}

void GroupMembersWidget::setItemFlags(Item *item, ChatData *chat) {
	auto user = getMember(item)->user();
	auto isCreator = (peerFromUser(chat->creator) == item->peer->id);
	auto isAdmin = chat->admins.contains(user);
	item->hasAdminStar = isCreator || isAdmin;
	if (item->peer->id == AuthSession::CurrentUserPeerId()) {
		item->hasRemoveLink = false;
	} else if (chat->amCreator() || (chat->amAdmin() && !item->hasAdminStar)) {
		item->hasRemoveLink = true;
	} else {
		item->hasRemoveLink = chat->invitedByMe.contains(user);
	}
}

GroupMembersWidget::Member *GroupMembersWidget::addUser(ChannelData *megagroup, UserData *user) {
	auto member = computeMember(user);
	setItemFlags(member, megagroup);
	addItem(member);
	return member;
}

void GroupMembersWidget::fillMegagroupMembers(ChannelData *megagroup) {
	t_assert(megagroup->mgInfo != nullptr);
	if (megagroup->mgInfo->lastParticipants.isEmpty()) return;

	if (!megagroup->canViewMembers()) {
		clearItems();
		return;
	}

	_sortByOnline = (megagroup->membersCount() > 0 && megagroup->membersCount() <= Global::ChatSizeMax());

	auto &membersList = megagroup->mgInfo->lastParticipants;
	if (_sortByOnline) {
		clearItems();
		reserveItemsForSize(membersList.size());
		if (megagroup->amIn()) {
			addUser(megagroup, App::self())->onlineForSort = INT_MAX;
		}
	} else if (membersList.size() >= itemsCount()) {
		if (addUsersToEnd(megagroup)) {
			return;
		}
	}
	if (!_sortByOnline) {
		clearItems();
		reserveItemsForSize(membersList.size());
	}
	for_const (auto user, membersList) {
		if (!_sortByOnline || !user->isSelf()) {
			addUser(megagroup, user);
		}
	}
}

bool GroupMembersWidget::addUsersToEnd(ChannelData *megagroup) {
	auto &membersList = megagroup->mgInfo->lastParticipants;
	auto &itemsList = items();
	for (int i = 0, count = itemsList.size(); i < count; ++i) {
		if (itemsList[i]->peer != membersList.at(i)) {
			return false;
		}
	}
	reserveItemsForSize(membersList.size());
	for (int i = itemsCount(), count = membersList.size(); i < count; ++i) {
		addUser(megagroup, membersList.at(i));
	}
	return true;
}

void GroupMembersWidget::setItemFlags(Item *item, ChannelData *megagroup) {
	auto amCreatorOrAdmin = (item->peer->id == AuthSession::CurrentUserPeerId()) && (megagroup->amCreator() || megagroup->amEditor());
	auto isAdmin = megagroup->mgInfo->lastAdmins.contains(getMember(item)->user());
	item->hasAdminStar = amCreatorOrAdmin || isAdmin;
	if (item->peer->isSelf()) {
		item->hasRemoveLink = false;
	} else if (megagroup->amCreator() || (megagroup->amEditor() && !item->hasAdminStar)) {
		item->hasRemoveLink = true;
	} else {
		item->hasRemoveLink = false;
	}
}

GroupMembersWidget::Member *GroupMembersWidget::computeMember(UserData *user) {
	auto it = _membersByUser.constFind(user);
	if (it == _membersByUser.cend()) {
		auto member = new Member(user);
		it = _membersByUser.insert(user, member);
		member->statusHasOnlineColor = !user->botInfo && App::onlineColorUse(user->onlineTill, _now);
		member->onlineTill = user->onlineTill;
		member->onlineForSort = App::onlineForSort(user, _now);
	}
	return it.value();
}

void GroupMembersWidget::onUpdateOnlineDisplay() {
	if (_sortByOnline) {
		_now = unixtime();

		bool changed = false;
		for_const (auto item, items()) {
			if (!item->statusHasOnlineColor) {
				if (!item->peer->isSelf()) {
					continue;
				} else {
					break;
				}
			}
			auto member = getMember(item);
			bool isOnline = !member->user()->botInfo && App::onlineColorUse(member->onlineTill, _now);
			if (!isOnline) {
				changed = true;
			}
		}
		if (changed) {
			updateOnlineCount();
		}
	}
	update();
}

GroupMembersWidget::~GroupMembersWidget() {
	auto members = base::take(_membersByUser);
	for_const (auto member, members) {
		delete member;
	}
}

} // namespace Profile
