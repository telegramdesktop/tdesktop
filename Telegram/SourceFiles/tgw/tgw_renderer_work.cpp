/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_renderer_work.h"

#include "test/test_widget_ops.h"
#include "tgw/tgw_backend.h"
#include "tgw/tgw_kept_budget.h"
#include "tgw/tgw_painter.h"
#include "tgw/tgw_render_thread.h"
#include "tgw/tgw_renderer_input.h"
#include "tgw/tgw_renderer_platform.h"
#include "tgw/tgw_renderer_rules.h"
#include "twidget/sandbox_engine.h"

#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
#include <private/qfont_p.h>
#include <private/qfontengine_p.h>
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API

#include "styles/style_basic.h"

namespace Tgw {
namespace {

constexpr auto kPaintBoundsKind = "paint-bounds";
constexpr auto kRendererStopDetail = "The widget renderer stopped";
constexpr auto kMinWakeDelay = crl::time(1);
constexpr auto kMaxWakeDelay = crl::time(std::numeric_limits<int>::max());
constexpr auto kMaxStopHold = crl::time(
	twidget::SandboxEngine::kPlatformAnswerSeconds * 1000);
// Mirrors lib_ui's private kAnimationTick, the cadence every clock runs at.
constexpr auto kHeldTick = crl::time(1000) / st::universalDuration;

using WorkRegistry = base::flat_map<uint64, std::unique_ptr<RendererWork>>;

struct FrameAsk {
	uint64 seq = 0;
	uint64 serial = 0;
	uint64 epoch = 0;
	bool tick = false;
};

[[nodiscard]] bool FitsCard(
		QSize frame,
		QSize card,
		int ratio,
		bool autoHeight) {
	return (std::abs(frame.width() - card.width()) <= ratio)
		&& (!autoHeight
			|| (std::abs(frame.height() - card.height()) <= ratio));
}

[[nodiscard]] bool FitsHeight(
		QSize cardArea,
		const RendererCut &cut,
		std::optional<double> height,
		int scale) {
	return height
		&& (!cut.autoHeight
			|| (LogicalHeight(*height, scale) == cardArea.height()));
}

[[nodiscard]] bool FitsLayout(
		QSize device,
		QSize cardArea,
		const RendererCut &cut,
		std::optional<double> height,
		int scale) {
	return FitsCard(device, cardArea * cut.ratio, cut.ratio, cut.autoHeight)
		&& FitsHeight(cardArea, cut, height, scale);
}

[[nodiscard]] Images::CornersMaskRef CornersOf(const RendererCut &cut) {
	auto result = Images::CornersMaskRef();
	for (auto i = 0; i != 4; ++i) {
		if (!cut.corners[i].isNull()) {
			result.p[i] = &cut.corners[i];
		}
	}
	return result;
}

[[nodiscard]] QRect CornerPatch(
		Images::CornersMaskRef corners,
		int index,
		QSize card) {
	const auto mask = corners.p[index];
	if (!mask
		|| (mask->width() > card.width())
		|| (mask->height() > card.height())) {
		return QRect();
	}
	const auto right = (index == Images::kTopRight)
		|| (index == Images::kBottomRight);
	const auto bottom = (index == Images::kBottomLeft)
		|| (index == Images::kBottomRight);
	return QRect(
		QPoint(
			right ? (card.width() - mask->width()) : 0,
			bottom ? (card.height() - mask->height()) : 0),
		mask->size());
}

[[nodiscard]] QImage RoundFrame(
		QImage &&frame,
		Images::CornersMaskRef corners,
		QSize card) {
	const auto bounds = QRect(QPoint(), frame.size());
	for (auto i = 0; i != 4; ++i) {
		const auto patch = CornerPatch(corners, i, card);
		const auto target = patch.intersected(bounds);
		if (target.isEmpty()) {
			continue;
		}
		const auto mask = corners.p[i];
		const auto part = (target == patch)
			? *mask
			: mask->copy(target.translated(-patch.topLeft()));
		auto single = Images::CornersMaskRef();
		single.p[Images::kTopLeft] = &part;
		frame = Images::Round(std::move(frame), single, target);
	}
	return std::move(frame);
}

[[nodiscard]] crl::time WakeDelay(double seconds) {
	return crl::time(std::clamp(
		std::ceil(seconds * 1000.),
		double(kMinWakeDelay),
		double(kMaxWakeDelay)));
}

[[nodiscard]] bool ValidState(const twlite::remote::State &state) {
	return std::isfinite(state.width)
		&& (state.width >= 0.)
		&& (state.height >= 0)
		&& (state.next_tick_in >= 0.);
}

[[nodiscard]] std::optional<QSize> ReportedSize(
		const twlite::remote::State &state,
		double dpr) {
	const auto width = std::ceil(state.width * dpr);
	const auto height = std::ceil(state.height * dpr);
	const auto most = double(std::numeric_limits<int>::max());
	if ((width > most) || (height > most)) {
		return std::nullopt;
	}
	return QSize(int(width), int(height));
}

void BeginFrame(QtBackend &backend, RendererPlatform &platform) {
	if (backend.beginFrame()) {
		platform.refillCanvasFonts();
	}
}

[[nodiscard]] WorkRegistry &Works() {
	static auto &result = *new WorkRegistry();
	return result;
}

[[nodiscard]] Fn<QImage(const Fn<QImage()> &)> Acquiring(
		KeptBudget::Backing &backing) {
	return [&backing](const Fn<QImage()> &make) {
		const auto acquisition = KeptBudget::Acquisition();
		auto result = make();
		backing = acquisition.count(result);
		return result;
	};
}

#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
[[nodiscard]] FontPosition PositionOf(const QFontDef &def, int count) {
	return {
		.family = def.families.value(0),
		.styleName = def.styleName,
		.pixelSize = def.pixelSize,
		.weight = int(def.weight),
		.style = int(def.style),
		.stretch = int(def.stretch),
		.hint = int(def.styleHint),
		.strategy = int(def.styleStrategy),
		.hinting = int(def.hintingPreference),
		.count = count,
	};
}

[[nodiscard]] bool SameRequest(
		const FontPosition &a,
		const FontPosition &b) {
	return (a.family == b.family)
		&& (a.styleName == b.styleName)
		&& (a.pixelSize == b.pixelSize)
		&& (a.weight == b.weight)
		&& (a.style == b.style)
		&& (a.stretch == b.stretch)
		&& (a.hint == b.hint)
		&& (a.strategy == b.strategy)
		&& (a.hinting == b.hinting);
}
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API

} // namespace

void RendererReflow::demand() {
	_owed = Owed::Demand;
}

void RendererReflow::settle(bool blocked) {
	if (!blocked || (_owed != Owed::Demand)) {
		_owed = Owed::None;
	}
}

void RendererReflow::asked() {
	if (_owed == Owed::Correction) {
		_correction = false;
	}
}

void RendererReflow::replied() {
	if (_owed == Owed::Correction) {
		_owed = Owed::None;
	}
}

void RendererReflow::served(RendererGeometry geometry) {
	const auto client = !_served
		|| (_served->serial != geometry.serial)
		|| (_served->epoch != geometry.epoch)
		|| (_served->cut != geometry.cut);
	_correction = _correction || client || (_owed == Owed::Demand);
	_served = std::move(geometry);
	_owed = Owed::None;
	_told = std::nullopt;
}

void RendererReflow::follow(const RendererGeometry &geometry) {
	if (_correction
		&& (_owed == Owed::None)
		&& _served
		&& (_served->serial == geometry.serial)
		&& (_served->epoch == geometry.epoch)
		&& (*_served != geometry)) {
		_owed = Owed::Correction;
	}
}

std::optional<bool> RendererReflow::outdated(
		const RendererGeometry &geometry) {
	if (!_served
		|| (_owed != Owed::None)
		|| (_served->serial != geometry.serial)
		|| (_served->epoch != geometry.epoch)
		|| (_told == geometry)) {
		return std::nullopt;
	} else if (*_served == geometry) {
		return base::take(_told)
			? std::make_optional(false)
			: std::nullopt;
	}
	_told = geometry;
	return true;
}

bool RendererReflow::owed() const {
	return (_owed != Owed::None);
}

bool RendererReflow::shows(const RendererGeometry &geometry) const {
	return (_served == geometry);
}

void PicturePost::offer(RendererLayout layout) {
	auto dropped = std::optional<RendererLayout>();
	const auto lock = std::lock_guard(_mutex);
	if (_closed) {
		return;
	} else if (_latest) {
		layout.client = layout.client || _latest->client;
	}
	dropped = std::exchange(_latest, std::move(layout));
	_offered = true;
	_outdated = false;
}

uint64 PicturePost::send(bool answer) {
	const auto lock = std::lock_guard(_mutex);
	auto result = uint64();
	if (answer) {
		result = _answered = ++_sent;
	}
	if (!std::exchange(_offered, false)) {
		return result;
	} else if (answer) {
		_from = result;
	} else {
		_from = _answered + 1;
		if (_sent <= std::max(_answered, _received)) {
			result = ++_sent;
		}
	}
	return result;
}

PicturePost::Withdrawn PicturePost::withdraw() {
	auto dropped = std::optional<RendererLayout>();
	{
		const auto lock = std::lock_guard(_mutex);
		dropped = base::take(_latest);
		_offered = false;
	}
	return {
		.layout = dropped.has_value(),
		.client = dropped && dropped->client,
	};
}

std::optional<RendererLayout> PicturePost::take(
		uint64 post,
		uint64 serial,
		uint64 epoch) {
	const auto lock = std::lock_guard(_mutex);
	_received = post;
	// An obsolete picture stays for the queued resize or drop to withdraw.
	return (_latest
		&& !_offered
		&& (_from <= post)
		&& (_latest->serial == serial)
		&& (_latest->epoch == epoch))
		? base::take(_latest)
		: std::nullopt;
}

void PicturePost::setOutdated(bool outdated) {
	const auto lock = std::lock_guard(_mutex);
	_outdated = outdated;
}

bool PicturePost::takeOutdated() {
	const auto lock = std::lock_guard(_mutex);
	return base::take(_outdated);
}

void PicturePost::close() {
	auto dropped = std::optional<RendererLayout>();
	const auto lock = std::lock_guard(_mutex);
	_closed = true;
	_offered = false;
	dropped = base::take(_latest);
}

struct RendererWork::Hosted {
	std::unique_ptr<RendererPlatform> platform;
	std::unique_ptr<HostedInput> input;
	std::shared_ptr<PicturePost> pictures;
	twlite::remote::State state;
	std::optional<FrameAsk> asked;
	base::Timer stopBound;
	crl::time tickAt = 0;
	bool paintFirst = false;
	bool blocked = false;
	bool held = false;
	bool closed = false;
	bool reportClosed = false;
	bool releaseLive = false;
	bool limited = false;
	bool stopHeld = false;
};

#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
std::vector<FontPosition> CurrentFontPositions() {
	auto result = std::vector<FontPosition>();
	auto seen = base::flat_set<const QFontEngine*>();
	const auto &engines = QFontCache::instance()->engineCache;
	for (auto i = engines.cbegin(); i != engines.cend(); ++i) {
		const auto engine = i.value().data;
		if (i.key().multi
			|| !engine
			|| (engine->m_subPixelPositionCount <= 0)
			|| !seen.emplace(engine).second) {
			continue;
		}
		result.push_back(
			PositionOf(i.key().def, engine->m_subPixelPositionCount));
	}
	return result;
}

void AdoptFontPositions(
		const QFont &font,
		const std::vector<FontPosition> &positions) {
	if (positions.empty()) {
		return;
	}
	auto engine = QFontPrivate::get(font)->engineForScript(
		QChar::Script_Common);
	if (engine && (engine->type() == QFontEngine::Multi)) {
		engine = static_cast<QFontEngineMulti*>(engine)->engine(0);
	}
	if (!engine || engine->m_subPixelPositionCount) {
		return;
	}
	const auto &engines = QFontCache::instance()->engineCache;
	for (auto i = engines.cbegin(); i != engines.cend(); ++i) {
		if (i.key().multi || (i.value().data != engine)) {
			continue;
		}
		const auto request = PositionOf(i.key().def, 0);
		const auto j = ranges::find_if(positions, [&](
				const FontPosition &position) {
			return SameRequest(position, request);
		});
		if (j != end(positions)) {
			// Qt fixes it at the first glyph; the base used the UI's engine.
			engine->m_subPixelPositionCount = j->count;
		}
		return;
	}
}
#else // !DESKTOP_APP_DISABLE_PRIVATE_QT_API
std::vector<FontPosition> CurrentFontPositions() {
	return {};
}

void AdoptFontPositions(const QFont &, const std::vector<FontPosition> &) {
}
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API

QString StalledReason() {
	return u"The widget stopped responding"_q;
}

RendererWork::RendererWork(
	uint64 id,
	base::weak_ptr<RendererCore> chat,
	double dpr,
	const RendererValues &values,
	Host host,
	RendererHosted hosted)
: _id(id)
, _chat(std::move(chat))
, _host(std::move(host))
, _dpr(DeviceRatio(dpr))
, _backend(std::make_unique<QtBackend>(
	_host.diag,
	[=] { backendRefused(); },
	values.regularFamily,
	values.monospaceFamily,
	values.positions))
, _hosted(makeHosted(std::move(hosted)))
, _clock([=] { clockStep(); })
, _wake([=] { wake(); }) {
	ConfigureEmojiTiles(values.emoji, EmojiTilesLanded);
}

RendererWork::~RendererWork() {
	_clock.cancel();
	_wake.cancel();
	releaseInput();
	_hosted->pictures->close();
}

void RendererWork::Create(
		uint64 id,
		base::weak_ptr<RendererCore> chat,
		double dpr,
		const RendererValues &values,
		Host host,
		RendererHosted hosted) {
	Expects(OnRenderThread());

	auto &works = Works();
	works.emplace(
		id,
		std::make_unique<RendererWork>(
			id,
			std::move(chat),
			dpr,
			values,
			std::move(host),
			std::move(hosted)));
}

std::unique_ptr<RendererWork::Hosted> RendererWork::makeHosted(
		RendererHosted hosted) {
	using Frame = RendererPlatform::Frame;
	auto result = std::make_unique<Hosted>();
	result->pictures = hosted.pictures
		? std::move(hosted.pictures)
		: std::make_shared<PicturePost>();
	result->platform = std::make_unique<RendererPlatform>(
		_backend.get(),
		RendererPlatform::Route{
			.send = _host.platform,
			.press = _host.press,
			.released = _host.released,
			.localStop = _host.localStop,
		},
		RendererPlatform::Reports{
			.frame = [=](Frame &&frame) {
				hostedFrame(std::move(frame));
			},
			.state = [=](const twlite::remote::State &state) {
				hostedState(state);
			},
			.size = [=](double height) {
				hostedSize(height);
			},
			.limit = [=] {
				_hosted->limited = true;
				if (_hosted->stopHeld && _onScreen) {
					_reflow.demand();
					schedule();
				}
			},
			.pointer = [=](const twlite::remote::Pointer &pointer) {
				_hosted->input->pointer(pointer);
			},
			.selection = [=](QString text) {
				_hosted->input->selection(std::move(text));
			},
			.claim = [=](const twlite::remote::ClaimAnswer &answer) {
				_hosted->input->claim(answer);
			},
			.cursor = [=](std::string_view cursor) {
				_hosted->input->cursor(cursor);
			},
			.clicked = [=](uint64 press) {
				_hosted->input->clicked(press);
			},
			.content = [=] {
				hostedContent();
			},
			.loaded = [=] {
				hostedLoaded();
			},
			.stopped = [=](QString reason) {
				refuseRoute(std::move(reason), kRendererStopDetail);
			},
			.failed = [=](QString reason) {
				refuse(std::move(reason));
			},
			.held = [=](crl::time) {
				hostedHeld();
			},
			.drained = [=] {
				if (_hosted->stopHeld) {
					schedule();
				}
			},
		},
		std::move(hosted.wait));
	result->input = std::make_unique<HostedInput>(
		result->platform.get(),
		(hosted.reports
			? std::move(hosted.reports)
			: std::make_shared<ReportPost>()),
		HostedInput::Delegate{
			.accepts = [=] {
				return acceptsInput();
			},
			.input = [=] {
				_backend->input();
				resumeInbound();
				refreshClock();
			},
			.schedule = [=] {
				schedule();
			},
			.reports = [=] {
				crl::on_main(_chat, [chat = _chat] {
					chat->takeReports();
				});
			},
			.observed = [=](PressStep step, uint64 press) {
				Test::WidgetPressObserved(_id, press, step);
			},
			.overflow = [=](QString limit) {
				refuse(std::move(limit));
			},
		});
	result->stopBound.setCallback([=] {
		schedule();
	});
	return result;
}

RendererWork *RendererWork::Find(uint64 id) {
	Expects(OnRenderThread());

	const auto &works = Works();
	const auto i = works.find(id);
	return (i != end(works)) ? i->second.get() : nullptr;
}

void RendererWork::Destroy(uint64 id) {
	Expects(OnRenderThread());

	auto &works = Works();
	const auto i = works.find(id);
	if (i == end(works)) {
		return;
	}
	const auto taken = std::move(i->second);
	works.erase(i);
}

void RendererWork::DestroyAll() {
	Expects(OnRenderThread());

	base::take(Works());
}

double RendererWork::DeviceRatio(double dpr) {
	return RendererRules::DeviceRatio(dpr);
}

WidgetMeter::Operation RendererWork::operation() {
	return _backend->operation();
}

bool RendererWork::dirty() const {
	return _hosted->state.dirty;
}

bool RendererWork::wantsTick() const {
	return _hosted->state.wants_tick || _hosted->state.frame_asked;
}

double RendererWork::nextTickIn() const {
	const auto &hosted = *_hosted;
	if (hosted.asked || !hosted.tickAt) {
		return std::numeric_limits<double>::infinity();
	}
	return std::max(hosted.tickAt - crl::now(), crl::time()) / 1000.;
}

int RendererWork::viewportHeight() const {
	return _hosted->state.height;
}

bool RendererWork::routeAlive() const {
	return !_hosted->platform->ended();
}

void RendererWork::platformTraffic(HostedTraffic traffic) {
	if (routeAlive()) {
		_hosted->platform->enqueue(std::move(traffic));
	}
}

void RendererWork::routeEnded() {
	if (routeAlive()) {
		closeRoute(false);
	}
}

void RendererWork::EmojiTilesLanded(std::optional<int> glyph) {
	Expects(OnRenderThread());

	for (const auto &entry : Works()) {
		const auto work = entry.second.get();
		if (work->_emojiWaits.wants(glyph)) {
			work->refreshFrame();
			work->demand();
		}
	}
}

void RendererWork::detach() {
	_detached = _detaching = true;
	_undrawn = _rasterThrottled = false;
	releaseCanvas();
	refreshClock();
	schedule();
}

void RendererWork::finishDetach() {
	cancelInput();
	releaseInput();
	_hosted->input->detach();
}

void RendererWork::setOnScreen(bool onScreen, uint64 visibility) {
	_visibility = visibility;
	if (_onScreen == onScreen) {
		return;
	}
	_onScreen = onScreen;
	_stepBusy = _undrawn = false;
	if (!onScreen) {
		resetInput();
	}
	refreshClock();
	if (onScreen) {
		schedule();
	}
}

bool RendererWork::acceptsInput() const {
	return _hosted->platform->accepting()
		&& canTick()
		&& _hasContent;
}

void RendererWork::pointerEnter(uint64 serial) {
	_hosted->input->enter(serial);
}

void RendererWork::pointerMove(QPointF point, int modifiers, uint64 serial) {
	_hosted->input->move(point, modifiers, serial);
}

void RendererWork::pointerLeave(uint64 serial) {
	_hosted->input->leave(serial);
}

void RendererWork::pointerPress(
		QPointF point,
		int modifiers,
		uint64 serial,
		uint64 selection) {
	_hosted->input->press(point, modifiers, serial, selection);
}

void RendererWork::pointerRelease(
		QPointF point,
		int modifiers,
		bool cancel,
		uint64 serial) {
	_hosted->input->release(point, modifiers, cancel, serial);
}

void RendererWork::menuPress(QPointF point, int modifiers, uint64 serial) {
	_hosted->input->menuPress(point, modifiers, serial);
}

void RendererWork::menuDecided(uint64 serial, bool deliver) {
	_hosted->input->menuDecided(serial, deliver);
}

void RendererWork::menuRelease(int modifiers, bool cancel) {
	_hosted->input->menuRelease(modifiers, cancel);
}

void RendererWork::fingerPress(QPointF point, int modifiers, uint64 serial) {
	_hosted->input->fingerPress(point, modifiers, serial);
}

void RendererWork::fingerMove(QPointF point, int modifiers) {
	_hosted->input->fingerMove(point, modifiers);
}

void RendererWork::fingerHold() {
	if (_hosted->input->fingerHold(_hosted->asked.has_value())) {
		askTick();
	}
}

void RendererWork::fingerRelease(QPointF point, int modifiers) {
	_hosted->input->fingerRelease(point, modifiers);
}

void RendererWork::fingerCancel() {
	_hosted->input->fingerCancel();
}

void RendererWork::reportInput(RendererAnswer &answer) {
	_hosted->input->report(answer, !_detached && !_stopped);
}

void RendererWork::clearSelection(uint64 serial) {
	_hosted->input->clearSelection(serial);
}

void RendererWork::setSize(const RendererSize &size, uint64 serial) {
	_size = size;
	_sizeSerial = serial;
	_sizePending = true;
	_undrawn = false;
	const auto withdrawn = _hosted->pictures->withdraw();
	const auto superseded = canTick() && _hosted->asked.has_value();
	_client = _client || withdrawn.client;
	if (withdrawn.layout || superseded) {
		_reflow.demand();
	}
	schedule();
}

void RendererWork::setCut(const RendererCut &cut) {
	if (_cut && (*_cut == cut)) {
		return;
	}
	_cut = cut;
	_undrawn = false;
	demand();
}

void RendererWork::demand() {
	_reflow.demand();
	_hosted->blocked = false;
	schedule();
}

void RendererWork::drawn() {
	if (base::take(_undrawn)) {
		schedule();
	}
}

void RendererWork::dropFrame(uint64 epoch) {
	_epoch = epoch;
	_reflow = RendererReflow();
	_undrawn = false;
	_emojiWaits.clear();
	dropCanvas();
	if (_hosted->pictures->withdraw().client) {
		_client = true;
	}
}

void RendererWork::refreshFrame() {
	// A paint-stopped core's replay is refused, so its last picture stays.
	_painted.device = QSize();
}

void RendererWork::schedule() {
	if (_scheduled) {
		return;
	}
	PostToRenderThread(crl::guard(this, [=] {
		work();
	}));
	_scheduled = true;
}

void RendererWork::work() {
	const auto operation = _backend->operation();
	_scheduled = false;
	if (_host.gone()) {
		return;
	} else if (base::take(_detaching)) {
		finishDetach();
	}
	if (base::take(_hosted->releaseLive)) {
		_backend->releaseLive();
	}
	applySizeRequest();
	const auto follows = _detached && _cut;
	if (follows && !(_hosted->asked && !_hosted->asked->tick)) {
		_reflow.follow(geometry());
	}
	const auto refused = _backend->refused();
	if (refused) {
		refuse(_backend->refusal());
	}
	const auto wanted = !refused && rasterWanted();
	const auto throttled = wanted
		&& ((_backend->heldFor() > 0) || inboundFirst());
	_hosted->paintFirst = false;
	if (wanted && !throttled) {
		raster();
	}
	if (_hosted->stopHeld) {
		const auto waits = routeAlive()
			&& !_detached
			&& _hosted->stopBound.isActive()
			&& (_hosted->platform->waiting()
				|| (_hosted->limited
					&& _onScreen
					&& (_hosted->asked || throttled)));
		if (!waits) {
			_hosted->stopHeld = _hosted->limited = false;
			_hosted->stopBound.cancel();
			_postedChanges |= Change::Stopped | Change::Repaint;
		}
	}
	if (throttled) {
		_rasterThrottled = true;
		if (!_clockOn) {
			scheduleWake();
		}
	} else {
		_rasterThrottled = false;
		if (!awaitingFrame()) {
			_reflow.settle(_hosted->blocked);
			if (follows) {
				if (const auto outdated = _reflow.outdated(geometry())) {
					_hosted->pictures->setOutdated(*outdated);
				}
			}
		}
	}
	auto answer = RendererAnswer();
	reportInput(answer);
	answer.closed = base::take(_hosted->reportClosed);
	const auto changes = base::take(_postedChanges);
	const auto stopped = bool(changes & Change::Stopped);
	const auto ordered = stopped
		|| (changes & (Change::Content | Change::Loaded))
		|| answer.menu
		|| answer.claim
		|| answer.closed;
	answer.post = _hosted->pictures->send(ordered);
	if (!answer.post) {
		return;
	}
	answer.state = { .hasContent = _hasContent, .loaded = _loaded };
	if (stopped) {
		answer.stop = RendererStop{ _stopReason, _stopFatal };
	}
	deliver(std::move(answer));
}

void RendererWork::applySizeRequest() {
	if (!base::take(_sizePending)) {
		return;
	}
	_client = true;
	_hosted->blocked = false;
	if (routeAlive()) {
		_hosted->platform->setWidth(_size.width);
		_hosted->platform->setMaxHeight(_size.maxHeight);
	}
}

bool RendererWork::rasterWanted() const {
	return _loaded
		&& _cut
		&& (_size.cardWidth > 0)
		&& (_reflow.owed() || (canTick() && !_undrawn));
}

RendererGeometry RendererWork::geometry() const {
	return {
		.cut = *_cut,
		.device = frameSize().value_or(QSize()),
		.area = layoutArea(),
		.height = _height,
		.serial = _sizeSerial,
		.epoch = _epoch,
	};
}

bool RendererWork::geometryPainted() const {
	const auto now = geometry();
	return (_painted == now)
		&& (_frame.isNull() || framed(now.cut, now.area));
}

QSize RendererWork::layoutArea() const {
	const auto css = _cut->autoHeight
		? std::clamp(double(viewportHeight()), kMinHeight, kMaxHeight)
		: _size.maxHeight;
	return QSize(_size.cardWidth, LogicalHeight(css, _size.scale));
}

std::optional<QSize> RendererWork::frameSize() const {
	if (!_height) {
		return std::nullopt;
	}
	const auto height = std::min(*_height, _size.maxHeight);
	const auto width = std::ceil(_size.width * _dpr);
	const auto rows = std::ceil(height * _dpr);
	const auto valid = [](double side) {
		return std::isfinite(side)
			&& (side > 0.)
			&& (side <= double(std::numeric_limits<int>::max()));
	};
	if (!valid(width) || !valid(rows)) {
		return std::nullopt;
	}
	return QSize(int(width), int(rows));
}

bool RendererWork::framed(const RendererCut &cut, QSize cardArea) const {
	return !_frame.isNull()
		&& (_painted.area == cardArea)
		&& (_painted.cut == cut)
		&& (_frame.devicePixelRatio() == cut.ratio)
		&& FitsCard(
			_frame.size(),
			cardArea * cut.ratio,
			cut.ratio,
			cut.autoHeight);
}

void RendererWork::raster() {
	// A released core shows its last picture, whatever build it holds.
	if ((_detached || !dirty()) && geometryPainted()) {
		return;
	}
	const auto size = frameSize().value_or(QSize());
	if (!size.isEmpty()
		&& !_hosted->blocked
		&& routeAlive()
		&& !sealedAtStop()) {
		requestPaint();
	}
}

RendererWork::ListPaint RendererWork::paintList(
		const std::vector<twlite::Command> &list,
		QSize size) {
	const auto &cut = *_cut;
	const auto ratio = cut.ratio;
	const auto corners = CornersOf(cut);
	const auto cardArea = layoutArea();
	const auto card = cardArea * ratio;
	const auto fits = [&] {
		return framed(cut, cardArea);
	};
	if (!FitsCard(size, card, ratio, cut.autoHeight)) {
		if (!fits()) {
			dropCanvas();
		}
		return {};
	}
	const auto work = _backend->work();
	const auto started = crl::now();
	auto result = ListPaint();
	auto painted = PaintFrame(
		list,
		*_backend,
		size,
		_dpr,
		started,
		Acquiring(result.backing));
	auto limit = std::move(painted.limit);
	if (limit.isEmpty() || !fits()) {
		_frame = std::move(painted.image);
		if (!_frame.isNull()) {
			_frame = RoundFrame(std::move(_frame), corners, card);
			_frame.setDevicePixelRatio(ratio);
		}
	}
	TakeEmojiWaits(_backend->emoji(), _emojiPainted);
	const auto refused = !limit.isEmpty();
	_painted = {
		.cut = cut,
		.device = size,
		.area = cardArea,
		.height = _height,
		.serial = _sizeSerial,
		.epoch = _epoch,
	};
	if (!refused) {
		result.layout = completed(cardArea);
	}
	if (result.layout) {
		std::swap(_emojiWaits, _emojiPainted);
	} else {
		_emojiWaits.add(_emojiPainted);
	}
	if (refused) {
		refuse(std::move(limit));
	}
	releaseCanvas();
	return result;
}

std::optional<RendererLayout> RendererWork::completed(QSize cardArea) {
	if (_frame.isNull()) {
		return std::nullopt;
	} else if (!FitsHeight(cardArea, *_cut, _height, _size.scale)) {
		_painted.device = QSize();
		return std::nullopt;
	}
	const auto client = base::take(_client) || !_reported || _detached;
	_reported = true;
	return RendererLayout{
		.picture = _frame,
		.area = cardArea,
		.height = *_height,
		.serial = _sizeSerial,
		.visibility = _visibility,
		.epoch = _epoch,
		.bottom = _heightBottom,
		.client = client,
	};
}

void RendererWork::releaseCanvas() {
	_frame = QImage();
}

void RendererWork::dropCanvas() {
	_frame = QImage();
	_painted = RendererGeometry();
}

void RendererWork::deliver(RendererAnswer &&answer) {
	crl::on_main(_chat, [chat = _chat, answer = std::move(answer)]() mutable {
		chat->apply(std::move(answer));
	});
}

bool RendererWork::inboundFirst() const {
	return _hosted->platform->requestWaiting();
}

bool RendererWork::sealedAtStop() const {
	return _stopped
		&& !_detached
		&& !(_hosted->stopHeld && _hosted->limited && _onScreen);
}

bool RendererWork::awaitingFrame() const {
	return _hosted->paintFirst
		|| (_hosted->asked && !_hosted->asked->tick)
		|| (_stopped && !_detached);
}

void RendererWork::requestPaint() {
	auto &hosted = *_hosted;
	if (hosted.asked) {
		hosted.paintFirst = true;
		if (hosted.asked->serial != _sizeSerial) {
			_reflow.demand();
		}
		return;
	} else if (!_detached && !_stopped) {
		BeginFrame(*_backend, *hosted.platform);
	}
	const auto seq = hosted.platform->requestFrame(
		false,
		twlite::remote::Paint::Force);
	if (!seq) {
		return;
	}
	hosted.asked = FrameAsk{
		.seq = seq,
		.serial = _sizeSerial,
		.epoch = _epoch,
	};
	_reflow.asked();
}

void RendererWork::hostedTick() {
	auto &hosted = *_hosted;
	if (base::take(_rasterThrottled) || hosted.paintFirst) {
		schedule();
	}
	if (hosted.paintFirst || hosted.asked || !routeAlive()) {
		return;
	}
	BeginFrame(*_backend, *hosted.platform);
	askTick();
}

void RendererWork::askTick() {
	auto &hosted = *_hosted;
	hosted.input->beforeTick();
	const auto seq = hosted.platform->requestFrame(
		true,
		twlite::remote::Paint::Skip);
	if (seq) {
		hosted.tickAt = 0;
		hosted.asked = FrameAsk{
			.seq = seq,
			.serial = _sizeSerial,
			.epoch = _epoch,
			.tick = true,
		};
	}
}

void RendererWork::hostedFrame(twlite::remote::Frame &&frame) {
	auto &hosted = *_hosted;
	auto asked = base::take(hosted.asked);
	if (!asked || (asked->seq != frame.seq)) {
		refuse(u"The widget renderer sent a frame nothing asked for"_q);
		return;
	}
	if (asked->tick == frame.painted) {
		refuse(asked->tick
			? u"The widget renderer painted a frame asked to tick"_q
			: u"The widget renderer skipped a frame asked to paint"_q);
		return;
	}
	const auto current = (asked->serial == _sizeSerial)
		&& (asked->tick || (_cut && (asked->epoch == _epoch)));
	if (!takeState(frame.state, current)) {
		return;
	} else if (!current) {
		schedule();
	} else if (!asked->tick && !_host.gone()) {
		const auto computed = frameSize();
		if (!computed || (ReportedSize(frame.state, _dpr) != computed)) {
			hosted.blocked = true;
			refreshFrame();
		} else {
			paintFrame(frame.list, *computed);
		}
		hosted.limited = false;
		_reflow.replied();
		if (_detached) {
			schedule();
		}
	}
	if (hosted.input->tickWanted() && !hosted.asked) {
		askTick();
	}
	if (hosted.paintFirst) {
		schedule();
	}
	refreshClock();
}

void RendererWork::paintFrame(
		const twlite::DisplayList &list,
		QSize device) {
	if (_hosted->closed) {
		return;
	} else if (sealedAtStop()) {
		if (!_reflow.shows(geometry())) {
			_reflow.demand();
		}
		return;
	} else if (!FitsLayout(
			device,
			layoutArea(),
			*_cut,
			_height,
			_size.scale)) {
		_hosted->blocked = true;
		refreshFrame();
		return;
	}
	auto painted = paintList(list, device);
	if (!painted.layout || _hosted->closed) {
		return;
	}
	auto &layout = *painted.layout;
	layout.pixels = KeptBudget::Pixels::Make(
		layout.picture,
		std::move(painted.backing));
	if (!_detached) {
		_undrawn = true;
	}
	_reflow.served(_painted);
	_hosted->pictures->offer(std::move(layout));
	schedule();
}

void RendererWork::hostedState(const twlite::remote::State &state) {
	if (takeState(state, true)) {
		refreshClock();
	}
}

bool RendererWork::takeState(
		const twlite::remote::State &state,
		bool current) {
	if (!ValidState(state)) {
		refuse(u"A widget renderer reported a state out of range"_q);
		return false;
	} else if (!current) {
		return true;
	}
	auto &hosted = *_hosted;
	const auto was = std::exchange(hosted.state, state);
	hosted.tickAt = std::isfinite(state.next_tick_in)
		? (crl::now() + WakeDelay(state.next_tick_in))
		: crl::time();
	if ((was.width != state.width) || (was.height != state.height)) {
		unblock();
	}
	if (!was.dirty && state.dirty) {
		hosted.paintFirst = true;
		post(Change::Repaint);
	}
	return true;
}

void RendererWork::hostedSize(double height) {
	const auto changes = applyHeight(height);
	if (changes & Change::Height) {
		unblock();
	}
	post(changes);
}

void RendererWork::hostedContent() {
	if (!_hasContent) {
		_hasContent = true;
		post(Change::Content | Change::Repaint);
	}
}

void RendererWork::hostedLoaded() {
	if (!_loaded) {
		_loaded = true;
		bindHeightAnchor();
		post(Change::Loaded);
	}
}

void RendererWork::hostedHeld() {
	_hosted->held = true;
	if (!_clockOn) {
		scheduleWake();
	}
}

void RendererWork::unblock() {
	auto &hosted = *_hosted;
	const auto computed = frameSize();
	if (hosted.blocked
		&& computed
		&& (ReportedSize(hosted.state, _dpr) == computed)
		&& FitsLayout(*computed, layoutArea(), *_cut, _height, _size.scale)) {
		hosted.blocked = false;
		schedule();
	}
}

void RendererWork::resumeInbound() {
	if (_hosted->held && !_backend->heldFor()) {
		_hosted->held = false;
		_hosted->platform->schedule();
	}
}

void RendererWork::closeRoute(bool checked) {
	auto &hosted = *_hosted;
	hosted.platform->close();
	hosted.asked = std::nullopt;
	hosted.paintFirst = hosted.held = false;
	hosted.tickAt = 0;
	hosted.releaseLive = true;
	_reflow = RendererReflow();
	_emojiWaits.clear();
	hosted.state.dirty = false;
	hosted.state.wants_tick = false;
	hosted.state.frame_asked = false;
	hosted.state.next_tick_in = std::numeric_limits<double>::infinity();
	if (checked && !std::exchange(hosted.closed, true)) {
		hosted.reportClosed = true;
	}
	hosted.input->close();
	releaseInput();
	refreshClock();
	schedule();
}

void RendererWork::refuseRoute(QString limit, const char *detail) {
	if (_hosted->closed) {
		return;
	}
	const auto stalled = _host.localStop && _host.localStop(limit);
	closeRoute(true);
	const auto reason = limit;
	if (!_stopped) {
		stop(stalled ? StalledReason() : std::move(limit), true);
		refreshClock();
		post(Change::Stopped | Change::Repaint);
	}
	_host.diag(kPaintBoundsKind, detail ? detail : reason.toStdString());
}

void RendererWork::freeze(QString reason, bool fatal) {
	if (_stopped) {
		return;
	}
	stop(std::move(reason), fatal);
	if (!routeAlive() || _detached) {
		refreshClock();
		post(Change::Stopped | Change::Repaint);
		return;
	}
	// After what the helper sent before the death, and a paint stop's frame.
	_hosted->stopHeld = true;
	_hosted->stopBound.callOnce(kMaxStopHold, Qt::PreciseTimer);
	if (_hosted->limited && _onScreen) {
		_reflow.demand();
	}
	refreshClock();
	schedule();
}

void RendererWork::hostDied(Fn<void()> reached) {
	if (routeAlive()) {
		_hosted->platform->afterQueued(std::move(reached));
	} else {
		reached();
	}
}

void RendererWork::resetInput() {
	if (!_detached) {
		cancelInput();
	}
	releaseInput();
}

void RendererWork::cancelInput() {
	_hosted->input->cancel(_stopped);
}

void RendererWork::releaseInput() {
	_hosted->input->reset();
}

void RendererWork::stop(QString reason, bool fatal) {
	_stopped = true;
	_rasterThrottled = false;
	_stopReason = std::move(reason);
	_stopFatal = fatal;
	releaseInput();
	_hosted->input->deactivate();
}

void RendererWork::refuse(QString limit) {
	refuseRoute(std::move(limit));
}

void RendererWork::backendRefused() {
	if (_host.localStop) {
		_host.localStop(_backend->refusal());
	}
	PostToRenderThread(crl::guard(this, [=] {
		refuse(_backend->refusal());
	}));
}

bool RendererWork::canTick() const {
	return _onScreen && !_detached && !_stopped && !_host.gone();
}

bool RendererWork::clockWanted() const {
	return canTick() && (_stepBusy || wantsTick());
}

void RendererWork::refreshClock() {
	if (clockWanted()) {
		_wake.cancel();
		startClock();
	} else {
		stopClock();
		scheduleWake();
	}
}

void RendererWork::startClock() {
	if (_clockOn) {
		return;
	}
	_clockOn = true;
	_clock.callOnce(0);
}

void RendererWork::stopClock() {
	_clockOn = false;
	_clock.cancel();
}

void RendererWork::clockStep() {
	const auto operation = _backend->operation();
	if (!_clockOn) {
		return;
	}
	resumeInbound();
	if (!step()) {
		stopClock();
	} else if (_clockOn && !_clock.isActive()) {
		_clock.callEach(kHeldTick);
	}
}

void RendererWork::scheduleWake() {
	const auto seconds = canTick()
		? nextTickIn()
		: std::numeric_limits<double>::infinity();
	const auto held = std::min(_backend->heldFor(), kMaxWakeDelay);
	auto delay = std::isfinite(seconds)
		? std::make_optional(std::max(WakeDelay(seconds), held))
		: std::optional<crl::time>();
	if (_rasterThrottled || _hosted->held) {
		const auto resume = std::max(held, kMinWakeDelay);
		delay = std::min(delay.value_or(resume), resume);
	}
	if (delay) {
		_wake.callOnce(*delay);
	} else {
		_wake.cancel();
	}
}

bool RendererWork::step() {
	_stepBusy = canTick() && wantsTick();
	if (!_stepBusy) {
		scheduleWake();
		return false;
	}
	tick();
	return true;
}

void RendererWork::wake() {
	const auto operation = _backend->operation();
	resumeInbound();
	if (canTick()) {
		tick();
	} else if (_rasterThrottled && !_backend->heldFor()) {
		_rasterThrottled = false;
		schedule();
	}
	refreshClock();
}

void RendererWork::tick() {
	if (_backend->heldFor()) {
		return;
	}
	hostedTick();
}

RendererWork::Changes RendererWork::applyHeight(double height) {
	if (!std::isfinite(height)) {
		return {};
	}
	const auto clamped = std::clamp(height, kMinHeight, kMaxHeight);
	if (_height == clamped) {
		return {};
	}
	bindHeightAnchor();
	_height = clamped;
	if (!_onScreen) {
		_client = true;
	}
	return Change::Height;
}

void RendererWork::bindHeightAnchor() {
	if (const auto bottom = _host.bottom()) {
		_heightBottom = *bottom;
	}
}

void RendererWork::post(Changes changes) {
	if (changes) {
		_postedChanges |= changes;
		schedule();
	}
}

} // namespace Tgw
