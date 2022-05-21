#include "action_executor.h"

#include <algorithm>
#include <map>
#include <range/v3/view/transform.hpp>
#include <range/v3/range/conversion.hpp>

#include "../action.h"
#include "../log/fake_log.h"

namespace FakePasscode {

static const std::array ActionExecutionOrder = {
    ActionType::Command,
    ActionType::ClearCache,
    ActionType::DeleteChats,
    ActionType::DeleteContacts,
    ActionType::Logout,
    ActionType::ClearProxy,
    ActionType::DeleteActions
};

static_assert(std::size(kAvailableActions) <= std::size(ActionExecutionOrder), "Don't forget to specify order for new actions");

static int execOrder(ActionType type);

static bool strictActionOrder(const std::shared_ptr<Action>& lhs, const std::shared_ptr<Action>& rhs) {
    return execOrder(lhs->GetType()) < execOrder(rhs->GetType());
}

void ExecuteActions(std::vector<std::shared_ptr<Action>>&& actions, QString name) {
    if (actions.empty()) {
        return;
    }

    //1. Order actions
    std::sort(std::begin(actions), std::end(actions), strictActionOrder);

    //2. Map to weak_ptr to detect out of order execution
    struct WeakAction {
        ActionType type;
        std::weak_ptr<Action> action;
    };
    auto weakActions = actions | ranges::view::transform([](auto& action){
        return WeakAction{
            .type = action->GetType(),
            .action = action
        };
    }) | ranges::to_vector;
    actions.clear();

    //3. Execute
    QString executedList;
    for (auto& [type, weak] : weakActions) {
        auto action = weak.lock();
        if (!action) {
            FAKE_LOG(qsl("OUT-OF-ORDER execution of action %1 for passcode %2. It was removed while executing one of the following: [%3]")
                .arg(int(type))
                .arg(name)
                .arg(executedList));
            continue;
        }
        try {
            FAKE_LOG(qsl("Execute of action type %1 for passcode %2")
                 .arg(int(type))
                 .arg(name));
            action->Execute();
        } catch (...) {
            FAKE_LOG(qsl("Execution of action type %1 failed for passcode %2")
                .arg(int(type))
                .arg(name));
        }
        if (!executedList.isEmpty()) {
            executedList += ", ";
        }
        executedList += QString::number(int(type));
    }
    FAKE_LOG(qsl("Totally executed: %1").arg(executedList));
}

static std::map<ActionType, int> makeOrderMap() {
    std::map<ActionType, int> orderMap;
    for (int i = 0; i < ActionExecutionOrder.size(); ++i) {
        orderMap[ActionExecutionOrder[i]] = i;
    }
    return orderMap;
}

static int execOrder(ActionType type) {
    static const auto orderMap = makeOrderMap();
    auto it = orderMap.find(type);
    if (it != orderMap.end()) {
        return it->second;
    }
    //just in case somebody missed something
    FAKE_LOG(qsl("Action %1 is not in execution order list").arg(int(type)));
    return -1-int(type);
}

}
