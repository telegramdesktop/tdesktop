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

template <typename TRequest>
mtpRequestId MTProtoSession::send(const TRequest &request, RPCResponseHandler callbacks, uint64 msCanWait, uint32 layer, bool toMainDC) {
    mtpRequestId requestId = 0;
    if (layer && dc->needConnectionInit()) {
        MTPInitConnection<TRequest> requestWrap(MTPinitConnection<TRequest>(MTP_int(ApiId), MTP_string(cApiDeviceModel()), MTP_string(cApiSystemVersion()), MTP_string(cApiAppVersion()), MTP_string(ApiLang), request));
        return sendFirst(requestWrap, callbacks, msCanWait, layer, toMainDC);
    }
    try {
        uint32 requestSize = request.size() >> 2;
        if (dc->connectionInited()) layer = 0;
        mtpRequest reqSerialized(mtpRequestData::prepare(requestSize + (layer ? 1 : 0)));
        if (layer) reqSerialized->push_back(mtpLayers[layer]);
        request.write(*reqSerialized);

        DEBUG_LOG(("MTP Info: adding request to toSendMap, msCanWait %1").arg(msCanWait));

        reqSerialized->msDate = getms(); // > 0 - can send without container
        requestId = _mtp_internal::storeRequest(reqSerialized, callbacks);

        sendPrepared(reqSerialized, msCanWait);
    } catch (Exception &e) {
        requestId = 0;
        _mtp_internal::rpcErrorOccured(requestId, callbacks, rpcClientError("NO_REQUEST_ID", QString("send() failed to queue request, exception: %1").arg(e.what())));
    }
    if (requestId) _mtp_internal::registerRequest(requestId, toMainDC ? -getDC() : getDC());
    return requestId;
}

class RPCWrappedDcDoneHandler : public RPCAbstractDoneHandler {
public:
    RPCWrappedDcDoneHandler(const MTProtoDCPtr &dc, const RPCDoneHandlerPtr &ondone) : _dc(dc), _ondone(ondone) {
    }

    void operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) const {
        _dc->setConnectionInited();
        return (*_ondone)(requestId, from, end);
    }

private:
    MTProtoDCPtr _dc;
    RPCDoneHandlerPtr _ondone;
};

template <typename TRequest>
mtpRequestId MTProtoSession::sendFirst(const MTPInitConnection<TRequest> &request, RPCResponseHandler callbacks, uint64 msCanWait, uint32 layer, bool toMainDC) {
    mtpRequestId requestId = 0;
    try {
        uint32 requestSize = request.size() >> 2;
        mtpRequest reqSerialized(mtpRequestData::prepare(requestSize + (layer ? 1 : 0)));
        if (layer) reqSerialized->push_back(mtpLayers[layer]);
        request.write(*reqSerialized);

        DEBUG_LOG(("MTP Info: adding wrapped to init connection request to toSendMap, msCanWait %1").arg(msCanWait));
        callbacks.onDone = RPCDoneHandlerPtr(new RPCWrappedDcDoneHandler(dc, callbacks.onDone));
        reqSerialized->msDate = getms(); // > 0 - can send without container
        requestId = _mtp_internal::storeRequest(reqSerialized, callbacks);

        sendPrepared(reqSerialized, msCanWait);
    } catch (Exception &e) {
        requestId = 0;
        _mtp_internal::rpcErrorOccured(requestId, callbacks, rpcClientError("NO_REQUEST_ID", QString("sendFirst() failed to queue request, exception: %1").arg(e.what())));
    }
    if (requestId) {
        _mtp_internal::registerRequest(requestId, toMainDC ? -getDC() : getDC());
    }
    return requestId;
}
