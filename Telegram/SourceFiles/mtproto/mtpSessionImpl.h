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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

template <typename TRequest>
mtpRequestId MTProtoSession::send(const TRequest &request, RPCResponseHandler callbacks, uint64 msCanWait, bool needsLayer, bool toMainDC, mtpRequestId after) {
    mtpRequestId requestId = 0;
    try {
		uint32 requestSize = request.innerLength() >> 2;
		mtpRequest reqSerialized(mtpRequestData::prepare(requestSize));
        request.write(*reqSerialized);

        DEBUG_LOG(("MTP Info: adding request to toSendMap, msCanWait %1").arg(msCanWait));

        reqSerialized->msDate = getms(true); // > 0 - can send without container
		reqSerialized->needsLayer = needsLayer;
		if (after) reqSerialized->after = _mtp_internal::getRequest(after);
		requestId = _mtp_internal::storeRequest(reqSerialized, callbacks);

        sendPrepared(reqSerialized, msCanWait);
    } catch (Exception &e) {
        requestId = 0;
        _mtp_internal::rpcErrorOccured(requestId, callbacks, rpcClientError("NO_REQUEST_ID", QString("send() failed to queue request, exception: %1").arg(e.what())));
    }
	if (requestId) _mtp_internal::registerRequest(requestId, toMainDC ? -getDcWithShift() : getDcWithShift());
    return requestId;
}
