/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_changes.h"

#include "main/main_session.h"
#include "history/history_item.h"
#include "history/history.h"
#include "data/data_peer.h"
#include "data/data_photo.h" // For MediaPhoto
#include "data/data_document.h" // For MediaFile
#include "data/data_media_types.h" // For Media types
#include "data/data_web_page.h" // For MediaWebPage (if handled)
#include "data/data_poll.h" // For MediaPoll (if handled)
#include "data/data_game.h" // For MediaGame (if handled)
#include "data/data_invoice.h" // For MediaInvoice (if handled)
#include "data/data_media_contact.h" // For MediaContact (if handled)
#include "data/data_media_venue.h" // For MediaVenue (if handled)
#include "core/file_location.h"
#include "storage/deleted_messages_storage.h"
#include "data/stored_deleted_message.h"
#include "base/unixtime.h"
#include "base/platform/base_platform_file_utilities.h" // For Global::UserBasePath(), though session->userBasePath() is preferred
#include "core/application.h" // For Core::App().ensureWorkThread(); (if needed for DB ops, though storage should handle it)

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QUuid>


namespace Data {

namespace { // Anonymous namespace for helpers

// Helper Function: CopyDeletedMedia
QString CopyDeletedMedia(
        const Core::FileLocation &location,
        const QString &userBasePath, // tdata path
        const QString &suggestedName) {
    if (location.isEmpty() || location.name().isEmpty()) {
        LOG(("Debug: CopyDeletedMedia: Location is empty or has no name."));
        return QString();
    }

    const QString mediaDirSubPath = qsl("deleted_media");
    const QString mediaDirPath = userBasePath + mediaDirSubPath;
    QDir dir(mediaDirPath);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            LOG(("Error: Could not create directory for deleted media: %1").arg(mediaDirPath));
            return QString();
        }
    }

    QString baseName = suggestedName;
    if (baseName.isEmpty()) {
        // Try to get a name from the location itself if suggestedName is empty
        baseName = QFileInfo(location.name()).fileName();
    }
    if (baseName.isEmpty()) { // Still empty, generate a UUID based name
        baseName = QUuid::createUuid().toString(QUuid::WithoutBraces) + qsl(".bin");
    }
    
    // Ensure a somewhat unique name to avoid collisions, though UUID is better if no name parts
    QString newFileName = QDateTime::currentDateTime().toString(qsl("yyyyMMdd_HHmmss_zzz_")) + baseName;
    newFileName.remove(QRegularExpression(qsl("[^a-zA-Z0-9_.-]"))); // Sanitize filename

    QString newFilePath = mediaDirPath + qsl("/") + newFileName;

    if (QFile::exists(location.name())) { // Check if source file exists
        if (QFile::copy(location.name(), newFilePath)) {
            LOG(("Info: Copied deleted media from '%1' to '%2'").arg(location.name()).arg(newFilePath));
            return newFilePath;
        } else {
            LOG(("Error: Failed to copy media from '%1' to '%2'. Error: %3")
                .arg(location.name())
                .arg(newFilePath)
                .arg(QFileDevice::NoError)); // QFile doesn't set error string on copy failure directly
        }
    } else {
         LOG(("Warning: Source media file does not exist: %1").arg(location.name()));
    }
    return QString();
}


// Helper Function: PopulateStoredDeletedMessageFromHistoryItem
void PopulateStoredDeletedMessageFromHistoryItem(
        StoredDeletedMessage &outMsg,
        const HistoryItem *item,
        const QString& userBasePath) { // userBasePath is tdata
    if (!item) return;

    outMsg.originalMessageId = item->id;
    if (item->history()) { // Should always be true
        outMsg.peerId = item->history()->peer->id;
    }
    outMsg.globalId = item->globalId(); // Assuming GlobalMsgId is directly assignable
    outMsg.date = item->date();
    if (item->from()) { // Should always be true for regular messages
        outMsg.senderId = item->from()->id;
    }
    outMsg.flags = item->flags();
    outMsg.text = item->originalText(); // TextWithEntities
    outMsg.topicRootId = item->topicRootId();

    // Forward Info
    if (const auto* historyForwarded = item->Get<HistoryMessageForwarded>()) {
        StoredMessageForwardInfo fwdInfo;
        if (historyForwarded->originalSender) {
            fwdInfo.originalSenderId = historyForwarded->originalSender->id;
        } else if (historyForwarded->originalHiddenSenderInfo) {
            fwdInfo.originalSenderName = historyForwarded->originalHiddenSenderInfo->name;
            // originalSenderId remains 0 or a special value like PeerId()
        }
        fwdInfo.originalDate = historyForwarded->originalDate;
        fwdInfo.originalMessageId = historyForwarded->originalId;
        // TODO: Consider historyForwarded->originalFullId if needed for StoredMessageForwardInfo
        outMsg.forwardInfo = fwdInfo;
    }

    // Reply Info
    if (const auto* historyReply = item->Get<HistoryMessageReply>()) {
        StoredMessageReplyInfo replyInfo;
        const auto& replyFields = historyReply->fields();
        replyInfo.replyToMessageId = replyFields.messageId;
        if (replyFields.externalPeerId) {
            replyInfo.replyToPeerId = replyFields.externalPeerId;
        } else if (replyFields.messageId && item->history()) { // If not external, it's in the same peer
            replyInfo.replyToPeerId = item->history()->peer->id;
        }
        // TODO: Consider storing replyFields.quote and replyFields.quoteOffset if desired
        outMsg.replyInfo = replyInfo;
    }

    // Media Info
    // Helper lambda to process a single Data::Media item
    auto processMediaItem = [&](const Data::Media* mediaItem) {
        if (!mediaItem) return;

        StoredMediaInfo smi;
        smi.caption = mediaItem->caption(); // TextWithEntities

        if (const auto photo = mediaItem->photo()) {
            smi.type = StoredMediaType::Photo;
            smi.remoteFileId = QString::number(photo->id.value); // Assuming PhotoId has .value
            // Attempt to copy the largest available cached image
            // This PhotoSize logic might need adjustment based on actual PhotoData structure.
            auto imageLocation = photo->location(Window::Adaptive::DevicePixelRatio() > 1 ? PhotoSize::Large : PhotoSize::Medium);
            if (imageLocation.isEmpty()) { // Fallback if preferred size not found
                imageLocation = photo->location(PhotoSize::Small); // Or any other available
            }
            smi.filePath = CopyDeletedMedia(imageLocation, userBasePath, "photo_" + smi.remoteFileId + ".jpg");
            // TODO: Store dimensions (photo->width(), photo->height()) if needed in StoredMediaInfo
        } else if (const auto document = mediaItem->document()) {
            if (document->isVideoFile()) {
                smi.type = StoredMediaType::Video;
                smi.duration = document->duration();
            } else if (document->isGifv()) {
                smi.type = StoredMediaType::Gif; // Or AnimatedSticker if it's a .LOTTIE
                smi.duration = document->duration();
            } else if (document->isVoiceMessage()) {
                smi.type = StoredMediaType::VoiceMessage;
                smi.duration = document->duration();
            } else if (document->isAudioFile()) {
                smi.type = StoredMediaType::AudioFile;
                smi.duration = document->duration();
            } else if (document->isSticker()) {
                 smi.type = StoredMediaType::Sticker; // Or AnimatedSticker if LOTTIE
                 // TODO: Store sticker set info, emoji from document->sticker()
            } else {
                smi.type = StoredMediaType::Document;
            }
            smi.remoteFileId = QString::number(document->id.value); // Assuming DocumentId has .value
            smi.filePath = CopyDeletedMedia(document->location(), userBasePath, document->filename());
            // TODO: Store document attributes like filename, mime_type if needed in StoredMediaInfo
        } else if (const auto webpage = mediaItem->webpage()) {
            smi.type = StoredMediaType::WebPage;
            // TODO: Populate WebPage specific fields for StoredMediaInfo
            // e.g., webpage->url(), webpage->siteName(), webpage->title(), webpage->description()
            // If webpage has a photo or document, recursively call processMediaItem or similar logic
        } else if (const auto poll = mediaItem->poll()) {
            smi.type = StoredMediaType::Poll;
            // TODO: Serialize poll data (poll->data()) into smi.filePath or specific fields
        }
        // TODO: Handle other media types: Game, Invoice, Contact, Venue etc.
        // case Data::MediaGame: smi.type = StoredMediaType::Game; ...
        // case Data::MediaInvoice: ... (might not be directly copyable, store info)
        // case Data::MediaContact: smi.type = StoredMediaType::Contact; ... (store contact info)
        // case Data::MediaVenue: smi.type = StoredMediaType::Location; ... (store geo point, title, address)

        if (smi.type != StoredMediaType::None) { // Only add if we recognized and processed it
            outMsg.mediaList.push_back(smi);
        }
    };

    if (item->media()) {
        if (item->media()->isGrouped()) {
            // For albums/grouped media
            const auto &grouped = item->groupedMedia();
            for (const auto& groupItem : grouped) {
                if (groupItem && groupItem->media()) {
                    processMediaItem(groupItem->media());
                }
            }
        } else {
            // For single media items
            processMediaItem(item->media());
        }
    }
}

} // anonymous namespace


template <typename DataType, typename UpdateType>
void Changes::Manager<DataType, UpdateType>::updated(
		not_null<DataType*> data,
		Flags flags,
		bool dropScheduled) {
	sendRealtimeNotifications(data, flags);
	if (dropScheduled) {
		const auto i = _updates.find(data);
		if (i != _updates.end()) {
			flags |= i->second;
			_updates.erase(i);
		}
		_stream.fire({ data, flags });
	} else {
		_updates[data] |= flags;
	}
}

template <typename DataType, typename UpdateType>
void Changes::Manager<DataType, UpdateType>::sendRealtimeNotifications(
		not_null<DataType*> data,
		Flags flags) {
	for (auto i = 0; i != kCount; ++i) {
		const auto flag = static_cast<Flag>(1U << i);
		if (flags & flag) {
			_realtimeStreams[i].fire({ data, flags });
		}
	}
}

template <typename DataType, typename UpdateType>
rpl::producer<UpdateType> Changes::Manager<DataType, UpdateType>::updates(
		Flags flags) const {
	return _stream.events(
	) | rpl::filter([=](const UpdateType &update) {
		return (update.flags & flags);
	});
}

template <typename DataType, typename UpdateType>
rpl::producer<UpdateType> Changes::Manager<DataType, UpdateType>::updates(
		not_null<DataType*> data,
		Flags flags) const {
	return _stream.events(
	) | rpl::filter([=](const UpdateType &update) {
		const auto &[updateData, updateFlags] = update;
		return (updateData == data) && (updateFlags & flags);
	});
}

template <typename DataType, typename UpdateType>
auto Changes::Manager<DataType, UpdateType>::realtimeUpdates(Flag flag) const
-> rpl::producer<UpdateType> {
	return _realtimeStreams[details::CountBit(flag)].events();
}

template <typename DataType, typename UpdateType>
rpl::producer<UpdateType> Changes::Manager<DataType, UpdateType>::flagsValue(
		not_null<DataType*> data,
		Flags flags) const {
	return rpl::single(
		UpdateType{ data, flags }
	) | rpl::then(updates(data, flags));
}

template <typename DataType, typename UpdateType>
void Changes::Manager<DataType, UpdateType>::drop(not_null<DataType*> data) {
	_updates.remove(data);
}

template <typename DataType, typename UpdateType>
void Changes::Manager<DataType, UpdateType>::sendNotifications() {
	for (const auto &[data, flags] : base::take(_updates)) {
		_stream.fire({ data, flags });
	}
}

Changes::Changes(not_null<Main::Session*> session) : _session(session) {
}

Main::Session &Changes::session() const {
	return *_session;
}

void Changes::nameUpdated(
		not_null<PeerData*> peer,
		base::flat_set<QChar> oldFirstLetters) {
	_nameStream.fire({ peer, std::move(oldFirstLetters) });
}

rpl::producer<NameUpdate> Changes::realtimeNameUpdates() const {
	return _nameStream.events();
}

rpl::producer<NameUpdate> Changes::realtimeNameUpdates(
		not_null<PeerData*> peer) const {
	return _nameStream.events() | rpl::filter([=](const NameUpdate &update) {
		return (update.peer == peer);
	});
}

void Changes::peerUpdated(not_null<PeerData*> peer, PeerUpdate::Flags flags) {
	_peerChanges.updated(peer, flags);
	scheduleNotifications();
}

rpl::producer<PeerUpdate> Changes::peerUpdates(
		PeerUpdate::Flags flags) const {
	return _peerChanges.updates(flags);
}

rpl::producer<PeerUpdate> Changes::peerUpdates(
		not_null<PeerData*> peer,
		PeerUpdate::Flags flags) const {
	return _peerChanges.updates(peer, flags);
}

rpl::producer<PeerUpdate> Changes::peerFlagsValue(
		not_null<PeerData*> peer,
		PeerUpdate::Flags flags) const {
	return _peerChanges.flagsValue(peer, flags);
}

rpl::producer<PeerUpdate> Changes::realtimePeerUpdates(
		PeerUpdate::Flag flag) const {
	return _peerChanges.realtimeUpdates(flag);
}

void Changes::historyUpdated(
		not_null<History*> history,
		HistoryUpdate::Flags flags) {
	_historyChanges.updated(history, flags);
	scheduleNotifications();
}

rpl::producer<HistoryUpdate> Changes::historyUpdates(
		HistoryUpdate::Flags flags) const {
	return _historyChanges.updates(flags);
}

rpl::producer<HistoryUpdate> Changes::historyUpdates(
		not_null<History*> history,
		HistoryUpdate::Flags flags) const {
	return _historyChanges.updates(history, flags);
}

rpl::producer<HistoryUpdate> Changes::historyFlagsValue(
		not_null<History*> history,
		HistoryUpdate::Flags flags) const {
	return _historyChanges.flagsValue(history, flags);
}

rpl::producer<HistoryUpdate> Changes::realtimeHistoryUpdates(
		HistoryUpdate::Flag flag) const {
	return _historyChanges.realtimeUpdates(flag);
}

void Changes::topicUpdated(
		not_null<ForumTopic*> topic,
		TopicUpdate::Flags flags) {
	const auto drop = (flags & TopicUpdate::Flag::Destroyed);
	_topicChanges.updated(topic, flags, drop);
	if (!drop) {
		scheduleNotifications();
	}
}

rpl::producer<TopicUpdate> Changes::topicUpdates(
		TopicUpdate::Flags flags) const {
	return _topicChanges.updates(flags);
}

rpl::producer<TopicUpdate> Changes::topicUpdates(
		not_null<ForumTopic*> topic,
		TopicUpdate::Flags flags) const {
	return _topicChanges.updates(topic, flags);
}

rpl::producer<TopicUpdate> Changes::topicFlagsValue(
		not_null<ForumTopic*> topic,
		TopicUpdate::Flags flags) const {
	return _topicChanges.flagsValue(topic, flags);
}

rpl::producer<TopicUpdate> Changes::realtimeTopicUpdates(
		TopicUpdate::Flag flag) const {
	return _topicChanges.realtimeUpdates(flag);
}

void Changes::topicRemoved(not_null<ForumTopic*> topic) {
	_topicChanges.drop(topic);
}

void Changes::messageUpdated(
		not_null<HistoryItem*> item,
		MessageUpdate::Flags flags) {

	if (flags & MessageUpdate::Flag::Destroyed) {
		// Check if it's a message type we want to save (e.g., not a temporary service message if desired)
		// For now, assume we save all types that appear here.
		// This check could be enhanced, e.g. !item->isService() or item->isRegular()

		if (!_session->settings().saveDeletedMessagesEnabled()) {
			// If feature is disabled, skip saving.
			// We still need to call the original _messageChanges.updated below.
			const auto drop = (flags & MessageUpdate::Flag::Destroyed); // Recalculate drop for clarity
			_messageChanges.updated(item, flags, drop);
			if (!drop) {
				scheduleNotifications();
			}
			return;
		}

		Storage::DeletedMessagesStorage* deletedStorage = _session->deletedMessagesStorage();
		if (deletedStorage) {
			StoredDeletedMessage storedMsg;

			// Populate storedMsg from 'item'
			PopulateStoredDeletedMessageFromHistoryItem(storedMsg, item, _session->local().basePath());

			// Set the deletedDate
			storedMsg.deletedDate = static_cast<TimeId>(base::unixtime::now());

			if (!deletedStorage->addMessage(storedMsg)) {
				LOG(("Error: Failed to save deleted message (PeerID: %1, MsgID: %2) to local store.")
					.arg(item->history()->peer->id.value)
					.arg(item->id));
			} else {
				LOG(("Info: Saved deleted message (PeerID: %1, MsgID: %2) to local store.")
					.arg(item->history()->peer->id.value)
					.arg(item->id));
			}
		} else {
			LOG(("Error: DeletedMessagesStorage instance is not available. Cannot save deleted message."));
		}
	}

	const auto drop = (flags & MessageUpdate::Flag::Destroyed);
	_messageChanges.updated(item, flags, drop);
	if (!drop) {
		scheduleNotifications();
	}
}

rpl::producer<MessageUpdate> Changes::messageUpdates(
		MessageUpdate::Flags flags) const {
	return _messageChanges.updates(flags);
}

rpl::producer<MessageUpdate> Changes::messageUpdates(
		not_null<HistoryItem*> item,
		MessageUpdate::Flags flags) const {
	return _messageChanges.updates(item, flags);
}

rpl::producer<MessageUpdate> Changes::messageFlagsValue(
		not_null<HistoryItem*> item,
		MessageUpdate::Flags flags) const {
	return _messageChanges.flagsValue(item, flags);
}

rpl::producer<MessageUpdate> Changes::realtimeMessageUpdates(
		MessageUpdate::Flag flag) const {
	return _messageChanges.realtimeUpdates(flag);
}

void Changes::entryUpdated(
		not_null<Dialogs::Entry*> entry,
		EntryUpdate::Flags flags) {
	const auto drop = (flags & EntryUpdate::Flag::Destroyed);
	_entryChanges.updated(entry, flags, drop);
	if (!drop) {
		scheduleNotifications();
	}
}

rpl::producer<EntryUpdate> Changes::entryUpdates(
		EntryUpdate::Flags flags) const {
	return _entryChanges.updates(flags);
}

rpl::producer<EntryUpdate> Changes::entryUpdates(
		not_null<Dialogs::Entry*> entry,
		EntryUpdate::Flags flags) const {
	return _entryChanges.updates(entry, flags);
}

rpl::producer<EntryUpdate> Changes::entryFlagsValue(
		not_null<Dialogs::Entry*> entry,
		EntryUpdate::Flags flags) const {
	return _entryChanges.flagsValue(entry, flags);
}

rpl::producer<EntryUpdate> Changes::realtimeEntryUpdates(
		EntryUpdate::Flag flag) const {
	return _entryChanges.realtimeUpdates(flag);
}

void Changes::entryRemoved(not_null<Dialogs::Entry*> entry) {
	_entryChanges.drop(entry);
}

void Changes::storyUpdated(
		not_null<Story*> story,
		StoryUpdate::Flags flags) {
	const auto drop = (flags & StoryUpdate::Flag::Destroyed);
	_storyChanges.updated(story, flags, drop);
	if (!drop) {
		scheduleNotifications();
	}
}

rpl::producer<StoryUpdate> Changes::storyUpdates(
		StoryUpdate::Flags flags) const {
	return _storyChanges.updates(flags);
}

rpl::producer<StoryUpdate> Changes::storyUpdates(
		not_null<Story*> story,
		StoryUpdate::Flags flags) const {
	return _storyChanges.updates(story, flags);
}

rpl::producer<StoryUpdate> Changes::storyFlagsValue(
		not_null<Story*> story,
		StoryUpdate::Flags flags) const {
	return _storyChanges.flagsValue(story, flags);
}

rpl::producer<StoryUpdate> Changes::realtimeStoryUpdates(
		StoryUpdate::Flag flag) const {
	return _storyChanges.realtimeUpdates(flag);
}

void Changes::scheduleNotifications() {
	if (!_notify) {
		_notify = true;
		crl::on_main(&session(), [=] {
			sendNotifications();
		});
	}
}

void Changes::sendNotifications() {
	if (!_notify) {
		return;
	}
	_notify = false;
	_peerChanges.sendNotifications();
	_historyChanges.sendNotifications();
	_messageChanges.sendNotifications();
	_entryChanges.sendNotifications();
	_topicChanges.sendNotifications();
	_storyChanges.sendNotifications();
}

} // namespace Data
