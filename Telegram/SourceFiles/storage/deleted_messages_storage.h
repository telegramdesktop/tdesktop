#pragma once

#include "data/stored_deleted_message.h" // The structure defined in the previous step
#include "base/thread_safe_object_ptr.h" // For thread-safe singleton or member
#include "storage/storage_facade_fwd.h" // For Storage::Id

#include <QObject> // For Q_OBJECT macro if signals/slots are needed later
#include <QString>
#include <vector>
#include <optional>
#include <memory> // For std::unique_ptr

// Forward declare QtSql classes to attempt using QtSql module first
class QSqlDatabase;
class QSqlQuery;

namespace Storage {

class DeletedMessagesStorage : public QObject { // Inherit QObject if signals might be useful
    Q_OBJECT

public:
    explicit DeletedMessagesStorage(const QString &basePath); // basePath for tdata
    ~DeletedMessagesStorage();

    // Initializes the database, opens connection, creates table if not exists.
    // Returns true on success, false on failure.
    bool initialize();

    // Adds a deleted message to the database.
    // Returns true on success, false on failure.
    bool addMessage(const Data::StoredDeletedMessage &message);

    // Retrieves messages for a specific peer.
    // Implement pagination later (offset, limit).
    std::vector<Data::StoredDeletedMessage> getMessagesForPeer(
        Storage::PeerId peerId,
        int limit = 100,
        Storage::MsgId offsetId = 0, // To fetch messages older than this ID
        Storage::TimeId offsetDate = 0);

    // Retrieves a specific message by its original ID and peer ID.
    std::optional<Data::StoredDeletedMessage> getMessage(
        Storage::PeerId peerId,
        Storage::MsgId originalMessageId);

    // Clears all messages for a specific peer.
    bool clearMessagesForPeer(Storage::PeerId peerId);

    // Clears all deleted messages from the database.
    bool clearAllMessages();

    // Closes the database connection.
    void close();

private:
    // Private methods for database operations
    bool openDatabase();
    bool createTableIfNotExists();
    bool executeQuery(QSqlQuery &query); // Helper for executing queries

    // Converts a StoredDeletedMessage to/from QSqlQuery (implementation in .cpp)
    static Data::StoredDeletedMessage messageFromQuery(QSqlQuery &query);
    static void bindMessageToQuery(const Data::StoredDeletedMessage &message, QSqlQuery &query, Main::Session *session = nullptr); // Session might be needed for deserialization context

    QString _dbPath;
    std::unique_ptr<QSqlDatabase> _db; // Using unique_ptr for RAII

    // TODO: Add encryption key management if needed, possibly derived from SettingsKey
    // QByteArray _encryptionKey;
};

// Potentially a singleton accessor if this storage is to be globally accessible
// class Instance {
// public:
//     static DeletedMessagesStorage *Get(Main::Session *session); // Or pass base path
// };

} // namespace Storage
