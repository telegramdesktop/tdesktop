/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/details/mtproto_received_ids_manager.h"

namespace MTP::details {

ReceivedIdsManager::Result ReceivedIdsManager::registerMsgId(
		mtpMsgId msgId,
		bool needAck) {
	const auto i = _idsNeedAck.find(msgId);
	if (i != _idsNeedAck.end()) {
		MTP_LOG(-1, ("No need to handle - %1 already is in map").arg(msgId));
		return Result::Duplicate;
	} else if (_idsNeedAck.size() < kIdsBufferSize || msgId > min()) {
		_idsNeedAck.emplace(msgId, needAck);
		return Result::Success;
	}
	MTP_LOG(-1, ("Reset on too old - %1 < min = %2").arg(msgId).arg(min()));
	return Result::TooOld;
}

mtpMsgId ReceivedIdsManager::min() const {
	return _idsNeedAck.empty() ? 0 : _idsNeedAck.begin()->first;
}

mtpMsgId ReceivedIdsManager::max() const {
	auto end = _idsNeedAck.end();
	return _idsNeedAck.empty() ? 0 : (--end)->first;
}

ReceivedIdsManager::State ReceivedIdsManager::lookup(mtpMsgId msgId) const {
	auto i = _idsNeedAck.find(msgId);
	if (i == _idsNeedAck.end()) {
		return State::NotFound;
	}
	return i->second ? State::NeedsAck : State::NoAckNeeded;
}

void ReceivedIdsManager::shrink() {
	auto size = _idsNeedAck.size();
	while (size-- > kIdsBufferSize) {
		_idsNeedAck.erase(_idsNeedAck.begin());
	}
}

void ReceivedIdsManager::clear() {
	_idsNeedAck.clear();
}

} // namespace MTP::details
