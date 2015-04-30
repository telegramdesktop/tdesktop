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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "lang.h"

#include "audio.h"
#include "application.h"
#include "fileuploader.h"
#include "mainwidget.h"
#include <libexif/exif-data.h>

#include "localstorage.h"

namespace {
	bool quiting = false;

	UserData *self = 0;

	typedef QHash<PeerId, PeerData*> PeersData;
	PeersData peersData;

	typedef QMap<PeerData*, bool> MutedPeers;
	MutedPeers mutedPeers;

	typedef QHash<PhotoId, PhotoData*> PhotosData;
	PhotosData photosData;

	typedef QHash<VideoId, VideoData*> VideosData;
	VideosData videosData;

	typedef QHash<AudioId, AudioData*> AudiosData;
	AudiosData audiosData;

	typedef QHash<QString, ImageLinkData*> ImageLinksData;
	ImageLinksData imageLinksData;

	typedef QHash<DocumentId, DocumentData*> DocumentsData;
	DocumentsData documentsData;

	typedef QHash<WebPageId, WebPageData*> WebPagesData;
	WebPagesData webPagesData;

	VideoItems videoItems;
	AudioItems audioItems;
	DocumentItems documentItems;
	WebPageItems webPageItems;
	typedef QMap<HistoryItem*, QMap<HistoryReply*, bool> > RepliesTo;
	RepliesTo repliesTo;

	Histories histories;

	typedef QHash<MsgId, HistoryItem*> MsgsData;
	MsgsData msgsData;
	int32 maxMsgId = 0;

	typedef QMap<uint64, MsgId> RandomData;
	RandomData randomData;

	HistoryItem *hoveredItem = 0, *pressedItem = 0, *hoveredLinkItem = 0, *pressedLinkItem = 0, *contextItem = 0, *mousedItem = 0;

	QPixmap *sprite = 0, *emojis = 0;

	typedef QMap<uint32, QPixmap> EmojisMap;
	EmojisMap mainEmojisMap;
	QMap<int32, EmojisMap> otherEmojisMap;

	int32 serviceImageCacheSize = 0;

	typedef QLinkedList<PhotoData*> LastPhotosList;
	LastPhotosList lastPhotos;
	typedef QHash<PhotoData*, LastPhotosList::iterator> LastPhotosMap;
	LastPhotosMap lastPhotosMap;

	style::color _msgServiceBG;
	style::color _historyScrollBarColor;
	style::color _historyScrollBgColor;
	style::color _historyScrollBarOverColor;
	style::color _historyScrollBgOverColor;
	style::color _introPointHoverColor;
}

namespace App {

	QString formatPhone(QString phone) {
		return '+' + phone.replace(QRegularExpression(qsl("[^\\d]")), QString());
	}

	Application *app() {
		return Application::app();
	}

	Window *wnd() {
		return Application::wnd();
	}

	MainWidget *main() {
		Window *w(wnd());
		return w ? w->mainWidget() : 0;
	}

	SettingsWidget *settings() {
		Window *w(wnd());
		return w ? w->settingsWidget() : 0;
	}

	bool passcoded() {
		Window *w(wnd());
		return w ? w->passcodeWidget() : 0;
	}

	FileUploader *uploader() {
		return app() ? app()->uploader() : 0;
	}

	ApiWrap *api() {
		return main() ? main()->api() : 0;
	}

	void showSettings() {
		Window *w(wnd());
		if (w) w->showSettings();
	}

	bool loggedOut() {
		Window *w(wnd());
		if (cHasPasscode()) {
			cSetHasPasscode(false);
		}
		if (w) {
			w->tempDirDelete(Local::ClearManagerAll);
			w->notifyClearFast();
			w->setupIntro(true);
		}
		MainWidget *m(main());
		if (m) m->destroyData();
		MTP::authed(0);
		Local::reset();

		cSetOtherOnline(0);
		histories().clear();
		globalNotifyAllPtr = UnknownNotifySettings;
		globalNotifyUsersPtr = UnknownNotifySettings;
		globalNotifyChatsPtr = UnknownNotifySettings;
		App::uploader()->clear();
		clearStorageImages();
		if (w) {
			w->getTitle()->updateBackButton();
			w->updateTitleStatus();
			w->getTitle()->resizeEvent(0);
		}
		return true;
	}

	void logOut() {
		if (MTP::started()) {
			MTP::logoutKeys(rpcDone(&loggedOut), rpcFail(&loggedOut));
		} else {
			loggedOut();
			MTP::start();
		}
	}

	PeerId peerFromMTP(const MTPPeer &peer_id) {
		switch (peer_id.type()) {
		case mtpc_peerChat: return peerFromChat(peer_id.c_peerChat().vchat_id);
		case mtpc_peerUser: return peerFromUser(peer_id.c_peerUser().vuser_id);
		}
		return 0;
	}

	PeerId peerFromChat(int32 chat_id) {
		return 0x100000000L | uint64(uint32(chat_id));
	}

	PeerId peerFromUser(int32 user_id) {
		return uint64(uint32(user_id));
	}
	
	MTPpeer peerToMTP(const PeerId &peer_id) {
		return (peer_id & 0x100000000L) ? MTP_peerChat(MTP_int(int32(peer_id & 0xFFFFFFFFL))) : MTP_peerUser(MTP_int(int32(peer_id & 0xFFFFFFFFL)));
	}

    int32 userFromPeer(const PeerId &peer_id) {
        return (peer_id & 0x100000000L) ? 0 : int32(peer_id & 0xFFFFFFFFL);
    }
    int32 chatFromPeer(const PeerId &peer_id) {
        return (peer_id & 0x100000000L) ? int32(peer_id & 0xFFFFFFFFL) : 0;
    }

	int32 onlineForSort(int32 online, int32 now) {
		if (online <= 0) {
			switch (online) {
			case 0:
			case -1: return online;

			case -2: {
				QDate yesterday(date(now).date());
				return int32(QDateTime(yesterday.addDays(-3)).toTime_t());
			} break;

			case -3: {
				QDate weekago(date(now).date());
				return int32(QDateTime(weekago.addDays(-7)).toTime_t());
			} break;

			case -4: {
				QDate monthago(date(now).date());
				return int32(QDateTime(monthago.addDays(-30)).toTime_t());
			} break;
			}
			return -online;
		}
		return online;
	}

	int32 onlineWillChangeIn(int32 online, int32 now) {
        if (online <= 0) {
            if (-online > now) return -online - now;
            return 86400;
        }
		if (online > now) {
			return online - now;
		}
		int32 minutes = (now - online) / 60;
		if (minutes < 60) {
			return (minutes + 1) * 60 - (now - online);
		}
		int32 hours = (now - online) / 3600;
		if (hours < 12) {
			return (hours + 1) * 3600 - (now - online);
		}
		QDateTime dNow(date(now)), dTomorrow(dNow.date().addDays(1));
		return dNow.secsTo(dTomorrow);
	}

	QString onlineText(UserData *user, int32 now, bool precise) {
		if (isServiceUser(user->id)) {
			return lang(lng_status_service_notifications);
		}
		int32 online = user->onlineTill;
		if (online <= 0) {
			switch (online) {
			case 0: return lang(lng_status_offline);
            case -1: return lang(lng_status_invisible);
			case -2: return lang(lng_status_recently);
			case -3: return lang(lng_status_last_week);
			case -4: return lang(lng_status_last_month);
			}
            return (-online > now) ? lang(lng_status_online) : lang(lng_status_recently);
		}
		if (online > now) {
			return lang(lng_status_online);
		}
		QString when;
		if (precise) {
			QDateTime dOnline(date(online)), dNow(date(now));
			if (dOnline.date() == dNow.date()) {
				return lng_status_lastseen_today(lt_time, dOnline.time().toString(cTimeFormat()));
			} else if (dOnline.date().addDays(1) == dNow.date()) {
				return lng_status_lastseen_yesterday(lt_time, dOnline.time().toString(cTimeFormat()));
			}
			return lng_status_lastseen_date_time(lt_date, dOnline.date().toString(qsl("dd.MM.yy")), lt_time, dOnline.time().toString(cTimeFormat()));
		}
		int32 minutes = (now - online) / 60;
		if (!minutes) {
			return lang(lng_status_lastseen_now);
		} else if (minutes < 60) {
			return lng_status_lastseen_minutes(lt_count, minutes);
		}
		int32 hours = (now - online) / 3600;
		if (hours < 12) {
			return lng_status_lastseen_hours(lt_count, hours);
		}
		QDateTime dOnline(date(online)), dNow(date(now));
		if (dOnline.date() == dNow.date()) {
			return lng_status_lastseen_today(lt_time, dOnline.time().toString(cTimeFormat()));
		} else if (dOnline.date().addDays(1) == dNow.date()) {
			return lng_status_lastseen_yesterday(lt_time, dOnline.time().toString(cTimeFormat()));
		}
		return lng_status_lastseen_date(lt_date, dOnline.date().toString(qsl("dd.MM.yy")));
	}

	bool onlineColorUse(int32 online, int32 now) {
		if (online <= 0) {
			switch (online) {
			case 0:
			case -1:
			case -2:
			case -3:
			case -4: return false;
			}
			return (-online > now);
		}
		return (online > now);
	}

	UserData *feedUsers(const MTPVector<MTPUser> &users) {
        UserData *data = 0;
		const QVector<MTPUser> &v(users.c_vector().v);
		for (QVector<MTPUser>::const_iterator i = v.cbegin(), e = v.cend(); i != e; ++i) {
			const MTPuser &user(*i);
            data = 0;
			bool wasContact = false;
			const MTPUserStatus *status = 0, emptyStatus = MTP_userStatusEmpty();

			switch (user.type()) {
			case mtpc_userEmpty: {
				const MTPDuserEmpty &d(user.c_userEmpty());

				PeerId peer(peerFromUser(d.vid.v));
				data = App::user(peer);
				data->input = MTP_inputPeerContact(d.vid);
				data->inputUser = MTP_inputUserContact(d.vid);
				data->setName(lang(lng_deleted), QString(), QString(), QString());
				data->setPhoto(MTP_userProfilePhotoEmpty());
				data->access = UserNoAccess;
				wasContact = (data->contact > 0);
				status = &emptyStatus;
				data->contact = -1;
			} break;
			case mtpc_userDeleted: {
				const MTPDuserDeleted &d(user.c_userDeleted());

				PeerId peer(peerFromUser(d.vid.v));
				data = App::user(peer);
				data->input = MTP_inputPeerContact(d.vid);
				data->inputUser = MTP_inputUserContact(d.vid);
				data->setName(lang(lng_deleted), QString(), QString(), QString());
//				data->setName(textOneLine(qs(d.vfirst_name)), textOneLine(qs(d.vlast_name)), QString(), textOneLine(qs(d.vusername)));
				data->setPhoto(MTP_userProfilePhotoEmpty());
				data->access = UserNoAccess;
				wasContact = (data->contact > 0);
				status = &emptyStatus;
				data->contact = -1;
			} break;
			case mtpc_userSelf: {
				const MTPDuserSelf &d(user.c_userSelf());

				PeerId peer(peerFromUser(d.vid.v));
				data = App::user(peer);
				data->input = MTP_inputPeerSelf();
				data->inputUser = MTP_inputUserSelf();
				data->setName(textOneLine(qs(d.vfirst_name)), textOneLine(qs(d.vlast_name)), QString(), textOneLine(qs(d.vusername)));
				data->setPhoto(d.vphoto);
				data->setPhone(qs(d.vphone));
				data->access = 0;
				wasContact = (data->contact > 0);
				status = &d.vstatus;

				if (::self != data) {
					::self = data;
					if (App::wnd()) App::wnd()->updateGlobalMenu();
				}
			} break;
			case mtpc_userContact: {
				const MTPDuserContact &d(user.c_userContact());

				PeerId peer(peerFromUser(d.vid.v));
				data = App::user(peer);
				data->input = MTP_inputPeerContact(d.vid);
				data->inputUser = MTP_inputUserContact(d.vid);
				data->setName(textOneLine(qs(d.vfirst_name)), textOneLine(qs(d.vlast_name)), QString(), textOneLine(qs(d.vusername)));
				data->setPhoto(d.vphoto);
				data->setPhone(qs(d.vphone));
				data->access = d.vaccess_hash.v;
				wasContact = (data->contact > 0);
				data->contact = 1;
				status = &d.vstatus;
			} break;
			case mtpc_userRequest: {
				const MTPDuserRequest &d(user.c_userRequest());
				
				PeerId peer(peerFromUser(d.vid.v));
				data = App::user(peer);
				data->input = MTP_inputPeerForeign(d.vid, d.vaccess_hash);
				data->inputUser = MTP_inputUserForeign(d.vid, d.vaccess_hash);
				data->setPhone(qs(d.vphone));
				data->setName(textOneLine(qs(d.vfirst_name)), textOneLine(qs(d.vlast_name)), (!isServiceUser(data->id) && !data->phone.isEmpty()) ? formatPhone(data->phone) : QString(), textOneLine(qs(d.vusername)));
				data->setPhoto(d.vphoto);
				data->access = d.vaccess_hash.v;
				wasContact = (data->contact > 0);
				data->contact = 0;
				status = &d.vstatus;
			} break;
			case mtpc_userForeign: {
				const MTPDuserForeign &d(user.c_userForeign());

				PeerId peer(peerFromUser(d.vid.v));
				data = App::user(peer);
				data->input = MTP_inputPeerForeign(d.vid, d.vaccess_hash);
				data->inputUser = MTP_inputUserForeign(d.vid, d.vaccess_hash);
				data->setName(textOneLine(qs(d.vfirst_name)), textOneLine(qs(d.vlast_name)), QString(), textOneLine(qs(d.vusername)));
				data->setPhoto(d.vphoto);
				data->access = d.vaccess_hash.v;
				wasContact = (data->contact > 0);
				data->contact = -1;
				status = &d.vstatus;
			} break;
			}

            if (!data) continue;

			data->loaded = true;
			if (status) switch (status->type()) {
			case mtpc_userStatusEmpty: data->onlineTill = 0; break;
			case mtpc_userStatusRecently:
				if (data->onlineTill > -10) { // don't modify pseudo-online
					data->onlineTill = -2;
				}
			break;
			case mtpc_userStatusLastWeek: data->onlineTill = -3; break;
			case mtpc_userStatusLastMonth: data->onlineTill = -4; break;
			case mtpc_userStatusOffline: data->onlineTill = status->c_userStatusOffline().vwas_online.v; break;
			case mtpc_userStatusOnline: data->onlineTill = status->c_userStatusOnline().vexpires.v; break;
			}

            if (data->contact < 0 && !data->phone.isEmpty() && int32(data->id & 0xFFFFFFFF) != MTP::authedId()) {
				data->contact = 0;
			}
			if (data->contact > 0 && !wasContact) {
				App::main()->addNewContact(data->id & 0xFFFFFFFF, false);
			} else if (wasContact && data->contact <= 0) {
				App::main()->removeContact(data);
			}

			if (App::main()) App::main()->peerUpdated(data);
		}

		return data;
	}

	ChatData *feedChats(const MTPVector<MTPChat> &chats) {
		ChatData *data = 0;
		const QVector<MTPChat> &v(chats.c_vector().v);
		for (QVector<MTPChat>::const_iterator i = v.cbegin(), e = v.cend(); i != e; ++i) {
			const MTPchat &chat(*i);
			data = 0;
			QString title;
			switch (chat.type()) {
			case mtpc_chat: {
				const MTPDchat &d(chat.c_chat());
				title = qs(d.vtitle);

				PeerId peer(peerFromChat(d.vid.v));
				data = App::chat(peer);
				data->input = MTP_inputPeerChat(d.vid);
				data->setPhoto(d.vphoto);
				data->date = d.vdate.v;
				data->count = d.vparticipants_count.v;
				data->left = d.vleft.v;
				data->forbidden = false;
				data->access = 0;
				if (data->version < d.vversion.v) {
					data->version = d.vversion.v;
					data->participants = ChatData::Participants();
				}
			} break;
			case mtpc_chatForbidden: {
				const MTPDchatForbidden &d(chat.c_chatForbidden());
				title = qs(d.vtitle);

				PeerId peer(peerFromChat(d.vid.v));
				data = App::chat(peer);
				data->input = MTP_inputPeerChat(d.vid);
				data->setPhoto(MTP_chatPhotoEmpty());
				data->date = 0;
				data->count = -1;
				data->left = false;
				data->forbidden = true;
				data->access = 0;
			} break;
			case mtpc_geoChat: {
				const MTPDgeoChat &d(chat.c_geoChat());
				data = 0;
/*				title = qs(d.vtitle);

				PeerId peer(peerFromChat(d.vid.v));
				data = App::chat(peer);
				data->input = MTP_inputPeerChat(d.vid);
				data->setPhoto(d.vphoto);
				data->date = d.vdate.v;
				data->count = d.vparticipants_count.v;
				data->left = false;
				data->forbidden = false;
				data->access = d.vaccess_hash.v;
				if (data->version < d.vversion.v) {
					data->version = d.vversion.v;
					data->participants = ChatData::Participants();
				}/**/
			} break;
			}
			if (!data) continue;

			data->loaded = true;
			data->updateName(title.trimmed(), QString(), QString());

			if (App::main()) App::main()->peerUpdated(data);
		}
		return data;
	}

	void feedParticipants(const MTPChatParticipants &p) {
		switch (p.type()) {
		case mtpc_chatParticipantsForbidden: {
			const MTPDchatParticipantsForbidden &d(p.c_chatParticipantsForbidden());
			ChatData *chat = App::chat(d.vchat_id.v);
			chat->count = -1;
			if (App::main()) App::main()->peerUpdated(chat);
		} break;
		case mtpc_chatParticipants: {
			const MTPDchatParticipants &d(p.c_chatParticipants());
			ChatData *chat = App::chat(d.vchat_id.v);
			chat->admin = d.vadmin_id.v;
			if (chat->version <= d.vversion.v) {
				chat->version = d.vversion.v;
				const QVector<MTPChatParticipant> &v(d.vparticipants.c_vector().v);
				chat->count = v.size();
				int32 pversion = chat->participants.isEmpty() ? 1 : (chat->participants.begin().value() + 1);
				chat->cankick = ChatData::CanKick();
				for (QVector<MTPChatParticipant>::const_iterator i = v.cbegin(), e = v.cend(); i != e; ++i) {
					UserData *user = App::userLoaded(i->c_chatParticipant().vuser_id.v);
					if (user) {
						chat->participants[user] = pversion;
						if (i->c_chatParticipant().vinviter_id.v == MTP::authedId()) {
							chat->cankick[user] = true;
						}
					} else {
						chat->participants = ChatData::Participants();
						break;
					}
				}
				if (!chat->participants.isEmpty()) {
					for (ChatData::Participants::iterator i = chat->participants.begin(), e = chat->participants.end(); i != e;) {
						if (i.value() < pversion) {
							i = chat->participants.erase(i);
						} else {
							++i;
						}
					}
				}
				if (App::main()) App::main()->peerUpdated(chat);
			}
		} break;
		}
	}

	void feedParticipantAdd(const MTPDupdateChatParticipantAdd &d) {
		ChatData *chat = App::chat(d.vchat_id.v);
		if (chat->version <= d.vversion.v && chat->count >= 0) {
			chat->version = d.vversion.v;
			UserData *user = App::userLoaded(d.vuser_id.v);
			if (user) {
				if (chat->participants.isEmpty() && chat->count) {
					chat->count++;
				} else if (chat->participants.find(user) == chat->participants.end()) {
					chat->participants[user] = (chat->participants.isEmpty() ? 1 : chat->participants.begin().value());
					if (d.vinviter_id.v == MTP::authedId()) {
						chat->cankick[user] = true;
					} else {
						chat->cankick.remove(user);
					}
					chat->count++;
				}
			} else {
				chat->participants = ChatData::Participants();
				chat->count++;
			}
			if (App::main()) App::main()->peerUpdated(chat);
		}
	}

	void feedParticipantDelete(const MTPDupdateChatParticipantDelete &d) {
		ChatData *chat = App::chat(d.vchat_id.v);
		if (chat->version <= d.vversion.v && chat->count > 0) {
			chat->version = d.vversion.v;
			UserData *user = App::userLoaded(d.vuser_id.v);
			if (user) {
				if (chat->participants.isEmpty()) {
					chat->count--;
				} else {
					ChatData::Participants::iterator i = chat->participants.find(user);
					if (i != chat->participants.end()) {
						chat->participants.erase(i);
						chat->count--;
					}
				}
			} else {
				chat->participants = ChatData::Participants();
				chat->count--;
			}
			if (App::main()) App::main()->peerUpdated(chat);
		}
	}

	void feedMsgs(const MTPVector<MTPMessage> &msgs, int msgsState) {
		const QVector<MTPMessage> &v(msgs.c_vector().v);
		QMap<int32, int32> msgsIds;
		for (int32 i = 0, l = v.size(); i < l; ++i) {
			const MTPMessage &msg(v.at(i));
			switch (msg.type()) {
			case mtpc_message: msgsIds.insert(msg.c_message().vid.v, i); break;
			case mtpc_messageEmpty: msgsIds.insert(msg.c_messageEmpty().vid.v, i); break;
			case mtpc_messageService: msgsIds.insert(msg.c_messageService().vid.v, i); break;
			}
		}
		for (QMap<int32, int32>::const_iterator i = msgsIds.cbegin(), e = msgsIds.cend(); i != e; ++i) {
			histories().addToBack(v.at(*i), msgsState);
		}
	}

	int32 maxMsgId() {
		return ::maxMsgId;
	}

	ImagePtr image(const MTPPhotoSize &size) {
		switch (size.type()) {
		case mtpc_photoSize: {
			const MTPDphotoSize &d(size.c_photoSize());
			if (d.vlocation.type() == mtpc_fileLocation) {
				const MTPDfileLocation &l(d.vlocation.c_fileLocation());
				return ImagePtr(d.vw.v, d.vh.v, l.vdc_id.v, l.vvolume_id.v, l.vlocal_id.v, l.vsecret.v, d.vsize.v);
			}
		} break;
		case mtpc_photoCachedSize: {
			const MTPDphotoCachedSize &d(size.c_photoCachedSize());
			if (d.vlocation.type() == mtpc_fileLocation) {
				const MTPDfileLocation &l(d.vlocation.c_fileLocation());
				const string &s(d.vbytes.c_string().v);
				QByteArray bytes(s.data(), s.size());
				return ImagePtr(d.vw.v, d.vh.v, l.vdc_id.v, l.vvolume_id.v, l.vlocal_id.v, l.vsecret.v, bytes);
			} else if (d.vlocation.type() == mtpc_fileLocationUnavailable) {
				const string &s(d.vbytes.c_string().v);
				QByteArray bytes(s.data(), s.size());
				return ImagePtr(d.vw.v, d.vh.v, 0, 0, 0, 0, bytes);
			}				
		} break;
		}
		return ImagePtr();
	}

	void feedWereRead(const QVector<MTPint> &msgsIds) {
		for (QVector<MTPint>::const_iterator i = msgsIds.cbegin(), e = msgsIds.cend(); i != e; ++i) {
			MsgsData::const_iterator j = msgsData.constFind(i->v);
			if (j != msgsData.cend()) {
				(*j)->markRead();
			}
		}
	}
	
	void feedInboxRead(const PeerId &peer, int32 upTo) {
		History *h = App::historyLoaded(peer);
		if (h) {
			h->inboxRead(upTo);
		}
	}

	void feedOutboxRead(const PeerId &peer, int32 upTo) {
		History *h = App::historyLoaded(peer);
		if (h) {
			h->outboxRead(upTo);
		}
	}

	void feedWereDeleted(const QVector<MTPint> &msgsIds) {
		bool resized = false;
		for (QVector<MTPint>::const_iterator i = msgsIds.cbegin(), e = msgsIds.cend(); i != e; ++i) {
			MsgsData::const_iterator j = msgsData.constFind(i->v);
			if (j != msgsData.cend()) {
				History *h = (*j)->history();
				(*j)->destroy();
				if (App::main() && h->peer == App::main()->peer()) {
					resized = true;
				}
			}
		}
		if (resized) {
			App::main()->itemResized(0);
		}
	}

	void feedUserLinks(const MTPVector<MTPcontacts_Link> &links) {
		const QVector<MTPcontacts_Link> &v(links.c_vector().v);
		for (QVector<MTPcontacts_Link>::const_iterator i = v.cbegin(), e = v.cend(); i != e; ++i) {
			const MTPDcontacts_link &dv(i->c_contacts_link());
			feedUsers(MTP_vector<MTPUser>(1, dv.vuser));
			MTPint userId(MTP_int(0));
			switch (dv.vuser.type()) {
			case mtpc_userEmpty: userId = dv.vuser.c_userEmpty().vid; break;
			case mtpc_userDeleted: userId = dv.vuser.c_userDeleted().vid; break;
			case mtpc_userContact: userId = dv.vuser.c_userContact().vid; break;
			case mtpc_userSelf: userId = dv.vuser.c_userSelf().vid; break;
			case mtpc_userRequest: userId = dv.vuser.c_userRequest().vid; break;
			case mtpc_userForeign: userId = dv.vuser.c_userForeign().vid; break;
			}
			if (userId.v) {
				feedUserLink(userId, dv.vmy_link, dv.vforeign_link);
			}
		}
	}

	void feedUserLink(MTPint userId, const MTPContactLink &myLink, const MTPContactLink &foreignLink) {
		UserData *user = userLoaded(userId.v);
		if (user) {
			bool wasContact = (user->contact > 0);
			switch (myLink.type()) {
			case mtpc_contactLinkContact:
				user->contact = 1;
			break;
			case mtpc_contactLinkHasPhone:
				user->contact = 0;
			break;
			case mtpc_contactLinkNone:
			case mtpc_contactLinkUnknown:
				user->contact = -1;
			break;
			}
			if (user->contact > 0) {
				if (!wasContact) {
					App::main()->addNewContact(App::userFromPeer(user->id), false);
					if (user->input.type() != mtpc_inputPeerSelf) user->input = MTP_inputPeerContact(userId);
					if (user->inputUser.type() != mtpc_inputUserSelf) user->inputUser = MTP_inputUserContact(userId);
				}
			} else {
				if (user->access && user->access != UserNoAccess) {
					if (user->input.type() != mtpc_inputPeerSelf) user->input = MTP_inputPeerForeign(userId, MTP_long(user->access));
					if (user->inputUser.type() != mtpc_inputUserSelf) user->inputUser = MTP_inputUserForeign(userId, MTP_long(user->access));
				}
				if (user->contact < 0 && !user->phone.isEmpty() && App::userFromPeer(user->id) != MTP::authedId()) {
					user->contact = 0;
				}
				if (wasContact) {
					App::main()->removeContact(user);
				}
			}
			user->setName(textOneLine(user->firstName), textOneLine(user->lastName), (user->contact || isServiceUser(user->id) || user->phone.isEmpty()) ? QString() : App::formatPhone(user->phone), textOneLine(user->username));
			if (App::main()) App::main()->peerUpdated(user);
		}
	}

	PhotoData *feedPhoto(const MTPPhoto &photo, PhotoData *convert) {
		switch (photo.type()) {
		case mtpc_photo: {
			return feedPhoto(photo.c_photo(), convert);
		} break;
		case mtpc_photoEmpty: {
			return App::photo(photo.c_photoEmpty().vid.v, convert);
		} break;
		}
		return App::photo(0);
	}

	PhotoData *feedPhoto(const MTPPhoto &photo, const PreparedPhotoThumbs &thumbs) {
		const QPixmap *thumb = 0, *medium = 0, *full = 0;
		int32 thumbLevel = -1, mediumLevel = -1, fullLevel = -1;
		for (PreparedPhotoThumbs::const_iterator i = thumbs.cbegin(), e = thumbs.cend(); i != e; ++i) {
			int32 newThumbLevel = -1, newMediumLevel = -1, newFullLevel = -1;
			switch (i.key()) {
			case 's': newThumbLevel = 0; newMediumLevel = 5; newFullLevel = 4; break; // box 100x100
			case 'm': newThumbLevel = 2; newMediumLevel = 0; newFullLevel = 3; break; // box 320x320
			case 'x': newThumbLevel = 5; newMediumLevel = 3; newFullLevel = 1; break; // box 800x800
			case 'y': newThumbLevel = 6; newMediumLevel = 6; newFullLevel = 0; break; // box 1280x1280
			case 'w': newThumbLevel = 8; newMediumLevel = 8; newFullLevel = 2; break; // box 2560x2560
			case 'a': newThumbLevel = 1; newMediumLevel = 4; newFullLevel = 8; break; // crop 160x160
			case 'b': newThumbLevel = 3; newMediumLevel = 1; newFullLevel = 7; break; // crop 320x320
			case 'c': newThumbLevel = 4; newMediumLevel = 2; newFullLevel = 6; break; // crop 640x640
			case 'd': newThumbLevel = 7; newMediumLevel = 7; newFullLevel = 5; break; // crop 1280x1280
			}
			if (newThumbLevel < 0 || newMediumLevel < 0 || newFullLevel < 0) {
				continue;
			}
			if (thumbLevel < 0 || newThumbLevel < thumbLevel) {
				thumbLevel = newThumbLevel;
				thumb = &i.value();
			}
			if (mediumLevel < 0 || newMediumLevel < mediumLevel) {
				mediumLevel = newMediumLevel;
				medium = &i.value();
			}
			if (fullLevel < 0 || newFullLevel < fullLevel) {
				fullLevel = newFullLevel;
				full = &i.value();
			}
		}
		if (!thumb || !medium || !full) {
			return App::photo(0);
		}
		switch (photo.type()) {
		case mtpc_photo: {
			const MTPDphoto &ph(photo.c_photo());
			return App::photo(ph.vid.v, 0, ph.vaccess_hash.v, ph.vuser_id.v, ph.vdate.v, ImagePtr(*thumb, "JPG"), ImagePtr(*medium, "JPG"), ImagePtr(*full, "JPG"));
		} break;
		case mtpc_photoEmpty: return App::photo(photo.c_photoEmpty().vid.v);
		}
		return App::photo(0);
	}

	PhotoData *feedPhoto(const MTPDphoto &photo, PhotoData *convert) {
		const QVector<MTPPhotoSize> &sizes(photo.vsizes.c_vector().v);
		const MTPPhotoSize *thumb = 0, *medium = 0, *full = 0;
		int32 thumbLevel = -1, mediumLevel = -1, fullLevel = -1;
		for (QVector<MTPPhotoSize>::const_iterator i = sizes.cbegin(), e = sizes.cend(); i != e; ++i) {
			char size = 0;
			switch (i->type()) {
			case mtpc_photoSize: {
				const string &s(i->c_photoSize().vtype.c_string().v);
				if (s.size()) size = s[0];
			} break;
				
			case mtpc_photoCachedSize: {
				const string &s(i->c_photoCachedSize().vtype.c_string().v);
				if (s.size()) size = s[0];
			} break;
			}
			if (!size) continue;

			int32 newThumbLevel = -1, newMediumLevel = -1, newFullLevel = -1;
			switch (size) {
			case 's': newThumbLevel = 0; newMediumLevel = 5; newFullLevel = 4; break; // box 100x100
			case 'm': newThumbLevel = 2; newMediumLevel = 0; newFullLevel = 3; break; // box 320x320
			case 'x': newThumbLevel = 5; newMediumLevel = 3; newFullLevel = 1; break; // box 800x800
			case 'y': newThumbLevel = 6; newMediumLevel = 6; newFullLevel = 0; break; // box 1280x1280
			case 'w': newThumbLevel = 8; newMediumLevel = 8; newFullLevel = 2; break; // box 2560x2560
			case 'a': newThumbLevel = 1; newMediumLevel = 4; newFullLevel = 8; break; // crop 160x160
			case 'b': newThumbLevel = 3; newMediumLevel = 1; newFullLevel = 7; break; // crop 320x320
			case 'c': newThumbLevel = 4; newMediumLevel = 2; newFullLevel = 6; break; // crop 640x640
			case 'd': newThumbLevel = 7; newMediumLevel = 7; newFullLevel = 5; break; // crop 1280x1280
			}
			if (newThumbLevel < 0 || newMediumLevel < 0 || newFullLevel < 0) {
				continue;
			}
			if (thumbLevel < 0 || newThumbLevel < thumbLevel) {
				thumbLevel = newThumbLevel;
				thumb = &(*i);
			}
			if (mediumLevel < 0 || newMediumLevel < mediumLevel) {
				mediumLevel = newMediumLevel;
				medium = &(*i);
			}
			if (fullLevel < 0 || newFullLevel < fullLevel) {
				fullLevel = newFullLevel;
				full = &(*i);
			}
		}
		if (thumb && medium && full) {
			return App::photo(photo.vid.v, convert, photo.vaccess_hash.v, photo.vuser_id.v, photo.vdate.v, App::image(*thumb), App::image(*medium), App::image(*full));
		}
		return App::photo(photo.vid.v, convert);
	}
	
	VideoData *feedVideo(const MTPDvideo &video, VideoData *convert) {
		return App::video(video.vid.v, convert, video.vaccess_hash.v, video.vuser_id.v, video.vdate.v, video.vduration.v, video.vw.v, video.vh.v, App::image(video.vthumb), video.vdc_id.v, video.vsize.v);
	}

	AudioData *feedAudio(const MTPDaudio &audio, AudioData *convert) {
		return App::audio(audio.vid.v, convert, audio.vaccess_hash.v, audio.vuser_id.v, audio.vdate.v, qs(audio.vmime_type), audio.vduration.v, audio.vdc_id.v, audio.vsize.v);
	}

	DocumentData *feedDocument(const MTPdocument &document, const QPixmap &thumb) {
		switch (document.type()) {
		case mtpc_document: {
			const MTPDdocument &d(document.c_document());
			return App::document(d.vid.v, 0, d.vaccess_hash.v, d.vdate.v, d.vattributes.c_vector().v, qs(d.vmime_type), ImagePtr(thumb, "JPG"), d.vdc_id.v, d.vsize.v);
		} break;
		case mtpc_documentEmpty: return App::document(document.c_documentEmpty().vid.v);
		}
		return App::document(0);
	}

	DocumentData *feedDocument(const MTPdocument &document, DocumentData *convert) {
		switch (document.type()) {
		case mtpc_document: {
			return feedDocument(document.c_document(), convert);
		} break;
		case mtpc_documentEmpty: {
			return App::document(document.c_documentEmpty().vid.v, convert);
		} break;
		}
		return App::document(0);
	}

	DocumentData *feedDocument(const MTPDdocument &document, DocumentData *convert) {
		return App::document(document.vid.v, convert, document.vaccess_hash.v, document.vdate.v, document.vattributes.c_vector().v, qs(document.vmime_type), App::image(document.vthumb), document.vdc_id.v, document.vsize.v);
	}

	WebPageData *feedWebPage(const MTPDwebPage &webpage, WebPageData *convert) {
		return App::webPage(webpage.vid.v, convert, webpage.has_type() ? qs(webpage.vtype) : qsl("article"), qs(webpage.vurl), qs(webpage.vdisplay_url), webpage.has_site_name() ? qs(webpage.vsite_name) : QString(), webpage.has_title() ? qs(webpage.vtitle) : QString(), webpage.has_description() ? qs(webpage.vdescription) : QString(), webpage.has_photo() ? App::feedPhoto(webpage.vphoto) : 0, webpage.has_duration() ? webpage.vduration.v : 0, webpage.has_author() ? qs(webpage.vauthor) : QString(), 0);
	}

	WebPageData *feedWebPage(const MTPDwebPagePending &webpage, WebPageData *convert) {
		return App::webPage(webpage.vid.v, convert, QString(), QString(), QString(), QString(), QString(), QString(), 0, 0, QString(), webpage.vdate.v);
	}

	WebPageData *feedWebPage(const MTPWebPage &webpage) {
		switch (webpage.type()) {
		case mtpc_webPage: return App::feedWebPage(webpage.c_webPage());
		case mtpc_webPageEmpty: return App::webPage(webpage.c_webPageEmpty().vid.v);
		case mtpc_webPagePending: return App::feedWebPage(webpage.c_webPagePending());
		}
		return 0;
	}

	UserData *userLoaded(const PeerId &user) {
		PeerData *peer = peerLoaded(user);
		return (peer && peer->loaded) ? peer->asUser() : 0;
	}

	ChatData *chatLoaded(const PeerId &chat) {
		PeerData *peer = peerLoaded(chat);
		return (peer && peer->loaded) ? peer->asChat() : 0;
	}

	PeerData *peerLoaded(const PeerId &peer) {
		PeersData::const_iterator i = peersData.constFind(peer);
		return (i != peersData.cend()) ? i.value() : 0;
	}

	UserData *userLoaded(int32 user) {
		return userLoaded(App::peerFromUser(user));
	}

	ChatData *chatLoaded(int32 chat) {
		return chatLoaded(App::peerFromChat(chat));
	}

	UserData *curUser() {
		return user(MTP::authedId());
	}

	PeerData *peer(const PeerId &peer) {
		PeersData::const_iterator i = peersData.constFind(peer);
		if (i == peersData.cend()) {
			PeerData *newData = App::isChat(peer) ? (PeerData*)(new ChatData(peer)) : (PeerData*)(new UserData(peer));
			newData->input = MTPinputPeer(MTP_inputPeerEmpty());
			i = peersData.insert(peer, newData);
		}
		return i.value();
	}

	UserData *user(const PeerId &peer) {
		PeerData *d = App::peer(peer);
		return d->asUser();
	}

	UserData *user(int32 user) {
		return App::peer(App::peerFromUser(user))->asUser();
	}

	UserData *self() {
		return ::self;
	}

	UserData *userByName(const QString &username) {
		for (PeersData::const_iterator i = peersData.cbegin(), e = peersData.cend(); i != e; ++i) {
			if (!i.value()->chat && !i.value()->asUser()->username.compare(username.trimmed(), Qt::CaseInsensitive)) {
				return i.value()->asUser();
			}
		}
		return 0;
	}

	ChatData *chat(const PeerId &peer) {
		PeerData *d = App::peer(peer);
		return d->asChat();
	}

	ChatData *chat(int32 chat) {
		return App::peer(App::peerFromChat(chat))->asChat();
	}

	PhotoData *photo(const PhotoId &photo, PhotoData *convert, const uint64 &access, int32 user, int32 date, const ImagePtr &thumb, const ImagePtr &medium, const ImagePtr &full) {
		if (convert) {
			if (convert->id != photo) {
				PhotosData::iterator i = photosData.find(convert->id);
				if (i != photosData.cend() && i.value() == convert) {
					photosData.erase(i);
				}
				convert->id = photo;
			}
			convert->access = access;
			if (!convert->user && !convert->date && (user || date)) {
				convert->user = user;
				convert->date = date;
				convert->thumb = thumb;
				convert->medium = medium;
				convert->full = full;
			}
		}
		PhotosData::const_iterator i = photosData.constFind(photo);
		PhotoData *result;
		LastPhotosMap::iterator inLastIter = lastPhotosMap.end();
		if (i == photosData.cend()) {
			if (convert) {
				result = convert;
			} else {
				result = new PhotoData(photo, access, user, date, thumb, medium, full);
			}
			photosData.insert(photo, result);
		} else {
			result = i.value();
			if (result != convert && !result->user && !result->date && (user || date)) {
				result->access = access;
				result->user = user;
				result->date = date;
				result->thumb = thumb;
				result->medium = medium;
				result->full = full;
			}
			inLastIter = lastPhotosMap.find(result);
		}
		if (inLastIter == lastPhotosMap.end()) { // insert new one
			if (lastPhotos.size() == MaxPhotosInMemory) {
				lastPhotos.front()->forget();
				lastPhotosMap.remove(lastPhotos.front());
				lastPhotos.pop_front();
			}
			lastPhotosMap.insert(result, lastPhotos.insert(lastPhotos.end(), result));
		} else {
			lastPhotos.erase(inLastIter.value()); // move to back
			(*inLastIter) = lastPhotos.insert(lastPhotos.end(), result);
		}
		return result;
	}

	VideoData *video(const VideoId &video, VideoData *convert, const uint64 &access, int32 user, int32 date, int32 duration, int32 w, int32 h, const ImagePtr &thumb, int32 dc, int32 size) {
		if (convert) {
			if (convert->id != video) {
				VideosData::iterator i = videosData.find(convert->id);
				if (i != videosData.cend() && i.value() == convert) {
					videosData.erase(i);
				}
				convert->id = video;
				convert->status = FileReady;
			}
			convert->access = access;
			if (!convert->user && !convert->date && (user || date)) {
				convert->user = user;
				convert->date = date;
				convert->duration = duration;
				convert->w = w;
				convert->h = h;
				convert->thumb = thumb;
				convert->dc = dc;
				convert->size = size;
			}
		}
		VideosData::const_iterator i = videosData.constFind(video);
		VideoData *result;
		if (i == videosData.cend()) {
			if (convert) {
				result = convert;
			} else {
				result = new VideoData(video, access, user, date, duration, w, h, thumb, dc, size);
			}
			videosData.insert(video, result);
		} else {
			result = i.value();
			if (result != convert && !result->user && !result->date && (user || date)) {
				result->access = access;
				result->user = user;
				result->date = date;
				result->duration = duration;
				result->w = w;
				result->h = h;
				result->thumb = thumb;
				result->dc = dc;
				result->size = size;
			}
		}
		return result;
	}

	AudioData *audio(const AudioId &audio, AudioData *convert, const uint64 &access, int32 user, int32 date, const QString &mime, int32 duration, int32 dc, int32 size) {
		if (convert) {
			if (convert->id != audio) {
				AudiosData::iterator i = audiosData.find(convert->id);
				if (i != audiosData.cend() && i.value() == convert) {
					audiosData.erase(i);
				}
				convert->id = audio;
				convert->status = FileReady;
			}
			convert->access = access;
			if (!convert->user && !convert->date && (user || date)) {
				convert->user = user;
				convert->date = date;
				convert->mime = mime;
				convert->duration = duration;
				convert->dc = dc;
				convert->size = size;
			}
		}
		AudiosData::const_iterator i = audiosData.constFind(audio);
		AudioData *result;
		if (i == audiosData.cend()) {
			if (convert) {
				result = convert;
			} else {
				result = new AudioData(audio, access, user, date, mime, duration, dc, size);
			}
			audiosData.insert(audio, result);
		} else {
			result = i.value();
			if (result != convert && !result->user && !result->date && (user || date)) {
				result->access = access;
				result->user = user;
				result->date = date;
				result->mime = mime;
				result->duration = duration;
				result->dc = dc;
				result->size = size;
			}
		}
		return result;
	}

	DocumentData *document(const DocumentId &document, DocumentData *convert, const uint64 &access, int32 date, const QVector<MTPDocumentAttribute> &attributes, const QString &mime, const ImagePtr &thumb, int32 dc, int32 size) {
		if (convert) {
			if (convert->id != document) {
				DocumentsData::iterator i = documentsData.find(convert->id);
				if (i != documentsData.cend() && i.value() == convert) {
					documentsData.erase(i);
				}
				convert->id = document;
				convert->status = FileReady;
			}
			convert->access = access;
			if (!convert->date && date) {
				convert->date = date;
				convert->setattributes(attributes);
				convert->mime = mime;
				convert->thumb = thumb;
				convert->dc = dc;
				convert->size = size;
			} else if (convert->thumb->isNull() && !thumb->isNull()) {
				convert->thumb = thumb;
			}

			if (convert->location.check()) {
				Local::writeFileLocation(mediaKey(mtpc_inputDocumentFileLocation, convert->dc, convert->id), convert->location);
			}
		}
		DocumentsData::const_iterator i = documentsData.constFind(document);
		DocumentData *result;
		if (i == documentsData.cend()) {
			if (convert) {
				result = convert;
			} else {
				result = new DocumentData(document, access, date, attributes, mime, thumb, dc, size);
			}
			documentsData.insert(document, result);
		} else {
			result = i.value();
			if (result != convert) {
				if (!result->date && date) {
					result->access = access;
					result->date = date;
					result->setattributes(attributes);
					result->mime = mime;
					result->thumb = thumb;
					result->dc = dc;
					result->size = size;
				} else {
					if (result->thumb->isNull() && !thumb->isNull()) {
						result->thumb = thumb;
					}
					if (result->alt.isEmpty()) {
						for (QVector<MTPDocumentAttribute>::const_iterator i = attributes.cbegin(), e = attributes.cend(); i != e; ++i) {
							if (i->type() == mtpc_documentAttributeSticker) {
								const MTPDdocumentAttributeSticker &d(i->c_documentAttributeSticker());
								if (d.valt.c_string().v.length() > 0) {
									result->alt = qs(d.valt);
								}
							}
						}
					}
				}
			}
		}
		return result;
	}

	WebPageData *webPage(const WebPageId &webPage, WebPageData *convert, const QString &type, const QString &url, const QString &displayUrl, const QString &siteName, const QString &title, const QString &description, PhotoData *photo, int32 duration, const QString &author, int32 pendingTill) {
		if (convert) {
			if (convert->id != webPage) {
				WebPagesData::iterator i = webPagesData.find(convert->id);
				if (i != webPagesData.cend() && i.value() == convert) {
					webPagesData.erase(i);
				}
				convert->id = webPage;
			}
			if ((convert->url.isEmpty() && !url.isEmpty()) || (convert->pendingTill && convert->pendingTill != pendingTill && pendingTill >= -1)) {
				convert->type = toWebPageType(type);
				convert->url = url;
				convert->displayUrl = displayUrl;
				convert->siteName = siteName;
				convert->title = title;
				convert->description = description;
				convert->photo = photo;
				convert->duration = duration;
				convert->author = author;
				if (convert->pendingTill > 0 && pendingTill <= 0 && api()) api()->clearWebPageRequest(convert);
				convert->pendingTill = pendingTill;
				if (App::main()) App::main()->webPageUpdated(convert);
			}
		}
		WebPagesData::const_iterator i = webPagesData.constFind(webPage);
		WebPageData *result;
		if (i == webPagesData.cend()) {
			if (convert) {
				result = convert;
			} else {
				result = new WebPageData(webPage, toWebPageType(type), url, displayUrl, siteName, title, description, photo, duration, author, (pendingTill >= -1) ? pendingTill : -1);
				if (pendingTill > 0 && api()) {
					api()->requestWebPageDelayed(result);
				}
			}
			webPagesData.insert(webPage, result);
		} else {
			result = i.value();
			if (result != convert) {
				if ((result->url.isEmpty() && !url.isEmpty()) || (result->pendingTill && result->pendingTill != pendingTill && pendingTill >= -1)) {
					result->type = toWebPageType(type);
					result->url = url;
					result->displayUrl = displayUrl;
					result->siteName = siteName;
					result->title = title;
					result->description = description;
					result->photo = photo;
					result->duration = duration;
					result->author = author;
					if (result->pendingTill > 0 && pendingTill <= 0 && api()) api()->clearWebPageRequest(result);
					result->pendingTill = pendingTill;
					if (App::main()) App::main()->webPageUpdated(result);
				}
			}
		}
		return result;
	}
	
	ImageLinkData *imageLink(const QString &imageLink, ImageLinkType type, const QString &url) {
		ImageLinksData::const_iterator i = imageLinksData.constFind(imageLink);
		ImageLinkData *result;
		if (i == imageLinksData.cend()) {
			result = new ImageLinkData(imageLink);
			imageLinksData.insert(imageLink, result);
			result->type = type;
		} else {
			result = i.value();
		}
		return result;
	}

	void forgetMedia() {
		lastPhotos.clear();
		lastPhotosMap.clear();
		for (PhotosData::const_iterator i = photosData.cbegin(), e = photosData.cend(); i != e; ++i) {
			i.value()->forget();
		}
		for (VideosData::const_iterator i = videosData.cbegin(), e = videosData.cend(); i != e; ++i) {
			i.value()->forget();
		}
		for (AudiosData::const_iterator i = audiosData.cbegin(), e = audiosData.cend(); i != e; ++i) {
			i.value()->forget();
		}
		for (DocumentsData::const_iterator i = documentsData.cbegin(), e = documentsData.cend(); i != e; ++i) {
			i.value()->forget();
		}
		for (ImageLinksData::const_iterator i = imageLinksData.cbegin(), e = imageLinksData.cend(); i != e; ++i) {
			i.value()->thumb->forget();
		}
	}

	MTPPhoto photoFromUserPhoto(MTPint userId, MTPint date, const MTPUserProfilePhoto &photo) {
		if (photo.type() == mtpc_userProfilePhoto) {
			const MTPDuserProfilePhoto &uphoto(photo.c_userProfilePhoto());
				
			QVector<MTPPhotoSize> photoSizes;
			photoSizes.push_back(MTP_photoSize(MTP_string("a"), uphoto.vphoto_small, MTP_int(160), MTP_int(160), MTP_int(0)));
			photoSizes.push_back(MTP_photoSize(MTP_string("c"), uphoto.vphoto_big, MTP_int(640), MTP_int(640), MTP_int(0)));

			return MTP_photo(uphoto.vphoto_id, MTP_long(0), userId, date, MTP_geoPointEmpty(), MTP_vector<MTPPhotoSize>(photoSizes));
		}
		return MTP_photoEmpty(MTP_long(0));
	}

	QString peerName(const PeerData *peer, bool forDialogs) {
		return peer ? (forDialogs ? peer->nameOrPhone : peer->name) : lang(lng_deleted);
	}

	Histories &histories() {
		return ::histories;
	}

	History *history(const PeerId &peer, int32 unreadCnt, int32 maxInboxRead) {
		Histories::const_iterator i = ::histories.constFind(peer);
		if (i == ::histories.cend()) {
			i = App::histories().insert(peer, new History(peer));
			i.value()->setUnreadCount(unreadCnt, false);
			if (maxInboxRead) {
				i.value()->inboxReadTill = maxInboxRead;
			}
		}
		return i.value();
	}
	
	History *historyLoaded(const PeerId &peer) {
		Histories::const_iterator i = ::histories.constFind(peer);
		return (i == ::histories.cend()) ? 0 : i.value();
	}

	HistoryItem *histItemById(MsgId itemId) {
		MsgsData::const_iterator i = msgsData.constFind(itemId);
		if (i != msgsData.cend()) {
			return i.value();
		}
		return 0;
	}

	void itemReplaced(HistoryItem *oldItem, HistoryItem *newItem) {
		if (HistoryReply *r = oldItem->toHistoryReply()) {
			QMap<HistoryReply*, bool> &replies(::repliesTo[r->replyToMessage()]);
			replies.remove(r);
			if (HistoryReply *n = newItem->toHistoryReply()) {
				replies.insert(n, true);
			}
		}
		RepliesTo::iterator i = ::repliesTo.find(oldItem);
		if (i != ::repliesTo.cend() && oldItem != newItem) {
			QMap<HistoryReply*, bool> replies = i.value();
			::repliesTo.erase(i);
			::repliesTo[newItem] = replies;
			for (QMap<HistoryReply*, bool>::iterator i = replies.begin(), e = replies.end(); i != e; ++i) {
				i.key()->replyToReplaced(oldItem, newItem);
			}
		}
		newItem->history()->itemReplaced(oldItem, newItem);
		if (App::main()) App::main()->itemReplaced(oldItem, newItem);
		if (App::hoveredItem() == oldItem) App::hoveredItem(newItem);
		if (App::pressedItem() == oldItem) App::pressedItem(newItem);
		if (App::hoveredLinkItem() == oldItem) App::hoveredLinkItem(newItem);
		if (App::pressedLinkItem() == oldItem) App::pressedLinkItem(newItem);
		if (App::contextItem() == oldItem) App::contextItem(newItem);
		if (App::mousedItem() == oldItem) App::mousedItem(newItem);
	}

	HistoryItem *historyRegItem(HistoryItem *item) {
		MsgsData::const_iterator i = msgsData.constFind(item->id);
		if (i == msgsData.cend()) {
			msgsData.insert(item->id, item);
			if (item->id > ::maxMsgId) ::maxMsgId = item->id;
			return 0;
		}
		if (i.value() != item && !i.value()->block() && item->block()) { // replace search item
			itemReplaced(i.value(), item);
			delete i.value();
			msgsData.insert(item->id, item);
			return 0;
		}
		return (i.value() == item) ? 0 : i.value();
	}

	void historyItemDetached(HistoryItem *item) {
		if (::hoveredItem == item) {
			hoveredItem(0);
		}
		if (::pressedItem == item) {
			pressedItem(0);
		}
		if (::hoveredLinkItem == item) {
			hoveredLinkItem(0);
		}
		if (::pressedLinkItem == item) {
			pressedLinkItem(0);
		}
		if (::contextItem == item) {
			contextItem(0);
		}
		if (::mousedItem == item) {
			mousedItem(0);
		}
		if (App::wnd()) {
			App::wnd()->notifyItemRemoved(item);
		}
	}

	void historyUnregItem(HistoryItem *item) {
		MsgsData::iterator i = msgsData.find(item->id);
		if (i != msgsData.cend()) {
			if (i.value() == item) {
				msgsData.erase(i);
			}
		}
		historyItemDetached(item);
		RepliesTo::iterator j = ::repliesTo.find(item);
		if (j != ::repliesTo.cend()) {
			for (QMap<HistoryReply*, bool>::const_iterator k = j.value().cbegin(), e = j.value().cend(); k != e; ++k) {
				k.key()->replyToReplaced(item, 0);
			}
			::repliesTo.erase(j);
		}
		if (App::main() && !App::quiting()) {
			App::main()->itemRemoved(item);
		}
	}

	void historyClearMsgs() {
		QVector<HistoryItem*> toDelete;
		for (MsgsData::const_iterator i = msgsData.cbegin(), e = msgsData.cend(); i != e; ++i) {
			if ((*i)->detached()) {
				toDelete.push_back(*i);
			}
		}
		msgsData.clear();
		for (int i = 0, l = toDelete.size(); i < l; ++i) {
			delete toDelete[i];
		}
		::maxMsgId = 0;
		::hoveredItem = ::pressedItem = ::hoveredLinkItem = ::pressedLinkItem = ::contextItem = 0;
	}

	void historyClearItems() {
		historyClearMsgs();
		randomData.clear();
		mutedPeers.clear();
		for (PeersData::const_iterator i = peersData.cbegin(), e = peersData.cend(); i != e; ++i) {
			delete *i;
		}
		peersData.clear();
		for (PhotosData::const_iterator i = photosData.cbegin(), e = photosData.cend(); i != e; ++i) {
			delete *i;
		}
		photosData.clear();
		for (VideosData::const_iterator i = videosData.cbegin(), e = videosData.cend(); i != e; ++i) {
			delete *i;
		}
		videosData.clear();
		for (AudiosData::const_iterator i = audiosData.cbegin(), e = audiosData.cend(); i != e; ++i) {
			delete *i;
		}
		audiosData.clear();
		for (DocumentsData::const_iterator i = documentsData.cbegin(), e = documentsData.cend(); i != e; ++i) {
			delete *i;
		}
		documentsData.clear();
		for (WebPagesData::const_iterator i = webPagesData.cbegin(), e = webPagesData.cend(); i != e; ++i) {
			delete *i;
		}
		webPagesData.clear();
		if (api()) api()->clearWebPageRequests();
		cSetRecentStickers(RecentStickerPack());
		cSetStickersHash(QByteArray());
		cSetStickers(AllStickers());
		cSetEmojiStickers(EmojiStickersMap());
		::videoItems.clear();
		::audioItems.clear();
		::documentItems.clear();
		::webPageItems.clear();
		::repliesTo.clear();
		lastPhotos.clear();
		lastPhotosMap.clear();
		::self = 0;
		if (App::wnd()) App::wnd()->updateGlobalMenu();
	}

	void historyRegReply(HistoryReply *reply, HistoryItem *to) {
		::repliesTo[to].insert(reply, true);
	}

	void historyUnregReply(HistoryReply *reply, HistoryItem *to) {
		RepliesTo::iterator i = ::repliesTo.find(to);
		if (i != ::repliesTo.cend()) {
			i.value().remove(reply);
			if (i.value().isEmpty()) {
				::repliesTo.erase(i);
			}
		}
	}

	/* // don't delete history without deleting its' peerdata
	void deleteHistory(const PeerId &peer) {
		Histories::iterator i = ::histories.find(peer);
		if (i != ::histories.end()) {
			::histories.typing.remove(i.value());
			::histories.erase(i);
		}
	}
/**/
	void historyRegRandom(uint64 randomId, MsgId itemId) {
		randomData.insert(randomId, itemId);
	}

	void historyUnregRandom(uint64 randomId) {
		randomData.remove(randomId);
	}

	MsgId histItemByRandom(uint64 randomId) {
		RandomData::const_iterator i = randomData.constFind(randomId);
		if (i != randomData.cend()) {
			return i.value();
		}
		return 0;
	}

	void initMedia() {
		deinitMedia(false);
		audioInit();

		if (!::sprite) {
			if (rtl()) {
				::sprite = new QPixmap(QPixmap::fromImage(QImage(st::spriteFile).mirrored(true, false)));
			} else {
				::sprite = new QPixmap(st::spriteFile);
			}
            if (cRetina()) ::sprite->setDevicePixelRatio(cRetinaFactor());
		}
		if (!::emojis) {
			::emojis = new QPixmap(st::emojisFile);
            if (cRetina()) ::emojis->setDevicePixelRatio(cRetinaFactor());
		}
		initEmoji();
	}
	
	void deinitMedia(bool completely) {
		textlnkOver(TextLinkPtr());
		textlnkDown(TextLinkPtr());

		histories().clear();

		if (completely) {
			audioFinish();

			delete ::sprite;
			::sprite = 0;
			delete ::emojis;
			::emojis = 0;
			mainEmojisMap.clear();
			otherEmojisMap.clear();

			clearAllImages();
		} else {
			clearStorageImages();
			cSetServerBackgrounds(WallPapers());
		}

		serviceImageCacheSize = imageCacheSize();
	}

	void hoveredItem(HistoryItem *item) {
		::hoveredItem = item;
	}

	HistoryItem *hoveredItem() {
		return ::hoveredItem;
	}

	void pressedItem(HistoryItem *item) {
		::pressedItem = item;
	}

	HistoryItem *pressedItem() {
		return ::pressedItem;
	}
	
	void hoveredLinkItem(HistoryItem *item) {
		::hoveredLinkItem = item;
	}

	HistoryItem *hoveredLinkItem() {
		return ::hoveredLinkItem;
	}

	void pressedLinkItem(HistoryItem *item) {
		::pressedLinkItem = item;
	}

	HistoryItem *pressedLinkItem() {
		return ::pressedLinkItem;
	}
	
	void contextItem(HistoryItem *item) {
		::contextItem = item;
	}

	HistoryItem *contextItem() {
		return ::contextItem;
	}

	void mousedItem(HistoryItem *item) {
		::mousedItem = item;
	}

	HistoryItem *mousedItem() {
		return ::mousedItem;
	}

	const QPixmap &sprite() {
		return *::sprite;
	}

	const QPixmap &emojis() {
		return *::emojis;
	}

	const QPixmap &emojiSingle(const EmojiData *emoji, int32 fontHeight) {
		EmojisMap *map = &(fontHeight == st::taDefFlat.font->height ? mainEmojisMap : otherEmojisMap[fontHeight]);
		EmojisMap::const_iterator i = map->constFind(emoji->code);
		if (i == map->cend()) {
			QImage img(st::emojiImgSize + st::emojiPadding * cIntRetinaFactor() * 2, fontHeight * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
            if (cRetina()) img.setDevicePixelRatio(cRetinaFactor());
			{
				QPainter p(&img);
				p.setCompositionMode(QPainter::CompositionMode_Source);
				p.fillRect(0, 0, img.width(), img.height(), Qt::transparent);
				p.drawPixmap(QPoint(st::emojiPadding * cIntRetinaFactor(), (fontHeight * cIntRetinaFactor() - st::emojiImgSize) / 2), App::emojis(), QRect(emoji->x, emoji->y, st::emojiImgSize, st::emojiImgSize));
			}
			i = map->insert(emoji->code, QPixmap::fromImage(img, Qt::ColorOnly));
		}
		return i.value();
	}

	void playSound() {
		if (cSoundNotify() && !psSkipAudioNotify()) audioPlayNotify();
	}

	void checkImageCacheSize() {
		int64 nowImageCacheSize = imageCacheSize();
		if (nowImageCacheSize > serviceImageCacheSize + MemoryForImageCache) {
			App::forgetMedia();
			serviceImageCacheSize = imageCacheSize();
		}
	}

	bool isValidPhone(QString phone) {
		phone = phone.replace(QRegularExpression(qsl("[^\\d]")), QString());
		return phone.length() >= 8 || phone == qsl("777") || phone == qsl("333") || phone == qsl("111") || (phone.startsWith(qsl("42")) && (phone.length() == 2 || phone.length() == 5 || phone == qsl("4242")));
	}

	void quit() {
		if (quiting()) return;

		setQuiting();
		if (wnd()) {
			wnd()->quit();
		}
		if (app()) {
			app()->quit();
		}
	}

	bool quiting() {
		return ::quiting;
	}

	void setQuiting() {
		::quiting = true;
	}

	QImage readImage(QByteArray data, QByteArray *format, bool opaque, bool *animated) {
        QByteArray tmpFormat;
		QImage result;
		QBuffer buffer(&data);
        if (!format) {
            format = &tmpFormat;
        }
        QImageReader reader(&buffer, *format);
		if (animated) *animated = reader.supportsAnimation() && reader.imageCount() > 1;
		if (!reader.read(&result)) {
			return QImage();
		}

		buffer.seek(0);
        *format = reader.format();
        QString fmt = QString::fromUtf8(*format).toLower() ;
		if (fmt == "jpg" || fmt == "jpeg") {
			ExifData *exifData = exif_data_new_from_data((const uchar*)(data.constData()), data.size());
			if (exifData) {
				ExifByteOrder byteOrder = exif_data_get_byte_order(exifData);
				ExifEntry *exifEntry = exif_data_get_entry(exifData, EXIF_TAG_ORIENTATION);
				if (exifEntry) {
					QTransform orientationFix;
					int orientation = exif_get_short(exifEntry->data, byteOrder);
					switch (orientation) {
					case 2: orientationFix = QTransform(-1, 0, 0, 1, 0, 0); break;
					case 3: orientationFix = QTransform(-1, 0, 0, -1, 0, 0); break;
					case 4: orientationFix = QTransform(1, 0, 0, -1, 0, 0); break;
					case 5: orientationFix = QTransform(0, -1, -1, 0, 0, 0); break;
					case 6: orientationFix = QTransform(0, 1, -1, 0, 0, 0); break;
					case 7: orientationFix = QTransform(0, 1, 1, 0, 0, 0); break;
					case 8: orientationFix = QTransform(0, -1, 1, 0, 0, 0); break;
					}
					result = result.transformed(orientationFix);
				}
				exif_data_free(exifData);
			}
		} else if (opaque && result.hasAlphaChannel()) {
			QImage solid(result.width(), result.height(), QImage::Format_ARGB32_Premultiplied);
			solid.fill(st::white->c);
			{
				QPainter(&solid).drawImage(0, 0, result);
			}
			result = solid;
		}
		return result;
	}

	QImage readImage(const QString &file, QByteArray *format, bool opaque, bool *animated, QByteArray *content) {
		QFile f(file);
		if (!f.open(QIODevice::ReadOnly)) {
			if (animated) *animated = false;
			return QImage();
		}
		QByteArray img = f.readAll();
		QImage result = readImage(img, format, opaque, animated);
		if (content && !result.isNull()) *content = img;
		return result;
	}

	void regVideoItem(VideoData *data, HistoryItem *item) {
		::videoItems[data][item] = true;
	}

	void unregVideoItem(VideoData *data, HistoryItem *item) {
		::videoItems[data].remove(item);
	}

	const VideoItems &videoItems() {
		return ::videoItems;
	}

	void regAudioItem(AudioData *data, HistoryItem *item) {
		::audioItems[data][item] = true;
	}

	void unregAudioItem(AudioData*data, HistoryItem *item) {
		::audioItems[data].remove(item);
	}

	const AudioItems &audioItems() {
		return ::audioItems;
	}

	void regDocumentItem(DocumentData *data, HistoryItem *item) {
		::documentItems[data][item] = true;
	}

	void unregDocumentItem(DocumentData *data, HistoryItem *item) {
		::documentItems[data].remove(item);
	}

	const DocumentItems &documentItems() {
		return ::documentItems;
	}

	void regWebPageItem(WebPageData *data, HistoryItem *item) {
		::webPageItems[data][item] = true;
	}

	void unregWebPageItem(WebPageData *data, HistoryItem *item) {
		::webPageItems[data].remove(item);
	}

	const WebPageItems &webPageItems() {
		return ::webPageItems;
	}

	void regMuted(PeerData *peer, int32 changeIn) {
		::mutedPeers.insert(peer, true);
		if (App::main()) App::main()->updateMutedIn(changeIn);
	}

	void unregMuted(PeerData *peer) {
		::mutedPeers.remove(peer);
	}

	void updateMuted() {
		int32 changeInMin = 0;
		for (MutedPeers::iterator i = ::mutedPeers.begin(); i != ::mutedPeers.end();) {
			int32 changeIn = 0;
			History *h = App::history(i.key()->id);
			if (isNotifyMuted(i.key()->notify, &changeIn)) {
				h->setMute(true);
				if (changeIn && (!changeInMin || changeIn < changeInMin)) {
					changeInMin = changeIn;
				}
				++i;
			} else {
				h->setMute(false);
				i = ::mutedPeers.erase(i);
			}
		}
		if (changeInMin) App::main()->updateMutedIn(changeInMin);
	}

	void setProxySettings(QNetworkAccessManager &manager) {
		if (cConnectionType() == dbictHttpProxy) {
			const ConnectionProxy &p(cConnectionProxy());
			manager.setProxy(QNetworkProxy(QNetworkProxy::HttpProxy, p.host, p.port, p.user, p.password));
		} else {
			manager.setProxy(QNetworkProxy(QNetworkProxy::DefaultProxy));
		}
	}

	void setProxySettings(QTcpSocket &socket) {
		if (cConnectionType() == dbictTcpProxy) {
			const ConnectionProxy &p(cConnectionProxy());
			socket.setProxy(QNetworkProxy(QNetworkProxy::Socks5Proxy, p.host, p.port, p.user, p.password));
		} else {
			socket.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
		}
	}	

	void searchByHashtag(const QString &tag) {
		if (App::main()) {
			App::main()->searchMessages(tag + ' ');
		}
	}

	void openUserByName(const QString &username, bool toProfile) {
		if (App::main()) {
			App::main()->openUserByName(username, toProfile);
		}
	}

	void joinGroupByHash(const QString &hash) {
		if (App::main()) {
			App::main()->joinGroupByHash(hash);
		}
	}

	void openLocalUrl(const QString &url) {
		if (App::main()) {
			App::main()->openLocalUrl(url);
		}
	}

	void initBackground(int32 id, const QImage &p, bool nowrite) {
		if (Local::readBackground()) return;

		QImage img(p);
		bool remove = false;
		if (p.isNull()) {
			if (id == DefaultChatBackground) {
				img.load(st::msgBG);
			} else {
				img.load(st::msgBG0);
				if (cRetina()) {
					img = img.scaledToWidth(img.width() * 2, Qt::SmoothTransformation);
				} else if (cScale() != dbisOne) {
					img = img.scaledToWidth(convertScale(img.width()), Qt::SmoothTransformation);
				}
				id = 0;
			}
			remove = true;
		}
		if (img.format() != QImage::Format_ARGB32 && img.format() != QImage::Format_ARGB32_Premultiplied && img.format() != QImage::Format_RGB32) {
			img = img.convertToFormat(QImage::Format_RGB32);
		}
		img.setDevicePixelRatio(cRetinaFactor());

		if (!nowrite) {
			Local::writeBackground(id, remove ? QImage() : img);
		}

		delete cChatBackground();
		cSetChatBackground(new QPixmap(QPixmap::fromImage(img, Qt::ColorOnly)));
		cSetChatBackgroundId(id);

		if (App::main()) App::main()->clearCachedBackground();

		uint64 components[3] = { 0 }, componentsScroll[3] = { 0 }, componentsPoint[3] = { 0 };
		int w = img.width(), h = img.height(), size = w * h;
		const uchar *pix = img.constBits();
		if (pix) {
			for (int32 i = 0, l = size * 4; i < l; i += 4) {
				components[2] += pix[i + 0];
				components[1] += pix[i + 1];
				components[0] += pix[i + 2];
			}
		}
		if (size) {
			for (int32 i = 0; i < 3; ++i) components[i] /= size;
		}
		int maxtomin[3] = { 0, 1, 2 };
		if (components[maxtomin[0]] < components[maxtomin[1]]) {
			qSwap(maxtomin[0], maxtomin[1]);
		}
		if (components[maxtomin[1]] < components[maxtomin[2]]) {
			qSwap(maxtomin[1], maxtomin[2]);
			if (components[maxtomin[0]] < components[maxtomin[1]]) {
				qSwap(maxtomin[0], maxtomin[1]);
			}
		}

		uint64 max = qMax(1ULL, components[maxtomin[0]]), mid = qMax(1ULL, components[maxtomin[1]]), min = qMax(1ULL, components[maxtomin[2]]);

		QImage dog = App::sprite().toImage().copy(st::msgDogImg);
		QImage::Format f = dog.format();
		if (f != QImage::Format_ARGB32 && f != QImage::Format_ARGB32_Premultiplied) {
			dog = dog.convertToFormat(QImage::Format_ARGB32_Premultiplied);
		}
		uchar *dogBits = dog.bits();
		if (max != min) {
			float64 coef = float64(mid - min) / float64(max - min);
			for (int i = 0, s = dog.width() * dog.height() * 4; i < s; i += 4) {
				int dogmaxtomin[3] = { i, i + 1, i + 2 };
				if (dogBits[dogmaxtomin[0]] < dogBits[dogmaxtomin[1]]) {
					qSwap(dogmaxtomin[0], dogmaxtomin[1]);
				}
				if (dogBits[dogmaxtomin[1]] < dogBits[dogmaxtomin[2]]) {
					qSwap(dogmaxtomin[1], dogmaxtomin[2]);
					if (dogBits[dogmaxtomin[0]] < dogBits[dogmaxtomin[1]]) {
						qSwap(dogmaxtomin[0], dogmaxtomin[1]);
					}
				}
				uchar result[3];
				result[maxtomin[0]] = dogBits[dogmaxtomin[0]];
				result[maxtomin[2]] = dogBits[dogmaxtomin[2]];
				result[maxtomin[1]] = uchar(qRound(result[maxtomin[2]] + (result[maxtomin[0]] - result[maxtomin[2]]) * coef));
				dogBits[i] = result[2];
				dogBits[i + 1] = result[1];
				dogBits[i + 2] = result[0];
			}
		} else {
			for (int i = 0, s = dog.width() * dog.height() * 4; i < s; i += 4) {
				uchar b = dogBits[i], g = dogBits[i + 1], r = dogBits[i + 2];
				dogBits[i] = dogBits[i + 1] = dogBits[i + 2] = (r + r + b + g + g + g) / 6;
			}
		}
		delete cChatDogImage();
		cSetChatDogImage(new QPixmap(QPixmap::fromImage(dog)));

		memcpy(componentsScroll, components, sizeof(components));
		memcpy(componentsPoint, components, sizeof(components));

		if (max != min) {
			if (min > uint64(qRound(0.77 * max))) {
				uint64 newmin = qRound(0.77 * max); // min saturation 23%
				uint64 newmid = max - ((max - mid) * (max - newmin)) / (max - min);
				components[maxtomin[1]] = newmid;
				components[maxtomin[2]] = newmin;
			}
			uint64 newmin = qRound(0.77 * max); // saturation 23% for scroll
			uint64 newmid = max - ((max - mid) * (max - newmin)) / (max - min);
			componentsScroll[maxtomin[1]] = newmid;
			componentsScroll[maxtomin[2]] = newmin;

			uint64 pmax = 227; // 89% brightness
			uint64 pmin = qRound(0.75 * pmax); // 41% saturation
			uint64 pmid = pmax - ((max - mid) * (pmax - pmin)) / (max - min);
			componentsPoint[maxtomin[0]] = pmax;
			componentsPoint[maxtomin[1]] = pmid;
			componentsPoint[maxtomin[2]] = pmin;
		} else {
			componentsPoint[0] = componentsPoint[1] = componentsPoint[2] = 227; // 89% brightness
		}

		float64 luminance = 0.299 * componentsScroll[0] + 0.587 * componentsScroll[1] + 0.114 * componentsScroll[2];
		uint64 maxScroll = max;
		if (luminance < 0.5 * 0xFF) {
			maxScroll += qRound(0.2 * 0xFF);
		} else {
			maxScroll -= qRound(0.2 * 0xFF);
		}
		componentsScroll[maxtomin[2]] = qMin(uint64(float64(componentsScroll[maxtomin[2]]) * maxScroll / float64(componentsScroll[maxtomin[0]])), 0xFFULL);
		componentsScroll[maxtomin[1]] = qMin(uint64(float64(componentsScroll[maxtomin[1]]) * maxScroll / float64(componentsScroll[maxtomin[0]])), 0xFFULL);
		componentsScroll[maxtomin[0]] = qMin(maxScroll, 0xFFULL);

        if (max > uint64(qRound(0.2 * 0xFF))) { // brightness greater than 20%
			max -= qRound(0.2 * 0xFF);
		} else {
			max = 0;
		}
		components[maxtomin[2]] = uint64(float64(components[maxtomin[2]]) * max / float64(components[maxtomin[0]]));
		components[maxtomin[1]] = uint64(float64(components[maxtomin[1]]) * max / float64(components[maxtomin[0]]));
		components[maxtomin[0]] = max;

		uchar r = uchar(components[0]), g = uchar(components[1]), b = uchar(components[2]);
		_msgServiceBG = style::color(r, g, b, qRound(st::msgServiceBG->c.alphaF() * 0xFF));

		uchar rScroll = uchar(componentsScroll[0]), gScroll = uchar(componentsScroll[1]), bScroll = uchar(componentsScroll[2]);
		_historyScrollBarColor = style::color(rScroll, gScroll, bScroll, qRound(st::historyScroll.barColor->c.alphaF() * 0xFF));
		_historyScrollBgColor = style::color(rScroll, gScroll, bScroll, qRound(st::historyScroll.bgColor->c.alphaF() * 0xFF));
		_historyScrollBarOverColor = style::color(rScroll, gScroll, bScroll, qRound(st::historyScroll.barOverColor->c.alphaF() * 0xFF));
		_historyScrollBgOverColor = style::color(rScroll, gScroll, bScroll, qRound(st::historyScroll.bgOverColor->c.alphaF() * 0xFF));

		uchar rPoint = uchar(componentsPoint[0]), gPoint = uchar(componentsPoint[1]), bPoint = uchar(componentsPoint[2]);
		_introPointHoverColor = style::color(rPoint, gPoint, bPoint);
		if (App::main()) App::main()->updateScrollColors();
	}

	style::color msgServiceBG() {
		return _msgServiceBG;
	}

	style::color historyScrollBarColor() {
		return _historyScrollBarColor;
	}

	style::color historyScrollBgColor() {
		return _historyScrollBgColor;
	}

	style::color historyScrollBarOverColor() {
		return _historyScrollBarOverColor;
	}

	style::color historyScrollBgOverColor() {
		return _historyScrollBgOverColor;
	}

	style::color introPointHoverColor() {
		return _introPointHoverColor;
	}

	WallPapers gServerBackgrounds;

}
