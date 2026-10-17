/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"

namespace Main {
class Session;
} // namespace Main

namespace Wallet {

class Onramp final {
public:
	explicit Onramp(not_null<Main::Session*> session);

	// Resolves the only provider we support and opens a session with it,
	// calling done() with an empty url when any of the steps gives nothing.
	void requestSessionUrl(
		const QString &address,
		const QString &baseCurrency,
		Fn<void(QString)> done);

private:
	struct Request {
		QString address;
		QString baseCurrency;
		QString provider;
		Fn<void(QString)> done;
	};

	void requestProviders();
	void requestBaseCurrencies();
	void createSession(bool withBaseCurrency);
	void finish(const QString &url);

	MTP::Sender _api;
	std::optional<Request> _request;
	mtpRequestId _requestId = 0;

};

} // namespace Wallet
