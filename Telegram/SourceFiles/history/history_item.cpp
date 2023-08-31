/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_item.h"

#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "calls/calls_instance.h" // Core::App().calls().joinGroupCall.
#include "history/view/history_view_element.h"
#include "history/view/history_view_item_preview.h"
#include "history/view/history_view_message.h"
#include "history/view/history_view_service_message.h"
#include "history/view/media/history_view_media_grouped.h"
#include "history/history_item_components.h"
#include "history/history_item_helpers.h"
#include "history/history_unread_things.h"
#include "history/history.h"
#include "mtproto/mtproto_config.h"
#include "media/clip/media_clip_reader.h"
#include "ui/text/format_values.h"
#include "ui/text/text_isolated_emoji.h"
#include "ui/text/text_utilities.h"
#include "storage/file_upload.h"
#include "storage/storage_facade.h"
#include "storage/storage_shared_media.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "menu/menu_ttl_validator.h"
#include "apiwrap.h"
#include "media/audio/media_audio.h"
#include "core/application.h"
#include "mainwindow.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "core/click_handler_types.h"
#include "base/unixtime.h"
#include "base/timer_rpl.h"
#include "api/api_text_entities.h"
#include "api/api_updates.h"
#include "dialogs/ui/dialogs_message_view.h"
#include "data/notify/data_notify_settings.h"
#include "data/data_bot_app.h"
#include "data/data_scheduled_messages.h"
#include "data/data_changes.h"
#include "data/data_session.h"
#include "data/data_message_reactions.h"
#include "data/data_messages.h"
#include "data/data_media_types.h"
#include "data/data_folder.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_game.h"
#include "data/data_user.h"
#include "data/data_group_call.h" // Data::GroupCall::id().
#include "data/data_poll.h" // PollData::publicVotes.
#include "data/data_sponsored_messages.h"
#include "data/data_stories.h"
#include "data/data_wall_paper.h"
#include "data/data_web_page.h"
#include "chat_helpers/stickers_gift_box_pack.h"
#include "payments/payments_checkout_process.h" // CheckoutProcess::Start.
#include "styles/style_dialogs.h"

namespace {

constexpr auto kNotificationTextLimit = 255;
constexpr auto kPinnedMessageTextLimit = 16;

using ItemPreview = HistoryView::ItemPreview;

[[nodiscard]] TextWithEntities SpoilerLoginCode(TextWithEntities text) {
	const auto r = QRegularExpression(u"([\\d\\-]{5,7})"_q);
	const auto m = r.match(text.text);
	if (!m.hasMatch()) {
		return text;
	}
	const auto codeStart = int(m.capturedStart(1));
	const auto codeLength = int(m.capturedLength(1));
	auto i = text.entities.begin();
	const auto e = text.entities.end();
	while (i != e && i->offset() < codeStart) {
		if (i->offset() + i->length() > codeStart) {
			return text; // Entities should not intersect code.
		}
		++i;
	}
	text.entities.insert(i, { EntityType::Spoiler, codeStart, codeLength });
	return text;
}

[[nodiscard]] bool HasNotEmojiAndSpaces(const QString &text) {
	if (text.isEmpty()) {
		return false;
	}
	auto emoji = 0;
	auto start = text.data();
	const auto end = start + text.size();
	while (start < end) {
		if (start->isSpace()) {
			++start;
		} else if (Ui::Emoji::Find(start, end, &emoji)) {
			start += emoji;
		} else {
			return true;
		}
	}
	return false;
}

} // namespace

void HistoryItem::HistoryItem::Destroyer::operator()(HistoryItem *value) {
	if (value) {
		value->destroy();
	}
}

struct HistoryItem::CreateConfig {
	PeerId replyToPeer = 0;
	MsgId replyTo = 0;
	MsgId replyToTop = 0;
	StoryId replyToStory = 0;
	bool replyIsTopicPost = false;
	UserId viaBotId = 0;
	int viewsCount = -1;
	int forwardsCount = -1;
	QString author;
	PeerId senderOriginal = 0;
	QString senderNameOriginal;
	QString forwardPsaType;
	MsgId originalId = 0;
	PeerId savedFromPeer = 0;
	MsgId savedFromMsgId = 0;
	QString authorOriginal;
	TimeId originalDate = 0;
	TimeId editDate = 0;
	HistoryMessageMarkupData markup;
	HistoryMessageRepliesData replies;
	bool imported = false;

	// For messages created from existing messages (forwarded).
	const HistoryMessageReplyMarkup *inlineMarkup = nullptr;
};

void HistoryItem::FillForwardedInfo(
		CreateConfig &config,
		const MTPDmessageFwdHeader &data) {
	if (const auto fromId = data.vfrom_id()) {
		config.senderOriginal = peerFromMTP(*fromId);
	}
	config.originalDate = data.vdate().v;
	config.senderNameOriginal = qs(data.vfrom_name().value_or_empty());
	config.forwardPsaType = qs(data.vpsa_type().value_or_empty());
	config.originalId = data.vchannel_post().value_or_empty();
	config.authorOriginal = qs(data.vpost_author().value_or_empty());
	const auto savedFromPeer = data.vsaved_from_peer();
	const auto savedFromMsgId = data.vsaved_from_msg_id();
	if (savedFromPeer && savedFromMsgId) {
		config.savedFromPeer = peerFromMTP(*savedFromPeer);
		config.savedFromMsgId = savedFromMsgId->v;
	}
	config.imported = data.is_imported();
}

std::unique_ptr<Data::Media> HistoryItem::CreateMedia(
		not_null<HistoryItem*> item,
		const MTPMessageMedia &media) {
	using Result = std::unique_ptr<Data::Media>;
	return media.match([&](const MTPDmessageMediaContact &media) -> Result {
		return std::make_unique<Data::MediaContact>(
			item,
			media.vuser_id().v,
			qs(media.vfirst_name()),
			qs(media.vlast_name()),
			qs(media.vphone_number()));
	}, [&](const MTPDmessageMediaGeo &media) -> Result {
		return media.vgeo().match([&](const MTPDgeoPoint &point) -> Result {
			return std::make_unique<Data::MediaLocation>(
				item,
				Data::LocationPoint(point));
		}, [](const MTPDgeoPointEmpty &) -> Result {
			return nullptr;
		});
	}, [&](const MTPDmessageMediaGeoLive &media) -> Result {
		return media.vgeo().match([&](const MTPDgeoPoint &point) -> Result {
			return std::make_unique<Data::MediaLocation>(
				item,
				Data::LocationPoint(point));
		}, [](const MTPDgeoPointEmpty &) -> Result {
			return nullptr;
		});
	}, [&](const MTPDmessageMediaVenue &media) -> Result {
		return media.vgeo().match([&](const MTPDgeoPoint &point) -> Result {
			return std::make_unique<Data::MediaLocation>(
				item,
				Data::LocationPoint(point),
				qs(media.vtitle()),
				qs(media.vaddress()));
		}, [](const MTPDgeoPointEmpty &data) -> Result {
			return nullptr;
		});
	}, [&](const MTPDmessageMediaPhoto &media) -> Result {
		const auto photo = media.vphoto();
		if (media.vttl_seconds()) {
			LOG(("App Error: "
				"Unexpected MTPMessageMediaPhoto "
				"with ttl_seconds in CreateMedia."));
			return nullptr;
		} else if (!photo) {
			LOG(("API Error: "
				"Got MTPMessageMediaPhoto "
				"without photo and without ttl_seconds."));
			return nullptr;
		}
		return photo->match([&](const MTPDphoto &photo) -> Result {
			return std::make_unique<Data::MediaPhoto>(
				item,
				item->history()->owner().processPhoto(photo),
				media.is_spoiler());
		}, [](const MTPDphotoEmpty &) -> Result {
			return nullptr;
		});
	}, [&](const MTPDmessageMediaDocument &media) -> Result {
		const auto document = media.vdocument();
		if (media.vttl_seconds()) {
			LOG(("App Error: "
				"Unexpected MTPMessageMediaDocument "
				"with ttl_seconds in CreateMedia."));
			return nullptr;
		} else if (!document) {
			LOG(("API Error: "
				"Got MTPMessageMediaDocument "
				"without document and without ttl_seconds."));
			return nullptr;
		}
		return document->match([&](const MTPDdocument &document) -> Result {
			return std::make_unique<Data::MediaFile>(
				item,
				item->history()->owner().processDocument(document),
				media.is_nopremium(),
				media.is_spoiler());
		}, [](const MTPDdocumentEmpty &) -> Result {
			return nullptr;
		});
	}, [&](const MTPDmessageMediaWebPage &media) {
		return media.vwebpage().match([](const MTPDwebPageEmpty &) -> Result {
			return nullptr;
		}, [&](const MTPDwebPagePending &webpage) -> Result {
			return std::make_unique<Data::MediaWebPage>(
				item,
				item->history()->owner().processWebpage(webpage));
		}, [&](const MTPDwebPage &webpage) -> Result {
			return std::make_unique<Data::MediaWebPage>(
				item,
				item->history()->owner().processWebpage(webpage));
		}, [](const MTPDwebPageNotModified &) -> Result {
			LOG(("API Error: "
				"webPageNotModified is unexpected in message media."));
			return nullptr;
		});
	}, [&](const MTPDmessageMediaGame &media) -> Result {
		return media.vgame().match([&](const MTPDgame &game) {
			return std::make_unique<Data::MediaGame>(
				item,
				item->history()->owner().processGame(game));
		});
	}, [&](const MTPDmessageMediaInvoice &media) -> Result {
		return std::make_unique<Data::MediaInvoice>(
			item,
			Data::ComputeInvoiceData(item, media));
	}, [&](const MTPDmessageMediaPoll &media) -> Result {
		return std::make_unique<Data::MediaPoll>(
			item,
			item->history()->owner().processPoll(media));
	}, [&](const MTPDmessageMediaDice &media) -> Result {
		return std::make_unique<Data::MediaDice>(
			item,
			qs(media.vemoticon()),
			media.vvalue().v);
	}, [&](const MTPDmessageMediaStory &media) -> Result {
		return std::make_unique<Data::MediaStory>(item, FullStoryId{
			peerFromMTP(media.vpeer()),
			media.vid().v,
		}, media.is_via_mention());
	}, [](const MTPDmessageMediaEmpty &) -> Result {
		return nullptr;
	}, [](const MTPDmessageMediaUnsupported &) -> Result {
		return nullptr;
	});
}

HistoryItem::HistoryItem(
	not_null<History*> history,
	MsgId id,
	const MTPDmessage &data,
	MessageFlags localFlags)
: HistoryItem(
		history,
		id,
		FlagsFromMTP(id, data.vflags().v, localFlags),
		data.vdate().v,
		data.vfrom_id() ? peerFromMTP(*data.vfrom_id()) : PeerId(0)) {
	const auto media = data.vmedia();
	const auto checked = media
		? CheckMessageMedia(*media)
		: MediaCheckResult::Good;
	if (checked == MediaCheckResult::Unsupported) {
		_flags &= ~MessageFlag::HasPostAuthor;
		_flags |= MessageFlag::Legacy;
		createComponents(data);
		setText(UnsupportedMessageText());
	} else if (checked == MediaCheckResult::Empty) {
		AddComponents(HistoryServiceData::Bit());
		setServiceText({
			tr::lng_message_empty(tr::now, Ui::Text::WithEntities)
		});
	} else if (checked == MediaCheckResult::HasTimeToLive) {
		createServiceFromMtp(data);
		applyTTL(data);
	} else if (checked == MediaCheckResult::HasStoryMention) {
		setMedia(*data.vmedia());
		createServiceFromMtp(data);
		applyTTL(data);
	} else {
		createComponents(data);
		if (const auto media = data.vmedia()) {
			setMedia(*media);
		}
		auto textWithEntities = TextWithEntities{
			qs(data.vmessage()),
			Api::EntitiesFromMTP(
				&history->session(),
				data.ventities().value_or_empty())
		};
		setText(_media ? textWithEntities : EnsureNonEmpty(textWithEntities));
		if (const auto groupedId = data.vgrouped_id()) {
			setGroupId(
				MessageGroupId::FromRaw(
					history->peer->id,
					groupedId->v,
					_flags & MessageFlag::IsOrWasScheduled));
		}
		setReactions(data.vreactions());
		applyTTL(data);
	}
}

HistoryItem::HistoryItem(
	not_null<History*> history,
	MsgId id,
	const MTPDmessageService &data,
	MessageFlags localFlags)
: HistoryItem(
		history,
		id,
		FlagsFromMTP(id, data.vflags().v, localFlags),
		data.vdate().v,
		data.vfrom_id() ? peerFromMTP(*data.vfrom_id()) : PeerId(0)) {
	if (data.vaction().type() != mtpc_messageActionPhoneCall) {
		createServiceFromMtp(data);
	} else {
		createComponents(CreateConfig());
		_media = std::make_unique<Data::MediaCall>(
			this,
			Data::ComputeCallData(data.vaction().c_messageActionPhoneCall()));
		setTextValue({});
	}
	applyTTL(data);
}

HistoryItem::HistoryItem(
	not_null<History*> history,
	MsgId id,
	const MTPDmessageEmpty &data,
	MessageFlags localFlags)
: HistoryItem(
	history,
	id,
	localFlags,
	TimeId(0),
	PreparedServiceText{ tr::lng_message_empty(
		tr::now,
		Ui::Text::WithEntities) }) {
}

HistoryItem::HistoryItem(
	not_null<History*> history,
	MsgId id,
	MessageFlags flags,
	TimeId date,
	PreparedServiceText &&message,
	PeerId from,
	PhotoData *photo)
: HistoryItem(history, id, flags, date, from) {
	setServiceText(std::move(message));
	if (photo) {
		_media = std::make_unique<Data::MediaPhoto>(
			this,
			history->peer,
			photo);
	}
}

HistoryItem::HistoryItem(
	not_null<History*> history,
	MsgId id,
	MessageFlags flags,
	TimeId date,
	PeerId from,
	const QString &postAuthor,
	not_null<HistoryItem*> original,
	MsgId topicRootId)
: HistoryItem(
		history,
		id,
		(NewForwardedFlags(history->peer, from, original) | flags),
		date,
		from) {
	const auto peer = history->peer;

	auto config = CreateConfig();

	const auto originalMedia = original->media();
	const auto dropForwardInfo = original->computeDropForwardedInfo();
	config.replyTo = config.replyToTop = topicRootId;
	config.replyIsTopicPost = (topicRootId != 0);
	if (!dropForwardInfo) {
		config.originalDate = original->dateOriginal();
		if (const auto info = original->hiddenSenderInfo()) {
			config.senderNameOriginal = info->name;
		} else if (const auto senderOriginal = original->senderOriginal()) {
			config.senderOriginal = senderOriginal->id;
			if (senderOriginal->isChannel()) {
				config.originalId = original->idOriginal();
			}
		} else {
			Unexpected("Corrupt forwarded information in message.");
		}
		config.authorOriginal = original->authorOriginal();
	}
	if (peer->isSelf()) {
		//
		// iOS app sends you to the original post if we forward a forward from channel.
		// But server returns not the original post but the forward in saved_from_...
		//
		//if (config.originalId) {
		//	config.savedFromPeer = config.senderOriginal;
		//	config.savedFromMsgId = config.originalId;
		//} else {
			config.savedFromPeer = original->history()->peer->id;
			config.savedFromMsgId = original->id;
		//}
	}
	if (flags & MessageFlag::HasPostAuthor) {
		config.author = postAuthor;
	}
	if (const auto fwdViaBot = original->viaBot()) {
		config.viaBotId = peerToUser(fwdViaBot->id);
	} else if (originalMedia && originalMedia->game()) {
		if (const auto sender = original->senderOriginal()) {
			if (const auto user = sender->asUser()) {
				if (user->isBot()) {
					config.viaBotId = peerToUser(user->id);
				}
			}
		}
	}
	const auto fwdViewsCount = original->viewsCount();
	if (fwdViewsCount > 0) {
		config.viewsCount = fwdViewsCount;
	} else if ((isPost() && !isScheduled())
		|| (original->senderOriginal()
			&& original->senderOriginal()->isChannel())) {
		config.viewsCount = 1;
	}

	const auto mediaOriginal = original->media();
	if (CopyMarkupToForward(original)) {
		config.inlineMarkup = original->inlineReplyMarkup();
	}
	createComponents(std::move(config));

	const auto ignoreMedia = [&] {
		if (mediaOriginal && mediaOriginal->webpage()) {
			if (peer->amRestricted(ChatRestriction::EmbedLinks)) {
				return true;
			}
		}
		return false;
	};
	if (mediaOriginal && !ignoreMedia()) {
		_media = mediaOriginal->clone(this);
	}

	const auto dropCustomEmoji = dropForwardInfo
		&& !history->session().premium()
		&& !history->peer->isSelf();
	setText(dropCustomEmoji
		? DropCustomEmoji(original->originalText())
		: original->originalText());
}

HistoryItem::HistoryItem(
	not_null<History*> history,
	MsgId id,
	MessageFlags flags,
	FullReplyTo replyTo,
	UserId viaBotId,
	TimeId date,
	PeerId from,
	const QString &postAuthor,
	const TextWithEntities &textWithEntities,
	const MTPMessageMedia &media,
	HistoryMessageMarkupData &&markup,
	uint64 groupedId)
: HistoryItem(
		history,
		id,
		flags,
		date,
		(flags & MessageFlag::HasFromId) ? from : 0) {
	createComponentsHelper(
		flags,
		replyTo,
		viaBotId,
		postAuthor,
		std::move(markup));
	setMedia(media);
	setText(textWithEntities);
	if (groupedId) {
		setGroupId(MessageGroupId::FromRaw(
			history->peer->id,
			groupedId,
			flags & MessageFlag::IsOrWasScheduled));
	}
}

HistoryItem::HistoryItem(
	not_null<History*> history,
	MsgId id,
	MessageFlags flags,
	FullReplyTo replyTo,
	UserId viaBotId,
	TimeId date,
	PeerId from,
	const QString &postAuthor,
	not_null<DocumentData*> document,
	const TextWithEntities &caption,
	HistoryMessageMarkupData &&markup)
: HistoryItem(
		history,
		id,
		flags,
		date,
		(flags & MessageFlag::HasFromId) ? from : 0) {
	createComponentsHelper(
		flags,
		replyTo,
		viaBotId,
		postAuthor,
		std::move(markup));

	const auto skipPremiumEffect = !history->session().premium();
	const auto spoiler = false;
	_media = std::make_unique<Data::MediaFile>(
		this,
		document,
		skipPremiumEffect,
		spoiler);
	setText(caption);
}

HistoryItem::HistoryItem(
	not_null<History*> history,
	MsgId id,
	MessageFlags flags,
	FullReplyTo replyTo,
	UserId viaBotId,
	TimeId date,
	PeerId from,
	const QString &postAuthor,
	not_null<PhotoData*> photo,
	const TextWithEntities &caption,
	HistoryMessageMarkupData &&markup)
: HistoryItem(
		history,
		id,
		flags,
		date,
		(flags & MessageFlag::HasFromId) ? from : 0) {
	createComponentsHelper(
		flags,
		replyTo,
		viaBotId,
		postAuthor,
		std::move(markup));

	const auto spoiler = false;
	_media = std::make_unique<Data::MediaPhoto>(this, photo, spoiler);
	setText(caption);
}

HistoryItem::HistoryItem(
	not_null<History*> history,
	MsgId id,
	MessageFlags flags,
	FullReplyTo replyTo,
	UserId viaBotId,
	TimeId date,
	PeerId from,
	const QString &postAuthor,
	not_null<GameData*> game,
	HistoryMessageMarkupData &&markup)
: HistoryItem(
		history,
		id,
		flags,
		date,
		(flags & MessageFlag::HasFromId) ? from : 0) {
	createComponentsHelper(
		flags,
		replyTo,
		viaBotId,
		postAuthor,
		std::move(markup));

	_media = std::make_unique<Data::MediaGame>(this, game);
	setTextValue({});
}

HistoryItem::HistoryItem(
	not_null<History*> history,
	MsgId id,
	Data::SponsoredFrom from,
	const TextWithEntities &textWithEntities,
	HistoryItem *injectedAfter)
: HistoryItem(
		history,
		id,
		((history->peer->isChannel() ? MessageFlag::Post : MessageFlag(0))
			//| (from.peer ? MessageFlag::HasFromId : MessageFlag(0))
			| MessageFlag::Local),
		HistoryItem::NewMessageDate(injectedAfter
			? injectedAfter->date()
			: 0),
		/*from.peer ? from.peer->id : */PeerId(0)) {
	createComponentsHelper(
		_flags,
		FullReplyTo(),
		UserId(0), // viaBotId
		QString(), // postAuthor
		HistoryMessageMarkupData());
	setText(textWithEntities);
	setSponsoredFrom(from);
}

HistoryItem::HistoryItem(
	not_null<History*> history,
	MsgId id,
	MessageFlags flags,
	TimeId date,
	PeerId from)
: id(id)
, _history(history)
, _from(from ? history->owner().peer(from) : history->peer)
, _flags(FinalizeMessageFlags(flags))
, _date(date) {
	if (isHistoryEntry() && IsClientMsgId(id)) {
		_history->registerClientSideMessage(this);
	}
}

HistoryItem::HistoryItem(
	not_null<History*> history,
	not_null<Data::Story*> story)
: id(StoryIdToMsgId(story->id()))
, _history(history)
, _from(history->peer)
, _flags(MessageFlag::Local
	| MessageFlag::Outgoing
	| MessageFlag::FakeHistoryItem
	| MessageFlag::StoryItem)
, _date(story->date()) {
	setStoryFields(story);
}

HistoryItem::~HistoryItem() {
	_media = nullptr;
	clearSavedMedia();
	if (auto reply = Get<HistoryMessageReply>()) {
		reply->clearData(this);
	}
	clearDependencyMessage();
	applyTTL(0);
}

TimeId HistoryItem::date() const {
	return _date;
}

TimeId HistoryItem::NewMessageDate(TimeId scheduled) {
	return scheduled ? scheduled : base::unixtime::now();
}

HistoryServiceDependentData *HistoryItem::GetServiceDependentData() {
	if (const auto pinned = Get<HistoryServicePinned>()) {
		return pinned;
	} else if (const auto gamescore = Get<HistoryServiceGameScore>()) {
		return gamescore;
	} else if (const auto payment = Get<HistoryServicePayment>()) {
		return payment;
	} else if (const auto info = Get<HistoryServiceTopicInfo>()) {
		return info;
	} else if (const auto same = Get<HistoryServiceSameBackground>()) {
		return same;
	}
	return nullptr;
}

auto HistoryItem::GetServiceDependentData() const
-> const HistoryServiceDependentData * {
	return const_cast<HistoryItem*>(this)->GetServiceDependentData();
}

void HistoryItem::dependencyItemRemoved(not_null<HistoryItem*> dependency) {
	if (const auto reply = Get<HistoryMessageReply>()) {
		const auto documentId = reply->replyToDocumentId;
		reply->itemRemoved(this, dependency);
		if (documentId != reply->replyToDocumentId
			&& generateLocalEntitiesByReply()) {
			_history->owner().requestItemTextRefresh(this);
		}
	} else {
		clearDependencyMessage();
		updateDependentServiceText();
	}
}

void HistoryItem::dependencyStoryRemoved(
		not_null<Data::Story*> dependency) {
	if (const auto reply = Get<HistoryMessageReply>()) {
		const auto documentId = reply->replyToDocumentId;
		reply->storyRemoved(this, dependency);
		if (documentId != reply->replyToDocumentId
			&& generateLocalEntitiesByReply()) {
			_history->owner().requestItemTextRefresh(this);
		}
	}
}

void HistoryItem::updateDependencyItem() {
	if (const auto reply = Get<HistoryMessageReply>()) {
		const auto documentId = reply->replyToDocumentId;
		const auto webpageId = reply->replyToWebPageId;
		reply->updateData(this, true);
		const auto mediaIdChanged = (documentId != reply->replyToDocumentId)
			|| (webpageId != reply->replyToWebPageId);
		if (mediaIdChanged && generateLocalEntitiesByReply()) {
			_history->owner().requestItemTextRefresh(this);
		}
	} else if (GetServiceDependentData()) {
		updateServiceDependent(true);
	}
}

void HistoryItem::updateDependentServiceText() {
	if (Has<HistoryServicePinned>()) {
		updateServiceText(preparePinnedText());
	} else if (Has<HistoryServiceGameScore>()) {
		updateServiceText(prepareGameScoreText());
	} else if (Has<HistoryServicePayment>()) {
		updateServiceText(preparePaymentSentText());
	}
}

bool HistoryItem::updateServiceDependent(bool force) {
	auto dependent = GetServiceDependentData();
	Assert(dependent != nullptr);

	if (!force) {
		if (!dependent->msgId || dependent->msg) {
			return true;
		}
	}

	if (!dependent->lnk) {
		dependent->lnk = JumpToMessageClickHandler(
			(dependent->peerId
				? _history->owner().peer(dependent->peerId)
				: _history->peer),
			dependent->msgId,
			fullId());
	}
	auto gotDependencyItem = false;
	if (!dependent->msg) {
		dependent->msg = _history->owner().message(
			(dependent->peerId
				? dependent->peerId
				: _history->peer->id),
			dependent->msgId);
		if (dependent->msg) {
			if (dependent->msg->isEmpty()) {
				// Really it is deleted.
				dependent->msg = nullptr;
				force = true;
			} else {
				_history->owner().registerDependentMessage(
					this,
					dependent->msg);
				gotDependencyItem = true;
			}
		}
	}
	if (dependent->msg) {
		updateDependentServiceText();
	} else if (force) {
		if (dependent->msgId > 0) {
			dependent->msgId = 0;
			gotDependencyItem = true;
		}
		updateDependentServiceText();
	}
	if (force && gotDependencyItem) {
		Core::App().notifications().checkDelayed();
	}
	return (dependent->msg || !dependent->msgId);
}

MsgId HistoryItem::dependencyMsgId() const {
	if (auto dependent = GetServiceDependentData()) {
		return dependent->msgId;
	}
	return replyToId();
}

void HistoryItem::checkBuyButton() {
	if (const auto invoice = _media ? _media->invoice() : nullptr) {
		if (invoice->receiptMsgId) {
			replaceBuyWithReceiptInMarkup();
		}
	}
}

bool HistoryItem::notificationReady() const {
	if (const auto dependent = GetServiceDependentData()) {
		return (dependent->msg || !dependent->msgId);
	}
	return true;
}

void HistoryItem::finishEdition(int oldKeyboardTop) {
	if (const auto group = _history->owner().groups().find(this)) {
		for (const auto &item : group->items) {
			_history->owner().requestItemViewRefresh(item);
			item->invalidateChatListEntry();
		}
	} else {
		_history->owner().requestItemViewRefresh(this);
		invalidateChatListEntry();
	}

	// Should be completely redesigned as the oldTop no longer exists.
	//if (oldKeyboardTop >= 0) { // #TODO edit bot message
	//	if (auto keyboard = Get<HistoryMessageReplyMarkup>()) {
	//		keyboard->oldTop = oldKeyboardTop;
	//	}
	//}

	_history->owner().updateDependentMessages(this);
}

void HistoryItem::setGroupId(MessageGroupId groupId) {
	Expects(!_groupId);

	_groupId = groupId;
	_history->owner().groups().registerMessage(this);
}

bool HistoryItem::checkCommentsLinkedChat(ChannelId id) const {
	if (!id) {
		return true;
	} else if (const auto channel = _history->peer->asChannel()) {
		if (channel->linkedChatKnown()
			|| !(channel->flags() & ChannelDataFlag::HasLink)) {
			const auto linked = channel->linkedChat();
			if (!linked || peerToChannel(linked->id) != id) {
				return false;
			}
		}
		return true;
	}
	return false;
}

void HistoryItem::setReplyMarkup(HistoryMessageMarkupData &&markup) {
	const auto requestUpdate = [&] {
		history()->owner().requestItemResize(this);
		history()->session().changes().messageUpdated(
			this,
			Data::MessageUpdate::Flag::ReplyMarkup);
	};
	if (markup.isNull()) {
		if (_flags & MessageFlag::HasReplyMarkup) {
			_flags &= ~MessageFlag::HasReplyMarkup;
			if (Has<HistoryMessageReplyMarkup>()) {
				RemoveComponents(HistoryMessageReplyMarkup::Bit());
			}
			requestUpdate();
		}
		return;
	}

	// optimization: don't create markup component for the case
	// MTPDreplyKeyboardHide with flags = 0, assume it has f_zero flag
	if (markup.isTrivial()) {
		bool changed = false;
		if (Has<HistoryMessageReplyMarkup>()) {
			RemoveComponents(HistoryMessageReplyMarkup::Bit());
			changed = true;
		}
		if (!(_flags & MessageFlag::HasReplyMarkup)) {
			_flags |= MessageFlag::HasReplyMarkup;
			changed = true;
		}
		if (changed) {
			requestUpdate();
		}
	} else {
		if (!(_flags & MessageFlag::HasReplyMarkup)) {
			_flags |= MessageFlag::HasReplyMarkup;
		}
		if (!Has<HistoryMessageReplyMarkup>()) {
			AddComponents(HistoryMessageReplyMarkup::Bit());
		}
		Get<HistoryMessageReplyMarkup>()->updateData(std::move(markup));
		requestUpdate();
	}
}

void HistoryItem::setCommentsInboxReadTill(MsgId readTillId) {
	const auto views = Get<HistoryMessageViews>();
	if (!views) {
		return;
	}
	const auto newReadTillId = std::max(readTillId.bare, int64(1));
	const auto ignore = (newReadTillId < views->commentsInboxReadTillId);
	if (ignore) {
		return;
	}
	const auto changed = (newReadTillId > views->commentsInboxReadTillId);
	if (!changed) {
		return;
	}
	const auto wasUnread = areCommentsUnread();
	views->commentsInboxReadTillId = newReadTillId;
	if (wasUnread && !areCommentsUnread()) {
		_history->owner().requestItemRepaint(this);
	}
}

void HistoryItem::setCommentsMaxId(MsgId maxId) {
	if (const auto views = Get<HistoryMessageViews>()) {
		if (views->commentsMaxId != maxId) {
			const auto wasUnread = areCommentsUnread();
			views->commentsMaxId = maxId;
			if (wasUnread != areCommentsUnread()) {
				_history->owner().requestItemRepaint(this);
			}
		}
	}
}

void HistoryItem::setCommentsPossibleMaxId(MsgId possibleMaxId) {
	if (const auto views = Get<HistoryMessageViews>()) {
		if (views->commentsMaxId < possibleMaxId) {
			const auto wasUnread = areCommentsUnread();
			views->commentsMaxId = possibleMaxId;
			if (!wasUnread && areCommentsUnread()) {
				_history->owner().requestItemRepaint(this);
			}
		}
	}
}

bool HistoryItem::areCommentsUnread() const {
	const auto views = Get<HistoryMessageViews>();
	if (!views
		|| !views->commentsMegagroupId
		|| !checkCommentsLinkedChat(views->commentsMegagroupId)) {
		return false;
	}
	const auto till = views->commentsInboxReadTillId;
	if (views->commentsInboxReadTillId < 2 || views->commentsMaxId <= till) {
		return false;
	}
	const auto group = views->commentsMegagroupId
		? _history->owner().historyLoaded(
			peerFromChannel(views->commentsMegagroupId))
		: _history.get();
	return !group || (views->commentsMaxId > group->inboxReadTillId());
}

FullMsgId HistoryItem::commentsItemId() const {
	if (const auto views = Get<HistoryMessageViews>()) {
		return FullMsgId(
			PeerId(views->commentsMegagroupId),
			views->commentsRootId);
	}
	return FullMsgId();
}

void HistoryItem::setCommentsItemId(FullMsgId id) {
	if (id.peer == _history->peer->id) {
		if (id.msg != this->id) {
			if (const auto reply = Get<HistoryMessageReply>()) {
				reply->replyToMsgTop = id.msg;
			}
		}
	} else if (const auto views = Get<HistoryMessageViews>()) {
		if (const auto channelId = peerToChannel(id.peer)) {
			if (views->commentsMegagroupId != channelId) {
				views->commentsMegagroupId = channelId;
				_history->owner().requestItemResize(this);
			}
			views->commentsRootId = id.msg;
		}
	}
}

void HistoryItem::setServiceText(PreparedServiceText &&prepared) {
	AddComponents(HistoryServiceData::Bit());
	_flags &= ~MessageFlag::HasTextLinks;
	const auto data = Get<HistoryServiceData>();
	const auto had = !_text.empty();
	_text = std::move(prepared.text);
	data->textLinks = std::move(prepared.links);
	if (had) {
		_history->owner().requestItemTextRefresh(this);
	}
}

void HistoryItem::updateServiceText(PreparedServiceText &&text) {
	setServiceText(std::move(text));
	_history->owner().requestItemResize(this);
	invalidateChatListEntry();
	_history->owner().updateDependentMessages(this);
}

void HistoryItem::updateStoryMentionText() {
	setServiceText(prepareStoryMentionText());
}

HistoryMessageReplyMarkup *HistoryItem::inlineReplyMarkup() {
	if (const auto markup = Get<HistoryMessageReplyMarkup>()) {
		if (markup->data.flags & ReplyMarkupFlag::Inline) {
			return markup;
		}
	}
	return nullptr;
}

ReplyKeyboard *HistoryItem::inlineReplyKeyboard() {
	if (const auto markup = inlineReplyMarkup()) {
		return markup->inlineKeyboard.get();
	}
	return nullptr;
}

ChannelData *HistoryItem::discussionPostOriginalSender() const {
	if (!_history->peer->isMegagroup()) {
		return nullptr;
	}
	if (const auto forwarded = Get<HistoryMessageForwarded>()) {
		const auto from = forwarded->savedFromPeer;
		if (const auto result = from ? from->asChannel() : nullptr) {
			return result;
		}
	}
	return nullptr;
}

bool HistoryItem::isDiscussionPost() const {
	return (discussionPostOriginalSender() != nullptr);
}

HistoryItem *HistoryItem::lookupDiscussionPostOriginal() const {
	if (!_history->peer->isMegagroup()) {
		return nullptr;
	}
	const auto forwarded = Get<HistoryMessageForwarded>();
	if (!forwarded
		|| !forwarded->savedFromPeer
		|| !forwarded->savedFromMsgId) {
		return nullptr;
	}
	return _history->owner().message(
		forwarded->savedFromPeer->id,
		forwarded->savedFromMsgId);
}

PeerData *HistoryItem::displayFrom() const {
	if (const auto sender = discussionPostOriginalSender()) {
		return sender;
	} else if (const auto sponsored = Get<HistoryMessageSponsored>()) {
		if (sponsored->sender) {
			return nullptr;
		}
	} else if (const auto forwarded = Get<HistoryMessageForwarded>()) {
		if (_history->peer->isSelf() || _history->peer->isRepliesChat() || forwarded->imported) {
			return forwarded->originalSender;
		}
	}
	return author().get();
}

std::unique_ptr<HistoryView::Element> HistoryItem::createView(
		not_null<HistoryView::ElementDelegate*> delegate,
		HistoryView::Element *replacing) {
	if (isService()) {
		return std::make_unique<HistoryView::Service>(
			delegate,
			this,
			replacing);
	}
	return std::make_unique<HistoryView::Message>(delegate, this, replacing);
}

void HistoryItem::invalidateChatListEntry() {
	_history->session().changes().messageUpdated(
		this,
		Data::MessageUpdate::Flag::DialogRowRefresh);
	_history->lastItemDialogsView().itemInvalidated(this);
	if (const auto topic = this->topic()) {
		topic->lastItemDialogsView().itemInvalidated(this);
	}
}

void HistoryItem::customEmojiRepaint() {
	if (!(_flags & MessageFlag::CustomEmojiRepainting)) {
		_flags |= MessageFlag::CustomEmojiRepainting;
		_history->owner().requestItemRepaint(this);
	}
}

void HistoryItem::finishEditionToEmpty() {
	finishEdition(-1);
	_history->itemVanished(this);
}

bool HistoryItem::hasUnreadMediaFlag() const {
	if (_history->peer->isChannel()) {
		const auto passed = base::unixtime::now() - date();
		const auto &config = _history->session().serverConfig();
		if (passed >= config.channelsReadMediaPeriod) {
			return false;
		}
	}
	return _flags & MessageFlag::MediaIsUnread;
}

bool HistoryItem::isUnreadMention() const {
	return !out() && mentionsMe() && (_flags & MessageFlag::MediaIsUnread);
}

bool HistoryItem::hasUnreadReaction() const {
	return (_flags & MessageFlag::HasUnreadReaction);
}

bool HistoryItem::mentionsMe() const {
	if (Has<HistoryServicePinned>()
		&& !Core::App().settings().notifyAboutPinned()) {
		return false;
	}
	return _flags & MessageFlag::MentionsMe;
}

bool HistoryItem::isUnreadMedia() const {
	if (!hasUnreadMediaFlag()) {
		return false;
	} else if (const auto media = this->media()) {
		if (const auto document = media->document()) {
			if (document->isVoiceMessage() || document->isVideoMessage()) {
				return (media->webpage() == nullptr);
			}
		}
	}
	return false;
}

bool HistoryItem::isIncomingUnreadMedia() const {
	return !out() && isUnreadMedia();
}

void HistoryItem::markMediaAndMentionRead() {
	_flags &= ~MessageFlag::MediaIsUnread;

	if (mentionsMe()) {
		_history->updateChatListEntry();
		_history->unreadMentions().erase(id);
		if (const auto topic = this->topic()) {
			topic->updateChatListEntry();
			topic->unreadMentions().erase(id);
		}
	}

	if (const auto selfdestruct = Get<HistoryServiceSelfDestruct>()) {
		if (selfdestruct->destructAt == crl::time()) {
			const auto ttl = selfdestruct->timeToLive;
			if (const auto maybeTime = std::get_if<crl::time>(&ttl)) {
				const auto time = *maybeTime;
				selfdestruct->destructAt = crl::now() + time;
				_history->owner().selfDestructIn(this, time);
			} else {
				selfdestruct->destructAt = TimeToLiveSingleView();
			}
		}
	}
}

void HistoryItem::markReactionsRead() {
	if (_reactions) {
		_reactions->markRead();
	}
	_flags &= ~MessageFlag::HasUnreadReaction;
	_history->updateChatListEntry();
	_history->unreadReactions().erase(id);
	if (const auto topic = this->topic()) {
		topic->updateChatListEntry();
		topic->unreadReactions().erase(id);
	}
}

bool HistoryItem::markContentsRead(bool fromThisClient) {
	if (hasUnreadReaction()) {
		if (fromThisClient) {
			_history->owner().requestUnreadReactionsAnimation(this);
		}
		markReactionsRead();
		return true;
	} else if (isUnreadMention() || isIncomingUnreadMedia()) {
		markMediaAndMentionRead();
		return true;
	}
	return false;
}

void HistoryItem::setIsPinned(bool pinned) {
	const auto changed = (isPinned() != pinned);
	if (pinned) {
		_flags |= MessageFlag::Pinned;
		auto &storage = _history->session().storage();
		storage.add(Storage::SharedMediaAddExisting(
			_history->peer->id,
			MsgId(0), // topicRootId
			Storage::SharedMediaType::Pinned,
			id,
			{ id, id }));
		_history->setHasPinnedMessages(true);
		if (const auto topic = this->topic()) {
			storage.add(Storage::SharedMediaAddExisting(
				_history->peer->id,
				topic->rootId(),
				Storage::SharedMediaType::Pinned,
				id,
				{ id, id }));
			topic->setHasPinnedMessages(true);
		}
	} else {
		_flags &= ~MessageFlag::Pinned;
		_history->session().storage().remove(Storage::SharedMediaRemoveOne(
			_history->peer->id,
			Storage::SharedMediaType::Pinned,
			id));
	}
	if (changed) {
		_history->owner().notifyItemDataChange(this);
	}
}

void HistoryItem::returnSavedMedia() {
	if (!isEditingMedia()) {
		return;
	}
	const auto wasGrouped = history()->owner().groups().isGrouped(this);
	_media = std::move(_savedLocalEditMediaData->media);
	setText(_savedLocalEditMediaData->text);
	clearSavedMedia();
	if (wasGrouped) {
		history()->owner().groups().refreshMessage(this, true);
	} else {
		history()->owner().requestItemViewRefresh(this);
		history()->owner().updateDependentMessages(this);
	}
}

void HistoryItem::savePreviousMedia() {
	Expects(_media != nullptr);

	using Data = SavedMediaData;
	_savedLocalEditMediaData = std::make_unique<Data>(Data{
		.text = originalText(),
		.media = _media->clone(this),
	});
}

bool HistoryItem::isEditingMedia() const {
	return _savedLocalEditMediaData != nullptr;
}

void HistoryItem::clearSavedMedia() {
	_savedLocalEditMediaData = nullptr;
}

bool HistoryItem::definesReplyKeyboard() const {
	if (const auto markup = Get<HistoryMessageReplyMarkup>()) {
		if (markup->data.flags & ReplyMarkupFlag::Inline) {
			return false;
		}
		return true;
	}

	// optimization: don't create markup component for the case
	// MTPDreplyKeyboardHide with flags = 0, assume it has f_zero flag
	return (_flags & MessageFlag::HasReplyMarkup);
}

ReplyMarkupFlags HistoryItem::replyKeyboardFlags() const {
	Expects(definesReplyKeyboard());

	if (const auto markup = Get<HistoryMessageReplyMarkup>()) {
		return markup->data.flags;
	}

	// optimization: don't create markup component for the case
	// MTPDreplyKeyboardHide with flags = 0, assume it has f_zero flag
	return ReplyMarkupFlag::None;
}

void HistoryItem::addLogEntryOriginal(
		WebPageId localId,
		const QString &label,
		const TextWithEntities &content) {
	Expects(isAdminLogEntry());

	AddComponents(HistoryMessageLogEntryOriginal::Bit());
	Get<HistoryMessageLogEntryOriginal>()->page = _history->owner().webpage(
		localId,
		label,
		content);
}

PeerData *HistoryItem::specialNotificationPeer() const {
	return (mentionsMe() && !_history->peer->isUser())
		? from().get()
		: nullptr;
}

UserData *HistoryItem::viaBot() const {
	if (const auto via = Get<HistoryMessageVia>()) {
		return via->bot;
	}
	return nullptr;
}

UserData *HistoryItem::getMessageBot() const {
	if (const auto bot = viaBot()) {
		return bot;
	}
	auto bot = from()->asUser();
	if (!bot) {
		bot = _history->peer->asUser();
	}
	return (bot && bot->isBot()) ? bot : nullptr;
}

bool HistoryItem::isHistoryEntry() const {
	return (_flags & MessageFlag::HistoryEntry);
}

bool HistoryItem::isAdminLogEntry() const {
	return (_flags & MessageFlag::AdminLogEntry);
}

bool HistoryItem::isFromScheduled() const {
	return isHistoryEntry()
		&& (_flags & MessageFlag::IsOrWasScheduled);
}

bool HistoryItem::isScheduled() const {
	return !isHistoryEntry()
		&& !isAdminLogEntry()
		&& (_flags & MessageFlag::IsOrWasScheduled);
}

bool HistoryItem::isSponsored() const {
	return Has<HistoryMessageSponsored>();
}

bool HistoryItem::skipNotification() const {
	if (isSilent() && (_flags & MessageFlag::IsContactSignUp)) {
		return true;
	} else if (const auto forwarded = Get<HistoryMessageForwarded>()) {
		if (forwarded->imported) {
			return true;
		}
	}
	return false;
}

bool HistoryItem::isUserpicSuggestion() const {
	return (_flags & MessageFlag::IsUserpicSuggestion);
}

void HistoryItem::destroy() {
	_history->destroyMessage(this);
}

not_null<Data::Thread*> HistoryItem::notificationThread() const {
	if (const auto rootId = topicRootId()) {
		if (const auto forum = _history->asForum()) {
			return forum->enforceTopicFor(rootId);
		}
	}
	return _history;
}

Data::ForumTopic *HistoryItem::topic() const {
	if (const auto rootId = topicRootId()) {
		if (const auto forum = _history->asForum()) {
			return forum->topicFor(rootId);
		}
	}
	return nullptr;
}

void HistoryItem::refreshMainView() {
	if (const auto view = mainView()) {
		_history->owner().notifyHistoryChangeDelayed(_history);
		view->refreshInBlock();
	}
}

void HistoryItem::removeMainView() {
	if (const auto view = mainView()) {
		_history->owner().notifyHistoryChangeDelayed(_history);
		view->removeFromBlock();
	}
}

void HistoryItem::clearMainView() {
	_mainView = nullptr;
}

void HistoryItem::applyEdition(HistoryMessageEdition &&edition) {
	int keyboardTop = -1;
	//if (!pendingResize()) {// #TODO edit bot message
	//	if (auto keyboard = inlineReplyKeyboard()) {
	//		int h = st::msgBotKbButton.margin + keyboard->naturalHeight();
	//		keyboardTop = _height - h + st::msgBotKbButton.margin - marginBottom();
	//	}
	//}

	const auto updatingSavedLocalEdit = !edition.savePreviousMedia
		&& (_savedLocalEditMediaData != nullptr);
	if (!_savedLocalEditMediaData && edition.savePreviousMedia) {
		savePreviousMedia();
	}
	Assert(!updatingSavedLocalEdit || !isLocalUpdateMedia());

	if (edition.isEditHide) {
		_flags |= MessageFlag::HideEdited;
	} else {
		_flags &= ~MessageFlag::HideEdited;
	}

	if (edition.editDate != -1) {
		//_flags |= MTPDmessage::Flag::f_edit_date;
		if (!Has<HistoryMessageEdited>()) {
			AddComponents(HistoryMessageEdited::Bit());
		}
		auto edited = Get<HistoryMessageEdited>();
		edited->date = edition.editDate;
	}

	if (!edition.useSameMarkup) {
		setReplyMarkup(base::take(edition.replyMarkup));
	}
	if (updatingSavedLocalEdit) {
		_savedLocalEditMediaData->media = edition.mtpMedia
			? CreateMedia(this, *edition.mtpMedia)
			: nullptr;
	} else {
		removeFromSharedMediaIndex();
		refreshMedia(edition.mtpMedia);
	}
	if (!edition.useSameReactions) {
		updateReactions(edition.mtpReactions);
	}
	if (!edition.useSameViews) {
		changeViewsCount(edition.views);
	}
	if (!edition.useSameForwards) {
		setForwardsCount(edition.forwards);
	}
	const auto &checkedMedia = updatingSavedLocalEdit
		? _savedLocalEditMediaData->media
		: _media;
	auto updatedText = checkedMedia
		? edition.textWithEntities
		: EnsureNonEmpty(edition.textWithEntities);
	if (updatingSavedLocalEdit) {
		_savedLocalEditMediaData->text = std::move(updatedText);
	} else {
		setText(std::move(updatedText));
		addToSharedMediaIndex();
	}
	if (!edition.useSameReplies) {
		if (!edition.replies.isNull) {
			if (checkRepliesPts(edition.replies)) {
				setReplies(base::take(edition.replies));
			}
		} else {
			clearReplies();
		}
	}

	applyTTL(edition.ttl);

	finishEdition(keyboardTop);
}

void HistoryItem::applyChanges(not_null<Data::Story*> story) {
	Expects(_flags & MessageFlag::StoryItem);
	Expects(StoryIdFromMsgId(id) == story->id());

	_media = nullptr;
	setStoryFields(story);

	finishEdition(-1);
}

void HistoryItem::setStoryFields(not_null<Data::Story*> story) {
	const auto spoiler = false;
	if (const auto photo = story->photo()) {
		_media = std::make_unique<Data::MediaPhoto>(this, photo, spoiler);
	} else if (const auto document = story->document()) {
		_media = std::make_unique<Data::MediaFile>(
			this,
			document,
			/*skipPremiumEffect=*/false,
			spoiler);
	}
	setText(story->caption());
}

void HistoryItem::applyEdition(const MTPDmessageService &message) {
	if (message.vaction().type() == mtpc_messageActionHistoryClear) {
		const auto wasGrouped = history()->owner().groups().isGrouped(this);
		setReplyMarkup({});
		removeFromSharedMediaIndex();
		refreshMedia(nullptr);
		setTextValue({});
		changeViewsCount(-1);
		setForwardsCount(-1);
		if (wasGrouped) {
			history()->owner().groups().unregisterMessage(this);
		}
		if (const auto reply = Get<HistoryMessageReply>()) {
			reply->clearData(this);
		}
		clearDependencyMessage();
		UpdateComponents(0);
		createServiceFromMtp(message);
		applyServiceDateEdition(message);
		finishEditionToEmpty();
	} else if (isService()) {
		if (const auto reply = Get<HistoryMessageReply>()) {
			reply->clearData(this);
		}
		clearDependencyMessage();
		UpdateComponents(0);
		createServiceFromMtp(message);
		applyServiceDateEdition(message);
		finishEdition(-1);
	}
}

void HistoryItem::applyEdition(const MTPMessageExtendedMedia &media) {
	if (const auto existing = this->media()) {
		if (existing->updateExtendedMedia(this, media)) {
			checkBuyButton();
			finishEdition(-1);
		}
	}
}

void HistoryItem::applySentMessage(const MTPDmessage &data) {
	updateSentContent({
		qs(data.vmessage()),
		Api::EntitiesFromMTP(
			&_history->session(),
			data.ventities().value_or_empty())
	}, data.vmedia());
	updateReplyMarkup(HistoryMessageMarkupData(data.vreply_markup()));
	updateForwardedInfo(data.vfwd_from());
	changeViewsCount(data.vviews().value_or(-1));
	if (const auto replies = data.vreplies()) {
		setReplies(HistoryMessageRepliesData(replies));
	} else {
		clearReplies();
	}
	setForwardsCount(data.vforwards().value_or(-1));
	if (const auto reply = data.vreply_to()) {
		reply->match([&](const MTPDmessageReplyHeader &data) {
			setReplyFields(
				data.vreply_to_msg_id().v,
				data.vreply_to_top_id().value_or(
					data.vreply_to_msg_id().v),
				data.is_forum_topic());
		}, [](const MTPDmessageReplyStoryHeader &data) {
		});
	}
	setPostAuthor(data.vpost_author().value_or_empty());
	setIsPinned(data.is_pinned());
	contributeToSlowmode(data.vdate().v);
	addToSharedMediaIndex();
	invalidateChatListEntry();
	if (const auto period = data.vttl_period(); period && period->v > 0) {
		applyTTL(data.vdate().v + period->v);
	} else {
		applyTTL(0);
	}
	_history->owner().notifyItemDataChange(this);
	_history->owner().requestItemTextRefresh(this);
	_history->owner().updateDependentMessages(this);
}

void HistoryItem::applySentMessage(
		const QString &text,
		const MTPDupdateShortSentMessage &data,
		bool wasAlready) {
	updateSentContent({
		text,
		Api::EntitiesFromMTP(
			&_history->session(),
			data.ventities().value_or_empty())
		}, data.vmedia());
	contributeToSlowmode(data.vdate().v);
	if (!wasAlready) {
		addToSharedMediaIndex();
	}
	invalidateChatListEntry();
	if (const auto period = data.vttl_period(); period && period->v > 0) {
		applyTTL(data.vdate().v + period->v);
	} else {
		applyTTL(0);
	}
}

void HistoryItem::updateSentContent(
		const TextWithEntities &textWithEntities,
		const MTPMessageMedia *media) {
	if (_savedLocalEditMediaData) {
		return;
	}
	setText(textWithEntities);
	if (_flags & MessageFlag::FromInlineBot) {
		if (!media || !_media || !_media->updateInlineResultMedia(*media)) {
			refreshSentMedia(media);
		}
		_flags &= ~MessageFlag::FromInlineBot;
	} else if (media || _media) {
		if (!media || !_media || !_media->updateSentMedia(*media)) {
			refreshSentMedia(media);
		}
	}
	history()->owner().requestItemResize(this);
}

void HistoryItem::updateForwardedInfo(const MTPMessageFwdHeader *fwd) {
	const auto forwarded = Get<HistoryMessageForwarded>();
	if (!fwd) {
		if (forwarded) {
			LOG(("API Error: Server removed forwarded information."));
		}
		return;
	} else if (!forwarded) {
		LOG(("API Error: Server added forwarded information."));
		return;
	}
	fwd->match([&](const MTPDmessageFwdHeader &data) {
		auto config = CreateConfig();
		FillForwardedInfo(config, data);
		setupForwardedComponent(config);
		history()->owner().requestItemResize(this);
	});
}

void HistoryItem::applyEditionToHistoryCleared() {
	applyEdition(
		MTP_messageService(
			MTP_flags(0),
			MTP_int(id),
			peerToMTP(PeerId(0)), // from_id
			peerToMTP(_history->peer->id),
			MTPMessageReplyHeader(),
			MTP_int(date()),
			MTP_messageActionHistoryClear(),
			MTPint() // ttl_period
		).c_messageService());
}

void HistoryItem::updateReplyMarkup(HistoryMessageMarkupData &&markup) {
	setReplyMarkup(std::move(markup));
}

void HistoryItem::contributeToSlowmode(TimeId realDate) {
	if (const auto channel = history()->peer->asChannel()) {
		if (out() && isRegular() && !isService()) {
			channel->growSlowmodeLastMessage(realDate ? realDate : date());
		}
	}
}

void HistoryItem::addToUnreadThings(HistoryUnreadThings::AddType type) {
	if (!isRegular()) {
		return;
	}
	const auto mention = isUnreadMention();
	const auto reaction = hasUnreadReaction();
	if (!mention && !reaction) {
		return;
	}
	const auto topic = this->topic();
	const auto history = this->history();
	const auto changes = &history->session().changes();
	if (mention) {
		if (history->unreadMentions().add(id, type)) {
			changes->historyUpdated(
				history,
				Data::HistoryUpdate::Flag::UnreadMentions);
		}
		if (topic && topic->unreadMentions().add(id, type)) {
			changes->topicUpdated(
				topic,
				Data::TopicUpdate::Flag::UnreadMentions);
		}
	}
	if (reaction) {
		const auto toHistory = history->unreadReactions().add(id, type);
		const auto toTopic = topic && topic->unreadReactions().add(id, type);
		if (toHistory || toTopic) {
			if (type == HistoryUnreadThings::AddType::New) {
				changes->messageUpdated(
					this,
					Data::MessageUpdate::Flag::NewUnreadReaction);
			}
			if (hasUnreadReaction()) {
				if (toHistory) {
					changes->historyUpdated(
						history,
						Data::HistoryUpdate::Flag::UnreadReactions);
				}
				if (toTopic) {
					changes->topicUpdated(
						topic,
						Data::TopicUpdate::Flag::UnreadReactions);
				}
			}
		}
	}
}

void HistoryItem::destroyHistoryEntry() {
	if (isUnreadMention()) {
		history()->unreadMentions().erase(id);
		if (const auto topic = this->topic()) {
			topic->unreadMentions().erase(id);
		}
	}
	if (hasUnreadReaction()) {
		history()->unreadReactions().erase(id);
		if (const auto topic = this->topic()) {
			topic->unreadReactions().erase(id);
		}
	}
	if (const auto reply = Get<HistoryMessageReply>()) {
		changeReplyToTopCounter(reply, -1);
	}
}

Storage::SharedMediaTypesMask HistoryItem::sharedMediaTypes() const {
	auto result = Storage::SharedMediaTypesMask {};
	const auto media = _savedLocalEditMediaData
		? _savedLocalEditMediaData->media.get()
		: _media.get();
	if (media) {
		result.set(media->sharedMediaTypes());
	}
	if (hasTextLinks()) {
		result.set(Storage::SharedMediaType::Link);
	}
	if (isPinned()) {
		result.set(Storage::SharedMediaType::Pinned);
	}
	return result;
}

void HistoryItem::indexAsNewItem() {
	if (isRegular()) {
		addToUnreadThings(HistoryUnreadThings::AddType::New);
	}
	addToSharedMediaIndex();
}

void HistoryItem::addToSharedMediaIndex() {
	if (isRegular()) {
		if (const auto types = sharedMediaTypes()) {
			_history->session().storage().add(Storage::SharedMediaAddNew(
				_history->peer->id,
				topicRootId(),
				types,
				id));
			if (types.test(Storage::SharedMediaType::Pinned)) {
				_history->setHasPinnedMessages(true);
				if (const auto topic = this->topic()) {
					topic->setHasPinnedMessages(true);
				}
			}
		}
	}
}

void HistoryItem::removeFromSharedMediaIndex() {
	if (isRegular()) {
		if (const auto types = sharedMediaTypes()) {
			_history->session().storage().remove(
				Storage::SharedMediaRemoveOne(
					_history->peer->id,
					types,
					id));
		}
	}
}

void HistoryItem::incrementReplyToTopCounter() {
	if (const auto reply = Get<HistoryMessageReply>()) {
		changeReplyToTopCounter(reply, 1);
	}
}

void HistoryItem::changeReplyToTopCounter(
		not_null<HistoryMessageReply*> reply,
		int delta) {
	if (!isRegular() || !_history->peer->isMegagroup()) {
		return;
	} else if (delta > 0) {
		_history->session().changes().messageUpdated(
			this,
			Data::MessageUpdate::Flag::ReplyToTopAdded);
	}
	const auto topId = reply->replyToTop();
	if (!topId) {
		return;
	}
	const auto top = _history->owner().message(_history->peer->id, topId);
	if (!top) {
		return;
	}
	const auto from = displayFrom();
	const auto replier = from ? from->id : PeerId();
	top->changeRepliesCount(delta, replier);
	if (const auto original = top->lookupDiscussionPostOriginal()) {
		original->changeRepliesCount(delta, replier);
	}
}

QString HistoryItem::notificationHeader() const {
	if (isService()) {
		return QString();
	} else if (out() && isFromScheduled() && !_history->peer->isSelf()) {
		return tr::lng_from_you(tr::now);
	} else if (!_history->peer->isUser() && !isPost()) {
		return from()->name();
	}
	return QString();
}

void HistoryItem::setRealId(MsgId newId) {
	Expects(_flags & MessageFlag::BeingSent);
	Expects(IsClientMsgId(id));

	const auto oldId = std::exchange(id, newId);
	_flags &= ~(MessageFlag::BeingSent | MessageFlag::Local);
	if (isRegular()) {
		_history->unregisterClientSideMessage(this);
	}
	_history->owner().notifyItemIdChange({ fullId(), oldId });

	// We don't fire MessageUpdate::Flag::ReplyMarkup and update keyboard
	// in history widget, because it can't exist for an outgoing message.
	// Only inline keyboards can be in outgoing messages.
	if (const auto markup = inlineReplyMarkup()) {
		if (markup->inlineKeyboard) {
			markup->inlineKeyboard->updateMessageId();
		}
	}

	_history->owner().notifyItemDataChange(this);
	_history->owner().groups().refreshMessage(this);
	_history->owner().requestItemResize(this);

	if (const auto reply = Get<HistoryMessageReply>()) {
		if (reply->replyToLink()) {
			reply->setReplyToLinkFrom(this);
		}
		changeReplyToTopCounter(reply, 1);
	}
}

bool HistoryItem::canPin() const {
	if (!isRegular() || isService()) {
		return false;
	} else if (const auto m = media(); m && m->call()) {
		return false;
	}
	return _history->peer->canPinMessages();
}

bool HistoryItem::allowsSendNow() const {
	return !isService()
		&& isScheduled()
		&& !isSending()
		&& !hasFailed()
		&& !isEditingMedia();
}

bool HistoryItem::allowsForward() const {
	return !isService()
		&& isRegular()
		&& !forbidsForward()
		&& history()->peer->allowsForwarding()
		&& (!_media || _media->allowsForward());
}

bool HistoryItem::isTooOldForEdit(TimeId now) const {
	return !_history->peer->canEditMessagesIndefinitely()
		&& !isScheduled()
		&& (now - date() >= _history->session().serverConfig().editTimeLimit);
}

bool HistoryItem::allowsEdit(TimeId now) const {
	return !isService()
		&& canBeEdited()
		&& !isTooOldForEdit(now)
		&& (!_media || _media->allowsEdit())
		&& !isLegacyMessage()
		&& !isEditingMedia();
}

bool HistoryItem::canBeEdited() const {
	if ((!isRegular() && !isScheduled())
		|| Has<HistoryMessageVia>()
		|| Has<HistoryMessageForwarded>()) {
		return false;
	}

	const auto peer = _history->peer;
	if (peer->isSelf()) {
		return true;
	} else if (const auto channel = peer->asChannel()) {
		if (isPost() && channel->canEditMessages()) {
			return true;
		} else if (out()) {
			if (isPost()) {
				return channel->canPostMessages();
			} else if (const auto topic = this->topic()) {
				return Data::CanSendAnything(topic);
			} else {
				return Data::CanSendAnything(channel);
			}
		} else {
			return false;
		}
	}
	return out();
}

bool HistoryItem::canStopPoll() const {
	return canBeEdited() && isRegular();
}

bool HistoryItem::forbidsForward() const {
	return (_flags & MessageFlag::NoForwards);
}

bool HistoryItem::forbidsSaving() const {
	if (forbidsForward()) {
		return true;
	} else if (const auto invoice = _media ? _media->invoice() : nullptr) {
		return (invoice->extendedMedia != nullptr);
	}
	return false;
}

bool HistoryItem::canDelete() const {
	if (isSponsored()) {
		return false;
	} else if (IsStoryMsgId(id)) {
		return false;
	} else if (isService() && !isRegular()) {
		return false;
	} else if (topicRootId() == id) {
		return false;
	} else if (!isHistoryEntry() && !isScheduled()) {
		return false;
	}
	auto channel = _history->peer->asChannel();
	if (!channel) {
		return !isGroupMigrate();
	}

	if (id == 1) {
		return false;
	}
	if (channel->canDeleteMessages()) {
		return true;
	} else if (out() && !isService()) {
		return isPost() ? channel->canPostMessages() : true;
	}
	return false;
}

bool HistoryItem::canDeleteForEveryone(TimeId now) const {
	const auto peer = _history->peer;
	const auto &config = _history->session().serverConfig();
	const auto messageToMyself = peer->isSelf();
	const auto messageTooOld = messageToMyself
		? false
		: peer->isUser()
		? (now - date() >= config.revokePrivateTimeLimit)
		: (now - date() >= config.revokeTimeLimit);
	if (!isRegular() || messageToMyself || messageTooOld || isPost()) {
		return false;
	}
	if (peer->isChannel()) {
		return false;
	} else if (const auto user = peer->asUser()) {
		// Bots receive all messages and there is no sense in revoking them.
		// See https://github.com/telegramdesktop/tdesktop/issues/3818
		if (user->isBot() && !user->isSupport()) {
			return false;
		}
	}
	if (const auto media = this->media()) {
		if (!media->allowsRevoke(now)) {
			return false;
		}
	}
	if (!out()) {
		if (const auto chat = peer->asChat()) {
			if (!chat->canDeleteMessages()) {
				return false;
			}
		} else if (peer->isUser()) {
			return config.revokePrivateInbox;
		} else {
			return false;
		}
	}
	return true;
}

bool HistoryItem::suggestReport() const {
	if (out() || isService() || !isRegular()) {
		return false;
	} else if (const auto channel = _history->peer->asChannel()) {
		return true;
	} else if (const auto user = _history->peer->asUser()) {
		return user->isBot();
	}
	return false;
}

bool HistoryItem::suggestBanReport() const {
	const auto channel = _history->peer->asChannel();
	if (!channel || !channel->canRestrictParticipant(from())) {
		return false;
	}
	return !isPost() && !out();
}

bool HistoryItem::suggestDeleteAllReport() const {
	auto channel = _history->peer->asChannel();
	if (!channel || !channel->canDeleteMessages()) {
		return false;
	}
	return !isPost() && !out();
}

ChatRestriction HistoryItem::requiredSendRight() const {
	const auto media = this->media();
	if (media && media->game()) {
		return ChatRestriction::SendGames;
	}
	const auto photo = (media && !media->webpage())
		? media->photo()
		: nullptr;
	const auto document = (media && !media->webpage())
		? media->document()
		: nullptr;
	if (photo) {
		return ChatRestriction::SendPhotos;
	} else if (document) {
		return document->requiredSendRight();
	} else if (media && media->poll()) {
		return ChatRestriction::SendPolls;
	}
	return ChatRestriction::SendOther;
}

bool HistoryItem::requiresSendInlineRight() const {
	return Has<HistoryMessageVia>();
}

std::optional<QString> HistoryItem::errorTextForForward(
		not_null<Data::Thread*> to) const {
	const auto requiredRight = requiredSendRight();
	const auto requiresInline = requiresSendInlineRight();
	const auto peer = to->peer();
	constexpr auto kInline = ChatRestriction::SendInline;
	if (const auto error = Data::RestrictionError(peer, requiredRight)) {
		return *error;
	} else if (requiresInline && !Data::CanSend(to, kInline)) {
		return Data::RestrictionError(peer, kInline).value_or(
			tr::lng_forward_cant(tr::now));
	} else if (_media
		&& _media->poll()
		&& _media->poll()->publicVotes()
		&& peer->isBroadcast()) {
		return tr::lng_restricted_send_public_polls(tr::now);
	} else if (!Data::CanSend(to, requiredRight, false)) {
		return tr::lng_forward_cant(tr::now);
	}
	return {};
}

const HistoryMessageTranslation *HistoryItem::translation() const {
	return Get<HistoryMessageTranslation>();
}

bool HistoryItem::translationShowRequiresCheck(LanguageId to) const {
	// Check if a call to translationShowRequiresRequest(to) is not a no-op.
	if (!to) {
		if (const auto translation = Get<HistoryMessageTranslation>()) {
			return (!translation->failed && translation->text.empty())
				|| translation->used;
		}
		return false;
	} else if (const auto translation = Get<HistoryMessageTranslation>()) {
		if (translation->to == to) {
			return !translation->used && !translation->text.empty();
		}
		return true;
	} else {
		return true;
	}
}

bool HistoryItem::translationShowRequiresRequest(LanguageId to) {
	// When changing be sure to reflect in translationShowRequiresCheck(to).
	if (!to) {
		if (const auto translation = Get<HistoryMessageTranslation>()) {
			if (!translation->failed && translation->text.empty()) {
				Assert(!translation->used);
				RemoveComponents(HistoryMessageTranslation::Bit());
			} else {
				translationToggle(translation, false);
			}
		}
		return false;
	} else if (const auto translation = Get<HistoryMessageTranslation>()) {
		if (translation->to == to) {
			translationToggle(translation, true);
			return false;
		}
		translationToggle(translation, false);
		translation->to = to;
		translation->requested = true;
		translation->failed = false;
		translation->text = {};
		return true;
	} else {
		AddComponents(HistoryMessageTranslation::Bit());
		const auto added = Get<HistoryMessageTranslation>();
		added->to = to;
		added->requested = true;
		return true;
	}
}

void HistoryItem::translationToggle(
		not_null<HistoryMessageTranslation*> translation,
		bool used) {
	if (translation->used != used && !translation->text.empty()) {
		translation->used = used;
		_history->owner().requestItemTextRefresh(this);
		_history->owner().updateDependentMessages(this);
	}
}

void HistoryItem::translationDone(LanguageId to, TextWithEntities result) {
	const auto set = [&](not_null<HistoryMessageTranslation*> translation) {
		if (result.empty()) {
			translation->failed = true;
		} else {
			translation->text = std::move(result);
			if (_history->translatedTo() == to) {
				translationToggle(translation, true);
			}
		}
	};
	if (const auto translation = Get<HistoryMessageTranslation>()) {
		if (translation->to == to && translation->text.empty()) {
			translation->requested = false;
			set(translation);
		}
	} else {
		AddComponents(HistoryMessageTranslation::Bit());
		const auto added = Get<HistoryMessageTranslation>();
		added->to = to;
		set(added);
	}
}

bool HistoryItem::canReact() const {
	if (!isRegular() || isService()) {
		return false;
	} else if (const auto media = this->media()) {
		if (media->call()) {
			return false;
		}
	}
	return true;
}

void HistoryItem::toggleReaction(
		const Data::ReactionId &reaction,
		ReactionSource source) {
	if (!_reactions) {
		_reactions = std::make_unique<Data::MessageReactions>(this);
		const auto canViewReactions = !isDiscussionPost()
			&& (_history->peer->isChat() || _history->peer->isMegagroup());
		if (canViewReactions) {
			_flags |= MessageFlag::CanViewReactions;
		}
		_reactions->add(reaction, (source == ReactionSource::Selector));
	} else if (ranges::contains(_reactions->chosen(), reaction)) {
		_reactions->remove(reaction);
		if (_reactions->empty()) {
			_reactions = nullptr;
			_flags &= ~MessageFlag::CanViewReactions;
			_history->owner().notifyItemDataChange(this);
		}
	} else {
		_reactions->add(reaction, (source == ReactionSource::Selector));
	}
	_history->owner().notifyItemDataChange(this);
}

void HistoryItem::updateReactionsUnknown() {
	_reactionsLastRefreshed = 1;
}

const std::vector<Data::MessageReaction> &HistoryItem::reactions() const {
	static const auto kEmpty = std::vector<Data::MessageReaction>();
	return _reactions ? _reactions->list() : kEmpty;
}

auto HistoryItem::recentReactions() const
-> const base::flat_map<
		Data::ReactionId,
		std::vector<Data::RecentReaction>> & {
	static const auto kEmpty = base::flat_map<
		Data::ReactionId,
		std::vector<Data::RecentReaction>>();
	return _reactions ? _reactions->recent() : kEmpty;
}

bool HistoryItem::canViewReactions() const {
	return (_flags & MessageFlag::CanViewReactions)
		&& _reactions
		&& !_reactions->list().empty();
}

std::vector<Data::ReactionId> HistoryItem::chosenReactions() const {
	return _reactions
		? _reactions->chosen()
		: std::vector<Data::ReactionId>();
}

Data::ReactionId HistoryItem::lookupUnreadReaction(
		not_null<UserData*> from) const {
	if (!_reactions) {
		return {};
	}
	const auto recent = _reactions->recent();
	for (const auto &[id, list] : _reactions->recent()) {
		const auto i = ranges::find(
			list,
			from,
			&Data::RecentReaction::peer);
		if (i != end(list) && i->unread) {
			return id;
		}
	}
	return {};
}

crl::time HistoryItem::lastReactionsRefreshTime() const {
	return _reactionsLastRefreshed;
}

bool HistoryItem::hasDirectLink() const {
	return isRegular() && _history->peer->isChannel();
}

bool HistoryItem::changesWallPaper() const {
	if (const auto media = _media.get()) {
		return media->paper() != nullptr;
	}
	return Has<HistoryServiceSameBackground>();
}

FullMsgId HistoryItem::fullId() const {
	return FullMsgId(_history->peer->id, id);
}

GlobalMsgId HistoryItem::globalId() const {
	return { fullId(), _history->session().uniqueId() };
}

Data::MessagePosition HistoryItem::position() const {
	return { .fullId = fullId(), .date = date() };
}

bool HistoryItem::computeDropForwardedInfo() const {
	const auto media = this->media();
	return (media && media->dropForwardedInfo())
		|| (_history->peer->isSelf()
			&& !Has<HistoryMessageForwarded>()
			&& (!media || !media->forceForwardedInfo()));
}

bool HistoryItem::inThread(MsgId rootId) const {
	return (replyToTop() == rootId)
		|| (topicRootId() == rootId);
}

not_null<PeerData*> HistoryItem::author() const {
	return (isPost() && !isSponsored()) ? _history->peer : from();
}

TimeId HistoryItem::dateOriginal() const {
	if (const auto forwarded = Get<HistoryMessageForwarded>()) {
		return forwarded->originalDate;
	}
	return date();
}

PeerData *HistoryItem::senderOriginal() const {
	if (const auto forwarded = Get<HistoryMessageForwarded>()) {
		return forwarded->originalSender;
	}
	const auto peer = _history->peer;
	return (peer->isChannel() && !peer->isMegagroup()) ? peer : from();
}

const HiddenSenderInfo *HistoryItem::hiddenSenderInfo() const {
	if (const auto sponsored = Get<HistoryMessageSponsored>()) {
		return sponsored->sender.get();
	} else if (const auto forwarded = Get<HistoryMessageForwarded>()) {
		return forwarded->hiddenSenderInfo.get();
	}
	return nullptr;
}

not_null<PeerData*> HistoryItem::fromOriginal() const {
	if (const auto forwarded = Get<HistoryMessageForwarded>()) {
		if (forwarded->originalSender) {
			if (const auto user = forwarded->originalSender->asUser()) {
				return user;
			}
		}
	}
	return from();
}

QString HistoryItem::authorOriginal() const {
	if (const auto forwarded = Get<HistoryMessageForwarded>()) {
		return forwarded->originalAuthor;
	} else if (const auto msgsigned = Get<HistoryMessageSigned>()) {
		if (!msgsigned->isAnonymousRank) {
			return msgsigned->author;
		}
	}
	return QString();
}

MsgId HistoryItem::idOriginal() const {
	if (const auto forwarded = Get<HistoryMessageForwarded>()) {
		return forwarded->originalId;
	}
	return id;
}

const TextWithEntities &HistoryItem::originalText() const {
	static const auto kEmpty = TextWithEntities();
	return isService() ? kEmpty : _text;
}

const TextWithEntities &HistoryItem::translatedText() const {
	if (isService()) {
		static const auto kEmpty = TextWithEntities();
		return kEmpty;
	} else if (const auto translation = this->translation()
		; translation
		&& translation->used
		&& (translation->to == history()->translatedTo())) {
		return translation->text;
	} else {
		return originalText();
	}
}

TextWithEntities HistoryItem::translatedTextWithLocalEntities() const {
	if (isService()) {
		return {};
	} else {
		return withLocalEntities(translatedText());
	}
}

TextForMimeData HistoryItem::clipboardText() const {
	return isService()
		? TextForMimeData()
		: TextForMimeData::WithExpandedLinks(_text);
}

bool HistoryItem::changeViewsCount(int count) {
	const auto views = Get<HistoryMessageViews>();
	if (!views
		|| views->views.count == count
		|| (count >= 0 && views->views.count > count)) {
		return false;
	}

	views->views.count = count;
	return true;
}

void HistoryItem::setForwardsCount(int count) {
	const auto views = Get<HistoryMessageViews>();
	if (!views
		|| views->forwardsCount == count
		|| (count >= 0 && views->forwardsCount > count)) {
		return;
	}

	views->forwardsCount = count;
	history()->owner().notifyItemDataChange(this);
}

void HistoryItem::setPostAuthor(const QString &author) {
	auto msgsigned = Get<HistoryMessageSigned>();
	if (author.isEmpty()) {
		if (!msgsigned) {
			return;
		}
		RemoveComponents(HistoryMessageSigned::Bit());
		history()->owner().requestItemResize(this);
		return;
	}
	if (!msgsigned) {
		AddComponents(HistoryMessageSigned::Bit());
		msgsigned = Get<HistoryMessageSigned>();
	} else if (msgsigned->author == author) {
		return;
	}
	msgsigned->author = author;
	msgsigned->isAnonymousRank = !isDiscussionPost()
		&& this->author()->isMegagroup();
	history()->owner().requestItemResize(this);
}

void HistoryItem::setReplies(HistoryMessageRepliesData &&data) {
	if (data.isNull) {
		return;
	}
	auto views = Get<HistoryMessageViews>();
	if (!views) {
		AddComponents(HistoryMessageViews::Bit());
		views = Get<HistoryMessageViews>();
	}
	const auto &repliers = data.recentRepliers;
	const auto count = data.repliesCount;
	const auto channelId = data.channelId;
	const auto readTillId = data.readMaxId
		? std::max({
			views->commentsInboxReadTillId.bare,
			data.readMaxId.bare,
			int64(1),
		})
		: views->commentsInboxReadTillId;
	const auto maxId = data.maxId ? data.maxId : views->commentsMaxId;
	const auto countsChanged = (views->replies.count != count)
		|| (views->commentsInboxReadTillId != readTillId)
		|| (views->commentsMaxId != maxId);
	const auto megagroupChanged = (views->commentsMegagroupId != channelId);
	const auto recentChanged = (views->recentRepliers != repliers);
	if (!countsChanged && !megagroupChanged && !recentChanged) {
		return;
	}
	views->replies.count = count;
	if (recentChanged) {
		views->recentRepliers = repliers;
	}
	const auto wasUnread = areCommentsUnread();
	views->commentsMegagroupId = channelId;
	views->commentsInboxReadTillId = readTillId;
	views->commentsMaxId = maxId;
	if (wasUnread != areCommentsUnread()) {
		history()->owner().requestItemRepaint(this);
	}
	refreshRepliesText(views, megagroupChanged);
}

void HistoryItem::clearReplies() {
	auto views = Get<HistoryMessageViews>();
	if (!views) {
		return;
	}
	const auto viewsPart = views->views;
	if (viewsPart.count < 0) {
		RemoveComponents(HistoryMessageViews::Bit());
	} else {
		*views = HistoryMessageViews();
		views->views = viewsPart;
	}
	history()->owner().requestItemResize(this);
}

void HistoryItem::refreshRepliesText(
		not_null<HistoryMessageViews*> views,
		bool forceResize) {
	if (views->commentsMegagroupId) {
		views->replies.text = (views->replies.count > 0)
			? tr::lng_comments_open_count(
				tr::now,
				lt_count_short,
				views->replies.count)
			: tr::lng_comments_open_none(tr::now);
		views->replies.textWidth = st::semiboldFont->width(
			views->replies.text);
		views->repliesSmall.text = (views->replies.count > 0)
			? Lang::FormatCountToShort(views->replies.count).string
			: QString();
		const auto hadText = (views->repliesSmall.textWidth > 0);
		views->repliesSmall.textWidth = (views->replies.count > 0)
			? st::semiboldFont->width(views->repliesSmall.text)
			: 0;
		const auto hasText = (views->repliesSmall.textWidth > 0);
		if (hasText != hadText) {
			forceResize = true;
		}
	}
	if (forceResize) {
		history()->owner().requestItemResize(this);
	} else {
		history()->owner().requestItemRepaint(this);
	}
}

void HistoryItem::changeRepliesCount(int delta, PeerId replier) {
	const auto views = Get<HistoryMessageViews>();
	const auto limit = HistoryMessageViews::kMaxRecentRepliers;
	if (!views) {
		return;
	}

	// Update full count.
	if (views->replies.count < 0) {
		return;
	}
	views->replies.count = std::max(views->replies.count + delta, 0);
	if (replier && views->commentsMegagroupId) {
		if (delta < 0) {
			views->recentRepliers.erase(
				ranges::remove(views->recentRepliers, replier),
				end(views->recentRepliers));
		} else if (!ranges::contains(views->recentRepliers, replier)) {
			views->recentRepliers.insert(views->recentRepliers.begin(), replier);
			while (views->recentRepliers.size() > limit) {
				views->recentRepliers.pop_back();
			}
		}
	}
	refreshRepliesText(views);
	history()->owner().notifyItemDataChange(this);
}

void HistoryItem::setReplyFields(
		MsgId replyTo,
		MsgId replyToTop,
		bool isForumPost) {
	if (isScheduled()) {
		return;
	} else if (const auto data = GetServiceDependentData()) {
		if ((data->topId != replyToTop) && !IsServerMsgId(data->topId)) {
			data->topId = replyToTop;
			if (isForumPost) {
				data->topicPost = true;
			}
		}
	} else if (const auto reply = Get<HistoryMessageReply>()) {
		reply->topicPost = isForumPost;
		if ((reply->replyToMsgId != replyTo)
			&& !IsServerMsgId(reply->replyToMsgId)) {
			reply->replyToMsgId = replyTo;
			if (!reply->updateData(this)) {
				RequestDependentMessageItem(
					this,
					reply->replyToPeerId,
					reply->replyToMsgId);
			}
		}
		if ((reply->replyToMsgTop != replyToTop)
			&& !IsServerMsgId(reply->replyToMsgTop)) {
			reply->replyToMsgTop = replyToTop;
			changeReplyToTopCounter(reply, 1);
		}
	}
	if (const auto topic = this->topic()) {
		topic->maybeSetLastMessage(this);
	}
}

void HistoryItem::updateDate(TimeId newDate) {
	if (canUpdateDate() && _date != newDate) {
		_date = newDate;
		_history->owner().requestItemViewRefresh(this);
	}
}

bool HistoryItem::canUpdateDate() const {
	return isScheduled();
}

void HistoryItem::applyTTL(TimeId destroyAt) {
	const auto previousDestroyAt = std::exchange(_ttlDestroyAt, destroyAt);
	if (previousDestroyAt) {
		_history->owner().unregisterMessageTTL(previousDestroyAt, this);
	}
	if (!_ttlDestroyAt) {
		return;
	} else if (base::unixtime::now() >= _ttlDestroyAt) {
		const auto session = &_history->session();
		crl::on_main(session, [session, id = fullId()]{
			if (const auto item = session->data().message(id)) {
				item->destroy();
			}
		});
	} else {
		_history->owner().registerMessageTTL(_ttlDestroyAt, this);
	}
}

void HistoryItem::replaceBuyWithReceiptInMarkup() {
	if (const auto markup = inlineReplyMarkup()) {
		for (auto &row : markup->data.rows) {
			for (auto &button : row) {
				if (button.type == HistoryMessageMarkupButton::Type::Buy) {
					const auto receipt = tr::lng_payments_receipt_button(tr::now);
					if (button.text != receipt) {
						button.text = receipt;
						if (markup->inlineKeyboard) {
							markup->inlineKeyboard = nullptr;
							_history->owner().requestItemResize(this);
						}
					}
				}
			}
		}
	}
}

bool HistoryItem::isUploading() const {
	return _media && _media->uploading();
}

bool HistoryItem::isRegular() const {
	return isHistoryEntry() && !isLocal();
}

int HistoryItem::viewsCount() const {
	if (const auto views = Get<HistoryMessageViews>()) {
		return std::max(views->views.count, 0);
	}
	return hasViews() ? 1 : -1;
}

int HistoryItem::repliesCount() const {
	if (const auto views = Get<HistoryMessageViews>()) {
		if (!checkCommentsLinkedChat(views->commentsMegagroupId)) {
			return 0;
		}
		return std::max(views->replies.count, 0);
	}
	return 0;
}

bool HistoryItem::repliesAreComments() const {
	if (const auto views = Get<HistoryMessageViews>()) {
		return (views->commentsMegagroupId != 0)
			&& checkCommentsLinkedChat(views->commentsMegagroupId);
	}
	return false;
}

bool HistoryItem::externalReply() const {
	if (!_history->peer->isRepliesChat()) {
		return false;
	} else if (const auto forwarded = Get<HistoryMessageForwarded>()) {
		return forwarded->savedFromPeer && forwarded->savedFromMsgId;
	}
	return false;
}

bool HistoryItem::hasExtendedMediaPreview() const {
	if (const auto media = _media.get()) {
		if (const auto invoice = media->invoice()) {
			return (invoice->extendedPreview && !invoice->extendedMedia);
		}
	}
	return false;
}

void HistoryItem::sendFailed() {
	Expects(_flags & MessageFlag::BeingSent);
	Expects(!(_flags & MessageFlag::SendingFailed));

	_flags = (_flags | MessageFlag::SendingFailed) & ~MessageFlag::BeingSent;
	_history->owner().notifyItemDataChange(this);
	_history->session().changes().historyUpdated(
		_history,
		Data::HistoryUpdate::Flag::ClientSideMessages);
}

bool HistoryItem::needCheck() const {
	return (out() && !isEmpty())
		|| (!isRegular() && _history->peer->isSelf());
}

bool HistoryItem::isService() const {
	return Has<HistoryServiceData>();
}

bool HistoryItem::unread(not_null<Data::Thread*> thread) const {
	// Messages from myself are always read, unless scheduled.
	if (_history->peer->isSelf() && !isFromScheduled()) {
		return false;
	}

	// All messages in converted chats are always read.
	if (_history->peer->migrateTo()) {
		return false;
	}

	if (isRegular()) {
		if (!thread->isServerSideUnread(this)) {
			return false;
		}
		if (out()) {
			if (const auto user = _history->peer->asUser()) {
				if (user->isBot() && !user->isSupport()) {
					return false;
				}
			} else if (const auto channel = _history->peer->asChannel()) {
				if (!channel->isMegagroup()) {
					return false;
				}
			}
		}
		return true;
	}

	return out() || (_flags & MessageFlag::ClientSideUnread);
}

MsgId HistoryItem::replyToId() const {
	if (const auto reply = Get<HistoryMessageReply>()) {
		return reply->replyToId();
	}
	return 0;
}

MsgId HistoryItem::replyToTop() const {
	if (const auto reply = Get<HistoryMessageReply>()) {
		return reply->replyToTop();
	} else if (const auto data = GetServiceDependentData()) {
		return data->topId;
	}
	return 0;
}

MsgId HistoryItem::topicRootId() const {
	if (const auto reply = Get<HistoryMessageReply>()
		; reply && reply->topicPost) {
		return reply->replyToTop();
	} else if (const auto data = GetServiceDependentData()
		; data && data->topicPost && data->topId) {
		return data->topId;
	} else if (const auto info = Get<HistoryServiceTopicInfo>()) {
		if (info->created()) {
			return id;
		}
	}
	return Data::ForumTopic::kGeneralId;
}

FullStoryId HistoryItem::replyToStory() const {
	if (const auto reply = Get<HistoryMessageReply>()) {
		if (reply->replyToStoryId) {
			const auto peerId = reply->replyToPeerId
				? reply->replyToPeerId
				: _history->peer->id;
			return { .peer = peerId, .story = reply->replyToStoryId };
		}
	}
	return {};
}

FullReplyTo HistoryItem::replyTo() const {
	return {
		.msgId = replyToId(),
		.topicRootId = topicRootId(),
		.storyId = replyToStory(),
	};
}

void HistoryItem::setText(const TextWithEntities &textWithEntities) {
	for (const auto &entity : textWithEntities.entities) {
		auto type = entity.type();
		if (type == EntityType::Url
			|| type == EntityType::CustomUrl
			|| type == EntityType::Email) {
			_flags |= MessageFlag::HasTextLinks;
			break;
		}
	}
	setTextValue((_media && _media->consumeMessageText(textWithEntities))
		? TextWithEntities()
		: std::move(textWithEntities));
}

void HistoryItem::setTextValue(TextWithEntities text) {
	const auto had = !_text.empty();
	_text = std::move(text);
	RemoveComponents(HistoryMessageTranslation::Bit());
	if (had) {
		history()->owner().requestItemTextRefresh(this);
	}
}

bool HistoryItem::showNotification() const {
	const auto channel = _history->peer->asChannel();
	if (channel && !channel->amIn()) {
		return false;
	}
	return (out() || _history->peer->isSelf())
		? isFromScheduled()
		: unread(notificationThread());
}

void HistoryItem::markClientSideAsRead() {
	_flags &= ~MessageFlag::ClientSideUnread;
}

MessageGroupId HistoryItem::groupId() const {
	return _groupId;
}

bool HistoryItem::isEmpty() const {
	return _text.empty()
		&& !_media
		&& !Has<HistoryMessageLogEntryOriginal>();
}

TextWithEntities HistoryItem::notificationText(
		NotificationTextOptions options) const {
	auto result = [&] {
		if (_media && !isService()) {
			return _media->notificationText();
		} else if (!emptyText()) {
			return _text;
		}
		return TextWithEntities();
	}();
	if (options.spoilerLoginCode
		&& !out()
		&& history()->peer->isNotificationsUser()) {
		result = SpoilerLoginCode(std::move(result));
	}
	if (result.text.size() <= kNotificationTextLimit) {
		return result;
	}
	return Ui::Text::Mid(result, 0, kNotificationTextLimit).append(
		Ui::kQEllipsis);
}

ItemPreview HistoryItem::toPreview(ToPreviewOptions options) const {
	if (isService()) {
		// Don't show small media for service messages (chat photo changed).
		// Because larger version is shown exactly to the left of the small.
		//auto media = _media ? _media->toPreview(options) : ItemPreview();
		return {
			.text = Ui::Text::Wrapped(
				notificationText(),
				EntityType::PlainLink),
			//.images = std::move(media.images),
			//.loadingContext = std::move(media.loadingContext),
		};
	}

	auto result = [&]() -> ItemPreview {
		if (_media) {
			return _media->toPreview(options);
		} else if (!emptyText()) {
			return { .text = options.translated ? translatedText() : _text };
		}
		return {};
	}();
	if (options.spoilerLoginCode
		&& !out()
		&& history()->peer->isNotificationsUser()) {
		result.text = SpoilerLoginCode(std::move(result.text));
	}
	const auto fromSender = [](not_null<PeerData*> sender) {
		return sender->isSelf()
			? tr::lng_from_you(tr::now)
			: sender->shortName();
	};
	result.icon = (Get<HistoryMessageForwarded>() != nullptr)
		? ItemPreview::Icon::ForwardedMessage
		: replyToStory().valid()
		? ItemPreview::Icon::ReplyToStory
		: ItemPreview::Icon::None;
	const auto fromForwarded = [&]() -> std::optional<QString> {
		if (const auto forwarded = Get<HistoryMessageForwarded>()) {
			return forwarded->originalSender
				? fromSender(forwarded->originalSender)
				: forwarded->hiddenSenderInfo->name;
		}
		return {};
	};
	const auto sender = [&]() -> std::optional<QString> {
		if (options.hideSender || isPost() || isEmpty()) {
			return {};
		} else if (const auto sponsored = Get<HistoryMessageSponsored>()) {
			return sponsored->sender->name;
		} else if (!_history->peer->isUser()) {
			if (const auto from = displayFrom()) {
				return fromSender(from);
			}
			return fromForwarded();
		} else if (_history->peer->isSelf()) {
			return fromForwarded();
		}
		return {};
	}();
	if (!sender) {
		return result;
	}
	const auto topic = options.ignoreTopic ? nullptr : this->topic();
	return Dialogs::Ui::PreviewWithSender(
		std::move(result),
		*sender,
		topic ? topic->titleWithIcon() : TextWithEntities());
}

TextWithEntities HistoryItem::inReplyText() const {
	if (!isService()) {
		return toPreview({
			.hideSender = true,
			.generateImages = false,
			.translated = true,
		}).text;
	}
	auto result = notificationText();
	const auto &name = author()->name();
	TextUtilities::Trim(result);
	if (result.text.startsWith(name)) {
		result = Ui::Text::Mid(result, name.size());
		TextUtilities::Trim(result);
	}
	return Ui::Text::Wrapped(result, EntityType::PlainLink);
}

const std::vector<ClickHandlerPtr> &HistoryItem::customTextLinks() const {
	static const auto kEmpty = std::vector<ClickHandlerPtr>();
	const auto service = Get<HistoryServiceData>();
	return service ? service->textLinks : kEmpty;
}

void HistoryItem::createComponents(CreateConfig &&config) {
	uint64 mask = 0;
	if (config.replyTo || config.replyToStory) {
		mask |= HistoryMessageReply::Bit();
	}
	if (config.viaBotId) {
		mask |= HistoryMessageVia::Bit();
	}
	if (config.viewsCount >= 0 || !config.replies.isNull) {
		mask |= HistoryMessageViews::Bit();
	}
	if (!config.author.isEmpty()) {
		mask |= HistoryMessageSigned::Bit();
	} else if (_history->peer->isMegagroup() // Discussion posts signatures.
		&& config.savedFromPeer
		&& !config.authorOriginal.isEmpty()) {
		const auto savedFrom = _history->owner().peerLoaded(
			config.savedFromPeer);
		if (savedFrom && savedFrom->isChannel()) {
			mask |= HistoryMessageSigned::Bit();
		}
	} else if ((_history->peer->isSelf() || _history->peer->isRepliesChat())
		&& !config.authorOriginal.isEmpty()) {
		mask |= HistoryMessageSigned::Bit();
	}
	if (config.editDate != TimeId(0)) {
		mask |= HistoryMessageEdited::Bit();
	}
	if (config.originalDate != 0) {
		mask |= HistoryMessageForwarded::Bit();
	}
	if (!config.markup.isTrivial()) {
		mask |= HistoryMessageReplyMarkup::Bit();
	} else if (config.inlineMarkup) {
		mask |= HistoryMessageReplyMarkup::Bit();
	}

	UpdateComponents(mask);

	if (const auto reply = Get<HistoryMessageReply>()) {
		reply->replyToPeerId = config.replyToPeer;
		reply->replyToMsgId = config.replyTo;
		reply->replyToMsgTop = isScheduled() ? 0 : config.replyToTop;
		reply->replyToStoryId = config.replyToStory;
		reply->storyReply = (config.replyToStory != 0);
		reply->topicPost = config.replyIsTopicPost;
		if (!reply->updateData(this)) {
			if (reply->replyToMsgId) {
				RequestDependentMessageItem(
					this,
					reply->replyToPeerId,
					reply->replyToMsgId);
			} else if (reply->replyToStoryId) {
				RequestDependentMessageStory(
					this,
					reply->replyToPeerId,
					reply->replyToStoryId);
			}
		}
	}
	if (const auto via = Get<HistoryMessageVia>()) {
		via->create(&_history->owner(), config.viaBotId);
	}
	if (const auto views = Get<HistoryMessageViews>()) {
		changeViewsCount(config.viewsCount);
		if (config.replies.isNull
			&& isSending()
			&& config.markup.isNull()) {
			if (const auto broadcast = _history->peer->asBroadcast()) {
				if (const auto linked = broadcast->linkedChat()) {
					config.replies.isNull = false;
					config.replies.channelId = peerToChannel(linked->id);
				}
			}
		}
		setForwardsCount(config.forwardsCount);
		setReplies(std::move(config.replies));
	}
	if (const auto edited = Get<HistoryMessageEdited>()) {
		edited->date = config.editDate;
	}
	if (const auto msgsigned = Get<HistoryMessageSigned>()) {
		msgsigned->author = config.author.isEmpty()
			? config.authorOriginal
			: config.author;
		msgsigned->isAnonymousRank = !isDiscussionPost()
			&& author()->isMegagroup();
	}
	setupForwardedComponent(config);
	if (const auto markup = Get<HistoryMessageReplyMarkup>()) {
		if (!config.markup.isTrivial()) {
			markup->updateData(std::move(config.markup));
		} else if (config.inlineMarkup) {
			markup->createForwarded(*config.inlineMarkup);
		}
		if (markup->data.flags & ReplyMarkupFlag::HasSwitchInlineButton) {
			_flags |= MessageFlag::HasSwitchInlineButton;
		}
	} else if (!config.markup.isNull()) {
		_flags |= MessageFlag::HasReplyMarkup;
	} else {
		_flags &= ~MessageFlag::HasReplyMarkup;
	}
}

bool HistoryItem::checkRepliesPts(
		const HistoryMessageRepliesData &data) const {
	const auto channel = _history->peer->asChannel();
	const auto pts = channel
		? channel->pts()
		: _history->session().updates().pts();
	return (data.pts >= pts);
}

void HistoryItem::setupForwardedComponent(const CreateConfig &config) {
	const auto forwarded = Get<HistoryMessageForwarded>();
	if (!forwarded) {
		return;
	}
	forwarded->originalDate = config.originalDate;
	const auto originalSender = config.senderOriginal
		? config.senderOriginal
		: !config.senderNameOriginal.isEmpty()
		? PeerId()
		: from()->id;
	forwarded->originalSender = originalSender
		? _history->owner().peer(originalSender).get()
		: nullptr;
	if (!forwarded->originalSender) {
		forwarded->hiddenSenderInfo = std::make_unique<HiddenSenderInfo>(
			config.senderNameOriginal,
			config.imported);
	}
	forwarded->originalId = config.originalId;
	forwarded->originalAuthor = config.authorOriginal;
	forwarded->psaType = config.forwardPsaType;
	forwarded->savedFromPeer = _history->owner().peerLoaded(
		config.savedFromPeer);
	forwarded->savedFromMsgId = config.savedFromMsgId;
	forwarded->imported = config.imported;
}

bool HistoryItem::generateLocalEntitiesByReply() const {
	using namespace HistoryView;
	if (!_media) {
		return true;
	} else if (const auto document = _media->document()) {
		return !DurationForTimestampLinks(document);
	} else if (const auto webpage = _media->webpage()) {
		return (webpage->type != WebPageType::Video)
			&& !DurationForTimestampLinks(webpage);
	}
	return true;
}

TextWithEntities HistoryItem::withLocalEntities(
		const TextWithEntities &textWithEntities) const {
	using namespace HistoryView;
	if (!generateLocalEntitiesByReply()) {
		if (!_media) {
		} else if (const auto document = _media->document()) {
			if (const auto duration = DurationForTimestampLinks(document)) {
				return AddTimestampLinks(
					textWithEntities,
					duration,
					TimestampLinkBase(document, fullId()));
			}
		} else if (const auto webpage = _media->webpage()) {
			if (const auto duration = DurationForTimestampLinks(webpage)) {
				return AddTimestampLinks(
					textWithEntities,
					duration,
					TimestampLinkBase(webpage, fullId()));
			}
		}
		return textWithEntities;
	}
	if (const auto reply = Get<HistoryMessageReply>()) {
		const auto document = reply->replyToDocumentId
			? _history->owner().document(reply->replyToDocumentId).get()
			: nullptr;
		const auto webpage = reply->replyToWebPageId
			? _history->owner().webpage(reply->replyToWebPageId).get()
			: nullptr;
		if (document) {
			if (const auto duration = DurationForTimestampLinks(document)) {
				const auto context = reply->replyToMsg->fullId();
				return AddTimestampLinks(
					textWithEntities,
					duration,
					TimestampLinkBase(document, context));
			}
		} else if (webpage) {
			if (const auto duration = DurationForTimestampLinks(webpage)) {
				const auto context = reply->replyToMsg->fullId();
				return AddTimestampLinks(
					textWithEntities,
					duration,
					TimestampLinkBase(webpage, context));
			}
		}
	}
	return textWithEntities;
}

void HistoryItem::setSponsoredFrom(const Data::SponsoredFrom &from) {
	AddComponents(HistoryMessageSponsored::Bit());
	const auto sponsored = Get<HistoryMessageSponsored>();
	sponsored->sender = std::make_unique<HiddenSenderInfo>(
		from.title,
		false);
	sponsored->recommended = from.isRecommended;
	sponsored->isForceUserpicDisplay = from.isForceUserpicDisplay;
	if (from.userpic.location.valid()) {
		sponsored->sender->customUserpic.set(
			&_history->session(),
			from.userpic);
	}

	sponsored->externalLink = from.externalLink;
	using Type = HistoryMessageSponsored::Type;
	sponsored->type = (!from.externalLink.isEmpty())
		? Type::ExternalLink
		: from.isExactPost
		? Type::Post
		: from.isBot
		? Type::Bot
		: from.isBroadcast
		? Type::Broadcast
		: (from.peer && from.peer->isUser())
		? Type::User
		: Type::Group;
}

void HistoryItem::createComponentsHelper(
		MessageFlags flags,
		FullReplyTo replyTo,
		UserId viaBotId,
		const QString &postAuthor,
		HistoryMessageMarkupData &&markup) {
	auto config = CreateConfig();
	config.viaBotId = viaBotId;
	if (flags & MessageFlag::HasReplyInfo) {
		config.replyTo = replyTo.msgId;
		config.replyToStory = replyTo.storyId.story;
		config.replyToPeer = replyTo.storyId ? replyTo.storyId.peer : 0;
		const auto to = LookupReplyTo(_history, replyTo.msgId);
		const auto replyToTop = LookupReplyToTop(to);
		config.replyToTop = replyToTop ? replyToTop : replyTo.msgId;
		const auto forum = _history->asForum();
		config.replyIsTopicPost = LookupReplyIsTopicPost(to)
			|| (to && to->Has<HistoryServiceTopicInfo>())
			|| (forum && forum->creating(config.replyToTop));
	}
	config.markup = std::move(markup);
	if (flags & MessageFlag::HasPostAuthor) config.author = postAuthor;
	if (flags & MessageFlag::HasViews) config.viewsCount = 1;

	createComponents(std::move(config));
}

void HistoryItem::setReactions(const MTPMessageReactions *reactions) {
	Expects(!_reactions);

	if (changeReactions(reactions) && _reactions->hasUnread()) {
		_flags |= MessageFlag::HasUnreadReaction;
	}
}

void HistoryItem::updateReactions(const MTPMessageReactions *reactions) {
	const auto wasRecentUsers = LookupRecentUnreadReactedUsers(this);
	const auto hadUnread = hasUnreadReaction();
	const auto changed = changeReactions(reactions);
	if (!changed) {
		return;
	}
	const auto hasUnread = _reactions && _reactions->hasUnread();
	if (hasUnread && !hadUnread) {
		_flags |= MessageFlag::HasUnreadReaction;

		addToUnreadThings(HistoryUnreadThings::AddType::New);
	} else if (!hasUnread && hadUnread) {
		markReactionsRead();
	}
	CheckReactionNotificationSchedule(this, wasRecentUsers);
	_history->owner().notifyItemDataChange(this);
}

bool HistoryItem::changeReactions(const MTPMessageReactions *reactions) {
	if (reactions || _reactionsLastRefreshed) {
		_reactionsLastRefreshed = crl::now();
	}
	if (!reactions) {
		_flags &= ~MessageFlag::CanViewReactions;
		return (base::take(_reactions) != nullptr);
	}
	return reactions->match([&](const MTPDmessageReactions &data) {
		if (data.is_can_see_list()) {
			_flags |= MessageFlag::CanViewReactions;
		} else {
			_flags &= ~MessageFlag::CanViewReactions;
		}
		if (data.vresults().v.isEmpty()) {
			return (base::take(_reactions) != nullptr);
		} else if (!_reactions) {
			_reactions = std::make_unique<Data::MessageReactions>(this);
		}
		const auto min = data.is_min();
		const auto &list = data.vresults().v;
		const auto &recent = data.vrecent_reactions().value_or_empty();
		if (min && hasUnreadReaction()) {
			// We can't update reactions from min if we have unread.
			if (_reactions->checkIfChanged(list, recent, min)) {
				updateReactionsUnknown();
			}
			return false;
		}
		return _reactions->change(list, recent, min);
	});
}

void HistoryItem::applyTTL(const MTPDmessage &data) {
	if (const auto period = data.vttl_period()) {
		if (period->v > 0) {
			applyTTL(data.vdate().v + period->v);
		}
	}
}

void HistoryItem::applyTTL(const MTPDmessageService &data) {
	if (const auto period = data.vttl_period()) {
		if (period->v > 0) {
			applyTTL(data.vdate().v + period->v);
		}
	}
}

void HistoryItem::createComponents(const MTPDmessage &data) {
	auto config = CreateConfig();
	if (const auto forwarded = data.vfwd_from()) {
		forwarded->match([&](const MTPDmessageFwdHeader &data) {
			FillForwardedInfo(config, data);
		});
	}
	if (const auto reply = data.vreply_to()) {
		reply->match([&](const MTPDmessageReplyHeader &data) {
			if (const auto peer = data.vreply_to_peer_id()) {
				config.replyToPeer = peerFromMTP(*peer);
				if (config.replyToPeer == _history->peer->id) {
					config.replyToPeer = 0;
				}
			}
			const auto id = data.vreply_to_msg_id().v;
			config.replyTo = data.is_reply_to_scheduled()
				? _history->owner().scheduledMessages().localMessageId(id)
				: id;
			config.replyToTop = data.vreply_to_top_id().value_or(id);
			config.replyIsTopicPost = data.is_forum_topic();
		}, [&](const MTPDmessageReplyStoryHeader &data) {
			config.replyToPeer = peerFromUser(data.vuser_id());
			config.replyToStory = data.vstory_id().v;
		});
	}
	config.viaBotId = data.vvia_bot_id().value_or_empty();
	config.viewsCount = data.vviews().value_or(-1);
	config.forwardsCount = data.vforwards().value_or(-1);
	config.replies = isScheduled()
		? HistoryMessageRepliesData()
		: HistoryMessageRepliesData(data.vreplies());
	config.markup = HistoryMessageMarkupData(data.vreply_markup());
	config.editDate = data.vedit_date().value_or_empty();
	config.author = qs(data.vpost_author().value_or_empty());
	createComponents(std::move(config));
}

void HistoryItem::refreshMedia(const MTPMessageMedia *media) {
	const auto was = (_media != nullptr);
	if (const auto invoice = was ? _media->invoice() : nullptr) {
		if (invoice->extendedMedia) {
			return;
		}
	}
	_media = nullptr;
	if (media) {
		setMedia(*media);
	}
	if (was || _media) {
		if (const auto views = Get<HistoryMessageViews>()) {
			refreshRepliesText(views);
		}
	}
}

void HistoryItem::refreshSentMedia(const MTPMessageMedia *media) {
	const auto wasGrouped = history()->owner().groups().isGrouped(this);
	refreshMedia(media);
	if (wasGrouped) {
		history()->owner().groups().refreshMessage(this);
	} else {
		history()->owner().requestItemViewRefresh(this);
	}
}

void HistoryItem::createServiceFromMtp(const MTPDmessage &message) {
	AddComponents(HistoryServiceData::Bit());

	const auto unread = message.is_media_unread();
	const auto media = message.vmedia();
	Assert(media != nullptr);

	media->match([&](const MTPDmessageMediaPhoto &data) {
		if (unread) {
			const auto ttl = data.vttl_seconds();
			Assert(ttl != nullptr);

			setSelfDestruct(HistoryServiceSelfDestruct::Type::Photo, *ttl);
			if (out()) {
				setServiceText({
					tr::lng_ttl_photo_sent(tr::now, Ui::Text::WithEntities)
				});
			} else {
				auto result = PreparedServiceText();
				result.links.push_back(fromLink());
				result.text = tr::lng_ttl_photo_received(
					tr::now,
					lt_from,
					fromLinkText(), // Link 1.
					Ui::Text::WithEntities);
				setServiceText(std::move(result));
			}
		} else {
			setServiceText({
				tr::lng_ttl_photo_expired(tr::now, Ui::Text::WithEntities)
			});
		}
	}, [&](const MTPDmessageMediaDocument &data) {
		if (unread) {
			const auto ttl = data.vttl_seconds();
			Assert(ttl != nullptr);

			setSelfDestruct(HistoryServiceSelfDestruct::Type::Video, *ttl);
			if (out()) {
				setServiceText({
					tr::lng_ttl_video_sent(tr::now, Ui::Text::WithEntities)
				});
			} else {
				auto result = PreparedServiceText();
				result.links.push_back(fromLink());
				result.text = tr::lng_ttl_video_received(
					tr::now,
					lt_from,
					fromLinkText(), // Link 1.
					Ui::Text::WithEntities);
				setServiceText(std::move(result));
			}
		} else {
			setServiceText({
				tr::lng_ttl_video_expired(tr::now, Ui::Text::WithEntities)
			});
		}
	}, [&](const MTPDmessageMediaStory &data) {
		setServiceText(prepareStoryMentionText());
	}, [](const auto &) {
		Unexpected("Media type in HistoryItem::createServiceFromMtp()");
	});

	if (const auto reactions = message.vreactions()) {
		updateReactions(reactions);
	}
}

void HistoryItem::createServiceFromMtp(const MTPDmessageService &message) {
	AddComponents(HistoryServiceData::Bit());

	const auto &action = message.vaction();
	const auto type = action.type();
	if (type == mtpc_messageActionPinMessage) {
		UpdateComponents(HistoryServicePinned::Bit());
	} else if (type == mtpc_messageActionTopicCreate
		|| type == mtpc_messageActionTopicEdit) {
		UpdateComponents(HistoryServiceTopicInfo::Bit());
		const auto info = Get<HistoryServiceTopicInfo>();
		info->topicPost = true;
		if (type == mtpc_messageActionTopicEdit) {
			const auto &data = action.c_messageActionTopicEdit();
			if (const auto title = data.vtitle()) {
				info->title = qs(*title);
				info->renamed = true;
			}
			if (const auto icon = data.vicon_emoji_id()) {
				info->iconId = icon->v;
				info->reiconed = true;
			}
			if (const auto closed = data.vclosed()) {
				info->closed = mtpIsTrue(*closed);
				info->reopened = !info->closed;
			}
			if (const auto hidden = data.vhidden()) {
				info->hidden = mtpIsTrue(*hidden);
				info->unhidden = !info->hidden;
			}
		} else {
			const auto &data = action.c_messageActionTopicCreate();
			info->title = qs(data.vtitle());
			info->iconId = data.vicon_emoji_id().value_or_empty();
		}
	} else if (type == mtpc_messageActionSetChatTheme) {
		setupChatThemeChange();
	} else if (type == mtpc_messageActionSetMessagesTTL) {
		setupTTLChange();
	} else if (type == mtpc_messageActionGameScore) {
		const auto &data = action.c_messageActionGameScore();
		UpdateComponents(HistoryServiceGameScore::Bit());
		Get<HistoryServiceGameScore>()->score = data.vscore().v;
	} else if (type == mtpc_messageActionPaymentSent) {
		const auto &data = action.c_messageActionPaymentSent();
		UpdateComponents(HistoryServicePayment::Bit());
		const auto amount = data.vtotal_amount().v;
		const auto currency = qs(data.vcurrency());
		const auto payment = Get<HistoryServicePayment>();
		const auto id = fullId();
		const auto owner = &_history->owner();
		payment->slug = data.vinvoice_slug().value_or_empty();
		payment->recurringInit = data.is_recurring_init();
		payment->recurringUsed = data.is_recurring_used();
		payment->amount = Ui::FillAmountAndCurrency(amount, currency);
		payment->invoiceLink = std::make_shared<LambdaClickHandler>([=](
				ClickContext context) {
			using namespace Payments;
			const auto my = context.other.value<ClickHandlerContext>();
			const auto weak = my.sessionWindow;
			if (const auto item = owner->message(id)) {
				CheckoutProcess::Start(
					item,
					Mode::Receipt,
					crl::guard(weak, [=](auto) { weak->window().activate(); }));
			}
		});
	} else if (type == mtpc_messageActionGroupCall
		|| type == mtpc_messageActionGroupCallScheduled) {
		const auto started = (type == mtpc_messageActionGroupCall);
		const auto &callData = started
			? action.c_messageActionGroupCall().vcall()
			: action.c_messageActionGroupCallScheduled().vcall();
		const auto duration = started
			? action.c_messageActionGroupCall().vduration()
			: tl::conditional<MTPint>();
		if (duration) {
			RemoveComponents(HistoryServiceOngoingCall::Bit());
		} else {
			UpdateComponents(HistoryServiceOngoingCall::Bit());
			const auto call = Get<HistoryServiceOngoingCall>();
			call->id = CallIdFromInput(callData);
			call->link = GroupCallClickHandler(_history->peer, call->id);
		}
	} else if (type == mtpc_messageActionInviteToGroupCall) {
		const auto &data = action.c_messageActionInviteToGroupCall();
		const auto id = CallIdFromInput(data.vcall());
		const auto peer = _history->peer;
		const auto has = PeerHasThisCall(peer, id);
		auto hasLink = !has.has_value()
			? PeerHasThisCallValue(peer, id)
			: (*has)
			? PeerHasThisCallValue(
				peer,
				id) | rpl::skip(1) | rpl::type_erased()
			: rpl::producer<bool>();
		if (!hasLink) {
			RemoveComponents(HistoryServiceOngoingCall::Bit());
		} else {
			UpdateComponents(HistoryServiceOngoingCall::Bit());
			const auto call = Get<HistoryServiceOngoingCall>();
			call->id = id;
			call->lifetime.destroy();

			const auto users = data.vusers().v;
			std::move(hasLink) | rpl::start_with_next([=](bool has) {
				updateServiceText(
					prepareInvitedToCallText(
						ParseInvitedToCallUsers(this, users),
						has ? id : 0));
				if (!has) {
					RemoveComponents(HistoryServiceOngoingCall::Bit());
				}
			}, call->lifetime);
		}
	} else if (type == mtpc_messageActionSetSameChatWallPaper) {
		UpdateComponents(HistoryServiceSameBackground::Bit());
	}
	if (const auto replyTo = message.vreply_to()) {
		replyTo->match([&](const MTPDmessageReplyHeader &data) {
			const auto peerId = data.vreply_to_peer_id()
				? peerFromMTP(*data.vreply_to_peer_id())
				: _history->peer->id;
			if (const auto dependent = GetServiceDependentData()) {
				dependent->peerId = (peerId != _history->peer->id)
					? peerId
					: 0;
				const auto id = data.vreply_to_msg_id().v;
				dependent->msgId = id;
				dependent->topId = data.vreply_to_top_id().value_or(id);
				dependent->topicPost = data.is_forum_topic()
					|| Has<HistoryServiceTopicInfo>();
				if (!updateServiceDependent()) {
					RequestDependentMessageItem(
						this,
						(dependent->peerId
							? dependent->peerId
							: _history->peer->id),
						dependent->msgId);
				}
			}
		}, [](const MTPDmessageReplyStoryHeader &data) {
		});
	}
	setServiceMessageByAction(action);
}

void HistoryItem::setMedia(const MTPMessageMedia &media) {
	_media = CreateMedia(this, media);
	checkStoryForwardInfo();
	checkBuyButton();
}

void HistoryItem::checkStoryForwardInfo() {
	if (const auto storyId = _media ? _media->storyId() : FullStoryId()) {
		const auto adding = !Has<HistoryMessageForwarded>();
		if (adding) {
			AddComponents(HistoryMessageForwarded::Bit());
		}
		const auto forwarded = Get<HistoryMessageForwarded>();
		if (forwarded->story || adding) {
			const auto peer = history()->owner().peer(storyId.peer);
			forwarded->story = true;
			forwarded->originalSender = peer;
		}
	} else if (const auto forwarded = Get<HistoryMessageForwarded>()) {
		if (forwarded->story) {
			RemoveComponents(HistoryMessageForwarded::Bit());
		}
	}
}

void HistoryItem::applyServiceDateEdition(const MTPDmessageService &data) {
	const auto date = data.vdate().v;
	if (_date == date) {
		return;
	}
	_date = date;
}

void HistoryItem::setServiceMessageByAction(const MTPmessageAction &action) {
	auto prepareChatAddUserText = [this](const MTPDmessageActionChatAddUser &action) {
		auto result = PreparedServiceText();
		auto &users = action.vusers().v;
		if (users.size() == 1) {
			auto u = _history->owner().user(users[0].v);
			if (u == _from) {
				result.links.push_back(fromLink());
				result.text = tr::lng_action_user_joined(
					tr::now,
					lt_from,
					fromLinkText(), // Link 1.
					Ui::Text::WithEntities);
			} else {
				result.links.push_back(fromLink());
				result.links.push_back(u->createOpenLink());
				result.text = tr::lng_action_add_user(
					tr::now,
					lt_from,
					fromLinkText(), // Link 1.
					lt_user,
					Ui::Text::Link(u->name(), 2), // Link 2.
					Ui::Text::WithEntities);
			}
		} else if (users.isEmpty()) {
			result.links.push_back(fromLink());
			result.text = tr::lng_action_add_user(
				tr::now,
				lt_from,
				fromLinkText(), // Link 1.
				lt_user,
				{ .text = u"somebody"_q },
				Ui::Text::WithEntities);
		} else {
			result.links.push_back(fromLink());
			for (auto i = 0, l = int(users.size()); i != l; ++i) {
				auto user = _history->owner().user(users[i].v);
				result.links.push_back(user->createOpenLink());

				auto linkText = Ui::Text::Link(user->name(), 2 + i);
				if (i == 0) {
					result.text = linkText;
				} else if (i + 1 == l) {
					result.text = tr::lng_action_add_users_and_last(
						tr::now,
						lt_accumulated,
						result.text,
						lt_user,
						linkText,
						Ui::Text::WithEntities);
				} else {
					result.text = tr::lng_action_add_users_and_one(
						tr::now,
						lt_accumulated,
						result.text,
						lt_user,
						linkText,
						Ui::Text::WithEntities);
				}
			}
			result.text = tr::lng_action_add_users_many(
				tr::now,
				lt_from,
				fromLinkText(), // Link 1.
				lt_users,
				result.text,
				Ui::Text::WithEntities);
		}
		return result;
	};

	auto prepareChatJoinedByLink = [this](const MTPDmessageActionChatJoinedByLink &action) {
		auto result = PreparedServiceText();
		result.links.push_back(fromLink());
		result.text = tr::lng_action_user_joined_by_link(
			tr::now,
			lt_from,
			fromLinkText(), // Link 1.
			Ui::Text::WithEntities);
		return result;
	};

	auto prepareChatCreate = [this](const MTPDmessageActionChatCreate &action) {
		auto result = PreparedServiceText();
		result.links.push_back(fromLink());
		result.text = tr::lng_action_created_chat(
			tr::now,
			lt_from,
			fromLinkText(), // Link 1.
			lt_title,
			{ .text = qs(action.vtitle()) },
			Ui::Text::WithEntities);
		return result;
	};

	auto prepareChannelCreate = [this](const MTPDmessageActionChannelCreate &action) {
		auto result = PreparedServiceText();
		if (isPost()) {
			result.text = tr::lng_action_created_channel(
				tr::now,
				Ui::Text::WithEntities);
		} else {
			result.links.push_back(fromLink());
			result.text = tr::lng_action_created_chat(
				tr::now,
				lt_from,
				fromLinkText(), // Link 1.
				lt_title,
				{ .text = qs(action.vtitle()) },
				Ui::Text::WithEntities);
		}
		return result;
	};

	auto prepareChatDeletePhoto = [this] {
		auto result = PreparedServiceText();
		if (isPost()) {
			result.text = tr::lng_action_removed_photo_channel(
				tr::now,
				Ui::Text::WithEntities);
		} else {
			result.links.push_back(fromLink());
			result.text = tr::lng_action_removed_photo(
				tr::now,
				lt_from,
				fromLinkText(), // Link 1.
				Ui::Text::WithEntities);
		}
		return result;
	};

	auto prepareChatDeleteUser = [this](const MTPDmessageActionChatDeleteUser &action) {
		auto result = PreparedServiceText();
		if (peerFromUser(action.vuser_id()) == _from->id) {
			result.links.push_back(fromLink());
			result.text = tr::lng_action_user_left(
				tr::now,
				lt_from,
				fromLinkText(), // Link 1.
				Ui::Text::WithEntities);
		} else {
			auto user = _history->owner().user(action.vuser_id().v);
			result.links.push_back(fromLink());
			result.links.push_back(user->createOpenLink());
			result.text = tr::lng_action_kick_user(
				tr::now,
				lt_from,
				fromLinkText(), // Link 1.
				lt_user,
				Ui::Text::Link(user->name(), 2),  // Link 2.
				Ui::Text::WithEntities);
		}
		return result;
	};

	auto prepareChatEditPhoto = [this](const MTPDmessageActionChatEditPhoto &action) {
		auto result = PreparedServiceText();
		if (isPost()) {
			result.text = tr::lng_action_changed_photo_channel(
				tr::now,
				Ui::Text::WithEntities);
		} else {
			result.links.push_back(fromLink());
			result.text = tr::lng_action_changed_photo(
				tr::now,
				lt_from,
				fromLinkText(), // Link 1.
				Ui::Text::WithEntities);
		}
		return result;
	};

	auto prepareChatEditTitle = [this](const MTPDmessageActionChatEditTitle &action) {
		auto result = PreparedServiceText();
		if (isPost()) {
			result.text = tr::lng_action_changed_title_channel(
				tr::now,
				lt_title,
				{ .text = (qs(action.vtitle())) },
				Ui::Text::WithEntities);
		} else {
			result.links.push_back(fromLink());
			result.text = tr::lng_action_changed_title(
				tr::now,
				lt_from,
				fromLinkText(), // Link 1.
				lt_title,
				{ .text = qs(action.vtitle()) },
				Ui::Text::WithEntities);
		}
		return result;
	};

	auto prepareScreenshotTaken = [this] {
		auto result = PreparedServiceText();
		if (out()) {
			result.text = tr::lng_action_you_took_screenshot(
				tr::now,
				Ui::Text::WithEntities);
		} else {
			result.links.push_back(fromLink());
			result.text = tr::lng_action_took_screenshot(
				tr::now,
				lt_from,
				fromLinkText(), // Link 1.
				Ui::Text::WithEntities);
		}
		return result;
	};

	auto prepareCustomAction = [&](const MTPDmessageActionCustomAction &action) {
		auto result = PreparedServiceText();
		result.text = { .text = qs(action.vmessage()) };
		return result;
	};

	auto prepareBotAllowed = [&](const MTPDmessageActionBotAllowed &action) {
		auto result = PreparedServiceText();
		if (action.is_attach_menu()) {
			result.text = {
				tr::lng_action_attach_menu_bot_allowed(tr::now)
			};
		} else if (action.is_from_request()) {
			result.text = {
				tr::lng_action_webapp_bot_allowed(tr::now)
			};
		} else if (const auto app = action.vapp()) {
			const auto bot = history()->peer->asUser();
			const auto botId = bot ? bot->id : PeerId();
			const auto info = history()->owner().processBotApp(botId, *app);
			const auto url = (bot && info)
				? history()->session().createInternalLinkFull(
					bot->username() + '/' + info->shortName)
				: QString();
			result.text = tr::lng_action_bot_allowed_from_app(
				tr::now,
				lt_app,
				(url.isEmpty()
					? TextWithEntities{ u"App"_q }
					: Ui::Text::Link(info->title, url)),
				Ui::Text::WithEntities);
		} else {
			const auto domain = qs(action.vdomain().value_or_empty());
			result.text = tr::lng_action_bot_allowed_from_domain(
				tr::now,
				lt_domain,
				Ui::Text::Link(domain, u"http://"_q + domain),
				Ui::Text::WithEntities);
		}
		return result;
	};

	auto prepareSecureValuesSent = [&](const MTPDmessageActionSecureValuesSent &action) {
		auto result = PreparedServiceText();
		auto documents = QStringList();
		for (const auto &type : action.vtypes().v) {
			documents.push_back([&] {
				switch (type.type()) {
				case mtpc_secureValueTypePersonalDetails:
					return tr::lng_action_secure_personal_details(tr::now);
				case mtpc_secureValueTypePassport:
				case mtpc_secureValueTypeDriverLicense:
				case mtpc_secureValueTypeIdentityCard:
				case mtpc_secureValueTypeInternalPassport:
					return tr::lng_action_secure_proof_of_identity(tr::now);
				case mtpc_secureValueTypeAddress:
					return tr::lng_action_secure_address(tr::now);
				case mtpc_secureValueTypeUtilityBill:
				case mtpc_secureValueTypeBankStatement:
				case mtpc_secureValueTypeRentalAgreement:
				case mtpc_secureValueTypePassportRegistration:
				case mtpc_secureValueTypeTemporaryRegistration:
					return tr::lng_action_secure_proof_of_address(tr::now);
				case mtpc_secureValueTypePhone:
					return tr::lng_action_secure_phone(tr::now);
				case mtpc_secureValueTypeEmail:
					return tr::lng_action_secure_email(tr::now);
				}
				Unexpected("Type in prepareSecureValuesSent.");
			}());
		};
		result.links.push_back(_history->peer->createOpenLink());
		result.text = tr::lng_action_secure_values_sent(
			tr::now,
			lt_user,
			Ui::Text::Link(_history->peer->name(), QString()), // Link 1.
			lt_documents,
			{ .text = documents.join(", ") },
			Ui::Text::WithEntities);
		return result;
	};

	auto prepareContactSignUp = [this] {
		auto result = PreparedServiceText();
		result.links.push_back(fromLink());
		result.text = tr::lng_action_user_registered(
			tr::now,
			lt_from,
			fromLinkText(), // Link 1.
			Ui::Text::WithEntities);
		return result;
	};

	auto prepareProximityReached = [this](const MTPDmessageActionGeoProximityReached &action) {
		auto result = PreparedServiceText();
		const auto fromId = peerFromMTP(action.vfrom_id());
		const auto fromPeer = _history->owner().peer(fromId);
		const auto toId = peerFromMTP(action.vto_id());
		const auto toPeer = _history->owner().peer(toId);
		const auto selfId = _from->session().userPeerId();
		const auto distanceMeters = action.vdistance().v;
		const auto distance = [&] {
			if (distanceMeters >= 1000) {
				const auto km = (10 * (distanceMeters / 10)) / 1000.;
				return tr::lng_action_proximity_distance_km(
					tr::now,
					lt_count,
					km);
			} else {
				return tr::lng_action_proximity_distance_m(
					tr::now,
					lt_count,
					distanceMeters);
			}
		}();
		result.text = [&] {
			if (fromId == selfId) {
				result.links.push_back(toPeer->createOpenLink());
				return tr::lng_action_you_proximity_reached(
					tr::now,
					lt_distance,
					{ .text = distance },
					lt_user,
					Ui::Text::Link(toPeer->name(), QString()), // Link 1.
					Ui::Text::WithEntities);
			} else if (toId == selfId) {
				result.links.push_back(fromPeer->createOpenLink());
				return tr::lng_action_proximity_reached_you(
					tr::now,
					lt_from,
					Ui::Text::Link(fromPeer->name(), QString()), // Link 1.
					lt_distance,
					{ .text = distance },
					Ui::Text::WithEntities);
			} else {
				result.links.push_back(fromPeer->createOpenLink());
				result.links.push_back(toPeer->createOpenLink());
				return tr::lng_action_proximity_reached(
					tr::now,
					lt_from,
					Ui::Text::Link(fromPeer->name(), 1), // Link 1.
					lt_distance,
					{ .text = distance },
					lt_user,
					Ui::Text::Link(toPeer->name(), 2), // Link 2.
					Ui::Text::WithEntities);
			}
		}();
		return result;
	};

	auto prepareGroupCall = [this](const MTPDmessageActionGroupCall &action) {
		auto result = PreparedServiceText();
		if (const auto duration = action.vduration()) {
			const auto seconds = duration->v;
			const auto days = seconds / 86400;
			const auto hours = seconds / 3600;
			const auto minutes = seconds / 60;
			auto text = (days > 1)
				? tr::lng_days(tr::now, lt_count, days)
				: (hours > 1)
				? tr::lng_hours(tr::now, lt_count, hours)
				: (minutes > 1)
				? tr::lng_minutes(tr::now, lt_count, minutes)
				: tr::lng_seconds(tr::now, lt_count, seconds);
			if (_history->peer->isBroadcast()) {
				result.text = tr::lng_action_group_call_finished(
					tr::now,
					lt_duration,
					{ .text = text },
					Ui::Text::WithEntities);
			} else {
				result.links.push_back(fromLink());
				result.text = tr::lng_action_group_call_finished_group(
					tr::now,
					lt_from,
					fromLinkText(), // Link 1.
					lt_duration,
					{ .text = text },
					Ui::Text::WithEntities);
			}
			return result;
		}
		if (_history->peer->isBroadcast()) {
			result.text = tr::lng_action_group_call_started_channel(
				tr::now,
				Ui::Text::WithEntities);
		} else {
			result.links.push_back(fromLink());
			result.text = tr::lng_action_group_call_started_group(
				tr::now,
				lt_from,
				fromLinkText(), // Link 1.
				Ui::Text::WithEntities);
		}
		return result;
	};

	auto prepareInviteToGroupCall = [this](const MTPDmessageActionInviteToGroupCall &action) {
		const auto callId = CallIdFromInput(action.vcall());
		const auto owner = &_history->owner();
		const auto peer = _history->peer;
		for (const auto &id : action.vusers().v) {
			const auto user = owner->user(id.v);
			if (callId) {
				owner->registerInvitedToCallUser(callId, peer, user);
			}
		};
		const auto linkCallId = PeerHasThisCall(peer, callId).value_or(false)
			? callId
			: 0;
		return prepareInvitedToCallText(
			ParseInvitedToCallUsers(this, action.vusers().v),
			linkCallId);
	};

	auto prepareSetMessagesTTL = [this](const MTPDmessageActionSetMessagesTTL &action) {
		auto result = PreparedServiceText();
		const auto period = action.vperiod().v;
		const auto duration = (period == 5)
			? u"5 seconds"_q
			: Ui::FormatTTL(period);
		if (const auto from = action.vauto_setting_from(); from && period) {
			if (const auto peer = _from->owner().peer(peerFromUser(*from))) {
				result.text = (peer->id == peer->session().userPeerId())
					? tr::lng_action_ttl_global_me(
						tr::now,
						lt_duration,
						{ .text = duration },
						Ui::Text::WithEntities)
					: tr::lng_action_ttl_global(
						tr::now,
						lt_from,
						Ui::Text::Link(peer->name(), 1), // Link 1.
						lt_duration,
						{ .text = duration },
						Ui::Text::WithEntities);
				return result;
			}
		}
		if (isPost()) {
			if (!period) {
				result.text = tr::lng_action_ttl_removed_channel(
					tr::now,
					Ui::Text::WithEntities);
			} else {
				result.text = tr::lng_action_ttl_changed_channel(
					tr::now,
					lt_duration,
					{ .text = duration },
					Ui::Text::WithEntities);
			}
		} else if (_from->isSelf()) {
			if (!period) {
				result.text = tr::lng_action_ttl_removed_you(
					tr::now,
					Ui::Text::WithEntities);
			} else {
				result.text = tr::lng_action_ttl_changed_you(
					tr::now,
					lt_duration,
					{ .text = duration },
					Ui::Text::WithEntities);
			}
		} else {
			result.links.push_back(fromLink());
			if (!period) {
				result.text = tr::lng_action_ttl_removed(
					tr::now,
					lt_from,
					fromLinkText(), // Link 1.
					Ui::Text::WithEntities);
			} else {
				result.text = tr::lng_action_ttl_changed(
					tr::now,
					lt_from,
					fromLinkText(), // Link 1.
					lt_duration,
					{ .text = duration },
					Ui::Text::WithEntities);
			}
		}
		return result;
	};

	auto prepareSetChatTheme = [this](const MTPDmessageActionSetChatTheme &action) {
		auto result = PreparedServiceText();
		const auto text = qs(action.vemoticon());
		if (!text.isEmpty()) {
			if (_from->isSelf()) {
				result.text = tr::lng_action_you_theme_changed(
					tr::now,
					lt_emoji,
					{ .text = text },
					Ui::Text::WithEntities);
			} else {
				result.links.push_back(fromLink());
				result.text = tr::lng_action_theme_changed(
					tr::now,
					lt_from,
					fromLinkText(), // Link 1.
					lt_emoji,
					{ .text = text },
					Ui::Text::WithEntities);
			}
		} else {
			if (_from->isSelf()) {
				result.text = tr::lng_action_you_theme_disabled(
					tr::now,
					Ui::Text::WithEntities);
			} else {
				result.links.push_back(fromLink());
				result.text = tr::lng_action_theme_disabled(
					tr::now,
					lt_from,
					fromLinkText(), // Link 1.
					Ui::Text::WithEntities);
			}
		}
		return result;
	};

	auto prepareChatJoinedByRequest = [this](const MTPDmessageActionChatJoinedByRequest &action) {
		auto result = PreparedServiceText();
		result.links.push_back(fromLink());
		result.text = tr::lng_action_user_joined_by_request(
			tr::now,
			lt_from,
			fromLinkText(), // Link 1.
			Ui::Text::WithEntities);
		return result;
	};

	auto prepareWebViewDataSent = [](const MTPDmessageActionWebViewDataSent &action) {
		auto result = PreparedServiceText();
		result.text = tr::lng_action_webview_data_done(
			tr::now,
			lt_text,
			{ .text = qs(action.vtext()) },
			Ui::Text::WithEntities);
		return result;
	};

	auto prepareGiftPremium = [&](
			const MTPDmessageActionGiftPremium &action) {
		auto result = PreparedServiceText();
		const auto isSelf = (_from->id == _from->session().userPeerId());
		const auto peer = isSelf ? _history->peer : _from;
		_history->session().giftBoxStickersPacks().load();
		const auto amount = action.vamount().v;
		const auto currency = qs(action.vcurrency());
		result.links.push_back(peer->createOpenLink());
		result.text = (isSelf
			? tr::lng_action_gift_received_me
			: tr::lng_action_gift_received)(
				tr::now,
				lt_user,
				Ui::Text::Link(peer->name(), 1), // Link 1.
				lt_cost,
				{ Ui::FillAmountAndCurrency(amount, currency) },
				Ui::Text::WithEntities);
		return result;
	};

	auto prepareTopicCreate = [&](const MTPDmessageActionTopicCreate &action) {
		auto result = PreparedServiceText();
		const auto topicUrl = u"internal:url:https://t.me/c/%1/%2"_q
			.arg(peerToChannel(_history->peer->id).bare)
			.arg(id.bare);
		result.text = tr::lng_action_topic_created(
			tr::now,
			lt_topic,
			Ui::Text::Link(
				Data::ForumTopicIconWithTitle(
					id,
					action.vicon_emoji_id().value_or_empty(),
					qs(action.vtitle())),
				topicUrl),
			Ui::Text::WithEntities);
		return result;
	};

	auto prepareTopicEdit = [&](const MTPDmessageActionTopicEdit &action) {
		auto result = PreparedServiceText();
		if (const auto closed = action.vclosed()) {
			result.text = { mtpIsTrue(*closed)
				? tr::lng_action_topic_closed_inside(tr::now)
				: tr::lng_action_topic_reopened_inside(tr::now) };
		} else if (const auto hidden = action.vhidden()) {
			result.text = { mtpIsTrue(*hidden)
				? tr::lng_action_topic_hidden_inside(tr::now)
				: tr::lng_action_topic_unhidden_inside(tr::now) };
		} else if (!action.vtitle()) {
			if (const auto icon = action.vicon_emoji_id()) {
				if (const auto iconId = icon->v) {
					result.links.push_back(fromLink());
					result.text = tr::lng_action_topic_icon_changed(
						tr::now,
						lt_from,
						fromLinkText(), // Link 1.
						lt_link,
						{ tr::lng_action_topic_placeholder(tr::now) },
						lt_emoji,
						Data::SingleCustomEmoji(iconId),
						Ui::Text::WithEntities);
				} else {
					result.links.push_back(fromLink());
					result.text = tr::lng_action_topic_icon_removed(
						tr::now,
						lt_from,
						fromLinkText(), // Link 1.
						lt_link,
						{ tr::lng_action_topic_placeholder(tr::now) },
						Ui::Text::WithEntities);
				}
			}
		} else {
			result.links.push_back(fromLink());
			result.text = tr::lng_action_topic_renamed(
				tr::now,
				lt_from,
				fromLinkText(), // Link 1.
				lt_link,
				{ tr::lng_action_topic_placeholder(tr::now) },
				lt_title,
				Data::ForumTopicIconWithTitle(
					topicRootId(),
					action.vicon_emoji_id().value_or_empty(),
					qs(*action.vtitle())),
				Ui::Text::WithEntities);
		}
		if (result.text.empty()) {
			result.text = { tr::lng_message_empty(tr::now) };
		}
		return result;
	};

	auto prepareSuggestProfilePhoto = [this](const MTPDmessageActionSuggestProfilePhoto &action) {
		auto result = PreparedServiceText{};
		const auto isSelf = (_from->id == _from->session().userPeerId());
		const auto isVideo = action.vphoto().match([&](const MTPDphoto &data) {
			return data.vvideo_sizes().has_value()
				&& !data.vvideo_sizes()->v.isEmpty();
		}, [](const MTPDphotoEmpty &) {
			return false;
		});
		const auto peer = isSelf ? history()->peer : _from;
		const auto user = peer->asUser();
		const auto name = (user && !user->firstName.isEmpty())
			? user->firstName
			: peer->name();
		result.links.push_back(peer->createOpenLink());
		result.text = (isSelf
			? (isVideo
				? tr::lng_action_suggested_video_me
				: tr::lng_action_suggested_photo_me)
			: (isVideo
				? tr::lng_action_suggested_video
				: tr::lng_action_suggested_photo))(
				tr::now,
				lt_user,
				Ui::Text::Link(name, 1), // Link 1.
				Ui::Text::WithEntities);
		return result;
	};

	auto prepareRequestedPeer = [&](
			const MTPDmessageActionRequestedPeer &action) {
		const auto peerId = peerFromMTP(action.vpeer());
		const auto peer = history()->owner().peer(peerId);
		auto result = PreparedServiceText{};
		result.text = tr::lng_action_shared_chat_with_bot(
			tr::now,
			lt_chat,
			Ui::Text::Link(peer->name(), 1),
			lt_bot,
			Ui::Text::Link(history()->peer->name(), 2),
			Ui::Text::WithEntities);
		result.links.push_back(peer->createOpenLink());
		result.links.push_back(history()->peer->createOpenLink());
		return result;
	};

	auto prepareSetChatWallPaper = [&](
			const MTPDmessageActionSetChatWallPaper &action) {
		const auto isSelf = (_from->id == _from->session().userPeerId());
		const auto peer = isSelf ? history()->peer : _from;
		const auto user = peer->asUser();
		const auto name = (user && !user->firstName.isEmpty())
			? user->firstName
			: peer->name();
		auto result = PreparedServiceText{};
		result.links.push_back(peer->createOpenLink());
		result.text = isSelf
			? tr::lng_action_set_wallpaper_me(
				tr::now,
				Ui::Text::WithEntities)
			: tr::lng_action_set_wallpaper(
				tr::now,
				lt_user,
				Ui::Text::Link(name, 1), // Link 1.
				Ui::Text::WithEntities);
		return result;
	};

	auto prepareSetSameChatWallPaper = [&](
			const MTPDmessageActionSetSameChatWallPaper &action) {
		const auto isSelf = (_from->id == _from->session().userPeerId());
		const auto peer = isSelf ? history()->peer : _from;
		const auto user = peer->asUser();
		const auto name = (user && !user->firstName.isEmpty())
			? user->firstName
			: peer->name();
		auto result = PreparedServiceText{};
		if (!isSelf) {
			result.links.push_back(peer->createOpenLink());
		}
		result.text = isSelf
			? tr::lng_action_set_same_wallpaper_me(
				tr::now,
				Ui::Text::WithEntities)
			: tr::lng_action_set_same_wallpaper(
				tr::now,
				lt_user,
				Ui::Text::Link(name, 1), // Link 1.
				Ui::Text::WithEntities);
		return result;
	};

	setServiceText(action.match([&](
			const MTPDmessageActionChatAddUser &data) {
		return prepareChatAddUserText(data);
	}, [&](const MTPDmessageActionChatJoinedByLink &data) {
		return prepareChatJoinedByLink(data);
	}, [&](const MTPDmessageActionChatCreate &data) {
		return prepareChatCreate(data);
	}, [](const MTPDmessageActionChatMigrateTo &) {
		return PreparedServiceText();
	}, [](const MTPDmessageActionChannelMigrateFrom &) {
		return PreparedServiceText();
	}, [](const MTPDmessageActionHistoryClear &) {
		return PreparedServiceText();
	}, [&](const MTPDmessageActionChannelCreate &data) {
		return prepareChannelCreate(data);
	}, [&](const MTPDmessageActionChatDeletePhoto &) {
		return prepareChatDeletePhoto();
	}, [&](const MTPDmessageActionChatDeleteUser &data) {
		return prepareChatDeleteUser(data);
	}, [&](const MTPDmessageActionChatEditPhoto &data) {
		return prepareChatEditPhoto(data);
	}, [&](const MTPDmessageActionChatEditTitle &data) {
		return prepareChatEditTitle(data);
	}, [&](const MTPDmessageActionPinMessage &) {
		return preparePinnedText();
	}, [&](const MTPDmessageActionGameScore &) {
		return prepareGameScoreText();
	}, [&](const MTPDmessageActionPhoneCall &) -> PreparedServiceText {
		Unexpected("PhoneCall type in setServiceMessageFromMtp.");
	}, [&](const MTPDmessageActionPaymentSent &) {
		return preparePaymentSentText();
	}, [&](const MTPDmessageActionScreenshotTaken &) {
		return prepareScreenshotTaken();
	}, [&](const MTPDmessageActionCustomAction &data) {
		return prepareCustomAction(data);
	}, [&](const MTPDmessageActionBotAllowed &data) {
		return prepareBotAllowed(data);
	}, [&](const MTPDmessageActionSecureValuesSent &data) {
		return prepareSecureValuesSent(data);
	}, [&](const MTPDmessageActionContactSignUp &data) {
		return prepareContactSignUp();
	}, [&](const MTPDmessageActionGeoProximityReached &data) {
		return prepareProximityReached(data);
	}, [](const MTPDmessageActionPaymentSentMe &) {
		LOG(("API Error: messageActionPaymentSentMe received."));
		return PreparedServiceText{ { tr::lng_message_empty(tr::now) } };
	}, [](const MTPDmessageActionSecureValuesSentMe &) {
		LOG(("API Error: messageActionSecureValuesSentMe received."));
		return PreparedServiceText{ { tr::lng_message_empty(tr::now) } };
	}, [&](const MTPDmessageActionGroupCall &data) {
		return prepareGroupCall(data);
	}, [&](const MTPDmessageActionInviteToGroupCall &data) {
		return prepareInviteToGroupCall(data);
	}, [&](const MTPDmessageActionSetMessagesTTL &data) {
		return prepareSetMessagesTTL(data);
	}, [&](const MTPDmessageActionGroupCallScheduled &data) {
		return prepareCallScheduledText(data.vschedule_date().v);
	}, [&](const MTPDmessageActionSetChatTheme &data) {
		return prepareSetChatTheme(data);
	}, [&](const MTPDmessageActionChatJoinedByRequest &data) {
		return prepareChatJoinedByRequest(data);
	}, [&](const MTPDmessageActionWebViewDataSent &data) {
		return prepareWebViewDataSent(data);
	}, [&](const MTPDmessageActionGiftPremium &data) {
		return prepareGiftPremium(data);
	}, [&](const MTPDmessageActionTopicCreate &data) {
		return prepareTopicCreate(data);
	}, [&](const MTPDmessageActionTopicEdit &data) {
		return prepareTopicEdit(data);
	}, [&](const MTPDmessageActionWebViewDataSentMe &data) {
		LOG(("API Error: messageActionWebViewDataSentMe received."));
		return PreparedServiceText{ { tr::lng_message_empty(tr::now) } };
	}, [&](const MTPDmessageActionSuggestProfilePhoto &data) {
		return prepareSuggestProfilePhoto(data);
	}, [&](const MTPDmessageActionRequestedPeer &data) {
		return prepareRequestedPeer(data);
	}, [&](const MTPDmessageActionSetChatWallPaper &data) {
		return prepareSetChatWallPaper(data);
	}, [&](const MTPDmessageActionSetSameChatWallPaper &data) {
		return prepareSetSameChatWallPaper(data);
	}, [](const MTPDmessageActionEmpty &) {
		return PreparedServiceText{ { tr::lng_message_empty(tr::now) } };
	}));

	// Additional information.
	applyAction(action);
}

void HistoryItem::applyAction(const MTPMessageAction &action) {
	action.match([&](const MTPDmessageActionChatAddUser &data) {
		if (const auto channel = _history->peer->asMegagroup()) {
			const auto selfUserId = _history->session().userId();
			for (const auto &item : data.vusers().v) {
				if (peerFromUser(item) == selfUserId) {
					channel->mgInfo->joinedMessageFound = true;
					break;
				}
			}
		}
	}, [&](const MTPDmessageActionChatJoinedByLink &data) {
		if (_from->isSelf()) {
			if (const auto channel = _history->peer->asMegagroup()) {
				channel->mgInfo->joinedMessageFound = true;
			}
		}
	}, [&](const MTPDmessageActionChatEditPhoto &data) {
		data.vphoto().match([&](const MTPDphoto &photo) {
			_media = std::make_unique<Data::MediaPhoto>(
				this,
				_history->peer,
				_history->owner().processPhoto(photo));
		}, [](const MTPDphotoEmpty &) {
		});
	}, [&](const MTPDmessageActionChatCreate &) {
		_flags |= MessageFlag::IsGroupEssential;
	}, [&](const MTPDmessageActionChannelCreate &) {
		_flags |= MessageFlag::IsGroupEssential;
	}, [&](const MTPDmessageActionChatMigrateTo &) {
		_flags |= MessageFlag::IsGroupEssential;
	}, [&](const MTPDmessageActionChannelMigrateFrom &) {
		_flags |= MessageFlag::IsGroupEssential;
	}, [&](const MTPDmessageActionContactSignUp &) {
		_flags |= MessageFlag::IsContactSignUp;
	}, [&](const MTPDmessageActionChatJoinedByRequest &data) {
		if (_from->isSelf()) {
			if (const auto channel = _history->peer->asMegagroup()) {
				channel->mgInfo->joinedMessageFound = true;
			}
		}
	}, [&](const MTPDmessageActionGiftPremium &data) {
		_media = std::make_unique<Data::MediaGiftBox>(
			this,
			_from,
			data.vmonths().v);
	}, [&](const MTPDmessageActionSuggestProfilePhoto &data) {
		data.vphoto().match([&](const MTPDphoto &photo) {
			_flags |= MessageFlag::IsUserpicSuggestion;
			_media = std::make_unique<Data::MediaPhoto>(
				this,
				history()->peer,
				history()->owner().processPhoto(photo));
		}, [](const MTPDphotoEmpty &) {
		});
	}, [&](const MTPDmessageActionSetChatWallPaper &data) {
		const auto session = &history()->session();
		const auto &attached = data.vwallpaper();
		if (const auto paper = Data::WallPaper::Create(session, attached)) {
			_media = std::make_unique<Data::MediaWallPaper>(this, *paper);
		}
	}, [](const auto &) {
	});
}

void HistoryItem::setSelfDestruct(
		HistoryServiceSelfDestruct::Type type,
		MTPint mtpTTLvalue) {
	UpdateComponents(HistoryServiceSelfDestruct::Bit());
	const auto selfdestruct = Get<HistoryServiceSelfDestruct>();
	if (mtpTTLvalue.v == TimeId(0x7FFFFFFF)) {
		selfdestruct->timeToLive = TimeToLiveSingleView();
	} else {
		selfdestruct->timeToLive = mtpTTLvalue.v * crl::time(1000);
	}
	selfdestruct->type = type;
}

PreparedServiceText HistoryItem::prepareInvitedToCallText(
		const std::vector<not_null<UserData*>> &users,
		CallId linkCallId) {
	auto chatText = tr::lng_action_invite_user_chat(
		tr::now,
		Ui::Text::WithEntities);
	auto result = PreparedServiceText();
	result.links.push_back(fromLink());
	auto linkIndex = 1;
	if (linkCallId) {
		const auto peer = _history->peer;
		result.links.push_back(GroupCallClickHandler(peer, linkCallId));
		chatText = Ui::Text::Link(chatText.text, ++linkIndex);
	}
	if (users.size() == 1) {
		auto user = users[0];
		result.links.push_back(user->createOpenLink());
		result.text = tr::lng_action_invite_user(
			tr::now,
			lt_from,
			fromLinkText(), // Link 1.
			lt_user,
			Ui::Text::Link(user->name(), ++linkIndex), // Link N.
			lt_chat,
			chatText,
			Ui::Text::WithEntities);
	} else if (users.empty()) {
		result.text = tr::lng_action_invite_user(
			tr::now,
			lt_from,
			fromLinkText(), // Link 1.
			lt_user,
			{ .text = u"somebody"_q },
			lt_chat,
			chatText,
			Ui::Text::WithEntities);
	} else {
		for (auto i = 0, l = int(users.size()); i != l; ++i) {
			const auto user = users[i];
			result.links.push_back(user->createOpenLink());

			auto linkText = Ui::Text::Link(user->name(), ++linkIndex);
			if (i == 0) {
				result.text = linkText;
			} else if (i + 1 == l) {
				result.text = tr::lng_action_invite_users_and_last(
					tr::now,
					lt_accumulated,
					result.text,
					lt_user,
					linkText,
					Ui::Text::WithEntities);
			} else {
				result.text = tr::lng_action_invite_users_and_one(
					tr::now,
					lt_accumulated,
					result.text,
					lt_user,
					linkText,
					Ui::Text::WithEntities);
			}
		}
		result.text = tr::lng_action_invite_users_many(
			tr::now,
			lt_from,
			fromLinkText(), // Link 1.
			lt_users,
			result.text,
			lt_chat,
			chatText,
			Ui::Text::WithEntities);
	}
	return result;
}

PreparedServiceText HistoryItem::preparePinnedText() {
	auto result = PreparedServiceText();
	auto pinned = Get<HistoryServicePinned>();
	if (pinned && pinned->msg) {
		const auto mediaText = [&] {
			using TTL = HistoryServiceSelfDestruct;
			if (const auto media = pinned->msg->media()) {
				return media->pinnedTextSubstring();
			} else if (const auto selfdestruct = pinned->msg->Get<TTL>()) {
				if (selfdestruct->type == TTL::Type::Photo) {
					return tr::lng_action_pinned_media_photo(tr::now);
				} else if (selfdestruct->type == TTL::Type::Video) {
					return tr::lng_action_pinned_media_video(tr::now);
				}
			}
			return QString();
		}();
		result.links.push_back(fromLink());
		result.links.push_back(pinned->lnk);
		if (mediaText.isEmpty()) {
			auto original = pinned->msg->translatedText();
			auto cutAt = 0;
			auto limit = kPinnedMessageTextLimit;
			auto size = original.text.size();
			for (; limit != 0;) {
				--limit;
				if (cutAt >= size) break;
				if (original.text.at(cutAt).isLowSurrogate()
					&& (cutAt + 1 < size)
					&& original.text.at(cutAt + 1).isHighSurrogate()) {
					cutAt += 2;
				} else {
					++cutAt;
				}
			}
			if (!limit && cutAt + 5 < size) {
				original = Ui::Text::Mid(original, 0, cutAt).append(
					Ui::kQEllipsis);
			}
			original = Ui::Text::Link(
				Ui::Text::Filtered(
					std::move(original),
					{
						EntityType::Spoiler,
						EntityType::StrikeOut,
						EntityType::Italic,
						EntityType::CustomEmoji,
					}),
				2);
			result.text = tr::lng_action_pinned_message(
				tr::now,
				lt_from,
				fromLinkText(), // Link 1.
				lt_text,
				std::move(original), // Link 2.
				Ui::Text::WithEntities);
		} else {
			result.text = tr::lng_action_pinned_media(
				tr::now,
				lt_from,
				fromLinkText(), // Link 1.
				lt_media,
				Ui::Text::Link(mediaText, 2), // Link 2.
				Ui::Text::WithEntities);
		}
	} else if (pinned && pinned->msgId) {
		result.links.push_back(fromLink());
		result.links.push_back(pinned->lnk);
		result.text = tr::lng_action_pinned_media(
			tr::now,
			lt_from,
			fromLinkText(), // Link 1.
			lt_media,
			Ui::Text::Link(tr::lng_contacts_loading(tr::now), 2), // Link 2.
			Ui::Text::WithEntities);
	} else {
		result.links.push_back(fromLink());
		result.text = tr::lng_action_pinned_media(
			tr::now,
			lt_from,
			fromLinkText(), // Link 1.
			lt_media,
			{ .text = tr::lng_deleted_message(tr::now) },
			Ui::Text::WithEntities);
	}
	return result;
}

PreparedServiceText HistoryItem::prepareGameScoreText() {
	auto result = PreparedServiceText();
	auto gamescore = Get<HistoryServiceGameScore>();

	auto computeGameTitle = [&]() -> TextWithEntities {
		if (gamescore && gamescore->msg) {
			if (const auto media = gamescore->msg->media()) {
				if (const auto game = media->game()) {
					const auto row = 0;
					const auto column = 0;
					result.links.push_back(
						std::make_shared<ReplyMarkupClickHandler>(
							&_history->owner(),
							row,
							column,
							gamescore->msg->fullId()));
					auto titleText = game->title;
					return Ui::Text::Link(titleText, QString());
				}
			}
			return tr::lng_deleted_message(tr::now, Ui::Text::WithEntities);
		} else if (gamescore && gamescore->msgId) {
			return tr::lng_contacts_loading(tr::now, Ui::Text::WithEntities);
		}
		return {};
	};

	const auto scoreNumber = gamescore ? gamescore->score : 0;
	if (_from->isSelf()) {
		auto gameTitle = computeGameTitle();
		if (gameTitle.text.isEmpty()) {
			result.text = tr::lng_action_game_you_scored_no_game(
				tr::now,
				lt_count,
				scoreNumber,
				Ui::Text::WithEntities);
		} else {
			result.text = tr::lng_action_game_you_scored(
				tr::now,
				lt_count,
				scoreNumber,
				lt_game,
				gameTitle,
				Ui::Text::WithEntities);
		}
	} else {
		result.links.push_back(fromLink());
		auto gameTitle = computeGameTitle();
		if (gameTitle.text.isEmpty()) {
			result.text = tr::lng_action_game_score_no_game(
				tr::now,
				lt_count,
				scoreNumber,
				lt_from,
				fromLinkText(), // Link 1.
				Ui::Text::WithEntities);
		} else {
			result.text = tr::lng_action_game_score(
				tr::now,
				lt_count,
				scoreNumber,
				lt_from,
				fromLinkText(), // Link 1.
				lt_game,
				gameTitle,
				Ui::Text::WithEntities);
		}
	}
	return result;
}

PreparedServiceText HistoryItem::preparePaymentSentText() {
	auto result = PreparedServiceText();
	const auto payment = Get<HistoryServicePayment>();
	Assert(payment != nullptr);

	auto invoiceTitle = [&] {
		if (payment->msg) {
			if (const auto media = payment->msg->media()) {
				if (const auto invoice = media->invoice()) {
					return Ui::Text::Link(invoice->title, QString());
				}
			}
		}
		return TextWithEntities();
	}();

	if (invoiceTitle.text.isEmpty()) {
		if (payment->recurringUsed) {
			result.text = tr::lng_action_payment_used_recurring(
				tr::now,
				lt_amount,
				{ .text = payment->amount },
				Ui::Text::WithEntities);
		} else {
			result.text = (payment->recurringInit
				? tr::lng_action_payment_init_recurring
				: tr::lng_action_payment_done)(
					tr::now,
					lt_amount,
					{ .text = payment->amount },
					lt_user,
					{ .text = _history->peer->name() },
					Ui::Text::WithEntities);
		}
	} else {
		result.text = (payment->recurringInit
			? tr::lng_action_payment_init_recurring_for
			: tr::lng_action_payment_done_for)(
				tr::now,
				lt_amount,
				{ .text = payment->amount },
				lt_user,
				{ .text = _history->peer->name() },
				lt_invoice,
				invoiceTitle,
				Ui::Text::WithEntities);
		if (payment->msg) {
			result.links.push_back(payment->lnk);
		}
	}
	return result;
}

PreparedServiceText HistoryItem::prepareStoryMentionText() {
	auto result = PreparedServiceText();
	const auto peer = history()->peer;
	result.links.push_back(peer->createOpenLink());
	const auto phrase = (this->media() && this->media()->storyExpired(true))
		? (out()
			? tr::lng_action_story_mention_me_unavailable
			: tr::lng_action_story_mention_unavailable)
		: (out()
			? tr::lng_action_story_mention_me
			: tr::lng_action_story_mention);
	result.text = phrase(
		tr::now,
		lt_user,
		Ui::Text::Wrapped(
			Ui::Text::Bold(peer->shortName()),
			EntityType::CustomUrl,
			u"internal:index"_q + QChar(1)),
		Ui::Text::WithEntities);
	return result;
}

PreparedServiceText HistoryItem::prepareCallScheduledText(
		TimeId scheduleDate) {
	const auto call = Get<HistoryServiceOngoingCall>();
	Assert(call != nullptr);

	const auto scheduled = base::unixtime::parse(scheduleDate);
	const auto date = scheduled.date();
	const auto now = QDateTime::currentDateTime();
	const auto secsToDateAddDays = [&](int days) {
		return now.secsTo(QDateTime(date.addDays(days), QTime(0, 0)));
	};
	auto result = PreparedServiceText();
	const auto prepareWithDate = [&](const QString &date) {
		if (_history->peer->isBroadcast()) {
			result.text = tr::lng_action_group_call_scheduled_channel(
				tr::now,
				lt_date,
				{ .text = date },
				Ui::Text::WithEntities);
		} else {
			result.links.push_back(fromLink());
			result.text = tr::lng_action_group_call_scheduled_group(
				tr::now,
				lt_from,
				fromLinkText(), // Link 1.
				lt_date,
				{ .text = date },
				Ui::Text::WithEntities);
		}
	};
	const auto time = QLocale().toString(
		scheduled.time(),
		QLocale::ShortFormat);
	const auto prepareGeneric = [&] {
		prepareWithDate(tr::lng_group_call_starts_date(
			tr::now,
			lt_date,
			langDayOfMonthFull(date),
			lt_time,
			time));
	};
	auto nextIn = TimeId(0);
	if (now.date().addDays(1) < scheduled.date()) {
		nextIn = secsToDateAddDays(-1);
		prepareGeneric();
	} else if (now.date().addDays(1) == scheduled.date()) {
		nextIn = secsToDateAddDays(0);
		prepareWithDate(
			tr::lng_group_call_starts_tomorrow(tr::now, lt_time, time));
	} else if (now.date() == scheduled.date()) {
		nextIn = secsToDateAddDays(1);
		prepareWithDate(
			tr::lng_group_call_starts_today(tr::now, lt_time, time));
	} else {
		prepareGeneric();
	}
	if (nextIn) {
		call->lifetime = base::timer_once(
			(nextIn + 2) * crl::time(1000)
		) | rpl::start_with_next([=] {
			updateServiceText(prepareCallScheduledText(scheduleDate));
		});
	}
	return result;
}

TextWithEntities HistoryItem::fromLinkText() const {
	return Ui::Text::Link(_from->name(), 1);
}

ClickHandlerPtr HistoryItem::fromLink() const {
	return _from->createOpenLink();
}

crl::time HistoryItem::getSelfDestructIn(crl::time now) {
	if (const auto selfdestruct = Get<HistoryServiceSelfDestruct>()) {
		const auto at = std::get_if<crl::time>(&selfdestruct->destructAt);
		if (at && (*at) > 0) {
			const auto destruct = *at;
			if (destruct <= now) {
				auto text = [&] {
					switch (selfdestruct->type) {
					case HistoryServiceSelfDestruct::Type::Photo:
						return tr::lng_ttl_photo_expired(tr::now);
					case HistoryServiceSelfDestruct::Type::Video:
						return tr::lng_ttl_video_expired(tr::now);
					}
					Unexpected("Type in HistoryServiceSelfDestruct::Type");
				};
				setServiceText({ TextWithEntities{ .text = text() } });
				return 0;
			}
			return destruct - now;
		}
	}
	return 0;
}

void HistoryItem::cacheOnlyEmojiAndSpaces(bool only) {
	_flags |= MessageFlag::OnlyEmojiAndSpacesSet;
	if (only) {
		_flags |= MessageFlag::OnlyEmojiAndSpaces;
	} else {
		_flags &= ~MessageFlag::OnlyEmojiAndSpaces;
	}
}

bool HistoryItem::isOnlyEmojiAndSpaces() const {
	if (!(_flags & MessageFlag::OnlyEmojiAndSpacesSet)) {
		const_cast<HistoryItem*>(this)->cacheOnlyEmojiAndSpaces(
			!HasNotEmojiAndSpaces(_text.text));
	}
	return (_flags & MessageFlag::OnlyEmojiAndSpaces);
}

void HistoryItem::setupChatThemeChange() {
	if (const auto user = history()->peer->asUser()) {
		auto link = std::make_shared<LambdaClickHandler>([=](
				ClickContext context) {
			const auto my = context.other.value<ClickHandlerContext>();
			if (const auto controller = my.sessionWindow.get()) {
				controller->toggleChooseChatTheme(user);
			}
		});

		UpdateComponents(HistoryServiceChatThemeChange::Bit());
		Get<HistoryServiceChatThemeChange>()->link = std::move(link);
	} else {
		RemoveComponents(HistoryServiceChatThemeChange::Bit());
	}
}

void HistoryItem::setupTTLChange() {
	const auto peer = history()->peer;
	auto link = std::make_shared<LambdaClickHandler>([=](
			ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		if (const auto controller = my.sessionWindow.get()) {
			const auto validator = TTLMenu::TTLValidator(
				controller->uiShow(),
				peer);
			if (validator.can()) {
				validator.showBox();
			}
		}
	});

	UpdateComponents(HistoryServiceTTLChange::Bit());
	Get<HistoryServiceTTLChange>()->link = std::move(link);
}

void HistoryItem::clearDependencyMessage() {
	if (const auto dependent = GetServiceDependentData()) {
		if (dependent->msg) {
			_history->owner().unregisterDependentMessage(
				this,
				dependent->msg);
			dependent->msg = nullptr;
			dependent->msgId = 0;
		}
	}
}
