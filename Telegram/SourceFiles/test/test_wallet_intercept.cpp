/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "test/test_wallet_intercept.h"

#ifdef _DEBUG

#include "gram/api/gram_api_request.h"
#include "mtproto/mtp_instance.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "wallet/wallet_api.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QThread>

namespace Test {
namespace {

using MTP::details::SerializedRequest;

constexpr auto kReleaseCode = 400;

// The self-test's synthetic request ids. Nothing is ever handed to an
// MTP::Instance, so they only have to be distinct inside the self-test.
constexpr auto kFirstSelfTestId = mtpRequestId(0x7E570001);
constexpr auto kHeldWindow = crl::time(150);
constexpr auto kReleaseBound = crl::time(2000);

const auto kReleaseType = u"TIMEOUT"_q;
const auto kJsonRpcEndpoint = u"/api/v2/jsonRPC"_q;
const auto kSendBocMethod = u"sendBoc"_q;
const auto kItemsEndpoint = u"/api/v3/nft/items"_q;
const auto kAddressBookEndpoint = u"/api/v3/addressBook"_q;

struct Decoded {
	mtpTypeId type = 0;
	QString endpoint;
	bool provider = false;
	bool post = false;
	bool sendBoc = false;
	bool transfer = false;
};

class MtpRequester final : public InterceptRequester {
public:
	explicit MtpRequester(not_null<MTP::Instance*> instance);

	[[nodiscard]] const void *identity() const override;
	[[nodiscard]] bool waiting(mtpRequestId id) const override;
	[[nodiscard]] ControlledRpcDelivery answerError(
		mtpRequestId id,
		int code,
		const QString &type) override;
	void cancel(mtpRequestId id) override;

private:
	const QPointer<MTP::Instance> _instance;
	const void *_identity = nullptr;

};

// The self-test's stand-in for an MTP::Instance's callback registry.
class SelfTestRequester final : public InterceptRequester {
public:
	[[nodiscard]] const void *identity() const override;
	[[nodiscard]] bool waiting(mtpRequestId id) const override;
	[[nodiscard]] ControlledRpcDelivery answerError(
		mtpRequestId id,
		int code,
		const QString &type) override;
	void cancel(mtpRequestId id) override;

	// What storeRequest does before sendPrepared: |id| gets a callback
	// that converts the error the way Wallet::Api::request does.
	void registerCall(mtpRequestId id);

	[[nodiscard]] int invocations(mtpRequestId id) const;
	[[nodiscard]] Gram::ApiError lastError(mtpRequestId id) const;
	[[nodiscard]] ControlledRpcDelivery lastDelivery(mtpRequestId id) const;

private:
	struct Answers {
		ControlledRpcDelivery delivery;
		Gram::ApiError error;
		int invocations = 0;
	};

	std::map<mtpRequestId, Fn<void(const MTP::Error&)>> _callbacks;
	std::map<mtpRequestId, Answers> _answers;

};

struct SelfTestState {
	std::shared_ptr<SelfTestRequester> requester
		= std::make_shared<SelfTestRequester>();
	std::shared_ptr<SelfTestRequester> other
		= std::make_shared<SelfTestRequester>();
	WalletIntercept release{ u"wallet-intercept-selftest-release"_q };
	WalletIntercept readiness{ u"wallet-intercept-selftest-readiness"_q };
	WalletIntercept drain{ u"wallet-intercept-selftest-drain"_q };
	WalletIntercept sealed{ u"wallet-intercept-selftest-sealed"_q };
	WalletIntercept unsealed{ u"wallet-intercept-selftest-unsealed"_q };
	std::shared_ptr<WalletDrainGate> gate
		= std::make_shared<WalletDrainGate>();
	mtpRequestId nextId = kFirstSelfTestId;
	mtpRequestId heldId = 0;
	mtpRequestId releasingId = 0;
	mtpRequestId stateId = 0;
	mtpRequestId drainId = 0;
	crl::time heldAt = 0;
	int rowsMark = 0;
	int releasedByCall = 0;
	int registeredCalls = 0;
	int registeredIndex = -1;
	bool heldIntercepted = false;
	bool releasingIntercepted = false;
	bool releasingWaitingAfter = false;
	bool stateIntercepted = false;
	bool drainIntercepted = false;
};

// What S3's quiesce saw on its first call.
struct QuiesceView {
	QString providerType;
	int calls = 0;
	int providerInvocations = 0;
	int otherInvocations = 0;
	int transferInvocations = 0;
	bool sealed = false;
	bool armed = false;
	bool providerWaiting = true;
	bool otherWaiting = true;
	bool transferWaiting = true;
	bool queuedSuppressed = false;
};

[[nodiscard]] QString Bit(bool value) {
	return value ? u"1"_q : u"0"_q;
}

[[nodiscard]] QString TypeText(mtpTypeId type) {
	return QString::number(type, 16).rightJustified(8, QChar('0'));
}

// The constructor word, read in place: no copy for the constructors this
// module does not decode, since a sealed launch sees every request.
[[nodiscard]] mtpTypeId PeekConstructor(const SerializedRequest &request) {
	constexpr auto kBody = SerializedRequest::kMessageBodyPosition;
	return (request && (request->size() > kBody))
		? mtpTypeId(request->constData()[kBody])
		: mtpTypeId(0);
}

// The generated request class has no field accessors, so the provider
// call's fields are read by hand in TL order.
[[nodiscard]] Decoded Decode(const SerializedRequest &request) {
	auto result = Decoded{ .type = PeekConstructor(request) };
	if (result.type == mtpc_wallet_sendTransfer) {
		result.transfer = true;
		return result;
	} else if (result.type != mtpc_toncenter_performApiRequest) {
		return result;
	}
	result.provider = true;
	auto body = mtpBuffer();
	request.write(body);
	if (body.isEmpty()) {
		return result;
	}
	using Flag = MTPtoncenter_performApiRequest::Flag;
	auto from = body.constData() + 1;
	const auto end = body.constData() + body.size();
	auto flags = MTPint();
	auto endpoint = MTPstring();
	auto query = MTPstring();
	auto payload = MTPstring();
	const auto has = [&](Flag flag) {
		return (uint32(flags.v) & uint32(flag)) != 0;
	};
	if (!flags.read(from, end)
		|| !endpoint.read(from, end)
		|| (has(Flag::f_query) && !query.read(from, end))
		|| (has(Flag::f_payload) && !payload.read(from, end))) {
		return result;
	}
	result.endpoint = qs(endpoint);
	result.post = has(Flag::f_post);
	const auto method = QJsonDocument::fromJson(
		payload.v
	).object().value(u"method"_q);
	result.sendBoc = result.post
		&& (result.endpoint == kJsonRpcEndpoint)
		&& method.isString()
		&& (method.toString() == kSendBocMethod);
	result.transfer = result.sendBoc;
	return result;
}

[[nodiscard]] QString DeliveryText(const ControlledRpcDelivery &delivery) {
	return u"registeredBefore=%1 registeredAfter=%2 invoked=%3 "
		"diagnosis=[%4]"_q.arg(
			Bit(delivery.registeredBefore),
			Bit(delivery.registeredAfter),
			Bit(delivery.invokedProcessCallback),
			delivery.diagnosis);
}

[[nodiscard]] SerializedRequest Identified(
		SerializedRequest request,
		mtpRequestId id) {
	request->requestId = id;
	return request;
}

[[nodiscard]] SerializedRequest ProviderGet(
		mtpRequestId id,
		const QString &endpoint) {
	using Flag = MTPtoncenter_performApiRequest::Flag;
	return Identified(
		SerializedRequest::Serialize(MTPtoncenter_PerformApiRequest(
			MTP_flags(Flag::f_query),
			MTP_string(endpoint),
			MTP_string(u"limit=1"_q),
			MTP_string())),
		id);
}

[[nodiscard]] SerializedRequest ProviderJsonRpc(
		mtpRequestId id,
		const QString &method) {
	using Flag = MTPtoncenter_performApiRequest::Flag;
	const auto payload = QJsonDocument(QJsonObject{
		{ u"jsonrpc"_q, u"2.0"_q },
		{ u"id"_q, 1 },
		{ u"method"_q, method },
		{ u"params"_q, QJsonObject{ { u"boc"_q, u"AQIDBA=="_q } } },
	}).toJson(QJsonDocument::Compact);
	return Identified(
		SerializedRequest::Serialize(MTPtoncenter_PerformApiRequest(
			MTP_flags(Flag::f_post | Flag::f_payload),
			MTP_string(kJsonRpcEndpoint),
			MTP_string(),
			MTP_string(QString::fromUtf8(payload)))),
		id);
}

[[nodiscard]] SerializedRequest Transfer(mtpRequestId id) {
	return Identified(
		SerializedRequest::Serialize(MTPwallet_SendTransfer(
			MTP_flags(0),
			MTP_bytes(QByteArray::fromBase64(QByteArray("AQIDBA=="))),
			MTP_bytes(),
			MTP_inputUserEmpty(),
			MTP_long(0x5E1F7E57ULL))),
		id);
}

[[nodiscard]] SerializedRequest GetState(mtpRequestId id) {
	return Identified(
		SerializedRequest::Serialize(MTPwallet_GetState()),
		id);
}

// Registers |request| with |requester| the way storeRequest does, then
// hands it to |intercept| the way the overlay hook in sendPrepared does.
[[nodiscard]] bool Offer(
		WalletIntercept &intercept,
		const std::shared_ptr<SelfTestRequester> &requester,
		const SerializedRequest &request) {
	requester->registerCall(request->requestId);
	return intercept.intercept(requester, request);
}

[[nodiscard]] int FindRecord(
		const WalletIntercept &intercept,
		const std::shared_ptr<SelfTestRequester> &requester,
		mtpRequestId id) {
	for (auto i = intercept.mark(); i != 0; --i) {
		const auto &record = intercept.record(i - 1);
		if (record.id == id
			&& record.requester
			&& (record.requester->identity() == requester->identity())) {
			return i - 1;
		}
	}
	return -1;
}

[[nodiscard]] QString CallText(
		const WalletIntercept &intercept,
		const std::shared_ptr<SelfTestRequester> &requester,
		mtpRequestId id) {
	const auto index = FindRecord(intercept, requester, id);
	const auto error = requester->lastError(id);
	return u"id=%1 record=%2 suppressed=%3 held=%4 waiting=%5 "
		"invocations=%6 code=%7 type=%8 delivery=[%9]"_q.arg(
			QString::number(id),
			QString::number(index),
			Bit((index >= 0) && intercept.record(index).suppressed),
			Bit(intercept.held(index)),
			Bit(requester->waiting(id)),
			QString::number(requester->invocations(id)),
			QString::number(error.code),
			error.message,
			DeliveryText(requester->lastDelivery(id)));
}

[[nodiscard]] QString PairText(
		const WalletReadiness &refusal,
		const WalletReadiness &control) {
	return u"refusal=[%1] control=[%2]"_q.arg(
		refusal.observation,
		control.observation);
}

[[nodiscard]] QString RowsText(const std::vector<QString> &rows) {
	auto list = QStringList();
	for (const auto &row : rows) {
		list.push_back(row);
	}
	return list.isEmpty() ? u"none"_q : list.join(u"; "_q);
}

[[nodiscard]] QString RowWith(
		const std::vector<QString> &rows,
		const QString &prefix) {
	for (const auto &row : rows) {
		if (row.startsWith(prefix)) {
			return row;
		}
	}
	return QString();
}

[[nodiscard]] crl::time AfterDisarmMs(const QString &row) {
	const auto key = u"afterDisarmMs="_q;
	const auto from = row.indexOf(key);
	if (from < 0) {
		return -1;
	}
	auto ok = false;
	const auto value = row.mid(from + key.size()).section(
		QChar(' '),
		0,
		0).toLongLong(&ok);
	return ok ? crl::time(value) : crl::time(-1);
}

[[nodiscard]] QString QuiesceText(const QuiesceView &view) {
	return u"quiesce=[calls=%1 sealed=%2 armed=%3 providerWaiting=%4 "
		"providerInvocations=%5 providerType=%6 otherWaiting=%7 "
		"otherInvocations=%8 transferWaiting=%9"_q.arg(
			QString::number(view.calls),
			Bit(view.sealed),
			Bit(view.armed),
			Bit(view.providerWaiting),
			QString::number(view.providerInvocations),
			view.providerType,
			Bit(view.otherWaiting),
			QString::number(view.otherInvocations),
			Bit(view.transferWaiting))
		+ u" transferInvocations=%1 queuedSuppressed=%2]"_q.arg(
			QString::number(view.transferInvocations),
			Bit(view.queuedSuppressed));
}

MtpRequester::MtpRequester(not_null<MTP::Instance*> instance)
: _instance(instance.get())
, _identity(instance.get()) {
}

const void *MtpRequester::identity() const {
	return _identity;
}

bool MtpRequester::waiting(mtpRequestId id) const {
	return _instance && _instance->hasCallback(id);
}

ControlledRpcDelivery MtpRequester::answerError(
		mtpRequestId id,
		int code,
		const QString &type) {
	if (!_instance) {
		auto result = ControlledRpcDelivery();
		result.diagnosis = u"instance gone"_q;
		return result;
	}
	return DeliverControlledRpcError(_instance.data(), id, code, type);
}

void MtpRequester::cancel(mtpRequestId id) {
	if (_instance) {
		_instance->cancel(id);
	}
}

const void *SelfTestRequester::identity() const {
	return this;
}

bool SelfTestRequester::waiting(mtpRequestId id) const {
	return _callbacks.contains(id);
}

ControlledRpcDelivery SelfTestRequester::answerError(
		mtpRequestId id,
		int code,
		const QString &type) {
	auto delivery = ControlledRpcDelivery();
	delivery.registeredBefore = waiting(id);
	auto callback = Fn<void(const MTP::Error&)>();
	const auto i = _callbacks.find(id);
	if (i != _callbacks.end()) {
		callback = std::move(i->second);
		_callbacks.erase(i);
	}
	if (callback) {
		callback(MTP::Error(MTP_rpc_error(MTP_int(code), MTP_string(type))));
	}
	delivery.route = ControlledRpcDelivery::Route::Error;
	delivery.invokedProcessCallback = true;
	delivery.registeredAfter = waiting(id);
	_answers[id].delivery = delivery;
	return delivery;
}

void SelfTestRequester::cancel(mtpRequestId id) {
	_callbacks.erase(id);
}

void SelfTestRequester::registerCall(mtpRequestId id) {
	_callbacks[id] = [this, id](const MTP::Error &error) {
		auto &answers = _answers[id];
		++answers.invocations;
		answers.error = Gram::ApiError{
			.code = error.code(),
			.message = error.type(),
		};
	};
}

int SelfTestRequester::invocations(mtpRequestId id) const {
	const auto i = _answers.find(id);
	return (i != _answers.end()) ? i->second.invocations : 0;
}

Gram::ApiError SelfTestRequester::lastError(mtpRequestId id) const {
	const auto i = _answers.find(id);
	return (i != _answers.end()) ? i->second.error : Gram::ApiError();
}

ControlledRpcDelivery SelfTestRequester::lastDelivery(
		mtpRequestId id) const {
	const auto i = _answers.find(id);
	return (i != _answers.end())
		? i->second.delivery
		: ControlledRpcDelivery();
}

} // namespace

std::shared_ptr<InterceptRequester> MtpInterceptRequester(
		not_null<MTP::Instance*> instance) {
	return std::make_shared<MtpRequester>(instance);
}

bool WalletEngineWorkerIdle(const WalletEngineWorker &worker) {
	return !worker.queued && (worker.started == worker.finished);
}

QString WalletEngineWorkerText(const WalletEngineWorker &worker) {
	return u"queued=%1 started=%2 finished=%3 absent=%4"_q
		.arg(worker.queued)
		.arg(worker.started)
		.arg(worker.finished)
		.arg(worker.absent ? 1 : 0);
}

QString WalletReadinessStateName(WalletReadinessState state) {
	switch (state) {
	case WalletReadinessState::Ready:
		return u"ready"_q;
	case WalletReadinessState::WorkerUnread:
		return u"worker-unread"_q;
	case WalletReadinessState::WorkerBusy:
		return u"worker-busy"_q;
	case WalletReadinessState::ProviderHeld:
		return u"provider-held"_q;
	case WalletReadinessState::Quiet:
		return u"quiet"_q;
	}
	return u"missing"_q;
}

bool IsWalletTransferRequest(const MTP::details::SerializedRequest &request) {
	return Decode(request).transfer;
}

WalletIntercept::WalletIntercept(QString probeName)
: _probe(std::move(probeName)) {
}

void WalletIntercept::arm(const void *identity) {
	_identity = identity;
	_armed = true;
	_everArmed = true;
	_probe.record(u"armed identity=0x%1 sealed=%2"_q.arg(
		QString::number(quintptr(identity), 16),
		Bit(_sealed)));
}

void WalletIntercept::seal() {
	if (_sealed) {
		return;
	}
	_sealed = true;
	_probe.record(u"sealed armed=%1"_q.arg(Bit(_armed)));
}

void WalletIntercept::disarm() {
	if (!base::take(_armed)) {
		return;
	}
	_disarmedAt = crl::now();
	_probe.record(u"disarmed sealed=%1"_q.arg(Bit(_sealed)));
}

bool WalletIntercept::armed() const {
	return _armed;
}

bool WalletIntercept::everArmed() const {
	return _everArmed;
}

bool WalletIntercept::sealed() const {
	return _sealed;
}

crl::time WalletIntercept::disarmedAt() const {
	return _disarmedAt;
}

bool WalletIntercept::intercept(
		std::shared_ptr<InterceptRequester> requester,
		const MTP::details::SerializedRequest &request) {
	if ((!_everArmed && !_sealed) || !requester || !request) {
		return false;
	}
	const auto identity = requester->identity();
	const auto mine = _armed && (identity == _identity);
	const auto decoded = Decode(request);
	if (!mine && !decoded.transfer) {
		return false;
	}
	const auto now = crl::now();
	const auto id = request->requestId;
	const auto existing = find(identity, id);
	const auto index = (existing >= 0) ? existing : int(_records.size());
	if (existing < 0) {
		_records.push_back({
			.requester = std::move(requester),
			.id = id,
			.type = decoded.type,
			.endpoint = decoded.endpoint,
			.registeredAt = now,
			.provider = decoded.provider,
			.post = decoded.post,
			.sendBoc = decoded.sendBoc,
		});
	}
	auto &record = _records[index];
	const auto resend = (existing >= 0) ? u" resend=1"_q : QString();
	if (mine) {
		record.suppressed = true;
		_probe.record(u"registered id=%1 type=0x%2 endpoint=%3 post=%4 "
			"sendBoc=%5%6"_q.arg(
				QString::number(id),
				TypeText(decoded.type),
				decoded.endpoint,
				Bit(decoded.post),
				Bit(decoded.sendBoc),
				resend));
		if (decoded.provider) {
			_providerActivityAt = now;
		}
		if ((existing < 0) && _registered) {
			_registered(index);
		}
		if (decoded.provider && _releasing) {
			crl::on_main(this, [this, index] {
				if (_releasing) {
					release(index);
				}
			});
		}
		return true;
	} else if (_sealed) {
		record.suppressed = true;
		record.sealedOnly = true;
		const auto afterDisarm = (_armed || !_disarmedAt)
			? crl::time(-1)
			: (now - _disarmedAt);
		_probe.record(u"sealed id=%1 type=0x%2 afterDisarmMs=%3%4"_q.arg(
			QString::number(id),
			TypeText(decoded.type),
			QString::number(afterDisarm),
			resend));
		return true;
	}
	const auto counted = (existing >= 0) && !record.suppressed;
	record.suppressed = false;
	if (!counted) {
		++_passedTransfers;
	}
	_probe.record(u"passed wallet transfer id=%1 type=0x%2%3"_q.arg(
		QString::number(id),
		TypeText(decoded.type),
		resend));
	return false;
}

void WalletIntercept::onRegistered(Fn<void(int index)> callback) {
	_registered = std::move(callback);
}

void WalletIntercept::markAnswered(int index) {
	if (index < 0 || index >= int(_records.size())) {
		return;
	}
	auto &record = _records[index];
	if (record.answered) {
		return;
	}
	record.answered = true;
	if (record.provider) {
		_providerActivityAt = crl::now();
	}
}

void WalletIntercept::setReleasing(bool releasing) {
	_releasing = releasing;
}

bool WalletIntercept::releasing() const {
	return _releasing;
}

int WalletIntercept::releaseHeld() {
	const auto before = _released;
	for (auto i = 0; i != int(_records.size()); ++i) {
		release(i);
	}
	return _released - before;
}

void WalletIntercept::release(int index) {
	if (!held(index)) {
		return;
	}
	const auto requester = _records[index].requester;
	const auto id = _records[index].id;
	_records[index].answered = true;
	const auto delivery = requester->answerError(
		id,
		kReleaseCode,
		kReleaseType);
	const auto now = crl::now();
	auto &record = _records[index];
	record.releasedAt = now;
	_providerActivityAt = now;
	++_released;
	_probe.record(u"released id=%1 type=0x%2 endpoint=%3 ageMs=%4 "
		"registeredBefore=%5 registeredAfter=%6 invoked=%7"_q.arg(
			QString::number(id),
			TypeText(record.type),
			record.endpoint,
			QString::number(now - record.registeredAt),
			Bit(delivery.registeredBefore),
			Bit(delivery.registeredAfter),
			Bit(delivery.invokedProcessCallback)));
}

int WalletIntercept::mark() const {
	return int(_records.size());
}

const WalletInterceptRecord &WalletIntercept::record(int index) const {
	Expects(index >= 0 && index < int(_records.size()));

	return _records[index];
}

int WalletIntercept::latest(mtpTypeId type, int from) const {
	for (auto i = int(_records.size()); i > std::max(from, 0); --i) {
		const auto &record = _records[i - 1];
		if (record.type == type
			&& record.suppressed
			&& !record.answered
			&& record.requester
			&& record.requester->waiting(record.id)) {
			return i - 1;
		}
	}
	return -1;
}

bool WalletIntercept::held(int index) const {
	if (index < 0 || index >= int(_records.size())) {
		return false;
	}
	const auto &record = _records[index];
	return record.provider
		&& record.suppressed
		&& !record.answered
		&& record.requester
		&& record.requester->waiting(record.id);
}

int WalletIntercept::heldCount(int from, int till) const {
	const auto size = int(_records.size());
	const auto end = (till < 0) ? size : std::min(till, size);
	auto result = 0;
	for (auto i = std::max(from, 0); i < end; ++i) {
		result += held(i) ? 1 : 0;
	}
	return result;
}

QString WalletIntercept::heldText(int from, int till) const {
	return heldList(from, till, crl::now());
}

QString WalletIntercept::heldList(int from, int till, crl::time now) const {
	const auto size = int(_records.size());
	const auto end = (till < 0) ? size : std::min(till, size);
	auto list = QStringList();
	for (auto i = std::max(from, 0); i < end; ++i) {
		if (!held(i)) {
			continue;
		}
		const auto &record = _records[i];
		list.push_back(u"id=%1 endpoint=%2 ageMs=%3"_q.arg(
			QString::number(record.id),
			record.endpoint,
			QString::number(now - record.registeredAt)));
	}
	return list.isEmpty() ? u"none"_q : list.join(u", "_q);
}

int WalletIntercept::releasedCount() const {
	return _released;
}

int WalletIntercept::sealedSuppressionsSince(int mark) const {
	auto result = 0;
	for (auto i = std::max(mark, 0); i < int(_records.size()); ++i) {
		result += _records[i].sealedOnly ? 1 : 0;
	}
	return result;
}

int WalletIntercept::passedTransfers() const {
	return _passedTransfers;
}

WalletReadiness WalletIntercept::readiness(
		crl::time now,
		std::optional<WalletEngineWorker> worker,
		crl::time quiet) const {
	auto result = WalletReadiness{
		.held = heldCount(),
		.quietFor = _providerActivityAt
			? (now - _providerActivityAt)
			: crl::time(-1),
	};
	if (!worker) {
		result.state = WalletReadinessState::WorkerUnread;
	} else if (!WalletEngineWorkerIdle(*worker)) {
		result.state = WalletReadinessState::WorkerBusy;
	} else if (result.held) {
		result.state = WalletReadinessState::ProviderHeld;
	} else if (_providerActivityAt && (now - _providerActivityAt < quiet)) {
		result.state = WalletReadinessState::Quiet;
	} else {
		result.state = WalletReadinessState::Ready;
	}
	result.observation = u"state=%1 worker=[%2] held=[%3] quietFor=%4 "
		"quiet=%5 released=%6"_q.arg(
			WalletReadinessStateName(result.state),
			worker ? WalletEngineWorkerText(*worker) : u"unread"_q,
			heldList(0, -1, now),
			(_providerActivityAt
				? QString::number(result.quietFor)
				: u"never"_q),
			QString::number(quiet),
			QString::number(_released));
	return result;
}

void WalletIntercept::teardown(Fn<void()> quiesce) {
	seal();
	_releasing = false;
	const auto released = releaseHeld();
	auto cancelled = 0;
	for (auto i = 0; i != int(_records.size()); ++i) {
		const auto &record = _records[i];
		if (!record.suppressed
			|| record.answered
			|| !record.requester
			|| !record.requester->waiting(record.id)) {
			continue;
		}
		const auto requester = record.requester;
		const auto id = record.id;
		_records[i].answered = true;
		requester->cancel(id);
		++cancelled;
	}
	_probe.record(u"teardown released=%1 cancelled=%2"_q
		.arg(released)
		.arg(cancelled));
	if (quiesce) {
		quiesce();
	}
	disarm();
}

Probe &WalletIntercept::probe() {
	return _probe;
}

int WalletIntercept::find(const void *identity, mtpRequestId id) const {
	for (auto i = int(_records.size()); i != 0; --i) {
		const auto &record = _records[i - 1];
		if (record.id == id
			&& !record.answered
			&& record.requester
			&& (record.requester->identity() == identity)) {
			return i - 1;
		}
	}
	return -1;
}

WalletIntercept &ControlledWalletIntercept() {
	static auto &result = *new WalletIntercept();
	return result;
}

bool InterceptWalletRequest(
		not_null<MTP::Instance*> instance,
		const MTP::details::SerializedRequest &request) {
	Expects(QThread::currentThread() == QCoreApplication::instance()->thread());

	auto &intercept = ControlledWalletIntercept();
	if (!intercept.everArmed() && !intercept.sealed()) {
		return false;
	}
	return intercept.intercept(MtpInterceptRequester(instance), request);
}

void AppendWalletEngineDrain(
		not_null<Runner*> runner,
		const QString &name,
		not_null<WalletIntercept*> intercept,
		Fn<std::optional<WalletEngineWorker>()> worker,
		std::shared_ptr<WalletDrainGate> gate,
		Fn<QString()> skip,
		crl::time timeout) {
	struct Drain {
		std::shared_ptr<WalletDrainGate> gate;
		int releasedFrom = 0;
	};
	const auto drain = std::make_shared<Drain>(Drain{
		.gate = gate ? std::move(gate) : std::make_shared<WalletDrainGate>(),
	});
	const auto read = [=](crl::time now) {
		return intercept->readiness(
			now,
			worker ? worker() : std::optional<WalletEngineWorker>());
	};
	runner->add({
		.name = name + u" drain before the box"_q,
		.skipReason = std::move(skip),
		.run = [=] {
			// Stamped before the release, so readyAt - startedAt can never
			// read shorter than the quiet window the release opened.
			drain->gate->startedAt = crl::now();
			intercept->setReleasing(true);
			drain->releasedFrom = intercept->releasedCount();
			intercept->releaseHeld();
		},
		.until = [=] {
			return read(crl::now()).ready();
		},
		.then = [=] {
			const auto now = crl::now();
			const auto readiness = read(now);
			auto &filled = *drain->gate;
			filled.requestMark = intercept->mark();
			filled.releasedInDrain = intercept->releasedCount()
				- drain->releasedFrom;
			filled.readyAt = now;
			Check(
				readiness.ready(),
				name + u" fixture gate: the engine worker is idle, no "
					"provider call is held and none was registered or "
					"released for %1 ms before the box"_q.arg(
						kWalletEngineQuiet),
				readiness.observation
					+ u" releasedInDrain=%1 requestMark=%2"_q
						.arg(filled.releasedInDrain)
						.arg(filled.requestMark));
		},
		.timeout = timeout,
		.timeoutDetails = [=] {
			return u"%1 heldNow=[%2] releasedInDrain=%3"_q.arg(
				read(crl::now()).observation,
				intercept->heldText(),
				QString::number(
					intercept->releasedCount() - drain->releasedFrom));
		},
	});
}

void CheckNoEarlierProviderCallHeld(
		const QString &name,
		const WalletIntercept &intercept,
		int boxMark,
		std::optional<WalletEngineWorker> worker) {
	Check(
		!intercept.heldCount(0, boxMark),
		name + u" fixture gate: no earlier leg's provider call is held at "
			"the press"_q,
		u"earlier=[%1] since-box=[%2] boxMark=%3 worker=[%4]"_q.arg(
			intercept.heldText(0, boxMark),
			intercept.heldText(boxMark),
			QString::number(boxMark),
			worker ? WalletEngineWorkerText(*worker) : u"unread"_q));
}

void AppendWalletInterceptSelfTest(not_null<Runner*> runner) {
	const auto state = std::make_shared<SelfTestState>();

	runner->add({
		.name = u"wallet intercept self-test: S1a a provider call with the "
			"release off"_q,
		.run = [=] {
			auto &intercept = state->release;
			intercept.onRegistered([=](int index) {
				++state->registeredCalls;
				state->registeredIndex = index;
			});
			intercept.arm(state->requester->identity());
			state->rowsMark = intercept.probe().mark();
			state->heldId = state->nextId++;
			state->heldIntercepted = Offer(
				intercept,
				state->requester,
				ProviderGet(state->heldId, kItemsEndpoint));
			state->heldAt = crl::now();
		},
		.until = [=] {
			return (crl::now() - state->heldAt) >= kHeldWindow;
		},
		.then = [=] {
			auto &intercept = state->release;
			const auto &requester = state->requester;
			const auto id = state->heldId;
			const auto index = FindRecord(intercept, requester, id);
			Check(
				state->heldIntercepted
					&& (index >= 0)
					&& intercept.held(index)
					&& requester->waiting(id)
					&& !requester->invocations(id)
					&& (state->registeredCalls == 1)
					&& (state->registeredIndex == index)
					&& !intercept.releasing(),
				u"wallet intercept self-test: S1a a held provider call "
				"stays held and waiting, never invoked, with the release "
				"off"_q,
				u"intercepted=%1 registeredCalls=%2 registeredIndex=%3 "
				"releasing=%4 waitedMs=%5 call=[%6] rows=[%7]"_q.arg(
					Bit(state->heldIntercepted),
					QString::number(state->registeredCalls),
					QString::number(state->registeredIndex),
					Bit(intercept.releasing()),
					QString::number(crl::now() - state->heldAt),
					CallText(intercept, requester, id),
					RowsText(intercept.probe().rowsSince(state->rowsMark))));
		},
	});

	runner->add({
		.name = u"wallet intercept self-test: S1b the release on the same "
			"call"_q,
		.run = [=] {
			state->rowsMark = state->release.probe().mark();
			state->releasedByCall = state->release.releaseHeld();
		},
		.then = [=] {
			auto &intercept = state->release;
			const auto &requester = state->requester;
			const auto id = state->heldId;
			const auto index = FindRecord(intercept, requester, id);
			const auto delivery = requester->lastDelivery(id);
			const auto error = requester->lastError(id);
			const auto rows = intercept.probe().rowsSince(state->rowsMark);
			const auto row = RowWith(
				rows,
				u"released id=%1 "_q.arg(QString::number(id)));
			Check(
				delivery.registeredBefore
					&& !delivery.registeredAfter
					&& delivery.invokedProcessCallback
					&& (requester->invocations(id) == 1)
					&& (error.code == kReleaseCode)
					&& (error.message == kReleaseType)
					&& Wallet::Api::IsTimeoutError(error)
					&& (index >= 0)
					&& !intercept.held(index)
					&& (state->releasedByCall == 1)
					&& !row.isEmpty(),
				u"wallet intercept self-test: S1b the release answers the "
				"same call through its registered callback with TIMEOUT, "
				"which Wallet::Api reads as its own timeout"_q,
				u"releasedByCall=%1 isTimeoutError=%2 call=[%3] "
				"rows=[%4]"_q.arg(
					QString::number(state->releasedByCall),
					Bit(Wallet::Api::IsTimeoutError(error)),
					CallText(intercept, requester, id),
					RowsText(rows)));
		},
	});

	runner->add({
		.name = u"wallet intercept self-test: S1c new calls while "
			"releasing"_q,
		.run = [=] {
			auto &intercept = state->release;
			const auto &requester = state->requester;
			intercept.setReleasing(true);
			state->releasingId = state->nextId++;
			state->releasingIntercepted = Offer(
				intercept,
				requester,
				ProviderGet(state->releasingId, kAddressBookEndpoint));
			state->releasingWaitingAfter
				= requester->waiting(state->releasingId)
				&& !requester->invocations(state->releasingId);
			state->stateId = state->nextId++;
			state->stateIntercepted = Offer(
				intercept,
				requester,
				GetState(state->stateId));
		},
		.until = [=] {
			return !state->requester->waiting(state->releasingId);
		},
		.then = [=] {
			auto &intercept = state->release;
			const auto &requester = state->requester;
			const auto provider = state->releasingId;
			const auto other = state->stateId;
			const auto otherIndex = FindRecord(intercept, requester, other);
			Check(
				state->releasingIntercepted
					&& state->releasingWaitingAfter
					&& (requester->invocations(provider) == 1)
					&& (requester->lastError(provider).message
						== kReleaseType)
					&& !requester->waiting(provider)
					&& state->stateIntercepted
					&& (otherIndex >= 0)
					&& intercept.record(otherIndex).suppressed
					&& !intercept.held(otherIndex)
					&& requester->waiting(other)
					&& !requester->invocations(other),
				u"wallet intercept self-test: S1c while releasing, a new "
				"provider call is answered once on a later turn and a "
				"non-provider call is never released"_q,
				u"waitingWhenInterceptReturned=%1 provider=[%2] "
				"getState=[%3]"_q.arg(
					Bit(state->releasingWaitingAfter),
					CallText(intercept, requester, provider),
					CallText(intercept, requester, other)));
			intercept.setReleasing(false);
		},
		.timeout = kReleaseBound,
		.timeoutDetails = [=] {
			return u"provider=[%1] getState=[%2]"_q.arg(
				CallText(
					state->release,
					state->requester,
					state->releasingId),
				CallText(state->release, state->requester, state->stateId));
		},
	});

	runner->add({
		.name = u"wallet intercept self-test: S2 readiness refusals beside "
			"their ready controls"_q,
		.then = [=] {
			auto &intercept = state->readiness;
			const auto &requester = state->requester;
			const auto quiet = kWalletEngineQuiet;
			const auto idle = WalletEngineWorker();
			intercept.arm(requester->identity());

			const auto now = crl::now();
			const auto unread = intercept.readiness(now, std::nullopt);
			const auto ready = intercept.readiness(now, idle);
			const auto absent = intercept.readiness(
				now,
				WalletEngineWorker{ .absent = true });
			Check(
				(unread.state == WalletReadinessState::WorkerUnread)
					&& ready.ready()
					&& absent.ready(),
				u"wallet intercept self-test: S2 a missing worker reading "
				"refuses, beside an idle and an absent worker that read "
				"ready"_q,
				u"%1 absent=[%2]"_q.arg(
					PairText(unread, ready),
					absent.observation));

			const auto queued = intercept.readiness(
				now,
				WalletEngineWorker{ .queued = 1 });
			const auto started = intercept.readiness(
				now,
				WalletEngineWorker{ .started = 11, .finished = 10 });
			Check(
				(queued.state == WalletReadinessState::WorkerBusy)
					&& (started.state == WalletReadinessState::WorkerBusy)
					&& ready.ready(),
				u"wallet intercept self-test: S2 a busy worker refuses, "
				"queued or started ahead of finished, beside the idle "
				"reading that is ready"_q,
				u"%1 startedAhead=[%2]"_q.arg(
					PairText(queued, ready),
					started.observation));

			const auto heldId = state->nextId++;
			const auto heldTaken = Offer(
				intercept,
				requester,
				ProviderGet(heldId, kItemsEndpoint));
			const auto heldIndex = FindRecord(intercept, requester, heldId);
			const auto registeredAt = (heldIndex >= 0)
				? intercept.record(heldIndex).registeredAt
				: crl::time(0);
			const auto held = intercept.readiness(
				registeredAt + 10 * quiet,
				idle);
			const auto released = intercept.releaseHeld();
			const auto releasedAt = (heldIndex >= 0)
				? intercept.record(heldIndex).releasedAt
				: crl::time(0);
			const auto releaseQuiet = intercept.readiness(
				releasedAt + quiet - 1,
				idle);
			const auto releaseReady = intercept.readiness(
				releasedAt + quiet,
				idle);
			Check(
				heldTaken
					&& (held.state == WalletReadinessState::ProviderHeld)
					&& (released == 1)
					&& releaseReady.ready(),
				u"wallet intercept self-test: S2 a held provider call "
				"refuses even ten quiet windows after its registration, "
				"beside the ready reading a quiet window after its "
				"release"_q,
				u"%1 registeredAt=%2 releasedAt=%3 released=%4"_q.arg(
					PairText(held, releaseReady),
					QString::number(registeredAt),
					QString::number(releasedAt),
					QString::number(released)));
			Check(
				(releasedAt > 0)
					&& (releaseQuiet.state == WalletReadinessState::Quiet)
					&& releaseReady.ready(),
				u"wallet intercept self-test: S2 a release refuses one "
				"millisecond inside the quiet window and is ready at its "
				"end"_q,
				u"%1 releasedAt=%2 quietMs=%3"_q.arg(
					PairText(releaseQuiet, releaseReady),
					QString::number(releasedAt),
					QString::number(quiet)));

			const auto cancelledId = state->nextId++;
			const auto cancelledTaken = Offer(
				intercept,
				requester,
				ProviderGet(cancelledId, kAddressBookEndpoint));
			requester->cancel(cancelledId);
			const auto cancelledIndex = FindRecord(
				intercept,
				requester,
				cancelledId);
			const auto cancelledAt = (cancelledIndex >= 0)
				? intercept.record(cancelledIndex).registeredAt
				: crl::time(0);
			const auto registerQuiet = intercept.readiness(
				cancelledAt + quiet - 1,
				idle);
			const auto registerReady = intercept.readiness(
				cancelledAt + quiet,
				idle);
			Check(
				cancelledTaken
					&& (cancelledAt > 0)
					&& !intercept.held(cancelledIndex)
					&& !requester->invocations(cancelledId)
					&& (intercept.releasedCount() == 1)
					&& (registerQuiet.state == WalletReadinessState::Quiet)
					&& registerReady.ready(),
				u"wallet intercept self-test: S2 a registration refuses one "
				"millisecond inside the quiet window although nothing was "
				"released, and is ready at its end"_q,
				u"%1 registeredAt=%2 call=[%3]"_q.arg(
					PairText(registerQuiet, registerReady),
					QString::number(cancelledAt),
					CallText(intercept, requester, cancelledId)));
		},
	});

	runner->add({
		.name = u"wallet intercept self-test: S2 drain premise"_q,
		.run = [=] {
			auto &intercept = state->drain;
			intercept.arm(state->requester->identity());
			state->drainId = state->nextId++;
			state->drainIntercepted = Offer(
				intercept,
				state->requester,
				ProviderGet(state->drainId, kItemsEndpoint));
		},
		.then = [=] {
			auto &intercept = state->drain;
			const auto index = FindRecord(
				intercept,
				state->requester,
				state->drainId);
			Check(
				state->drainIntercepted
					&& intercept.held(index)
					&& !intercept.releasing(),
				u"wallet intercept self-test: S2 drain premise: one "
				"provider call is held with the release off"_q,
				CallText(intercept, state->requester, state->drainId));
		},
	});

	AppendWalletEngineDrain(
		runner,
		u"wallet intercept self-test: S2"_q,
		&state->drain,
		[] { return std::make_optional(WalletEngineWorker()); },
		state->gate);

	runner->add({
		.name = u"wallet intercept self-test: S2 drain gate"_q,
		.then = [=] {
			const auto &gate = *state->gate;
			const auto waited = gate.readyAt - gate.startedAt;
			const auto &requester = state->requester;
			Check(
				(gate.releasedInDrain == 1)
					&& (waited >= kWalletEngineQuiet)
					&& (requester->invocations(state->drainId) == 1)
					&& !requester->waiting(state->drainId)
					&& state->drain.releasing(),
				u"wallet intercept self-test: S2 the drain released "
				"exactly the held call, passed no sooner than the quiet "
				"window on the live clock and left the release on"_q,
				u"releasedInDrain=%1 startedAt=%2 readyAt=%3 waitedMs=%4 "
				"quietMs=%5 requestMark=%6 releasing=%7 call=[%8]"_q.arg(
					QString::number(gate.releasedInDrain),
					QString::number(gate.startedAt),
					QString::number(gate.readyAt),
					QString::number(waited),
					QString::number(kWalletEngineQuiet),
					QString::number(gate.requestMark),
					Bit(state->drain.releasing()),
					CallText(state->drain, requester, state->drainId)));
		},
	});

	runner->add({
		.name = u"wallet intercept self-test: S3 seal and teardown "
			"order"_q,
		.then = [=] {
			auto &intercept = state->sealed;
			const auto requester = state->requester;
			const auto view = std::make_shared<QuiesceView>();
			const auto providerId = state->nextId++;
			const auto otherId = state->nextId++;
			const auto transferId = state->nextId++;
			const auto queuedId = state->nextId++;

			intercept.arm(requester->identity());
			const auto providerTaken = Offer(
				intercept,
				requester,
				ProviderGet(providerId, kItemsEndpoint));
			const auto otherTaken = Offer(
				intercept,
				requester,
				GetState(otherId));
			const auto transferTaken = Offer(
				intercept,
				requester,
				Transfer(transferId));
			const auto quiesce = [=] {
				if (++view->calls > 1) {
					return;
				}
				auto &subject = state->sealed;
				view->sealed = subject.sealed();
				view->armed = subject.armed();
				view->providerWaiting = requester->waiting(providerId);
				view->providerInvocations = requester->invocations(
					providerId);
				view->providerType = requester->lastError(
					providerId).message;
				view->otherWaiting = requester->waiting(otherId);
				view->otherInvocations = requester->invocations(otherId);
				view->transferWaiting = requester->waiting(transferId);
				view->transferInvocations = requester->invocations(
					transferId);

				// A queued send leaving the engine in the middle of the
				// teardown, the way Run 1's did.
				view->queuedSuppressed = Offer(
					subject,
					requester,
					Transfer(queuedId));
			};
			const auto teardownMark = intercept.probe().mark();
			intercept.teardown(quiesce);
			const auto teardownRows = intercept.probe().rowsSince(
				teardownMark);
			const auto disarmed = !intercept.armed();
			const auto stillSealed = intercept.sealed();
			const auto disarmedAt = intercept.disarmedAt();
			const auto queuedIndex = FindRecord(
				intercept,
				requester,
				queuedId);

			Check(
				providerTaken
					&& otherTaken
					&& transferTaken
					&& (view->calls == 1)
					&& view->sealed
					&& view->armed
					&& !view->providerWaiting
					&& (view->providerInvocations == 1)
					&& (view->providerType == kReleaseType)
					&& !view->otherWaiting
					&& !view->otherInvocations
					&& !view->transferWaiting
					&& !view->transferInvocations,
				u"wallet intercept self-test: S3 teardown seals, releases "
				"the held provider call and cancels the other suppressed "
				"requests before its quiesce, while still armed"_q,
				u"taken=[provider=%1 other=%2 transfer=%3] %4 rows=[%5]"_q
					.arg(
						Bit(providerTaken),
						Bit(otherTaken),
						Bit(transferTaken),
						QuiesceText(*view),
						RowsText(teardownRows)));
			Check(
				view->queuedSuppressed
					&& (queuedIndex >= 0)
					&& intercept.record(queuedIndex).suppressed,
				u"wallet intercept self-test: S3 a transfer that leaves "
				"the engine during the teardown is suppressed"_q,
				CallText(intercept, requester, queuedId));
			Check(
				disarmed && stillSealed && (disarmedAt > 0),
				u"wallet intercept self-test: S3 teardown disarms last and "
				"keeps the seal"_q,
				u"armed=%1 sealed=%2 disarmedAt=%3"_q.arg(
					Bit(!disarmed),
					Bit(stillSealed),
					QString::number(disarmedAt)));

			const auto recordsMark = intercept.mark();
			const auto rowsMark = intercept.probe().mark();
			const auto transfer = Transfer(state->nextId++);
			const auto transferAfter = Offer(intercept, requester, transfer);
			const auto stateAfter = Offer(
				intercept,
				requester,
				GetState(state->nextId++));
			const auto otherRequester = intercept.intercept(
				state->other,
				transfer);
			const auto sendBoc = intercept.intercept(
				requester,
				ProviderJsonRpc(state->nextId++, kSendBocMethod));
			const auto runGetMethod = intercept.intercept(
				requester,
				ProviderJsonRpc(state->nextId++, u"runGetMethod"_q));
			const auto providerGet = intercept.intercept(
				requester,
				ProviderGet(state->nextId++, kItemsEndpoint));
			const auto sealedSince = intercept.sealedSuppressionsSince(
				recordsMark);
			const auto rows = intercept.probe().rowsSince(rowsMark);
			const auto afterDisarmMs = AfterDisarmMs(RowWith(
				rows,
				u"sealed id=%1 "_q.arg(
					QString::number(transfer->requestId))));
			Check(
				transferAfter
					&& (afterDisarmMs >= 0)
					&& !stateAfter
					&& otherRequester
					&& sendBoc
					&& !runGetMethod
					&& !providerGet
					&& (sealedSince == 3)
					&& !intercept.passedTransfers(),
				u"wallet intercept self-test: S3 after seal and disarm a "
				"wallet.sendTransfer is still suppressed, from any "
				"requester, and so is a provider sendBoc, while another "
				"constructor passes"_q,
				u"transfer=%1 afterDisarmMs=%2 getState=%3 "
				"otherRequester=%4 sendBoc=%5 runGetMethod=%6 "
				"providerGet=%7 sealedSince=%8 passed=%9"_q.arg(
					Bit(transferAfter),
					QString::number(afterDisarmMs),
					Bit(stateAfter),
					Bit(otherRequester),
					Bit(sendBoc),
					Bit(runGetMethod),
					Bit(providerGet),
					QString::number(sealedSince),
					QString::number(intercept.passedTransfers()))
				+ u" rows=[%1]"_q.arg(RowsText(rows)));

			const auto predicate = IsWalletTransferRequest(transfer)
				&& IsWalletTransferRequest(
					ProviderJsonRpc(state->nextId++, kSendBocMethod))
				&& !IsWalletTransferRequest(
					ProviderJsonRpc(state->nextId++, u"runGetMethod"_q))
				&& !IsWalletTransferRequest(
					ProviderGet(state->nextId++, kItemsEndpoint))
				&& !IsWalletTransferRequest(GetState(state->nextId++));
			Check(
				predicate,
				u"wallet intercept self-test: S3 IsWalletTransferRequest "
				"names wallet.sendTransfer and a provider jsonRPC sendBoc, "
				"and nothing else tried"_q,
				u"sendTransfer, sendBoc, runGetMethod, GET items, "
				"getState"_q);

			const auto secondMark = intercept.probe().mark();
			intercept.teardown(quiesce);
			Check(
				(view->calls == 2)
					&& !intercept.armed()
					&& intercept.sealed()
					&& (intercept.disarmedAt() == disarmedAt)
					&& !requester->waiting(queuedId)
					&& !requester->waiting(transfer->requestId),
				u"wallet intercept self-test: S3 a second teardown does "
				"only what was left and stays sealed and disarmed"_q,
				u"quiesceCalls=%1 armed=%2 sealed=%3 disarmedAt=%4 "
				"queued=[%5] rows=[%6]"_q.arg(
					QString::number(view->calls),
					Bit(intercept.armed()),
					Bit(intercept.sealed()),
					QString::number(intercept.disarmedAt()),
					CallText(intercept, requester, queuedId),
					RowsText(intercept.probe().rowsSince(secondMark))));

			auto &control = state->unsealed;
			control.arm(requester->identity());
			control.disarm();
			const auto controlMark = control.probe().mark();
			const auto controlSuppressed = control.intercept(
				requester,
				transfer);
			const auto controlRows = control.probe().rowsSince(controlMark);
			const auto controlRow = RowWith(
				controlRows,
				u"passed wallet transfer id=%1 "_q.arg(
					QString::number(transfer->requestId)));
			Check(
				!controlSuppressed
					&& (control.passedTransfers() == 1)
					&& !control.sealed()
					&& !controlRow.isEmpty(),
				u"wallet intercept self-test: S3 control: armed and "
				"disarmed without the seal, the same wallet.sendTransfer "
				"would be let through"_q,
				u"suppressed=%1 passed=%2 sealed=%3 rows=[%4]"_q.arg(
					Bit(controlSuppressed),
					QString::number(control.passedTransfers()),
					Bit(control.sealed()),
					RowsText(controlRows)));
		},
	});
}

} // namespace Test

#endif // _DEBUG
