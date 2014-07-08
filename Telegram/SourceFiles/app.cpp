/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#include "stdafx.h"
#include "lang.h"

#include "app.h"

#include "application.h"
#include "fileuploader.h"
#include "mainwidget.h"
#include <QtMultimedia/QSoundEffect>
#include <libexif/exif-data.h>

namespace {
	bool quiting = false;

	UserData *self = 0;

	typedef QHash<PeerId, PeerData*> PeersData;
	PeersData peersData;

	typedef QHash<PhotoId, PhotoData*> PhotosData;
	PhotosData photosData;

	typedef QHash<VideoId, VideoData*> VideosData;
	VideosData videosData;

	typedef QHash<AudioId, AudioData*> AudiosData;
	AudiosData audiosData;

	typedef QHash<DocumentId, DocumentData*> DocumentsData;
	DocumentsData documentsData;

	VideoItems videoItems;
	AudioItems audioItems;
	DocumentItems documentItems;

	Histories histories;

	typedef QHash<MsgId, HistoryItem*> MsgsData;
	MsgsData msgsData;
	int32 maxMsgId = 0;

	typedef QMap<uint64, MsgId> RandomData;
	RandomData randomData;

	HistoryItem *hoveredItem = 0, *pressedItem = 0, *hoveredLinkItem = 0, *pressedLinkItem = 0, *contextItem = 0, *mousedItem = 0;

	QSoundEffect *newMsgSound = 0;
	QPixmap *sprite = 0, *emojis = 0;

	typedef QMap<uint32, QPixmap> EmojisMap;
	EmojisMap mainEmojisMap;
	QMap<int32, EmojisMap> otherEmojisMap;

	int32 serviceImageCacheSize = 0;

	typedef QLinkedList<PhotoData*> LastPhotosList;
	LastPhotosList lastPhotos;
	typedef QHash<PhotoData*, LastPhotosList::iterator> LastPhotosMap;
	LastPhotosMap lastPhotosMap;
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

	Settings *settings() {
		Window *w(wnd());
		return w ? w->settingsWidget() : 0;
	}

	FileUploader *uploader() {
		return app() ? app()->uploader() : 0;
	}

	void showSettings() {
		Window *w(wnd());
		if (w) w->showSettings();
	}

	bool loggedOut() {
		Window *w(wnd());
		if (w) {
			w->tempDirDelete();
			w->notifyClearFast();
			w->setupIntro(true);
		}
		MainWidget *m(main());
		if (m) m->destroyData();
		MTP::authed(0);
		histories().clear();
		globalNotifyAllPtr = UnknownNotifySettings;
		globalNotifyUsersPtr = UnknownNotifySettings;
		globalNotifyChatsPtr = UnknownNotifySettings;
		App::uploader()->clear();
		clearStorageImages();
		if (w) {
			w->updateTitleStatus();
			w->getTitle()->resizeEvent(0);
		}
		return true;
	}

	void logOut() {
		MTP::send(MTPauth_LogOut(), rpcDone(&loggedOut), rpcFail(&loggedOut));
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

	int32 onlineWillChangeIn(int32 online, int32 now) {
		if (online <= 0) return 86400;
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

	QString onlineText(int32 online, int32 now) {
		if (!online) return lang(lng_status_offline);
		if (online < 0) return lang(lng_status_invisible);
		if (online > now) {
			return lang(lng_status_online);
		}
		int32 minutes = (now - online) / 60;
		QString when;
		if (!minutes) {
			when = lang(lng_status_lastseen_now);
		} else if (minutes == 1) {
			when = lang(lng_status_lastseen_minute).arg(minutes);
		} else if (minutes < 60) {
			when = lang(lng_status_lastseen_minutes).arg(minutes);
		} else {
			int32 hours = (now - online) / 3600;
			if (hours == 1) {
				when = lang(lng_status_lastseen_hour).arg(hours);
			} else if (hours < 12) {
				when = lang(lng_status_lastseen_hours).arg(hours);
			} else {
				QDateTime dOnline(date(online)), dNow(date(now));
				if (dOnline.date() == dNow.date()) {
					when = lang(lng_status_lastseen_today).replace(qsl("{time}"), dOnline.time().toString(qsl("hh:mm")));
				} else if (dOnline.date().addDays(1) == dNow.date()) {
					when = lang(lng_status_lastseen_yesterday).replace(qsl("{time}"), dOnline.time().toString(qsl("hh:mm")));
				} else {
					when = lang(lng_status_lastseen_date).replace(qsl("{date}"), dOnline.date().toString(qsl("dd.MM.yy")));
				}
			}
		}
		if (when.isEmpty()) {
			int a = 0;
		}
		return lang(lng_status_lastseen).replace(qsl("{when}"), when);
	}

	void feedUsers(const MTPVector<MTPUser> &users) {
		const QVector<MTPUser> &v(users.c_vector().v);
		for (QVector<MTPUser>::const_iterator i = v.cbegin(), e = v.cend(); i != e; ++i) {
			const MTPuser &user(*i);
			UserData *data;
			bool wasContact = false;
			const MTPUserStatus *status = 0;

			switch (user.type()) {
			case mtpc_userEmpty: {
				const MTPDuserEmpty &d(user.c_userEmpty());

				PeerId peer(peerFromUser(d.vid.v));
				data = App::user(peer);
				data->input = MTP_inputPeerContact(d.vid);
				data->inputUser = MTP_inputUserContact(d.vid);
				data->setName(lang(lng_deleted), QString(), QString());
				data->setPhoto(MTP_userProfilePhotoEmpty());
				data->access = 0;
				wasContact = (data->contact > 0);
				data->contact = -1;
			} break;
			case mtpc_userDeleted: {
				const MTPDuserDeleted &d(user.c_userDeleted());

				PeerId peer(peerFromUser(d.vid.v));
				data = App::user(peer);
				data->input = MTP_inputPeerContact(d.vid);
				data->inputUser = MTP_inputUserContact(d.vid);
				data->setName(textOneLine(qs(d.vfirst_name)), textOneLine(qs(d.vlast_name)), QString());
				data->setPhoto(MTP_userProfilePhotoEmpty());
				data->access = 0;
				wasContact = (data->contact > 0);
				data->contact = -1;
			} break;
			case mtpc_userSelf: {
				const MTPDuserSelf &d(user.c_userSelf());

				PeerId peer(peerFromUser(d.vid.v));
				data = App::user(peer);
				data->input = MTP_inputPeerSelf();
				data->inputUser = MTP_inputUserSelf();
				data->setName(textOneLine(qs(d.vfirst_name)), textOneLine(qs(d.vlast_name)), QString());
				data->setPhoto(d.vphoto);
				data->setPhone(qs(d.vphone));
				data->access = 0;
				wasContact = (data->contact > 0);
				data->contact = -1;
				status = &d.vstatus;

				::self = data;
			} break;
			case mtpc_userContact: {
				const MTPDuserContact &d(user.c_userContact());

				PeerId peer(peerFromUser(d.vid.v));
				data = App::user(peer);
				data->input = MTP_inputPeerContact(d.vid);
				data->inputUser = MTP_inputUserContact(d.vid);
				data->setName(textOneLine(qs(d.vfirst_name)), textOneLine(qs(d.vlast_name)), QString());
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
				data->setName(textOneLine(qs(d.vfirst_name)), textOneLine(qs(d.vlast_name)), (data->id != 333000 && !data->phone.isEmpty()) ? formatPhone(data->phone) : QString());
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
				data->setName(textOneLine(qs(d.vfirst_name)), textOneLine(qs(d.vlast_name)), QString());
				data->setPhoto(d.vphoto);
				data->access = d.vaccess_hash.v;
				wasContact = (data->contact > 0);
				data->contact = -1;
				status = &d.vstatus;
			} break;
			}

			data->loaded = true;
			if (status) switch (status->type()) {
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
	}

	void feedChats(const MTPVector<MTPChat> &chats) {
		const QVector<MTPChat> &v(chats.c_vector().v);
		for (QVector<MTPChat>::const_iterator i = v.cbegin(), e = v.cend(); i != e; ++i) {
			const MTPchat &chat(*i);
			ChatData *data;
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
				data->left = false;
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
			data->updateName(title.trimmed(), QString());

			if (App::main()) App::main()->peerUpdated(data);
		}
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

	void feedMsgs(const MTPVector<MTPMessage> &msgs, bool newMsgs) {
		const QVector<MTPMessage> &v(msgs.c_vector().v);
		QMap<int32, int32> msgsIds;
		for (int32 i = 0, l = v.size(); i < l; ++i) {
			const MTPMessage &msg(v[i]);
			switch (msg.type()) {
			case mtpc_message: msgsIds.insert(msg.c_message().vid.v, i); break;
			case mtpc_messageEmpty: msgsIds.insert(msg.c_messageEmpty().vid.v, i); break;
			case mtpc_messageForwarded: msgsIds.insert(msg.c_messageForwarded().vid.v, i); break;
			case mtpc_messageService: msgsIds.insert(msg.c_messageService().vid.v, i); break;
			}
		}
		for (QMap<int32, int32>::const_iterator i = msgsIds.cbegin(), e = msgsIds.cend(); i != e; ++i) {
			histories().addToBack(v[*i], newMsgs ? 1 : 0);
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
				return ImagePtr(d.vw.v, d.vh.v, l.vdc_id.v, l.vvolume_id.v, l.vlocal_id.v, l.vsecret.v);
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
	
	void feedWereDeleted(const QVector<MTPint> &msgsIds) {
		for (QVector<MTPint>::const_iterator i = msgsIds.cbegin(), e = msgsIds.cend(); i != e; ++i) {
			MsgsData::const_iterator j = msgsData.constFind(i->v);
			if (j != msgsData.cend()) {
				History *h = (*j)->history();
				(*j)->destroy();
			}
		}
	}

	void feedUserLinks(const MTPVector<MTPcontacts_Link> &links) {
		const QVector<MTPcontacts_Link> &v(links.c_vector().v);
		for (QVector<MTPcontacts_Link>::const_iterator i = v.cbegin(), e = v.cend(); i != e; ++i) {
			const MTPDcontacts_link &dv(i->c_contacts_link());
			feedUsers(MTP_vector<MTPUser>(QVector<MTPUser>(1, dv.vuser)));
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

	void feedUserLink(MTPint userId, const MTPcontacts_MyLink &myLink, const MTPcontacts_ForeignLink &foreignLink) {
		UserData *user = userLoaded(userId.v);
		if (user) {
			bool wasContact = (user->contact > 0);
			switch (myLink.type()) {
			case mtpc_contacts_myLinkContact:
				user->contact = 1;
			break;
			case mtpc_contacts_myLinkEmpty:
			case mtpc_contacts_myLinkRequested:
				if (myLink.type() == mtpc_contacts_myLinkRequested && myLink.c_contacts_myLinkRequested().vcontact.v) {
					user->contact = 1;
				} else {
					switch (foreignLink.type()) {
					case mtpc_contacts_foreignLinkRequested:
						if (foreignLink.c_contacts_foreignLinkRequested().vhas_phone.v) {
							user->contact = 0;
						} else {
							user->contact = -1;
						}
					break;
					default: user->contact = -1; break;
					}
				}
			break;
			}
			if (user->contact > 0) {
				if (!wasContact) {
					App::main()->addNewContact(App::userFromPeer(user->id), false);
					user->input = MTP_inputPeerContact(userId);
					user->inputUser = MTP_inputUserContact(userId);
				}
			} else {
				if (user->access) {
					user->input = MTP_inputPeerForeign(userId, MTP_long(user->access));
					user->inputUser = MTP_inputUserForeign(userId, MTP_long(user->access));
				}
				if (user->contact < 0 && !user->phone.isEmpty() && App::userFromPeer(user->id) != MTP::authedId()) {
					user->contact = 0;
				}
				if (wasContact) {
					App::main()->removeContact(user);
				}
			}
			user->setName(textOneLine(user->firstName), textOneLine(user->lastName), (user->contact || user->id == 333000 || user->phone.isEmpty()) ? QString() : App::formatPhone(user->phone));
			if (App::main()) App::main()->peerUpdated(user);
		}
	}

	void feedMessageMedia(MsgId msgId, const MTPMessage &msg) {
		const MTPMessageMedia *media = 0;
		switch (msg.type()) {
		case mtpc_message: media = &msg.c_message().vmedia; break;
		case mtpc_messageForwarded: media = &msg.c_messageForwarded().vmedia; break;
		}
		if (media) {
			MsgsData::iterator i = msgsData.find(msgId);
			if (i != msgsData.cend()) {
				i.value()->updateMedia(*media);
			}
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
		const QPixmap *thumb = 0, *full = 0;
		int32 thumbLevel = -1, fullLevel = -1;
		for (PreparedPhotoThumbs::const_iterator i = thumbs.cbegin(), e = thumbs.cend(); i != e; ++i) {
			int32 newThumbLevel = -1, newFullLevel = -1;
			switch (i.key()) {
			case 's': newThumbLevel = 0; newFullLevel = 4; break; // box 100x100
			case 'm': newThumbLevel = 2; newFullLevel = 3; break; // box 320x320
			case 'x': newThumbLevel = 5; newFullLevel = 0; break; // box 800x800
			case 'y': newThumbLevel = 6; newFullLevel = 1; break; // box 1280x1280
			case 'w': newThumbLevel = 8; newFullLevel = 2; break; // box 2560x2560
			case 'a': newThumbLevel = 1; newFullLevel = 8; break; // crop 160x160
			case 'b': newThumbLevel = 3; newFullLevel = 7; break; // crop 320x320
			case 'c': newThumbLevel = 4; newFullLevel = 6; break; // crop 640x640
			case 'd': newThumbLevel = 7; newFullLevel = 5; break; // crop 1280x1280
			}
			if (newThumbLevel < 0 || newFullLevel < 0) {
				continue;
			}
			if (thumbLevel < 0 || newThumbLevel < thumbLevel) {
				thumbLevel = newThumbLevel;
				thumb = &i.value();
			}
			if (fullLevel < 0 || newFullLevel < fullLevel) {
				fullLevel = newFullLevel;
				full = &i.value();
			}
		}
		if (!thumb || !full) {
			return App::photo(0);
		}
		switch (photo.type()) {
		case mtpc_photo: {
			const MTPDphoto &ph(photo.c_photo());
			return App::photo(ph.vid.v, 0, ph.vaccess_hash.v, ph.vuser_id.v, ph.vdate.v, ImagePtr(*thumb, "JPG"), ImagePtr(*full, "JPG"));
		} break;
		case mtpc_photoEmpty: return App::photo(photo.c_photoEmpty().vid.v);
		}
		return App::photo(0);
	}

	PhotoData *feedPhoto(const MTPDphoto &photo, PhotoData *convert) {
		const QVector<MTPPhotoSize> &sizes(photo.vsizes.c_vector().v);
		const MTPPhotoSize *thumb = 0, *full = 0;
		int32 thumbLevel = -1, fullLevel = -1;
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

			int32 newThumbLevel = -1, newFullLevel = -1;
			switch (size) {
			case 's': newThumbLevel = 0; newFullLevel = 4; break; // box 100x100
			case 'm': newThumbLevel = 2; newFullLevel = 3; break; // box 320x320
			case 'x': newThumbLevel = 5; newFullLevel = 0; break; // box 800x800
			case 'y': newThumbLevel = 6; newFullLevel = 1; break; // box 1280x1280
			case 'w': newThumbLevel = 8; newFullLevel = 2; break; // box 2560x2560
			case 'a': newThumbLevel = 1; newFullLevel = 8; break; // crop 160x160
			case 'b': newThumbLevel = 3; newFullLevel = 7; break; // crop 320x320
			case 'c': newThumbLevel = 4; newFullLevel = 6; break; // crop 640x640
			case 'd': newThumbLevel = 7; newFullLevel = 5; break; // crop 1280x1280
			}
			if (newThumbLevel < 0 || newFullLevel < 0) {
				continue;
			}
			if (thumbLevel < 0 || newThumbLevel < thumbLevel) {
				thumbLevel = newThumbLevel;
				thumb = &(*i);
			}
			if (fullLevel < 0 || newFullLevel < fullLevel) {
				fullLevel = newFullLevel;
				full = &(*i);
			}
		}
		if (thumb && full) {
			return App::photo(photo.vid.v, convert, photo.vaccess_hash.v, photo.vuser_id.v, photo.vdate.v, App::image(*thumb), App::image(*full));
		}
		return App::photo(photo.vid.v, convert);
	}
	
	VideoData *feedVideo(const MTPDvideo &video, VideoData *convert) {
		return App::video(video.vid.v, convert, video.vaccess_hash.v, video.vuser_id.v, video.vdate.v, video.vduration.v, video.vw.v, video.vh.v, App::image(video.vthumb), video.vdc_id.v, video.vsize.v);
	}

	AudioData *feedAudio(const MTPDaudio &audio, AudioData *convert) {
		return App::audio(audio.vid.v, convert, audio.vaccess_hash.v, audio.vuser_id.v, audio.vdate.v, audio.vduration.v, audio.vdc_id.v, audio.vsize.v);
	}

	DocumentData *feedDocument(const MTPdocument &document, const QPixmap &thumb) {
		switch (document.type()) {
		case mtpc_document: {
			const MTPDdocument &d(document.c_document());
			return App::document(d.vid.v, 0, d.vaccess_hash.v, d.vuser_id.v, d.vdate.v, qs(d.vfile_name), qs(d.vmime_type), ImagePtr(thumb, "JPG"), d.vdc_id.v, d.vsize.v);
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
		return App::document(document.vid.v, convert, document.vaccess_hash.v, document.vuser_id.v, document.vdate.v, qs(document.vfile_name), qs(document.vmime_type), App::image(document.vthumb), document.vdc_id.v, document.vsize.v);
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

	ChatData *chat(const PeerId &peer) {
		PeerData *d = App::peer(peer);
		return d->asChat();
	}

	ChatData *chat(int32 chat) {
		return App::peer(App::peerFromChat(chat))->asChat();
	}

	PhotoData *photo(const PhotoId &photo, PhotoData *convert, const uint64 &access, int32 user, int32 date, const ImagePtr &thumb, const ImagePtr &full) {
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
				result = new PhotoData(photo, access, user, date, thumb, full);
			}
			photosData.insert(photo, result);
		} else {
			result = i.value();
			if (result != convert && !result->user && !result->date && (user || date)) {
				result->access = access;
				result->user = user;
				result->date = date;
				result->thumb = thumb;
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

	void forgetPhotos() {
		lastPhotos.clear();
		lastPhotosMap.clear();
		for (PhotosData::const_iterator i = photosData.cbegin(), e = photosData.cend(); i != e; ++i) {
			i.value()->forget();
		}
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

	void forgetVideos() {
		for (VideosData::const_iterator i = videosData.cbegin(), e = videosData.cend(); i != e; ++i) {
			i.value()->forget();
		}
	}

	AudioData *audio(const AudioId &audio, AudioData *convert, const uint64 &access, int32 user, int32 date, int32 duration, int32 dc, int32 size) {
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
				result = new AudioData(audio, access, user, date, duration, dc, size);
			}
			audiosData.insert(audio, result);
		} else {
			result = i.value();
			if (result != convert && !result->user && !result->date && (user || date)) {
				result->access = access;
				result->user = user;
				result->date = date;
				result->duration = duration;
				result->dc = dc;
				result->size = size;
			}
		}
		return result;
	}

	void forgetAudios() {
		for (AudiosData::const_iterator i = audiosData.cbegin(), e = audiosData.cend(); i != e; ++i) {
			i.value()->forget();
		}
	}

	DocumentData *document(const DocumentId &document, DocumentData *convert, const uint64 &access, int32 user, int32 date, const QString &name, const QString &mime, const ImagePtr &thumb, int32 dc, int32 size) {
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
			if (!convert->user && !convert->date && (user || date)) {
				convert->user = user;
				convert->date = date;
				convert->name = name;
				convert->mime = mime;
				convert->thumb = thumb;
				convert->dc = dc;
				convert->size = size;
			}
		}
		DocumentsData::const_iterator i = documentsData.constFind(document);
		DocumentData *result;
		if (i == documentsData.cend()) {
			if (convert) {
				result = convert;
			} else {
				result = new DocumentData(document, access, user, date, name, mime, thumb, dc, size);
			}
			documentsData.insert(document, result);
		} else {
			result = i.value();
			if (result != convert && !result->user && !result->date && (user || date)) {
				result->access = access;
				result->user = user;
				result->date = date;
				result->name = name;
				result->mime = mime;
				result->thumb = thumb;
				result->dc = dc;
				result->size = size;
			}
		}
		return result;
	}

	void forgetDocuments() {
		for (DocumentsData::const_iterator i = documentsData.cbegin(), e = documentsData.cend(); i != e; ++i) {
			i.value()->forget();
		}
	}

	MTPPhoto photoFromUserPhoto(MTPint userId, MTPint date, const MTPUserProfilePhoto &photo) {
		if (photo.type() == mtpc_userProfilePhoto) {
			const MTPDuserProfilePhoto &uphoto(photo.c_userProfilePhoto());
				
			QVector<MTPPhotoSize> photoSizes;
			photoSizes.push_back(MTP_photoSize(MTP_string("a"), uphoto.vphoto_small, MTP_int(160), MTP_int(160), MTP_int(0)));
			photoSizes.push_back(MTP_photoSize(MTP_string("c"), uphoto.vphoto_big, MTP_int(640), MTP_int(640), MTP_int(0)));

			return MTP_photo(uphoto.vphoto_id, MTP_long(0), userId, date, MTP_string(""), MTP_geoPointEmpty(), MTP_vector<MTPPhotoSize>(photoSizes));
		}
		return MTP_photoEmpty(MTP_long(0));
	}

	QString peerName(const PeerData *peer, bool forDialogs) {
		return peer ? (forDialogs ? peer->nameOrPhone : peer->name) : lang(lng_deleted);
	}

	Histories &histories() {
		return ::histories;
	}

	History *history(const PeerId &peer, int32 unreadCnt) {
		Histories::const_iterator i = ::histories.constFind(peer);
		if (i == ::histories.cend()) {
			i = App::histories().insert(peer, new History(peer));
			i.value()->setUnreadCount(unreadCnt, false);
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

	HistoryItem *historyRegItem(HistoryItem *item) {
		MsgsData::const_iterator i = msgsData.constFind(item->id);
		if (i == msgsData.cend()) {
			msgsData.insert(item->id, item);
			if (item->id > ::maxMsgId) ::maxMsgId = item->id;
			return 0;
		}
		if (i.value() != item && !i.value()->block() && item->block()) { // replace search item
			item->history()->itemReplaced(i.value(), item);
			if (App::main()) {
				emit App::main()->historyItemReplaced(i.value(), item);
			}
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
	}

	void historyUnregItem(HistoryItem *item) {
		MsgsData::iterator i = msgsData.find(item->id);
		if (i != msgsData.cend()) {
			if (i.value() == item) {
				msgsData.erase(i);
			}
		}
		historyItemDetached(item);
		if (App::main()) {
			emit App::main()->historyItemDeleted(item);
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
		::videoItems.clear();
		::audioItems.clear();
		::documentItems.clear();
		lastPhotos.clear();
		lastPhotosMap.clear();
		::self = 0;
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
		if (!newMsgSound) {
			newMsgSound = new QSoundEffect();
			newMsgSound->setSource(QUrl::fromLocalFile(st::newMsgSound));
			newMsgSound->setVolume(1);
		}

		if (!::sprite) {
			::sprite = new QPixmap(st::spriteFile);
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

		if (completely && App::main()) {
			App::main()->disconnect(SIGNAL(historyItemDeleted(HistoryItem *)));
		}

		histories().clear();

		if (completely) {
			LOG(("Deleting sound.."));
			delete newMsgSound;
			LOG(("Sound deleted!"));
			newMsgSound = 0;

			delete ::sprite;
			::sprite = 0;
			delete ::emojis;
			::emojis = 0;
			mainEmojisMap.clear();
			otherEmojisMap.clear();

			clearAllImages();
		} else {
			clearStorageImages();
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

	QPixmap &sprite() {
		return *::sprite;
	}

	QPixmap &emojis() {
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
			i = map->insert(emoji->code, QPixmap::fromImage(img));
		}
		return i.value();
	}

	void playSound() {
		if (cSoundNotify() && newMsgSound) newMsgSound->play();
	}

	void writeConfig() {
		QDir().mkdir(cWorkingDir() + qsl("tdata"));
		QFile configFile(cWorkingDir() + qsl("tdata/config"));
		if (configFile.open(QIODevice::WriteOnly)) {
			DEBUG_LOG(("App Info: writing config file"));
			QDataStream configStream(&configFile);
			configStream.setVersion(QDataStream::Qt_5_1);
			configStream << quint32(dbiVersion) << qint32(AppVersion);

			configStream << quint32(dbiAutoStart) << qint32(cAutoStart());
			configStream << quint32(dbiStartMinimized) << qint32(cStartMinimized());
			configStream << quint32(dbiWorkMode) << qint32(cWorkMode());
			configStream << quint32(dbiSeenTrayTooltip) << qint32(cSeenTrayTooltip());
			configStream << quint32(dbiAutoUpdate) << qint32(cAutoUpdate());
			configStream << quint32(dbiLastUpdateCheck) << qint32(cLastUpdateCheck());
			configStream << quint32(dbiScale) << qint32(cConfigScale());

			configStream << quint32(dbiConnectionType) << qint32(cConnectionType());
			if (cConnectionType() == dbictHttpProxy || cConnectionType() == dbictTcpProxy) {
				const ConnectionProxy &proxy(cConnectionProxy());
				configStream << proxy.host << qint32(proxy.port) << proxy.user << proxy.password;
			}

			TWindowPos pos(cWindowPos());
			configStream << quint32(dbiWindowPosition) << qint32(pos.x) << qint32(pos.y) << qint32(pos.w) << qint32(pos.h) << qint32(pos.moncrc) << qint32(pos.maximized);

			if (configStream.status() != QDataStream::Ok) {
				LOG(("App Error: could not write user config file, status: %1").arg(configStream.status()));
			}
		} else {
			LOG(("App Error: could not open user config file for writing"));
		}
	}

	void readConfig() {
		QFile configFile(cWorkingDir() + qsl("tdata/config"));
		if (configFile.open(QIODevice::ReadOnly)) {
			DEBUG_LOG(("App Info: config file opened for reading"));
			QDataStream configStream(&configFile);
			configStream.setVersion(QDataStream::Qt_5_1);

			qint32 configVersion = 0;
			while (true) {
				quint32 blockId;
				configStream >> blockId;
				if (configStream.status() == QDataStream::ReadPastEnd) {
					DEBUG_LOG(("App Info: config file read end"));
					break;
				} else if (configStream.status() != QDataStream::Ok) {
					LOG(("App Error: could not read block id, status: %1 - config file is corrupted?..").arg(configStream.status()));
					break;
				}

				if (blockId == dbiVersion) {
					configStream >> configVersion;
					if (configVersion > AppVersion) break;
					continue;
				}

				switch (blockId) {
				case dbiAutoStart: {
					qint32 v;
					configStream >> v;
					cSetAutoStart(v == 1);
				} break;

				case dbiStartMinimized: {
					qint32 v;
					configStream >> v;
					cSetStartMinimized(v == 1);
				} break;

				case dbiSoundNotify: {
					if (configVersion < 3008) {
						qint32 v;
						configStream >> v;
						cSetSoundNotify(v == 1);
						cSetNeedConfigResave(true);
					}
				} break;

				case dbiDesktopNotify: {
					if (configVersion < 3008) {
						qint32 v;
						configStream >> v;
						cSetDesktopNotify(v == 1);
						cSetNeedConfigResave(true);
					}
				} break;

				case dbiWorkMode: {
					qint32 v;
					configStream >> v;
					switch (v) {
					case dbiwmTrayOnly: cSetWorkMode(dbiwmTrayOnly); break;
					case dbiwmWindowOnly: cSetWorkMode(dbiwmWindowOnly); break;
					default: cSetWorkMode(dbiwmWindowAndTray); break;
					};
				} break;

				case dbiConnectionType: {
					qint32 v;
					configStream >> v;

					switch (v) {
					case dbictHttpProxy:
					case dbictTcpProxy: {
						ConnectionProxy p;
						qint32 port;
						configStream >> p.host >> port >> p.user >> p.password;
						p.port = uint32(port);
						cSetConnectionProxy(p);
					}
					cSetConnectionType(DBIConnectionType(v));
					break;
					case dbictHttpAuto:
					default: cSetConnectionType(dbictAuto); break;
					};
				} break;

				case dbiSeenTrayTooltip: {
					qint32 v;
					configStream >> v;
					cSetSeenTrayTooltip(v == 1);
				} break;

				case dbiAutoUpdate: {
					qint32 v;
					configStream >> v;
					cSetAutoUpdate(v == 1);
				} break;

				case dbiLastUpdateCheck: {
					qint32 v;
					configStream >> v;
					cSetLastUpdateCheck(v);
				} break;

				case dbiScale: {
					qint32 v;
					configStream >> v;

					DBIScale s = cRealScale();
					switch (v) {
					case dbisAuto: s = dbisAuto; break;
					case dbisOne: s = dbisOne; break;
					case dbisOneAndQuarter: s = dbisOneAndQuarter; break;
					case dbisOneAndHalf: s = dbisOneAndHalf; break;
					case dbisTwo: s = dbisTwo; break;
					}
					cSetConfigScale(s);
					cSetRealScale(s);
				} break;

				case dbiWindowPosition: {
					TWindowPos pos;
					configStream >> pos.x >> pos.y >> pos.w >> pos.h >> pos.moncrc >> pos.maximized;
					cSetWindowPos(pos);
				} break;
				}

				if (configStream.status() != QDataStream::Ok) {
					LOG(("App Error: could not read data, status: %1 - user config file is corrupted?..").arg(configStream.status()));
					break;
				}
			}
		}
	}

	void writeUserConfig() {
		QFile configFile(cWorkingDir() + cDataFile() + qsl("_config"));
		if (configFile.open(QIODevice::WriteOnly)) {
			DEBUG_LOG(("App Info: writing user config data for encrypt"));
			QByteArray toEncrypt;
			toEncrypt.reserve(65536);
			toEncrypt.resize(4);
			{
				QBuffer buffer(&toEncrypt);
				buffer.open(QIODevice::Append);

				QDataStream stream(&buffer);
				stream.setVersion(QDataStream::Qt_5_1);

				if (MTP::authedId()) {
					stream << quint32(dbiUser) << qint32(MTP::authedId()) << quint32(MTP::maindc());
				}

				stream << quint32(dbiSendKey) << qint32(cCtrlEnter() ? dbiskCtrlEnter : dbiskEnter);
				stream << quint32(dbiCatsAndDogs) << qint32(cCatsAndDogs() ? 1 : 0);
				stream << quint32(dbiReplaceEmojis) << qint32(cReplaceEmojis() ? 1 : 0);
				stream << quint32(dbiDefaultAttach) << qint32(cDefaultAttach());
				stream << quint32(dbiSoundNotify) << qint32(cSoundNotify());
				stream << quint32(dbiDesktopNotify) << qint32(cDesktopNotify());
				stream << quint32(dbiAskDownloadPath) << qint32(cAskDownloadPath());
				stream << quint32(dbiDownloadPath) << (cAskDownloadPath() ? QString() : cDownloadPath());
				stream << quint32(dbiEmojiTab) << qint32(cEmojiTab());

				RecentEmojiPreload v;
				v.reserve(cGetRecentEmojis().size());
				for (RecentEmojiPack::const_iterator i = cGetRecentEmojis().cbegin(), e = cGetRecentEmojis().cend(); i != e; ++i) {
					v.push_back(qMakePair(i->first->code, i->second));
				}
				stream << quint32(dbiRecentEmojis) << v;

				writeAllMuted(stream);

				MTP::writeConfig(stream);
				if (stream.status() != QDataStream::Ok) {
					LOG(("App Error: could not write user config to memory buf, status: %1").arg(stream.status()));
					return;
				}
			}
			*(uint32*)(toEncrypt.data()) = toEncrypt.size();

			uint32 size = toEncrypt.size(), fullSize = size;
			if (fullSize & 0x0F) {
				fullSize += 0x10 - (fullSize & 0x0F);
				toEncrypt.resize(fullSize);
				memset_rand(toEncrypt.data() + size, fullSize - size);
			}
			QByteArray encrypted(16 + fullSize, Qt::Uninitialized); // 128bit of sha1 - key128, sizeof(data), data
			hashSha1(toEncrypt.constData(), toEncrypt.size(), encrypted.data());
			aesEncryptLocal(toEncrypt.constData(), encrypted.data() + 16, fullSize, &MTP::localKey(), encrypted.constData());

			DEBUG_LOG(("App Info: writing user config file"));
			QDataStream configStream(&configFile);
			configStream.setVersion(QDataStream::Qt_5_1);
			configStream << quint32(dbiVersion) << qint32(AppVersion);

			configStream << quint32(dbiEncryptedWithSalt) << cLocalSalt() << encrypted; // write all encrypted data

			if (configStream.status() != QDataStream::Ok) {
				LOG(("App Error: could not write user config file, status: %1").arg(configStream.status()));
			}
		} else {
			LOG(("App Error: could not open user config file for writing"));
		}
	}

	void readUserConfigFields(QIODevice *io) {
		if (!io->isOpen()) io->open(QIODevice::ReadOnly);

		QDataStream stream(io);
		stream.setVersion(QDataStream::Qt_5_1);

		while (true) {
			quint32 blockId;
			stream >> blockId;
			if (stream.status() == QDataStream::ReadPastEnd) {
				DEBUG_LOG(("App Info: config file read end"));
				break;
			} else if (stream.status() != QDataStream::Ok) {
				LOG(("App Error: could not read block id, status: %1 - user config file is corrupted?..").arg(stream.status()));
				break;
			}

			if (blockId == dbiVersion) { // should not be in encrypted part, just ignore
				qint32 configVersion;
				stream >> configVersion;
				continue;
			}

			switch (blockId) {
			case dbiEncryptedWithSalt: {
				QByteArray salt, data, decrypted;
				stream >> salt >> data;

				if (salt.size() != 32) {
					LOG(("App Error: bad salt in encrypted part, size: %1").arg(salt.size()));
					continue;
				}

				cSetLocalSalt(salt);
				MTP::createLocalKey(QByteArray(), &salt);

				if (data.size() <= 16 || (data.size() & 0x0F)) {
					LOG(("App Error: bad encrypted part size: %1").arg(data.size()));
					continue;
				}
				uint32 fullDataLen = data.size() - 16;
				decrypted.resize(fullDataLen);
				const char *dataKey = data.constData(), *encrypted = data.constData() + 16;
				aesDecryptLocal(encrypted, decrypted.data(), fullDataLen, &MTP::localKey(), dataKey);
				uchar sha1Buffer[20];
				if (memcmp(hashSha1(decrypted.constData(), decrypted.size(), sha1Buffer), dataKey, 16)) {
					LOG(("App Error: bad decrypt key, data from user-config not decrypted"));
					continue;
				}
				uint32 dataLen = *(const uint32*)decrypted.constData();
				if (dataLen > uint32(decrypted.size()) || dataLen <= fullDataLen - 16 || dataLen < 4) {
					LOG(("App Error: bad decrypted part size: %1, fullDataLen: %2, decrypted size: %3").arg(dataLen).arg(fullDataLen).arg(decrypted.size()));
					continue;
				}
				decrypted.resize(dataLen);
				QBuffer decryptedStream(&decrypted);
				decryptedStream.open(QIODevice::ReadOnly);
				decryptedStream.seek(4); // skip size
				readUserConfigFields(&decryptedStream);
			} break;

			case dbiLoggedPhoneNumber: {
				QString v;
				stream >> v;
				if (stream.status() == QDataStream::Ok) {
					cSetLoggedPhoneNumber(v);
				}
			} break;

			case dbiMutePeer: {
				readOneMuted(stream);
			} break;

			case dbiMutedPeers: {
				readAllMuted(stream);
			} break;

			case dbiSendKey: {
				qint32 v;
				stream >> v;
				cSetCtrlEnter(v == dbiskCtrlEnter);
			} break;

			case dbiCatsAndDogs: {
				qint32 v;
				stream >> v;
				cSetCatsAndDogs(v == 1);
			} break;

			case dbiReplaceEmojis: {
				qint32 v;
				stream >> v;
				cSetReplaceEmojis(v == 1);
			} break;

			case dbiDefaultAttach: {
				qint32 v;
				stream >> v;
				switch (v) {
				case dbidaPhoto: cSetDefaultAttach(dbidaPhoto); break;
				default: cSetDefaultAttach(dbidaDocument); break;
				}
			} break;

			case dbiSoundNotify: {
				qint32 v;
				stream >> v;
				cSetSoundNotify(v == 1);
			} break;

			case dbiDesktopNotify: {
				qint32 v;
				stream >> v;
				cSetDesktopNotify(v == 1);
			} break;

			case dbiAskDownloadPath: {
				qint32 v;
				stream >> v;
				cSetAskDownloadPath(v == 1);
			} break;

			case dbiDownloadPath: {
				QString v;
				stream >> v;
				cSetDownloadPath(v);
			} break;

			case dbiEmojiTab: {
				qint32 v;
				stream >> v;
				switch (v) {
				case dbietRecent : cSetEmojiTab(dbietRecent);  break;
				case dbietPeople : cSetEmojiTab(dbietPeople);  break;
				case dbietNature : cSetEmojiTab(dbietNature);  break;
				case dbietObjects: cSetEmojiTab(dbietObjects); break;
				case dbietPlaces : cSetEmojiTab(dbietPlaces);  break;
				case dbietSymbols: cSetEmojiTab(dbietSymbols); break;
				}
			} break;

			case dbiRecentEmojis: {
				RecentEmojiPreload v;
				stream >> v;
				cSetRecentEmojisPreload(v);
			} break;

			default:
				if (!MTP::readConfigElem(blockId, stream)) {
				}
				break;
			}

			if (stream.status() != QDataStream::Ok) {
				LOG(("App Error: could not read data, status: %1 - user config file is corrupted?..").arg(stream.status()));
				break;
			}
		}
	}

	void readUserConfig() {
		QFile configFile(cWorkingDir() + cDataFile() + qsl("_config"));
		if (configFile.open(QIODevice::ReadOnly)) {
			DEBUG_LOG(("App Info: user config file opened for reading"));
			{
				QDataStream configStream(&configFile);
				configStream.setVersion(QDataStream::Qt_5_1);

				quint32 blockId;
				configStream >> blockId;
				if (configStream.status() == QDataStream::ReadPastEnd) {
					DEBUG_LOG(("App Info: config file read end"));
					return;
				} else if (configStream.status() != QDataStream::Ok) {
					LOG(("App Error: could not read block id, status: %1 - user config file is corrupted?..").arg(configStream.status()));
					return;
				}

				if (blockId == dbiVersion) {
					qint32 configVersion;
					configStream >> configVersion;
					if (configVersion > AppVersion) return;

					configStream >> blockId;
					if (configStream.status() == QDataStream::ReadPastEnd) {
						DEBUG_LOG(("App Info: config file read end"));
						return;
					} else if (configStream.status() != QDataStream::Ok) {
						LOG(("App Error: could not read block id, status: %1 - user config file is corrupted?..").arg(configStream.status()));
						return;
					}
					if (blockId != dbiEncryptedWithSalt) { // old version data - not encrypted
						cSetNeedConfigResave(true);
					}
				} else {
					cSetNeedConfigResave(true);
				}
			}

			configFile.reset();
			readUserConfigFields(&configFile);
		}
	}

	void writeAllMuted(QDataStream &stream) { // deprecated
	}

	void readOneMuted(QDataStream &stream) { // deprecated
		quint64 peerId;
		stream >> peerId;
	}

	void readAllMuted(QDataStream &stream) {
		quint32 count;
		stream >> count;

		for (uint32 i = 0; i < count; ++i) {
			readOneMuted(stream);
		}
	}

	void checkImageCacheSize() {
		int64 nowImageCacheSize = imageCacheSize();
		if (nowImageCacheSize > serviceImageCacheSize + MemoryForImageCache) {
			App::forgetPhotos();
			App::forgetVideos();
			App::forgetAudios();
			App::forgetDocuments();
			serviceImageCacheSize = imageCacheSize();
		}
	}

	bool isValidPhone(QString phone) {
		phone = phone.replace(QRegularExpression(qsl("[^\\d]")), QString());
		return phone.length() >= 8 || phone == qsl("777") || phone == qsl("333") || phone == qsl("42") || phone == qsl("111");
	}

	void quit() {
		if (quiting()) return;

		setQuiting();
		if (wnd()) {
			wnd()->notifyClearFast();
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


    QImage readImage(QByteArray data, QByteArray *format) {
        QByteArray tmpFormat;
		QImage result;
		QBuffer buffer(&data);
        if (!format) {
            format = &tmpFormat;
        }
        QImageReader reader(&buffer, *format);
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
		} else {
			QImage solid(result.width(), result.height(), QImage::Format_ARGB32_Premultiplied);
			solid.fill(st::white->c);
			{
				QPainter(&solid).drawImage(0, 0, result);
			}
			result = solid;
		}
		return result;
	}

    QImage readImage(const QString &file, QByteArray *format) {
		QFile f(file);
		if (!f.open(QIODevice::ReadOnly)) {
			return QImage();
		}
		return readImage(f.readAll(), format);
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

}
