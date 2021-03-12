/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_set.h"

class RPCError {
public:
	explicit RPCError(const MTPrpcError &error);
	explicit RPCError(const mtpBuffer &reply);

	enum {
		NoError,
		TimeoutError
	};

	[[nodiscard]] int32 code() const;
	[[nodiscard]] const QString &type() const;
	[[nodiscard]] const QString &description() const;

	[[nodiscard]] static RPCError Local(
		const QString &type,
		const QString &description);
	[[nodiscard]] static MTPrpcError MTPLocal(
		const QString &type,
		const QString &description);

private:
	int32 _code = 0;
	QString _type, _description;

};

namespace MTP {

inline bool isFloodError(const RPCError &error) {
	return error.type().startsWith(qstr("FLOOD_WAIT_"));
}

inline bool isTemporaryError(const RPCError &error) {
	return error.code() < 0 || error.code() >= 500 || isFloodError(error);
}

inline bool isDefaultHandledError(const RPCError &error) {
	return isTemporaryError(error);
}

struct Response {
	mtpBuffer reply;
	mtpMsgId outerMsgId = 0;
	mtpRequestId requestId = 0;
};

using DoneHandler = FnMut<bool(const Response&)>;
using FailHandler = Fn<bool(const RPCError&, const Response&)>;

struct ResponseHandler {
	DoneHandler done;
	FailHandler fail;
};

} // namespace MTP
