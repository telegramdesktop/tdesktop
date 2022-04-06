#include "delete_actions.h"

#include "core/application.h"
#include "main/main_session.h"
#include "main/main_domain.h"
#include "main/main_account.h"
#include "data/data_session.h"
#include "storage/storage_facade.h"
#include "storage/storage_account.h"
#include "core/file_utilities.h"
#include "data/data_user.h"
#include "fakepasscode/log/fake_log.h"
#include "storage/storage_domain.h"

void FakePasscode::DeleteActions::Execute() {
    Expects(Core::App().maybeActiveSession() != nullptr);

    const auto session = Core::App().maybeActiveSession();
    const qint32 current_passcode_idx = session->domainLocal().GetFakePasscodeIndex();
    session->domainLocal().ClearActions(current_passcode_idx);
}

QByteArray FakePasscode::DeleteActions::Serialize() const {
    QByteArray result;
    QDataStream stream(&result, QIODevice::ReadWrite);
    stream << static_cast<qint32>(ActionType::DeleteActions);
    return result;
}

FakePasscode::ActionType FakePasscode::DeleteActions::GetType() const {
    return ActionType::DeleteActions;
}
