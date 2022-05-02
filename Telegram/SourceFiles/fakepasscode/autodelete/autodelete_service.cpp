#include "autodelete_service.h"

#include <base/unixtime.h>
#include <crl/crl.h>

#include <apiwrap.h>
#include <core/application.h>
#include <main/main_domain.h>
#include <main/main_account.h>
#include <main/main_session.h>
#include <data/data_session.h>
#include <data/data_histories.h>
#include <data/data_channel.h>
#include <data/data_user.h>
#include <history/history.h>
#include <history/history_item.h>
#include <storage/storage_domain.h>

#include "fakepasscode/log/fake_log.h"
#include "fakepasscode/mtp_holder/crit_api.h"

constexpr int VERSION = 1;

static Main::Account* getAccount(int a) {
    for (const auto &[index, account] : Core::App().domain().accounts()) {
        if (a == index) {
            return account.get();
        }
    }
    return nullptr;
}

static int getAccountIndex(Main::Session* session) {
    for (const auto &[index, account] : session->domain().accounts()) {
        if (account->maybeSession() == session) {
            return index;
        }
    }
    return -1;
}

namespace FakePasscode {

AutoDeleteService::AutoDeleteService(Storage::Domain* owner)
    : owner(owner)
    , timer([this] { tick(); }){
    timer.callEach(1000);
}

void AutoDeleteService::tick() const {
    timeStream.fire(base::unixtime::now());
}

rpl::producer<TimeId> AutoDeleteService::nowTicks() const {
    return timeStream.events_starting_with(base::unixtime::now());
}

rpl::producer<TimeId> AutoDeleteService::nextTicks() const {
    return timeStream.events();
}

void AutoDeleteService::DeleteAll() {
    base::flat_map<int, std::vector<FullMsgId>> remove;
    for (auto& mp : { waitRead, scheduled }) {
        for (auto& [index, messages]: mp) {
            for (auto& [msgId, _]: messages) {
                remove[index].push_back(msgId);
            }
        }
    }
    for (auto& [index, messages] : remove) {
        deleteAll[index] = true;
        if (auto account = getAccount(index)) {
            if (!account->sessionExists()) {
                continue;
            }
            autoDelete(account->maybeSession(), index, messages);
        }
    }
}

void AutoDeleteService::DeleteAll(Main::Session* session) {
    const int index = getAccountIndex(session);
    if (index < 0) {
        FAKE_LOG(qsl("Can't find account index by session"));
        return;
    }

    deleteAll[index] = true;
    std::vector<FullMsgId> remove;
    for (auto& mp : { waitRead, scheduled }) {
        auto it = mp.find(index);
        if (it == mp.end()) {
            continue;
        }
        for (auto& [msgId, _]: it->second) {
            remove.push_back(msgId);
        }
    }

    if (remove.empty()) {
        return;
    }
    autoDelete(session, index, remove);
}

bool AutoDeleteService::deleteImmediately(int index) const {
    if (deleteAll.contains(index)) {
        return true;
    }
    return owner->IsFakeInfinityFlag();
}

void AutoDeleteService::RegisterAutoDeleteMessage(Main::Session* session, uint64 randomId, PeerId peer, TimeId timeout) {
    if (session->domainLocal().IsFake()) {
        //registration of new messages is disabled when fake code is activated
        return;
    }
    const int index = getAccountIndex(session);
    if (index < 0) {
        FAKE_LOG(qsl("Can't find account index by session"));
        return;
    }
    watchSession(session, index);

    registered[index][randomId] = {
        .peer = peer,
        .timeout = timeout,
        .created = base::unixtime::now()
    };
    postponeSave();
}

void AutoDeleteService::UnregisterMessageRandomId(Main::Session* session, uint64 randomId) {
    const int index = getAccountIndex(session);
    if (index < 0) {
        FAKE_LOG(qsl("Can't find account index by session"));
        return;
    }

    auto removed = registered[index].erase(randomId);
    if (removed) {
        postponeSave();
    }
}

void AutoDeleteService::UpdateMessageId(Main::Session *session, uint64 randomId, int64 newMsgId) {
    // if randomId is already registered we should acquire newMsgId even if fake mode is activated

    const int index = getAccountIndex(session);
    if (index < 0) {
        FAKE_LOG(qsl("Can't find account index by session"));
        return;
    }

    auto& account = registered[index];
    auto it = account.find(randomId);
    if (it == account.end()) {
        return;
    }

    watchSession(session, index);

    const TimeId timeout = it->second.timeout;
    const FullMsgId msgId = FullMsgId(it->second.peer, newMsgId);
    waitRead[index][msgId] = timeout;
    postponeSave();
    postponeCall(crl::guard(session, [=]{
        waitUntilRead(session, index, msgId, timeout);
    }));
}

void AutoDeleteService::waitUntilRead(Main::Session* session, int index, FullMsgId msgId, TimeId timeout) {
    messageRead(session, msgId)
        | rpl::filter([=] (bool read) {
            return read || deleteImmediately(index);
        })
        | rpl::take(1)
        | rpl::start_with_next([=](){
            waitRead[index].erase(msgId);
            scheduled[index][msgId] = scheduleDeleteWithTimeout(session, index, msgId, timeout);
            postponeSave();
        }, session->lifetime());
}

rpl::producer<bool> AutoDeleteService::messageRead(Main::Session *session, FullMsgId msgId) const {
    return nowTicks()
        | rpl::start_spawning(session->lifetime())
        | rpl::map([=]{ return isRead(session, msgId); });
}

bool AutoDeleteService::isRead(Main::Session *session, FullMsgId msgId) const {
    //we reimplement HistoryItem::unread, telegram may load not all messages we are interested in

    // Messages from myself are always read, unless scheduled.
    not_null<PeerData*> peer = session->data().peer(msgId.peer);
    if (peer->isSelf()) {
        return true;
    }

    // Outgoing messages in converted chats are always read.
    if (peer->migrateTo()) {
        return true;
    }

    not_null<History*> history = session->data().history(msgId.peer);
    const MsgId readTill = history->outboxReadTillId();
    if (msgId.msg <= readTill) {
        return true;
    }
    if (const auto user = peer->asUser()) {
        if (user->isBot() && !user->isSupport()) {
            return true;
        }
    } else if (const auto channel = peer->asChannel()) {
        if (!channel->isMegagroup()) {
            return true;
        }
    }

    //request history if it is not loaded
    if (!history->lastMessageKnown()) {
        session->data().histories().requestDialogEntry(history);
    }

    return false;
}

TimeId AutoDeleteService::scheduleDeleteWithTimeout(Main::Session* session, int index, FullMsgId msgId, TimeId timeout) {
    const TimeId deadline = base::unixtime::now() + timeout;
    scheduleDeleteWithDeadline(session, index, msgId, deadline);
    return deadline;
}

void AutoDeleteService::scheduleDeleteWithDeadline(Main::Session *session, int index, FullMsgId msgId, TimeId deadline) {
    nowTicks()
        | rpl::filter([=] (TimeId now) {
            return now >= deadline || deleteImmediately(index);
        })
        | rpl::take(1)
        | rpl::start_with_next([=] {
            autoDelete(session, index, {msgId});
        }, session->lifetime());
}

void AutoDeleteService::autoDelete(Main::Session *session, int index, const std::vector<FullMsgId>& messages) {
    //We have to reimplement Histories::deleteMessages as we need to handle fail callbacks

    struct Messages {
        QVector<HistoryItem*> items;
        QVector<MTPint> ids;
        QVector<FullMsgId> messages;
    };
    base::flat_map<History*, Messages> historyItems;
    base::flat_map<PeerData*, Messages> peerItems;
    auto& data = session->data();

    for (FullMsgId msgId : messages) {
        if (const auto item = data.message(msgId)) {
            auto& ref = historyItems[item->history()];
            ref.items.push_back(item);
            ref.messages.push_back(msgId);
        } else {
            not_null<PeerData*> peer = data.peer(msgId.peer);
            auto& ref = peerItems[peer];
            ref.ids.push_back(MTP_int(msgId.msg));
            ref.messages.push_back(msgId);
        }
    }

    auto onDone = [=] (QVector<FullMsgId> msgs) {
        return [=] {
            for (FullMsgId msgId : msgs) {
                waitRead[index].erase(msgId);
                scheduled[index].erase(msgId);
            }
            postponeSave();
        };
    };

    auto onError = [=] (QVector<FullMsgId> msgs) {
        return [=] {
            for (FullMsgId msgId : msgs) {
                scheduleDeleteWithTimeout(session, index, msgId, 5);
            }
        };
    };

    for (auto&[history, grouped] : historyItems) {
        QVector<FullMsgId> msgs = grouped.messages;
        autoDeleteItems(session, history, grouped.items, onDone(msgs), onError(msgs));
    }
    for (auto&[peer, grouped] : peerItems) {
        QVector<FullMsgId> msgs = grouped.messages;
        autoDeleteRaw(session, peer, grouped.ids, onDone(msgs), onError(msgs));
    }

    data.sendHistoryChangeNotifications();
}

void AutoDeleteService::autoDeleteItems(Main::Session* session, History* history, QVector<HistoryItem*> items, Fn<void()> onDone, Fn<void()> onError) {
    using namespace Data;

    QVector<MTPint> messages;
    for (auto item : items) {
        messages.push_back(MTP_int(item->id));
    }

    session->data().histories().sendRequest(history, Histories::RequestType::Delete,
        [=](Fn<void()> finish) -> mtpRequestId {
            return autoDeleteRaw(session, history->peer, messages,
                [=] {
                    finish();
                    history->requestChatListMessage();
                    onDone();
                },
                [=] {
                    finish();
                    onError();
                });
        });

    for (auto item : items) {
        const auto wasLast = (history->lastMessage() == item);
        const auto wasInChats = (history->chatListMessage() == item);
        item->destroy();

        if (wasLast || wasInChats) {
            history->requestChatListMessage();
        }
    }
}

mtpRequestId AutoDeleteService::autoDeleteRaw(Main::Session* session, PeerData* peer, QVector<MTPint> messages, Fn<void()> onDone, Fn<void()> onError) {
    auto ids = MTP_vector(messages);
    auto done = [=](const MTPmessages_AffectedMessages &result) {
        session->api().applyAffectedMessages(peer, result);
        onDone();
    };
    auto error = [=](const MTP::Error &error, const MTP::Response &response) {
        onError();
    };
    if (const auto channel = peer->asChannel()) {
        return FAKE_CRITICAL_REQUEST(session) session->api().request(MTPchannels_DeleteMessages(channel->inputChannel, ids))
            .done(done).fail(error).send();
    } else {
        using Flag = MTPmessages_DeleteMessages::Flag;
        return FAKE_CRITICAL_REQUEST(session) session->api().request(MTPmessages_DeleteMessages(MTP_flags(Flag::f_revoke), ids))
            .done(done).fail(error).send();
    }
}

void AutoDeleteService::watchSession(Main::Session* session, int index) {
    if (watchingSessions[index]) {
        return;
    }
    watchingSessions[index] = true;

    session->account().sessionChanges()
        | rpl::filter([] (Main::Session* s) {
            return s == nullptr;
        })
        | rpl::take(1)
        | rpl::start_with_next([=]{
            onLogout(index);
        }, lifetime);
}

void AutoDeleteService::onLogout(int index) {
    //At this point session is already destroyed. So we can't delete scheduled messages. Just cleanup
    //DeleteAll operation is performed by Application::logoutWithChecksAndClear
    registered.erase(index);
    waitRead.erase(index);
    scheduled.erase(index);
    watchingSessions.erase(index);
    postponeSave();
}

void AutoDeleteService::save() {
    if (dirty) {
        dirty = false;
        owner->writeAccounts();
    }
}

void AutoDeleteService::postponeSave() {
    dirty = true;
    postponeCall([=]{ save(); });
}

QByteArray AutoDeleteService::Serialize() const {
    struct AccountData {
        int index;
        QByteArray data;
    };
    std::vector<AccountData> data;
    for (const auto &[index, _] : Core::App().domain().accounts()) {
        QByteArray accData = serialize(index);
        if (!accData.isEmpty()) {
            data.push_back({index, accData});
        }
    }
    QByteArray serialized;
    if (data.empty()) {
        return serialized;
    }

    QDataStream stream(&serialized, QIODevice::WriteOnly);
    stream << VERSION;
    stream << int(data.size());
    for (const auto& accData : data) {
        stream << accData.index << accData.data;
    }
    return serialized;
}

QByteArray AutoDeleteService::serialize(int index) const {
    QByteArray data;
    RandomIdsMap reg;
    DeletionMap waiting;
    DeletionMap sched;

    constexpr TimeId day = 24*60*60;
    TimeId old = base::unixtime::now() - 2*day;
    if (auto it = registered.find(index); it != registered.end()) {
        reg = it->second
            | ranges::view::filter([ old ] (auto& pair) {
                return pair.second.created > old;
            })
            | ranges::to<RandomIdsMap>();
    }
    if (auto it = waitRead.find(index); it != waitRead.end()) {
        waiting = it->second;
    }
    if (auto it = scheduled.find(index); it != scheduled.end()) {
        sched = it->second;
    }

    if (reg.empty() && waiting.empty() && sched.empty()) {
        return data;
    }

    QDataStream stream(&data, QIODevice::WriteOnly);
    stream << int(reg.size());
    for (const auto &[randomId, rec] : reg) {
        stream << randomId << rec.peer.value << rec.timeout << rec.created;
    }
    stream << int(waiting.size());
    for (const auto &[messageId, timeout] : waiting) {
        stream << messageId.peer.value << messageId.msg.bare << timeout;
    }
    stream << int(sched.size());
    for (const auto &[messageId, deadline] : sched) {
        stream << messageId.peer.value << messageId.msg.bare << deadline;
    }

    return data;
}

void AutoDeleteService::DeSerialize(QByteArray data) {
    if (data.isEmpty()) {
        return;
    }

    QDataStream stream(&data, QIODevice::ReadOnly);
    int size = -1, version = -1;
    stream >> version >> size;
    for (int i = 0; i < size; i++) {
        int index;
        QByteArray accData;
        stream >> index >> accData;
        deserialize(index, accData);
    }
}

void AutoDeleteService::deserialize(int index, QByteArray data) {
    if (data.isEmpty()) {
        return;
    }
    auto* account = getAccount(index);
    if (account == nullptr) {
        return;
    }
    if (!account->sessionExists()) {
        return;
    }
    auto* session = account->maybeSession();

    QDataStream stream(&data, QIODevice::ReadOnly);
    int size = -1;
    stream >> size;
    for (int i = 0; i < size; ++i) {
        uint64 randomId = 0;
        RandomIdData rec = {0};
        stream >> randomId >> rec.peer.value >> rec.timeout >> rec.created;
        registered[index][randomId] = rec;
    }
    stream >> size;
    for (int i = 0; i < size; ++i) {
        FullMsgId msgId;
        TimeId timeout;
        stream >> msgId.peer.value >> msgId.msg.bare >> timeout;
        waitRead[index][msgId] = timeout;
        postponeCall(crl::guard(session, [=]{
            waitUntilRead(session, index, msgId, timeout);
        }));
    }
    stream >> size;
    for (int i = 0; i < size; ++i) {
        FullMsgId msgId;
        TimeId deadline;
        stream >> msgId.peer.value >> msgId.msg.bare >> deadline;
        scheduled[index][msgId] = deadline;
        postponeCall(crl::guard(session, [=]{
            scheduleDeleteWithDeadline(session, index, msgId, deadline);
        }));
    }
}

template<typename Fn>
void AutoDeleteService::postponeCall(Fn&& fn) {
    Core::App().postponeCall(crl::guard(this, std::forward<Fn>(fn)));
}

}
