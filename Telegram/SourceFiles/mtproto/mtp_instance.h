/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include <map>
#include <set>

namespace MTP {
namespace internal {
class Dcenter;
} // namespace internal

class DcOptions;
class Session;
class AuthKey;
using AuthKeyPtr = std::shared_ptr<AuthKey>;
using AuthKeysList = std::vector<AuthKeyPtr>;

class Instance : public QObject {
	Q_OBJECT

public:
	struct Config {
		static constexpr auto kNoneMainDc = -1;
		static constexpr auto kNotSetMainDc = 0;
		static constexpr auto kDefaultMainDc = 2;
		static constexpr auto kTemporaryMainDc = 1000;

		DcId mainDcId = kNotSetMainDc;
		AuthKeysList keys;
	};
	enum class Mode {
		Normal,
		SpecialConfigRequester,
		KeysDestroyer,
	};
	Instance(not_null<DcOptions*> options, Mode mode, Config &&config);

	Instance(const Instance &other) = delete;
	Instance &operator=(const Instance &other) = delete;

	void suggestMainDcId(DcId mainDcId);
	void setMainDcId(DcId mainDcId);
	DcId mainDcId() const;
	QString systemLangCode() const;
	QString cloudLangCode() const;

	void setKeyForWrite(DcId dcId, const AuthKeyPtr &key);
	AuthKeysList getKeysForWrite() const;
	void addKeysForDestroy(AuthKeysList &&keys);

	not_null<DcOptions*> dcOptions();

	template <typename TRequest>
	mtpRequestId send(const TRequest &request, RPCResponseHandler callbacks = RPCResponseHandler(), ShiftedDcId dcId = 0, TimeMs msCanWait = 0, mtpRequestId after = 0) {
		if (auto session = getSession(dcId)) {
			return session->send(request, callbacks, msCanWait, true, !dcId, after);
		}
		return 0;
	}

	template <typename TRequest>
	mtpRequestId send(const TRequest &request, RPCDoneHandlerPtr onDone, RPCFailHandlerPtr onFail = RPCFailHandlerPtr(), int32 dc = 0, TimeMs msCanWait = 0, mtpRequestId after = 0) {
		return send(request, RPCResponseHandler(onDone, onFail), dc, msCanWait, after);
	}

	void sendAnything(ShiftedDcId dcId = 0, TimeMs msCanWait = 0) {
		if (auto session = getSession(dcId)) {
			session->sendAnything(msCanWait);
		}
	}

	void restart();
	void restart(ShiftedDcId shiftedDcId);
	int32 dcstate(ShiftedDcId shiftedDcId = 0);
	QString dctransport(ShiftedDcId shiftedDcId = 0);
	void ping();
	void cancel(mtpRequestId requestId);
	int32 state(mtpRequestId requestId); // < 0 means waiting for such count of ms
	void killSession(ShiftedDcId shiftedDcId);
	void stopSession(ShiftedDcId shiftedDcId);
	void reInitConnection(DcId dcId);
	void logout(RPCDoneHandlerPtr onDone, RPCFailHandlerPtr onFail);

	std::shared_ptr<internal::Dcenter> getDcById(ShiftedDcId shiftedDcId);
	void unpaused();

	void queueQuittingConnection(std::unique_ptr<internal::Connection> connection);

	void setUpdatesHandler(RPCDoneHandlerPtr onDone);
	void setGlobalFailHandler(RPCFailHandlerPtr onFail);
	void setStateChangedHandler(base::lambda<void(ShiftedDcId shiftedDcId, int32 state)> handler);
	void setSessionResetHandler(base::lambda<void(ShiftedDcId shiftedDcId)> handler);
	void clearGlobalHandlers();

	void onStateChange(ShiftedDcId dcWithShift, int32 state);
	void onSessionReset(ShiftedDcId dcWithShift);

	void registerRequest(mtpRequestId requestId, ShiftedDcId dcWithShift);
	mtpRequestId storeRequest(mtpRequest &request, const RPCResponseHandler &parser);
	mtpRequest getRequest(mtpRequestId requestId);
	void clearCallbacksDelayed(const RPCCallbackClears &requestIds);

	void execCallback(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end);
	bool hasCallbacks(mtpRequestId requestId);
	void globalCallback(const mtpPrime *from, const mtpPrime *end);

	// return true if need to clean request data
	bool rpcErrorOccured(mtpRequestId requestId, const RPCFailHandlerPtr &onFail, const RPCError &err);

	bool isKeysDestroyer() const;
	void scheduleKeyDestroy(ShiftedDcId shiftedDcId);

	void requestConfig();
	void requestCDNConfig();

	~Instance();

public slots:
	void connectionFinished(internal::Connection *connection);

signals:
	void configLoaded();
	void cdnConfigLoaded();
	void keyDestroyed(qint32 shiftedDcId);
	void allKeysDestroyed();

private slots:
	void onKeyDestroyed(qint32 shiftedDcId);

private:
	internal::Session *getSession(ShiftedDcId shiftedDcId);

	class Private;
	const std::unique_ptr<Private> _private;

};

} // namespace MTP
