#ifndef TELEGRAM_FAKE_MESSAGES_H
#define TELEGRAM_FAKE_MESSAGES_H

#include <base/basic_types.h>
#include <api/api_common.h>
#include <data/data_msg_id.h>

namespace Main {
class Session;
}

namespace FakePasscode {

void RegisterMessageRandomId(Main::Session* session, uint64 randomId, PeerId peer, Api::SendOptions options);
uint64 RegisterMessageRandomId(Main::Session* session, PeerId peer, Api::SendOptions options);

void UnregisterMessageRandomId(Main::Session* session, uint64 randomId);

void UpdateMessageId(Main::Session* session, uint64 randomId, int64 newMsgId);

};

#endif //TELEGRAM_FAKE_MESSAGES_H
