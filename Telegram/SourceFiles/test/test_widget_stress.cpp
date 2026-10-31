/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_widget_stress.h"

#include "core/version.h"
#include "test/test_widget_host.h"
#include "test/test_widget_stress_internal.h"

#include <QtCore/QByteArray>
#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QJsonValue>
#include <QtCore/QRect>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtCore/QSet>
#include <QtCore/QSettings>
#include <QtCore/QSysInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#endif // Q_OS_WIN

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Test::Stress {
namespace {

constexpr auto kPrefix = "widget stress: ";

// The run file's fixed names: run.schema.json's and CLIENTS.md's.
constexpr auto kPlatform = "desktop";
constexpr auto kClient = "Telegram Desktop";
constexpr auto kRenderer = "twidget-core";
constexpr auto kRendererKind = "own";
constexpr auto kDeviceClass = "desktop";
constexpr auto kFrameSource = "window-update-request";

// The `v` of every data-stress value the kit's widgets publish.
constexpr auto kStressVersion = 1;

// CLIENTS.md's measure table; the production pass overrides nothing and
// asks for the product's own strict configuration.
constexpr auto kMeasurePolicy = "compat";
constexpr auto kProductionPolicy = "strict";
constexpr auto kMeasureFeature = "webAnimations";
constexpr auto kMeasureCpuLimitMs = 500.;
constexpr auto kMeasureCpuQuotaMs = 10000.;
constexpr auto kMeasureCpuQuotaWindowMs = 1000.;
constexpr auto kMeasureMainThreadQuotaMs = 10000.;
constexpr auto kMeasureMemoryLimitBytes = size_t(64 * 1024 * 1024);
constexpr auto kMeasureMaxTimers = 256;

// What a launch must share with the run file it merges into, in the order
// a refusal names the first key that differs.
constexpr auto kMergeKeys = std::array{
	"kit",
	"platform",
	"client",
	"clientBuild",
	"framework",
	"renderer",
	"rendererKind",
	"engine",
	"device",
	"pass",
	"config",
};

constexpr auto kRunnerErrorPrefix = "runner: ";

// The dead a host gets when its engine is ended by the host's destruction,
// as a retired runtime's host is: no reason of the widget's own.
constexpr auto kTerminated = "terminated";

// ---------------------------------------------------------------------
// One level's arithmetic.

// A measuring window: the crl::profile() stamps of its two snapshots.
struct StressWindow {
	crl::profile_time start = 0;
	crl::profile_time end = 0;
};

struct StressFrames {
	std::optional<double> p50;
	std::optional<double> p95;
	std::optional<double> max;
	// The updates inside the window and the intervals between them.
	int updates = 0;
	int count = 0;
};

struct StressRenderer {
	// One per complete group: a frame() attempt's own time plus the message
	// and tick work since the attempt before it, ms, replayed or not.
	std::vector<double> samples;
	std::optional<double> msPerFrame;
	std::optional<double> p95Ms;
	std::optional<int64> displayListOps;
	// The first group in the window, discarded: its work began before.
	std::optional<double> partialMs;
	// Message and tick work after the window's last frame() attempt.
	double trailingMs = 0.;
	int trailingEvents = 0;
	// Events that finished inside the window, by kind, and those of
	// another attachment; the frame() attempts split by whether they
	// replayed the display list into an image.
	int frames = 0;
	int replayedFrames = 0;
	int unreplayedFrames = 0;
	int ticks = 0;
	int messages = 0;
	int foreign = 0;
	QStringList errors;
};

struct StressHost {
	std::optional<double> jsMsPerSec;
	std::optional<double> opsPerSec;
	std::optional<double> queriesPerSec;
	std::optional<double> queryMsPerSec;
	std::optional<int64> nodes;
	std::optional<int64> memoryBytes;
	std::optional<int64> throttledTasks;
	std::optional<int64> maxBatchOps;
	// How long before each snapshot its stats last changed.
	std::optional<double> startStatsAgeMs;
	std::optional<double> endStatsAgeMs;
	QStringList errors;
};

// One data-stress value that passed the checks.
struct StressValue {
	QJsonObject object;
	QString text;
	crl::profile_time receivedAt = 0;
	double t = 0.;
	double maxGap = 0.;
	int64 frames = 0;
	int64 jank = 0;
};

struct StressWidgetStats {
	std::optional<double> fps;
	std::optional<int64> frames;
	std::optional<double> maxGap;
	std::optional<int64> jank;
	std::optional<StressValue> last;
	QStringList errors;
	// Values received at or before the end, those inside the window, and
	// those received after the end, never used.
	int values = 0;
	int windowValues = 0;
	int late = 0;
};

struct StressMerge {
	QJsonObject file;
	QString error;
};

// ---------------------------------------------------------------------
// Small helpers.

[[nodiscard]] double Ms(crl::profile_time duration) {
	return duration / 1000.;
}

[[nodiscard]] QJsonValue Null() {
	return QJsonValue(QJsonValue::Null);
}

[[nodiscard]] QJsonValue Nullable(std::optional<double> value) {
	return value ? QJsonValue(*value) : Null();
}

[[nodiscard]] QJsonValue NullableInteger(std::optional<int64> value) {
	return value ? QJsonValue(qint64(*value)) : Null();
}

[[nodiscard]] QString OptionalText(std::optional<double> value) {
	return value ? QString::number(*value) : u"null"_q;
}

[[nodiscard]] QString Compact(const QJsonValue &value) {
	const auto bytes = QJsonDocument(
		QJsonArray{ value }).toJson(QJsonDocument::Compact);
	return QString::fromUtf8(bytes.mid(1, bytes.size() - 2));
}

[[nodiscard]] QJsonArray Strings(const std::vector<std::string> &list) {
	auto result = QJsonArray();
	for (const auto &entry : list) {
		result.push_back(QString::fromStdString(entry));
	}
	return result;
}

// JSON equality with every number compared by its value, whether Qt
// keeps it as an integer or a double.
[[nodiscard]] bool SameJson(const QJsonValue &a, const QJsonValue &b) {
	if (a.isDouble() && b.isDouble()) {
		return (a.toDouble() == b.toDouble());
	} else if (a.type() != b.type()) {
		return false;
	} else if (a.isObject()) {
		const auto x = a.toObject();
		const auto y = b.toObject();
		if (x.size() != y.size()) {
			return false;
		}
		for (auto i = x.begin(); i != x.end(); ++i) {
			if (!y.contains(i.key()) || !SameJson(i.value(), y[i.key()])) {
				return false;
			}
		}
		return true;
	} else if (a.isArray()) {
		const auto x = a.toArray();
		const auto y = b.toArray();
		if (x.size() != y.size()) {
			return false;
		}
		for (auto i = 0; i != x.size(); ++i) {
			if (!SameJson(x[i], y[i])) {
				return false;
			}
		}
		return true;
	}
	return (a == b);
}

// Nearest rank of |sorted|: v[min(n - 1, round(p * (n - 1)))].
[[nodiscard]] std::optional<double> NearestRank(
		const std::vector<double> &sorted,
		double p) {
	if (sorted.empty()) {
		return std::nullopt;
	}
	const auto last = int(sorted.size()) - 1;
	return sorted[std::min(last, int(std::lround(p * last)))];
}

[[nodiscard]] std::optional<double> Mean(const std::vector<double> &list) {
	if (list.empty()) {
		return std::nullopt;
	}
	auto sum = 0.;
	for (const auto value : list) {
		sum += value;
	}
	return sum / list.size();
}

// ---------------------------------------------------------------------
// The kit, read once while the stages are registered.

[[nodiscard]] QByteArray ReadBytes(const QString &path, QString *error) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		*error = file.errorString();
		return QByteArray();
	}
	return file.readAll();
}

// A whole number of at least |min| that fits an int, or nothing.
[[nodiscard]] std::optional<int> WholeNumber(
		const QJsonValue &value,
		int min) {
	if (!value.isDouble()) {
		return std::nullopt;
	}
	const auto number = value.toDouble();
	if (!std::isfinite(number)
		|| (std::floor(number) != number)
		|| (number < min)
		|| (number > std::numeric_limits<int>::max())) {
		return std::nullopt;
	}
	return int(number);
}

[[nodiscard]] std::optional<QStringList> StringList(const QJsonValue &value) {
	if (!value.isArray()) {
		return std::nullopt;
	}
	auto result = QStringList();
	for (const auto &entry : value.toArray()) {
		if (!entry.isString()) {
			return std::nullopt;
		}
		result.push_back(entry.toString());
	}
	return result;
}

[[nodiscard]] StressWidget ParseWidget(
		const QJsonValue &entry,
		int index,
		QSet<QString> &ids,
		QStringList &problems) {
	const auto object = entry.toObject();
	const auto level = WholeNumber(object.value(u"level"_q), 0);
	const auto number = object.value(u"value"_q);
	const auto crosses = StringList(object.value(u"crosses"_q));
	auto result = StressWidget{
		.id = object.value(u"id"_q).toString(),
		.load = object.value(u"load"_q).toString(),
		.file = object.value(u"file"_q).toString(),
		.crosses = crosses.value_or(QStringList()),
		.value = number.toDouble(),
		.level = level.value_or(-1),
		.index = index,
	};
	auto missing = QStringList();
	if (!entry.isObject()) {
		missing.push_back(u"to be an object"_q);
	}
	if (result.id.isEmpty()) {
		missing.push_back(u"an id"_q);
	} else if (ids.contains(result.id)) {
		problems.push_back(u"widget %1 [%2] repeats an id"_q.arg(
			QString::number(index),
			result.id));
	}
	ids.insert(result.id);
	if (result.load.isEmpty()) {
		missing.push_back(u"a load"_q);
	}
	if (result.file.isEmpty()) {
		missing.push_back(u"a file"_q);
	}
	if (!level) {
		missing.push_back(u"a whole level"_q);
	}
	if (!number.isDouble() || !std::isfinite(number.toDouble())) {
		missing.push_back(u"a numeric value"_q);
	}
	if (!crosses) {
		missing.push_back(u"crosses as a list of strings"_q);
	}
	if (!missing.isEmpty()) {
		problems.push_back(u"widget %1 [%2] needs %3"_q.arg(
			QString::number(index),
			result.id,
			missing.join(u", "_q)));
	}
	return result;
}

void ParseLoads(
		const QJsonValue &loads,
		StressManifest &manifest,
		QStringList &problems) {
	if (!loads.isArray() || loads.toArray().isEmpty()) {
		problems.push_back(u"loads is not a non-empty array"_q);
		return;
	}
	auto ids = QSet<QString>();
	for (const auto &entry : loads.toArray()) {
		const auto object = entry.toObject();
		const auto id = object.value(u"id"_q).toString();
		const auto list = StringList(object.value(u"widgets"_q));
		if (id.isEmpty() || !list) {
			problems.push_back(u"a load [%1] needs an id and widgets as a "
				u"list of ids"_q.arg(id));
			continue;
		} else if (ids.contains(id)) {
			problems.push_back(u"load [%1] is listed twice"_q.arg(id));
		}
		ids.insert(id);
		for (const auto &widget : *list) {
			const auto i = std::find_if(
				manifest.widgets.begin(),
				manifest.widgets.end(),
				[&](const StressWidget &known) { return known.id == widget; });
			if (i == manifest.widgets.end() || i->load != id) {
				problems.push_back(u"load [%1] lists [%2], which is no widget "
					u"of that load"_q.arg(id, widget));
			}
		}
		manifest.loads.push_back({ .id = id, .widgets = *list });
	}
	for (const auto &widget : manifest.widgets) {
		const auto i = std::find_if(
			manifest.loads.begin(),
			manifest.loads.end(),
			[&](const StressLoad &load) { return load.id == widget.load; });
		if (!widget.load.isEmpty()
			&& (i == manifest.loads.end()
				|| !i->widgets.contains(widget.id))) {
			problems.push_back(u"widget [%1] is not listed by its load "
				u"[%2]"_q.arg(widget.id, widget.load));
		}
	}
}

// manifest.json's version, run, widgets and loads; |error| names every
// problem found, joined.
[[nodiscard]] StressManifest ParseManifest(
		const QByteArray &bytes,
		QString *error) {
	auto result = StressManifest();
	auto parse = QJsonParseError();
	const auto document = QJsonDocument::fromJson(bytes, &parse);
	if (parse.error != QJsonParseError::NoError) {
		*error = u"not JSON: %1 at offset %2"_q.arg(
			parse.errorString(),
			QString::number(parse.offset));
		return result;
	} else if (!document.isObject()) {
		*error = u"not a JSON object"_q;
		return result;
	}
	const auto root = document.object();
	const auto run = root.value(u"run"_q).toObject();
	const auto version = WholeNumber(root.value(u"version"_q), 1);
	const auto warmup = WholeNumber(run.value(u"warmupMs"_q), 0);
	const auto measure = WholeNumber(run.value(u"measureMs"_q), 1);
	const auto width = WholeNumber(run.value(u"width"_q), 1);
	auto problems = QStringList();
	if (!version) {
		problems.push_back(u"version is not a positive whole number"_q);
	}
	if (!warmup || !measure || !width) {
		problems.push_back(u"run needs a whole warmupMs of at least 0 and a "
			u"whole measureMs and width of at least 1"_q);
	}
	result.version = version.value_or(0);
	result.warmupMs = warmup.value_or(0);
	result.measureMs = measure.value_or(0);
	result.width = width.value_or(0);
	const auto widgets = root.value(u"widgets"_q);
	if (!widgets.isArray() || widgets.toArray().isEmpty()) {
		problems.push_back(u"widgets is not a non-empty array"_q);
	}
	auto ids = QSet<QString>();
	for (const auto &entry : widgets.toArray()) {
		const auto index = int(result.widgets.size());
		result.widgets.push_back(ParseWidget(entry, index, ids, problems));
	}
	ParseLoads(root.value(u"loads"_q), result, problems);
	*error = problems.join(u"; "_q);
	return result;
}

// The requested loads, in manifest order, and their widgets; an empty
// request or an unknown load is refused by name.
[[nodiscard]] StressSelection SelectLoads(
		const StressManifest &manifest,
		const QStringList &requested,
		QString *error) {
	auto problems = QStringList();
	if (requested.isEmpty()) {
		problems.push_back(u"no load was requested"_q);
	}
	for (const auto &load : requested) {
		const auto known = std::any_of(
			manifest.loads.begin(),
			manifest.loads.end(),
			[&](const StressLoad &entry) { return entry.id == load; });
		if (!known) {
			problems.push_back(u"unknown load [%1]"_q.arg(load));
		}
	}
	if (!problems.isEmpty()) {
		*error = problems.join(u"; "_q);
		return StressSelection();
	}
	auto result = StressSelection();
	for (const auto &load : manifest.loads) {
		if (requested.contains(load.id)) {
			result.loads.push_back(load.id);
		}
	}
	for (const auto &widget : manifest.widgets) {
		if (result.loads.contains(widget.load)) {
			result.widgets.push_back(widget.index);
		}
	}
	return result;
}

// Reads the HTML of every selected widget; names each one that is
// unreadable or empty.
[[nodiscard]] QString ReadSelectedHtml(
		StressManifest &manifest,
		const StressSelection &selection,
		const QString &package) {
	auto problems = QStringList();
	for (const auto index : selection.widgets) {
		auto &widget = manifest.widgets[index];
		auto error = QString();
		widget.html = ReadBytes(QDir(package).filePath(widget.file), &error);
		if (!error.isEmpty()) {
			problems.push_back(u"widget [%1]: %2 is unreadable: %3"_q.arg(
				widget.id,
				widget.file,
				error));
		} else if (widget.html.isEmpty()) {
			problems.push_back(u"widget [%1]: %2 is empty"_q.arg(
				widget.id,
				widget.file));
		}
	}
	return problems.join(u"; "_q);
}

[[nodiscard]] QString ArgsError(const WidgetStressArgs &args) {
	auto problems = QStringList();
	const auto output = QFileInfo(args.output);
	if (args.output.isEmpty() || !output.isAbsolute()) {
		problems.push_back(
			u"the output [%1] is not an absolute path"_q.arg(args.output));
	} else if (output.isDir()) {
		problems.push_back(u"the output [%1] is a directory, not the pass's "
			u"run file"_q.arg(args.output));
	}
	if (args.framework.isEmpty()) {
		problems.push_back(u"no framework tag was given"_q);
	}
	if (args.engine.isEmpty()) {
		problems.push_back(u"no engine text was given"_q);
	}
	if (args.warmupMs && (*args.warmupMs < 0)) {
		problems.push_back(u"the warm-up is negative"_q);
	}
	if (args.measureMs && (*args.measureMs <= 0)) {
		problems.push_back(u"the measuring window is not positive"_q);
	}
	return problems.join(u"; "_q);
}

} // namespace

QString Prefixed(const QString &text) {
	return QString::fromLatin1(kPrefix) + text;
}

QString PassName(WidgetStressPass pass) {
	return (pass == WidgetStressPass::Measure)
		? u"measure"_q
		: u"production"_q;
}

// The arguments, manifest.json and the selected widgets' HTML; nothing
// else is read or changed.
StressPackage ReadStressPackage(const WidgetStressArgs &args) {
	auto result = StressPackage{ .error = ArgsError(args) };
	if (!result.error.isEmpty()) {
		return result;
	}
	const auto info = QFileInfo(args.package);
	if (args.package.isEmpty() || !info.isAbsolute() || !info.isDir()) {
		result.error = u"the package directory [%1] is not an absolute "
			u"directory"_q.arg(args.package);
		return result;
	}
	auto error = QString();
	const auto bytes = ReadBytes(
		QDir(args.package).filePath(u"manifest.json"_q),
		&error);
	if (error.isEmpty()) {
		result.manifest = ParseManifest(bytes, &error);
	}
	if (!error.isEmpty()) {
		result.error = u"manifest.json: "_q + error;
		return result;
	}
	result.selection = SelectLoads(result.manifest, args.loads, &error);
	if (error.isEmpty()) {
		error = ReadSelectedHtml(
			result.manifest,
			result.selection,
			args.package);
	}
	result.error = error;
	return result;
}

// What each level's watch overrides: CLIENTS.md's measure table, or
// nothing for the production pass.
WidgetHostOverride HostOverride(WidgetStressPass pass) {
	if (pass != WidgetStressPass::Measure) {
		return WidgetHostOverride();
	}
	return {
		.policy = std::string(kMeasurePolicy),
		.features = std::vector<std::string>{ std::string(kMeasureFeature) },
		.memoryLimitBytes = kMeasureMemoryLimitBytes,
		.cpuLimitMs = kMeasureCpuLimitMs,
		.cpuQuotaMs = kMeasureCpuQuotaMs,
		.cpuQuotaWindowMs = kMeasureCpuQuotaWindowMs,
		.mainThreadQuotaMs = kMeasureMainThreadQuotaMs,
		.maxTimers = kMeasureMaxTimers,
	};
}

namespace {

// ---------------------------------------------------------------------
// Frames: the exact window's update intervals.

[[nodiscard]] StressFrames ComputeFrames(
		std::vector<crl::profile_time> stamps,
		StressWindow window) {
	std::erase_if(stamps, [&](crl::profile_time stamp) {
		return (stamp < window.start) || (stamp > window.end);
	});
	std::sort(stamps.begin(), stamps.end());
	auto result = StressFrames{ .updates = int(stamps.size()) };
	if (stamps.size() < 2) {
		return result;
	}
	auto intervals = std::vector<double>();
	intervals.reserve(stamps.size() - 1);
	for (auto i = size_t(1); i != stamps.size(); ++i) {
		intervals.push_back(Ms(stamps[i] - stamps[i - 1]));
	}
	std::sort(intervals.begin(), intervals.end());
	result.count = int(intervals.size());
	result.p50 = NearestRank(intervals, 0.5);
	result.p95 = NearestRank(intervals, 0.95);
	result.max = intervals.back();
	return result;
}

// ---------------------------------------------------------------------
// Renderer: the measured core's work, grouped by frame() attempt.

[[nodiscard]] StressRenderer ComputeRenderer(
		const WidgetWorkReading &work,
		int host,
		StressWindow window) {
	auto result = StressRenderer();
	if (work.dropped > 0) {
		result.errors.push_back(u"renderer timing: %1 work scopes found the "
			u"level's log full at %2 events, so its timing is "
			u"incomplete"_q.arg(
				QString::number(work.dropped),
				QString::number(work.capacity)));
	}
	if (!work.reservedAt || (work.reservedAt > window.start)) {
		result.errors.push_back(u"renderer timing: the level's work log was "
			u"not reserved before its window started"_q);
	}
	if (work.events.empty()) {
		result.errors.push_back(u"renderer timing: no renderer work was "
			u"recorded since the level's reservation: the timing overlay "
			u"is missing or the core was never registered"_q);
	}
	if (!result.errors.isEmpty()) {
		return result;
	}
	auto pending = 0.;
	auto pendingEvents = 0;
	auto framed = false;
	auto lastList = int64(-1);
	for (const auto &event : work.events) {
		if (event.finished < window.start) {
			continue;
		} else if (event.finished > window.end) {
			break;
		} else if (event.host != host) {
			++result.foreign;
			continue;
		}
		const auto duration = Ms(event.finished - event.started);
		if (event.kind != WidgetWorkKind::Frame) {
			++((event.kind == WidgetWorkKind::Tick)
				? result.ticks
				: result.messages);
			pending += duration;
			++pendingEvents;
			continue;
		}
		++result.frames;
		++(event.replayed ? result.replayedFrames : result.unreplayedFrames);
		const auto group = pending + duration;
		pending = 0.;
		pendingEvents = 0;
		if (!framed) {
			framed = true;
			result.partialMs = group;
		} else {
			result.samples.push_back(group);
			lastList = event.displayList;
		}
	}
	result.trailingMs = pending;
	result.trailingEvents = pendingEvents;
	if (result.foreign > 0) {
		result.errors.push_back(u"renderer timing: %1 work events of another "
			u"host attachment finished inside the window"_q.arg(
				QString::number(result.foreign)));
		return result;
	}
	auto sorted = result.samples;
	std::sort(sorted.begin(), sorted.end());
	result.msPerFrame = Mean(sorted);
	result.p95Ms = NearestRank(sorted, 0.95);
	if (!result.samples.empty() && (lastList >= 0)) {
		result.displayListOps = lastList;
	}
	return result;
}

// ---------------------------------------------------------------------
// Host: the stats and the tap's counts at the window's two ends.

[[nodiscard]] std::optional<double> StatsAgeMs(
		const StressSnapshot &snapshot) {
	if (!snapshot.attached || !snapshot.host.statsChangedAt) {
		return std::nullopt;
	}
	return Ms(snapshot.at - snapshot.host.statsChangedAt);
}

[[nodiscard]] StressHost ComputeHost(
		const std::optional<StressSnapshot> &start,
		const StressSnapshot &end) {
	auto result = StressHost();
	if (!end.attached) {
		result.errors.push_back(
			u"host: no host attachment was read for this level"_q);
		return result;
	}
	result.nodes = end.host.nodes;
	if (end.host.statsChangedAt) {
		result.memoryBytes = int64(end.host.stats.memoryBytes);
	}
	if (end.generation > 0 && end.batches > 0) {
		result.maxBatchOps = end.maxBatchOps;
	}
	result.endStatsAgeMs = StatsAgeMs(end);
	if (!start) {
		return result;
	} else if (!start->attached) {
		result.errors.push_back(
			u"host: the window's start read no host attachment"_q);
		return result;
	}
	result.startStatsAgeMs = StatsAgeMs(*start);
	const auto seconds = (end.at - start->at) / 1e6;
	if (seconds <= 0.) {
		result.errors.push_back(u"host: the window's end snapshot is not "
			u"after its start"_q);
		return result;
	}
	const auto delta = [&](
			double from,
			double to,
			const QString &name) -> std::optional<double> {
		if (to < from) {
			result.errors.push_back(u"%1 went backwards between the "
				u"window's snapshots (%2 -> %3)"_q.arg(
					name,
					QString::number(from),
					QString::number(to)));
			return std::nullopt;
		}
		return to - from;
	};
	const auto rate = [&](double from, double to, const QString &name) {
		const auto value = delta(from, to, name);
		return value ? std::make_optional(*value / seconds) : std::nullopt;
	};
	if (!end.host.statsChangedAt) {
		result.errors.push_back(
			u"host: the host reported no stats by the window's end"_q);
	} else {
		const auto &from = start->host.stats;
		const auto &to = end.host.stats;
		result.jsMsPerSec = rate(from.jsMs, to.jsMs, u"host: stats.jsMs"_q);
		result.queriesPerSec = rate(
			from.queries,
			to.queries,
			u"host: stats.queries"_q);
		result.queryMsPerSec = rate(
			from.queryMs,
			to.queryMs,
			u"host: stats.queryMs"_q);
		const auto throttled = delta(
			from.throttledTasks,
			to.throttledTasks,
			u"host: stats.throttledTasks"_q);
		if (throttled) {
			result.throttledTasks = int64(std::llround(*throttled));
		}
	}
	if (!end.generation || (start->generation != end.generation)) {
		result.errors.push_back(u"ops: the window's ends read the ops of "
			u"host generations %1 and %2, not one host"_q.arg(
				QString::number(start->generation),
				QString::number(end.generation)));
		result.maxBatchOps = std::nullopt;
	} else {
		result.opsPerSec = rate(
			double(start->ops),
			double(end.ops),
			u"ops: the newest host's validated ops"_q);
	}
	return result;
}

// ---------------------------------------------------------------------
// Widget: its own data-stress values, clipped at the window's ends.

[[nodiscard]] std::optional<StressValue> ParseStressValue(
		const WidgetStressValue &value,
		const QString &id,
		QString *error) {
	auto parse = QJsonParseError();
	const auto bytes = QByteArray::fromStdString(value.value);
	const auto document = QJsonDocument::fromJson(bytes, &parse);
	if (parse.error != QJsonParseError::NoError || !document.isObject()) {
		*error = u"not a JSON object"_q;
		return std::nullopt;
	}
	const auto object = document.object();
	const auto number = [&](const QString &key) -> std::optional<double> {
		const auto field = object.value(key);
		const auto result = field.toDouble();
		return (field.isDouble() && std::isfinite(result) && (result >= 0.))
			? std::make_optional(result)
			: std::nullopt;
	};
	const auto count = [&](const QString &key) -> std::optional<int64> {
		const auto result = number(key);
		return (result && (std::floor(*result) == *result))
			? std::make_optional(int64(*result))
			: std::nullopt;
	};
	const auto version = number(u"v"_q);
	const auto owner = object.value(u"id"_q).toString();
	const auto t = number(u"t"_q);
	const auto maxGap = number(u"maxGap"_q);
	const auto frames = count(u"frames"_q);
	const auto jank = count(u"jank"_q);
	if (version != double(kStressVersion)) {
		*error = u"v is not %1"_q.arg(kStressVersion);
	} else if (owner != id) {
		*error = u"its id [%1] is not this entry's"_q.arg(owner);
	} else if (!t || !maxGap) {
		*error = u"t and maxGap are not non-negative numbers"_q;
	} else if (!frames || !jank) {
		*error = u"frames and jank are not non-negative whole numbers"_q;
	} else {
		return StressValue{
			.object = object,
			.text = QString::fromStdString(value.value),
			.receivedAt = value.receivedAt,
			.t = *t,
			.maxGap = *maxGap,
			.frames = *frames,
			.jank = *jank,
		};
	}
	return std::nullopt;
}

// Every value received at or before |end| must pass the checks and keep
// t, frames and jank from going backwards, or the widget's fields are all
// null with a named error. A measured level needs a value unless the
// widget died: the start one is the last at or before |start|, else the
// first after it, the end one the last at or before |end|.
[[nodiscard]] StressWidgetStats ComputeWidget(
		const std::vector<WidgetStressValue> &values,
		int dropped,
		const QString &id,
		std::optional<crl::profile_time> start,
		crl::profile_time end,
		bool dead) {
	auto result = StressWidgetStats();
	auto parsed = std::vector<StressValue>();
	for (auto i = 0; i != int(values.size()); ++i) {
		const auto &value = values[i];
		if (value.receivedAt > end) {
			++result.late;
			continue;
		}
		auto error = QString();
		const auto reading = ParseStressValue(value, id, &error);
		const auto previous = parsed.empty() ? nullptr : &parsed.back();
		if (reading
			&& previous
			&& ((reading->t < previous->t)
				|| (reading->frames < previous->frames)
				|| (reading->jank < previous->jank))) {
			error = u"t, frames or jank went backwards (t %1 -> %2, frames "
				u"%3 -> %4, jank %5 -> %6)"_q
				.arg(QString::number(previous->t))
				.arg(QString::number(reading->t))
				.arg(QString::number(previous->frames))
				.arg(QString::number(reading->frames))
				.arg(QString::number(previous->jank))
				.arg(QString::number(reading->jank));
		}
		if (!error.isEmpty()) {
			result.errors.push_back(u"widget: data-stress value %1: %2"_q.arg(
				QString::number(i + 1),
				error));
			return result;
		}
		parsed.push_back(*reading);
	}
	result.values = int(parsed.size());
	if (dropped > 0) {
		result.errors.push_back(u"widget: %1 data-stress values found the "
			u"tap's store full, so the values are incomplete"_q.arg(
				QString::number(dropped)));
		return result;
	} else if (!start) {
		if (!parsed.empty()) {
			result.last = parsed.back();
		}
		return result;
	} else if (parsed.empty()) {
		if (!dead) {
			result.errors.push_back(u"widget: no valid data-stress value was "
				u"received by the window's end"_q);
		}
		return result;
	}
	const StressValue *before = nullptr;
	const StressValue *firstInside = nullptr;
	for (const auto &value : parsed) {
		if (value.receivedAt <= *start) {
			before = &value;
			continue;
		}
		++result.windowValues;
		if (!firstInside) {
			firstInside = &value;
		}
		result.maxGap = std::max(result.maxGap.value_or(0.), value.maxGap);
	}
	if (!before) {
		before = firstInside;
	}
	const auto &after = parsed.back();
	const auto dt = after.t - before->t;
	result.frames = after.frames - before->frames;
	result.jank = after.jank - before->jank;
	if (dt > 0.) {
		result.fps = *result.frames * 1000. / dt;
	}
	result.last = after;
	return result;
}

// ---------------------------------------------------------------------
// Window: the worker's console-lock and covering-window samples.

// A latched in-window failure is a runner error, as one at an end is; so
// is a window two sampling periods long or more that no sample saw.
[[nodiscard]] QStringList CoverErrors(
		const StressCoverSamples &samples,
		StressWindow window) {
	if (!samples.failure.isEmpty()) {
		return { u"window: in-window sample %1, %2 ms after the start: %3"_q
			.arg(
				QString::number(samples.failureSample),
				QString::number(Ms(samples.failureAt - window.start)),
				samples.failure) };
	} else if (!samples.samples
		&& (window.end - window.start >= 2 * kObserveEvery * 1000)) {
		return { u"window: no console-lock or covering-window sample ran "
			u"inside the window"_q };
	}
	return {};
}

[[nodiscard]] QJsonObject CoverSamplesObject(
		const StressCoverSamples &samples,
		StressWindow window) {
	const auto failure = samples.failure.isEmpty()
		? Null()
		: QJsonValue(QJsonObject{
			{ u"sample"_q, samples.failureSample },
			{ u"offsetMs"_q, Ms(samples.failureAt - window.start) },
			{ u"details"_q, samples.failure },
		});
	return {
		{ u"thread"_q, u"harness worker"_q },
		{ u"samples"_q, samples.samples },
		{ u"sampleEveryMs"_q, qint64(kObserveEvery) },
		{ u"zOrder"_q, samples.zOrder },
		{ u"costMs"_q, Ms(samples.cost) },
		{ u"maxCostMs"_q, Ms(samples.maxCost) },
		{ u"joinMs"_q, Ms(samples.joinCost) },
		{ u"failure"_q, failure },
	};
}

// ---------------------------------------------------------------------
// The entry.

[[nodiscard]] QJsonObject FramesObject(const StressFrames &frames) {
	return {
		{ u"count"_q, frames.count },
		{ u"p50"_q, Nullable(frames.p50) },
		{ u"p95"_q, Nullable(frames.p95) },
		{ u"max"_q, Nullable(frames.max) },
		{ u"dropped"_q, Null() },
		{ u"longTaskMs"_q, Null() },
		{ u"mainThreadMsPerSec"_q, Null() },
	};
}

[[nodiscard]] QJsonObject RendererObject(const StressRenderer &renderer) {
	const auto valid = renderer.errors.isEmpty();
	return {
		{
			u"msPerFrame"_q,
			Nullable(valid ? renderer.msPerFrame : std::nullopt),
		},
		{ u"p95Ms"_q, Nullable(valid ? renderer.p95Ms : std::nullopt) },
		{
			u"displayListOps"_q,
			NullableInteger(valid ? renderer.displayListOps : std::nullopt),
		},
	};
}

[[nodiscard]] QJsonObject HostObject(const StressHost &host) {
	return {
		{ u"jsMsPerSec"_q, Nullable(host.jsMsPerSec) },
		{ u"opsPerSec"_q, Nullable(host.opsPerSec) },
		{ u"queriesPerSec"_q, Nullable(host.queriesPerSec) },
		{ u"queryMsPerSec"_q, Nullable(host.queryMsPerSec) },
		{ u"nodes"_q, NullableInteger(host.nodes) },
		{ u"memoryBytes"_q, NullableInteger(host.memoryBytes) },
		{ u"throttledTasks"_q, NullableInteger(host.throttledTasks) },
		{ u"maxBatchOps"_q, NullableInteger(host.maxBatchOps) },
		{ u"processMemoryMb"_q, Null() },
	};
}

[[nodiscard]] QJsonObject WidgetObject(const StressWidgetStats &widget) {
	return {
		{ u"fps"_q, Nullable(widget.fps) },
		{ u"frames"_q, NullableInteger(widget.frames) },
		{ u"maxGap"_q, Nullable(widget.maxGap) },
		{ u"jank"_q, NullableInteger(widget.jank) },
		{
			u"last"_q,
			widget.last ? QJsonValue(widget.last->object) : Null(),
		},
	};
}

[[nodiscard]] QJsonObject DesktopObject(
		const StressLevel &level,
		const StressFrames &frames,
		const StressRenderer &renderer,
		const StressHost &host,
		const StressWidgetStats &widget,
		int runnerErrors) {
	const auto measured = level.start.has_value();
	const auto window = measured
		? QJsonValue(Ms(level.end.at - level.start->at))
		: Null();
	const auto work = measured
		? QJsonValue(QJsonObject{
			{ u"samples"_q, int(renderer.samples.size()) },
			{ u"frames"_q, renderer.frames },
			{ u"replayedFrames"_q, renderer.replayedFrames },
			{ u"unreplayedFrames"_q, renderer.unreplayedFrames },
			{ u"ticks"_q, renderer.ticks },
			{ u"messages"_q, renderer.messages },
			{ u"foreign"_q, renderer.foreign },
			{ u"partialGroupMs"_q, Nullable(renderer.partialMs) },
			{ u"trailingWorkMs"_q, renderer.trailingMs },
			{ u"trailingWorkEvents"_q, renderer.trailingEvents },
		})
		: Null();
	auto result = level.desktop;
	auto checks = result.value(u"window"_q).toObject();
	checks.insert(u"coverAndLock"_q, measured
		? QJsonValue(CoverSamplesObject(
			level.coverAndLock,
			{ .start = level.start->at, .end = level.end.at }))
		: Null());
	result.insert(u"window"_q, checks);
	result.insert(u"measured"_q, measured);
	result.insert(u"windowMs"_q, window);
	result.insert(u"updates"_q, measured ? QJsonValue(frames.updates) : Null());
	result.insert(u"rendererWork"_q, work);
	result.insert(u"statsAgeMs"_q, QJsonObject{
		{ u"start"_q, Nullable(host.startStatsAgeMs) },
		{ u"end"_q, Nullable(host.endStatsAgeMs) },
	});
	result.insert(
		u"errorLogs"_q,
		level.end.attached ? level.end.host.errorLogs : 0);
	result.insert(u"values"_q, QJsonObject{
		{ u"received"_q, widget.values },
		{ u"inWindow"_q, widget.windowValues },
		{ u"late"_q, widget.late },
		{
			u"lastText"_q,
			widget.last ? QJsonValue(widget.last->text) : Null(),
		},
	});
	result.insert(u"runnerErrors"_q, runnerErrors);
	return result;
}

} // namespace

StressSnapshot MakeSnapshot(
		const WidgetHostReading &reading,
		int host,
		const WidgetOpsLatest &latest,
		crl::profile_time at) {
	auto result = StressSnapshot{
		.at = at,
		.ops = latest.ops,
		.batches = latest.batches,
		.maxBatchOps = latest.maxBatchOps,
		.generation = latest.generation,
	};
	if ((host >= 0) && (host < int(reading.hosts.size()))) {
		result.host = reading.hosts[host];
		result.attached = true;
	}
	return result;
}

// The host's creation in crl::profile() is about loadedAt - loadedMs: an
// estimate off by the load merge's lag, and |seenAt| lags the stop.
void ApplyDesktopStop(
		StressSnapshot &snapshot,
		const QString &reason,
		crl::profile_time seenAt) {
	auto &host = snapshot.host;
	if (reason.isEmpty() || (host.dead && (*host.dead != kTerminated))) {
		return;
	}
	host.dead = reason.toStdString();
	host.deadAt = seenAt;
	host.diedAtMs = (host.loadedMs && (host.loadedAt != 0))
		? std::make_optional(
			*host.loadedMs + double(seenAt - host.loadedAt) / 1000.)
		: std::nullopt;
}

void AddCoverSample(
		StressCoverSamples &samples,
		crl::profile_time started,
		crl::profile_time finished,
		const QString &failure) {
	const auto cost = finished - started;
	++samples.samples;
	samples.cost += cost;
	samples.maxCost = std::max(samples.maxCost, cost);
	if (!failure.isEmpty() && samples.failure.isEmpty()) {
		samples.failure = failure;
		samples.failureAt = started;
		samples.failureSample = samples.samples;
	}
}

// The run entry of one level: loaded, boot and death as the host reported
// them; a level without a window has frames.count 0 and null window
// metrics; `errors` are the host's error logs, then the runner's errors,
// each prefixed.
StressEntry ComputeEntry(const StressLevel &level) {
	const auto &end = level.end;
	const auto &sample = end.host;
	const auto measured = level.start.has_value();
	const auto window = StressWindow{
		.start = measured ? level.start->at : end.at,
		.end = end.at,
	};
	const auto loaded = end.attached && sample.loaded;
	const auto dead = end.attached && sample.dead.has_value();
	const auto host = ComputeHost(level.start, end);
	const auto widget = ComputeWidget(
		level.values,
		level.valuesDropped,
		level.id,
		measured ? std::make_optional(window.start) : std::nullopt,
		end.at,
		dead);
	const auto frames = measured
		? ComputeFrames(level.updates, window)
		: StressFrames();
	const auto renderer = measured
		? ComputeRenderer(level.work, level.host, window)
		: StressRenderer();
	auto runnerErrors = level.runnerErrors;
	if (measured) {
		runnerErrors += CoverErrors(level.coverAndLock, window);
	}
	runnerErrors += host.errors;
	runnerErrors += widget.errors;
	runnerErrors += renderer.errors;
	auto errors = QJsonArray();
	auto diags = QJsonObject();
	if (end.attached) {
		for (const auto &text : sample.errors) {
			errors.push_back(QString::fromStdString(text));
		}
		for (const auto &[kind, count] : sample.diags) {
			diags.insert(QString::fromStdString(kind), count);
		}
	}
	for (const auto &text : runnerErrors) {
		errors.push_back(QString::fromLatin1(kRunnerErrorPrefix) + text);
	}
	const auto object = QJsonObject{
		{ u"id"_q, level.id },
		{ u"load"_q, level.load },
		{ u"value"_q, level.value },
		{ u"loaded"_q, loaded },
		{ u"bootMs"_q, Nullable(loaded ? sample.loadedMs : std::nullopt) },
		{
			u"dead"_q,
			dead ? QJsonValue(QString::fromStdString(*sample.dead)) : Null(),
		},
		{ u"diedAtMs"_q, Nullable(dead ? sample.diedAtMs : std::nullopt) },
		{ u"frames"_q, FramesObject(frames) },
		{ u"renderer"_q, RendererObject(renderer) },
		{ u"host"_q, HostObject(host) },
		{ u"widget"_q, WidgetObject(widget) },
		{ u"thermal"_q, Null() },
		{ u"errors"_q, errors },
		{ u"diags"_q, diags },
		{
			u"desktop"_q,
			DesktopObject(
				level,
				frames,
				renderer,
				host,
				widget,
				int(runnerErrors.size())),
		},
	};
	return { .object = object, .runnerErrors = runnerErrors };
}

// Why this launch's entries cannot be accepted: entries that are not
// exactly the selection's levels in manifest order, and every entry with
// a runner error.
QStringList AcceptanceRefusals(
		const StressManifest &manifest,
		const StressSelection &selection,
		const std::vector<StressEntry> &entries) {
	auto ids = QStringList();
	for (const auto &entry : entries) {
		ids.push_back(entry.object.value(u"id"_q).toString());
	}
	auto expected = QStringList();
	for (const auto index : selection.widgets) {
		expected.push_back(manifest.widgets[index].id);
	}
	auto result = QStringList();
	if (ids != expected) {
		result.push_back(u"the launch has entries [%1] where its selection "
			u"has [%2], in manifest order"_q.arg(
				ids.join(u", "_q),
				expected.join(u", "_q)));
	}
	for (const auto &entry : entries) {
		if (!entry.runnerErrors.isEmpty()) {
			result.push_back(u"entry [%1]: %2"_q.arg(
				entry.object.value(u"id"_q).toString(),
				entry.runnerErrors.join(u"; "_q)));
		}
	}
	return result;
}

namespace {

// ---------------------------------------------------------------------
// The envelope.

[[nodiscard]] QString ClientBuild(const QString &note) {
#ifdef _DEBUG
	const auto configuration = u"Debug"_q;
#else // _DEBUG
	const auto configuration = u"Release"_q;
#endif // _DEBUG
	auto parts = QStringList{
		QString::fromLatin1(AppVersionStr),
		configuration,
		QSysInfo::buildCpuArchitecture(),
	};
	if (!note.isEmpty()) {
		parts.push_back(note);
	}
	return parts.join(u", "_q);
}

// The CPU, the OS with its kernel and the installed memory, as the OS
// reports them.
[[nodiscard]] StressMachine ReadMachine() {
	auto result = StressMachine{
		.os = u"%1 (kernel %2)"_q.arg(
			QSysInfo::prettyProductName(),
			QSysInfo::kernelVersion()),
	};
#ifdef Q_OS_WIN
	const auto processor = QSettings(
		u"HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System"
			u"\\CentralProcessor\\0"_q,
		QSettings::NativeFormat);
	result.model = processor.value(
		u"ProcessorNameString"_q).toString().trimmed();
	auto kilobytes = ULONGLONG(0);
	if (GetPhysicallyInstalledSystemMemory(&kilobytes)) {
		result.ramMb = int64(kilobytes / 1024);
	}
#endif // Q_OS_WIN
	if (result.model.isEmpty()) {
		result.model = u"unknown CPU (%1)"_q.arg(
			QSysInfo::currentCpuArchitecture());
	}
	return result;
}

[[nodiscard]] QString NewLaunchId() {
	return u"%1/%2"_q.arg(
		QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),
		QString::number(QCoreApplication::applicationPid()));
}

[[nodiscard]] bool Smoke(const StressLaunch &launch) {
	return (launch.warmupMs != launch.manifestWarmupMs)
		|| (launch.measureMs != launch.manifestMeasureMs);
}

// The definitions every run file carries, the smoke marker, then the
// caller's notes. Only the smoke marker depends on the launch, through
// durations the requested configuration also holds.
[[nodiscard]] QStringList Notes(const StressLaunch &launch) {
	auto result = QStringList{
		u"frames: the intervals between consecutive QEvent::UpdateRequest "
			u"events delivered to the exact top-level window that shows the "
			u"widget's card (a Qt Widgets top-level window, its QWindow "
			u"surface type in desktop.screen.surface; a harness event "
			u"filter stamps each with crl::profile() and never repaints), "
			u"inside the measuring window. count is the number of "
			u"intervals, p50 and p95 the nearest ranks v[min(n - 1, "
			u"round(p (n - 1)))] of the sorted intervals, max the largest; "
			u"fewer than two updates give count 0 and nulls. It is an "
			u"observable window-update proxy, not a DWM presentation proven "
			u"to be display-paced: the window updates on demand, so a "
			u"static widget's sparse updates are not dropped frames. With a "
			u"Direct3DSurface Qt Widgets paints the raster backing store "
			u"and flushes it through QRhi into a swapchain."_q,
		u"frames.dropped is null in every entry: demand-driven window "
			u"updates, not proven to be display-paced, cannot measure the "
			u"share of physical display frames missed. tools/report.mjs treats a null dropped "
			u"as zero when it computes fullRateUpTo, so its client-frame "
			u"full-rate column is only an update-cadence observation, not a "
			u"display-performance limit, and \"none\" for a sparse or "
			u"static load is not a stall. longTaskMs and mainThreadMsPerSec "
			u"are null: not measured."_q,
		u"renderer: the work of the measured widget's RendererCore on the "
			u"main thread, timed by harness scopes: each host message "
			u"applied (ops, animations, queries), each tick and each timed "
			u"frame() attempt past its cached-frame return (TwidgetCore's "
			u"style, layout and display list, then its QPainter replay into "
			u"an ARGB32 image on the CPU and the rounding), never the "
			u"window's flush or other chat painting. An attempt may end "
			u"before the QPainter replay when the list has not laid out the "
			u"card's size; desktop.rendererWork.replayedFrames counts the "
			u"attempts inside the window that painted an image and "
			u"unreplayedFrames those that did not. A sample is one attempt's "
			u"own time plus the message and tick work since the attempt "
			u"before it, replayed or not: work before the window is dropped, "
			u"the first group in the window is partial and discarded, only "
			u"complete groups whose attempt finished inside the window "
			u"count, and the work after the window's last attempt is "
			u"desktop.rendererWork.trailingWorkMs. msPerFrame is the samples' "
			u"mean, p95Ms their nearest rank, displayListOps the display "
			u"list size of the last sampled attempt; no sample gives nulls. "
			u"The scopes' own clock reads are inside the times."_q,
		u"host: the deltas of SandboxStats jsMs, queries, queryMs and "
			u"throttledTasks between the window's start and end snapshots "
			u"over the snapshots' actual elapsed seconds (desktop.windowMs). "
			u"The stats are the host's last post, at most every 200 ms, with "
			u"memoryBytes refreshed at most once a second; their age at "
			u"each snapshot is desktop.statsAgeMs. opsPerSec is the "
			u"validated ops the newest host sent its renderer during the "
			u"window (the ops tap), maxBatchOps that host's largest ops "
			u"message since it was created; nodes and memoryBytes are read "
			u"at the end. A counter that went backwards is a named error "
			u"and null, never clamped."_q,
		u"widget: the data-stress values in the validated ops, each "
			u"stamped when the host handed its batch to the tap. The start "
			u"value is the last valid one received at or before the start "
			u"snapshot (else the first after it), the end value the last "
			u"received at or before the end snapshot, none after it; fps = "
			u"(frames at the end - at the start) x 1000 / (t at the end - at "
			u"the start), frames and jank the differences, maxGap the "
			u"largest published by the values received inside the window, "
			u"last the end value (its text verbatim in "
			u"desktop.values.lastText)."_q,
		u"entries: one per manifest widget, in manifest order. A level that "
			u"never loaded or died before its window has frames.count 0 "
			u"and null window metrics, with its boot, death and final host "
			u"readings; a loaded widget that dies later keeps its whole "
			u"window. dead is the host's first dead reason verbatim and "
			u"diedAtMs the host's steady clock from its creation to the "
			u"change that carried it. For a stop the desktop decided itself "
			u"(a paint bound), which no host death carries, dead is "
			u"RendererCore's stop reason and diedAtMs the host clock at the "
			u"runner's first reading that saw it (loadedMs plus the time "
			u"since the load was seen, null without them): an estimate, not "
			u"a measurement of the stop. A host reason other than "
			u"\"terminated\" comes first. The runner's own failures are "
			u"errors prefixed \"runner: \", never a dead reason; thermal is "
			u"null."_q,
		u"observers inside the measured process, all included in the "
			u"readings: the ops tap's parse and leak scan of every batch on "
			u"the host thread, whose cost scales with the batches' size and "
			u"frequency and so depends on the workload; the host watch's "
			u"diagnostics copy in each onChange on the host thread; the "
			u"renderer work scopes' clock reads and the window's "
			u"UpdateRequest event filter on the main thread; the 500 ms "
			u"window and card samples on the main thread "
			u"(desktop.window.samples, sampleCostMs) and, on each 50 ms "
			u"runner tick of the warm-up and the window until it sees one, "
			u"the reading of a stop the desktop decided itself (the host "
			u"watch, the tap's counts and the card's runtime); the 500 ms "
			u"console-lock and covering-window samples on a harness worker "
			u"thread inside the window, off the main thread, which only "
			u"starts it before the window and stops and joins it before the "
			u"end snapshot (desktop.window.coverAndLock: samples, costMs, "
			u"maxCostMs, joinMs); and -testagent's debug logging and the "
			u"deadlock detector's ping thread. None runs on the widget's JS "
			u"thread."_q,
		(launch.pass == WidgetStressPass::Measure)
			? (u"measure: CLIENTS.md's measure table replaced those fields "
				u"of each level's host options right before its Job; "
				u"config.applied is what the host was built with, the card's "
				u"CSS width and the theme its renderer got."_q)
			: (u"production: each level's host was built with the product's "
				u"own options, unchanged; config.applied is what the host was "
				u"built with, the card's CSS width and the theme its renderer "
				u"got."_q),
	};
	if (Smoke(launch)) {
		result.push_back(u"SMOKE: a warm-up of %1 ms and a measuring window "
			u"of %2 ms instead of manifest.run's %3 ms and %4 ms; not a run "
			u"to send."_q.arg(
				QString::number(launch.warmupMs),
				QString::number(launch.measureMs),
				QString::number(launch.manifestWarmupMs),
				QString::number(launch.manifestMeasureMs)));
	}
	result += launch.notes;
	return result;
}

[[nodiscard]] QJsonObject RequestedObject(const StressLaunch &launch) {
	const auto measure = HostOverride(launch.pass);
	auto result = QJsonObject{
		{
			u"policy"_q,
			measure.policy
				? QString::fromStdString(*measure.policy)
				: QString::fromLatin1(kProductionPolicy),
		},
		{ u"width"_q, launch.width },
		{ u"theme"_q, QString::fromLatin1(kTheme) },
		{ u"warmupMs"_q, qint64(launch.warmupMs) },
		{ u"measureMs"_q, qint64(launch.measureMs) },
	};
	if (measure.cpuLimitMs) {
		result.insert(u"cpuLimitMs"_q, *measure.cpuLimitMs);
	}
	if (measure.cpuQuotaMs) {
		result.insert(u"cpuQuotaMs"_q, *measure.cpuQuotaMs);
	}
	if (measure.cpuQuotaWindowMs) {
		result.insert(u"cpuQuotaWindowMs"_q, *measure.cpuQuotaWindowMs);
	}
	if (measure.mainThreadQuotaMs) {
		result.insert(u"mainThreadQuotaMs"_q, *measure.mainThreadQuotaMs);
	}
	if (measure.maxTimers) {
		result.insert(u"maxTimers"_q, *measure.maxTimers);
	}
	if (measure.memoryLimitBytes) {
		result.insert(
			u"memoryLimitBytes"_q,
			qint64(*measure.memoryLimitBytes));
	}
	if (measure.features) {
		result.insert(u"features"_q, Strings(*measure.features));
	}
	return result;
}

[[nodiscard]] QJsonObject AppliedObject(
		const std::optional<StressApplied> &applied) {
	if (!applied) {
		return QJsonObject();
	}
	const auto &options = applied->options;
	return {
		{ u"policy"_q, QString::fromStdString(options.policy) },
		{ u"cpuLimitMs"_q, options.cpuLimitMs },
		{ u"cpuQuotaMs"_q, options.cpuQuotaMs },
		{ u"cpuQuotaWindowMs"_q, options.cpuQuotaWindowMs },
		{ u"cpuKillAfterMs"_q, options.cpuKillAfterMs },
		{ u"mainThreadQuotaMs"_q, options.mainThreadQuotaMs },
		{ u"maxTimers"_q, options.maxTimers },
		{ u"memoryLimitBytes"_q, qint64(options.memoryLimitBytes) },
		{ u"features"_q, Strings(options.features) },
		{ u"devicePixelRatio"_q, options.devicePixelRatio },
		{ u"width"_q, applied->cardCssWidth },
		{ u"theme"_q, applied->theme },
	};
}

[[nodiscard]] QJsonValue Positive(double value) {
	return (value > 0.) ? QJsonValue(value) : Null();
}

// The window the levels were measured in; null when the live runner never
// read it. Part of device, so every launch merged into one file shares it.
[[nodiscard]] QJsonValue WindowLayoutObject(const StressWindowLayout &window) {
	if (window.layout.isEmpty()) {
		return Null();
	}
	return QJsonObject{
		{ u"width"_q, window.width },
		{ u"height"_q, window.height },
		{ u"layout"_q, window.layout },
		{ u"filtersSidebar"_q, window.filtersSidebar },
	};
}

[[nodiscard]] QJsonObject DeviceObject(const StressLaunch &launch) {
	return {
		{ u"model"_q, launch.machine.model },
		{ u"os"_q, launch.machine.os },
		{ u"class"_q, QString::fromLatin1(kDeviceClass) },
		{ u"refreshHz"_q, Positive(launch.screen.refreshHz) },
		{ u"dpr"_q, Positive(launch.screen.windowDpr) },
		{ u"window"_q, WindowLayoutObject(launch.window) },
		{ u"ramMb"_q, NullableInteger(launch.machine.ramMb) },
		{ u"lowPowerMode"_q, Null() },
	};
}

[[nodiscard]] QJsonObject ScreenObject(const StressScreen &screen) {
	const auto &geometry = screen.geometry;
	return {
		{ u"name"_q, screen.name },
		{
			u"geometry"_q,
			QJsonArray{
				geometry.x(),
				geometry.y(),
				geometry.width(),
				geometry.height(),
			},
		},
		{ u"refreshHz"_q, Positive(screen.refreshHz) },
		{ u"surface"_q, screen.surface },
	};
}

[[nodiscard]] QJsonObject ScalesObject(const StressScreen &screen) {
	return {
		{ u"windowDpr"_q, Positive(screen.windowDpr) },
		{ u"coreRatio"_q, Positive(screen.coreRatio) },
		{ u"uiScale"_q, screen.uiScale },
	};
}

[[nodiscard]] QJsonObject LaunchRecord(
		const StressLaunch &launch,
		int entries) {
	auto result = launch.record;
	result.insert(u"id"_q, launch.id);
	result.insert(u"date"_q, launch.date);
	result.insert(u"loads"_q, QJsonArray::fromStringList(launch.loads));
	result.insert(u"entries"_q, entries);
	result.insert(u"warmupMs"_q, qint64(launch.warmupMs));
	result.insert(u"measureMs"_q, qint64(launch.measureMs));
	result.insert(u"screen"_q, ScreenObject(launch.screen));
	result.insert(u"scales"_q, ScalesObject(launch.screen));
	result.insert(u"window"_q, WindowLayoutObject(launch.window));
	return result;
}

// One launch's run file: the envelope and |entries| as given.
[[nodiscard]] QJsonObject RunFileObject(
		const StressLaunch &launch,
		const std::vector<StressEntry> &entries) {
	auto runs = QJsonArray();
	for (const auto &entry : entries) {
		runs.push_back(entry.object);
	}
	return {
		{
			u"kit"_q,
			QJsonObject{
				{ u"version"_q, launch.kitVersion },
				{ u"commit"_q, launch.framework },
			},
		},
		{ u"platform"_q, QString::fromLatin1(kPlatform) },
		{ u"client"_q, QString::fromLatin1(kClient) },
		{ u"clientBuild"_q, ClientBuild(launch.buildNote) },
		{ u"framework"_q, launch.framework },
		{ u"renderer"_q, QString::fromLatin1(kRenderer) },
		{ u"rendererKind"_q, QString::fromLatin1(kRendererKind) },
		{ u"engine"_q, launch.engine },
		{ u"device"_q, DeviceObject(launch) },
		{ u"pass"_q, PassName(launch.pass) },
		{
			u"config"_q,
			QJsonObject{
				{ u"requested"_q, RequestedObject(launch) },
				{ u"applied"_q, AppliedObject(launch.applied) },
			},
		},
		{ u"date"_q, launch.date },
		{ u"notes"_q, QJsonArray::fromStringList(Notes(launch)) },
		{ u"runs"_q, runs },
		{
			u"desktop"_q,
			QJsonObject{
				{ u"frameSource"_q, QString::fromLatin1(kFrameSource) },
				{ u"screen"_q, ScreenObject(launch.screen) },
				{ u"scales"_q, ScalesObject(launch.screen) },
				{
					u"launches"_q,
					QJsonArray{ LaunchRecord(launch, int(entries.size())) },
				},
			},
		},
	};
}

// ---------------------------------------------------------------------
// The merge.

[[nodiscard]] int ManifestIndex(
		const StressManifest &manifest,
		const QString &id) {
	const auto i = std::find_if(
		manifest.widgets.begin(),
		manifest.widgets.end(),
		[&](const StressWidget &widget) { return widget.id == id; });
	return (i != manifest.widgets.end()) ? i->index : -1;
}

// |fresh|, one launch's run file of |loads|, merged into |existing|, the
// pass's run file, or into an empty file of |fresh|'s envelope when there
// is none yet. Every kMergeKeys value must be the same;
// |existing| keeps every other key, its date, its notes with |fresh|'s new
// ones after them, and its launches with |fresh|'s unknown ones after
// them; the entries of |loads| are |fresh|'s, every other one stays, and
// all are in manifest order.
[[nodiscard]] StressMerge MergeRunFile(
		const std::optional<QJsonObject> &existing,
		const QJsonObject &fresh,
		const StressManifest &manifest,
		const QStringList &loads) {
	auto base = existing.value_or(fresh);
	if (!existing) {
		auto desktop = fresh.value(u"desktop"_q).toObject();
		desktop.insert(u"launches"_q, QJsonArray());
		base.insert(u"runs"_q, QJsonArray());
		base.insert(u"notes"_q, QJsonArray());
		base.insert(u"desktop"_q, desktop);
	}
	for (const auto key : kMergeKeys) {
		const auto name = QString::fromLatin1(key);
		const auto was = base.value(name);
		const auto now = fresh.value(name);
		if (!SameJson(was, now)) {
			return {
				.error = u"the run file's %1 differs from this launch's: %2 "
					u"there, %3 here"_q.arg(name, Compact(was), Compact(now)),
			};
		}
	}
	const auto wasRuns = base.value(u"runs"_q);
	if (!wasRuns.isArray()) {
		return { .error = u"the run file's runs is not an array"_q };
	}
	auto runs = std::vector<std::pair<int, QJsonObject>>();
	auto seen = QSet<QString>();
	const auto take = [&](const QJsonValue &run, bool replaced) {
		const auto object = run.toObject();
		const auto id = object.value(u"id"_q).toString();
		const auto index = ManifestIndex(manifest, id);
		if (index < 0) {
			return u"the entry [%1] is not in this kit's manifest"_q.arg(id);
		} else if (replaced && loads.contains(manifest.widgets[index].load)) {
			return QString();
		} else if (seen.contains(id)) {
			return u"the entry [%1] is there twice"_q.arg(id);
		}
		seen.insert(id);
		runs.emplace_back(index, object);
		return QString();
	};
	for (const auto &run : wasRuns.toArray()) {
		const auto error = take(run, true);
		if (!error.isEmpty()) {
			return { .error = u"the run file: "_q + error };
		}
	}
	for (const auto &run : fresh.value(u"runs"_q).toArray()) {
		const auto error = take(run, false);
		if (!error.isEmpty()) {
			return { .error = u"this launch: "_q + error };
		}
	}
	std::stable_sort(runs.begin(), runs.end(), [](
			const auto &a,
			const auto &b) {
		return a.first < b.first;
	});
	auto sorted = QJsonArray();
	for (const auto &run : runs) {
		sorted.push_back(run.second);
	}
	auto notes = base.value(u"notes"_q).toArray();
	auto noted = QSet<QString>();
	for (const auto &note : notes) {
		noted.insert(note.toString());
	}
	for (const auto &note : fresh.value(u"notes"_q).toArray()) {
		if (!noted.contains(note.toString())) {
			noted.insert(note.toString());
			notes.push_back(note);
		}
	}
	auto desktop = base.value(u"desktop"_q).toObject();
	const auto freshDesktop = fresh.value(u"desktop"_q).toObject();
	for (auto i = freshDesktop.begin(); i != freshDesktop.end(); ++i) {
		if (!desktop.contains(i.key())) {
			desktop.insert(i.key(), i.value());
		}
	}
	auto launches = desktop.value(u"launches"_q).toArray();
	auto known = QSet<QString>();
	for (const auto &launch : launches) {
		known.insert(launch.toObject().value(u"id"_q).toString());
	}
	for (const auto &launch : freshDesktop.value(u"launches"_q).toArray()) {
		const auto id = launch.toObject().value(u"id"_q).toString();
		if (!known.contains(id)) {
			known.insert(id);
			launches.push_back(launch);
		}
	}
	desktop.insert(u"launches"_q, launches);
	auto result = base;
	result.insert(u"runs"_q, sorted);
	result.insert(u"notes"_q, notes);
	result.insert(u"desktop"_q, desktop);
	if (base.value(u"date"_q).toString().isEmpty()) {
		result.insert(u"date"_q, fresh.value(u"date"_q));
	}
	return { .file = result };
}

// ---------------------------------------------------------------------
// The files.

// The pass's run file at |path|, or nothing; |error| says why there is
// nothing when a file is there.
[[nodiscard]] std::optional<QJsonObject> ReadRunFile(
		const QString &path,
		QString *error) {
	if (!QFileInfo::exists(path)) {
		return std::nullopt;
	}
	auto read = QString();
	const auto bytes = ReadBytes(path, &read);
	if (!read.isEmpty()) {
		*error = u"the run file [%1] could not be read: %2"_q.arg(path, read);
		return std::nullopt;
	}
	auto parse = QJsonParseError();
	const auto document = QJsonDocument::fromJson(bytes, &parse);
	if (parse.error != QJsonParseError::NoError || !document.isObject()) {
		*error = u"the run file [%1] is not a JSON object"_q.arg(path);
		return std::nullopt;
	}
	return document.object();
}

// Writes |object| to |path| atomically; returns why it could not.
[[nodiscard]] QString SaveJson(const QString &path, const QJsonObject &object) {
	if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
		return u"its directory could not be created"_q;
	}
	const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
	auto file = QSaveFile(path);
	if (!file.open(QIODevice::WriteOnly)) {
		return u"open: "_q + file.errorString();
	} else if (file.write(bytes) != bytes.size()) {
		const auto error = u"write: "_q + file.errorString();
		file.cancelWriting();
		return error;
	} else if (!file.commit()) {
		return u"commit: "_q + file.errorString();
	}
	return QString();
}

[[nodiscard]] QString LaunchFilePath(
		const QString &output,
		const QStringList &loads) {
	return output + u".launch-"_q + loads.join(QChar('+')) + u".json"_q;
}

} // namespace

// A launch of |selection| as |args| ask; the live runner fills its screen,
// window, applied options and record before the run file is written.
StressLaunch MakeLaunch(
		const WidgetStressArgs &args,
		const StressManifest &manifest,
		const StressSelection &selection) {
	return {
		.id = NewLaunchId(),
		.date = QDateTime::currentDateTimeUtc().toString(Qt::ISODate),
		.framework = args.framework,
		.engine = args.engine,
		.buildNote = args.buildNote,
		.notes = args.notes,
		.loads = selection.loads,
		.machine = ReadMachine(),
		.warmupMs = args.warmupMs.value_or(manifest.warmupMs),
		.measureMs = args.measureMs.value_or(manifest.measureMs),
		.manifestWarmupMs = manifest.warmupMs,
		.manifestMeasureMs = manifest.measureMs,
		.kitVersion = manifest.version,
		.width = manifest.width,
		.pass = args.pass,
	};
}

// Merges an accepted launch, one without |refusals|, into the run file at
// |output|; a refused launch, or one the merge or the write refuses, goes
// to its launch file with every reason instead, and the run file stays as
// it was.
StressPublication PublishLaunch(
		const QString &output,
		const StressManifest &manifest,
		const StressLaunch &launch,
		const std::vector<StressEntry> &entries,
		QStringList refusals) {
	const auto fresh = RunFileObject(launch, entries);
	if (refusals.isEmpty()) {
		auto error = QString();
		const auto existing = ReadRunFile(output, &error);
		if (error.isEmpty()) {
			const auto merge = MergeRunFile(
				existing,
				fresh,
				manifest,
				launch.loads);
			error = merge.error;
			if (error.isEmpty()) {
				const auto saved = SaveJson(output, merge.file);
				if (saved.isEmpty()) {
					return { .path = output, .merged = true };
				}
				error = u"the run file [%1] could not be written: %2"_q.arg(
					output,
					saved);
			}
		}
		refusals.push_back(error);
	}
	auto partial = fresh;
	partial.insert(u"launch"_q, QJsonObject{
		{ u"accepted"_q, false },
		{ u"refusals"_q, QJsonArray::fromStringList(refusals) },
	});
	auto result = StressPublication{
		.path = LaunchFilePath(output, launch.loads),
		.error = refusals.join(u"; "_q),
	};
	const auto saved = SaveJson(result.path, partial);
	if (!saved.isEmpty()) {
		result.error += u"; the launch file [%1] could not be written "
			u"either: %2"_q.arg(result.path, saved);
		result.path = QString();
	}
	return result;
}

namespace {

// ---------------------------------------------------------------------
// Self-checks: literal readings through the functions above.

constexpr auto kControlId = "shadow-moving/16";
constexpr auto kControlStart = crl::profile_time(1'000'000);
constexpr auto kControlEnd = crl::profile_time(11'000'000);

constexpr auto kControlManifest = R"json({"version":1,
"run":{"warmupMs":2000,"measureMs":10000,"width":390},
"loads":[{"id":"a","widgets":["a/0","a/1"]},
{"id":"b","widgets":["b/0","b/1"]},
{"id":"c","widgets":["c/0"]}],
"widgets":[
{"id":"a/0","file":"widgets/a/000000.html","load":"a","level":0,"value":0,
"crosses":[]},
{"id":"a/1","file":"widgets/a/000001.html","load":"a","level":1,"value":1,
"crosses":[]},
{"id":"b/0","file":"widgets/b/000000.html","load":"b","level":0,"value":0,
"crosses":[]},
{"id":"b/1","file":"widgets/b/000008.html","load":"b","level":1,"value":8,
"crosses":["heavyBoxes 8 > 4"]},
{"id":"c/0","file":"widgets/c/000000.html","load":"c","level":0,"value":0,
"crosses":[]}]})json";

[[nodiscard]] WidgetOpsSelfCheckRow SelfCheckRow(
		bool passed,
		const QString &name,
		const QString &details) {
	return {
		.name = Prefixed(name),
		.details = details,
		.passed = passed,
	};
}

[[nodiscard]] StressManifest ControlManifest() {
	auto error = QString();
	return ParseManifest(QByteArray(kControlManifest), &error);
}

[[nodiscard]] WidgetStressValue ControlValue(
		crl::profile_time receivedAt,
		int t,
		int frames,
		int maxGap,
		int jank,
		const QString &id = QString::fromLatin1(kControlId)) {
	const auto object = QJsonObject{
		{ u"v"_q, 1 },
		{ u"id"_q, id },
		{ u"level"_q, 3 },
		{ u"value"_q, 16 },
		{ u"status"_q, t ? u"running"_q : u"setup"_q },
		{ u"t"_q, t },
		{ u"frames"_q, frames },
		{ u"fps"_q, 60 },
		{ u"maxGap"_q, maxGap },
		{ u"jank"_q, jank },
	};
	return {
		.value = QJsonDocument(object).toJson(
			QJsonDocument::Compact).toStdString(),
		.receivedAt = receivedAt,
	};
}

[[nodiscard]] StressSnapshot ControlSnapshot(
		crl::profile_time at,
		double jsMs,
		int64 ops,
		size_t memoryBytes,
		crl::profile_time statsChangedAt) {
	auto sample = WidgetHostSample();
	sample.stats.jsMs = jsMs;
	sample.stats.queries = 10;
	sample.stats.queryMs = 5.;
	sample.stats.memoryBytes = memoryBytes;
	sample.statsChangedAt = statsChangedAt;
	sample.loadedMs = 84.5;
	sample.nodes = 35;
	sample.loaded = true;
	auto latest = WidgetOpsLatest();
	latest.ops = ops;
	latest.batches = 50;
	latest.maxBatchOps = 34;
	latest.generation = 1;
	return MakeSnapshot(
		WidgetHostReading{ .hosts = { sample } },
		0,
		latest,
		at);
}

[[nodiscard]] WidgetWorkEvent Work(
		WidgetWorkKind kind,
		crl::profile_time started,
		crl::profile_time finished,
		int64 displayList = -1,
		bool replayed = false) {
	return {
		.started = started,
		.finished = finished,
		.displayList = displayList,
		.kind = kind,
		.replayed = replayed,
	};
}

// Before the window: cleared. Then a partial T1 F4, discarded; M2 T1 F3
// (not replayed) and T1 F2, the samples 6 and 3; a trailing M1; a frame
// after the end.
[[nodiscard]] WidgetWorkReading ControlWork() {
	using Kind = WidgetWorkKind;
	return {
		.events = {
			Work(Kind::Message, 945'000, 950'000),
			Work(Kind::Tick, 1'049'000, 1'050'000),
			Work(Kind::Frame, 1'056'000, 1'060'000, 400, true),
			Work(Kind::Message, 1'200'000, 1'202'000),
			Work(Kind::Tick, 1'203'000, 1'204'000),
			Work(Kind::Frame, 1'205'000, 1'208'000, 410, false),
			Work(Kind::Tick, 1'300'000, 1'301'000),
			Work(Kind::Frame, 1'302'000, 1'304'000, 420, true),
			Work(Kind::Message, 1'400'000, 1'401'000),
			Work(Kind::Frame, 11'100'000, 11'105'000, 430, true),
		},
		.reservedAt = 500'000,
		.capacity = 64,
		.registered = 1,
	};
}

// The worker's 19 passing samples of 300 us between the window's ends.
[[nodiscard]] StressCoverSamples ControlCoverSamples() {
	auto result = StressCoverSamples{ .zOrder = true };
	for (auto i = 1; i != 20; ++i) {
		const auto started = kControlStart + i * 500'000;
		AddCoverSample(result, started, started + 300, QString());
	}
	return result;
}

// A measured level: 10 s between the snapshots, five updates inside the
// window 8, 8, 8 and 33 ms apart and one on each side, the work above,
// passing in-window samples and data-stress values on both sides of both
// ends.
[[nodiscard]] StressLevel ControlLevel() {
	return {
		.id = QString::fromLatin1(kControlId),
		.load = u"shadow-moving"_q,
		.value = 16.,
		.index = 1,
		.start = ControlSnapshot(kControlStart, 100., 1000, 2'000'000, 900'000),
		.end = ControlSnapshot(
			kControlEnd,
			510.,
			40'000,
			2'900'000,
			10'850'000),
		.updates = {
			900'000,
			1'100'000,
			1'108'000,
			1'116'000,
			1'124'000,
			1'157'000,
			11'100'000,
		},
		.work = ControlWork(),
		.coverAndLock = ControlCoverSamples(),
		.values = {
			ControlValue(400'000, 0, 0, 0, 0),
			ControlValue(900'000, 2000, 120, 25, 0),
			ControlValue(1'400'000, 2500, 150, 18, 0),
			ControlValue(6'000'000, 7000, 420, 60, 1),
			ControlValue(10'900'000, 11900, 714, 17, 1),
			ControlValue(11'400'000, 12400, 744, 99, 2),
		},
	};
}

[[nodiscard]] StressLaunch ControlLaunch(WidgetStressPass pass) {
	const auto measure = (pass == WidgetStressPass::Measure);
	const auto options = WidgetHostApplied{
		.policy = measure ? "compat" : "strict",
		.features = { "webAnimations" },
		.locale = "en",
		.memoryLimitBytes = measure ? size_t(67108864) : size_t(8388608),
		.cpuLimitMs = 500.,
		.cpuQuotaMs = measure ? 10000. : 300.,
		.cpuQuotaWindowMs = 1000.,
		.cpuKillAfterMs = 0.,
		.mainThreadQuotaMs = measure ? 10000. : 100.,
		.devicePixelRatio = 1.5,
		.maxTimers = measure ? 256 : 64,
		.visible = true,
	};
	return {
		.id = u"launch-1"_q,
		.date = u"2026-10-30T10:00:00Z"_q,
		.framework = u"v0.4.0"_q,
		.engine = u"TwidgetCore control"_q,
		.buildNote = u"control build"_q,
		.notes = { u"caller note"_q },
		.loads = { u"b"_q },
		.machine = {
			.model = u"Control CPU"_q,
			.os = u"Control OS (kernel 1.0)"_q,
			.ramMb = 65536,
		},
		.screen = {
			.name = u"CONTROL"_q,
			.geometry = QRect(5120, 0, 3840, 2160),
			.surface = u"RasterSurface"_q,
			.refreshHz = 60.,
			.windowDpr = 1.5,
			.coreRatio = 1.5,
			.uiScale = 150,
		},
		.window = {
			.layout = u"one-column"_q,
			.width = 966,
			.height = 1000,
			.filtersSidebar = false,
		},
		.applied = StressApplied{
			.options = options,
			.theme = u"light"_q,
			.cardCssWidth = 390.,
		},
		.warmupMs = 2000,
		.measureMs = 10000,
		.manifestWarmupMs = 2000,
		.manifestMeasureMs = 10000,
		.kitVersion = 1,
		.width = 390,
		.pass = pass,
	};
}

[[nodiscard]] StressEntry MarkedEntry(
		const QString &id,
		const QString &marker) {
	return {
		.object = QJsonObject{
			{ u"id"_q, id },
			{ u"marker"_q, marker },
		},
	};
}

[[nodiscard]] QStringList RunIds(const QJsonObject &file) {
	auto result = QStringList();
	for (const auto &run : file.value(u"runs"_q).toArray()) {
		result.push_back(run.toObject().value(u"id"_q).toString());
	}
	return result;
}

[[nodiscard]] QStringList RunMarkers(const QJsonObject &file) {
	auto result = QStringList();
	for (const auto &run : file.value(u"runs"_q).toArray()) {
		result.push_back(run.toObject().value(u"marker"_q).toString());
	}
	return result;
}

[[nodiscard]] QStringList LaunchIds(const QJsonObject &file) {
	auto result = QStringList();
	const auto launches = file.value(
		u"desktop"_q).toObject().value(u"launches"_q).toArray();
	for (const auto &launch : launches) {
		result.push_back(launch.toObject().value(u"id"_q).toString());
	}
	return result;
}

[[nodiscard]] QJsonObject FirstLaunch(const QJsonObject &file) {
	const auto launches = file.value(
		u"desktop"_q).toObject().value(u"launches"_q).toArray();
	return launches.isEmpty() ? QJsonObject() : launches[0].toObject();
}

[[nodiscard]] QStringList NoteList(const QJsonObject &file) {
	auto result = QStringList();
	for (const auto &note : file.value(u"notes"_q).toArray()) {
		result.push_back(note.toString());
	}
	return result;
}

[[nodiscard]] WidgetOpsSelfCheckRow CheckManifest() {
	auto error = QString();
	const auto manifest = ParseManifest(
		QByteArray(kControlManifest),
		&error);
	const auto parsed = error.isEmpty()
		&& (manifest.version == 1)
		&& (manifest.warmupMs == 2000)
		&& (manifest.measureMs == 10000)
		&& (manifest.width == 390)
		&& (manifest.widgets.size() == 5)
		&& (manifest.loads.size() == 3)
		&& (manifest.widgets[3].id == u"b/1"_q)
		&& (manifest.widgets[3].index == 3)
		&& (manifest.widgets[3].value == 8.)
		&& (manifest.widgets[3].level == 1)
		&& (manifest.widgets[3].crosses
			== QStringList{ u"heavyBoxes 8 > 4"_q });
	auto selectError = QString();
	const auto selection = SelectLoads(
		manifest,
		{ u"c"_q, u"a"_q },
		&selectError);
	const auto selected = selectError.isEmpty()
		&& (selection.loads == QStringList{ u"a"_q, u"c"_q })
		&& (selection.widgets == std::vector<int>{ 0, 1, 4 });
	auto unknownError = QString();
	const auto unknown = SelectLoads(
		manifest,
		{ u"a"_q, u"zzz"_q },
		&unknownError);
	auto emptyError = QString();
	const auto empty = SelectLoads(manifest, {}, &emptyError);
	auto broken = QString::fromLatin1(kControlManifest);
	broken.replace(
		u"\"file\":\"widgets/b/000008.html\","_q,
		QString());
	broken.replace(u"[\"c/0\"]"_q, u"[\"c/0\",\"b/0\"]"_q);
	auto brokenError = QString();
	const auto damaged = ParseManifest(broken.toUtf8(), &brokenError);
	const auto refused = unknown.loads.isEmpty()
		&& (damaged.widgets.size() == 5)
		&& unknownError.contains(u"[zzz]"_q)
		&& empty.widgets.empty()
		&& !emptyError.isEmpty()
		&& brokenError.contains(u"[b/1] needs a file"_q)
		&& brokenError.contains(u"load [c] lists [b/0]"_q);
	return SelfCheckRow(
		parsed && selected && refused,
		u"manifest: version, run, widgets with their index, value, level "
			u"and crosses and loads are read; loads c and a select a then "
			u"c and widgets 0, 1 and 4 in manifest order; an unknown load, "
			u"an empty request, a widget without a file and a load listing "
			u"another load's widget are refused by name"_q,
		u"parsed=%1 loads=[%2] widgets=%3 unknown=[%4] empty=[%5] "
			u"broken=[%6]"_q
			.arg(parsed ? 1 : 0)
			.arg(selection.loads.join(u","_q))
			.arg(int(selection.widgets.size()))
			.arg(unknownError)
			.arg(emptyError)
			.arg(brokenError));
}

[[nodiscard]] WidgetOpsSelfCheckRow CheckFrames() {
	const auto window = StressWindow{
		.start = kControlStart,
		.end = kControlEnd,
	};
	const auto level = ControlLevel();
	const auto frames = ComputeFrames(level.updates, window);
	const auto single = ComputeFrames(
		{ 900'000, 1'500'000, 11'100'000 },
		window);
	const auto entry = ComputeEntry(level).object.value(
		u"frames"_q).toObject();
	const auto passed = (frames.count == 4)
		&& (frames.updates == 5)
		&& (frames.p50 == 8.)
		&& (frames.p95 == 33.)
		&& (frames.max == 33.)
		&& (single.count == 0)
		&& (single.updates == 1)
		&& !single.p50
		&& !single.p95
		&& !single.max
		&& SameJson(entry.value(u"count"_q), 4)
		&& SameJson(entry.value(u"p50"_q), 8)
		&& SameJson(entry.value(u"max"_q), 33)
		&& entry.value(u"dropped"_q).isNull()
		&& entry.value(u"longTaskMs"_q).isNull()
		&& entry.value(u"mainThreadMsPerSec"_q).isNull();
	return SelfCheckRow(
		passed,
		u"frames: five exact-window updates 8, 8, 8 and 33 ms apart inside "
			u"the window give count 4, p50 8, p95 33 and max 33 with "
			u"dropped, longTaskMs and mainThreadMsPerSec null; the updates "
			u"outside the window are ignored, and one update inside it "
			u"gives count 0 and nulls"_q,
		u"count=%1 updates=%2 p50=%3 p95=%4 max=%5 single={count=%6 "
			u"updates=%7 p50=%8} entry=%9"_q
			.arg(frames.count)
			.arg(frames.updates)
			.arg(OptionalText(frames.p50))
			.arg(OptionalText(frames.p95))
			.arg(OptionalText(frames.max))
			.arg(single.count)
			.arg(single.updates)
			.arg(OptionalText(single.p50))
			.arg(Compact(entry)));
}

[[nodiscard]] WidgetOpsSelfCheckRow CheckRendererGroups() {
	const auto window = StressWindow{
		.start = kControlStart,
		.end = kControlEnd,
	};
	const auto renderer = ComputeRenderer(ControlWork(), 0, window);
	const auto entry = ComputeEntry(ControlLevel()).object;
	const auto object = entry.value(u"renderer"_q).toObject();
	const auto work = entry.value(
		u"desktop"_q).toObject().value(u"rendererWork"_q).toObject();
	auto samples = QStringList();
	for (const auto sample : renderer.samples) {
		samples.push_back(QString::number(sample));
	}
	const auto passed = renderer.errors.isEmpty()
		&& (renderer.samples == std::vector<double>{ 6., 3. })
		&& (renderer.partialMs == 5.)
		&& (renderer.trailingMs == 1.)
		&& (renderer.trailingEvents == 1)
		&& (renderer.frames == 3)
		&& (renderer.replayedFrames == 2)
		&& (renderer.unreplayedFrames == 1)
		&& (renderer.ticks == 3)
		&& (renderer.messages == 2)
		&& (renderer.msPerFrame == 4.5)
		&& (renderer.p95Ms == 6.)
		&& (renderer.displayListOps == int64(420))
		&& SameJson(object.value(u"msPerFrame"_q), 4.5)
		&& SameJson(object.value(u"p95Ms"_q), 6)
		&& SameJson(object.value(u"displayListOps"_q), 420)
		&& SameJson(work.value(u"trailingWorkMs"_q), 1)
		&& SameJson(work.value(u"partialGroupMs"_q), 5)
		&& SameJson(work.value(u"samples"_q), 2)
		&& SameJson(work.value(u"replayedFrames"_q), 2)
		&& SameJson(work.value(u"unreplayedFrames"_q), 1);
	return SelfCheckRow(
		passed,
		u"renderer: work before the window is cleared, the first group in "
			u"it (T1 F4) is partial and discarded, M2 T1 F3 and T1 F2 give "
			u"samples 6 and 3 (mean 4.5, p95 6, the last sampled frame's "
			u"display list 420) though F3 did not replay, the attempts count "
			u"2 replayed and 1 unreplayed, the trailing M1 is kept apart and "
			u"the frame after the end is ignored"_q,
		u"samples=[%1] partial=%2 trailing=%3/%4 frames=%5 replayed=%6/%7 "
			u"ticks=%8 messages=%9 entry=%10"_q
			.arg(samples.join(u","_q))
			.arg(OptionalText(renderer.partialMs))
			.arg(renderer.trailingMs)
			.arg(renderer.trailingEvents)
			.arg(renderer.frames)
			.arg(renderer.replayedFrames)
			.arg(renderer.unreplayedFrames)
			.arg(renderer.ticks)
			.arg(renderer.messages)
			.arg(Compact(object)));
}

[[nodiscard]] WidgetOpsSelfCheckRow CheckRendererInvalid() {
	const auto window = StressWindow{
		.start = kControlStart,
		.end = kControlEnd,
	};
	auto overflow = ControlWork();
	overflow.dropped = 2;
	auto late = ControlWork();
	late.reservedAt = 1'500'000;
	auto missing = ControlWork();
	missing.events.clear();
	auto foreign = ControlWork();
	foreign.events[4].host = 1;
	auto readings = QStringList();
	const auto refused = [&](
			const WidgetWorkReading &work,
			const QString &word) {
		const auto renderer = ComputeRenderer(work, 0, window);
		const auto object = RendererObject(renderer);
		readings.push_back(u"{%1: %2}"_q.arg(
			renderer.errors.join(u"; "_q),
			Compact(object)));
		return renderer.errors.join(u"; "_q).contains(word)
			&& object.value(u"msPerFrame"_q).isNull()
			&& object.value(u"p95Ms"_q).isNull()
			&& object.value(u"displayListOps"_q).isNull();
	};
	const auto checked = refused(overflow, u"log full"_q)
		&& refused(late, u"not reserved"_q)
		&& refused(missing, u"no renderer work"_q)
		&& refused(foreign, u"another host"_q);
	auto level = ControlLevel();
	level.work = overflow;
	const auto entry = ComputeEntry(level);
	const auto errors = entry.object.value(u"errors"_q).toArray();
	const auto listed = (entry.runnerErrors.size() == 1)
		&& (errors.size() == 1)
		&& errors[0].toString().startsWith(
			QString::fromLatin1(kRunnerErrorPrefix) + u"renderer timing"_q);
	return SelfCheckRow(
		checked && listed,
		u"renderer: a work log that overflowed, was reserved after the "
			u"window started, recorded nothing or holds another "
			u"attachment's work inside the window is a named runner error "
			u"with null renderer fields, never a truncated or all-null "
			u"success"_q,
		readings.join(u" "_q)
			+ u" entryErrors="_q
			+ Compact(errors));
}

[[nodiscard]] WidgetOpsSelfCheckRow CheckCoverSamples() {
	const auto covered = u"windows above the measured one cover the card: "
		u"class=Control process=other overlap=10,10 50x50"_q;
	auto level = ControlLevel();
	auto &samples = level.coverAndLock;
	samples = StressCoverSamples{ .zOrder = true };
	AddCoverSample(samples, 1'500'000, 1'500'400, QString());
	AddCoverSample(samples, 4'000'000, 4'000'900, covered);
	AddCoverSample(samples, 6'000'000, 6'000'300, u"the console is locked"_q);
	AddCoverSample(samples, 8'000'000, 8'000'200, QString());
	const auto entry = ComputeEntry(level);
	const auto object = entry.object.value(u"desktop"_q).toObject().value(
		u"window"_q).toObject().value(u"coverAndLock"_q).toObject();
	const auto failure = object.value(u"failure"_q).toObject();
	auto unseen = ControlLevel();
	unseen.coverAndLock = StressCoverSamples();
	const auto unseenErrors = ComputeEntry(unseen).runnerErrors;
	const auto errors = entry.runnerErrors.join(u"; "_q);
	const auto passed = (samples.samples == 4)
		&& (samples.failureSample == 2)
		&& (samples.cost == 1800)
		&& (samples.maxCost == 900)
		&& (entry.runnerErrors.size() == 1)
		&& errors.contains(u"in-window sample 2, 3000 ms after the start"_q)
		&& errors.contains(covered)
		&& !errors.contains(u"locked"_q)
		&& SameJson(object.value(u"samples"_q), 4)
		&& SameJson(object.value(u"sampleEveryMs"_q), 500)
		&& SameJson(object.value(u"costMs"_q), 1.8)
		&& SameJson(object.value(u"maxCostMs"_q), 0.9)
		&& SameJson(failure.value(u"sample"_q), 2)
		&& SameJson(failure.value(u"offsetMs"_q), 3000)
		&& (failure.value(u"details"_q).toString() == covered)
		&& ComputeEntry(ControlLevel()).runnerErrors.isEmpty()
		&& (unseenErrors.size() == 1)
		&& unseenErrors[0].contains(u"no console-lock or covering-window"_q);
	return SelfCheckRow(
		passed,
		u"window: of four in-window samples the second, a covering window "
			u"3000 ms after the start, is latched and later failures are not, "
			u"it is the level's one runner error, the samples' count and "
			u"cost (1.8 ms, at most 0.9) are in desktop.window.coverAndLock, "
			u"19 passing samples give none, and a 10 s window with no sample "
			u"is a runner error"_q,
		errors + u" object="_q + Compact(object) + u" unseen="_q
			+ unseenErrors.join(u"; "_q));
}

[[nodiscard]] WidgetOpsSelfCheckRow CheckHost() {
	const auto entry = ComputeEntry(ControlLevel());
	const auto host = entry.object.value(u"host"_q).toObject();
	const auto ages = entry.object.value(
		u"desktop"_q).toObject().value(u"statsAgeMs"_q).toObject();
	const auto passed = entry.runnerErrors.isEmpty()
		&& SameJson(host.value(u"jsMsPerSec"_q), 41)
		&& SameJson(host.value(u"opsPerSec"_q), 3900)
		&& SameJson(host.value(u"queriesPerSec"_q), 0)
		&& SameJson(host.value(u"queryMsPerSec"_q), 0)
		&& SameJson(host.value(u"throttledTasks"_q), 0)
		&& SameJson(host.value(u"nodes"_q), 35)
		&& SameJson(host.value(u"memoryBytes"_q), 2900000)
		&& SameJson(host.value(u"maxBatchOps"_q), 34)
		&& host.value(u"processMemoryMb"_q).isNull()
		&& SameJson(ages.value(u"start"_q), 100)
		&& SameJson(ages.value(u"end"_q), 150)
		&& SameJson(entry.object.value(u"bootMs"_q), 84.5)
		&& (entry.object.value(u"loaded"_q) == QJsonValue(true))
		&& entry.object.value(u"dead"_q).isNull();
	return SelfCheckRow(
		passed,
		u"host: jsMs 100 -> 510 and ops 1000 -> 40000 over snapshots 10 s "
			u"apart give 41 and 3900 per second, unchanged queries 0, "
			u"throttledTasks 0, nodes 35, memoryBytes 2900000 and "
			u"maxBatchOps 34 at the end, stats ages 100 and 150 ms"_q,
		Compact(host) + u" ages="_q + Compact(ages));
}

[[nodiscard]] WidgetOpsSelfCheckRow CheckWidgetWindow() {
	const auto level = ControlLevel();
	const auto stats = ComputeWidget(
		level.values,
		0,
		level.id,
		kControlStart,
		kControlEnd,
		false);
	const auto entry = ComputeEntry(level).object;
	const auto widget = entry.value(u"widget"_q).toObject();
	const auto values = entry.value(
		u"desktop"_q).toObject().value(u"values"_q).toObject();
	const auto inside = std::vector<WidgetStressValue>(
		level.values.begin() + 2,
		level.values.end() - 1);
	const auto fallback = ComputeWidget(
		inside,
		0,
		level.id,
		kControlStart,
		kControlEnd,
		false);
	const auto clipped = stats.errors.isEmpty()
		&& (stats.frames == int64(594))
		&& (stats.jank == int64(1))
		&& (stats.fps == 60.)
		&& (stats.maxGap == 60.)
		&& (stats.values == 5)
		&& (stats.windowValues == 3)
		&& (stats.late == 1)
		&& stats.last
		&& (stats.last->frames == 714)
		&& SameJson(widget.value(u"fps"_q), 60)
		&& SameJson(widget.value(u"frames"_q), 594)
		&& SameJson(widget.value(u"maxGap"_q), 60)
		&& SameJson(widget.value(u"jank"_q), 1)
		&& SameJson(
			widget.value(u"last"_q).toObject().value(u"frames"_q),
			714)
		&& (values.value(u"lastText"_q).toString()
			== QString::fromStdString(level.values[4].value));
	const auto first = fallback.errors.isEmpty()
		&& (fallback.frames == int64(564))
		&& (fallback.fps == 60.)
		&& (fallback.maxGap == 60.);
	return SelfCheckRow(
		clipped && first,
		u"widget: the start value is the last at or before the start "
			u"snapshot (frames 120, t 2000, maxGap 25 not counted), the end "
			u"value the last at or before the end (frames 714, t 11900), "
			u"the value after the end (maxGap 99) unused: fps 60, frames "
			u"594, jank 1, maxGap 60, last verbatim; without a value before "
			u"the start the first inside the window starts it (frames "
			u"564)"_q,
		Compact(widget)
			+ u" fallback={frames=%1 fps=%2 maxGap=%3}"_q
				.arg(fallback.frames.value_or(-1))
				.arg(OptionalText(fallback.fps))
				.arg(OptionalText(fallback.maxGap)));
}

[[nodiscard]] WidgetOpsSelfCheckRow CheckWidgetInvalid() {
	auto readings = QStringList();
	const auto refused = [&](
			std::vector<WidgetStressValue> values,
			const QString &word) {
		auto level = ControlLevel();
		level.values = std::move(values);
		const auto entry = ComputeEntry(level);
		const auto widget = entry.object.value(u"widget"_q).toObject();
		const auto errors = entry.runnerErrors.join(u"; "_q);
		readings.push_back(u"{%1}"_q.arg(errors));
		return errors.contains(word)
			&& widget.value(u"fps"_q).isNull()
			&& widget.value(u"frames"_q).isNull()
			&& widget.value(u"maxGap"_q).isNull()
			&& widget.value(u"jank"_q).isNull()
			&& widget.value(u"last"_q).isNull();
	};
	const auto base = ControlLevel().values;
	auto foreign = base;
	foreign[3] = ControlValue(
		6'000'000,
		7000,
		420,
		60,
		1,
		u"shadow-moving/8"_q);
	auto broken = base;
	broken[3].value = R"({"v":1,)";
	auto backwards = base;
	backwards[3] = ControlValue(6'000'000, 7000, 100, 60, 1);
	auto garbageLate = base;
	garbageLate[5].value = "not json";
	auto lateLevel = ControlLevel();
	lateLevel.values = garbageLate;
	const auto late = ComputeEntry(lateLevel);
	const auto lateUnused = late.runnerErrors.isEmpty()
		&& SameJson(
			late.object.value(u"widget"_q).toObject().value(u"frames"_q),
			594);
	const auto passed = refused(foreign, u"[shadow-moving/8]"_q)
		&& refused(broken, u"not a JSON object"_q)
		&& refused(backwards, u"went backwards"_q)
		&& lateUnused;
	return SelfCheckRow(
		passed,
		u"widget: a value with another entry's id, text that is no JSON "
			u"object and frames going backwards are named runner errors with "
			u"every widget field null; a broken value after the end stamp "
			u"is never read"_q,
		readings.join(u" "_q)
			+ u" late=%1"_q.arg(late.runnerErrors.join(u"; "_q)));
}

[[nodiscard]] WidgetOpsSelfCheckRow CheckCounterReset() {
	auto level = ControlLevel();
	level.end.host.stats.jsMs = 50.;
	level.end.ops = 500;
	const auto entry = ComputeEntry(level);
	const auto host = entry.object.value(u"host"_q).toObject();
	const auto errors = entry.runnerErrors.join(u"; "_q);
	const auto passed = (entry.runnerErrors.size() == 2)
		&& errors.contains(u"stats.jsMs went backwards"_q)
		&& errors.contains(u"validated ops went backwards"_q)
		&& host.value(u"jsMsPerSec"_q).isNull()
		&& host.value(u"opsPerSec"_q).isNull()
		&& SameJson(host.value(u"queriesPerSec"_q), 0)
		&& SameJson(host.value(u"maxBatchOps"_q), 34);
	return SelfCheckRow(
		passed,
		u"host: jsMs 100 -> 50 and ops 1000 -> 500 across the window are "
			u"named runner errors with jsMsPerSec and opsPerSec null, never "
			u"clamped to zero; the other readings stay"_q,
		errors + u" host="_q + Compact(host));
}

[[nodiscard]] WidgetOpsSelfCheckRow CheckNeverLoaded() {
	auto sample = WidgetHostSample();
	sample.dead = std::string("memory: heap limit of 8388608 bytes reached");
	sample.diedAtMs = 12.5;
	sample.errors = { "RangeError: out of memory" };
	sample.errorLogs = 1;
	sample.logs = 3;
	sample.diags = { { "limit", 1 } };
	auto latest = WidgetOpsLatest();
	latest.generation = 1;
	const auto level = StressLevel{
		.id = u"dom-static/16000"_q,
		.load = u"dom-static"_q,
		.value = 16000.,
		.index = 60,
		.end = MakeSnapshot(
			WidgetHostReading{ .hosts = { sample } },
			0,
			latest,
			2'000'000),
	};
	const auto entry = ComputeEntry(level);
	const auto &object = entry.object;
	const auto frames = object.value(u"frames"_q).toObject();
	const auto renderer = object.value(u"renderer"_q).toObject();
	const auto host = object.value(u"host"_q).toObject();
	const auto widget = object.value(u"widget"_q).toObject();
	const auto allNull = [](const QJsonObject &part, QStringList keys) {
		return std::all_of(keys.begin(), keys.end(), [&](const QString &key) {
			return part.contains(key) && part.value(key).isNull();
		});
	};
	const auto passed = entry.runnerErrors.isEmpty()
		&& (object.value(u"loaded"_q) == QJsonValue(false))
		&& object.value(u"bootMs"_q).isNull()
		&& (object.value(u"dead"_q).toString()
			== u"memory: heap limit of 8388608 bytes reached"_q)
		&& SameJson(object.value(u"diedAtMs"_q), 12.5)
		&& SameJson(frames.value(u"count"_q), 0)
		&& allNull(frames, { u"p50"_q, u"p95"_q, u"max"_q, u"dropped"_q })
		&& allNull(renderer, {
			u"msPerFrame"_q,
			u"p95Ms"_q,
			u"displayListOps"_q,
		})
		&& allNull(host, {
			u"jsMsPerSec"_q,
			u"opsPerSec"_q,
			u"queriesPerSec"_q,
			u"queryMsPerSec"_q,
			u"memoryBytes"_q,
			u"throttledTasks"_q,
			u"maxBatchOps"_q,
		})
		&& SameJson(host.value(u"nodes"_q), 0)
		&& allNull(widget, {
			u"fps"_q,
			u"frames"_q,
			u"maxGap"_q,
			u"jank"_q,
			u"last"_q,
		})
		&& object.value(u"thermal"_q).isNull()
		&& SameJson(
			object.value(u"errors"_q),
			QJsonArray{ u"RangeError: out of memory"_q })
		&& SameJson(object.value(u"diags"_q), QJsonObject{ { u"limit"_q, 1 } })
		&& (object.value(u"desktop"_q).toObject().value(u"measured"_q)
			== QJsonValue(false));
	return SelfCheckRow(
		passed,
		u"never loaded: a host that died before loading gives loaded false, "
			u"bootMs null, its dead reason verbatim and diedAtMs, frames "
			u"count 0 and every window metric null, its final node count, "
			u"its error log and diag counts, and no runner error"_q,
		Compact(object));
}

// The measured control level with its end's host dead set to |dead| (with
// diedAtMs 12.5 and deadAt 10.5 s), its load seen at 0.5 s when
// |loadStamp|, loadedMs 84.5 when |loadedMs|, then a desktop stop of
// |reason| seen at 10.25025 s merged into that end.
[[nodiscard]] std::pair<StressSnapshot, StressEntry> DesktopStopLevel(
		const std::optional<std::string> &dead,
		const QString &reason,
		bool loadStamp,
		bool loadedMs) {
	auto level = ControlLevel();
	auto &host = level.end.host;
	if (dead) {
		host.dead = dead;
		host.diedAtMs = 12.5;
		host.deadAt = 10'500'000;
	}
	host.loadedAt = loadStamp ? 500'000 : 0;
	if (!loadedMs) {
		host.loadedMs = std::nullopt;
	}
	ApplyDesktopStop(level.end, reason, 10'250'250);
	return { level.end, ComputeEntry(level) };
}

[[nodiscard]] WidgetOpsSelfCheckRow CheckDesktopStop() {
	const auto stop = u"Paint limit exceeded: filter work of 8437640 units "
		u"in one frame (max 8388608)"_q;
	const auto own = std::string("Ops limit exceeded: 12000 operations in "
		"one batch (max 10000)");
	const auto terminated = std::string(kTerminated);
	auto readings = QStringList();
	auto passed = true;
	const auto expect = [&](
			const QString &name,
			const std::pair<StressSnapshot, StressEntry> &merged,
			const QJsonValue &dead,
			const QJsonValue &diedAtMs,
			crl::profile_time deadAt) {
		const auto &[end, entry] = merged;
		const auto &object = entry.object;
		passed = passed
			&& entry.runnerErrors.isEmpty()
			&& (object.value(u"desktop"_q).toObject().value(u"measured"_q)
				== QJsonValue(true))
			&& SameJson(
				object.value(u"frames"_q).toObject().value(u"count"_q),
				4)
			&& SameJson(object.value(u"dead"_q), dead)
			&& SameJson(object.value(u"diedAtMs"_q), diedAtMs)
			&& (end.host.deadAt == deadAt);
		readings.push_back(u"{%1: dead=%2 diedAtMs=%3 deadAt=%4 errors=[%5]}"_q
			.arg(name)
			.arg(Compact(object.value(u"dead"_q)))
			.arg(Compact(object.value(u"diedAtMs"_q)))
			.arg(QString::number(end.host.deadAt))
			.arg(entry.runnerErrors.join(u"; "_q)));
	};
	expect(
		u"null"_q,
		DesktopStopLevel(std::nullopt, stop, true, true),
		stop,
		9834.75,
		10'250'250);
	expect(
		u"terminated"_q,
		DesktopStopLevel(terminated, stop, true, true),
		stop,
		9834.75,
		10'250'250);
	expect(
		u"host's own"_q,
		DesktopStopLevel(own, stop, true, true),
		QString::fromStdString(own),
		12.5,
		10'500'000);
	expect(
		u"no stop, null"_q,
		DesktopStopLevel(std::nullopt, QString(), true, true),
		Null(),
		Null(),
		0);
	expect(
		u"no stop, terminated"_q,
		DesktopStopLevel(terminated, QString(), true, true),
		QString::fromStdString(terminated),
		12.5,
		10'500'000);
	expect(
		u"no load stamp"_q,
		DesktopStopLevel(std::nullopt, stop, false, true),
		stop,
		Null(),
		10'250'250);
	expect(
		u"no loadedMs"_q,
		DesktopStopLevel(terminated, stop, true, false),
		stop,
		Null(),
		10'250'250);
	return SelfCheckRow(
		passed,
		u"desktop stop: a stop the desktop decided itself replaces a null "
			u"and a \"terminated\" dead at a window's end, which keeps its "
			u"window (4 frame intervals), with deadAt the reading that saw "
			u"it and diedAtMs "
			u"loadedMs 84.5 + 9750.25 ms since the load was seen = 9834.75; "
			u"a host's own reason keeps its dead, diedAtMs and deadAt; no "
			u"stop keeps a null and a \"terminated\" dead; without the "
			u"load's stamp or loadedMs diedAtMs is null"_q,
		readings.join(QChar(' ')));
}

[[nodiscard]] WidgetOpsSelfCheckRow CheckEnvelope() {
	const auto measure = RunFileObject(
		ControlLaunch(WidgetStressPass::Measure),
		{});
	const auto production = RunFileObject(
		ControlLaunch(WidgetStressPass::Production),
		{});
	auto smokeLaunch = ControlLaunch(WidgetStressPass::Measure);
	smokeLaunch.warmupMs = 500;
	const auto smoke = RunFileObject(smokeLaunch, {});
	auto unreadLaunch = ControlLaunch(WidgetStressPass::Measure);
	unreadLaunch.window = StressWindowLayout();
	const auto unreadDevice = RunFileObject(unreadLaunch, {}).value(
		u"device"_q).toObject();
	const auto requested = QJsonObject{
		{ u"policy"_q, u"compat"_q },
		{ u"cpuLimitMs"_q, 500 },
		{ u"cpuQuotaMs"_q, 10000 },
		{ u"cpuQuotaWindowMs"_q, 1000 },
		{ u"mainThreadQuotaMs"_q, 10000 },
		{ u"maxTimers"_q, 256 },
		{ u"memoryLimitBytes"_q, 67108864 },
		{ u"features"_q, QJsonArray{ u"webAnimations"_q } },
		{ u"width"_q, 390 },
		{ u"theme"_q, u"light"_q },
		{ u"warmupMs"_q, 2000 },
		{ u"measureMs"_q, 10000 },
	};
	const auto productionRequested = QJsonObject{
		{ u"policy"_q, u"strict"_q },
		{ u"width"_q, 390 },
		{ u"theme"_q, u"light"_q },
		{ u"warmupMs"_q, 2000 },
		{ u"measureMs"_q, 10000 },
	};
	const auto config = measure.value(u"config"_q).toObject();
	const auto applied = config.value(u"applied"_q).toObject();
	const auto device = measure.value(u"device"_q).toObject();
	const auto desktop = measure.value(u"desktop"_q).toObject();
	const auto notes = NoteList(measure);
	const auto hasSmoke = [](const QStringList &list) {
		return std::any_of(list.begin(), list.end(), [](const QString &n) {
			return n.startsWith(u"SMOKE:"_q);
		});
	};
	const auto names = SameJson(
			measure.value(u"kit"_q),
			QJsonObject{
				{ u"version"_q, 1 },
				{ u"commit"_q, u"v0.4.0"_q },
			})
		&& (measure.value(u"platform"_q).toString() == u"desktop"_q)
		&& (measure.value(u"client"_q).toString() == u"Telegram Desktop"_q)
		&& (measure.value(u"renderer"_q).toString() == u"twidget-core"_q)
		&& (measure.value(u"rendererKind"_q).toString() == u"own"_q)
		&& (measure.value(u"framework"_q).toString() == u"v0.4.0"_q)
		&& (measure.value(u"engine"_q).toString()
			== u"TwidgetCore control"_q)
		&& measure.value(u"clientBuild"_q).toString().startsWith(
			QString::fromLatin1(AppVersionStr) + u", "_q)
		&& measure.value(u"clientBuild"_q).toString().endsWith(
			u", control build"_q)
		&& (measure.value(u"pass"_q).toString() == u"measure"_q)
		&& (production.value(u"pass"_q).toString() == u"production"_q)
		&& (measure.value(u"date"_q).toString() == u"2026-10-30T10:00:00Z"_q);
	const auto described = SameJson(device, QJsonObject{
			{ u"model"_q, u"Control CPU"_q },
			{ u"os"_q, u"Control OS (kernel 1.0)"_q },
			{ u"class"_q, u"desktop"_q },
			{ u"refreshHz"_q, 60 },
			{ u"dpr"_q, 1.5 },
			{
				u"window"_q,
				QJsonObject{
					{ u"width"_q, 966 },
					{ u"height"_q, 1000 },
					{ u"layout"_q, u"one-column"_q },
					{ u"filtersSidebar"_q, false },
				},
			},
			{ u"ramMb"_q, 65536 },
			{ u"lowPowerMode"_q, QJsonValue(QJsonValue::Null) },
		})
		&& SameJson(config.value(u"requested"_q), requested)
		&& SameJson(
			production.value(
				u"config"_q).toObject().value(u"requested"_q),
			productionRequested)
		&& (applied.value(u"policy"_q).toString() == u"compat"_q)
		&& SameJson(applied.value(u"cpuQuotaMs"_q), 10000)
		&& SameJson(applied.value(u"cpuKillAfterMs"_q), 0)
		&& SameJson(applied.value(u"memoryLimitBytes"_q), 67108864)
		&& SameJson(applied.value(u"devicePixelRatio"_q), 1.5)
		&& SameJson(applied.value(u"width"_q), 390)
		&& (applied.value(u"theme"_q).toString() == u"light"_q)
		&& SameJson(
			applied.value(u"features"_q),
			QJsonArray{ u"webAnimations"_q })
		&& (desktop.value(u"frameSource"_q).toString()
			== u"window-update-request"_q)
		&& (LaunchIds(measure) == QStringList{ u"launch-1"_q })
		&& SameJson(
			FirstLaunch(measure).value(u"window"_q),
			device.value(u"window"_q))
		&& (unreadDevice.value(u"window"_q)
			== QJsonValue(QJsonValue::Null));
	const auto noted = notes.join(u"\n"_q).contains(u"fullRateUpTo"_q)
		&& (notes.last() == u"caller note"_q)
		&& !hasSmoke(notes)
		&& hasSmoke(NoteList(smoke))
		&& (NoteList(smoke).last() == u"caller note"_q);
	return SelfCheckRow(
		names && described && noted,
		u"envelope: kit version and tag, desktop, Telegram Desktop, "
			u"twidget-core, own, the engine, clientBuild from the version, "
			u"configuration, architecture and note, the device as measured "
			u"with its window (size, one-column layout, filters sidebar; "
			u"also in the launch record, null when never read), "
			u"CLIENTS.md's measure table or strict as requested, the "
			u"host's options with the card's width and theme as applied, "
			u"the frame source, the null-dropped caveat, a smoke marker "
			u"only for other durations and the caller's notes last"_q,
		u"clientBuild=[%1] device=%2 requested=%3 applied=%4 smoke=%5"_q.arg(
			measure.value(u"clientBuild"_q).toString(),
			Compact(device),
			Compact(config.value(u"requested"_q)),
			Compact(applied),
			hasSmoke(NoteList(smoke)) ? u"1"_q : u"0"_q));
}

[[nodiscard]] WidgetOpsSelfCheckRow CheckIntegers() {
	const auto measured = ComputeEntry(ControlLevel());
	const auto file = RunFileObject(
		ControlLaunch(WidgetStressPass::Measure),
		{ measured });
	const auto text = QString::fromUtf8(
		QJsonDocument(file).toJson(QJsonDocument::Compact));
	const auto expected = QStringList{
		u"\"kit\":{\"commit\":\"v0.4.0\",\"version\":1}"_q,
		u"\"count\":4,"_q,
		u"\"nodes\":35,"_q,
		u"\"memoryBytes\":2900000,"_q,
		u"\"throttledTasks\":0}"_q,
		u"\"maxBatchOps\":34,"_q,
		u"\"frames\":594,"_q,
		u"\"jank\":1,"_q,
		u"\"displayListOps\":420,"_q,
		u"\"maxTimers\":256,"_q,
		u"\"memoryLimitBytes\":67108864,"_q,
		u"\"ramMb\":65536,"_q,
		u"\"warmupMs\":2000,"_q,
		u"\"uiScale\":150"_q,
		u"\"height\":1000,"_q,
		u"\"width\":966}"_q,
	};
	auto missing = QStringList();
	for (const auto &entry : expected) {
		if (!text.contains(entry)) {
			missing.push_back(entry);
		}
	}
	const auto fraction = QRegularExpression(u":-?[0-9]+\\.0+[,}\\]]"_q);
	const auto exponent = QRegularExpression(u":-?[0-9.]+[eE][+-]?[0-9]"_q);
	const auto passed = missing.isEmpty()
		&& !text.contains(fraction)
		&& !text.contains(exponent);
	return SelfCheckRow(
		passed,
		u"integers: kit.version, frames.count, host.nodes, memoryBytes, "
			u"throttledTasks and maxBatchOps, widget.frames and jank, "
			u"displayListOps, the options' integers, ramMb, the window's "
			u"size and the durations serialize as whole numbers, without a "
			u"fraction or an exponent"_q,
		u"missing=[%1] bytes=%2"_q.arg(
			missing.join(u" "_q),
			QString::number(text.size())));
}

[[nodiscard]] WidgetOpsSelfCheckRow CheckLadderMerge() {
	const auto manifest = ControlManifest();
	auto first = ControlLaunch(WidgetStressPass::Measure);
	first.id = u"launch-ac"_q;
	first.loads = { u"a"_q, u"c"_q };
	first.notes = {};
	const auto initial = MergeRunFile(
		std::nullopt,
		RunFileObject(first, {
			MarkedEntry(u"c/0"_q, u"first"_q),
			MarkedEntry(u"a/1"_q, u"first"_q),
			MarkedEntry(u"a/0"_q, u"first"_q),
		}),
		manifest,
		first.loads);
	auto old = ControlLaunch(WidgetStressPass::Measure);
	old.id = u"launch-b-old"_q;
	old.date = u"2026-10-30T11:00:00Z"_q;
	old.notes = {};
	auto withOld = MergeRunFile(
		initial.file,
		RunFileObject(old, {
			MarkedEntry(u"b/0"_q, u"old"_q),
			MarkedEntry(u"b/1"_q, u"old"_q),
		}),
		manifest,
		old.loads);
	auto driven = withOld.file;
	auto drivenNotes = driven.value(u"notes"_q).toArray();
	drivenNotes.push_back(u"driver note"_q);
	driven.insert(u"notes"_q, drivenNotes);
	auto rerun = ControlLaunch(WidgetStressPass::Measure);
	rerun.id = u"launch-b-new"_q;
	rerun.date = u"2026-10-30T12:00:00Z"_q;
	rerun.notes = { u"rerun note"_q };
	const auto fresh = RunFileObject(rerun, {
		MarkedEntry(u"b/0"_q, u"new"_q),
		MarkedEntry(u"b/1"_q, u"new"_q),
	});
	const auto merged = MergeRunFile(driven, fresh, manifest, rerun.loads);
	const auto again = MergeRunFile(merged.file, fresh, manifest, rerun.loads);
	const auto notes = NoteList(merged.file);
	const auto ids = QStringList{
		u"a/0"_q,
		u"a/1"_q,
		u"b/0"_q,
		u"b/1"_q,
		u"c/0"_q,
	};
	const auto markers = QStringList{
		u"first"_q,
		u"first"_q,
		u"new"_q,
		u"new"_q,
		u"first"_q,
	};
	const auto launches = QStringList{
		u"launch-ac"_q,
		u"launch-b-old"_q,
		u"launch-b-new"_q,
	};
	const auto passed = initial.error.isEmpty()
		&& (RunIds(initial.file)
			== QStringList{ u"a/0"_q, u"a/1"_q, u"c/0"_q })
		&& (LaunchIds(initial.file) == QStringList{ u"launch-ac"_q })
		&& withOld.error.isEmpty()
		&& merged.error.isEmpty()
		&& again.error.isEmpty()
		&& (RunIds(merged.file) == ids)
		&& (RunMarkers(merged.file) == markers)
		&& (merged.file.value(u"date"_q).toString()
			== u"2026-10-30T10:00:00Z"_q)
		&& (notes.size() >= 2)
		&& (notes[notes.size() - 2] == u"driver note"_q)
		&& (notes.last() == u"rerun note"_q)
		&& (notes.count(u"driver note"_q) == 1)
		&& (LaunchIds(merged.file) == launches)
		&& (LaunchIds(again.file) == launches)
		&& (RunIds(again.file) == ids)
		&& (NoteList(again.file) == notes);
	return SelfCheckRow(
		passed,
		u"merge: a rerun of ladder b replaces only b's entries, all "
			u"entries end in manifest order (a first launch's c/0, a/1, a/0 "
			u"included), the first date stays, the driver's note stays "
			u"before the rerun's new one, and merging the same launch again "
			u"adds no launch, entry or note"_q,
		u"ids=[%1] markers=[%2] date=%3 launches=[%4] again=[%5] errors=[%6]"_q
			.arg(RunIds(merged.file).join(u","_q))
			.arg(RunMarkers(merged.file).join(u","_q))
			.arg(merged.file.value(u"date"_q).toString())
			.arg(LaunchIds(merged.file).join(u","_q))
			.arg(LaunchIds(again.file).join(u","_q))
			.arg(QStringList{
				initial.error,
				withOld.error,
				merged.error,
				again.error,
			}.join(u"|"_q)));
}

[[nodiscard]] WidgetOpsSelfCheckRow CheckMergeRefusal() {
	const auto manifest = ControlManifest();
	const auto base = ControlLaunch(WidgetStressPass::Measure);
	const auto existing = RunFileObject(base, {
		MarkedEntry(u"b/0"_q, u"first"_q),
	});
	auto otherDevice = base;
	otherDevice.machine.model = u"Other CPU"_q;
	otherDevice.applied->cardCssWidth = 380.;
	auto otherWindow = base;
	otherWindow.window.width = 1280;
	otherWindow.window.layout = u"normal"_q;
	auto otherPass = ControlLaunch(WidgetStressPass::Production);
	auto smoke = base;
	smoke.measureMs = 3000;
	const auto refusal = [&](const StressLaunch &launch) {
		return MergeRunFile(
			existing,
			RunFileObject(launch, { MarkedEntry(u"b/0"_q, u"new"_q) }),
			manifest,
			launch.loads);
	};
	const auto device = refusal(otherDevice);
	const auto window = refusal(otherWindow);
	const auto pass = refusal(otherPass);
	const auto config = refusal(smoke);
	const auto prefix = u"the run file's "_q;
	const auto passed = device.file.isEmpty()
		&& device.error.startsWith(prefix + u"device differs"_q)
		&& device.error.contains(u"Other CPU"_q)
		&& window.file.isEmpty()
		&& window.error.startsWith(prefix + u"device differs"_q)
		&& window.error.contains(u"\"layout\":\"normal\""_q)
		&& pass.file.isEmpty()
		&& pass.error.startsWith(prefix + u"pass differs"_q)
		&& config.file.isEmpty()
		&& config.error.startsWith(prefix + u"config differs"_q);
	return SelfCheckRow(
		passed,
		u"merge: a launch on another device, or in another window size or "
			u"column layout, is refused naming device, before its other "
			u"configuration; another pass is refused naming pass and a "
			u"smoke's durations naming config, each leaving no file to "
			u"write"_q,
		QStringList{
			device.error,
			window.error,
			pass.error,
			config.error,
		}.join(u" | "_q));
}

[[nodiscard]] WidgetOpsSelfCheckRow CheckAcceptance() {
	const auto manifest = ControlManifest();
	auto error = QString();
	const auto selection = SelectLoads(manifest, { u"b"_q }, &error);
	auto failed = MarkedEntry(u"b/1"_q, u"new"_q);
	failed.runnerErrors = { u"the level's load wait timed out"_q };
	const auto clean = AcceptanceRefusals(manifest, selection, {
		MarkedEntry(u"b/0"_q, u"new"_q),
		MarkedEntry(u"b/1"_q, u"new"_q),
	});
	const auto erred = AcceptanceRefusals(manifest, selection, {
		MarkedEntry(u"b/0"_q, u"new"_q),
		failed,
	});
	const auto partial = AcceptanceRefusals(manifest, selection, {
		MarkedEntry(u"b/0"_q, u"new"_q),
	});
	const auto passed = error.isEmpty()
		&& clean.isEmpty()
		&& (erred.size() == 1)
		&& erred[0].contains(u"[b/1]"_q)
		&& erred[0].contains(u"timed out"_q)
		&& (partial.size() == 1)
		&& partial[0].contains(u"[b/0, b/1]"_q)
		&& (LaunchFilePath(u"C:/runs/measure.json"_q, { u"a"_q, u"c"_q })
			== u"C:/runs/measure.json.launch-a+c.json"_q);
	return SelfCheckRow(
		passed,
		u"acceptance: both of ladder b's levels without a runner error are "
			u"accepted; a level with one, or a missing level, refuses the "
			u"launch by name, and a refused launch's file is "
			u"<output>.launch-<loads>.json"_q,
		u"erred=[%1] partial=[%2]"_q.arg(
			erred.join(u"; "_q),
			partial.join(u"; "_q)));
}

} // namespace

} // namespace Test::Stress

namespace Test {

std::vector<WidgetOpsSelfCheckRow> RunWidgetStressSelfCheck() {
	using namespace Stress;

	return {
		CheckManifest(),
		CheckFrames(),
		CheckRendererGroups(),
		CheckRendererInvalid(),
		CheckCoverSamples(),
		CheckHost(),
		CheckWidgetWindow(),
		CheckWidgetInvalid(),
		CheckCounterReset(),
		CheckNeverLoaded(),
		CheckDesktopStop(),
		CheckEnvelope(),
		CheckIntegers(),
		CheckLadderMerge(),
		CheckMergeRefusal(),
		CheckAcceptance(),
	};
}

} // namespace Test

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
