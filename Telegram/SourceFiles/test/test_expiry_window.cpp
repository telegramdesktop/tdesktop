/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_expiry_window.h"

#include "base/unixtime.h"
#include "test/test_log.h"
#include "test/test_runner.h"

#include <ctime>
#include <type_traits>
#include <vector>

namespace Test {
namespace {

constexpr auto kClockAttempts = 3;

// Adds base::unixtime's whole-second shift, measured against time(nullptr)
// (the clock it shifts), to a Qt millisecond read whose second may differ.
[[nodiscard]] int64 ComposeServerMs(
		int64 localMs,
		TimeId localSeconds,
		TimeId shiftedSeconds) {
	return localMs + (int64(shiftedSeconds) - localSeconds) * 1000;
}

[[nodiscard]] QString NumberText(const std::optional<int64> &value) {
	return value ? QString::number(*value) : u"none"_q;
}

[[nodiscard]] ExpiryRefusal OwnerRefusal(
		QObject *bound,
		const ExpiryReading &current) {
	if (!bound) {
		return ExpiryRefusal::OwnerNotLive;
	} else if (!current.owner || current.owner.data() != bound) {
		return ExpiryRefusal::WrongOwner;
	}
	return ExpiryRefusal::None;
}

[[nodiscard]] ExpiryRefusal IdentityRefusal(
		QObject *bound,
		const std::optional<uint64> &boundGeneration,
		const QString &boundLink,
		const ExpiryReading &current) {
	const auto owner = OwnerRefusal(bound, current);
	if (owner != ExpiryRefusal::None) {
		return owner;
	} else if (!boundGeneration || !current.generation) {
		return ExpiryRefusal::GenerationAbsent;
	} else if (*boundGeneration != *current.generation) {
		return ExpiryRefusal::ObsoleteGeneration;
	} else if (boundLink.isEmpty() || current.link.isEmpty()) {
		return ExpiryRefusal::LinkAbsent;
	} else if (boundLink != current.link) {
		return ExpiryRefusal::WrongLink;
	}
	return ExpiryRefusal::None;
}

[[nodiscard]] ExpiryRefusal ServedRefusal(
		const std::optional<TimeId> &served) {
	if (!served) {
		return ExpiryRefusal::ServedExpiryAbsent;
	} else if (*served <= 0) {
		return ExpiryRefusal::ServedExpiryNotPositive;
	}
	return ExpiryRefusal::None;
}

[[nodiscard]] ExpiryRefusal ReceiptRefusal(
		const ServerClockReading &receipt) {
	if (!receipt.serverMs) {
		return ExpiryRefusal::ReceiptAbsent;
	} else if (*receipt.serverMs <= 0) {
		return ExpiryRefusal::ReceiptNotPositive;
	}
	return ExpiryRefusal::None;
}

[[nodiscard]] ExpiryRefusal ProductRefusal(
		const std::optional<TimeId> &product,
		TimeId served) {
	if (!product) {
		return ExpiryRefusal::ProductExpiryAbsent;
	} else if (*product <= 0) {
		return ExpiryRefusal::ProductExpiryNotPositive;
	} else if (*product != served) {
		return ExpiryRefusal::ProductExpiryDiffers;
	}
	return ExpiryRefusal::None;
}

[[nodiscard]] ExpiryRefusal ClockRefusal(const ServerClockReading &clock) {
	if (!clock.serverMs) {
		return ExpiryRefusal::ClockAbsent;
	} else if (*clock.serverMs <= 0) {
		return ExpiryRefusal::ClockNotPositive;
	}
	return ExpiryRefusal::None;
}

[[nodiscard]] ExpiryRefusal FreezeRefusal(
		const ExpiryWindowArgs &args,
		const ExpiryObservation &observation,
		const ExpiryReading &current) {
	const auto identity = IdentityRefusal(
		observation.owner.data(),
		observation.generation,
		observation.link,
		current);
	if (identity != ExpiryRefusal::None) {
		return identity;
	}
	const auto served = ServedRefusal(observation.servedExpires);
	if (served != ExpiryRefusal::None) {
		return served;
	}
	const auto receipt = ReceiptRefusal(observation.receipt);
	if (receipt != ExpiryRefusal::None) {
		return receipt;
	}
	const auto product = ProductRefusal(
		current.productExpires,
		*observation.servedExpires);
	if (product != ExpiryRefusal::None) {
		return product;
	}
	const auto clock = ClockRefusal(current.clock);
	if (clock != ExpiryRefusal::None) {
		return clock;
	}
	const auto until = int64(*observation.servedExpires) * 1000
		- args.cutoffLead
		- *current.clock.serverMs;
	return (until <= 0 || until < args.minimumAhead)
		? ExpiryRefusal::CutoffNotAhead
		: ExpiryRefusal::None;
}

[[nodiscard]] ExpiryRefusal PlanRefusal(
		const ExpiryWindow &window,
		const ExpiryReading &current) {
	const auto identity = IdentityRefusal(
		window.owner(),
		window.generation(),
		window.link(),
		current);
	if (identity != ExpiryRefusal::None) {
		return identity;
	}
	const auto product = ProductRefusal(
		current.productExpires,
		window.servedExpires());
	if (product != ExpiryRefusal::None) {
		return product;
	}
	return ClockRefusal(current.clock);
}

[[nodiscard]] QString OwnerText(QObject *bound) {
	return bound
		? (u"live:"_q + QString::fromLatin1(bound->metaObject()->className()))
		: u"not-live"_q;
}

[[nodiscard]] QString CurrentOwnerText(
		QObject *bound,
		const ExpiryReading &current) {
	return !current.owner
		? u"absent"_q
		: (current.owner.data() == bound)
		? u"same"_q
		: u"other"_q;
}

[[nodiscard]] QString LinkText(
		const QString &bound,
		const QString &current) {
	return (bound.isEmpty() || current.isEmpty())
		? u"absent"_q
		: (bound == current)
		? u"same"_q
		: u"other"_q;
}

[[nodiscard]] QString ReactionPremise(
		QObject *bound,
		const ExpiryReading &current) {
	return u"owner=%1 current=%2 clockServerMs=%3"_q.arg(
		OwnerText(bound),
		CurrentOwnerText(bound, current),
		NumberText(current.clock.serverMs));
}

[[nodiscard]] QString StateText(ExpiryWatchState state) {
	switch (state) {
	case ExpiryWatchState::Waiting: return u"waiting"_q;
	case ExpiryWatchState::Reached: return u"reached"_q;
	case ExpiryWatchState::Refused: return u"refused"_q;
	}
	Unexpected("State in Test::ExpiryBoundaryWatch::details.");
}

} // namespace

static_assert(!std::is_copy_constructible_v<ExpiryWindow>);
static_assert(!std::is_copy_assignable_v<ExpiryWindow>);
static_assert(std::is_same_v<
	decltype(ExpiryFreeze::window),
	std::shared_ptr<const ExpiryWindow>>);

ServerClockReading ReadServerClock() {
	for (auto attempt = 0; attempt != kClockAttempts; ++attempt) {
		const auto local = TimeId(std::time(nullptr));
		const auto shifted = base::unixtime::now();
		if (TimeId(std::time(nullptr)) == local) {
			return {
				.serverMs = ComposeServerMs(
					QDateTime::currentMSecsSinceEpoch(),
					local,
					shifted),
			};
		}
	}
	return {};
}

QString ExpiryRefusalName(ExpiryRefusal refusal) {
	switch (refusal) {
	case ExpiryRefusal::None: return u"none"_q;
	case ExpiryRefusal::NotEvaluated: return u"not-evaluated"_q;
	case ExpiryRefusal::OwnerNotLive: return u"owner-not-live"_q;
	case ExpiryRefusal::WrongOwner: return u"wrong-owner"_q;
	case ExpiryRefusal::GenerationAbsent: return u"generation-absent"_q;
	case ExpiryRefusal::ObsoleteGeneration: return u"obsolete-generation"_q;
	case ExpiryRefusal::LinkAbsent: return u"link-absent"_q;
	case ExpiryRefusal::WrongLink: return u"wrong-link"_q;
	case ExpiryRefusal::ServedExpiryAbsent:
		return u"served-expiry-absent"_q;
	case ExpiryRefusal::ServedExpiryNotPositive:
		return u"served-expiry-not-positive"_q;
	case ExpiryRefusal::ReceiptAbsent: return u"receipt-absent"_q;
	case ExpiryRefusal::ReceiptNotPositive: return u"receipt-not-positive"_q;
	case ExpiryRefusal::ProductExpiryAbsent:
		return u"product-expiry-absent"_q;
	case ExpiryRefusal::ProductExpiryNotPositive:
		return u"product-expiry-not-positive"_q;
	case ExpiryRefusal::ProductExpiryDiffers:
		return u"product-expiry-differs"_q;
	case ExpiryRefusal::ClockAbsent: return u"clock-absent"_q;
	case ExpiryRefusal::ClockNotPositive: return u"clock-not-positive"_q;
	case ExpiryRefusal::CutoffNotAhead: return u"cutoff-not-ahead"_q;
	case ExpiryRefusal::BoundaryUnconfirmed:
		return u"boundary-unconfirmed"_q;
	}
	Unexpected("Refusal in Test::ExpiryRefusalName.");
}

ExpiryWindow::ExpiryWindow(
	const ExpiryWindowArgs &args,
	const ExpiryObservation &observation,
	int64 frozenServerMs)
: _label(args.label)
, _owner(observation.owner)
, _link(observation.link)
, _generation(*observation.generation)
, _receiptServerMs(*observation.receipt.serverMs)
, _frozenServerMs(frozenServerMs)
, _cutoffLead(args.cutoffLead)
, _servedExpires(*observation.servedExpires) {
}

const QString &ExpiryWindow::label() const {
	return _label;
}

QObject *ExpiryWindow::owner() const {
	return _owner.data();
}

uint64 ExpiryWindow::generation() const {
	return _generation;
}

const QString &ExpiryWindow::link() const {
	return _link;
}

TimeId ExpiryWindow::servedExpires() const {
	return _servedExpires;
}

int64 ExpiryWindow::receiptServerMs() const {
	return _receiptServerMs;
}

int64 ExpiryWindow::frozenServerMs() const {
	return _frozenServerMs;
}

crl::time ExpiryWindow::cutoffLead() const {
	return _cutoffLead;
}

int64 ExpiryWindow::cutoffServerMs() const {
	return int64(_servedExpires) * 1000 - _cutoffLead;
}

ExpiryFreeze FreezeExpiryWindow(
		const ExpiryWindowArgs &args,
		const ExpiryObservation &observation,
		const ExpiryReading &current) {
	const auto refusal = FreezeRefusal(args, observation, current);
	const auto bound = observation.owner.data();
	const auto served = observation.servedExpires;
	const auto clock = current.clock.serverMs;
	const auto cutoff = (served && *served > 0)
		? std::make_optional(int64(*served) * 1000 - args.cutoffLead)
		: std::nullopt;
	const auto until = (cutoff && clock && *clock > 0)
		? std::make_optional(*cutoff - *clock)
		: std::nullopt;
	auto result = ExpiryFreeze{
		.refusal = refusal,
		.details = u"window=%1 refusal=%2 owner=%3 current=%4 "
			u"generation=%5/%6 link=%7 served=%8 receiptServerMs=%9 "
			u"product=%10 clockServerMs=%11 cutoffLead=%12 "
			u"minimumAhead=%13 cutoffServerMs=%14 untilCutoffMs=%15"_q.arg(
				args.label,
				ExpiryRefusalName(refusal),
				OwnerText(bound),
				CurrentOwnerText(bound, current),
				NumberText(observation.generation),
				NumberText(current.generation),
				LinkText(observation.link, current.link),
				NumberText(served),
				NumberText(observation.receipt.serverMs),
				NumberText(current.productExpires),
				NumberText(clock),
				QString::number(args.cutoffLead),
				QString::number(args.minimumAhead),
				NumberText(cutoff),
				NumberText(until)),
	};
	if (refusal == ExpiryRefusal::None) {
		result.window = std::shared_ptr<const ExpiryWindow>(
			new ExpiryWindow(args, observation, *clock));
	}
	return result;
}

ExpiryPlan PlanExpirySample(
		const ExpiryWindow &window,
		const ExpiryReading &current) {
	const auto refusal = PlanRefusal(window, current);
	const auto clock = current.clock.serverMs;
	const auto until = (refusal == ExpiryRefusal::None)
		? std::make_optional(window.cutoffServerMs() - *clock)
		: std::nullopt;
	return {
		.untilCutoffMs = until,
		.refusal = refusal,
		.details = u"window=%1 refusal=%2 owner=%3 current=%4 "
			u"generation=%5/%6 link=%7 served=%8 product=%9 "
			u"clockServerMs=%10 cutoffServerMs=%11 untilCutoffMs=%12"_q.arg(
				window.label(),
				ExpiryRefusalName(refusal),
				OwnerText(window.owner()),
				CurrentOwnerText(window.owner(), current),
				QString::number(window.generation()),
				NumberText(current.generation),
				LinkText(window.link(), current.link),
				QString::number(window.servedExpires()),
				NumberText(current.productExpires),
				NumberText(clock),
				QString::number(window.cutoffServerMs()),
				NumberText(until)),
	};
}

ExpiryBoundaryWatch::ExpiryBoundaryWatch(
	const ExpiryFreeze &freeze,
	crl::time offset)
: _window(freeze.frozen() ? freeze.window : nullptr)
, _offset(offset) {
	if (!_window) {
		refuse((freeze.refusal == ExpiryRefusal::None)
			? ExpiryRefusal::NotEvaluated
			: freeze.refusal);
	}
}

ExpiryWatchState ExpiryBoundaryWatch::poll(const ExpiryReading &current) {
	if (_state != ExpiryWatchState::Waiting) {
		return _state;
	}
	++_polls;
	const auto clock = ClockRefusal(current.clock);
	if (clock != ExpiryRefusal::None) {
		_premise = ReactionPremise(_window->owner(), current);
		refuse(clock);
		return _state;
	}
	const auto now = *current.clock.serverMs;
	const auto cutoff = _window->cutoffServerMs();
	const auto boundary = cutoff + _offset;
	if (now < std::min(cutoff, boundary)) {
		const auto plan = PlanExpirySample(*_window, current);
		_premise = plan.details;
		if (!plan.accepted()) {
			refuse(plan.refusal);
			return _state;
		}
		++_confirmations;
		_confirmedServerMs = now;
		_untilBoundaryMs = boundary - now;
		return _state;
	}
	_premise = ReactionPremise(_window->owner(), current);
	const auto owner = OwnerRefusal(_window->owner(), current);
	if (!_confirmations) {
		refuse(ExpiryRefusal::BoundaryUnconfirmed);
	} else if (owner != ExpiryRefusal::None) {
		refuse(owner);
	} else if (now < boundary) {
		_untilBoundaryMs = boundary - now;
	} else {
		_state = ExpiryWatchState::Reached;
		_reachedServerMs = now;
		_untilBoundaryMs = std::nullopt;
	}
	return _state;
}

ExpiryWatchState ExpiryBoundaryWatch::state() const {
	return _state;
}

ExpiryRefusal ExpiryBoundaryWatch::refusal() const {
	return _refusal;
}

const std::shared_ptr<const ExpiryWindow> &ExpiryBoundaryWatch::window() const {
	return _window;
}

std::optional<int64> ExpiryBoundaryWatch::boundaryServerMs() const {
	return _window
		? std::make_optional(_window->cutoffServerMs() + _offset)
		: std::nullopt;
}

std::optional<int64> ExpiryBoundaryWatch::untilBoundaryMs() const {
	return _untilBoundaryMs;
}

std::optional<int64> ExpiryBoundaryWatch::reachedServerMs() const {
	return _reachedServerMs;
}

int ExpiryBoundaryWatch::polls() const {
	return _polls;
}

int ExpiryBoundaryWatch::confirmations() const {
	return _confirmations;
}

QString ExpiryBoundaryWatch::details() const {
	const auto cutoff = _window
		? std::make_optional(_window->cutoffServerMs())
		: std::nullopt;
	const auto boundary = boundaryServerMs();
	const auto late = (_reachedServerMs && boundary)
		? std::make_optional(*_reachedServerMs - *boundary)
		: std::nullopt;
	return u"window=%1 state=%2 refusal=%3 offset=%4 cutoffServerMs=%5 "
		u"boundaryServerMs=%6 polls=%7 confirmations=%8 "
		u"confirmedServerMs=%9 untilBoundaryMs=%10 reachedServerMs=%11 "
		u"lateMs=%12 premise=[%13]"_q.arg(
			_window ? _window->label() : u"none"_q,
			StateText(_state),
			ExpiryRefusalName(_refusal),
			QString::number(_offset),
			NumberText(cutoff),
			NumberText(boundary),
			QString::number(_polls),
			QString::number(_confirmations),
			NumberText(_confirmedServerMs),
			NumberText(_untilBoundaryMs),
			NumberText(_reachedServerMs),
			NumberText(late),
			_premise);
}

void ExpiryBoundaryWatch::refuse(ExpiryRefusal refusal) {
	_state = ExpiryWatchState::Refused;
	_refusal = refusal;
	_untilBoundaryMs = std::nullopt;
}

namespace {

// Literal caller-owned values: no real expiry, link or clock is read.
constexpr auto kServed = TimeId(1900000000);
constexpr auto kReceiptFractionMs = int64(437);
constexpr auto kReceiptMs = int64(kServed - 60) * 1000 + kReceiptFractionMs;
constexpr auto kFreezeMs = kReceiptMs + 2000;
constexpr auto kLead = crl::time(3000);
constexpr auto kAhead = crl::time(4000);
constexpr auto kOffset = crl::time(60);
constexpr auto kCutoffMs = int64(kServed) * 1000 - kLead;
constexpr auto kBoundaryMs = kCutoffMs + kOffset;
constexpr auto kRefusalCount = 19;
const auto kLinkA = u"self-test-link-a"_q;
const auto kLinkB = u"self-test-link-b"_q;

// The simultaneous windows: a natural expiry on the share's owner and link,
// and a render window on another owner whose source is refreshed.
constexpr auto kNaturalCutoffMs = int64(kServed) * 1000;
constexpr auto kRenderGeneration = uint64(5);
constexpr auto kRenderServed = TimeId(kServed + 20);
constexpr auto kRenderCutoffMs = int64(kRenderServed) * 1000;
constexpr auto kRefreshGeneration = uint64(6);
constexpr auto kRefreshServed = TimeId(kServed + 50);
constexpr auto kRefreshReceiptMs = kReceiptMs + 10000;
constexpr auto kRefreshMs = kFreezeMs + 10000;
constexpr auto kRefreshCutoffMs = int64(kRefreshServed) * 1000;
constexpr auto kRefreshBoundaryMs = kRefreshCutoffMs + kOffset;

// Lifetime, boundary and resynchronization readings.
constexpr auto kNextGeneration = uint64(2);
constexpr auto kLaterMs = kFreezeMs + 100;
constexpr auto kLateMs = int64(2500);
constexpr auto kEarlyOffset = crl::time(-500);
constexpr auto kEarlyBoundaryMs = kCutoffMs + kEarlyOffset;
constexpr auto kForwardMs = kLaterMs + 5000;
constexpr auto kBackwardMs = kLaterMs - 3000;
constexpr auto kClockUpdateMs = kFreezeMs + 5000;
constexpr auto kDisplayMs = kCutoffMs - 6900;
constexpr auto kFractionMs = int64(300);
constexpr auto kDisplaySeconds = int64(7);

// The server clock composition over literal reads: time(nullptr)'s second,
// and base::unixtime::now() seven seconds ahead of it.
constexpr auto kLocalSecond = TimeId(kServed - 100);
constexpr auto kShiftSeconds = TimeId(7);

struct SelfTest {
	std::optional<QObject> a;
	std::optional<QObject> b;
	std::optional<QObject> other;
	std::optional<QObject> slot;
	int releases = 0;
};

struct RefusalNameRow {
	ExpiryRefusal refusal = ExpiryRefusal::NotEvaluated;
	QString name;
};

struct FieldRow {
	QString name;
	QString actual;
	QString expected;
};

struct SeparationRow {
	QString name;
	std::optional<TimeId> served;
	std::optional<TimeId> product;
	ExpiryRefusal expected = ExpiryRefusal::NotEvaluated;
	QString servedText;
	QString productText;
};

struct FreezeInput {
	ExpiryWindowArgs args;
	ExpiryObservation observation;
	ExpiryReading current;
};

// |expected| is None for a control row, which must freeze.
struct FreezeRow {
	QString name;
	Fn<void(FreezeInput&)> mutate;
	ExpiryRefusal expected = ExpiryRefusal::NotEvaluated;
};

struct PlanRow {
	QString name;
	Fn<void(ExpiryReading&)> mutate;
	ExpiryRefusal expected = ExpiryRefusal::NotEvaluated;
};

// One poll of a watch and every reader it must leave behind.
struct PollRow {
	QString name;
	ExpiryReading reading;
	ExpiryWatchState state = ExpiryWatchState::Waiting;
	ExpiryRefusal refusal = ExpiryRefusal::None;
	int polls = 0;
	int confirmations = 0;
	std::optional<int64> untilBoundaryMs;
};

struct NamedOwner {
	const std::optional<QObject> *object = nullptr;
	QString name;
};

// One window's inputs and, independently, the cutoff it must freeze.
struct WindowSpec {
	QString label;
	QString link;
	QObject *owner = nullptr;
	uint64 generation = 0;
	int64 receiptMs = 0;
	int64 frozenMs = 0;
	int64 cutoffMs = 0;
	crl::time lead = 0;
	TimeId served = 0;
};

struct SimultaneousSpecs {
	WindowSpec share;
	WindowSpec natural;
	WindowSpec render;
	WindowSpec refreshed;
};

struct SnapshotRow {
	const WindowSpec *spec = nullptr;
	const ExpiryWindow *window = nullptr;
};

// The share and render watches polled at the same clock reading.
struct InterleavedRow {
	PollRow share;
	PollRow render;
};

// A Qt millisecond read against kLocalSecond, and the server milliseconds
// it must compose to, written out without the composition.
struct ComposeRow {
	QString name;
	int64 localMs = 0;
	int64 expected = 0;
};

[[nodiscard]] QObject *Live(std::optional<QObject> &object) {
	return object ? &*object : nullptr;
}

[[nodiscard]] ExpiryObservation BaseObservation(QObject *owner) {
	return {
		.owner = owner,
		.generation = uint64(1),
		.link = kLinkA,
		.servedExpires = kServed,
		.receipt = { .serverMs = kReceiptMs },
	};
}

[[nodiscard]] ExpiryReading BaseReading(QObject *owner, int64 clock) {
	return {
		.owner = owner,
		.generation = uint64(1),
		.link = kLinkA,
		.productExpires = kServed,
		.clock = { .serverMs = clock },
	};
}

[[nodiscard]] ExpiryWindowArgs ShareArgs() {
	return {
		.label = u"share"_q,
		.cutoffLead = kLead,
		.minimumAhead = kAhead,
	};
}

[[nodiscard]] QString FlagText(bool value) {
	return value ? u"yes"_q : u"no"_q;
}

[[nodiscard]] QString OwnerName(const SelfTest &state, const QObject *owner) {
	if (!owner) {
		return u"null"_q;
	}
	const auto named = std::vector<NamedOwner>{
		{ &state.a, u"a"_q },
		{ &state.b, u"b"_q },
		{ &state.other, u"other"_q },
		{ &state.slot, u"slot"_q },
	};
	for (const auto &entry : named) {
		if (*entry.object && (&**entry.object == owner)) {
			return entry.name;
		}
	}
	return u"unknown"_q;
}

[[nodiscard]] QString LinkName(const QString &link) {
	return link.isEmpty()
		? u"empty"_q
		: (link == kLinkA)
		? u"a"_q
		: (link == kLinkB)
		? u"b"_q
		: u"other"_q;
}

[[nodiscard]] QString RefusalText(ExpiryRefusal refusal) {
	return u"refusal=%1 "_q.arg(ExpiryRefusalName(refusal));
}

[[nodiscard]] QString FreezeText(const ExpiryFreeze &freeze) {
	return u"frozen=%1 window=%2 refusal=%3 details=[%4]"_q.arg(
		FlagText(freeze.frozen()),
		freeze.window ? u"present"_q : u"null"_q,
		ExpiryRefusalName(freeze.refusal),
		freeze.details);
}

[[nodiscard]] QString PlanText(const ExpiryPlan &plan) {
	return u"accepted=%1 untilCutoffMs=%2 refusal=%3 details=[%4]"_q.arg(
		FlagText(plan.accepted()),
		NumberText(plan.untilCutoffMs),
		ExpiryRefusalName(plan.refusal),
		plan.details);
}

[[nodiscard]] bool FreezeRefusedBy(
		const ExpiryFreeze &freeze,
		ExpiryRefusal expected) {
	return !freeze.frozen()
		&& !freeze.window
		&& (freeze.refusal == expected)
		&& freeze.details.contains(RefusalText(expected));
}

[[nodiscard]] bool WatchRefusedEmpty(
		const ExpiryBoundaryWatch &watch,
		ExpiryRefusal expected) {
	return (watch.state() == ExpiryWatchState::Refused)
		&& (watch.refusal() == expected)
		&& !watch.window()
		&& !watch.boundaryServerMs()
		&& !watch.untilBoundaryMs()
		&& !watch.reachedServerMs()
		&& (watch.polls() == 0)
		&& (watch.confirmations() == 0);
}

void CheckRefusedFreeze(
		const ExpiryFreeze &freeze,
		ExpiryRefusal expected,
		const QString &what) {
	Check(
		FreezeRefusedBy(freeze, expected),
		u"expiry window self-test: "_q
			+ what
			+ u" refuses "_q
			+ ExpiryRefusalName(expected)
			+ u" without a window"_q,
		FreezeText(freeze));
}

void CheckAcceptedPlan(
		const ExpiryWindow &window,
		const ExpiryReading &reading,
		int64 untilCutoffMs,
		const QString &what) {
	const auto plan = PlanExpirySample(window, reading);
	Check(
		plan.accepted()
			&& (plan.refusal == ExpiryRefusal::None)
			&& (plan.untilCutoffMs == untilCutoffMs)
			&& plan.details.contains(RefusalText(ExpiryRefusal::None)),
		u"expiry window self-test: "_q
			+ what
			+ u" plans a sample "_q
			+ QString::number(untilCutoffMs)
			+ u" ms before the cutoff"_q,
		PlanText(plan));
}

void CheckRefusedPlan(
		const ExpiryWindow &window,
		const ExpiryReading &reading,
		ExpiryRefusal expected,
		const QString &what) {
	const auto plan = PlanExpirySample(window, reading);
	Check(
		!plan.accepted()
			&& !plan.untilCutoffMs
			&& (plan.refusal == expected)
			&& plan.details.contains(RefusalText(expected)),
		u"expiry window self-test: "_q
			+ what
			+ u" refuses "_q
			+ ExpiryRefusalName(expected)
			+ u" with no delay"_q,
		PlanText(plan));
}

// A watch that must stay Refused by |expected| with no window, before and
// after a valid poll: nothing is left to wait on.
void CheckRefusedWatch(
		SelfTest &state,
		ExpiryBoundaryWatch &watch,
		ExpiryRefusal expected,
		const QString &what) {
	const auto constructed = WatchRefusedEmpty(watch, expected);
	const auto before = watch.details();
	const auto returned = watch.poll(BaseReading(Live(state.a), kFreezeMs));
	const auto polled = (returned == ExpiryWatchState::Refused)
		&& WatchRefusedEmpty(watch, expected);
	Check(
		constructed && polled,
		u"expiry window self-test: "_q
			+ what
			+ u" is refused "_q
			+ ExpiryRefusalName(expected)
			+ u" with nothing to wait on, before and after a poll"_q,
		u"constructed=[%1] returned=%2 polled=[%3]"_q.arg(
			before,
			StateText(returned),
			watch.details()));
}

void CheckPoll(ExpiryBoundaryWatch &watch, const PollRow &row) {
	const auto returned = watch.poll(row.reading);
	Check(
		(returned == row.state)
			&& (watch.state() == row.state)
			&& (watch.refusal() == row.refusal)
			&& (watch.polls() == row.polls)
			&& (watch.confirmations() == row.confirmations)
			&& (watch.untilBoundaryMs() == row.untilBoundaryMs),
		u"expiry window self-test: "_q + row.name,
		u"returned=%1 expected: state=%2 refusal=%3 polls=%4 "
		u"confirmations=%5 untilBoundaryMs=%6 watch=[%7]"_q.arg(
			StateText(returned),
			StateText(row.state),
			ExpiryRefusalName(row.refusal),
			QString::number(row.polls),
			QString::number(row.confirmations),
			NumberText(row.untilBoundaryMs),
			watch.details()));
}

[[nodiscard]] std::vector<RefusalNameRow> RefusalNameRows() {
	return {
		{ ExpiryRefusal::None, u"none"_q },
		{ ExpiryRefusal::NotEvaluated, u"not-evaluated"_q },
		{ ExpiryRefusal::OwnerNotLive, u"owner-not-live"_q },
		{ ExpiryRefusal::WrongOwner, u"wrong-owner"_q },
		{ ExpiryRefusal::GenerationAbsent, u"generation-absent"_q },
		{ ExpiryRefusal::ObsoleteGeneration, u"obsolete-generation"_q },
		{ ExpiryRefusal::LinkAbsent, u"link-absent"_q },
		{ ExpiryRefusal::WrongLink, u"wrong-link"_q },
		{ ExpiryRefusal::ServedExpiryAbsent, u"served-expiry-absent"_q },
		{
			ExpiryRefusal::ServedExpiryNotPositive,
			u"served-expiry-not-positive"_q,
		},
		{ ExpiryRefusal::ReceiptAbsent, u"receipt-absent"_q },
		{ ExpiryRefusal::ReceiptNotPositive, u"receipt-not-positive"_q },
		{ ExpiryRefusal::ProductExpiryAbsent, u"product-expiry-absent"_q },
		{
			ExpiryRefusal::ProductExpiryNotPositive,
			u"product-expiry-not-positive"_q,
		},
		{ ExpiryRefusal::ProductExpiryDiffers, u"product-expiry-differs"_q },
		{ ExpiryRefusal::ClockAbsent, u"clock-absent"_q },
		{ ExpiryRefusal::ClockNotPositive, u"clock-not-positive"_q },
		{ ExpiryRefusal::CutoffNotAhead, u"cutoff-not-ahead"_q },
		{ ExpiryRefusal::BoundaryUnconfirmed, u"boundary-unconfirmed"_q },
	};
}

void CheckRefusalNames() {
	const auto rows = RefusalNameRows();
	auto names = QStringList();
	auto mismatches = QStringList();
	auto empty = 0;
	for (const auto &row : rows) {
		const auto name = ExpiryRefusalName(row.refusal);
		if (name.isEmpty()) {
			++empty;
		}
		if (name != row.name) {
			mismatches.push_back(row.name + u"->"_q + name);
		}
		names.push_back(name);
	}
	auto unique = names;
	const auto duplicates = unique.removeDuplicates();
	Check(
		(int(rows.size()) == kRefusalCount)
			&& !empty
			&& !duplicates
			&& mismatches.isEmpty(),
		u"expiry window self-test: every refusal has its own documented "
		u"non-empty name"_q,
		u"count=%1 empty=%2 duplicates=%3 mismatches=[%4] names=[%5]"_q.arg(
			QString::number(rows.size()),
			QString::number(empty),
			QString::number(duplicates),
			mismatches.join(u","_q),
			names.join(u","_q)));
}

void CheckDefaults(SelfTest &state) {
	const auto freeze = ExpiryFreeze();
	Check(
		!freeze.frozen()
			&& !freeze.window
			&& (freeze.refusal == ExpiryRefusal::NotEvaluated),
		u"expiry window self-test: a default freeze is not frozen and reads "
		u"not-evaluated"_q,
		FreezeText(freeze));
	const auto plan = ExpiryPlan();
	Check(
		!plan.accepted()
			&& !plan.untilCutoffMs
			&& (plan.refusal == ExpiryRefusal::NotEvaluated),
		u"expiry window self-test: a default plan is not accepted, has no "
		u"delay and reads not-evaluated"_q,
		PlanText(plan));
	CheckRefusedFreeze(
		FreezeExpiryWindow(ShareArgs(), ExpiryObservation(), ExpiryReading()),
		ExpiryRefusal::OwnerNotLive,
		u"an empty observation and an empty reading"_q);
	auto watch = ExpiryBoundaryWatch(freeze, kOffset);
	CheckRefusedWatch(
		state,
		watch,
		ExpiryRefusal::NotEvaluated,
		u"a watch from a default freeze"_q);
	const auto unfilled = ExpiryFreeze{ .refusal = ExpiryRefusal::None };
	auto unfilledWatch = ExpiryBoundaryWatch(unfilled, kOffset);
	CheckRefusedWatch(
		state,
		unfilledWatch,
		ExpiryRefusal::NotEvaluated,
		u"a watch from a freeze reading none without a window"_q);
}

void SelfArm(SelfTest &state) {
	state.a.emplace();
	state.b.emplace();
	state.other.emplace();
	const auto a = Live(state.a);
	const auto b = Live(state.b);
	const auto other = Live(state.other);
	const auto live = a && b && other;
	const auto distinct = live && (a != b) && (a != other) && (b != other);
	Check(
		live && distinct,
		u"expiry window self-test: fixture gate: owners built"_q,
		u"a=%1 b=%2 other=%3 distinct=%4"_q.arg(
			FlagText(a != nullptr),
			FlagText(b != nullptr),
			FlagText(other != nullptr),
			FlagText(distinct)));
	CheckRefusalNames();
	CheckDefaults(state);
}

void CheckPositiveFields(const SelfTest &state, const ExpiryWindow &window) {
	const auto rows = std::vector<FieldRow>{
		{ u"label"_q, window.label(), u"share"_q },
		{ u"owner"_q, OwnerName(state, window.owner()), u"a"_q },
		{
			u"generation"_q,
			QString::number(window.generation()),
			u"1"_q,
		},
		{ u"link"_q, LinkName(window.link()), u"a"_q },
		{
			u"served expiry"_q,
			QString::number(window.servedExpires()),
			QString::number(kServed),
		},
		{
			u"receipt"_q,
			QString::number(window.receiptServerMs()),
			QString::number(kReceiptMs),
		},
		{
			u"receipt fraction"_q,
			QString::number(window.receiptServerMs() % 1000),
			QString::number(kReceiptFractionMs),
		},
		{
			u"freeze clock"_q,
			QString::number(window.frozenServerMs()),
			QString::number(kFreezeMs),
		},
		{
			u"cutoff lead"_q,
			QString::number(window.cutoffLead()),
			QString::number(kLead),
		},
		{
			u"cutoff"_q,
			QString::number(window.cutoffServerMs()),
			QString::number(kCutoffMs),
		},
	};
	for (const auto &row : rows) {
		Check(
			row.actual == row.expected,
			u"expiry window self-test: the frozen window keeps its "_q
				+ row.name,
			u"actual=%1 expected=%2"_q.arg(row.actual, row.expected));
	}
}

void CheckPositiveWatch(SelfTest &state, const ExpiryFreeze &freeze) {
	const auto a = Live(state.a);
	auto watch = ExpiryBoundaryWatch(freeze, kOffset);
	const auto rows = std::vector<PollRow>{
		{
			.name = u"the positive watch confirms at the freeze clock"_q,
			.reading = BaseReading(a, kFreezeMs),
			.polls = 1,
			.confirmations = 1,
			.untilBoundaryMs = kBoundaryMs - kFreezeMs,
		},
		{
			.name = u"the positive watch confirms 1 ms before the cutoff"_q,
			.reading = BaseReading(a, kCutoffMs - 1),
			.polls = 2,
			.confirmations = 2,
			.untilBoundaryMs = kOffset + 1,
		},
		{
			.name = u"the positive watch waits at the cutoff without "
				u"confirming"_q,
			.reading = BaseReading(a, kCutoffMs),
			.polls = 3,
			.confirmations = 2,
			.untilBoundaryMs = kOffset,
		},
		{
			.name = u"the positive watch reaches at its boundary"_q,
			.reading = BaseReading(a, kBoundaryMs),
			.state = ExpiryWatchState::Reached,
			.polls = 4,
			.confirmations = 2,
		},
	};
	for (const auto &row : rows) {
		CheckPoll(watch, row);
	}
	const auto details = watch.details();
	Check(
		(watch.window() == freeze.window)
			&& (watch.boundaryServerMs() == kBoundaryMs)
			&& (watch.reachedServerMs() == kBoundaryMs)
			&& details.contains(u" lateMs=0 "_q),
		u"expiry window self-test: the positive watch reached exactly at "
		u"the cutoff plus its offset"_q,
		details);
}

[[nodiscard]] std::vector<SeparationRow> SeparationRows() {
	return {
		{
			.name = u"a product expiry other than the served one"_q,
			.served = kServed,
			.product = kServed + 1,
			.expected = ExpiryRefusal::ProductExpiryDiffers,
			.servedText = QString::number(kServed),
			.productText = QString::number(kServed + 1),
		},
		{
			.name = u"an absent served expiry beside a product expiry"_q,
			.served = std::nullopt,
			.product = kServed,
			.expected = ExpiryRefusal::ServedExpiryAbsent,
			.servedText = u"none"_q,
			.productText = QString::number(kServed),
		},
		{
			.name = u"an absent product expiry beside a served expiry"_q,
			.served = kServed,
			.product = std::nullopt,
			.expected = ExpiryRefusal::ProductExpiryAbsent,
			.servedText = QString::number(kServed),
			.productText = u"none"_q,
		},
	};
}

// The product and the served values come from two inputs, and each one is
// printed as its own value.
void CheckSeparation(SelfTest &state) {
	const auto a = Live(state.a);
	for (const auto &row : SeparationRows()) {
		auto observation = BaseObservation(a);
		observation.servedExpires = row.served;
		auto reading = BaseReading(a, kFreezeMs);
		reading.productExpires = row.product;
		const auto freeze = FreezeExpiryWindow(
			ShareArgs(),
			observation,
			reading);
		const auto served = u" served=%1 "_q.arg(row.servedText);
		const auto product = u" product=%1 "_q.arg(row.productText);
		Check(
			FreezeRefusedBy(freeze, row.expected)
				&& freeze.details.contains(served)
				&& freeze.details.contains(product),
			u"expiry window self-test: separation control: "_q
				+ row.name
				+ u" refuses "_q
				+ ExpiryRefusalName(row.expected),
			FreezeText(freeze));
	}
}

void SelfPositive(SelfTest &state) {
	const auto a = Live(state.a);
	const auto freeze = FreezeExpiryWindow(
		ShareArgs(),
		BaseObservation(a),
		BaseReading(a, kFreezeMs));
	Check(
		freeze.frozen()
			&& (freeze.refusal == ExpiryRefusal::None)
			&& freeze.details.contains(RefusalText(ExpiryRefusal::None))
			&& freeze.details.contains(u" link=same "_q)
			&& !freeze.details.contains(kLinkA),
		u"expiry window self-test: a current positive observation freezes "
		u"a window and prints no link text"_q,
		FreezeText(freeze));
	if (freeze.frozen()) {
		CheckPositiveFields(state, *freeze.window);
		CheckAcceptedPlan(
			*freeze.window,
			BaseReading(a, kFreezeMs),
			kCutoffMs - kFreezeMs,
			u"the positive window at the freeze clock"_q);
		CheckPositiveWatch(state, freeze);
	}
	CheckSeparation(state);
}

[[nodiscard]] std::vector<FreezeRow> IdentityFreezeRows(QObject *b) {
	return {
		{
			.name = u"the observation owner null"_q,
			.mutate = [](FreezeInput &input) {
				input.observation.owner = nullptr;
			},
			.expected = ExpiryRefusal::OwnerNotLive,
		},
		{
			.name = u"both owners null"_q,
			.mutate = [](FreezeInput &input) {
				input.observation.owner = nullptr;
				input.current.owner = nullptr;
			},
			.expected = ExpiryRefusal::OwnerNotLive,
		},
		{
			.name = u"the current owner b"_q,
			.mutate = [=](FreezeInput &input) {
				input.current.owner = b;
			},
			.expected = ExpiryRefusal::WrongOwner,
		},
		{
			.name = u"the current owner null"_q,
			.mutate = [](FreezeInput &input) {
				input.current.owner = nullptr;
			},
			.expected = ExpiryRefusal::WrongOwner,
		},
		{
			.name = u"the observation generation absent"_q,
			.mutate = [](FreezeInput &input) {
				input.observation.generation = std::nullopt;
			},
			.expected = ExpiryRefusal::GenerationAbsent,
		},
		{
			.name = u"the current generation absent"_q,
			.mutate = [](FreezeInput &input) {
				input.current.generation = std::nullopt;
			},
			.expected = ExpiryRefusal::GenerationAbsent,
		},
		{
			.name = u"both generations absent"_q,
			.mutate = [](FreezeInput &input) {
				input.observation.generation = std::nullopt;
				input.current.generation = std::nullopt;
			},
			.expected = ExpiryRefusal::GenerationAbsent,
		},
		{
			.name = u"the current generation 2"_q,
			.mutate = [](FreezeInput &input) {
				input.current.generation = uint64(2);
			},
			.expected = ExpiryRefusal::ObsoleteGeneration,
		},
		{
			.name = u"the observation link empty"_q,
			.mutate = [](FreezeInput &input) {
				input.observation.link = QString();
			},
			.expected = ExpiryRefusal::LinkAbsent,
		},
		{
			.name = u"the current link empty"_q,
			.mutate = [](FreezeInput &input) {
				input.current.link = QString();
			},
			.expected = ExpiryRefusal::LinkAbsent,
		},
		{
			.name = u"both links empty"_q,
			.mutate = [](FreezeInput &input) {
				input.observation.link = QString();
				input.current.link = QString();
			},
			.expected = ExpiryRefusal::LinkAbsent,
		},
		{
			.name = u"the current link b"_q,
			.mutate = [](FreezeInput &input) {
				input.current.link = kLinkB;
			},
			.expected = ExpiryRefusal::WrongLink,
		},
	};
}

[[nodiscard]] std::vector<FreezeRow> ValueFreezeRows() {
	return {
		{
			.name = u"the served expiry absent"_q,
			.mutate = [](FreezeInput &input) {
				input.observation.servedExpires = std::nullopt;
			},
			.expected = ExpiryRefusal::ServedExpiryAbsent,
		},
		{
			.name = u"the served expiry 0"_q,
			.mutate = [](FreezeInput &input) {
				input.observation.servedExpires = TimeId(0);
			},
			.expected = ExpiryRefusal::ServedExpiryNotPositive,
		},
		{
			.name = u"the served expiry -1"_q,
			.mutate = [](FreezeInput &input) {
				input.observation.servedExpires = TimeId(-1);
			},
			.expected = ExpiryRefusal::ServedExpiryNotPositive,
		},
		{
			.name = u"the receipt absent"_q,
			.mutate = [](FreezeInput &input) {
				input.observation.receipt = ServerClockReading();
			},
			.expected = ExpiryRefusal::ReceiptAbsent,
		},
		{
			.name = u"the receipt 0"_q,
			.mutate = [](FreezeInput &input) {
				input.observation.receipt = { .serverMs = int64(0) };
			},
			.expected = ExpiryRefusal::ReceiptNotPositive,
		},
		{
			.name = u"the product expiry absent"_q,
			.mutate = [](FreezeInput &input) {
				input.current.productExpires = std::nullopt;
			},
			.expected = ExpiryRefusal::ProductExpiryAbsent,
		},
		{
			.name = u"the product expiry 0"_q,
			.mutate = [](FreezeInput &input) {
				input.current.productExpires = TimeId(0);
			},
			.expected = ExpiryRefusal::ProductExpiryNotPositive,
		},
		{
			.name = u"the product expiry one second past the served one"_q,
			.mutate = [](FreezeInput &input) {
				input.current.productExpires = TimeId(kServed + 1);
			},
			.expected = ExpiryRefusal::ProductExpiryDiffers,
		},
		{
			.name = u"the served and product expiries both absent"_q,
			.mutate = [](FreezeInput &input) {
				input.observation.servedExpires = std::nullopt;
				input.current.productExpires = std::nullopt;
			},
			.expected = ExpiryRefusal::ServedExpiryAbsent,
		},
		{
			.name = u"the served and product expiries both 0"_q,
			.mutate = [](FreezeInput &input) {
				input.observation.servedExpires = TimeId(0);
				input.current.productExpires = TimeId(0);
			},
			.expected = ExpiryRefusal::ServedExpiryNotPositive,
		},
	};
}

[[nodiscard]] FreezeRow ClockRow(
		const QString &name,
		int64 clock,
		crl::time minimumAhead,
		ExpiryRefusal expected) {
	return {
		.name = name,
		.mutate = [=](FreezeInput &input) {
			input.args.minimumAhead = minimumAhead;
			input.current.clock = { .serverMs = clock };
		},
		.expected = expected,
	};
}

[[nodiscard]] std::vector<FreezeRow> ClockFreezeRows() {
	return {
		{
			.name = u"the clock absent"_q,
			.mutate = [](FreezeInput &input) {
				input.current.clock = ServerClockReading();
			},
			.expected = ExpiryRefusal::ClockAbsent,
		},
		{
			.name = u"the clock 0"_q,
			.mutate = [](FreezeInput &input) {
				input.current.clock = { .serverMs = int64(0) };
			},
			.expected = ExpiryRefusal::ClockNotPositive,
		},
		{
			.name = u"the receipt and the clock both 0"_q,
			.mutate = [](FreezeInput &input) {
				input.observation.receipt = { .serverMs = int64(0) };
				input.current.clock = { .serverMs = int64(0) };
			},
			.expected = ExpiryRefusal::ReceiptNotPositive,
		},
		ClockRow(
			u"the clock 1 ms inside minimumAhead"_q,
			kCutoffMs - kAhead + 1,
			kAhead,
			ExpiryRefusal::CutoffNotAhead),
		ClockRow(
			u"the clock at the cutoff"_q,
			kCutoffMs,
			kAhead,
			ExpiryRefusal::CutoffNotAhead),
		ClockRow(
			u"the clock 1 ms past the cutoff"_q,
			kCutoffMs + 1,
			kAhead,
			ExpiryRefusal::CutoffNotAhead),
		ClockRow(
			u"minimumAhead 0 and the clock at the cutoff"_q,
			kCutoffMs,
			crl::time(0),
			ExpiryRefusal::CutoffNotAhead),
		ClockRow(
			u"the clock exactly minimumAhead before the cutoff"_q,
			kCutoffMs - kAhead,
			kAhead,
			ExpiryRefusal::None),
		ClockRow(
			u"minimumAhead 0 and the clock 1 ms before the cutoff"_q,
			kCutoffMs - 1,
			crl::time(0),
			ExpiryRefusal::None),
	};
}

[[nodiscard]] std::vector<FreezeRow> FreezeRows(SelfTest &state) {
	auto result = IdentityFreezeRows(Live(state.b));
	for (auto rows : { ValueFreezeRows(), ClockFreezeRows() }) {
		for (auto &row : rows) {
			result.push_back(std::move(row));
		}
	}
	return result;
}

// The same valid poll that leaves a refused watch unchanged confirms a
// watch from a frozen control row.
void CheckFreezeControl(
		SelfTest &state,
		const FreezeRow &row,
		const ExpiryFreeze &freeze) {
	Check(
		freeze.frozen()
			&& (freeze.refusal == ExpiryRefusal::None)
			&& freeze.details.contains(RefusalText(ExpiryRefusal::None)),
		u"expiry window self-test: control: a freeze with "_q
			+ row.name
			+ u" is frozen"_q,
		FreezeText(freeze));
	if (!freeze.frozen()) {
		return;
	}
	auto watch = ExpiryBoundaryWatch(freeze, kOffset);
	Check(
		(watch.state() == ExpiryWatchState::Waiting)
			&& (watch.refusal() == ExpiryRefusal::None)
			&& (watch.window() == freeze.window)
			&& (watch.boundaryServerMs() == kBoundaryMs)
			&& !watch.untilBoundaryMs()
			&& (watch.polls() == 0),
		u"expiry window self-test: control: a watch from the freeze with "_q
			+ row.name
			+ u" waits on its window"_q,
		watch.details());
	CheckPoll(watch, {
		.name = u"control: the watch from the freeze with "_q
			+ row.name
			+ u" confirms a valid poll"_q,
		.reading = BaseReading(Live(state.a), kFreezeMs),
		.polls = 1,
		.confirmations = 1,
		.untilBoundaryMs = kBoundaryMs - kFreezeMs,
	});
}

void CheckFreezeRow(SelfTest &state, const FreezeRow &row) {
	const auto a = Live(state.a);
	auto input = FreezeInput{
		.args = ShareArgs(),
		.observation = BaseObservation(a),
		.current = BaseReading(a, kFreezeMs),
	};
	row.mutate(input);
	const auto freeze = FreezeExpiryWindow(
		input.args,
		input.observation,
		input.current);
	if (row.expected == ExpiryRefusal::None) {
		CheckFreezeControl(state, row, freeze);
		return;
	}
	CheckRefusedFreeze(freeze, row.expected, u"a freeze with "_q + row.name);
	auto watch = ExpiryBoundaryWatch(freeze, kOffset);
	CheckRefusedWatch(
		state,
		watch,
		row.expected,
		u"a watch from the freeze with "_q + row.name);
}

void SelfFreezeRefusals(SelfTest &state) {
	for (const auto &row : FreezeRows(state)) {
		CheckFreezeRow(state, row);
	}
}

[[nodiscard]] std::vector<PlanRow> PlanRows(QObject *b) {
	return {
		{
			.name = u"the current owner b"_q,
			.mutate = [=](ExpiryReading &reading) { reading.owner = b; },
			.expected = ExpiryRefusal::WrongOwner,
		},
		{
			.name = u"the current owner null"_q,
			.mutate = [](ExpiryReading &reading) { reading.owner = nullptr; },
			.expected = ExpiryRefusal::WrongOwner,
		},
		{
			.name = u"the current generation absent"_q,
			.mutate = [](ExpiryReading &reading) {
				reading.generation = std::nullopt;
			},
			.expected = ExpiryRefusal::GenerationAbsent,
		},
		{
			.name = u"the current generation 2"_q,
			.mutate = [](ExpiryReading &reading) {
				reading.generation = uint64(2);
			},
			.expected = ExpiryRefusal::ObsoleteGeneration,
		},
		{
			.name = u"the current link empty"_q,
			.mutate = [](ExpiryReading &reading) { reading.link = QString(); },
			.expected = ExpiryRefusal::LinkAbsent,
		},
		{
			.name = u"the current link b"_q,
			.mutate = [](ExpiryReading &reading) { reading.link = kLinkB; },
			.expected = ExpiryRefusal::WrongLink,
		},
		{
			.name = u"the product expiry absent"_q,
			.mutate = [](ExpiryReading &reading) {
				reading.productExpires = std::nullopt;
			},
			.expected = ExpiryRefusal::ProductExpiryAbsent,
		},
		{
			.name = u"the product expiry 0"_q,
			.mutate = [](ExpiryReading &reading) {
				reading.productExpires = TimeId(0);
			},
			.expected = ExpiryRefusal::ProductExpiryNotPositive,
		},
		{
			.name = u"the product expiry one second past the served one"_q,
			.mutate = [](ExpiryReading &reading) {
				reading.productExpires = TimeId(kServed + 1);
			},
			.expected = ExpiryRefusal::ProductExpiryDiffers,
		},
		{
			.name = u"the clock absent"_q,
			.mutate = [](ExpiryReading &reading) {
				reading.clock = ServerClockReading();
			},
			.expected = ExpiryRefusal::ClockAbsent,
		},
		{
			.name = u"the clock 0"_q,
			.mutate = [](ExpiryReading &reading) {
				reading.clock = { .serverMs = int64(0) };
			},
			.expected = ExpiryRefusal::ClockNotPositive,
		},
	};
}

// A refusing reading leaves no delay and a watch Refused on its first poll
// and final after it, even when a later poll would reach the boundary.
void CheckPlanRow(
		SelfTest &state,
		const ExpiryFreeze &freeze,
		const PlanRow &row) {
	const auto a = Live(state.a);
	auto reading = BaseReading(a, kFreezeMs);
	row.mutate(reading);
	const auto name = ExpiryRefusalName(row.expected);
	CheckRefusedPlan(
		*freeze.window,
		reading,
		row.expected,
		u"a plan with "_q + row.name);
	auto watch = ExpiryBoundaryWatch(freeze, kOffset);
	CheckPoll(watch, {
		.name = u"a watch's first poll with "_q
			+ row.name
			+ u" refuses "_q
			+ name,
		.reading = reading,
		.state = ExpiryWatchState::Refused,
		.refusal = row.expected,
		.polls = 1,
	});
	CheckPoll(watch, {
		.name = u"a watch refused with "_q
			+ row.name
			+ u" stays refused at its boundary"_q,
		.reading = BaseReading(a, kBoundaryMs),
		.state = ExpiryWatchState::Refused,
		.refusal = row.expected,
		.polls = 1,
	});
}

void CheckPlanControl(SelfTest &state, const ExpiryFreeze &freeze) {
	const auto a = Live(state.a);
	CheckAcceptedPlan(
		*freeze.window,
		BaseReading(a, kFreezeMs),
		kCutoffMs - kFreezeMs,
		u"control: the unchanged reading"_q);
	auto watch = ExpiryBoundaryWatch(freeze, kOffset);
	CheckPoll(watch, {
		.name = u"control: a watch's first poll with the unchanged reading "
			u"confirms"_q,
		.reading = BaseReading(a, kFreezeMs),
		.polls = 1,
		.confirmations = 1,
		.untilBoundaryMs = kBoundaryMs - kFreezeMs,
	});
	CheckPoll(watch, {
		.name = u"control: the confirmed watch reaches at its boundary"_q,
		.reading = BaseReading(a, kBoundaryMs),
		.state = ExpiryWatchState::Reached,
		.polls = 2,
		.confirmations = 1,
	});
}

void SelfPlanRefusals(SelfTest &state) {
	const auto a = Live(state.a);
	const auto freeze = FreezeExpiryWindow(
		ShareArgs(),
		BaseObservation(a),
		BaseReading(a, kFreezeMs));
	Check(
		freeze.frozen(),
		u"expiry window self-test: fixture gate: the plan refusals' window "
		u"is frozen"_q,
		FreezeText(freeze));
	if (!freeze.frozen()) {
		return;
	}
	CheckPlanControl(state, freeze);
	for (const auto &row : PlanRows(Live(state.b))) {
		CheckPlanRow(state, freeze, row);
	}
}

[[nodiscard]] ExpiryFreeze FreezeShare(QObject *owner) {
	return FreezeExpiryWindow(
		ShareArgs(),
		BaseObservation(owner),
		BaseReading(owner, kFreezeMs));
}

[[nodiscard]] bool CheckFrozenGate(
		const ExpiryFreeze &freeze,
		const QString &what) {
	Check(
		freeze.frozen(),
		u"expiry window self-test: fixture gate: "_q + what + u" is frozen"_q,
		FreezeText(freeze));
	return freeze.frozen();
}

[[nodiscard]] ExpiryReading ProductAbsentReading(QObject *owner, int64 clock) {
	auto result = BaseReading(owner, clock);
	result.productExpires = std::nullopt;
	return result;
}

// The product reacted at its own cutoff: it cleared its expiry, moved to
// another link and counted a new generation.
[[nodiscard]] ExpiryReading ReactionReading(QObject *owner, int64 clock) {
	auto result = ProductAbsentReading(owner, clock);
	result.generation = kNextGeneration;
	result.link = kLinkB;
	return result;
}

// Every timing watch below starts with one confirming poll at the freeze
// clock, so its later rows measure a confirmed premise.
void ConfirmAtFreeze(
		ExpiryBoundaryWatch &watch,
		QObject *owner,
		int64 boundaryMs,
		const QString &name) {
	CheckPoll(watch, {
		.name = name + u" confirms at the freeze clock"_q,
		.reading = BaseReading(owner, kFreezeMs),
		.polls = 1,
		.confirmations = 1,
		.untilBoundaryMs = boundaryMs - kFreezeMs,
	});
}

void CheckReachedAt(
		const ExpiryBoundaryWatch &watch,
		int64 reachedServerMs,
		int64 lateMs,
		const QString &what) {
	const auto details = watch.details();
	Check(
		(watch.state() == ExpiryWatchState::Reached)
			&& (watch.reachedServerMs() == reachedServerMs)
			&& details.contains(
				u" lateMs=%1 "_q.arg(QString::number(lateMs))),
		u"expiry window self-test: "_q + what,
		details);
}

[[nodiscard]] WindowSpec ShareSpec(QObject *owner) {
	return {
		.label = u"share"_q,
		.link = kLinkA,
		.owner = owner,
		.generation = uint64(1),
		.receiptMs = kReceiptMs,
		.frozenMs = kFreezeMs,
		.cutoffMs = kCutoffMs,
		.lead = kLead,
		.served = kServed,
	};
}

[[nodiscard]] SimultaneousSpecs MakeSimultaneousSpecs(SelfTest &state) {
	const auto a = Live(state.a);
	const auto b = Live(state.b);
	return {
		.share = ShareSpec(a),
		.natural = {
			.label = u"natural"_q,
			.link = kLinkA,
			.owner = a,
			.generation = uint64(1),
			.receiptMs = kReceiptMs,
			.frozenMs = kFreezeMs,
			.cutoffMs = kNaturalCutoffMs,
			.lead = crl::time(0),
			.served = kServed,
		},
		.render = {
			.label = u"render"_q,
			.link = kLinkB,
			.owner = b,
			.generation = kRenderGeneration,
			.receiptMs = kReceiptMs,
			.frozenMs = kFreezeMs,
			.cutoffMs = kRenderCutoffMs,
			.lead = crl::time(0),
			.served = kRenderServed,
		},
		.refreshed = {
			.label = u"render2"_q,
			.link = kLinkB,
			.owner = b,
			.generation = kRefreshGeneration,
			.receiptMs = kRefreshReceiptMs,
			.frozenMs = kRefreshMs,
			.cutoffMs = kRefreshCutoffMs,
			.lead = crl::time(0),
			.served = kRefreshServed,
		},
	};
}

[[nodiscard]] ExpiryObservation SpecObservation(const WindowSpec &spec) {
	return {
		.owner = spec.owner,
		.generation = spec.generation,
		.link = spec.link,
		.servedExpires = spec.served,
		.receipt = { .serverMs = spec.receiptMs },
	};
}

[[nodiscard]] ExpiryReading SpecReading(const WindowSpec &spec, int64 clock) {
	return {
		.owner = spec.owner,
		.generation = spec.generation,
		.link = spec.link,
		.productExpires = spec.served,
		.clock = { .serverMs = clock },
	};
}

[[nodiscard]] ExpiryFreeze FreezeSpec(
		const WindowSpec &spec,
		const ExpiryObservation &observation) {
	return FreezeExpiryWindow(
		{
			.label = spec.label,
			.cutoffLead = spec.lead,
			.minimumAhead = kAhead,
		},
		observation,
		SpecReading(spec, spec.frozenMs));
}

[[nodiscard]] WindowSpec ReadSpec(const ExpiryWindow &window) {
	return {
		.label = window.label(),
		.link = window.link(),
		.owner = window.owner(),
		.generation = window.generation(),
		.receiptMs = window.receiptServerMs(),
		.frozenMs = window.frozenServerMs(),
		.cutoffMs = window.cutoffServerMs(),
		.lead = window.cutoffLead(),
		.served = window.servedExpires(),
	};
}

[[nodiscard]] QString SpecText(const SelfTest &state, const WindowSpec &spec) {
	return u"label=%1 owner=%2 generation=%3 link=%4 served=%5 "
		u"receiptServerMs=%6 frozenServerMs=%7 cutoffLead=%8 "
		u"cutoffServerMs=%9"_q.arg(
			spec.label,
			OwnerName(state, spec.owner),
			QString::number(spec.generation),
			LinkName(spec.link),
			QString::number(spec.served),
			QString::number(spec.receiptMs),
			QString::number(spec.frozenMs),
			QString::number(spec.lead),
			QString::number(spec.cutoffMs));
}

void CheckSnapshots(
		const SelfTest &state,
		const std::vector<SnapshotRow> &rows,
		const QString &when) {
	for (const auto &row : rows) {
		const auto actual = SpecText(state, ReadSpec(*row.window));
		const auto expected = SpecText(state, *row.spec);
		Check(
			actual == expected,
			u"expiry window self-test: the "_q
				+ row.spec->label
				+ u" window reads its own identity and cutoff "_q
				+ when,
			u"actual=[%1] expected=[%2]"_q.arg(actual, expected));
	}
}

void CheckDistinctCutoffs(const std::vector<SnapshotRow> &rows) {
	auto cutoffs = QStringList();
	auto equalPairs = 0;
	for (auto i = 0; i != int(rows.size()); ++i) {
		const auto cutoff = rows[i].window->cutoffServerMs();
		cutoffs.push_back(rows[i].spec->label
			+ u"="_q
			+ QString::number(cutoff));
		for (auto j = i + 1; j != int(rows.size()); ++j) {
			if (rows[j].window->cutoffServerMs() == cutoff) {
				++equalPairs;
			}
		}
	}
	Check(
		!equalPairs,
		u"expiry window self-test: the coexisting windows have pairwise "
		u"distinct cutoffs"_q,
		u"equalPairs=%1 cutoffs=[%2]"_q.arg(
			QString::number(equalPairs),
			cutoffs.join(u" "_q)));
}

[[nodiscard]] std::vector<InterleavedRow> InterleavedRows(
		const SimultaneousSpecs &specs) {
	const auto share = [&](int64 clock) {
		return SpecReading(specs.share, clock);
	};
	const auto render = [&](int64 clock) {
		return SpecReading(specs.refreshed, clock);
	};
	return {
		{
			.share = {
				.name = u"interleaved: the share watch confirms after the "
					u"refresh"_q,
				.reading = share(kRefreshMs),
				.polls = 1,
				.confirmations = 1,
				.untilBoundaryMs = kBoundaryMs - kRefreshMs,
			},
			.render = {
				.name = u"interleaved: the render watch confirms after the "
					u"refresh"_q,
				.reading = render(kRefreshMs),
				.polls = 1,
				.confirmations = 1,
				.untilBoundaryMs = kRefreshBoundaryMs - kRefreshMs,
			},
		},
		{
			.share = {
				.name = u"interleaved: the share watch confirms 1 ms before "
					u"its cutoff"_q,
				.reading = share(kCutoffMs - 1),
				.polls = 2,
				.confirmations = 2,
				.untilBoundaryMs = kOffset + 1,
			},
			.render = {
				.name = u"interleaved: the render watch confirms 1 ms before "
					u"the share cutoff"_q,
				.reading = render(kCutoffMs - 1),
				.polls = 2,
				.confirmations = 2,
				.untilBoundaryMs = kRefreshBoundaryMs - (kCutoffMs - 1),
			},
		},
		{
			.share = {
				.name = u"interleaved: the share watch reaches at its own "
					u"boundary"_q,
				.reading = share(kBoundaryMs),
				.state = ExpiryWatchState::Reached,
				.polls = 3,
				.confirmations = 2,
			},
			.render = {
				.name = u"interleaved: the render watch keeps waiting at the "
					u"share boundary"_q,
				.reading = render(kBoundaryMs),
				.polls = 3,
				.confirmations = 3,
				.untilBoundaryMs = kRefreshBoundaryMs - kBoundaryMs,
			},
		},
		{
			.share = {
				.name = u"interleaved: the reached share watch ignores a "
					u"poll 1 ms before the render cutoff"_q,
				.reading = share(kRefreshCutoffMs - 1),
				.state = ExpiryWatchState::Reached,
				.polls = 3,
				.confirmations = 2,
			},
			.render = {
				.name = u"interleaved: the render watch confirms 1 ms before "
					u"its cutoff"_q,
				.reading = render(kRefreshCutoffMs - 1),
				.polls = 4,
				.confirmations = 4,
				.untilBoundaryMs = kOffset + 1,
			},
		},
		{
			.share = {
				.name = u"interleaved: the reached share watch ignores a "
					u"poll at the render boundary"_q,
				.reading = share(kRefreshBoundaryMs),
				.state = ExpiryWatchState::Reached,
				.polls = 3,
				.confirmations = 2,
			},
			.render = {
				.name = u"interleaved: the render watch reaches at its own "
					u"boundary"_q,
				.reading = render(kRefreshBoundaryMs),
				.state = ExpiryWatchState::Reached,
				.polls = 5,
				.confirmations = 4,
			},
		},
	};
}

void CheckInterleaved(
		const SimultaneousSpecs &specs,
		const ExpiryFreeze &share,
		const ExpiryFreeze &refreshed) {
	auto shareWatch = ExpiryBoundaryWatch(share, kOffset);
	auto renderWatch = ExpiryBoundaryWatch(refreshed, kOffset);
	for (const auto &row : InterleavedRows(specs)) {
		CheckPoll(shareWatch, row.share);
		CheckPoll(renderWatch, row.render);
	}
	Check(
		(shareWatch.reachedServerMs() == kBoundaryMs)
			&& (renderWatch.reachedServerMs() == kRefreshBoundaryMs)
			&& (shareWatch.reachedServerMs() != kRefreshBoundaryMs)
			&& (renderWatch.reachedServerMs() != kBoundaryMs),
		u"expiry window self-test: interleaved watches each reach only at "
		u"their own boundary"_q,
		u"share=[%1] render=[%2]"_q.arg(
			shareWatch.details(),
			renderWatch.details()));
}

void SelfSimultaneous(SelfTest &state) {
	const auto specs = MakeSimultaneousSpecs(state);
	auto renderSource = SpecObservation(specs.render);
	const auto share = FreezeSpec(specs.share, SpecObservation(specs.share));
	const auto natural = FreezeSpec(
		specs.natural,
		SpecObservation(specs.natural));
	const auto render = FreezeSpec(specs.render, renderSource);
	const auto frozen = share.frozen() && natural.frozen() && render.frozen();
	Check(
		frozen,
		u"expiry window self-test: fixture gate: the share, natural and "
		u"render windows are frozen"_q,
		u"share=[%1] natural=[%2] render=[%3]"_q.arg(
			FreezeText(share),
			FreezeText(natural),
			FreezeText(render)));
	if (!frozen) {
		return;
	}
	auto rows = std::vector<SnapshotRow>{
		{ &specs.share, share.window.get() },
		{ &specs.natural, natural.window.get() },
		{ &specs.render, render.window.get() },
	};
	CheckSnapshots(state, rows, u"before the refresh"_q);
	CheckAcceptedPlan(
		*share.window,
		SpecReading(specs.share, kFreezeMs),
		kCutoffMs - kFreezeMs,
		u"the share window before the refresh"_q);

	// The render source is refreshed in place, as a reply hook would do.
	renderSource = SpecObservation(specs.refreshed);
	const auto refreshed = FreezeSpec(specs.refreshed, renderSource);
	if (!CheckFrozenGate(refreshed, u"the refreshed render window"_q)) {
		return;
	}
	rows.push_back({ &specs.refreshed, refreshed.window.get() });
	CheckSnapshots(state, rows, u"after the refresh"_q);
	CheckDistinctCutoffs(rows);
	const auto current = SpecReading(specs.refreshed, kRefreshMs);
	CheckRefusedPlan(
		*render.window,
		current,
		ExpiryRefusal::ObsoleteGeneration,
		u"the old render window read at the refreshed generation"_q);
	CheckAcceptedPlan(
		*refreshed.window,
		current,
		kRefreshCutoffMs - kRefreshMs,
		u"the refreshed render window"_q);
	CheckAcceptedPlan(
		*share.window,
		SpecReading(specs.share, kFreezeMs),
		kCutoffMs - kFreezeMs,
		u"the share window after the refresh"_q);
	CheckInterleaved(specs, share, refreshed);
}

void CheckReplacedWatches(
		ExpiryBoundaryWatch &before,
		ExpiryBoundaryWatch &inside,
		QObject *replacement) {
	CheckPoll(before, {
		.name = u"replacement: the watch polled before the cutoff refuses "
			u"owner-not-live"_q,
		.reading = BaseReading(replacement, kLaterMs),
		.state = ExpiryWatchState::Refused,
		.refusal = ExpiryRefusal::OwnerNotLive,
		.polls = 2,
		.confirmations = 1,
	});
	CheckPoll(inside, {
		.name = u"replacement: the watch polled inside the reaction window "
			u"refuses owner-not-live"_q,
		.reading = BaseReading(replacement, kCutoffMs + 10),
		.state = ExpiryWatchState::Refused,
		.refusal = ExpiryRefusal::OwnerNotLive,
		.polls = 2,
		.confirmations = 1,
	});
	CheckPoll(before, {
		.name = u"replacement: the watch refused before the cutoff stays "
			u"refused at its boundary"_q,
		.reading = BaseReading(replacement, kBoundaryMs),
		.state = ExpiryWatchState::Refused,
		.refusal = ExpiryRefusal::OwnerNotLive,
		.polls = 2,
		.confirmations = 1,
	});
	CheckPoll(inside, {
		.name = u"replacement: the watch refused inside the reaction window "
			u"stays refused at its boundary"_q,
		.reading = BaseReading(replacement, kBoundaryMs),
		.state = ExpiryWatchState::Refused,
		.refusal = ExpiryRefusal::OwnerNotLive,
		.polls = 2,
		.confirmations = 1,
	});
}

// The owner is destroyed and rebuilt in the same storage, so a raw pointer
// would accept the new object; only the QPointer identity refuses it.
void CheckSameAddressReplacement(SelfTest &state) {
	state.slot.emplace();
	const auto address = Live(state.slot);
	const auto observation = BaseObservation(address);
	const auto freeze = FreezeExpiryWindow(
		ShareArgs(),
		observation,
		BaseReading(address, kFreezeMs));
	if (!CheckFrozenGate(freeze, u"the slot window"_q)) {
		return;
	}
	const auto &window = *freeze.window;
	auto before = ExpiryBoundaryWatch(freeze, kOffset);
	auto inside = ExpiryBoundaryWatch(freeze, kOffset);
	ConfirmAtFreeze(
		before,
		address,
		kBoundaryMs,
		u"replacement: the watch polled before the cutoff"_q);
	ConfirmAtFreeze(
		inside,
		address,
		kBoundaryMs,
		u"replacement: the watch polled inside the reaction window"_q);
	const auto bound = (window.owner() == address);
	state.slot.reset();
	state.slot.emplace();
	const auto replacement = Live(state.slot);
	const auto sameAddress = (replacement == address);
	Check(
		bound && sameAddress && !window.owner() && !observation.owner,
		u"expiry window self-test: control: the replacement owner lives at "
		u"the destroyed owner's address"_q,
		u"boundBefore=%1 sameAddress=%2 windowOwner=%3 "
		u"observationOwner=%4"_q.arg(
			FlagText(bound),
			FlagText(sameAddress),
			OwnerName(state, window.owner()),
			OwnerName(state, observation.owner.data())));
	CheckRefusedPlan(
		window,
		BaseReading(replacement, kLaterMs),
		ExpiryRefusal::OwnerNotLive,
		u"the destroyed owner's window read with its same-address "
		u"replacement"_q);
	CheckRefusedFreeze(
		FreezeExpiryWindow(
			ShareArgs(),
			observation,
			BaseReading(replacement, kLaterMs)),
		ExpiryRefusal::OwnerNotLive,
		u"a re-freeze from the destroyed owner's observation"_q);
	CheckReplacedWatches(before, inside, replacement);
}

void CheckLiveReplacement(SelfTest &state) {
	const auto a = Live(state.a);
	const auto freeze = FreezeShare(a);
	if (!CheckFrozenGate(freeze, u"the live replacement window"_q)) {
		return;
	}
	const auto &window = *freeze.window;
	CheckRefusedPlan(
		window,
		BaseReading(Live(state.other), kLaterMs),
		ExpiryRefusal::WrongOwner,
		u"a window on a read with the live owner other"_q);
	auto observation = BaseObservation(a);
	observation.generation = kNextGeneration;
	auto reading = BaseReading(a, kLaterMs);
	reading.generation = kNextGeneration;
	CheckRefusedPlan(
		window,
		reading,
		ExpiryRefusal::ObsoleteGeneration,
		u"a generation 1 window read at generation 2"_q);
	const auto renewed = FreezeExpiryWindow(ShareArgs(), observation, reading);
	Check(
		renewed.frozen()
			&& (renewed.window != freeze.window)
			&& (renewed.window->generation() == kNextGeneration)
			&& (window.generation() == uint64(1)),
		u"expiry window self-test: a generation 2 observation freezes a new "
		u"window while the old one keeps generation 1"_q,
		u"old=[%1] new=[%2]"_q.arg(
			SpecText(state, ReadSpec(window)),
			FreezeText(renewed)));
	if (renewed.frozen()) {
		CheckAcceptedPlan(
			*renewed.window,
			reading,
			kCutoffMs - kLaterMs,
			u"the generation 2 window at generation 2"_q);
	}
}

void SelfReplacement(SelfTest &state) {
	CheckSameAddressReplacement(state);
	CheckLiveReplacement(state);
}

// Before the cutoff every poll validates the full premise; from it on the
// owner stays bound.
void CheckBoundaryValidation(SelfTest &state, const ExpiryFreeze &freeze) {
	const auto a = Live(state.a);
	auto validated = ExpiryBoundaryWatch(freeze, kOffset);
	ConfirmAtFreeze(
		validated,
		a,
		kBoundaryMs,
		u"boundary: the validated watch"_q);
	CheckPoll(validated, {
		.name = u"boundary: an absent product expiry 1 ms before the cutoff "
			u"refuses product-expiry-absent"_q,
		.reading = ProductAbsentReading(a, kCutoffMs - 1),
		.state = ExpiryWatchState::Refused,
		.refusal = ExpiryRefusal::ProductExpiryAbsent,
		.polls = 2,
		.confirmations = 1,
	});
	auto bound = ExpiryBoundaryWatch(freeze, kOffset);
	ConfirmAtFreeze(bound, a, kBoundaryMs, u"boundary: the bound watch"_q);
	CheckPoll(bound, {
		.name = u"boundary: another owner inside the reaction window "
			u"refuses wrong-owner"_q,
		.reading = BaseReading(Live(state.other), kCutoffMs + 10),
		.state = ExpiryWatchState::Refused,
		.refusal = ExpiryRefusal::WrongOwner,
		.polls = 2,
		.confirmations = 1,
	});
}

// From the cutoff on, the product's own reaction is the measured subject.
void CheckReactionWindow(SelfTest &state, const ExpiryFreeze &freeze) {
	const auto a = Live(state.a);
	auto watch = ExpiryBoundaryWatch(freeze, kOffset);
	ConfirmAtFreeze(watch, a, kBoundaryMs, u"boundary: the reacting watch"_q);
	const auto rows = std::vector<PollRow>{
		{
			.name = u"boundary: the product's reaction at the cutoff "
				u"waits"_q,
			.reading = ReactionReading(a, kCutoffMs),
			.polls = 2,
			.confirmations = 1,
			.untilBoundaryMs = kOffset,
		},
		{
			.name = u"boundary: the product's reaction 1 ms before the "
				u"boundary waits"_q,
			.reading = ReactionReading(a, kBoundaryMs - 1),
			.polls = 3,
			.confirmations = 1,
			.untilBoundaryMs = int64(1),
		},
		{
			.name = u"boundary: the product's reaction at the boundary "
				u"reaches"_q,
			.reading = ReactionReading(a, kBoundaryMs),
			.state = ExpiryWatchState::Reached,
			.polls = 4,
			.confirmations = 1,
		},
	};
	for (const auto &row : rows) {
		CheckPoll(watch, row);
	}
	CheckReachedAt(
		watch,
		kBoundaryMs,
		0,
		u"boundary: the reacting watch reached exactly at its boundary"_q);
	auto later = BaseReading(a, kBoundaryMs + kLateMs);
	later.clock = ServerClockReading();
	CheckPoll(watch, {
		.name = u"boundary: a poll with an absent clock after Reached "
			u"changes nothing"_q,
		.reading = later,
		.state = ExpiryWatchState::Reached,
		.polls = 4,
		.confirmations = 1,
	});
	CheckReachedAt(
		watch,
		kBoundaryMs,
		0,
		u"boundary: the reached watch keeps its reached reading"_q);
}

void CheckUnconfirmed(SelfTest &state, const ExpiryFreeze &freeze) {
	const auto a = Live(state.a);
	const auto rows = std::vector<PollRow>{
		{
			.name = u"boundary: a watch first polled at the cutoff refuses "
				u"boundary-unconfirmed"_q,
			.reading = BaseReading(a, kCutoffMs),
			.state = ExpiryWatchState::Refused,
			.refusal = ExpiryRefusal::BoundaryUnconfirmed,
			.polls = 1,
		},
		{
			.name = u"boundary: a watch first polled past the boundary "
				u"refuses boundary-unconfirmed"_q,
			.reading = BaseReading(a, kBoundaryMs + 10),
			.state = ExpiryWatchState::Refused,
			.refusal = ExpiryRefusal::BoundaryUnconfirmed,
			.polls = 1,
		},
	};
	for (const auto &row : rows) {
		auto watch = ExpiryBoundaryWatch(freeze, kOffset);
		CheckPoll(watch, row);
	}
}

void CheckLateReach(SelfTest &state, const ExpiryFreeze &freeze) {
	const auto a = Live(state.a);
	auto watch = ExpiryBoundaryWatch(freeze, kOffset);
	ConfirmAtFreeze(watch, a, kBoundaryMs, u"boundary: the late watch"_q);
	CheckPoll(watch, {
		.name = u"boundary: a poll 2500 ms past the boundary reaches"_q,
		.reading = BaseReading(a, kBoundaryMs + kLateMs),
		.state = ExpiryWatchState::Reached,
		.polls = 2,
		.confirmations = 1,
	});
	CheckReachedAt(
		watch,
		kBoundaryMs + kLateMs,
		kLateMs,
		u"boundary: the late watch reports its lateness"_q);
	const auto details = watch.details();
	Check(
		details.contains(u" confirmedServerMs=%1 "_q.arg(
			QString::number(kFreezeMs))),
		u"expiry window self-test: boundary: the late watch keeps the "
		u"freeze reading as its confirmation"_q,
		details);
}

// A negative offset samples before the cutoff, and the full premise is
// validated up to that earlier boundary.
void CheckEarlyBoundary(SelfTest &state, const ExpiryFreeze &freeze) {
	const auto a = Live(state.a);
	auto watch = ExpiryBoundaryWatch(freeze, kEarlyOffset);
	ConfirmAtFreeze(
		watch,
		a,
		kEarlyBoundaryMs,
		u"boundary: the early watch"_q);
	CheckPoll(watch, {
		.name = u"boundary: the early watch confirms 1 ms before its "
			u"boundary"_q,
		.reading = BaseReading(a, kEarlyBoundaryMs - 1),
		.polls = 2,
		.confirmations = 2,
		.untilBoundaryMs = int64(1),
	});
	CheckPoll(watch, {
		.name = u"boundary: the early watch reaches 500 ms before the "
			u"cutoff"_q,
		.reading = BaseReading(a, kEarlyBoundaryMs),
		.state = ExpiryWatchState::Reached,
		.polls = 3,
		.confirmations = 2,
	});
	CheckReachedAt(
		watch,
		kEarlyBoundaryMs,
		0,
		u"boundary: the early watch reached exactly at its boundary"_q);
	auto validated = ExpiryBoundaryWatch(freeze, kEarlyOffset);
	ConfirmAtFreeze(
		validated,
		a,
		kEarlyBoundaryMs,
		u"boundary: the validated early watch"_q);
	CheckPoll(validated, {
		.name = u"boundary: an absent product expiry 1 ms before the early "
			u"boundary refuses product-expiry-absent"_q,
		.reading = ProductAbsentReading(a, kEarlyBoundaryMs - 1),
		.state = ExpiryWatchState::Refused,
		.refusal = ExpiryRefusal::ProductExpiryAbsent,
		.polls = 2,
		.confirmations = 1,
	});
}

void SelfBoundary(SelfTest &state) {
	const auto freeze = FreezeShare(Live(state.a));
	if (!CheckFrozenGate(freeze, u"the boundary window"_q)) {
		return;
	}
	CheckBoundaryValidation(state, freeze);
	CheckReactionWindow(state, freeze);
	CheckUnconfirmed(state, freeze);
	CheckLateReach(state, freeze);
	CheckEarlyBoundary(state, freeze);
}

void CheckResyncRecalculation(SelfTest &state, const ExpiryFreeze &freeze) {
	const auto a = Live(state.a);
	auto watch = ExpiryBoundaryWatch(freeze, kOffset);
	const auto rows = std::vector<PollRow>{
		{
			.name = u"resynchronization: the first poll at the freeze "
				u"clock"_q,
			.reading = BaseReading(a, kFreezeMs),
			.polls = 1,
			.confirmations = 1,
			.untilBoundaryMs = kBoundaryMs - kFreezeMs,
		},
		{
			.name = u"resynchronization: a forward jump of 5 s recomputes "
				u"the remaining time"_q,
			.reading = BaseReading(a, kForwardMs),
			.polls = 2,
			.confirmations = 2,
			.untilBoundaryMs = kBoundaryMs - kForwardMs,
		},
		{
			.name = u"resynchronization: a backward jump of 3 s recomputes "
				u"the remaining time"_q,
			.reading = BaseReading(a, kBackwardMs),
			.polls = 3,
			.confirmations = 3,
			.untilBoundaryMs = kBoundaryMs - kBackwardMs,
		},
	};
	for (const auto &row : rows) {
		CheckPoll(watch, row);
	}
	const auto &window = *freeze.window;
	const auto actual = SpecText(state, ReadSpec(window));
	const auto expected = SpecText(state, ShareSpec(a));
	Check(
		(actual == expected) && (watch.boundaryServerMs() == kBoundaryMs),
		u"expiry window self-test: resynchronization keeps the window's "
		u"served expiry, link, generation and cutoff"_q,
		u"actual=[%1] expected=[%2] watch=[%3]"_q.arg(
			actual,
			expected,
			watch.details()));
	const auto plan = PlanExpirySample(window, BaseReading(a, kClockUpdateMs));
	const auto positive = u" served=%1 product=%1 "_q.arg(kServed);
	Check(
		plan.accepted()
			&& (plan.untilCutoffMs == kCutoffMs - kClockUpdateMs)
			&& plan.details.contains(positive),
		u"expiry window self-test: resynchronization: the clock-update "
		u"check is an accepted plan whose product expiry is the positive "
		u"served one"_q,
		PlanText(plan));
}

[[nodiscard]] std::vector<PlanRow> ResyncRows() {
	return {
		{
			.name = u"an absent clock after the jump"_q,
			.mutate = [](ExpiryReading &reading) {
				reading.clock = ServerClockReading();
			},
			.expected = ExpiryRefusal::ClockAbsent,
		},
		{
			.name = u"a zero clock after the jump"_q,
			.mutate = [](ExpiryReading &reading) {
				reading.clock = { .serverMs = int64(0) };
			},
			.expected = ExpiryRefusal::ClockNotPositive,
		},
		{
			.name = u"link b after the jump"_q,
			.mutate = [](ExpiryReading &reading) { reading.link = kLinkB; },
			.expected = ExpiryRefusal::WrongLink,
		},
		{
			.name = u"a product expiry rebased on the shifted clock"_q,
			.mutate = [](ExpiryReading &reading) {
				reading.productExpires = TimeId(kServed - 1);
			},
			.expected = ExpiryRefusal::ProductExpiryDiffers,
		},
	};
}

void CheckResyncRow(
		SelfTest &state,
		const ExpiryFreeze &freeze,
		const PlanRow &row) {
	const auto a = Live(state.a);
	auto watch = ExpiryBoundaryWatch(freeze, kOffset);
	ConfirmAtFreeze(
		watch,
		a,
		kBoundaryMs,
		u"resynchronization: the watch for "_q + row.name);
	auto reading = BaseReading(a, kForwardMs);
	row.mutate(reading);
	CheckPoll(watch, {
		.name = u"resynchronization: "_q
			+ row.name
			+ u" refuses "_q
			+ ExpiryRefusalName(row.expected),
		.reading = reading,
		.state = ExpiryWatchState::Refused,
		.refusal = row.expected,
		.polls = 2,
		.confirmations = 1,
	});
}

// The whole-second countdown a product would display, never an oracle.
[[nodiscard]] std::optional<int64> DisplaySeconds(const ExpiryPlan &plan) {
	return plan.untilCutoffMs
		? std::make_optional((*plan.untilCutoffMs + 999) / 1000)
		: std::nullopt;
}

void CheckFractionalTime(SelfTest &state, const ExpiryFreeze &freeze) {
	const auto a = Live(state.a);
	const auto &window = *freeze.window;
	const auto firstReading = BaseReading(a, kDisplayMs);
	const auto secondReading = BaseReading(a, kDisplayMs + kFractionMs);
	auto watch = ExpiryBoundaryWatch(freeze, kOffset);
	watch.poll(firstReading);
	const auto first = watch.untilBoundaryMs();
	watch.poll(secondReading);
	const auto second = watch.untilBoundaryMs();
	const auto firstPlan = PlanExpirySample(window, firstReading);
	const auto secondPlan = PlanExpirySample(window, secondReading);
	const auto firstDisplay = DisplaySeconds(firstPlan);
	const auto secondDisplay = DisplaySeconds(secondPlan);
	const auto sameSecond = (kDisplayMs / 1000)
		== ((kDisplayMs + kFractionMs) / 1000);
	Check(
		sameSecond
			&& (first == kBoundaryMs - kDisplayMs)
			&& second
			&& (*first - *second == kFractionMs)
			&& (firstDisplay == kDisplaySeconds)
			&& (secondDisplay == kDisplaySeconds),
		u"expiry window self-test: resynchronization: two readings 300 ms "
		u"apart in one server second differ by exactly 300 ms while the "
		u"whole-second countdown display reads 7 for both"_q,
		u"sameSecond=%1 first: clockServerMs=%2 untilBoundaryMs=%3 "
		u"untilCutoffMs=%4 display=%5 second: clockServerMs=%6 "
		u"untilBoundaryMs=%7 untilCutoffMs=%8 display=%9 "
		u"watch=[%10]"_q.arg(
			FlagText(sameSecond),
			QString::number(kDisplayMs),
			NumberText(first),
			NumberText(firstPlan.untilCutoffMs),
			NumberText(firstDisplay),
			QString::number(kDisplayMs + kFractionMs),
			NumberText(second),
			NumberText(secondPlan.untilCutoffMs),
			NumberText(secondDisplay),
			watch.details()));
}

void SelfResync(SelfTest &state) {
	const auto freeze = FreezeShare(Live(state.a));
	if (!CheckFrozenGate(freeze, u"the resynchronization window"_q)) {
		return;
	}
	CheckResyncRecalculation(state, freeze);
	for (const auto &row : ResyncRows()) {
		CheckResyncRow(state, freeze, row);
	}
	CheckFractionalTime(state, freeze);
}

// Qt rounds to the nearest ms on Unix and reads a coarse tick on Windows,
// so its second can lead or trail time(nullptr)'s.
[[nodiscard]] std::vector<ComposeRow> ComposeRows() {
	const auto local = int64(kLocalSecond) * 1000;
	const auto server = int64(kLocalSecond + kShiftSeconds) * 1000;
	return {
		{
			.name = u"a mid-second Qt read"_q,
			.localMs = local + 437,
			.expected = server + 437,
		},
		{
			.name = u"a Qt read rounded up into the next second"_q,
			.localMs = local + 1000,
			.expected = server + 1000,
		},
		{
			.name = u"a Qt read a tick behind time(nullptr)"_q,
			.localMs = local - 10,
			.expected = server - 10,
		},
	};
}

void CheckServerClockComposition() {
	const auto shifted = TimeId(kLocalSecond + kShiftSeconds);
	for (const auto &row : ComposeRows()) {
		const auto serverMs = ComposeServerMs(
			row.localMs,
			kLocalSecond,
			shifted);
		Check(
			serverMs == row.expected,
			u"expiry window self-test: server clock composition: "_q
				+ row.name
				+ u" keeps its milliseconds and adds exactly the "
				u"whole-second shift"_q,
			u"localMs=%1 localSecond=%2 shiftedSecond=%3 serverMs=%4 "
			u"expected=%5"_q.arg(
				QString::number(row.localMs),
				QString::number(kLocalSecond),
				QString::number(shifted),
				QString::number(serverMs),
				QString::number(row.expected)));
	}
}

// The oracle's own shift: one time(nullptr) read between two equal
// base::unixtime::now() reads, so a second rollover cannot tear it.
[[nodiscard]] std::optional<int64> SettledShiftSeconds() {
	for (auto attempt = 0; attempt != kClockAttempts; ++attempt) {
		const auto before = base::unixtime::now();
		const auto local = TimeId(std::time(nullptr));
		if (base::unixtime::now() == before) {
			return int64(before) - local;
		}
	}
	return std::nullopt;
}

// The only real-clock read, and it only reads.
void SelfRealClock() {
	CheckServerClockComposition();
	const auto localBefore = QDateTime::currentMSecsSinceEpoch();
	const auto reading = ReadServerClock();
	const auto localAfter = QDateTime::currentMSecsSinceEpoch();
	const auto shift = SettledShiftSeconds();
	const auto lower = shift
		? std::make_optional(localBefore + *shift * 1000)
		: std::nullopt;
	const auto upper = shift
		? std::make_optional(localAfter + *shift * 1000)
		: std::nullopt;
	const auto fraction = reading.serverMs
		? std::make_optional(*reading.serverMs % 1000)
		: std::nullopt;
	Check(
		reading.serverMs
			&& (*reading.serverMs > 0)
			&& lower
			&& upper
			&& (*lower <= *reading.serverMs)
			&& (*reading.serverMs <= *upper),
		u"expiry window self-test: the real server clock is a positive "
		u"reading between the local millisecond reads around it, shifted "
		u"by an independently read whole-second shift"_q,
		u"localBeforeMs=%1 serverMs=%2 localAfterMs=%3 shift=%4 lowerMs=%5 "
		u"upperMs=%6 fractionMs=%7"_q.arg(
			QString::number(localBefore),
			NumberText(reading.serverMs),
			QString::number(localAfter),
			shift ? QString::number(*shift) : u"unsettled"_q,
			NumberText(lower),
			NumberText(upper),
			NumberText(fraction)));
}

void SelfRelease(SelfTest &state) {
	++state.releases;
	state.slot.reset();
	state.other.reset();
	state.b.reset();
	state.a.reset();
	Note(u"expiry window self-test: finish release ran count=%1"_q.arg(
		state.releases));
}

} // namespace

void AppendExpiryWindowSelfTest(not_null<Runner*> runner) {
	const auto state = std::make_shared<SelfTest>();
	runner->onFinish([=] { SelfRelease(*state); });
	const auto leg = [&](const QString &name, Fn<void()> then) {
		runner->add({
			.name = u"expiry window self-test: "_q + name,
			.then = std::move(then),
		});
	};
	leg(u"arm"_q, [=] { SelfArm(*state); });
	leg(u"positive observation"_q, [=] { SelfPositive(*state); });
	leg(u"freeze refusals"_q, [=] { SelfFreezeRefusals(*state); });
	leg(u"plan refusals"_q, [=] { SelfPlanRefusals(*state); });
	leg(u"simultaneous windows"_q, [=] { SelfSimultaneous(*state); });
	leg(u"destruction and replacement"_q, [=] { SelfReplacement(*state); });
	leg(u"boundary"_q, [=] { SelfBoundary(*state); });
	leg(u"resynchronization"_q, [=] { SelfResync(*state); });
	leg(u"real server clock"_q, SelfRealClock);
}

} // namespace Test

#endif // _DEBUG
