/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_session.h"

#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "main/main_app_config.h"
#include "apiwrap.h"
#include "mainwidget.h"
#include "api/api_bot.h"
#include "api/api_text_entities.h"
#include "api/api_user_names.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/mime_type.h" // Core::IsMimeSticker
#include "ui/image/image_location_factory.h" // Images::FromPhotoSize
#include "ui/text/format_values.h" // Ui::FormatPhone
#include "export/export_manager.h"
#include "export/view/export_view_panel_controller.h"
#include "mtproto/mtproto_config.h"
#include "window/notifications_manager.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "history/view/media/history_view_media.h"
#include "history/view/history_view_element.h"
#include "inline_bots/inline_bot_layout_item.h"
#include "storage/storage_account.h"
#include "storage/storage_encrypted_file.h"
#include "media/player/media_player_instance.h" // instance()->play()
#include "media/audio/media_audio.h"
#include "boxes/abstract_box.h"
#include "passport/passport_form_controller.h"
#include "iv/iv_data.h"
#include "lang/lang_keys.h" // tr::lng_deleted(tr::now) in user name
#include "data/business/data_business_chatbots.h"
#include "data/business/data_business_info.h"
#include "data/business/data_shortcut_messages.h"
#include "data/components/scheduled_messages.h"
#include "data/components/sponsored_messages.h"
#include "data/stickers/data_stickers.h"
#include "data/notify/data_notify_settings.h"
#include "data/data_bot_app.h"
#include "data/data_changes.h"
#include "data/data_group_call.h"
#include "data/data_folder.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_file_origin.h"
#include "data/data_download_manager.h"
#include "data/data_web_page.h"
#include "data/data_game.h"
#include "data/data_poll.h"
#include "data/data_replies_list.h"
#include "data/data_chat_filters.h"
#include "data/data_send_action.h"
#include "data/data_message_reactions.h"
#include "data/data_emoji_statuses.h"
#include "data/data_forum_icons.h"
#include "data/data_cloud_themes.h"
#include "data/data_saved_messages.h"
#include "data/data_saved_sublist.h"
#include "data/data_stories.h"
#include "data/data_streaming.h"
#include "data/data_media_rotation.h"
#include "data/data_histories.h"
#include "data/data_peer_values.h"
#include "data/data_premium_limits.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "base/platform/base_platform_info.h"
#include "base/unixtime.h"
#include "base/call_delayed.h"
#include "base/random.h"
#include "spellcheck/spellcheck_highlight_syntax.h"

namespace Data {
namespace {

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
			for (const auto &row : markup->data.rows) {
				for (const auto &button : row) {
					using ButtonType = HistoryMessageMarkupButton::Type;
					if (button.type == ButtonType::SwitchInline) {
						const auto session = &item->history()->session();
						const auto &windows = session->windows();
						if (!windows.empty()) {
							Api::SwitchInlineBotButtonReceived(
								windows.front(),
								button.data);
						}
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
	return ranges::views::all(
		restrictions
	) | ranges::views::filter([](const MTPRestrictionReason &restriction) {
		return restriction.match([&](const MTPDrestrictionReason &data) {
			const auto platform = qs(data.vplatform());
			return false
#ifdef OS_MAC_STORE
				|| (platform == u"ios"_q)
#elif defined OS_WIN_STORE // OS_MAC_STORE
				|| (platform == u"ms"_q)
#endif // OS_MAC_STORE || OS_WIN_STORE
				|| (platform == u"all"_q);
		});
	}) | ranges::views::transform([](const MTPRestrictionReason &restriction) {
		return restriction.match([&](const MTPDrestrictionReason &data) {
			return UnavailableReason{ qs(data.vreason()), qs(data.vtext()) };
		});
	}) | ranges::to_vector;
}

[[nodiscard]] InlineImageLocation FindInlineThumbnail(
		const QVector<MTPPhotoSize> &sizes) {
	const auto i = ranges::find(
		sizes,
		mtpc_photoStrippedSize,
		&MTPPhotoSize::type);
	const auto j = ranges::find(
		sizes,
		mtpc_photoPathSize,
		&MTPPhotoSize::type);
	return (i != sizes.end())
		? InlineImageLocation{ i->c_photoStrippedSize().vbytes().v, false }
		: (j != sizes.end())
		? InlineImageLocation{ j->c_photoPathSize().vbytes().v, true }
		: InlineImageLocation();
}

[[nodiscard]] InlineImageLocation FindDocumentInlineThumbnail(
		const MTPDdocument &data) {
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
		return size.match([](const MTPDvideoSize &data) {
			return (data.vw().v * data.vh().v);
		}, [](const MTPDvideoSizeEmojiMarkup &) {
			return 0;
		}, [](const MTPDvideoSizeStickerMarkup &) {
			return 0;
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
	const auto thumbnail = FindInlineThumbnail(data.vsizes().v);
	return !thumbnail.isPath ? thumbnail.bytes : QByteArray();
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
, _groupFreeTranscribeLevel(session->appConfig().value(
) | rpl::map([limits = Data::LevelLimits(session)] {
	return limits.groupTranscribeLevelMin();
}))
, _chatsList(
	session,
	FilterId(),
	maxPinnedChatsLimitValue(nullptr))
, _contactsList(Dialogs::SortMode::Name)
, _contactsNoChatsList(Dialogs::SortMode::Name)
, _ttlCheckTimer([=] { checkTTLs(); })
, _selfDestructTimer([=] { checkSelfDestructItems(); })
, _pollsClosingTimer([=] { checkPollsClosings(); })
, _watchForOfflineTimer([=] { checkLocalUsersWentOffline(); })
, _groups(this)
, _chatsFilters(std::make_unique<ChatFilters>(this))
, _cloudThemes(std::make_unique<CloudThemes>(session))
, _sendActionManager(std::make_unique<SendActionManager>())
, _streaming(std::make_unique<Streaming>(this))
, _mediaRotation(std::make_unique<MediaRotation>())
, _histories(std::make_unique<Histories>(this))
, _stickers(std::make_unique<Stickers>(this))
, _reactions(std::make_unique<Reactions>(this))
, _emojiStatuses(std::make_unique<EmojiStatuses>(this))
, _forumIcons(std::make_unique<ForumIcons>(this))
, _notifySettings(std::make_unique<NotifySettings>(this))
, _customEmojiManager(std::make_unique<CustomEmojiManager>(this))
, _stories(std::make_unique<Stories>(this))
, _savedMessages(std::make_unique<SavedMessages>(this))
, _chatbots(std::make_unique<Chatbots>(this))
, _businessInfo(std::make_unique<BusinessInfo>(this))
, _shortcutMessages(std::make_unique<ShortcutMessages>(this)) {
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
		const auto enabled = _chatsFilters->has();
		if (enabled != session->settings().dialogsFiltersEnabled()) {
			session->settings().setDialogsFiltersEnabled(enabled);
			session->saveSettingsDelayed();
		}
	}, _lifetime);

	_reactions->myTagRenamed(
	) | rpl::start_with_next([=](const ReactionId &id) {
		const auto i = _viewsByTag.find(id);
		if (i != end(_viewsByTag)) {
			for (const auto &view : i->second) {
				notifyItemDataChange(view->data());
			}
		}
	}, _lifetime);

	Spellchecker::HighlightReady(
	) | rpl::start_with_next([=](uint64 processId) {
		highlightProcessDone(processId);
	}, _lifetime);

	subscribeForTopicRepliesLists();

	crl::on_main(_session, [=] {
		AmPremiumValue(
			_session
		) | rpl::start_with_next([=] {
			for (const auto &[document, items] : _documentItems) {
				if (document->isVoiceMessage()) {
					for (const auto &item : items) {
						requestItemResize(item);
					}
				}
			}
		}, _lifetime);

		_stories->loadMore(Data::StorySourcesList::NotHidden);
	});
}

void Session::subscribeForTopicRepliesLists() {
	repliesReadTillUpdates(
	) | rpl::start_with_next([=](const RepliesReadTillUpdate &update) {
		if (const auto peer = peerLoaded(update.id.peer)) {
			if (const auto topic = peer->forumTopicFor(update.id.msg)) {
				topic->replies()->apply(update);
			}
		}
	}, _lifetime);

	session().changes().messageUpdates(
		MessageUpdate::Flag::NewAdded
		| MessageUpdate::Flag::NewMaybeAdded
		| MessageUpdate::Flag::ReplyToTopAdded
		| MessageUpdate::Flag::Destroyed
	) | rpl::start_with_next([=](const MessageUpdate &update) {
		if (const auto topic = update.item->topic()) {
			topic->replies()->apply(update);
		}
	}, _lifetime);

	session().changes().topicUpdates(
		TopicUpdate::Flag::Creator
	) | rpl::start_with_next([=](const TopicUpdate &update) {
		update.topic->replies()->apply(update);
	}, _lifetime);

	channelDifferenceTooLong(
	) | rpl::start_with_next([=](not_null<ChannelData*> channel) {
		if (const auto forum = channel->forum()) {
			forum->enumerateTopics([](not_null<ForumTopic*> topic) {
				topic->replies()->applyDifferenceTooLong();
			});
		}
	}, _lifetime);
}

void Session::clear() {
	// Optimization: clear notifications before destroying items.
	Core::App().notifications().clearFromSession(_session);

	// We must clear all forums before clearing customEmojiManager.
	// Because in Data::ForumTopic an Ui::Text::CustomEmoji is cached.
	auto forums = base::flat_set<not_null<ChannelData*>>();
	for (const auto &[peerId, peer] : _peers) {
		if (const auto channel = peer->asChannel()) {
			if (channel->isForum()) {
				forums.emplace(channel);
			}
		}
	}
	for (const auto &channel : forums) {
		channel->setFlags(channel->flags() & ~ChannelDataFlag::Forum);
	}

	_sendActionManager->clear();

	_histories->unloadAll();
	_shortcutMessages = nullptr;
	_session->scheduledMessages().clear();
	_session->sponsoredMessages().clear();
	_dependentMessages.clear();
	base::take(_messages);
	base::take(_nonChannelMessages);
	_messageByRandomId.clear();
	_sentMessagesData.clear();
	cSetRecentInlineBots(RecentInlineBots());
	cSetRecentStickers(RecentStickerPack());
	HistoryView::Element::ClearGlobal();
	_contactsNoChatsList.clear();
	_contactsList.clear();
	_chatsList.clear();
	for (const auto &[id, folder] : _folders) {
		folder->clearChatsList();
	}
	_chatsFilters->clear();
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
	} else if (!i->second->isLoaded()) {
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
		result->setFlags(UserDataFlag::Deleted);
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

		using Flag = UserDataFlag;
		const auto flagsMask = Flag::Deleted
			| Flag::Verified
			| Flag::Scam
			| Flag::Fake
			| Flag::BotInlineGeo
			| Flag::Premium
			| Flag::Support
			| Flag::SomeRequirePremiumToWrite
			| Flag::RequirePremiumToWriteKnown
			| (!minimal
				? Flag::Contact
				| Flag::MutualContact
				| Flag::DiscardMinPhoto
				| Flag::StoriesHidden
				: Flag());
		const auto storiesState = minimal
			? std::optional<Data::Stories::PeerSourceState>()
			: data.is_stories_unavailable()
			? Data::Stories::PeerSourceState()
			: !data.vstories_max_id()
			? std::optional<Data::Stories::PeerSourceState>()
			: stories().peerSourceState(result, data.vstories_max_id()->v);
		const auto flagsSet = (data.is_deleted() ? Flag::Deleted : Flag())
			| (data.is_verified() ? Flag::Verified : Flag())
			| (data.is_scam() ? Flag::Scam : Flag())
			| (data.is_fake() ? Flag::Fake : Flag())
			| (data.is_bot_inline_geo() ? Flag::BotInlineGeo : Flag())
			| (data.is_premium() ? Flag::Premium : Flag())
			| (data.is_support() ? Flag::Support : Flag())
			| (data.is_contact_require_premium()
				? (Flag::SomeRequirePremiumToWrite
					| (result->someRequirePremiumToWrite()
						? (result->requirePremiumToWriteKnown()
							? Flag::RequirePremiumToWriteKnown
							: Flag())
						: Flag()))
				: Flag())
			| (!minimal
				? (data.is_contact() ? Flag::Contact : Flag())
				| (data.is_mutual_contact() ? Flag::MutualContact : Flag())
				| (data.is_apply_min_photo()
					? Flag()
					: Flag::DiscardMinPhoto)
				| (data.is_stories_hidden() ? Flag::StoriesHidden : Flag())
				: Flag());
		result->setFlags((result->flags() & ~flagsMask) | flagsSet);
		if (minimal) {
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
			if (storiesState) {
				result->setStoriesState(!storiesState->maxId
					? UserData::StoriesState::None
					: (storiesState->maxId > storiesState->readTill)
					? UserData::StoriesState::HasUnread
					: UserData::StoriesState::HasRead);
			}
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
			const auto noLocalName = result->firstName.isEmpty()
				&& result->lastName.isEmpty();
			const auto fname = (!minimal || noLocalName)
				? TextUtilities::SingleLine(
					qs(data.vfirst_name().value_or_empty()))
				: result->firstName;
			const auto lname = (!minimal || noLocalName)
				? TextUtilities::SingleLine(
					qs(data.vlast_name().value_or_empty()))
				: result->lastName;

			const auto phone = minimal
				? result->phone()
				: qs(data.vphone().value_or_empty());
			const auto uname = minimal
				? result->username()
				: TextUtilities::SingleLine(
					qs(data.vusername().value_or_empty()));

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
					? Ui::FormatPhone(phone)
					: QString())
				: result->nameOrPhone;

			result->setName(fname, lname, pname, uname);
			if (!minimal || result->applyMinPhoto()) {
				if (const auto photo = data.vphoto()) {
					result->setPhoto(*photo);
				} else {
					result->setPhoto(MTP_userProfilePhotoEmpty());
				}
			}
			if (const auto accessHash = data.vaccess_hash()) {
				result->setAccessHash(accessHash->v);
			}
			status = data.vstatus();
			if (!minimal) {
				const auto newUsername = uname;
				const auto newUsernames = data.vusernames()
					? Api::Usernames::FromTL(*data.vusernames())
					: !newUsername.isEmpty()
					? Data::Usernames{{ newUsername, true, true }}
					: Data::Usernames();
				result->setUsernames(newUsernames);
			}
		}
		if (const auto &status = data.vemoji_status()) {
			result->setEmojiStatus(*status);
		} else {
			result->setEmojiStatus(0);
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
				result->botInfo->supportsAttachMenu = data.is_bot_attach_menu();
				result->botInfo->supportsBusiness = data.is_bot_business();
				result->botInfo->canEditInformation = data.is_bot_can_edit();
			} else {
				result->setBotInfoVersion(-1);
			}
			result->setIsContact(data.is_contact()
				|| data.is_mutual_contact());
		}

		if (canShareThisContact != result->canShareThisContactFast()) {
			flags |= UpdateFlag::CanShareContact;
		}

		if (result->changeColor(data.vcolor())) {
			flags |= UpdateFlag::Color;
			if (result->isMinimalLoaded()) {
				_peerDecorationsUpdated.fire_copy(result);
			}
		}
	});

	if (minimal) {
		if (!result->isMinimalLoaded()) {
			result->setLoadedStatus(PeerData::LoadedStatus::Minimal);
		}
	} else if (!result->isLoaded()
		&& (!result->isSelf() || !result->phone().isEmpty())) {
		result->setLoadedStatus(PeerData::LoadedStatus::Normal);
	}

	if (!minimal) {
		const auto lastseen = status
			? LastseenFromMTP(*status, result->lastseen())
			: Data::LastseenStatus::LongAgo(false);
		if (result->updateLastseen(lastseen)) {
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
			chat->setAdminRights(ChatAdminRightsInfo(*rights).flags);
		} else {
			chat->setAdminRights(ChatAdminRights());
		}
		if (const auto rights = data.vdefault_banned_rights()) {
			chat->setDefaultRestrictions(ChatRestrictionsInfo(*rights).flags);
		} else {
			chat->setDefaultRestrictions(ChatRestrictions());
		}

		if (const auto migratedTo = data.vmigrated_to()) {
			migratedTo->match([&](const MTPDinputChannel &input) {
				const auto channel = this->channel(input.vchannel_id().v);
				channel->addFlags(ChannelDataFlag::Megagroup);
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

		using Flag = ChatDataFlag;
		const auto flagsMask = Flag::Left
			| Flag::Creator
			| Flag::Deactivated
			| Flag::Forbidden
			| Flag::CallActive
			| Flag::CallNotEmpty
			| Flag::NoForwards;
		const auto flagsSet = (data.is_left() ? Flag::Left : Flag())
			| (data.is_creator() ? Flag::Creator : Flag())
			| (data.is_deactivated() ? Flag::Deactivated : Flag())
			| (data.is_call_active() ? Flag::CallActive : Flag())
			| ((data.is_call_not_empty()
				|| (chat->groupCall()
					&& chat->groupCall()->fullCount() > 0))
				? Flag::CallNotEmpty
				: Flag())
			| (data.is_noforwards() ? Flag::NoForwards : Flag());
		chat->setFlags((chat->flags() & ~flagsMask) | flagsSet);
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
		chat->setFlags(ChatDataFlag::Forbidden);
		chat->setAdminRights(ChatAdminRights());
		chat->setDefaultRestrictions(ChatRestrictions());

		if (canAddMembers != chat->canAddMembers()) {
			flags |= UpdateFlag::Rights;
		}
	}, [&](const MTPDchannel &data) {
		const auto channel = result->asChannel();

		minimal = data.is_min();
		if (minimal && !result->isLoaded()) {
			LOG(("API Warning: not loaded minimal channel applied."));
		}

		const auto wasInChannel = channel->amIn();
		const auto canViewAdmins = channel->canViewAdmins();
		const auto canViewMembers = channel->canViewMembers();
		const auto canAddMembers = channel->canAddMembers();

		const auto wasCallNotEmpty = Data::ChannelHasActiveCall(channel);

		channel->updateLevelHint(data.vlevel().value_or_empty());
		if (const auto count = data.vparticipants_count()) {
			channel->setMembersCount(count->v);
		}
		if (const auto rights = data.vdefault_banned_rights()) {
			channel->setDefaultRestrictions(ChatRestrictionsInfo(*rights).flags);
		} else {
			channel->setDefaultRestrictions(ChatRestrictions());
		}

		if (const auto &status = data.vemoji_status()) {
			channel->setEmojiStatus(*status);
		} else {
			channel->setEmojiStatus(0);
		}
		if (minimal) {
			if (channel->input.type() == mtpc_inputPeerEmpty
				|| channel->inputChannel.type() == mtpc_inputChannelEmpty) {
				channel->setAccessHash(data.vaccess_hash().value_or_empty());
			}
		} else {
			if (const auto rights = data.vadmin_rights()) {
				channel->setAdminRights(ChatAdminRightsInfo(*rights).flags);
			} else if (channel->hasAdminRights()) {
				channel->setAdminRights(ChatAdminRights());
			}
			if (const auto rights = data.vbanned_rights()) {
				channel->setRestrictions(ChatRestrictionsInfo(*rights));
			} else if (channel->hasRestrictions()) {
				channel->setRestrictions(ChatRestrictionsInfo());
			}
			channel->setAccessHash(
				data.vaccess_hash().value_or(channel->access));
			channel->date = data.vdate().v;
			if (const auto restriction = data.vrestriction_reason()) {
				channel->setUnavailableReasons(
					ExtractUnavailableReasons(restriction->v));
			} else {
				channel->setUnavailableReasons({});
			}
		}

		{
			const auto newUsername = qs(data.vusername().value_or_empty());
			const auto newUsernames = data.vusernames()
				? Api::Usernames::FromTL(*data.vusernames())
				: !newUsername.isEmpty()
				? Data::Usernames{ Data::Username{ newUsername, true, true } }
				: Data::Usernames();
			channel->setName(
				qs(data.vtitle()),
				TextUtilities::SingleLine(newUsername));
			channel->setUsernames(newUsernames);
		}
		const auto hasUsername = !channel->username().isEmpty();

		using Flag = ChannelDataFlag;
		const auto flagsMask = Flag::Broadcast
			| Flag::Verified
			| Flag::Scam
			| Flag::Fake
			| Flag::Megagroup
			| Flag::Gigagroup
			| Flag::Username
			| Flag::Signatures
			| Flag::HasLink
			| Flag::SlowmodeEnabled
			| Flag::CallActive
			| Flag::CallNotEmpty
			| Flag::Forbidden
			| (!minimal ? (Flag::Left | Flag::Creator) : Flag())
			| Flag::NoForwards
			| Flag::JoinToWrite
			| Flag::RequestToJoin
			| Flag::Forum
			| ((!minimal && !data.is_stories_hidden_min())
				? Flag::StoriesHidden
				: Flag());
		const auto storiesState = minimal
			? std::optional<Data::Stories::PeerSourceState>()
			: data.is_stories_unavailable()
			? Data::Stories::PeerSourceState()
			: !data.vstories_max_id()
			? std::optional<Data::Stories::PeerSourceState>()
			: stories().peerSourceState(channel, data.vstories_max_id()->v);
		const auto flagsSet = (data.is_broadcast() ? Flag::Broadcast : Flag())
			| (data.is_verified() ? Flag::Verified : Flag())
			| (data.is_scam() ? Flag::Scam : Flag())
			| (data.is_fake() ? Flag::Fake : Flag())
			| (data.is_megagroup() ? Flag::Megagroup : Flag())
			| (data.is_gigagroup() ? Flag::Gigagroup : Flag())
			| (hasUsername ? Flag::Username : Flag())
			| (data.is_signatures() ? Flag::Signatures : Flag())
			| (data.is_has_link() ? Flag::HasLink : Flag())
			| (data.is_slowmode_enabled() ? Flag::SlowmodeEnabled : Flag())
			| (data.is_call_active() ? Flag::CallActive : Flag())
			| ((data.is_call_not_empty()
				|| (channel->groupCall()
					&& channel->groupCall()->fullCount() > 0))
				? Flag::CallNotEmpty
				: Flag())
			| (!minimal
				? (data.is_left() ? Flag::Left : Flag())
				| (data.is_creator() ? Flag::Creator : Flag())
				: Flag())
			| (data.is_noforwards() ? Flag::NoForwards : Flag())
			| (data.is_join_to_send() ? Flag::JoinToWrite : Flag())
			| (data.is_join_request() ? Flag::RequestToJoin : Flag())
			| ((data.is_forum() && data.is_megagroup())
				? Flag::Forum
				: Flag())
			| ((!minimal
				&& !data.is_stories_hidden_min()
				&& data.is_stories_hidden())
				? Flag::StoriesHidden
				: Flag());
		channel->setFlags((channel->flags() & ~flagsMask) | flagsSet);
		if (!minimal && storiesState) {
			result->setStoriesState(!storiesState->maxId
				? UserData::StoriesState::None
				: (storiesState->maxId > storiesState->readTill)
				? UserData::StoriesState::HasUnread
				: UserData::StoriesState::HasRead);
		}

		channel->setPhoto(data.vphoto());

		if (wasInChannel != channel->amIn()) {
			flags |= UpdateFlag::ChannelAmIn;
		}
		if (canViewAdmins != channel->canViewAdmins()
			|| canViewMembers != channel->canViewMembers()
			|| canAddMembers != channel->canAddMembers()) {
			flags |= UpdateFlag::Rights;
		}
		if (wasCallNotEmpty != Data::ChannelHasActiveCall(channel)) {
			flags |= UpdateFlag::GroupCall;
		}
		if (result->changeColor(data.vcolor())) {
			flags |= UpdateFlag::Color;
			if (result->isMinimalLoaded()) {
				_peerDecorationsUpdated.fire_copy(result);
			}
		}
	}, [&](const MTPDchannelForbidden &data) {
		const auto channel = result->asChannel();

		auto wasInChannel = channel->amIn();
		auto canViewAdmins = channel->canViewAdmins();
		auto canViewMembers = channel->canViewMembers();
		auto canAddMembers = channel->canAddMembers();

		using Flag = ChannelDataFlag;
		const auto flagsMask = Flag::Broadcast
			| Flag::Megagroup
			| Flag::Forbidden;
		const auto flagsSet = (data.is_broadcast() ? Flag::Broadcast : Flag())
			| (data.is_megagroup() ? Flag::Megagroup : Flag())
			| Flag::Forbidden;
		channel->setFlags((channel->flags() & ~flagsMask) | flagsSet);

		if (channel->hasAdminRights()) {
			channel->setAdminRights(ChatAdminRights());
		}
		if (channel->hasRestrictions()) {
			channel->setRestrictions(ChatRestrictionsInfo());
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
	} else if (!result->isLoaded()) {
		result->setLoadedStatus(PeerData::LoadedStatus::Normal);
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

GroupCall *Session::groupCall(CallId callId) const {
	const auto i = _groupCalls.find(callId);
	return (i != end(_groupCalls)) ? i->second.get() : nullptr;
}

void Session::watchForOffline(not_null<UserData*> user, TimeId now) {
	if (!now) {
		now = base::unixtime::now();
	}
	if (!Data::IsUserOnline(user, now)) {
		return;
	}
	const auto lastseen = user->lastseen();
	const auto till = lastseen.onlineTill();
	const auto &[i, ok] = _watchingForOffline.emplace(user, till);
	if (!ok) {
		if (i->second == till) {
			return;
		}
		i->second = till;
	}
	const auto timeout = Data::OnlineChangeTimeout(lastseen, now);
	const auto fires = _watchForOfflineTimer.isActive()
		? _watchForOfflineTimer.remainingTime()
		: -1;
	if (fires >= 0 && fires <= timeout) {
		return;
	}
	_watchForOfflineTimer.callOnce(std::max(timeout, crl::time(1)));
}

void Session::maybeStopWatchForOffline(not_null<UserData*> user) {
	if (Data::IsUserOnline(user)) {
		return;
	} else if (_watchingForOffline.remove(user)
		&& _watchingForOffline.empty()) {
		_watchForOfflineTimer.cancel();
	}
}

void Session::checkLocalUsersWentOffline() {
	_watchForOfflineTimer.cancel();

	auto minimal = 86400 * crl::time(1000);
	const auto now = base::unixtime::now();
	for (auto i = begin(_watchingForOffline)
		; i != end(_watchingForOffline);) {
		const auto user = i->first;
		if (!Data::IsUserOnline(user, now)) {
			i = _watchingForOffline.erase(i);
			session().changes().peerUpdated(
				user,
				PeerUpdate::Flag::OnlineStatus);
		} else {
			const auto timeout = Data::OnlineChangeTimeout(user, now);
			accumulate_min(minimal, timeout);
			++i;
		}
	}
	if (!_watchingForOffline.empty()) {
		_watchForOfflineTimer.callOnce(std::max(minimal, crl::time(1)));
	}
}

auto Session::invitedToCallUsers(CallId callId) const
-> const base::flat_set<not_null<UserData*>> & {
	static const base::flat_set<not_null<UserData*>> kEmpty;
	const auto i = _invitedToCallUsers.find(callId);
	return (i != _invitedToCallUsers.end()) ? i->second : kEmpty;
}

void Session::registerInvitedToCallUser(
		CallId callId,
		not_null<PeerData*> peer,
		not_null<UserData*> user) {
	const auto call = peer->groupCall();
	if (call && call->id() == callId) {
		const auto inCall = ranges::contains(
			call->participants(),
			user,
			&Data::GroupCallParticipant::peer);
		if (inCall) {
			return;
		}
	}
	_invitedToCallUsers[callId].emplace(user);
	_invitesToCalls.fire({ callId, user });
}

void Session::unregisterInvitedToCallUser(
		CallId callId,
		not_null<UserData*> user) {
	const auto i = _invitedToCallUsers.find(callId);
	if (i != _invitedToCallUsers.end()) {
		i->second.remove(user);
		if (i->second.empty()) {
			_invitedToCallUsers.erase(i);
		}
	}
}

UserData *Session::userByPhone(const QString &phone) const {
	const auto pname = phone.trimmed();
	for (const auto &[peerId, peer] : _peers) {
		if (const auto user = peer->asUser()) {
			if (user->phone() == pname) {
				return user;
			}
		}
	}
	return nullptr;
}

PeerData *Session::peerByUsername(const QString &username) const {
	const auto uname = username.trimmed();
	for (const auto &[peerId, peer] : _peers) {
		if (peer->isLoaded()
			&& !peer->username().compare(uname, Qt::CaseInsensitive)) {
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

void Session::enumerateBroadcasts(
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
		history->clearFolder();
		history->clear(peer->isChannel()
			? History::ClearType::Unload
			: History::ClearType::DeleteChat);
	}
	if (const auto channel = peer->asMegagroup()) {
		channel->addFlags(ChannelDataFlag::Left);
		if (const auto from = channel->getMigrateFromChat()) {
			if (const auto migrated = historyLoaded(from)) {
				migrated->updateChatListExistence();
			}
		}
	}
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

void Session::botCommandsChanged(not_null<PeerData*> peer) {
	_botCommandsChanges.fire_copy(peer);
}

rpl::producer<not_null<PeerData*>> Session::botCommandsChanges() const {
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

QString Session::nameSortKey(const QString &name) const {
	return TextUtilities::RemoveAccents(name).toLower();
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
			if (const auto history = historyLoaded(channel->id)) {
				history->removeJoinedMessage();
				history->updateChatListExistence();
				history->updateChatListSortPosition();
				if (!history->inChatList()) {
					history->clearFolder();
				}
			}
		}
	}, _lifetime);
}

void Session::setupPeerNameViewer() {
	session().changes().realtimeNameUpdates(
	) | rpl::start_with_next([=](const NameUpdate &update) {
		const auto peer = update.peer;
		if (const auto history = historyLoaded(peer)) {
			history->refreshChatListNameSortKey();
		}
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
			for (const auto &view : i->second) {
				requestViewResize(view);
			}
		}
		if (!user->isLoaded()) {
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
			_contactsNoChatsList.remove(history);
			_contactsList.remove(history);
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
		for (const auto &item : i->second) {
			notifyItemLayoutChange(item);
		}
	}
}

void Session::requestPhotoViewRepaint(not_null<const PhotoData*> photo) {
	const auto i = _photoItems.find(photo);
	if (i != end(_photoItems)) {
		for (const auto &item : i->second) {
			requestItemRepaint(item);
		}
	}
}

void Session::notifyDocumentLayoutChanged(
		not_null<const DocumentData*> document) {
	const auto i = _documentItems.find(document);
	if (i != end(_documentItems)) {
		for (const auto &item : i->second) {
			notifyItemLayoutChange(item);
		}
	}
	if (const auto items = InlineBots::Layout::documentItems()) {
		if (const auto i = items->find(document); i != items->end()) {
			for (const auto &item : i->second) {
				item->layoutChanged();
			}
		}
	}
}

void Session::requestDocumentViewRepaint(
		not_null<const DocumentData*> document) {
	const auto i = _documentItems.find(document);
	if (i != end(_documentItems)) {
		for (const auto &item : i->second) {
			requestItemRepaint(item);
		}
	}
}

void Session::requestPollViewRepaint(not_null<const PollData*> poll) {
	if (const auto i = _pollViews.find(poll); i != _pollViews.end()) {
		for (const auto &view : i->second) {
			requestViewResize(view);
		}
	}
}

void Session::documentLoadProgress(not_null<DocumentData*> document) {
	requestDocumentViewRepaint(document);
	_documentLoadProgress.fire_copy(document);
}

void Session::documentLoadDone(not_null<DocumentData*> document) {
	notifyDocumentLayoutChanged(document);
	_documentLoadProgress.fire_copy(document);
}

void Session::documentLoadFail(
		not_null<DocumentData*> document,
		bool started) {
	notifyDocumentLayoutChanged(document);
	_documentLoadProgress.fire_copy(document);
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
		auto items = base::flat_set<not_null<HistoryItem*>>();
		items.reserve(i->second.size());
		for (const auto &item : i->second) {
			if (item->isUnreadMention() || item->isIncomingUnreadMedia()) {
				items.emplace(item);
			}
		}
		_session->api().markContentsRead(items);
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

void Session::notifyNewItemAdded(not_null<HistoryItem*> item) {
	_newItemAdded.fire_copy(item);
}

rpl::producer<not_null<HistoryItem*>> Session::newItemAdded() const {
	return _newItemAdded.events();
}

HistoryItem *Session::changeMessageId(PeerId peerId, MsgId wasId, MsgId nowId) {
	const auto list = messagesListForInsert(peerId);
	const auto i = list->find(wasId);
	if (i == list->end()) {
		return nullptr;
	}
	const auto item = i->second;
	list->erase(i);
	const auto &[j, ok] = list->emplace(nowId, item);

	if (!peerIsChannel(peerId)) {
		if (IsServerMsgId(wasId)) {
			const auto k = _nonChannelMessages.find(wasId);
			Assert(k != end(_nonChannelMessages));
			_nonChannelMessages.erase(k);
		}
		if (IsServerMsgId(nowId)) {
			_nonChannelMessages.emplace(nowId, item);
		}
	}

	Ensures(ok);
	return item;
}

bool Session::queryItemVisibility(not_null<HistoryItem*> item) const {
	auto result = false;
	_itemVisibilityQueries.fire({ item, &result });
	return result;
}

bool Session::queryDocumentVisibility(
		not_null<DocumentData*> document) const {
	const auto i = _documentItems.find(document);
	if (i != end(_documentItems)) {
		for (const auto &item : i->second) {
			if (queryItemVisibility(item)) {
				return true;
			}
		}
	}
	return false;
}


[[nodiscard]] auto Session::itemVisibilityQueries() const
-> rpl::producer<Session::ItemVisibilityQuery> {
	return _itemVisibilityQueries.events();
}

void Session::itemVisibilitiesUpdated() {
	// This could be rewritten in a more generic form, like:
	// rpl::producer<> itemVisibilitiesUpdates()
	// if someone else requires those methods, using fast for now.
	Core::App().downloadManager().itemVisibilitiesUpdated(_session);
}

void Session::notifyItemIdChange(IdChange event) {
	const auto item = changeMessageId(
		event.newId.peer,
		event.oldId,
		event.newId.msg);

	_itemIdChanges.fire_copy(event);

	if (item) {
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
}

rpl::producer<Session::IdChange> Session::itemIdChanged() const {
	return _itemIdChanges.events();
}

void Session::requestItemRepaint(not_null<const HistoryItem*> item) {
	_itemRepaintRequest.fire_copy(item);
	auto repaintGroupLeader = false;
	auto repaintView = [&](not_null<const ViewElement*> view) {
		if (view->isHiddenByGroup()) {
			repaintGroupLeader = true;
		} else {
			requestViewRepaint(view);
		}
	};
	enumerateItemViews(item, repaintView);
	if (repaintGroupLeader) {
		if (const auto group = groups().find(item)) {
			const auto leader = group->items.front();
			if (leader != item) {
				enumerateItemViews(leader, repaintView);
			}
		}
	}
	const auto history = item->history();
	if (history->lastItemDialogsView().dependsOn(item)) {
		history->updateChatListEntry();
	}
	if (const auto topic = item->topic()) {
		if (topic->lastItemDialogsView().dependsOn(item)) {
			topic->updateChatListEntry();
		}
	}
	if (const auto sublist = item->savedSublist()) {
		if (sublist->lastItemDialogsView().dependsOn(item)) {
			sublist->updateChatListEntry();
		}
	}
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

void Session::notifyItemDataChange(not_null<HistoryItem*> item) {
	_itemDataChanges.fire_copy(item);
}

rpl::producer<not_null<HistoryItem*>> Session::itemDataChanges() const {
	return _itemDataChanges.events();
}

void Session::requestItemTextRefresh(not_null<HistoryItem*> item) {
	const auto call = [&](not_null<HistoryItem*> item) {
		enumerateItemViews(item, [&](not_null<ViewElement*> view) {
			view->itemTextUpdated();
		});
		requestItemResize(item);
	};
	if (const auto group = groups().find(item)) {
		call(group->items.front());
	} else {
		call(item);
	}
}

void Session::registerHighlightProcess(
		uint64 processId,
		not_null<HistoryItem*> item) {
	Expects(item->inHighlightProcess());

	const auto &[i, ok] = _highlightings.emplace(processId, item);

	Ensures(ok);
}

void Session::highlightProcessDone(uint64 processId) {
	if (const auto done = _highlightings.take(processId)) {
		for (const auto &[id, item] : _highlightings) {
			if (item == *done) {
				return;
			}
		}
		(*done)->highlightProcessDone();
	}
}

void Session::requestUnreadReactionsAnimation(not_null<HistoryItem*> item) {
	enumerateItemViews(item, [&](not_null<ViewElement*> view) {
		view->animateUnreadReactions();
	});
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
	for (const auto &history : base::take(_historiesChanged)) {
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
		for (const auto &view : base::take(_heavyViewParts)) {
			view->unloadHeavyPart();
		}
	} else {
		auto remove = std::vector<not_null<ViewElement*>>();
		for (const auto &view : _heavyViewParts) {
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
	for (const auto &view : _heavyViewParts) {
		if (view->delegate() == delegate
			&& !delegate->elementIntersectsRange(view, from, till)) {
			remove.push_back(view);
		}
	}
	for (const auto view : remove) {
		view->unloadHeavyPart();
	}
}

void Session::registerShownSpoiler(not_null<ViewElement*> view) {
	_shownSpoilers.emplace(view);
}

void Session::hideShownSpoilers() {
	for (const auto &view : base::take(_shownSpoilers)) {
		view->hideSpoilers();
		requestViewRepaint(view);
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
	return ranges::views::all(
		ids
	) | ranges::views::transform([&](const FullMsgId &fullId) {
		return message(fullId);
	}) | ranges::views::filter([](HistoryItem *item) {
		return item != nullptr;
	}) | ranges::views::transform([](HistoryItem *item) {
		return not_null<HistoryItem*>(item);
	}) | ranges::to_vector;
}

MessageIdsList Session::itemsToIds(
		const HistoryItemsList &items) const {
	return ranges::views::all(
		items
	) | ranges::views::transform([](not_null<HistoryItem*> item) {
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
		Dialogs::Key key,
		FilterId filterId,
		bool pinned) {
	Expects(key.entry()->folderKnown());

	const auto list = (filterId
		? chatsFilters().chatsList(filterId)
		: chatsListFor(key.entry()))->pinned();
	list->setPinned(key, pinned);
	notifyPinnedDialogsOrderUpdated();
}

void Session::setPinnedFromEntryList(Dialogs::Key key, bool pinned) {
	Expects(key.entry()->folderKnown());

	const auto list = chatsListFor(key.entry())->pinned();
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

void Session::applyPinnedTopics(
		not_null<Data::Forum*> forum,
		const QVector<MTPint> &list) {
	forum->topicsList()->pinned()->applyList(forum, list);
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
	setPinnedFromEntryList(history, data.is_pinned());

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
	setPinnedFromEntryList(folder, data.is_pinned());
}

bool Session::pinnedCanPin(not_null<Dialogs::Entry*> entry) const {
	if (const auto sublist = entry->asSublist()) {
		const auto saved = &savedMessages();
		return pinnedChatsOrder(saved).size() < pinnedChatsLimit(saved);
	} else if (const auto topic = entry->asTopic()) {
		const auto forum = topic->forum();
		return pinnedChatsOrder(forum).size() < pinnedChatsLimit(forum);
	} else {
		const auto folder = entry->folder();
		return pinnedChatsOrder(folder).size() < pinnedChatsLimit(folder);
	}
}

bool Session::pinnedCanPin(
		FilterId filterId,
		not_null<History*> history) const {
	Expects(filterId != 0);

	const auto &list = chatsFilters().list();
	const auto i = ranges::find(list, filterId, &Data::ChatFilter::id);
	return (i == end(list))
		|| (i->always().contains(history))
		|| (i->always().size() < pinnedChatsLimit(filterId));
}

int Session::pinnedChatsLimit(Data::Folder *folder) const {
	const auto limits = Data::PremiumLimits(_session);
	return folder
		? limits.dialogsFolderPinnedCurrent()
		: limits.dialogsPinnedCurrent();
}

int Session::pinnedChatsLimit(FilterId filterId) const {
	const auto limits = Data::PremiumLimits(_session);
	return limits.dialogFiltersChatsCurrent();
}

int Session::pinnedChatsLimit(not_null<Data::Forum*> forum) const {
	const auto limits = Data::PremiumLimits(_session);
	return limits.topicsPinnedCurrent();
}

int Session::pinnedChatsLimit(not_null<Data::SavedMessages*> saved) const {
	const auto limits = Data::PremiumLimits(_session);
	return limits.savedSublistsPinnedCurrent();
}

rpl::producer<int> Session::maxPinnedChatsLimitValue(
		Data::Folder *folder) const {
	// Premium limit from appconfig.
	// We always use premium limit in the MainList limit producer,
	// because it slices the list to that limit. We don't want to slice
	// premium-ly added chats from the pinned list because of sync issues.
	return _session->appConfig().value(
	) | rpl::map([folder, limits = Data::PremiumLimits(_session)] {
		return folder
			? limits.dialogsFolderPinnedPremium()
			: limits.dialogsPinnedPremium();
	});
}

rpl::producer<int> Session::maxPinnedChatsLimitValue(
		FilterId filterId) const {
	// Premium limit from appconfig.
	// We always use premium limit in the MainList limit producer,
	// because it slices the list to that limit. We don't want to slice
	// premium-ly added chats from the pinned list because of sync issues.
	return _session->appConfig().value(
	) | rpl::map([limits = Data::PremiumLimits(_session)] {
		return limits.dialogFiltersChatsPremium();
	});
}

rpl::producer<int> Session::maxPinnedChatsLimitValue(
		not_null<Data::Forum*> forum) const {
	return _session->appConfig().value(
	) | rpl::map([limits = Data::PremiumLimits(_session)] {
		return limits.topicsPinnedCurrent();
	});
}

rpl::producer<int> Session::maxPinnedChatsLimitValue(
		not_null<SavedMessages*> saved) const {
	// Premium limit from appconfig.
	// We always use premium limit in the MainList limit producer,
	// because it slices the list to that limit. We don't want to slice
	// premium-ly added chats from the pinned list because of sync issues.
	return _session->appConfig().value(
	) | rpl::map([limits = Data::PremiumLimits(_session)] {
		return limits.savedSublistsPinnedPremium();
	});
}

int Session::groupFreeTranscribeLevel() const {
	return _groupFreeTranscribeLevel.current();
}

const std::vector<Dialogs::Key> &Session::pinnedChatsOrder(
		Data::Folder *folder) const {
	return chatsList(folder)->pinned()->order();
}

const std::vector<Dialogs::Key> &Session::pinnedChatsOrder(
		FilterId filterId) const {
	return chatsFilters().chatsList(filterId)->pinned()->order();
}

const std::vector<Dialogs::Key> &Session::pinnedChatsOrder(
		not_null<Data::Forum*> forum) const {
	return forum->topicsList()->pinned()->order();
}

const std::vector<Dialogs::Key> &Session::pinnedChatsOrder(
		not_null<Data::SavedMessages*> saved) const {
	return saved->chatsList()->pinned()->order();
}

void Session::clearPinnedChats(Data::Folder *folder) {
	chatsList(folder)->pinned()->clear();
}

void Session::reorderTwoPinnedChats(
		FilterId filterId,
		Dialogs::Key key1,
		Dialogs::Key key2) {
	Expects(key1.entry()->folderKnown() && key2.entry()->folderKnown());
	Expects(filterId || (key1.entry()->folder() == key2.entry()->folder()));

	const auto topic = key1.topic();
	const auto list = topic
		? topic->forum()->topicsList()
		: filterId
		? chatsFilters().chatsList(filterId)
		: chatsListFor(key1.entry());
	list->pinned()->reorder(key1, key2);
	notifyPinnedDialogsOrderUpdated();
}

bool Session::updateExistingMessage(const MTPDmessage &data) {
	const auto peer = peerFromMTP(data.vpeer_id());
	const auto existing = message(peer, data.vid().v);
	if (!existing) {
		return false;
	}
	existing->applySentMessage(data);
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
		return message(peerFromMTP(data.vpeer_id()), data.vid().v);
	});
	if (!existing) {
		Reactions::CheckUnknownForUnread(this, data);
		return;
	}
	if (existing->isLocalUpdateMedia() && data.type() == mtpc_message) {
		updateExistingMessage(data.c_message());
	}
	data.match([](const MTPDmessageEmpty &) {
	}, [&](const MTPDmessageService &data) {
		existing->applyEdition(data);
	}, [&](const auto &data) {
		existing->applyEdition(HistoryMessageEdition(_session, data));
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
				&& updateExistingMessage(data)) {
				continue;
			}
		}
		const auto id = IdFromMessage(message); // Only 32 bit values here.
		indices.emplace((uint64(uint32(id.bare)) << 32) | uint64(i), i);
	}
	for (const auto &[position, index] : indices) {
		addNewMessage(
			data[index],
			MessageFlags(),
			type);
	}
}

void Session::processMessages(
		const MTPVector<MTPMessage> &data,
		NewMessageType type) {
	processMessages(data.v, type);
}

void Session::processExistingMessages(
		ChannelData *channel,
		const MTPmessages_Messages &data) {
	data.match([&](const MTPDmessages_channelMessages &data) {
		if (channel) {
			channel->ptsReceived(data.vpts().v);
			channel->processTopics(data.vtopics());
		} else {
			LOG(("App Error: received messages.channelMessages!"));
		}
	}, [](const auto &) {});

	data.match([&](const MTPDmessages_messagesNotModified&) {
		LOG(("API Error: received messages.messagesNotModified!"));
	}, [&](const auto &data) {
		processUsers(data.vusers());
		processChats(data.vchats());
		processMessages(data.vmessages(), NewMessageType::Existing);
	});
}

const Session::Messages *Session::messagesList(PeerId peerId) const {
	const auto i = _messages.find(peerId);
	return (i != end(_messages)) ? &i->second : nullptr;
}

auto Session::messagesListForInsert(PeerId peerId)
-> not_null<Messages*> {
	return &_messages[peerId];
}

void Session::registerMessage(not_null<HistoryItem*> item) {
	const auto peerId = item->history()->peer->id;
	const auto list = messagesListForInsert(peerId);
	const auto itemId = item->id;
	const auto i = list->find(itemId);
	if (i != list->end()) {
		LOG(("App Error: Trying to re-registerMessage()."));
		i->second->destroy();
	}
	list->emplace(itemId, item);

	if (!peerIsChannel(peerId) && IsServerMsgId(itemId)) {
		_nonChannelMessages.emplace(itemId, item);
	}
}

void Session::registerMessageTTL(TimeId when, not_null<HistoryItem*> item) {
	Expects(when > 0);

	auto &list = _ttlMessages[when];
	list.emplace(item);

	const auto nearest = _ttlMessages.begin()->first;
	if (nearest < when && _ttlCheckTimer.isActive()) {
		return;
	}
	scheduleNextTTLs();
}

void Session::scheduleNextTTLs() {
	if (_ttlMessages.empty()) {
		return;
	}
	const auto nearest = _ttlMessages.begin()->first;
	const auto now = base::unixtime::now();

	// Set timer not more than for 24 hours.
	const auto maxTimeout = TimeId(86400);
	const auto timeout = std::min(std::max(now, nearest) - now, maxTimeout);
	_ttlCheckTimer.callOnce(timeout * crl::time(1000));
}

void Session::unregisterMessageTTL(
		TimeId when,
		not_null<HistoryItem*> item) {
	Expects(when > 0);

	const auto i = _ttlMessages.find(when);
	if (i == end(_ttlMessages)) {
		return;
	}
	auto &list = i->second;
	list.erase(item);
	if (list.empty()) {
		_ttlMessages.erase(i);
	}
}

void Session::checkTTLs() {
	_ttlCheckTimer.cancel();
	const auto now = base::unixtime::now();
	while (!_ttlMessages.empty() && _ttlMessages.begin()->first <= now) {
		_ttlMessages.begin()->second.front()->destroy();
	}
	scheduleNextTTLs();
}

void Session::processMessagesDeleted(
		PeerId peerId,
		const QVector<MTPint> &data) {
	const auto list = messagesList(peerId);
	const auto affected = historyLoaded(peerId);
	if (!list && !affected) {
		return;
	}

	auto historiesToCheck = base::flat_set<not_null<History*>>();
	for (const auto &messageId : data) {
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
	for (const auto &history : historiesToCheck) {
		history->requestChatListMessage();
	}
}

void Session::processNonChannelMessagesDeleted(const QVector<MTPint> &data) {
	auto historiesToCheck = base::flat_set<not_null<History*>>();
	for (const auto &messageId : data) {
		if (const auto item = nonChannelMessage(messageId.v)) {
			const auto history = item->history();
			item->destroy();
			if (!history->chatListMessageKnown()) {
				historiesToCheck.emplace(history);
			}
		}
	}
	for (const auto &history : historiesToCheck) {
		history->requestChatListMessage();
	}
}

void Session::removeDependencyMessage(not_null<HistoryItem*> item) {
	const auto i = _dependentMessages.find(item);
	if (i != end(_dependentMessages)) {
		const auto items = std::move(i->second);
		_dependentMessages.erase(i);

		for (const auto &dependent : items) {
			dependent->dependencyItemRemoved(item);
		}
	}
	if (item->groupId()) {
		if (const auto group = groups().find(item)) {
			for (const auto &groupedItem : group->items) {
				updateDependentMessages(groupedItem);
			}
		}
	}
}

void Session::unregisterMessage(not_null<HistoryItem*> item) {
	const auto peerId = item->history()->peer->id;
	const auto itemId = item->id;
	_itemRemoved.fire_copy(item);
	session().changes().messageUpdated(
		item,
		Data::MessageUpdate::Flag::Destroyed);
	groups().unregisterMessage(item);
	removeDependencyMessage(item);
	for (auto i = begin(_highlightings); i != end(_highlightings);) {
		if (i->second == item) {
			i = _highlightings.erase(i);
		} else {
			++i;
		}
	}
	messagesListForInsert(peerId)->erase(itemId);

	if (!peerIsChannel(peerId) && IsServerMsgId(itemId)) {
		_nonChannelMessages.erase(itemId);
	}
}

MsgId Session::nextLocalMessageId() {
	Expects(_localMessageIdCounter < EndClientMsgId);

	return _localMessageIdCounter++;
}

void Session::setSuggestToGigagroup(
		not_null<ChannelData*> group,
		bool suggest) {
	if (suggest) {
		_suggestToGigagroup.emplace(group);
	} else {
		_suggestToGigagroup.remove(group);
	}
}

bool Session::suggestToGigagroup(not_null<ChannelData*> group) const {
	return _suggestToGigagroup.contains(group);
}

HistoryItem *Session::message(PeerId peerId, MsgId itemId) const {
	if (!itemId) {
		return nullptr;
	}

	const auto data = messagesList(peerId);
	if (!data) {
		return nullptr;
	}

	const auto i = data->find(itemId);
	return (i != data->end()) ? i->second.get() : nullptr;
}

HistoryItem *Session::message(
		not_null<const PeerData*> peer,
		MsgId itemId) const {
	return message(peer->id, itemId);
}

HistoryItem *Session::message(FullMsgId itemId) const {
	return message(itemId.peer, itemId.msg);
}

HistoryItem *Session::nonChannelMessage(MsgId itemId) const {
	if (!IsServerMsgId(itemId)) {
		return nullptr;
	}
	const auto i = _nonChannelMessages.find(itemId);
	return (i != end(_nonChannelMessages)) ? i->second.get() : nullptr;
}

void Session::updateDependentMessages(not_null<HistoryItem*> item) {
	const auto i = _dependentMessages.find(item);
	if (i != end(_dependentMessages)) {
		for (const auto &dependent : i->second) {
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

HistoryItem *Session::addNewMessage(
		const MTPMessage &data,
		MessageFlags localFlags,
		NewMessageType type) {
	return addNewMessage(IdFromMessage(data), data, localFlags, type);
}

HistoryItem *Session::addNewMessage(
		MsgId id,
		const MTPMessage &data,
		MessageFlags localFlags,
		NewMessageType type) {
	const auto peerId = PeerFromMessage(data);
	if (!peerId) {
		return nullptr;
	}

	const auto result = history(peerId)->addNewMessage(
		id,
		data,
		localFlags,
		type);
	if (type == NewMessageType::Unread) {
		CheckForSwitchInlineButton(result);
	}
	return result;
}

int Session::unreadBadge() const {
	return computeUnreadBadge(_chatsList.unreadState());
}

bool Session::unreadBadgeMuted() const {
	return computeUnreadBadgeMuted(_chatsList.unreadState());
}

int Session::unreadBadgeIgnoreOne(Dialogs::Key key) const {
	const auto remove = (key && key.entry()->inChatList())
		? key.entry()->chatListUnreadState()
		: Dialogs::UnreadState();
	return computeUnreadBadge(_chatsList.unreadState() - remove);
}

bool Session::unreadBadgeMutedIgnoreOne(Dialogs::Key key) const {
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

void Session::updateRepliesReadTill(RepliesReadTillUpdate update) {
	_repliesReadTillUpdates.fire(std::move(update));
}

auto Session::repliesReadTillUpdates() const
-> rpl::producer<RepliesReadTillUpdate> {
	return _repliesReadTillUpdates.events();
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
			if (const auto destructIn = item->getSelfDestructIn(now)) {
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
		const ImageWithLocation &videoSmall,
		const ImageWithLocation &videoLarge,
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
		videoSmall,
		videoLarge,
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
		base::RandomValue<PhotoId>(),
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
	const auto findVideoSize = [&](PhotoSize size)
	-> std::optional<MTPVideoSize> {
		const auto sizes = data.vvideo_sizes();
		if (!sizes) {
			return std::nullopt;
		}
		const auto area = [](const MTPVideoSize &size) {
			return size.match([](const MTPDvideoSize &data) {
				return data.vsize().v ? (data.vw().v * data.vh().v) : 0;
			}, [](const MTPDvideoSizeEmojiMarkup &) {
				return 0;
			}, [](const MTPDvideoSizeStickerMarkup &) {
				return 0;
			});
		};
		const auto type = [](const MTPVideoSize &size) {
			return size.match([](const MTPDvideoSize &data) {
				return data.vtype().v.isEmpty()
					? char(0)
					: data.vtype().v.front();
			}, [](const auto &) {
				return char(0);
			});
		};
		const auto result = (size == PhotoSize::Small)
			? ranges::find(sizes->v, 'p', type)
			: ranges::max_element(sizes->v, std::less<>(), area);
		if (result == sizes->v.end() || area(*result) <= 0) {
			return std::nullopt;
		}
		return std::make_optional(*result);
	};
	const auto useProgressive = (progressive != sizes.end());
	const auto large = useProgressive
		? Images::FromPhotoSize(_session, data, *progressive)
		: image(LargeLevels);
	if (large.location.valid()) {
		const auto videoSmall = findVideoSize(PhotoSize::Small);
		const auto videoLarge = findVideoSize(PhotoSize::Large);
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
			(videoSmall
				? Images::FromVideoSize(_session, data, *videoSmall)
				: ImageWithLocation()),
			(videoLarge
				? Images::FromVideoSize(_session, data, *videoLarge)
				: ImageWithLocation()),
			(videoLarge
				? videoLarge->match([](const MTPDvideoSize &data) {
					return VideoStartTime(data);
				}, [](const MTPDvideoSizeEmojiMarkup &) { return 0;
				}, [](const MTPDvideoSizeStickerMarkup &) { return 0; })
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
		const ImageWithLocation &videoSmall,
		const ImageWithLocation &videoLarge,
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
		videoSmall,
		videoLarge,
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
			InlineImageLocation(),
			thumbnail,
			ImageWithLocation(), // videoThumbnail
			false, // isPremiumSticker
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
		const InlineImageLocation &inlineThumbnail,
		const ImageWithLocation &thumbnail,
		const ImageWithLocation &videoThumbnail,
		bool isPremiumSticker,
		int32 dc,
		int64 size) {
	const auto result = document(id);
	documentApplyFields(
		result,
		access,
		fileReference,
		date,
		attributes,
		mime,
		inlineThumbnail,
		thumbnail,
		videoThumbnail,
		isPremiumSticker,
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
		base::RandomValue<DocumentId>(),
		uint64(0),
		QByteArray(),
		base::unixtime::now(),
		data.vattributes().v,
		data.vmime_type().v,
		InlineImageLocation(),
		ImageWithLocation{ .location = thumbnailLocation },
		ImageWithLocation{ .location = videoThumbnailLocation },
		false, // isPremiumSticker
		session().mainDcId(),
		int64(0)); // data.vsize().v
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
		base::RandomValue<DocumentId>(),
		uint64(0),
		QByteArray(),
		base::unixtime::now(),
		data.vattributes().v,
		data.vmime_type().v,
		InlineImageLocation(),
		ImageWithLocation{ .location = thumbnailLocation },
		ImageWithLocation{ .location = videoThumbnailLocation },
		false, // isPremiumSticker
		session().mainDcId(),
		int64(0)); // data.vsize().v
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
	const auto inlineThumbnail = FindDocumentInlineThumbnail(data);
	const auto thumbnailSize = FindDocumentThumbnail(data);
	const auto videoThumbnailSize = FindDocumentVideoThumbnail(data);
	const auto prepared = Images::FromPhotoSize(
		_session,
		data,
		thumbnailSize);
	const auto videoThumbnail = videoThumbnailSize
		? Images::FromVideoSize(_session, data, *videoThumbnailSize)
		: ImageWithLocation();
	const auto isPremiumSticker = videoThumbnailSize
		&& (videoThumbnailSize->c_videoSize().vtype().v == "f");
	documentApplyFields(
		document,
		data.vaccess_hash().v,
		data.vfile_reference().v,
		data.vdate().v,
		data.vattributes().v,
		qs(data.vmime_type()),
		inlineThumbnail,
		prepared,
		videoThumbnail,
		isPremiumSticker,
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
		const InlineImageLocation &inlineThumbnail,
		const ImageWithLocation &thumbnail,
		const ImageWithLocation &videoThumbnail,
		bool isPremiumSticker,
		int32 dc,
		int64 size) {
	if (!date) {
		return;
	}
	document->date = date;
	document->setMimeString(mime);
	document->updateThumbnails(
		inlineThumbnail,
		thumbnail,
		videoThumbnail,
		isPremiumSticker);
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
		result->type = WebPageType::None;
		if (result->pendingTill > 0) {
			result->pendingTill = 0;
			result->failed = 1;
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

not_null<WebPageData*> Session::processWebpage(
		const MTPDwebPagePending &data) {
	constexpr auto kDefaultPendingTimeout = 60;
	const auto result = webpage(data.vid().v);
	webpageApplyFields(
		result,
		WebPageType::None,
		QString(),
		QString(),
		QString(),
		QString(),
		TextWithEntities(),
		FullStoryId(),
		nullptr,
		nullptr,
		WebPageCollage(),
		nullptr,
		nullptr,
		0,
		QString(),
		false,
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
		nullptr,
		nullptr,
		0,
		QString(),
		false,
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
		std::unique_ptr<Iv::Data> iv,
		std::unique_ptr<WebPageStickerSet> stickerSet,
		int duration,
		const QString &author,
		bool hasLargeMedia,
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
		FullStoryId(),
		photo,
		document,
		std::move(collage),
		std::move(iv),
		std::move(stickerSet),
		duration,
		author,
		hasLargeMedia,
		pendingTill);
	return result;
}

void Session::webpageApplyFields(
		not_null<WebPageData*> page,
		const MTPDwebPage &data) {
	auto description = TextWithEntities{
		qs(data.vdescription().value_or_empty())
	};
	const auto siteName = qs(data.vsite_name().value_or_empty());
	auto parseFlags = TextParseLinks | TextParseMultiline;
	if (siteName == u"Twitter"_q || siteName == u"Instagram"_q) {
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
				}, [](const MTPDwebPageAttributeStory &) {
					return (DocumentData*)nullptr;
				}, [](const MTPDwebPageAttributeStickerSet &) {
					return (DocumentData*)nullptr;
				});
				if (result) {
					return result;
				}
			}
		}
		return nullptr;
	};
	using WebPageStickerSetPtr = std::unique_ptr<WebPageStickerSet>;
	const auto lookupStickerSet = [&]() -> WebPageStickerSetPtr {
		if (const auto attributes = data.vattributes()) {
			for (const auto &attribute : attributes->v) {
				auto result = attribute.match([&](
						const MTPDwebPageAttributeStickerSet &data) {
					auto result = std::make_unique<WebPageStickerSet>();
					result->isEmoji = data.is_emojis();
					result->isTextColor = data.is_text_color();
					for (const auto &tl : data.vstickers().v) {
						result->items.push_back(processDocument(tl));
					}
					return result;
				}, [](const auto &) {
					return WebPageStickerSetPtr(nullptr);
				});
				if (result && !result->items.empty()) {
					return result;
				}
			}
		}
		return nullptr;
	};
	auto story = (Data::Story*)nullptr;
	auto storyId = FullStoryId();
	if (const auto attributes = data.vattributes()) {
		for (const auto &attribute : attributes->v) {
			attribute.match([&](const MTPDwebPageAttributeStory &data) {
				storyId = FullStoryId{
					peerFromMTP(data.vpeer()),
					data.vid().v,
				};
				if (const auto embed = data.vstory()) {
					story = stories().applySingle(
						peerFromMTP(data.vpeer()),
						*embed);
				} else if (const auto maybe = stories().lookup(storyId)) {
					story = *maybe;
				} else if (maybe.error() == Data::NoStory::Unknown) {
					stories().resolve(storyId, [=] {
						if (const auto maybe = stories().lookup(storyId)) {
							const auto story = *maybe;
							page->document = story->document();
							page->photo = story->photo();
							page->description = story->caption();
							page->type = WebPageType::Story;
							notifyWebPageUpdateDelayed(page);
						}
					});
				}
			}, [](const auto &) {});
		}
	}
	if (const auto page = data.vcached_page()) {
		for (const auto &photo : page->data().vphotos().v) {
			processPhoto(photo);
		}
		for (const auto &document : page->data().vdocuments().v) {
			processDocument(document);
		}
		const auto process = [&](
				const MTPPageBlock &block,
				const auto &self) -> void {
			block.match([&](const MTPDpageBlockChannel &data) {
				processChat(data.vchannel());
			}, [&](const MTPDpageBlockCover &data) {
				self(data.vcover(), self);
			}, [&](const MTPDpageBlockEmbedPost &data) {
				for (const auto &block : data.vblocks().v) {
					self(block, self);
				}
			}, [&](const MTPDpageBlockCollage &data) {
				for (const auto &block : data.vitems().v) {
					self(block, self);
				}
			}, [&](const MTPDpageBlockSlideshow &data) {
				for (const auto &block : data.vitems().v) {
					self(block, self);
				}
			}, [&](const MTPDpageBlockDetails &data) {
				for (const auto &block : data.vblocks().v) {
					self(block, self);
				}
			}, [](const auto &) {});
		};
		for (const auto &block : page->data().vblocks().v) {
			process(block, process);
		}
	}
	const auto type = story ? WebPageType::Story : ParseWebPageType(data);
	auto iv = (data.vcached_page() && !IgnoreIv(type))
		? std::make_unique<Iv::Data>(data, *data.vcached_page())
		: nullptr;
	webpageApplyFields(
		page,
		type,
		qs(data.vurl()),
		qs(data.vdisplay_url()),
		siteName,
		qs(data.vtitle().value_or_empty()),
		(story ? story->caption() : description),
		storyId,
		(story
			? story->photo()
			: photo
			? processPhoto(*photo).get()
			: nullptr),
		(story
			? story->document()
			: document
			? processDocument(*document).get()
			: lookupThemeDocument()),
		WebPageCollage(this, data),
		std::move(iv),
		lookupStickerSet(),
		data.vduration().value_or_empty(),
		qs(data.vauthor().value_or_empty()),
		data.is_has_large_media(),
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
		FullStoryId storyId,
		PhotoData *photo,
		DocumentData *document,
		WebPageCollage &&collage,
		std::unique_ptr<Iv::Data> iv,
		std::unique_ptr<WebPageStickerSet> stickerSet,
		int duration,
		const QString &author,
		bool hasLargeMedia,
		TimeId pendingTill) {
	const auto requestPending = (!page->pendingTill && pendingTill > 0);
	const auto changed = page->applyChanges(
		type,
		url,
		displayUrl,
		siteName,
		title,
		description,
		storyId,
		photo,
		document,
		std::move(collage),
		std::move(iv),
		std::move(stickerSet),
		duration,
		author,
		hasLargeMedia,
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
		i = _games.emplace(id, std::make_unique<GameData>(this, id)).first;
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
	game->shortName = shortName;
	game->title = TextUtilities::SingleLine(title);
	game->description = description;
	game->photo = photo;
	game->document = document;
	notifyGameUpdateDelayed(game);
}

not_null<BotAppData*> Session::botApp(BotAppId id) {
	const auto i = _botApps.find(id);
	return (i != end(_botApps))
		? i->second.get()
		: _botApps.emplace(
			id,
			std::make_unique<BotAppData>(this, id)).first->second.get();
}

BotAppData *Session::findBotApp(PeerId botId, const QString &appName) const {
	for (const auto &[id, app] : _botApps) {
		if (app->botId == botId && app->shortName == appName) {
			return app.get();
		}
	}
	return nullptr;
}

BotAppData *Session::processBotApp(
		PeerId botId,
		const MTPBotApp &data) {
	return data.match([&](const MTPDbotApp &data) {
		const auto result = botApp(data.vid().v);
		result->botId = botId;
		result->shortName = qs(data.vshort_name());
		result->title = qs(data.vtitle());
		result->description = qs(data.vdescription());
		result->photo = processPhoto(data.vphoto());
		result->document = data.vdocument()
			? processDocument(*data.vdocument()).get()
			: nullptr;
		result->accessHash = data.vaccess_hash().v;
		result->hash = data.vhash().v;
		return result.get();
	}, [](const MTPDbotAppNotModified &) {
		return (BotAppData*)nullptr;
	});
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
		for (const auto &user : chat->participants) {
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

void Session::registerCallItem(not_null<HistoryItem*> item) {
	_callItems.emplace(item);
}

void Session::unregisterCallItem(not_null<HistoryItem*> item) {
	_callItems.erase(item);
}

void Session::destroyAllCallItems() {
	while (!_callItems.empty()) {
		(*_callItems.begin())->destroy();
	}
}

void Session::registerStoryItem(
		FullStoryId id,
		not_null<HistoryItem*> item) {
	_storyItems[id].emplace(item);
}

void Session::unregisterStoryItem(
		FullStoryId id,
		not_null<HistoryItem*> item) {
	const auto i = _storyItems.find(id);
	if (i != _storyItems.end()) {
		auto &items = i->second;
		if (items.remove(item) && items.empty()) {
			_storyItems.erase(i);
		}
	}
}

void Session::refreshStoryItemViews(FullStoryId id) {
	const auto i = _storyItems.find(id);
	if (i != _storyItems.end()) {
		for (const auto item : i->second) {
			if (const auto media = item->media()) {
				if (media->storyMention()) {
					item->updateStoryMentionText();
				}
			}
			requestItemViewRefresh(item);
		}
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
	for (const auto &view : _heavyViewParts) {
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
	for (const auto &view : check) {
		view->media()->checkAnimation();
	}
}

HistoryItem *Session::findWebPageItem(not_null<WebPageData*> page) const {
	const auto i = _webpageItems.find(page);
	if (i != _webpageItems.end()) {
		for (const auto &item : i->second) {
			if (item->isRegular()) {
				return item;
			}
		}
	}
	return nullptr;
}

QString Session::findContactPhone(not_null<UserData*> contact) const {
	const auto result = contact->phone();
	return result.isEmpty()
		? findContactPhone(peerToUser(contact->id))
		: Ui::FormatPhone(result);
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
	auto resize = std::vector<not_null<ViewElement*>>();
	for (const auto &page : base::take(_webpagesUpdated)) {
		_webpageUpdates.fire_copy(page);
		if (const auto i = _webpageViews.find(page)
			; i != _webpageViews.end()) {
			resize.insert(end(resize), begin(i->second), end(i->second));
		}
	}
	for (const auto &game : base::take(_gamesUpdated)) {
		if (const auto i = _gameViews.find(game); i != _gameViews.end()) {
			resize.insert(end(resize), begin(i->second), end(i->second));
		}
	}
	for (const auto &poll : base::take(_pollsUpdated)) {
		if (const auto i = _pollViews.find(poll); i != _pollViews.end()) {
			resize.insert(end(resize), begin(i->second), end(i->second));
		}
	}
	for (const auto &view : resize) {
		requestViewResize(view);
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

	_shownSpoilers.remove(view);

	const auto i = _views.find(view->data());
	if (i != end(_views)) {
		auto &list = i->second;
		list.erase(ranges::remove(list, view), end(list));
		if (list.empty()) {
			_views.erase(i);
		}
	}

	using namespace HistoryView;
	if (Element::Hovered() == view) {
		Element::Hovered(nullptr);
	}
	if (Element::Pressed() == view) {
		Element::Pressed(nullptr);
	}
	if (Element::HoveredLink() == view) {
		Element::HoveredLink(nullptr);
	}
	if (Element::PressedLink() == view) {
		Element::PressedLink(nullptr);
	}
	if (Element::Moused() == view) {
		Element::Moused(nullptr);
	}
}

not_null<Folder*> Session::folder(FolderId id) {
	if (const auto result = folderLoaded(id)) {
		return result;
	}
	const auto &[it, ok] = _folders.emplace(
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
	return folder(data.vid().v);
}

not_null<Dialogs::MainList*> Session::chatsListFor(
		not_null<Dialogs::Entry*> entry) {
	const auto topic = entry->asTopic();
	return topic
		? topic->forum()->topicsList()
		: entry->asSublist()
		? _savedMessages->chatsList()
		: chatsList(entry->folder());
}

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
	const auto history = entry->asHistory();
	const auto topic = entry->asTopic();
	const auto mainList = chatsListFor(entry);
	auto event = ChatListEntryRefresh{ .key = key };
	const auto creating = event.existenceChanged = !entry->inChatList();
	if (creating && topic && topic->creating()) {
		return;
	} else if (event.existenceChanged) {
		const auto mainRow = entry->addToChatList(0, mainList);
		_contactsNoChatsList.remove(key, mainRow);
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
		if (!id) {
			continue;
		}
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
		if (const auto forum = history->peer->forum()) {
			forum->preloadTopics();
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
		if (id && entry->inChatList(id)) {
			entry->removeFromChatList(id, chatsFilters().chatsList(id));
			_chatListEntryRefreshes.fire(ChatListEntryRefresh{
				.key = key,
				.filterId = id,
				.existenceChanged = true
			});
		}
	}
	const auto mainList = chatsListFor(entry);
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
	if (const auto topic = key.topic()) {
		Core::App().notifications().clearFromTopic(topic);
	} else if (const auto history = key.history()) {
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

void Session::serviceNotification(
		const TextWithEntities &message,
		const MTPMessageMedia &media,
		bool invertMedia) {
	const auto date = base::unixtime::now();
	if (!peerLoaded(PeerData::kServiceNotificationsId)) {
		processUser(MTP_user(
			MTP_flags(
				MTPDuser::Flag::f_first_name
				| MTPDuser::Flag::f_phone
				| MTPDuser::Flag::f_status
				| MTPDuser::Flag::f_verified),
			MTP_long(peerToUser(PeerData::kServiceNotificationsId).bare),
			MTPlong(), // access_hash
			MTP_string("Telegram"),
			MTPstring(), // last_name
			MTPstring(), // username
			MTP_string("42777"),
			MTP_userProfilePhotoEmpty(),
			MTP_userStatusRecently(MTP_flags(0)),
			MTPint(), // bot_info_version
			MTPVector<MTPRestrictionReason>(),
			MTPstring(), // bot_inline_placeholder
			MTPstring(), // lang_code
			MTPEmojiStatus(),
			MTPVector<MTPUsername>(),
			MTPint(), // stories_max_id
			MTPPeerColor(), // color
			MTPPeerColor())); // profile_color
	}
	const auto history = this->history(PeerData::kServiceNotificationsId);
	const auto insert = [=] {
		insertCheckedServiceNotification(message, media, date, invertMedia);
	};
	if (!history->folderKnown()) {
		histories().requestDialogEntry(history, insert);
	} else {
		insert();
	}
}

void Session::insertCheckedServiceNotification(
		const TextWithEntities &message,
		const MTPMessageMedia &media,
		TimeId date,
		bool invertMedia) {
	const auto flags = MTPDmessage::Flag::f_entities
		| MTPDmessage::Flag::f_from_id
		| MTPDmessage::Flag::f_media
		| (invertMedia
			? MTPDmessage::Flag::f_invert_media
			: MTPDmessage::Flag());
	const auto localFlags = MessageFlag::ClientSideUnread
		| MessageFlag::Local;
	auto sending = TextWithEntities(), left = message;
	while (TextUtilities::CutPart(sending, left, MaxMessageSize)) {
		const auto id = nextLocalMessageId();
		addNewMessage(
			id,
			MTP_message(
				MTP_flags(flags),
				MTP_int(0), // Not used (would've been trimmed to 32 bits).
				peerToMTP(PeerData::kServiceNotificationsId),
				MTPint(), // from_boosts_applied
				peerToMTP(PeerData::kServiceNotificationsId),
				MTPPeer(), // saved_peer_id
				MTPMessageFwdHeader(),
				MTPlong(), // via_bot_id
				MTPlong(), // via_business_bot_id
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
				MTPMessageReactions(),
				MTPVector<MTPRestrictionReason>(),
				MTPint(), // ttl_period
				MTPint(), // quick_reply_shortcut_id
				MTPlong(), // effect
				MTPFactCheck()),
			localFlags,
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

void Session::setWallpapers(const QVector<MTPWallPaper> &data, uint64 hash) {
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

	// Put the legacy2 (flowers) wallpaper to the front of the list.
	const auto legacy2 = ranges::find_if(
		_wallpapers,
		Data::IsLegacy2DefaultWallPaper);
	if (legacy2 != end(_wallpapers)) {
		ranges::rotate(begin(_wallpapers), legacy2, legacy2 + 1);
	}

	// Put the legacy3 (static gradient) wallpaper to the front of the list.
	const auto legacy3 = ranges::find_if(
		_wallpapers,
		Data::IsLegacy3DefaultWallPaper);
	if (legacy3 != end(_wallpapers)) {
		ranges::rotate(begin(_wallpapers), legacy3, legacy3 + 1);
	}

	if (ranges::none_of(_wallpapers, Data::IsDefaultWallPaper)) {
		_wallpapers.push_back(Data::DefaultWallPaper());
		_wallpapers.back().setLocalImageAsThumbnail(std::make_shared<Image>(
			u":/gui/art/bg_thumbnail.png"_q));
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

uint64 Session::wallpapersHash() const {
	return _wallpapersHash;
}

MTP::DcId Session::statsDcId(not_null<ChannelData*> channel) {
	const auto it = _channelStatsDcIds.find(channel);
	return (it == end(_channelStatsDcIds)) ? MTP::DcId(0) : it->second;
}

void Session::applyStatsDcId(
		not_null<ChannelData*> channel,
		MTP::DcId dcId) {
	if (dcId != channel->session().mainDcId()) {
		_channelStatsDcIds[channel] = dcId;
	}
}

void Session::saveViewAsMessages(
		not_null<Forum*> forum,
		bool viewAsMessages) {
	const auto channel = forum->channel();
	if (const auto requestId = _viewAsMessagesRequests.take(channel)) {
		_session->api().request(*requestId).cancel();
	}
	_viewAsMessagesRequests[channel] = _session->api().request(
		MTPchannels_ToggleViewForumAsMessages(
			channel->inputChannel,
			MTP_bool(viewAsMessages))
	).done([=] {
		_viewAsMessagesRequests.remove(channel);
	}).fail([=] {
		_viewAsMessagesRequests.remove(channel);
	}).send();
	channel->setViewAsMessagesFlag(viewAsMessages);
}

void Session::webViewResultSent(WebViewResultSent &&sent) {
	return _webViewResultSent.fire(std::move(sent));
}

auto Session::webViewResultSent() const -> rpl::producer<WebViewResultSent> {
	return _webViewResultSent.events();
}

rpl::producer<not_null<PeerData*>> Session::peerDecorationsUpdated() const {
	return _peerDecorationsUpdated.events();
}

void Session::viewTagsChanged(
		not_null<ViewElement*> view,
		std::vector<Data::ReactionId> &&was,
		std::vector<Data::ReactionId> &&now) {
	for (const auto &id : now) {
		const auto i = ranges::remove(was, id);
		if (i != end(was)) {
			was.erase(i, end(was));
		} else {
			_viewsByTag[id].emplace(view);
		}
	}
	for (const auto &id : was) {
		const auto i = _viewsByTag.find(id);
		if (i != end(_viewsByTag)
			&& i->second.remove(view)
			&& i->second.empty()) {
			_viewsByTag.erase(i);
		}
	}
}

void Session::clearLocalStorage() {
	_cache->close();
	_cache->clear();
	_bigFileCache->close();
	_bigFileCache->clear();
}

} // namespace Data
