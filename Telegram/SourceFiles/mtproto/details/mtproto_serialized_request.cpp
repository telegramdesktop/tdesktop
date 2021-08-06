/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/details/mtproto_serialized_request.h"

#include "base/openssl_help.h"

namespace MTP::details {
namespace {

uint32 CountPaddingPrimesCount(
		uint32 requestSize,
		bool forAuthKeyInner) {
	if (forAuthKeyInner) {
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

	// Some more random padding.
	return result + ((openssl::RandomValue<uchar>() & 0x0F) << 2);
}

} // namespace

SerializedRequest::SerializedRequest(const RequestConstructHider::Tag &tag)
: _data(std::make_shared<RequestData>(tag)) {
}

SerializedRequest SerializedRequest::Prepare(
		uint32 size,
		uint32 reserveSize) {
	Expects(size > 0);

	const auto finalSize = std::max(size, reserveSize);

	auto result = SerializedRequest(RequestConstructHider::Tag{});
	result->reserve(kMessageBodyPosition + finalSize);
	result->resize(kMessageBodyPosition);
	result->back() = (size << 2);
	result->lastSentTime = crl::now();
	return result;
}

RequestData *SerializedRequest::operator->() const {
	Expects(_data != nullptr);

	return _data.get();
}

RequestData &SerializedRequest::operator*() const {
	Expects(_data != nullptr);

	return *_data;
}

SerializedRequest::operator bool() const {
	return (_data != nullptr);
}

void SerializedRequest::setMsgId(mtpMsgId msgId) {
	Expects(_data != nullptr);
	Expects(_data->size() > kMessageBodyPosition);

	memcpy(_data->data() + kMessageIdPosition, &msgId, sizeof(mtpMsgId));
}

mtpMsgId SerializedRequest::getMsgId() const {
	Expects(_data != nullptr);
	Expects(_data->size() > kMessageBodyPosition);

	return *(mtpMsgId*)(_data->constData() + kMessageIdPosition);
}

void SerializedRequest::setSeqNo(uint32 seqNo) {
	Expects(_data != nullptr);
	Expects(_data->size() > kMessageBodyPosition);

	(*_data)[kSeqNoPosition] = mtpPrime(seqNo);
}

uint32 SerializedRequest::getSeqNo() const {
	Expects(_data != nullptr);
	Expects(_data->size() > kMessageBodyPosition);

	return uint32((*_data)[kSeqNoPosition]);
}

void SerializedRequest::addPadding(bool forAuthKeyInner) {
	Expects(_data != nullptr);
	Expects(_data->size() > kMessageBodyPosition);

	const auto requestSize = (tl::count_length(*this) >> 2);
	const auto padding = CountPaddingPrimesCount(
		requestSize,
		forAuthKeyInner);
	const auto fullSize = kMessageBodyPosition + requestSize + padding;
	if (uint32(_data->size()) != fullSize) {
		_data->resize(fullSize);
		if (padding > 0) {
			bytes::set_random(bytes::make_span(*_data).subspan(
				(fullSize - padding) * sizeof(mtpPrime)));
		}
	}
}

uint32 SerializedRequest::messageSize() const {
	Expects(_data != nullptr);
	Expects(_data->size() > kMessageBodyPosition);

	const auto ints = (tl::count_length(*this) >> 2);
	return kMessageIdInts + kSeqNoInts + kMessageLengthInts + ints;
}

bool SerializedRequest::needAck() const {
	Expects(_data != nullptr);
	Expects(_data->size() > kMessageBodyPosition);

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

size_t SerializedRequest::sizeInBytes() const {
	Expects(!_data || _data->size() > kMessageBodyPosition);
	return _data ? (*_data)[kMessageLengthPosition] : 0;
}

const void *SerializedRequest::dataInBytes() const {
	Expects(!_data || _data->size() > kMessageBodyPosition);
	return _data ? (_data->constData() + kMessageBodyPosition) : nullptr;
}

} // namespace MTP
