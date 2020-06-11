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
	RPCError(const MTPrpcError &error);

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

	static RPCError Local(const QString &type, const QString &description) {
		return MTP_rpc_error(
			MTP_int(0),
			MTP_bytes(
			("CLIENT_"
				+ type
				+ (description.length()
					? (": " + description)
					: QString())).toUtf8()));
	}

private:
	int32 _code;
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

} // namespace MTP

class RPCAbstractDoneHandler { // abstract done
public:
	[[nodiscard]] virtual bool operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) = 0;
	virtual ~RPCAbstractDoneHandler() {
	}

};
using RPCDoneHandlerPtr = std::shared_ptr<RPCAbstractDoneHandler>;

class RPCAbstractFailHandler { // abstract fail
public:
	virtual bool operator()(mtpRequestId requestId, const RPCError &e) = 0;
	virtual ~RPCAbstractFailHandler() {
	}
};
using RPCFailHandlerPtr = std::shared_ptr<RPCAbstractFailHandler>;

struct RPCResponseHandler {
	RPCResponseHandler() = default;
	RPCResponseHandler(RPCDoneHandlerPtr &&done, RPCFailHandlerPtr &&fail)
	: onDone(std::move(done))
	, onFail(std::move(fail)) {
	}

	RPCDoneHandlerPtr onDone;
	RPCFailHandlerPtr onFail;

};

class RPCDoneHandlerBare : public RPCAbstractDoneHandler { // done(from, end)
	using CallbackType = bool (*)(const mtpPrime *, const mtpPrime *);

public:
	RPCDoneHandlerBare(CallbackType onDone) : _onDone(onDone) {
	}
	bool operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) override {
		return (*_onDone)(from, end);
	}

private:
	CallbackType _onDone;

};

class RPCDoneHandlerBareReq : public RPCAbstractDoneHandler { // done(from, end, req_id)
	using CallbackType = bool (*)(const mtpPrime *, const mtpPrime *, mtpRequestId);

public:
	RPCDoneHandlerBareReq(CallbackType onDone) : _onDone(onDone) {
	}
	bool operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) override {
		return (*_onDone)(from, end, requestId);
	}

private:
	CallbackType _onDone;

};

template <typename TReturn, typename TResponse>
class RPCDoneHandlerPlain : public RPCAbstractDoneHandler { // done(result)
	using CallbackType = TReturn (*)(const TResponse &);

public:
	RPCDoneHandlerPlain(CallbackType onDone) : _onDone(onDone) {
	}
	bool operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) override {
		auto response = TResponse();
		if (!response.read(from, end)) {
			return false;
		}
		(*_onDone)(std::move(response));
		return true;
	}

private:
	CallbackType _onDone;

};

template <typename TReturn, typename TResponse>
class RPCDoneHandlerReq : public RPCAbstractDoneHandler { // done(result, req_id)
	using CallbackType = TReturn (*)(const TResponse &, mtpRequestId);

public:
	RPCDoneHandlerReq(CallbackType onDone) : _onDone(onDone) {
	}
	bool operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) override {
		auto response = TResponse();
		if (!response.read(from, end)) {
			return false;
		}
		(*_onDone)(std::move(response), requestId);
		return true;
	}

private:
	CallbackType _onDone;

};

template <typename TReturn>
class RPCDoneHandlerNo : public RPCAbstractDoneHandler { // done()
	using CallbackType = TReturn (*)();

public:
	RPCDoneHandlerNo(CallbackType onDone) : _onDone(onDone) {
	}
	bool operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) override {
		(*_onDone)();
		return true;
	}

private:
	CallbackType _onDone;

};

template <typename TReturn>
class RPCDoneHandlerNoReq : public RPCAbstractDoneHandler { // done(req_id)
	using CallbackType = TReturn (*)(mtpRequestId);

public:
	RPCDoneHandlerNoReq(CallbackType onDone) : _onDone(onDone) {
	}
	bool operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) override {
		(*_onDone)(requestId);
		return true;
	}

private:
	CallbackType _onDone;

};

class RPCFailHandlerPlain : public RPCAbstractFailHandler { // fail(error)
	using CallbackType = bool (*)(const RPCError &);

public:
	RPCFailHandlerPlain(CallbackType onFail) : _onFail(onFail) {
	}
	bool operator()(mtpRequestId requestId, const RPCError &e) override {
		return (*_onFail)(e);
	}

private:
	CallbackType _onFail;

};

class RPCFailHandlerReq : public RPCAbstractFailHandler { // fail(error, req_id)
	using CallbackType = bool (*)(const RPCError &, mtpRequestId);

public:
	RPCFailHandlerReq(CallbackType onFail) : _onFail(onFail) {
	}
	bool operator()(mtpRequestId requestId, const RPCError &e) override {
		return (*_onFail)(e, requestId);
	}

private:
	CallbackType _onFail;

};

class RPCFailHandlerNo : public RPCAbstractFailHandler { // fail()
	using CallbackType = bool (*)();

public:
	RPCFailHandlerNo(CallbackType onFail) : _onFail(onFail) {
	}
	bool operator()(mtpRequestId requestId, const RPCError &e) override {
		return (*_onFail)();
	}

private:
	CallbackType _onFail;

};

class RPCFailHandlerNoReq : public RPCAbstractFailHandler { // fail(req_id)
	using CallbackType = bool (*)(mtpRequestId);

public:
	RPCFailHandlerNoReq(CallbackType onFail) : _onFail(onFail) {
	}
	bool operator()(mtpRequestId requestId, const RPCError &e) override {
		return (*_onFail)(requestId);
	}

private:
	CallbackType _onFail;

};

struct RPCCallbackClear {
	RPCCallbackClear(mtpRequestId id, int32 code = RPCError::NoError)
	: requestId(id)
	, errorCode(code) {
	}

	mtpRequestId requestId = 0;
	int32 errorCode = 0;

};

inline RPCDoneHandlerPtr rpcDone(bool (*onDone)(const mtpPrime *, const mtpPrime *)) { // done(from, end)
	return RPCDoneHandlerPtr(new RPCDoneHandlerBare(onDone));
}

inline RPCDoneHandlerPtr rpcDone(bool (*onDone)(const mtpPrime *, const mtpPrime *, mtpRequestId)) { // done(from, end, req_id)
	return RPCDoneHandlerPtr(new RPCDoneHandlerBareReq(onDone));
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

using MTPStateChangedHandler = void (*)(int32 dcId, int32 state);
using MTPSessionResetHandler = void (*)(int32 dcId);

template <typename Base, typename FunctionType>
class RPCHandlerImplementation : public Base {
protected:
	using Lambda = FnMut<FunctionType>;
	using Parent = RPCHandlerImplementation<Base, FunctionType>;

public:
	RPCHandlerImplementation(Lambda handler) : _handler(std::move(handler)) {
	}

protected:
	Lambda _handler;

};

template <typename FunctionType>
using RPCDoneHandlerImplementation = RPCHandlerImplementation<RPCAbstractDoneHandler, FunctionType>;

class RPCDoneHandlerImplementationBare : public RPCDoneHandlerImplementation<bool(const mtpPrime*, const mtpPrime*)> { // done(from, end)
public:
	using RPCDoneHandlerImplementation<bool(const mtpPrime*, const mtpPrime*)>::Parent::Parent;
	bool operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) override {
		return this->_handler ? this->_handler(from, end) : true;
	}

};

class RPCDoneHandlerImplementationBareReq : public RPCDoneHandlerImplementation<bool(const mtpPrime*, const mtpPrime*, mtpRequestId)> { // done(from, end, req_id)
public:
	using RPCDoneHandlerImplementation<bool(const mtpPrime*, const mtpPrime*, mtpRequestId)>::Parent::Parent;
	bool operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) override {
		return this->_handler ? this->_handler(from, end, requestId) : true;
	}

};

template <typename R, typename TResponse>
class RPCDoneHandlerImplementationPlain : public RPCDoneHandlerImplementation<R(const TResponse&)> { // done(result)
public:
	using RPCDoneHandlerImplementation<R(const TResponse&)>::Parent::Parent;
	bool operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) override {
		auto response = TResponse();
		if (!response.read(from, end)) {
			return false;
		}
		if (this->_handler) {
			this->_handler(std::move(response));
		}
		return true;
	}

};

template <typename R, typename TResponse>
class RPCDoneHandlerImplementationReq : public RPCDoneHandlerImplementation<R(const TResponse&, mtpRequestId)> { // done(result, req_id)
public:
	using RPCDoneHandlerImplementation<R(const TResponse&, mtpRequestId)>::Parent::Parent;
	bool operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) override {
		auto response = TResponse();
		if (!response.read(from, end)) {
			return false;
		}
		if (this->_handler) {
			this->_handler(std::move(response), requestId);
		}
		return true;
	}

};

template <typename R>
class RPCDoneHandlerImplementationNo : public RPCDoneHandlerImplementation<R()> { // done()
public:
	using RPCDoneHandlerImplementation<R()>::Parent::Parent;
	bool operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) override {
		if (this->_handler) {
			this->_handler();
		}
		return true;
	}

};

template <typename R>
class RPCDoneHandlerImplementationNoReq : public RPCDoneHandlerImplementation<R(mtpRequestId)> { // done(req_id)
public:
	using RPCDoneHandlerImplementation<R(mtpRequestId)>::Parent::Parent;
	bool operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) override {
		if (this->_handler) {
			this->_handler(requestId);
		}
		return true;
	}

};

template <typename Lambda>
constexpr bool rpcDone_canCallBare_v = rpl::details::is_callable_plain_v<
	Lambda, const mtpPrime*, const mtpPrime*>;

template <typename Lambda>
constexpr bool rpcDone_canCallBareReq_v = rpl::details::is_callable_plain_v<
	Lambda, const mtpPrime*, const mtpPrime*, mtpRequestId>;

template <typename Lambda>
constexpr bool rpcDone_canCallNo_v = rpl::details::is_callable_plain_v<
	Lambda>;

template <typename Lambda>
constexpr bool rpcDone_canCallNoReq_v = rpl::details::is_callable_plain_v<
	Lambda, mtpRequestId>;

template <typename Function>
struct rpcDone_canCallPlain : std::false_type {
};

template <typename Lambda, typename Return, typename T>
struct rpcDone_canCallPlain<Return(Lambda::*)(const T&)> : std::true_type {
	using Arg = T;
};

template <typename Lambda, typename Return, typename T>
struct rpcDone_canCallPlain<Return(Lambda::*)(const T&)const>
	: rpcDone_canCallPlain<Return(Lambda::*)(const T&)> {
};

template <typename Function>
constexpr bool rpcDone_canCallPlain_v = rpcDone_canCallPlain<Function>::value;

template <typename Function>
struct rpcDone_canCallReq : std::false_type {
};

template <typename Lambda, typename Return, typename T>
struct rpcDone_canCallReq<Return(Lambda::*)(const T&, mtpRequestId)> : std::true_type {
	using Arg = T;
};

template <typename Lambda, typename Return, typename T>
struct rpcDone_canCallReq<Return(Lambda::*)(const T&, mtpRequestId)const>
	: rpcDone_canCallReq<Return(Lambda::*)(const T&, mtpRequestId)> {
};

template <typename Function>
constexpr bool rpcDone_canCallReq_v = rpcDone_canCallReq<Function>::value;

template <typename Function>
struct rpcDone_returnType;

template <typename Lambda, typename Return, typename ...Args>
struct rpcDone_returnType<Return(Lambda::*)(Args...)> {
	using type = Return;
};

template <typename Lambda, typename Return, typename ...Args>
struct rpcDone_returnType<Return(Lambda::*)(Args...)const> {
	using type = Return;
};

template <typename Function>
using rpcDone_returnType_t = typename rpcDone_returnType<Function>::type;

template <
	typename Lambda,
	typename Function = crl::deduced_call_type<Lambda>>
RPCDoneHandlerPtr rpcDone(Lambda lambda) {
	using R = rpcDone_returnType_t<Function>;
	if constexpr (rpcDone_canCallBare_v<Lambda>) {
		return RPCDoneHandlerPtr(new RPCDoneHandlerImplementationBare(std::move(lambda)));
	} else if constexpr (rpcDone_canCallBareReq_v<Lambda>) {
		return RPCDoneHandlerPtr(new RPCDoneHandlerImplementationBareReq(std::move(lambda)));
	} else if constexpr (rpcDone_canCallNo_v<Lambda>) {
		return RPCDoneHandlerPtr(new RPCDoneHandlerImplementationNo<R>(std::move(lambda)));
	} else if constexpr (rpcDone_canCallNoReq_v<Lambda>) {
		return RPCDoneHandlerPtr(new RPCDoneHandlerImplementationNoReq<R>(std::move(lambda)));
	} else if constexpr (rpcDone_canCallPlain_v<Function>) {
		using T = typename rpcDone_canCallPlain<Function>::Arg;
		return RPCDoneHandlerPtr(new RPCDoneHandlerImplementationPlain<R, T>(std::move(lambda)));
	} else if constexpr (rpcDone_canCallReq_v<Function>) {
		using T = typename rpcDone_canCallReq<Function>::Arg;
		return RPCDoneHandlerPtr(new RPCDoneHandlerImplementationReq<R, T>(std::move(lambda)));
	} else {
		static_assert(false_t(lambda), "Unknown method.");
	}
}

template <typename FunctionType>
using RPCFailHandlerImplementation = RPCHandlerImplementation<RPCAbstractFailHandler, FunctionType>;

class RPCFailHandlerImplementationPlain : public RPCFailHandlerImplementation<bool(const RPCError&)> { // fail(error)
public:
	using Parent::Parent;
	bool operator()(mtpRequestId requestId, const RPCError &error) override {
		return _handler ? _handler(error) : true;
	}

};

class RPCFailHandlerImplementationReq : public RPCFailHandlerImplementation<bool(const RPCError&, mtpRequestId)> { // fail(error, req_id)
public:
	using Parent::Parent;
	bool operator()(mtpRequestId requestId, const RPCError &error) override {
		return this->_handler ? this->_handler(error, requestId) : true;
	}

};

class RPCFailHandlerImplementationNo : public RPCFailHandlerImplementation<bool()> { // fail()
public:
	using Parent::Parent;
	bool operator()(mtpRequestId requestId, const RPCError &error) override {
		return this->_handler ? this->_handler() : true;
	}

};

class RPCFailHandlerImplementationNoReq : public RPCFailHandlerImplementation<bool(mtpRequestId)> { // fail(req_id)
public:
	using Parent::Parent;
	bool operator()(mtpRequestId requestId, const RPCError &error) override {
		return this->_handler ? this->_handler(requestId) : true;
	}

};

template <typename Lambda>
constexpr bool rpcFail_canCallNo_v = rpl::details::is_callable_plain_v<
	Lambda>;

template <typename Lambda>
constexpr bool rpcFail_canCallNoReq_v = rpl::details::is_callable_plain_v<
	Lambda, mtpRequestId>;

template <typename Lambda>
constexpr bool rpcFail_canCallPlain_v = rpl::details::is_callable_plain_v<
	Lambda, const RPCError&>;

template <typename Lambda>
constexpr bool rpcFail_canCallReq_v = rpl::details::is_callable_plain_v<
	Lambda, const RPCError&, mtpRequestId>;

template <
	typename Lambda,
	typename Function = crl::deduced_call_type<Lambda>>
RPCFailHandlerPtr rpcFail(Lambda lambda) {
	if constexpr (rpcFail_canCallNo_v<Lambda>) {
		return RPCFailHandlerPtr(new RPCFailHandlerImplementationNo(std::move(lambda)));
	} else if constexpr (rpcFail_canCallNoReq_v<Lambda>) {
		return RPCFailHandlerPtr(new RPCFailHandlerImplementationNoReq(std::move(lambda)));
	} else if constexpr (rpcFail_canCallPlain_v<Lambda>) {
		return RPCFailHandlerPtr(new RPCFailHandlerImplementationPlain(std::move(lambda)));
	} else if constexpr (rpcFail_canCallReq_v<Lambda>) {
		return RPCFailHandlerPtr(new RPCFailHandlerImplementationReq(std::move(lambda)));
	} else {
		static_assert(false_t(lambda), "Unknown method.");
	}
}
