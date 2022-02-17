#ifndef TELEGRAM_MULTIACCOUNT_ACTION_H
#define TELEGRAM_MULTIACCOUNT_ACTION_H

#include "action.h"

namespace Main {
    class Account;
}

namespace FakePasscode {
    class LogoutSubscribedAction : public Action {
    public:
        void Prepare() override;

    protected:
        rpl::lifetime sub_lifetime_;

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
        virtual void ExecuteAccountAction(int index, const std::unique_ptr<Main::Account>& account, const Data& action) = 0;

        bool HasAction(qint32 index) const;
        void RemoveAction(qint32 index);
        template<typename T>
        void AddAction(qint32 index, T&& t) {
            logAddAction(index);
            index_actions_.insert_or_assign(index, std::forward<T>(t));
            SubscribeOnLoggingOut();
        }

        QByteArray Serialize() const override;

    protected:
        base::flat_map<qint32, Data> index_actions_;

        void OnAccountLoggedOut(qint32 index) override;

    private:
        void logAddAction(qint32 index);
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
}


#endif //TELEGRAM_MULTIACCOUNT_ACTION_H
