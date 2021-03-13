/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/mtproto_concurrent_sender.h"

#include "mtproto/mtp_instance.h"
#include "mtproto/mtproto_response.h"
#include "mtproto/facade.h"

namespace MTP {

class ConcurrentSender::HandlerMaker final {
public:
	static DoneHandler MakeDone(
		not_null<ConcurrentSender*> sender,
		Fn<void(FnMut<void()>)> runner);
	static FailHandler MakeFail(
		not_null<ConcurrentSender*> sender,
		Fn<void(FnMut<void()>)> runner,
		FailSkipPolicy skipPolicy);
};

DoneHandler ConcurrentSender::HandlerMaker::MakeDone(
		not_null<ConcurrentSender*> sender,
		Fn<void(FnMut<void()>)> runner) {
	return [
		weak = base::make_weak(sender.get()),
		runner = std::move(runner)
	](const Response &response) mutable {
		runner([=]() mutable {
			if (const auto strong = weak.get()) {
				strong->senderRequestDone(
					response.requestId,
					bytes::make_span(response.reply));
			}
		});
		return true;
	};
}

FailHandler ConcurrentSender::HandlerMaker::MakeFail(
		not_null<ConcurrentSender*> sender,
		Fn<void(FnMut<void()>)> runner,
		FailSkipPolicy skipPolicy) {
	return [
		weak = base::make_weak(sender.get()),
		runner = std::move(runner),
		skipPolicy
	](const Error &error, const Response &response) mutable {
		if (skipPolicy == FailSkipPolicy::Simple) {
			if (IsDefaultHandledError(error)) {
				return false;
			}
		} else if (skipPolicy == FailSkipPolicy::HandleFlood) {
			if (IsDefaultHandledError(error) && !IsFloodError(error)) {
				return false;
			}
		}
		runner([=, requestId = response.requestId]() mutable {
			if (const auto strong = weak.get()) {
				strong->senderRequestFail(requestId, error);
			}
		});
		return true;
	};
}

template <typename Method>
auto ConcurrentSender::with_instance(Method &&method)
-> std::enable_if_t<is_callable_v<Method, not_null<Instance*>>> {
	crl::on_main([
		weak = _weak,
		method = std::forward<Method>(method)
	]() mutable {
		if (const auto instance = weak.data()) {
			std::move(method)(instance);
		}
	});
}

ConcurrentSender::RequestBuilder::RequestBuilder(
	not_null<ConcurrentSender*> sender,
	details::SerializedRequest &&serialized) noexcept
: _sender(sender)
, _serialized(std::move(serialized)) {
}

void ConcurrentSender::RequestBuilder::setToDC(ShiftedDcId dcId) noexcept {
	_dcId = dcId;
}

void ConcurrentSender::RequestBuilder::setCanWait(crl::time ms) noexcept {
	_canWait = ms;
}

void ConcurrentSender::RequestBuilder::setFailSkipPolicy(
		FailSkipPolicy policy) noexcept {
	_failSkipPolicy = policy;
}

void ConcurrentSender::RequestBuilder::setAfter(
		mtpRequestId requestId) noexcept {
	_afterRequestId = requestId;
}

mtpRequestId ConcurrentSender::RequestBuilder::send() {
	const auto requestId = details::GetNextRequestId();
	const auto dcId = _dcId;
	const auto msCanWait = _canWait;
	const auto afterRequestId = _afterRequestId;

	_sender->senderRequestRegister(requestId, std::move(_handlers));
	_sender->with_instance([
		=,
		request = std::move(_serialized),
		done = HandlerMaker::MakeDone(_sender, _sender->_runner),
		fail = HandlerMaker::MakeFail(
			_sender,
			_sender->_runner,
			_failSkipPolicy)
	](not_null<Instance*> instance) mutable {
		instance->sendSerialized(
			requestId,
			std::move(request),
			ResponseHandler{ std::move(done), std::move(fail) },
			dcId,
			msCanWait,
			afterRequestId);
	});

	return requestId;
}

ConcurrentSender::ConcurrentSender(
	QPointer<Instance> weak,
	Fn<void(FnMut<void()>)> runner)
: _weak(weak)
, _runner(runner) {
}

ConcurrentSender::~ConcurrentSender() {
	senderRequestCancelAll();
}

void ConcurrentSender::senderRequestRegister(
		mtpRequestId requestId,
		Handlers &&handlers) {
	_requests.emplace(requestId, std::move(handlers));
}

void ConcurrentSender::senderRequestDone(
		mtpRequestId requestId,
		bytes::const_span result) {
	if (auto handlers = _requests.take(requestId)) {
		if (!handlers->done(requestId, result)) {
			handlers->fail(
				requestId,
				Error::Local(
					"RESPONSE_PARSE_FAILED",
					"ConcurrentSender::senderRequestDone"));
		}
	}
}

void ConcurrentSender::senderRequestFail(
		mtpRequestId requestId,
		const Error &error) {
	if (auto handlers = _requests.take(requestId)) {
		handlers->fail(requestId, error);
	}
}

void ConcurrentSender::senderRequestCancel(mtpRequestId requestId) {
	senderRequestDetach(requestId);
	with_instance([=](not_null<Instance*> instance) {
		instance->cancel(requestId);
	});
}

void ConcurrentSender::senderRequestCancelAll() {
	auto list = std::vector<mtpRequestId>(_requests.size());
	for (const auto &pair : base::take(_requests)) {
		list.push_back(pair.first);
	}
	with_instance([list = std::move(list)](not_null<Instance*> instance) {
		for (const auto requestId : list) {
			instance->cancel(requestId);
		}
	});
}

void ConcurrentSender::senderRequestDetach(mtpRequestId requestId) {
	_requests.erase(requestId);
}

} // namespace MTP
