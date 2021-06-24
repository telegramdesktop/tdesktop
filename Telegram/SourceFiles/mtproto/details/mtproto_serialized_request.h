/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/core_types.h"

#include <crl/crl_time.h>

namespace MTP {
namespace details {

class RequestData;
class SerializedRequest;

class RequestConstructHider {
	struct Tag {};
	friend class RequestData;
	friend class SerializedRequest;
};

class SerializedRequest {
public:
	SerializedRequest() = default;

	static constexpr auto kSaltInts = 2;
	static constexpr auto kSessionIdInts = 2;
	static constexpr auto kMessageIdPosition = kSaltInts + kSessionIdInts;
	static constexpr auto kMessageIdInts = 2;
	static constexpr auto kSeqNoPosition = kMessageIdPosition
		+ kMessageIdInts;
	static constexpr auto kSeqNoInts = 1;
	static constexpr auto kMessageLengthPosition = kSeqNoPosition
		+ kSeqNoInts;
	static constexpr auto kMessageLengthInts = 1;
	static constexpr auto kMessageBodyPosition = kMessageLengthPosition
		+ kMessageLengthInts;

	static SerializedRequest Prepare(uint32 size, uint32 reserveSize = 0);

	template <
		typename Request,
		typename = std::enable_if_t<tl::is_boxed_v<Request>>>
		static SerializedRequest Serialize(const Request &request);

	// For template MTP requests and MTPBoxed instantiation.
	template <typename Accumulator>
	void write(Accumulator &to) const {
		if (const auto size = sizeInBytes()) {
			tl::Writer<Accumulator>::PutBytes(to, dataInBytes(), size);
		}
	}

	RequestData *operator->() const;
	RequestData &operator*() const;
	explicit operator bool() const;

	void setMsgId(mtpMsgId msgId);
	[[nodiscard]] mtpMsgId getMsgId() const;

	void setSeqNo(uint32 seqNo);
	[[nodiscard]] uint32 getSeqNo() const;

	void addPadding(bool forAuthKeyInner);
	[[nodiscard]] uint32 messageSize() const;

	[[nodiscard]] bool needAck() const;

	using ResponseType = void; // don't know real response type =(

private:
	explicit SerializedRequest(const RequestConstructHider::Tag &);

	[[nodiscard]] size_t sizeInBytes() const;
	[[nodiscard]] const void *dataInBytes() const;

	std::shared_ptr<RequestData> _data;

};

class RequestData : public mtpBuffer {
public:
	explicit RequestData(const RequestConstructHider::Tag &) {
	}

	SerializedRequest after;
	crl::time lastSentTime = 0;
	mtpRequestId requestId = 0;
	bool needsLayer = false;
	bool forceSendInContainer = false;

};

template <typename Request, typename>
SerializedRequest SerializedRequest::Serialize(const Request &request) {
	const auto requestSize = tl::count_length(request) >> 2;
	auto serialized = Prepare(requestSize);
	request.template write<mtpBuffer>(*serialized);
	return serialized;
}

} // namespace details
} // namespace MTP
