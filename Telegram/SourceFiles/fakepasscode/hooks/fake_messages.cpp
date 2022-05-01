#include "fake_messages.h"

#include <base/random.h>
#include <main/main_session.h>
#include <storage/storage_domain.h>

#include "../autodelete/autodelete_service.h"

namespace FakePasscode {

void RegisterMessageRandomId(Main::Session* session, uint64 randomId, PeerId peer, Api::SendOptions options) {
    if (options.ptgAutoDelete) {
        if (auto autoDelete = session->domainLocal().GetAutoDelete()) {
            autoDelete->RegisterAutoDeleteMessage(session, randomId, peer, *options.ptgAutoDelete);
        }
    }
}

uint64 RegisterMessageRandomId(Main::Session* session, PeerId peer, Api::SendOptions options) {
    const uint64 randomId = base::RandomValue<uint64>();
    RegisterMessageRandomId(session, randomId, peer, options);
    return randomId;
}

void UnregisterMessageRandomId(Main::Session* session, uint64 randomId) {
    if (auto autoDelete = session->domainLocal().GetAutoDelete()) {
        autoDelete->UnregisterMessageRandomId(session, randomId);
    }
}

void UpdateMessageId(Main::Session* session, uint64 randomId, int64 newMsgId) {
    if (auto autoDelete = session->domainLocal().GetAutoDelete()) {
        autoDelete->UpdateMessageId(session, randomId, newMsgId);
    }
}

}