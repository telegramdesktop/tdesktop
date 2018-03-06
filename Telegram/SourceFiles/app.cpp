/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "app.h"

#ifdef OS_MAC_OLD
#include <libexif/exif-data.h>
#endif // OS_MAC_OLD

#include "styles/style_overview.h"
#include "styles/style_mediaview.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_history.h"
#include "styles/style_boxes.h"
#include "lang/lang_keys.h"
#include "data/data_abstract_structure.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_location_manager.h"
#include "history/history_media_types.h"
#include "history/history_item_components.h"
#include "history/view/history_view_service_message.h"
#include "media/media_audio.h"
#include "inline_bots/inline_bot_layout_item.h"
#include "messenger.h"
#include "application.h"
#include "storage/file_upload.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "storage/localstorage.h"
#include "apiwrap.h"
#include "numbers.h"
#include "observer_peer.h"
#include "auth_session.h"
#include "core/crash_reports.h"
#include "storage/storage_facade.h"
#include "storage/storage_shared_media.h"
#include "window/themes/window_theme.h"
#include "window/notifications_manager.h"
#include "platform/platform_notifications_manager.h"

namespace {
	App::LaunchState _launchState = App::Launched;

	UserData *self = nullptr;

	using PeersData = QHash<PeerId, PeerData*>;
	PeersData peersData;

	using MutedPeers = QMap<not_null<PeerData*>, bool>;
	MutedPeers mutedPeers;

	using LocationsData = QHash<LocationCoords, LocationData*>;
	LocationsData locationsData;

	using DependentItemsSet = OrderedSet<HistoryItem*>;
	using DependentItems = QMap<HistoryItem*, DependentItemsSet>;
	DependentItems dependentItems;

	Histories histories;

	using MsgsData = QHash<MsgId, HistoryItem*>;
	MsgsData msgsData;
	using ChannelMsgsData = QMap<ChannelId, MsgsData>;
	ChannelMsgsData channelMsgsData;

	using RandomData = QMap<uint64, FullMsgId>;
	RandomData randomData;

	using SentData = QMap<uint64, QPair<PeerId, QString>>;
	SentData sentData;

	HistoryView::Element *hoveredItem = nullptr,
		*pressedItem = nullptr,
		*hoveredLinkItem = nullptr,
		*pressedLinkItem = nullptr,
		*mousedItem = nullptr;

	QPixmap *emoji = nullptr, *emojiLarge = nullptr;
	style::font monofont;

	struct CornersPixmaps {
		QPixmap p[4];
	};
	QVector<CornersPixmaps> corners;
	using CornersMap = QMap<uint32, CornersPixmaps>;
	CornersMap cornersMap;
	QImage cornersMaskLarge[4], cornersMaskSmall[4];

	using EmojiImagesMap = QMap<int, QPixmap>;
	EmojiImagesMap MainEmojiMap;
	QMap<int, EmojiImagesMap> OtherEmojiMap;

	int32 serviceImageCacheSize = 0;

} // namespace

namespace App {

	QString formatPhone(QString phone) {
		if (phone.isEmpty()) return QString();
		if (phone.at(0) == '0') return phone;

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

	MainWindow *wnd() {
		if (auto instance = Messenger::InstancePointer()) {
			return instance->getActiveWindow();
		}
		return nullptr;
	}

	MainWidget *main() {
		if (auto window = wnd()) {
			return window->mainWidget();
		}
		return nullptr;
	}

	bool passcoded() {
		if (auto window = wnd()) {
			return window->passcodeWidget();
		}
		return false;
	}

namespace {
	bool loggedOut() {
		if (Global::LocalPasscode()) {
			Global::SetLocalPasscode(false);
			Global::RefLocalPasscodeChanged().notify();
		}
		Media::Player::mixer()->stopAndClear();
		if (auto w = wnd()) {
			w->tempDirDelete(Local::ClearManagerAll);
			w->setupIntro();
		}
		histories().clear();
		Messenger::Instance().authSessionDestroy();
		Local::reset();
		Window::Theme::Background()->reset();

		cSetOtherOnline(0);
		clearStorageImages();
		if (auto w = wnd()) {
			w->updateConnectingStatus();
		}
		return true;
	}
} // namespace

	void logOut() {
		if (auto mtproto = Messenger::Instance().mtp()) {
			mtproto->logout(rpcDone([] {
				return loggedOut();
			}), rpcFail([] {
				return loggedOut();
			}));
		} else {
			// We log out because we've forgotten passcode.
			// So we just start mtproto from scratch.
			Messenger::Instance().startMtp();
			loggedOut();
		}
	}

	namespace {
		// we should get a full restriction in "{fulltype}: {reason}" format and we
		// need to find a "-all" tag in {fulltype}, otherwise ignore this restriction
		QString extractRestrictionReason(const QString &fullRestriction) {
			int fullTypeEnd = fullRestriction.indexOf(':');
			if (fullTypeEnd <= 0) {
				return QString();
			}

			// {fulltype} is in "{type}-{tag}-{tag}-{tag}" format
			// if we find "all" tag we return the restriction string
			auto typeTags = fullRestriction.mid(0, fullTypeEnd).split('-').mid(1);
#ifndef OS_MAC_STORE
			auto restrictionApplies = typeTags.contains(qsl("all"));
#else // OS_MAC_STORE
			auto restrictionApplies = typeTags.contains(qsl("all")) || typeTags.contains(qsl("ios"));
#endif // OS_MAC_STORE
			if (restrictionApplies) {
				return fullRestriction.midRef(fullTypeEnd + 1).trimmed().toString();
			}
			return QString();
		}
	}

	UserData *feedUser(const MTPUser &user) {
		UserData *data = nullptr;
		bool minimal = false;
		const MTPUserStatus *status = 0, emptyStatus = MTP_userStatusEmpty();

		Notify::PeerUpdate update;
		using UpdateFlag = Notify::PeerUpdate::Flag;

		switch (user.type()) {
		case mtpc_userEmpty: {
			auto &d = user.c_userEmpty();

			auto peer = peerFromUser(d.vid.v);
			data = App::user(peer);
			auto canShareThisContact = data->canShareThisContactFast();

			data->input = MTP_inputPeerUser(d.vid, MTP_long(0));
			data->inputUser = MTP_inputUser(d.vid, MTP_long(0));
			data->setName(lang(lng_deleted), QString(), QString(), QString());
			data->setPhoto(MTP_userProfilePhotoEmpty());
			//data->setFlags(MTPDuser_ClientFlag::f_inaccessible | 0);
			data->setFlags(MTPDuser::Flag::f_deleted);
			if (!data->phone().isEmpty()) {
				data->setPhone(QString());
				update.flags |= UpdateFlag::UserPhoneChanged;
			}
			data->setBotInfoVersion(-1);
			status = &emptyStatus;
			data->setContactStatus(UserData::ContactStatus::PhoneUnknown);

			if (canShareThisContact != data->canShareThisContactFast()) {
				update.flags |= UpdateFlag::UserCanShareContact;
			}
		} break;
		case mtpc_user: {
			auto &d = user.c_user();
			minimal = d.is_min();

			auto peer = peerFromUser(d.vid.v);
			data = App::user(peer);
			auto canShareThisContact = data->canShareThisContactFast();
			if (minimal) {
				auto mask = 0
					//| MTPDuser_ClientFlag::f_inaccessible
					| MTPDuser::Flag::f_deleted;
				data->setFlags((data->flags() & ~mask) | (d.vflags.v & mask));
			} else {
				data->setFlags(d.vflags.v);
				if (d.is_self()) {
					data->input = MTP_inputPeerSelf();
					data->inputUser = MTP_inputUserSelf();
				} else if (!d.has_access_hash()) {
					data->input = MTP_inputPeerUser(d.vid, MTP_long(data->accessHash()));
					data->inputUser = MTP_inputUser(d.vid, MTP_long(data->accessHash()));
				} else {
					data->input = MTP_inputPeerUser(d.vid, d.vaccess_hash);
					data->inputUser = MTP_inputUser(d.vid, d.vaccess_hash);
				}
				if (d.is_restricted()) {
					data->setRestrictionReason(extractRestrictionReason(qs(d.vrestriction_reason)));
				} else {
					data->setRestrictionReason(QString());
				}
			}
			if (d.is_deleted()) {
				if (!data->phone().isEmpty()) {
					data->setPhone(QString());
					update.flags |= UpdateFlag::UserPhoneChanged;
				}
				data->setName(lang(lng_deleted), QString(), QString(), QString());
				data->setPhoto(MTP_userProfilePhotoEmpty());
				status = &emptyStatus;
			} else {
				// apply first_name and last_name from minimal user only if we don't have
				// local values for first name and last name already, otherwise skip
				bool noLocalName = data->firstName.isEmpty() && data->lastName.isEmpty();
				QString fname = (!minimal || noLocalName) ? (d.has_first_name() ? TextUtilities::SingleLine(qs(d.vfirst_name)) : QString()) : data->firstName;
				QString lname = (!minimal || noLocalName) ? (d.has_last_name() ? TextUtilities::SingleLine(qs(d.vlast_name)) : QString()) : data->lastName;

				QString phone = minimal ? data->phone() : (d.has_phone() ? qs(d.vphone) : QString());
				QString uname = minimal ? data->username : (d.has_username() ? TextUtilities::SingleLine(qs(d.vusername)) : QString());

				bool phoneChanged = (data->phone() != phone);
				if (phoneChanged) {
					data->setPhone(phone);
					update.flags |= UpdateFlag::UserPhoneChanged;
				}
				bool nameChanged = (data->firstName != fname) || (data->lastName != lname);

				bool showPhone = !isServiceUser(data->id)
					&& !d.is_self()
					&& !d.is_contact()
					&& !d.is_mutual_contact();
				bool showPhoneChanged = !isServiceUser(data->id)
					&& !d.is_self()
					&& ((showPhone
						&& data->contactStatus() == UserData::ContactStatus::Contact)
						|| (!showPhone
							&& data->contactStatus() == UserData::ContactStatus::CanAdd));
				if (minimal) {
					showPhoneChanged = false;
					showPhone = !isServiceUser(data->id)
						&& (data->id != Auth().userPeerId())
						&& (data->contactStatus() == UserData::ContactStatus::CanAdd);
				}

				// see also Local::readPeer

				const auto pname = (showPhoneChanged || phoneChanged || nameChanged)
					? ((showPhone && !phone.isEmpty())
						? formatPhone(phone)
						: QString())
					: data->nameOrPhone;

				if (!minimal && d.is_self() && uname != data->username) {
					CrashReports::SetAnnotation("Username", uname);
				}
				data->setName(fname, lname, pname, uname);
				if (d.has_photo()) {
					data->setPhoto(d.vphoto);
				} else {
					data->setPhoto(MTP_userProfilePhotoEmpty());
				}
				if (d.has_access_hash()) {
					data->setAccessHash(d.vaccess_hash.v);
				}
				status = d.has_status() ? &d.vstatus : &emptyStatus;
			}
			if (!minimal) {
				if (d.has_bot_info_version()) {
					data->setBotInfoVersion(d.vbot_info_version.v);
					data->botInfo->readsAllHistory = d.is_bot_chat_history();
					if (data->botInfo->cantJoinGroups != d.is_bot_nochats()) {
						data->botInfo->cantJoinGroups = d.is_bot_nochats();
						update.flags |= UpdateFlag::BotCanAddToGroups;
					}
					data->botInfo->inlinePlaceholder = d.has_bot_inline_placeholder() ? '_' + qs(d.vbot_inline_placeholder) : QString();
				} else {
					data->setBotInfoVersion(-1);
				}
				data->setContactStatus((d.is_contact() || d.is_mutual_contact())
					? UserData::ContactStatus::Contact
					: data->phone().isEmpty()
					? UserData::ContactStatus::PhoneUnknown
					: UserData::ContactStatus::CanAdd);
				if (d.is_self() && ::self != data) {
					::self = data;
					Global::RefSelfChanged().notify();
				}
			}

			if (canShareThisContact != data->canShareThisContactFast()) {
				update.flags |= UpdateFlag::UserCanShareContact;
			}
		} break;
		}

		if (!data) {
			return nullptr;
		}

		if (minimal) {
			if (data->loadedStatus == PeerData::NotLoaded) {
				data->loadedStatus = PeerData::MinimalLoaded;
			}
		} else if (data->loadedStatus != PeerData::FullLoaded) {
			data->loadedStatus = PeerData::FullLoaded;
		}

		if (status && !minimal) {
			auto oldOnlineTill = data->onlineTill;
			auto newOnlineTill = Auth().api().onlineTillFromStatus(*status, oldOnlineTill);
			if (oldOnlineTill != newOnlineTill) {
				data->onlineTill = newOnlineTill;
				update.flags |= UpdateFlag::UserOnlineChanged;
			}
		}

		if (data->contactStatus() == UserData::ContactStatus::PhoneUnknown
			&& !data->phone().isEmpty()
			&& data->id != Auth().userPeerId()) {
			data->setContactStatus(UserData::ContactStatus::CanAdd);
		}
		if (App::main()) {
			if (update.flags) {
				update.peer = data;
				Notify::peerUpdatedDelayed(update);
			}
		}
		return data;
	}

	UserData *feedUsers(const MTPVector<MTPUser> &users) {
        UserData *result = nullptr;
		for_const (auto &user, users.v) {
			if (auto feededUser = feedUser(user)) {
				result = feededUser;
			}
		}

		return result;
	}

	PeerData *feedChat(const MTPChat &chat) {
		PeerData *data = nullptr;
		bool minimal = false;

		Notify::PeerUpdate update;
		using UpdateFlag = Notify::PeerUpdate::Flag;

		switch (chat.type()) {
		case mtpc_chat: {
			auto &d(chat.c_chat());

			data = App::chat(peerFromChat(d.vid.v));
			auto cdata = data->asChat();
			auto canEdit = cdata->canEdit();

			if (cdata->version < d.vversion.v) {
				cdata->version = d.vversion.v;
				cdata->invalidateParticipants();
			}

			data->input = MTP_inputPeerChat(d.vid);
			cdata->setName(qs(d.vtitle));
			cdata->setPhoto(d.vphoto);
			cdata->date = d.vdate.v;

			if (d.has_migrated_to() && d.vmigrated_to.type() == mtpc_inputChannel) {
				auto &c = d.vmigrated_to.c_inputChannel();
				auto channel = App::channel(peerFromChannel(c.vchannel_id));
				channel->addFlags(MTPDchannel::Flag::f_megagroup);
				if (!channel->access) {
					channel->input = MTP_inputPeerChannel(c.vchannel_id, c.vaccess_hash);
					channel->inputChannel = d.vmigrated_to;
					channel->access = d.vmigrated_to.c_inputChannel().vaccess_hash.v;
				}
				bool updatedTo = (cdata->migrateToPtr != channel), updatedFrom = (channel->mgInfo->migrateFromPtr != cdata);
				if (updatedTo) {
					cdata->migrateToPtr = channel;
				}
				if (updatedFrom) {
					channel->mgInfo->migrateFromPtr = cdata;
					if (auto h = App::historyLoaded(cdata->id)) {
						if (auto hto = App::historyLoaded(channel->id)) {
							if (!h->isEmpty()) {
								h->unloadBlocks();
							}
							if (hto->inChatList(Dialogs::Mode::All) && h->inChatList(Dialogs::Mode::All)) {
								App::main()->removeDialog(h);
							}
						}
					}
					Notify::migrateUpdated(channel);
					update.flags |= UpdateFlag::MigrationChanged;
				}
				if (updatedTo) {
					Notify::migrateUpdated(cdata);
					update.flags |= UpdateFlag::MigrationChanged;
				}
			}

			if (!(cdata->flags() & MTPDchat::Flag::f_admins_enabled) && (d.vflags.v & MTPDchat::Flag::f_admins_enabled)) {
				cdata->invalidateParticipants();
			}
			cdata->setFlags(d.vflags.v);

			cdata->count = d.vparticipants_count.v;
			if (canEdit != cdata->canEdit()) {
				update.flags |= UpdateFlag::ChatCanEdit;
			}
		} break;
		case mtpc_chatForbidden: {
			auto &d(chat.c_chatForbidden());

			data = App::chat(peerFromChat(d.vid.v));
			auto cdata = data->asChat();
			auto canEdit = cdata->canEdit();

			data->input = MTP_inputPeerChat(d.vid);
			cdata->setName(qs(d.vtitle));
			cdata->setPhoto(MTP_chatPhotoEmpty());
			cdata->date = 0;
			cdata->count = -1;
			cdata->invalidateParticipants();
			cdata->setFlags(MTPDchat_ClientFlag::f_forbidden | 0);
			if (canEdit != cdata->canEdit()) {
				update.flags |= UpdateFlag::ChatCanEdit;
			}
		} break;
		case mtpc_channel: {
			const auto &d = chat.c_channel();

			const auto peerId = peerFromChannel(d.vid.v);
			minimal = d.is_min();
			if (minimal) {
				data = App::channelLoaded(peerId);
				if (!data) {
					// Can't apply minimal to a not loaded channel.
					// Need to make getDifference.
					return nullptr;
				}
			} else {
				data = App::channel(peerId);
				const auto accessHash = d.has_access_hash()
					? d.vaccess_hash
					: MTP_long(0);
				data->input = MTP_inputPeerChannel(d.vid, accessHash);
			}

			const auto cdata = data->asChannel();
			const auto wasInChannel = cdata->amIn();
			const auto canViewAdmins = cdata->canViewAdmins();
			const auto canViewMembers = cdata->canViewMembers();
			const auto canAddMembers = cdata->canAddMembers();

			if (d.has_participants_count()) {
				cdata->setMembersCount(d.vparticipants_count.v);
			}
			if (minimal) {
				auto mask = 0
					| MTPDchannel::Flag::f_broadcast
					| MTPDchannel::Flag::f_verified
					| MTPDchannel::Flag::f_megagroup
					| MTPDchannel::Flag::f_democracy
					| MTPDchannel_ClientFlag::f_forbidden;
				cdata->setFlags((cdata->flags() & ~mask) | (d.vflags.v & mask));
			} else {
				if (d.has_admin_rights()) {
					cdata->setAdminRights(d.vadmin_rights);
				} else if (cdata->hasAdminRights()) {
					cdata->setAdminRights(MTP_channelAdminRights(MTP_flags(0)));
				}
				if (d.has_banned_rights()) {
					cdata->setRestrictedRights(d.vbanned_rights);
				} else if (cdata->hasRestrictions()) {
					cdata->setRestrictedRights(MTP_channelBannedRights(MTP_flags(0), MTP_int(0)));
				}
				cdata->inputChannel = MTP_inputChannel(d.vid, d.vaccess_hash);
				cdata->access = d.vaccess_hash.v;
				cdata->date = d.vdate.v;
				if (cdata->version < d.vversion.v) {
					cdata->version = d.vversion.v;
				}
				if (d.is_restricted()) {
					cdata->setRestrictionReason(extractRestrictionReason(qs(d.vrestriction_reason)));
				} else {
					cdata->setRestrictionReason(QString());
				}
				cdata->setFlags(d.vflags.v);
				//if (d.has_feed_id()) { // #feed
				//	cdata->setFeed(Auth().data().feed(d.vfeed_id.v));
				//} else {
				//	cdata->clearFeed();
				//}
			}

			QString uname = d.has_username() ? TextUtilities::SingleLine(qs(d.vusername)) : QString();
			cdata->setName(qs(d.vtitle), uname);

			cdata->setPhoto(d.vphoto);

			if (wasInChannel != cdata->amIn()) {
				update.flags |= UpdateFlag::ChannelAmIn;
			}
			if (canViewAdmins != cdata->canViewAdmins()
				|| canViewMembers != cdata->canViewMembers()
				|| canAddMembers != cdata->canAddMembers()) {
				update.flags |= UpdateFlag::ChannelRightsChanged;
			}
		} break;
		case mtpc_channelForbidden: {
			auto &d(chat.c_channelForbidden());

			auto peerId = peerFromChannel(d.vid.v);
			data = App::channel(peerId);
			data->input = MTP_inputPeerChannel(d.vid, d.vaccess_hash);

			auto cdata = data->asChannel();
			auto wasInChannel = cdata->amIn();
			auto canViewAdmins = cdata->canViewAdmins();
			auto canViewMembers = cdata->canViewMembers();
			auto canAddMembers = cdata->canAddMembers();

			cdata->inputChannel = MTP_inputChannel(d.vid, d.vaccess_hash);

			auto mask = mtpCastFlags(MTPDchannelForbidden::Flag::f_broadcast | MTPDchannelForbidden::Flag::f_megagroup);
			cdata->setFlags((cdata->flags() & ~mask) | (mtpCastFlags(d.vflags) & mask) | MTPDchannel_ClientFlag::f_forbidden);

			if (cdata->hasAdminRights()) {
				cdata->setAdminRights(MTP_channelAdminRights(MTP_flags(0)));
			}
			if (cdata->hasRestrictions()) {
				cdata->setRestrictedRights(MTP_channelBannedRights(MTP_flags(0), MTP_int(0)));
			}

			cdata->setName(qs(d.vtitle), QString());

			cdata->access = d.vaccess_hash.v;
			cdata->setPhoto(MTP_chatPhotoEmpty());
			cdata->date = 0;
			cdata->setMembersCount(0);

			if (wasInChannel != cdata->amIn()) {
				update.flags |= UpdateFlag::ChannelAmIn;
			}
			if (canViewAdmins != cdata->canViewAdmins()
				|| canViewMembers != cdata->canViewMembers()
				|| canAddMembers != cdata->canAddMembers()) {
				update.flags |= UpdateFlag::ChannelRightsChanged;
			}
		} break;
		}
		if (!data) {
			return nullptr;
		}

		if (minimal) {
			if (data->loadedStatus == PeerData::NotLoaded) {
				data->loadedStatus = PeerData::MinimalLoaded;
			}
		} else if (data->loadedStatus != PeerData::FullLoaded) {
			data->loadedStatus = PeerData::FullLoaded;
		}
		if (update.flags) {
			update.peer = data;
			Notify::peerUpdatedDelayed(update);
		}
		return data;
	}

	PeerData *feedChats(const MTPVector<MTPChat> &chats) {
		PeerData *result = nullptr;
		for_const (auto &chat, chats.v) {
			if (auto feededChat = feedChat(chat)) {
				result = feededChat;
			}
		}
		return result;
	}

	void feedParticipants(const MTPChatParticipants &p, bool requestBotInfos) {
		ChatData *chat = 0;
		switch (p.type()) {
		case mtpc_chatParticipantsForbidden: {
			const auto &d(p.c_chatParticipantsForbidden());
			chat = App::chat(d.vchat_id.v);
			chat->count = -1;
			chat->invalidateParticipants();
		} break;

		case mtpc_chatParticipants: {
			const auto &d(p.c_chatParticipants());
			chat = App::chat(d.vchat_id.v);
			auto canEdit = chat->canEdit();
			if (!requestBotInfos || chat->version <= d.vversion.v) { // !requestBotInfos is true on getFullChat result
				chat->version = d.vversion.v;
				auto &v = d.vparticipants.v;
				chat->count = v.size();
				int32 pversion = chat->participants.empty()
					? 1
					: (chat->participants.begin()->second + 1);
				chat->invitedByMe.clear();
				chat->admins.clear();
				chat->removeFlags(MTPDchat::Flag::f_admin);
				for (auto i = v.cbegin(), e = v.cend(); i != e; ++i) {
					int32 uid = 0, inviter = 0;
					switch (i->type()) {
					case mtpc_chatParticipantCreator: {
						const auto &p(i->c_chatParticipantCreator());
						uid = p.vuser_id.v;
						chat->creator = uid;
					} break;
					case mtpc_chatParticipantAdmin: {
						const auto &p(i->c_chatParticipantAdmin());
						uid = p.vuser_id.v;
						inviter = p.vinviter_id.v;
					} break;
					case mtpc_chatParticipant: {
						const auto &p(i->c_chatParticipant());
						uid = p.vuser_id.v;
						inviter = p.vinviter_id.v;
					} break;
					}
					if (!uid) continue;

					UserData *user = App::userLoaded(uid);
					if (user) {
						chat->participants[user] = pversion;
						if (inviter == Auth().userId()) {
							chat->invitedByMe.insert(user);
						}
						if (i->type() == mtpc_chatParticipantAdmin) {
							chat->admins.insert(user);
							if (user->isSelf()) {
								chat->addFlags(MTPDchat::Flag::f_admin);
							}
						}
					} else {
						chat->invalidateParticipants();
						break;
					}
				}
				if (!chat->participants.empty()) {
					auto h = App::historyLoaded(chat->id);
					bool found = !h || !h->lastKeyboardFrom;
					auto botStatus = -1;
					for (auto i = chat->participants.begin(), e = chat->participants.end(); i != e;) {
						auto [user, version] = *i;
						if (version < pversion) {
							i = chat->participants.erase(i);
						} else {
							if (user->botInfo) {
								botStatus = 2;// (botStatus > 0/* || !user->botInfo->readsAllHistory*/) ? 2 : 1;
								if (requestBotInfos && !user->botInfo->inited) {
									Auth().api().requestFullPeer(user);
								}
							}
							if (!found && user->id == h->lastKeyboardFrom) {
								found = true;
							}
							++i;
						}
					}
					chat->botStatus = botStatus;
					if (!found) {
						h->clearLastKeyboard();
					}
				}
			}
			if (canEdit != chat->canEdit()) {
				Notify::peerUpdatedDelayed(chat, Notify::PeerUpdate::Flag::ChatCanEdit);
			}
		} break;
		}
		Notify::peerUpdatedDelayed(chat, Notify::PeerUpdate::Flag::MembersChanged | Notify::PeerUpdate::Flag::AdminsChanged);
	}

	void feedParticipantAdd(const MTPDupdateChatParticipantAdd &d) {
		ChatData *chat = App::chat(d.vchat_id.v);
		if (chat->version + 1 < d.vversion.v) {
			chat->version = d.vversion.v;
			chat->invalidateParticipants();
			Auth().api().requestPeer(chat);
		} else if (chat->version <= d.vversion.v && chat->count >= 0) {
			chat->version = d.vversion.v;
			UserData *user = App::userLoaded(d.vuser_id.v);
			if (user) {
				if (chat->participants.empty() && chat->count) {
					chat->count++;
					chat->botStatus = 0;
				} else if (chat->participants.find(user) == chat->participants.end()) {
					chat->participants[user] = (chat->participants.empty() ? 1 : chat->participants.begin()->second);
					if (d.vinviter_id.v == Auth().userId()) {
						chat->invitedByMe.insert(user);
					} else {
						chat->invitedByMe.remove(user);
					}
					chat->count++;
					if (user->botInfo) {
						chat->botStatus = 2;// (chat->botStatus > 0/* || !user->botInfo->readsAllHistory*/) ? 2 : 1;
						if (!user->botInfo->inited) {
							Auth().api().requestFullPeer(user);
						}
					}
				}
			} else {
				chat->invalidateParticipants();
				chat->count++;
			}
			Notify::peerUpdatedDelayed(chat, Notify::PeerUpdate::Flag::MembersChanged);
		}
	}

	void feedParticipantDelete(const MTPDupdateChatParticipantDelete &d) {
		ChatData *chat = App::chat(d.vchat_id.v);
		if (chat->version + 1 < d.vversion.v) {
			chat->version = d.vversion.v;
			chat->invalidateParticipants();
			Auth().api().requestPeer(chat);
		} else if (chat->version <= d.vversion.v && chat->count > 0) {
			chat->version = d.vversion.v;
			auto canEdit = chat->canEdit();
			UserData *user = App::userLoaded(d.vuser_id.v);
			if (user) {
				if (chat->participants.empty()) {
					if (chat->count > 0) {
						chat->count--;
					}
				} else {
					auto i = chat->participants.find(user);
					if (i != chat->participants.end()) {
						chat->participants.erase(i);
						chat->count--;
						chat->invitedByMe.remove(user);
						chat->admins.remove(user);
						if (user->isSelf()) {
							chat->removeFlags(MTPDchat::Flag::f_admin);
						}

						History *h = App::historyLoaded(chat->id);
						if (h && h->lastKeyboardFrom == user->id) {
							h->clearLastKeyboard();
						}
					}
					if (chat->botStatus > 0 && user->botInfo) {
						int32 botStatus = -1;
						for (auto [participant, v] : chat->participants) {
							if (participant->botInfo) {
								if (true || botStatus > 0/* || !participant->botInfo->readsAllHistory*/) {
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
				chat->invalidateParticipants();
				chat->count--;
			}
			if (canEdit != chat->canEdit()) {
				Notify::peerUpdatedDelayed(chat, Notify::PeerUpdate::Flag::ChatCanEdit);
			}
			Notify::peerUpdatedDelayed(chat, Notify::PeerUpdate::Flag::MembersChanged);
		}
	}

	void feedChatAdmins(const MTPDupdateChatAdmins &d) {
		auto chat = App::chat(d.vchat_id.v);
		if (chat->version <= d.vversion.v) {
			auto wasCanEdit = chat->canEdit();
			auto badVersion = (chat->version + 1 < d.vversion.v);
			chat->version = d.vversion.v;
			if (mtpIsTrue(d.venabled)) {
				chat->addFlags(MTPDchat::Flag::f_admins_enabled);
			} else {
				chat->removeFlags(MTPDchat::Flag::f_admins_enabled);
			}
			if (badVersion || mtpIsTrue(d.venabled)) {
				chat->invalidateParticipants();
				Auth().api().requestPeer(chat);
			}
			if (wasCanEdit != chat->canEdit()) {
				Notify::peerUpdatedDelayed(
					chat,
					Notify::PeerUpdate::Flag::ChatCanEdit);
			}
			Notify::peerUpdatedDelayed(
				chat,
				Notify::PeerUpdate::Flag::AdminsChanged);
		}
	}

	void feedParticipantAdmin(const MTPDupdateChatParticipantAdmin &d) {
		ChatData *chat = App::chat(d.vchat_id.v);
		if (chat->version + 1 < d.vversion.v) {
			chat->version = d.vversion.v;
			chat->invalidateParticipants();
			Auth().api().requestPeer(chat);
		} else if (chat->version <= d.vversion.v && chat->count > 0) {
			chat->version = d.vversion.v;
			auto canEdit = chat->canEdit();
			UserData *user = App::userLoaded(d.vuser_id.v);
			if (user) {
				if (mtpIsTrue(d.vis_admin)) {
					if (user->isSelf()) {
						chat->addFlags(MTPDchat::Flag::f_admin);
					}
					if (chat->noParticipantInfo()) {
						Auth().api().requestFullPeer(chat);
					} else {
						chat->admins.insert(user);
					}
				} else {
					if (user->isSelf()) {
						chat->removeFlags(MTPDchat::Flag::f_admin);
					}
					chat->admins.remove(user);
				}
			} else {
				chat->invalidateParticipants();
			}
			if (canEdit != chat->canEdit()) {
				Notify::peerUpdatedDelayed(chat, Notify::PeerUpdate::Flag::ChatCanEdit);
			}
			Notify::peerUpdatedDelayed(chat, Notify::PeerUpdate::Flag::AdminsChanged);
		}
	}

	bool checkEntitiesAndViewsUpdate(const MTPDmessage &m) {
		auto peerId = peerFromMTP(m.vto_id);
		if (m.has_from_id() && peerId == Auth().userPeerId()) {
			peerId = peerFromUser(m.vfrom_id);
		}
		if (const auto existing = App::histItemById(peerToChannel(peerId), m.vid.v)) {
			auto text = qs(m.vmessage);
			auto entities = m.has_entities()
				? TextUtilities::EntitiesFromMTP(m.ventities.v)
				: EntitiesInText();
			const auto media = m.has_media() ? &m.vmedia : nullptr;
			existing->setText({ text, entities });
			existing->updateSentMedia(m.has_media() ? &m.vmedia : nullptr);
			existing->updateReplyMarkup(m.has_reply_markup()
				? (&m.vreply_markup)
				: nullptr);
			existing->setViewsCount(m.has_views() ? m.vviews.v : -1);
			existing->indexAsNewItem();
			if (existing->mainView()) {
				App::checkSavedGif(existing);
				return true;
			}
			return false;
		}
		return false;
	}

	void updateEditedMessage(const MTPMessage &m) {
		auto apply = [](const auto &data) {
			auto peerId = peerFromMTP(data.vto_id);
			if (data.has_from_id() && peerId == Auth().userPeerId()) {
				peerId = peerFromUser(data.vfrom_id);
			}
			if (auto existing = App::histItemById(peerToChannel(peerId), data.vid.v)) {
				existing->applyEdition(data);
			}
		};

		if (m.type() == mtpc_message) { // apply message edit
			apply(m.c_message());
		} else if (m.type() == mtpc_messageService) {
			apply(m.c_messageService());
		}
	}

	void addSavedGif(DocumentData *doc) {
		auto &saved = Auth().data().savedGifsRef();
		int32 index = saved.indexOf(doc);
		if (index) {
			if (index > 0) saved.remove(index);
			saved.push_front(doc);
			if (saved.size() > Global::SavedGifsLimit()) saved.pop_back();
			Local::writeSavedGifs();

			Auth().data().notifySavedGifsUpdated();
			Auth().data().setLastSavedGifsUpdate(0);
			Auth().api().updateStickers();
		}
	}

	void checkSavedGif(HistoryItem *item) {
		if (!item->Has<HistoryMessageForwarded>() && (item->out() || item->history()->peer == App::self())) {
			if (const auto media = item->media()) {
				if (const auto document = media->document()) {
					if (document->isGifv()) {
						addSavedGif(document);
					}
				}
			}
		}
	}

	void feedMsgs(const QVector<MTPMessage> &msgs, NewMessageType type) {
		auto indices = base::flat_map<uint64, int>();
		for (int i = 0, l = msgs.size(); i != l; ++i) {
			const auto &msg = msgs[i];
			if (msg.type() == mtpc_message) {
				const auto &data = msg.c_message();
				if (type == NewMessageUnread) { // new message, index my forwarded messages to links overview
					if (checkEntitiesAndViewsUpdate(data)) { // already in blocks
						LOG(("Skipping message, because it is already in blocks!"));
						continue;
					}
				}
			}
			const auto msgId = idFromMessage(msg);
			indices.emplace((uint64(uint32(msgId)) << 32) | uint64(i), i);
		}
		for (const auto [position, index] : indices) {
			histories().addNewMessage(msgs[index], type);
		}
	}

	void feedMsgs(const MTPVector<MTPMessage> &msgs, NewMessageType type) {
		return feedMsgs(msgs.v, type);
	}

	ImagePtr image(const MTPPhotoSize &size) {
		switch (size.type()) {
		case mtpc_photoSize: {
			auto &d = size.c_photoSize();
			if (d.vlocation.type() == mtpc_fileLocation) {
				auto &l = d.vlocation.c_fileLocation();
				return ImagePtr(StorageImageLocation(d.vw.v, d.vh.v, l.vdc_id.v, l.vvolume_id.v, l.vlocal_id.v, l.vsecret.v), d.vsize.v);
			}
		} break;
		case mtpc_photoCachedSize: {
			auto &d = size.c_photoCachedSize();
			if (d.vlocation.type() == mtpc_fileLocation) {
				auto &l = d.vlocation.c_fileLocation();
				auto bytes = qba(d.vbytes);
				return ImagePtr(StorageImageLocation(d.vw.v, d.vh.v, l.vdc_id.v, l.vvolume_id.v, l.vlocal_id.v, l.vsecret.v), bytes);
			} else if (d.vlocation.type() == mtpc_fileLocationUnavailable) {
				auto bytes = qba(d.vbytes);
				return ImagePtr(StorageImageLocation(d.vw.v, d.vh.v, 0, 0, 0, 0), bytes);
			}
		} break;
		}
		return ImagePtr();
	}

	void feedInboxRead(const PeerId &peer, MsgId upTo) {
		if (const auto history = App::historyLoaded(peer)) {
			history->inboxRead(upTo);
		}
	}

	void feedOutboxRead(const PeerId &peer, MsgId upTo, TimeId when) {
		if (auto history = App::historyLoaded(peer)) {
			history->outboxRead(upTo);
			if (const auto user = history->peer->asUser()) {
				user->madeAction(when);
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

	void feedWereDeleted(
			ChannelId channelId,
			const QVector<MTPint> &msgsIds) {
		const auto data = fetchMsgsData(channelId, false);
		if (!data) return;

		const auto affectedHistory = (channelId != NoChannel)
			? App::history(peerFromChannel(channelId)).get()
			: nullptr;

		auto historiesToCheck = base::flat_set<not_null<History*>>();
		for (const auto msgId : msgsIds) {
			auto j = data->constFind(msgId.v);
			if (j != data->cend()) {
				const auto history = (*j)->history();
				(*j)->destroy();
				if (!history->lastMessageKnown()) {
					historiesToCheck.emplace(history);
				}
			} else if (affectedHistory) {
				affectedHistory->unknownMessageDeleted(msgId.v);
			}
		}
		for (const auto history : historiesToCheck) {
			Auth().api().requestDialogEntry(history);
		}
	}

	void feedUserLink(MTPint userId, const MTPContactLink &myLink, const MTPContactLink &foreignLink) {
		if (const auto user = userLoaded(userId.v)) {
			const auto wasShowPhone = (user->contactStatus() == UserData::ContactStatus::CanAdd);
			switch (myLink.type()) {
			case mtpc_contactLinkContact:
				user->setContactStatus(UserData::ContactStatus::Contact);
			break;
			case mtpc_contactLinkHasPhone:
				user->setContactStatus(UserData::ContactStatus::CanAdd);
			break;
			case mtpc_contactLinkNone:
			case mtpc_contactLinkUnknown:
				user->setContactStatus(UserData::ContactStatus::PhoneUnknown);
			break;
			}
			if (user->contactStatus() == UserData::ContactStatus::PhoneUnknown
				&& !user->phone().isEmpty()
				&& user->id != Auth().userPeerId()) {
				user->setContactStatus(UserData::ContactStatus::CanAdd);
			}

			const auto showPhone = !isServiceUser(user->id)
				&& !user->isSelf()
				&& user->contactStatus() == UserData::ContactStatus::CanAdd;
			const auto showPhoneChanged = !isServiceUser(user->id)
				&& !user->isSelf()
				&& (showPhone != wasShowPhone);
			if (showPhoneChanged) {
				user->setName(
					TextUtilities::SingleLine(user->firstName),
					TextUtilities::SingleLine(user->lastName),
					showPhone
						? App::formatPhone(user->phone())
						: QString(),
					TextUtilities::SingleLine(user->username));
			}
		}
	}

	PeerData *peer(const PeerId &id, PeerData::LoadedStatus restriction) {
		if (!id) return nullptr;

		auto i = peersData.constFind(id);
		if (i == peersData.cend()) {
			PeerData *newData = nullptr;
			if (peerIsUser(id)) {
				newData = new UserData(id);
			} else if (peerIsChat(id)) {
				newData = new ChatData(id);
			} else if (peerIsChannel(id)) {
				newData = new ChannelData(id);
			}
			Assert(newData != nullptr);

			newData->input = MTPinputPeer(MTP_inputPeerEmpty());
			i = peersData.insert(id, newData);
		}
		switch (restriction) {
		case PeerData::MinimalLoaded: {
			if (i.value()->loadedStatus == PeerData::NotLoaded) {
				return nullptr;
			}
		} break;
		case PeerData::FullLoaded: {
			if (i.value()->loadedStatus != PeerData::FullLoaded) {
				return nullptr;
			}
		} break;
		}
		return i.value();
	}

	void enumerateUsers(base::lambda<void(UserData*)> action) {
		for_const (auto peer, peersData) {
			if (auto user = peer->asUser()) {
				action(user);
			}
		}
	}

	UserData *self() {
		return ::self;
	}

	PeerData *peerByName(const QString &username) {
		QString uname(username.trimmed());
		for_const (PeerData *peer, peersData) {
			if (!peer->userName().compare(uname, Qt::CaseInsensitive)) {
				return peer;
			}
		}
		return nullptr;
	}

	LocationData *location(const LocationCoords &coords) {
		auto i = locationsData.constFind(coords);
		if (i == locationsData.cend()) {
			i = locationsData.insert(coords, new LocationData(coords));
		}
		return i.value();
	}

	void forgetMedia() {
		for_const (auto location, ::locationsData) {
			location->thumb->forget();
		}
	}

	MTPPhoto photoFromUserPhoto(MTPint userId, MTPint date, const MTPUserProfilePhoto &photo) {
		if (photo.type() == mtpc_userProfilePhoto) {
			const auto &uphoto(photo.c_userProfilePhoto());

			QVector<MTPPhotoSize> photoSizes;
			photoSizes.push_back(MTP_photoSize(MTP_string("a"), uphoto.vphoto_small, MTP_int(160), MTP_int(160), MTP_int(0)));
			photoSizes.push_back(MTP_photoSize(MTP_string("c"), uphoto.vphoto_big, MTP_int(640), MTP_int(640), MTP_int(0)));

			return MTP_photo(MTP_flags(0), uphoto.vphoto_id, MTP_long(0), date, MTP_vector<MTPPhotoSize>(photoSizes));
		}
		return MTP_photoEmpty(MTP_long(0));
	}

	QString peerName(const PeerData *peer, bool forDialogs) {
		return peer ? ((forDialogs && peer->isUser() && !peer->asUser()->nameOrPhone.isEmpty()) ? peer->asUser()->nameOrPhone : peer->name) : lang(lng_deleted);
	}

	Histories &histories() {
		return ::histories;
	}

	not_null<History*> history(const PeerId &peer) {
		return ::histories.findOrInsert(peer);
	}

	History *historyLoaded(const PeerId &peer) {
		if (!peer) {
			return nullptr;
		}
		return ::histories.find(peer);
	}

	HistoryItem *histItemById(ChannelId channelId, MsgId itemId) {
		if (!itemId) return nullptr;

		auto data = fetchMsgsData(channelId, false);
		if (!data) return nullptr;

		auto i = data->constFind(itemId);
		if (i != data->cend()) {
			return i.value();
		}
		return nullptr;
	}

	void historyRegItem(not_null<HistoryItem*> item) {
		const auto data = fetchMsgsData(item->channelId());
		const auto i = data->constFind(item->id);
		if (i == data->cend()) {
			data->insert(item->id, item);
		} else if (i.value() != item) {
			LOG(("App Error: trying to historyRegItem() an already registered item"));
			i.value()->destroy();
			data->insert(item->id, item);
		}
	}

	void historyUnregItem(not_null<HistoryItem*> item) {
		const auto data = fetchMsgsData(item->channelId(), false);
		if (!data) return;

		const auto i = data->find(item->id);
		if (i != data->cend()) {
			if (i.value() == item) {
				data->erase(i);
			}
		}
		const auto j = ::dependentItems.find(item);
		if (j != ::dependentItems.cend()) {
			DependentItemsSet items;
			std::swap(items, j.value());
			::dependentItems.erase(j);

			for_const (auto dependent, items) {
				dependent->dependencyItemRemoved(item);
			}
		}
		Auth().notifications().clearFromItem(item);
	}

	void historyUpdateDependent(not_null<HistoryItem*> item) {
		const auto j = ::dependentItems.find(item);
		if (j != ::dependentItems.cend()) {
			for_const (const auto dependent, j.value()) {
				dependent->updateDependencyItem();
			}
		}
		if (App::main()) {
			App::main()->itemEdited(item);
		}
	}

	void historyClearMsgs() {
		::dependentItems.clear();

		QVector<HistoryItem*> toDelete;
		for_const (auto item, msgsData) {
			if (!item->mainView()) {
				toDelete.push_back(item);
			}
		}
		for_const (auto &chMsgsData, channelMsgsData) {
			for_const (auto item, chMsgsData) {
				if (!item->mainView()) {
					toDelete.push_back(item);
				}
			}
		}
		msgsData.clear();
		channelMsgsData.clear();
		for_const (auto item, toDelete) {
			delete item;
		}

		clearMousedItems();
	}

	void historyClearItems() {
		randomData.clear();
		sentData.clear();
		mutedPeers.clear();
		cSetSavedPeers(SavedPeers());
		cSetSavedPeersByTime(SavedPeersByTime());
		cSetRecentInlineBots(RecentInlineBots());

		for_const (auto peer, ::peersData) {
			delete peer;
		}
		::peersData.clear();

		if (AuthSession::Exists()) {
			Auth().api().clearWebPageRequests();
		}
		cSetRecentStickers(RecentStickerPack());
		cSetReportSpamStatuses(ReportSpamStatuses());
		cSetAutoDownloadPhoto(0);
		cSetAutoDownloadAudio(0);
		cSetAutoDownloadGif(0);
		::self = nullptr;
		Global::RefSelfChanged().notify(true);
	}

	void historyRegDependency(HistoryItem *dependent, HistoryItem *dependency) {
		::dependentItems[dependency].insert(dependent);
	}

	void historyUnregDependency(HistoryItem *dependent, HistoryItem *dependency) {
		auto i = ::dependentItems.find(dependency);
		if (i != ::dependentItems.cend()) {
			i.value().remove(dependent);
			if (i.value().isEmpty()) {
				::dependentItems.erase(i);
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

	void prepareCorners(RoundCorners index, int32 radius, const QBrush &brush, const style::color *shadow = nullptr, QImage *cors = nullptr) {
		Expects(::corners.size() > index);
		int32 r = radius * cIntRetinaFactor(), s = st::msgShadow * cIntRetinaFactor();
		QImage rect(r * 3, r * 3 + (shadow ? s : 0), QImage::Format_ARGB32_Premultiplied), localCors[4];
		{
			Painter p(&rect);
			PainterHighQualityEnabler hq(p);

			p.setCompositionMode(QPainter::CompositionMode_Source);
			p.fillRect(QRect(0, 0, rect.width(), rect.height()), Qt::transparent);
			p.setCompositionMode(QPainter::CompositionMode_SourceOver);
			p.setPen(Qt::NoPen);
			if (shadow) {
				p.setBrush((*shadow)->b);
				p.drawRoundedRect(0, s, r * 3, r * 3, r, r);
			}
			p.setBrush(brush);
			p.drawRoundedRect(0, 0, r * 3, r * 3, r, r);
		}
		if (!cors) cors = localCors;
		cors[0] = rect.copy(0, 0, r, r);
		cors[1] = rect.copy(r * 2, 0, r, r);
		cors[2] = rect.copy(0, r * 2, r, r + (shadow ? s : 0));
		cors[3] = rect.copy(r * 2, r * 2, r, r + (shadow ? s : 0));
		if (index != SmallMaskCorners && index != LargeMaskCorners) {
			for (int i = 0; i < 4; ++i) {
				::corners[index].p[i] = pixmapFromImageInPlace(std::move(cors[i]));
				::corners[index].p[i].setDevicePixelRatio(cRetinaFactor());
			}
		}
	}

	void tryFontFamily(QString &family, const QString &tryFamily) {
		if (family.isEmpty()) {
			if (!QFontInfo(QFont(tryFamily)).family().trimmed().compare(tryFamily, Qt::CaseInsensitive)) {
				family = tryFamily;
			}
		}
	}

	void createMaskCorners() {
		QImage mask[4];
		prepareCorners(SmallMaskCorners, st::buttonRadius, QColor(255, 255, 255), nullptr, mask);
		for (int i = 0; i < 4; ++i) {
			::cornersMaskSmall[i] = mask[i].convertToFormat(QImage::Format_ARGB32_Premultiplied);
			::cornersMaskSmall[i].setDevicePixelRatio(cRetinaFactor());
		}
		prepareCorners(LargeMaskCorners, st::historyMessageRadius, QColor(255, 255, 255), nullptr, mask);
		for (int i = 0; i < 4; ++i) {
			::cornersMaskLarge[i] = mask[i].convertToFormat(QImage::Format_ARGB32_Premultiplied);
			::cornersMaskLarge[i].setDevicePixelRatio(cRetinaFactor());
		}
	}

	void createPaletteCorners() {
		prepareCorners(MenuCorners, st::buttonRadius, st::menuBg);
		prepareCorners(BoxCorners, st::boxRadius, st::boxBg);
		prepareCorners(BotKbOverCorners, st::dateRadius, st::msgBotKbOverBgAdd);
		prepareCorners(StickerCorners, st::dateRadius, st::msgServiceBg);
		prepareCorners(StickerSelectedCorners, st::dateRadius, st::msgServiceBgSelected);
		prepareCorners(SelectedOverlaySmallCorners, st::buttonRadius, st::msgSelectOverlay);
		prepareCorners(SelectedOverlayLargeCorners, st::historyMessageRadius, st::msgSelectOverlay);
		prepareCorners(DateCorners, st::dateRadius, st::msgDateImgBg);
		prepareCorners(DateSelectedCorners, st::dateRadius, st::msgDateImgBgSelected);
		prepareCorners(InShadowCorners, st::historyMessageRadius, st::msgInShadow);
		prepareCorners(InSelectedShadowCorners, st::historyMessageRadius, st::msgInShadowSelected);
		prepareCorners(ForwardCorners, st::historyMessageRadius, st::historyForwardChooseBg);
		prepareCorners(MediaviewSaveCorners, st::mediaviewControllerRadius, st::mediaviewSaveMsgBg);
		prepareCorners(EmojiHoverCorners, st::buttonRadius, st::emojiPanHover);
		prepareCorners(StickerHoverCorners, st::buttonRadius, st::emojiPanHover);
		prepareCorners(BotKeyboardCorners, st::buttonRadius, st::botKbBg);
		prepareCorners(PhotoSelectOverlayCorners, st::buttonRadius, st::overviewPhotoSelectOverlay);

		prepareCorners(Doc1Corners, st::buttonRadius, st::msgFile1Bg);
		prepareCorners(Doc2Corners, st::buttonRadius, st::msgFile2Bg);
		prepareCorners(Doc3Corners, st::buttonRadius, st::msgFile3Bg);
		prepareCorners(Doc4Corners, st::buttonRadius, st::msgFile4Bg);

		prepareCorners(MessageInCorners, st::historyMessageRadius, st::msgInBg, &st::msgInShadow);
		prepareCorners(MessageInSelectedCorners, st::historyMessageRadius, st::msgInBgSelected, &st::msgInShadowSelected);
		prepareCorners(MessageOutCorners, st::historyMessageRadius, st::msgOutBg, &st::msgOutShadow);
		prepareCorners(MessageOutSelectedCorners, st::historyMessageRadius, st::msgOutBgSelected, &st::msgOutShadowSelected);
	}

	void createCorners() {
		::corners.resize(RoundCornersCount);
		createMaskCorners();
		createPaletteCorners();
	}

	void clearCorners() {
		::corners.clear();
		::cornersMap.clear();
	}

	void initMedia() {
		if (!::monofont) {
			QString family;
			tryFontFamily(family, qsl("Consolas"));
			tryFontFamily(family, qsl("Liberation Mono"));
			tryFontFamily(family, qsl("Menlo"));
			tryFontFamily(family, qsl("Courier"));
			if (family.isEmpty()) family = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
			::monofont = style::font(st::normalFont->f.pixelSize(), 0, family);
		}
		Ui::Emoji::Init();
		if (!::emoji) {
			::emoji = new QPixmap(Ui::Emoji::Filename(Ui::Emoji::Index()));
            if (cRetina()) ::emoji->setDevicePixelRatio(cRetinaFactor());
		}
		if (!::emojiLarge) {
			::emojiLarge = new QPixmap(Ui::Emoji::Filename(Ui::Emoji::Index() + 1));
			if (cRetina()) ::emojiLarge->setDevicePixelRatio(cRetinaFactor());
		}

		createCorners();

		using Update = Window::Theme::BackgroundUpdate;
		static auto subscription = Window::Theme::Background()->add_subscription([](const Update &update) {
			if (update.paletteChanged()) {
				createPaletteCorners();

				if (App::main()) {
					App::main()->updateScrollColors();
				}
				HistoryView::serviceColorsUpdated();
			} else if (update.type == Update::Type::New) {
				prepareCorners(StickerCorners, st::dateRadius, st::msgServiceBg);
				prepareCorners(StickerSelectedCorners, st::dateRadius, st::msgServiceBgSelected);

				if (App::main()) {
					App::main()->updateScrollColors();
				}
				HistoryView::serviceColorsUpdated();
			}
		});
	}

	void clearHistories() {
		ClickHandler::clearActive();
		ClickHandler::unpressed();

		if (AuthSession::Exists()) {
			// Clear notifications to prevent any showNotification() calls while destroying items.
			Auth().notifications().clearAllFast();
		}

		histories().clear();

		clearStorageImages();
		cSetServerBackgrounds(WallPapers());

		serviceImageCacheSize = imageCacheSize();
	}

	void deinitMedia() {
		delete ::emoji;
		::emoji = nullptr;
		delete ::emojiLarge;
		::emojiLarge = nullptr;

		clearCorners();

		MainEmojiMap.clear();
		OtherEmojiMap.clear();

		Data::clearGlobalStructures();

		clearAllImages();
	}

	void hoveredItem(HistoryView::Element *item) {
		::hoveredItem = item;
	}

	HistoryView::Element *hoveredItem() {
		return ::hoveredItem;
	}

	void pressedItem(HistoryView::Element *item) {
		::pressedItem = item;
	}

	HistoryView::Element *pressedItem() {
		return ::pressedItem;
	}

	void hoveredLinkItem(HistoryView::Element *item) {
		::hoveredLinkItem = item;
	}

	HistoryView::Element *hoveredLinkItem() {
		return ::hoveredLinkItem;
	}

	void pressedLinkItem(HistoryView::Element *item) {
		::pressedLinkItem = item;
	}

	HistoryView::Element *pressedLinkItem() {
		return ::pressedLinkItem;
	}

	void mousedItem(HistoryView::Element *item) {
		::mousedItem = item;
	}

	HistoryView::Element *mousedItem() {
		return ::mousedItem;
	}

	void clearMousedItems() {
		hoveredItem(nullptr);
		pressedItem(nullptr);
		hoveredLinkItem(nullptr);
		pressedLinkItem(nullptr);
		mousedItem(nullptr);
	}

	const style::font &monofont() {
		return ::monofont;
	}

	const QPixmap &emoji() {
		return *::emoji;
	}

	const QPixmap &emojiLarge() {
		return *::emojiLarge;
	}

	const QPixmap &emojiSingle(EmojiPtr emoji, int32 fontHeight) {
		auto &map = (fontHeight == st::msgFont->height) ? MainEmojiMap : OtherEmojiMap[fontHeight];
		auto i = map.constFind(emoji->index());
		if (i == map.cend()) {
			auto image = QImage(Ui::Emoji::Size() + st::emojiPadding * cIntRetinaFactor() * 2, fontHeight * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
            if (cRetina()) image.setDevicePixelRatio(cRetinaFactor());
			image.fill(Qt::transparent);
			{
				QPainter p(&image);
				emojiDraw(p, emoji, st::emojiPadding * cIntRetinaFactor(), (fontHeight * cIntRetinaFactor() - Ui::Emoji::Size()) / 2);
			}
			i = map.insert(emoji->index(), App::pixmapFromImageInPlace(std::move(image)));
		}
		return i.value();
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
		if (quitting()) return;
		setLaunchState(QuitRequested);

		if (auto window = wnd()) {
			if (!Sandbox::isSavingSession()) {
				window->hide();
			}
		}
		if (auto mainwidget = main()) {
			mainwidget->saveDraftToCloud();
		}
		Messenger::QuitAttempt();
	}

	bool quitting() {
		return _launchState != Launched;
	}

	LaunchState launchState() {
		return _launchState;
	}

	void setLaunchState(LaunchState state) {
		_launchState = state;
	}

	void restart() {
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
		bool updateReady = (Sandbox::updatingState() == Application::UpdatingReady);
#else // !TDESKTOP_DISABLE_AUTOUPDATE
		bool updateReady = false;
#endif // else for !TDESKTOP_DISABLE_AUTOUPDATE
		if (updateReady) {
			cSetRestartingUpdate(true);
		} else {
			cSetRestarting(true);
			cSetRestartingToSettings(true);
		}
		App::quit();
	}

	QImage readImage(QByteArray data, QByteArray *format, bool opaque, bool *animated) {
        QByteArray tmpFormat;
		QImage result;
		QBuffer buffer(&data);
        if (!format) {
            format = &tmpFormat;
        }
		{
			QImageReader reader(&buffer, *format);
#ifndef OS_MAC_OLD
			reader.setAutoTransform(true);
#endif // OS_MAC_OLD
			if (animated) *animated = reader.supportsAnimation() && reader.imageCount() > 1;
			QByteArray fmt = reader.format();
			if (!fmt.isEmpty()) *format = fmt;
			if (!reader.read(&result)) {
				return QImage();
			}
			fmt = reader.format();
			if (!fmt.isEmpty()) *format = fmt;
		}
		buffer.seek(0);
		auto fmt = QString::fromUtf8(*format).toLower();
		if (fmt == "jpg" || fmt == "jpeg") {
#ifdef OS_MAC_OLD
			if (auto exifData = exif_data_new_from_data((const uchar*)(data.constData()), data.size())) {
				auto byteOrder = exif_data_get_byte_order(exifData);
				if (auto exifEntry = exif_data_get_entry(exifData, EXIF_TAG_ORIENTATION)) {
					auto orientationFix = [exifEntry, byteOrder] {
						auto orientation = exif_get_short(exifEntry->data, byteOrder);
						switch (orientation) {
						case 2: return QTransform(-1, 0, 0, 1, 0, 0);
						case 3: return QTransform(-1, 0, 0, -1, 0, 0);
						case 4: return QTransform(1, 0, 0, -1, 0, 0);
						case 5: return QTransform(0, -1, -1, 0, 0, 0);
						case 6: return QTransform(0, 1, -1, 0, 0, 0);
						case 7: return QTransform(0, 1, 1, 0, 0, 0);
						case 8: return QTransform(0, -1, 1, 0, 0, 0);
						}
						return QTransform();
					};
					result = result.transformed(orientationFix());
				}
				exif_data_free(exifData);
			}
#endif // OS_MAC_OLD
		} else if (opaque) {
			result = Images::prepareOpaque(std::move(result));
		}
		return result;
	}

	QImage readImage(const QString &file, QByteArray *format, bool opaque, bool *animated, QByteArray *content) {
		QFile f(file);
		if (f.size() > kImageSizeLimit || !f.open(QIODevice::ReadOnly)) {
			if (animated) *animated = false;
			return QImage();
		}
		auto imageBytes = f.readAll();
		auto result = readImage(imageBytes, format, opaque, animated);
		if (content && !result.isNull()) {
			*content = imageBytes;
		}
		return result;
	}

	QPixmap pixmapFromImageInPlace(QImage &&image) {
		return QPixmap::fromImage(std::move(image), Qt::ColorOnly);
	}

	void regMuted(not_null<PeerData*> peer, TimeMs changeIn) {
		::mutedPeers.insert(peer, true);
		App::main()->updateMutedIn(changeIn);
	}

	void unregMuted(not_null<PeerData*> peer) {
		::mutedPeers.remove(peer);
	}

	void updateMuted() {
		auto changeInMin = TimeMs(0);
		for (auto i = ::mutedPeers.begin(); i != ::mutedPeers.end();) {
			const auto history = App::historyLoaded(i.key()->id);
			const auto muteFinishesIn = i.key()->notifyMuteFinishesIn();
			if (muteFinishesIn > 0) {
				if (history) {
					history->changeMute(true);
				}
				if (!changeInMin || muteFinishesIn < changeInMin) {
					changeInMin = muteFinishesIn;
				}
				++i;
			} else {
				if (history) {
					history->changeMute(false);
				}
				i = ::mutedPeers.erase(i);
			}
		}
		if (changeInMin) App::main()->updateMutedIn(changeInMin);
	}

	void setProxySettings(QNetworkAccessManager &manager) {
#ifndef TDESKTOP_DISABLE_NETWORK_PROXY
		manager.setProxy(getHttpProxySettings());
#endif // !TDESKTOP_DISABLE_NETWORK_PROXY
	}

#ifndef TDESKTOP_DISABLE_NETWORK_PROXY
	QNetworkProxy getHttpProxySettings() {
		const ProxyData *proxy = nullptr;
		if (Global::started()) {
			proxy = (Global::ConnectionType() == dbictHttpProxy) ? (&Global::ConnectionProxy()) : nullptr;
		} else {
			proxy = Sandbox::PreLaunchProxy().host.isEmpty() ? nullptr : (&Sandbox::PreLaunchProxy());
		}
		if (proxy) {
			return QNetworkProxy(QNetworkProxy::HttpProxy, proxy->host, proxy->port, proxy->user, proxy->password);
		}
		return QNetworkProxy(QNetworkProxy::DefaultProxy);
	}
#endif // !TDESKTOP_DISABLE_NETWORK_PROXY

	void setProxySettings(QTcpSocket &socket) {
#ifndef TDESKTOP_DISABLE_NETWORK_PROXY
		if (Global::ConnectionType() == dbictTcpProxy) {
			auto &p = Global::ConnectionProxy();
			socket.setProxy(QNetworkProxy(QNetworkProxy::Socks5Proxy, p.host, p.port, p.user, p.password));
		} else {
			socket.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
		}
#endif // !TDESKTOP_DISABLE_NETWORK_PROXY
	}

	void rectWithCorners(Painter &p, QRect rect, const style::color &bg, RoundCorners index, RectParts corners) {
		auto parts = RectPart::Top
			| RectPart::NoTopBottom
			| RectPart::Bottom
			| corners;
		roundRect(p, rect, bg, index, nullptr, parts);
		if ((corners & RectPart::AllCorners) != RectPart::AllCorners) {
			const auto size = ::corners[index].p[0].width() / cIntRetinaFactor();
			if (!(corners & RectPart::TopLeft)) {
				p.fillRect(rect.x(), rect.y(), size, size, bg);
			}
			if (!(corners & RectPart::TopRight)) {
				p.fillRect(rect.x() + rect.width() - size, rect.y(), size, size, bg);
			}
			if (!(corners & RectPart::BottomLeft)) {
				p.fillRect(rect.x(), rect.y() + rect.height() - size, size, size, bg);
			}
			if (!(corners & RectPart::BottomRight)) {
				p.fillRect(rect.x() + rect.width() - size, rect.y() + rect.height() - size, size, size, bg);
			}
		}
	}

	void complexOverlayRect(Painter &p, QRect rect, ImageRoundRadius radius, RectParts corners) {
		if (radius == ImageRoundRadius::Ellipse) {
			PainterHighQualityEnabler hq(p);
			p.setPen(Qt::NoPen);
			p.setBrush(p.textPalette().selectOverlay);
			p.drawEllipse(rect);
		} else {
			auto overlayCorners = (radius == ImageRoundRadius::Small)
				? SelectedOverlaySmallCorners
				: SelectedOverlayLargeCorners;
			const auto bg = p.textPalette().selectOverlay;
			rectWithCorners(p, rect, bg, overlayCorners, corners);
		}
	}

	void complexLocationRect(Painter &p, QRect rect, ImageRoundRadius radius, RectParts corners) {
		rectWithCorners(p, rect, st::msgInBg, MessageInCorners, corners);
	}

	QImage *cornersMask(ImageRoundRadius radius) {
		switch (radius) {
		case ImageRoundRadius::Large: return ::cornersMaskLarge;
		case ImageRoundRadius::Small:
		default: break;
		}
		return ::cornersMaskSmall;
	}

	void roundRect(Painter &p, int32 x, int32 y, int32 w, int32 h, style::color bg, const CornersPixmaps &corner, const style::color *shadow, RectParts parts) {
		auto cornerWidth = corner.p[0].width() / cIntRetinaFactor();
		auto cornerHeight = corner.p[0].height() / cIntRetinaFactor();
		if (w < 2 * cornerWidth || h < 2 * cornerHeight) return;
		if (w > 2 * cornerWidth) {
			if (parts & RectPart::Top) {
				p.fillRect(x + cornerWidth, y, w - 2 * cornerWidth, cornerHeight, bg);
			}
			if (parts & RectPart::Bottom) {
				p.fillRect(x + cornerWidth, y + h - cornerHeight, w - 2 * cornerWidth, cornerHeight, bg);
				if (shadow) {
					p.fillRect(x + cornerWidth, y + h, w - 2 * cornerWidth, st::msgShadow, *shadow);
				}
			}
		}
		if (h > 2 * cornerHeight) {
			if ((parts & RectPart::NoTopBottom) == RectPart::NoTopBottom) {
				p.fillRect(x, y + cornerHeight, w, h - 2 * cornerHeight, bg);
			} else {
				if (parts & RectPart::Left) {
					p.fillRect(x, y + cornerHeight, cornerWidth, h - 2 * cornerHeight, bg);
				}
				if ((parts & RectPart::Center) && w > 2 * cornerWidth) {
					p.fillRect(x + cornerWidth, y + cornerHeight, w - 2 * cornerWidth, h - 2 * cornerHeight, bg);
				}
				if (parts & RectPart::Right) {
					p.fillRect(x + w - cornerWidth, y + cornerHeight, cornerWidth, h - 2 * cornerHeight, bg);
				}
			}
		}
		if (parts & RectPart::TopLeft) {
			p.drawPixmap(x, y, corner.p[0]);
		}
		if (parts & RectPart::TopRight) {
			p.drawPixmap(x + w - cornerWidth, y, corner.p[1]);
		}
		if (parts & RectPart::BottomLeft) {
			p.drawPixmap(x, y + h - cornerHeight, corner.p[2]);
		}
		if (parts & RectPart::BottomRight) {
			p.drawPixmap(x + w - cornerWidth, y + h - cornerHeight, corner.p[3]);
		}
	}

	void roundRect(Painter &p, int32 x, int32 y, int32 w, int32 h, style::color bg, RoundCorners index, const style::color *shadow, RectParts parts) {
		roundRect(p, x, y, w, h, bg, ::corners[index], shadow, parts);
	}

	void roundShadow(Painter &p, int32 x, int32 y, int32 w, int32 h, style::color shadow, RoundCorners index, RectParts parts) {
		auto &corner = ::corners[index];
		auto cornerWidth = corner.p[0].width() / cIntRetinaFactor();
		auto cornerHeight = corner.p[0].height() / cIntRetinaFactor();
		if (parts & RectPart::Bottom) {
			p.fillRect(x + cornerWidth, y + h, w - 2 * cornerWidth, st::msgShadow, shadow);
		}
		if (parts & RectPart::BottomLeft) {
			p.fillRect(x, y + h - cornerHeight, cornerWidth, st::msgShadow, shadow);
			p.drawPixmap(x, y + h - cornerHeight + st::msgShadow, corner.p[2]);
		}
		if (parts & RectPart::BottomRight) {
			p.fillRect(x + w - cornerWidth, y + h - cornerHeight, cornerWidth, st::msgShadow, shadow);
			p.drawPixmap(x + w - cornerWidth, y + h - cornerHeight + st::msgShadow, corner.p[3]);
		}
	}

	void roundRect(Painter &p, int32 x, int32 y, int32 w, int32 h, style::color bg, ImageRoundRadius radius, RectParts parts) {
		auto colorKey = ((uint32(bg->c.alpha()) & 0xFF) << 24) | ((uint32(bg->c.red()) & 0xFF) << 16) | ((uint32(bg->c.green()) & 0xFF) << 8) | ((uint32(bg->c.blue()) & 0xFF) << 24);
		auto i = cornersMap.find(colorKey);
		if (i == cornersMap.cend()) {
			QImage images[4];
			switch (radius) {
			case ImageRoundRadius::Small: prepareCorners(SmallMaskCorners, st::buttonRadius, bg, nullptr, images); break;
			case ImageRoundRadius::Large: prepareCorners(LargeMaskCorners, st::historyMessageRadius, bg, nullptr, images); break;
			default: p.fillRect(x, y, w, h, bg); return;
			}

			CornersPixmaps pixmaps;
			for (int j = 0; j < 4; ++j) {
				pixmaps.p[j] = pixmapFromImageInPlace(std::move(images[j]));
				pixmaps.p[j].setDevicePixelRatio(cRetinaFactor());
			}
			i = cornersMap.insert(colorKey, pixmaps);
		}
		roundRect(p, x, y, w, h, bg, i.value(), nullptr, parts);
	}

	WallPapers gServerBackgrounds;

}
