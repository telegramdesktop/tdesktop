/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_separate_id.h"

#include "data/data_folder.h"
#include "data/data_peer.h"
#include "data/data_saved_messages.h"
#include "data/data_session.h"
#include "data/data_thread.h"
#include "history/history.h"
#include "main/main_account.h"
#include "main/main_session.h"

namespace Window {

SeparateId::SeparateId(std::nullptr_t) {
}

SeparateId::SeparateId(not_null<Main::Account*> account)
: account(account) {
}

SeparateId::SeparateId(SeparateType type, not_null<Main::Session*> session)
: type(type)
, account(&session->account()) {
}

SeparateId::SeparateId(SeparateType type, not_null<Data::Thread*> thread)
: type(type)
, account(&thread->session().account())
, thread(thread) {
}

SeparateId::SeparateId(not_null<Data::Thread*> thread)
: SeparateId(SeparateType::Chat, thread) {
}

SeparateId::SeparateId(not_null<PeerData*> peer)
: SeparateId(SeparateType::Chat, peer->owner().history(peer)) {
}

SeparateId::SeparateId(SeparateSharedMedia data)
: type(SeparateType::SharedMedia)
, sharedMedia(data.type)
, account(&data.peer->session().account())
, sharedMediaDataPeer(data.peer)
, sharedMediaDataTopicRootId(data.topicRootId) {
}

bool SeparateId::primary() const {
	return (type == SeparateType::Primary);
}

Data::Thread *SeparateId::chat() const {
	return (type == SeparateType::Chat) ? thread : nullptr;
}

Data::Forum *SeparateId::forum() const {
	return (type == SeparateType::Forum) ? thread->asForum() : nullptr;
}

Data::Folder *SeparateId::folder() const {
	return (type == SeparateType::Archive)
		? account->session().data().folder(Data::Folder::kId).get()
		: nullptr;
}

Data::SavedSublist *SeparateId::sublist() const {
	return (type == SeparateType::SavedSublist)
		? thread->owner().savedMessages().sublist(thread->peer()).get()
		: nullptr;
}

bool SeparateId::hasChatsList() const {
	return (type == SeparateType::Primary)
		|| (type == SeparateType::Archive)
		|| (type == SeparateType::Forum);
}

PeerData *SeparateId::sharedMediaPeer() const {
	return (type == SeparateType::SharedMedia)
		? sharedMediaDataPeer
		: nullptr;
}

MsgId SeparateId::sharedMediaTopicRootId() const {
	return (type == SeparateType::SharedMedia)
		? sharedMediaDataTopicRootId
		: MsgId();
}

} // namespace Window
