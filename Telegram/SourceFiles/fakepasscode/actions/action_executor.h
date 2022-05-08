#ifndef TELEGRAM_ACTION_EXECUTOR_H
#define TELEGRAM_ACTION_EXECUTOR_H

#include <memory>
#include <vector>

namespace FakePasscode {

class Action;

void ExecuteActions(std::vector<std::shared_ptr<Action>>&& actions, QString name);

}

#endif //TELEGRAM_ACTION_EXECUTOR_H
