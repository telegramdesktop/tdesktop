/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "test/test_widget_ops.h"

#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_agent.h"
#include "test/test_widget_ops_internal.h"
#include "test/test_widget_renderer_forward.h"
#include "tgw/tgw_hosted_wire.h"
#include "tgw/tgw_renderer.h"
#include "tgw/tgw_renderer_input.h"
#include "twidget/json.h"
#include "twidget/sdk_policy.h"
#include "twidget/widget_host.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Test::WidgetOps {
namespace {

namespace HostedWire = Tgw::HostedWire;

// What one hosted host tells its tap, shared by the observers installed on
// it and destroyed with the last of them, in ~WidgetHost after its host
// thread was joined (or right after, by a theme or press seam that held it
// across that destruction). The observer calls run on that thread; the
// seams' observeBuilt() and themeApplied() run on the runtime's per-host
// job worker, themeSelected(), themeScheduled(), retired(), detached() and
// pressDispatched() on main and pressObserved() on the widget render
// thread, and they touch only the tap and, for the theme, the host the
// seam gave.
class HostedObservations final {
public:
	HostedObservations(
		std::shared_ptr<WidgetOpsTap> tap,
		int selection,
		const std::shared_ptr<twidget::WidgetHost> &host,
		uint64 work,
		const twidget::WidgetTheme &built);
	HostedObservations(const HostedObservations &other) = delete;
	HostedObservations &operator=(const HostedObservations &other) = delete;
	~HostedObservations();

	void ops(const std::string &clean) const;
	// True for a well-formed Mirrored record of an armed tap, recorded and
	// so not for the runtime.
	[[nodiscard]] bool consumed(const std::string &message);
	// Any thread the host reports a change on: the first rendered
	// acknowledgment counts, and the first change that carries the host's
	// death keeps it raw in the selection's record, also when the host's
	// owner kept or destroyed it, so that its onDeath stays silent.
	void changed(const twidget::WidgetDiagnostics &diagnostics);
	// At the install, before any observer is set: the theme the host was
	// built with, as the generation's first applied theme.
	void recordBuilt();
	// The runtime's job for |host|, on its worker: the theme it was built
	// with, probed and recorded for that first serial.
	void observeBuilt(twidget::WidgetHost &host) const;
	// The theme the runtime wants the host to have while its theme job
	// runs, on main: pending from now on.
	void themeSelected(const twidget::WidgetTheme &theme) const;
	// A later theme the runtime scheduled for the host, on main: pending,
	// its token returned.
	[[nodiscard]] uint64 themeScheduled(
		const twidget::WidgetTheme &theme) const;
	// That theme applied to |host| by the job of |token|, on its worker:
	// recorded, then probed there.
	void themeApplied(
		twidget::WidgetHost &host,
		const twidget::WidgetTheme &theme,
		uint64 token) const;
	// The runtime retired the host, on main.
	void retired() const;
	// The core of the host's work is being detached, on main.
	void detached() const;
	void pressDispatched(uint64 serial) const;
	void pressObserved(uint64 press, const Tgw::PressStep &step) const;
	void replaceProbe(Fn<std::string()> probe);

private:
	[[nodiscard]] int recordApplied(
		const twidget::WidgetTheme &theme,
		uint64 token = 0) const;
	[[nodiscard]] std::string probe(twidget::WidgetHost &host) const;
	void work(std::string_view text) const;
	void scheme(char way, std::string_view text);

	const std::shared_ptr<WidgetOpsTap> _tap;
	const twidget::WidgetTheme _built;
	const void *const _host = nullptr;
	const uint64 _work = 0;
	const int _selection = 0;
	// Set by recordBuilt() before the host's observers exist, read by them.
	int _builtSerial = 0;
	// Set and reset by the self-check on main while no probe runs.
	Fn<std::string()> _probe;
	std::atomic<bool> _rendered = false;
	std::atomic<bool> _dead = false;
	bool _reported = false;

};

// Shared by every copy of one hosted tap function: released with the last,
// completed then when it was never installed.
class HostedSelection final {
public:
	HostedSelection(
		std::shared_ptr<WidgetOpsTap> tap,
		int index,
		uint64 work,
		const twidget::WidgetTheme &theme);
	HostedSelection(const HostedSelection &other) = delete;
	HostedSelection &operator=(const HostedSelection &other) = delete;
	~HostedSelection();

	void install(const std::shared_ptr<twidget::WidgetHost> &pointer);

private:
	const std::shared_ptr<WidgetOpsTap> _tap;
	const twidget::WidgetTheme _theme;
	const uint64 _work = 0;
	const int _index = 0;
	std::atomic<bool> _installed = false;

};

// The hosted hosts with an installed tap, by host and by work id, for the
// product's theme and press seams, which run on threads of their own.
// Each entry is its observations' while they live. A host is registered
// while it lives, so its address names no other; a work id is never
// reused, so a late construction of a core that is gone cannot take a
// newer core's entry.
struct HostedRegistry {
	struct Entry {
		std::weak_ptr<HostedObservations> observations;
		const HostedObservations *raw = nullptr;
	};
	template <typename Key>
	using Entries = std::map<Key, Entry>;

	std::mutex mutex;
	Entries<const void*> hosts;
	Entries<uint64> works;
};

static_assert(int(WidgetHelperWorkKind::Message)
	== int(HelperWorkKind::Message));
static_assert(int(WidgetHelperWorkKind::Control)
	== int(HelperWorkKind::Control));

// The scheme of |theme| as a WidgetHost keeps it.
[[nodiscard]] std::string HostedScheme(const twidget::WidgetTheme &theme) {
	return (theme.colorScheme == "dark") ? "dark" : "light";
}

// The theme message a host that keeps |theme| gives its renderer: the
// served host's in the helper, built as WidgetHost::themeMessage() builds
// it from the theme the hosted start and hostedTheme() carry there.
[[nodiscard]] std::string HostedThemeMessage(
		const twidget::WidgetTheme &theme) {
	const auto checked = twidget::sdk::checkThemeParams(theme.params);
	auto inner = twidget::Json::object();
	inner.set("colorScheme", HostedScheme(theme));
	inner.set("params", twidget::sdk::themeParamsJson(checked));
	inner.set("button", theme.button.json());
	inner.set("text", theme.text.json());
	auto result = twidget::Json::object();
	result.set("type", "theme");
	result.set("theme", std::move(inner));
	return result.dump();
}

// The first difference of a widget's reading from |theme| as the SDK
// normalizes it: the scheme a host keeps, then each key of the checked
// palette, which the widget completes from the scheme's defaults alone.
[[nodiscard]] std::string EffectiveMismatch(
		const std::string &scheme,
		const twidget::Json &params,
		const twidget::WidgetTheme &theme) {
	const auto expected = HostedScheme(theme);
	if (scheme != expected) {
		return "colorScheme ["
			+ scheme.substr(0, kDetailBytes)
			+ "], applied ["
			+ expected
			+ ']';
	}
	const auto checked = twidget::sdk::checkThemeParams(theme.params);
	for (const auto &[key, value] : checked) {
		const auto got = params.find(key);
		if (got && got->isString() && (got->asString() == value)) {
			continue;
		}
		const auto text = !got
			? std::string("missing")
			: got->isString()
			? got->asString().substr(0, kDetailBytes)
			: got->dump().substr(0, kDetailBytes);
		return key + " [" + text + "], applied [" + value + ']';
	}
	return std::string();
}

[[nodiscard]] EffectiveReading MissedEffective(
		std::string why,
		std::string_view answer = {}) {
	auto result = EffectiveReading();
	result.missed = std::move(why);
	if (!answer.empty()) {
		result.missed += ": " + std::string(answer.substr(0, kGapBytes));
	}
	return result;
}

// |a| - |b|, or nullopt when the difference does not fit.
[[nodiscard]] std::optional<crl::profile_time> CheckedTimeDifference(
		crl::profile_time a,
		crl::profile_time b) {
	using Limits = std::numeric_limits<crl::profile_time>;
	const auto overflows = (b < 0)
		? (a > Limits::max() + b)
		: (a < Limits::min() + b);
	return overflows ? std::nullopt : std::make_optional(a - b);
}

// The final observation of the run TapHostedWidget() selected |selection|
// of |tap| for: the tap held weakly, nothing of the selection or its host,
// and every copy's call after the first one ignored.
[[nodiscard]] Fn<void(const Tgw::HostedDecision &decision)> HostedFinish(
		const std::shared_ptr<WidgetOpsTap> &tap,
		int selection,
		uint64 work) {
	const auto called = std::make_shared<std::atomic<bool>>(false);
	return [weak = std::weak_ptr(tap), selection, work, called](
			const Tgw::HostedDecision &decision) {
		if (called->exchange(true)) {
			return;
		} else if (const auto strong = weak.lock()) {
			strong->hostedFinal(selection, work, decision);
		}
	};
}

// Never destroyed: a host's deleter worker or the render thread may still
// reach it while the process exits.
[[nodiscard]] HostedRegistry &HostedTaps() {
	static auto &result = *new HostedRegistry();
	return result;
}

void RegisterHosted(
		const std::shared_ptr<HostedObservations> &observations,
		const void *host,
		uint64 work) {
	auto &registry = HostedTaps();
	const auto entry = HostedRegistry::Entry{
		.observations = observations,
		.raw = observations.get(),
	};
	auto lock = std::lock_guard(registry.mutex);
	registry.hosts[host] = entry;
	if (work) {
		registry.works[work] = entry;
	}
}

template <typename Key>
void EraseHosted(
		HostedRegistry::Entries<Key> &entries,
		Key key,
		const HostedObservations *raw) {
	const auto i = entries.find(key);
	if (i != end(entries) && (i->second.raw == raw)) {
		entries.erase(i);
	}
}

void UnregisterHosted(
		const HostedObservations *raw,
		const void *host,
		uint64 work) {
	auto &registry = HostedTaps();
	auto lock = std::lock_guard(registry.mutex);
	EraseHosted(registry.hosts, host, raw);
	EraseHosted(registry.works, work, raw);
}

// The observations registered for |key|, held by the caller: they are
// released, if last, after the registry lock.
template <typename Key>
[[nodiscard]] std::shared_ptr<HostedObservations> FindHosted(
		HostedRegistry::Entries<Key> HostedRegistry::*entries,
		std::type_identity_t<Key> key) {
	auto &registry = HostedTaps();
	auto lock = std::lock_guard(registry.mutex);
	const auto &map = registry.*entries;
	const auto i = map.find(key);
	return (i != end(map)) ? i->second.observations.lock() : nullptr;
}

HostedObservations::HostedObservations(
	std::shared_ptr<WidgetOpsTap> tap,
	int selection,
	const std::shared_ptr<twidget::WidgetHost> &host,
	uint64 work,
	const twidget::WidgetTheme &built)
: _tap(std::move(tap))
, _built(built)
, _host(host.get())
, _work(work)
, _selection(selection) {
	_tap->hostedBuilt();
}

HostedObservations::~HostedObservations() {
	UnregisterHosted(this, _host, _work);
	_tap->wrapperDestroyed();
}

void HostedObservations::ops(const std::string &clean) const {
	auto json = std::string();
	json.reserve(kOpsPrefix.size() + clean.size() + kOpsSuffix.size());
	json.append(kOpsPrefix);
	json.append(clean);
	json.append(kOpsSuffix);
	_tap->hostMessage(_selection, json);
}

bool HostedObservations::consumed(const std::string &message) {
	if (!kHelperMirrors
		|| (HostedWire::KindOf(message) != HostedWire::Kind::Mirrored)) {
		return false;
	}
	const auto record = HostedWire::DecodeMirrored(message);
	if (!record) {
		return false;
	}
	switch (record->way) {
	case HostedWire::kMirroredBuilt:
	case HostedWire::kMirroredTheme:
		scheme(record->way, record->text);
		break;
	case HostedWire::kMirroredHost:
		_tap->servedMessage(_selection, record->text);
		break;
	case HostedWire::kMirroredRenderer:
		_tap->rendererMessage(_selection, record->text);
		break;
	case HostedWire::kMirroredWork:
		work(record->text);
		break;
	}
	return true;
}

void HostedObservations::changed(
		const twidget::WidgetDiagnostics &diagnostics) {
	if (diagnostics.dead && !_dead.exchange(true)) {
		_tap->hostedDied(_selection, *diagnostics.dead);
	}
	if (!diagnostics.rendered || _rendered.exchange(true)) {
		return;
	}
	_tap->hostRendered();
}

void HostedObservations::recordBuilt() {
	_builtSerial = recordApplied(_built);
}

void HostedObservations::observeBuilt(twidget::WidgetHost &host) const {
	if (_builtSerial) {
		_tap->themeEffective(
			_selection,
			_builtSerial,
			ReadEffective(probe(host), _built));
	}
}

void HostedObservations::themeSelected(
		const twidget::WidgetTheme &theme) const {
	_tap->themeSelected(_selection, HostedThemeMessage(theme));
}

uint64 HostedObservations::themeScheduled(
		const twidget::WidgetTheme &theme) const {
	return _tap->themeScheduled(_selection, HostedThemeMessage(theme));
}

void HostedObservations::themeApplied(
		twidget::WidgetHost &host,
		const twidget::WidgetTheme &theme,
		uint64 token) const {
	if (const auto serial = recordApplied(theme, token)) {
		_tap->themeEffective(
			_selection,
			serial,
			ReadEffective(probe(host), theme));
	}
}

void HostedObservations::retired() const {
	_tap->hostRetired(_selection, _work);
}

void HostedObservations::detached() const {
	_tap->detached(_selection, _work);
}

void HostedObservations::pressDispatched(uint64 serial) const {
	_tap->pressDispatched(_selection, _work, serial);
}

void HostedObservations::pressObserved(
		uint64 press,
		const Tgw::PressStep &step) const {
	_tap->pressObserved(_selection, _work, press, step);
}

void HostedObservations::replaceProbe(Fn<std::string()> probe) {
	_probe = std::move(probe);
}

int HostedObservations::recordApplied(
		const twidget::WidgetTheme &theme,
		uint64 token) const {
	auto message = HostedThemeMessage(theme);
	if (message.size() > kMaxKeptValueBytes) {
		_tap->gap("applied theme message of "
			+ std::to_string(message.size())
			+ " bytes, past "
			+ std::to_string(kMaxKeptValueBytes));
		return 0;
	}
	return _tap->themeApplied(
		_selection,
		HostedScheme(theme),
		std::move(message),
		token);
}

std::string HostedObservations::probe(twidget::WidgetHost &host) const {
	return _probe ? _probe() : host.probe(kEffectiveProbe);
}

void HostedObservations::work(std::string_view text) const {
	const auto record = DecodeHelperWork(text);
	const auto event = record
		? HelperWorkEvent(*record, crl::profile(), SteadyMicros(), _selection)
		: std::nullopt;
	if (!event) {
		_tap->helperWorkMalformed();
		return;
	}
	_tap->helperWork(*event);
}

void HostedObservations::scheme(char way, std::string_view text) {
	if (text.size() > kMaxSchemeBytes) {
		_tap->gap("helper scheme of "
			+ std::to_string(text.size())
			+ " bytes, past "
			+ std::to_string(kMaxSchemeBytes));
		return;
	} else if (way == HostedWire::kMirroredTheme) {
		_tap->themeSet(_selection, std::string(text));
	} else if (!std::exchange(_reported, true)) {
		// The helper's tap reports its build once; a repeat changes nothing.
		_tap->schemeReported(_selection, std::string(text));
	}
}

HostedSelection::HostedSelection(
	std::shared_ptr<WidgetOpsTap> tap,
	int index,
	uint64 work,
	const twidget::WidgetTheme &theme)
: _tap(std::move(tap))
, _theme(theme)
, _work(work)
, _index(index) {
}

HostedSelection::~HostedSelection() {
	if (!_installed) {
		_tap->factoryCompleted();
	}
	_tap->factoryReleased();
}

void HostedSelection::install(
		const std::shared_ptr<twidget::WidgetHost> &pointer) {
	if (!pointer || _installed.exchange(true)) {
		return;
	}
	_tap->factoryCompleted();
	auto &host = *pointer;
	const auto observations = std::make_shared<HostedObservations>(
		_tap,
		_index,
		pointer,
		_work,
		_theme);
	RegisterHosted(observations, &host, _work);
	observations->recordBuilt();
	auto ops = std::move(host.onRenderOps);
	host.onRenderOps = [observations, ops = std::move(ops)](
			const std::string &clean) {
		observations->ops(clean);
		if (ops) {
			ops(clean);
		}
	};
	auto platform = std::move(host.onPlatformMessage);
	host.onPlatformMessage = [
		observations,
		platform = std::move(platform)
	](const std::string &message) {
		if (!observations->consumed(message) && platform) {
			platform(message);
		}
	};
	auto change = std::move(host.onChange);
	host.onChange = [observations, change = std::move(change)](
			const twidget::WidgetDiagnostics &diagnostics) {
		observations->changed(diagnostics);
		if (change) {
			change(diagnostics);
		}
	};
	if (kHelperMirrors) {
		host.sendPlatform(_tap->asksWork()
			? HostedWire::EncodeMirrorWork()
			: HostedWire::EncodeMirror());
	}
}

} // namespace

crl::profile_time SteadyMicros() {
	using namespace std::chrono;
	return duration_cast<microseconds>(
		steady_clock::now().time_since_epoch()).count();
}

bool ReplaceEffectiveProbe(
		twidget::WidgetHost &host,
		Fn<std::string()> probe) {
	const auto observations = FindHosted(&HostedRegistry::hosts, &host);
	if (!observations) {
		return false;
	}
	observations->replaceProbe(std::move(probe));
	return true;
}

EffectiveReading ReadEffective(
		std::string_view answer,
		const twidget::WidgetTheme &theme) {
	if (answer.size() > kMaxEffectiveBytes) {
		return MissedEffective("a theme reading of "
			+ std::to_string(answer.size())
			+ " bytes, past "
			+ std::to_string(kMaxEffectiveBytes),
			answer);
	}
	const auto parsed = twidget::Json::parse(answer);
	if (!parsed || !parsed->isArray() || (parsed->size() != 2)) {
		return MissedEffective("no theme reading", answer);
	}
	const auto &scheme = (*parsed)[std::size_t(0)];
	const auto &params = (*parsed)[std::size_t(1)];
	if (!scheme.isString() || !params.isObject()) {
		return MissedEffective("no theme reading", answer);
	}
	auto inner = twidget::Json::object();
	inner.set("colorScheme", scheme.asString());
	inner.set("params", params);
	auto message = twidget::Json::object();
	message.set("type", "theme");
	message.set("theme", std::move(inner));
	return {
		.scheme = scheme.asString(),
		.params = params.dump(),
		.message = message.dump(),
		.mismatch = EffectiveMismatch(scheme.asString(), params, theme),
	};
}

std::optional<WidgetHelperWorkEvent> HelperWorkEvent(
		const HelperWorkRecord &record,
		crl::profile_time profile,
		crl::profile_time steady,
		int generation) {
	if (record.finished > steady) {
		return std::nullopt;
	}
	const auto offset = CheckedTimeDifference(profile, steady);
	const auto started = offset
		? CheckedTimeSum(record.started, *offset)
		: std::nullopt;
	const auto finished = offset
		? CheckedTimeSum(record.finished, *offset)
		: std::nullopt;
	if (!started || !finished) {
		return std::nullopt;
	}
	return WidgetHelperWorkEvent{
		.started = *started,
		.finished = *finished,
		.waited = record.waited,
		.seq = record.seq,
		.waits = record.waits,
		.frameBytes = record.frameBytes,
		.generation = generation,
		.kind = WidgetHelperWorkKind(record.kind),
	};
}

} // namespace Test::WidgetOps

namespace Test {

using namespace WidgetOps;

Fn<void(const std::shared_ptr<twidget::WidgetHost> &host)> TapHostedWidget(
		GlobalMsgId id,
		uint64 work,
		const twidget::WidgetTheme &theme,
		Fn<void(const Tgw::HostedDecision &decision)> *finish) {
	if (!Active()) {
		return nullptr;
	}
	const auto &armed = ArmedTaps();
	const auto i = armed.find(id);
	if (i == end(armed)) {
		return nullptr;
	}
	const auto index = i->second->factorySelected();
	i->second->hostedSelected(index, work, finish != nullptr);
	if (finish) {
		*finish = HostedFinish(i->second, index, work);
	}
	const auto selection = std::make_shared<HostedSelection>(
		i->second,
		index,
		work,
		theme);
	return [selection](const std::shared_ptr<twidget::WidgetHost> &host) {
		selection->install(host);
	};
}

Fn<void(twidget::WidgetHost &host)> WidgetThemeObservation(
		twidget::WidgetHost &host) {
	const auto observations = FindHosted(&HostedRegistry::hosts, &host);
	if (!observations) {
		return nullptr;
	}
	return [weak = std::weak_ptr<HostedObservations>(observations)](
			twidget::WidgetHost &host) {
		if (const auto strong = weak.lock()) {
			strong->observeBuilt(host);
		}
	};
}

void WidgetThemeSelected(
		twidget::WidgetHost &host,
		const twidget::WidgetTheme &theme) {
	if (const auto observations = FindHosted(&HostedRegistry::hosts, &host)) {
		observations->themeSelected(theme);
	}
}

uint64 WidgetThemeScheduled(
		twidget::WidgetHost &host,
		const twidget::WidgetTheme &theme) {
	const auto observations = FindHosted(&HostedRegistry::hosts, &host);
	return observations ? observations->themeScheduled(theme) : 0;
}

void WidgetThemeApplied(
		twidget::WidgetHost &host,
		const twidget::WidgetTheme &theme,
		uint64 token) {
	if (const auto observations = FindHosted(&HostedRegistry::hosts, &host)) {
		observations->themeApplied(host, theme, token);
	}
}

void WidgetHostRetired(twidget::WidgetHost &host) {
	if (const auto observations = FindHosted(&HostedRegistry::hosts, &host)) {
		observations->retired();
	}
}

void WidgetPressDispatched(uint64 work, uint64 serial) {
	if (const auto observations = FindHosted(&HostedRegistry::works, work)) {
		observations->pressDispatched(serial);
	}
}

void WidgetCoreDetached(uint64 work) {
	if (const auto observations = FindHosted(&HostedRegistry::works, work)) {
		observations->detached();
	}
}

void WidgetPressObserved(
		uint64 work,
		uint64 press,
		const Tgw::PressStep &step) {
	if (const auto observations = FindHosted(&HostedRegistry::works, work)) {
		observations->pressObserved(press, step);
	}
}

std::string WidgetThemeUnobserved(const WidgetOpsLatest &latest) {
	if (latest.themePending) {
		return "theme-pending: the theme the runtime wants generation "
			+ std::to_string(latest.generation)
			+ " to have (application "
			+ std::to_string(latest.themePending)
			+ ", after theme "
			+ std::to_string(latest.appliedThemes)
			+ ") is not yet applied and read";
	} else if (latest.appliedThemes
		&& (latest.effectiveSerial == latest.appliedThemes)) {
		return std::string();
	}
	return "theme-unobserved: the widget's theme was not read after theme "
		+ std::to_string(latest.appliedThemes)
		+ " the runtime applied to generation "
		+ std::to_string(latest.generation)
		+ " (kept readings "
		+ std::to_string(latest.effectiveThemes)
		+ ", the newest for theme "
		+ std::to_string(latest.effectiveSerial)
		+ ", probes without one "
		+ std::to_string(latest.effectiveMissed)
		+ (latest.effectiveError.empty()
			? std::string()
			: (", the last: " + latest.effectiveError))
		+ ')';
}

WidgetThemeCheck CheckWidgetTheme(
		const WidgetOpsLatest &latest,
		std::string_view scheme,
		const WidgetOpsLatest *before) {
	const auto unobserved = [](std::string detail) {
		return WidgetThemeCheck{
			.verdict = WidgetThemeVerdict::Unobserved,
			.detail = std::move(detail),
		};
	};
	const auto wrong = [](std::string detail) {
		return WidgetThemeCheck{
			.verdict = WidgetThemeVerdict::Wrong,
			.detail = std::move(detail),
		};
	};
	if (before && (latest.generation != before->generation)) {
		return unobserved("theme-superseded: generation "
			+ std::to_string(latest.generation)
			+ " replaced generation "
			+ std::to_string(before->generation)
			+ " the switch began with");
	} else if (before && (latest.appliedThemes <= before->appliedThemes)) {
		return unobserved("theme-unapplied: the runtime applied no theme "
			"after theme "
			+ std::to_string(before->appliedThemes)
			+ " of generation "
			+ std::to_string(latest.generation));
	}
	auto missing = WidgetThemeUnobserved(latest);
	if (!missing.empty()) {
		return unobserved(std::move(missing));
	} else if (latest.appliedScheme != scheme) {
		return wrong("the runtime applied the scheme ["
			+ latest.appliedScheme
			+ "], not "
			+ std::string(scheme));
	} else if (!latest.effectiveMismatch.empty()) {
		return wrong("the widget reads another theme than the runtime "
			"applied: "
			+ latest.effectiveMismatch);
	}
	return {
		.verdict = WidgetThemeVerdict::Matches,
	};
}

std::optional<crl::profile_time> CheckedTimeSum(
		crl::profile_time a,
		crl::profile_time b) {
	using Limits = std::numeric_limits<crl::profile_time>;
	const auto overflows = (b > 0)
		? (a > Limits::max() - b)
		: (a < Limits::min() - b);
	return overflows ? std::nullopt : std::make_optional(a + b);
}

} // namespace Test

#else // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS

namespace Test {

Fn<void(const std::shared_ptr<twidget::WidgetHost> &host)> TapHostedWidget(
		GlobalMsgId,
		uint64,
		const twidget::WidgetTheme &,
		Fn<void(const Tgw::HostedDecision &decision)> *) {
	return nullptr;
}

Fn<void(twidget::WidgetHost &host)> WidgetThemeObservation(
		twidget::WidgetHost &) {
	return nullptr;
}

void WidgetThemeSelected(
		twidget::WidgetHost &,
		const twidget::WidgetTheme &) {
}

uint64 WidgetThemeScheduled(
		twidget::WidgetHost &,
		const twidget::WidgetTheme &) {
	return 0;
}

void WidgetThemeApplied(
		twidget::WidgetHost &,
		const twidget::WidgetTheme &,
		uint64) {
}

void WidgetHostRetired(twidget::WidgetHost &) {
}

void WidgetPressDispatched(uint64, uint64) {
}

void WidgetCoreDetached(uint64) {
}

void WidgetPressObserved(uint64, uint64, const Tgw::PressStep &) {
}

} // namespace Test

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
