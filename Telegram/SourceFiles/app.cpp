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

#include "audio.h"
#include "application.h"
#include "fileuploader.h"
#include "mainwidget.h"
#include <libexif/exif-data.h>

#include "localstorage.h"

#include "numbers.h"

namespace {
	bool quiting = false;

	UserData *self = 0;

	typedef QHash<PeerId, PeerData*> PeersData;
	PeersData peersData;

	typedef QMap<PeerData*, bool> MutedPeers;
	MutedPeers mutedPeers;

	typedef QMap<PeerData*, bool> UpdatedPeers;
	UpdatedPeers updatedPeers;

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

	typedef QMap<MsgId, ReplyMarkup> ReplyMarkups;
	ReplyMarkups replyMarkups;
	ReplyMarkup zeroMarkup(MTPDreplyKeyboardMarkup_flag_ZERO);
	typedef QMap<ChannelId, ReplyMarkups> ChannelReplyMarkups;
	ChannelReplyMarkups channelReplyMarkups;

	VideoItems videoItems;
	AudioItems audioItems;
	DocumentItems documentItems;
	WebPageItems webPageItems;
	typedef QMap<HistoryItem*, QMap<HistoryReply*, bool> > RepliesTo;
	RepliesTo repliesTo;

	typedef QMap<int32, QString> SharedContactPhones;
	SharedContactPhones sharedContactPhones;

	Histories histories;

	typedef QHash<MsgId, HistoryItem*> MsgsData;
	MsgsData msgsData;
	typedef QMap<ChannelId, MsgsData> ChannelMsgsData;
	ChannelMsgsData channelMsgsData;

	typedef QMap<uint64, FullMsgId> RandomData;
	RandomData randomData;

	typedef QMap<uint64, QPair<PeerId, QString> > SentData;
	SentData sentData;

	HistoryItem *hoveredItem = 0, *pressedItem = 0, *hoveredLinkItem = 0, *pressedLinkItem = 0, *contextItem = 0, *mousedItem = 0;

	QPixmap *sprite = 0, *emoji = 0, *emojiLarge = 0;

	struct CornersPixmaps {
		CornersPixmaps() {
			memset(p, 0, sizeof(p));
		}
		QPixmap *p[4];
	};
	CornersPixmaps corners[RoundCornersCount];
	typedef QMap<uint32, CornersPixmaps> CornersMap;
	CornersMap cornersMap;
	QImage *cornersMask[4] = { 0 };

	typedef QMap<uint64, QPixmap> EmojiMap;
	EmojiMap mainEmojiMap;
	QMap<int32, EmojiMap> otherEmojiMap;

	int32 serviceImageCacheSize = 0;

	typedef QLinkedList<PhotoData*> LastPhotosList;
	LastPhotosList lastPhotos;
	typedef QHash<PhotoData*, LastPhotosList::iterator> LastPhotosMap;
	LastPhotosMap lastPhotosMap;

	style::color _msgServiceBg;
	style::color _msgServiceSelectBg;
	style::color _historyScrollBarColor;
	style::color _historyScrollBgColor;
	style::color _historyScrollBarOverColor;
	style::color _historyScrollBgOverColor;
	style::color _introPointHoverColor;
}

namespace App {

	QString formatPhone(QString phone) {
		if (phone.isEmpty()) return QString();
		QString number = phone;
		for (const QChar *ch = phone.constData(), *e = ch + phone.size(); ch != e; ++ch) {
			if (ch->unicode() < '0' || ch->unicode() > '9') {
				number = phone.replace(QRegularExpression(qsl("[^\\d]")), QString());
			}
		}
		QVector<int> groups = phoneNumberParse(number);
		if (groups.isEmpty()) return '+' + number;

		QString result;
		result.reserve(number.size() + groups.size() + 1);
		result.append('+');
		int32 sum = 0;
		for (int32 i = 0, l = groups.size(); i < l; ++i) {
			result.append(number.midRef(sum, groups.at(i)));
			sum += groups.at(i);
			if (sum < number.size()) result.append(' ');
		}
		if (sum < number.size()) result.append(number.midRef(sum));
		return result;
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

	int32 onlineForSort(UserData *user, int32 now) {
		if (isServiceUser(user->id) || user->botInfo) {
			return -1;
		}
		int32 online = user->onlineTill;
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

	int32 onlineWillChangeIn(UserData *user, int32 now) {
		if (isServiceUser(user->id) || user->botInfo) {
			return 86400;
		}
		int32 online = user->onlineTill;
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
		if (isNotificationsUser(user->id)) {
			return lang(lng_status_service_notifications);
		} else if (isServiceUser(user->id)) {
			return lang(lng_status_support);
		} else if (user->botInfo) {
			return lang(lng_status_bot);
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

	bool onlineColorUse(UserData *user, int32 now) {
		if (isServiceUser(user->id) || user->botInfo) {
			return false;
		}
		int32 online = user->onlineTill;
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

	UserData *feedUsers(const MTPVector<MTPUser> &users, bool emitPeerUpdated) {
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
				data->input = MTP_inputPeerUser(d.vid, MTP_long(0));
				data->inputUser = MTP_inputUser(d.vid, MTP_long(0));
				data->setName(lang(lng_deleted), QString(), QString(), QString());
				data->setPhoto(MTP_userProfilePhotoEmpty());
				data->access = UserNoAccess;
				data->setBotInfoVersion(-1);
				wasContact = (data->contact > 0);
				status = &emptyStatus;
				data->contact = -1;
			} break;
			case mtpc_user: {
				const MTPDuser &d(user.c_user());

				PeerId peer(peerFromUser(d.vid.v));
				data = App::user(peer);
				int32 flags = d.vflags.v;
				if (flags & MTPDuser_flag_self) {
					data->input = MTP_inputPeerSelf();
					data->inputUser = MTP_inputUserSelf();
				} else if (!d.has_access_hash()) {
					data->input = MTP_inputPeerUser(d.vid, MTP_long((data->access == UserNoAccess) ? 0 : data->access));
					data->inputUser = MTP_inputUser(d.vid, MTP_long((data->access == UserNoAccess) ? 0 : data->access));
				} else {
					data->input = MTP_inputPeerUser(d.vid, d.vaccess_hash);
					data->inputUser = MTP_inputUser(d.vid, d.vaccess_hash);
				}
				if (flags & MTPDuser_flag_deleted) {
					data->setPhone(QString());
					data->setName(lang(lng_deleted), QString(), QString(), QString());
					data->setPhoto(MTP_userProfilePhotoEmpty());
					data->access = UserNoAccess;
					status = &emptyStatus;
				} else {
					QString phone = d.has_phone() ? qs(d.vphone) : QString();
					QString fname = d.has_first_name() ? textOneLine(qs(d.vfirst_name)) : QString();
					QString lname = d.has_last_name() ? textOneLine(qs(d.vlast_name)) : QString();
					QString uname = d.has_username() ? textOneLine(qs(d.vusername)) : QString();

					bool phoneChanged = (data->phone != phone);
					if (phoneChanged) data->setPhone(phone);

					bool nameChanged = (data->firstName != fname) || (data->lastName != lname);

					bool showPhone = !isServiceUser(data->id) && !(flags & (MTPDuser_flag_self | MTPDuser_flag_contact | MTPDuser_flag_mutual_contact));
					bool showPhoneChanged = !isServiceUser(data->id) && !(flags & (MTPDuser_flag_self)) && ((showPhone && data->contact) || (!showPhone && !data->contact));

					// see also Local::readPeer

					QString pname = (showPhoneChanged || phoneChanged || nameChanged) ? ((showPhone && !phone.isEmpty()) ? formatPhone(phone) : QString()) : data->nameOrPhone;

					data->setName(fname, lname, pname, uname);
					if (d.has_photo()) {
						data->setPhoto(d.vphoto);
					} else {
						data->setPhoto(MTP_userProfilePhotoEmpty());
					}
					if (d.has_access_hash()) data->access = d.vaccess_hash.v;
					status = d.has_status() ? &d.vstatus : &emptyStatus;
				}
				wasContact = (data->contact > 0);
				if (d.has_bot_info_version()) {
					data->setBotInfoVersion(d.vbot_info_version.v);
					data->botInfo->readsAllHistory = (d.vflags.v & MTPDuser_flag_bot_reads_all);
					data->botInfo->cantJoinGroups = (d.vflags.v & MTPDuser_flag_bot_cant_join);
				} else {
					data->setBotInfoVersion(-1);
				}
				data->contact = (flags & (MTPDuser_flag_contact | MTPDuser_flag_mutual_contact)) ? 1 : (data->phone.isEmpty() ? -1 : 0);
				if (data->contact == 1 && cReportSpamStatuses().value(data->id, dbiprsNoButton) != dbiprsNoButton) {
					cRefReportSpamStatuses().insert(data->id, dbiprsNoButton);
					Local::writeReportSpamStatuses();
				}
				if ((flags & MTPDuser_flag_self) && ::self != data) {
					::self = data;
					if (App::wnd()) App::wnd()->updateGlobalMenu();
				}
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

            if (data->contact < 0 && !data->phone.isEmpty() && peerToUser(data->id) != MTP::authedId()) {
				data->contact = 0;
			}
			if (App::main()) {
				if (data->contact > 0 && !wasContact) {
					App::main()->addNewContact(peerToUser(data->id), false);
				} else if (wasContact && data->contact <= 0) {
					App::main()->removeContact(data);
				}

				if (emitPeerUpdated) {
					App::main()->peerUpdated(data);
				} else {
					markPeerUpdated(data);
				}
			}
		}

		return data;
	}

	PeerData *feedChats(const MTPVector<MTPChat> &chats, bool emitPeerUpdated) {
		PeerData *data = 0;
		const QVector<MTPChat> &v(chats.c_vector().v);
		for (QVector<MTPChat>::const_iterator i = v.cbegin(), e = v.cend(); i != e; ++i) {
			const MTPchat &chat(*i);
			data = 0;
			switch (chat.type()) {
			case mtpc_chat: {
				const MTPDchat &d(chat.c_chat());

				data = App::chat(peerFromChat(d.vid.v));
				data->input = MTP_inputPeerChat(d.vid);

				data->updateName(qs(d.vtitle), QString(), QString());

				ChatData *cdata = data->asChat();
				cdata->setPhoto(d.vphoto);
				cdata->date = d.vdate.v;
				cdata->count = d.vparticipants_count.v;
				cdata->isForbidden = (d.vflags.v & MTPDchat_flag_kicked);
				cdata->haveLeft = (d.vflags.v & MTPDchat_flag_left);
				if (cdata->version < d.vversion.v) {
					cdata->version = d.vversion.v;
					cdata->participants = ChatData::Participants();
					cdata->botStatus = 0;
				}
			} break;
			case mtpc_chatForbidden: {
				const MTPDchatForbidden &d(chat.c_chatForbidden());

				data = App::chat(peerFromChat(d.vid.v));
				data->input = MTP_inputPeerChat(d.vid);

				data->updateName(qs(d.vtitle), QString(), QString());

				ChatData *cdata = data->asChat();
				cdata->setPhoto(MTP_chatPhotoEmpty());
				cdata->date = 0;
				cdata->count = -1;
				cdata->isForbidden = true;
				cdata->haveLeft = false;
			} break;
			case mtpc_channel: {
				const MTPDchannel &d(chat.c_channel());

				PeerId peer(peerFromChannel(d.vid.v));
				data = App::channel(peer);
				data->input = MTP_inputPeerChannel(d.vid, d.vaccess_hash);

				ChannelData *cdata = data->asChannel();
				cdata->inputChannel = MTP_inputChannel(d.vid, d.vaccess_hash);
				
				QString uname = d.has_username() ? textOneLine(qs(d.vusername)) : QString();
				cdata->setName(qs(d.vtitle), uname);

				cdata->access = d.vaccess_hash.v;
				cdata->setPhoto(d.vphoto);
				cdata->date = d.vdate.v;
				cdata->flags = d.vflags.v;
				cdata->isForbidden = false;

				if (cdata->version < d.vversion.v) {
					cdata->version = d.vversion.v;
				}
			} break;
			case mtpc_channelForbidden: {
				const MTPDchannelForbidden &d(chat.c_channelForbidden());

				PeerId peer(peerFromChannel(d.vid.v));
				data = App::channel(peer);
				data->input = MTP_inputPeerChannel(d.vid, d.vaccess_hash);

				ChannelData *cdata = data->asChannel();
				cdata->inputChannel = MTP_inputChannel(d.vid, d.vaccess_hash);

				cdata->setName(qs(d.vtitle), QString());

				cdata->access = d.vaccess_hash.v;
				cdata->setPhoto(MTP_chatPhotoEmpty());
				cdata->date = 0;
				cdata->count = 0;
				cdata->isForbidden = true;
			} break;
			}
			if (!data) continue;

			data->loaded = true;
			if (App::main()) {
				if (emitPeerUpdated) {
					App::main()->peerUpdated(data);
				} else {
					markPeerUpdated(data);
				}
			}
		}
		return data;
	}

	void feedParticipants(const MTPChatParticipants &p, bool requestBotInfos, bool emitPeerUpdated) {
		ChatData *chat = 0;
		switch (p.type()) {
		case mtpc_chatParticipantsForbidden: {
			const MTPDchatParticipantsForbidden &d(p.c_chatParticipantsForbidden());
			chat = App::chat(d.vchat_id.v);
			chat->count = -1;
		} break;

		case mtpc_chatParticipants: {
			const MTPDchatParticipants &d(p.c_chatParticipants());
			chat = App::chat(d.vchat_id.v);
			chat->creator = d.vadmin_id.v;
			if (!requestBotInfos || chat->version <= d.vversion.v) { // !requestBotInfos is true on getFullChat result
				chat->version = d.vversion.v;
				const QVector<MTPChatParticipant> &v(d.vparticipants.c_vector().v);
				chat->count = v.size();
				int32 pversion = chat->participants.isEmpty() ? 1 : (chat->participants.begin().value() + 1);
				chat->cankick = ChatData::CanKick();
				for (QVector<MTPChatParticipant>::const_iterator i = v.cbegin(), e = v.cend(); i != e; ++i) {
					if (i->type() != mtpc_chatParticipant) continue;

					const MTPDchatParticipant &p(i->c_chatParticipant());
					//if (p.vuser_id.v == MTP::authedId()) {
					//	chat->inviter = p.vinviter_id.v; // we use inviter only from service msgs
					//	chat->inviteDate = p.vdate.v;
					//}
					UserData *user = App::userLoaded(p.vuser_id.v);
					if (user) {
						chat->participants[user] = pversion;
						if (p.vinviter_id.v == MTP::authedId()) {
							chat->cankick[user] = true;
						}
					} else {
						chat->participants = ChatData::Participants();
						chat->botStatus = 0;
						break;
					}
				}
				if (!chat->participants.isEmpty()) {
					History *h = App::historyLoaded(chat->id);
					bool found = !h || !h->lastKeyboardFrom;
					int32 botStatus = -1;
					for (ChatData::Participants::iterator i = chat->participants.begin(), e = chat->participants.end(); i != e;) {
						if (i.value() < pversion) {
							i = chat->participants.erase(i);
						} else {
							if (i.key()->botInfo) {
								botStatus = (botStatus > 0/* || i.key()->botInfo->readsAllHistory*/) ? 2 : 1;
								if (requestBotInfos && !i.key()->botInfo->inited && App::api()) App::api()->requestFullPeer(i.key());
							}
							if (!found && i.key()->id == h->lastKeyboardFrom) {
								found = true;
							}
							++i;
						}
					}
					chat->botStatus = botStatus;
					if (!found) {
						h->lastKeyboardId = 0;
						h->lastKeyboardFrom = 0;
						if (App::main()) App::main()->updateBotKeyboard();
					}
				}
			}
		} break;
		}
		if (chat && App::main()) {
			if (emitPeerUpdated) {
				App::main()->peerUpdated(chat);
			} else {
				markPeerUpdated(chat);
			}
		}
	}

	void feedParticipantAdd(const MTPDupdateChatParticipantAdd &d, bool emitPeerUpdated) {
		ChatData *chat = App::chat(d.vchat_id.v);
		if (chat->version <= d.vversion.v && chat->count >= 0) {
			chat->version = d.vversion.v;
			//if (d.vuser_id.v == MTP::authedId()) {
			//	chat->inviter = d.vinviter_id.v; // we use inviter only from service msgs
			//	chat->inviteDate = unixtime(); // no event date here :(
			//}
			UserData *user = App::userLoaded(d.vuser_id.v);
			if (user) {
				if (chat->participants.isEmpty() && chat->count) {
					chat->count++;
					chat->botStatus = 0;
				} else if (chat->participants.find(user) == chat->participants.end()) {
					chat->participants[user] = (chat->participants.isEmpty() ? 1 : chat->participants.begin().value());
					if (d.vinviter_id.v == MTP::authedId()) {
						chat->cankick[user] = true;
					} else {
						chat->cankick.remove(user);
					}
					chat->count++;
					if (user->botInfo) {
						chat->botStatus = (chat->botStatus > 0/* || !user->botInfo->readsAllHistory*/) ? 2 : 1;
						if (!user->botInfo->inited && App::api()) App::api()->requestFullPeer(user);
					}
				}
			} else {
				chat->participants = ChatData::Participants();
				chat->botStatus = 0;
				chat->count++;
			}
			if (App::main()) {
				if (emitPeerUpdated) {
					App::main()->peerUpdated(chat);
				} else {
					markPeerUpdated(chat);
				}
			}
		}
	}

	void feedParticipantDelete(const MTPDupdateChatParticipantDelete &d, bool emitPeerUpdated) {
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

						History *h = App::historyLoaded(chat->id);
						if (h && h->lastKeyboardFrom == user->id) {
							h->lastKeyboardId = 0;
							h->lastKeyboardFrom = 0;
							if (App::main()) App::main()->updateBotKeyboard();
						}
					}
					if (chat->botStatus > 0 && user->botInfo) {
						int32 botStatus = -1;
						for (ChatData::Participants::const_iterator j = chat->participants.cbegin(), e = chat->participants.cend(); j != e; ++j) {
							if (j.key()->botInfo) {
								if (botStatus > 0/* || !j.key()->botInfo->readsAllHistory*/) {
									botStatus = 2;
									break;
								}
								botStatus = 1;
							}
						}
						chat->botStatus = botStatus;
					}
				}
			} else {
				chat->participants = ChatData::Participants();
				chat->botStatus = 0;
				chat->count--;
			}
			if (App::main()) {
				if (emitPeerUpdated) {
					App::main()->peerUpdated(chat);
				} else {
					markPeerUpdated(chat);
				}
			}
		}
	}

	bool checkEntitiesAndViewsUpdate(const MTPDmessage &m) {
		PeerId peerId = peerFromMTP(m.vto_id);
		if (m.has_from_id() && peerToUser(peerId) == MTP::authedId()) {
			peerId = peerFromUser(m.vfrom_id);
		}
		if (HistoryItem *existing = App::histItemById(peerToChannel(peerId), m.vid.v)) {
			bool hasLinks = m.has_entities() && !m.ventities.c_vector().v.isEmpty();
			if ((hasLinks && !existing->hasTextLinks()) || (!hasLinks && existing->textHasLinks())) {
				existing->setText(qs(m.vmessage), m.has_entities() ? linksFromMTP(m.ventities.c_vector().v) : LinksInText());
				existing->initDimensions();
				if (App::main()) App::main()->itemResized(existing);
				if (existing->hasTextLinks() && (!existing->history()->isChannel() || existing->fromChannel())) {
					existing->history()->addToOverview(existing, OverviewLinks);
				}
			}

			existing->updateMedia(m.has_media() ? (&m.vmedia) : 0, true);

			existing->setViewsCount(m.has_views() ? m.vviews.v : -1);

			return !existing->detached();
		}
		return false;
	}

	void feedMsgs(const MTPVector<MTPMessage> &msgs, NewMessageType type) {
		const QVector<MTPMessage> &v(msgs.c_vector().v);
		QMap<uint64, int32> msgsIds;
		for (int32 i = 0, l = v.size(); i < l; ++i) {
			const MTPMessage &msg(v.at(i));
			switch (msg.type()) {
			case mtpc_message: {
				const MTPDmessage &d(msg.c_message());
				bool needToAdd = true;
				if (type == NewMessageUnread) { // new message, index my forwarded messages to links overview
					if (checkEntitiesAndViewsUpdate(d)) { // already in blocks
						LOG(("Skipping message, because it is already in blocks!"));
						needToAdd = false;
					}
				}
				if (needToAdd) {
					msgsIds.insert((uint64(uint32(d.vid.v)) << 32) | uint64(i), i);
				}
			} break;
			case mtpc_messageEmpty: msgsIds.insert((uint64(uint32(msg.c_messageEmpty().vid.v)) << 32) | uint64(i), i); break;
			case mtpc_messageService: msgsIds.insert((uint64(uint32(msg.c_messageService().vid.v)) << 32) | uint64(i), i); break;
			}
		}
		for (QMap<uint64, int32>::const_iterator i = msgsIds.cbegin(), e = msgsIds.cend(); i != e; ++i) {
			histories().addNewMessage(v.at(i.value()), type);
		}
	}

	ImagePtr image(const MTPPhotoSize &size) {
		switch (size.type()) {
		case mtpc_photoSize: {
			const MTPDphotoSize &d(size.c_photoSize());
			if (d.vlocation.type() == mtpc_fileLocation) {
				const MTPDfileLocation &l(d.vlocation.c_fileLocation());
				return ImagePtr(StorageImageLocation(d.vw.v, d.vh.v, l.vdc_id.v, l.vvolume_id.v, l.vlocal_id.v, l.vsecret.v), d.vsize.v);
			}
		} break;
		case mtpc_photoCachedSize: {
			const MTPDphotoCachedSize &d(size.c_photoCachedSize());
			if (d.vlocation.type() == mtpc_fileLocation) {
				const MTPDfileLocation &l(d.vlocation.c_fileLocation());
				const string &s(d.vbytes.c_string().v);
				QByteArray bytes(s.data(), s.size());
				return ImagePtr(StorageImageLocation(d.vw.v, d.vh.v, l.vdc_id.v, l.vvolume_id.v, l.vlocal_id.v, l.vsecret.v), bytes);
			} else if (d.vlocation.type() == mtpc_fileLocationUnavailable) {
				const string &s(d.vbytes.c_string().v);
				QByteArray bytes(s.data(), s.size());
				return ImagePtr(StorageImageLocation(d.vw.v, d.vh.v, 0, 0, 0, 0), bytes);
			}				
		} break;
		}
		return ImagePtr();
	}

	StorageImageLocation imageLocation(int32 w, int32 h, const MTPFileLocation &loc) {
		if (loc.type() == mtpc_fileLocation) {
			const MTPDfileLocation &l(loc.c_fileLocation());
			return StorageImageLocation(w, h, l.vdc_id.v, l.vvolume_id.v, l.vlocal_id.v, l.vsecret.v);
		}
		return StorageImageLocation(w, h, 0, 0, 0, 0);
	}

	StorageImageLocation imageLocation(const MTPPhotoSize &size) {
		switch (size.type()) {
		case mtpc_photoSize: {
			const MTPDphotoSize &d(size.c_photoSize());
			return imageLocation(d.vw.v, d.vh.v, d.vlocation);
		} break;
		case mtpc_photoCachedSize: {
			const MTPDphotoCachedSize &d(size.c_photoCachedSize());
			return imageLocation(d.vw.v, d.vh.v, d.vlocation);
		} break;
		}
		return StorageImageLocation();
	}
	
	void feedInboxRead(const PeerId &peer, MsgId upTo) {
		History *h = App::historyLoaded(peer);
		if (h) {
			h->inboxRead(upTo);
		}
	}

	void feedOutboxRead(const PeerId &peer, MsgId upTo) {
		History *h = App::historyLoaded(peer);
		if (h) {
			h->outboxRead(upTo);
			if (h->peer->isUser()) {
				h->peer->asUser()->madeAction();
			}
		}
	}

	inline MsgsData *fetchMsgsData(ChannelId channelId, bool insert = true) {
		if (channelId == NoChannel) return &msgsData;
		ChannelMsgsData::iterator i = channelMsgsData.find(channelId);
		if (i == channelMsgsData.cend()) {
			if (insert) {
				i = channelMsgsData.insert(channelId, MsgsData());
			} else {
				return 0;
			}
		}
		return &(*i);
	}

	void feedWereDeleted(ChannelId channelId, const QVector<MTPint> &msgsIds) {
		bool resized = false;
		MsgsData *data = fetchMsgsData(channelId, false);
		if (!data) return;

		ChannelHistory *channelHistory = (channelId == NoChannel) ? 0 : App::historyLoaded(peerFromChannel(channelId))->asChannelHistory();

		QMap<History*, bool> historiesToCheck;
		for (QVector<MTPint>::const_iterator i = msgsIds.cbegin(), e = msgsIds.cend(); i != e; ++i) {
			MsgsData::const_iterator j = data->constFind(i->v);
			if (j != data->cend()) {
				History *h = (*j)->history();
				if (App::main() && h->peer == App::main()->peer() && !(*j)->detached()) {
					resized = true;
				}
				(*j)->destroy();
				if (!h->lastMsg) historiesToCheck.insert(h, true);
			} else if (channelHistory) {
				channelHistory->messageWithIdDeleted(i->v);
			}
		}
		if (resized) {
			App::main()->itemResized(0);
		}
		if (main()) {
			for (QMap<History*, bool>::const_iterator i = historiesToCheck.cbegin(), e = historiesToCheck.cend(); i != e; ++i) {
				main()->checkPeerHistory(i.key()->peer);
			}
		}
	}

	void feedUserLinks(const MTPVector<MTPcontacts_Link> &links, bool emitPeerUpdated) {
		const QVector<MTPcontacts_Link> &v(links.c_vector().v);
		for (QVector<MTPcontacts_Link>::const_iterator i = v.cbegin(), e = v.cend(); i != e; ++i) {
			const MTPDcontacts_link &dv(i->c_contacts_link());
			UserData *user = feedUsers(MTP_vector<MTPUser>(1, dv.vuser), false);
			MTPint userId(MTP_int(0));
			switch (dv.vuser.type()) {
			case mtpc_userEmpty: userId = dv.vuser.c_userEmpty().vid; break;
			case mtpc_user: userId = dv.vuser.c_user().vid; break;
			}
			if (userId.v) {
				feedUserLink(userId, dv.vmy_link, dv.vforeign_link, false);
			}
			if (user && App::main()) {
				if (emitPeerUpdated) {
					App::main()->peerUpdated(user);
				} else {
					markPeerUpdated(user);
				}
			}
		}
	}

	void feedUserLink(MTPint userId, const MTPContactLink &myLink, const MTPContactLink &foreignLink, bool emitPeerUpdated) {
		UserData *user = userLoaded(userId.v);
		if (user) {
			bool wasContact = (user->contact > 0);
			bool wasShowPhone = !user->contact;
			switch (myLink.type()) {
			case mtpc_contactLinkContact:
				user->contact = 1;
				if (user->contact == 1 && cReportSpamStatuses().value(user->id, dbiprsNoButton) != dbiprsNoButton) {
					cRefReportSpamStatuses().insert(user->id, dbiprsNoButton);
					Local::writeReportSpamStatuses();
				}
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
					App::main()->addNewContact(peerToUser(user->id), false);
				}
			} else {
				if (user->contact < 0 && !user->phone.isEmpty() && peerToUser(user->id) != MTP::authedId()) {
					user->contact = 0;
				}
				if (wasContact) {
					App::main()->removeContact(user);
				}
			}

			bool showPhone = !isServiceUser(user->id) && (user->input.type() != mtpc_inputPeerSelf) && !user->contact;
			bool showPhoneChanged = !isServiceUser(user->id) && (user->input.type() != mtpc_inputPeerSelf) && ((showPhone && !wasShowPhone) || (!showPhone && wasShowPhone));
			if (showPhoneChanged) {
				user->setName(textOneLine(user->firstName), textOneLine(user->lastName), showPhone ? App::formatPhone(user->phone) : QString(), textOneLine(user->username));
			}
			if (App::main()) {
				if (emitPeerUpdated) {
					App::main()->peerUpdated(user);
				} else {
					markPeerUpdated(user);
				}
			}
		}
	}

	void markPeerUpdated(PeerData *data) {
		updatedPeers.insert(data, true);
	}

	void clearPeerUpdated(PeerData *data) {
		updatedPeers.remove(data);
	}

	void emitPeerUpdated() {
		if (!updatedPeers.isEmpty() && App::main()) {
			UpdatedPeers upd = updatedPeers;
			updatedPeers.clear();

			for (UpdatedPeers::const_iterator i = upd.cbegin(), e = upd.cend(); i != e; ++i) {
				App::main()->peerUpdated(i.key());
			}
		}
	}

	PhotoData *feedPhoto(const MTPPhoto &photo, PhotoData *convert) {
		switch (photo.type()) {
		case mtpc_photo: {
			return feedPhoto(photo.c_photo(), convert);
		} break;
		case mtpc_photoEmpty: {
			return App::photoSet(photo.c_photoEmpty().vid.v, convert, 0, 0, ImagePtr(), ImagePtr(), ImagePtr());
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
			return App::photoSet(ph.vid.v, 0, ph.vaccess_hash.v, ph.vdate.v, ImagePtr(*thumb, "JPG"), ImagePtr(*medium, "JPG"), ImagePtr(*full, "JPG"));
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
			return App::photoSet(photo.vid.v, convert, photo.vaccess_hash.v, photo.vdate.v, App::image(*thumb), App::image(*medium), App::image(*full));
		}
		return App::photoSet(photo.vid.v, convert, 0, 0, ImagePtr(), ImagePtr(), ImagePtr());
	}
	
	VideoData *feedVideo(const MTPDvideo &video, VideoData *convert) {
		return App::videoSet(video.vid.v, convert, video.vaccess_hash.v, video.vdate.v, video.vduration.v, video.vw.v, video.vh.v, App::image(video.vthumb), video.vdc_id.v, video.vsize.v);
	}

	AudioData *feedAudio(const MTPaudio &audio, AudioData *convert) {
		switch (audio.type()) {
		case mtpc_audio: {
			return feedAudio(audio.c_audio(), convert);
		} break;
		case mtpc_audioEmpty: {
			return App::audioSet(audio.c_audioEmpty().vid.v, convert, 0, 0, QString(), 0, 0, 0);
		} break;
		}
		return App::audio(0);
	}

	AudioData *feedAudio(const MTPDaudio &audio, AudioData *convert) {
		return App::audioSet(audio.vid.v, convert, audio.vaccess_hash.v, audio.vdate.v, qs(audio.vmime_type), audio.vduration.v, audio.vdc_id.v, audio.vsize.v);
	}

	DocumentData *feedDocument(const MTPdocument &document, const QPixmap &thumb) {
		switch (document.type()) {
		case mtpc_document: {
			const MTPDdocument &d(document.c_document());
			return App::documentSet(d.vid.v, 0, d.vaccess_hash.v, d.vdate.v, d.vattributes.c_vector().v, qs(d.vmime_type), ImagePtr(thumb, "JPG"), d.vdc_id.v, d.vsize.v, StorageImageLocation());
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
			return App::documentSet(document.c_documentEmpty().vid.v, convert, 0, 0, QVector<MTPDocumentAttribute>(), QString(), ImagePtr(), 0, 0, StorageImageLocation());
		} break;
		}
		return App::document(0);
	}

	DocumentData *feedDocument(const MTPDdocument &document, DocumentData *convert) {
		return App::documentSet(document.vid.v, convert, document.vaccess_hash.v, document.vdate.v, document.vattributes.c_vector().v, qs(document.vmime_type), App::image(document.vthumb), document.vdc_id.v, document.vsize.v, App::imageLocation(document.vthumb));
	}

	WebPageData *feedWebPage(const MTPDwebPage &webpage, WebPageData *convert) {
		return App::webPageSet(webpage.vid.v, convert, webpage.has_type() ? qs(webpage.vtype) : qsl("article"), qs(webpage.vurl), qs(webpage.vdisplay_url), webpage.has_site_name() ? qs(webpage.vsite_name) : QString(), webpage.has_title() ? qs(webpage.vtitle) : QString(), webpage.has_description() ? qs(webpage.vdescription) : QString(), webpage.has_photo() ? App::feedPhoto(webpage.vphoto) : 0, webpage.has_document() ? App::feedDocument(webpage.vdocument) : 0, webpage.has_duration() ? webpage.vduration.v : 0, webpage.has_author() ? qs(webpage.vauthor) : QString(), 0);
	}

	WebPageData *feedWebPage(const MTPDwebPagePending &webpage, WebPageData *convert) {
		return App::webPageSet(webpage.vid.v, convert, QString(), QString(), QString(), QString(), QString(), QString(), 0, 0, 0, QString(), webpage.vdate.v);
	}

	WebPageData *feedWebPage(const MTPWebPage &webpage) {
		switch (webpage.type()) {
		case mtpc_webPage: return App::feedWebPage(webpage.c_webPage());
		case mtpc_webPageEmpty: {
			WebPageData *page = App::webPage(webpage.c_webPageEmpty().vid.v);
			if (page->pendingTill > 0) page->pendingTill = -1; // failed
			return page;
		} break;
		case mtpc_webPagePending: return App::feedWebPage(webpage.c_webPagePending());
		}
		return 0;
	}

	PeerData *peerLoaded(const PeerId &peer) {
		PeersData::const_iterator i = peersData.constFind(peer);
		return (i != peersData.cend()) ? i.value() : 0;
	}

	UserData *userLoaded(const PeerId &id) {
		PeerData *peer = peerLoaded(id);
		return (peer && peer->loaded) ? peer->asUser() : 0;
	}
	ChatData *chatLoaded(const PeerId &id) {
		PeerData *peer = peerLoaded(id);
		return (peer && peer->loaded) ? peer->asChat() : 0;
	}
	ChannelData *channelLoaded(const PeerId &id) {
		PeerData *peer = peerLoaded(id);
		return (peer && peer->loaded) ? peer->asChannel() : 0;
	}
	UserData *userLoaded(int32 user_id) {
		return userLoaded(peerFromUser(user_id));
	}
	ChatData *chatLoaded(int32 chat_id) {
		return chatLoaded(peerFromChat(chat_id));
	}
	ChannelData *channelLoaded(int32 channel_id) {
		return channelLoaded(peerFromChannel(channel_id));
	}

	UserData *curUser() {
		return user(MTP::authedId());
	}

	PeerData *peer(const PeerId &id) {
		PeersData::const_iterator i = peersData.constFind(id);
		if (i == peersData.cend()) {
			PeerData *newData = 0;
			if (peerIsUser(id)) {
				newData = new UserData(id);
			} else if (peerIsChat(id)) {
				newData = new ChatData(id);
			} else if (peerIsChannel(id)) {
				newData = new ChannelData(id);
			}
			if (!newData) return 0;

			newData->input = MTPinputPeer(MTP_inputPeerEmpty());
			i = peersData.insert(id, newData);
		}
		return i.value();
	}

	UserData *user(const PeerId &id) {
		return peer(id)->asUser();
	}
	ChatData *chat(const PeerId &id) {
		return peer(id)->asChat();
	}
	ChannelData *channel(const PeerId &id) {
		return peer(id)->asChannel();
	}
	UserData *user(int32 user_id) {
		return user(peerFromUser(user_id));
	}
	ChatData *chat(int32 chat_id) {
		return chat(peerFromChat(chat_id));
	}
	ChannelData *channel(int32 channel_id) {
		return channel(peerFromChannel(channel_id));
	}

	UserData *self() {
		return ::self;
	}

	PeerData *peerByName(const QString &username) {
		for (PeersData::const_iterator i = peersData.cbegin(), e = peersData.cend(); i != e; ++i) {
			if (!i.value()->userName().compare(username.trimmed(), Qt::CaseInsensitive)) {
				return i.value()->asUser();
			}
		}
		return 0;
	}

	PhotoData *photo(const PhotoId &photo) {
		PhotosData::const_iterator i = photosData.constFind(photo);
		if (i == photosData.cend()) {
			i = photosData.insert(photo, new PhotoData(photo));
		}
		return i.value();
	}

	PhotoData *photoSet(const PhotoId &photo, PhotoData *convert, const uint64 &access, int32 date, const ImagePtr &thumb, const ImagePtr &medium, const ImagePtr &full) {
		if (convert) {
			if (convert->id != photo) {
				PhotosData::iterator i = photosData.find(convert->id);
				if (i != photosData.cend() && i.value() == convert) {
					photosData.erase(i);
				}
				convert->id = photo;
			}
			convert->access = access;
			if (!convert->date && date) {
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
				result = new PhotoData(photo, access, date, thumb, medium, full);
			}
			photosData.insert(photo, result);
		} else {
			result = i.value();
			if (result != convert && !result->date && date) {
				result->access = access;
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

	VideoData *video(const VideoId &video) {
		VideosData::const_iterator i = videosData.constFind(video);
		if (i == videosData.cend()) {
			i = videosData.insert(video, new VideoData(video));
		}
		return i.value();
	}

	VideoData *videoSet(const VideoId &video, VideoData *convert, const uint64 &access, int32 date, int32 duration, int32 w, int32 h, const ImagePtr &thumb, int32 dc, int32 size) {
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
			if (!convert->date && date) {
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
				result = new VideoData(video, access, date, duration, w, h, thumb, dc, size);
			}
			videosData.insert(video, result);
		} else {
			result = i.value();
			if (result != convert && !result->date && date) {
				result->access = access;
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

	AudioData *audio(const AudioId &audio) {
		AudiosData::const_iterator i = audiosData.constFind(audio);
		if (i == audiosData.cend()) {
			i = audiosData.insert(audio, new AudioData(audio));
		}
		return i.value();
	}

	AudioData *audioSet(const AudioId &audio, AudioData *convert, const uint64 &access, int32 date, const QString &mime, int32 duration, int32 dc, int32 size) {
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
			if (!convert->date && date) {
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
				result = new AudioData(audio, access, date, mime, duration, dc, size);
			}
			audiosData.insert(audio, result);
		} else {
			result = i.value();
			if (result != convert && !result->date && date) {
				result->access = access;
				result->date = date;
				result->mime = mime;
				result->duration = duration;
				result->dc = dc;
				result->size = size;
			}
		}
		return result;
	}

	DocumentData *document(const DocumentId &document) {
		DocumentsData::const_iterator i = documentsData.constFind(document);
		if (i == documentsData.cend()) {
			i = documentsData.insert(document, new DocumentData(document));
		}
		return i.value();
	}

	DocumentData *documentSet(const DocumentId &document, DocumentData *convert, const uint64 &access, int32 date, const QVector<MTPDocumentAttribute> &attributes, const QString &mime, const ImagePtr &thumb, int32 dc, int32 size, const StorageImageLocation &thumbLocation) {
		bool sentSticker = false;
		if (convert) {
			if (convert->id != document) {
				DocumentsData::iterator i = documentsData.find(convert->id);
				if (i != documentsData.cend() && i.value() == convert) {
					documentsData.erase(i);
				}
				convert->id = document;
				convert->status = FileReady;
				sentSticker = !!convert->sticker();
			}
			convert->access = access;
			if (!convert->date && date) {
				convert->date = date;
				convert->setattributes(attributes);
				convert->mime = mime;
				convert->thumb = thumb;
				convert->dc = dc;
				convert->size = size;
			} else {
				if (!thumb->isNull() && (convert->thumb->isNull() || convert->thumb->width() < thumb->width() || convert->thumb->height() < thumb->height())) {
					convert->thumb = thumb;
				}
				if (convert->sticker() && !attributes.isEmpty() && (convert->sticker()->alt.isEmpty() || convert->sticker()->set.type() == mtpc_inputStickerSetEmpty)) {
					for (QVector<MTPDocumentAttribute>::const_iterator i = attributes.cbegin(), e = attributes.cend(); i != e; ++i) {
						if (i->type() == mtpc_documentAttributeSticker) {
							const MTPDdocumentAttributeSticker &d(i->c_documentAttributeSticker());
							if (d.valt.c_string().v.length() > 0) {
								convert->sticker()->alt = qs(d.valt);
								convert->sticker()->set = d.vstickerset;
							}
						}
					}
				}
			}
			if (convert->sticker() && !convert->sticker()->loc.dc && thumbLocation.dc) {
				convert->sticker()->loc = thumbLocation;
			}

			if (convert->location.check()) {
				Local::writeFileLocation(mediaKey(DocumentFileLocation, convert->dc, convert->id), convert->location);
			}
		}
		DocumentsData::const_iterator i = documentsData.constFind(document);
		DocumentData *result;
		if (i == documentsData.cend()) {
			if (convert) {
				result = convert;
			} else {
				result = new DocumentData(document, access, date, attributes, mime, thumb, dc, size);
				if (result->sticker()) result->sticker()->loc = thumbLocation;
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
					if (!thumb->isNull() && (result->thumb->isNull() || result->thumb->width() < thumb->width() || result->thumb->height() < thumb->height())) {
						result->thumb = thumb;
					}
					if (result->sticker() && !attributes.isEmpty() && (result->sticker()->alt.isEmpty() || result->sticker()->set.type() == mtpc_inputStickerSetEmpty)) {
						for (QVector<MTPDocumentAttribute>::const_iterator i = attributes.cbegin(), e = attributes.cend(); i != e; ++i) {
							if (i->type() == mtpc_documentAttributeSticker) {
								const MTPDdocumentAttributeSticker &d(i->c_documentAttributeSticker());
								if (d.valt.c_string().v.length() > 0) {
									result->sticker()->alt = qs(d.valt);
									result->sticker()->set = d.vstickerset;
								}
							}
						}
					}
					if (result->sticker() && !result->sticker()->loc.dc && thumbLocation.dc) {
						result->sticker()->loc = thumbLocation;
					}
				}
			}
		}
		if (sentSticker && App::main()) App::main()->incrementSticker(result);
		return result;
	}

	WebPageData *webPage(const WebPageId &webPage) {
		WebPagesData::const_iterator i = webPagesData.constFind(webPage);
		if (i == webPagesData.cend()) {
			i = webPagesData.insert(webPage, new WebPageData(webPage));
		}
		return i.value();
	}

	WebPageData *webPageSet(const WebPageId &webPage, WebPageData *convert, const QString &type, const QString &url, const QString &displayUrl, const QString &siteName, const QString &title, const QString &description, PhotoData *photo, DocumentData *doc, int32 duration, const QString &author, int32 pendingTill) {
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
				convert->doc = doc;
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
				result = new WebPageData(webPage, toWebPageType(type), url, displayUrl, siteName, title, description, photo, doc, duration, author, (pendingTill >= -1) ? pendingTill : -1);
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
					result->doc = doc;
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
	
	ImageLinkData *imageLink(const QString &imageLink) {
		ImageLinksData::const_iterator i = imageLinksData.constFind(imageLink);
		if (i == imageLinksData.cend()) {
			i = imageLinksData.insert(imageLink, new ImageLinkData(imageLink));
		}
		return i.value();
	}
		
	ImageLinkData *imageLinkSet(const QString &imageLink, ImageLinkType type, const QString &url) {
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

			return MTP_photo(uphoto.vphoto_id, MTP_long(0), date, MTP_vector<MTPPhotoSize>(photoSizes));
		}
		return MTP_photoEmpty(MTP_long(0));
	}

	QString peerName(const PeerData *peer, bool forDialogs) {
		return peer ? ((forDialogs && peer->isUser() && !peer->asUser()->nameOrPhone.isEmpty()) ? peer->asUser()->nameOrPhone : peer->name) : lang(lng_deleted);
	}

	Histories &histories() {
		return ::histories;
	}

	History *history(const PeerId &peer) {
		return ::histories.findOrInsert(peer, 0, 0);
	}

	History *historyFromDialog(const PeerId &peer, int32 unreadCnt, int32 maxInboxRead) {
		return ::histories.findOrInsert(peer, unreadCnt, maxInboxRead);
	}
	
	History *historyLoaded(const PeerId &peer) {
		return ::histories.find(peer);
	}

	HistoryItem *histItemById(ChannelId channelId, MsgId itemId) {
		MsgsData *data = fetchMsgsData(channelId, false);
		if (!data) return 0;

		MsgsData::const_iterator i = data->constFind(itemId);
		if (i != data->cend()) {
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
		MsgsData *data = fetchMsgsData(item->channelId());
		MsgsData::const_iterator i = data->constFind(item->id);
		if (i == data->cend()) {
			data->insert(item->id, item);
			return 0;
		}
		if (i.value() != item && !i.value()->block() && item->block()) { // replace search item
			itemReplaced(i.value(), item);
			delete i.value();
			data->insert(item->id, item);
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
		MsgsData *data = fetchMsgsData(item->channelId(), false);
		if (!data) return;

		MsgsData::iterator i = data->find(item->id);
		if (i != data->cend()) {
			if (i.value() == item) {
				data->erase(i);
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
		for (ChannelMsgsData::const_iterator j = channelMsgsData.cbegin(), end = channelMsgsData.cend(); j != end; ++j) {
			for (MsgsData::const_iterator i = j->cbegin(), e = j->cend(); i != e; ++i) {
				if ((*i)->detached()) {
					toDelete.push_back(*i);
				}
			}
		}
		msgsData.clear();
		channelMsgsData.clear();
		for (int32 i = 0, l = toDelete.size(); i < l; ++i) {
			delete toDelete[i];
		}

		::hoveredItem = ::pressedItem = ::hoveredLinkItem = ::pressedLinkItem = ::contextItem = 0;
		replyMarkups.clear();
		channelReplyMarkups.clear();
	}

	void historyClearItems() {
		historyClearMsgs();
		randomData.clear();
		sentData.clear();
		mutedPeers.clear();
		updatedPeers.clear();
		cSetSavedPeers(SavedPeers());
		cSetSavedPeersByTime(SavedPeersByTime());
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
		cSetStickerSets(StickerSets());
		cSetStickerSetsOrder(StickerSetsOrder());
		cSetLastStickersUpdate(0);
		cSetReportSpamStatuses(ReportSpamStatuses());
		::videoItems.clear();
		::audioItems.clear();
		::documentItems.clear();
		::webPageItems.clear();
		::sharedContactPhones.clear();
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

	void historyRegRandom(uint64 randomId, const FullMsgId &itemId) {
		randomData.insert(randomId, itemId);
	}

	void historyUnregRandom(uint64 randomId) {
		randomData.remove(randomId);
	}

	FullMsgId histItemByRandom(uint64 randomId) {
		RandomData::const_iterator i = randomData.constFind(randomId);
		if (i != randomData.cend()) {
			return i.value();
		}
		return FullMsgId();
	}

	void historyRegSentData(uint64 randomId, const PeerId &peerId, const QString &text) {
		sentData.insert(randomId, qMakePair(peerId, text));
	}

	void historyUnregSentData(uint64 randomId) {
		sentData.remove(randomId);
	}

	void histSentDataByItem(uint64 randomId, PeerId &peerId, QString &text) {
		QPair<PeerId, QString> d = sentData.value(randomId);
		peerId = d.first;
		text = d.second;
	}

	void prepareCorners(RoundCorners index, int32 radius, const style::color &color, const style::color *shadow = 0, QImage *cors = 0) {
		int32 r = radius * cIntRetinaFactor(), s = st::msgShadow * cIntRetinaFactor();
		QImage rect(r * 3, r * 3 + (shadow ? s : 0), QImage::Format_ARGB32_Premultiplied), localCors[4];
		{
			QPainter p(&rect);
			p.setCompositionMode(QPainter::CompositionMode_Source);
			p.fillRect(QRect(0, 0, rect.width(), rect.height()), st::transparent->b);
			p.setCompositionMode(QPainter::CompositionMode_SourceOver);
			p.setRenderHint(QPainter::HighQualityAntialiasing);
			p.setPen(Qt::NoPen);
			if (shadow) {
				p.setBrush((*shadow)->b);
				p.drawRoundedRect(0, s, r * 3, r * 3, r, r);
			}
			p.setBrush(color->b);
			p.drawRoundedRect(0, 0, r * 3, r * 3, r, r);
		}
		if (!cors) cors = localCors;
		cors[0] = rect.copy(0, 0, r, r);
		cors[1] = rect.copy(r * 2, 0, r, r);
		cors[2] = rect.copy(0, r * 2, r, r + (shadow ? s : 0));
		cors[3] = rect.copy(r * 2, r * 2, r, r + (shadow ? s : 0));
		if (index != NoneCorners) {
			for (int i = 0; i < 4; ++i) {
				::corners[index].p[i] = new QPixmap(QPixmap::fromImage(cors[i], Qt::ColorOnly));
				::corners[index].p[i]->setDevicePixelRatio(cRetinaFactor());
			}
		}
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
		emojiInit();
		if (!::emoji) {
			::emoji = new QPixmap(QLatin1String(EName));
            if (cRetina()) ::emoji->setDevicePixelRatio(cRetinaFactor());
		}
		if (!::emojiLarge) {
			::emojiLarge = new QPixmap(QLatin1String(EmojiNames[EIndex + 1]));
			if (cRetina()) ::emojiLarge->setDevicePixelRatio(cRetinaFactor());
		}

		QImage mask[4];
		prepareCorners(NoneCorners, st::msgRadius, st::white, 0, mask);
		for (int i = 0; i < 4; ++i) {
			::cornersMask[i] = new QImage(mask[i].convertToFormat(QImage::Format_ARGB32_Premultiplied));
			::cornersMask[i]->setDevicePixelRatio(cRetinaFactor());
		}
		prepareCorners(BlackCorners, st::msgRadius, st::black);
		prepareCorners(ServiceCorners, st::msgRadius, st::msgServiceBg);
		prepareCorners(ServiceSelectedCorners, st::msgRadius, st::msgServiceSelectBg);
		prepareCorners(SelectedOverlayCorners, st::msgRadius, st::msgSelectOverlay);
		prepareCorners(DateCorners, st::msgRadius, st::msgDateImgBg);
		prepareCorners(DateSelectedCorners, st::msgRadius, st::msgDateImgSelectBg);
		prepareCorners(InShadowCorners, st::msgRadius, st::msgInShadow);
		prepareCorners(InSelectedShadowCorners, st::msgRadius, st::msgInSelectShadow);
		prepareCorners(ForwardCorners, st::msgRadius, st::forwardBg);
		prepareCorners(MediaviewSaveCorners, st::msgRadius, st::medviewSaveMsg);
		prepareCorners(EmojiHoverCorners, st::msgRadius, st::emojiPanHover);
		prepareCorners(StickerHoverCorners, st::msgRadius, st::emojiPanHover);
		prepareCorners(BotKeyboardCorners, st::msgRadius, st::botKbBg);
		prepareCorners(BotKeyboardOverCorners, st::msgRadius, st::botKbOverBg);
		prepareCorners(BotKeyboardDownCorners, st::msgRadius, st::botKbDownBg);
		prepareCorners(PhotoSelectOverlayCorners, st::msgRadius, st::overviewPhotoSelectOverlay);

		prepareCorners(DocRedCorners, st::msgRadius, st::mvDocRedColor);
		prepareCorners(DocYellowCorners, st::msgRadius, st::mvDocYellowColor);
		prepareCorners(DocGreenCorners, st::msgRadius, st::mvDocGreenColor);
		prepareCorners(DocBlueCorners, st::msgRadius, st::mvDocBlueColor);

		prepareCorners(MessageInCorners, st::msgRadius, st::msgInBg, &st::msgInShadow);
		prepareCorners(MessageInSelectedCorners, st::msgRadius, st::msgInSelectBg, &st::msgInSelectShadow);
		prepareCorners(MessageOutCorners, st::msgRadius, st::msgOutBg, &st::msgOutShadow);
		prepareCorners(MessageOutSelectedCorners, st::msgRadius, st::msgOutSelectBg, &st::msgOutSelectShadow);
		prepareCorners(ButtonHoverCorners, st::msgRadius, st::mediaSaveButton.overBgColor, &st::msgInShadow);

	}
	
	void deinitMedia(bool completely) {
		textlnkOver(TextLinkPtr());
		textlnkDown(TextLinkPtr());

		histories().clear();

		if (completely) {
			audioFinish();

			delete ::sprite;
			::sprite = 0;
			delete ::emoji;
			::emoji = 0;
			delete ::emojiLarge;
			::emojiLarge = 0;
			for (int32 j = 0; j < 4; ++j) {
				for (int32 i = 0; i < RoundCornersCount; ++i) {
					delete ::corners[i].p[j]; ::corners[i].p[j] = 0;
				}
				delete ::cornersMask[j]; ::cornersMask[j] = 0;
			}
			for (CornersMap::const_iterator i = ::cornersMap.cbegin(), e = ::cornersMap.cend(); i != e; ++i) {
				for (int32 j = 0; j < 4; ++j) {
					delete i->p[j];
				}
			}
			::cornersMap.clear();
			mainEmojiMap.clear();
			otherEmojiMap.clear();

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

	const QPixmap &emoji() {
		return *::emoji;
	}

	const QPixmap &emojiLarge() {
		return *::emojiLarge;
	}

	const QPixmap &emojiSingle(EmojiPtr emoji, int32 fontHeight) {
		EmojiMap *map = &(fontHeight == st::taDefFlat.font->height ? mainEmojiMap : otherEmojiMap[fontHeight]);
		EmojiMap::const_iterator i = map->constFind(emojiKey(emoji));
		if (i == map->cend()) {
			QImage img(ESize + st::emojiPadding * cIntRetinaFactor() * 2, fontHeight * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
            if (cRetina()) img.setDevicePixelRatio(cRetinaFactor());
			{
				QPainter p(&img);
				QPainter::CompositionMode m = p.compositionMode();
				p.setCompositionMode(QPainter::CompositionMode_Source);
				p.fillRect(0, 0, img.width(), img.height(), Qt::transparent);
				p.setCompositionMode(m);
				emojiDraw(p, emoji, st::emojiPadding * cIntRetinaFactor(), (fontHeight * cIntRetinaFactor() - ESize) / 2);
			}
			i = map->insert(emojiKey(emoji), QPixmap::fromImage(img, Qt::ColorOnly));
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

	void regSharedContactPhone(int32 userId, const QString &phone) {
		::sharedContactPhones[userId] = phone;
	}

	QString phoneFromSharedContact(int32 userId) {
		return ::sharedContactPhones.value(userId);
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

	inline void insertReplyMarkup(ChannelId channelId, MsgId msgId, const ReplyMarkup &markup) {
		if (channelId == NoChannel) {
			replyMarkups.insert(msgId, markup);
		} else {
			channelReplyMarkups[channelId].insert(msgId, markup);
		}
	}

	void feedReplyMarkup(ChannelId channelId, MsgId msgId, const MTPReplyMarkup &markup) {
		ReplyMarkup data;
		ReplyMarkup::Commands &commands(data.commands);
		switch (markup.type()) {
		case mtpc_replyKeyboardMarkup: {
			const MTPDreplyKeyboardMarkup &d(markup.c_replyKeyboardMarkup());
			data.flags = d.vflags.v;
			
			const QVector<MTPKeyboardButtonRow> &v(d.vrows.c_vector().v);
			if (!v.isEmpty()) {
				commands.reserve(v.size());
				for (int32 i = 0, l = v.size(); i < l; ++i) {
					switch (v.at(i).type()) {
					case mtpc_keyboardButtonRow: {
						const MTPDkeyboardButtonRow &r(v.at(i).c_keyboardButtonRow());
						const QVector<MTPKeyboardButton> &b(r.vbuttons.c_vector().v);
						if (!b.isEmpty()) {
							QList<QString> btns;
							btns.reserve(b.size());
							for (int32 j = 0, s = b.size(); j < s; ++j) {
								switch (b.at(j).type()) {
								case mtpc_keyboardButton: {
									btns.push_back(qs(b.at(j).c_keyboardButton().vtext));
								} break;
								}
							}
							if (!btns.isEmpty()) commands.push_back(btns);
						}
					} break;
					}
				}
				if (!commands.isEmpty()) {
					insertReplyMarkup(channelId, msgId, data);
				}
			}
		} break;

		case mtpc_replyKeyboardHide: {
			const MTPDreplyKeyboardHide &d(markup.c_replyKeyboardHide());
			if (d.vflags.v) {
				insertReplyMarkup(channelId, msgId, ReplyMarkup(d.vflags.v | MTPDreplyKeyboardMarkup_flag_ZERO));
			}
		} break;

		case mtpc_replyKeyboardForceReply: {
			const MTPDreplyKeyboardForceReply &d(markup.c_replyKeyboardForceReply());
			insertReplyMarkup(channelId, msgId, ReplyMarkup(d.vflags.v | MTPDreplyKeyboardMarkup_flag_FORCE_REPLY));
		} break;
		}
	}

	void clearReplyMarkup(ChannelId channelId, MsgId msgId) {
		if (channelId == NoChannel) {
			replyMarkups.remove(msgId);
		} else {
			ChannelReplyMarkups::iterator i = channelReplyMarkups.find(channelId);
			if (i != channelReplyMarkups.cend()) {
				i->remove(msgId);
			}
			if (i->isEmpty()) {
				channelReplyMarkups.erase(i);
			}
		}
	}

	inline const ReplyMarkup &replyMarkup(const ReplyMarkups &markups, MsgId msgId) {
		ReplyMarkups::const_iterator i = replyMarkups.constFind(msgId);
		if (i == replyMarkups.cend()) return zeroMarkup;
		return i.value();
	}
	const ReplyMarkup &replyMarkup(ChannelId channelId, MsgId msgId) {
		if (channelId == NoChannel) {
			return replyMarkup(replyMarkups, msgId);
		}
		ChannelReplyMarkups::const_iterator j = channelReplyMarkups.constFind(channelId);
		if (j == channelReplyMarkups.cend()) return zeroMarkup;
		return replyMarkup(*j, msgId);
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

	void sendBotCommand(const QString &cmd, MsgId replyTo) {
		if (App::main()) {
			App::main()->sendBotCommand(cmd, replyTo);
		}
	}

	void insertBotCommand(const QString &cmd) {
		if (App::main()) {
			App::main()->insertBotCommand(cmd);
		}
	}

	void searchByHashtag(const QString &tag, PeerData *inPeer) {
		if (App::main()) {
			App::main()->searchMessages(tag + ' ', (inPeer && inPeer->isChannel()) ? inPeer : 0);
		}
	}

	void openPeerByName(const QString &username, bool toProfile, const QString &startToken) {
		if (App::main()) {
			App::main()->openPeerByName(username, toProfile, startToken);
		}
	}

	void joinGroupByHash(const QString &hash) {
		if (App::main()) {
			App::main()->joinGroupByHash(hash);
		}
	}

	void stickersBox(const QString &name) {
		if (App::main()) {
			App::main()->stickersBox(MTP_inputStickerSetShortName(MTP_string(name)));
		}
	}

	void openLocalUrl(const QString &url) {
		if (App::main()) {
			App::main()->openLocalUrl(url);
		}
	}

	QImage **cornersMask() {
		return ::cornersMask;
	}
	void roundRect(Painter &p, int32 x, int32 y, int32 w, int32 h, const style::color &bg, const CornersPixmaps &c, const style::color *sh) {
		int32 cw = c.p[0]->width() / cIntRetinaFactor(), ch = c.p[0]->height() / cIntRetinaFactor();
		if (w < 2 * cw || h < 2 * ch) return;
		if (w > 2 * cw) {
			p.fillRect(QRect(x + cw, y, w - 2 * cw, ch), bg->b);
			p.fillRect(QRect(x + cw, y + h - ch, w - 2 * cw, ch), bg->b);
			if (sh) p.fillRect(QRect(x + cw, y + h, w - 2 * cw, st::msgShadow), (*sh)->b);
		}
		if (h > 2 * ch) {
			p.fillRect(QRect(x, y + ch, w, h - 2 * ch), bg->b);
		}
		p.drawPixmap(QPoint(x, y), *c.p[0]);
		p.drawPixmap(QPoint(x + w - cw, y), *c.p[1]);
		p.drawPixmap(QPoint(x, y + h - ch), *c.p[2]);
		p.drawPixmap(QPoint(x + w - cw, y + h - ch), *c.p[3]);
	}

	void roundRect(Painter &p, int32 x, int32 y, int32 w, int32 h, const style::color &bg, RoundCorners index, const style::color *sh) {
		roundRect(p, x, y, w, h, bg, ::corners[index], sh);
	}

	void roundShadow(Painter &p, int32 x, int32 y, int32 w, int32 h, const style::color &sh, RoundCorners index) {
		const CornersPixmaps &c = ::corners[index];
		int32 cw = c.p[0]->width() / cIntRetinaFactor(), ch = c.p[0]->height() / cIntRetinaFactor();
		p.fillRect(x + cw, y + h, w - 2 * cw, st::msgShadow, sh->b);
		p.fillRect(x, y + h - ch, cw, st::msgShadow, sh->b);
		p.fillRect(x + w - cw, y + h - ch, cw, st::msgShadow, sh->b);
		p.drawPixmap(x, y + h - ch + st::msgShadow, *c.p[2]);
		p.drawPixmap(x + w - cw, y + h - ch + st::msgShadow, *c.p[3]);
	}

	void roundRect(Painter &p, int32 x, int32 y, int32 w, int32 h, const style::color &bg) {
		uint32 colorKey = ((uint32(bg->c.alpha()) & 0xFF) << 24) | ((uint32(bg->c.red()) & 0xFF) << 16) | ((uint32(bg->c.green()) & 0xFF) << 8) | ((uint32(bg->c.blue()) & 0xFF) << 24);
		CornersMap::const_iterator i = cornersMap.find(colorKey);
		if (i == cornersMap.cend()) {
			QImage images[4];
			prepareCorners(NoneCorners, st::msgRadius, bg, 0, images);

			CornersPixmaps pixmaps;
			for (int j = 0; j < 4; ++j) {
				pixmaps.p[j] = new QPixmap(QPixmap::fromImage(images[j], Qt::ColorOnly));
				pixmaps.p[j]->setDevicePixelRatio(cRetinaFactor());
			}
			i = cornersMap.insert(colorKey, pixmaps);
		}
		roundRect(p, x, y, w, h, bg, i.value(), 0);
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
		float64 alpha = st::msgServiceBg->c.alphaF();
		_msgServiceBg = style::color(r, g, b, qRound(alpha * 0xFF));

		float64 alphaSel = st::msgServiceSelectBg->c.alphaF(), addSel = (1. - ((1. - alphaSel) / (1. - alpha))) * 0xFF;
		uchar rsel = snap(qRound(((1. - alphaSel) * r + addSel) / alphaSel), 0, 0xFF);
		uchar gsel = snap(qRound(((1. - alphaSel) * g + addSel) / alphaSel), 0, 0xFF);
		uchar bsel = snap(qRound(((1. - alphaSel) * b + addSel) / alphaSel), 0, 0xFF);
		_msgServiceSelectBg = style::color(r, g, b, qRound(alphaSel * 0xFF));

		prepareCorners(ServiceCorners, st::msgRadius, _msgServiceBg);
		prepareCorners(ServiceSelectedCorners, st::msgRadius, _msgServiceSelectBg);

		uchar rScroll = uchar(componentsScroll[0]), gScroll = uchar(componentsScroll[1]), bScroll = uchar(componentsScroll[2]);
		_historyScrollBarColor = style::color(rScroll, gScroll, bScroll, qRound(st::historyScroll.barColor->c.alphaF() * 0xFF));
		_historyScrollBgColor = style::color(rScroll, gScroll, bScroll, qRound(st::historyScroll.bgColor->c.alphaF() * 0xFF));
		_historyScrollBarOverColor = style::color(rScroll, gScroll, bScroll, qRound(st::historyScroll.barOverColor->c.alphaF() * 0xFF));
		_historyScrollBgOverColor = style::color(rScroll, gScroll, bScroll, qRound(st::historyScroll.bgOverColor->c.alphaF() * 0xFF));

		uchar rPoint = uchar(componentsPoint[0]), gPoint = uchar(componentsPoint[1]), bPoint = uchar(componentsPoint[2]);
		_introPointHoverColor = style::color(rPoint, gPoint, bPoint);
		if (App::main()) App::main()->updateScrollColors();
	}

	const style::color &msgServiceBg() {
		return _msgServiceBg;
	}

	const style::color &msgServiceSelectBg() {
		return _msgServiceSelectBg;
	}

	const style::color &historyScrollBarColor() {
		return _historyScrollBarColor;
	}

	const style::color &historyScrollBgColor() {
		return _historyScrollBgColor;
	}

	const style::color &historyScrollBarOverColor() {
		return _historyScrollBarOverColor;
	}

	const style::color &historyScrollBgOverColor() {
		return _historyScrollBgOverColor;
	}

	const style::color &introPointHoverColor() {
		return _introPointHoverColor;
	}

	WallPapers gServerBackgrounds;

}
