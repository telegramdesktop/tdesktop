/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/core_types.h"

namespace MTP {
namespace {

uint32 CountPaddingPrimesCount(uint32 requestSize, bool extended, bool old) {
	if (old) {
		return ((8 + requestSize) & 0x03)
			? (4 - ((8 + requestSize) & 0x03))
			: 0;
	}
	auto result = ((8 + requestSize) & 0x03)
		? (4 - ((8 + requestSize) & 0x03))
		: 0;

	// At least 12 bytes of random padding.
	if (result < 3) {
		result += 4;
	}

	if (extended) {
		// Some more random padding.
		result += ((rand_value<uchar>() & 0x0F) << 2);
	}

	return result;
}

} // namespace

SecureRequest::SecureRequest(const details::SecureRequestCreateTag &tag)
: _data(std::make_shared<SecureRequestData>(tag)) {
}

SecureRequest SecureRequest::Prepare(uint32 size, uint32 reserveSize) {
	const auto finalSize = std::max(size, reserveSize);

	auto result = SecureRequest(details::SecureRequestCreateTag{});
	result->reserve(kMessageBodyPosition + finalSize);
	result->resize(kMessageBodyPosition);
	result->back() = (size << 2);
	result->msDate = crl::now(); // > 0 - can send without container
	return result;
}

SecureRequestData *SecureRequest::operator->() const {
	Expects(_data != nullptr);

	return _data.get();
}

SecureRequestData &SecureRequest::operator*() const {
	Expects(_data != nullptr);

	return *_data;
}

SecureRequest::operator bool() const {
	return (_data != nullptr);
}

void SecureRequest::setMsgId(mtpMsgId msgId) {
	Expects(_data != nullptr);

	memcpy(_data->data() + kMessageIdPosition, &msgId, sizeof(mtpMsgId));
}

mtpMsgId SecureRequest::getMsgId() const {
	Expects(_data != nullptr);

	return *(mtpMsgId*)(_data->constData() + kMessageIdPosition);
}

void SecureRequest::setSeqNo(uint32 seqNo) {
	Expects(_data != nullptr);

	(*_data)[kSeqNoPosition] = mtpPrime(seqNo);
}

uint32 SecureRequest::getSeqNo() const {
	Expects(_data != nullptr);

	return uint32((*_data)[kSeqNoPosition]);
}

void SecureRequest::addPadding(bool extended, bool old) {
	Expects(_data != nullptr);

	if (_data->size() <= kMessageBodyPosition) {
		return;
	}

	const auto requestSize = (tl::count_length(*this) >> 2);
	const auto padding = CountPaddingPrimesCount(requestSize, extended, old);
	const auto fullSize = kMessageBodyPosition + requestSize + padding;
	if (uint32(_data->size()) != fullSize) {
		_data->resize(fullSize);
		if (padding > 0) {
			memset_rand(
				_data->data() + (fullSize - padding),
				padding * sizeof(mtpPrime));
		}
	}
}

uint32 SecureRequest::messageSize() const {
	Expects(_data != nullptr);

	if (_data->size() <= kMessageBodyPosition) {
		return 0;
	}
	const auto ints = (tl::count_length(*this) >> 2);
	return kMessageIdInts + kSeqNoInts + kMessageLengthInts + ints;
}

bool SecureRequest::isSentContainer() const {
	Expects(_data != nullptr);

	if (_data->size() <= kMessageBodyPosition) {
		return false;
	}
	return (!_data->msDate && !getSeqNo()); // msDate = 0, seqNo = 0
}

bool SecureRequest::isStateRequest() const {
	Expects(_data != nullptr);

	if (_data->size() <= kMessageBodyPosition) {
		return false;
	}
	const auto type = mtpTypeId((*_data)[kMessageBodyPosition]);
	return (type == mtpc_msgs_state_req);
}

bool SecureRequest::needAck() const {
	Expects(_data != nullptr);

	if (_data->size() <= kMessageBodyPosition) {
		return false;
	}
	const auto type = mtpTypeId((*_data)[kMessageBodyPosition]);
	switch (type) {
	case mtpc_msg_container:
	case mtpc_msgs_ack:
	case mtpc_http_wait:
	case mtpc_bad_msg_notification:
	case mtpc_msgs_all_info:
	case mtpc_msgs_state_info:
	case mtpc_msg_detailed_info:
	case mtpc_msg_new_detailed_info:
		return false;
	}
	return true;
}

size_t SecureRequest::sizeInBytes() const {
	return (_data && _data->size() > kMessageBodyPosition)
		? (*_data)[kMessageLengthPosition]
		: 0;
}

const void *SecureRequest::dataInBytes() const {
	return (_data && _data->size() > kMessageBodyPosition)
		? (_data->constData() + kMessageBodyPosition)
		: nullptr;
}

} // namespace MTP
