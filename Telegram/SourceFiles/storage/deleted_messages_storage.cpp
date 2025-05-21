#include "storage/deleted_messages_storage.h"
#include "base/logging.h"
#include "data/data_peer_id.h" // For PeerId construction (e.g. peerFromUser)
#include "data/data_channel_id.h" // For ChannelId construction
#include "data/data_msg_id.h" // For MsgId construction
// #include "main/main_session.h" // If session context is needed for deserialization (e.g. resolving peer names)

#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>
#include <QVariant>
#include <QDir>
#include <QFile>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>


// Forward declare TextEntity if its full definition is not pulled by stored_deleted_message.h
// For now, assuming TextEntity definition used in serialization is available or compatible.
// If Data::TextEntity is complex, its include might be needed.
// For simplicity, this example assumes Data::TextEntity is defined as it was in the previous step's serialization.
namespace Data {
// Assuming TextEntity and EntityType are defined as previously.
// If they are defined in a header, this redefinition is not needed.
#ifndef TEXT_ENTITY_DEFINED_PREVIOUSLY // Crude guard
#define TEXT_ENTITY_DEFINED_PREVIOUSLY
enum class EntityType : uint16_t {
    // Add actual entity types here, e.g., Bold, Italic, Url, CustomEmoji, Mention, Hashtag etc.
    Unknown,
    Url,
    CustomEmoji,
    MentionName, // Example
};

class TextEntity {
public:
    TextEntity() = default;
    TextEntity(EntityType type, int offset, int length, QString argument = QString(), uint64 custom_id = 0)
    : _type(type), _offset(offset), _length(length), _argument(argument), _custom_id(custom_id) {}

    EntityType type() const { return _type; }
    int offset() const { return _offset; }
    int length() const { return _length; }
    QString argument() const { return _argument; } // For URL, etc.
    uint64 custom_id() const { return _custom_id; } // For CustomEmoji

private:
    EntityType _type = EntityType::Unknown;
    int _offset = 0;
    int _length = 0;
    QString _argument;
    uint64 _custom_id = 0;
};
#endif
} // namespace Data


namespace Storage {

namespace { // Anonymous namespace for helpers

// DESERIALIZATION HELPERS

std::vector<Data::TextEntity> DeserializeTextEntities(const QString& serializedEntitiesText) {
    if (serializedEntitiesText.isEmpty() || serializedEntitiesText == qsl("null")) {
        return {};
    }
    std::vector<Data::TextEntity> result;
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(serializedEntitiesText.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isArray()) {
        LOG(("Error: Failed to parse TextEntities JSON: %1. JSON: %2").arg(error.errorString()).arg(serializedEntitiesText));
        return {};
    }
    QJsonArray jsonArray = doc.array();
    for (const QJsonValue &value : jsonArray) {
        if (!value.isObject()) continue;
        QJsonObject jsonObj = value.toObject();
        // Ensure Data::EntityType is appropriate for this cast.
        // This might need a mapping function if enum values don't match stored int.
        Data::EntityType type = static_cast<Data::EntityType>(jsonObj["type"].toInt(static_cast<int>(Data::EntityType::Unknown)));
        int offset = jsonObj["offset"].toInt();
        int length = jsonObj["length"].toInt();
        QString argument;
        uint64 customId = 0;

        // Example for custom types
        if (jsonObj.contains("url")) { // Assuming type was Url
            argument = jsonObj["url"].toString();
        } else if (jsonObj.contains("custom_id")) { // Assuming type was CustomEmoji
             argument = jsonObj["custom_id"].toString(); // Store custom_id as string if it was stored so
             customId = argument.toULongLong();
        }
        result.emplace_back(type, offset, length, argument, customId);
    }
    return result;
}


std::vector<Data::StoredMediaInfo> DeserializeMediaList(const QString& serializedMediaListText) {
    if (serializedMediaListText.isEmpty() || serializedMediaListText == qsl("null")) {
        return {};
    }
    std::vector<Data::StoredMediaInfo> result;
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(serializedMediaListText.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isArray()) {
        LOG(("Error: Failed to parse MediaList JSON: %1. JSON: %2").arg(error.errorString()).arg(serializedMediaListText));
        return {};
    }
    QJsonArray jsonArray = doc.array();
    for (const QJsonValue &value : jsonArray) {
        if (!value.isObject()) continue;
        QJsonObject jsonObj = value.toObject();
        Data::StoredMediaInfo media;
        media.type = static_cast<Data::StoredMediaType>(jsonObj["type"].toInt(static_cast<int>(Data::StoredMediaType::None)));
        media.filePath = jsonObj["filePath"].toString();
        media.remoteFileId = jsonObj["remoteFileId"].toString();
        media.caption.text = jsonObj["caption_text"].toString();
        media.caption.entities = DeserializeTextEntities(jsonObj["caption_entities"].toString());
        media.duration = jsonObj["duration"].toInt();
        // ... deserialize other StoredMediaInfo fields
        result.push_back(media);
    }
    return result;
}

std::optional<Data::StoredMessageForwardInfo> DeserializeForwardInfo(const QString& serializedForwardInfoText) {
    if (serializedForwardInfoText.isEmpty() || serializedForwardInfoText == qsl("null")) {
        return std::nullopt;
    }
    Data::StoredMessageForwardInfo forwardInfo;
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(serializedForwardInfoText.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
         LOG(("Error: Failed to parse ForwardInfo JSON: %1. JSON: %2").arg(error.errorString()).arg(serializedForwardInfoText));
        return std::nullopt;
    }
    QJsonObject jsonObj = doc.object();
    // Assuming PeerId can be reconstructed from its raw value.
    // This might need more context (e.g. type of peer) for full PeerId object.
    // For now, using .value directly to store the ID.
    forwardInfo.originalSenderId = PeerId(jsonObj["originalSenderId"].toString().toLongLong());
    forwardInfo.originalSenderName = jsonObj["originalSenderName"].toString();
    forwardInfo.originalDate = jsonObj["originalDate"].toVariant().toLongLong(); // qint64 for time
    forwardInfo.originalMessageId = jsonObj["originalMessageId"].toVariant().toLongLong();
    return forwardInfo;
}

std::optional<Data::StoredMessageReplyInfo> DeserializeReplyInfo(const QString& serializedReplyInfoText) {
    if (serializedReplyInfoText.isEmpty() || serializedReplyInfoText == qsl("null")) {
        return std::nullopt;
    }
    Data::StoredMessageReplyInfo replyInfo;
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(serializedReplyInfoText.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        LOG(("Error: Failed to parse ReplyInfo JSON: %1. JSON: %2").arg(error.errorString()).arg(serializedReplyInfoText));
        return std::nullopt;
    }
    QJsonObject jsonObj = doc.object();
    replyInfo.replyToMessageId = jsonObj["replyToMessageId"].toVariant().toLongLong();
    replyInfo.replyToPeerId = PeerId(jsonObj["replyToPeerId"].toString().toLongLong());
    return replyInfo;
}


Data::StoredDeletedMessage MessageFromQuery(QSqlQuery &query) {
    Data::StoredDeletedMessage msg;
    msg.originalMessageId = query.value("originalMessageId").toLongLong();
    msg.peerId = PeerId(query.value("peerId").toLongLong());

    // Reconstruct GlobalMsgId - this is highly dependent on its actual structure
    // Assuming GlobalMsgId { ItemId item; } where ItemId { ChannelId channelId; MsgId msgId; }
    // And ChannelId, MsgId have .bare accessors for the raw ID.
    // This is a specific interpretation and needs to match the definition of GlobalMsgId.
    quint64 globalId_ch_bare = query.value("globalId_part1").toULongLong();
    quint64 globalId_msg_bare = query.value("globalId_part2").toULongLong();
    if (globalId_ch_bare != 0 && globalId_msg_bare != 0) { // Basic check if it was stored
         Data::ChannelId channelId(globalId_ch_bare); // Assuming ChannelId can be constructed from bare
         Data::MsgId msgId(globalId_msg_bare);       // Assuming MsgId can be constructed from bare
         msg.globalId = Storage::GlobalMsgId{ Data::FullMsgId(channelId, msgId) }; // Reconstruct based on assumed structure
    } else {
        // Handle case where globalId wasn't stored or is invalid
        msg.globalId = Storage::GlobalMsgId{}; // Default value
    }


    msg.date = query.value("date").toLongLong();
    msg.deletedDate = query.value("deletedDate").toLongLong();
    msg.senderId = PeerId(query.value("senderId").toLongLong());
    msg.flags = static_cast<Storage::MessageFlags>(query.value("flags").toInt());
    msg.text.text = query.value("text_content").toString();
    msg.text.entities = DeserializeTextEntities(query.value("text_entities").toString());
    msg.mediaList = DeserializeMediaList(query.value("media_info").toString());
    
    QString forwardInfoStr = query.value("forward_info").toString();
    if (!forwardInfoStr.isEmpty() && forwardInfoStr != qsl("null")) {
        msg.forwardInfo = DeserializeForwardInfo(forwardInfoStr);
    }
    QString replyInfoStr = query.value("reply_info").toString();
    if (!replyInfoStr.isEmpty() && replyInfoStr != qsl("null")) {
        msg.replyInfo = DeserializeReplyInfo(replyInfoStr);
    }
    msg.topicRootId = query.value("topicRootId").toLongLong();
    return msg;
}

} // anonymous namespace


// SERIALIZATION HELPERS (copied from previous step for completeness, can be refactored)
QString SerializeTextEntities(const std::vector<Data::TextEntity>& entities) {
    QJsonArray jsonArray;
    for (const auto& entity : entities) {
        QJsonObject jsonObj;
        jsonObj["type"] = static_cast<int>(entity.type());
        jsonObj["offset"] = entity.offset();
        jsonObj["length"] = entity.length();
        if (!entity.argument().isEmpty()) { // Example for URL / custom data
             jsonObj["argument"] = entity.argument();
        }
        if (entity.custom_id() != 0) { // Example for custom emoji
             jsonObj["custom_id"] = QString::number(entity.custom_id());
        }
        jsonArray.append(jsonObj);
    }
    return QString::fromUtf8(QJsonDocument(jsonArray).toJson(QJsonDocument::Compact));
}

QString SerializeMediaList(const std::vector<Data::StoredMediaInfo>& mediaList) {
    QJsonArray jsonArray;
    for (const auto& media : mediaList) {
        QJsonObject jsonObj;
        jsonObj["type"] = static_cast<int>(media.type);
        jsonObj["filePath"] = media.filePath;
        jsonObj["remoteFileId"] = media.remoteFileId;
        jsonObj["caption_text"] = media.caption.text;
        jsonObj["caption_entities"] = SerializeTextEntities(media.caption.entities);
        jsonObj["duration"] = media.duration;
        // ... other StoredMediaInfo fields
        jsonArray.append(jsonObj);
    }
    return QString::fromUtf8(QJsonDocument(jsonArray).toJson(QJsonDocument::Compact));
}

QString SerializeForwardInfo(const std::optional<Data::StoredMessageForwardInfo>& forwardInfo) {
    if (!forwardInfo) return QStringLiteral("null");
    QJsonObject jsonObj;
    jsonObj["originalSenderId"] = QString::number(forwardInfo->originalSenderId.value);
    jsonObj["originalSenderName"] = forwardInfo->originalSenderName;
    jsonObj["originalDate"] = static_cast<qint64>(forwardInfo->originalDate);
    jsonObj["originalMessageId"] = static_cast<qint64>(forwardInfo->originalMessageId);
    return QString::fromUtf8(QJsonDocument(jsonObj).toJson(QJsonDocument::Compact));
}

QString SerializeReplyInfo(const std::optional<Data::StoredMessageReplyInfo>& replyInfo) {
    if (!replyInfo) return QStringLiteral("null");
    QJsonObject jsonObj;
    jsonObj["replyToMessageId"] = static_cast<qint64>(replyInfo->replyToMessageId);
    jsonObj["replyToPeerId"] = QString::number(replyInfo->replyToPeerId.value);
    return QString::fromUtf8(QJsonDocument(jsonObj).toJson(QJsonDocument::Compact));
}


DeletedMessagesStorage::DeletedMessagesStorage(const QString &basePath)
: _dbPath(basePath + qsl("/deleted_messages.sqlite")) {
    LOG(("DeletedMessagesStorage: Path set to %1").arg(_dbPath));

    const QString connectionName = qsl("deleted_messages_connection_") + QString::number(reinterpret_cast<quintptr>(this));
    if (QSqlDatabase::contains(connectionName)) {
        _db = std::make_unique<QSqlDatabase>(QSqlDatabase::database(connectionName, false));
    } else {
        _db = std::make_unique<QSqlDatabase>(QSqlDatabase::addDatabase("QSQLITE", connectionName));
    }
    _db->setDatabaseName(_dbPath);
}

DeletedMessagesStorage::~DeletedMessagesStorage() {
    close(); // Ensure DB is closed
    if (_db) { // Check if _db was initialized
        const QString actualConnectionName = _db->connectionName();
        _db.reset(); // Release the QSqlDatabase object
        if (QSqlDatabase::contains(actualConnectionName)) {
            QSqlDatabase::removeDatabase(actualConnectionName);
            LOG(("DeletedMessagesStorage: Database connection '%1' removed.").arg(actualConnectionName));
        }
    }
}


bool DeletedMessagesStorage::initialize() {
    if (!_db) {
        LOG(("Error: DeletedMessagesStorage database object is null during initialize."));
        // Attempt to re-create it
        const QString connectionName = qsl("deleted_messages_connection_init_") + QString::number(reinterpret_cast<quintptr>(this));
         if (QSqlDatabase::contains(connectionName)) {
             _db = std::make_unique<QSqlDatabase>(QSqlDatabase::database(connectionName, false));
         } else {
             _db = std::make_unique<QSqlDatabase>(QSqlDatabase::addDatabase("QSQLITE", connectionName));
         }
        _db->setDatabaseName(_dbPath);
        if (!_db) {
             LOG(("Error: Failed to recreate DeletedMessagesStorage database object."));
             return false;
        }
    }

    if (!_db->isValid()) {
        LOG(("Error: DeletedMessagesStorage database driver not valid. Check Qt SQL plugin availability."));
        return false;
    }

    QFileInfo dbFileInfo(_dbPath);
    QDir dbDir = dbFileInfo.absoluteDir();
    if (!dbDir.exists()) {
        if (!dbDir.mkpath(".")) {
            LOG(("Error: Could not create directory for SQLite database: %1").arg(dbDir.path()));
            return false;
        }
        LOG(("DeletedMessagesStorage: Created directory %1").arg(dbDir.path()));
    }

    if (!_db->isOpen()) {
        if (!_db->open()) {
            LOG(("Error: Failed to open deleted messages database at %1. Error: %2")
                .arg(_dbPath)
                .arg(_db->lastError().text()));
            return false;
        }
        LOG(("DeletedMessagesStorage: Database opened at %1 with connection '%2'").arg(_dbPath).arg(_db->connectionName()));
    }

    return createTableIfNotExists();
}

bool DeletedMessagesStorage::createTableIfNotExists() {
    if (!_db || !_db->isOpen()) {
        LOG(("Error: Database not open in createTableIfNotExists."));
        return false;
    }

    QSqlQuery query(*_db);
    const QString createTableSQL = qsl(R"(
        CREATE TABLE IF NOT EXISTS deleted_messages (
            originalMessageId BIGINT NOT NULL,
            peerId BIGINT NOT NULL,
            globalId_part1 BIGINT,
            globalId_part2 BIGINT,
            date INTEGER NOT NULL,
            deletedDate INTEGER NOT NULL,
            senderId BIGINT,
            flags INTEGER,
            text_content TEXT,
            text_entities TEXT,
            media_info TEXT,
            forward_info TEXT,
            reply_info TEXT,
            topicRootId BIGINT,
            PRIMARY KEY (peerId, originalMessageId)
        )
    )");
    // Changed PK to (peerId, originalMessageId) for better query patterns if peerId is often the first filter.

    if (!query.exec(createTableSQL)) {
        LOG(("Error: Failed to create deleted_messages table. Error: %1. Query: %2")
            .arg(query.lastError().text())
            .arg(createTableSQL));
        return false;
    }
    LOG(("DeletedMessagesStorage: Table 'deleted_messages' checked/created successfully."));
    return true;
}

bool DeletedMessagesStorage::addMessage(const Data::StoredDeletedMessage &message) {
    if (!_db || !_db->isOpen()) {
        LOG(("Error: Database not open in addMessage. Attempting to initialize..."));
        if (!initialize()) {
             LOG(("Error: Failed to re-initialize database in addMessage. Cannot add message."));
             return false;
        }
    }

    QSqlQuery query(*_db);
    query.prepare(qsl(R"(
        INSERT OR REPLACE INTO deleted_messages (
            originalMessageId, peerId, globalId_part1, globalId_part2, date, deletedDate,
            senderId, flags, text_content, text_entities, media_info,
            forward_info, reply_info, topicRootId
        ) VALUES (
            :originalMessageId, :peerId, :globalId_part1, :globalId_part2, :date, :deletedDate,
            :senderId, :flags, :text_content, :text_entities, :media_info,
            :forward_info, :reply_info, :topicRootId
        )
    )"));

    query.bindValue(":originalMessageId", static_cast<qint64>(message.originalMessageId));
    query.bindValue(":peerId", static_cast<qint64>(message.peerId.value));
    query.bindValue(":globalId_part1", static_cast<qint64>(message.globalId.item.channelId.bare));
    query.bindValue(":globalId_part2", static_cast<qint64>(message.globalId.item.msgId.bare));
    query.bindValue(":date", static_cast<qint64>(message.date));
    query.bindValue(":deletedDate", static_cast<qint64>(message.deletedDate));
    query.bindValue(":senderId", static_cast<qint64>(message.senderId.value));
    query.bindValue(":flags", static_cast<int>(message.flags));
    query.bindValue(":text_content", message.text.text);
    query.bindValue(":text_entities", SerializeTextEntities(message.text.entities));
    query.bindValue(":media_info", SerializeMediaList(message.mediaList));
    query.bindValue(":forward_info", SerializeForwardInfo(message.forwardInfo));
    query.bindValue(":reply_info", SerializeReplyInfo(message.replyInfo));
    query.bindValue(":topicRootId", static_cast<qint64>(message.topicRootId));

    if (!query.exec()) {
        LOG(("Error: Failed to insert message into deleted_messages. Error: %1. PeerID: %2, MsgID: %3")
            .arg(query.lastError().text())
            .arg(message.peerId.value)
            .arg(message.originalMessageId));
        return false;
    }
    LOG(("DeletedMessagesStorage: Message added/replaced. PeerID: %1, MsgID: %2")
        .arg(message.peerId.value)
        .arg(message.originalMessageId));
    return true;
}

std::vector<Data::StoredDeletedMessage> DeletedMessagesStorage::getMessagesForPeer(
    Storage::PeerId peerId,
    int limit,
    Storage::MsgId offsetId,
    Storage::TimeId offsetDate) {
    if (!_db || !_db->isOpen()) {
        LOG(("Error: Database not open in getMessagesForPeer."));
        if (!initialize()) { // Attempt to initialize if not open
             LOG(("Error: Failed to initialize DB in getMessagesForPeer."));
             return {};
        }
    }

    std::vector<Data::StoredDeletedMessage> messages;
    QSqlQuery query(*_db);
    QString queryString = qsl("SELECT * FROM deleted_messages WHERE peerId = :peerId ");

    if (offsetId != 0 && offsetDate != 0) { // Both must be provided for pagination
        queryString += qsl("AND (date < :offsetDate OR (date = :offsetDate AND originalMessageId < :offsetId)) ");
    }
    queryString += qsl("ORDER BY date DESC, originalMessageId DESC LIMIT :limit");

    query.prepare(queryString);
    query.bindValue(":peerId", static_cast<qint64>(peerId.value));
    query.bindValue(":limit", limit);
    if (offsetId != 0 && offsetDate != 0) {
        query.bindValue(":offsetDate", static_cast<qint64>(offsetDate));
        query.bindValue(":offsetId", static_cast<qint64>(offsetId));
    }

    if (!query.exec()) {
        LOG(("Error: Failed to retrieve messages for peer %1. Error: %2. Query: %3")
            .arg(peerId.value)
            .arg(query.lastError().text())
            .arg(queryString));
        return {};
    }

    while (query.next()) {
        messages.push_back(MessageFromQuery(query));
    }
    LOG(("DeletedMessagesStorage: Retrieved %1 messages for peer %2.").arg(messages.size()).arg(peerId.value));
    return messages;
}

std::optional<Data::StoredDeletedMessage> DeletedMessagesStorage::getMessage(
    Storage::PeerId peerId,
    Storage::MsgId originalMessageId) {
    if (!_db || !_db->isOpen()) {
        LOG(("Error: Database not open in getMessage."));
         if (!initialize()) {
             LOG(("Error: Failed to initialize DB in getMessage."));
            return std::nullopt;
        }
    }

    QSqlQuery query(*_db);
    query.prepare(qsl("SELECT * FROM deleted_messages WHERE peerId = :peerId AND originalMessageId = :originalMessageId"));
    query.bindValue(":peerId", static_cast<qint64>(peerId.value));
    query.bindValue(":originalMessageId", static_cast<qint64>(originalMessageId));

    if (!query.exec()) {
        LOG(("Error: Failed to retrieve message %1 for peer %2. Error: %3")
            .arg(originalMessageId)
            .arg(peerId.value)
            .arg(query.lastError().text()));
        return std::nullopt;
    }

    if (query.next()) {
        LOG(("DeletedMessagesStorage: Message %1 for peer %2 found.").arg(originalMessageId).arg(peerId.value));
        return MessageFromQuery(query);
    }
    LOG(("DeletedMessagesStorage: Message %1 for peer %2 not found.").arg(originalMessageId).arg(peerId.value));
    return std::nullopt;
}

bool DeletedMessagesStorage::clearMessagesForPeer(Storage::PeerId peerId) {
    if (!_db || !_db->isOpen()) {
        LOG(("Error: Database not open in clearMessagesForPeer."));
        if (!initialize()) {
            LOG(("Error: Failed to initialize DB in clearMessagesForPeer."));
            return false;
        }
    }

    QSqlQuery query(*_db);
    query.prepare(qsl("DELETE FROM deleted_messages WHERE peerId = :peerId"));
    query.bindValue(":peerId", static_cast<qint64>(peerId.value));

    if (!query.exec()) {
        LOG(("Error: Failed to clear messages for peer %1. Error: %2")
            .arg(peerId.value)
            .arg(query.lastError().text()));
        return false;
    }
    LOG(("DeletedMessagesStorage: Cleared messages for peer %1. Rows affected: %2")
        .arg(peerId.value)
        .arg(query.numRowsAffected()));
    return true;
}

bool DeletedMessagesStorage::clearAllMessages() {
    if (!_db || !_db->isOpen()) {
        LOG(("Error: Database not open in clearAllMessages."));
        if (!initialize()) {
            LOG(("Error: Failed to initialize DB in clearAllMessages."));
            return false;
        }
    }

    QSqlQuery query(*_db);
    query.prepare(qsl("DELETE FROM deleted_messages"));

    if (!query.exec()) {
        LOG(("Error: Failed to clear all messages. Error: %1").arg(query.lastError().text()));
        return false;
    }
    LOG(("DeletedMessagesStorage: Cleared all messages. Rows affected: %1").arg(query.numRowsAffected()));
    return true;
}

void DeletedMessagesStorage::close() {
    if (_db && _db->isOpen()) {
        _db->close();
        LOG(("DeletedMessagesStorage: Database connection '%1' closed explicitly.").arg(_db->connectionName()));
    }
}

// bool DeletedMessagesStorage::openDatabase() - This was a helper, initialize() handles opening.

} // namespace Storage
