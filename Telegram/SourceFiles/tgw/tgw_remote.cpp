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
#include "twidget/remote_stream.h"

#include <condition_variable>
#include <mutex>
#include <variant>

namespace Tgw {
namespace {

constexpr auto kMaxFailures = 3;
constexpr auto kFailureRunAge = crl::time(60000);
constexpr auto kIdleTimeout = crl::time(30000);
constexpr auto kExitWait = crl::time(1000);
constexpr auto kKillWait = crl::time(1000);
constexpr auto kMaxReasonBytes = size_t(4096);

using Channel = std::shared_ptr<twidget::SandboxRemoteChannel>;
using HostedRoute = std::shared_ptr<twidget::SandboxHostedChannel>;
using HostedFactory = Fn<HostedRoute(
	std::shared_ptr<twidget::SandboxResources>)>;
using Completion = Platform::WidgetHelper::Completion;

struct Choice {
	EnginePlace place = EnginePlace::Nowhere;
	bool hosted = false;
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
	crl::time handed = 0;
	int64 pid = 0;
	uint64 id = 0;
	bool retiring = false;
	bool intentional = false;
	bool launching = false;
};

class Manager final {
public:
	explicit Manager(bool hosting);

	[[nodiscard]] Channel channel(
		std::shared_ptr<twidget::SandboxResources> resources);
	[[nodiscard]] HostedRoute hostedChannel(
		std::shared_ptr<twidget::SandboxResources> resources);
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
	const bool _hosting = false;
	bool _stopped = false;
	base::Timer _idle;
	Fn<void()> _whenStopped;

};

auto Instance = (Manager*)nullptr;
auto Place = std::optional<EnginePlace>();
auto Hosted = false;
auto HostedRoutes = HostedFactory();

[[nodiscard]] std::string BuildString() {
	return std::to_string(AppVersion);
}

[[nodiscard]] QString ReasonText(const std::string &reason) {
	const auto size = std::min(reason.size(), kMaxReasonBytes);
	return QString::fromUtf8(reason.data(), int(size));
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

[[nodiscard]] Channel Refusing(const QString &reason) {
	return twidget::SandboxRemoteHostEnd::refusing(reason.toStdString());
}

[[nodiscard]] HostedRoute RefusingHosted(const QString &reason) {
	return twidget::SandboxRemoteHostEnd::refusingHosted(
		reason.toStdString());
}

void RefuseEngines(const QString &reason) {
	twidget::SandboxEngine::setRemoteChannelFactory([=](
			std::shared_ptr<twidget::SandboxResources>) {
		return Refusing(reason);
	});
}

[[nodiscard]] QString CouldNotStart(const QString &error) {
	LOG(("Tgw Error: The widget helper could not start: %1.").arg(error));
	return u"the widget helper could not start: "_q + error;
}

[[nodiscard]] QString Unavailable() {
	const auto reason = Platform::WidgetHelper::UnsupportedReason();
	const auto text = u"no sandboxed widget helper is available here"_q;
	return reason.isEmpty() ? text : (text + u": "_q + reason);
}

[[nodiscard]] bool HostsWidgets() {
	return Platform::WidgetHelper::Renders();
}

[[nodiscard]] Choice Logged(Choice choice, const QString &value) {
	const auto what = choice.hosted
		? u"Widget engines, validators and renderers"_q
		: u"Widget engines"_q;
	const auto from = value.isEmpty()
		? QString()
		: u" (TDESKTOP_WIDGET_ENGINES=%1)"_q.arg(value);
	switch (choice.place) {
	case EnginePlace::InProcess:
		LOG(("Tgw: %1 run in process%2.").arg(what, from));
		break;
	case EnginePlace::Helper:
		LOG(("Tgw: %1 run in the widget helper%2.").arg(what, from));
		break;
	case EnginePlace::Nowhere:
		LOG(("Tgw: %1 do not run, %2.").arg(what, Unavailable()));
		break;
	}
	return choice;
}

[[nodiscard]] Choice DefaultPlace() {
	const auto place = Platform::WidgetHelper::Enabled()
		? EnginePlace::Helper
		: EnginePlace::Nowhere;
	return Logged({ .place = place, .hosted = HostsWidgets() }, QString());
}

// Engine switches and probes bypass or inspect the sandbox: never shipped.
#ifdef _DEBUG
[[nodiscard]] QString EnginesSwitch() {
	return qEnvironmentVariable("TDESKTOP_WIDGET_ENGINES");
}

[[nodiscard]] Choice ChoosePlace() {
	const auto value = EnginesSwitch();
	if (value == u"in-process"_q) {
		return Logged({ .place = EnginePlace::InProcess }, value);
	} else if (value == u"helper-engines"_q) {
		return Logged({ .place = EnginePlace::Helper }, value);
	} else if (value == u"helper"_q
		|| Test::IsWidgetHelperProbeMode(value)) {
		return Logged({
			.place = EnginePlace::Helper,
			.hosted = HostsWidgets(),
		}, value);
	}
	return DefaultPlace();
}

[[nodiscard]] QStringList HelperArguments() {
	return Test::WidgetHelperProbeArguments(EnginesSwitch());
}
#else // _DEBUG
[[nodiscard]] Choice ChoosePlace() {
	return DefaultPlace();
}

[[nodiscard]] QStringList HelperArguments() {
	return {};
}
#endif // _DEBUG

Manager::Manager(bool hosting)
: _hosting(hosting)
, _idle([=] { checkIdle(); }) {
}

Channel Manager::channel(
		std::shared_ptr<twidget::SandboxResources> resources) {
	Expects(resources != nullptr);

	auto lock = Lock(_mutex);
	return v::match(endFor(lock, *resources), [&](const QString &reason) {
		return Refusing(reason);
	}, [&](not_null<End*> end) {
		auto result = end->channel();
		handOut(lock);
		return result;
	});
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
		if (_stopped) {
			return u"the widget helper was stopped"_q;
		} else if (!_disabled.isEmpty()) {
			return _disabled;
		} else if (!_helper) {
			return launch(lock, resources);
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
	_helper->pid = launched.pid;
	_helper->launching = false;
	_reaped.notify_all();
	if (_stopped) {
		const auto retired = retireLocked(true);
		lock.unlock();
		scheduleReap(retired);
		return u"the widget helper was stopped"_q;
	}
	const auto start = _hosting ? &End::startHosting : &End::start;
	try {
		_helper->end = start(
			_helper->stream,
			resources,
			BuildString(),
			std::move(events),
			{});
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
	_helper->intentional = intentional && !Read(*_helper->reports).ended;
	if (!_helper->intentional) {
		failedLocked(crl::now() - _helper->started);
	}
	return _helper->id;
}

crl::time Manager::idleDeadlineLocked() const {
	if (_stopped || !_helper || _helper->retiring || _helper->launching) {
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
		reap(id);
	});
}

void Manager::reap(uint64 id) {
	auto lock = Lock(_mutex);
	Assert(_helper && _helper->retiring && (_helper->id == id));
	const auto end = base::take(_helper->end);
	const auto stream = base::take(_helper->stream);
	auto complete = base::take(_helper->complete);
	const auto reports = _helper->reports;
	const auto pid = _helper->pid;
	lock.unlock();

	if (end) {
		end->stop();
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
	if (!result.reaped && _disabled.isEmpty()) {
		_disabled = u"the widget helper was not reaped"_q;
	} else if (result.reaped && record->intentional) {
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
	Hosted = choice.hosted;
	if (choice.place == EnginePlace::InProcess) {
		return;
	} else if (choice.place == EnginePlace::Nowhere) {
		const auto reason = Unavailable();
		RefuseEngines(reason);
		if (choice.hosted) {
			HostedRoutes = [=](std::shared_ptr<twidget::SandboxResources>) {
				return RefusingHosted(reason);
			};
		}
		return;
	}
	Instance = new Manager(choice.hosted);
	if (!choice.hosted) {
		twidget::SandboxEngine::setRemoteChannelFactory([manager = Instance](
				std::shared_ptr<twidget::SandboxResources> resources) {
			return manager->channel(std::move(resources));
		});
		return;
	}
	RefuseEngines(u"widget engines run only beside their renderers "
		"in the widget helper"_q);
	HostedRoutes = [manager = Instance](
			std::shared_ptr<twidget::SandboxResources> resources) {
		return manager->hostedChannel(std::move(resources));
	};
}

EnginePlace ChosenEnginePlace() {
	Expects(Place.has_value());

	return *Place;
}

bool WidgetsHosted() {
	Expects(Place.has_value());

	return Hosted;
}

HostedRoute HostedChannel(
		std::shared_ptr<twidget::SandboxResources> resources) {
	Expects(HostedRoutes != nullptr);

	return HostedRoutes(std::move(resources));
}

bool StopRemoteEngines(Fn<void()> stopped) {
	return Instance && Instance->stop(std::move(stopped));
}

} // namespace Tgw
