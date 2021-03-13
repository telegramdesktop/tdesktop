/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/variant.h"
#include "mtproto/mtproto_response.h"
#include "mtproto/mtp_instance.h"
#include "mtproto/facade.h"

namespace MTP {

class Sender {
	class RequestBuilder {
	public:
		RequestBuilder(const RequestBuilder &other) = delete;
		RequestBuilder &operator=(const RequestBuilder &other) = delete;
		RequestBuilder &operator=(RequestBuilder &&other) = delete;

	protected:
		enum class FailSkipPolicy {
			Simple,
			HandleFlood,
			HandleAll,
		};
		using FailPlainHandler = Fn<void()>;
		using FailErrorHandler = Fn<void(const Error&)>;
		using FailRequestIdHandler = Fn<void(const Error&, mtpRequestId)>;
		using FailFullHandler = Fn<void(const Error&, const Response&)>;

		template <typename ...Args>
		static constexpr bool IsCallable
			= rpl::details::is_callable_plain_v<Args...>;

		template <typename Result, typename Handler>
		[[nodiscard]] DoneHandler MakeDoneHandler(
				not_null<Sender*> sender,
				Handler &&handler) {
			return [sender, handler = std::forward<Handler>(handler)](
					const Response &response) mutable {
				auto onstack = std::move(handler);
				sender->senderRequestHandled(response.requestId);

				auto result = Result();
				auto from = response.reply.constData();
				if (!result.read(from, from + response.reply.size())) {
					return false;
				} else if (!onstack) {
					return true;
				} else if constexpr (IsCallable<
						Handler,
						const Result&,
						const Response&>) {
					onstack(result, response);
				} else if constexpr (IsCallable<
						Handler,
						const Result&,
						mtpRequestId>) {
					onstack(result, response.requestId);
				} else if constexpr (IsCallable<
						Handler,
						const Result&>) {
					onstack(result);
				} else if constexpr (IsCallable<Handler>) {
					onstack();
				} else {
					static_assert(false_t(Handler{}), "Bad done handler.");
				}
				return true;
			};
		}

		template <typename Handler>
		[[nodiscard]] FailHandler MakeFailHandler(
				not_null<Sender*> sender,
				Handler &&handler,
				FailSkipPolicy skipPolicy) {
			return [
				sender,
				handler = std::forward<Handler>(handler),
				skipPolicy
			](const Error &error, const Response &response) {
				if (skipPolicy == FailSkipPolicy::Simple) {
					if (IsDefaultHandledError(error)) {
						return false;
					}
				} else if (skipPolicy == FailSkipPolicy::HandleFlood) {
					if (IsDefaultHandledError(error) && !IsFloodError(error)) {
						return false;
					}
				}

				auto onstack = handler;
				sender->senderRequestHandled(response.requestId);

				if (!onstack) {
					return true;
				} else if constexpr (IsCallable<
						Handler,
						const Error&,
						const Response&>) {
					onstack(error, response);
				} else if constexpr (IsCallable<
						Handler,
						const Error&,
						mtpRequestId>) {
					onstack(error, response.requestId);
				} else if constexpr (IsCallable<
						Handler,
						const Error&>) {
					onstack(error);
				} else if constexpr (IsCallable<Handler>) {
					onstack();
				} else {
					static_assert(false_t(Handler{}), "Bad fail handler.");
				}
				return true;
			};
		}

		explicit RequestBuilder(not_null<Sender*> sender) noexcept
		: _sender(sender) {
		}
		RequestBuilder(RequestBuilder &&other) = default;

		void setToDC(ShiftedDcId dcId) noexcept {
			_dcId = dcId;
		}
		void setCanWait(crl::time ms) noexcept {
			_canWait = ms;
		}
		void setDoneHandler(DoneHandler &&handler) noexcept {
			_done = std::move(handler);
		}
		template <typename Handler>
		void setFailHandler(Handler &&handler) noexcept {
			_fail = std::forward<Handler>(handler);
		}
		void setFailSkipPolicy(FailSkipPolicy policy) noexcept {
			_failSkipPolicy = policy;
		}
		void setAfter(mtpRequestId requestId) noexcept {
			_afterRequestId = requestId;
		}

		ShiftedDcId takeDcId() const noexcept {
			return _dcId;
		}
		crl::time takeCanWait() const noexcept {
			return _canWait;
		}
		DoneHandler takeOnDone() noexcept {
			return std::move(_done);
		}
		FailHandler takeOnFail() {
			return v::match(_fail, [&](auto &value) {
				return MakeFailHandler(
					_sender,
					std::move(value),
					_failSkipPolicy);
			});
		}
		mtpRequestId takeAfter() const noexcept {
			return _afterRequestId;
		}

		not_null<Sender*> sender() const noexcept {
			return _sender;
		}
		void registerRequest(mtpRequestId requestId) {
			_sender->senderRequestRegister(requestId);
		}

	private:
		not_null<Sender*> _sender;
		ShiftedDcId _dcId = 0;
		crl::time _canWait = 0;
		DoneHandler _done;
		std::variant<
			FailPlainHandler,
			FailErrorHandler,
			FailRequestIdHandler,
			FailFullHandler> _fail;
		FailSkipPolicy _failSkipPolicy = FailSkipPolicy::Simple;
		mtpRequestId _afterRequestId = 0;

	};

public:
	explicit Sender(not_null<Instance*> instance) noexcept
	: _instance(instance) {
	}

	[[nodiscard]] Instance &instance() const {
		return *_instance;
	}

	template <typename Request>
	class SpecificRequestBuilder : public RequestBuilder {
	private:
		friend class Sender;
		SpecificRequestBuilder(not_null<Sender*> sender, Request &&request) noexcept
		: RequestBuilder(sender)
		, _request(std::move(request)) {
		}
		SpecificRequestBuilder(SpecificRequestBuilder &&other) = default;

	public:
		[[nodiscard]] SpecificRequestBuilder &toDC(ShiftedDcId dcId) noexcept {
			setToDC(dcId);
			return *this;
		}
		[[nodiscard]] SpecificRequestBuilder &afterDelay(crl::time ms) noexcept {
			setCanWait(ms);
			return *this;
		}

		using Result = typename Request::ResponseType;
		[[nodiscard]] SpecificRequestBuilder &done(
			FnMut<void(
				const Result &result,
				mtpRequestId requestId)> callback) {
			setDoneHandler(
				MakeDoneHandler<Result>(sender(), std::move(callback)));
			return *this;
		}
		[[nodiscard]] SpecificRequestBuilder &done(
			FnMut<void(
				const Result &result,
				const Response &response)> callback) {
			setDoneHandler(
				MakeDoneHandler<Result>(sender(), std::move(callback)));
			return *this;
		}
		[[nodiscard]] SpecificRequestBuilder &done(
				FnMut<void()> callback) {
			setDoneHandler(
				MakeDoneHandler<Result>(sender(), std::move(callback)));
			return *this;
		}
		[[nodiscard]] SpecificRequestBuilder &done(
			FnMut<void(
				const typename Request::ResponseType &result)> callback) {
			setDoneHandler(
				MakeDoneHandler<Result>(sender(), std::move(callback)));
			return *this;
		}

		[[nodiscard]] SpecificRequestBuilder &fail(
			Fn<void(
				const Error &error,
				mtpRequestId requestId)> callback) noexcept {
			setFailHandler(std::move(callback));
			return *this;
		}
		[[nodiscard]] SpecificRequestBuilder &fail(
			Fn<void(
				const Error &error,
				const Response &response)> callback) noexcept {
			setFailHandler(std::move(callback));
			return *this;
		}
		[[nodiscard]] SpecificRequestBuilder &fail(
				Fn<void()> callback) noexcept {
			setFailHandler(std::move(callback));
			return *this;
		}
		[[nodiscard]] SpecificRequestBuilder &fail(
				Fn<void(const Error &error)> callback) noexcept {
			setFailHandler(std::move(callback));
			return *this;
		}

		[[nodiscard]] SpecificRequestBuilder &handleFloodErrors() noexcept {
			setFailSkipPolicy(FailSkipPolicy::HandleFlood);
			return *this;
		}
		[[nodiscard]] SpecificRequestBuilder &handleAllErrors() noexcept {
			setFailSkipPolicy(FailSkipPolicy::HandleAll);
			return *this;
		}
		[[nodiscard]] SpecificRequestBuilder &afterRequest(mtpRequestId requestId) noexcept {
			setAfter(requestId);
			return *this;
		}

		mtpRequestId send() {
			const auto id = sender()->_instance->send(
				_request,
				takeOnDone(),
				takeOnFail(),
				takeDcId(),
				takeCanWait(),
				takeAfter());
			registerRequest(id);
			return id;
		}

	private:
		Request _request;

	};

	class SentRequestWrap {
	private:
		friend class Sender;
		SentRequestWrap(not_null<Sender*> sender, mtpRequestId requestId) : _sender(sender), _requestId(requestId) {
		}

	public:
		void cancel() {
			if (_requestId) {
				_sender->senderRequestCancel(_requestId);
			}
		}

	private:
		not_null<Sender*> _sender;
		mtpRequestId _requestId = 0;

	};

	template <
		typename Request,
		typename = std::enable_if_t<!std::is_reference_v<Request>>,
		typename = typename Request::Unboxed>
	[[nodiscard]] SpecificRequestBuilder<Request> request(Request &&request) noexcept;

	[[nodiscard]] SentRequestWrap request(mtpRequestId requestId) noexcept;

	[[nodiscard]] auto requestCanceller() noexcept {
		return [this](mtpRequestId requestId) {
			request(requestId).cancel();
		};
	}

	void requestSendDelayed() {
		_instance->sendAnything();
	}
	void requestCancellingDiscard() {
		for (auto &request : base::take(_requests)) {
			request.handled();
		}
	}

private:
	class RequestWrap {
	public:
		RequestWrap(
			not_null<Instance*> instance,
			mtpRequestId requestId) noexcept
		: _instance(instance)
		, _id(requestId) {
		}

		RequestWrap(const RequestWrap &other) = delete;
		RequestWrap &operator=(const RequestWrap &other) = delete;
		RequestWrap(RequestWrap &&other)
		: _instance(other._instance)
		, _id(base::take(other._id)) {
		}
		RequestWrap &operator=(RequestWrap &&other) {
			Expects(_instance == other._instance);

			if (_id != other._id) {
				cancelRequest();
				_id = base::take(other._id);
			}
			return *this;
		}

		mtpRequestId id() const noexcept {
			return _id;
		}
		void handled() const noexcept {
			_id = 0;
		}

		~RequestWrap() {
			cancelRequest();
		}

	private:
		void cancelRequest() {
			if (_id) {
				_instance->cancel(_id);
			}
		}
		const not_null<Instance*> _instance;
		mutable mtpRequestId _id = 0;

	};

	struct RequestWrapComparator {
		using is_transparent = std::true_type;

		struct helper {
			mtpRequestId requestId = 0;

			helper() = default;
			helper(const helper &other) = default;
			helper(mtpRequestId requestId) noexcept : requestId(requestId) {
			}
			helper(const RequestWrap &request) noexcept : requestId(request.id()) {
			}
			bool operator<(helper other) const {
				return requestId < other.requestId;
			}
		};
		bool operator()(const helper &&lhs, const helper &&rhs) const {
			return lhs < rhs;
		}

	};

	template <typename Request>
	friend class SpecificRequestBuilder;
	friend class RequestBuilder;
	friend class RequestWrap;
	friend class SentRequestWrap;

	void senderRequestRegister(mtpRequestId requestId) {
		_requests.emplace(_instance, requestId);
	}
	void senderRequestHandled(mtpRequestId requestId) {
		auto it = _requests.find(requestId);
		if (it != _requests.cend()) {
			it->handled();
			_requests.erase(it);
		}
	}
	void senderRequestCancel(mtpRequestId requestId) {
		auto it = _requests.find(requestId);
		if (it != _requests.cend()) {
			_requests.erase(it);
		}
	}

	const not_null<Instance*> _instance;
	base::flat_set<RequestWrap, RequestWrapComparator> _requests;

};

template <typename Request, typename, typename>
Sender::SpecificRequestBuilder<Request> Sender::request(Request &&request) noexcept {
	return SpecificRequestBuilder<Request>(this, std::move(request));
}

inline Sender::SentRequestWrap Sender::request(mtpRequestId requestId) noexcept {
	return SentRequestWrap(this, requestId);
}

} // namespace MTP
