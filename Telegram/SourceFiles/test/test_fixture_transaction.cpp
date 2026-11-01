/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_fixture_transaction.h"

#include "test/test_log.h"
#include "test/test_runner.h"
#include "settings.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>

#include <limits>

namespace Test {
namespace {

const auto kSchema = u"test-fixture-obligation-1"_q;
const auto kRecordFolder = u"test_fixture_obligations/"_q;

// Larger server waits are unreadable here, never shortened to a guess.
constexpr auto kMaxRateSeconds = qint64(std::numeric_limits<int>::max());

[[nodiscard]] QString ObligationDir() {
	return cWorkingDir() + kRecordFolder;
}

[[nodiscard]] bool ValidLabel(const QString &label) {
	static const auto expression = QRegularExpression(
		QRegularExpression::anchoredPattern(u"[A-Za-z0-9_-]+"_q));
	return expression.match(label).hasMatch();
}

[[nodiscard]] std::optional<crl::time> RateDelay(const MTP::Error &error) {
	static const auto expression = QRegularExpression(
		QRegularExpression::anchoredPattern(
			u"FLOOD_(?:PREMIUM_)?WAIT_(\\d+)"_q));
	const auto match = expression.match(error.type());
	if (!match.hasMatch()) {
		return std::nullopt;
	}
	auto ok = false;
	const auto seconds = match.captured(1).toLongLong(&ok);
	if (!ok || seconds > kMaxRateSeconds) {
		return std::nullopt;
	}
	return crl::time(seconds) * 1000;
}

[[nodiscard]] QString OperationName(FixtureOperation operation) {
	switch (operation) {
	case FixtureOperation::None: return u"none"_q;
	case FixtureOperation::Transition: return u"transition"_q;
	case FixtureOperation::Restore: return u"restore"_q;
	}
	Unexpected("Operation in Test::OperationName.");
}

[[nodiscard]] QString OutcomeName(FixtureOutcome outcome) {
	switch (outcome) {
	case FixtureOutcome::None: return u"none"_q;
	case FixtureOutcome::Equal: return u"equal"_q;
	case FixtureOutcome::Written: return u"written"_q;
	case FixtureOutcome::Unresolved: return u"unresolved"_q;
	case FixtureOutcome::Refused: return u"refused"_q;
	}
	Unexpected("Outcome in Test::OutcomeName.");
}

[[nodiscard]] QString FlagText(bool value) {
	return value ? u"1"_q : u"0"_q;
}

[[nodiscard]] QString DeadlineText(crl::time notBefore, crl::time now) {
	return notBefore ? QString::number(notBefore - now) : u"none"_q;
}

[[nodiscard]] QString ResultText(
		const QString &label,
		const FixtureResult &result,
		crl::time now) {
	return u"FIXTURE_TRANSACTION: label=%1 op=%2 outcome=%3 reason=[%4] "
		"reads=%5 writes=%6 waits=%7 pending=%8 notBeforeInMs=%9"_q.arg(
			label,
			OperationName(result.operation),
			OutcomeName(result.outcome),
			result.reason,
			QString::number(result.reads),
			QString::number(result.writes),
			QString::number(result.waits),
			FlagText(result.pending),
			DeadlineText(result.notBefore, now));
}

[[nodiscard]] QString RateText(
		const QString &label,
		FixtureOperation operation,
		const QString &type,
		crl::time notBefore,
		crl::time now) {
	return u"FIXTURE_TRANSACTION: label=%1 op=%2 rate=[%3] "
		"notBeforeInMs=%4"_q.arg(
			label,
			OperationName(operation),
			type,
			DeadlineText(notBefore, now));
}

[[nodiscard]] QString ReleaseText(
		const QString &label,
		bool busy,
		bool pending,
		crl::time notBefore,
		crl::time now) {
	return u"FIXTURE_TRANSACTION: label=%1 released busy=%2 pending=%3 "
		"notBeforeInMs=%4"_q.arg(
			label,
			FlagText(busy),
			FlagText(pending),
			DeadlineText(notBefore, now));
}

} // namespace

FixtureTransaction::FixtureTransaction(FixtureTransactionArgs &&args)
: _args(std::move(args)) {
	const auto &transport = _args.transport;
	const auto &clock = _args.clock;
	_refusal = !ValidLabel(_args.label)
		? u"invalid-label"_q
		: !_args.authorize
		? u"no-authorization"_q
		: (!_args.equal
			|| !transport.read
			|| !transport.write
			|| (!clock.now != !clock.schedule))
		? u"incomplete-arguments"_q
		: QString();
	if (_refusal.isEmpty()) {
		const auto dir = _args.recordDir.isEmpty()
			? ObligationDir()
			: _args.recordDir;
		_path = (dir.endsWith('/') ? dir : (dir + '/'))
			+ _args.label
			+ u".json"_q;
		switch (LoadRecord(_path, _args.label, _args.target, _record)) {
		case RecordLoad::Absent:
			break;
		case RecordLoad::Valid:
			_originalUsable = _record.pending;
			break;
		case RecordLoad::Malformed:
			_refusal = u"malformed-record"_q;
			break;
		case RecordLoad::Foreign:
			_refusal = u"foreign-record"_q;
			break;
		}
	}
	_result.pending = _record.pending;
	_result.notBefore = _record.notBefore;
	if (!_refusal.isEmpty()) {
		_result.outcome = FixtureOutcome::Refused;
		_result.reason = _refusal;
	}
	if (!_refusal.isEmpty() || _record.pending) {
		Note(ResultText(_args.label, _result, now()));
	}
}

FixtureTransaction::RecordLoad FixtureTransaction::LoadRecord(
		const QString &path,
		const QString &label,
		const QString &target,
		Record &record) {
	if (!QFile::exists(path)) {
		return RecordLoad::Absent;
	}
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return RecordLoad::Malformed;
	}
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(file.readAll(), &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		return RecordLoad::Malformed;
	}
	const auto object = document.object();
	const auto stored = object.value(u"target"_q);
	if (object.value(u"schema"_q).toString() != kSchema
		|| object.value(u"label"_q).toString() != label
		|| !stored.isString()) {
		return RecordLoad::Malformed;
	} else if (stored.toString() != target) {
		return RecordLoad::Foreign;
	}
	const auto pending = object.value(u"pending"_q);
	const auto notBefore = object.value(u"notBeforeMs"_q);
	const auto deadline = notBefore.toInteger(-1);
	if (!pending.isBool() || !notBefore.isDouble() || deadline < 0) {
		return RecordLoad::Malformed;
	}
	auto result = Record{
		.notBefore = crl::time(deadline),
		.pending = pending.toBool(),
	};
	if (object.contains(u"original"_q)) {
		const auto original = object.value(u"original"_q);
		if (!original.isString()) {
			return RecordLoad::Malformed;
		}
		auto decoded = QByteArray::fromBase64Encoding(
			original.toString().toLatin1(),
			QByteArray::AbortOnBase64DecodingErrors);
		if (!decoded) {
			return RecordLoad::Malformed;
		}
		result.original = std::move(*decoded);
		result.originalKnown = true;
	}
	if (result.pending && !result.originalKnown) {
		return RecordLoad::Malformed;
	}
	record = std::move(result);
	return RecordLoad::Valid;
}

bool FixtureTransaction::SaveRecord(
		const QString &path,
		const QString &label,
		const QString &target,
		const Record &record) {
	auto object = QJsonObject{
		{ u"schema"_q, kSchema },
		{ u"label"_q, label },
		{ u"target"_q, target },
		{ u"pending"_q, record.pending },
		{ u"notBeforeMs"_q, QJsonValue(qint64(record.notBefore)) },
	};
	if (record.originalKnown) {
		object.insert(
			u"original"_q,
			QString::fromLatin1(record.original.toBase64()));
	}
	const auto content = QJsonDocument(object).toJson(
		QJsonDocument::Compact);
	auto file = QSaveFile(path);
	return QDir().mkpath(QFileInfo(path).absolutePath())
		&& file.open(QIODevice::WriteOnly)
		&& (file.write(content) == content.size())
		&& file.commit();
}

void FixtureTransaction::transition(FixtureState requested) {
	begin(FixtureOperation::Transition, std::move(requested));
}

void FixtureTransaction::restore() {
	begin(FixtureOperation::Restore, _record.original);
}

void FixtureTransaction::release() {
	if (_released) {
		return;
	}
	_released = true;
	_timer.cancel();
	const auto wasBusy = _busy;
	if (wasBusy) {
		conclude(FixtureOutcome::Unresolved, u"released"_q);
	}
	if (wasBusy || _record.pending) {
		Note(ReleaseText(
			_args.label,
			wasBusy,
			_record.pending,
			_record.notBefore,
			now()));
	}
}

bool FixtureTransaction::busy() const {
	return _busy;
}

const FixtureResult &FixtureTransaction::result() const {
	return _result;
}

crl::time FixtureTransaction::now() const {
	return _args.clock.now
		? _args.clock.now()
		: crl::time(QDateTime::currentMSecsSinceEpoch());
}

bool FixtureTransaction::persist(const Record &record) const {
	return SaveRecord(_path, _args.label, _args.target, record);
}

bool FixtureTransaction::authorized() {
	const auto refusal = _args.authorize();
	if (refusal.isEmpty()) {
		return true;
	}
	finish(FixtureOutcome::Refused, u"unauthorized: "_q + refusal);
	return false;
}

void FixtureTransaction::begin(
		FixtureOperation operation,
		FixtureState target) {
	Expects(!_busy);

	_result = FixtureResult{ .operation = operation };
	_busy = true;
	_target = std::move(target);
	const auto restoring = (operation == FixtureOperation::Restore);
	const auto refusal = !_refusal.isEmpty()
		? _refusal
		: _released
		? u"released"_q
		: (!restoring && _record.pending && !_ownsObligation)
		? u"pending-restoration"_q
		: (restoring && !_originalUsable)
		? u"no-obligation"_q
		: QString();
	if (refusal.isEmpty()) {
		proceed();
	} else {
		finish(FixtureOutcome::Refused, refusal);
	}
}

void FixtureTransaction::proceed() {
	_verifying = false;
	const auto left = _record.notBefore - now();
	if (left <= 0) {
		sendRead();
	} else if (left > _args.maxWait) {
		finish(FixtureOutcome::Unresolved, u"deadline-beyond-bound"_q);
	} else {
		++_result.waits;
		wait(_record.notBefore);
	}
}

void FixtureTransaction::wait(crl::time at) {
	const auto callback = crl::guard(this, [=] {
		resume();
	});
	if (_args.clock.schedule) {
		_args.clock.schedule(at, callback);
	} else {
		_timer.setCallback(callback);
		_timer.callOnce(
			std::max(at - now(), crl::time(1)),
			Qt::PreciseTimer);
	}
}

void FixtureTransaction::resume() {
	if (_released) {
		return;
	} else if (now() < _record.notBefore) {
		wait(_record.notBefore);
	} else {
		_verifying = false;
		sendRead();
	}
}

void FixtureTransaction::sendRead() {
	if (!authorized()) {
		return;
	}
	++_result.reads;
	_args.transport.read(
		crl::guard(this, [=](FixtureState current) {
			onRead(std::move(current));
		}),
		crl::guard(this, [=](const MTP::Error &error) {
			onError(error);
		}));
}

void FixtureTransaction::sendWrite() {
	if (!authorized()) {
		return;
	}
	auto record = _record;
	record.pending = true;
	if (!persist(record)) {
		finish(FixtureOutcome::Refused, u"record-not-persisted"_q);
		return;
	}
	_record = std::move(record);
	if (_result.operation == FixtureOperation::Transition) {
		_ownsObligation = true;
		_originalUsable = true;
	}
	_verifying = true;
	++_result.writes;
	_args.transport.write(
		_target,
		crl::guard(this, [=] {
			onWritten();
		}),
		crl::guard(this, [=](const MTP::Error &error) {
			onError(error);
		}));
}

void FixtureTransaction::onRead(FixtureState current) {
	if (_released) {
		return;
	}
	const auto restoring = (_result.operation == FixtureOperation::Restore);
	if (_verifying) {
		if (!_args.equal(current, _target)) {
			finish(FixtureOutcome::Unresolved, u"verify-unequal"_q);
		} else if (restoring) {
			finishRestored(FixtureOutcome::Written);
		} else {
			finish(FixtureOutcome::Written);
		}
		return;
	}
	if (!restoring && !_record.pending) {
		_record.original = current;
		_record.originalKnown = true;
		_originalUsable = true;
	}
	if (!_args.equal(current, _target)) {
		sendWrite();
	} else if (restoring && _record.pending) {
		finishRestored(FixtureOutcome::Equal);
	} else {
		finish(FixtureOutcome::Equal);
	}
}

void FixtureTransaction::onWritten() {
	if (!_released) {
		sendRead();
	}
}

void FixtureTransaction::onError(const MTP::Error &error) {
	if (_released) {
		return;
	}
	const auto delay = RateDelay(error);
	if (!delay) {
		finish(FixtureOutcome::Unresolved, error.type());
		return;
	}
	const auto at = now();
	_record.notBefore = std::max(_record.notBefore, at + *delay);
	if (!persist(_record)) {
		finish(FixtureOutcome::Unresolved, u"record-not-persisted"_q);
		return;
	}
	Note(RateText(
		_args.label,
		_result.operation,
		error.type(),
		_record.notBefore,
		at));
	if (_result.waits > 0) {
		finish(FixtureOutcome::Unresolved, u"rate-retry-exhausted"_q);
	} else if (_record.notBefore - at > _args.maxWait) {
		finish(FixtureOutcome::Unresolved, u"deadline-beyond-bound"_q);
	} else {
		++_result.waits;
		wait(_record.notBefore);
	}
}

void FixtureTransaction::finishRestored(FixtureOutcome outcome) {
	auto record = _record;
	record.pending = false;
	if (!persist(record)) {
		finish(FixtureOutcome::Unresolved, u"record-not-persisted"_q);
		return;
	}
	_record = std::move(record);
	_ownsObligation = false;
	finish(outcome);
}

void FixtureTransaction::finish(
		FixtureOutcome outcome,
		const QString &reason) {
	conclude(outcome, reason);
	Note(ResultText(_args.label, _result, now()));
}

void FixtureTransaction::conclude(
		FixtureOutcome outcome,
		const QString &reason) {
	_busy = false;
	_result.outcome = outcome;
	_result.reason = reason;
	_result.pending = _record.pending;
	_result.notBefore = _record.notBefore;
}

std::shared_ptr<FixtureTransaction> OpenFixtureTransaction(
		not_null<Runner*> runner,
		FixtureTransactionArgs args) {
	auto result = std::make_shared<FixtureTransaction>(std::move(args));
	runner->onFinish([weak = base::make_weak(result)] {
		if (const auto strong = weak.get()) {
			strong->release();
		}
	});
	return result;
}

namespace {

// The self-test plays the caller: privacy-rule-shaped literal states, the
// caller's own equality, a fake server and a fake wall clock. Rows name
// states (original, requested, ...) and print times relative to a deadline,
// never a state, a person id, the target, an original or a path.
const auto kSelfTestFolder = u"fixture-transaction-self-test/"_q;
const auto kOriginal = "allowContacts;allowUsers:7300411,7300412;"
	"disallowAll"_q;
const auto kRequested = "disallowAll"_q;
const auto kReordered = "allowContacts;allowUsers:7300412,7300411;"
	"disallowAll"_q;
const auto kAbsentRule = "allowContacts;disallowAll"_q;
const auto kPresentEmpty = "allowContacts;allowUsers:;disallowAll"_q;
const auto kExtraPerson = "allowContacts;allowUsers:7300411,7300412,"
	"7300413;disallowAll"_q;
const auto kSwapped = "disallowAll;allowContacts;"
	"allowUsers:7300411,7300412"_q;
const auto kTarget = u"self-test-owner-7300499"_q;
const auto kOtherTarget = u"self-test-other-owner"_q;
const auto kPersonIds = std::array{
	"7300411"_q,
	"7300412"_q,
	"7300413"_q,
	"7300499"_q,
};
const auto kStateText = "allowContacts;allowUsers"_q;
const auto kDenied = u"fixture account not authorized by construction"_q;
const auto kWithdrawn = u"fixture account authorization withdrawn by "
	"construction"_q;

constexpr auto kEpoch = crl::time(1800) * 1000 * 1000 * 1000;
constexpr auto kHour = crl::time(3600) * 1000;
constexpr auto kRealClockTimeout = crl::time(5000);

struct FakeClock {
	void advance(crl::time to);
	void fireEarly();

	crl::time now = kEpoch;
	std::vector<std::pair<crl::time, Fn<void()>>> scheduled;
};

// Stores the state and answers; requests are stamped (write, at) by |now|.
struct FakeServer {
	[[nodiscard]] int requestsFrom(crl::time at) const;

	FixtureState state;
	std::vector<std::pair<bool, crl::time>> requests;
	std::deque<QString> writeErrors;
	std::deque<QString> readErrors;
	bool holdReads = false;
	Fn<void()> held;
	bool partialWrite = false;
	Fn<void()> beforeWrite;
	Fn<crl::time()> now;
};

struct Leg {
	QString path;
	std::shared_ptr<FakeServer> server;
	std::shared_ptr<FakeClock> clock;
	std::shared_ptr<FixtureTransaction> transaction;
};

struct SelfTest {
	QString dir;
	qint64 logOffset = 0;
	QStringList labels;
	crl::time resumeDeadline = 0;
	std::shared_ptr<FakeServer> resumeServer;
	Leg realClock;
	Leg finish;
};

struct Refusals {
	bool ok = false;
	QString text;
};

void FakeClock::advance(crl::time to) {
	while (true) {
		auto due = std::vector<std::pair<crl::time, Fn<void()>>>();
		for (auto &entry : base::take(scheduled)) {
			if (entry.first <= to) {
				due.push_back(std::move(entry));
			} else {
				scheduled.push_back(std::move(entry));
			}
		}
		if (due.empty()) {
			break;
		}
		ranges::stable_sort(due, ranges::less(), [](const auto &entry) {
			return entry.first;
		});
		for (const auto &[at, callback] : due) {
			now = std::max(now, at);
			callback();
		}
	}
	now = std::max(now, to);
}

void FakeClock::fireEarly() {
	for (const auto &entry : base::take(scheduled)) {
		entry.second();
	}
}

int FakeServer::requestsFrom(crl::time at) const {
	return int(ranges::count_if(requests, [&](const auto &request) {
		return request.second >= at;
	}));
}

[[nodiscard]] QList<QByteArray> People(const QByteArray &list) {
	auto result = list.split(',');
	result.removeAll(QByteArray());
	ranges::sort(result);
	result.erase(ranges::unique(result), result.end());
	return result;
}

[[nodiscard]] bool RuleEqual(const QByteArray &a, const QByteArray &b) {
	const auto colonA = a.indexOf(':');
	const auto colonB = b.indexOf(':');
	if (colonA < 0 || colonB < 0) {
		return (a == b);
	}
	return (a.left(colonA) == b.left(colonB))
		&& (People(a.mid(colonA + 1)) == People(b.mid(colonB + 1)));
}

// The caller's equality: rule order is meaningful, people order is not, and
// a rule listing nobody differs from an absent rule.
[[nodiscard]] bool RulesEqual(const FixtureState &a, const FixtureState &b) {
	return ranges::equal(a.split(';'), b.split(';'), RuleEqual);
}

[[nodiscard]] MTP::Error RpcError(const QString &type) {
	const auto code = type.startsWith(u"FLOOD_"_q) ? 420 : 400;
	return MTP::Error(MTP_rpc_error(MTP_int(code), MTP_string(type)));
}

void FakeRead(
		FakeServer &server,
		Fn<void(FixtureState)> done,
		Fn<void(const MTP::Error &)> fail) {
	server.requests.emplace_back(false, server.now());
	if (!server.readErrors.empty()) {
		const auto type = server.readErrors.front();
		server.readErrors.pop_front();
		fail(RpcError(type));
	} else if (server.holdReads) {
		server.held = [done = std::move(done), state = server.state] {
			done(state);
		};
	} else {
		done(server.state);
	}
}

void FakeWrite(
		FakeServer &server,
		const FixtureState &state,
		Fn<void()> done,
		Fn<void(const MTP::Error &)> fail) {
	server.requests.emplace_back(true, server.now());
	if (server.beforeWrite) {
		server.beforeWrite();
	}
	if (!server.writeErrors.empty()) {
		const auto type = server.writeErrors.front();
		server.writeErrors.pop_front();
		fail(RpcError(type));
		return;
	}
	server.state = server.partialWrite ? kExtraPerson : state;
	done();
}

[[nodiscard]] FixtureTransactionArgs FakeArgs(
		const QString &label,
		std::shared_ptr<FakeServer> server,
		std::shared_ptr<FakeClock> clock,
		const QString &dir,
		Fn<QString()> authorize) {
	if (clock) {
		server->now = [=] { return clock->now; };
	} else {
		server->now = [] {
			return crl::time(QDateTime::currentMSecsSinceEpoch());
		};
	}
	auto result = FixtureTransactionArgs{
		.label = label,
		.target = kTarget,
		.authorize = authorize
			? std::move(authorize)
			: Fn<QString()>([] { return QString(); }),
		.transport = {
			.read = [=](
					Fn<void(FixtureState)> done,
					Fn<void(const MTP::Error &)> fail) {
				FakeRead(*server, std::move(done), std::move(fail));
			},
			.write = [=](
					const FixtureState &state,
					Fn<void()> done,
					Fn<void(const MTP::Error &)> fail) {
				FakeWrite(*server, state, std::move(done), std::move(fail));
			},
		},
		.equal = RulesEqual,
		.recordDir = dir,
	};
	if (clock) {
		result.clock = {
			.now = [=] { return clock->now; },
			.schedule = [=](crl::time at, Fn<void()> callback) {
				clock->scheduled.emplace_back(at, std::move(callback));
			},
		};
	}
	return result;
}

[[nodiscard]] std::shared_ptr<FakeServer> Server(const FixtureState &state) {
	auto result = std::make_shared<FakeServer>();
	result->state = state;
	return result;
}

[[nodiscard]] QString RecordPath(
		const SelfTest &state,
		const QString &label) {
	return state.dir + label + u".json"_q;
}

[[nodiscard]] Leg OpenLeg(
		not_null<Runner*> runner,
		SelfTest &state,
		const QString &label,
		std::shared_ptr<FakeServer> server,
		std::shared_ptr<FakeClock> clock = std::make_shared<FakeClock>(),
		Fn<QString()> authorize = nullptr) {
	state.labels.push_back(label);
	auto args = FakeArgs(label, server, clock, state.dir, std::move(authorize));
	return {
		.path = RecordPath(state, label),
		.server = std::move(server),
		.clock = std::move(clock),
		.transaction = OpenFixtureTransaction(runner, std::move(args)),
	};
}

// The starting point of every bounded-refusal and teardown case.
[[nodiscard]] bool TransitionWritten(const Leg &leg) {
	leg.transaction->transition(kRequested);
	return !leg.transaction->busy()
		&& (leg.transaction->result().outcome == FixtureOutcome::Written)
		&& (leg.server->state == kRequested);
}

[[nodiscard]] bool Ended(
		const FixtureTransaction &transaction,
		FixtureOutcome outcome,
		const QString &reason = QString()) {
	const auto &result = transaction.result();
	return !transaction.busy()
		&& (result.outcome == outcome)
		&& (result.reason == reason);
}

[[nodiscard]] QByteArray ReadBytes(const QString &path, qint64 from = 0) {
	auto file = QFile(path);
	return (file.open(QIODevice::ReadOnly) && file.seek(from))
		? file.readAll()
		: QByteArray();
}

[[nodiscard]] bool WriteFile(const QString &path, const QByteArray &content) {
	auto file = QFile(path);
	return QDir().mkpath(QFileInfo(path).absolutePath())
		&& file.open(QIODevice::WriteOnly | QIODevice::Truncate)
		&& (file.write(content) == content.size());
}

// The independent oracle: the record as it stands on disk.
[[nodiscard]] QJsonObject ReadRecordFile(const QString &path) {
	return QJsonDocument::fromJson(ReadBytes(path)).object();
}

[[nodiscard]] bool RewriteRecordField(
		const QString &path,
		const QString &key,
		const QJsonValue &value) {
	auto record = ReadRecordFile(path);
	if (record.isEmpty()) {
		return false;
	}
	record.insert(key, value);
	return WriteFile(
		path,
		QJsonDocument(record).toJson(QJsonDocument::Compact));
}

[[nodiscard]] bool RecordPending(const QJsonObject &record) {
	return record.value(u"pending"_q).toBool();
}

[[nodiscard]] FixtureState RecordOriginal(const QJsonObject &record) {
	return QByteArray::fromBase64(
		record.value(u"original"_q).toString().toLatin1());
}

[[nodiscard]] crl::time RecordNotBefore(const QJsonObject &record) {
	return crl::time(record.value(u"notBeforeMs"_q).toInteger());
}

[[nodiscard]] int Occurrences(
		const QByteArray &haystack,
		const QByteArray &needle) {
	auto result = 0;
	for (auto from = haystack.indexOf(needle)
		; from >= 0
		; from = haystack.indexOf(needle, from + 1)) {
		++result;
	}
	return result;
}

[[nodiscard]] QString SelfTestLogPath() {
	return EvidenceDir() + u"test_log.txt"_q;
}

[[nodiscard]] QString StateName(const FixtureState &state) {
	const auto named = std::array{
		std::pair(kOriginal, u"original"_q),
		std::pair(kRequested, u"requested"_q),
		std::pair(kReordered, u"reordered"_q),
		std::pair(kAbsentRule, u"absentRule"_q),
		std::pair(kPresentEmpty, u"presentEmpty"_q),
		std::pair(kExtraPerson, u"extraPerson"_q),
		std::pair(kSwapped, u"swapped"_q),
	};
	for (const auto &[value, name] : named) {
		if (value == state) {
			return name;
		}
	}
	return state.isEmpty() ? u"empty"_q : u"other"_q;
}

[[nodiscard]] QString Relative(crl::time delta) {
	return ((delta >= 0) ? u"+"_q : QString()) + QString::number(delta);
}

[[nodiscard]] int RequestCount(const Leg &leg) {
	return int(leg.server->requests.size());
}

// "rwr": the request kinds from |from| on, in order.
[[nodiscard]] QString Order(const FakeServer &server, int from = 0) {
	auto result = QString();
	for (auto i = from; i < int(server.requests.size()); ++i) {
		result += QChar(server.requests[i].first ? 'w' : 'r');
	}
	return result;
}

[[nodiscard]] QString RequestsText(
		const FakeServer &server,
		crl::time reference) {
	auto result = QStringList();
	for (const auto &[write, at] : server.requests) {
		const auto kind = write ? u"w@"_q : u"r@"_q;
		result.push_back(kind + Relative(at - reference));
	}
	return u"requests=["_q + result.join(',') + ']';
}

[[nodiscard]] bool ScheduledFrom(const FakeClock &clock, crl::time at) {
	return !clock.scheduled.empty()
		&& ranges::all_of(clock.scheduled, [&](const auto &entry) {
			return entry.first >= at;
		});
}

[[nodiscard]] QString ScheduledText(
		const FakeClock &clock,
		crl::time reference) {
	auto result = QStringList();
	for (const auto &entry : clock.scheduled) {
		result.push_back(Relative(entry.first - reference));
	}
	return u"scheduled=["_q + result.join(',') + ']';
}

[[nodiscard]] QString ResultReading(const FixtureTransaction &transaction) {
	const auto &result = transaction.result();
	return u"busy=%1 outcome=%2 reason=[%3] reads=%4 writes=%5 waits=%6 "
		"pending=%7"_q.arg(
			FlagText(transaction.busy()),
			OutcomeName(result.outcome),
			result.reason,
			QString::number(result.reads),
			QString::number(result.writes),
			QString::number(result.waits),
			FlagText(result.pending));
}

[[nodiscard]] QString RecordText(
		const QJsonObject &record,
		crl::time reference) {
	if (record.isEmpty()) {
		return u"record=absent"_q;
	}
	const auto notBefore = RecordNotBefore(record);
	return u"recordPending=%1 recordOriginal=%2 recordNotBefore=%3"_q.arg(
		FlagText(RecordPending(record)),
		(record.contains(u"original"_q)
			? StateName(RecordOriginal(record))
			: u"none"_q),
		notBefore ? Relative(notBefore - reference) : u"none"_q);
}

[[nodiscard]] QString LegText(const Leg &leg, crl::time reference) {
	return QStringList{
		ResultReading(*leg.transaction),
		RequestsText(*leg.server, reference),
		u"server="_q + StateName(leg.server->state),
		RecordText(ReadRecordFile(leg.path), reference),
	}.join(' ');
}

[[nodiscard]] bool RecordHolds(
		const QJsonObject &record,
		bool pending,
		crl::time notBefore) {
	return (RecordPending(record) == pending)
		&& (RecordOriginal(record) == kOriginal)
		&& (RecordNotBefore(record) == notBefore);
}

void SelfArm(SelfTest &state) {
	state.logOffset = QFileInfo(SelfTestLogPath()).size();
	state.dir = EvidenceDir() + kSelfTestFolder;
	const auto fresh = QDir(state.dir).removeRecursively();
	const auto under = state.dir.startsWith(EvidenceDir());
	const auto separate = (QDir::cleanPath(state.dir)
		!= QDir::cleanPath(ObligationDir()));
	Check(
		fresh && under && separate,
		u"fixture transaction self-test: records go to a fresh folder under "
		"the evidence directory, never the live copy's"_q,
		u"fresh=%1 underEvidence=%2 separateFromLive=%3"_q.arg(
			FlagText(fresh),
			FlagText(under),
			FlagText(separate)));
}

void SelfEqual(not_null<Runner*> runner, SelfTest &state) {
	const auto leg = OpenLeg(runner, state, u"ft-equal"_q, Server(kOriginal));
	leg.transaction->transition(kOriginal);
	const auto &result = leg.transaction->result();
	const auto recorded = QFile::exists(leg.path);
	Check(
		Ended(*leg.transaction, FixtureOutcome::Equal)
			&& (result.reads == 1)
			&& (result.writes == 0)
			&& (Order(*leg.server) == u"r"_q)
			&& !recorded,
		u"fixture transaction self-test: an equal fresh read sends no write "
		"and writes no record"_q,
		LegText(leg, kEpoch) + u" recordExists="_q + FlagText(recorded));
}

void SelfWrite(not_null<Runner*> runner, SelfTest &state) {
	struct AtWrite {
		int writes = 0;
		bool pending = false;
		bool original = false;
		bool schema = false;
		bool label = false;
	};
	const auto atWrite = std::make_shared<AtWrite>();
	const auto leg = OpenLeg(runner, state, u"ft-write"_q, Server(kOriginal));
	leg.server->beforeWrite = [=, path = leg.path] {
		if (++atWrite->writes > 1) {
			return;
		}
		const auto record = ReadRecordFile(path);
		atWrite->pending = RecordPending(record);
		atWrite->original = (record.value(u"original"_q).toString()
			== QString::fromLatin1(kOriginal.toBase64()));
		atWrite->schema = (record.value(u"schema"_q).toString() == kSchema);
		atWrite->label = (record.value(u"label"_q).toString()
			== u"ft-write"_q);
	};
	const auto &result = leg.transaction->result();
	leg.transaction->transition(kRequested);
	Check(
		Ended(*leg.transaction, FixtureOutcome::Written)
			&& (result.reads == 2)
			&& (result.writes == 1)
			&& (Order(*leg.server) == u"rwr"_q)
			&& (leg.server->state == kRequested)
			&& atWrite->pending
			&& atWrite->original
			&& atWrite->schema
			&& atWrite->label,
		u"fixture transaction self-test: a necessary transition writes once, "
		"after the record holds the original, and succeeds on a fresh "
		"read"_q,
		LegText(leg, kEpoch)
			+ u" atWrite: recordPending=%1 recordOriginalEqual=%2 schema=%3 "
			"label=%4"_q.arg(
				FlagText(atWrite->pending),
				FlagText(atWrite->original),
				FlagText(atWrite->schema),
				FlagText(atWrite->label)));

	leg.transaction->transition(kSwapped);
	Check(
		Ended(*leg.transaction, FixtureOutcome::Written)
			&& (leg.server->state == kSwapped)
			&& RecordHolds(ReadRecordFile(leg.path), true, 0),
		u"fixture transaction self-test: a further transition of the same "
		"instance keeps the first original"_q,
		LegText(leg, kEpoch));

	leg.transaction->restore();
	Check(
		Ended(*leg.transaction, FixtureOutcome::Written)
			&& (leg.server->state == kOriginal)
			&& !result.pending
			&& RecordHolds(ReadRecordFile(leg.path), false, 0),
		u"fixture transaction self-test: its restore writes the exact "
		"original back and clears the obligation"_q,
		LegText(leg, kEpoch));

	const auto partial = OpenLeg(
		runner,
		state,
		u"ft-partial"_q,
		Server(kOriginal));
	partial.server->partialWrite = true;
	partial.transaction->transition(kRequested);
	const auto &unequal = partial.transaction->result();
	Check(
		Ended(
			*partial.transaction,
			FixtureOutcome::Unresolved,
			u"verify-unequal"_q)
			&& (unequal.reads == 2)
			&& (unequal.writes == 1)
			&& unequal.pending
			&& RecordHolds(ReadRecordFile(partial.path), true, 0),
		u"fixture transaction self-test: a write whose fresh read differs "
		"stays unresolved and keeps the obligation"_q,
		LegText(partial, kEpoch));
}

void SelfNotBefore(not_null<Runner*> runner, SelfTest &state) {
	const auto leg = OpenLeg(runner, state, u"ft-rate"_q, Server(kOriginal));
	leg.server->writeErrors.push_back(u"FLOOD_WAIT_13"_q);
	leg.transaction->transition(kRequested);
	const auto refusalAt = leg.server->requests.back().second;
	const auto refused = RequestCount(leg);
	const auto notBefore = RecordNotBefore(ReadRecordFile(leg.path));
	Check(
		leg.transaction->busy()
			&& (leg.transaction->result().waits == 1)
			&& (notBefore == refusalAt + 13000)
			&& ScheduledFrom(*leg.clock, notBefore),
		u"fixture transaction self-test: a FLOOD_WAIT_13 refusal records the "
		"server deadline and schedules nothing before it"_q,
		QStringList{
			ResultReading(*leg.transaction),
			u"recordNotBeforeFromRefusal="_q + Relative(notBefore - refusalAt),
			ScheduledText(*leg.clock, notBefore),
		}.join(' '));

	leg.clock->fireEarly();
	const auto early = RequestCount(leg) - refused;
	const auto busyAfterEarly = leg.transaction->busy();
	leg.clock->advance(notBefore - 1);
	const auto beforeDeadline = RequestCount(leg) - refused;
	Check(
		!early
			&& !beforeDeadline
			&& busyAfterEarly
			&& leg.transaction->busy()
			&& ScheduledFrom(*leg.clock, notBefore),
		u"fixture transaction self-test: an early timer and the last "
		"millisecond before the deadline send nothing"_q,
		u"earlyRequests=%1 requestsBeforeDeadline=%2 busy=%3 "_q.arg(
			QString::number(early),
			QString::number(beforeDeadline),
			FlagText(leg.transaction->busy()))
			+ ScheduledText(*leg.clock, notBefore));

	leg.clock->advance(notBefore);
	const auto after = RequestCount(leg) - refused;
	const auto firstAfter = (after > 0)
		? leg.server->requests[refused].second
		: crl::time(0);
	Check(
		Ended(*leg.transaction, FixtureOutcome::Written)
			&& (leg.transaction->result().waits == 1)
			&& (Order(*leg.server, refused) == u"rwr"_q)
			&& (firstAfter == notBefore)
			&& (leg.server->requestsFrom(notBefore) == after)
			&& RecordHolds(ReadRecordFile(leg.path), true, notBefore),
		u"fixture transaction self-test: at the deadline one fresh read "
		"compares again, then the write and its verify complete"_q,
		LegText(leg, notBefore));
}

void SelfReadNotBefore(not_null<Runner*> runner, SelfTest &state) {
	const auto leg = OpenLeg(
		runner,
		state,
		u"ft-rate-read"_q,
		Server(kOriginal));
	leg.server->readErrors.push_back(u"FLOOD_WAIT_5"_q);
	leg.transaction->transition(kRequested);
	const auto refusalAt = leg.server->requests.front().second;
	const auto notBefore = RecordNotBefore(ReadRecordFile(leg.path));
	leg.clock->fireEarly();
	leg.clock->advance(notBefore - 1);
	const auto early = RequestCount(leg) - 1;
	leg.clock->advance(notBefore);
	Check(
		Ended(*leg.transaction, FixtureOutcome::Written)
			&& (leg.transaction->result().waits == 1)
			&& (notBefore == refusalAt + 5000)
			&& !early
			&& (Order(*leg.server) == u"rrwr"_q)
			&& (leg.server->requestsFrom(notBefore) == 3)
			&& RecordHolds(ReadRecordFile(leg.path), true, notBefore),
		u"fixture transaction self-test: a refused read waits for its "
		"deadline too, and the fresh read after it becomes the original"_q,
		LegText(leg, notBefore)
			+ u" requestsBeforeDeadline="_q
			+ QString::number(early));
}

void SelfRealClockStart(not_null<Runner*> runner, SelfTest &state) {
	auto server = Server(kOriginal);
	server->writeErrors.push_back(u"FLOOD_WAIT_1"_q);
	state.realClock = OpenLeg(
		runner,
		state,
		u"ft-real-clock"_q,
		std::move(server),
		nullptr);
	state.realClock.transaction->transition(kRequested);
}

void SelfRealClockCheck(SelfTest &state) {
	const auto leg = base::take(state.realClock);
	const auto notBefore = leg.transaction->result().notBefore;
	const auto &requests = leg.server->requests;
	const auto firstAfter = (requests.size() > 2)
		? requests[2].second
		: crl::time(0);
	Check(
		Ended(*leg.transaction, FixtureOutcome::Written)
			&& (leg.transaction->result().waits == 1)
			&& (notBefore > 0)
			&& (Order(*leg.server) == u"rwrwr"_q)
			&& (firstAfter >= notBefore)
			&& (leg.server->requestsFrom(notBefore) == 3),
		u"fixture transaction self-test: the default wall-clock timer sends "
		"nothing before a FLOOD_WAIT_1 deadline and completes"_q,
		ResultReading(*leg.transaction)
			+ ' '
			+ RequestsText(*leg.server, notBefore));
}

void SelfEqualAtDeadline(not_null<Runner*> runner, SelfTest &state) {
	const auto leg = OpenLeg(
		runner,
		state,
		u"ft-rate-equal"_q,
		Server(kOriginal));
	const auto applied = TransitionWritten(leg);
	leg.server->writeErrors.push_back(u"FLOOD_WAIT_13"_q);
	leg.transaction->restore();
	const auto waiting = leg.transaction->busy();
	const auto notBefore = RecordNotBefore(ReadRecordFile(leg.path));
	const auto refused = RequestCount(leg);
	leg.server->state = kOriginal;
	leg.clock->advance(notBefore);
	const auto &result = leg.transaction->result();
	Check(
		applied
			&& waiting
			&& Ended(*leg.transaction, FixtureOutcome::Equal)
			&& (result.reads == 2)
			&& (result.writes == 1)
			&& (result.waits == 1)
			&& !result.pending
			&& (Order(*leg.server, refused) == u"r"_q)
			&& (leg.server->state == kOriginal)
			&& RecordHolds(ReadRecordFile(leg.path), false, notBefore),
		u"fixture transaction self-test: an equal fresh read at the deadline "
		"is a no-op that clears the obligation"_q,
		LegText(leg, notBefore));
}

void SelfExhausted(not_null<Runner*> runner, SelfTest &state) {
	const auto leg = OpenLeg(
		runner,
		state,
		u"ft-exhausted"_q,
		Server(kOriginal));
	const auto applied = TransitionWritten(leg);
	leg.server->writeErrors = { u"FLOOD_WAIT_13"_q, u"FLOOD_WAIT_20"_q };
	leg.transaction->restore();
	leg.clock->advance(RecordNotBefore(ReadRecordFile(leg.path)));
	const auto secondAt = leg.server->requests.back().second;
	const auto settled = RequestCount(leg);
	const auto scheduled = !leg.clock->scheduled.empty();
	leg.clock->advance(leg.clock->now + kHour);
	const auto later = RequestCount(leg) - settled;
	const auto &result = leg.transaction->result();
	Check(
		applied
			&& Ended(
				*leg.transaction,
				FixtureOutcome::Unresolved,
				u"rate-retry-exhausted"_q)
			&& (result.writes == 2)
			&& (result.waits == 1)
			&& result.pending
			&& (result.notBefore == secondAt + 20000)
			&& RecordHolds(ReadRecordFile(leg.path), true, secondAt + 20000)
			&& !scheduled
			&& !later,
		u"fixture transaction self-test: a second rate refusal ends "
		"unresolved with the original and the next server deadline kept, "
		"and nothing retries"_q,
		LegText(leg, secondAt)
			+ u" scheduledAfter=%1 requestsInNextHour=%2"_q.arg(
				FlagText(scheduled),
				QString::number(later)));
}

void SelfZeroWait(not_null<Runner*> runner, SelfTest &state) {
	const auto leg = OpenLeg(runner, state, u"ft-zero"_q, Server(kOriginal));
	const auto applied = TransitionWritten(leg);
	leg.server->writeErrors = { u"FLOOD_WAIT_0"_q, u"FLOOD_WAIT_0"_q };
	leg.transaction->restore();
	const auto refusalAt = leg.server->requests.back().second;
	const auto retry = ScheduledText(*leg.clock, refusalAt);
	const auto atOnce = (leg.clock->scheduled.size() == 1)
		&& (leg.clock->scheduled.front().first == refusalAt);
	leg.clock->advance(leg.clock->now);
	const auto settled = RequestCount(leg);
	const auto scheduled = !leg.clock->scheduled.empty();
	leg.clock->advance(leg.clock->now + kHour);
	const auto later = RequestCount(leg) - settled;
	const auto &result = leg.transaction->result();
	Check(
		applied
			&& atOnce
			&& Ended(
				*leg.transaction,
				FixtureOutcome::Unresolved,
				u"rate-retry-exhausted"_q)
			&& (result.reads == 2)
			&& (result.writes == 2)
			&& (result.waits == 1)
			&& result.pending
			&& !scheduled
			&& !later,
		u"fixture transaction self-test: FLOOD_WAIT_0 twice retries once at "
		"once, then stops: no loop"_q,
		LegText(leg, refusalAt)
			+ u" retry "_q
			+ retry
			+ u" requestsInNextHour="_q
			+ QString::number(later));
}

void SelfBeyondBound(not_null<Runner*> runner, SelfTest &state) {
	const auto leg = OpenLeg(runner, state, u"ft-bound"_q, Server(kOriginal));
	const auto applied = TransitionWritten(leg);
	leg.server->writeErrors.push_back(u"FLOOD_WAIT_3600"_q);
	leg.transaction->restore();
	const auto refusalAt = leg.server->requests.back().second;
	const auto &result = leg.transaction->result();
	Check(
		applied
			&& Ended(
				*leg.transaction,
				FixtureOutcome::Unresolved,
				u"deadline-beyond-bound"_q)
			&& (result.writes == 1)
			&& (result.waits == 0)
			&& leg.clock->scheduled.empty()
			&& RecordHolds(ReadRecordFile(leg.path), true, refusalAt + kHour),
		u"fixture transaction self-test: a FLOOD_WAIT_3600 beyond the bound "
		"ends unresolved at once with the deadline recorded"_q,
		LegText(leg, refusalAt) + ' ' + ScheduledText(*leg.clock, refusalAt));
}

void SelfOtherErrors(not_null<Runner*> runner, SelfTest &state) {
	const auto cases = std::array{
		std::pair(u"ft-error"_q, u"FLOOD_WAIT_X"_q),
		std::pair(u"ft-error-400"_q, u"PRIVACY_INVALID"_q),
	};
	for (const auto &[label, type] : cases) {
		const auto leg = OpenLeg(runner, state, label, Server(kOriginal));
		const auto applied = TransitionWritten(leg);
		leg.server->writeErrors.push_back(type);
		leg.transaction->restore();
		const auto &result = leg.transaction->result();
		Check(
			applied
				&& Ended(*leg.transaction, FixtureOutcome::Unresolved, type)
				&& (result.writes == 1)
				&& (result.waits == 0)
				&& leg.clock->scheduled.empty()
				&& RecordHolds(ReadRecordFile(leg.path), true, 0),
			u"fixture transaction self-test: %1 ends unresolved at once, "
			"with no wait"_q.arg(type),
			LegText(leg, kEpoch) + ' ' + ScheduledText(*leg.clock, kEpoch));
	}
}

void SelfRelease(not_null<Runner*> runner, SelfTest &state) {
	const auto leg = OpenLeg(runner, state, u"ft-resume"_q, Server(kOriginal));
	const auto applied = TransitionWritten(leg);
	leg.server->writeErrors.push_back(u"FLOOD_WAIT_13"_q);
	leg.transaction->restore();
	const auto deadline = RecordNotBefore(ReadRecordFile(leg.path));
	const auto waiting = leg.transaction->busy();
	leg.transaction->release();
	const auto released = Ended(
		*leg.transaction,
		FixtureOutcome::Unresolved,
		u"released"_q);
	const auto releasedText = ResultReading(*leg.transaction);
	const auto settled = RequestCount(leg);
	leg.clock->advance(deadline);
	const auto atDeadline = RequestCount(leg) - settled;
	leg.transaction->restore();
	const auto refusedAfter = Ended(
		*leg.transaction,
		FixtureOutcome::Refused,
		u"released"_q);
	Check(
		applied
			&& waiting
			&& released
			&& !atDeadline
			&& refusedAfter
			&& (RequestCount(leg) == settled)
			&& leg.clock->scheduled.empty()
			&& RecordHolds(ReadRecordFile(leg.path), true, deadline),
		u"fixture transaction self-test: release while waiting stops the "
		"restore, sends nothing at the deadline and keeps the obligation"_q,
		u"released: "_q
			+ releasedText
			+ u" after: "_q
			+ LegText(leg, deadline)
			+ u" requestsAtDeadline="_q
			+ QString::number(atDeadline));
	state.resumeDeadline = deadline;
	state.resumeServer = leg.server;
}

void SelfHeldAnswer(not_null<Runner*> runner, SelfTest &state) {
	const auto leg = OpenLeg(runner, state, u"ft-held"_q, Server(kOriginal));
	const auto applied = TransitionWritten(leg);
	leg.server->holdReads = true;
	leg.transaction->restore();
	const auto held = leg.transaction->busy() && bool(leg.server->held);
	leg.transaction->release();
	const auto settled = RequestCount(leg);
	if (const auto deliver = base::take(leg.server->held)) {
		deliver();
	}
	Check(
		applied
			&& held
			&& Ended(
				*leg.transaction,
				FixtureOutcome::Unresolved,
				u"released"_q)
			&& (RequestCount(leg) == settled)
			&& (leg.server->state == kRequested)
			&& RecordHolds(ReadRecordFile(leg.path), true, 0),
		u"fixture transaction self-test: a read answer delivered after "
		"release sends no write and keeps the obligation"_q,
		LegText(leg, kEpoch) + u" held="_q + FlagText(held));
}

void SelfDestroyedOwner(not_null<Runner*> runner, SelfTest &state) {
	auto leg = OpenLeg(runner, state, u"ft-destroyed"_q, Server(kOriginal));
	const auto applied = TransitionWritten(leg);
	leg.server->writeErrors.push_back(u"FLOOD_WAIT_13"_q);
	leg.transaction->restore();
	const auto deadline = RecordNotBefore(ReadRecordFile(leg.path));
	const auto waiting = leg.transaction->busy()
		&& !leg.clock->scheduled.empty();
	const auto weak = base::make_weak(leg.transaction);
	leg.transaction = nullptr;
	const auto destroyed = !weak.get();
	const auto settled = RequestCount(leg);
	leg.clock->advance(deadline);
	const auto record = ReadRecordFile(leg.path);
	Check(
		applied
			&& waiting
			&& destroyed
			&& (RequestCount(leg) == settled)
			&& leg.clock->scheduled.empty()
			&& RecordHolds(record, true, deadline),
		u"fixture transaction self-test: a destroyed owner's scheduled "
		"callback sends nothing and the record keeps the obligation"_q,
		u"waiting=%1 destroyed=%2 "_q.arg(
			FlagText(waiting),
			FlagText(destroyed))
			+ RequestsText(*leg.server, deadline)
			+ ' '
			+ RecordText(record, deadline));
}

void SelfResume(not_null<Runner*> runner, SelfTest &state) {
	const auto path = RecordPath(state, u"ft-resume"_q);
	const auto control = ReadRecordFile(path);
	const auto rewritten = state.resumeDeadline + 1000;
	const auto edited = RewriteRecordField(
		path,
		u"notBeforeMs"_q,
		QJsonValue(qint64(rewritten)));
	const auto leg = OpenLeg(runner, state, u"ft-resume"_q, state.resumeServer);
	const auto &result = leg.transaction->result();
	Check(
		RecordPending(control)
			&& edited
			&& result.pending
			&& (result.notBefore == rewritten)
			&& (result.outcome == FixtureOutcome::None),
		u"fixture transaction self-test: a later instance reads the pending "
		"obligation and its deadline from the record file"_q,
		u"controlRecordPending=%1 edited=%2 loadedPending=%3 "
		"loadedNotBeforeFromRewritten=%4 outcome=%5"_q.arg(
			FlagText(RecordPending(control)),
			FlagText(edited),
			FlagText(result.pending),
			Relative(result.notBefore - rewritten),
			OutcomeName(result.outcome)));

	const auto start = RequestCount(leg);
	leg.transaction->transition(kRequested);
	Check(
		Ended(
			*leg.transaction,
			FixtureOutcome::Refused,
			u"pending-restoration"_q)
			&& (RequestCount(leg) == start),
		u"fixture transaction self-test: a new transition refuses while an "
		"earlier invocation's obligation is pending"_q,
		LegText(leg, rewritten));

	leg.transaction->restore();
	const auto waiting = leg.transaction->busy();
	leg.clock->fireEarly();
	const auto early = RequestCount(leg) - start;
	leg.clock->advance(rewritten - 1);
	const auto beforeDeadline = RequestCount(leg) - start;
	leg.clock->advance(rewritten);
	const auto sent = RequestCount(leg) - start;
	Check(
		waiting
			&& !early
			&& !beforeDeadline
			&& Ended(*leg.transaction, FixtureOutcome::Written)
			&& (result.waits == 1)
			&& (sent > 0)
			&& (leg.server->requestsFrom(rewritten) == sent)
			&& (leg.server->state == kOriginal)
			&& RecordHolds(ReadRecordFile(path), false, rewritten),
		u"fixture transaction self-test: the resumed restore waits for the "
		"recorded deadline, then writes the original back and verifies "
		"it"_q,
		LegText(leg, rewritten)
			+ u" earlyRequests=%1 requestsBeforeDeadline=%2"_q.arg(
				QString::number(early),
				QString::number(beforeDeadline)));

	const auto again = RequestCount(leg);
	leg.transaction->restore();
	Check(
		Ended(*leg.transaction, FixtureOutcome::Equal)
			&& (result.reads == 1)
			&& (result.writes == 0)
			&& !result.pending
			&& (RequestCount(leg) == again + 1),
		u"fixture transaction self-test: repeating a successful restore is "
		"an observed no-op"_q,
		LegText(leg, rewritten));

	const auto third = OpenLeg(
		runner,
		state,
		u"ft-resume"_q,
		state.resumeServer);
	const auto thirdStart = RequestCount(third);
	third.transaction->restore();
	Check(
		Ended(*third.transaction, FixtureOutcome::Refused, u"no-obligation"_q)
			&& (RequestCount(third) == thirdStart),
		u"fixture transaction self-test: a resolved record authorizes no "
		"restore by a later instance"_q,
		LegText(third, rewritten));
}

void SelfOriginals(not_null<Runner*> runner, SelfTest &state) {
	struct Control {
		QString name;
		FixtureState original;
		FixtureState server;
		bool equal = false;
	};
	const auto controls = std::array{
		Control{ u"reordered"_q, kOriginal, kReordered, true },
		Control{ u"absent"_q, kOriginal, kAbsentRule, false },
		Control{ u"extra"_q, kOriginal, kExtraPerson, false },
		Control{ u"swapped"_q, kOriginal, kSwapped, false },
		Control{ u"present-empty"_q, kAbsentRule, kPresentEmpty, false },
	};
	for (const auto &control : controls) {
		const auto leg = OpenLeg(
			runner,
			state,
			u"ft-ctl-"_q + control.name,
			Server(control.original));
		const auto applied = TransitionWritten(leg);
		leg.server->state = control.server;
		leg.transaction->restore();
		const auto &result = leg.transaction->result();
		const auto ok = control.equal
			? (Ended(*leg.transaction, FixtureOutcome::Equal)
				&& (result.writes == 0)
				&& (leg.server->state == control.server))
			: (Ended(*leg.transaction, FixtureOutcome::Written)
				&& (result.writes == 1)
				&& (leg.server->state == control.original));
		Check(
			applied && ok && !result.pending,
			u"fixture transaction self-test: restoring original %1 over "
			"%2 is %3"_q.arg(
				StateName(control.original),
				StateName(control.server),
				(control.equal
					? u"an equal no-op"_q
					: u"one write back to the exact original"_q)),
			LegText(leg, kEpoch));
	}
}

void SelfAuthorization(not_null<Runner*> runner, SelfTest &state) {
	const auto denied = OpenLeg(
		runner,
		state,
		u"ft-auth"_q,
		Server(kOriginal),
		std::make_shared<FakeClock>(),
		[] { return kDenied; });
	denied.transaction->transition(kRequested);
	const auto recorded = QFile::exists(denied.path);
	Check(
		Ended(
			*denied.transaction,
			FixtureOutcome::Refused,
			u"unauthorized: "_q + kDenied)
			&& denied.server->requests.empty()
			&& !recorded,
		u"fixture transaction self-test: a refusing authorization sends no "
		"request"_q,
		LegText(denied, kEpoch));

	const auto calls = std::make_shared<int>(0);
	const auto late = OpenLeg(
		runner,
		state,
		u"ft-auth-late"_q,
		Server(kOriginal),
		std::make_shared<FakeClock>(),
		[=] { return (++*calls > 1) ? kWithdrawn : QString(); });
	late.transaction->transition(kRequested);
	const auto &result = late.transaction->result();
	Check(
		Ended(
			*late.transaction,
			FixtureOutcome::Refused,
			u"unauthorized: "_q + kWithdrawn)
			&& (result.reads == 1)
			&& (result.writes == 0)
			&& !result.pending
			&& (Order(*late.server) == u"r"_q)
			&& (late.server->state == kOriginal)
			&& !RecordPending(ReadRecordFile(late.path)),
		u"fixture transaction self-test: authorization withdrawn before the "
		"write refuses it and records no obligation"_q,
		LegText(late, kEpoch)
			+ u" authorizeCalls="_q
			+ QString::number(*calls));
}

[[nodiscard]] Refusals RefusedEverywhere(
		not_null<Runner*> runner,
		const SelfTest &state,
		const QString &label,
		const QString &target,
		const QString &reason) {
	const auto path = RecordPath(state, label);
	const auto bytes = ReadBytes(path);
	const auto server = Server(kOriginal);
	auto args = FakeArgs(
		label,
		server,
		std::make_shared<FakeClock>(),
		state.dir,
		nullptr);
	args.target = target;
	const auto transaction = OpenFixtureTransaction(runner, std::move(args));
	const auto refused = [&] {
		return Ended(*transaction, FixtureOutcome::Refused, reason);
	};
	const auto loaded = refused();
	transaction->transition(kRequested);
	const auto transitioned = refused();
	transaction->restore();
	const auto restored = refused();
	const auto unchanged = (ReadBytes(path) == bytes);
	return {
		.ok = loaded
			&& transitioned
			&& restored
			&& server->requests.empty()
			&& unchanged,
		.text = u"loaded=%1 transition=%2 restore=%3 requests=%4 "
			"recordExists=%5 recordUnchanged=%6 "_q.arg(
				FlagText(loaded),
				FlagText(transitioned),
				FlagText(restored),
				QString::number(server->requests.size()),
				FlagText(!bytes.isEmpty()),
				FlagText(unchanged))
			+ ResultReading(*transaction),
	};
}

void SelfIdentity(not_null<Runner*> runner, SelfTest &state) {
	const auto foreignExists = QFile::exists(
		RecordPath(state, u"ft-resume"_q));
	const auto foreign = RefusedEverywhere(
		runner,
		state,
		u"ft-resume"_q,
		kOtherTarget,
		u"foreign-record"_q);
	Check(
		foreignExists && foreign.ok,
		u"fixture transaction self-test: a record bound to another target "
		"refuses every operation and stays byte for byte"_q,
		foreign.text);

	state.labels.push_back(u"ft-malformed"_q);
	const auto written = WriteFile(
		RecordPath(state, u"ft-malformed"_q),
		"{ not a fixture obligation"_q);
	const auto malformed = RefusedEverywhere(
		runner,
		state,
		u"ft-malformed"_q,
		kTarget,
		u"malformed-record"_q);
	Check(
		written && malformed.ok,
		u"fixture transaction self-test: a malformed record refuses every "
		"operation and stays byte for byte"_q,
		malformed.text);

	const auto invalid = RefusedEverywhere(
		runner,
		state,
		u"bad/label"_q,
		kTarget,
		u"invalid-label"_q);
	const auto created = QFile::exists(RecordPath(state, u"bad/label"_q));
	Check(
		invalid.ok && !created,
		u"fixture transaction self-test: a label that is not a file-safe "
		"name refuses every operation"_q,
		invalid.text + u" created="_q + FlagText(created));
}

void SelfFinish(
		not_null<Runner*> runner,
		const std::shared_ptr<SelfTest> &state) {
	state->finish = OpenLeg(
		runner,
		*state,
		u"ft-finish"_q,
		Server(kOriginal));
	const auto &leg = state->finish;
	const auto applied = TransitionWritten(leg);
	leg.server->writeErrors.push_back(u"FLOOD_WAIT_13"_q);
	leg.transaction->restore();
	const auto waiting = leg.transaction->busy()
		&& !leg.clock->scheduled.empty();
	Check(
		applied && waiting,
		u"fixture transaction self-test: a restore is left waiting on its "
		"deadline for the runner's finish"_q,
		ResultReading(*leg.transaction)
			+ ' '
			+ ScheduledText(*leg.clock, leg.clock->now));

	// Registered after the transaction's own release, so it runs second.
	runner->onFinish([=] {
		const auto &owned = state->finish;
		const auto before = RequestCount(owned);
		const auto deadline = owned.clock->now + kHour;
		owned.clock->fireEarly();
		owned.clock->advance(deadline);
		const auto after = RequestCount(owned) - before;
		const auto record = ReadRecordFile(owned.path);
		Check(
			Ended(
				*owned.transaction,
				FixtureOutcome::Unresolved,
				u"released"_q)
				&& !after
				&& RecordPending(record)
				&& (RecordOriginal(record) == kOriginal),
			u"fixture transaction self-test: the runner's finish released "
			"the waiting restore, its callbacks send nothing and the record "
			"stays pending"_q,
			ResultReading(*owned.transaction)
				+ u" requestsAfterFinish="_q
				+ QString::number(after)
				+ ' '
				+ RecordText(record, deadline));
	});
}

void SelfSecrecy(const SelfTest &state) {
	const auto log = ReadBytes(SelfTestLogPath(), state.logOffset);
	const auto original = kOriginal.toBase64();
	const auto target = kTarget.toUtf8();
	auto personIds = 0;
	for (const auto &id : kPersonIds) {
		personIds += Occurrences(log, id);
	}
	const auto targets = Occurrences(log, target);
	const auto states = Occurrences(log, kStateText);
	const auto originals = Occurrences(log, original);
	Check(
		!log.isEmpty() && !personIds && !targets && !states && !originals,
		u"fixture transaction self-test: the test log carries no person id, "
		"target, state or original"_q,
		u"scannedBytes=%1 personIds=%2 target=%3 stateText=%4 "
		"originalBase64=%5"_q.arg(
			QString::number(log.size()),
			QString::number(personIds),
			QString::number(targets),
			QString::number(states),
			QString::number(originals)));

	auto records = QByteArray();
	const auto files = QDir(state.dir).entryList(
		{ u"*.json"_q },
		QDir::Files);
	for (const auto &name : files) {
		records += ReadBytes(state.dir + name);
	}
	const auto recordTargets = Occurrences(records, target);
	const auto recordOriginals = Occurrences(records, original);
	Check(
		(recordTargets > 0) && (recordOriginals > 0),
		u"fixture transaction self-test: the same scan finds the target and "
		"the original in the self-test's record files"_q,
		u"recordFiles=%1 target=%2 originalBase64=%3"_q.arg(
			QString::number(files.size()),
			QString::number(recordTargets),
			QString::number(recordOriginals)));

	auto live = 0;
	for (const auto &label : state.labels) {
		if (QFile::exists(ObligationDir() + label + u".json"_q)) {
			++live;
		}
	}
	Check(
		!live,
		u"fixture transaction self-test: no self-test record exists in the "
		"live copy's obligation folder"_q,
		u"labels=%1 liveRecords=%2"_q.arg(
			QString::number(state.labels.size()),
			QString::number(live)));
}

} // namespace

void AppendFixtureTransactionSelfTest(not_null<Runner*> runner) {
	const auto state = std::make_shared<SelfTest>();
	const auto leg = [&](const QString &name, Fn<void()> then) {
		runner->add({
			.name = u"fixture transaction self-test: "_q + name,
			.then = std::move(then),
		});
	};
	leg(u"arm"_q, [=] { SelfArm(*state); });
	leg(u"equality"_q, [=] { SelfEqual(runner, *state); });
	leg(u"necessary write"_q, [=] { SelfWrite(runner, *state); });
	leg(u"not-before deadline"_q, [=] {
		SelfNotBefore(runner, *state);
		SelfReadNotBefore(runner, *state);
	});
	runner->add({
		.name = u"fixture transaction self-test: default wall-clock timer"_q,
		.run = [=] { SelfRealClockStart(runner, *state); },
		.until = [=] {
			const auto &transaction = state->realClock.transaction;
			return !transaction || !transaction->busy();
		},
		.then = [=] { SelfRealClockCheck(*state); },
		.timeout = kRealClockTimeout,
		.timeoutDetails = [=] {
			const auto &transaction = state->realClock.transaction;
			return transaction ? ResultReading(*transaction) : QString();
		},
	});
	leg(u"equal read at the deadline"_q, [=] {
		SelfEqualAtDeadline(runner, *state);
	});
	leg(u"bounded refusal"_q, [=] {
		SelfExhausted(runner, *state);
		SelfZeroWait(runner, *state);
		SelfBeyondBound(runner, *state);
		SelfOtherErrors(runner, *state);
	});
	leg(u"teardown"_q, [=] {
		SelfRelease(runner, *state);
		SelfHeldAnswer(runner, *state);
		SelfDestroyedOwner(runner, *state);
	});
	leg(u"later invocation"_q, [=] { SelfResume(runner, *state); });
	leg(u"exact and unequal originals"_q, [=] {
		SelfOriginals(runner, *state);
	});
	leg(u"authorization and identity"_q, [=] {
		SelfAuthorization(runner, *state);
		SelfIdentity(runner, *state);
	});
	leg(u"finish ownership"_q, [=] { SelfFinish(runner, state); });
	leg(u"log secrecy"_q, [=] { SelfSecrecy(*state); });
}

} // namespace Test

#endif // _DEBUG
