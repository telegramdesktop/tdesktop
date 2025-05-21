#pragma once

#include "storage/storage_facade_fwd.h" // For Storage::MessageId, Storage::PeerId, etc. (or equivalent basic types)
#include "data/data_text_utils.h"     // For TextWithEntities
#include "data/data_media_types.h"   // For potential media-related enums or structs if simple ones exist
#include "data/data_saved_messages_fwd.h" // For Data::MessagePosition if needed for context

#include <QString>
#include <vector>
#include <cstdint>
#include <optional> // Required for std::optional

namespace Data {

// Forward declarations for complex types if full includes are too heavy
class Media; // Assuming Data::Media is the base class for media types
struct MessageForwarded; // Assuming a struct for forwarded info
struct MessageReply;     // Assuming a struct for reply info

// Enum to represent the type of media stored, if we store simplified media info
enum class StoredMediaType : uint8_t {
    None,
    Photo,
    Video,
    AudioFile,
    VoiceMessage,
    Document,
    Sticker,
    AnimatedSticker,
    Poll,
    WebPage,
    Game,
    Location,
    Contact,
    Call,
    Gif,
    // Add more as needed
};

struct StoredMediaInfo {
    StoredMediaType type = StoredMediaType::None;
    QString filePath; // Path to a copied media file, if applicable
    QString remoteFileId; // For referencing original media if not copied or for re-download
    TextWithEntities caption; // Media caption, if any
    int duration = 0; // For voice/video/audio
    // Add other common media attributes: width, height, stickerSetId, pollData, etc.
    // For complex media like polls, we might need a serialized string or a dedicated substructure.
};

struct StoredMessageForwardInfo {
    Storage::PeerId originalSenderId = 0;
    QString originalSenderName; // If sender is hidden or not in contacts
    Storage::TimeId originalDate = 0;
    Storage::MsgId originalMessageId = 0; // If forwarded from a channel
    // Potentially: FullMsgId originalFullId;
};

struct StoredMessageReplyInfo {
    Storage::MsgId replyToMessageId = 0;
    Storage::PeerId replyToPeerId = 0; // Peer of the message being replied to, if different
    // Potentially: FullMsgId replyToFullId;
    // Consider storing a snippet of the replied-to message text/media for context if needed.
};

struct StoredDeletedMessage {
    Storage::MsgId originalMessageId = 0;
    Storage::GlobalMsgId globalId; // Useful for unique identification across accounts if ever needed

    Storage::PeerId peerId = 0;         // Peer ID of the chat/channel
    Storage::TimeId date = 0;
    Storage::TimeId deletedDate = 0;    // Timestamp of when it was marked as deleted

    Storage::PeerId senderId = 0;
    Storage::MessageFlags flags = 0;

    TextWithEntities text;
    std::vector<StoredMediaInfo> mediaList; // Use a list if a message can have multiple media (e.g., album)

    std::optional<StoredMessageForwardInfo> forwardInfo;
    std::optional<StoredMessageReplyInfo> replyInfo;

    Storage::MsgId topicRootId = 0;

    // TODO: Consider adding other relevant fields from HistoryItem or its components
    // - viaBotId
    // - groupedId
    // - reactions (might be complex to store fully)
    // - editDate
};

} // namespace Data
