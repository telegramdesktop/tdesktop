/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/variant.h"
#include "mtproto/mtproto_rpc_sender.h"
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
		using FailPlainHandler = FnMut<void(const RPCError &error)>;
		using FailRequestIdHandler = FnMut<void(const RPCError &error, mtpRequestId requestId)>;
		enum class FailSkipPolicy {
			Simple,
			HandleFlood,
			HandleAll,
		};
		template <typename Response>
		struct DonePlainPolicy {
			using Callback = FnMut<void(const Response &result)>;
			static void handle(Callback &&handler, mtpRequestId requestId, Response &&result) {
				handler(result);
			}

		};
		template <typename Response>
		struct DoneRequestIdPolicy {
			using Callback = FnMut<void(const Response &result, mtpRequestId requestId)>;
			static void handle(Callback &&handler, mtpRequestId requestId, Response &&result) {
				handler(result, requestId);
			}

		};
		template <typename Response, template <typename> typename PolicyTemplate>
		class DoneHandler : public RPCAbstractDoneHandler {
			using Policy = PolicyTemplate<Response>;
			using Callback = typename Policy::Callback;

		public:
			DoneHandler(not_null<Sender*> sender, Callback handler) : _sender(sender), _handler(std::move(handler)) {
			}

			bool operator()(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) override {
				auto handler = std::move(_handler);
				_sender->senderRequestHandled(requestId);

				auto result = Response();
				if (!result.read(from, end)) {
					return false;
				}
				if (handler) {
					Policy::handle(std::move(handler), requestId, std::move(result));
				}
				return true;
			}

		private:
			not_null<Sender*> _sender;
			Callback _handler;

		};

		struct FailPlainPolicy {
			using Callback = FnMut<void(const RPCError &error)>;
			static void handle(Callback &&handler, mtpRequestId requestId, const RPCError &error) {
				handler(error);
			}

		};
		struct FailRequestIdPolicy {
			using Callback = FnMut<void(const RPCError &error, mtpRequestId requestId)>;
			static void handle(Callback &&handler, mtpRequestId requestId, const RPCError &error) {
				handler(error, requestId);
			}

		};
		template <typename Policy>
		class FailHandler : public RPCAbstractFailHandler {
			using Callback = typename Policy::Callback;

		public:
			FailHandler(not_null<Sender*> sender, Callback handler, FailSkipPolicy skipPolicy)
				: _sender(sender)
				, _handler(std::move(handler))
				, _skipPolicy(skipPolicy) {
			}

			bool operator()(mtpRequestId requestId, const RPCError &error) override {
				if (_skipPolicy == FailSkipPolicy::Simple) {
					if (isDefaultHandledError(error)) {
						return false;
					}
				} else if (_skipPolicy == FailSkipPolicy::HandleFlood) {
					if (isDefaultHandledError(error) && !isFloodError(error)) {
						return false;
					}
				}

				auto handler = std::move(_handler);
				_sender->senderRequestHandled(requestId);

				if (handler) {
					Policy::handle(std::move(handler), requestId, error);
				}
				return true;
			}

		private:
			not_null<Sender*> _sender;
			Callback _handler;
			FailSkipPolicy _skipPolicy = FailSkipPolicy::Simple;

		};

		explicit RequestBuilder(not_null<Sender*> sender) noexcept : _sender(sender) {
		}
		RequestBuilder(RequestBuilder &&other) = default;

		void setToDC(ShiftedDcId dcId) noexcept {
			_dcId = dcId;
		}
		void setCanWait(crl::time ms) noexcept {
			_canWait = ms;
		}
		void setDoneHandler(RPCDoneHandlerPtr &&handler) noexcept {
			_done = std::move(handler);
		}
		void setFailHandler(FailPlainHandler &&handler) noexcept {
			_fail = std::move(handler);
		}
		void setFailHandler(FailRequestIdHandler &&handler) noexcept {
			_fail = std::move(handler);
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
		RPCDoneHandlerPtr takeOnDone() noexcept {
			return std::move(_done);
		}
		RPCFailHandlerPtr takeOnFail() {
			return v::match(_fail, [&](FailPlainHandler &value)
			-> RPCFailHandlerPtr {
				return std::make_shared<FailHandler<FailPlainPolicy>>(
					_sender,
					std::move(value),
					_failSkipPolicy);
			}, [&](FailRequestIdHandler &value) -> RPCFailHandlerPtr {
				return std::make_shared<FailHandler<FailRequestIdPolicy>>(
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
		RPCDoneHandlerPtr _done;
		std::variant<FailPlainHandler, FailRequestIdHandler> _fail;
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
		SpecificRequestBuilder(not_null<Sender*> sender, Request &&request) noexcept : RequestBuilder(sender), _request(std::move(request)) {
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
		[[nodiscard]] SpecificRequestBuilder &done(FnMut<void(const typename Request::ResponseType &result)> callback) {
			setDoneHandler(std::make_shared<DoneHandler<typename Request::ResponseType, DonePlainPolicy>>(sender(), std::move(callback)));
			return *this;
		}
		[[nodiscard]] SpecificRequestBuilder &done(FnMut<void(const typename Request::ResponseType &result, mtpRequestId requestId)> callback) {
			setDoneHandler(std::make_shared<DoneHandler<typename Request::ResponseType, DoneRequestIdPolicy>>(sender(), std::move(callback)));
			return *this;
		}
		[[nodiscard]] SpecificRequestBuilder &fail(FnMut<void(const RPCError &error)> callback) noexcept {
			setFailHandler(std::move(callback));
			return *this;
		}
		[[nodiscard]] SpecificRequestBuilder &fail(FnMut<void(const RPCError &error, mtpRequestId requestId)> callback) noexcept {
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
