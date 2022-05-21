#ifndef TELEGRAM_MULTIACCOUNT_ACTION_H
#define TELEGRAM_MULTIACCOUNT_ACTION_H

#include "action.h"

#include "base/flat_set.h"

namespace Main {
    class Account;
}

namespace FakePasscode {
    class LogoutSubscribedAction : public Action {
    public:
        void Prepare() override;

    protected:
        rpl::lifetime sub_lifetime_;
        rpl::lifetime lifetime_;

        virtual void SubscribeOnLoggingOut();
        virtual void OnAccountLoggedOut(qint32 index) = 0;
    };

    template<typename Data>
    class MultiAccountAction : public LogoutSubscribedAction {
    public:
        MultiAccountAction() = default;
        explicit MultiAccountAction(QByteArray inner_data);
        MultiAccountAction(base::flat_map<qint32, Data> data);

        void Prepare() override;
        void Execute() override;
        virtual void ExecuteAccountAction(int index, Main::Account* account, const Data& action) = 0;

        void AddAction(qint32 index, const Data& data);
        void AddAction(qint32 index, Data&& data);

        void UpdateAction(qint32 index, const Data& data);
        void UpdateAction(qint32 index, Data&& data);

        const Data& GetData(qint32 index) const;

        void UpdateOrAddAction(qint32 index, const Data& data);
        void UpdateOrAddAction(qint32 index, Data&& data);

        bool HasAction(qint32 index) const;
        void RemoveAction(qint32 index);

        QByteArray Serialize() const override;

    protected:
        base::flat_map<qint32, Data> index_actions_;
        base::flat_set<qint32> executionInProgress_;
        base::has_weak_ptr guard_;

        void OnAccountLoggedOut(qint32 index) override;
        template<typename Fn>
        void PostponeCall(Fn&& fn);

        static const Data kEmptyData;
    };

    struct SelectPeersData {
        base::flat_set<quint64> peer_ids;
    };

    struct ToggleAction {};

    template<class Stream>
    Stream& operator<<(Stream& stream, ToggleAction) {
        return stream;
    }

    template<class Stream>
    Stream& operator>>(Stream& stream, ToggleAction) {
        return stream;
    }

    template<class Stream>
    Stream& operator<<(Stream& stream, const SelectPeersData& data) {
        stream << quint64(data.peer_ids.size());
        for (quint64 id : data.peer_ids) {
            stream << id;
        }
        return stream;
    }

    template<class Stream>
    Stream& operator>>(Stream& stream, SelectPeersData& data) {
        quint64 size;
        stream >> size;
        data.peer_ids.reserve(size);
        for (qint64 i = 0; i < size; ++i) {
            qint64 id;
            stream >> id;
            data.peer_ids.insert(id);
        }
        return stream;
    }
}


#endif //TELEGRAM_MULTIACCOUNT_ACTION_H
