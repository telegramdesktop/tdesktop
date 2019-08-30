/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_session.h"

#include "observer_peer.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "mainwidget.h"
#include "core/application.h"
#include "core/crash_reports.h" // CrashReports::SetAnnotation
#include "ui/image/image.h"
#include "ui/image/image_source.h" // Images::LocalFileSource
#include "export/export_controller.h"
#include "export/view/export_view_panel_controller.h"
#include "window/notifications_manager.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "history/view/media/history_view_media.h"
#include "history/view/history_view_element.h"
#include "inline_bots/inline_bot_layout_item.h"
#include "storage/localstorage.h"
#include "storage/storage_encrypted_file.h"
#include "main/main_account.h"
#include "media/player/media_player_instance.h" // instance()->play()
#include "media/streaming/media_streaming_loader.h" // unique_ptr<Loader>
#include "media/streaming/media_streaming_reader.h" // make_shared<Reader>
#include "boxes/abstract_box.h"
#include "platform/platform_info.h"
#include "passport/passport_form_controller.h"
#include "window/themes/window_theme.h"
#include "lang/lang_keys.h" // tr::lng_deleted(tr::now) in user name
#include "data/data_media_types.h"
#include "data/data_folder.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_file_origin.h"
#include "data/data_photo.h"
#include "data/data_document.h"
#include "data/data_web_page.h"
#include "data/data_game.h"
#include "data/data_poll.h"
#include "data/data_scheduled_messages.h"
#include "base/unixtime.h"
#include "styles/style_boxes.h" // st::backgroundSize

namespace Data {
namespace {

constexpr auto kMaxNotifyCheckDelay = 24 * 3600 * crl::time(1000);
constexpr auto kMaxWallpaperSize = 10 * 1024 * 1024;

using ViewElement = HistoryView::Element;

// s: box 100x100
// m: box 320x320
// x: box 800x800
// y: box 1280x1280
// w: box 2560x2560 // if loading this fix HistoryPhoto::updateFrom
// a: crop 160x160
// b: crop 320x320
// c: crop 640x640
// d: crop 1280x1280
const auto InlineLevels = QByteArray::fromRawData("i", 1);
const auto SmallLevels = QByteArray::fromRawData("sambcxydwi", 10);
const auto ThumbnailLevels = QByteArray::fromRawData("mbcxasydwi", 10);
const auto LargeLevels = QByteArray::fromRawData("yxwmsdcbai", 10);

void CheckForSwitchInlineButton(not_null<HistoryItem*> item) {
	if (item->out() || !item->hasSwitchInlineButton()) {
		return;
	}
	if (const auto user = item->history()->peer->asUser()) {
		if (!user->isBot() || !user->botInfo->inlineReturnPeerId) {
			return;
		}
		if (const auto markup = item->Get<HistoryMessageReplyMarkup>()) {
			for (const auto &row : markup->rows) {
				for (const auto &button : row) {
					using ButtonType = HistoryMessageMarkupButton::Type;
					if (button.type == ButtonType::SwitchInline) {
						Notify::switchInlineBotButtonReceived(
							QString::fromUtf8(button.data));
						return;
					}
				}
			}
		}
	}
}

// We should get a full restriction in "{full}: {reason}" format and we
// need to find an "-all" tag in {full}, otherwise ignore this restriction.
QString ExtractUnavailableReason(
		const QVector<MTPRestrictionReason> &restrictions) {
	auto &&texts = ranges::view::all(
		restrictions
	) | ranges::view::transform([](const MTPRestrictionReason &restriction) {
		return restriction.match([&](const MTPDrestrictionReason &data) {
			const auto platform = qs(data.vplatform());
			return (false
#ifdef OS_MAC_STORE
				|| (platform == qstr("ios"))
#elif defined OS_WIN_STORE // OS_MAC_STORE
				|| (platform == qstr("ms"))
#endif // OS_MAC_STORE || OS_WIN_STORE
				|| (platform == qstr("all")))
				? std::make_optional(qs(data.vtext()))
				: std::nullopt;
		});
	}) | ranges::view::filter([](const std::optional<QString> &value) {
		return value.has_value();
	}) | ranges::view::transform([](const std::optional<QString> &value) {
		return *value;
	});
	const auto begin = texts.begin();
	return (begin != texts.end()) ? *begin : nullptr;
}

MTPPhotoSize FindDocumentInlineThumbnail(const MTPDdocument &data) {
	const auto thumbs = data.vthumbs();
	if (!thumbs) {
		return MTP_photoSizeEmpty(MTP_string());
	}
	const auto &list = thumbs->v;
	const auto i = ranges::find(
		list,
		mtpc_photoStrippedSize,
		&MTPPhotoSize::type);
	return (i != list.end())
		? (*i)
		: MTPPhotoSize(MTP_photoSizeEmpty(MTP_string()));
}

MTPPhotoSize FindDocumentThumbnail(const MTPDdocument &data) {
	const auto area = [](const MTPPhotoSize &size) {
		static constexpr auto kInvalid = 0;
		return size.match([](const MTPDphotoSizeEmpty &) {
			return kInvalid;
		}, [](const MTPDphotoStrippedSize &) {
			return kInvalid;
		}, [](const auto &data) {
			return (data.vw().v * data.vh().v);
		});
	};
	const auto thumbs = data.vthumbs();
	if (!thumbs) {
		return MTP_photoSizeEmpty(MTP_string());
	}
	const auto &list = thumbs->v;
	const auto i = ranges::max_element(list, std::less<>(), area);
	return (i != list.end() && area(*i) > 0)
		? (*i)
		: MTPPhotoSize(MTP_photoSizeEmpty(MTP_string()));
}

rpl::producer<int> PinnedDialogsCountMaxValue(
		not_null<Main::Session*> session) {
	return rpl::single(
		rpl::empty_value()
	) | rpl::then(
		session->account().configUpdates()
	) | rpl::map([=] {
		return Global::PinnedDialogsCountMax();
	});
}

bool PruneDestroyedAndSet(
		base::flat_map<
			not_null<DocumentData*>,
			std::weak_ptr<::Media::Streaming::Reader>> &readers,
		not_null<DocumentData*> document,
		const std::shared_ptr<::Media::Streaming::Reader> &reader) {
	auto result = false;
	for (auto i = begin(readers); i != end(readers);) {
		if (i->first == document) {
			(i++)->second = reader;
			result = true;
		} else if (i->second.lock() != nullptr) {
			++i;
		} else {
			i = readers.erase(i);
		}
	}
	return result;
}

} // namespace

Session::Session(not_null<Main::Session*> session)
: _session(session)
, _cache(Core::App().databases().get(
	Local::cachePath(),
	Local::cacheSettings()))
, _bigFileCache(Core::App().databases().get(
	Local::cacheBigFilePath(),
	Local::cacheBigFileSettings()))
, _chatsList(PinnedDialogsCountMaxValue(session))
, _contactsList(Dialogs::SortMode::Name)
, _contactsNoChatsList(Dialogs::SortMode::Name)
, _selfDestructTimer([=] { checkSelfDestructItems(); })
, _sendActionsAnimation([=](crl::time now) {
	return sendActionsAnimationCallback(now);
})
, _unmuteByFinishedTimer([=] { unmuteByFinished(); })
, _groups(this)
, _scheduledMessages(std::make_unique<ScheduledMessages>(this)) {
	_cache->open(Local::cacheKey());
	_bigFileCache->open(Local::cacheBigFileKey());

	if constexpr (Platform::IsLinux()) {
		const auto wasVersion = Local::oldMapVersion();
		if (wasVersion >= 1007011 && wasVersion < 1007015) {
			_bigFileCache->clear();
			_cache->clearByTag(Data::kImageCacheTag);
		}
	}

	setupContactViewsViewer();
	setupChannelLeavingViewer();
	setupPeerNameViewer();
	setupUserIsContactViewer();
}

void Session::clear() {
	_sendActions.clear();

	for (const auto &[peerId, history] : _histories) {
		history->clear(History::ClearType::Unload);
	}
	_scheduledMessages = nullptr;
	_dependentMessages.clear();
	base::take(_messages);
	base::take(_channelMessages);
	_messageByRandomId.clear();
	_sentMessagesData.clear();
	cSetRecentInlineBots(RecentInlineBots());
	cSetRecentStickers(RecentStickerPack());
	App::clearMousedItems();
	_histories.clear();
}

not_null<PeerData*> Session::peer(PeerId id) {
	const auto i = _peers.find(id);
	if (i != _peers.cend()) {
		return i->second.get();
	}
	auto result = [&]() -> std::unique_ptr<PeerData> {
		if (peerIsUser(id)) {
			return std::make_unique<UserData>(this, id);
		} else if (peerIsChat(id)) {
			return std::make_unique<ChatData>(this, id);
		} else if (peerIsChannel(id)) {
			return std::make_unique<ChannelData>(this, id);
		}
		Unexpected("Peer id type.");
	}();

	result->input = MTPinputPeer(MTP_inputPeerEmpty());
	return _peers.emplace(id, std::move(result)).first->second.get();
}

not_null<UserData*> Session::user(UserId id) {
	return peer(peerFromUser(id))->asUser();
}

not_null<ChatData*> Session::chat(ChatId id) {
	return peer(peerFromChat(id))->asChat();
}

not_null<ChannelData*> Session::channel(ChannelId id) {
	return peer(peerFromChannel(id))->asChannel();
}

PeerData *Session::peerLoaded(PeerId id) const {
	const auto i = _peers.find(id);
	if (i == end(_peers)) {
		return nullptr;
	} else if (i->second->loadedStatus != PeerData::FullLoaded) {
		return nullptr;
	}
	return i->second.get();
}

UserData *Session::userLoaded(UserId id) const {
	if (const auto peer = peerLoaded(peerFromUser(id))) {
		return peer->asUser();
	}
	return nullptr;
}

ChatData *Session::chatLoaded(ChatId id) const {
	if (const auto peer = peerLoaded(peerFromChat(id))) {
		return peer->asChat();
	}
	return nullptr;
}

ChannelData *Session::channelLoaded(ChannelId id) const {
	if (const auto peer = peerLoaded(peerFromChannel(id))) {
		return peer->asChannel();
	}
	return nullptr;
}

not_null<UserData*> Session::processUser(const MTPUser &data) {
	const auto result = user(data.match([](const auto &data) {
		return data.vid().v;
	}));
	auto minimal = false;
	const MTPUserStatus *status = nullptr;
	const MTPUserStatus emptyStatus = MTP_userStatusEmpty();

	Notify::PeerUpdate update;
	using UpdateFlag = Notify::PeerUpdate::Flag;
	data.match([&](const MTPDuserEmpty &data) {
		const auto canShareThisContact = result->canShareThisContactFast();

		result->input = MTP_inputPeerUser(data.vid(), MTP_long(0));
		result->inputUser = MTP_inputUser(data.vid(), MTP_long(0));
		result->setName(tr::lng_deleted(tr::now), QString(), QString(), QString());
		result->setPhoto(MTP_userProfilePhotoEmpty());
		//result->setFlags(MTPDuser_ClientFlag::f_inaccessible | 0);
		result->setFlags(MTPDuser::Flag::f_deleted);
		if (!result->phone().isEmpty()) {
			result->setPhone(QString());
			update.flags |= UpdateFlag::UserPhoneChanged;
		}
		result->setBotInfoVersion(-1);
		status = &emptyStatus;
		result->setIsContact(false);
		if (canShareThisContact != result->canShareThisContactFast()) {
			update.flags |= UpdateFlag::UserCanShareContact;
		}
	}, [&](const MTPDuser &data) {
		minimal = data.is_min();

		const auto canShareThisContact = result->canShareThisContactFast();
		if (minimal) {
			const auto mask = 0
				//| MTPDuser_ClientFlag::f_inaccessible
				| MTPDuser::Flag::f_deleted;
			result->setFlags((result->flags() & ~mask) | (data.vflags().v & mask));
		} else {
			result->setFlags(data.vflags().v);
			if (data.is_self()) {
				result->input = MTP_inputPeerSelf();
				result->inputUser = MTP_inputUserSelf();
			} else if (const auto accessHash = data.vaccess_hash()) {
				result->input = MTP_inputPeerUser(data.vid(), *accessHash);
				result->inputUser = MTP_inputUser(data.vid(), *accessHash);
			} else {
				result->input = MTP_inputPeerUser(data.vid(), MTP_long(result->accessHash()));
				result->inputUser = MTP_inputUser(data.vid(), MTP_long(result->accessHash()));
			}
			if (const auto restriction = data.vrestriction_reason()) {
				result->setUnavailableReason(
					ExtractUnavailableReason(restriction->v));
			} else {
				result->setUnavailableReason(QString());
			}
		}
		if (data.is_deleted()) {
			if (!result->phone().isEmpty()) {
				result->setPhone(QString());
				update.flags |= UpdateFlag::UserPhoneChanged;
			}
			result->setName(tr::lng_deleted(tr::now), QString(), QString(), QString());
			result->setPhoto(MTP_userProfilePhotoEmpty());
			status = &emptyStatus;
		} else {
			// apply first_name and last_name from minimal user only if we don't have
			// local values for first name and last name already, otherwise skip
			bool noLocalName = result->firstName.isEmpty() && result->lastName.isEmpty();
			QString fname = (!minimal || noLocalName) ? TextUtilities::SingleLine(qs(data.vfirst_name().value_or_empty())) : result->firstName;
			QString lname = (!minimal || noLocalName) ? TextUtilities::SingleLine(qs(data.vlast_name().value_or_empty())) : result->lastName;

			QString phone = minimal ? result->phone() : qs(data.vphone().value_or_empty());
			QString uname = minimal ? result->username : TextUtilities::SingleLine(qs(data.vusername().value_or_empty()));

			const auto phoneChanged = (result->phone() != phone);
			if (phoneChanged) {
				result->setPhone(phone);
				update.flags |= UpdateFlag::UserPhoneChanged;
			}
			const auto nameChanged = (result->firstName != fname)
				|| (result->lastName != lname);

			auto showPhone = !result->isServiceUser()
				&& !data.is_support()
				&& !data.is_self()
				&& !data.is_contact()
				&& !data.is_mutual_contact();
			auto showPhoneChanged = !result->isServiceUser()
				&& !data.is_self()
				&& ((showPhone && result->isContact())
					|| (!showPhone
						&& !result->isContact()
						&& !result->phone().isEmpty()));
			if (minimal) {
				showPhoneChanged = false;
				showPhone = !result->isServiceUser()
					&& !result->isContact()
					&& !result->phone().isEmpty()
					&& (result->id != _session->userPeerId());
			}

			// see also Local::readPeer

			const auto pname = (showPhoneChanged || phoneChanged || nameChanged)
				? ((showPhone && !phone.isEmpty())
					? App::formatPhone(phone)
					: QString())
				: result->nameOrPhone;

			if (!minimal && data.is_self() && uname != result->username) {
				CrashReports::SetAnnotation("Username", uname);
			}
			result->setName(fname, lname, pname, uname);
			if (const auto photo = data.vphoto()) {
				result->setPhoto(*photo);
			} else {
				result->setPhoto(MTP_userProfilePhotoEmpty());
			}
			if (const auto accessHash = data.vaccess_hash()) {
				result->setAccessHash(accessHash->v);
			}
			status = data.vstatus();
		}
		if (!minimal) {
			if (const auto botInfoVersion = data.vbot_info_version()) {
				result->setBotInfoVersion(botInfoVersion->v);
				result->botInfo->readsAllHistory = data.is_bot_chat_history();
				if (result->botInfo->cantJoinGroups != data.is_bot_nochats()) {
					result->botInfo->cantJoinGroups = data.is_bot_nochats();
					update.flags |= UpdateFlag::BotCanAddToGroups;
				}
				if (const auto placeholder = data.vbot_inline_placeholder()) {
					result->botInfo->inlinePlaceholder = '_' + qs(*placeholder);
				} else {
					result->botInfo->inlinePlaceholder = QString();
				}
			} else {
				result->setBotInfoVersion(-1);
			}
			result->setIsContact(data.is_contact()
				|| data.is_mutual_contact());
		}

		if (canShareThisContact != result->canShareThisContactFast()) {
			update.flags |= UpdateFlag::UserCanShareContact;
		}
	});

	if (minimal) {
		if (result->loadedStatus == PeerData::NotLoaded) {
			result->loadedStatus = PeerData::MinimalLoaded;
		}
	} else if (result->loadedStatus != PeerData::FullLoaded
		&& (!result->isSelf() || !result->phone().isEmpty())) {
		result->loadedStatus = PeerData::FullLoaded;
	}

	if (status && !minimal) {
		const auto oldOnlineTill = result->onlineTill;
		const auto newOnlineTill = ApiWrap::OnlineTillFromStatus(
			*status,
			oldOnlineTill);
		if (oldOnlineTill != newOnlineTill) {
			result->onlineTill = newOnlineTill;
			update.flags |= UpdateFlag::UserOnlineChanged;
		}
	}

	if (App::main()) {
		if (update.flags) {
			update.peer = result;
			Notify::peerUpdatedDelayed(update);
		}
	}
	return result;
}

not_null<PeerData*> Session::processChat(const MTPChat &data) {
	const auto result = data.match([&](const MTPDchat &data) {
		return peer(peerFromChat(data.vid().v));
	}, [&](const MTPDchatForbidden &data) {
		return peer(peerFromChat(data.vid().v));
	}, [&](const MTPDchatEmpty &data) {
		return peer(peerFromChat(data.vid().v));
	}, [&](const MTPDchannel &data) {
		return peer(peerFromChannel(data.vid().v));
	}, [&](const MTPDchannelForbidden &data) {
		return peer(peerFromChannel(data.vid().v));
	});
	auto minimal = false;

	Notify::PeerUpdate update;
	using UpdateFlag = Notify::PeerUpdate::Flag;

	data.match([&](const MTPDchat &data) {
		const auto chat = result->asChat();

		const auto canAddMembers = chat->canAddMembers();
		if (chat->version() < data.vversion().v) {
			chat->setVersion(data.vversion().v);
			chat->invalidateParticipants();
		}

		chat->input = MTP_inputPeerChat(data.vid());
		chat->setName(qs(data.vtitle()));
		chat->setPhoto(data.vphoto());
		chat->date = data.vdate().v;

		if (const auto rights = data.vadmin_rights()) {
			chat->setAdminRights(*rights);
		} else {
			chat->setAdminRights(MTP_chatAdminRights(MTP_flags(0)));
		}
		if (const auto rights = data.vdefault_banned_rights()) {
			chat->setDefaultRestrictions(*rights);
		} else {
			chat->setDefaultRestrictions(
				MTP_chatBannedRights(MTP_flags(0), MTP_int(0)));
		}

		if (const auto migratedTo = data.vmigrated_to()) {
			migratedTo->match([&](const MTPDinputChannel &input) {
				const auto channel = this->channel(input.vchannel_id().v);
				channel->addFlags(MTPDchannel::Flag::f_megagroup);
				if (!channel->access) {
					channel->input = MTP_inputPeerChannel(
						input.vchannel_id(),
						input.vaccess_hash());
					channel->inputChannel = *migratedTo;
					channel->access = input.vaccess_hash().v;
				}
				ApplyMigration(chat, channel);
			}, [](const MTPDinputChannelFromMessage &) {
				LOG(("API Error: "
					"migrated_to contains channel from message."));
			}, [](const MTPDinputChannelEmpty &) {
			});
		}

		chat->setFlags(data.vflags().v);
		chat->count = data.vparticipants_count().v;

		if (canAddMembers != chat->canAddMembers()) {
			update.flags |= UpdateFlag::RightsChanged;
		}
	}, [&](const MTPDchatForbidden &data) {
		const auto chat = result->asChat();

		const auto canAddMembers = chat->canAddMembers();

		chat->input = MTP_inputPeerChat(data.vid());
		chat->setName(qs(data.vtitle()));
		chat->setPhoto(MTP_chatPhotoEmpty());
		chat->date = 0;
		chat->count = -1;
		chat->invalidateParticipants();
		chat->setFlags(MTPDchat_ClientFlag::f_forbidden | 0);
		chat->setAdminRights(MTP_chatAdminRights(MTP_flags(0)));
		chat->setDefaultRestrictions(
			MTP_chatBannedRights(MTP_flags(0), MTP_int(0)));

		if (canAddMembers != chat->canAddMembers()) {
			update.flags |= UpdateFlag::RightsChanged;
		}
	}, [&](const MTPDchannel &data) {
		const auto channel = result->asChannel();

		minimal = data.is_min();
		if (minimal) {
			if (result->loadedStatus != PeerData::FullLoaded) {
				LOG(("API Warning: not loaded minimal channel applied."));
			}
		} else {
			channel->input = MTP_inputPeerChannel(
				data.vid(),
				MTP_long(data.vaccess_hash().value_or_empty()));
		}

		const auto wasInChannel = channel->amIn();
		const auto canViewAdmins = channel->canViewAdmins();
		const auto canViewMembers = channel->canViewMembers();
		const auto canAddMembers = channel->canAddMembers();

		if (const auto count = data.vparticipants_count()) {
			channel->setMembersCount(count->v);
		}
		if (const auto rights = data.vdefault_banned_rights()) {
			channel->setDefaultRestrictions(*rights);
		} else {
			channel->setDefaultRestrictions(
				MTP_chatBannedRights(MTP_flags(0), MTP_int(0)));
		}
		if (minimal) {
			auto mask = 0
				| MTPDchannel::Flag::f_broadcast
				| MTPDchannel::Flag::f_verified
				| MTPDchannel::Flag::f_megagroup
				| MTPDchannel_ClientFlag::f_forbidden;
			channel->setFlags((channel->flags() & ~mask) | (data.vflags().v & mask));
		} else {
			if (const auto rights = data.vadmin_rights()) {
				channel->setAdminRights(*rights);
			} else if (channel->hasAdminRights()) {
				channel->setAdminRights(MTP_chatAdminRights(MTP_flags(0)));
			}
			if (const auto rights = data.vbanned_rights()) {
				channel->setRestrictions(*rights);
			} else if (channel->hasRestrictions()) {
				channel->setRestrictions(
					MTP_chatBannedRights(MTP_flags(0), MTP_int(0)));
			}
			const auto hash = data.vaccess_hash().value_or(channel->access);
			channel->inputChannel = MTP_inputChannel(
				data.vid(),
				MTP_long(hash));
			channel->access = hash;
			channel->date = data.vdate().v;
			if (channel->version() < data.vversion().v) {
				channel->setVersion(data.vversion().v);
			}
			if (const auto restriction = data.vrestriction_reason()) {
				channel->setUnavailableReason(
					ExtractUnavailableReason(restriction->v));
			} else {
				channel->setUnavailableReason(QString());
			}
			channel->setFlags(data.vflags().v);
			//if (const auto feedId = data.vfeed_id()) { // #feed
			//	channel->setFeed(feed(feedId->v));
			//} else {
			//	channel->clearFeed();
			//}
		}

		channel->setName(
			qs(data.vtitle()),
			TextUtilities::SingleLine(qs(data.vusername().value_or_empty())));

		channel->setPhoto(data.vphoto());

		if (wasInChannel != channel->amIn()) {
			update.flags |= UpdateFlag::ChannelAmIn;
		}
		if (canViewAdmins != channel->canViewAdmins()
			|| canViewMembers != channel->canViewMembers()
			|| canAddMembers != channel->canAddMembers()) {
			update.flags |= UpdateFlag::RightsChanged;
		}
	}, [&](const MTPDchannelForbidden &data) {
		const auto channel = result->asChannel();
		channel->input = MTP_inputPeerChannel(data.vid(), data.vaccess_hash());

		auto wasInChannel = channel->amIn();
		auto canViewAdmins = channel->canViewAdmins();
		auto canViewMembers = channel->canViewMembers();
		auto canAddMembers = channel->canAddMembers();

		channel->inputChannel = MTP_inputChannel(data.vid(), data.vaccess_hash());

		auto mask = mtpCastFlags(MTPDchannelForbidden::Flag::f_broadcast | MTPDchannelForbidden::Flag::f_megagroup);
		channel->setFlags((channel->flags() & ~mask) | (mtpCastFlags(data.vflags()) & mask) | MTPDchannel_ClientFlag::f_forbidden);

		if (channel->hasAdminRights()) {
			channel->setAdminRights(MTP_chatAdminRights(MTP_flags(0)));
		}
		if (channel->hasRestrictions()) {
			channel->setRestrictions(MTP_chatBannedRights(MTP_flags(0), MTP_int(0)));
		}

		channel->setName(qs(data.vtitle()), QString());

		channel->access = data.vaccess_hash().v;
		channel->setPhoto(MTP_chatPhotoEmpty());
		channel->date = 0;
		channel->setMembersCount(0);

		if (wasInChannel != channel->amIn()) {
			update.flags |= UpdateFlag::ChannelAmIn;
		}
		if (canViewAdmins != channel->canViewAdmins()
			|| canViewMembers != channel->canViewMembers()
			|| canAddMembers != channel->canAddMembers()) {
			update.flags |= UpdateFlag::RightsChanged;
		}
	}, [](const MTPDchatEmpty &) {
	});

	if (minimal) {
		if (result->loadedStatus == PeerData::NotLoaded) {
			result->loadedStatus = PeerData::MinimalLoaded;
		}
	} else if (result->loadedStatus != PeerData::FullLoaded) {
		result->loadedStatus = PeerData::FullLoaded;
	}
	if (update.flags) {
		update.peer = result;
		Notify::peerUpdatedDelayed(update);
	}
	return result;
}

UserData *Session::processUsers(const MTPVector<MTPUser> &data) {
	auto result = (UserData*)nullptr;
	for (const auto &user : data.v) {
		result = processUser(user);
	}
	return result;
}

PeerData *Session::processChats(const MTPVector<MTPChat> &data) {
	auto result = (PeerData*)nullptr;
	for (const auto &chat : data.v) {
		result = processChat(chat);
	}
	return result;
}

void Session::applyMaximumChatVersions(const MTPVector<MTPChat> &data) {
	for (const auto &chat : data.v) {
		chat.match([&](const MTPDchat &data) {
			if (const auto chat = chatLoaded(data.vid().v)) {
				if (data.vversion().v < chat->version()) {
					chat->setVersion(data.vversion().v);
				}
			}
		}, [&](const MTPDchannel &data) {
			if (const auto channel = channelLoaded(data.vid().v)) {
				if (data.vversion().v < channel->version()) {
					channel->setVersion(data.vversion().v);
				}
			}
		}, [](const auto &) {
		});
	}
}

PeerData *Session::peerByUsername(const QString &username) const {
	const auto uname = username.trimmed();
	for (const auto &[peerId, peer] : _peers) {
		if (!peer->userName().compare(uname, Qt::CaseInsensitive)) {
			return peer.get();
		}
	}
	return nullptr;
}

void Session::enumerateUsers(Fn<void(not_null<UserData*>)> action) const {
	for (const auto &[peerId, peer] : _peers) {
		if (const auto user = peer->asUser()) {
			action(user);
		}
	}
}

void Session::enumerateGroups(Fn<void(not_null<PeerData*>)> action) const {
	for (const auto &[peerId, peer] : _peers) {
		if (peer->isChat() || peer->isMegagroup()) {
			action(peer.get());
		}
	}
}

void Session::enumerateChannels(
		Fn<void(not_null<ChannelData*>)> action) const {
	for (const auto &[peerId, peer] : _peers) {
		if (const auto channel = peer->asChannel()) {
			if (!channel->isMegagroup()) {
				action(channel);
			}
		}
	}
}

not_null<History*> Session::history(PeerId peerId) {
	Expects(peerId != 0);

	if (const auto result = historyLoaded(peerId)) {
		return result;
	}
	const auto [i, ok] = _histories.emplace(
		peerId,
		std::make_unique<History>(this, peerId));
	return i->second.get();
}

History *Session::historyLoaded(PeerId peerId) const {
	const auto i = peerId ? _histories.find(peerId) : end(_histories);
	return (i != end(_histories)) ? i->second.get() : nullptr;
}

not_null<History*> Session::history(not_null<const PeerData*> peer) {
	return history(peer->id);
}

History *Session::historyLoaded(const PeerData *peer) {
	return peer ? historyLoaded(peer->id) : nullptr;
}

void Session::deleteConversationLocally(not_null<PeerData*> peer) {
	const auto history = historyLoaded(peer);
	if (history) {
		if (history->folderKnown()) {
			setChatPinned(history, false);
		}
		App::main()->removeDialog(history);
		history->clear(peer->isChannel()
			? History::ClearType::Unload
			: History::ClearType::DeleteChat);
	}
	if (const auto channel = peer->asMegagroup()) {
		channel->addFlags(MTPDchannel::Flag::f_left);
		if (const auto from = channel->getMigrateFromChat()) {
			if (const auto migrated = historyLoaded(from)) {
				migrated->updateChatListExistence();
			}
		}
	}
}

void Session::registerSendAction(
		not_null<History*> history,
		not_null<UserData*> user,
		const MTPSendMessageAction &action,
		TimeId when) {
	if (history->updateSendActionNeedsAnimating(user, action)) {
		user->madeAction(when);

		const auto i = _sendActions.find(history);
		if (!_sendActions.contains(history)) {
			_sendActions.emplace(history, crl::now());
			_sendActionsAnimation.start();
		}
	}
}

bool Session::sendActionsAnimationCallback(crl::time now) {
	for (auto i = begin(_sendActions); i != end(_sendActions);) {
		if (i->first->updateSendActionNeedsAnimating(now)) {
			++i;
		} else {
			i = _sendActions.erase(i);
		}
	}
	return !_sendActions.empty();
}

bool Session::chatsListLoaded(Data::Folder *folder) {
	return chatsList(folder)->loaded();
}

void Session::chatsListChanged(FolderId folderId) {
	chatsListChanged(folderId ? folder(folderId).get() : nullptr);
}

void Session::chatsListChanged(Data::Folder *folder) {
	_chatsListChanged.fire_copy(folder);
}

void Session::chatsListDone(Data::Folder *folder) {
	if (folder) {
		folder->setChatsListLoaded();
	} else {
		_chatsList.setLoaded();
	}
	_chatsListLoadedEvents.fire_copy(folder);
}

Storage::Cache::Database &Session::cache() {
	return *_cache;
}

Storage::Cache::Database &Session::cacheBigFile() {
	return *_bigFileCache;
}

void Session::startExport(PeerData *peer) {
	startExport(peer ? peer->input : MTP_inputPeerEmpty());
}

void Session::startExport(const MTPInputPeer &singlePeer) {
	if (_exportPanel) {
		_exportPanel->activatePanel();
		return;
	}
	_export = std::make_unique<Export::Controller>(singlePeer);
	_exportPanel = std::make_unique<Export::View::PanelController>(
		_export.get());

	_exportViewChanges.fire(_exportPanel.get());

	_exportPanel->stopRequests(
	) | rpl::start_with_next([=] {
		LOG(("Export Info: Stop requested."));
		stopExport();
	}, _export->lifetime());
}

void Session::suggestStartExport(TimeId availableAt) {
	_exportAvailableAt = availableAt;
	suggestStartExport();
}

void Session::clearExportSuggestion() {
	_exportAvailableAt = 0;
	if (_exportSuggestion) {
		_exportSuggestion->closeBox();
	}
}

void Session::suggestStartExport() {
	if (_exportAvailableAt <= 0) {
		return;
	}

	const auto now = base::unixtime::now();
	const auto left = (_exportAvailableAt <= now)
		? 0
		: (_exportAvailableAt - now);
	if (left) {
		App::CallDelayed(
			std::min(left + 5, 3600) * crl::time(1000),
			_session,
			[=] { suggestStartExport(); });
	} else if (_export) {
		Export::View::ClearSuggestStart();
	} else {
		_exportSuggestion = Export::View::SuggestStart();
	}
}

rpl::producer<Export::View::PanelController*> Session::currentExportView(
) const {
	return _exportViewChanges.events_starting_with(_exportPanel.get());
}

bool Session::exportInProgress() const {
	return _export != nullptr;
}

void Session::stopExportWithConfirmation(FnMut<void()> callback) {
	if (!_exportPanel) {
		callback();
		return;
	}
	auto closeAndCall = [=, callback = std::move(callback)]() mutable {
		auto saved = std::move(callback);
		LOG(("Export Info: Stop With Confirmation."));
		stopExport();
		if (saved) {
			saved();
		}
	};
	_exportPanel->stopWithConfirmation(std::move(closeAndCall));
}

void Session::stopExport() {
	if (_exportPanel) {
		LOG(("Export Info: Destroying."));
		_exportPanel = nullptr;
		_exportViewChanges.fire(nullptr);
	}
	_export = nullptr;
}

const Passport::SavedCredentials *Session::passportCredentials() const {
	return _passportCredentials ? &_passportCredentials->first : nullptr;
}

void Session::rememberPassportCredentials(
		Passport::SavedCredentials data,
		crl::time rememberFor) {
	Expects(rememberFor > 0);

	static auto generation = 0;
	_passportCredentials = std::make_unique<CredentialsWithGeneration>(
		std::move(data),
		++generation);
	App::CallDelayed(rememberFor, _session, [=, check = generation] {
		if (_passportCredentials && _passportCredentials->second == check) {
			forgetPassportCredentials();
		}
	});
}

void Session::forgetPassportCredentials() {
	_passportCredentials = nullptr;
}

void Session::setupContactViewsViewer() {
	Notify::PeerUpdateViewer(
		Notify::PeerUpdate::Flag::UserIsContact
	) | rpl::map([](const Notify::PeerUpdate &update) {
		return update.peer->asUser();
	}) | rpl::filter([](UserData *user) {
		return user != nullptr;
	}) | rpl::start_with_next([=](not_null<UserData*> user) {
		userIsContactUpdated(user);
	}, _lifetime);
}

void Session::setupChannelLeavingViewer() {
	Notify::PeerUpdateViewer(
		Notify::PeerUpdate::Flag::ChannelAmIn
	) | rpl::map([](const Notify::PeerUpdate &update) {
		return update.peer->asChannel();
	}) | rpl::filter([](ChannelData *channel) {
		return (channel != nullptr)
			&& !(channel->amIn());
	}) | rpl::start_with_next([=](not_null<ChannelData*> channel) {
//		channel->clearFeed(); // #feed
		if (const auto history = historyLoaded(channel->id)) {
			history->removeJoinedMessage();
			history->updateChatListExistence();
			history->updateChatListSortPosition();
		}
	}, _lifetime);
}

void Session::setupPeerNameViewer() {
	Notify::PeerUpdateViewer(
		Notify::PeerUpdate::Flag::NameChanged
	) | rpl::start_with_next([=](const Notify::PeerUpdate &update) {
		const auto peer = update.peer;
		const auto &oldLetters = update.oldNameFirstLetters;
		_contactsNoChatsList.peerNameChanged(peer, oldLetters);
		_contactsList.peerNameChanged(peer, oldLetters);
	}, _lifetime);
}

void Session::setupUserIsContactViewer() {
	Notify::PeerUpdateViewer(
		Notify::PeerUpdate::Flag::UserIsContact
	) | rpl::filter([=](const Notify::PeerUpdate &update) {
		return update.peer->isUser();
	}) | rpl::start_with_next([=](const Notify::PeerUpdate &update) {
		const auto user = update.peer->asUser();
		if (user->loadedStatus != PeerData::FullLoaded) {
			LOG(("API Error: "
				"userIsContactChanged() called for a not loaded user!"));
			return;
		}
		if (user->isContact()) {
			const auto history = user->owner().history(user->id);
			_contactsList.addByName(history);
			if (!history->inChatList()) {
				_contactsNoChatsList.addByName(history);
			}
		} else if (const auto history = user->owner().historyLoaded(user)) {
			_contactsNoChatsList.del(history);
			_contactsList.del(history);
		}
	}, _lifetime);
}

Session::~Session() {
	// Optimization: clear notifications before destroying items.
	_session->notifications().clearAllFast();

	clear();
	Images::ClearRemote();
}

template <typename Method>
void Session::enumerateItemViews(
		not_null<const HistoryItem*> item,
		Method method) {
	if (const auto i = _views.find(item); i != _views.end()) {
		for (const auto view : i->second) {
			method(view);
		}
	}
}

void Session::photoLoadSettingsChanged() {
	for (const auto &[id, photo] : _photos) {
		photo->automaticLoadSettingsChanged();
	}
}

void Session::documentLoadSettingsChanged() {
	for (const auto &[id, document] : _documents) {
		document->automaticLoadSettingsChanged();
	}
}

void Session::notifyPhotoLayoutChanged(not_null<const PhotoData*> photo) {
	if (const auto i = _photoItems.find(photo); i != end(_photoItems)) {
		for (const auto item : i->second) {
			notifyItemLayoutChange(item);
		}
	}
}

void Session::notifyDocumentLayoutChanged(
		not_null<const DocumentData*> document) {
	const auto i = _documentItems.find(document);
	if (i != end(_documentItems)) {
		for (const auto item : i->second) {
			notifyItemLayoutChange(item);
		}
	}
	if (const auto items = InlineBots::Layout::documentItems()) {
		if (const auto i = items->find(document); i != items->end()) {
			for (const auto item : i->second) {
				item->layoutChanged();
			}
		}
	}
}

void Session::requestDocumentViewRepaint(
		not_null<const DocumentData*> document) {
	const auto i = _documentItems.find(document);
	if (i != end(_documentItems)) {
		for (const auto item : i->second) {
			requestItemRepaint(item);
		}
	}
}

std::shared_ptr<::Media::Streaming::Reader> Session::documentStreamedReader(
		not_null<DocumentData*> document,
		FileOrigin origin,
		bool forceRemoteLoader) {
	const auto i = _streamedReaders.find(document);
	if (i != end(_streamedReaders)) {
		if (auto result = i->second.lock()) {
			if (!forceRemoteLoader || result->isRemoteLoader()) {
				return result;
			}
		}
	}
	auto loader = document->createStreamingLoader(origin, forceRemoteLoader);
	if (!loader) {
		return nullptr;
	}
	auto result = std::make_shared<::Media::Streaming::Reader>(
		&cacheBigFile(),
		std::move(loader));
	if (!PruneDestroyedAndSet(_streamedReaders, document, result)) {
		_streamedReaders.emplace_or_assign(document, result);
	}
	return result;
}

void Session::requestPollViewRepaint(not_null<const PollData*> poll) {
	if (const auto i = _pollViews.find(poll); i != _pollViews.end()) {
		for (const auto view : i->second) {
			requestViewResize(view);
		}
	}
}

void Session::markMediaRead(not_null<const DocumentData*> document) {
	const auto i = _documentItems.find(document);
	if (i != end(_documentItems)) {
		_session->api().markMediaRead({ begin(i->second), end(i->second) });
	}
}

void Session::notifyItemLayoutChange(not_null<const HistoryItem*> item) {
	_itemLayoutChanges.fire_copy(item);
	enumerateItemViews(item, [&](not_null<ViewElement*> view) {
		notifyViewLayoutChange(view);
	});
}

rpl::producer<not_null<const HistoryItem*>> Session::itemLayoutChanged() const {
	return _itemLayoutChanges.events();
}

void Session::notifyViewLayoutChange(not_null<const ViewElement*> view) {
	_viewLayoutChanges.fire_copy(view);
}

rpl::producer<not_null<const ViewElement*>> Session::viewLayoutChanged() const {
	return _viewLayoutChanges.events();
}

void Session::notifyUnreadItemAdded(not_null<HistoryItem*> item) {
	_unreadItemAdded.fire_copy(item);
}

rpl::producer<not_null<HistoryItem*>> Session::unreadItemAdded() const {
	return _unreadItemAdded.events();
}

void Session::changeMessageId(ChannelId channel, MsgId wasId, MsgId nowId) {
	const auto list = messagesListForInsert(channel);
	auto i = list->find(wasId);
	Assert(i != list->end());
	auto owned = std::move(i->second);
	list->erase(i);
	const auto [j, ok] = list->emplace(nowId, std::move(owned));

	Ensures(ok);
}

void Session::notifyItemIdChange(IdChange event) {
	const auto item = event.item;
	changeMessageId(item->history()->channelId(), event.oldId, item->id);

	_itemIdChanges.fire_copy(event);

	const auto refreshViewDataId = [](not_null<ViewElement*> view) {
		view->refreshDataId();
	};
	enumerateItemViews(item, refreshViewDataId);
	if (const auto group = groups().find(item)) {
		const auto leader = group->items.back();
		if (leader != item) {
			enumerateItemViews(leader, refreshViewDataId);
		}
	}
}

rpl::producer<Session::IdChange> Session::itemIdChanged() const {
	return _itemIdChanges.events();
}

void Session::requestItemRepaint(not_null<const HistoryItem*> item) {
	_itemRepaintRequest.fire_copy(item);
	enumerateItemViews(item, [&](not_null<const ViewElement*> view) {
		requestViewRepaint(view);
	});
}

rpl::producer<not_null<const HistoryItem*>> Session::itemRepaintRequest() const {
	return _itemRepaintRequest.events();
}

void Session::requestViewRepaint(not_null<const ViewElement*> view) {
	_viewRepaintRequest.fire_copy(view);
}

rpl::producer<not_null<const ViewElement*>> Session::viewRepaintRequest() const {
	return _viewRepaintRequest.events();
}

void Session::requestItemResize(not_null<const HistoryItem*> item) {
	_itemResizeRequest.fire_copy(item);
	enumerateItemViews(item, [&](not_null<ViewElement*> view) {
		requestViewResize(view);
	});
}

rpl::producer<not_null<const HistoryItem*>> Session::itemResizeRequest() const {
	return _itemResizeRequest.events();
}

void Session::requestViewResize(not_null<ViewElement*> view) {
	view->setPendingResize();
	_viewResizeRequest.fire_copy(view);
	notifyViewLayoutChange(view);
}

rpl::producer<not_null<ViewElement*>> Session::viewResizeRequest() const {
	return _viewResizeRequest.events();
}

void Session::requestItemViewRefresh(not_null<HistoryItem*> item) {
	if (const auto view = item->mainView()) {
		view->setPendingResize();
	}
	_itemViewRefreshRequest.fire_copy(item);
}

rpl::producer<not_null<HistoryItem*>> Session::itemViewRefreshRequest() const {
	return _itemViewRefreshRequest.events();
}

void Session::requestItemTextRefresh(not_null<HistoryItem*> item) {
	if (const auto i = _views.find(item); i != _views.end()) {
		for (const auto view : i->second) {
			if (const auto media = view->media()) {
				media->parentTextUpdated();
			}
		}
	}
}

void Session::requestAnimationPlayInline(not_null<HistoryItem*> item) {
	_animationPlayInlineRequest.fire_copy(item);

	if (const auto media = item->media()) {
		if (const auto data = media->document()) {
			if (data && data->isVideoMessage()) {
				const auto msgId = item->fullId();
				::Media::Player::instance()->playPause({ data, msgId });
			}
		}
	}
}

rpl::producer<not_null<HistoryItem*>> Session::animationPlayInlineRequest() const {
	return _animationPlayInlineRequest.events();
}

rpl::producer<not_null<const HistoryItem*>> Session::itemRemoved() const {
	return _itemRemoved.events();
}

void Session::notifyViewRemoved(not_null<const ViewElement*> view) {
	_viewRemoved.fire_copy(view);
}

rpl::producer<not_null<const ViewElement*>> Session::viewRemoved() const {
	return _viewRemoved.events();
}

void Session::notifyHistoryUnloaded(not_null<const History*> history) {
	_historyUnloaded.fire_copy(history);
}

rpl::producer<not_null<const History*>> Session::historyUnloaded() const {
	return _historyUnloaded.events();
}

void Session::notifyHistoryCleared(not_null<const History*> history) {
	_historyCleared.fire_copy(history);
}

rpl::producer<not_null<const History*>> Session::historyCleared() const {
	return _historyCleared.events();
}

void Session::notifyHistoryChangeDelayed(not_null<History*> history) {
	history->setHasPendingResizedItems();
	_historiesChanged.insert(history);
}

rpl::producer<not_null<History*>> Session::historyChanged() const {
	return _historyChanged.events();
}

void Session::sendHistoryChangeNotifications() {
	for (const auto history : base::take(_historiesChanged)) {
		_historyChanged.fire_copy(history);
	}
}

void Session::registerHeavyViewPart(not_null<ViewElement*> view) {
	_heavyViewParts.emplace(view);
}

void Session::unregisterHeavyViewPart(not_null<ViewElement*> view) {
	_heavyViewParts.remove(view);
}

void Session::unloadHeavyViewParts(
		not_null<HistoryView::ElementDelegate*> delegate) {
	if (_heavyViewParts.empty()) {
		return;
	}
	const auto remove = ranges::count(_heavyViewParts, delegate, [](not_null<ViewElement*> element) {
		return element->delegate();
	});
	if (remove == _heavyViewParts.size()) {
		for (const auto view : base::take(_heavyViewParts)) {
			view->unloadHeavyPart();
		}
	} else {
		auto remove = std::vector<not_null<ViewElement*>>();
		for (const auto view : _heavyViewParts) {
			if (view->delegate() == delegate) {
				remove.push_back(view);
			}
		}
		for (const auto view : remove) {
			view->unloadHeavyPart();
		}
	}
}

void Session::unloadHeavyViewParts(
		not_null<HistoryView::ElementDelegate*> delegate,
		int from,
		int till) {
	if (_heavyViewParts.empty()) {
		return;
	}
	auto remove = std::vector<not_null<ViewElement*>>();
	for (const auto view : _heavyViewParts) {
		if (view->delegate() == delegate
			&& !delegate->elementIntersectsRange(view, from, till)) {
			remove.push_back(view);
		}
	}
	for (const auto view : remove) {
		view->unloadHeavyPart();
	}
}

void Session::removeMegagroupParticipant(
		not_null<ChannelData*> channel,
		not_null<UserData*> user) {
	_megagroupParticipantRemoved.fire({ channel, user });
}

auto Session::megagroupParticipantRemoved() const
-> rpl::producer<MegagroupParticipant> {
	return _megagroupParticipantRemoved.events();
}

rpl::producer<not_null<UserData*>> Session::megagroupParticipantRemoved(
		not_null<ChannelData*> channel) const {
	return megagroupParticipantRemoved(
	) | rpl::filter([channel](auto updateChannel, auto user) {
		return (updateChannel == channel);
	}) | rpl::map([](auto updateChannel, auto user) {
		return user;
	});
}

void Session::addNewMegagroupParticipant(
		not_null<ChannelData*> channel,
		not_null<UserData*> user) {
	_megagroupParticipantAdded.fire({ channel, user });
}

auto Session::megagroupParticipantAdded() const
-> rpl::producer<MegagroupParticipant> {
	return _megagroupParticipantAdded.events();
}

rpl::producer<not_null<UserData*>> Session::megagroupParticipantAdded(
		not_null<ChannelData*> channel) const {
	return megagroupParticipantAdded(
	) | rpl::filter([channel](auto updateChannel, auto user) {
		return (updateChannel == channel);
	}) | rpl::map([](auto updateChannel, auto user) {
		return user;
	});
}

void Session::notifyStickersUpdated() {
	_stickersUpdated.fire({});
}

rpl::producer<> Session::stickersUpdated() const {
	return _stickersUpdated.events();
}

void Session::notifyRecentStickersUpdated() {
	_recentStickersUpdated.fire({});
}

rpl::producer<> Session::recentStickersUpdated() const {
	return _recentStickersUpdated.events();
}

void Session::notifySavedGifsUpdated() {
	_savedGifsUpdated.fire({});
}

rpl::producer<> Session::savedGifsUpdated() const {
	return _savedGifsUpdated.events();
}

void Session::notifyPinnedDialogsOrderUpdated() {
	_pinnedDialogsOrderUpdated.fire({});
}

rpl::producer<> Session::pinnedDialogsOrderUpdated() const {
	return _pinnedDialogsOrderUpdated.events();
}

void Session::userIsContactUpdated(not_null<UserData*> user) {
	const auto i = _contactViews.find(peerToUser(user->id));
	if (i != _contactViews.end()) {
		for (const auto view : i->second) {
			requestViewResize(view);
		}
	}
}

HistoryItemsList Session::idsToItems(
		const MessageIdsList &ids) const {
	return ranges::view::all(
		ids
	) | ranges::view::transform([&](const FullMsgId &fullId) {
		return message(fullId);
	}) | ranges::view::filter([](HistoryItem *item) {
		return item != nullptr;
	}) | ranges::view::transform([](HistoryItem *item) {
		return not_null<HistoryItem*>(item);
	}) | ranges::to_vector;
}

MessageIdsList Session::itemsToIds(
		const HistoryItemsList &items) const {
	return ranges::view::all(
		items
	) | ranges::view::transform([](not_null<HistoryItem*> item) {
		return item->fullId();
	}) | ranges::to_vector;
}

MessageIdsList Session::itemOrItsGroup(not_null<HistoryItem*> item) const {
	if (const auto group = groups().find(item)) {
		return itemsToIds(group->items);
	}
	return { 1, item->fullId() };
}

void Session::setChatPinned(const Dialogs::Key &key, bool pinned) {
	Expects(key.entry()->folderKnown());

	const auto list = chatsList(key.entry()->folder())->pinned();
	list->setPinned(key, pinned);
	notifyPinnedDialogsOrderUpdated();
}

void Session::setPinnedFromDialog(const Dialogs::Key &key, bool pinned) {
	Expects(key.entry()->folderKnown());

	const auto list = chatsList(key.entry()->folder())->pinned();
	if (pinned) {
		list->addPinned(key);
	} else {
		list->setPinned(key, false);
	}
}

void Session::applyPinnedChats(
		Data::Folder *folder,
		const QVector<MTPDialogPeer> &list) {
	for (const auto &peer : list) {
		peer.match([&](const MTPDdialogPeer &data) {
			const auto history = this->history(peerFromMTP(data.vpeer()));
			if (folder) {
				history->setFolder(folder);
			} else {
				history->clearFolder();
			}
		}, [&](const MTPDdialogPeerFolder &data) {
			if (folder) {
				LOG(("API Error: Nested folders detected."));
			}
		});
	}
	chatsList(folder)->pinned()->applyList(this, list);
	notifyPinnedDialogsOrderUpdated();
}

void Session::applyDialogs(
		Data::Folder *requestFolder,
		const QVector<MTPMessage> &messages,
		const QVector<MTPDialog> &dialogs,
		std::optional<int> count) {
	processMessages(messages, NewMessageType::Last);
	for (const auto &dialog : dialogs) {
		dialog.match([&](const auto &data) {
			applyDialog(requestFolder, data);
		});
	}
	if (requestFolder && count) {
		requestFolder->setCloudChatsListSize(*count);
	}
}

void Session::applyDialog(
		Data::Folder *requestFolder,
		const MTPDdialog &data) {
	const auto peerId = peerFromMTP(data.vpeer());
	if (!peerId) {
		return;
	}

	const auto history = session().data().history(peerId);
	history->applyDialog(requestFolder, data);
	setPinnedFromDialog(history, data.is_pinned());

	if (const auto from = history->peer->migrateFrom()) {
		if (const auto historyFrom = from->owner().historyLoaded(from)) {
			App::main()->removeDialog(historyFrom);
		}
	} else if (const auto to = history->peer->migrateTo()) {
		if (to->amIn()) {
			App::main()->removeDialog(history);
		}
	}
}

void Session::applyDialog(
		Data::Folder *requestFolder,
		const MTPDdialogFolder &data) {
	if (requestFolder) {
		LOG(("API Error: requestFolder != nullptr for dialogFolder."));
	}
	const auto folder = processFolder(data.vfolder());
	folder->applyDialog(data);
	setPinnedFromDialog(folder, data.is_pinned());
}

int Session::pinnedChatsCount(Data::Folder *folder) const {
	return pinnedChatsOrder(folder).size();
}

int Session::pinnedChatsLimit(Data::Folder *folder) const {
	return folder
		? Global::PinnedDialogsInFolderMax()
		: Global::PinnedDialogsCountMax();
}

const std::vector<Dialogs::Key> &Session::pinnedChatsOrder(
		Data::Folder *folder) const {
	return chatsList(folder)->pinned()->order();
}

void Session::clearPinnedChats(Data::Folder *folder) {
	chatsList(folder)->pinned()->clear();
}

void Session::reorderTwoPinnedChats(
		const Dialogs::Key &key1,
		const Dialogs::Key &key2) {
	Expects(key1.entry()->folderKnown() && key2.entry()->folderKnown());
	Expects(key1.entry()->folder() == key2.entry()->folder());

	chatsList(key1.entry()->folder())->pinned()->reorder(key1, key2);
	notifyPinnedDialogsOrderUpdated();
}

bool Session::checkEntitiesAndViewsUpdate(const MTPDmessage &data) {
	const auto peer = [&] {
		const auto result = peerFromMTP(data.vto_id());
		if (const auto fromId = data.vfrom_id()) {
			if (result == session().userPeerId()) {
				return peerFromUser(*fromId);
			}
		}
		return result;
	}();
	if (const auto existing = message(peerToChannel(peer), data.vid().v)) {
		existing->updateSentContent({
			qs(data.vmessage()),
			TextUtilities::EntitiesFromMTP(data.ventities().value_or_empty())
		}, data.vmedia());
		existing->updateReplyMarkup(data.vreply_markup());
		existing->updateForwardedInfo(data.vfwd_from());
		existing->setViewsCount(data.vviews().value_or(-1));
		existing->indexAsNewItem();
		existing->contributeToSlowmode(data.vdate().v);
		requestItemTextRefresh(existing);
		if (existing->mainView()) {
			checkSavedGif(existing);
			return true;
		}
		return false;
	}
	return false;
}

void Session::addSavedGif(not_null<DocumentData*> document) {
	const auto index = _savedGifs.indexOf(document);
	if (!index) {
		return;
	}
	if (index > 0) {
		_savedGifs.remove(index);
	}
	_savedGifs.push_front(document);
	if (_savedGifs.size() > Global::SavedGifsLimit()) {
		_savedGifs.pop_back();
	}
	Local::writeSavedGifs();

	notifySavedGifsUpdated();
	setLastSavedGifsUpdate(0);
	session().api().updateStickers();
}

void Session::checkSavedGif(not_null<HistoryItem*> item) {
	if (item->Has<HistoryMessageForwarded>()
		|| (!item->out()
			&& item->history()->peer != session().user())) {
		return;
	}
	if (const auto media = item->media()) {
		if (const auto document = media->document()) {
			if (document->isGifv()) {
				addSavedGif(document);
			}
		}
	}
}

void Session::updateEditedMessage(const MTPMessage &data) {
	const auto existing = data.match([](const MTPDmessageEmpty &)
			-> HistoryItem* {
		return nullptr;
	}, [&](const auto &data) {
		const auto peer = [&] {
			const auto result = peerFromMTP(data.vto_id());
			if (const auto fromId = data.vfrom_id()) {
				if (result == session().userPeerId()) {
					return peerFromUser(*fromId);
				}
			}
			return result;
		}();
		return message(peerToChannel(peer), data.vid().v);
	});
	if (!existing) {
		return;
	}
	if (existing->isLocalUpdateMedia() && data.type() == mtpc_message) {
		checkEntitiesAndViewsUpdate(data.c_message());
	}
	data.match([](const MTPDmessageEmpty &) {
	}, [&](const auto &data) {
		existing->applyEdition(data);
	});
}

void Session::processMessages(
		const QVector<MTPMessage> &data,
		NewMessageType type) {
	auto indices = base::flat_map<uint64, int>();
	for (int i = 0, l = data.size(); i != l; ++i) {
		const auto &message = data[i];
		if (message.type() == mtpc_message) {
			const auto &data = message.c_message();
			// new message, index my forwarded messages to links overview
			if ((type == NewMessageType::Unread)
				&& checkEntitiesAndViewsUpdate(data)) {
				continue;
			}
		}
		const auto id = IdFromMessage(message);
		indices.emplace((uint64(uint32(id)) << 32) | uint64(i), i);
	}
	for (const auto [position, index] : indices) {
		addNewMessage(
			data[index],
			MTPDmessage_ClientFlags(),
			type);
	}
}

void Session::processMessages(
		const MTPVector<MTPMessage> &data,
		NewMessageType type) {
	processMessages(data.v, type);
}

const Session::Messages *Session::messagesList(ChannelId channelId) const {
	if (channelId == NoChannel) {
		return &_messages;
	}
	const auto i = _channelMessages.find(channelId);
	return (i != end(_channelMessages)) ? &i->second : nullptr;
}

auto Session::messagesListForInsert(ChannelId channelId)
-> not_null<Messages*> {
	return (channelId == NoChannel)
		? &_messages
		: &_channelMessages[channelId];
}

HistoryItem *Session::registerMessage(std::unique_ptr<HistoryItem> item) {
	Expects(item != nullptr);

	const auto result = item.get();
	const auto list = messagesListForInsert(result->channelId());
	const auto i = list->find(result->id);
	if (i != list->end()) {
		LOG(("App Error: Trying to re-registerMessage()."));
		i->second->destroy();
	}
	list->emplace(result->id, std::move(item));
	return result;
}

void Session::processMessagesDeleted(
		ChannelId channelId,
		const QVector<MTPint> &data) {
	const auto list = messagesList(channelId);
	const auto affected = (channelId != NoChannel)
		? historyLoaded(peerFromChannel(channelId))
		: nullptr;
	if (!list && !affected) {
		return;
	}

	auto historiesToCheck = base::flat_set<not_null<History*>>();
	for (const auto messageId : data) {
		const auto i = list ? list->find(messageId.v) : Messages::iterator();
		if (list && i != list->end()) {
			const auto history = i->second->history();
			destroyMessage(i->second.get());
			if (!history->chatListMessageKnown()) {
				historiesToCheck.emplace(history);
			}
		} else if (affected) {
			affected->unknownMessageDeleted(messageId.v);
		}
	}
	for (const auto history : historiesToCheck) {
		history->requestChatListMessage();
	}
}

void Session::removeDependencyMessage(not_null<HistoryItem*> item) {
	const auto i = _dependentMessages.find(item);
	if (i == end(_dependentMessages)) {
		return;
	}
	const auto items = std::move(i->second);
	_dependentMessages.erase(i);

	for (const auto dependent : items) {
		dependent->dependencyItemRemoved(item);
	}
}

void Session::destroyMessage(not_null<HistoryItem*> item) {
	Expects(item->isHistoryEntry() || !item->mainView());

	const auto peerId = item->history()->peer->id;
	if (item->isHistoryEntry()) {
		// All this must be done for all items manually in History::clear()!
		item->eraseFromUnreadMentions();
		if (IsServerMsgId(item->id)) {
			if (const auto types = item->sharedMediaTypes()) {
				session().storage().remove(Storage::SharedMediaRemoveOne(
					peerId,
					types,
					item->id));
			}
		} else {
			session().api().cancelLocalItem(item);
		}
		item->history()->itemRemoved(item);
	}
	_itemRemoved.fire_copy(item);
	groups().unregisterMessage(item);
	removeDependencyMessage(item);
	session().notifications().clearFromItem(item);

	const auto list = messagesListForInsert(peerToChannel(peerId));
	list->erase(item->id);
}

MsgId Session::nextLocalMessageId() {
	Expects(_localMessageIdCounter < EndClientMsgId);

	return _localMessageIdCounter++;
}

HistoryItem *Session::message(ChannelId channelId, MsgId itemId) const {
	if (!itemId) {
		return nullptr;
	}

	const auto data = messagesList(channelId);
	if (!data) {
		return nullptr;
	}

	const auto i = data->find(itemId);
	return (i != data->end()) ? i->second.get() : nullptr;
}

HistoryItem *Session::message(
		const ChannelData *channel,
		MsgId itemId) const {
	return message(channel ? peerToChannel(channel->id) : 0, itemId);
}

HistoryItem *Session::message(FullMsgId itemId) const {
	return message(itemId.channel, itemId.msg);
}

void Session::updateDependentMessages(not_null<HistoryItem*> item) {
	const auto i = _dependentMessages.find(item);
	if (i != end(_dependentMessages)) {
		for (const auto dependent : i->second) {
			dependent->updateDependencyItem();
		}
	}
	if (App::main()) {
		App::main()->itemEdited(item);
	}
}

void Session::registerDependentMessage(
		not_null<HistoryItem*> dependent,
		not_null<HistoryItem*> dependency) {
	_dependentMessages[dependency].emplace(dependent);
}

void Session::unregisterDependentMessage(
		not_null<HistoryItem*> dependent,
		not_null<HistoryItem*> dependency) {
	const auto i = _dependentMessages.find(dependency);
	if (i != end(_dependentMessages)) {
		if (i->second.remove(dependent) && i->second.empty()) {
			_dependentMessages.erase(i);
		}
	}
}

void Session::registerMessageRandomId(uint64 randomId, FullMsgId itemId) {
	_messageByRandomId.emplace(randomId, itemId);
}

void Session::unregisterMessageRandomId(uint64 randomId) {
	_messageByRandomId.remove(randomId);
}

FullMsgId Session::messageIdByRandomId(uint64 randomId) const {
	const auto i = _messageByRandomId.find(randomId);
	return (i != end(_messageByRandomId)) ? i->second : FullMsgId();
}

void Session::registerMessageSentData(
		uint64 randomId,
		PeerId peerId,
		const QString &text) {
	_sentMessagesData.emplace(randomId, SentData{ peerId, text });
}

void Session::unregisterMessageSentData(uint64 randomId) {
	_sentMessagesData.remove(randomId);
}

Session::SentData Session::messageSentData(uint64 randomId) const {
	const auto i = _sentMessagesData.find(randomId);
	return (i != end(_sentMessagesData)) ? i->second : SentData();
}

NotifySettings &Session::defaultNotifySettings(
		not_null<const PeerData*> peer) {
	return peer->isUser()
		? _defaultUserNotifySettings
		: (peer->isChat() || peer->isMegagroup())
		? _defaultChatNotifySettings
		: _defaultBroadcastNotifySettings;
}

const NotifySettings &Session::defaultNotifySettings(
		not_null<const PeerData*> peer) const {
	return peer->isUser()
		? _defaultUserNotifySettings
		: (peer->isChat() || peer->isMegagroup())
		? _defaultChatNotifySettings
		: _defaultBroadcastNotifySettings;
}

void Session::updateNotifySettingsLocal(not_null<PeerData*> peer) {
	const auto history = historyLoaded(peer->id);
	auto changesIn = crl::time(0);
	const auto muted = notifyIsMuted(peer, &changesIn);
	if (history && history->changeMute(muted)) {
		// Notification already sent.
	} else {
		Notify::peerUpdatedDelayed(
			peer,
			Notify::PeerUpdate::Flag::NotificationsEnabled);
	}

	if (muted) {
		_mutedPeers.emplace(peer);
		unmuteByFinishedDelayed(changesIn);
		if (history) {
			_session->notifications().clearIncomingFromHistory(history);
		}
	} else {
		_mutedPeers.erase(peer);
	}
}

void Session::unmuteByFinishedDelayed(crl::time delay) {
	accumulate_min(delay, kMaxNotifyCheckDelay);
	if (!_unmuteByFinishedTimer.isActive()
		|| _unmuteByFinishedTimer.remainingTime() > delay) {
		_unmuteByFinishedTimer.callOnce(delay);
	}
}

void Session::unmuteByFinished() {
	auto changesInMin = crl::time(0);
	for (auto i = begin(_mutedPeers); i != end(_mutedPeers);) {
		const auto history = historyLoaded((*i)->id);
		auto changesIn = crl::time(0);
		const auto muted = notifyIsMuted(*i, &changesIn);
		if (muted) {
			if (history) {
				history->changeMute(true);
			}
			if (!changesInMin || changesInMin > changesIn) {
				changesInMin = changesIn;
			}
			++i;
		} else {
			if (history) {
				history->changeMute(false);
			}
			i = _mutedPeers.erase(i);
		}
	}
	if (changesInMin) {
		unmuteByFinishedDelayed(changesInMin);
	}
}

HistoryItem *Session::addNewMessage(
		const MTPMessage &data,
		MTPDmessage_ClientFlags clientFlags,
		NewMessageType type) {
	const auto peerId = PeerFromMessage(data);
	if (!peerId) {
		return nullptr;
	}

	const auto result = history(peerId)->addNewMessage(
		data,
		clientFlags,
		type);
	if (result && type == NewMessageType::Unread) {
		CheckForSwitchInlineButton(result);
	}
	return result;
}

auto Session::sendActionAnimationUpdated() const
-> rpl::producer<SendActionAnimationUpdate> {
	return _sendActionAnimationUpdate.events();
}

void Session::updateSendActionAnimation(
		SendActionAnimationUpdate &&update) {
	_sendActionAnimationUpdate.fire(std::move(update));
}

int Session::unreadBadge() const {
	return computeUnreadBadge(_chatsList.unreadState());
}

bool Session::unreadBadgeMuted() const {
	return computeUnreadBadgeMuted(_chatsList.unreadState());
}

int Session::unreadBadgeIgnoreOne(const Dialogs::Key &key) const {
	const auto remove = (key && key.entry()->inChatList())
		? key.entry()->chatListUnreadState()
		: Dialogs::UnreadState();
	return computeUnreadBadge(_chatsList.unreadState() - remove);
}

bool Session::unreadBadgeMutedIgnoreOne(const Dialogs::Key &key) const {
	if (!_session->settings().includeMutedCounter()) {
		return false;
	}
	const auto remove = (key && key.entry()->inChatList())
		? key.entry()->chatListUnreadState()
		: Dialogs::UnreadState();
	return computeUnreadBadgeMuted(_chatsList.unreadState() - remove);
}

int Session::unreadOnlyMutedBadge() const {
	const auto state = _chatsList.unreadState();
	return _session->settings().countUnreadMessages()
		? state.messagesMuted
		: state.chatsMuted;
}

int Session::computeUnreadBadge(const Dialogs::UnreadState &state) const {
	const auto all = _session->settings().includeMutedCounter();
	return std::max(state.marks - (all ? 0 : state.marksMuted), 0)
		+ (_session->settings().countUnreadMessages()
			? std::max(state.messages - (all ? 0 : state.messagesMuted), 0)
			: std::max(state.chats - (all ? 0 : state.chatsMuted), 0));
}

bool Session::computeUnreadBadgeMuted(
		const Dialogs::UnreadState &state) const {
	if (!_session->settings().includeMutedCounter()) {
		return false;
	}
	return (state.marksMuted >= state.marks)
		&& (_session->settings().countUnreadMessages()
			? (state.messagesMuted >= state.messages)
			: (state.chatsMuted >= state.chats));
}

void Session::unreadStateChanged(
		const Dialogs::Key &key,
		const Dialogs::UnreadState &wasState) {
	Expects(key.entry()->folderKnown());
	Expects(key.entry()->inChatList());

	const auto nowState = key.entry()->chatListUnreadState();
	if (const auto folder = key.entry()->folder()) {
		folder->unreadStateChanged(key, wasState, nowState);
	} else {
		_chatsList.unreadStateChanged(wasState, nowState);
	}
	Notify::unreadCounterUpdated();
}

void Session::unreadEntryChanged(const Dialogs::Key &key, bool added) {
	Expects(key.entry()->folderKnown());

	const auto state = key.entry()->chatListUnreadState();
	if (!state.empty()) {
		if (const auto folder = key.entry()->folder()) {
			folder->unreadEntryChanged(key, state, added);
		} else {
			_chatsList.unreadEntryChanged(state, added);
		}
	}
}

void Session::selfDestructIn(not_null<HistoryItem*> item, crl::time delay) {
	_selfDestructItems.push_back(item->fullId());
	if (!_selfDestructTimer.isActive()
		|| _selfDestructTimer.remainingTime() > delay) {
		_selfDestructTimer.callOnce(delay);
	}
}

void Session::checkSelfDestructItems() {
	const auto now = crl::now();
	auto nextDestructIn = crl::time(0);
	for (auto i = _selfDestructItems.begin(); i != _selfDestructItems.cend();) {
		if (const auto item = message(*i)) {
			if (auto destructIn = item->getSelfDestructIn(now)) {
				if (nextDestructIn > 0) {
					accumulate_min(nextDestructIn, destructIn);
				} else {
					nextDestructIn = destructIn;
				}
				++i;
			} else {
				i = _selfDestructItems.erase(i);
			}
		} else {
			i = _selfDestructItems.erase(i);
		}
	}
	if (nextDestructIn > 0) {
		_selfDestructTimer.callOnce(nextDestructIn);
	}
}

not_null<PhotoData*> Session::photo(PhotoId id) {
	auto i = _photos.find(id);
	if (i == _photos.end()) {
		i = _photos.emplace(
			id,
			std::make_unique<PhotoData>(this, id)).first;
	}
	return i->second.get();
}

not_null<PhotoData*> Session::processPhoto(const MTPPhoto &data) {
	return data.match([&](const MTPDphoto &data) {
		return processPhoto(data);
	}, [&](const MTPDphotoEmpty &data) {
		return photo(data.vid().v);
	});
}

not_null<PhotoData*> Session::processPhoto(const MTPDphoto &data) {
	const auto result = photo(data.vid().v);
	photoApplyFields(result, data);
	return result;
}

not_null<PhotoData*> Session::processPhoto(
		const MTPPhoto &data,
		const PreparedPhotoThumbs &thumbs) {
	Expects(!thumbs.empty());

	const auto find = [&](const QByteArray &levels) {
		const auto kInvalidIndex = int(levels.size());
		const auto level = [&](const auto &pair) {
			const auto letter = pair.first;
			const auto index = levels.indexOf(letter);
			return (index >= 0) ? index : kInvalidIndex;
		};
		const auto result = ranges::max_element(
			thumbs,
			std::greater<>(),
			level);
		return (level(*result) == kInvalidIndex) ? thumbs.end() : result;
	};
	const auto image = [&](const QByteArray &levels) {
		const auto i = find(levels);
		return (i == thumbs.end())
			? ImagePtr()
			: Images::Create(base::duplicate(i->second), "JPG");
	};
	const auto thumbnailInline = image(InlineLevels);
	const auto thumbnailSmall = image(SmallLevels);
	const auto thumbnail = image(ThumbnailLevels);
	const auto large = image(LargeLevels);
	return data.match([&](const MTPDphoto &data) {
		return photo(
			data.vid().v,
			data.vaccess_hash().v,
			data.vfile_reference().v,
			data.vdate().v,
			data.vdc_id().v,
			data.is_has_stickers(),
			thumbnailInline,
			thumbnailSmall,
			thumbnail,
			large);
	}, [&](const MTPDphotoEmpty &data) {
		return photo(data.vid().v);
	});
}

not_null<PhotoData*> Session::photo(
		PhotoId id,
		const uint64 &access,
		const QByteArray &fileReference,
		TimeId date,
		int32 dc,
		bool hasSticker,
		const ImagePtr &thumbnailInline,
		const ImagePtr &thumbnailSmall,
		const ImagePtr &thumbnail,
		const ImagePtr &large) {
	const auto result = photo(id);
	photoApplyFields(
		result,
		access,
		fileReference,
		date,
		dc,
		hasSticker,
		thumbnailInline,
		thumbnailSmall,
		thumbnail,
		large);
	return result;
}

void Session::photoConvert(
		not_null<PhotoData*> original,
		const MTPPhoto &data) {
	const auto id = data.match([](const auto &data) {
		return data.vid().v;
	});
	if (original->id != id) {
		auto i = _photos.find(id);
		if (i == _photos.end()) {
			const auto j = _photos.find(original->id);
			Assert(j != _photos.end());
			auto owned = std::move(j->second);
			_photos.erase(j);
			i = _photos.emplace(id, std::move(owned)).first;
		}

		original->id = id;
		original->uploadingData = nullptr;

		if (i->second.get() != original) {
			photoApplyFields(i->second.get(), data);
		}
	}
	photoApplyFields(original, data);
}

PhotoData *Session::photoFromWeb(
		const MTPWebDocument &data,
		ImagePtr thumbnail,
		bool willBecomeNormal) {
	const auto large = Images::Create(data);
	const auto thumbnailInline = ImagePtr();
	if (large->isNull()) {
		return nullptr;
	}
	auto thumbnailSmall = large;
	if (willBecomeNormal) {
		const auto width = large->width();
		const auto height = large->height();

		auto thumbsize = shrinkToKeepAspect(width, height, 100, 100);
		thumbnailSmall = Images::Create(thumbsize.width(), thumbsize.height());

		if (thumbnail->isNull()) {
			auto mediumsize = shrinkToKeepAspect(width, height, 320, 320);
			thumbnail = Images::Create(mediumsize.width(), mediumsize.height());
		}
	} else if (thumbnail->isNull()) {
		thumbnail = large;
	}

	return photo(
		rand_value<PhotoId>(),
		uint64(0),
		QByteArray(),
		base::unixtime::now(),
		0,
		false,
		thumbnailInline,
		thumbnailSmall,
		thumbnail,
		large);
}

void Session::photoApplyFields(
		not_null<PhotoData*> photo,
		const MTPPhoto &data) {
	if (data.type() == mtpc_photo) {
		photoApplyFields(photo, data.c_photo());
	}
}

void Session::photoApplyFields(
		not_null<PhotoData*> photo,
		const MTPDphoto &data) {
	const auto &sizes = data.vsizes().v;
	const auto find = [&](const QByteArray &levels) {
		const auto kInvalidIndex = int(levels.size());
		const auto level = [&](const MTPPhotoSize &size) {
			const auto letter = size.match([](const MTPDphotoSizeEmpty &) {
				return char(0);
			}, [](const auto &size) {
				return size.vtype().v.isEmpty() ? char(0) : size.vtype().v[0];
			});
			const auto index = levels.indexOf(letter);
			return (index >= 0) ? index : kInvalidIndex;
		};
		const auto result = ranges::max_element(
			sizes,
			std::greater<>(),
			level);
		return (level(*result) == kInvalidIndex) ? sizes.end() : result;
	};
	const auto image = [&](const QByteArray &levels) {
		const auto i = find(levels);
		return (i == sizes.end()) ? ImagePtr() : Images::Create(data, *i);
	};
	const auto thumbnailInline = image(InlineLevels);
	const auto thumbnailSmall = image(SmallLevels);
	const auto thumbnail = image(ThumbnailLevels);
	const auto large = image(LargeLevels);
	if (thumbnailSmall && thumbnail && large) {
		photoApplyFields(
			photo,
			data.vaccess_hash().v,
			data.vfile_reference().v,
			data.vdate().v,
			data.vdc_id().v,
			data.is_has_stickers(),
			thumbnailInline,
			thumbnailSmall,
			thumbnail,
			large);
	}
}

void Session::photoApplyFields(
		not_null<PhotoData*> photo,
		const uint64 &access,
		const QByteArray &fileReference,
		TimeId date,
		int32 dc,
		bool hasSticker,
		const ImagePtr &thumbnailInline,
		const ImagePtr &thumbnailSmall,
		const ImagePtr &thumbnail,
		const ImagePtr &large) {
	if (!date) {
		return;
	}
	photo->setRemoteLocation(dc, access, fileReference);
	photo->date = date;
	photo->hasSticker = hasSticker;
	photo->updateImages(
		thumbnailInline,
		thumbnailSmall,
		thumbnail,
		large);
}

not_null<DocumentData*> Session::document(DocumentId id) {
	auto i = _documents.find(id);
	if (i == _documents.cend()) {
		i = _documents.emplace(
			id,
			std::make_unique<DocumentData>(this, id)).first;
	}
	return i->second.get();
}

not_null<DocumentData*> Session::processDocument(const MTPDocument &data) {
	switch (data.type()) {
	case mtpc_document:
		return processDocument(data.c_document());

	case mtpc_documentEmpty:
		return document(data.c_documentEmpty().vid().v);
	}
	Unexpected("Type in Session::document().");
}

not_null<DocumentData*> Session::processDocument(const MTPDdocument &data) {
	const auto result = document(data.vid().v);
	documentApplyFields(result, data);
	return result;
}

not_null<DocumentData*> Session::processDocument(
		const MTPdocument &data,
		QImage &&thumb) {
	switch (data.type()) {
	case mtpc_documentEmpty:
		return document(data.c_documentEmpty().vid().v);

	case mtpc_document: {
		const auto &fields = data.c_document();
		const auto mime = qs(fields.vmime_type());
		const auto format = (mime == qstr("image/webp")
			|| mime == qstr("application/x-tgsticker"))
			? "WEBP"
			: "JPG";
		return document(
			fields.vid().v,
			fields.vaccess_hash().v,
			fields.vfile_reference().v,
			fields.vdate().v,
			fields.vattributes().v,
			mime,
			ImagePtr(),
			Images::Create(std::move(thumb), format),
			fields.vdc_id().v,
			fields.vsize().v,
			StorageImageLocation());
	} break;
	}
	Unexpected("Type in Session::document() with thumb.");
}

not_null<DocumentData*> Session::document(
		DocumentId id,
		const uint64 &access,
		const QByteArray &fileReference,
		TimeId date,
		const QVector<MTPDocumentAttribute> &attributes,
		const QString &mime,
		const ImagePtr &thumbnailInline,
		const ImagePtr &thumbnail,
		int32 dc,
		int32 size,
		const StorageImageLocation &thumbLocation) {
	const auto result = document(id);
	documentApplyFields(
		result,
		access,
		fileReference,
		date,
		attributes,
		mime,
		thumbnailInline,
		thumbnail,
		dc,
		size,
		thumbLocation);
	return result;
}

void Session::documentConvert(
		not_null<DocumentData*> original,
		const MTPDocument &data) {
	const auto id = [&] {
		switch (data.type()) {
		case mtpc_document: return data.c_document().vid().v;
		case mtpc_documentEmpty: return data.c_documentEmpty().vid().v;
		}
		Unexpected("Type in Session::documentConvert().");
	}();
	const auto oldKey = original->mediaKey();
	const auto oldCacheKey = original->cacheKey();
	const auto idChanged = (original->id != id);
	const auto sentSticker = idChanged && (original->sticker() != nullptr);
	if (idChanged) {
		auto i = _documents.find(id);
		if (i == _documents.end()) {
			const auto j = _documents.find(original->id);
			Assert(j != _documents.end());
			auto owned = std::move(j->second);
			_documents.erase(j);
			i = _documents.emplace(id, std::move(owned)).first;
		}

		original->id = id;
		original->status = FileReady;
		original->uploadingData = nullptr;

		if (i->second.get() != original) {
			documentApplyFields(i->second.get(), data);
		}
	}
	documentApplyFields(original, data);
	if (idChanged) {
		cache().moveIfEmpty(oldCacheKey, original->cacheKey());
		if (savedGifs().indexOf(original) >= 0) {
			Local::writeSavedGifs();
		}
	}
}

DocumentData *Session::documentFromWeb(
		const MTPWebDocument &data,
		ImagePtr thumb) {
	switch (data.type()) {
	case mtpc_webDocument:
		return documentFromWeb(data.c_webDocument(), thumb);

	case mtpc_webDocumentNoProxy:
		return documentFromWeb(data.c_webDocumentNoProxy(), thumb);

	}
	Unexpected("Type in Session::documentFromWeb.");
}

DocumentData *Session::documentFromWeb(
		const MTPDwebDocument &data,
		ImagePtr thumb) {
	const auto result = document(
		rand_value<DocumentId>(),
		uint64(0),
		QByteArray(),
		base::unixtime::now(),
		data.vattributes().v,
		data.vmime_type().v,
		ImagePtr(),
		thumb,
		MTP::maindc(),
		int32(0), // data.vsize().v
		StorageImageLocation());
	result->setWebLocation(WebFileLocation(
		data.vurl().v,
		data.vaccess_hash().v));
	return result;
}

DocumentData *Session::documentFromWeb(
		const MTPDwebDocumentNoProxy &data,
		ImagePtr thumb) {
	const auto result = document(
		rand_value<DocumentId>(),
		uint64(0),
		QByteArray(),
		base::unixtime::now(),
		data.vattributes().v,
		data.vmime_type().v,
		ImagePtr(),
		thumb,
		MTP::maindc(),
		int32(0), // data.vsize().v
		StorageImageLocation());
	result->setContentUrl(qs(data.vurl()));
	return result;
}

void Session::documentApplyFields(
		not_null<DocumentData*> document,
		const MTPDocument &data) {
	if (data.type() == mtpc_document) {
		documentApplyFields(document, data.c_document());
	}
}

void Session::documentApplyFields(
		not_null<DocumentData*> document,
		const MTPDdocument &data) {
	const auto thumbnailInline = FindDocumentInlineThumbnail(data);
	const auto thumbnailSize = FindDocumentThumbnail(data);
	const auto thumbnail = Images::Create(data, thumbnailSize);
	documentApplyFields(
		document,
		data.vaccess_hash().v,
		data.vfile_reference().v,
		data.vdate().v,
		data.vattributes().v,
		qs(data.vmime_type()),
		Images::Create(data, thumbnailInline),
		thumbnail,
		data.vdc_id().v,
		data.vsize().v,
		thumbnail->location());
}

void Session::documentApplyFields(
		not_null<DocumentData*> document,
		const uint64 &access,
		const QByteArray &fileReference,
		TimeId date,
		const QVector<MTPDocumentAttribute> &attributes,
		const QString &mime,
		const ImagePtr &thumbnailInline,
		const ImagePtr &thumbnail,
		int32 dc,
		int32 size,
		const StorageImageLocation &thumbLocation) {
	if (!date) {
		return;
	}
	document->date = date;
	document->setMimeString(mime);
	document->updateThumbnails(thumbnailInline, thumbnail);
	document->size = size;
	document->setattributes(attributes);

	// Uses 'type' that is computed from attributes.
	document->recountIsImage();
	if (dc != 0 && access != 0) {
		document->setRemoteLocation(dc, access, fileReference);
	}

	if (document->sticker()
		&& !document->sticker()->loc.valid()
		&& thumbLocation.valid()) {
		document->sticker()->loc = thumbLocation;
	}
}

not_null<WebPageData*> Session::webpage(WebPageId id) {
	auto i = _webpages.find(id);
	if (i == _webpages.cend()) {
		i = _webpages.emplace(id, std::make_unique<WebPageData>(id)).first;
	}
	return i->second.get();
}

not_null<WebPageData*> Session::processWebpage(const MTPWebPage &data) {
	switch (data.type()) {
	case mtpc_webPage:
		return processWebpage(data.c_webPage());
	case mtpc_webPageEmpty: {
		const auto result = webpage(data.c_webPageEmpty().vid().v);
		if (result->pendingTill > 0) {
			result->pendingTill = -1; // failed
		}
		return result;
	} break;
	case mtpc_webPagePending:
		return processWebpage(data.c_webPagePending());
	case mtpc_webPageNotModified:
		LOG(("API Error: "
			"webPageNotModified is unexpected in Session::webpage()."));
		return webpage(0);
	}
	Unexpected("Type in Session::webpage().");
}

not_null<WebPageData*> Session::processWebpage(const MTPDwebPage &data) {
	const auto result = webpage(data.vid().v);
	webpageApplyFields(result, data);
	return result;
}

not_null<WebPageData*> Session::processWebpage(const MTPDwebPagePending &data) {
	constexpr auto kDefaultPendingTimeout = 60;
	const auto result = webpage(data.vid().v);
	webpageApplyFields(
		result,
		WebPageType::Article,
		QString(),
		QString(),
		QString(),
		QString(),
		TextWithEntities(),
		nullptr,
		nullptr,
		WebPageCollage(),
		0,
		QString(),
		data.vdate().v
			? data.vdate().v
			: (base::unixtime::now() + kDefaultPendingTimeout));
	return result;
}

not_null<WebPageData*> Session::webpage(
		WebPageId id,
		const QString &siteName,
		const TextWithEntities &content) {
	return webpage(
		id,
		WebPageType::Article,
		QString(),
		QString(),
		siteName,
		QString(),
		content,
		nullptr,
		nullptr,
		WebPageCollage(),
		0,
		QString(),
		TimeId(0));
}

not_null<WebPageData*> Session::webpage(
		WebPageId id,
		WebPageType type,
		const QString &url,
		const QString &displayUrl,
		const QString &siteName,
		const QString &title,
		const TextWithEntities &description,
		PhotoData *photo,
		DocumentData *document,
		WebPageCollage &&collage,
		int duration,
		const QString &author,
		TimeId pendingTill) {
	const auto result = webpage(id);
	webpageApplyFields(
		result,
		type,
		url,
		displayUrl,
		siteName,
		title,
		description,
		photo,
		document,
		std::move(collage),
		duration,
		author,
		pendingTill);
	return result;
}

void Session::webpageApplyFields(
		not_null<WebPageData*> page,
		const MTPDwebPage &data) {
	auto description = TextWithEntities {
		TextUtilities::Clean(qs(data.vdescription().value_or_empty()))
	};
	const auto siteName = qs(data.vsite_name().value_or_empty());
	auto parseFlags = TextParseLinks | TextParseMultiline | TextParseRichText;
	if (siteName == qstr("Twitter") || siteName == qstr("Instagram")) {
		parseFlags |= TextParseHashtags | TextParseMentions;
	}
	TextUtilities::ParseEntities(description, parseFlags);
	const auto pendingTill = TimeId(0);
	const auto photo = data.vphoto();
	const auto document = data.vdocument();
	webpageApplyFields(
		page,
		ParseWebPageType(data),
		qs(data.vurl()),
		qs(data.vdisplay_url()),
		siteName,
		qs(data.vtitle().value_or_empty()),
		description,
		photo ? processPhoto(*photo).get() : nullptr,
		document ? processDocument(*document).get() : nullptr,
		WebPageCollage(data),
		data.vduration().value_or_empty(),
		qs(data.vauthor().value_or_empty()),
		pendingTill);
}

void Session::webpageApplyFields(
		not_null<WebPageData*> page,
		WebPageType type,
		const QString &url,
		const QString &displayUrl,
		const QString &siteName,
		const QString &title,
		const TextWithEntities &description,
		PhotoData *photo,
		DocumentData *document,
		WebPageCollage &&collage,
		int duration,
		const QString &author,
		TimeId pendingTill) {
	const auto requestPending = (!page->pendingTill && pendingTill > 0);
	const auto changed = page->applyChanges(
		type,
		url,
		displayUrl,
		siteName,
		title,
		description,
		photo,
		document,
		std::move(collage),
		duration,
		author,
		pendingTill);
	if (requestPending) {
		_session->api().requestWebPageDelayed(page);
	}
	if (changed) {
		notifyWebPageUpdateDelayed(page);
	}
}

not_null<GameData*> Session::game(GameId id) {
	auto i = _games.find(id);
	if (i == _games.cend()) {
		i = _games.emplace(id, std::make_unique<GameData>(id)).first;
	}
	return i->second.get();
}

not_null<GameData*> Session::processGame(const MTPDgame &data) {
	const auto result = game(data.vid().v);
	gameApplyFields(result, data);
	return result;
}

not_null<GameData*> Session::game(
		GameId id,
		const uint64 &accessHash,
		const QString &shortName,
		const QString &title,
		const QString &description,
		PhotoData *photo,
		DocumentData *document) {
	const auto result = game(id);
	gameApplyFields(
		result,
		accessHash,
		shortName,
		title,
		description,
		photo,
		document);
	return result;
}

void Session::gameConvert(
		not_null<GameData*> original,
		const MTPGame &data) {
	Expects(data.type() == mtpc_game);

	const auto id = data.c_game().vid().v;
	if (original->id != id) {
		auto i = _games.find(id);
		if (i == _games.end()) {
			const auto j = _games.find(original->id);
			Assert(j != _games.end());
			auto owned = std::move(j->second);
			_games.erase(j);
			i = _games.emplace(id, std::move(owned)).first;
		}

		original->id = id;
		original->accessHash = 0;

		if (i->second.get() != original) {
			gameApplyFields(i->second.get(), data.c_game());
		}
	}
	gameApplyFields(original, data.c_game());
}

void Session::gameApplyFields(
		not_null<GameData*> game,
		const MTPDgame &data) {
	const auto document = data.vdocument();
	gameApplyFields(
		game,
		data.vaccess_hash().v,
		qs(data.vshort_name()),
		qs(data.vtitle()),
		qs(data.vdescription()),
		processPhoto(data.vphoto()),
		document ? processDocument(*document).get() : nullptr);
}

void Session::gameApplyFields(
		not_null<GameData*> game,
		const uint64 &accessHash,
		const QString &shortName,
		const QString &title,
		const QString &description,
		PhotoData *photo,
		DocumentData *document) {
	if (game->accessHash) {
		return;
	}
	game->accessHash = accessHash;
	game->shortName = TextUtilities::Clean(shortName);
	game->title = TextUtilities::SingleLine(title);
	game->description = TextUtilities::Clean(description);
	game->photo = photo;
	game->document = document;
	notifyGameUpdateDelayed(game);
}

not_null<PollData*> Session::poll(PollId id) {
	auto i = _polls.find(id);
	if (i == _polls.cend()) {
		i = _polls.emplace(id, std::make_unique<PollData>(id)).first;
	}
	return i->second.get();
}

not_null<PollData*> Session::processPoll(const MTPPoll &data) {
	return data.match([&](const MTPDpoll &data) {
		const auto id = data.vid().v;
		const auto result = poll(id);
		const auto changed = result->applyChanges(data);
		if (changed) {
			notifyPollUpdateDelayed(result);
		}
		return result;
	});
}

not_null<PollData*> Session::processPoll(const MTPDmessageMediaPoll &data) {
	const auto result = processPoll(data.vpoll());
	const auto changed = result->applyResults(data.vresults());
	if (changed) {
		notifyPollUpdateDelayed(result);
	}
	return result;
}

void Session::applyUpdate(const MTPDupdateMessagePoll &update) {
	const auto updated = [&] {
		const auto poll = update.vpoll();
		const auto i = _polls.find(update.vpoll_id().v);
		return (i == end(_polls))
			? nullptr
			: poll
			? processPoll(*poll).get()
			: i->second.get();
	}();
	if (updated && updated->applyResults(update.vresults())) {
		notifyPollUpdateDelayed(updated);
	}
}

void Session::applyUpdate(const MTPDupdateChatParticipants &update) {
	const auto chatId = update.vparticipants().match([](const auto &update) {
		return update.vchat_id().v;
	});
	if (const auto chat = chatLoaded(chatId)) {
		ApplyChatUpdate(chat, update);
		for (const auto user : chat->participants) {
			if (user->isBot() && !user->botInfo->inited) {
				_session->api().requestFullPeer(user);
			}
		}
	}
}

void Session::applyUpdate(const MTPDupdateChatParticipantAdd &update) {
	if (const auto chat = chatLoaded(update.vchat_id().v)) {
		ApplyChatUpdate(chat, update);
	}
}

void Session::applyUpdate(const MTPDupdateChatParticipantDelete &update) {
	if (const auto chat = chatLoaded(update.vchat_id().v)) {
		ApplyChatUpdate(chat, update);
	}
}

void Session::applyUpdate(const MTPDupdateChatParticipantAdmin &update) {
	if (const auto chat = chatLoaded(update.vchat_id().v)) {
		ApplyChatUpdate(chat, update);
	}
}

void Session::applyUpdate(const MTPDupdateChatDefaultBannedRights &update) {
	if (const auto peer = peerLoaded(peerFromMTP(update.vpeer()))) {
		if (const auto chat = peer->asChat()) {
			ApplyChatUpdate(chat, update);
		} else if (const auto channel = peer->asChannel()) {
			ApplyChannelUpdate(channel, update);
		} else {
			LOG(("API Error: "
				"User received in updateChatDefaultBannedRights."));
		}
	}
}

not_null<LocationThumbnail*> Session::location(const LocationPoint &point) {
	const auto i = _locations.find(point);
	return (i != _locations.cend())
		? i->second.get()
		: _locations.emplace(
			point,
			std::make_unique<LocationThumbnail>(point)).first->second.get();
}

void Session::registerPhotoItem(
		not_null<const PhotoData*> photo,
		not_null<HistoryItem*> item) {
	_photoItems[photo].insert(item);
}

void Session::unregisterPhotoItem(
		not_null<const PhotoData*> photo,
		not_null<HistoryItem*> item) {
	const auto i = _photoItems.find(photo);
	if (i != _photoItems.end()) {
		auto &items = i->second;
		if (items.remove(item) && items.empty()) {
			_photoItems.erase(i);
		}
	}
}

void Session::registerDocumentItem(
		not_null<const DocumentData*> document,
		not_null<HistoryItem*> item) {
	_documentItems[document].insert(item);
}

void Session::unregisterDocumentItem(
		not_null<const DocumentData*> document,
		not_null<HistoryItem*> item) {
	const auto i = _documentItems.find(document);
	if (i != _documentItems.end()) {
		auto &items = i->second;
		if (items.remove(item) && items.empty()) {
			_documentItems.erase(i);
		}
	}
}

void Session::registerWebPageView(
		not_null<const WebPageData*> page,
		not_null<ViewElement*> view) {
	_webpageViews[page].insert(view);
}

void Session::unregisterWebPageView(
		not_null<const WebPageData*> page,
		not_null<ViewElement*> view) {
	const auto i = _webpageViews.find(page);
	if (i != _webpageViews.end()) {
		auto &items = i->second;
		if (items.remove(view) && items.empty()) {
			_webpageViews.erase(i);
		}
	}
}

void Session::registerWebPageItem(
		not_null<const WebPageData*> page,
		not_null<HistoryItem*> item) {
	_webpageItems[page].insert(item);
}

void Session::unregisterWebPageItem(
		not_null<const WebPageData*> page,
		not_null<HistoryItem*> item) {
	const auto i = _webpageItems.find(page);
	if (i != _webpageItems.end()) {
		auto &items = i->second;
		if (items.remove(item) && items.empty()) {
			_webpageItems.erase(i);
		}
	}
}

void Session::registerGameView(
		not_null<const GameData*> game,
		not_null<ViewElement*> view) {
	_gameViews[game].insert(view);
}

void Session::unregisterGameView(
		not_null<const GameData*> game,
		not_null<ViewElement*> view) {
	const auto i = _gameViews.find(game);
	if (i != _gameViews.end()) {
		auto &items = i->second;
		if (items.remove(view) && items.empty()) {
			_gameViews.erase(i);
		}
	}
}

void Session::registerPollView(
		not_null<const PollData*> poll,
		not_null<ViewElement*> view) {
	_pollViews[poll].insert(view);
}

void Session::unregisterPollView(
		not_null<const PollData*> poll,
		not_null<ViewElement*> view) {
	const auto i = _pollViews.find(poll);
	if (i != _pollViews.end()) {
		auto &items = i->second;
		if (items.remove(view) && items.empty()) {
			_pollViews.erase(i);
		}
	}
}

void Session::registerContactView(
		UserId contactId,
		not_null<ViewElement*> view) {
	if (!contactId) {
		return;
	}
	_contactViews[contactId].insert(view);
}

void Session::unregisterContactView(
		UserId contactId,
		not_null<ViewElement*> view) {
	if (!contactId) {
		return;
	}
	const auto i = _contactViews.find(contactId);
	if (i != _contactViews.end()) {
		auto &items = i->second;
		if (items.remove(view) && items.empty()) {
			_contactViews.erase(i);
		}
	}
}

void Session::registerContactItem(
		UserId contactId,
		not_null<HistoryItem*> item) {
	if (!contactId) {
		return;
	}
	const auto contact = userLoaded(contactId);
	const auto canShare = contact ? contact->canShareThisContact() : false;

	_contactItems[contactId].insert(item);

	if (contact && canShare != contact->canShareThisContact()) {
		Notify::peerUpdatedDelayed(
			contact,
			Notify::PeerUpdate::Flag::UserCanShareContact);
	}

	if (const auto i = _views.find(item); i != _views.end()) {
		for (const auto view : i->second) {
			if (const auto media = view->media()) {
				media->updateSharedContactUserId(contactId);
			}
		}
	}
}

void Session::unregisterContactItem(
		UserId contactId,
		not_null<HistoryItem*> item) {
	if (!contactId) {
		return;
	}
	const auto contact = userLoaded(contactId);
	const auto canShare = contact ? contact->canShareThisContact() : false;

	const auto i = _contactItems.find(contactId);
	if (i != _contactItems.end()) {
		auto &items = i->second;
		if (items.remove(item) && items.empty()) {
			_contactItems.erase(i);
		}
	}

	if (contact && canShare != contact->canShareThisContact()) {
		Notify::peerUpdatedDelayed(
			contact,
			Notify::PeerUpdate::Flag::UserCanShareContact);
	}
}

void Session::registerAutoplayAnimation(
		not_null<::Media::Clip::Reader*> reader,
		not_null<ViewElement*> view) {
	_autoplayAnimations.emplace(reader, view);
}

void Session::unregisterAutoplayAnimation(
		not_null<::Media::Clip::Reader*> reader) {
	_autoplayAnimations.remove(reader);
}

void Session::stopAutoplayAnimations() {
	for (const auto [reader, view] : base::take(_autoplayAnimations)) {
		if (const auto media = view->media()) {
			media->stopAnimation();
		}
	}
}

HistoryItem *Session::findWebPageItem(not_null<WebPageData*> page) const {
	const auto i = _webpageItems.find(page);
	if (i != _webpageItems.end()) {
		for (const auto item : i->second) {
			if (IsServerMsgId(item->id)) {
				return item;
			}
		}
	}
	return nullptr;
}

QString Session::findContactPhone(not_null<UserData*> contact) const {
	const auto result = contact->phone();
	return result.isEmpty()
		? findContactPhone(contact->bareId())
		: App::formatPhone(result);
}

QString Session::findContactPhone(UserId contactId) const {
	const auto i = _contactItems.find(contactId);
	if (i != _contactItems.end()) {
		if (const auto media = (*begin(i->second))->media()) {
			if (const auto contact = media->sharedContact()) {
				return contact->phoneNumber;
			}
		}
	}
	return QString();
}

bool Session::hasPendingWebPageGamePollNotification() const {
	return !_webpagesUpdated.empty()
		|| !_gamesUpdated.empty()
		|| !_pollsUpdated.empty();
}

void Session::notifyWebPageUpdateDelayed(not_null<WebPageData*> page) {
	const auto invoke = !hasPendingWebPageGamePollNotification();
	_webpagesUpdated.insert(page);
	if (invoke) {
		crl::on_main(_session, [=] { sendWebPageGamePollNotifications(); });
	}
}

void Session::notifyGameUpdateDelayed(not_null<GameData*> game) {
	const auto invoke = !hasPendingWebPageGamePollNotification();
	_gamesUpdated.insert(game);
	if (invoke) {
		crl::on_main(_session, [=] { sendWebPageGamePollNotifications(); });
	}
}

void Session::notifyPollUpdateDelayed(not_null<PollData*> poll) {
	const auto invoke = !hasPendingWebPageGamePollNotification();
	_pollsUpdated.insert(poll);
	if (invoke) {
		crl::on_main(_session, [=] { sendWebPageGamePollNotifications(); });
	}
}

void Session::sendWebPageGamePollNotifications() {
	for (const auto page : base::take(_webpagesUpdated)) {
		const auto i = _webpageViews.find(page);
		if (i != _webpageViews.end()) {
			for (const auto view : i->second) {
				requestViewResize(view);
			}
		}
	}
	for (const auto game : base::take(_gamesUpdated)) {
		if (const auto i = _gameViews.find(game); i != _gameViews.end()) {
			for (const auto view : i->second) {
				requestViewResize(view);
			}
		}
	}
	for (const auto poll : base::take(_pollsUpdated)) {
		if (const auto i = _pollViews.find(poll); i != _pollViews.end()) {
			for (const auto view : i->second) {
				requestViewResize(view);
			}
		}
	}
}

void Session::registerItemView(not_null<ViewElement*> view) {
	_views[view->data()].push_back(view);
}

void Session::unregisterItemView(not_null<ViewElement*> view) {
	const auto i = _views.find(view->data());
	if (i != end(_views)) {
		auto &list = i->second;
		list.erase(ranges::remove(list, view), end(list));
		if (list.empty()) {
			_views.erase(i);
		}
	}
	if (App::hoveredItem() == view) {
		App::hoveredItem(nullptr);
	}
	if (App::pressedItem() == view) {
		App::pressedItem(nullptr);
	}
	if (App::hoveredLinkItem() == view) {
		App::hoveredLinkItem(nullptr);
	}
	if (App::pressedLinkItem() == view) {
		App::pressedLinkItem(nullptr);
	}
	if (App::mousedItem() == view) {
		App::mousedItem(nullptr);
	}
}

not_null<Folder*> Session::folder(FolderId id) {
	if (const auto result = folderLoaded(id)) {
		return result;
	}
	const auto [it, ok] = _folders.emplace(
		id,
		std::make_unique<Folder>(this, id));
	return it->second.get();
}

Folder *Session::folderLoaded(FolderId id) const {
	const auto it = _folders.find(id);
	return (it == end(_folders)) ? nullptr : it->second.get();
}

not_null<Folder*> Session::processFolder(const MTPFolder &data) {
	return data.match([&](const MTPDfolder &data) {
		return processFolder(data);
	});
}

not_null<Folder*> Session::processFolder(const MTPDfolder &data) {
	const auto result = folder(data.vid().v);
	//data.vphoto();
	//data.vtitle();
	return result;
}
// // #feed
//void Session::setDefaultFeedId(FeedId id) {
//	_defaultFeedId = id;
//}
//
//FeedId Session::defaultFeedId() const {
//	return _defaultFeedId.current();
//}
//
//rpl::producer<FeedId> Session::defaultFeedIdValue() const {
//	return _defaultFeedId.value();
//}

not_null<Dialogs::MainList*> Session::chatsList(Data::Folder *folder) {
	return folder ? folder->chatsList().get() : &_chatsList;
}

not_null<const Dialogs::MainList*> Session::chatsList(
		Data::Folder *folder) const {
	return folder ? folder->chatsList() : &_chatsList;
}

not_null<Dialogs::IndexedList*> Session::contactsList() {
	return &_contactsList;
}

not_null<Dialogs::IndexedList*> Session::contactsNoChatsList() {
	return &_contactsNoChatsList;
}

auto Session::refreshChatListEntry(Dialogs::Key key)
-> RefreshChatListEntryResult {
	using namespace Dialogs;

	const auto entry = key.entry();
	auto result = RefreshChatListEntryResult();
	result.changed = !entry->inChatList();
	if (result.changed) {
		const auto mainRow = entry->addToChatList(Mode::All);
		_contactsNoChatsList.del(key, mainRow);
	} else {
		result.moved = entry->adjustByPosInChatList(Mode::All);
	}
	if (Global::DialogsModeEnabled()) {
		if (entry->toImportant()) {
			result.importantChanged = !entry->inChatList(Mode::Important);
			if (result.importantChanged) {
				entry->addToChatList(Mode::Important);
			} else {
				result.importantMoved = entry->adjustByPosInChatList(
					Mode::Important);
			}
		} else if (entry->inChatList(Mode::Important)) {
			entry->removeFromChatList(Mode::Important);
			result.importantChanged = true;
		}
	}
	return result;
}

void Session::removeChatListEntry(Dialogs::Key key) {
	using namespace Dialogs;

	const auto entry = key.entry();
	entry->removeFromChatList(Mode::All);
	if (Global::DialogsModeEnabled()) {
		entry->removeFromChatList(Mode::Important);
	}
	if (_contactsList.contains(key)) {
		if (!_contactsNoChatsList.contains(key)) {
			_contactsNoChatsList.addByName(key);
		}
	}
}

void Session::dialogsRowReplaced(DialogsRowReplacement replacement) {
	_dialogsRowReplacements.fire(std::move(replacement));
}

auto Session::dialogsRowReplacements() const
-> rpl::producer<DialogsRowReplacement> {
	return _dialogsRowReplacements.events();
}

void Session::requestNotifySettings(not_null<PeerData*> peer) {
	if (peer->notifySettingsUnknown()) {
		_session->api().requestNotifySettings(
			MTP_inputNotifyPeer(peer->input));
	}
	if (defaultNotifySettings(peer).settingsUnknown()) {
		_session->api().requestNotifySettings(peer->isUser()
			? MTP_inputNotifyUsers()
			: (peer->isChat() || peer->isMegagroup())
			? MTP_inputNotifyChats()
			: MTP_inputNotifyBroadcasts());
	}
}

void Session::applyNotifySetting(
		const MTPNotifyPeer &notifyPeer,
		const MTPPeerNotifySettings &settings) {
	switch (notifyPeer.type()) {
	case mtpc_notifyUsers: {
		if (_defaultUserNotifySettings.change(settings)) {
			_defaultUserNotifyUpdates.fire({});

			enumerateUsers([&](not_null<UserData*> user) {
				if (!user->notifySettingsUnknown()
					&& ((!user->notifyMuteUntil()
						&& _defaultUserNotifySettings.muteUntil())
						|| (!user->notifySilentPosts()
							&& _defaultUserNotifySettings.silentPosts()))) {
					updateNotifySettingsLocal(user);
				}
			});
		}
	} break;
	case mtpc_notifyChats: {
		if (_defaultChatNotifySettings.change(settings)) {
			_defaultChatNotifyUpdates.fire({});

			enumerateGroups([&](not_null<PeerData*> peer) {
				if (!peer->notifySettingsUnknown()
					&& ((!peer->notifyMuteUntil()
						&& _defaultChatNotifySettings.muteUntil())
						|| (!peer->notifySilentPosts()
							&& _defaultChatNotifySettings.silentPosts()))) {
					updateNotifySettingsLocal(peer);
				}
			});
		}
	} break;
	case mtpc_notifyBroadcasts: {
		if (_defaultBroadcastNotifySettings.change(settings)) {
			_defaultBroadcastNotifyUpdates.fire({});

			enumerateChannels([&](not_null<ChannelData*> channel) {
				if (!channel->notifySettingsUnknown()
					&& ((!channel->notifyMuteUntil()
						&& _defaultBroadcastNotifySettings.muteUntil())
						|| (!channel->notifySilentPosts()
							&& _defaultBroadcastNotifySettings.silentPosts()))) {
					updateNotifySettingsLocal(channel);
				}
			});
		}
	} break;
	case mtpc_notifyPeer: {
		const auto &data = notifyPeer.c_notifyPeer();
		if (const auto peer = peerLoaded(peerFromMTP(data.vpeer()))) {
			if (peer->notifyChange(settings)) {
				updateNotifySettingsLocal(peer);
			}
		}
	} break;
	}
}

void Session::updateNotifySettings(
		not_null<PeerData*> peer,
		std::optional<int> muteForSeconds,
		std::optional<bool> silentPosts) {
	if (peer->notifyChange(muteForSeconds, silentPosts)) {
		updateNotifySettingsLocal(peer);
		_session->api().updateNotifySettingsDelayed(peer);
	}
}

bool Session::notifyIsMuted(
		not_null<const PeerData*> peer,
		crl::time *changesIn) const {
	const auto resultFromUntil = [&](TimeId until) {
		const auto now = base::unixtime::now();
		const auto result = (until > now) ? (until - now) : 0;
		if (changesIn) {
			*changesIn = (result > 0)
				? std::min(result * crl::time(1000), kMaxNotifyCheckDelay)
				: kMaxNotifyCheckDelay;
		}
		return (result > 0);
	};
	if (const auto until = peer->notifyMuteUntil()) {
		return resultFromUntil(*until);
	}
	const auto &settings = defaultNotifySettings(peer);
	if (const auto until = settings.muteUntil()) {
		return resultFromUntil(*until);
	}
	return true;
}

bool Session::notifySilentPosts(not_null<const PeerData*> peer) const {
	if (const auto silent = peer->notifySilentPosts()) {
		return *silent;
	}
	const auto &settings = defaultNotifySettings(peer);
	if (const auto silent = settings.silentPosts()) {
		return *silent;
	}
	return false;
}

bool Session::notifyMuteUnknown(not_null<const PeerData*> peer) const {
	if (peer->notifySettingsUnknown()) {
		return true;
	} else if (const auto nonDefault = peer->notifyMuteUntil()) {
		return false;
	}
	return defaultNotifySettings(peer).settingsUnknown();
}

bool Session::notifySilentPostsUnknown(
		not_null<const PeerData*> peer) const {
	if (peer->notifySettingsUnknown()) {
		return true;
	} else if (const auto nonDefault = peer->notifySilentPosts()) {
		return false;
	}
	return defaultNotifySettings(peer).settingsUnknown();
}

bool Session::notifySettingsUnknown(not_null<const PeerData*> peer) const {
	return notifyMuteUnknown(peer) || notifySilentPostsUnknown(peer);
}

rpl::producer<> Session::defaultUserNotifyUpdates() const {
	return _defaultUserNotifyUpdates.events();
}

rpl::producer<> Session::defaultChatNotifyUpdates() const {
	return _defaultChatNotifyUpdates.events();
}

rpl::producer<> Session::defaultBroadcastNotifyUpdates() const {
	return _defaultBroadcastNotifyUpdates.events();
}

rpl::producer<> Session::defaultNotifyUpdates(
		not_null<const PeerData*> peer) const {
	return peer->isUser()
		? defaultUserNotifyUpdates()
		: (peer->isChat() || peer->isMegagroup())
		? defaultChatNotifyUpdates()
		: defaultBroadcastNotifyUpdates();
}

void Session::serviceNotification(
		const TextWithEntities &message,
		const MTPMessageMedia &media) {
	const auto date = base::unixtime::now();
	if (!peerLoaded(PeerData::kServiceNotificationsId)) {
		processUser(MTP_user(
			MTP_flags(
				MTPDuser::Flag::f_first_name
				| MTPDuser::Flag::f_phone
				| MTPDuser::Flag::f_status
				| MTPDuser::Flag::f_verified),
			MTP_int(peerToUser(PeerData::kServiceNotificationsId)),
			MTPlong(), // access_hash
			MTP_string("Telegram"),
			MTPstring(), // last_name
			MTPstring(), // username
			MTP_string("42777"),
			MTP_userProfilePhotoEmpty(),
			MTP_userStatusRecently(),
			MTPint(), // bot_info_version
			MTPVector<MTPRestrictionReason>(),
			MTPstring(), // bot_inline_placeholder
			MTPstring())); // lang_code
	}
	const auto history = this->history(PeerData::kServiceNotificationsId);
	if (!history->folderKnown()) {
		_session->api().requestDialogEntry(history, [=] {
			insertCheckedServiceNotification(message, media, date);
		});
	} else {
		insertCheckedServiceNotification(message, media, date);
	}
}

void Session::checkNewAuthorization() {
	_newAuthorizationChecks.fire({});
}

rpl::producer<> Session::newAuthorizationChecks() const {
	return _newAuthorizationChecks.events();
}

void Session::insertCheckedServiceNotification(
		const TextWithEntities &message,
		const MTPMessageMedia &media,
		TimeId date) {
	const auto history = this->history(PeerData::kServiceNotificationsId);
	const auto flags = MTPDmessage::Flag::f_entities
		| MTPDmessage::Flag::f_from_id
		| MTPDmessage::Flag::f_media;
	const auto clientFlags = MTPDmessage_ClientFlag::f_clientside_unread
		| MTPDmessage_ClientFlag::f_local_history_entry;
	auto sending = TextWithEntities(), left = message;
	while (TextUtilities::CutPart(sending, left, MaxMessageSize)) {
		addNewMessage(
			MTP_message(
				MTP_flags(flags),
				MTP_int(nextLocalMessageId()),
				MTP_int(peerToUser(PeerData::kServiceNotificationsId)),
				MTP_peerUser(MTP_int(_session->userId())),
				MTPMessageFwdHeader(),
				MTPint(),
				MTPint(),
				MTP_int(date),
				MTP_string(sending.text),
				media,
				MTPReplyMarkup(),
				TextUtilities::EntitiesToMTP(sending.entities),
				MTPint(),
				MTPint(),
				MTPstring(),
				MTPlong(),
				//MTPMessageReactions(),
				MTPVector<MTPRestrictionReason>()),
			clientFlags,
			NewMessageType::Unread);
	}
	sendHistoryChangeNotifications();
}

void Session::setMimeForwardIds(MessageIdsList &&list) {
	_mimeForwardIds = std::move(list);
}

MessageIdsList Session::takeMimeForwardIds() {
	return std::move(_mimeForwardIds);
}

void Session::setProxyPromoted(PeerData *promoted) {
	if (_proxyPromoted != promoted) {
		if (const auto history = historyLoaded(_proxyPromoted)) {
			history->cacheProxyPromoted(false);
		}
		const auto old = std::exchange(_proxyPromoted, promoted);
		if (_proxyPromoted) {
			const auto history = this->history(_proxyPromoted);
			history->cacheProxyPromoted(true);
			history->requestChatListMessage();
			Notify::peerUpdatedDelayed(
				_proxyPromoted,
				Notify::PeerUpdate::Flag::ChannelPromotedChanged);
		}
		if (old) {
			Notify::peerUpdatedDelayed(
				old,
				Notify::PeerUpdate::Flag::ChannelPromotedChanged);
		}
	}
}

PeerData *Session::proxyPromoted() const {
	return _proxyPromoted;
}

bool Session::updateWallpapers(const MTPaccount_WallPapers &data) {
	return data.match([&](const MTPDaccount_wallPapers &data) {
		setWallpapers(data.vwallpapers().v, data.vhash().v);
		return true;
	}, [&](const MTPDaccount_wallPapersNotModified &) {
		return false;
	});
}

void Session::setWallpapers(const QVector<MTPWallPaper> &data, int32 hash) {
	_wallpapersHash = hash;

	_wallpapers.clear();
	_wallpapers.reserve(data.size() + 2);

	_wallpapers.push_back(Data::Legacy1DefaultWallPaper());
	_wallpapers.back().setLocalImageAsThumbnail(std::make_shared<Image>(
		std::make_unique<Images::LocalFileSource>(
			qsl(":/gui/art/bg_initial.jpg"),
			QByteArray(),
			"JPG")));
	for (const auto &paper : data) {
		paper.match([&](const MTPDwallPaper &paper) {
			if (const auto parsed = Data::WallPaper::Create(paper)) {
				_wallpapers.push_back(*parsed);
			}
		});
	}
	const auto defaultFound = ranges::find_if(
		_wallpapers,
		Data::IsDefaultWallPaper);
	if (defaultFound == end(_wallpapers)) {
		_wallpapers.push_back(Data::DefaultWallPaper());
		_wallpapers.back().setLocalImageAsThumbnail(std::make_shared<Image>(
			std::make_unique<Images::LocalFileSource>(
				qsl(":/gui/arg/bg.jpg"),
				QByteArray(),
				"JPG")));
	}
}

void Session::removeWallpaper(const WallPaper &paper) {
	const auto i = ranges::find(_wallpapers, paper.id(), &WallPaper::id);
	if (i != end(_wallpapers)) {
		_wallpapers.erase(i);
	}
}

const std::vector<WallPaper> &Session::wallpapers() const {
	return _wallpapers;
}

int32 Session::wallpapersHash() const {
	return _wallpapersHash;
}

void Session::clearLocalStorage() {
	clear();

	_cache->close();
	_cache->clear();
}

} // namespace Data
