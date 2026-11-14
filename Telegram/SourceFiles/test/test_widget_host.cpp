/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "test/test_widget_host.h"

#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_agent.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <utility>

namespace Test {
namespace {

constexpr auto kMaxErrors = 20;

constexpr auto kErrorLevel = "error";

using SteadyTime = std::chrono::steady_clock::time_point;

// The test-only private-member accessor of test_chat_show.cpp and
// test_widget_fixture.cpp, kept file-private here: WidgetHost's const
// creation time, the origin of its own loadedMs, read once by value. Only
// reads.
template <typename Tag, typename Tag::type Member>
struct PrivateMember {
	friend typename Tag::type Get(Tag) {
		return Member;
	}
};

struct HostCreatedTag {
	using type = const SteadyTime twidget::WidgetHost::*;
	friend type Get(HostCreatedTag);
};

template struct PrivateMember<
	HostCreatedTag,
	&twidget::WidgetHost::createdAt_>;

// One attached host's record, the only state its onChange callback keeps.
// Merged on the host thread and, once, on main at the attachment; read on
// main. Every field is under the mutex once the record is shared.
struct HostRecord {
	std::mutex mutex;
	WidgetHostSample sample;
	// Console entries already scanned: the host only appends to its logs.
	int logsSeen = 0;
};

} // namespace

// Main thread only: written by the scopes of the watch's registered cores,
// reserved and read by its consumer.
class WidgetWorkLog final {
public:
	std::vector<WidgetWorkEvent> events;
	crl::profile_time reservedAt = 0;
	int64 dropped = 0;
	int capacity = 0;
	// A scope of this log is recording: nested ones stay inert.
	bool open = false;

};

class WidgetHostWatch final {
public:
	explicit WidgetHostWatch(WidgetHostOverride measure);

	// Main thread, each called by its product seam for an armed id.
	[[nodiscard]] twidget::WidgetHostOptions apply(
		twidget::WidgetHostOptions options);
	void attach(twidget::WidgetHost &host, const void *core);

	[[nodiscard]] WidgetHostReading read() const;

	// Main thread.
	void reserveWork(int capacity);
	[[nodiscard]] WidgetWorkReading readWork(crl::profile_time since) const;
	void unregisterWork();

private:
	const WidgetHostOverride _measure;
	const std::shared_ptr<WidgetWorkLog> _work;
	std::vector<WidgetHostApplied> _applied;
	std::vector<std::shared_ptr<HostRecord>> _hosts;

};

namespace {

// Keyed by session too: local message ids are allocated per session.
using ArmedMap = std::map<GlobalMsgId, std::shared_ptr<WidgetHostWatch>>;

[[nodiscard]] ArmedMap &ArmedWatches() {
	static auto result = ArmedMap();
	return result;
}

// A core whose scopes record into |log|, for the attachment |host| of the
// watch that owns it. Main thread only.
struct WorkRegistration {
	const void *core = nullptr;
	std::shared_ptr<WidgetWorkLog> log;
	int host = 0;
};

[[nodiscard]] std::vector<WorkRegistration> &WorkRegistrations() {
	static auto result = std::vector<WorkRegistration>();
	return result;
}

void RegisterWork(
		const void *core,
		const std::shared_ptr<WidgetWorkLog> &log,
		int host) {
	auto &list = WorkRegistrations();
	// A live core owns its address: an older registration names a dead one.
	std::erase_if(list, [&](const WorkRegistration &registration) {
		return (registration.core == core);
	});
	list.push_back({ .core = core, .log = log, .host = host });
}

void UnregisterWork(const WidgetWorkLog *log) {
	std::erase_if(WorkRegistrations(), [&](
			const WorkRegistration &registration) {
		return (registration.log.get() == log);
	});
}

[[nodiscard]] int RegisteredWork(const WidgetWorkLog *log) {
	const auto &list = WorkRegistrations();
	return int(std::count_if(list.begin(), list.end(), [&](
			const WorkRegistration &registration) {
		return (registration.log.get() == log);
	}));
}

[[nodiscard]] WidgetHostWatch *Armed(GlobalMsgId id) {
	const auto &armed = ArmedWatches();
	const auto i = armed.find(id);
	return (i != end(armed)) ? i->second.get() : nullptr;
}

void ApplyOverride(
		twidget::WidgetHostOptions &options,
		const WidgetHostOverride &measure) {
	if (measure.policy) {
		options.policy = *measure.policy;
	}
	if (measure.features) {
		options.features = *measure.features;
	}
	if (measure.memoryLimitBytes) {
		options.memoryLimitBytes = *measure.memoryLimitBytes;
	}
	if (measure.cpuLimitMs) {
		options.cpuLimitMs = *measure.cpuLimitMs;
	}
	if (measure.cpuQuotaMs) {
		options.cpuQuotaMs = *measure.cpuQuotaMs;
	}
	if (measure.cpuQuotaWindowMs) {
		options.cpuQuotaWindowMs = *measure.cpuQuotaWindowMs;
	}
	if (measure.mainThreadQuotaMs) {
		options.mainThreadQuotaMs = *measure.mainThreadQuotaMs;
	}
	if (measure.maxTimers) {
		options.maxTimers = *measure.maxTimers;
	}
}

[[nodiscard]] WidgetHostApplied Applied(
		const twidget::WidgetHostOptions &options) {
	return {
		.policy = options.policy,
		.features = options.features,
		.locale = options.locale,
		.contentSafeArea = options.contentSafeArea,
		.memoryLimitBytes = options.memoryLimitBytes,
		.cpuLimitMs = options.cpuLimitMs,
		.cpuQuotaMs = options.cpuQuotaMs,
		.cpuQuotaWindowMs = options.cpuQuotaWindowMs,
		.cpuKillAfterMs = options.cpuKillAfterMs,
		.mainThreadQuotaMs = options.mainThreadQuotaMs,
		.devicePixelRatio = options.devicePixelRatio,
		.maxTimers = options.maxTimers,
		.visible = options.visible,
	};
}

[[nodiscard]] bool SameStats(
		const twidget::SandboxStats &a,
		const twidget::SandboxStats &b) {
	return (a.jsMs == b.jsMs)
		&& (a.tasks == b.tasks)
		&& (a.memoryBytes == b.memoryBytes)
		&& (a.ops == b.ops)
		&& (a.queries == b.queries)
		&& (a.queryMs == b.queryMs)
		&& (a.queryCacheHits == b.queryCacheHits)
		&& (a.missedMutations == b.missedMutations)
		&& (a.throttledTasks == b.throttledTasks)
		&& (a.cpuWindowMs == b.cpuWindowMs);
}

// |diagnostics| is the copy WidgetHost::changed() made for this onChange
// call, or the attachment's diagnostics() (|delivered| false). Both stamps
// are read before the lock; nothing is called while holding it.
void Merge(
		HostRecord &record,
		const twidget::WidgetDiagnostics &diagnostics,
		SteadyTime createdAt,
		bool delivered) {
	const auto now = crl::profile();
	const auto sinceCreated = std::chrono::duration<double, std::milli>(
		std::chrono::steady_clock::now() - createdAt).count();
	const auto lock = std::lock_guard(record.mutex);
	auto &sample = record.sample;
	if (delivered) {
		++sample.changes;
	}
	if (!SameStats(sample.stats, diagnostics.stats)) {
		sample.stats = diagnostics.stats;
		sample.statsChangedAt = now;
		++sample.statsChanges;
	}
	sample.nodes = diagnostics.nodes;
	if (diagnostics.loaded && !sample.loaded) {
		sample.loaded = true;
		sample.loadedMs = diagnostics.loadedMs;
		sample.loadedAt = now;
	}
	if (diagnostics.dead && !sample.dead) {
		sample.dead = diagnostics.dead;
		sample.diedAtMs = sinceCreated;
		sample.deadAt = now;
	}
	for (const auto &[kind, entry] : diagnostics.diags) {
		sample.diags[kind] = entry.first;
	}
	const auto logs = int(diagnostics.logs.size());
	for (auto i = record.logsSeen; i < logs; ++i) {
		const auto &[level, text] = diagnostics.logs[i];
		if (level != kErrorLevel) {
			continue;
		}
		++sample.errorLogs;
		if (int(sample.errors.size()) < kMaxErrors) {
			sample.errors.push_back(text);
		}
	}
	record.logsSeen = std::max(record.logsSeen, logs);
	sample.logs = record.logsSeen;
}

} // namespace

WidgetHostWatch::WidgetHostWatch(WidgetHostOverride measure)
: _measure(std::move(measure))
, _work(std::make_shared<WidgetWorkLog>()) {
}

twidget::WidgetHostOptions WidgetHostWatch::apply(
		twidget::WidgetHostOptions options) {
	ApplyOverride(options, _measure);
	_applied.push_back(Applied(options));
	return options;
}

void WidgetHostWatch::attach(twidget::WidgetHost &host, const void *core) {
	const auto createdAt = SteadyTime(host.*Get(HostCreatedTag()));
	const auto record = std::make_shared<HostRecord>();
	record->sample.core = reinterpret_cast<quintptr>(core);
	record->sample.attachedAt = crl::profile();
	_hosts.push_back(record);
	if (core) {
		RegisterWork(core, _work, int(_hosts.size()) - 1);
	}
	// WHY: a stale validator bytecode posts a diag from the constructor, so
	// the host thread may already run changed(); there the snapshot and the
	// new callback leave no change between them and race no onChange read.
	host.onHostThread([&] {
		Merge(*record, host.diagnostics(), createdAt, false);
		auto earlier = std::move(host.onChange);
		host.onChange = [record, createdAt, earlier = std::move(earlier)](
				const twidget::WidgetDiagnostics &diagnostics) {
			Merge(*record, diagnostics, createdAt, true);
			if (earlier) {
				earlier(diagnostics);
			}
		};
	});
}

WidgetHostReading WidgetHostWatch::read() const {
	auto result = WidgetHostReading{ .applied = _applied };
	result.hosts.reserve(_hosts.size());
	for (const auto &record : _hosts) {
		const auto lock = std::lock_guard(record->mutex);
		result.hosts.push_back(record->sample);
	}
	return result;
}

void WidgetHostWatch::reserveWork(int capacity) {
	auto &log = *_work;
	log.capacity = std::max(capacity, 0);
	log.events.clear();
	log.events.reserve(log.capacity);
	log.dropped = 0;
	log.reservedAt = crl::profile();
}

WidgetWorkReading WidgetHostWatch::readWork(crl::profile_time since) const {
	const auto &log = *_work;
	const auto before = [&](const WidgetWorkEvent &event) {
		return (event.finished < since);
	};
	const auto from = std::partition_point(
		log.events.begin(),
		log.events.end(),
		before);
	return {
		.events = std::vector<WidgetWorkEvent>(from, log.events.end()),
		.reservedAt = log.reservedAt,
		.capacity = log.capacity,
		.dropped = log.dropped,
		.registered = RegisteredWork(_work.get()),
	};
}

void WidgetHostWatch::unregisterWork() {
	UnregisterWork(_work.get());
}

WidgetWorkScope::WidgetWorkScope(const void *core, WidgetWorkKind kind)
: _kind(kind) {
	for (const auto &registration : WorkRegistrations()) {
		if (registration.core == core) {
			if (!registration.log->open) {
				registration.log->open = true;
				_log = registration.log;
				_host = registration.host;
				_started = crl::profile();
			}
			return;
		}
	}
}

WidgetWorkScope::~WidgetWorkScope() {
	if (!_log) {
		return;
	}
	const auto finished = crl::profile();
	auto &log = *_log;
	log.open = false;
	if (int(log.events.size()) < log.capacity) {
		log.events.push_back({
			.started = _started,
			.finished = finished,
			.displayList = _displayList,
			.host = _host,
			.kind = _kind,
			.replayed = _replayed,
		});
	} else {
		++log.dropped;
	}
}

void WidgetWorkScope::displayList(size_t size) {
	if (_log) {
		_displayList = int64(size);
	}
}

void WidgetWorkScope::replayed(bool image) {
	if (_log) {
		_replayed = image;
	}
}

twidget::WidgetHostOptions OverrideWidgetHostOptions(
		GlobalMsgId id,
		twidget::WidgetHostOptions options) {
	if (!Active()) {
		return options;
	} else if (const auto watch = Armed(id)) {
		return watch->apply(std::move(options));
	}
	return options;
}

void WatchWidgetHost(
		GlobalMsgId id,
		twidget::WidgetHost &host,
		const void *core) {
	if (!Active()) {
		return;
	} else if (const auto watch = Armed(id)) {
		watch->attach(host, core);
	}
}

std::shared_ptr<WidgetHostWatch> ArmWidgetHostWatch(
		GlobalMsgId id,
		WidgetHostOverride measure) {
	auto &watch = ArmedWatches()[id];
	if (!watch) {
		watch = std::make_shared<WidgetHostWatch>(std::move(measure));
	}
	return watch;
}

void DisarmWidgetHostWatch(GlobalMsgId id) {
	auto &armed = ArmedWatches();
	const auto i = armed.find(id);
	if (i == end(armed)) {
		return;
	}
	i->second->unregisterWork();
	armed.erase(i);
}

WidgetHostReading ReadWidgetHost(const WidgetHostWatch &watch) {
	return watch.read();
}

void ReserveWidgetWork(WidgetHostWatch &watch, int capacity) {
	watch.reserveWork(capacity);
}

WidgetWorkReading ReadWidgetWork(
		const WidgetHostWatch &watch,
		crl::profile_time since) {
	return watch.readWork(since);
}

} // namespace Test

#else // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS

namespace Test {

twidget::WidgetHostOptions OverrideWidgetHostOptions(
		GlobalMsgId,
		twidget::WidgetHostOptions options) {
	return options;
}

void WatchWidgetHost(GlobalMsgId, twidget::WidgetHost &, const void *) {
}

} // namespace Test

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
