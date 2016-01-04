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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"

#include "lang.h"
#include "window.h"
#include "mainwidget.h"
#include "profilewidget.h"
#include "boxes/addcontactbox.h"
#include "boxes/confirmbox.h"
#include "boxes/photocropbox.h"
#include "application.h"
#include "boxes/contactsbox.h"
#include "gui/filedialog.h"

ProfileInner::ProfileInner(ProfileWidget *profile, ScrollArea *scroll, PeerData *peer) : TWidget(0)
, _profile(profile)
, _scroll(scroll)
, _peer(peer->migrateTo() ? peer->migrateTo() : peer)
, _peerUser(_peer->asUser())
, _peerChat(_peer->asChat())
, _peerChannel(_peer->asChannel())
, _migrated(_peer->migrateFrom() ? App::history(_peer->migrateFrom()->id) : 0)
, _history(App::history(_peer->id))
, _amCreator(_peerChat ? _peerChat->amCreator() : (_peerChannel ? _peerChannel->amCreator() : false))

, _width(0)
, _left(0)
, _addToHeight(0)

// profile
, _nameCache(_peer->name)
, _uploadPhoto(this, lang(lng_profile_set_group_photo), st::btnShareContact)
, _addParticipant(this, lang(lng_profile_add_participant), st::btnShareContact)
, _sendMessage(this, lang(lng_profile_send_message), st::btnShareContact)
, _shareContact(this, lang(lng_profile_share_contact), st::btnShareContact)
, _inviteToGroup(this, lang(lng_profile_invite_to_group), st::btnShareContact)
, _cancelPhoto(this, lang(lng_cancel))
, _createInvitationLink(this, lang(lng_group_invite_create))
, _invitationLink(this, qsl("telegram.me/joinchat/"))
, _botSettings(this, lang(lng_profile_bot_settings))
, _botHelp(this, lang(lng_profile_bot_help))
, _username(this, (_peerChannel && _peerChannel->isPublic()) ? (qsl("telegram.me/") + _peerChannel->username) : lang(lng_profile_create_public_link))
, _members(this, lng_channel_members_link(lt_count, (_peerChannel && _peerChannel->count > 0) ? _peerChannel->count : 1))
, _admins(this, lng_channel_admins_link(lt_count, (_peerChannel ? (_peerChannel->adminsCount > 0 ? _peerChannel->adminsCount : 1) : ((_peerChat && _peerChat->adminsEnabled()) ? (_peerChat->admins.size() + 1) : 0))))

// about
, _about(st::wndMinWidth - st::profilePadding.left() - st::profilePadding.right())
, _aboutTop(0)
, _aboutHeight(0)

, a_photoOver(0)
, _a_photo(animation(this, &ProfileInner::step_photo))
, _photoOver(false)

// migrate to megagroup
, _showMigrate(_peerChat && _amCreator && !_peerChat->isMigrated() && _peerChat->count >= cMaxGroupCount())
, _forceShowMigrate(false)
, _aboutMigrate(st::normalFont, lang(lng_profile_migrate_about), _defaultOptions, st::wndMinWidth - st::profilePadding.left() - st::profilePadding.right())
, _migrate(this, lang(lng_profile_migrate_button), st::btnMigrateToMega)

// settings
, _enableNotifications(this, lang(lng_profile_enable_notifications))

// shared media
, _notAllMediaLoaded(false)

// actions
, _searchInPeer(this, lang(lng_profile_search_messages))
, _clearHistory(this, lang(lng_profile_clear_history))
, _deleteConversation(this, lang(_peer->isUser() ? lng_profile_delete_conversation : (_peer->isChat() ? lng_profile_clear_and_exit : (_peer->isMegagroup() ? lng_profile_leave_group : lng_profile_leave_channel))))
, _wasBlocked(_peerUser ? _peerUser->blocked : UserBlockUnknown)
, _blockRequest(0)
, _blockUser(this, lang((_peerUser && _peerUser->botInfo) ? lng_profile_block_bot : lng_profile_block_user), st::btnRedLink)
, _deleteChannel(this, lang(_peer->isMegagroup() ? lng_profile_delete_group : lng_profile_delete_channel), st::btnRedLink)

// participants
, _pHeight(st::profileListPhotoSize + st::profileListPadding.height() * 2)
, _kickWidth(st::linkFont->width(lang(lng_profile_kick)))
, _selectedRow(-1)
, _lastPreload(0)
, _contactId(0)
, _kickOver(0)
, _kickDown(0)
, _kickConfirm(0)
	
, _menu(0) {
	connect(App::wnd(), SIGNAL(imageLoaded()), this, SLOT(update()));

	connect(App::api(), SIGNAL(fullPeerUpdated(PeerData*)), this, SLOT(onFullPeerUpdated(PeerData*)));

	if (_peerUser) {
		if (_peerUser->blocked == UserIsBlocked) {
			_blockUser.setText(lang(_peerUser->botInfo ? lng_profile_unblock_bot : lng_profile_unblock_user));
		}
		_phoneText = App::formatPhone(_peerUser->phone.isEmpty() ? App::phoneFromSharedContact(peerToUser(_peerUser->id)) : _peerUser->phone);
		PhotoData *userPhoto = (_peerUser->photoId && _peerUser->photoId != UnknownPeerPhotoId) ? App::photo(_peerUser->photoId) : 0;
		if (userPhoto && userPhoto->date) {
			_photoLink = TextLinkPtr(new PhotoLink(userPhoto, _peer));
		}
		if ((_peerUser->botInfo && !_peerUser->botInfo->inited) || (_peerUser->photoId == UnknownPeerPhotoId) || (_peerUser->photoId && !userPhoto->date) || (_peerUser->blocked == UserBlockUnknown)) {
			if (App::api()) App::api()->requestFullPeer(_peer);
		}
	} else if (_peerChat) {
		PhotoData *chatPhoto = (_peerChat->photoId && _peerChat->photoId != UnknownPeerPhotoId) ? App::photo(_peerChat->photoId) : 0;
		if (chatPhoto && chatPhoto->date) {
			_photoLink = TextLinkPtr(new PhotoLink(chatPhoto, _peer));
		}
		if (_peerChat->photoId == UnknownPeerPhotoId) {
			if (App::api()) App::api()->requestFullPeer(_peer);
		}
	} else if (_peerChannel) {
		PhotoData *chatPhoto = (_peerChannel->photoId && _peerChannel->photoId != UnknownPeerPhotoId) ? App::photo(_peerChannel->photoId) : 0;
		if (chatPhoto && chatPhoto->date) {
			_photoLink = TextLinkPtr(new PhotoLink(chatPhoto, _peer));
		}
		bool needAdmins = (_peerChannel->isMegagroup() && _peerChannel->amEditor()), adminsOutdated = (_peerChannel->isMegagroup() && (_peerChannel->mgInfo->lastParticipantsStatus & MegagroupInfo::LastParticipantsAdminsOutdated));
		if (_peerChannel->isMegagroup() && (_peerChannel->mgInfo->lastParticipants.isEmpty() || (needAdmins && adminsOutdated) || _peerChannel->lastParticipantsCountOutdated())) {
			if (App::api()) App::api()->requestLastParticipants(_peerChannel);
		}
		_peerChannel->updateFull();
	}

	// profile
	_nameText.setText(st::profileNameFont, _nameCache, _textNameOptions);
	connect(&_uploadPhoto, SIGNAL(clicked()), this, SLOT(onUpdatePhoto()));
	connect(&_addParticipant, SIGNAL(clicked()), this, SLOT(onAddParticipant()));
	connect(&_sendMessage, SIGNAL(clicked()), this, SLOT(onSendMessage()));
	connect(&_shareContact, SIGNAL(clicked()), this, SLOT(onShareContact()));
	connect(&_inviteToGroup, SIGNAL(clicked()), this, SLOT(onInviteToGroup()));
	connect(&_cancelPhoto, SIGNAL(clicked()), this, SLOT(onUpdatePhotoCancel()));
	connect(&_createInvitationLink, SIGNAL(clicked()), this, SLOT(onCreateInvitationLink()));
	connect(&_invitationLink, SIGNAL(clicked()), this, SLOT(onInvitationLink()));
	connect(&_username, SIGNAL(clicked()), this, SLOT(onPublicLink()));
	connect(&_members, SIGNAL(clicked()), this, SLOT(onMembers()));
	connect(&_admins, SIGNAL(clicked()), this, SLOT(onAdmins()));
	_invitationLink.setAcceptBoth(true);
	_username.setAcceptBoth(true);
	updateInvitationLink();

	if (_peerChat) {
		QString maxStr = lang(_uploadPhoto.textWidth() > _addParticipant.textWidth() ? lng_profile_set_group_photo : lng_profile_add_participant);
		_uploadPhoto.setAutoFontSize(st::profileMinBtnPadding, maxStr);
		_addParticipant.setAutoFontSize(st::profileMinBtnPadding, maxStr);
	} else if (_peerUser) {
		QString maxStr;
		if (_peerUser->botInfo && !_peerUser->botInfo->cantJoinGroups) {
			maxStr = lang(_sendMessage.textWidth() > _inviteToGroup.textWidth() ? lng_profile_send_message : lng_profile_invite_to_group);
		} else if (!_peerUser->phone.isEmpty()) {
			maxStr = lang(_sendMessage.textWidth() > _shareContact.textWidth() ? lng_profile_send_message : lng_profile_share_contact);
		} else {
			maxStr = lang(lng_profile_send_message);
		}
		_sendMessage.setAutoFontSize(st::profileMinBtnPadding, maxStr);
		_shareContact.setAutoFontSize(st::profileMinBtnPadding, maxStr);
		_inviteToGroup.setAutoFontSize(st::profileMinBtnPadding, maxStr);
	} else if (_peerChannel && _amCreator) {
		_uploadPhoto.setAutoFontSize(st::profileMinBtnPadding, lang(lng_profile_set_group_photo));
	}

	connect(&_botSettings, SIGNAL(clicked()), this, SLOT(onBotSettings()));
	connect(&_botHelp, SIGNAL(clicked()), this, SLOT(onBotHelp()));

	connect(App::app(), SIGNAL(peerPhotoDone(PeerId)), this, SLOT(onPhotoUpdateDone(PeerId)));
	connect(App::app(), SIGNAL(peerPhotoFail(PeerId)), this, SLOT(onPhotoUpdateFail(PeerId)));

	connect(App::main(), SIGNAL(peerPhotoChanged(PeerData *)), this, SLOT(peerUpdated(PeerData *)));
	connect(App::main(), SIGNAL(peerUpdated(PeerData *)), this, SLOT(peerUpdated(PeerData *)));
	connect(App::main(), SIGNAL(peerNameChanged(PeerData *, const PeerData::Names &, const PeerData::NameFirstChars &)), this, SLOT(peerUpdated(PeerData *)));

	// about
	if (_peerUser && _peerUser->botInfo) {
		if (!_peerUser->botInfo->shareText.isEmpty()) {
			_about.setText(st::linkFont, _peerUser->botInfo->shareText, _historyBotNoMonoOptions);
		}
		updateBotLinksVisibility();
	} else {
		if (_peerChannel && !_peerChannel->about.isEmpty()) {
			_about.setText(st::linkFont, _peerChannel->about, _historyTextNoMonoOptions);
		}
		_botSettings.hide();
		_botHelp.hide();
	}

	// migrate to megagroup
	connect(&_migrate, SIGNAL(clicked()), this, SLOT(onMigrate()));

	// settings
	connect(&_enableNotifications, SIGNAL(clicked()), this, SLOT(onEnableNotifications()));

	// shared media
	connect((_mediaButtons[OverviewPhotos] = new LinkButton(this, QString())), SIGNAL(clicked()), this, SLOT(onMediaPhotos()));
	connect((_mediaButtons[OverviewVideos] = new LinkButton(this, QString())), SIGNAL(clicked()), this, SLOT(onMediaVideos()));
	connect((_mediaButtons[OverviewAudioDocuments] = new LinkButton(this, QString())), SIGNAL(clicked()), this, SLOT(onMediaSongs()));
	connect((_mediaButtons[OverviewDocuments] = new LinkButton(this, QString())), SIGNAL(clicked()), this, SLOT(onMediaDocuments()));
	connect((_mediaButtons[OverviewAudios] = new LinkButton(this, QString())), SIGNAL(clicked()), this, SLOT(onMediaAudios()));
	connect((_mediaButtons[OverviewLinks] = new LinkButton(this, QString())), SIGNAL(clicked()), this, SLOT(onMediaLinks()));
	updateMediaLinks();

	// actions
	connect(&_searchInPeer, SIGNAL(clicked()), this, SLOT(onSearchInPeer()));
	connect(&_clearHistory, SIGNAL(clicked()), this, SLOT(onClearHistory()));
	connect(&_deleteConversation, SIGNAL(clicked()), this, SLOT(onDeleteConversation()));
	connect(&_blockUser, SIGNAL(clicked()), this, SLOT(onBlockUser()));
	connect(&_deleteChannel, SIGNAL(clicked()), this, SLOT(onDeleteChannel()));

	App::contextItem(0);

	resizeEvent(0);
	showAll();
}

void ProfileInner::onShareContact() {
	App::main()->shareContactLayer(_peerUser);
}

void ProfileInner::onInviteToGroup() {
	Ui::showLayer(new ContactsBox(_peerUser));
}

void ProfileInner::onSendMessage() {
	Ui::showPeerHistory(_peer, ShowAtUnreadMsgId);
}

void ProfileInner::onSearchInPeer() {
	App::main()->searchInPeer(_peer);
}

void ProfileInner::onEnableNotifications() {
	App::main()->updateNotifySetting(_peer, _enableNotifications.checked());
}

void ProfileInner::saveError(const QString &str) {
	_errorText = str;
	resizeEvent(0);
	showAll();
	update();
}

void ProfileInner::loadProfilePhotos(int32 yFrom) {
	_lastPreload = yFrom;

	int32 yTo = yFrom + (parentWidget() ? parentWidget()->height() : App::wnd()->height()) * 5;
	MTP::clearLoaderPriorities();

	int32 partfrom = _mediaButtons[OverviewAudios]->y() + _mediaButtons[OverviewAudios]->height() + st::profileHeaderSkip;
	yFrom -= partfrom;
	yTo -= partfrom;

	if (yTo < 0) return;
	if (yFrom < 0) yFrom = 0;
	yFrom /= _pHeight;
	yTo = yTo / _pHeight + 1;
	if (yFrom >= _participants.size()) return;
	if (yTo > _participants.size()) yTo = _participants.size();
	for (int32 i = yFrom; i < yTo; ++i) {
		_participants[i]->photo->load();
	}
}

void ProfileInner::onUpdatePhoto() {
	saveError();

	QStringList imgExtensions(cImgExtensions());	
	QString filter(qsl("Image files (*") + imgExtensions.join(qsl(" *")) + qsl(");;All files (*.*)"));

	QImage img;
	QString file;
	QByteArray remoteContent;
	if (filedialogGetOpenFile(file, remoteContent, lang(lng_choose_images), filter)) {
		if (!remoteContent.isEmpty()) {
			img = App::readImage(remoteContent);
		} else {
			if (!file.isEmpty()) {
				img = App::readImage(file);
			}
		}
	} else {
		return;
	}

	if (img.isNull() || img.width() > 10 * img.height() || img.height() > 10 * img.width()) {
		saveError(lang(lng_bad_photo));
		return;
	}
	PhotoCropBox *box = new PhotoCropBox(img, _peer);
	connect(box, SIGNAL(closed()), this, SLOT(onPhotoUpdateStart()));
	Ui::showLayer(box);
}

void ProfileInner::onClearHistory() {
	if (_peerChannel) return;
	ConfirmBox *box = new ConfirmBox(_peer->isUser() ? lng_sure_delete_history(lt_contact, _peer->name) : lng_sure_delete_group_history(lt_group, _peer->name), lang(lng_box_delete), st::attentionBoxButton);
	connect(box, SIGNAL(confirmed()), this, SLOT(onClearHistorySure()));
	Ui::showLayer(box);
}

void ProfileInner::onClearHistorySure() {
	Ui::hideLayer();
	App::main()->clearHistory(_peer);
}

void ProfileInner::onDeleteConversation() {
	ConfirmBox *box = new ConfirmBox(_peer->isUser() ? lng_sure_delete_history(lt_contact, _peer->name) : (_peer->isChat() ? lng_sure_delete_and_exit(lt_group, _peer->name) : lang(_peer->isMegagroup() ? lng_sure_leave_group : lng_sure_leave_channel)), lang(_peer->isUser() ? lng_box_delete : lng_box_leave), _peer->isChannel() ? st::defaultBoxButton : st::attentionBoxButton);
	connect(box, SIGNAL(confirmed()), this, SLOT(onDeleteConversationSure()));
	Ui::showLayer(box);
}

void ProfileInner::onDeleteConversationSure() {
	Ui::hideLayer();
	if (_peerUser) {
		App::main()->deleteConversation(_peer);
	} else if (_peerChat) {
		Ui::showChatsList();
		MTP::send(MTPmessages_DeleteChatUser(_peerChat->inputChat, App::self()->inputUser), App::main()->rpcDone(&MainWidget::deleteHistoryAfterLeave, _peer), App::main()->rpcFail(&MainWidget::leaveChatFailed, _peer));
	} else if (_peerChannel) {
		Ui::showChatsList();
		if (_peerChannel->migrateFrom()) {
			App::main()->deleteConversation(_peerChannel->migrateFrom());
		}
		MTP::send(MTPchannels_LeaveChannel(_peerChannel->inputChannel), App::main()->rpcDone(&MainWidget::sentUpdatesReceived));
	}
}

void ProfileInner::onDeleteChannel() {
	if (!_peerChannel) return;
	ConfirmBox *box = new ConfirmBox(lang(_peer->isMegagroup() ? lng_sure_delete_group : lng_sure_delete_channel), lang(lng_box_delete), st::attentionBoxButton);
	connect(box, SIGNAL(confirmed()), this, SLOT(onDeleteChannelSure()));
	Ui::showLayer(box);
}

void ProfileInner::onDeleteChannelSure() {
	if (_peerChannel) {
		Ui::hideLayer();
		Ui::showChatsList();
		if (_peerChannel->migrateFrom()) {
			App::main()->deleteConversation(_peerChannel->migrateFrom());
		}
		MTP::send(MTPchannels_DeleteChannel(_peerChannel->inputChannel), App::main()->rpcDone(&MainWidget::sentUpdatesReceived));
	}
}
void ProfileInner::onBlockUser() {
	if (!_peerUser || _blockRequest) return;
	if (_peerUser->blocked == UserIsBlocked) {
		_blockRequest = MTP::send(MTPcontacts_Unblock(_peerUser->inputUser), rpcDone(&ProfileInner::blockDone, false), rpcFail(&ProfileInner::blockFail));
	} else {
		_blockRequest = MTP::send(MTPcontacts_Block(_peerUser->inputUser), rpcDone(&ProfileInner::blockDone, true), rpcFail(&ProfileInner::blockFail));
	}
}

void ProfileInner::blockDone(bool blocked, const MTPBool &result) {
	_blockRequest = 0;
	if (!_peerUser) return;
	_peerUser->blocked = blocked ? UserIsBlocked : UserIsNotBlocked;
	emit App::main()->peerUpdated(_peerUser);
}

bool ProfileInner::blockFail(const RPCError &error) {
	if (mtpIsFlood(error)) return false;

	_blockRequest = 0;
	return false;
}

void ProfileInner::onAddParticipant() {
	if (_peerChat) {
		Ui::showLayer(new ContactsBox(_peerChat, MembersFilterRecent));
	} else if (_peerChannel && _peerChannel->mgInfo) {
		MembersAlreadyIn already;
		for (MegagroupInfo::LastParticipants::const_iterator i = _peerChannel->mgInfo->lastParticipants.cbegin(), e = _peerChannel->mgInfo->lastParticipants.cend(); i != e; ++i) {
			already.insert(*i, true);
		}
		Ui::showLayer(new ContactsBox(_peerChannel, MembersFilterRecent, already));
	}
}

void ProfileInner::onMigrate() {
	if (!_peerChat) return;

	ConfirmBox *box = new ConfirmBox(lang(lng_profile_migrate_sure));
	connect(box, SIGNAL(confirmed()), this, SLOT(onMigrateSure()));
	Ui::showLayer(box);
}

void ProfileInner::onMigrateSure() {
	if (!_peerChat) return;

	MTP::send(MTPmessages_MigrateChat(_peerChat->inputChat), rpcDone(&ProfileInner::migrateDone), rpcFail(&ProfileInner::migrateFail));
}

void ProfileInner::onUpdatePhotoCancel() {
	App::app()->cancelPhotoUpdate(_peer->id);
	showAll();
	update();
}

void ProfileInner::onPhotoUpdateStart() {
	showAll();
	update();
}

void ProfileInner::onPhotoUpdateFail(PeerId peer) {
	if (_peer->id != peer) return;
	saveError(lang(lng_bad_photo));
	showAll();
	update();
}

void ProfileInner::onPhotoUpdateDone(PeerId peer) {
	if (_peer->id != peer) return;
	saveError();
	showAll();
	update();
}

void ProfileInner::onMediaPhotos() {
	App::main()->showMediaOverview(_peer, OverviewPhotos);
}

void ProfileInner::onMediaVideos() {
	App::main()->showMediaOverview(_peer, OverviewVideos);
}

void ProfileInner::onMediaSongs() {
	App::main()->showMediaOverview(_peer, OverviewAudioDocuments);
}

void ProfileInner::onMediaDocuments() {
	App::main()->showMediaOverview(_peer, OverviewDocuments);
}

void ProfileInner::onMediaAudios() {
	App::main()->showMediaOverview(_peer, OverviewAudios);
}

void ProfileInner::onMediaLinks() {
	App::main()->showMediaOverview(_peer, OverviewLinks);
}

void ProfileInner::onInvitationLink() {
	if (!_peerChat && !_peerChannel) return;

	QApplication::clipboard()->setText(_peerChat ? _peerChat->invitationUrl : (_peerChannel ? _peerChannel->invitationUrl : QString()));
	Ui::showLayer(new InformBox(lang(lng_group_invite_copied)));
}

void ProfileInner::onPublicLink() {
	if (!_peerChannel) return;
	
	if (_peerChannel->isPublic()) {
		QApplication::clipboard()->setText(qsl("https://telegram.me/") + _peerChannel->username);
		Ui::showLayer(new InformBox(lang(lng_channel_public_link_copied)));
	} else {
		Ui::showLayer(new SetupChannelBox(_peerChannel, true));
	}
}

void ProfileInner::onMembers() {
	if (!_peerChannel) return;
	Ui::showLayer(new MembersBox(_peerChannel, MembersFilterRecent));
}

void ProfileInner::onAdmins() {
	if (_peerChannel) {
		Ui::showLayer(new MembersBox(_peerChannel, MembersFilterAdmins));
	} else if (_peerChat) {
		Ui::showLayer(new ContactsBox(_peerChat, MembersFilterAdmins));
	}
}

void ProfileInner::onCreateInvitationLink() {
	if (!_peerChat && !_peerChannel) return;

	ConfirmBox *box = new ConfirmBox(lang(((_peerChat && _peerChat->invitationUrl.isEmpty()) || (_peerChannel && _peerChannel->invitationUrl.isEmpty())) ? lng_group_invite_about : lng_group_invite_about_new));
	connect(box, SIGNAL(confirmed()), this, SLOT(onCreateInvitationLinkSure()));
	Ui::showLayer(box);
}

void ProfileInner::onCreateInvitationLinkSure() {
	if (!_peerChat && !_peerChannel) return;
	if (_peerChat) {
		MTP::send(MTPmessages_ExportChatInvite(_peerChat->inputChat), rpcDone(&ProfileInner::chatInviteDone));
	} else if (_peerChannel) {
		MTP::send(MTPchannels_ExportInvite(_peerChannel->inputChannel), rpcDone(&ProfileInner::chatInviteDone));
	}
}

void ProfileInner::chatInviteDone(const MTPExportedChatInvite &result) {
	if (!_peerChat && !_peerChannel) return;

	if (_peerChat) {
		_peerChat->invitationUrl = (result.type() == mtpc_chatInviteExported) ? qs(result.c_chatInviteExported().vlink) : QString();
	} else {
		_peerChannel->invitationUrl = (result.type() == mtpc_chatInviteExported) ? qs(result.c_chatInviteExported().vlink) : QString();
	}
	updateInvitationLink();
	showAll();
	resizeEvent(0);
	Ui::hideLayer();
}

void ProfileInner::onFullPeerUpdated(PeerData *peer) {
	if (peer != _peer) return;
	if (_peerUser) {
		PhotoData *userPhoto = (_peerUser->photoId && _peerUser->photoId != UnknownPeerPhotoId) ? App::photo(_peerUser->photoId) : 0;
		if (userPhoto && userPhoto->date) {
			_photoLink = TextLinkPtr(new PhotoLink(userPhoto, _peer));
		} else {
			_photoLink = TextLinkPtr();
		}
		if (_peerUser->botInfo) {
			if (_peerUser->botInfo->shareText.isEmpty()) {
				_about = Text(st::wndMinWidth - st::profilePadding.left() - st::profilePadding.right());
			} else {
				_about.setText(st::linkFont, _peerUser->botInfo->shareText, _historyBotNoMonoOptions);
			}
			updateBotLinksVisibility();
			resizeEvent(0);
		}
	} else if (_peerChat) {
		updateInvitationLink();
		_showMigrate = (_peerChat && _amCreator && !_peerChat->isMigrated() && (_forceShowMigrate || _peerChat->count >= cMaxGroupCount()));
		showAll();
		resizeEvent(0);
		_admins.setText(lng_channel_admins_link(lt_count, _peerChat->adminsEnabled() ? (_peerChat->admins.size() + 1) : 0));
	} else if (_peerChannel) {
		updateInvitationLink();
		_members.setText(lng_channel_members_link(lt_count, (_peerChannel->count > 0) ? _peerChannel->count : 1));
		_admins.setText(lng_channel_admins_link(lt_count, (_peerChannel->adminsCount > 0) ? _peerChannel->adminsCount : 1));
		_onlineText = (_peerChannel->count > 0) ? lng_chat_status_members(lt_count, _peerChannel->count) : lang(_peerChannel->isMegagroup() ? lng_group_status : lng_channel_status);
		if (_peerChannel->about.isEmpty()) {
			_about = Text(st::wndMinWidth - st::profilePadding.left() - st::profilePadding.right());
		} else {
			_about.setText(st::linkFont, _peerChannel->about, _historyTextNoMonoOptions);
		}
		showAll();
		resizeEvent(0);
	}
}

void ProfileInner::onBotSettings() {
	if (!_peerUser || !_peerUser->botInfo) return;

	for (int32 i = 0, l = _peerUser->botInfo->commands.size(); i != l; ++i) {
		QString cmd = _peerUser->botInfo->commands.at(i).command;
		if (!cmd.compare(qsl("settings"), Qt::CaseInsensitive)) {
			Ui::showPeerHistory(_peer, ShowAtTheEndMsgId);
			App::main()->sendBotCommand('/' + cmd, 0);
			return;
		}
	}
	updateBotLinksVisibility();
}

void ProfileInner::onBotHelp() {
	if (!_peerUser || !_peerUser->botInfo) return;

	for (int32 i = 0, l = _peerUser->botInfo->commands.size(); i != l; ++i) {
		QString cmd = _peerUser->botInfo->commands.at(i).command;
		if (!cmd.compare(qsl("help"), Qt::CaseInsensitive)) {
			Ui::showPeerHistory(_peer, ShowAtTheEndMsgId);
			App::main()->sendBotCommand('/' + cmd, 0);
			return;
		}
	}
	updateBotLinksVisibility();
}

void ProfileInner::peerUpdated(PeerData *data) {
	if (data == _peer) {
		PhotoData *photo = 0;
		if (_peerUser) {
			_phoneText = App::formatPhone(_peerUser->phone.isEmpty() ? App::phoneFromSharedContact(peerToUser(_peerUser->id)) : _peerUser->phone);
			if (_peerUser->photoId && _peerUser->photoId != UnknownPeerPhotoId) photo = App::photo(_peerUser->photoId);
			if (_wasBlocked != _peerUser->blocked) {
				_wasBlocked = _peerUser->blocked;
				_blockUser.setText(lang((_peerUser->blocked == UserIsBlocked) ? (_peerUser->botInfo ? lng_profile_unblock_bot : lng_profile_unblock_user) : (_peerUser->botInfo ? lng_profile_block_bot : lng_profile_block_user)));
			}
		} else if (_peerChat) {
			if (_peerChat->photoId && _peerChat->photoId != UnknownPeerPhotoId) photo = App::photo(_peerChat->photoId);
			_admins.setText(lng_channel_admins_link(lt_count, _peerChat->adminsEnabled() ? (_peerChat->admins.size() + 1) : 0));
			_showMigrate = (_peerChat && _amCreator && !_peerChat->isMigrated() && (_forceShowMigrate || _peerChat->count >= cMaxGroupCount()));
			if (App::main()) App::main()->topBar()->showAll();
		} else if (_peerChannel) {
			if (_peerChannel->photoId && _peerChannel->photoId != UnknownPeerPhotoId) photo = App::photo(_peerChannel->photoId);
			if (_peerChannel->isPublic() != _invitationLink.isHidden()) {
				peerUsernameChanged();
			}
			_members.setText(lng_channel_members_link(lt_count, (_peerChannel->count > 0) ? _peerChannel->count : 1));
			_admins.setText(lng_channel_admins_link(lt_count, (_peerChannel->adminsCount > 0) ? _peerChannel->adminsCount : 1));
			_onlineText = (_peerChannel->count > 0) ? lng_chat_status_members(lt_count, _peerChannel->count) : lang(_peerChannel->isMegagroup() ? lng_group_status : lng_channel_status);
		}
		_photoLink = (photo && photo->date) ? TextLinkPtr(new PhotoLink(photo, _peer)) : TextLinkPtr();
		if (_peer->name != _nameCache) {
			_nameCache = _peer->name;
			_nameText.setText(st::profileNameFont, _nameCache, _textNameOptions);
		}
		showAll();
		resizeEvent(0);
	} else {
		showAll();
	}
	update();
}

void ProfileInner::updateOnlineDisplay() {
	reorderParticipants();
	update();
}

void ProfileInner::updateOnlineDisplayTimer() {
	int32 t = unixtime(), minIn = 86400;
	if (_peerUser) {
		minIn = App::onlineWillChangeIn(_peerUser, t);
	} else if (_peerChat) {
		if (_peerChat->participants.isEmpty()) return;

		for (ChatData::Participants::const_iterator i = _peerChat->participants.cbegin(), e = _peerChat->participants.cend(); i != e; ++i) {
			int32 onlineWillChangeIn = App::onlineWillChangeIn(i.key(), t);
			if (onlineWillChangeIn < minIn) {
				minIn = onlineWillChangeIn;
			}
		}
	} else if (_peerChannel) {
	}
	App::main()->updateOnlineDisplayIn(minIn * 1000);
}

void ProfileInner::reorderParticipants() {
	int32 was = _participants.size(), t = unixtime(), onlineCount = 0;
	if (_peerChat && _peerChat->amIn()) {
		if (!_peerChat->participants.isEmpty()) {
			_participants.clear();
			for (ParticipantsData::iterator i = _participantsData.begin(), e = _participantsData.end(); i != e; ++i) {
				if (*i) {
					delete *i;
					*i = 0;
				}
			}
			_participants.reserve(_peerChat->participants.size());
			_participantsData.resize(_peerChat->participants.size());
		}
		UserData *self = App::self();
        bool onlyMe = true;
        for (ChatData::Participants::const_iterator i = _peerChat->participants.cbegin(), e = _peerChat->participants.cend(); i != e; ++i) {
			UserData *user = i.key();
			int32 until = App::onlineForSort(user, t);
			Participants::iterator before = _participants.begin();
			if (user != self) {
				if (before != _participants.end() && (*before) == self) {
					++before;
				}
				while (before != _participants.end() && App::onlineForSort(*before, t) >= until) {
					++before;
				}
                if (until > t && onlyMe) onlyMe = false;
            }
			_participants.insert(before, user);
			if (until > t) {
				++onlineCount;
			}
		}
		if (_peerChat->noParticipantInfo()) {
			if (App::api()) App::api()->requestFullPeer(_peer);
			if (_onlineText.isEmpty()) _onlineText = lng_chat_status_members(lt_count, _peerChat->count);
        } else if (onlineCount && !onlyMe) {
			_onlineText = lng_chat_status_members_online(lt_count, _participants.size(), lt_count_online, onlineCount);
		} else {
			_onlineText = lng_chat_status_members(lt_count, _participants.size());
		}
		loadProfilePhotos(_lastPreload);
	} else if (_peerChannel && _peerChannel->isMegagroup() && _peerChannel->amIn() && !_peerChannel->mgInfo->lastParticipants.isEmpty()) {
		bool needAdmins = _peerChannel->amEditor(), adminsOutdated = (_peerChannel->mgInfo->lastParticipantsStatus & MegagroupInfo::LastParticipantsAdminsOutdated);
		if (_peerChannel->mgInfo->lastParticipants.isEmpty() || (needAdmins && adminsOutdated) || _peerChannel->lastParticipantsCountOutdated()) {
			if (App::api()) App::api()->requestLastParticipants(_peerChannel);
		} else if (!_peerChannel->mgInfo->lastParticipants.isEmpty()) {
			const MegagroupInfo::LastParticipants &list(_peerChannel->mgInfo->lastParticipants);
			int32 s = list.size();
			for (int32 i = 0, l = _participants.size(); i < l; ++i) {
				if (i >= s || _participants.at(i) != list.at(i)) {
					if (_participantsData.at(i)) {
						delete _participantsData.at(i);
						_participantsData[i] = 0;
					}
					if (i < s) {
						_participants[i] = list.at(i);
					}
				}
			}
			if (_participants.size() > s) {
				_participants.resize(s);
			} else {
				_participants.reserve(s);
				for (int32 i = _participants.size(); i < s; ++i) {
					_participants.push_back(list.at(i));
				}
			}
			_participantsData.resize(s);
		}
		_onlineText = (_peerChannel->count > 0) ? lng_chat_status_members(lt_count, _peerChannel->count) : lang(_peerChannel->isMegagroup() ? lng_group_status : lng_channel_status);
		loadProfilePhotos(_lastPreload);
	} else {
		_participants.clear();
		if (_peerUser) {
			_onlineText = App::onlineText(_peerUser, t, true);
		} else if (_peerChannel) {
			_onlineText = (_peerChannel->count > 0) ? lng_chat_status_members(lt_count, _peerChannel->count) : lang(_peerChannel->isMegagroup() ? lng_group_status : lng_channel_status);
		} else {
			_onlineText = lang(lng_chat_status_unaccessible);
		}
	}
	if (was != _participants.size()) {
		resizeEvent(0);
	}
}

void ProfileInner::start() {
}

void ProfileInner::peerUsernameChanged() {
	if (_peerChannel) {
		_username.setText(_peerChannel->isPublic() ? (qsl("telegram.me/") + _peerChannel->username) : lang(lng_profile_create_public_link));
		resizeEvent(0);
		showAll();
	}
	update();
}

bool ProfileInner::event(QEvent *e) {
	if (e->type() == QEvent::MouseMove) {
		_lastPos = static_cast<QMouseEvent*>(e)->globalPos();
		updateSelected();
	}
	return QWidget::event(e);
}

void ProfileInner::paintEvent(QPaintEvent *e) {
	if (App::wnd() && App::wnd()->contentOverlapped(this, e)) return;

	Painter p(this);

	QRect r(e->rect());
	p.setClipRect(r);

	int32 top = 0, l_time = unixtime();

	// profile
	top += st::profilePadding.top();
	if (_photoLink || _peerUser || (_peerChat && !_peerChat->canEdit()) || (_peerChannel && !_amCreator)) {
		p.drawPixmap(_left, top, _peer->photo->pix(st::profilePhotoSize));
	} else {
		if (a_photoOver.current() < 1) {
			p.drawPixmap(QPoint(_left, top), App::sprite(), st::setPhotoImg);
		}
		if (a_photoOver.current() > 0) {
			p.setOpacity(a_photoOver.current());
			p.drawPixmap(QPoint(_left, top), App::sprite(), st::setOverPhotoImg);
			p.setOpacity(1);
		}
	}
	
	int32 namew = _width - st::profilePhotoSize - st::profileNameLeft;
	p.setPen(st::black->p);
	if (_peer->isVerified()) {
		namew -= st::verifiedCheckProfile.pxWidth() + st::verifiedCheckProfilePos.x();
		int32 cx = _left + st::profilePhotoSize + st::profileNameLeft + qMin(_nameText.maxWidth(), namew);
		p.drawSprite(QPoint(cx, top + st::profileNameTop) + st::verifiedCheckProfilePos, st::verifiedCheckProfile);
	}
	_nameText.drawElided(p, _left + st::profilePhotoSize + st::profileNameLeft, top + st::profileNameTop, namew);

	p.setFont(st::profileStatusFont->f);
	int32 addbyname = 0;
	if (_peerUser && !_peerUser->username.isEmpty()) {
		addbyname = st::profileStatusTop + st::linkFont->ascent - (st::profileNameTop + st::profileNameFont->ascent);
		p.setPen(st::black->p);
		p.drawText(_left + st::profilePhotoSize + st::profileStatusLeft, top + st::profileStatusTop + st::linkFont->ascent, '@' + _peerUser->username);
	} else if (_peerChannel && !_peerChannel->isMegagroup() && (_peerChannel->isPublic() || _amCreator )) {
		addbyname = st::profileStatusTop + st::linkFont->ascent - (st::profileNameTop + st::profileNameFont->ascent);
	}
	if (!_peerChannel || !_peerChannel->canViewParticipants() || _peerChannel->isMegagroup()) {
		p.setPen((_peerUser && App::onlineColorUse(_peerUser, l_time) ? st::profileOnlineColor : st::profileOfflineColor)->p);
		p.drawText(_left + st::profilePhotoSize + st::profileStatusLeft, top + addbyname + st::profileStatusTop + st::linkFont->ascent, _onlineText);
	}
	if (!_cancelPhoto.isHidden()) {
		p.setPen(st::profileOfflineColor->p);
		p.drawText(_left + st::profilePhotoSize + st::profilePhoneLeft, _cancelPhoto.y() + st::linkFont->ascent, lang(lng_settings_uploading_photo));
	}

	if (!_errorText.isEmpty()) {
		p.setFont(st::setErrFont->f);
		p.setPen(st::setErrColor->p);
		p.drawText(_left + st::profilePhotoSize + st::profilePhoneLeft, _cancelPhoto.y() + st::profilePhoneFont->ascent, _errorText);
	}
	if (!_phoneText.isEmpty()) {
		p.setPen(st::black->p);
		p.setFont(st::linkFont->f);
		p.drawText(_left + st::profilePhotoSize + st::profilePhoneLeft, top + addbyname + st::profilePhoneTop + st::profilePhoneFont->ascent, _phoneText);
	}
	top += st::profilePhotoSize;
	top += st::profileButtonTop;

	if ((!_peerChat || _peerChat->canEdit()) && (!_peerChannel || _amCreator || (_peerChannel->amEditor() && _peerChannel->isMegagroup()))) {
		top += _shareContact.height();
	} else {
		top -= st::profileButtonTop;
	}

	// about
	if (!_about.isEmpty()) {
		p.setFont(st::profileHeaderFont->f);
		p.setPen(st::profileHeaderColor->p);
		p.drawText(_left + st::profileHeaderLeft, top + st::profileHeaderTop + st::profileHeaderFont->ascent, lang(_peerChannel ? lng_profile_description_section : lng_profile_about_section));
		top += st::profileHeaderSkip;

		_about.draw(p, _left, top, _width);
		top += _aboutHeight;
	}

	// migrate to megagroup
	if (_showMigrate) {
		p.setFont(st::profileHeaderFont->f);
		p.setPen(st::profileHeaderColor->p);
		p.drawText(_left + st::profileHeaderLeft, top + st::profileHeaderTop + st::profileHeaderFont->ascent, lng_profile_migrate_reached(lt_count, cMaxGroupCount()));
		top += st::profileHeaderSkip;

		_aboutMigrate.draw(p, _left, top, _width); top += _aboutMigrate.countHeight(_width) + st::setLittleSkip;
		p.setFont(st::normalFont);
		p.setPen(st::black);
		p.drawText(_left, top + st::normalFont->ascent, lng_profile_migrate_feature1(lt_count, cMaxMegaGroupCount())); top += st::normalFont->height + st::setLittleSkip;
		p.drawText(_left, top + st::normalFont->ascent, lang(lng_profile_migrate_feature2)); top += st::normalFont->height + st::setLittleSkip;
		p.drawText(_left, top + st::normalFont->ascent, lang(lng_profile_migrate_feature3)); top += st::normalFont->height + st::setLittleSkip;
		p.drawText(_left, top + st::normalFont->ascent, lang(lng_profile_migrate_feature4)); top += st::normalFont->height + st::setSectionSkip;

		top += _migrate.height();
	}

	// settings
	p.setFont(st::profileHeaderFont->f);
	p.setPen(st::profileHeaderColor->p);
	p.drawText(_left + st::profileHeaderLeft, top + st::profileHeaderTop + st::profileHeaderFont->ascent, lang(lng_profile_settings_section));
	top += st::profileHeaderSkip;

	// invite link stuff
	if (_amCreator && ((_peerChat && _peerChat->canEdit()) || (_peerChannel && !_peerChannel->isPublic()))) {
		if ((_peerChat && !_peerChat->invitationUrl.isEmpty()) || (_peerChannel && !_peerChannel->invitationUrl.isEmpty())) {
			p.setPen(st::black);
			p.setFont(st::linkFont);
			p.drawText(_left, _invitationLink.y() + st::linkFont->ascent, lang(lng_group_invite_link));
			top += _invitationLink.height() + st::setLittleSkip;
		}
		top += _createInvitationLink.height() + st::setSectionSkip;
	}

	top += _enableNotifications.height();

	// shared media
	p.setFont(st::profileHeaderFont->f);
	p.setPen(st::profileHeaderColor->p);
	p.drawText(_left + st::profileHeaderLeft, top + st::profileHeaderTop + st::profileHeaderFont->ascent, lang(lng_profile_shared_media));
	top += st::profileHeaderSkip;

	p.setFont(st::linkFont->f);
	p.setPen(st::black->p);
	bool mediaFound = false;
	for (int i = 0; i < OverviewCount; ++i) {
		if (!_mediaButtons[i]->isHidden()) {
			mediaFound = true;
			top += _mediaButtons[i]->height() + st::setLittleSkip;
		}
	}
	if (_notAllMediaLoaded || !mediaFound) {
		p.drawText(_left, top + st::linkFont->ascent, lang(_notAllMediaLoaded ? lng_profile_loading : lng_profile_no_media));
		top += _mediaButtons[OverviewPhotos]->height();
	} else {
		top -= st::setLittleSkip;
	}

	// actions
	p.setFont(st::profileHeaderFont->f);
	p.setPen(st::profileHeaderColor->p);
	p.drawText(_left + st::profileHeaderLeft, top + st::profileHeaderTop + st::profileHeaderFont->ascent, lang(lng_profile_actions_section));
	top += st::profileHeaderSkip;

	top += _searchInPeer.height() + st::setLittleSkip;
	if (_peerUser || _peerChat) {
		top += _clearHistory.height() + st::setLittleSkip;
	}
	if (_peerUser || _peerChat || (_peerChannel->amIn() && !_amCreator)) {
		top += _deleteConversation.height();
	}
	if (_peerUser && peerToUser(_peerUser->id) != MTP::authedId()) {
		top += st::setSectionSkip + _blockUser.height();
	} else if (_peerChannel && _amCreator) {
		top += (_peerChannel->isMegagroup() ? 0 : (st::setSectionSkip - st::setLittleSkip)) + _deleteChannel.height();
	}

	// participants
	if ((_peerChat && _peerChat->amIn()) || (_peerChannel && _peerChannel->isMegagroup() && _peerChannel->amIn())) {
		QString sectionHeader = lang(_participants.isEmpty() ? lng_profile_loading : lng_profile_participants_section);
		p.setFont(st::profileHeaderFont->f);
		p.setPen(st::profileHeaderColor->p);
		p.drawText(_left + st::profileHeaderLeft, top + st::profileHeaderTop + st::profileHeaderFont->ascent, sectionHeader);
		top += st::profileHeaderSkip;

		int32 partfrom = top;
		if (!_participants.isEmpty()) {
			int32 cnt = 0, fullCnt = _participants.size();
			for (Participants::const_iterator i = _participants.cbegin(), e = _participants.cend(); i != e; ++i, ++cnt) {
				int32 top = partfrom + cnt * _pHeight;
				if (top + _pHeight <= r.top()) continue;
				if (top >= r.y() + r.height()) break;

				if (_selectedRow == cnt) {
					p.fillRect(_left - st::profileListPadding.width(), top, _width + 2 * st::profileListPadding.width(), _pHeight, st::profileHoverBG->b);
				}

				UserData *user = *i;
				p.drawPixmap(_left, top + st::profileListPadding.height(), user->photo->pix(st::profileListPhotoSize));
				ParticipantData *data = _participantsData[cnt];
				if (!data) {
					data = _participantsData[cnt] = new ParticipantData();
					data->name.setText(st::profileListNameFont, user->name, _textNameOptions);
					if (user->botInfo) {
						if (user->botInfo->readsAllHistory) {
							data->online = lang(lng_status_bot_reads_all);
						} else {
							data->online = lang(lng_status_bot_not_reads_all);
						}
					} else {
						data->online = App::onlineText(user, l_time);
					}
					if (_amCreator) {
						data->cankick = (user != App::self());
					} else if (_peerChat && _peerChat->amAdmin()) {
						data->cankick = (user != App::self()) && (_peerChat->admins.constFind(user) == _peerChat->admins.cend()) && (peerFromUser(_peerChat->creator) != user->id);
					} else if (_peerChannel && _peerChannel->amEditor()) {
						data->cankick = (user != App::self()) && (_peerChannel->mgInfo->lastAdmins.constFind(user) == _peerChannel->mgInfo->lastAdmins.cend());
					} else {
						data->cankick = (user != App::self()) && !_peerChannel && (_peerChat->invitedByMe.constFind(user) != _peerChat->invitedByMe.cend());
					}
				}
				p.setPen(st::profileListNameColor->p);
				p.setFont(st::linkFont->f);
				data->name.drawElided(p, _left + st::profileListPhotoSize + st::profileListPadding.width(), top + st::profileListNameTop, _width - _kickWidth - st::profileListPadding.width() - st::profileListPhotoSize - st::profileListPadding.width());
				p.setFont(st::profileSubFont->f);
				p.setPen((App::onlineColorUse(user, l_time) ? st::profileOnlineColor : st::profileOfflineColor)->p);
				p.drawText(_left + st::profileListPhotoSize + st::profileListPadding.width(), top + st::profileListPadding.height() + st::profileListPhotoSize - st::profileListStatusBottom, data->online);

				if (data->cankick) {
					bool over = (user == _kickOver && (!_kickDown || _kickDown == _kickOver));
					p.setFont((over ? st::linkOverFont : st::linkFont)->f);
					if (user == _kickOver && _kickOver == _kickDown) {
						p.setPen(st::btnDefLink.downColor->p);
					} else {
						p.setPen(st::btnDefLink.color->p);
					}
					p.drawText(_left + _width - _kickWidth, top + st::profileListNameTop + st::linkFont->ascent, lang(lng_profile_kick));
				}
			}
			top += fullCnt * _pHeight;
		}
	}

	top += st::profileHeaderTop + st::profileHeaderFont->ascent - st::linkFont->ascent;
	top += _clearHistory.height();
}

void ProfileInner::mouseMoveEvent(QMouseEvent *e) {
	_lastPos = e->globalPos();
	updateSelected();

	bool photoOver = QRect(_left, st::profilePadding.top(), st::setPhotoSize, st::setPhotoSize).contains(e->pos());
	if (photoOver != _photoOver) {
		_photoOver = photoOver;
		if (!_photoLink && ((_peerChat && _peerChat->canEdit()) || (_peerChannel && _amCreator))) {
			a_photoOver.start(_photoOver ? 1 : 0);
			_a_photo.start();
		}
	}
	if (!_photoLink && (_peerUser || (_peerChat && !_peerChat->canEdit()) || (_peerChannel && !_amCreator))) {
		setCursor((_kickOver || _kickDown || textlnkOver()) ? style::cur_pointer : style::cur_default);
	} else {
		setCursor((_kickOver || _kickDown || _photoOver || textlnkOver()) ? style::cur_pointer : style::cur_default);
	}
}

void ProfileInner::updateSelected() {
	if (!isVisible()) return;

	QPoint lp = mapFromGlobal(_lastPos);

	TextLinkPtr lnk;
	bool inText = false;
	if (!_about.isEmpty() && lp.y() >= _aboutTop && lp.y() < _aboutTop + _aboutHeight && lp.x() >= _left && lp.x() < _left + _width) {
		_about.getState(lnk, inText, lp.x() - _left, lp.y() - _aboutTop, _width);
	}
	if (textlnkOver() != lnk) {
		textlnkOver(lnk);
		update(QRect(_left, _aboutTop, _width, _aboutHeight));
	}

	int32 participantsTop = 0;
	if (_peerChannel && _amCreator) {
		participantsTop = _deleteChannel.y() + _deleteChannel.height();
	} else {
		participantsTop = _deleteConversation.y() + _deleteConversation.height();
	}
	participantsTop += st::profileHeaderSkip;
	int32 newSelected = (lp.x() >= _left - st::profileListPadding.width() && lp.x() < _left + _width + st::profileListPadding.width() && lp.y() >= participantsTop) ? (lp.y() - participantsTop) / _pHeight : -1;

	UserData *newKickOver = 0;
	if (newSelected >= 0 && newSelected < _participants.size()) {
		ParticipantData *data = _participantsData[newSelected];
		if (data && data->cankick) {
			int32 top = participantsTop + newSelected * _pHeight + st::profileListNameTop;
			if ((lp.x() >= _left + _width - _kickWidth) && (lp.x() < _left + _width) && (lp.y() >= top) && (lp.y() < top + st::linkFont->height)) {
				newKickOver = _participants[newSelected];
			}
		}
	}
	if (_kickOver != newKickOver) {
		_kickOver = newKickOver;
		update();
	}
	if (_kickDown) return;

	if (newSelected != _selectedRow) {
		_selectedRow = newSelected;
		update();
	}
}

void ProfileInner::mousePressEvent(QMouseEvent *e) {
	_lastPos = e->globalPos();
	updateSelected();
	if (e->button() == Qt::LeftButton) {
		if (_kickOver) {
			_kickDown = _kickOver;
			update();
		} else if (_selectedRow >= 0 && _selectedRow < _participants.size()) {
			App::main()->showPeerProfile(_participants[_selectedRow]);
		} else if (QRect(_left, st::profilePadding.top(), st::setPhotoSize, st::setPhotoSize).contains(e->pos())) {
			if (_photoLink) {
				_photoLink->onClick(e->button());
			} else if ((_peerChat && _peerChat->canEdit()) || (_peerChannel && _amCreator)) {
				onUpdatePhoto();
			}
		}
		textlnkDown(textlnkOver());
	}
}

void ProfileInner::mouseReleaseEvent(QMouseEvent *e) {
	_lastPos = e->globalPos();
	updateSelected();
	if (_kickDown && _kickDown == _kickOver) {
		_kickConfirm = _kickOver;
		ConfirmBox *box = new ConfirmBox(lng_profile_sure_kick(lt_user, _kickOver->firstName), lang(lng_box_remove));
		connect(box, SIGNAL(confirmed()), this, SLOT(onKickConfirm()));
		Ui::showLayer(box);
	}
	if (textlnkDown()) {
		TextLinkPtr lnk = textlnkDown();
		textlnkDown(TextLinkPtr());
		if (lnk == textlnkOver()) {
			if (reHashtag().match(lnk->encoded()).hasMatch() && _peerChannel) {
				App::searchByHashtag(lnk->encoded(), _peerChannel);
			} else {
				if (reBotCommand().match(lnk->encoded()).hasMatch()) {
					Ui::showPeerHistory(_peer, ShowAtTheEndMsgId);
				}
				lnk->onClick(e->button());
			}
		}
	}
	_kickDown = 0;
	if (!_photoLink && (_peerUser || (_peerChat && !_peerChat->canEdit()) || (_peerChannel && !_amCreator))) {
		setCursor((_kickOver || _kickDown || textlnkOver()) ? style::cur_pointer : style::cur_default);
	} else {
		setCursor((_kickOver || _kickDown || _photoOver || textlnkOver()) ? style::cur_pointer : style::cur_default);
	}
	update();
}

void ProfileInner::onKickConfirm() {
	if (_peerChat) {
		App::main()->kickParticipant(_peerChat, _kickConfirm);
	} else if (_peerChannel) {
		Ui::hideLayer();
		App::api()->kickParticipant(_peerChannel, _kickConfirm);
	}
}

void ProfileInner::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape || e->key() == Qt::Key_Back) {
		App::main()->showBackFromStack();
	}
	_secretText += e->text().toLower();
	int32 size = _secretText.size(), from = 0;
	while (size > from) {
		QStringRef str(_secretText.midRef(from));
		if (str == qstr("tosupergroup")) {
			_forceShowMigrate = true;
			peerUpdated(_peer);
		} else if (qsl("tosupergroup").startsWith(str)) {
			break;
		}
		++from;
	}
	_secretText = (size > from) ? _secretText.mid(from) : QString();

}

void ProfileInner::enterEvent(QEvent *e) {
	setMouseTracking(true);
	_lastPos = QCursor::pos();
	updateSelected();
	return TWidget::enterEvent(e);
}

void ProfileInner::leaveEvent(QEvent *e) {
	setMouseTracking(false);
	_lastPos = QCursor::pos();
	updateSelected();
	return TWidget::leaveEvent(e);
}

void ProfileInner::leaveToChildEvent(QEvent *e) {
	_lastPos = QCursor::pos();
	updateSelected();
	return TWidget::leaveToChildEvent(e);
}

bool ProfileInner::updateMediaLinks(int32 *addToScroll) {
	QPoint p(addToScroll ? mapFromGlobal(QCursor::pos()) : QPoint(0, 0));
	bool oneWasShown = false;
	for (int i = 0; i < OverviewCount; ++i) {
		if (!_mediaButtons[i]->isHidden()) {
			oneWasShown = true;
			break;
		}
	}

	bool newNotAllMediaLoaded = false, changed = false, substracted = !_notAllMediaLoaded && oneWasShown;
	bool notAllHistoryLoaded = false, notAllMigratedLoaded = false;

	bool oneIsShown = false;
	int32 y = _mediaButtons[OverviewPhotos]->y();
	if (addToScroll) *addToScroll = 0;
	for (int i = 0; i < OverviewCount; ++i) {
		int32 addToY = _mediaButtons[i]->height() + st::setLittleSkip;

		int32 count = _history->overviewCount(i), additional = _migrated ? _migrated->overviewCount(i) : 0;
		int32 sum = (count > 0 ? count : 0) + (additional > 0 ? additional : 0);
		if (sum > 0) {
			_mediaButtons[i]->setText(overviewLinkText(i, sum));
			if (_mediaButtons[i]->isHidden()) {
				_mediaButtons[i]->show();
				changed = true;

				if (addToScroll && p.y() >= y) {
					p.setY(p.y() + addToY);
					*addToScroll += addToY;
				}
			}
			y += addToY;
			oneIsShown = true;
		} else {
			if (!_mediaButtons[i]->isHidden()) {
				_mediaButtons[i]->hide();
				changed = true;

				if (addToScroll && p.y() >= y + addToY) {
					p.setY(p.y() - addToY);
					*addToScroll -= addToY;
				}
			}
			if (count < 0) {
				notAllHistoryLoaded = true;
			}
			if (additional < 0) {
				notAllMigratedLoaded = true;
			}
		}
	}
	newNotAllMediaLoaded = notAllHistoryLoaded || notAllMigratedLoaded;
	if (newNotAllMediaLoaded != _notAllMediaLoaded) {
		_notAllMediaLoaded = newNotAllMediaLoaded;
		changed = true;

		int32 addToY = _mediaButtons[OverviewPhotos]->height();
		if (_notAllMediaLoaded) {
			if (addToScroll && p.y() >= y) {
				p.setY(p.y() + addToY);
				*addToScroll += addToY;
			}
		} else {
			if (addToScroll && p.y() >= y + addToY) {
				p.setY(p.y() - addToY);
				*addToScroll -= addToY;
			}
		}

		if (App::main()) {
			if (notAllHistoryLoaded) App::main()->preloadOverviews(_peer);
			if (notAllMigratedLoaded) App::main()->preloadOverviews(_migrated->peer);
		}
	}
	bool newSubstracted = !_notAllMediaLoaded && oneIsShown;
	if (newSubstracted && newSubstracted != substracted) {
		int32 addToY = st::setLittleSkip;
		if (addToScroll && p.y() >= y + addToY) {
			p.setY(p.y() - addToY);
			*addToScroll -= addToY;
		}
	}
	return changed;
}

void ProfileInner::migrateDone(const MTPUpdates &updates) {
	Ui::hideLayer();
	App::main()->sentUpdatesReceived(updates);
	const QVector<MTPChat> *v = 0;
	switch (updates.type()) {
	case mtpc_updates: v = &updates.c_updates().vchats.c_vector().v; break;
	case mtpc_updatesCombined: v = &updates.c_updatesCombined().vchats.c_vector().v; break;
	default: LOG(("API Error: unexpected update cons %1 (ProfileInner::migrateDone)").arg(updates.type())); break;
	}

	PeerData *peer = 0;
	if (v && !v->isEmpty()) {
		for (int32 i = 0, l = v->size(); i < l; ++i) {
			if (v->at(i).type() == mtpc_channel) {
				peer = App::channel(v->at(i).c_channel().vid.v);
				Ui::showPeerHistory(peer, ShowAtUnreadMsgId);
				QTimer::singleShot(ReloadChannelMembersTimeout, App::api(), SLOT(delayedRequestParticipantsCount()));
			}
		}
	}
	if (!peer) {
		LOG(("API Error: channel not found in updates (ProfileInner::migrateDone)"));
	}
}

bool ProfileInner::migrateFail(const RPCError &error) {
	if (mtpIsFlood(error)) return false;
	Ui::hideLayer();
	return true;
}

void ProfileInner::resizeEvent(QResizeEvent *e) {
	_width = qMin(width() - st::profilePadding.left() - st::profilePadding.right(), int(st::profileMaxWidth));
	_left = (width() - _width) / 2;

	int32 top = 0, btnWidth = (_width - st::profileButtonSkip) / 2;
	
	// profile
	top += st::profilePadding.top();
	int32 addbyname = 0;
	if (_peerChannel && !_peerChannel->isMegagroup() && (_amCreator || _peerChannel->isPublic())) {
		_username.move(_left + st::profilePhotoSize + st::profileStatusLeft, top + st::profileStatusTop);
		addbyname = st::profileStatusTop + st::linkFont->ascent - (st::profileNameTop + st::profileNameFont->ascent);
	}
	_members.move(_left + st::profilePhotoSize + st::profileStatusLeft, top + addbyname + st::profileStatusTop);
	addbyname += st::profileStatusTop + st::linkFont->ascent - (st::profileNameTop + st::profileNameFont->ascent);
	_admins.move(_left + st::profilePhotoSize + st::profileStatusLeft, top + addbyname + st::profileStatusTop);
	if ((_peerChat && _amCreator && _peerChat->canEdit()) || (_peerChannel && (_amCreator || _peerChannel->amEditor() || _peerChannel->amModerator()))) {
		_cancelPhoto.move(_left + _width - _cancelPhoto.width(), top + st::profilePhotoSize - st::linkFont->height);
	} else {
		_cancelPhoto.move(_left + _width - _cancelPhoto.width(), top + st::profilePhoneTop);
		_botSettings.move(_left + st::profilePhotoSize + st::profilePhoneLeft, top + st::profileStatusTop + st::linkFont->ascent - (st::profileNameTop + st::profileNameFont->ascent) + st::profilePhoneTop);
		_botHelp.move(_botSettings.x() + (_botSettings.isHidden() ? 0 : _botSettings.width() + st::profilePhoneLeft), _botSettings.y());
	}
	top += st::profilePhotoSize;

	top += st::profileButtonTop;

	_uploadPhoto.setGeometry(_left, top, btnWidth, _uploadPhoto.height());
	_addParticipant.setGeometry(_left + _width - btnWidth, top, btnWidth, _addParticipant.height());

	_sendMessage.setGeometry(_left, top, btnWidth, _sendMessage.height());
	_shareContact.setGeometry(_left + _width - btnWidth, top, btnWidth, _shareContact.height());
	_inviteToGroup.setGeometry(_left + _width - btnWidth, top, btnWidth, _inviteToGroup.height());

	if ((!_peerChat || _peerChat->canEdit()) && (!_peerChannel || _amCreator || (_peerChannel->amEditor() && _peerChannel->isMegagroup()))) {
		top += _shareContact.height();
	} else {
		top -= st::profileButtonTop;
	}

	// about
	if (!_about.isEmpty()) {
		top += st::profileHeaderSkip;
		_aboutTop = top; _aboutHeight = _about.countHeight(_width); top += _aboutHeight;
	} else {
		_aboutTop = _aboutHeight = 0;
	}

	// migrate to megagroup
	if (_showMigrate) {
		top += st::profileHeaderSkip;
		top += _aboutMigrate.countHeight(_width) + st::setLittleSkip;
		top += st::normalFont->height * 4 + st::setLittleSkip * 3 + st::setSectionSkip;
		_migrate.move(_left, top); top += _migrate.height();
	}

	// settings
	top += st::profileHeaderSkip;

	// invite link stuff
	int32 _inviteLinkTextWidth(st::linkFont->width(lang(lng_group_invite_link)) + st::linkFont->spacew);
	if (_amCreator && ((_peerChat && _peerChat->canEdit()) || (_peerChannel && !_peerChannel->isPublic()))) {
		if (!_invitationText.isEmpty()) {
			_invitationLink.setText(st::linkFont->elided(_invitationText, _width - _inviteLinkTextWidth));
		}
		if ((_peerChat && !_peerChat->invitationUrl.isEmpty()) || (_peerChannel && !_peerChannel->invitationUrl.isEmpty())) {
			_invitationLink.move(_left + _inviteLinkTextWidth, top);
			top += _invitationLink.height() + st::setLittleSkip;
			_createInvitationLink.move(_left, top);
		} else {
			_createInvitationLink.move(_left, top);
		}
		top += _createInvitationLink.height() + st::setSectionSkip;
	}

	_enableNotifications.move(_left, top); top += _enableNotifications.height();

	// shared media
	top += st::profileHeaderSkip;

	bool mediaFound = false;
	for (int i = 0; i < OverviewCount; ++i) {
		_mediaButtons[i]->move(_left, top);
		if (!_mediaButtons[i]->isHidden()) {
			mediaFound = true;
			top += _mediaButtons[i]->height() + st::setLittleSkip;
		}
	}
	if (_notAllMediaLoaded || !mediaFound) {
		top += _mediaButtons[OverviewPhotos]->height();
	} else {
		top -= st::setLittleSkip;
	}

	// actions
	top += st::profileHeaderSkip;
	_searchInPeer.move(_left, top);	top += _searchInPeer.height() + st::setLittleSkip;
	if (_peerUser || _peerChat) {
		_clearHistory.move(_left, top); top += _clearHistory.height() + st::setLittleSkip;
	}
	if (_peerUser || _peerChat || (_peerChannel->amIn() && !_amCreator)) {
		_deleteConversation.move(_left, top); top += _deleteConversation.height();
	}
	if (_peerUser && peerToUser(_peerUser->id) != MTP::authedId()) {
		top += st::setSectionSkip;
		_blockUser.move(_left, top); top += _blockUser.height();
	} else if (_peerChannel && _amCreator) {
		top += (_peerChannel->isMegagroup() ? 0 : (st::setSectionSkip - st::setLittleSkip));
		_deleteChannel.move(_left, top); top += _deleteChannel.height();
	}

	// participants
	if ((_peerChat && _peerChat->amIn()) || (_peerChannel && _peerChannel->isMegagroup() && _peerChannel->amIn())) {
		top += st::profileHeaderSkip;
		if (!_participants.isEmpty()) {
			int32 fullCnt = _participants.size();
			top += fullCnt * _pHeight;
		}
	}
	top += st::profileHeaderTop + st::profileHeaderFont->ascent - st::linkFont->ascent;
}

void ProfileInner::contextMenuEvent(QContextMenuEvent *e) {
	if (_menu) {
		_menu->deleteLater();
		_menu = 0;
	}
	if (!_phoneText.isEmpty() || (_peerUser && !_peerUser->username.isEmpty())) {
		QRect info(_left + st::profilePhotoSize + st::profilePhoneLeft, st::profilePadding.top(), _width - st::profilePhotoSize - st::profilePhoneLeft, st::profilePhotoSize);
		if (info.contains(mapFromGlobal(e->globalPos()))) {
			_menu = new PopupMenu();
			if (!_phoneText.isEmpty()) {
				_menu->addAction(lang(lng_profile_copy_phone), this, SLOT(onCopyPhone()))->setEnabled(true);
			}
			if (_peerUser && !_peerUser->username.isEmpty()) {
				_menu->addAction(lang(lng_context_copy_mention), this, SLOT(onCopyUsername()))->setEnabled(true);
			}
			connect(_menu, SIGNAL(destroyed(QObject*)), this, SLOT(onMenuDestroy(QObject*)));
			_menu->popup(e->globalPos());
			e->accept();
		}
	}
}

void ProfileInner::onMenuDestroy(QObject *obj) {
	if (_menu == obj) {
		_menu = 0;
	}
}

void ProfileInner::onCopyPhone() {
	QApplication::clipboard()->setText(_phoneText);
}

void ProfileInner::onCopyUsername() {
	if (!_peerUser) return;
	QApplication::clipboard()->setText('@' + _peerUser->username);
}

void ProfileInner::step_photo(float64 ms, bool timer) {
	float64 dt = ms / st::setPhotoDuration;
	if (dt >= 1) {
		_a_photo.stop();
		a_photoOver.finish();
	} else {
		a_photoOver.update(dt, anim::linear);
	}
	if (timer) update(_left, st::profilePadding.top(), st::setPhotoSize, st::setPhotoSize);
}

PeerData *ProfileInner::peer() const {
	return _peer;
}

ProfileInner::~ProfileInner() {
	for (ParticipantsData::iterator i = _participantsData.begin(), e = _participantsData.end(); i != e; ++i) {
		delete *i;
	}
	_participantsData.clear();
}
	
void ProfileInner::openContextImage() {
}

void ProfileInner::deleteContextImage() {
}

void ProfileInner::updateNotifySettings() {
	_enableNotifications.setChecked(_peer->notify == EmptyNotifySettings || _peer->notify == UnknownNotifySettings || _peer->notify->mute < unixtime());
}

int32 ProfileInner::mediaOverviewUpdated(PeerData *peer, MediaOverviewType type) {
	int32 result = 0;
	if (peer == _peer || (_migrated && _migrated->peer == peer)) {
		if (updateMediaLinks(&result)) {
			showAll();
			resizeEvent(0);
			update();
		}
	}
	return result;
}

void ProfileInner::requestHeight(int32 newHeight) {
	if (newHeight > height()) {
		_addToHeight += newHeight - height();
		showAll();
	}
}

int32 ProfileInner::countMinHeight() {
	int32 h = 0;
	if (_peerUser) {
		if (peerToUser(_peerUser->id) == MTP::authedId()) {
			h = _deleteConversation.y() + _deleteConversation.height() + st::profileHeaderSkip;
		} else {
			h = _blockUser.y() + _blockUser.height() + st::profileHeaderSkip;
		}
	} else if (_peerChat) {
		h = _deleteConversation.y() + _deleteConversation.height() + st::profileHeaderSkip;
		if (!_participants.isEmpty()) {
			h += st::profileHeaderSkip + _participants.size() * _pHeight;
		} else if (_peerChat->amIn()) {
			h += st::profileHeaderSkip;
		}
	} else if (_peerChannel) {
		if (_amCreator) {
			h = _deleteChannel.y() + _deleteChannel.height() + st::profileHeaderSkip;
		} else if (_peerChannel->amIn()) {
			h = _deleteConversation.y() + _deleteConversation.height() + st::profileHeaderSkip;
		} else {
			h = _searchInPeer.y() + _searchInPeer.height() + st::profileHeaderSkip;
		}
		if (_peerChannel->isMegagroup()) {
			if (!_participants.isEmpty()) {
				h += st::profileHeaderSkip + _participants.size() * _pHeight;
			} else if (_peerChannel->amIn()) {
				h += st::profileHeaderSkip;
			}
		}
	}
	return h;
}

void ProfileInner::allowDecreaseHeight(int32 decreaseBy) {
	if (decreaseBy > 0 && _addToHeight > 0) {
		_addToHeight -= qMin(decreaseBy, _addToHeight);
		showAll();
	}
}

void ProfileInner::showAll() {
	_searchInPeer.show();
	if (_peerUser || _peerChat) {
		_clearHistory.show();
	} else {
		_clearHistory.hide();
	}
	if (_peerUser || _peerChat || (_peerChannel->amIn() && !_amCreator)) {
		_deleteConversation.show();
	} else {
		_deleteConversation.hide();
	}
	if (_peerUser) {
		_uploadPhoto.hide();
		_cancelPhoto.hide();
		_addParticipant.hide();
		_createInvitationLink.hide();
		_invitationLink.hide();
		_sendMessage.show();
		if (_peerUser->phone.isEmpty()) {
			_shareContact.hide();
			if (_peerUser->botInfo && !_peerUser->botInfo->cantJoinGroups) {
				_inviteToGroup.show();
			} else {
				_inviteToGroup.hide();
			}
		} else {
			_shareContact.show();
			_inviteToGroup.hide();
		}
		_clearHistory.show();
		if (peerToUser(_peerUser->id) != MTP::authedId()) {
			_blockUser.show();
		} else {
			_blockUser.hide();
		}
		_deleteChannel.hide();
		_username.hide();
		_members.hide();
		_admins.hide();
	} else if (_peerChat) {
		_sendMessage.hide();
		_shareContact.hide();
		_inviteToGroup.hide();
		if (!_peerChat->canEdit()) {
			_uploadPhoto.hide();
			_cancelPhoto.hide();
			_addParticipant.hide();
			_createInvitationLink.hide();
			_invitationLink.hide();
		} else {
			if (App::app()->isPhotoUpdating(_peer->id)) {
				_uploadPhoto.hide();
				_cancelPhoto.show();
			} else {
				_uploadPhoto.show();
				_cancelPhoto.hide();
			}
			if (_amCreator) {
				_createInvitationLink.show();
				if (_peerChat->invitationUrl.isEmpty()) {
					_invitationLink.hide();
				} else {
					_invitationLink.show();
				}
			} else {
				_createInvitationLink.hide();
				_invitationLink.hide();
			}
			if (_peerChat->count < cMaxGroupCount() && !_showMigrate) {
				_addParticipant.show();
			} else {
				_addParticipant.hide();
			}
		}
		_blockUser.hide();
		_deleteChannel.hide();
		_username.hide();
		_members.hide();
		if (_amCreator && _peerChat->canEdit()) {
			_admins.show();
		} else {
			_admins.hide();
		}
	} else if (_peerChannel) {
		_sendMessage.hide();
		_shareContact.hide();
		_inviteToGroup.hide();
		if (_peerChannel->isForbidden) {
			_uploadPhoto.hide();
			_cancelPhoto.hide();
			_createInvitationLink.hide();
			_invitationLink.hide();
		} else {
			if (App::app()->isPhotoUpdating(_peer->id)) {
				_uploadPhoto.hide();
				_cancelPhoto.show();
			} else {
				if (_amCreator || (_peerChannel->amEditor() && _peerChannel->isMegagroup())) {
					_uploadPhoto.show();
				} else {
					_uploadPhoto.hide();
				}
				_cancelPhoto.hide();
			}
			if (_amCreator && !_peerChannel->isPublic()) {
				_createInvitationLink.show();
				if (_peerChannel->invitationUrl.isEmpty()) {
					_invitationLink.hide();
				} else {
					_invitationLink.show();
				}
			} else {
				_createInvitationLink.hide();
				_invitationLink.hide();
			}
		}
		if (_peerChannel->count < cMaxMegaGroupCount() && _peerChannel->isMegagroup() && (_amCreator || _peerChannel->amEditor())) {
			_addParticipant.show();
		} else {
			_addParticipant.hide();
		}
		_blockUser.hide();
		if (_amCreator) {
			_deleteChannel.show();
		} else {
			_deleteChannel.hide();
		}
		if (!_peerChannel->isMegagroup() && (_peerChannel->isPublic() || _amCreator)) {
			_username.show();
		} else {
			_username.hide();
		}
		if (_amCreator || _peerChannel->amEditor() || _peerChannel->amModerator()) {
			_admins.show();
		} else {
			_admins.hide();
		}
		if (_peerChannel->canViewParticipants() && !_peerChannel->isMegagroup()) {
			_members.show();
		} else {
			_members.hide();
		}
	}
	if (_showMigrate) {
		_migrate.show();
	} else {
		_migrate.hide();
	}
	_enableNotifications.show();
	updateNotifySettings();

	// participants
	reorderParticipants();
	resize(width(), countMinHeight() + _addToHeight);
}

void ProfileInner::updateInvitationLink() {
	if (!_peerChat && !_peerChannel) return;

	if ((_peerChat && _peerChat->invitationUrl.isEmpty()) || (_peerChannel && _peerChannel->invitationUrl.isEmpty())) {
		_createInvitationLink.setText(lang(lng_group_invite_create));
	} else {
		_createInvitationLink.setText(lang(lng_group_invite_create_new));
		_invitationText = _peerChat ? _peerChat->invitationUrl : _peerChannel->invitationUrl;
		if (_invitationText.startsWith(qstr("http://"), Qt::CaseInsensitive)) {
			_invitationText = _invitationText.mid(7);
		} else if (_invitationText.startsWith(qstr("https://"), Qt::CaseInsensitive)) {
			_invitationText = _invitationText.mid(8);
		}
	}
}

void ProfileInner::updateBotLinksVisibility() {
	if (!_peerUser || !_peerUser->botInfo || _peerUser->botInfo->commands.isEmpty()) {
		_botSettings.hide();
		_botHelp.hide();
		return;
	}
	bool hasSettings = false, hasHelp = false;
	for (int32 i = 0, l = _peerUser->botInfo->commands.size(); i != l; ++i) {
		QString cmd = _peerUser->botInfo->commands.at(i).command;
		hasSettings |= !cmd.compare(qsl("settings"), Qt::CaseInsensitive);
		hasHelp |= !cmd.compare(qsl("help"), Qt::CaseInsensitive);
		if (hasSettings && hasHelp) break;
	}
	_botSettings.setVisible(hasSettings);
	_botHelp.setVisible(hasHelp);
}

QString ProfileInner::overviewLinkText(int32 type, int32 count) {
	switch (type) {
	case OverviewPhotos: return lng_profile_photos(lt_count, count);
	case OverviewVideos: return lng_profile_videos(lt_count, count);
	case OverviewAudioDocuments: return lng_profile_songs(lt_count, count);
	case OverviewDocuments: return lng_profile_files(lt_count, count);
	case OverviewAudios: return lng_profile_audios(lt_count, count);
	case OverviewLinks: return lng_profile_shared_links(lt_count, count);
	}
	return QString();
}

ProfileWidget::ProfileWidget(QWidget *parent, PeerData *peer) : TWidget(parent)
, _scroll(this, st::setScroll)
, _inner(this, &_scroll, peer)
, _a_show(animation(this, &ProfileWidget::step_show))
, _sideShadow(this, st::shadowColor)
, _topShadow(this, st::shadowColor)
, _inGrab(false) {
	_scroll.setWidget(&_inner);
	_scroll.move(0, 0);
	_inner.move(0, 0);
	_scroll.show();

	_sideShadow.setVisible(cWideMode());

	connect(&_scroll, SIGNAL(scrolled()), &_inner, SLOT(updateSelected()));
	connect(&_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));
}

void ProfileWidget::onScroll() {
	_inner.loadProfilePhotos(_scroll.scrollTop());
	if (!_scroll.isHidden() && _scroll.scrollTop() < _scroll.scrollTopMax()) {
		_inner.allowDecreaseHeight(_scroll.scrollTopMax() - _scroll.scrollTop());
	}
	if (peer()->isMegagroup() && !peer()->asChannel()->mgInfo->lastParticipants.isEmpty() && peer()->asChannel()->mgInfo->lastParticipants.size() < peer()->asChannel()->count) {
		if (_scroll.scrollTop() + PreloadHeightsCount * _scroll.height() > _scroll.scrollTopMax()) {
			App::api()->requestLastParticipants(peer()->asChannel(), false);
		}
	}
}

void ProfileWidget::resizeEvent(QResizeEvent *e) {
	int32 addToY = App::main() ? App::main()->contentScrollAddToY() : 0;
	int32 newScrollY = _scroll.scrollTop() + addToY;
	_scroll.resize(size());
	_inner.resize(width(), _inner.height());
	if (!_scroll.isHidden()) {
		if (addToY) {
			_scroll.scrollToY(newScrollY);
		}
		if (_scroll.scrollTop() < _scroll.scrollTopMax()) {
			_inner.allowDecreaseHeight(_scroll.scrollTopMax() - _scroll.scrollTop());
		}
	}

	_topShadow.resize(width() - ((cWideMode() && !_inGrab) ? st::lineWidth : 0), st::lineWidth);
	_topShadow.moveToLeft((cWideMode() && !_inGrab) ? st::lineWidth : 0, 0);
	_sideShadow.resize(st::lineWidth, height());
	_sideShadow.moveToLeft(0, 0);
}

void ProfileWidget::mousePressEvent(QMouseEvent *e) {
}

void ProfileWidget::paintEvent(QPaintEvent *e) {
	if (App::wnd() && App::wnd()->contentOverlapped(this, e)) return;

	Painter p(this);
	if (_a_show.animating()) {
		if (a_coordOver.current() > 0) {
			p.drawPixmap(QRect(0, 0, a_coordOver.current(), height()), _cacheUnder, QRect(-a_coordUnder.current() * cRetinaFactor(), 0, a_coordOver.current() * cRetinaFactor(), height() * cRetinaFactor()));
			p.setOpacity(a_shadow.current() * st::slideFadeOut);
			p.fillRect(0, 0, a_coordOver.current(), height(), st::black->b);
			p.setOpacity(1);
		}
		p.drawPixmap(a_coordOver.current(), 0, _cacheOver);
		p.setOpacity(a_shadow.current());
		p.drawPixmap(QRect(a_coordOver.current() - st::slideShadow.pxWidth(), 0, st::slideShadow.pxWidth(), height()), App::sprite(), st::slideShadow);
	} else {
		p.fillRect(e->rect(), st::white->b);
	}
}

void ProfileWidget::dragEnterEvent(QDragEnterEvent *e) {
}

void ProfileWidget::dropEvent(QDropEvent *e) {
}

void ProfileWidget::keyPressEvent(QKeyEvent *e) {
	return _inner.keyPressEvent(e);
}

void ProfileWidget::paintTopBar(QPainter &p, float64 over, int32 decreaseWidth) {
	if (_a_show.animating()) {
		p.drawPixmap(a_coordUnder.current(), 0, _cacheTopBarUnder);
		p.drawPixmap(a_coordOver.current(), 0, _cacheTopBarOver);
		p.setOpacity(a_shadow.current());
		p.drawPixmap(QRect(a_coordOver.current() - st::slideShadow.pxWidth(), 0, st::slideShadow.pxWidth(), st::topBarHeight), App::sprite(), st::slideShadow);
		return;
	}

	p.setOpacity(st::topBarBackAlpha + (1 - st::topBarBackAlpha) * over);
	p.drawPixmap(QPoint(st::topBarBackPadding.left(), (st::topBarHeight - st::topBarBackImg.pxHeight()) / 2), App::sprite(), st::topBarBackImg);
	p.setFont(st::topBarBackFont->f);
	p.setPen(st::topBarBackColor->p);
	p.drawText(st::topBarBackPadding.left() + st::topBarBackImg.pxWidth() + st::topBarBackPadding.right(), (st::topBarHeight - st::topBarBackFont->height) / 2 + st::topBarBackFont->ascent, lang(peer()->isUser() ? lng_profile_info : ((peer()->isChat() || peer()->isMegagroup()) ? lng_profile_group_info : lng_profile_channel_info)));
}

void ProfileWidget::topBarClick() {
	App::main()->showBackFromStack();
}

PeerData *ProfileWidget::peer() const {
	return _inner.peer();
}

int32 ProfileWidget::lastScrollTop() const {
	return _scroll.scrollTop();
}

void ProfileWidget::animShow(const QPixmap &bgAnimCache, const QPixmap &bgAnimTopBarCache, bool back, int32 lastScrollTop) {
	if (App::app()) App::app()->mtpPause();

	if (!cAutoPlayGif()) {
		App::stopGifItems();
	}

	(back ? _cacheOver : _cacheUnder) = bgAnimCache;
	(back ? _cacheTopBarOver : _cacheTopBarUnder) = bgAnimTopBarCache;
	if (lastScrollTop >= 0) _scroll.scrollToY(lastScrollTop);
	(back ? _cacheUnder : _cacheOver) = myGrab(this);
	App::main()->topBar()->stopAnim();
	(back ? _cacheTopBarUnder : _cacheTopBarOver) = myGrab(App::main()->topBar());
	App::main()->topBar()->startAnim();

	_scroll.hide();
	_topShadow.hide();

	a_coordUnder = back ? anim::ivalue(-qFloor(st::slideShift * width()), 0) : anim::ivalue(0, -qFloor(st::slideShift * width()));
	a_coordOver = back ? anim::ivalue(0, width()) : anim::ivalue(width(), 0);
	a_shadow = back ? anim::fvalue(1, 0) : anim::fvalue(0, 1);
	_a_show.start();

	show();

	App::main()->topBar()->update();
	_inner.setFocus();
}

void ProfileWidget::step_show(float64 ms, bool timer) {
	float64 dt = ms / st::slideDuration;
	if (dt >= 1) {
		_a_show.stop();
		_sideShadow.setVisible(cWideMode());
		_topShadow.show();

		a_coordUnder.finish();
		a_coordOver.finish();
		a_shadow.finish();
		_cacheUnder = _cacheOver = _cacheTopBarUnder = _cacheTopBarOver = QPixmap();
		App::main()->topBar()->stopAnim();

		_scroll.show();
		_inner.start();
		activate();

		if (App::app()) App::app()->mtpUnpause();
	} else {
		a_coordUnder.update(dt, st::slideFunction);
		a_coordOver.update(dt, st::slideFunction);
		a_shadow.update(dt, st::slideFunction);
	}
	if (timer) {
		update();
		App::main()->topBar()->update();
	}
}

void ProfileWidget::updateOnlineDisplay() {
	_inner.updateOnlineDisplay();
	updateOnlineDisplayTimer();
}

void ProfileWidget::updateOnlineDisplayTimer() {
	_inner.updateOnlineDisplayTimer();
}

void ProfileWidget::peerUsernameChanged() {
	_inner.peerUsernameChanged();
}

void ProfileWidget::updateNotifySettings() {
	_inner.updateNotifySettings();
}

void ProfileWidget::mediaOverviewUpdated(PeerData *peer, MediaOverviewType type) {
	int32 addToScroll = _inner.mediaOverviewUpdated(peer, type);
	if (!_scroll.isHidden() && addToScroll && _scroll.geometry().contains(mapFromGlobal(QCursor::pos()))) {
		if (addToScroll > 0 && _scroll.scrollTop() + addToScroll > _scroll.scrollTopMax()) {
			_inner.requestHeight(_scroll.scrollTop() + addToScroll + _scroll.height());
		}
		_scroll.scrollToY(_scroll.scrollTop() + addToScroll);
	}
}

void ProfileWidget::updateWideMode() {
	_sideShadow.setVisible(cWideMode());
}

void ProfileWidget::clear() {
	if (_inner.peer() && _inner.peer()->isUser() && _inner.peer()->asUser()->botInfo) {
		_inner.peer()->asUser()->botInfo->startGroupToken = QString();
	}
}

ProfileWidget::~ProfileWidget() {
}

void ProfileWidget::activate() {
	if (_scroll.isHidden()) {
		setFocus();
	} else {
		_inner.setFocus();
	}
}
