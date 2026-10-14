/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_runtime.h"

#include "core/application.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "renderer-litehtml/src/renderer.h"
#include "tgw/tgw_bridge.h"
#include "tgw/tgw_document.h"
#include "tgw/tgw_intl.h"
#include "tgw/tgw_renderer.h"
#include "tgw/tgw_resources.h"
#include "twidget/widget_host.h"
#include "ui/style/style_core_scale.h"
#include "ui/color_contrast.h"

namespace Tgw {
namespace {

constexpr auto kMaxAlive = 8;
constexpr auto kMaxNotes = 100;
constexpr auto kMaxLogLine = 240;
constexpr auto kMaxNoteLength = kMaxLogLine + 16;
constexpr auto kMaxLogDetails = 2;
constexpr auto kHostLogLimit = 5000;
constexpr auto kStateBytes = 8 * 1024;
constexpr auto kKindsBytes = 16 * 1024;
constexpr auto kConsoleBytes = 24 * 1024;
constexpr auto kBridgeBytes = 16 * 1024;
constexpr auto kOmittedBytes = 128;
constexpr auto kSnapshotBytes = 64 * 1024;
constexpr auto kNotesBytes = 56 * 1024;
constexpr auto kLogBytes = 128 * 1024;

static_assert(kStateBytes + kKindsBytes + kConsoleBytes + kBridgeBytes
	== kSnapshotBytes);
static_assert(kMaxNotes * kMaxNoteLength * int(sizeof(QChar))
	<= kNotesBytes);
static_assert(kSnapshotBytes + kNotesBytes < kLogBytes);

#ifdef _DEBUG
constexpr auto kCpuLimitScale = 4.; // unoptimized Debug QuickJS runs ~3x slower
#else
constexpr auto kCpuLimitScale = 1.;
#endif // _DEBUG

class Reservation final {
public:
	explicit Reservation(Fn<void()> released);
	Reservation(const Reservation &other) = delete;
	Reservation &operator=(const Reservation &other) = delete;
	~Reservation();

private:
	Fn<void()> _released;

};

using Slot = std::shared_ptr<Reservation>;

struct HostDeleter {
	Slot slot;

	void operator()(twidget::WidgetHost *host) const;
};

using HostPointer = std::shared_ptr<twidget::WidgetHost>;

struct Job {
	Fn<QByteArray()> read;
	std::shared_ptr<twidget::SandboxResources> resources;
	std::shared_ptr<twidget::IntlProvider> intl;
	twidget::RendererFactory factory;
	Fn<void(const std::string &, const std::string &)> bridge;
	twidget::WidgetHostOptions options;
	std::string theme;
	std::string stoppedLabel;
	Slot slot;
	double width = 0.;
};

struct Built {
	HostPointer host;
	QString error;
};

struct Registry {
	std::vector<not_null<Runtime*>> list;
	base::flat_set<not_null<Main::Session*>> sessions;
	Fn<void()> exitRetry;
	uint64 hiddenSerial = 0;
	uint64 queueSerial = 0;
	uint64 hostSerial = 0;
	uint64 requestSerial = 0;
	int reserved = 0;
	int jobs = 0;
	bool checkPosted = false;
	bool drainPosted = false;
	bool disabled = false;
	bool draining = false;
};

[[nodiscard]] Registry &Runtimes() {
	static auto result = Registry();
	return result;
}

[[nodiscard]] bool Busy() {
	const auto &registry = Runtimes();
	return (registry.jobs > 0) || (registry.reserved > 0);
}

void Drained() {
	auto &registry = Runtimes();
	registry.drainPosted = false;
	if (Busy()) {
		return;
	}
	LOG(("Tgw doesn't prevent quit any more."));
	if (const auto retry = base::take(registry.exitRetry)) {
		retry();
	} else if (Core::IsAppLaunched()) {
		Core::App().quitPreventFinished();
	}
}

void CheckDrained() {
	auto &registry = Runtimes();
	if (registry.draining && !registry.drainPosted && !Busy()) {
		registry.drainPosted = true;
		crl::on_main([] {
			Drained();
		});
	}
}

void JobDone() {
	--Runtimes().jobs;
	CheckDrained();
}

Reservation::Reservation(Fn<void()> released)
: _released(std::move(released)) {
	++Runtimes().reserved;
}

Reservation::~Reservation() {
	crl::on_main([released = std::move(_released)] {
		--Runtimes().reserved;
		released();
		CheckDrained();
	});
}

void HostDeleter::operator()(twidget::WidgetHost *host) const {
	// WHY: ~WidgetHost joins its engine and host threads for up to a CPU
	// slice, so every owner, dropped guarded posts included, deletes it on
	// a worker, and the slot is freed only after that join has finished.
	crl::async([host, slot = slot]() mutable {
		delete host;
		slot = nullptr;
	});
}

[[nodiscard]] double CssWidth(int width) {
	return width * 100. / style::Scale();
}

[[nodiscard]] double CoreRatio() {
	return style::DevicePixelRatio() * style::Scale() / 100.;
}

[[nodiscard]] int LogicalHeight(double css) {
	return int(std::ceil(css * style::Scale() / 100.));
}

[[nodiscard]] std::string ThemeName(bool dark) {
	return dark ? "dark" : "light";
}

void ApplyTheme(twidget::WidgetHost &host, const std::string &theme) {
	const auto apply = [&] {
		host.onHostThread([&] {
			host.setTheme(theme);
		});
	};
	apply();
	// WHY: the engine may still post a batch validated in the old theme, so
	// the styles are replayed again behind a barrier on its thread; probe()
	// wraps its code in String(), shadowed here to call no widget global.
	host.probe("0); function String() { return ''; } return (0");
	apply();
}

[[nodiscard]] twidget::WidgetHostOptions DefaultHostOptions(
		double ratio,
		bool visible) {
	auto result = twidget::WidgetHostOptions{
		.visible = visible,
		.devicePixelRatio = ratio,
		.features = { "webAnimations" },
	};
	result.cpuLimitMs *= kCpuLimitScale;
	return result;
}

[[nodiscard]] QString Clip(QString text, int length = kMaxLogLine) {
	if (text.size() <= length) {
		return text;
	}
	const auto ellipsis = u"..."_q;
	text.truncate(length - ellipsis.size());
	if (text.at(text.size() - 1).isHighSurrogate()) {
		text.chop(1);
	}
	return text + ellipsis;
}

[[nodiscard]] QString Clip(const std::string &text) {
	const auto size = std::min(text.size(), size_t(kMaxLogLine) * 4);
	return Clip(QString::fromUtf8(text.data(), int(size)));
}

[[nodiscard]] QString YesNo(bool value) {
	return value ? u"yes"_q : u"no"_q;
}

[[nodiscard]] QString Milliseconds(double value) {
	return u"%1 ms"_q.arg(QString::number(value, 'f', 0));
}

[[nodiscard]] QString Milliseconds(std::optional<double> value) {
	return value ? Milliseconds(*value) : u"-"_q;
}

[[nodiscard]] QString StateName(Runtime::State state) {
	using State = Runtime::State;
	switch (state) {
	case State::Idle: return u"idle"_q;
	case State::Waiting: return u"waiting for resources"_q;
	case State::Constructing: return u"constructing"_q;
	case State::Live: return u"live"_q;
	case State::Released: return u"released"_q;
	case State::Stopped: return u"stopped"_q;
	case State::Failed: return u"failed"_q;
	}
	Unexpected("State in Tgw::StateName.");
}

[[nodiscard]] int Bytes(const QString &line) {
	return (int(line.size()) + 1) * int(sizeof(QChar));
}

[[nodiscard]] bool Take(int &left, const QString &line) {
	const auto bytes = Bytes(line);
	if (bytes > left) {
		return false;
	}
	left -= bytes;
	return true;
}

void AppendState(
		QStringList &lines,
		const twidget::WidgetDiagnostics &diagnostics) {
	auto left = kStateBytes;
	const auto add = [&](QString line) {
		if (Take(left, line)) {
			lines.push_back(std::move(line));
		}
	};
	if (diagnostics.dead) {
		add(u"Died: "_q + Clip(*diagnostics.dead));
	}
	if (diagnostics.lastError) {
		add(u"Last error: "_q + Clip(*diagnostics.lastError));
	}
	add(u"Loaded: %1, rendered: %2, nodes: %3, height: %4."_q.arg(
		YesNo(diagnostics.loaded),
		YesNo(diagnostics.rendered),
		QString::number(diagnostics.nodes),
		QString::number(diagnostics.height)));
	add(u"Timing: renderer ready %1, loaded %2, rendered %3, "
		"first content %4."_q.arg(
			Milliseconds(diagnostics.rendererReadyMs),
			Milliseconds(diagnostics.loadedMs),
			Milliseconds(diagnostics.renderedMs),
			Milliseconds(diagnostics.firstContentMs)));
	const auto &stats = diagnostics.stats;
	add(u"Engine: %1 tasks, %2 of JS, %3 KB of memory, "
		"%4 throttled tasks."_q.arg(
			QString::number(stats.tasks),
			Milliseconds(stats.jsMs),
			QString::number(stats.memoryBytes / 1024),
			QString::number(stats.throttledTasks)));
	add(u"Renderer: %1 ops, %2 queries in %3 (%4 cached), "
		"%5 missed mutations."_q.arg(
			QString::number(stats.ops),
			QString::number(stats.queries),
			Milliseconds(stats.queryMs),
			QString::number(stats.queryCacheHits),
			QString::number(stats.missedMutations)));
}

void AppendKinds(
		QStringList &lines,
		const twidget::WidgetDiagnostics &diagnostics) {
	const auto kinds = int(diagnostics.diags.size());
	auto left = kKindsBytes - kOmittedBytes;
	auto header = u"Diagnostics: %1 kinds."_q.arg(kinds);
	if (Take(left, header)) {
		lines.push_back(std::move(header));
	}
	auto shown = 0;
	for (const auto &[kind, entry] : diagnostics.diags) {
		const auto &[count, details] = entry;
		auto block = QStringList{
			u"%1 x%2"_q.arg(Clip(kind), QString::number(count)),
		};
		const auto take = std::min(int(details.size()), kMaxLogDetails);
		for (auto i = 0; i != take; ++i) {
			block.push_back(u"  "_q + Clip(details[i]));
		}
		auto bytes = 0;
		for (const auto &line : block) {
			bytes += Bytes(line);
		}
		if (bytes > left) {
			break;
		}
		left -= bytes;
		lines.append(block);
		++shown;
	}
	if (shown < kinds) {
		lines.push_back(u"... %1 more kinds not shown."_q.arg(kinds - shown));
	}
}

void AppendTail(
		QStringList &lines,
		QString header,
		const std::vector<std::pair<std::string, std::string>> &entries,
		int budget) {
	auto left = budget - kOmittedBytes;
	if (Take(left, header)) {
		lines.push_back(std::move(header));
	}
	auto tail = QStringList();
	auto from = int(entries.size());
	while (from > 0) {
		const auto &[first, second] = entries[from - 1];
		auto line = u"%1: %2"_q.arg(Clip(first), Clip(second));
		if (!Take(left, line)) {
			break;
		}
		tail.push_back(std::move(line));
		--from;
	}
	if (from > 0) {
		lines.push_back(u"... %1 earlier entries not shown."_q.arg(from));
	}
	for (auto i = tail.size(); i != 0;) {
		lines.push_back(tail[--i]);
	}
}

[[nodiscard]] QString Summarize(const twidget::WidgetHost &host) {
	const auto diagnostics = host.diagnostics();
	auto lines = QStringList();
	AppendState(lines, diagnostics);
	AppendKinds(lines, diagnostics);
	const auto logs = int(diagnostics.logs.size());
	const auto console = (logs < kHostLogLimit)
		? u"Console: %1 entries."_q
		: u"Console: %1 entries, the host dropped all later ones."_q;
	AppendTail(lines, console.arg(logs), diagnostics.logs, kConsoleBytes);
	AppendTail(
		lines,
		u"Bridge calls: %1."_q.arg(int(diagnostics.bridge.size())),
		diagnostics.bridge,
		kBridgeBytes);
	return lines.join(QChar('\n'));
}

[[nodiscard]] Built Build(Job job) {
	const auto html = job.read();
	if (html.isEmpty()) {
		return { .error = u"Could not read the widget file."_q };
	}
	try {
		auto host = HostPointer(
			new twidget::WidgetHost(
				std::string(html.constData(), size_t(html.size())),
				std::move(job.theme),
				job.width,
				job.options,
				std::move(job.resources),
				std::move(job.factory),
				std::move(job.intl)),
			HostDeleter{ std::move(job.slot) });
		host->onBridgeCall = std::move(job.bridge);
		host->stoppedLabel = std::move(job.stoppedLabel);
		return { .host = std::move(host) };
	} catch (const std::exception &e) {
		return {
			.error = u"Could not create the sandbox: %1"_q.arg(
				QString::fromUtf8(e.what())),
		};
	}
}

void PostRefresh(not_null<HistoryItem*> item) {
	const auto session = &item->history()->session();
	const auto id = item->fullId();
	crl::on_main(session, [=] {
		const auto owner = &session->data();
		if (const auto found = owner->message(id)) {
			owner->requestItemViewRefresh(found);
		}
	});
}

void DisableAll() {
	auto &registry = Runtimes();
	if (registry.disabled) {
		return;
	}
	registry.disabled = true;
	auto items = std::vector<WidgetItemId>();
	items.reserve(registry.list.size());
	for (const auto &runtime : registry.list) {
		const auto item = runtime->item();
		items.push_back({
			.session = &item->history()->session(),
			.id = item->fullId(),
		});
	}
	DisableWidgetViews(items);
}

} // namespace

struct Runtime::Host {
	HostPointer pointer;
	base::weak_ptr<Runtime> owner;
	uint64 id = 0;
	bool sampling = false;
	bool theming = false;
	bool retired = false;
};

Runtime::Runtime(RuntimeDescriptor &&descriptor)
: _view(descriptor.view)
, _item(descriptor.item)
, _document(descriptor.document)
, _resize(std::move(descriptor.resize))
, _repaint(std::move(descriptor.repaint))
, _heavyChanged(std::move(descriptor.heavyChanged))
, _bridge(std::make_unique<Bridge>(descriptor.item)) {
	Runtimes().list.emplace_back(this);
}

Runtime::~Runtime() {
	auto &list = Runtimes().list;
	list.erase(ranges::remove(list, not_null<Runtime*>(this)), end(list));
	invalidate_weak_ptrs(this);
	_liveLifetime.destroy();
	if (_live) {
		_live->detach();
	}
	if (heavy()) {
		PostCheck();
	}
	if (const auto host = base::take(_host)) {
		host->retired = true;
		Sample(host);
	}
}

bool Runtime::IsQuitPrevent() {
	Drain();
	if (!Busy()) {
		return false;
	}
	LOG(("Tgw prevents quit, stopping widget sandboxes..."));
	return true;
}

bool Runtime::IsExitPrevent(Fn<void()> retry) {
	Drain();
	if (!Busy()) {
		return false;
	}
	// A JS task may still call into app statics until its engine is joined.
	Runtimes().exitRetry = std::move(retry);
	return true;
}

not_null<HistoryView::Element*> Runtime::view() const {
	return _view;
}

not_null<HistoryItem*> Runtime::item() const {
	return _item;
}

Runtime::State Runtime::state() const {
	return _state;
}

bool Runtime::heavy() const {
	return (_state == State::Waiting)
		|| (_state == State::Constructing)
		|| (_state == State::Live);
}

bool Runtime::stopped() const {
	return (_state == State::Stopped);
}

std::optional<int> Runtime::height() const {
	const auto shown = core();
	const auto css = shown ? shown->height() : std::optional<double>();
	return css ? std::make_optional(LogicalHeight(*css)) : std::nullopt;
}

RendererCore *Runtime::core() const {
	return (_live && _live->hasContent()) ? _live.get() : _still.get();
}

RendererCore *Runtime::interactive() const {
	const auto live = _live.get();
	return ((_state == State::Live)
		&& live
		&& (live == core())
		&& live->acceptsInput())
		? live
		: nullptr;
}

void Runtime::setWidth(int width) {
	if (_width == width) {
		return;
	}
	_width = width;
	if ((_live || _still) && !_widthPosted) {
		_widthPosted = true;
		crl::on_main(this, [=] {
			applyWidth();
		});
	}
}

void Runtime::setBubbleColor(const QColor &color) {
	const auto rgba = color.rgba();
	if (_bubbleColor == rgba) {
		return;
	}
	_bubbleColor = rgba;
	_dark = (Ui::CountContrast(color, QColor(255, 255, 255))
		> Ui::CountContrast(color, QColor(0, 0, 0)));
	syncHostTheme();
}

void Runtime::painted() {
	if (heavy() || (stopped() && _displayDeferred)) {
		if (!_visible) {
			PostCheck();
		}
		return;
	} else if (!startable()) {
		return;
	} else if (!_painted) {
		_painted = true;
		SubscribeSession(&_item->history()->session());
	}
	PostCheck();
}

void Runtime::unload() {
	_painted = false;
	_queueSerial = 0;
	if (heavy()) {
		release();
		addNote(u"Released with the unloaded messages."_q);
	}
	if (_still) {
		_still->dropFrame();
	}
}

QString Runtime::logText() const {
	auto lines = QStringList();
	lines.push_back(u"State: %1%2, generation %3, %4."_q.arg(
		StateName(_state),
		_queueSerial ? u" (waiting for a free sandbox)"_q : QString(),
		QString::number(_generation),
		_visible ? u"on screen"_q : u"off screen"_q));
	if (_state == State::Stopped) {
		lines.push_back(u"Stopped%1: %2"_q.arg(
			_stopFatal ? u" (fatal)"_q : QString(),
			_stopReason));
	}
	const auto updating = _logDone ? u", updating"_q : QString();
	if (_summaryHost) {
		const auto current = _host && (_host->id == _summaryHost);
		lines.push_back(u"%1 sandbox%2:"_q.arg(
			current ? u"Current"_q : u"Last"_q,
			updating));
		lines.push_back(_summary);
	} else if (_lastHost) {
		lines.push_back(u"No sandbox diagnostics yet%1."_q.arg(updating));
	} else {
		lines.push_back(u"No sandbox has run in this view."_q);
	}
	lines.push_back(u"Notes:"_q);
	for (const auto &note : _notes) {
		lines.push_back(note);
	}
	return Clip(lines.join(QChar('\n')), kLogBytes / int(sizeof(QChar)));
}

void Runtime::requestLog(Fn<void(const QString &text)> done) {
	if (Runtimes().draining || Core::Quitting()) {
		return;
	} else if (_host) {
		_logDone = std::move(done);
		_logHost = _host->id;
		Sample(_host);
	} else if (_lastHost
		&& (_summaryHost != _lastHost || !_summaryLast)) {
		_logDone = std::move(done);
		_logHost = _lastHost;
	} else {
		_logDone = nullptr;
	}
}

void Runtime::SubscribeSession(not_null<Main::Session*> session) {
	if (!Runtimes().sessions.emplace(session).second) {
		return;
	}
	session->data().itemVisibilitiesUpdates(
	) | rpl::on_next([] {
		PostCheck();
	}, session->lifetime());
	session->lifetime().add([=] {
		Runtimes().sessions.remove(session);
	});
}

void Runtime::PostCheck() {
	auto &registry = Runtimes();
	if (registry.checkPosted || registry.draining) {
		return;
	}
	registry.checkPosted = true;
	crl::on_main([] {
		Runtimes().checkPosted = false;
		Check();
	});
}

void Runtime::Check() {
	if (Runtimes().draining) {
		return;
	}
	RefreshVisibility();
	ScheduleStarts();
}

void Runtime::Drain() {
	auto &registry = Runtimes();
	if (registry.draining) {
		return;
	}
	registry.draining = true;
	if (StopResources([] { JobDone(); })) {
		++registry.jobs;
	}
	auto runtimes = std::vector<base::weak_ptr<Runtime>>();
	runtimes.reserve(registry.list.size());
	for (const auto &runtime : registry.list) {
		runtimes.emplace_back(runtime.get());
	}
	for (const auto &weak : runtimes) {
		if (const auto strong = weak.get()) {
			strong->_logDone = nullptr;
			strong->release();
		}
	}
}

void Runtime::Sample(const std::shared_ptr<Host> &host) {
	auto &registry = Runtimes();
	if (host->sampling) {
		return;
	} else if (host->retired && (registry.draining || !host->owner)) {
		host->pointer = nullptr;
		return;
	}
	const auto last = host->retired;
	const auto request = ++registry.requestSerial;
	auto pointer = last ? base::take(host->pointer) : host->pointer;
	host->sampling = true;
	++registry.jobs;
	crl::async([
		host = host,
		pointer = std::move(pointer),
		request,
		last
	]() mutable {
		auto text = Summarize(*pointer);
		pointer = nullptr;
		crl::on_main([
			host = std::move(host),
			text = std::move(text),
			request,
			last
		]() mutable {
			SampleDone(host, request, last, std::move(text));
		});
	});
}

void Runtime::SampleDone(
		const std::shared_ptr<Host> &host,
		uint64 request,
		bool last,
		QString text) {
	host->sampling = false;
	if (const auto owner = host->owner.get()) {
		owner->applySample(host->id, request, last, std::move(text));
	}
	if (host->retired && host->pointer) {
		Sample(host);
	}
	JobDone();
}

void Runtime::RefreshVisibility() {
	auto &registry = Runtimes();
	auto changed = std::vector<not_null<Runtime*>>();
	for (const auto &runtime : registry.list) {
		if ((runtime->heavy()
				|| runtime->_painted
				|| runtime->_displayDeferred)
			&& (runtime->queryVisible() != runtime->_visible)) {
			changed.push_back(runtime);
		}
	}
	for (const auto &runtime : changed) {
		runtime->_visible = !runtime->_visible;
		if (!runtime->_visible) {
			runtime->_hiddenSerial = ++registry.hiddenSerial;
		}
		runtime->syncHostVisibility();
	}
}

void Runtime::ScheduleStarts() {
	auto &registry = Runtimes();
	const auto serial = registry.queueSerial;
	auto queue = std::vector<not_null<Runtime*>>();
	auto waiting = 0;
	auto owned = 0;
	auto visibleOwned = 0;
	for (const auto &runtime : registry.list) {
		if (runtime->heavy()) {
			++owned;
			if (runtime->_state == State::Waiting) {
				++waiting;
			}
			if (runtime->_visible) {
				++visibleOwned;
			}
		} else if (!runtime->wantsStart()) {
			runtime->_queueSerial = 0;
		} else {
			if (!runtime->_queueSerial) {
				runtime->_queueSerial = ++registry.queueSerial;
			}
			queue.push_back(runtime);
		}
	}
	ranges::sort(queue, ranges::less(), [](not_null<Runtime*> runtime) {
		return runtime->_queueSerial;
	});
	const auto capacity = std::max(
		kMaxAlive,
		visibleOwned + int(queue.size()));
	auto occupied = registry.reserved + waiting;
	auto left = int(queue.size());
	for (const auto &runtime : queue) {
		if (occupied >= capacity) {
			break;
		}
		runtime->_queueSerial = 0;
		runtime->start();
		++occupied;
		++owned;
		--left;
	}
	for (const auto &runtime : queue) {
		if (runtime->_queueSerial > serial) {
			runtime->addNote(u"Waiting for a free sandbox."_q);
		}
	}
	while (owned + left > capacity) {
		const auto victim = FindVictim();
		if (!victim) {
			break;
		}
		victim->release();
		victim->addNote(u"Released off screen to stay in the limit."_q);
		--owned;
	}
}

Runtime *Runtime::FindVictim() {
	auto result = (Runtime*)nullptr;
	for (const auto &runtime : Runtimes().list) {
		if (runtime->heavy()
			&& !runtime->_visible
			&& (!result || runtime->_hiddenSerial < result->_hiddenSerial)) {
			result = runtime.get();
		}
	}
	return result;
}

bool Runtime::startable() const {
	return (_state != State::Stopped)
		&& (_state != State::Failed)
		&& !Runtimes().disabled
		&& !Runtimes().draining
		&& !Core::Quitting();
}

bool Runtime::wantsStart() const {
	return _painted && _visible && !heavy() && startable();
}

bool Runtime::queryVisible() const {
	return _item->history()->owner().queryItemVisibility(
		_item,
		_view.get());
}

void Runtime::start() {
	if (heavy() || !startable()) {
		return;
	}
	const auto generation = ++_generation;
	_state = State::Waiting;
	addNote(u"Starting."_q);
	heavyStateChanged(false);
	PrepareResources(crl::guard(this, [=](
			std::shared_ptr<twidget::SandboxResources> resources) {
		resourcesReady(generation, std::move(resources));
	}));
}

void Runtime::release() {
	if (heavy()) {
		retire(State::Released);
	}
}

void Runtime::resourcesReady(
		int generation,
		std::shared_ptr<twidget::SandboxResources> resources) {
	if (generation != _generation || _state != State::Waiting) {
		return;
	} else if (!resources) {
		failResources();
		return;
	} else if (Core::Quitting()) {
		release();
		return;
	}
	construct(std::move(resources));
}

void Runtime::construct(
		std::shared_ptr<twidget::SandboxResources> resources) {
	auto reader = PrepareContentReader(_document);
	if (!reader) {
		failContent(
			u"The widget file changed or is unavailable."_q,
			std::nullopt);
		return;
	}
	const auto width = CssWidth(_width);
	const auto ratio = CoreRatio();
	_live = std::make_unique<RendererCore>(width, ratio);
	subscribeLive();
	_state = State::Constructing;
	_hostVisible = _visible;
	_hostDark = _dark;
	auto job = Job{
		.read = std::move(reader->read),
		.resources = std::move(resources),
		.intl = CreateIntl(),
		.factory = _live->hostFactory(),
		.bridge = _live->hostBridgeHandler(),
		.options = DefaultHostOptions(ratio, _visible),
		.theme = ThemeName(_dark),
		.stoppedLabel = tr::lng_tgw_stopped(tr::now).toStdString(),
		.slot = std::make_shared<Reservation>([] { Check(); }),
		.width = width,
	};
	const auto weak = base::make_weak(this);
	const auto generation = _generation;
	const auto source = reader->generation;
	++Runtimes().jobs;
	crl::async([=, job = std::move(job)]() mutable {
		auto built = Build(std::move(job));
		crl::on_main([=, built = std::move(built)]() mutable {
			if (const auto strong = weak.get()) {
				strong->constructed(
					generation,
					source,
					base::take(built.host),
					built.error);
			}
			built.host = nullptr;
			JobDone();
		});
	});
}

void Runtime::constructed(
		int generation,
		int source,
		std::shared_ptr<twidget::WidgetHost> host,
		const QString &error) {
	if (generation != _generation || _state != State::Constructing) {
		return;
	} else if (!host) {
		failContent(error, source);
		return;
	} else if (Core::Quitting()) {
		release();
		return;
	} else if (_hostDark != _dark) {
		// Its engine would boot in the stale theme ahead of setTheme().
		release();
		addNote(u"Released, the theme changed before it started."_q);
		_repaint();
		return;
	}
	_host = std::make_shared<Host>(Host{
		.pointer = std::move(host),
		.owner = base::make_weak(this),
		.id = ++Runtimes().hostSerial,
	});
	_lastHost = _host->id;
	_logDone = nullptr;
	_state = State::Live;
	addNote(u"Started."_q);
	const auto width = CssWidth(_width);
	if (_live->width() != width) {
		_live->setWidth(width);
	}
	syncHostVisibility();
	_host->pointer->start();
}

void Runtime::retire(State state) {
	const auto wasHeavy = heavy();
	++_generation;
	_state = state;
	_painted = false;
	_queueSerial = 0;
	_liveLifetime.destroy();
	if (_live) {
		_live->detach();
		if (_live->hasContent()) {
			_still = std::move(_live);
		} else {
			_live = nullptr;
		}
	}
	if (const auto host = base::take(_host)) {
		host->retired = true;
		Sample(host);
	}
	heavyStateChanged(wasHeavy);
	if (wasHeavy) {
		PostCheck();
	}
}

void Runtime::failContent(
		const QString &error,
		std::optional<int> source) {
	retire(State::Failed);
	addNote(error);
	if (source) {
		LOG(("Tgw Error: %1").arg(error));
		MarkContentUnavailable(_document, *source, _item->fullId());
	} else {
		PostRefresh(_item);
	}
}

void Runtime::failResources() {
	retire(State::Failed);
	addNote(u"The sandbox resources are unavailable."_q);
	DisableAll();
}

void Runtime::subscribeLive() {
	using Change = RendererCore::Change;
	const auto renderer = _live.get();
	renderer->updates(
	) | rpl::on_next([=](RendererCore::Changes changes) {
		if (changes & Change::Stopped) {
			coreStopped(renderer);
		} else {
			coreUpdated(
				renderer,
				bool(changes & (Change::Repaint | Change::Content)));
		}
	}, _liveLifetime);

	renderer->bridgeCalls(
	) | rpl::on_next([=](const RendererCore::BridgeCall &call) {
		bridgeCall(renderer, call.method, call.arguments);
	}, _liveLifetime);
}

void Runtime::coreUpdated(not_null<RendererCore*> renderer, bool repaint) {
	if (renderer.get() != _live.get()) {
		return;
	}
	if (_still && renderer->hasContent()) {
		_still = nullptr;
	}
	displayChanged(repaint && (renderer.get() == core()));
}

void Runtime::coreStopped(not_null<RendererCore*> renderer) {
	if (renderer.get() != _live.get() || _state != State::Live) {
		return;
	}
	_stopReason = Clip(renderer->stopReason());
	_stopFatal = renderer->stopFatal();
	retire(State::Stopped);
	addNote(u"Stopped%1: %2"_q.arg(
		_stopFatal ? u" (fatal)"_q : QString(),
		_stopReason));
	displayChanged(true);
}

void Runtime::bridgeCall(
		not_null<RendererCore*> renderer,
		const std::string &method,
		const std::string &arguments) {
	if (_state != State::Live
		|| renderer.get() != _live.get()
		|| !renderer->attached()) {
		return;
	}
	const auto owner = &_item->history()->owner();
	if (!owner->queryItemVisibility(_item, _view.get())) {
		addNote(u"Bridge call refused, the widget is not visible."_q);
		return;
	}
	const auto weak = base::make_weak(this);
	const auto result = _bridge->call(method, arguments);
	if (weak) {
		addNote(result);
	}
}

void Runtime::syncHostVisibility() {
	if (_live) {
		_live->setOnScreen(_visible);
	}
	if (_visible) {
		postShownDisplay();
	}
	if (_host && _hostVisible != _visible) {
		_hostVisible = _visible;
		_host->pointer->setVisible(_visible);
	}
}

void Runtime::syncHostTheme() {
	if (!_host || _host->theming || _hostDark == _dark) {
		return;
	}
	_hostDark = _dark;
	_host->theming = true;
	addNote(_dark
		? u"Switched to the dark theme."_q
		: u"Switched to the light theme."_q);
	++Runtimes().jobs;
	crl::async([
		host = _host,
		pointer = _host->pointer,
		theme = ThemeName(_dark)
	]() mutable {
		ApplyTheme(*pointer, theme);
		pointer = nullptr;
		crl::on_main([host = std::move(host)] {
			ThemeDone(host);
		});
	});
}

void Runtime::ThemeDone(const std::shared_ptr<Host> &host) {
	host->theming = false;
	const auto owner = host->owner.get();
	if (owner && owner->_host == host) {
		owner->syncHostTheme();
	}
	JobDone();
}

void Runtime::applyWidth() {
	_widthPosted = false;
	const auto width = CssWidth(_width);
	auto repaint = false;
	if (_still && _still->width() != width) {
		_still->setWidth(width);
		repaint = (_still.get() == core());
	}
	if (_live && _live->width() != width) {
		_live->setWidth(width);
	}
	displayChanged(repaint);
}

void Runtime::displayChanged(bool repaint) {
	const auto now = height();
	const auto resize = (_shownHeight != now);
	if (!resize && !repaint) {
		return;
	} else if (!_visible
		&& (_state == State::Live || _state == State::Stopped)) {
		_displayDeferred = true;
		return;
	}
	_displayDeferred = false;
	if (resize) {
		_shownHeight = now;
		_resize();
	} else {
		_repaint();
	}
}

void Runtime::postShownDisplay() {
	const auto shown = core();
	if (shown && shown->renderer()->dirty()) {
		_displayDeferred = true;
	}
	if (!_displayDeferred || _displayPosted) {
		return;
	}
	_displayPosted = true;
	crl::on_main(this, [=] {
		_displayPosted = false;
		if (base::take(_displayDeferred)) {
			displayChanged(true);
		}
	});
}

void Runtime::heavyStateChanged(bool wasHeavy) {
	if (heavy() != wasHeavy) {
		_heavyChanged();
	}
}

void Runtime::addNote(const QString &text) {
	_notes.push_back(
		QTime::currentTime().toString(u"HH:mm:ss "_q) + Clip(text));
	if (int(_notes.size()) > kMaxNotes) {
		_notes.erase(begin(_notes));
	}
}

void Runtime::applySample(
		uint64 host,
		uint64 request,
		bool last,
		QString text) {
	if (host < _summaryHost
		|| (host == _summaryHost && request < _summaryRequest)) {
		return;
	}
	_summary = std::move(text);
	_summaryHost = host;
	_summaryRequest = request;
	_summaryLast = last;
	if (_logDone
		&& (_logHost == host)
		&& (last || _host)
		&& !Runtimes().draining) {
		const auto done = base::take(_logDone);
		done(logText());
	}
}

Runtime *FindRuntime(not_null<const HistoryView::Element*> view) {
	for (const auto &runtime : Runtimes().list) {
		if (runtime->view().get() == view.get()) {
			return runtime.get();
		}
	}
	return nullptr;
}

} // namespace Tgw
