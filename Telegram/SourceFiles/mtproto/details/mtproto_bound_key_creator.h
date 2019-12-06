/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/details/mtproto_dc_key_creator.h"
#include "mtproto/details/mtproto_dc_key_binder.h"

namespace MTP::details {

class SerializedRequest;

class BoundKeyCreator final {
public:
	struct Delegate {
		Fn<void(base::expected<DcKeyResult, DcKeyError>)> unboundReady;
		Fn<void(uint64)> sentSome;
		Fn<void()> receivedSome;
	};

	BoundKeyCreator(DcKeyRequest request, Delegate delegate);

	void start(
		DcId dcId,
		int16 protocolDcId,
		not_null<AbstractConnection*> connection,
		not_null<DcOptions*> dcOptions);
	void stop();

	void bind(AuthKeyPtr &&persistentKey);
	void restartBinder();
	[[nodiscard]] bool readyToBind() const;
	[[nodiscard]] SerializedRequest prepareBindRequest(
		const AuthKeyPtr &temporaryKey,
		uint64 sessionId);
	[[nodiscard]] DcKeyBindState handleBindResponse(
		const mtpBuffer &response);
	[[nodiscard]] AuthKeyPtr bindPersistentKey() const;

private:
	const DcKeyRequest _request;
	Delegate _delegate;

	std::optional<DcKeyCreator> _creator;
	std::optional<DcKeyBinder> _binder;

};


[[nodiscard]] bool IsDestroyedTemporaryKeyError(const mtpBuffer &buffer);

} // namespace MTP::details
