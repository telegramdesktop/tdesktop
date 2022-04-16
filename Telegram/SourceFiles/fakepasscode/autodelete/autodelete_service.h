#ifndef TELEGRAM_AUTODELETE_SERVICE_H
#define TELEGRAM_AUTODELETE_SERVICE_H

#include <base/basic_types.h>
#include <base/weak_ptr.h>
#include <base/flat_map.h>
#include <base/timer.h>
#include <data/data_msg_id.h>

#include <map>
#include <vector>
#include <rpl/rpl.h>

namespace Main {
class Session;
}

namespace Data{
class Session;
}

namespace Storage {
class Domain;
}

class History;
class HistoryItem;

namespace FakePasscode {

class AutoDeleteService final : public base::has_weak_ptr {
public:
    AutoDeleteService(Storage::Domain* owner);

    QByteArray Serialize() const;
    void DeSerialize(QByteArray data);

    void RegisterAutoDeleteMessage(Main::Session* session, uint64 randomId, PeerId peer, TimeId timeout);
    void UnregisterMessageRandomId(Main::Session* session, uint64 randomId);
    void UpdateMessageId(Main::Session* session, uint64 randomId, int64 newMsgId);

    void DeleteAll();
    void DeleteAll(Main::Session* session);

private:
    struct RandomIdData {
        PeerId peer;
        TimeId timeout;
        TimeId created;
    };
    using RandomIdsMap = std::unordered_map<uint64, RandomIdData>;
    using DeletionMap = std::map<FullMsgId, TimeId>;
    using AccountMap = base::flat_map<int, DeletionMap>;

    base::flat_map<int, RandomIdsMap> registered;
    AccountMap waitRead;
    AccountMap scheduled;
    bool dirty = false;

    Storage::Domain* owner;
    base::flat_map<int, bool> watchingSessions;
    base::flat_map<int, bool> deleteAll;
    base::Timer timer;
    rpl::event_stream<TimeId> timeStream;
    rpl::lifetime lifetime;

    void tick() const;
    rpl::producer<TimeId> nowTicks() const;
    rpl::producer<TimeId> nextTicks() const;
    rpl::producer<bool> messageRead(Main::Session* session, FullMsgId msgId) const;
    bool isRead(Main::Session* session, FullMsgId msgId) const;
    bool deleteImmediately(int index) const;

    void waitUntilRead(Main::Session* session, int index, FullMsgId msgId, TimeId timeout);
    TimeId scheduleDeleteWithTimeout(Main::Session* session, int index, FullMsgId msgId, TimeId timeout);
    void scheduleDeleteWithDeadline(Main::Session* session, int index, FullMsgId msgId, TimeId deadline);

    void autoDelete(Main::Session* session, int index, const std::vector<FullMsgId>& messages);
    void autoDeleteItems(Main::Session* session, History* history, QVector<HistoryItem*> items, Fn<void()> onDone, Fn<void()> onError);
    mtpRequestId autoDeleteRaw(Main::Session* session, PeerData* peer, QVector<MTPint> messages, Fn<void()> onDone, Fn<void()> onError);

    void watchSession(Main::Session* session, int index);
    void onLogout(int index);

    void save();
    void postponeSave();
    QByteArray serialize(int index) const;
    void deserialize(int index, QByteArray data);

    template<typename Fn>
    void postponeCall(Fn&& fn);
};

}

#endif //TELEGRAM_AUTODELETE_SERVICE_H
