/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/bytes.h"
#include "base/weak_ptr.h"
#include "base/flat_map.h"
#include "mtproto/core_types.h"
#include "mtproto/details/mtproto_serialized_request.h"

#include <QtCore/QPointer>
#include <rpl/details/callable.h>

#ifndef _DEBUG
#define MTP_SENDER_USE_GENERIC_HANDLERS
#endif // !_DEBUG


namespace MTP {

class Error;
class Instance;

class ConcurrentSender : public base::has_weak_ptr {
	template <typename ...Args>
	static constexpr bool is_callable_v
		= rpl::details::is_callable_v<Args...>;

	template <typename Method>
	auto with_instance(Method &&method)
	-> std::enable_if_t<is_callable_v<Method, not_null<Instance*>>>;

	struct Handlers {
		FnMut<bool(mtpRequestId requestId, bytes::const_span result)> done;
		FnMut<void(mtpRequestId requestId, const Error &error)> fail;
	};

	enum class FailSkipPolicy {
		Simple,
		HandleFlood,
		HandleAll,
	};

	class RequestBuilder {
	public:
		RequestBuilder(const RequestBuilder &other) = delete;
		RequestBuilder(RequestBuilder &&other) = default;
		RequestBuilder &operator=(const RequestBuilder &other) = delete;
		RequestBuilder &operator=(RequestBuilder &&other) = delete;

		mtpRequestId send();

	protected:
		RequestBuilder(
			not_null<ConcurrentSender*> sender,
			details::SerializedRequest &&serialized) noexcept;

		void setToDC(ShiftedDcId dcId) noexcept;
		void setCanWait(crl::time ms) noexcept;
		template <typename Response, typename InvokeFullDone>
		void setDoneHandler(InvokeFullDone &&invoke) noexcept;
		template <typename InvokeFullFail>
		void setFailHandler(InvokeFullFail &&invoke) noexcept;
		void setFailSkipPolicy(FailSkipPolicy policy) noexcept;
		void setAfter(mtpRequestId requestId) noexcept;

	private:
		not_null<ConcurrentSender*> _sender;
		details::SerializedRequest _serialized;
		ShiftedDcId _dcId = 0;
		crl::time _canWait = 0;

		Handlers _handlers;
		FailSkipPolicy _failSkipPolicy = FailSkipPolicy::Simple;
		mtpRequestId _afterRequestId = 0;

	};

public:
	ConcurrentSender(
		QPointer<Instance> weak,
		Fn<void(FnMut<void()>)> runner);

	template <typename Request>
	class SpecificRequestBuilder : public RequestBuilder {
	public:
		using Result = typename Request::ResponseType;

		SpecificRequestBuilder(
			const SpecificRequestBuilder &other) = delete;
		SpecificRequestBuilder(
			SpecificRequestBuilder &&other) = default;
		SpecificRequestBuilder &operator=(
			const SpecificRequestBuilder &other) = delete;
		SpecificRequestBuilder &operator=(
			SpecificRequestBuilder &&other) = delete;

		[[nodiscard]] SpecificRequestBuilder &toDC(
			ShiftedDcId dcId) noexcept;
		[[nodiscard]] SpecificRequestBuilder &afterDelay(
			crl::time ms) noexcept;

#ifndef MTP_SENDER_USE_GENERIC_HANDLERS
		// Allow code completion to show response type.
		[[nodiscard]] SpecificRequestBuilder &done(FnMut<void()> &&handler);
		[[nodiscard]] SpecificRequestBuilder &done(
			FnMut<void(mtpRequestId, Result &&)> &&handler);
		[[nodiscard]] SpecificRequestBuilder &done(
			FnMut<void(Result &&)> &&handler);
		[[nodiscard]] SpecificRequestBuilder &fail(Fn<void()> &&handler);
		[[nodiscard]] SpecificRequestBuilder &fail(
			Fn<void(mtpRequestId, const Error &)> &&handler);
		[[nodiscard]] SpecificRequestBuilder &fail(
			Fn<void(const Error &)> &&handler);
#else // !MTP_SENDER_USE_GENERIC_HANDLERS
		template <typename Handler>
		[[nodiscard]] SpecificRequestBuilder &done(Handler &&handler);
		template <typename Handler>
		[[nodiscard]] SpecificRequestBuilder &fail(Handler &&handler);
#endif // MTP_SENDER_USE_GENERIC_HANDLERS

		[[nodiscard]] SpecificRequestBuilder &handleFloodErrors() noexcept;
		[[nodiscard]] SpecificRequestBuilder &handleAllErrors() noexcept;
		[[nodiscard]] SpecificRequestBuilder &afterRequest(
			mtpRequestId requestId) noexcept;

	private:
		SpecificRequestBuilder(
			not_null<ConcurrentSender*> sender,
			Request &&request) noexcept;

		friend class ConcurrentSender;

	};

	class SentRequestWrap {
	public:
		void cancel();
		void detach();

	private:
		friend class ConcurrentSender;
		SentRequestWrap(
			not_null<ConcurrentSender*> sender,
			mtpRequestId requestId);

		not_null<ConcurrentSender*> _sender;
		mtpRequestId _requestId = 0;

	};

	template <
		typename Request,
		typename = std::enable_if_t<!std::is_reference_v<Request>>,
		typename = typename Request::Unboxed>
	[[nodiscard]] SpecificRequestBuilder<Request> request(
		Request &&request) noexcept;

	[[nodiscard]] SentRequestWrap request(mtpRequestId requestId) noexcept;

	[[nodiscard]] auto requestCanceller() noexcept;

	~ConcurrentSender();

private:
	class HandlerMaker;
	friend class HandlerMaker;
	friend class RequestBuilder;
	friend class SentRequestWrap;

	void senderRequestRegister(mtpRequestId requestId, Handlers &&handlers);
	void senderRequestDone(
		mtpRequestId requestId,
		bytes::const_span result);
	void senderRequestFail(
		mtpRequestId requestId,
		const Error &error);
	void senderRequestCancel(mtpRequestId requestId);
	void senderRequestCancelAll();
	void senderRequestDetach(mtpRequestId requestId);

	const QPointer<Instance> _weak;
	const Fn<void(FnMut<void()>)> _runner;
	base::flat_map<mtpRequestId, Handlers> _requests;

};

template <typename Result, typename InvokeFullDone>
void ConcurrentSender::RequestBuilder::setDoneHandler(
	InvokeFullDone &&invoke
) noexcept {
	_handlers.done = [handler = std::move(invoke)](
			mtpRequestId requestId,
			bytes::const_span result) mutable {
		auto from = reinterpret_cast<const mtpPrime*>(result.data());
		const auto end = from + result.size() / sizeof(mtpPrime);
		Result data;
		if (!data.read(from, end)) {
			return false;
		}
		handler(requestId, std::move(data));
		return true;
	};
}

template <typename InvokeFullFail>
void ConcurrentSender::RequestBuilder::setFailHandler(
	InvokeFullFail &&invoke
) noexcept {
	_handlers.fail = std::move(invoke);
}

template <typename Request>
ConcurrentSender::SpecificRequestBuilder<Request>::SpecificRequestBuilder(
	not_null<ConcurrentSender*> sender,
	Request &&request) noexcept
: RequestBuilder(sender, details::SerializedRequest::Serialize(request)) {
}

template <typename Request>
auto ConcurrentSender::SpecificRequestBuilder<Request>::toDC(
	ShiftedDcId dcId
) noexcept -> SpecificRequestBuilder & {
	setToDC(dcId);
	return *this;
}

template <typename Request>
auto ConcurrentSender::SpecificRequestBuilder<Request>::afterDelay(
	crl::time ms
) noexcept -> SpecificRequestBuilder & {
	setCanWait(ms);
	return *this;
}

#ifndef MTP_SENDER_USE_GENERIC_HANDLERS
// Allow code completion to show response type.
template <typename Request>
auto ConcurrentSender::SpecificRequestBuilder<Request>::done(
	FnMut<void(Result &&)> &&handler
) -> SpecificRequestBuilder & {
	setDoneHandler<Result>([handler = std::move(handler)](
			mtpRequestId requestId,
			Result &&result) mutable {
		handler(std::move(result));
	});
	return *this;
}

template <typename Request>
auto ConcurrentSender::SpecificRequestBuilder<Request>::done(
	FnMut<void(mtpRequestId, Result &&)> &&handler
) -> SpecificRequestBuilder & {
	setDoneHandler<Result>(std::move(handler));
	return *this;
}

template <typename Request>
auto ConcurrentSender::SpecificRequestBuilder<Request>::done(
	FnMut<void()> &&handler
) -> SpecificRequestBuilder & {
	setDoneHandler<Result>([handler = std::move(handler)](
			mtpRequestId requestId,
			Result &&result) mutable {
		std::move(handler)();
	});
	return *this;
}

template <typename Request>
auto ConcurrentSender::SpecificRequestBuilder<Request>::fail(
	Fn<void(const Error &)> &&handler
) -> SpecificRequestBuilder & {
	setFailHandler([handler = std::move(handler)](
			mtpRequestId requestId,
			const Error &error) {
		handler(error);
	});
	return *this;
}

template <typename Request>
auto ConcurrentSender::SpecificRequestBuilder<Request>::fail(
	Fn<void(mtpRequestId, const Error &)> &&handler
) -> SpecificRequestBuilder & {
	setFailHandler(std::move(handler));
	return *this;
}

template <typename Request>
auto ConcurrentSender::SpecificRequestBuilder<Request>::fail(
	Fn<void()> &&handler
) -> SpecificRequestBuilder & {
	setFailHandler([handler = std::move(handler)](
			mtpRequestId requestId,
			const Error &error) {
		handler();
	});
	return *this;
}
#else // !MTP_SENDER_USE_GENERIC_HANDLERS
template <typename Request>
template <typename Handler>
auto ConcurrentSender::SpecificRequestBuilder<Request>::done(
	Handler &&handler
) -> SpecificRequestBuilder & {
	using Result = typename Request::ResponseType;
	constexpr auto takesFull = rpl::details::is_callable_plain_v<
		Handler,
		mtpRequestId,
		Result>;
	[[maybe_unused]] constexpr auto takesResponse = rpl::details::is_callable_plain_v<
		Handler,
		Result>;
	[[maybe_unused]] constexpr auto takesNone = rpl::details::is_callable_plain_v<Handler>;

	if constexpr (takesFull) {
		setDoneHandler<Result>(std::forward<Handler>(handler));
	} else if constexpr (takesResponse) {
		setDoneHandler<Result>([handler = std::forward<Handler>(handler)](
				mtpRequestId requestId,
				Result &&result) mutable {
			handler(std::move(result));
		});
	} else if constexpr (takesNone) {
		setDoneHandler<Result>([handler = std::forward<Handler>(handler)](
				mtpRequestId requestId,
				Result &&result) mutable {
			handler();
		});
	} else {
		static_assert(false_t(Handler{}), "Bad done handler.");
	}
	return *this;
}

template <typename Request>
template <typename Handler>
auto ConcurrentSender::SpecificRequestBuilder<Request>::fail(
	Handler &&handler
) -> SpecificRequestBuilder & {
	constexpr auto takesFull = rpl::details::is_callable_plain_v<
		Handler,
		mtpRequestId,
		Error>;
	[[maybe_unused]] constexpr auto takesError = rpl::details::is_callable_plain_v<
		Handler,
		Error>;
	[[maybe_unused]] constexpr auto takesNone = rpl::details::is_callable_plain_v<Handler>;

	if constexpr (takesFull) {
		setFailHandler(std::forward<Handler>(handler));
	} else if constexpr (takesError) {
		setFailHandler([handler = std::forward<Handler>(handler)](
				mtpRequestId requestId,
				const Error &error) {
			handler(error);
		});
	} else if constexpr (takesNone) {
		setFailHandler([handler = std::forward<Handler>(handler)](
				mtpRequestId requestId,
				const Error &error) {
			handler();
		});
	} else {
		static_assert(false_t(Handler{}), "Bad fail handler.");
	}
	return *this;
}

#endif // MTP_SENDER_USE_GENERIC_HANDLERS

template <typename Request>
auto ConcurrentSender::SpecificRequestBuilder<Request>::handleFloodErrors(
) noexcept -> SpecificRequestBuilder & {
	setFailSkipPolicy(FailSkipPolicy::HandleFlood);
	return *this;
}

template <typename Request>
auto ConcurrentSender::SpecificRequestBuilder<Request>::handleAllErrors(
) noexcept -> SpecificRequestBuilder & {
	setFailSkipPolicy(FailSkipPolicy::HandleAll);
	return *this;
}

template <typename Request>
auto ConcurrentSender::SpecificRequestBuilder<Request>::afterRequest(
	mtpRequestId requestId
) noexcept -> SpecificRequestBuilder & {
	setAfter(requestId);
	return *this;
}

inline void ConcurrentSender::SentRequestWrap::cancel() {
	_sender->senderRequestCancel(_requestId);
}

inline void ConcurrentSender::SentRequestWrap::detach() {
	_sender->senderRequestDetach(_requestId);
}

inline ConcurrentSender::SentRequestWrap::SentRequestWrap(
	not_null<ConcurrentSender*> sender,
	mtpRequestId requestId
) : _sender(sender)
, _requestId(requestId) {
}

template <typename Request, typename, typename>
inline auto ConcurrentSender::request(Request &&request) noexcept
-> SpecificRequestBuilder<Request> {
	return SpecificRequestBuilder<Request>(this, std::move(request));
}

inline auto ConcurrentSender::requestCanceller() noexcept {
	return [=](mtpRequestId requestId) {
		request(requestId).cancel();
	};
}

inline auto ConcurrentSender::request(mtpRequestId requestId) noexcept
-> SentRequestWrap {
	return SentRequestWrap(this, requestId);
}

} // namespace MTP
