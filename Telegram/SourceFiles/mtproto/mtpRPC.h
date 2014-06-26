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

class RPCError {
public:

	RPCError(const MTPrpcError &error) : _code(error.c_rpc_error().verror_code.v) {
		const string &msg(error.c_rpc_error().verror_message.c_string().v);
		const QString &text(QString::fromUtf8(msg.c_str(), msg.length()));
		QRegularExpressionMatch m = QRegularExpression("^([A-Z0-9_]+)(: .*)?$", reMultiline).match(text);
		if (m.hasMatch()) {
			_type = m.captured(1);
			_description = m.captured(2).mid(2);
		} else {
			_type = "CLIENT_BAD_RPC_ERROR";
			_description = "Bad rpc error received, text = '" + text + "'";
		}
	}

	int32 code() const {
		return _code;
	}

	const QString &type() const {
		return _type;
	}

	const QString &description() const {
		return _description;
	}

	enum {
		NoError,
		TimeoutError
	};

private:

	int32 _code;
	QString _type, _description;
};

class RPCAbstractDoneHandler { // abstract done
public:
	virtual void operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) const = 0;
	virtual ~RPCAbstractDoneHandler() {
	}
};
typedef QSharedPointer<RPCAbstractDoneHandler> RPCDoneHandlerPtr;

class RPCAbstractFailHandler { // abstract fail
public:
	virtual bool operator()(mtpRequestId requestId, const RPCError &e) const = 0;
	virtual ~RPCAbstractFailHandler() {
	}
};
typedef QSharedPointer<RPCAbstractFailHandler> RPCFailHandlerPtr;

struct RPCResponseHandler {
	RPCResponseHandler() {
	}
	RPCResponseHandler(const RPCDoneHandlerPtr &ondone, const RPCFailHandlerPtr &onfail) : onDone(ondone), onFail(onfail) {
	}

	RPCDoneHandlerPtr onDone;
	RPCFailHandlerPtr onFail;
};
inline RPCResponseHandler rpcCb(const RPCDoneHandlerPtr &onDone = RPCDoneHandlerPtr(), const RPCFailHandlerPtr &onFail = RPCFailHandlerPtr()) {
	return RPCResponseHandler(onDone, onFail);
}

template <typename TReturn>
class RPCDoneHandlerBare : public RPCAbstractDoneHandler { // done(from, end)
	typedef TReturn (*CallbackType)(const mtpPrime *, const mtpPrime *);

public:
    RPCDoneHandlerBare(CallbackType onDone) : _onDone(onDone) {
	}
	virtual void operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) const {
		(*_onDone)(from, end);
	}

private:
	CallbackType _onDone;

};

template <typename TReturn>
class RPCDoneHandlerBareReq : public RPCAbstractDoneHandler { // done(from, end, req_id)
	typedef TReturn (*CallbackType)(const mtpPrime *, const mtpPrime *, mtpRequestId);

public:
    RPCDoneHandlerBareReq(CallbackType onDone) : _onDone(onDone) {
	}
	virtual void operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) const {
		(*_onDone)(from, end, requestId);
	}

private:
	CallbackType _onDone;

};

template <typename TReturn, typename TResponse>
class RPCDoneHandlerPlain : public RPCAbstractDoneHandler { // done(result)
	typedef TReturn (*CallbackType)(const TResponse &);

public:
    RPCDoneHandlerPlain(CallbackType onDone) : _onDone(onDone) {
	}
	virtual void operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) const {
		(*_onDone)(TResponse(from, end));
	}

private:
	CallbackType _onDone;

};

template <typename TReturn, typename TResponse>
class RPCDoneHandlerReq : public RPCAbstractDoneHandler { // done(result, req_id)
	typedef TReturn (*CallbackType)(const TResponse &, mtpRequestId);

public:
    RPCDoneHandlerReq(CallbackType onDone) : _onDone(onDone) {
	}
	virtual void operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) const {
		(*_onDone)(TResponse(from, end), requestId);
	}

private:
	CallbackType _onDone;

};

template <typename TReturn>
class RPCDoneHandlerNo : public RPCAbstractDoneHandler { // done()
	typedef TReturn (*CallbackType)();

public:
    RPCDoneHandlerNo(CallbackType onDone) : _onDone(onDone) {
	}
	virtual void operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) const {
		(*_onDone)();
	}

private:
	CallbackType _onDone;

};

template <typename TReturn>
class RPCDoneHandlerNoReq : public RPCAbstractDoneHandler { // done(req_id)
	typedef TReturn (*CallbackType)(mtpRequestId);

public:
    RPCDoneHandlerNoReq(CallbackType onDone) : _onDone(onDone) {
	}
	virtual void operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) const {
		(*_onDone)(requestId);
	}

private:
	CallbackType _onDone;

};

class RPCFailHandlerPlain : public RPCAbstractFailHandler { // fail(error)
	typedef bool (*CallbackType)(const RPCError &);

public:
	RPCFailHandlerPlain(CallbackType onFail) : _onFail(onFail) {
	}
	virtual bool operator()(mtpRequestId requestId, const RPCError &e) const {
		return (*_onFail)(e);
	}

private:
	CallbackType _onFail;

};

class RPCFailHandlerReq : public RPCAbstractFailHandler { // fail(error, req_id)
	typedef bool (*CallbackType)(const RPCError &, mtpRequestId);

public:
	RPCFailHandlerReq(CallbackType onFail) : _onFail(onFail) {
	}
	virtual bool operator()(mtpRequestId requestId, const RPCError &e) const {
		return (*_onFail)(e, requestId);
	}

private:
	CallbackType _onFail;

};

class RPCFailHandlerNo : public RPCAbstractFailHandler { // fail()
	typedef bool (*CallbackType)();

public:
	RPCFailHandlerNo(CallbackType onFail) : _onFail(onFail) {
	}
	virtual bool operator()(mtpRequestId requestId, const RPCError &e) const {
		return (*_onFail)();
	}

private:
	CallbackType _onFail;

};

class RPCFailHandlerNoReq : public RPCAbstractFailHandler { // fail(req_id)
	typedef bool (*CallbackType)(mtpRequestId);

public:
	RPCFailHandlerNoReq(CallbackType onFail) : _onFail(onFail) {
	}
	virtual bool operator()(mtpRequestId requestId, const RPCError &e) const {
		return (*_onFail)(requestId);
	}

private:
	CallbackType _onFail;

};

struct RPCCallbackClear {
	RPCCallbackClear(mtpRequestId id = 0, int32 code = RPCError::NoError) : requestId(id), errorCode(code) {
	}

	mtpRequestId requestId;
	int32 errorCode;
};
typedef QVector<RPCCallbackClear> RPCCallbackClears;

template <typename TReturn>
inline RPCDoneHandlerPtr rpcDone(TReturn (*onDone)(const mtpPrime *, const mtpPrime *)) { // done(from, end)
	return RPCDoneHandlerPtr(new RPCDoneHandlerBare<TReturn>(onDone));
}

template <typename TReturn>
inline RPCDoneHandlerPtr rpcDone(TReturn (*onDone)(const mtpPrime *, const mtpPrime *, mtpRequestId)) { // done(from, end, req_id)
	return RPCDoneHandlerPtr(new RPCDoneHandlerBareReq<TReturn>(onDone));
}

template <typename TReturn, typename TResponse>
inline RPCDoneHandlerPtr rpcDone(TReturn (*onDone)(const TResponse &)) { // done(result)
	return RPCDoneHandlerPtr(new RPCDoneHandlerPlain<TReturn, TResponse>(onDone));
}

template <typename TReturn, typename TResponse>
inline RPCDoneHandlerPtr rpcDone(TReturn (*onDone)(const TResponse &, mtpRequestId)) { // done(result, req_id)
	return RPCDoneHandlerPtr(new RPCDoneHandlerReq<TReturn, TResponse>(onDone));
}

template <typename TReturn>
inline RPCDoneHandlerPtr rpcDone(TReturn (*onDone)()) { // done()
	return RPCDoneHandlerPtr(new RPCDoneHandlerNo<TReturn>(onDone));
}

template <typename TReturn>
inline RPCDoneHandlerPtr rpcDone(TReturn (*onDone)(mtpRequestId)) { // done(req_id)
	return RPCDoneHandlerPtr(new RPCDoneHandlerNoReq<TReturn>(onDone));
}

inline RPCFailHandlerPtr rpcFail(bool (*onFail)(const RPCError &)) { // fail(error)
	return RPCFailHandlerPtr(new RPCFailHandlerPlain(onFail));
}

inline RPCFailHandlerPtr rpcFail(bool (*onFail)(const RPCError &, mtpRequestId)) { // fail(error, req_id)
	return RPCFailHandlerPtr(new RPCFailHandlerReq(onFail));
}

inline RPCFailHandlerPtr rpcFail(bool (*onFail)()) { // fail()
	return RPCFailHandlerPtr(new RPCFailHandlerNo(onFail));
}

inline RPCFailHandlerPtr rpcFail(bool (*onFail)(mtpRequestId)) { // fail(req_id)
	return RPCFailHandlerPtr(new RPCFailHandlerNoReq(onFail));
}

class RPCSender;

class RPCOwnedDoneHandler : public RPCAbstractDoneHandler { // abstract done
public:
	RPCOwnedDoneHandler(RPCSender *owner);
	void invalidate() {
		_owner = 0;
	}
	~RPCOwnedDoneHandler();

protected:
	RPCSender *_owner;
};

class RPCOwnedFailHandler : public RPCAbstractFailHandler { // abstract fail
public:
	RPCOwnedFailHandler(RPCSender *owner);
	void invalidate() {
		_owner = 0;
	}
	~RPCOwnedFailHandler();

protected:
	RPCSender *_owner;
};

template <typename TReturn, typename TReceiver>
class RPCDoneHandlerBareOwned : public RPCOwnedDoneHandler { // done(from, end)
	typedef TReturn (TReceiver::*CallbackType)(const mtpPrime *, const mtpPrime *);

public:
    RPCDoneHandlerBareOwned(TReceiver *receiver, CallbackType onDone) : RPCOwnedDoneHandler(receiver), _onDone(onDone) {
	}
	virtual void operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) const {
		if (_owner) (static_cast<TReceiver*>(_owner)->*_onDone)(from, end);
	}

private:
	CallbackType _onDone;

};

template <typename TReturn, typename TReceiver>
class RPCDoneHandlerBareOwnedReq : public RPCOwnedDoneHandler { // done(from, end, req_id)
	typedef TReturn (TReceiver::*CallbackType)(const mtpPrime *, const mtpPrime *, mtpRequestId);

public:
    RPCDoneHandlerBareOwnedReq(TReceiver *receiver, CallbackType onDone) : RPCOwnedDoneHandler(receiver), _onDone(onDone) {
	}
	virtual void operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) const {
		if (_owner) (static_cast<TReceiver*>(_owner)->*_onDone)(from, end, requestId);
	}

private:
	CallbackType _onDone;

};

template <typename TReturn, typename TReceiver, typename TResponse>
class RPCDoneHandlerOwned : public RPCOwnedDoneHandler { // done(result)
	typedef TReturn (TReceiver::*CallbackType)(const TResponse &);

public:
    RPCDoneHandlerOwned(TReceiver *receiver, CallbackType onDone) : RPCOwnedDoneHandler(receiver), _onDone(onDone) {
	}
	virtual void operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) const {
		if (_owner) (static_cast<TReceiver*>(_owner)->*_onDone)(TResponse(from, end));
	}

private:
	CallbackType _onDone;

};

template <typename TReturn, typename TReceiver, typename TResponse>
class RPCDoneHandlerOwnedReq : public RPCOwnedDoneHandler { // done(result, req_id)
	typedef TReturn (TReceiver::*CallbackType)(const TResponse &, mtpRequestId);

public:
    RPCDoneHandlerOwnedReq(TReceiver *receiver, CallbackType onDone) : RPCOwnedDoneHandler(receiver), _onDone(onDone) {
	}
	virtual void operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) const {
		if (_owner) (static_cast<TReceiver*>(_owner)->*_onDone)(TResponse(from, end), requestId);
	}

private:
	CallbackType _onDone;

};

template <typename TReturn, typename TReceiver>
class RPCDoneHandlerOwnedNo : public RPCOwnedDoneHandler { // done()
	typedef TReturn (TReceiver::*CallbackType)();

public:
    RPCDoneHandlerOwnedNo(TReceiver *receiver, CallbackType onDone) : RPCOwnedDoneHandler(receiver), _onDone(onDone) {
	}
	virtual void operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) const {
		if (_owner) (static_cast<TReceiver*>(_owner)->*_onDone)();
	}

private:
	CallbackType _onDone;

};

template <typename TReturn, typename TReceiver>
class RPCDoneHandlerOwnedNoReq : public RPCOwnedDoneHandler { // done(req_id)
	typedef TReturn (TReceiver::*CallbackType)(mtpRequestId);

public:
    RPCDoneHandlerOwnedNoReq(TReceiver *receiver, CallbackType onDone) : RPCOwnedDoneHandler(receiver), _onDone(onDone) {
	}
	virtual void operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) const {
		if (_owner) (static_cast<TReceiver*>(_owner)->*_onDone)(requestId);
	}

private:
	CallbackType _onDone;

};

template <typename T, typename TReturn, typename TReceiver>
class RPCBindedDoneHandlerBareOwned : public RPCOwnedDoneHandler { // done(b, from, end)
	typedef TReturn (TReceiver::*CallbackType)(T, const mtpPrime *, const mtpPrime *);

public:
    RPCBindedDoneHandlerBareOwned(T b, TReceiver *receiver, CallbackType onDone) : RPCOwnedDoneHandler(receiver), _b(b), _onDone(onDone) {
	}
	virtual void operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) const {
		if (_owner) (static_cast<TReceiver*>(_owner)->*_onDone)(_b, from, end);
	}

private:
	CallbackType _onDone;
	T _b;

};

template <typename T, typename TReturn, typename TReceiver>
class RPCBindedDoneHandlerBareOwnedReq : public RPCOwnedDoneHandler { // done(b, from, end, req_id)
	typedef TReturn (TReceiver::*CallbackType)(T, const mtpPrime *, const mtpPrime *, mtpRequestId);

public:
    RPCBindedDoneHandlerBareOwnedReq(T b, TReceiver *receiver, CallbackType onDone) : RPCOwnedDoneHandler(receiver), _b(b), _onDone(onDone) {
	}
	virtual void operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) const {
		if (_owner) (static_cast<TReceiver*>(_owner)->*_onDone)(_b, from, end, requestId);
	}

private:
	CallbackType _onDone;
	T _b;

};

template <typename T, typename TReturn, typename TReceiver, typename TResponse>
class RPCBindedDoneHandlerOwned : public RPCOwnedDoneHandler { // done(b, result)
	typedef TReturn (TReceiver::*CallbackType)(T, const TResponse &);

public:
    RPCBindedDoneHandlerOwned(T b, TReceiver *receiver, CallbackType onDone) : RPCOwnedDoneHandler(receiver), _onDone(onDone), _b(b) {
	}
	virtual void operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) const {
		if (_owner) (static_cast<TReceiver*>(_owner)->*_onDone)(_b, TResponse(from, end));
	}

private:
	CallbackType _onDone;
	T _b;

};

template <typename T, typename TReturn, typename TReceiver, typename TResponse>
class RPCBindedDoneHandlerOwnedReq : public RPCOwnedDoneHandler { // done(b, result, req_id)
	typedef TReturn (TReceiver::*CallbackType)(T, const TResponse &, mtpRequestId);

public:
    RPCBindedDoneHandlerOwnedReq(T b, TReceiver *receiver, CallbackType onDone) : RPCOwnedDoneHandler(receiver), _b(b), _onDone(onDone) {
	}
	virtual void operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) const {
		if (_owner) (static_cast<TReceiver*>(_owner)->*_onDone)(_b, TResponse(from, end), requestId);
	}

private:
	CallbackType _onDone;
	T _b;

};

template <typename T, typename TReturn, typename TReceiver>
class RPCBindedDoneHandlerOwnedNo : public RPCOwnedDoneHandler { // done(b)
	typedef TReturn (TReceiver::*CallbackType)(T);

public:
    RPCBindedDoneHandlerOwnedNo(T b, TReceiver *receiver, CallbackType onDone) : RPCOwnedDoneHandler(receiver), _b(b), _onDone(onDone) {
	}
	virtual void operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) const {
		if (_owner) (static_cast<TReceiver*>(_owner)->*_onDone)(_b);
	}

private:
	CallbackType _onDone;
	T _b;

};

template <typename T, typename TReturn, typename TReceiver>
class RPCBindedDoneHandlerOwnedNoReq : public RPCOwnedDoneHandler { // done(b, req_id)
	typedef TReturn (TReceiver::*CallbackType)(T, mtpRequestId);

public:
    RPCBindedDoneHandlerOwnedNoReq(T b, TReceiver *receiver, CallbackType onDone) : RPCOwnedDoneHandler(receiver), _b(b), _onDone(onDone) {
	}
	virtual void operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) const {
		if (_owner) (static_cast<TReceiver*>(_owner)->*_onDone)(_b, requestId);
	}

private:
	CallbackType _onDone;
	T _b;

};

template <typename TReceiver>
class RPCFailHandlerOwned : public RPCOwnedFailHandler { // fail(error)
	typedef bool (TReceiver::*CallbackType)(const RPCError &);

public:
	RPCFailHandlerOwned(TReceiver *receiver, CallbackType onFail) : RPCOwnedFailHandler(receiver), _onFail(onFail) {
	}
	virtual bool operator()(mtpRequestId requestId, const RPCError &e) const {
		return _owner ? (static_cast<TReceiver*>(_owner)->*_onFail)(e) : true;
	}

private:
	CallbackType _onFail;

};

template <typename TReceiver>
class RPCFailHandlerOwnedReq : public RPCOwnedFailHandler { // fail(error, req_id)
	typedef bool (TReceiver::*CallbackType)(const RPCError &, mtpRequestId);

public:
	RPCFailHandlerOwnedReq(TReceiver *receiver, CallbackType onFail) : RPCOwnedFailHandler(receiver), _onFail(onFail) {
	}
	virtual bool operator()(mtpRequestId requestId, const RPCError &e) const {
		return _owner ? (static_cast<TReceiver*>(_owner)->*_onFail)(e, requestId) : true;
	}

private:
	CallbackType _onFail;

};

template <typename TReceiver>
class RPCFailHandlerOwnedNo : public RPCOwnedFailHandler { // fail()
	typedef bool (TReceiver::*CallbackType)();

public:
	RPCFailHandlerOwnedNo(TReceiver *receiver, CallbackType onFail) : RPCOwnedFailHandler(receiver), _onFail(onFail) {
	}
	virtual bool operator()(mtpRequestId requestId, const RPCError &e) const {
		return _owner ? (static_cast<TReceiver*>(_owner)->*_onFail)() : true;
	}

private:
	CallbackType _onFail;

};

template <typename TReceiver>
class RPCFailHandlerOwnedNoReq : public RPCOwnedFailHandler { // fail(req_id)
	typedef bool (TReceiver::*CallbackType)(mtpRequestId);

public:
	RPCFailHandlerOwnedNoReq(TReceiver *receiver, CallbackType onFail) : RPCOwnedFailHandler(receiver), _onFail(onFail) {
	}
	virtual bool operator()(mtpRequestId requestId, const RPCError &e) const {
		return _owner ? (static_cast<TReceiver*>(_owner)->*_onFail)(requestId) : true;
	}

private:
	CallbackType _onFail;

};

template <typename T, typename TReceiver>
class RPCBindedFailHandlerOwned : public RPCOwnedFailHandler { // fail(b, error)
	typedef bool (TReceiver::*CallbackType)(T, const RPCError &);

public:
	RPCBindedFailHandlerOwned(T b, TReceiver *receiver, CallbackType onFail) : RPCOwnedFailHandler(receiver), _onFail(onFail), _b(b) {
	}
	virtual bool operator()(mtpRequestId requestId, const RPCError &e) const {
		return _owner ? (static_cast<TReceiver*>(_owner)->*_onFail)(_b, e) : true;
	}

private:
	CallbackType _onFail;
	T _b;

};

template <typename T, typename TReceiver>
class RPCBindedFailHandlerOwnedReq : public RPCOwnedFailHandler { // fail(b, error, req_id)
	typedef bool (TReceiver::*CallbackType)(T, const RPCError &, mtpRequestId);

public:
	RPCBindedFailHandlerOwnedReq(T b, TReceiver *receiver, CallbackType onFail) : RPCOwnedFailHandler(receiver), _b(b), _onFail(onFail) {
	}
	virtual bool operator()(mtpRequestId requestId, const RPCError &e) const {
		return _owner ? (static_cast<TReceiver*>(_owner)->*_onFail)(_b, e, requestId) : true;
	}

private:
	CallbackType _onFail;
	T _b;

};

template <typename T, typename TReceiver>
class RPCBindedFailHandlerOwnedNo : public RPCOwnedFailHandler { // fail(b)
	typedef bool (TReceiver::*CallbackType)(T);

public:
	RPCBindedFailHandlerOwnedNo(T b, TReceiver *receiver, CallbackType onFail) : RPCOwnedFailHandler(receiver), _onFail(onFail), _b(b) {
	}
	virtual bool operator()(mtpRequestId requestId, const RPCError &e) const {
		return _owner ? (static_cast<TReceiver*>(_owner)->*_onFail)(_b) : true;
	}

private:
	CallbackType _onFail;
	T _b;

};

template <typename T, typename TReceiver>
class RPCBindedFailHandlerOwnedNoReq : public RPCOwnedFailHandler { // fail(b, req_id)
	typedef bool (TReceiver::*CallbackType)(T, mtpRequestId);

public:
	RPCBindedFailHandlerOwnedNoReq(T b, TReceiver *receiver, CallbackType onFail) : RPCOwnedFailHandler(receiver), _onFail(onFail), _b(b) {
	}
	virtual bool operator()(mtpRequestId requestId, const RPCError &e) const {
		return _owner ? (static_cast<TReceiver*>(_owner)->*_onFail)(_b, requestId) : true;
	}

private:
	CallbackType _onFail;
	T _b;

};

class RPCSender {
	typedef QSet<RPCOwnedDoneHandler*> DoneHandlers;
	DoneHandlers doneHandlers;
	typedef QSet<RPCOwnedFailHandler*> FailHandlers;
	FailHandlers failHandlers;

public:

	template <typename TReturn, typename TReceiver> // done(from, end)
	RPCDoneHandlerPtr rpcDone(TReturn (TReceiver::*onDone)(const mtpPrime *, const mtpPrime *)) {
		return RPCDoneHandlerPtr(new RPCDoneHandlerBareOwned<TReturn, TReceiver>(static_cast<TReceiver*>(this), onDone));
	}

	template <typename TReturn, typename TReceiver> // done(from, end, req_id)
	RPCDoneHandlerPtr rpcDone(TReturn (TReceiver::*onDone)(const mtpPrime *, const mtpPrime *, mtpRequestId)) {
		return RPCDoneHandlerPtr(new RPCDoneHandlerBareOwnedReq<TReturn, TReceiver>(static_cast<TReceiver*>(this), onDone));
	}

	template <typename TReturn, typename TReceiver, typename TResponse> // done(result)
	RPCDoneHandlerPtr rpcDone(TReturn (TReceiver::*onDone)(const TResponse &)) {
		return RPCDoneHandlerPtr(new RPCDoneHandlerOwned<TReturn, TReceiver, TResponse>(static_cast<TReceiver*>(this), onDone));
	}

	template <typename TReturn, typename TReceiver, typename TResponse> // done(result, req_id)
	RPCDoneHandlerPtr rpcDone(TReturn (TReceiver::*onDone)(const TResponse &, mtpRequestId)) {
		return RPCDoneHandlerPtr(new RPCDoneHandlerOwnedReq<TReturn, TReceiver, TResponse>(static_cast<TReceiver*>(this), onDone));
	}

	template <typename TReturn, typename TReceiver> // done()
	RPCDoneHandlerPtr rpcDone(TReturn (TReceiver::*onDone)()) {
		return RPCDoneHandlerPtr(new RPCDoneHandlerOwnedNo<TReturn, TReceiver>(static_cast<TReceiver*>(this), onDone));
	}

	template <typename TReturn, typename TReceiver> // done(req_id)
	RPCDoneHandlerPtr rpcDone(TReturn (TReceiver::*onDone)(mtpRequestId)) {
		return RPCDoneHandlerPtr(new RPCDoneHandlerOwnedNoReq<TReturn, TReceiver>(static_cast<TReceiver*>(this), onDone));
	}

	template <typename TReceiver> // fail(error)
	RPCFailHandlerPtr rpcFail(bool (TReceiver::*onFail)(const RPCError &)) {
		return RPCFailHandlerPtr(new RPCFailHandlerOwned<TReceiver>(static_cast<TReceiver*>(this), onFail));
	}

	template <typename TReceiver> // fail(error, req_id)
	RPCFailHandlerPtr rpcFail(bool (TReceiver::*onFail)(const RPCError &, mtpRequestId)) {
		return RPCFailHandlerPtr(new RPCFailHandlerOwnedReq<TReceiver>(static_cast<TReceiver*>(this), onFail));
	}

	template <typename TReceiver> // fail()
	RPCFailHandlerPtr rpcFail(bool (TReceiver::*onFail)()) {
		return RPCFailHandlerPtr(new RPCFailHandlerOwnedNo<TReceiver>(static_cast<TReceiver*>(this), onFail));
	}

	template <typename TReceiver> // fail(req_id)
	RPCFailHandlerPtr rpcFail(bool (TReceiver::*onFail)(mtpRequestId)) {
		return RPCFailHandlerPtr(new RPCFailHandlerOwnedNo<TReceiver>(static_cast<TReceiver*>(this), onFail));
	}

	template <typename T, typename TReturn, typename TReceiver> // done(b, from, end)
	RPCDoneHandlerPtr rpcDone(TReturn (TReceiver::*onDone)(T, const mtpPrime *, const mtpPrime *), T b) {
		return RPCDoneHandlerPtr(new RPCBindedDoneHandlerBareOwned<T, TReturn, TReceiver>(b, static_cast<TReceiver*>(this), onDone));
	}

	template <typename T, typename TReturn, typename TReceiver> // done(b, from, end, req_id)
	RPCDoneHandlerPtr rpcDone(TReturn (TReceiver::*onDone)(T, const mtpPrime *, const mtpPrime *, mtpRequestId), T b) {
		return RPCDoneHandlerPtr(new RPCBindedDoneHandlerBareOwnedReq<T, TReturn, TReceiver>(b, static_cast<TReceiver*>(this), onDone));
	}

	template <typename T, typename TReturn, typename TReceiver, typename TResponse> // done(b, result)
	RPCDoneHandlerPtr rpcDone(TReturn (TReceiver::*onDone)(T, const TResponse &), T b) {
		return RPCDoneHandlerPtr(new RPCBindedDoneHandlerOwned<T, TReturn, TReceiver, TResponse>(b, static_cast<TReceiver*>(this), onDone));
	}

	template <typename T, typename TReturn, typename TReceiver, typename TResponse> // done(b, result, req_id)
	RPCDoneHandlerPtr rpcDone(TReturn (TReceiver::*onDone)(T, const TResponse &, mtpRequestId), T b) {
		return RPCDoneHandlerPtr(new RPCBindedDoneHandlerOwnedReq<T, TReturn, TReceiver, TResponse>(b, static_cast<TReceiver*>(this), onDone));
	}

	template <typename T, typename TReturn, typename TReceiver> // done(b)
	RPCDoneHandlerPtr rpcDone(TReturn (TReceiver::*onDone)(T), T b) {
		return RPCDoneHandlerPtr(new RPCBindedDoneHandlerOwnedNo<T, TReturn, TReceiver>(b, static_cast<TReceiver*>(this), onDone));
	}

	template <typename T, typename TReturn, typename TReceiver> // done(b, req_id)
	RPCDoneHandlerPtr rpcDone(TReturn (TReceiver::*onDone)(T, mtpRequestId), T b) {
		return RPCDoneHandlerPtr(new RPCBindedDoneHandlerOwnedNoReq<T, TReturn, TReceiver>(b, static_cast<TReceiver*>(this), onDone));
	}

	template <typename T, typename TReceiver> // fail(b, error)
	RPCFailHandlerPtr rpcFail(bool (TReceiver::*onFail)(T, const RPCError &), T b) {
		return RPCFailHandlerPtr(new RPCBindedFailHandlerOwned<T, TReceiver>(b, static_cast<TReceiver*>(this), onFail));
	}

	template <typename T, typename TReceiver> // fail(b, error, req_id)
	RPCFailHandlerPtr rpcFail(bool (TReceiver::*onFail)(T, const RPCError &, mtpRequestId), T b) {
		return RPCFailHandlerPtr(new RPCBindedFailHandlerOwnedReq<T, TReceiver>(b, static_cast<TReceiver*>(this), onFail));
	}

	template <typename T, typename TReceiver> // fail(b)
	RPCFailHandlerPtr rpcFail(bool (TReceiver::*onFail)(T), T b) {
		return RPCFailHandlerPtr(new RPCBindedFailHandlerOwnedNo<T, TReceiver>(b, static_cast<TReceiver*>(this), onFail));
	}

	template <typename T, typename TReceiver> // fail(b, req_id)
	RPCFailHandlerPtr rpcFail(bool (TReceiver::*onFail)(T, mtpRequestId), T b) {
		return RPCFailHandlerPtr(new RPCBindedFailHandlerOwnedNo<T, TReceiver>(b, static_cast<TReceiver*>(this), onFail));
	}

	void regHandler(RPCOwnedDoneHandler *handler) {
		doneHandlers.insert(handler);
	}

	void unregHandler(RPCOwnedDoneHandler *handler) {
		doneHandlers.remove(handler);
	}

	void regHandler(RPCOwnedFailHandler *handler) {
		failHandlers.insert(handler);
	}

	void unregHandler(RPCOwnedFailHandler *handler) {
		failHandlers.remove(handler);
	}

	~RPCSender() {
		for (DoneHandlers::iterator i = doneHandlers.begin(), e = doneHandlers.end(); i != e; ++i) {
			(*i)->invalidate();
		}
		for (FailHandlers::iterator i = failHandlers.begin(), e = failHandlers.end(); i != e; ++i) {
			(*i)->invalidate();
		}
	}

};

typedef void (*MTPStateChangedHandler)(int32 dcId, int32 state);
