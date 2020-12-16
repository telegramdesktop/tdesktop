/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_session.h"

#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "main/main_account.h"
#include "apiwrap.h"
#include "mainwidget.h"
#include "api/api_text_entities.h"
#include "core/application.h"
#include "core/mime_type.h" // Core::IsMimeSticker
#include "core/crash_reports.h" // CrashReports::SetAnnotation
#include "ui/image/image.h"
#include "ui/image/image_location_factory.h" // Images::FromPhotoSize
#include "export/export_manager.h"
#include "export/view/export_view_panel_controller.h"
#include "mtproto/mtproto_config.h"
#include "window/notifications_manager.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "history/view/media/history_view_media.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_send_action.h"
#include "inline_bots/inline_bot_layout_item.h"
#include "storage/storage_account.h"
#include "storage/storage_encrypted_file.h"
#include "media/player/media_player_instance.h" // instance()->play()
#include "media/audio/media_audio.h"
#include "boxes/abstract_box.h"
#include "passport/passport_form_controller.h"
#include "window/themes/window_theme.h"
#include "lang/lang_keys.h" // tr::lng_deleted(tr::now) in user name
#include "data/stickers/data_stickers.h"
#include "data/data_changes.h"
#include "data/data_group_call.h"
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
#include "data/data_chat_filters.h"
#include "data/data_scheduled_messages.h"
#include "data/data_cloud_themes.h"
#include "data/data_streaming.h"
#include "data/data_media_rotation.h"
#include "data/data_histories.h"
#include "base/platform/base_platform_info.h"
#include "base/unixtime.h"
#include "base/call_delayed.h"
#include "facades.h" // Notify::switchInlineBotButtonReceived
#include "app.h"
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
const auto InlineLevels = "i"_q;
const auto SmallLevels = "sa"_q;
const auto ThumbnailLevels = "mbsa"_q;
const auto LargeLevels = "ydxcwmbsa"_q;

void CheckForSwitchInlineButton(not_null<HistoryItem*> item) {
	if (item->out() || !item->hasSwitchInlineButton()) {
		return;
	}
	if (const auto user = item->history()->peer->asUser()) {
		if (!user->isBot() || !user->botInfo->inlineReturnTo.key) {
			return;
		}
		if (const auto markup = item->Get<HistoryMessageReplyMarkup>()) {
			for (const auto &row : markup->rows) {
				for (const auto &button : row) {
					using ButtonType = HistoryMessageMarkupButton::Type;
					if (button.type == ButtonType::SwitchInline) {
						Notify::switchInlineBotButtonReceived(
							&item->history()->session(),
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
std::vector<UnavailableReason> ExtractUnavailableReasons(
		const QVector<MTPRestrictionReason> &restrictions) {
	return ranges::view::all(
		restrictions
	) | ranges::view::filter([](const MTPRestrictionReason &restriction) {
		return restriction.match([&](const MTPDrestrictionReason &data) {
			const auto platform = qs(data.vplatform());
			return false
#ifdef OS_MAC_STORE
				|| (platform == qstr("ios"))
#elif defined OS_WIN_STORE // OS_MAC_STORE
				|| (platform == qstr("ms"))
#endif // OS_MAC_STORE || OS_WIN_STORE
				|| (platform == qstr("all"));
		});
	}) | ranges::view::transform([](const MTPRestrictionReason &restriction) {
		return restriction.match([&](const MTPDrestrictionReason &data) {
			return UnavailableReason{ qs(data.vreason()), qs(data.vtext()) };
		});
	}) | ranges::to_vector;
}

[[nodiscard]] QByteArray FindInlineThumbnail(
		const QVector<MTPPhotoSize> &sizes) {
	const auto i = ranges::find(
		sizes,
		mtpc_photoStrippedSize,
		&MTPPhotoSize::type);
	return (i != sizes.end())
		? i->c_photoStrippedSize().vbytes().v
		: QByteArray();
}

[[nodiscard]] QByteArray FindDocumentInlineThumbnail(const MTPDdocument &data) {
	return FindInlineThumbnail(data.vthumbs().value_or_empty());
}

[[nodiscard]] MTPPhotoSize FindDocumentThumbnail(const MTPDdocument &data) {
	const auto area = [](const MTPPhotoSize &size) {
		static constexpr auto kInvalid = 0;
		return size.match([](const MTPDphotoSizeEmpty &) {
			return kInvalid;
		}, [](const MTPDphotoStrippedSize &) {
			return kInvalid;
		}, [](const MTPDphotoPathSize &) {
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

[[nodiscard]] std::optional<MTPVideoSize> FindDocumentVideoThumbnail(
		const MTPDdocument &data) {
	const auto area = [](const MTPVideoSize &size) {
		static constexpr auto kInvalid = 0;
		return size.match([](const MTPDvideoSize &data) {
			return (data.vw().v * data.vh().v);
		});
	};
	const auto thumbs = data.vvideo_thumbs();
	if (!thumbs) {
		return std::nullopt;
	}
	const auto &list = thumbs->v;
	const auto i = ranges::max_element(list, std::less<>(), area);
	return (i != list.end() && area(*i) > 0)
		? std::make_optional(*i)
		: std::nullopt;
}

[[nodiscard]] QByteArray FindPhotoInlineThumbnail(const MTPDphoto &data) {
	return FindInlineThumbnail(data.vsizes().v);
}

[[nodiscard]] int VideoStartTime(const MTPDvideoSize &data) {
	return int(
		std::clamp(
			std::floor(data.vvideo_start_ts().value_or_empty() * 1000),
			0.,
			double(std::numeric_limits<int>::max())));
}

} // namespace

Session::Session(not_null<Main::Session*> session)
: _session(session)
, _cache(Core::App().databases().get(
	_session->local().cachePath(),
	_session->local().cacheSettings()))
, _bigFileCache(Core::App().databases().get(
	_session->local().cacheBigFilePath(),
	_session->local().cacheBigFileSettings()))
, _chatsList(
	session,
	FilterId(),
	session->serverConfig().pinnedDialogsCountMax.value())
, _contactsList(Dialogs::SortMode::Name)
, _contactsNoChatsList(Dialogs::SortMode::Name)
, _selfDestructTimer([=] { checkSelfDestructItems(); })
, _sendActionsAnimation([=](crl::time now) {
	return sendActionsAnimationCallback(now);
})
, _pollsClosingTimer([=] { checkPollsClosings(); })
, _unmuteByFinishedTimer([=] { unmuteByFinished(); })
, _groups(this)
, _chatsFilters(std::make_unique<ChatFilters>(this))
, _scheduledMessages(std::make_unique<ScheduledMessages>(this))
, _cloudThemes(std::make_unique<CloudThemes>(session))
, _streaming(std::make_unique<Streaming>(this))
, _mediaRotation(std::make_unique<MediaRotation>())
, _histories(std::make_unique<Histories>(this))
, _stickers(std::make_unique<Stickers>(this)) {
	_cache->open(_session->local().cacheKey());
	_bigFileCache->open(_session->local().cacheBigFileKey());

	if constexpr (Platform::IsLinux()) {
		const auto wasVersion = _session->local().oldMapVersion();
		if (wasVersion >= 1007011 && wasVersion < 1007015) {
			_bigFileCache->clear();
			_cache->clearByTag(Data::kImageCacheTag);
		}
	}

	setupMigrationViewer();
	setupChannelLeavingViewer();
	setupPeerNameViewer();
	setupUserIsContactViewer();

	_chatsList.unreadStateChanges(
	) | rpl::start_with_next([=] {
		notifyUnreadBadgeChanged();
	}, _lifetime);

	_chatsFilters->changed(
	) | rpl::start_with_next([=] {
		const auto enabled = !_chatsFilters->list().empty();
		if (enabled != session->settings().dialogsFiltersEnabled()) {
			session->settings().setDialogsFiltersEnabled(enabled);
			session->saveSettingsDelayed();
		}
	}, _lifetime);

}

void Session::clear() {
	// Optimization: clear notifications before destroying items.
	Core::App().notifications().clearFromSession(_session);

	_sendActions.clear();

	_histories->unloadAll();
	_scheduledMessages = nullptr;
	_dependentMessages.clear();
	base::take(_messages);
	base::take(_channelMessages);
	_messageByRandomId.clear();
	_sentMessagesData.clear();
	cSetRecentInlineBots(RecentInlineBots());
	cSetRecentStickers(RecentStickerPack());
	App::clearMousedItems();
	_histories->clearAll();
	_webpages.clear();
	_locations.clear();
	_polls.clear();
	_games.clear();
	_documents.clear();
	_photos.clear();
}

void Session::keepAlive(std::shared_ptr<PhotoMedia> media) {
	// NB! This allows PhotoMedia to outlive Main::Session!
	// In case this is a problem this code should be rewritten.
	crl::on_main(&session(), [media = std::move(media)]{});
}

void Session::keepAlive(std::shared_ptr<DocumentMedia> media) {
	// NB! This allows DocumentMedia to outlive Main::Session!
	// In case this is a problem this code should be rewritten.
	crl::on_main(&session(), [media = std::move(media)] {});
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
	} else if (!i->second->isFullLoaded()) {
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

	using UpdateFlag = PeerUpdate::Flag;
	auto flags = UpdateFlag::None | UpdateFlag::None;
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
			flags |= UpdateFlag::PhoneNumber;
		}
		result->setBotInfoVersion(-1);
		status = &emptyStatus;
		result->setIsContact(false);
		if (canShareThisContact != result->canShareThisContactFast()) {
			flags |= UpdateFlag::CanShareContact;
		}
	}, [&](const MTPDuser &data) {
		minimal = data.is_min();

		const auto canShareThisContact = result->canShareThisContactFast();
		if (minimal) {
			const auto mask = 0
				//| MTPDuser_ClientFlag::f_inaccessible
				| MTPDuser::Flag::f_deleted;
			result->setFlags((result->flags() & ~mask) | (data.vflags().v & mask));
			if (result->input.type() == mtpc_inputPeerEmpty) {
				result->input = MTP_inputPeerUser(
					data.vid(),
					MTP_long(data.vaccess_hash().value_or_empty()));
			}
			if (result->inputUser.type() == mtpc_inputUserEmpty) {
				result->inputUser = MTP_inputUser(
					data.vid(),
					MTP_long(data.vaccess_hash().value_or_empty()));
			}
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
				result->setUnavailableReasons(
					ExtractUnavailableReasons(restriction->v));
			} else {
				result->setUnavailableReasons({});
			}
		}
		if (data.is_deleted()) {
			if (!result->phone().isEmpty()) {
				result->setPhone(QString());
				flags |= UpdateFlag::PhoneNumber;
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
				flags |= UpdateFlag::PhoneNumber;
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

			// see also Serialize::readPeer

			const auto pname = (showPhoneChanged || phoneChanged || nameChanged)
				? ((showPhone && !phone.isEmpty())
					? App::formatPhone(phone)
					: QString())
				: result->nameOrPhone;

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
					flags |= UpdateFlag::BotCanBeInvited;
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
			flags |= UpdateFlag::CanShareContact;
		}
	});

	if (minimal) {
		if (!result->isMinimalLoaded()) {
			result->setLoadedStatus(PeerData::LoadedStatus::Minimal);
		}
	} else if (!result->isFullLoaded()
		&& (!result->isSelf() || !result->phone().isEmpty())) {
		result->setLoadedStatus(PeerData::LoadedStatus::Full);
	}

	if (status && !minimal) {
		const auto oldOnlineTill = result->onlineTill;
		const auto newOnlineTill = ApiWrap::OnlineTillFromStatus(
			*status,
			oldOnlineTill);
		if (oldOnlineTill != newOnlineTill) {
			result->onlineTill = newOnlineTill;
			flags |= UpdateFlag::OnlineStatus;
		}
	}

	if (flags) {
		session().changes().peerUpdated(result, flags);
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

	using UpdateFlag = Data::PeerUpdate::Flag;
	auto flags = UpdateFlag::None | UpdateFlag::None;
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
					channel->setAccessHash(input.vaccess_hash().v);
				}
				ApplyMigration(chat, channel);
			}, [](const MTPDinputChannelFromMessage &) {
				LOG(("API Error: "
					"migrated_to contains channel from message."));
			}, [](const MTPDinputChannelEmpty &) {
			});
		}

		const auto callFlag = MTPDchat::Flag::f_call_not_empty;
		const auto callNotEmpty = (data.vflags().v & callFlag)
			|| (chat->groupCall()
				&& chat->groupCall()->fullCount() > 0);
		chat->setFlags(data.vflags().v
			| (callNotEmpty ? callFlag : MTPDchat::Flag(0)));
		chat->count = data.vparticipants_count().v;

		if (canAddMembers != chat->canAddMembers()) {
			flags |= UpdateFlag::Rights;
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
			flags |= UpdateFlag::Rights;
		}
	}, [&](const MTPDchannel &data) {
		const auto channel = result->asChannel();

		minimal = data.is_min();
		if (minimal && !result->isFullLoaded()) {
			LOG(("API Warning: not loaded minimal channel applied."));
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
		const auto callFlag = MTPDchannel::Flag::f_call_not_empty;
		const auto callNotEmpty = (data.vflags().v & callFlag)
			|| (channel->groupCall()
				&& channel->groupCall()->fullCount() > 0);
		if (minimal) {
			auto mask = 0
				| MTPDchannel::Flag::f_broadcast
				| MTPDchannel::Flag::f_verified
				| MTPDchannel::Flag::f_megagroup
				| MTPDchannel::Flag::f_call_active
				| MTPDchannel::Flag::f_call_not_empty
				| MTPDchannel_ClientFlag::f_forbidden;
			channel->setFlags((channel->flags() & ~mask)
				| (data.vflags().v & mask)
				| (callNotEmpty ? callFlag : MTPDchannel::Flag(0)));
			if (channel->input.type() == mtpc_inputPeerEmpty
				|| channel->inputChannel.type() == mtpc_inputChannelEmpty) {
				channel->setAccessHash(data.vaccess_hash().value_or_empty());
			}
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
			channel->setAccessHash(
				data.vaccess_hash().value_or(channel->access));
			channel->date = data.vdate().v;
			if (channel->version() < data.vversion().v) {
				channel->setVersion(data.vversion().v);
			}
			if (const auto restriction = data.vrestriction_reason()) {
				channel->setUnavailableReasons(
					ExtractUnavailableReasons(restriction->v));
			} else {
				channel->setUnavailableReasons({});
			}
			channel->setFlags(data.vflags().v
				| (callNotEmpty ? callFlag : MTPDchannel::Flag(0)));
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
			flags |= UpdateFlag::ChannelAmIn;
		}
		if (canViewAdmins != channel->canViewAdmins()
			|| canViewMembers != channel->canViewMembers()
			|| canAddMembers != channel->canAddMembers()) {
			flags |= UpdateFlag::Rights;
		}
	}, [&](const MTPDchannelForbidden &data) {
		const auto channel = result->asChannel();

		auto wasInChannel = channel->amIn();
		auto canViewAdmins = channel->canViewAdmins();
		auto canViewMembers = channel->canViewMembers();
		auto canAddMembers = channel->canAddMembers();

		auto mask = mtpCastFlags(MTPDchannelForbidden::Flag::f_broadcast | MTPDchannelForbidden::Flag::f_megagroup);
		channel->setFlags((channel->flags() & ~mask) | (mtpCastFlags(data.vflags()) & mask) | MTPDchannel_ClientFlag::f_forbidden);

		if (channel->hasAdminRights()) {
			channel->setAdminRights(MTP_chatAdminRights(MTP_flags(0)));
		}
		if (channel->hasRestrictions()) {
			channel->setRestrictions(MTP_chatBannedRights(MTP_flags(0), MTP_int(0)));
		}

		channel->setName(qs(data.vtitle()), QString());

		channel->setAccessHash(data.vaccess_hash().v);
		channel->setPhoto(MTP_chatPhotoEmpty());
		channel->date = 0;
		channel->setMembersCount(0);

		if (wasInChannel != channel->amIn()) {
			flags |= UpdateFlag::ChannelAmIn;
		}
		if (canViewAdmins != channel->canViewAdmins()
			|| canViewMembers != channel->canViewMembers()
			|| canAddMembers != channel->canAddMembers()) {
			flags |= UpdateFlag::Rights;
		}
	}, [](const MTPDchatEmpty &) {
	});

	if (minimal) {
		if (!result->isMinimalLoaded()) {
			result->setLoadedStatus(PeerData::LoadedStatus::Minimal);
		}
	} else if (!result->isFullLoaded()) {
		result->setLoadedStatus(PeerData::LoadedStatus::Full);
	}
	if (flags) {
		session().changes().peerUpdated(result, flags);
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

void Session::registerGroupCall(not_null<GroupCall*> call) {
	_groupCalls.emplace(call->id(), call);
}

void Session::unregisterGroupCall(not_null<GroupCall*> call) {
	_groupCalls.remove(call->id());
}

GroupCall *Session::groupCall(uint64 callId) const {
	const auto i = _groupCalls.find(callId);
	return (i != end(_groupCalls)) ? i->second.get() : nullptr;
}

auto Session::invitedToCallUsers(uint64 callId) const
-> const base::flat_set<not_null<UserData*>> & {
	static const base::flat_set<not_null<UserData*>> kEmpty;
	const auto i = _invitedToCallUsers.find(callId);
	return (i != _invitedToCallUsers.end()) ? i->second : kEmpty;
}

void Session::registerInvitedToCallUser(
		uint64 callId,
		not_null<PeerData*> peer,
		not_null<UserData*> user) {
	const auto call = peer->groupCall();
	if (call && call->id() == callId) {
		const auto inCall = ranges::contains(
			call->participants(),
			user,
			&Data::GroupCall::Participant::user);
		if (inCall) {
			return;
		}
	}
	_invitedToCallUsers[callId].emplace(user);
	_invitesToCalls.fire({ callId, user });
}

void Session::unregisterInvitedToCallUser(
		uint64 callId,
		not_null<UserData*> user) {
	const auto i = _invitedToCallUsers.find(callId);
	if (i != _invitedToCallUsers.end()) {
		i->second.remove(user);
		if (i->second.empty()) {
			_invitedToCallUsers.erase(i);
		}
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
	return _histories->findOrCreate(peerId);
}

History *Session::historyLoaded(PeerId peerId) const {
	return _histories->find(peerId);
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
			setChatPinned(history, FilterId(), false);
		}
		removeChatListEntry(history);
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

void Session::cancelForwarding(not_null<History*> history) {
	history->setForwardDraft({});
	session().changes().historyUpdated(
		history,
		Data::HistoryUpdate::Flag::ForwardDraft);
}

HistoryView::SendActionPainter *Session::lookupSendActionPainter(
		not_null<History*> history,
		MsgId rootId) {
	if (!rootId) {
		return history->sendActionPainter();
	}
	const auto i = _sendActionPainters.find(history);
	if (i == end(_sendActionPainters)) {
		return nullptr;
	}
	const auto j = i->second.find(rootId);
	if (j == end(i->second)) {
		return nullptr;
	}
	const auto result = j->second.lock();
	if (!result) {
		i->second.erase(j);
		if (i->second.empty()) {
			_sendActionPainters.erase(i);
		}
		return nullptr;
	}
	crl::on_main([copy = result] {
	});
	return result.get();
}

void Session::registerSendAction(
		not_null<History*> history,
		MsgId rootId,
		not_null<UserData*> user,
		const MTPSendMessageAction &action,
		TimeId when) {
	if (history->peer->isSelf()) {
		return;
	}
	const auto sendAction = lookupSendActionPainter(history, rootId);
	if (!sendAction) {
		return;
	}
	if (sendAction->updateNeedsAnimating(user, action)) {
		user->madeAction(when);

		const auto i = _sendActions.find(std::pair{ history, rootId });
		if (!_sendActions.contains(std::pair{ history, rootId })) {
			_sendActions.emplace(std::pair{ history, rootId }, crl::now());
			_sendActionsAnimation.start();
		}
	}
}

auto Session::repliesSendActionPainter(
	not_null<History*> history,
	MsgId rootId)
-> std::shared_ptr<SendActionPainter> {
	auto &weak = _sendActionPainters[history][rootId];
	if (auto strong = weak.lock()) {
		return strong;
	}
	auto result = std::make_shared<SendActionPainter>(history);
	weak = result;
	return result;
}

void Session::repliesSendActionPainterRemoved(
		not_null<History*> history,
		MsgId rootId) {
	const auto i = _sendActionPainters.find(history);
	if (i == end(_sendActionPainters)) {
		return;
	}
	const auto j = i->second.find(rootId);
	if (j == end(i->second) || j->second.lock()) {
		return;
	}
	i->second.erase(j);
	if (i->second.empty()) {
		_sendActionPainters.erase(i);
	}
}

void Session::repliesSendActionPaintersClear(
		not_null<History*> history,
		not_null<UserData*> user) {
	auto &map = _sendActionPainters[history];
	for (auto i = map.begin(); i != map.end();) {
		if (auto strong = i->second.lock()) {
			strong->clear(user);
			++i;
		} else {
			i = map.erase(i);
		}
	}
	if (map.empty()) {
		_sendActionPainters.erase(history);
	}
}

bool Session::sendActionsAnimationCallback(crl::time now) {
	for (auto i = begin(_sendActions); i != end(_sendActions);) {
		const auto sendAction = lookupSendActionPainter(
			i->first.first,
			i->first.second);
		if (sendAction && sendAction->updateNeedsAnimating(now)) {
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
		folder->chatsList()->setLoaded();
	} else {
		_chatsList.setLoaded();
	}
	_chatsListLoadedEvents.fire_copy(folder);
}

void Session::userIsBotChanged(not_null<UserData*> user) {
	if (const auto history = this->history(user)) {
		chatsFilters().refreshHistory(history);
	}
	_userIsBotChanges.fire_copy(user);
}

rpl::producer<not_null<UserData*>> Session::userIsBotChanges() const {
	return _userIsBotChanges.events();
}

void Session::botCommandsChanged(not_null<UserData*> user) {
	_botCommandsChanges.fire_copy(user);
}

rpl::producer<not_null<UserData*>> Session::botCommandsChanges() const {
	return _botCommandsChanges.events();
}

Storage::Cache::Database &Session::cache() {
	return *_cache;
}

Storage::Cache::Database &Session::cacheBigFile() {
	return *_bigFileCache;
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
		base::call_delayed(
			std::min(left + 5, 3600) * crl::time(1000),
			_session,
			[=] { suggestStartExport(); });
	} else if (Core::App().exportManager().inProgress()) {
		Export::View::ClearSuggestStart(&session());
	} else {
		_exportSuggestion = Export::View::SuggestStart(&session());
	}
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
	base::call_delayed(rememberFor, _session, [=, check = generation] {
		if (_passportCredentials && _passportCredentials->second == check) {
			forgetPassportCredentials();
		}
	});
}

void Session::forgetPassportCredentials() {
	_passportCredentials = nullptr;
}

void Session::setupMigrationViewer() {
	session().changes().peerUpdates(
		PeerUpdate::Flag::Migration
	) | rpl::map([](const PeerUpdate &update) {
		return update.peer->asChat();
	}) | rpl::filter([=](ChatData *chat) {
		return (chat != nullptr);
	}) | rpl::start_with_next([=](not_null<ChatData*> chat) {
		const auto channel = chat->migrateTo();
		if (!channel) {
			return;
		}

		chat->clearGroupCall();
		if (const auto from = historyLoaded(chat)) {
			if (const auto to = historyLoaded(channel)) {
				if (to->inChatList() && from->inChatList()) {
					removeChatListEntry(from);
				}
			}
		}
	}, _lifetime);
}

void Session::setupChannelLeavingViewer() {
	session().changes().peerUpdates(
		PeerUpdate::Flag::ChannelAmIn
	) | rpl::map([](const PeerUpdate &update) {
		return update.peer->asChannel();
	}) | rpl::start_with_next([=](not_null<ChannelData*> channel) {
		if (channel->amIn()) {
			channel->clearInvitePeek();
		} else {
//			channel->clearFeed(); // #feed
			if (const auto history = historyLoaded(channel->id)) {
				history->removeJoinedMessage();
				history->updateChatListExistence();
				history->updateChatListSortPosition();
			}
		}
	}, _lifetime);
}

void Session::setupPeerNameViewer() {
	session().changes().realtimeNameUpdates(
	) | rpl::start_with_next([=](const NameUpdate &update) {
		const auto peer = update.peer;
		const auto &oldLetters = update.oldFirstLetters;
		_contactsNoChatsList.peerNameChanged(peer, oldLetters);
		_contactsList.peerNameChanged(peer, oldLetters);
	}, _lifetime);
}

void Session::setupUserIsContactViewer() {
	session().changes().peerUpdates(
		PeerUpdate::Flag::IsContact
	) | rpl::map([](const PeerUpdate &update) {
		return update.peer->asUser();
	}) | rpl::start_with_next([=](not_null<UserData*> user) {
		const auto i = _contactViews.find(peerToUser(user->id));
		if (i != _contactViews.end()) {
			for (const auto view : i->second) {
				requestViewResize(view);
			}
		}
		if (!user->isFullLoaded()) {
			LOG(("API Error: "
				"userIsContactChanged() called for a not loaded user!"));
			return;
		}
		if (user->isContact()) {
			const auto history = this->history(user->id);
			_contactsList.addByName(history);
			if (!history->inChatList()) {
				_contactsNoChatsList.addByName(history);
			}
		} else if (const auto history = historyLoaded(user)) {
			_contactsNoChatsList.del(history);
			_contactsList.del(history);
		}
	}, _lifetime);
}

Session::~Session() = default;

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

void Session::requestPhotoViewRepaint(not_null<const PhotoData*> photo) {
	const auto i = _photoItems.find(photo);
	if (i != end(_photoItems)) {
		for (const auto item : i->second) {
			requestItemRepaint(item);
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

void Session::requestPollViewRepaint(not_null<const PollData*> poll) {
	if (const auto i = _pollViews.find(poll); i != _pollViews.end()) {
		for (const auto view : i->second) {
			requestViewResize(view);
		}
	}
}

void Session::documentLoadProgress(not_null<DocumentData*> document) {
	requestDocumentViewRepaint(document);
	session().documentUpdated.notify(document, true);

	if (document->isAudioFile()) {
		::Media::Player::instance()->documentLoadProgress(document);
	}
}

void Session::documentLoadDone(not_null<DocumentData*> document) {
	notifyDocumentLayoutChanged(document);
}

void Session::documentLoadFail(
		not_null<DocumentData*> document,
		bool started) {
	notifyDocumentLayoutChanged(document);
}

void Session::photoLoadProgress(not_null<PhotoData*> photo) {
	requestPhotoViewRepaint(photo);
}

void Session::photoLoadDone(not_null<PhotoData*> photo) {
	notifyPhotoLayoutChanged(photo);
}

void Session::photoLoadFail(
		not_null<PhotoData*> photo,
		bool started) {
	notifyPhotoLayoutChanged(photo);
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
		const auto leader = group->items.front();
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
		notifyHistoryChangeDelayed(item->history());
		view->refreshInBlock();
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

rpl::producer<not_null<const HistoryItem*>> Session::itemRemoved(
		FullMsgId itemId) const {
	return itemRemoved(
	) | rpl::filter([=](not_null<const HistoryItem*> item) {
		return (itemId == item->fullId());
	});
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

void Session::notifyPinnedDialogsOrderUpdated() {
	_pinnedDialogsOrderUpdated.fire({});
}

rpl::producer<> Session::pinnedDialogsOrderUpdated() const {
	return _pinnedDialogsOrderUpdated.events();
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
	const auto remove = ranges::count(
		_heavyViewParts,
		delegate,
		[](not_null<ViewElement*> element) { return element->delegate(); });
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

void Session::setChatPinned(
		const Dialogs::Key &key,
		FilterId filterId,
		bool pinned) {
	Expects(key.entry()->folderKnown());

	const auto list = filterId
		? chatsFilters().chatsList(filterId)
		: chatsList(key.entry()->folder());
	list->pinned()->setPinned(key, pinned);
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
		requestFolder->chatsList()->setCloudListSize(*count);
	}
}

void Session::applyDialog(
		Data::Folder *requestFolder,
		const MTPDdialog &data) {
	const auto peerId = peerFromMTP(data.vpeer());
	if (!peerId) {
		return;
	}

	const auto history = this->history(peerId);
	history->applyDialog(requestFolder, data);
	setPinnedFromDialog(history, data.is_pinned());

	if (const auto from = history->peer->migrateFrom()) {
		if (const auto historyFrom = historyLoaded(from)) {
			removeChatListEntry(historyFrom);
		}
	} else if (const auto to = history->peer->migrateTo()) {
		if (to->amIn()) {
			removeChatListEntry(history);
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

int Session::pinnedChatsCount(
		Data::Folder *folder,
		FilterId filterId) const {
	if (!filterId) {
		return pinnedChatsOrder(folder, filterId).size();
	}
	const auto &list = chatsFilters().list();
	const auto i = ranges::find(list, filterId, &Data::ChatFilter::id);
	return (i != end(list)) ? i->pinned().size() : 0;
}

int Session::pinnedChatsLimit(
		Data::Folder *folder,
		FilterId filterId) const {
	if (!filterId) {
		return folder
			? session().serverConfig().pinnedDialogsInFolderMax.current()
			: session().serverConfig().pinnedDialogsCountMax.current();
	}
	const auto &list = chatsFilters().list();
	const auto i = ranges::find(list, filterId, &Data::ChatFilter::id);
	const auto pinned = (i != end(list)) ? i->pinned().size() : 0;
	const auto already = (i != end(list)) ? i->always().size() : 0;
	return Data::ChatFilter::kPinnedLimit + pinned - already;
}

const std::vector<Dialogs::Key> &Session::pinnedChatsOrder(
		Data::Folder *folder,
		FilterId filterId) const {
	const auto list = filterId
		? chatsFilters().chatsList(filterId)
		: chatsList(folder);
	return list->pinned()->order();
}

void Session::clearPinnedChats(Data::Folder *folder) {
	chatsList(folder)->pinned()->clear();
}

void Session::reorderTwoPinnedChats(
		FilterId filterId,
		const Dialogs::Key &key1,
		const Dialogs::Key &key2) {
	Expects(key1.entry()->folderKnown() && key2.entry()->folderKnown());
	Expects(filterId || (key1.entry()->folder() == key2.entry()->folder()));

	const auto list = filterId
		? chatsFilters().chatsList(filterId)
		: chatsList(key1.entry()->folder());
	list->pinned()->reorder(key1, key2);
	notifyPinnedDialogsOrderUpdated();
}

bool Session::checkEntitiesAndViewsUpdate(const MTPDmessage &data) {
	const auto peer = peerFromMTP(data.vpeer_id());
	const auto existing = message(peerToChannel(peer), data.vid().v);
	if (!existing) {
		return false;
	}
	existing->updateSentContent({
		qs(data.vmessage()),
		Api::EntitiesFromMTP(
			&session(),
			data.ventities().value_or_empty())
	}, data.vmedia());
	existing->updateReplyMarkup(data.vreply_markup());
	existing->updateForwardedInfo(data.vfwd_from());
	existing->setViewsCount(data.vviews().value_or(-1));
	if (const auto replies = data.vreplies()) {
		existing->setReplies(*replies);
	} else {
		existing->clearReplies();
	}
	existing->setForwardsCount(data.vforwards().value_or(-1));
	if (const auto reply = data.vreply_to()) {
		reply->match([&](const MTPDmessageReplyHeader &data) {
			existing->setReplyToTop(
				data.vreply_to_top_id().value_or(
					data.vreply_to_msg_id().v));
		});
	}
	existing->setPostAuthor(data.vpost_author().value_or_empty());
	existing->indexAsNewItem();
	existing->contributeToSlowmode(data.vdate().v);
	requestItemTextRefresh(existing);
	updateDependentMessages(existing);
	const auto result = (existing->mainView() != nullptr);
	if (result) {
		stickers().checkSavedGif(existing);
	}
	session().changes().messageUpdated(
		existing,
		Data::MessageUpdate::Flag::NewMaybeAdded);
	return result;
}

void Session::updateEditedMessage(const MTPMessage &data) {
	const auto existing = data.match([](const MTPDmessageEmpty &)
			-> HistoryItem* {
		return nullptr;
	}, [&](const auto &data) {
		const auto peer = peerFromMTP(data.vpeer_id());
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

void Session::registerMessage(not_null<HistoryItem*> item) {
	const auto list = messagesListForInsert(item->channelId());
	const auto itemId = item->id;
	const auto i = list->find(itemId);
	if (i != list->end()) {
		LOG(("App Error: Trying to re-registerMessage()."));
		i->second->destroy();
	}
	list->emplace(itemId, item);
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
			i->second->destroy();
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

void Session::unregisterMessage(not_null<HistoryItem*> item) {
	const auto peerId = item->history()->peer->id;
	_itemRemoved.fire_copy(item);
	session().changes().messageUpdated(
		item,
		Data::MessageUpdate::Flag::Destroyed);
	groups().unregisterMessage(item);
	removeDependencyMessage(item);
	messagesListForInsert(peerToChannel(peerId))->erase(item->id);
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
	session().changes().messageUpdated(
		item,
		Data::MessageUpdate::Flag::Edited);
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
		session().changes().peerUpdated(
			peer,
			PeerUpdate::Flag::Notifications);
	}

	if (muted) {
		_mutedPeers.emplace(peer);
		unmuteByFinishedDelayed(changesIn);
		if (history) {
			Core::App().notifications().clearIncomingFromHistory(history);
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

auto Session::speakingAnimationUpdated() const
-> rpl::producer<not_null<History*>> {
	return _speakingAnimationUpdate.events();
}

void Session::updateSpeakingAnimation(not_null<History*> history) {
	_speakingAnimationUpdate.fire_copy(history);
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
	if (!Core::App().settings().includeMutedCounter()) {
		return false;
	}
	const auto remove = (key && key.entry()->inChatList())
		? key.entry()->chatListUnreadState()
		: Dialogs::UnreadState();
	return computeUnreadBadgeMuted(_chatsList.unreadState() - remove);
}

int Session::unreadOnlyMutedBadge() const {
	const auto state = _chatsList.unreadState();
	return Core::App().settings().countUnreadMessages()
		? state.messagesMuted
		: state.chatsMuted;
}

rpl::producer<> Session::unreadBadgeChanges() const {
	return _unreadBadgeChanges.events();
}

void Session::notifyUnreadBadgeChanged() {
	_unreadBadgeChanges.fire({});
}

int Session::computeUnreadBadge(const Dialogs::UnreadState &state) const {
	const auto all = Core::App().settings().includeMutedCounter();
	return std::max(state.marks - (all ? 0 : state.marksMuted), 0)
		+ (Core::App().settings().countUnreadMessages()
			? std::max(state.messages - (all ? 0 : state.messagesMuted), 0)
			: std::max(state.chats - (all ? 0 : state.chatsMuted), 0));
}

bool Session::computeUnreadBadgeMuted(
		const Dialogs::UnreadState &state) const {
	if (!Core::App().settings().includeMutedCounter()) {
		return false;
	}
	return (state.marksMuted >= state.marks)
		&& (Core::App().settings().countUnreadMessages()
			? (state.messagesMuted >= state.messages)
			: (state.chatsMuted >= state.chats));
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
			? ImageWithLocation()
			: Images::FromImageInMemory(
				i->second.image,
				"JPG",
				i->second.bytes);
	};
	const auto small = image(SmallLevels);
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
			QByteArray(),
			small,
			thumbnail,
			large,
			ImageWithLocation{},
			crl::time(0));
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
		bool hasStickers,
		const QByteArray &inlineThumbnailBytes,
		const ImageWithLocation &small,
		const ImageWithLocation &thumbnail,
		const ImageWithLocation &large,
		const ImageWithLocation &video,
		crl::time videoStartTime) {
	const auto result = photo(id);
	photoApplyFields(
		result,
		access,
		fileReference,
		date,
		dc,
		hasStickers,
		inlineThumbnailBytes,
		small,
		thumbnail,
		large,
		video,
		videoStartTime);
	return result;
}

void Session::photoConvert(
		not_null<PhotoData*> original,
		const MTPPhoto &data) {
	const auto id = data.match([](const auto &data) {
		return data.vid().v;
	});
	const auto idChanged = (original->id != id);
	if (idChanged) {
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
		const ImageLocation &thumbnailLocation) {
	const auto large = Images::FromWebDocument(data);
	if (!large.valid()) {
		return nullptr;
	}
	return photo(
		rand_value<PhotoId>(),
		uint64(0),
		QByteArray(),
		base::unixtime::now(),
		0,
		false,
		QByteArray(),
		ImageWithLocation{},
		ImageWithLocation{ .location = thumbnailLocation },
		ImageWithLocation{ .location = large },
		ImageWithLocation{},
		crl::time(0));
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
	const auto progressive = [&] {
		const auto kInvalidIndex = sizes.size();
		const auto area = [&](const MTPPhotoSize &size) {
			return size.match([](const MTPDphotoSizeProgressive &data) {
				return data.vw().v * data.vh().v;
			}, [](const auto &) {
				return 0;
			});
		};
		const auto found = ranges::max_element(sizes, std::less<>(), area);
		return (found == sizes.end()
			|| found->type() != mtpc_photoSizeProgressive)
			? sizes.end()
			: found;
	}();
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
		return (i == sizes.end())
			? ImageWithLocation()
			: Images::FromPhotoSize(_session, data, *i);
	};
	const auto findVideoSize = [&]() -> std::optional<MTPVideoSize> {
		const auto sizes = data.vvideo_sizes();
		if (!sizes || sizes->v.isEmpty()) {
			return std::nullopt;
		}
		const auto area = [](const MTPVideoSize &size) {
			return size.match([](const MTPDvideoSize &data) {
				return data.vw().v * data.vh().v;
			});
		};
		return *ranges::max_element(
			sizes->v,
			std::greater<>(),
			area);
	};
	const auto useProgressive = (progressive != sizes.end());
	const auto large = useProgressive
		? Images::FromPhotoSize(_session, data, *progressive)
		: image(LargeLevels);
	if (large.location.valid()) {
		const auto video = findVideoSize();
		photoApplyFields(
			photo,
			data.vaccess_hash().v,
			data.vfile_reference().v,
			data.vdate().v,
			data.vdc_id().v,
			data.is_has_stickers(),
			FindPhotoInlineThumbnail(data),
			(useProgressive
				? ImageWithLocation()
				: image(SmallLevels)),
			(useProgressive
				? Images::FromProgressiveSize(_session, *progressive, 1)
				: image(ThumbnailLevels)),
			large,
			(video
				? Images::FromVideoSize(_session, data, *video)
				: ImageWithLocation()),
			(video
				? VideoStartTime(
					*video->match([](const auto &data) { return &data; }))
				: 0));
	}
}

void Session::photoApplyFields(
		not_null<PhotoData*> photo,
		const uint64 &access,
		const QByteArray &fileReference,
		TimeId date,
		int32 dc,
		bool hasStickers,
		const QByteArray &inlineThumbnailBytes,
		const ImageWithLocation &small,
		const ImageWithLocation &thumbnail,
		const ImageWithLocation &large,
		const ImageWithLocation &video,
		crl::time videoStartTime) {
	if (!date) {
		return;
	}
	photo->setRemoteLocation(dc, access, fileReference);
	photo->date = date;
	photo->setHasAttachedStickers(hasStickers);
	photo->updateImages(
		inlineThumbnailBytes,
		small,
		thumbnail,
		large,
		video,
		videoStartTime);
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
	return data.match([&](const MTPDdocument &data) {
		return processDocument(data);
	}, [&](const MTPDdocumentEmpty &data) {
		return document(data.vid().v);
	});
}

not_null<DocumentData*> Session::processDocument(const MTPDdocument &data) {
	const auto result = document(data.vid().v);
	documentApplyFields(result, data);
	return result;
}

not_null<DocumentData*> Session::processDocument(
		const MTPdocument &data,
		const ImageWithLocation &thumbnail) {
	return data.match([&](const MTPDdocument &data) {
		return document(
			data.vid().v,
			data.vaccess_hash().v,
			data.vfile_reference().v,
			data.vdate().v,
			data.vattributes().v,
			qs(data.vmime_type()),
			QByteArray(),
			thumbnail,
			ImageWithLocation(),
			data.vdc_id().v,
			data.vsize().v);
	}, [&](const MTPDdocumentEmpty &data) {
		return document(data.vid().v);
	});
}

not_null<DocumentData*> Session::document(
		DocumentId id,
		const uint64 &access,
		const QByteArray &fileReference,
		TimeId date,
		const QVector<MTPDocumentAttribute> &attributes,
		const QString &mime,
		const QByteArray &inlineThumbnailBytes,
		const ImageWithLocation &thumbnail,
		const ImageWithLocation &videoThumbnail,
		int32 dc,
		int32 size) {
	const auto result = document(id);
	documentApplyFields(
		result,
		access,
		fileReference,
		date,
		attributes,
		mime,
		inlineThumbnailBytes,
		thumbnail,
		videoThumbnail,
		dc,
		size);
	return result;
}

void Session::documentConvert(
		not_null<DocumentData*> original,
		const MTPDocument &data) {
	const auto id = data.match([](const auto &data) {
		return data.vid().v;
	});
	const auto oldCacheKey = original->cacheKey();
	const auto oldGoodKey = original->goodThumbnailCacheKey();
	const auto idChanged = (original->id != id);
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
		cache().moveIfEmpty(oldGoodKey, original->goodThumbnailCacheKey());
		if (stickers().savedGifs().indexOf(original) >= 0) {
			_session->local().writeSavedGifs();
		}
	}
}

DocumentData *Session::documentFromWeb(
		const MTPWebDocument &data,
		const ImageLocation &thumbnailLocation,
		const ImageLocation &videoThumbnailLocation) {
	return data.match([&](const auto &data) {
		return documentFromWeb(
			data,
			thumbnailLocation,
			videoThumbnailLocation);
	});
}

DocumentData *Session::documentFromWeb(
		const MTPDwebDocument &data,
		const ImageLocation &thumbnailLocation,
		const ImageLocation &videoThumbnailLocation) {
	const auto result = document(
		rand_value<DocumentId>(),
		uint64(0),
		QByteArray(),
		base::unixtime::now(),
		data.vattributes().v,
		data.vmime_type().v,
		QByteArray(),
		ImageWithLocation{ .location = thumbnailLocation },
		ImageWithLocation{ .location = videoThumbnailLocation },
		session().mainDcId(),
		int32(0)); // data.vsize().v
	result->setWebLocation(WebFileLocation(
		data.vurl().v,
		data.vaccess_hash().v));
	return result;
}

DocumentData *Session::documentFromWeb(
		const MTPDwebDocumentNoProxy &data,
		const ImageLocation &thumbnailLocation,
		const ImageLocation &videoThumbnailLocation) {
	const auto result = document(
		rand_value<DocumentId>(),
		uint64(0),
		QByteArray(),
		base::unixtime::now(),
		data.vattributes().v,
		data.vmime_type().v,
		QByteArray(),
		ImageWithLocation{ .location = thumbnailLocation },
		ImageWithLocation{ .location = videoThumbnailLocation },
		session().mainDcId(),
		int32(0)); // data.vsize().v
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
	const auto inlineThumbnailBytes = FindDocumentInlineThumbnail(data);
	const auto thumbnailSize = FindDocumentThumbnail(data);
	const auto videoThumbnailSize = FindDocumentVideoThumbnail(data);
	const auto prepared = Images::FromPhotoSize(
		_session,
		data,
		thumbnailSize);
	const auto videoThumbnail = videoThumbnailSize
		? Images::FromVideoSize(_session, data, *videoThumbnailSize)
		: ImageWithLocation();
	documentApplyFields(
		document,
		data.vaccess_hash().v,
		data.vfile_reference().v,
		data.vdate().v,
		data.vattributes().v,
		qs(data.vmime_type()),
		inlineThumbnailBytes,
		prepared,
		videoThumbnail,
		data.vdc_id().v,
		data.vsize().v);
}

void Session::documentApplyFields(
		not_null<DocumentData*> document,
		const uint64 &access,
		const QByteArray &fileReference,
		TimeId date,
		const QVector<MTPDocumentAttribute> &attributes,
		const QString &mime,
		const QByteArray &inlineThumbnailBytes,
		const ImageWithLocation &thumbnail,
		const ImageWithLocation &videoThumbnail,
		int32 dc,
		int32 size) {
	if (!date) {
		return;
	}
	document->date = date;
	document->setMimeString(mime);
	document->updateThumbnails(
		inlineThumbnailBytes,
		thumbnail,
		videoThumbnail);
	document->size = size;
	document->setattributes(attributes);

	// Uses 'type' that is computed from attributes.
	document->recountIsImage();
	if (dc != 0 && access != 0) {
		document->setRemoteLocation(dc, access, fileReference);
	}
}

not_null<WebPageData*> Session::webpage(WebPageId id) {
	auto i = _webpages.find(id);
	if (i == _webpages.cend()) {
		i = _webpages.emplace(
			id,
			std::make_unique<WebPageData>(this, id)).first;
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
			notifyWebPageUpdateDelayed(result);
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
	const auto lookupInAttribute = [&](
			const MTPDwebPageAttributeTheme &data) -> DocumentData* {
		if (const auto documents = data.vdocuments()) {
			for (const auto &document : documents->v) {
				const auto processed = processDocument(document);
				if (processed->isTheme()) {
					return processed;
				}
			}
		}
		return nullptr;
	};
	const auto lookupThemeDocument = [&]() -> DocumentData* {
		if (const auto attributes = data.vattributes()) {
			for (const auto &attribute : attributes->v) {
				const auto result = attribute.match([&](
						const MTPDwebPageAttributeTheme &data) {
					return lookupInAttribute(data);
				});
				if (result) {
					return result;
				}
			}
		}
		return nullptr;
	};
	webpageApplyFields(
		page,
		ParseWebPageType(data),
		qs(data.vurl()),
		qs(data.vdisplay_url()),
		siteName,
		qs(data.vtitle().value_or_empty()),
		description,
		photo ? processPhoto(*photo).get() : nullptr,
		document ? processDocument(*document).get() : lookupThemeDocument(),
		WebPageCollage(this, data),
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
		i = _polls.emplace(id, std::make_unique<PollData>(this, id)).first;
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
		if (result->closeDate > 0 && !result->closed()) {
			_pollsClosings.emplace(result->closeDate, result);
			checkPollsClosings();
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

void Session::checkPollsClosings() {
	const auto now = base::unixtime::now();
	auto closest = 0;
	for (auto i = _pollsClosings.begin(); i != _pollsClosings.end();) {
		if (i->first <= now) {
			if (i->second->closeByTimer()) {
				notifyPollUpdateDelayed(i->second);
			}
			i = _pollsClosings.erase(i);
		} else {
			if (!closest) {
				closest = i->first;
			}
			++i;
		}
	}
	if (closest) {
		_pollsClosingTimer.callOnce((closest - now) * crl::time(1000));
	} else {
		_pollsClosingTimer.cancel();
	}
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

not_null<Data::CloudImage*> Session::location(const LocationPoint &point) {
	const auto i = _locations.find(point);
	if (i != _locations.cend()) {
		return i->second.get();
	}
	const auto location = Data::ComputeLocation(point);
	const auto prepared = ImageWithLocation{
		.location = ImageLocation(
			{ location },
			location.width,
			location.height)
	};
	return _locations.emplace(
		point,
		std::make_unique<Data::CloudImage>(
			_session,
			prepared)).first->second.get();
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
		session().changes().peerUpdated(
			contact,
			PeerUpdate::Flag::CanShareContact);
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
		session().changes().peerUpdated(
			contact,
			PeerUpdate::Flag::CanShareContact);
	}
}

void Session::documentMessageRemoved(not_null<DocumentData*> document) {
	if (_documentItems.find(document) != _documentItems.end()) {
		return;
	}
	if (document->loading()) {
		document->cancel();
	}
}

void Session::checkPlayingAnimations() {
	auto check = base::flat_set<not_null<ViewElement*>>();
	for (const auto view : _heavyViewParts) {
		if (const auto media = view->media()) {
			if (const auto document = media->getDocument()) {
				if (document->isAnimation() || document->isVideoFile()) {
					check.emplace(view);
				}
			} else if (const auto photo = media->getPhoto()) {
				if (photo->hasVideo()) {
					check.emplace(view);
				}
			}
		}
	}
	for (const auto view : check) {
		view->media()->checkAnimation();
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
		_webpageUpdates.fire_copy(page);
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

rpl::producer<not_null<WebPageData*>> Session::webPageUpdates() const {
	return _webpageUpdates.events();
}

void Session::channelDifferenceTooLong(not_null<ChannelData*> channel) {
	_channelDifferenceTooLong.fire_copy(channel);
}

rpl::producer<not_null<ChannelData*>> Session::channelDifferenceTooLong() const {
	return _channelDifferenceTooLong.events();
}

void Session::registerItemView(not_null<ViewElement*> view) {
	_views[view->data()].push_back(view);
}

void Session::unregisterItemView(not_null<ViewElement*> view) {
	Expects(!_heavyViewParts.contains(view));

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

void Session::refreshChatListEntry(Dialogs::Key key) {
	Expects(key.entry()->folderKnown());

	using namespace Dialogs;

	const auto entry = key.entry();
	const auto history = key.history();
	const auto mainList = chatsList(entry->folder());
	auto event = ChatListEntryRefresh{ .key = key };
	const auto creating = event.existenceChanged = !entry->inChatList();
	if (event.existenceChanged) {
		const auto mainRow = entry->addToChatList(0, mainList);
		_contactsNoChatsList.del(key, mainRow);
	} else {
		event.moved = entry->adjustByPosInChatList(0, mainList);
	}
	if (event) {
		_chatListEntryRefreshes.fire(std::move(event));
	}
	if (!history) {
		return;
	}
	for (const auto &filter : _chatsFilters->list()) {
		const auto id = filter.id();
		const auto filterList = chatsFilters().chatsList(id);
		auto event = ChatListEntryRefresh{ .key = key, .filterId = id };
		if (filter.contains(history)) {
			event.existenceChanged = !entry->inChatList(id);
			if (event.existenceChanged) {
				entry->addToChatList(id, filterList);
			} else {
				event.moved = entry->adjustByPosInChatList(id, filterList);
			}
		} else if (entry->inChatList(id)) {
			entry->removeFromChatList(id, filterList);
			event.existenceChanged = true;
		}
		if (event) {
			_chatListEntryRefreshes.fire(std::move(event));
		}
	}

	if (creating) {
		if (const auto from = history->peer->migrateFrom()) {
			if (const auto migrated = historyLoaded(from)) {
				removeChatListEntry(migrated);
			}
		}
	}
}

void Session::removeChatListEntry(Dialogs::Key key) {
	using namespace Dialogs;

	const auto entry = key.entry();
	if (!entry->inChatList()) {
		return;
	}
	Assert(entry->folderKnown());
	for (const auto &filter : _chatsFilters->list()) {
		const auto id = filter.id();
		if (entry->inChatList(id)) {
			entry->removeFromChatList(id, chatsFilters().chatsList(id));
			_chatListEntryRefreshes.fire(ChatListEntryRefresh{
				.key = key,
				.filterId = id,
				.existenceChanged = true
			});
		}
	}
	const auto mainList = chatsList(entry->folder());
	entry->removeFromChatList(0, mainList);
	_chatListEntryRefreshes.fire(ChatListEntryRefresh{
		.key = key,
		.existenceChanged = true
	});
	if (_contactsList.contains(key)) {
		if (!_contactsNoChatsList.contains(key)) {
			_contactsNoChatsList.addByName(key);
		}
	}
	if (const auto history = key.history()) {
		Core::App().notifications().clearFromHistory(history);
	}
}

auto Session::chatListEntryRefreshes() const
-> rpl::producer<ChatListEntryRefresh> {
	return _chatListEntryRefreshes.events();
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

void Session::resetNotifySettingsToDefault(not_null<PeerData*> peer) {
	const auto empty = MTP_peerNotifySettings(
		MTP_flags(0),
		MTPBool(),
		MTPBool(),
		MTPint(),
		MTPstring());
	if (peer->notifyChange(empty)) {
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
		histories().requestDialogEntry(history, [=] {
			insertCheckedServiceNotification(message, media, date);
		});
	} else {
		insertCheckedServiceNotification(message, media, date);
	}
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
				peerToMTP(PeerData::kServiceNotificationsId),
				peerToMTP(PeerData::kServiceNotificationsId),
				MTPMessageFwdHeader(),
				MTPint(), // via_bot_id
				MTPMessageReplyHeader(),
				MTP_int(date),
				MTP_string(sending.text),
				media,
				MTPReplyMarkup(),
				Api::EntitiesToMTP(&session(), sending.entities),
				MTPint(), // views
				MTPint(), // forwards
				MTPMessageReplies(),
				MTPint(), // edit_date
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

void Session::setTopPromoted(
		History *promoted,
		const QString &type,
		const QString &message) {
	const auto changed = (_topPromoted != promoted);
	if (!changed
		&& (!promoted || promoted->topPromotionMessage() == message)) {
		return;
	}
	if (changed) {
		if (_topPromoted) {
			_topPromoted->cacheTopPromotion(false, QString(), QString());
		}
	}
	const auto old = std::exchange(_topPromoted, promoted);
	if (_topPromoted) {
		histories().requestDialogEntry(_topPromoted);
		_topPromoted->cacheTopPromotion(true, type, message);
		_topPromoted->requestChatListMessage();
		session().changes().historyUpdated(
			_topPromoted,
			HistoryUpdate::Flag::TopPromoted);
	}
	if (changed && old) {
		session().changes().historyUpdated(
			old,
			HistoryUpdate::Flag::TopPromoted);
	}
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
		u":/gui/art/bg_initial.jpg"_q));
	for (const auto &paper : data) {
		if (const auto parsed = Data::WallPaper::Create(&session(), paper)) {
			_wallpapers.push_back(*parsed);
		}
	}
	if (ranges::none_of(_wallpapers, Data::IsDefaultWallPaper)) {
		_wallpapers.push_back(Data::DefaultWallPaper());
		_wallpapers.back().setLocalImageAsThumbnail(std::make_shared<Image>(
			u":/gui/arg/bg.jpg"_q));
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
	_cache->close();
	_cache->clear();
	_bigFileCache->close();
	_bigFileCache->clear();
}

} // namespace Data
