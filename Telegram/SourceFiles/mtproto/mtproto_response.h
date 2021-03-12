/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_set.h"

namespace MTP {

class Error {
public:
	explicit Error(const MTPrpcError &error);
	explicit Error(const mtpBuffer &reply);

	enum {
		NoError,
		TimeoutError
	};

	[[nodiscard]] int32 code() const;
	[[nodiscard]] const QString &type() const;
	[[nodiscard]] const QString &description() const;

	[[nodiscard]] static Error Local(
		const QString &type,
		const QString &description);
	[[nodiscard]] static MTPrpcError MTPLocal(
		const QString &type,
		const QString &description);

private:
	int32 _code = 0;
	QString _type, _description;

};

inline bool IsFloodError(const Error &error) {
	return error.type().startsWith(qstr("FLOOD_WAIT_"));
}

inline bool IsTemporaryError(const Error &error) {
	return error.code() < 0 || error.code() >= 500 || IsFloodError(error);
}

inline bool IsDefaultHandledError(const Error &error) {
	return IsTemporaryError(error);
}

struct Response {
	mtpBuffer reply;
	mtpMsgId outerMsgId = 0;
	mtpRequestId requestId = 0;
};

using DoneHandler = FnMut<bool(const Response&)>;
using FailHandler = Fn<bool(const Error&, const Response&)>;

struct ResponseHandler {
	DoneHandler done;
	FailHandler fail;
};

} // namespace MTP
