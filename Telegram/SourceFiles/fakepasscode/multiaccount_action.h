#ifndef TELEGRAM_MULTIACCOUNT_ACTION_H
#define TELEGRAM_MULTIACCOUNT_ACTION_H

#include "action.h"

#include "core/application.h"
#include "main/main_account.h"
#include "main/main_domain.h"

namespace FakePasscode {
    template<typename Data>
    class MultiAccountAction : public Action {
    public:
        MultiAccountAction() = default;

        explicit MultiAccountAction(QByteArray inner_data) {
            if (!inner_data.isEmpty()) {
                QDataStream stream(&inner_data, QIODevice::ReadOnly);
                while (!stream.atEnd()) {
                    qint32 index;
                    Data action;
                    stream >> index >> action;
                    index_actions_[index] = action;
                }
            }
        }

        MultiAccountAction(base::flat_map<qint32, Data> data) : index_actions_(std::move(data)) {}

        bool HasAction(qint32 index) const {
            return index_actions_.contains(index);
        }

        void RemoveAction(qint32 index) {
            index_actions_.erase(index);
        }

        template<typename T>
        void AddAction(qint32 index, T&& t) {
            index_actions_.insert_or_assign(index, std::forward<T>(t));
        }

        void Prepare() override {
            SubscribeOnLoggingOut();
        }

        void Execute() override {
            for (const auto &[index, account] : Core::App().domain().accounts()) {
                if (const auto it = index_actions_.find(index); it != index_actions_.end()) {
                    ExecuteAccountAction(index, account, it->second);
                }
            }
        }

        virtual void ExecuteAccountAction(int index, const std::unique_ptr<Main::Account>& account, const Data& action) = 0;

        QByteArray Serialize() const override {
            if (index_actions_.empty()) {
                return {};
            }

            QByteArray result;
            QDataStream stream(&result, QIODevice::ReadWrite);
            stream << static_cast<qint32>(GetType());
            QByteArray inner_data;
            QDataStream inner_stream(&inner_data, QIODevice::ReadWrite);
            for (const auto&[index, action] : index_actions_) {
                inner_stream << index << action;
            }
            stream << inner_data;
            return result;
        }

    protected:
        base::flat_map<qint32, Data> index_actions_;
        rpl::lifetime lifetime_;

        virtual void SubscribeOnLoggingOut() {
            for (const auto&[index, account] : Core::App().domain().accounts()) {
                account->sessionChanges()
                    | rpl::filter([](const Main::Session* session) -> bool {
                            return session == nullptr;
                        })
                    | rpl::take(1)
                    | rpl::start_with_next([index = index, this] (const Main::Session*) {
                        OnAccountLoggedOut(index);
                    }, lifetime_);
            }
        }

        virtual void OnAccountLoggedOut(qint32 index) {
            RemoveAction(index);
        }
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
