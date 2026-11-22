/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_remote.h"

#include "base/timer.h"
#include "core/version.h"
#include "platform/platform_widget_helper.h"
#include "test/test_widget_helper_probe.h"
#include "tgw/tgw_hosted_wire.h"
#include "twidget/remote_stream.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <variant>

namespace Tgw {
namespace {

namespace Remote = twlite::remote;

constexpr auto kMaxFailures = 3;
constexpr auto kFailureRunAge = crl::time(60000);
constexpr auto kIdleTimeout = crl::time(30000);
constexpr auto kExitWait = crl::time(1000);
constexpr auto kKillWait = crl::time(1000);
constexpr auto kMaxReasonBytes = size_t(4096);
constexpr auto kRemoteHeaderBytes = std::size_t(2);
constexpr auto kSequenceBytes = std::size_t(8);
constexpr auto kWidthBytes = std::size_t(8);
constexpr auto kHeightBytes = std::size_t(4);
constexpr auto kTickBytes = std::size_t(8);
constexpr auto kLengthBytes = std::size_t(4);
constexpr auto kFormHeaderBytes = std::size_t(2);
constexpr auto kCountBytes = std::size_t(4);
constexpr auto kCommandBytes = std::size_t(5);
constexpr auto kRectBytes = std::size_t(16);

using HostedRoute = std::shared_ptr<twidget::SandboxHostedChannel>;
using HostedFactory = Fn<HostedRoute(
	std::shared_ptr<twidget::SandboxResources>)>;
using Completion = Platform::WidgetHelper::Completion;
using Answers = twidget::SandboxHostedAnswers;
using Clock = std::chrono::steady_clock;

struct Choice {
	EnginePlace place = EnginePlace::Nowhere;
	QString reason;
};

struct Key {
	std::optional<std::uint64_t> sequence;
};

struct Exchange {
	using Reader = std::optional<Key>(*)(
		std::string_view message,
		const Remote::Caps &caps);

	Remote::Kind request = Remote::Kind();
	Remote::Kind answer = Remote::Kind();
	Remote::Kind alsoAnswer = Remote::Kind();
	Reader answered = nullptr;
	std::size_t limit = HostedWire::kMaxMessageBytes;
	bool sequenced = false;
	bool whenVisible = false;
};

[[nodiscard]] std::optional<std::uint64_t> SequenceOf(
		std::string_view message) {
	if (message.size() < kRemoteHeaderBytes + kSequenceBytes) {
		return std::nullopt;
	}
	message.remove_prefix(kRemoteHeaderBytes);
	return HostedWire::details::TakeNumber(message, kSequenceBytes);
}

[[nodiscard]] bool Skip(std::string_view &body, std::size_t bytes) {
	if (body.size() < bytes) {
		return false;
	}
	body.remove_prefix(bytes);
	return true;
}

[[nodiscard]] std::optional<bool> TakeFlag(std::string_view &body) {
	if (body.empty() || (std::uint8_t(body.front()) > 1)) {
		return std::nullopt;
	}
	const auto result = (body.front() != 0);
	body.remove_prefix(1);
	return result;
}

[[nodiscard]] bool SkipFlag(std::string_view &body) {
	return TakeFlag(body).has_value();
}

[[nodiscard]] std::optional<std::uint64_t> TakeCount(
		std::string_view &body) {
	if (body.size() < kCountBytes) {
		return std::nullopt;
	}
	return HostedWire::details::TakeNumber(body, kCountBytes);
}

[[nodiscard]] std::optional<std::string_view> TakeValue(
		std::string_view &body,
		std::size_t limit) {
	if (body.size() < kLengthBytes) {
		return std::nullopt;
	}
	const auto length = HostedWire::details::TakeNumber(body, kLengthBytes);
	if ((length > limit) || (length > body.size())) {
		return std::nullopt;
	}
	const auto result = body.substr(0, std::size_t(length));
	body.remove_prefix(std::size_t(length));
	return result;
}

[[nodiscard]] bool SkipForm(
		std::string_view &value,
		twlite::wire::Form form) {
	return (value.size() >= kFormHeaderBytes)
		&& (std::uint8_t(value[0]) == std::uint8_t(form))
		&& (std::uint8_t(value[1]) == twlite::wire::VERSION)
		&& Skip(value, kFormHeaderBytes);
}

[[nodiscard]] bool ListAdmitted(
		std::string_view value,
		const twlite::wire::Caps &caps) {
	const auto count = SkipForm(value, twlite::wire::Form::List)
		? TakeCount(value)
		: std::nullopt;
	return count
		&& (*count <= caps.commands)
		&& (*count <= value.size() / kCommandBytes)
		&& (*count || value.empty());
}

[[nodiscard]] bool ListLengthAdmitted(std::uint32_t length) {
	const auto empty = kFormHeaderBytes + kCountBytes;
	return (length == empty) || (length >= empty + kCommandBytes);
}

[[nodiscard]] bool DamageComplete(
		std::string_view value,
		const twlite::wire::Caps &caps) {
	const auto rects = SkipForm(value, twlite::wire::Form::Damage)
		? TakeFlag(value)
		: std::nullopt;
	if (!rects) {
		return false;
	} else if (!*rects) {
		return value.empty();
	}
	const auto count = TakeCount(value);
	return count
		&& (*count <= caps.rects)
		&& (value.size() == *count * kRectBytes);
}

[[nodiscard]] bool SkipState(std::string_view &body) {
	return Skip(body, kWidthBytes + kHeightBytes)
		&& SkipFlag(body)
		&& SkipFlag(body)
		&& Skip(body, kTickBytes)
		&& SkipFlag(body);
}

[[nodiscard]] bool SkipClaim(std::string_view &body) {
	return SkipFlag(body) && SkipFlag(body) && SkipFlag(body);
}

[[nodiscard]] bool FrameComplete(
		std::string_view body,
		const Remote::Caps &caps) {
	const auto painted = (Skip(body, kSequenceBytes) && SkipState(body))
		? TakeFlag(body)
		: std::nullopt;
	if (!painted) {
		return false;
	} else if (!*painted) {
		return body.empty();
	}
	const auto list = TakeValue(body, caps.message);
	const auto damage = list ? TakeValue(body, caps.message) : std::nullopt;
	return damage
		&& body.empty()
		&& ListAdmitted(*list, caps.values)
		&& DamageComplete(*damage, caps.values);
}

[[nodiscard]] bool PointerComplete(
		std::string_view body,
		const Remote::Caps &caps) {
	const auto claimed = TakeValue(body, caps.text)
		? TakeFlag(body)
		: std::nullopt;
	return claimed.has_value()
		&& (!*claimed || SkipClaim(body))
		&& body.empty();
}

[[nodiscard]] std::optional<Key> DeltaKey(
		std::string_view message,
		const Remote::Caps &caps) {
	auto sequence = std::uint64_t();
	auto length = std::uint32_t();
	auto damage = std::string_view();
	if (!Remote::delta_scan(message, caps, sequence, length, damage)
		|| !ListLengthAdmitted(length)
		|| !DamageComplete(damage, caps.values)) {
		return std::nullopt;
	}
	return Key{ .sequence = sequence };
}

[[nodiscard]] std::optional<Key> FrameKey(
		std::string_view message,
		const Remote::Caps &caps) {
	if (Remote::kind_of(message) == Remote::Kind::FrameDelta) {
		return DeltaKey(message, caps);
	}
	const auto sequence = SequenceOf(message);
	if (!sequence
		|| !FrameComplete(message.substr(kRemoteHeaderBytes), caps)) {
		return std::nullopt;
	}
	return Key{ .sequence = sequence };
}

[[nodiscard]] std::optional<Key> PointerKey(
		std::string_view message,
		const Remote::Caps &caps) {
	if (!PointerComplete(message.substr(kRemoteHeaderBytes), caps)) {
		return std::nullopt;
	}
	return Key();
}

[[nodiscard]] std::optional<Key> ClaimKey(
		std::string_view message,
		const Remote::Caps &caps) {
	auto claim = Remote::ClaimAnswer();
	if (!Remote::decode(message, caps, claim)) {
		return std::nullopt;
	}
	return Key{ .sequence = claim.seq };
}

constexpr auto kExchanges = std::array{
	Exchange{
		.request = Remote::Kind::FrameRequest,
		.answer = Remote::Kind::Frame,
		.alsoAnswer = Remote::Kind::FrameDelta,
		.answered = FrameKey,
		.limit = HostedWire::kMaxFrameBytes,
		.sequenced = true,
		.whenVisible = true,
	},
	Exchange{
		.request = Remote::Kind::Touch,
		.answer = Remote::Kind::Pointer,
		.answered = PointerKey,
	},
	Exchange{
		.request = Remote::Kind::Hover,
		.answer = Remote::Kind::Pointer,
		.answered = PointerKey,
	},
	Exchange{
		.request = Remote::Kind::ClaimQuery,
		.answer = Remote::Kind::ClaimAnswer,
		.answered = ClaimKey,
		.sequenced = true,
	},
};

struct Reading {
	std::string reason;
	crl::time countedAt = 0;
	size_t open = 0;
	bool ended = false;
};

struct Reports {
	std::mutex mutex;
	Reading reading;
};

struct Helper {
	std::shared_ptr<twidget::SandboxRemoteHostEnd> end;
	std::shared_ptr<twidget::SandboxRemoteStream> stream;
	std::shared_ptr<Reports> reports;
	FnMut<Completion(crl::time exitWait, crl::time killWait)> complete;
	crl::time started = 0;
	Clock::time_point launched;
	crl::time handed = 0;
	int64 pid = 0;
	uint64 id = 0;
	bool retiring = false;
	bool reaping = false;
	bool intentional = false;
	bool launching = false;
};

class Manager final {
public:
	Manager();

	[[nodiscard]] HostedRoute hostedChannel(
		std::shared_ptr<twidget::SandboxResources> resources);
	void refuse();
	[[nodiscard]] bool stop(Fn<void()> stopped);

	void reconcile(uint64 id);
	void ended(uint64 id);

private:
	using Lock = std::unique_lock<std::mutex>;
	using End = twidget::SandboxRemoteHostEnd;
	using Selected = std::variant<QString, not_null<End*>>;

	[[nodiscard]] Selected endFor(
		Lock &lock,
		const twidget::SandboxResources &resources);
	[[nodiscard]] Selected launch(
		Lock &lock,
		const twidget::SandboxResources &resources);
	void handOut(Lock &lock);
	[[nodiscard]] uint64 retireLocked(bool intentional);
	[[nodiscard]] crl::time idleDeadlineLocked() const;
	void failedLocked(crl::time uptime);
	void scheduleReap(uint64 id);
	[[nodiscard]] bool claimReapLocked(uint64 id);
	void reap(uint64 id);
	void finished();
	void checkIdle();
	void armIdle(crl::time deadline);

	std::mutex _mutex;
	std::condition_variable _reaped;
	std::optional<Helper> _helper;
	QString _disabled;
	uint64 _serial = 0;
	int _failures = 0;
	bool _stopped = false;
	bool _refusing = false;
	base::Timer _idle;
	Fn<void()> _whenStopped;

};

auto Instance = (Manager*)nullptr;
auto Place = std::optional<EnginePlace>();
auto HostedRoutes = HostedFactory();

[[nodiscard]] std::string BuildString() {
	return std::to_string(AppVersion);
}

[[nodiscard]] QString ReasonText(const std::string &reason) {
	const auto size = std::min(reason.size(), kMaxReasonBytes);
	return QString::fromUtf8(reason.data(), int(size));
}

[[nodiscard]] std::optional<Key> RequestKey(
		const Exchange &exchange,
		std::string_view message) {
	if (!exchange.sequenced) {
		return Key();
	}
	const auto sequence = SequenceOf(message);
	if (!sequence) {
		return std::nullopt;
	}
	return Key{ .sequence = sequence };
}

[[nodiscard]] std::optional<Key> AnswerKey(
		const Exchange &exchange,
		std::string_view message) {
	const auto caps = HostedWire::EmbeddingCaps();
	if (message.size() > std::min(caps.message, exchange.limit)) {
		return std::nullopt;
	}
	return exchange.answered(message, caps);
}

[[nodiscard]] Answers::Reading ReadExchange(std::string_view message) {
	using Role = Answers::Role;
	if (HostedWire::IsControl(message)) {
		return HostedWire::DecodeStopped(message)
			? Answers::Reading{ .role = Role::Last }
			: Answers::Reading();
	}
	const auto kind = Remote::kind_of(message);
	for (const auto &exchange : kExchanges) {
		const auto expects = (kind == exchange.request);
		if (!expects
			&& (kind != exchange.answer)
			&& (kind != exchange.alsoAnswer)) {
			continue;
		}
		const auto key = expects
			? RequestKey(exchange, message)
			: AnswerKey(exchange, message);
		if (!key) {
			break;
		}
		return {
			.role = expects ? Role::Expects : Role::Answers,
			.family = std::uint32_t(exchange.answer),
			.sequence = key->sequence,
			.whenVisible = expects && exchange.whenVisible,
		};
	}
	return {};
}

[[nodiscard]] Reading Read(Reports &reports) {
	auto lock = std::lock_guard(reports.mutex);
	return reports.reading;
}

void PublishCount(Reports &reports, size_t open) {
	auto lock = std::lock_guard(reports.mutex);
	reports.reading.open = open;
	reports.reading.countedAt = crl::now();
}

void PublishEnd(Reports &reports, const std::string &reason) {
	auto lock = std::lock_guard(reports.mutex);
	reports.reading.reason = reason;
	reports.reading.ended = true;
}

[[nodiscard]] twidget::SandboxRemoteHostEnd::Events EventsFor(
		std::shared_ptr<Reports> reports,
		uint64 id) {
	return {
		.runtimes = [=](size_t open) {
			PublishCount(*reports, open);
			crl::on_main([=] {
				Instance->reconcile(id);
			});
		},
		.ended = [=](const std::string &reason) {
			PublishEnd(*reports, reason);
			crl::on_main([=] {
				Instance->ended(id);
			});
		},
	};
}

[[nodiscard]] HostedRoute RefusingHosted(const QString &reason) {
	return twidget::SandboxRemoteHostEnd::refusingHosted(
		reason.toStdString());
}

[[nodiscard]] QString CouldNotStart(const QString &error) {
	LOG(("Tgw Error: The widget helper could not start: %1.").arg(error));
	return u"the widget helper could not start: "_q + error;
}

#ifdef DESKTOP_APP_DISABLE_PRIVATE_QT_API
[[nodiscard]] bool HelperAvailable() {
	return false;
}

[[nodiscard]] QString UnsupportedReason() {
	return u"this build cannot bound the clips of widget pictures "
		"without Qt's private API"_q;
}
#else // DESKTOP_APP_DISABLE_PRIVATE_QT_API
[[nodiscard]] bool HelperAvailable() {
	return Platform::WidgetHelper::Enabled();
}

[[nodiscard]] QString UnsupportedReason() {
	return Platform::WidgetHelper::UnsupportedReason();
}
#endif // DESKTOP_APP_DISABLE_PRIVATE_QT_API

[[nodiscard]] QString Unavailable() {
	const auto reason = UnsupportedReason();
	const auto text = u"no sandboxed widget helper is available here"_q;
	return reason.isEmpty() ? text : (text + u": "_q + reason);
}

[[nodiscard]] Choice Logged(Choice choice, const QString &value) {
	const auto from = value.isEmpty()
		? QString()
		: u" (TDESKTOP_WIDGET_ENGINES=%1)"_q.arg(value);
	switch (choice.place) {
	case EnginePlace::Helper:
		LOG(("Tgw: Widget engines, validators and renderers run "
			"in the widget helper%1.").arg(from));
		break;
	case EnginePlace::Nowhere:
		LOG(("Tgw: Widget engines, validators and renderers do not run, "
			"%1.").arg(choice.reason));
		break;
	}
	return choice;
}

[[nodiscard]] Choice DefaultPlace() {
	const auto place = HelperAvailable()
		? EnginePlace::Helper
		: EnginePlace::Nowhere;
	return Logged({
		.place = place,
		.reason = ((place == EnginePlace::Nowhere)
			? Unavailable()
			: QString()),
	}, QString());
}

// Engine switches and probes bypass or inspect the sandbox: never shipped.
#if defined _DEBUG && !defined DESKTOP_APP_DISABLE_PRIVATE_QT_API
[[nodiscard]] QString EnginesSwitch() {
	return qEnvironmentVariable("TDESKTOP_WIDGET_ENGINES");
}

[[nodiscard]] Choice ChoosePlace() {
	const auto value = EnginesSwitch();
	if (value == u"helper"_q || Test::IsWidgetHelperProbeMode(value)) {
		return Logged({ .place = EnginePlace::Helper }, value);
	}
	return DefaultPlace();
}

[[nodiscard]] QStringList HelperArguments() {
	return Test::WidgetHelperProbeArguments(EnginesSwitch());
}
#else // _DEBUG && !DESKTOP_APP_DISABLE_PRIVATE_QT_API
[[nodiscard]] Choice ChoosePlace() {
	return DefaultPlace();
}

[[nodiscard]] QStringList HelperArguments() {
	return {};
}
#endif // _DEBUG && !DESKTOP_APP_DISABLE_PRIVATE_QT_API

Manager::Manager()
: _idle([=] { checkIdle(); }) {
}

HostedRoute Manager::hostedChannel(
		std::shared_ptr<twidget::SandboxResources> resources) {
	Expects(resources != nullptr);

	auto lock = Lock(_mutex);
	return v::match(endFor(lock, *resources), [&](const QString &reason) {
		return RefusingHosted(reason);
	}, [&](not_null<End*> end) {
		auto result = end->hostedChannel();
		handOut(lock);
		return result;
	});
}

void Manager::refuse() {
	auto lock = Lock(_mutex);
	_refusing = true;
	_reaped.notify_all();
}

bool Manager::stop(Fn<void()> stopped) {
	_idle.cancel();
	auto lock = Lock(_mutex);
	_stopped = true;
	_reaped.notify_all();
	if (!_helper || _whenStopped) {
		return false;
	}
	const auto retired = (_helper->retiring || _helper->launching)
		? 0
		: retireLocked(true);
	lock.unlock();

	_whenStopped = std::move(stopped);
	if (retired) {
		scheduleReap(retired);
	}
	return true;
}

void Manager::reconcile(uint64 id) {
	auto lock = Lock(_mutex);
	if (!_helper || (_helper->id != id)) {
		return;
	}
	const auto deadline = idleDeadlineLocked();
	lock.unlock();

	armIdle(deadline);
}

void Manager::ended(uint64 id) {
	auto lock = Lock(_mutex);
	if (!_helper || _helper->retiring || (_helper->id != id)) {
		return;
	}
	const auto retired = retireLocked(false);
	lock.unlock();
	scheduleReap(retired);
}

Manager::Selected Manager::endFor(
		Lock &lock,
		const twidget::SandboxResources &resources) {
	while (true) {
		if (_stopped || _refusing) {
			return u"the widget helper was stopped"_q;
		} else if (!_disabled.isEmpty()) {
			return _disabled;
		} else if (!_helper) {
			return launch(lock, resources);
		} else if (claimReapLocked(_helper->id)) {
			const auto id = _helper->id;
			lock.unlock();
			reap(id);
			lock.lock();
		} else if (_helper->retiring || _helper->launching) {
			_reaped.wait(lock);
		} else if (!Read(*_helper->reports).ended) {
			return not_null(_helper->end.get());
		} else {
			const auto retired = retireLocked(false);
			lock.unlock();
			scheduleReap(retired);
			lock.lock();
		}
	}
}

Manager::Selected Manager::launch(
		Lock &lock,
		const twidget::SandboxResources &resources) {
	const auto id = ++_serial;
	auto reports = std::make_shared<Reports>();
	auto events = EventsFor(reports, id);
	const auto arguments = HelperArguments();
	_helper = Helper{
		.reports = std::move(reports),
		.id = id,
		.launching = true,
	};
	lock.unlock();
	auto launched = Platform::WidgetHelper::Launched();
	try {
		launched = Platform::WidgetHelper::Launch(arguments);
	} catch (const std::exception &e) {
		launched.error = QString::fromUtf8(e.what());
	}
	lock.lock();
	if (!launched.error.isEmpty()) {
		const auto reason = CouldNotStart(launched.error);
		if (launched.complete) {
			_helper->complete = std::move(launched.complete);
			_helper->pid = launched.pid;
			_helper->launching = false;
			const auto retired = retireLocked(false);
			lock.unlock();
			scheduleReap(retired);
			return reason;
		}
		_helper = std::nullopt;
		_reaped.notify_all();
		failedLocked(0);
		if (_stopped) {
			crl::on_main([=] {
				finished();
			});
		}
		return reason;
	}
	_helper->stream = std::move(launched.stream);
	_helper->complete = std::move(launched.complete);
	_helper->started = crl::now();
	_helper->launched = Clock::now();
	_helper->pid = launched.pid;
	_helper->launching = false;
	_reaped.notify_all();
	if (_stopped) {
		const auto retired = retireLocked(true);
		lock.unlock();
		scheduleReap(retired);
		return u"the widget helper was stopped"_q;
	}
	try {
		_helper->end = End::startHosting(
			_helper->stream,
			resources,
			BuildString(),
			std::move(events),
			{},
			HostedAnswers());
	} catch (const std::exception &e) {
		const auto reason = CouldNotStart(QString::fromUtf8(e.what()));
		const auto retired = retireLocked(false);
		lock.unlock();
		scheduleReap(retired);
		return reason;
	}
	_helper->stream = nullptr;
	LOG(("Tgw: Widget helper %1 started.").arg(_helper->pid));
	return not_null(_helper->end.get());
}

void Manager::handOut(Lock &lock) {
	_helper->handed = crl::now();
	const auto id = _helper->id;
	lock.unlock();
	crl::on_main([=] {
		reconcile(id);
	});
}

uint64 Manager::retireLocked(bool intentional) {
	Expects(_helper && !_helper->retiring);

	_helper->retiring = true;
	_helper->intentional = intentional;
	if (!intentional && !_helper->end) {
		failedLocked(_helper->started
			? (crl::now() - _helper->started)
			: crl::time(0));
	}
	_reaped.notify_all();
	return _helper->id;
}

crl::time Manager::idleDeadlineLocked() const {
	if (_stopped
		|| _refusing
		|| !_helper
		|| _helper->retiring
		|| _helper->launching) {
		return 0;
	}
	const auto reading = Read(*_helper->reports);
	return (reading.ended || reading.open > 0)
		? 0
		: (std::max(reading.countedAt, _helper->handed) + kIdleTimeout);
}

void Manager::failedLocked(crl::time uptime) {
	_failures = (uptime >= kFailureRunAge) ? 1 : (_failures + 1);
	if (_failures < kMaxFailures || !_disabled.isEmpty()) {
		return;
	}
	_disabled = u"the widget helper failed %1 times in a row"_q.arg(_failures);
	LOG(("Tgw Error: The widget helper failed %1 times in a row, "
		"it is not started again.").arg(_failures));
}

void Manager::scheduleReap(uint64 id) {
	crl::async([=] {
		auto lock = Lock(_mutex);
		if (claimReapLocked(id)) {
			lock.unlock();
			reap(id);
		}
	});
}

bool Manager::claimReapLocked(uint64 id) {
	if (!_helper
		|| (_helper->id != id)
		|| !_helper->retiring
		|| _helper->reaping) {
		return false;
	}
	_helper->reaping = true;
	return true;
}

void Manager::reap(uint64 id) {
	auto lock = Lock(_mutex);
	Assert(_helper
		&& _helper->retiring
		&& _helper->reaping
		&& (_helper->id == id));
	const auto end = base::take(_helper->end);
	const auto stream = base::take(_helper->stream);
	auto complete = base::take(_helper->complete);
	const auto reports = _helper->reports;
	const auto pid = _helper->pid;
	lock.unlock();

	auto endedAt = std::optional<Clock::time_point>();
	if (end) {
		endedAt = end->stop();
		LOG(("Tgw: Widget helper %1 stream ended: %2."
			).arg(pid
			).arg(ReasonText(Read(*reports).reason)));
	} else if (stream) {
		stream->shutdown();
	}
	const auto result = complete(kExitWait, kKillWait);
	LOG(("Tgw: Widget helper %1 ended, %2.").arg(pid).arg(result.detail));

	lock.lock();
	const auto record = base::take(_helper);
	if (endedAt) {
		failedLocked(std::chrono::duration_cast<std::chrono::milliseconds>(
			*endedAt - record->launched).count());
	}
	if (!result.reaped && _disabled.isEmpty()) {
		_disabled = u"the widget helper was not reaped"_q;
	} else if (result.reaped && record->intentional && !endedAt) {
		_failures = 0;
	}
	_reaped.notify_all();
	lock.unlock();

	if (!result.reaped) {
		LOG(("Tgw Error: The widget helper %1 was not reaped, "
			"it is not started again.").arg(pid));
	}
	crl::on_main([=] {
		finished();
	});
}

void Manager::finished() {
	auto lock = Lock(_mutex);
	if (_helper) {
		return;
	}
	lock.unlock();

	if (const auto stopped = base::take(_whenStopped)) {
		stopped();
	}
}

void Manager::checkIdle() {
	auto lock = Lock(_mutex);
	const auto deadline = idleDeadlineLocked();
	if (!deadline) {
		return;
	} else if (crl::now() < deadline) {
		lock.unlock();
		armIdle(deadline);
		return;
	}
	const auto retired = retireLocked(true);
	lock.unlock();

	scheduleReap(retired);
}

void Manager::armIdle(crl::time deadline) {
	if (!deadline) {
		_idle.cancel();
	} else {
		_idle.callOnce(std::max(deadline - crl::now(), crl::time(0)));
	}
}

} // namespace

void PrepareRemoteEngines() {
	if (Place) {
		return;
	}
	const auto choice = ChoosePlace();
	Place = choice.place;
	if (choice.place == EnginePlace::Helper) {
		Instance = new Manager();
		HostedRoutes = [manager = Instance](
				std::shared_ptr<twidget::SandboxResources> resources) {
			return manager->hostedChannel(std::move(resources));
		};
	} else {
		HostedRoutes = [reason = choice.reason](
				std::shared_ptr<twidget::SandboxResources>) {
			return RefusingHosted(reason);
		};
	}
}

EnginePlace ChosenEnginePlace() {
	Expects(Place.has_value());

	return *Place;
}

HostedRoute HostedChannel(
		std::shared_ptr<twidget::SandboxResources> resources) {
	Expects(HostedRoutes != nullptr);

	return HostedRoutes(std::move(resources));
}

Answers HostedAnswers() {
	return { .read = ReadExchange };
}

void RefuseRemoteEngines() {
	if (Instance) {
		Instance->refuse();
	}
}

bool StopRemoteEngines(Fn<void()> stopped) {
	return Instance && Instance->stop(std::move(stopped));
}

} // namespace Tgw
