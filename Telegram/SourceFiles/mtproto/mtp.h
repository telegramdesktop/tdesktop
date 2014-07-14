/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#pragma once

#include "mtproto/mtpSession.h"
#include "mtproto/mtpFileLoader.h"

namespace _mtp_internal {
	MTProtoSessionPtr getSession(int32 dc = 0); // 0 - current set dc

	void registerRequest(mtpRequestId requestId, int32 dc);
	void unregisterRequest(mtpRequestId requestId);

	uint32 getLayer();

	static const uint32 dcShift = 10000;

	mtpRequestId storeRequest(mtpRequest &request, const RPCResponseHandler &parser);
	void replaceRequest(mtpRequest &newRequest, const mtpRequest &oldRequest);
	void clearCallbacks(mtpRequestId requestId, int32 errorCode = RPCError::NoError); // 0 - do not toggle onError callback
	void clearCallbacksDelayed(const RPCCallbackClears &requestIds);
	void performDelayedClear();
	void execCallback(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end);
	void globalCallback(const mtpPrime *from, const mtpPrime *end);
	void onStateChange(int32 dc, int32 state);
	bool rpcErrorOccured(mtpRequestId requestId, const RPCFailHandlerPtr &onFail, const RPCError &err); // return true if need to clean request data
	inline bool rpcErrorOccured(mtpRequestId requestId, const RPCResponseHandler &handler, const RPCError &err) {
		return rpcErrorOccured(requestId, handler.onFail, err);
	}

	class RequestResender : public QObject {
		Q_OBJECT

	public slots:

		void checkDelayed();
	};
};

namespace MTP {

	mtpAuthKey &localKey();
	void createLocalKey(const QByteArray &pass, QByteArray *salt = 0);

	static const uint32 dld = 1 * _mtp_internal::dcShift; // send(req, callbacks, MTP::dld + dc) - for download
	static const uint32 upl = 2 * _mtp_internal::dcShift; // send(req, callbacks, MTP::upl + dc) - for upload

	void start();
	void restart();

	void setLayer(uint32 layer);

	void setdc(int32 dc, bool fromZeroOnly = false);
	int32 maindc();
	int32 dcstate(int32 dc = 0);
	QString dctransport(int32 dc = 0);
	void initdc(int32 dc);
	template <typename TRequest>
	inline mtpRequestId send(const TRequest &request, RPCResponseHandler callbacks = RPCResponseHandler(), int32 dc = 0, uint64 msCanWait = 0) {
		return _mtp_internal::getSession(dc)->send(request, callbacks, msCanWait, _mtp_internal::getLayer(), !dc);
	}
	template <typename TRequest>
	inline mtpRequestId send(const TRequest &request, RPCDoneHandlerPtr onDone, RPCFailHandlerPtr onFail = RPCFailHandlerPtr(), int32 dc = 0, uint64 msCanWait = 0) {
		return send(request, RPCResponseHandler(onDone, onFail), dc, msCanWait);
	}
	void cancel(mtpRequestId req);
	
	enum {
		RequestSent = 0,
		RequestConnecting = 1,
		RequestSending = 2
	};
	int32 state(mtpRequestId req); // < 0 means waiting for such count of ms

	void defOnError(const RPCError &err);

	void stop();

	void authed(int32 uid);
	int32 authedId();
	void logoutKeys(RPCDoneHandlerPtr onDone, RPCFailHandlerPtr onFail);

	void setGlobalDoneHandler(RPCDoneHandlerPtr handler);
	void setGlobalFailHandler(RPCFailHandlerPtr handler);
	void setStateChangedHandler(MTPStateChangedHandler handler);
	void clearGlobalHandlers();

	template <typename T>
	T nonce() {
		T result;
		memset_rand(&result, sizeof(T));
		return result;
	}

	void writeConfig(QDataStream &stream);
	bool readConfigElem(int32 blockId, QDataStream &stream);

};

#include "mtproto/mtpSessionImpl.h"
