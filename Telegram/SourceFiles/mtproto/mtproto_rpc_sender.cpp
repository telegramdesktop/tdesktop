/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/mtproto_rpc_sender.h"

#include <QtCore/QRegularExpression>

RPCError::RPCError(const MTPrpcError &error)
: _code(error.c_rpc_error().verror_code().v) {
	QString text = qs(error.c_rpc_error().verror_message());
	if (_code < 0 || _code >= 500) {
		_type = "INTERNAL_SERVER_ERROR";
		_description = text;
	} else {
		const auto expression = QRegularExpression(
			"^([A-Z0-9_]+)(: .*)?$",
			(QRegularExpression::DotMatchesEverythingOption
				| QRegularExpression::MultilineOption));
		const auto match = expression.match(text);
		if (match.hasMatch()) {
			_type = match.captured(1);
			_description = match.captured(2).mid(2);
		} else {
			_type = "CLIENT_BAD_RPC_ERROR";
			_description = "Bad rpc error received, text = '" + text + '\'';
		}
	}
}
