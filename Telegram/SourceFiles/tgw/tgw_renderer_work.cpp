/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_renderer_work.h"

#include "core/src/renderer.h"
#include "test/test_widget_ops.h"
#include "tgw/tgw_backend.h"
#include "tgw/tgw_cursor.h"
#include "tgw/tgw_kept_budget.h"
#include "tgw/tgw_painter.h"
#include "tgw/tgw_painter_clips.h"
#include "tgw/tgw_render_thread.h"
#include "tgw/tgw_renderer_input.h"
#include "tgw/tgw_renderer_platform.h"
#include "tgw/tgw_renderer_rules.h"
#include "twidget/json.h"

#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
#include <private/qfont_p.h>
#include <private/qfontengine_p.h>
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API

#include "styles/style_basic.h"

namespace Tgw {
namespace {

using RendererRules::Append;
using RendererRules::FrameMessage;
using RendererRules::kFingerPointer;
using RendererRules::kHoverLeave;
using RendererRules::kHoverMove;
using RendererRules::kMousePointer;
using RendererRules::kPrimaryButton;
using RendererRules::kReadyMessage;
using RendererRules::kSecondaryButton;
using RendererRules::kTouchCancel;
using RendererRules::kTouchDown;
using RendererRules::kTouchMove;
using RendererRules::kTouchUp;
using RendererRules::LocalQuery;
using RendererRules::OfType;
using RendererRules::ParsedDocuments;
using RendererRules::TakeContextMenus;

constexpr auto kPaintBoundsKind = "paint-bounds";
constexpr auto kRendererStopDetail = "The widget renderer stopped";
constexpr auto kMinWakeDelay = crl::time(1);
constexpr auto kMaxWakeDelay = crl::time(std::numeric_limits<int>::max());
// Mirrors lib_ui's private kAnimationTick, the cadence every clock runs at.
constexpr auto kHeldTick = crl::time(1000) / st::universalDuration;
// A request this soon after a reply may continue a frame loop.
constexpr auto kRecentReply = crl::time(1000);

using WorkRegistry = base::flat_map<uint64, std::unique_ptr<RendererWork>>;

struct FrameAsk {
	uint64 seq = 0;
	uint64 serial = 0;
	uint64 epoch = 0;
	bool tick = false;
};

// WHY: one ops message draws canvas text and lays out the document through
// the same create_font(), so only the core's canvas_font() hook can tell
// canvas fonts apart for their allowance.
class CanvasFontRenderer final : public twlite::Renderer {
public:
	CanvasFontRenderer(
		double width,
		double dpr,
		bool dark,
		not_null<QtBackend*> backend);

private:
	int canvas_font(
		const std::string &css,
		twlite::FontMetrics &metrics) override;

	const not_null<QtBackend*> _backend;

};

[[nodiscard]] int DeviceSide(uint32 side) {
	return int(std::min(side, uint32(std::numeric_limits<int>::max())));
}

[[nodiscard]] QSize DeviceSize(const twlite::Renderer &renderer) {
	return QSize(
		DeviceSide(renderer.px_width()),
		DeviceSide(renderer.px_height()));
}

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

[[nodiscard]] QString FrameSizeMismatch(
		QSize reported,
		std::optional<QSize> computed) {
	const auto text = [](QSize size) {
		return u"%1x%2"_q.arg(size.width()).arg(size.height());
	};
	return u"Frame size mismatch: %1 reported, %2 computed"_q.arg(
		text(reported),
		computed ? text(*computed) : u"none"_q);
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
		QSize card,
		std::optional<QRect> repainted = std::nullopt) {
	const auto bounds = QRect(QPoint(), frame.size());
	for (auto i = 0; i != 4; ++i) {
		const auto patch = CornerPatch(corners, i, card);
		const auto target = patch.intersected(bounds);
		if (target.isEmpty()
			|| (repainted && !repainted->contains(target))) {
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

[[nodiscard]] bool LateAt(std::size_t scanned, crl::time started) {
	return !(scanned % kDeadlineStride)
		&& (crl::now() - started > kMaxRasterTime);
}

// WHY: a widget may report damage of any size, so it is bounded before it
// meets an int, and one rectangle grown over the corner patches it touches
// keeps antialiased edges and masked corners as a whole paint makes them.
[[nodiscard]] std::optional<QRect> RepaintArea(
		const std::vector<twlite::Rect> &damage,
		float64 cssToDevice,
		Images::CornersMaskRef corners,
		QSize card,
		QSize frame,
		crl::time started) {
	const auto bounds = QRect(QPoint(), frame);
	const auto reach = QRectF(bounds).adjusted(-1., -1., 1., 1.);
	auto result = QRect();
	auto scanned = std::size_t();
	for (const auto &rect : damage) {
		if (LateAt(++scanned, started)) {
			return std::nullopt;
		}
		const auto mapped = QRectF(
			rect.x * cssToDevice,
			rect.y * cssToDevice,
			rect.w * cssToDevice,
			rect.h * cssToDevice);
		if (!std::isfinite(mapped.x())
			|| !std::isfinite(mapped.y())
			|| !std::isfinite(mapped.width())
			|| !std::isfinite(mapped.height())) {
			return std::nullopt;
		}
		const auto shown = mapped.intersected(reach);
		if (!shown.isEmpty()) {
			result |= shown.toAlignedRect().adjusted(-1, -1, 1, 1);
		}
	}
	result &= bounds;
	for (auto grown = !result.isEmpty(); grown;) {
		grown = false;
		for (auto i = 0; i != 4; ++i) {
			const auto patch = CornerPatch(corners, i, card).intersected(
				bounds);
			if (!patch.isEmpty()
				&& result.intersects(patch)
				&& !result.contains(patch)) {
				result |= patch;
				grown = true;
			}
		}
	}
	return result;
}

// Backdrops read the pixels under them; nested filters damage too little.
[[nodiscard]] bool RepaintsInPlace(
		const twlite::DisplayList &list,
		crl::time started) {
	auto filters = 0;
	auto scanned = std::size_t();
	for (const auto &command : list) {
		if (LateAt(++scanned, started)) {
			return false;
		}
		switch (command.kind) {
		case twlite::Cmd::Backdrop: return false;
		case twlite::Cmd::FilterPush:
			if (++filters > 1) {
				return false;
			}
			break;
		case twlite::Cmd::FilterPop: filters = std::max(filters - 1, 0); break;
		default: break;
		}
	}
	return true;
}

[[nodiscard]] crl::time WakeDelay(double seconds) {
	return crl::time(std::clamp(
		std::ceil(seconds * 1000.),
		double(kMinWakeDelay),
		double(kMaxWakeDelay)));
}

[[nodiscard]] twidget::Json ElementAtQuery(QPointF point) {
	auto query = twidget::Json::object();
	query.set("q", "elementFromPoint");
	query.set("x", point.x());
	query.set("y", point.y());
	return LocalQuery(std::move(query));
}

[[nodiscard]] twidget::Json TakeMessage(
		std::vector<twidget::Json> &out,
		std::string_view type) {
	const auto i = ranges::find_if(out, OfType(type));
	if (i == end(out)) {
		return twidget::Json();
	}
	auto result = std::move(*i);
	out.erase(i);
	return result;
}

[[nodiscard]] twidget::WidgetGestureClaim GestureClaim(
		const std::vector<twidget::Json> &out) {
	const auto i = ranges::find_if(out, OfType("gesture"));
	if (i == end(out)) {
		return {};
	}
	return {
		.x = (*i)["x"].asBool(),
		.hold = (*i)["hold"].asBool(),
	};
}

[[nodiscard]] std::vector<twidget::Json> Touch(
		twlite::Renderer &renderer,
		int action,
		QPointF point,
		int modifiers,
		int pointer = kMousePointer,
		int button = kPrimaryButton) {
	return DeliverTouch(renderer, {
		.point = point,
		.action = action,
		.modifiers = modifiers,
		.pointer = pointer,
		.button = button,
	});
}

[[nodiscard]] bool OutsideSlop(QPointF point, QPointF origin) {
	const auto delta = point - origin;
	return RendererRules::OutsideSlop(delta.x(), delta.y());
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

[[nodiscard]] std::optional<QString> UnfoldedClips(
		const twlite::DisplayList &list) {
	if (ShortensClipHistory()) {
		return std::nullopt;
	}
	const auto clips = ranges::count(
		list,
		twlite::Cmd::ClipPush,
		&twlite::Command::kind);
	if (!clips) {
		return std::nullopt;
	}
	return PaintLimit(
		u"%1 clips in a list from the widget helper"_q.arg(clips),
		0);
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

CanvasFontRenderer::CanvasFontRenderer(
	double width,
	double dpr,
	bool dark,
	not_null<QtBackend*> backend)
: twlite::Renderer(width, dpr, dark, *backend)
, _backend(backend) {
}

int CanvasFontRenderer::canvas_font(
		const std::string &css,
		twlite::FontMetrics &metrics) {
	return _backend->canvasFont(css, metrics);
}

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
	crl::time tickAt = 0;
	bool paintFirst = false;
	bool blocked = false;
	bool held = false;
	bool closed = false;
	bool reportClosed = false;
	bool releaseLive = false;
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

RendererWork::RendererWork(
	uint64 id,
	base::weak_ptr<RendererCore> chat,
	double width,
	double dpr,
	const RendererValues &values,
	Host host,
	std::optional<RendererHosted> hosted)
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
, _renderer(hosted
	? std::unique_ptr<twlite::Renderer>()
	: std::make_unique<CanvasFontRenderer>(
		width,
		dpr,
		false,
		_backend.get()))
, _hosted(hosted ? makeHosted(std::move(*hosted)) : nullptr)
, _clock([=] { clockStep(); })
, _wake([=] { wake(); })
, _heldTimer([=] {
	const auto operation = this->operation();
	releaseHeld();
	refreshClock();
}) {
	ConfigureEmojiTiles(values.emoji, EmojiTilesLanded);
	_ready = (_hosted != nullptr);
}

RendererWork::~RendererWork() {
	_clock.cancel();
	_wake.cancel();
	releaseInput();
	if (_hosted) {
		_hosted->pictures->close();
	}
}

void RendererWork::Create(
		uint64 id,
		base::weak_ptr<RendererCore> chat,
		double width,
		double dpr,
		const RendererValues &values,
		Host host,
		std::optional<RendererHosted> hosted) {
	Expects(OnRenderThread());

	auto &works = Works();
	works.emplace(
		id,
		std::make_unique<RendererWork>(
			id,
			std::move(chat),
			width,
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
	return _renderer ? _renderer->dirty() : _hosted->state.dirty;
}

bool RendererWork::wantsTick() const {
	return _renderer
		? _renderer->wants_tick()
		: (_hosted->state.wants_tick || _hosted->state.frame_asked);
}

double RendererWork::nextTickIn() const {
	if (_renderer) {
		return _renderer->next_tick_in();
	}
	const auto &hosted = *_hosted;
	if (hosted.asked || !hosted.tickAt) {
		return std::numeric_limits<double>::infinity();
	}
	return std::max(hosted.tickAt - crl::now(), crl::time()) / 1000.;
}

int RendererWork::viewportHeight() const {
	return _renderer
		? _renderer->viewport_height()
		: _hosted->state.height;
}

bool RendererWork::routeAlive() const {
	return _hosted && !_hosted->platform->ended();
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
	_pendingSize = std::string();
	releaseCanvas();
	refreshClock();
	schedule();
}

void RendererWork::finishDetach() {
	cancelInput();
	releaseInput();
	if (_renderer) {
		_renderer->clear_selection();
		if (_loaded) {
			post(forwardAll(takeSettled()));
		}
		_backend->releaseCanvasFonts();
	} else {
		_hosted->input->detach();
	}
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
	return (_renderer || _hosted->platform->accepting())
		&& canTick()
		&& _hasContent;
}

void RendererWork::pointerEnter(uint64 serial) {
	if (_hosted) {
		_hosted->input->enter(serial);
		return;
	}
	_reports.pointer = serial;
	_reports.touched = true;
	schedule();
	if (acceptsInput()) {
		_pointer.over = true;
	}
}

void RendererWork::pointerMove(QPointF point, int modifiers, uint64 serial) {
	if (_hosted) {
		_hosted->input->move(point, modifiers, serial);
		return;
	}
	_reports.pointer = serial;
	_reports.touched = true;
	schedule();
	if (!acceptsInput()) {
		return;
	}
	_pointer.position = point;
	_pointer.hit = elementAt(point);
	const auto wasDirty = _renderer->dirty();
	if (_pointer.pressed) {
		auto out = Touch(*_renderer, kTouchMove, point, modifiers);
		_pointer.moved = _pointer.moved
			|| OutsideSlop(point, _pointer.origin);
		if (_pointer.moved) {
			trackHover();
		}
		inputDone(out, wasDirty);
		return;
	}
	_pointer.over = _pointer.hovering = true;
	auto out = _renderer->hover(kHoverMove, point.x(), point.y(), modifiers);
	trackHover();
	inputDone(out, wasDirty);
}

void RendererWork::pointerLeave(uint64 serial) {
	if (_hosted) {
		_hosted->input->leave(serial);
		return;
	}
	_reports.pointer = serial;
	_reports.touched = true;
	schedule();
	_cursor = nullptr;
	if (!acceptsInput()) {
		return;
	}
	_pointer.over = false;
	if (_pointer.pressed || !_pointer.hovering) {
		return;
	}
	const auto wasDirty = _renderer->dirty();
	inputDone(leaveHover(), wasDirty);
}

void RendererWork::pointerPress(
		QPointF point,
		int modifiers,
		uint64 serial,
		uint64 selection) {
	if (_hosted) {
		_hosted->input->press(point, modifiers, serial, selection);
		return;
	}
	_reports.pointer = serial;
	_reports.selection = selection;
	_reports.touched = true;
	schedule();
	if (!acceptsInput()) {
		return;
	}
	menuRelease(modifiers, false);
	const auto hit = elementAt(point);
	const auto wasDirty = _renderer->dirty();
	auto out = Touch(*_renderer, kTouchDown, point, modifiers);
	holdPointer(point, hit, false);
	Test::WidgetLocalPressDown(_id, serial);
	inputDone(out, wasDirty);
}

void RendererWork::pointerRelease(
		QPointF point,
		int modifiers,
		bool cancel,
		uint64 serial) {
	if (_hosted) {
		_hosted->input->release(point, modifiers, cancel, serial);
		return;
	}
	_reports.pointer = serial;
	_reports.touched = true;
	schedule();
	releasePointer(point, modifiers, cancel);
}

void RendererWork::releasePointer(
		QPointF point,
		int modifiers,
		bool cancel) {
	_pointer.menu = 0;
	_pointer.held.clear();
	if (!acceptsInput() || !_pointer.pressed) {
		return;
	}
	const auto wasDirty = _renderer->dirty();
	// A press the chat took for itself ends with no click.
	auto out = cancel
		? Touch(*_renderer, kTouchCancel, _pointer.position, 0)
		: Touch(*_renderer, kTouchUp, point, modifiers);
	if (!_pointer.secondary) {
		Test::WidgetLocalPressReleased(_id, out);
	}
	_pointer.position = point;
	_pointer.press = 0;
	_pointer.pressed = _pointer.secondary = false;
	_pointer.moved = _pointer.active = false;
	if (!_pointer.over) {
		Append(out, leaveHover());
	}
	inputDone(out, wasDirty);
	Test::WidgetLocalPressForwarded(_id);
}

void RendererWork::menuPress(QPointF point, int modifiers, uint64 serial) {
	if (_hosted) {
		_hosted->input->menuPress(point, modifiers, serial);
		return;
	}
	_reports.menu = RendererMenu{ .serial = serial };
	_reports.touched = true;
	schedule();
	if (!acceptsInput() || _pointer.pressed || _finger.pressed) {
		return;
	}
	const auto hit = elementAt(point);
	if (selected()) {
		const auto synced = (_pointer.parsed == ParsedDocuments(*_renderer));
		const auto hover = synced ? _pointer.hover : 0;
		const auto builds = _renderer->stats().builds;
		const auto held = cursorStyles().hold(*_renderer, hit, hover);
		flushBuilt(builds);
		// A core press drops the selection: keep it unless the hold is claimed.
		if (!held.value_or(false)) {
			return;
		}
	}
	const auto wasDirty = _renderer->dirty();
	auto out = Touch(
		*_renderer,
		kTouchDown,
		point,
		modifiers,
		kMousePointer,
		kSecondaryButton);
	const auto taken = MenuTaken(GestureClaim(out), point);
	if (taken) {
		holdPointer(point, hit, true);
		_pointer.held = TakeContextMenus(out, "mouse");
		_pointer.menu = serial;
	} else {
		Append(out, Touch(*_renderer, kTouchCancel, point, 0));
	}
	_reports.menu->taken = taken;
	inputDone(out, wasDirty);
}

void RendererWork::menuDecided(uint64 serial, bool deliver) {
	if (_hosted) {
		_hosted->input->menuDecided(serial, deliver);
		return;
	} else if (!serial || (_pointer.menu != serial)) {
		return;
	}
	_pointer.menu = 0;
	const auto held = base::take(_pointer.held);
	if (!_pointer.secondary || !acceptsInput()) {
		return;
	} else if (deliver) {
		for (const auto &message : held) {
			forward(message);
		}
	} else {
		releasePointer(_pointer.position, 0, true);
	}
}

void RendererWork::menuRelease(int modifiers, bool cancel) {
	if (_hosted) {
		_hosted->input->menuRelease(modifiers, cancel);
	} else if (_pointer.secondary) {
		releasePointer(_pointer.position, modifiers, cancel);
	}
}

void RendererWork::fingerPress(QPointF point, int modifiers, uint64 serial) {
	if (_hosted) {
		_hosted->input->fingerPress(point, modifiers, serial);
		return;
	}
	_reports.claim = RendererClaim{ .serial = serial };
	_reports.touched = true;
	schedule();
	if (!acceptsInput() || _finger.pressed) {
		return;
	}
	const auto wasDirty = _renderer->dirty();
	auto out = Touch(*_renderer, kTouchDown, point, modifiers, kFingerPointer);
	_reports.claim->claim = GestureClaim(out);
	_finger = { .position = point, .pressed = true };
	inputDone(out, wasDirty);
}

void RendererWork::fingerMove(QPointF point, int modifiers) {
	if (_hosted) {
		_hosted->input->fingerMove(point, modifiers);
		return;
	} else if (!_finger.pressed
		|| !acceptsInput()
		|| (point == _finger.position)) {
		return;
	}
	_finger.position = point;
	const auto wasDirty = _renderer->dirty();
	inputDone(
		Touch(*_renderer, kTouchMove, point, modifiers, kFingerPointer),
		wasDirty);
}

void RendererWork::fingerHold() {
	if (_hosted) {
		if (_hosted->input->fingerHold(_hosted->asked.has_value())) {
			askTick();
		}
		return;
	} else if (!_finger.pressed || !acceptsInput()) {
		return;
	}
	const auto wasDirty = _renderer->dirty();
	auto out = base::take(_finger.held);
	Append(out, _renderer->tick());
	inputDone(out, wasDirty);
}

void RendererWork::fingerRelease(QPointF point, int modifiers) {
	if (_hosted) {
		_hosted->input->fingerRelease(point, modifiers);
		return;
	} else if (!_finger.pressed || !acceptsInput()) {
		return;
	}
	const auto wasDirty = _renderer->dirty();
	auto out = base::take(_finger.held);
	if (point != _finger.position) {
		Append(
			out,
			Touch(*_renderer, kTouchMove, point, modifiers, kFingerPointer));
	}
	_finger = Finger();
	Append(out, Touch(*_renderer, kTouchUp, point, modifiers, kFingerPointer));
	inputDone(out, wasDirty);
}

void RendererWork::fingerCancel() {
	if (_hosted) {
		_hosted->input->fingerCancel();
		return;
	} else if (!_finger.pressed || !acceptsInput()) {
		return;
	}
	const auto position = base::take(_finger).position;
	const auto wasDirty = _renderer->dirty();
	inputDone(
		Touch(*_renderer, kTouchCancel, position, 0, kFingerPointer),
		wasDirty);
}

bool RendererWork::handAt(QPointF point) {
	if (!acceptsInput()) {
		return false;
	}
	const auto hit = elementAt(point);
	const auto held = _pointer.pressed
		&& !_pointer.moved
		&& !OutsideSlop(point, _pointer.origin);
	const auto targets = RendererRules::PointerTargets(
		hit,
		held,
		(_pointer.parsed == ParsedDocuments(*_renderer)),
		_pointer.hover,
		_pointer.active);
	const auto builds = _renderer->stats().builds;
	const auto result = cursorStyles().pointer(
		*_renderer,
		hit,
		targets.hover,
		targets.active);
	flushBuilt(builds);
	return result;
}

bool RendererWork::selected() const {
	return !_detached && !_stopped && _renderer->has_selection();
}

void RendererWork::reportInput(RendererAnswer &answer) {
	if (_hosted) {
		_hosted->input->report(answer, !_detached && !_stopped);
		return;
	}
	answer.cursor = reportCursor();
	answer.selection = reportSelection();
	answer.menu = base::take(_reports.menu);
	answer.claim = base::take(_reports.claim);
}

std::optional<RendererCursor> RendererWork::reportCursor() {
	if ((_reports.pointer == _reports.cursorSent)
		&& (!_pointer.over
			|| (_renderer->stats().builds == _reports.cursorBuilds))) {
		return std::nullopt;
	}
	const auto hand = _pointer.over && handAt(_pointer.position);
	_reports.cursorBuilds = _renderer->stats().builds;
	if ((_reports.pointer == _reports.cursorSent)
		&& (hand == _reports.hand)) {
		return std::nullopt;
	}
	_reports.cursorSent = _reports.pointer;
	_reports.hand = hand;
	return RendererCursor{ .serial = _reports.pointer, .hand = hand };
}

std::optional<RendererSelection> RendererWork::reportSelection() {
	const auto touched = base::take(_reports.touched);
	auto text = QString();
	if (!_detached && !_stopped) {
		const auto builds = _renderer->stats().builds;
		if (touched
			|| (_reports.has && (builds != _reports.textBuilds))) {
			_reports.textBuilds = builds;
			text = QString::fromStdString(_renderer->selected_text());
		} else {
			text = _reports.text;
		}
	}
	const auto has = !text.isEmpty();
	if ((_reports.selection == _reports.textSent)
		&& (has == _reports.has)
		&& (text == _reports.text)) {
		return std::nullopt;
	}
	_reports.textSent = _reports.selection;
	_reports.has = has;
	_reports.text = text;
	return RendererSelection{
		.text = std::move(text),
		.serial = _reports.selection,
		.has = has,
	};
}

void RendererWork::clearSelection(uint64 serial) {
	if (_hosted) {
		_hosted->input->clearSelection(serial);
		return;
	}
	_reports.selection = serial;
	_reports.touched = true;
	schedule();
	const auto wasDirty = _renderer->dirty();
	if (_renderer->clear_selection()) {
		inputDone({}, wasDirty);
	}
}

void RendererWork::setSize(const RendererSize &size, uint64 serial) {
	_size = size;
	_sizeSerial = serial;
	_sizePending = true;
	_undrawn = false;
	if (_hosted) {
		const auto withdrawn = _hosted->pictures->withdraw();
		const auto superseded = canTick() && _hosted->asked.has_value();
		_client = _client || withdrawn.client;
		if (withdrawn.layout || superseded) {
			_reflow.demand();
		}
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
	if (_hosted) {
		_hosted->blocked = false;
	}
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
	if (_hosted && _hosted->pictures->withdraw().client) {
		_client = true;
	}
}

void RendererWork::refreshFrame() {
	// A paint-stopped core's replay is refused, so its last picture stays.
	_painted.device = QSize();
	_frameRepaintable = false;
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
	if (_hosted && base::take(_hosted->releaseLive)) {
		_backend->releaseLive();
	}
	applySizeRequest();
	const auto follows = _hosted && _detached && _cut;
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
	if (_hosted) {
		_hosted->paintFirst = false;
	}
	auto layout = (wanted && !throttled) ? raster() : std::nullopt;
	if (throttled) {
		_rasterThrottled = true;
		if (!_clockOn) {
			scheduleWake();
		}
	} else {
		_rasterThrottled = false;
		if (!awaitingFrame()) {
			_reflow.settle(_hosted && _hosted->blocked);
			if (follows) {
				if (const auto outdated = _reflow.outdated(geometry())) {
					_hosted->pictures->setOutdated(*outdated);
				}
			}
		}
	}
	if (layout && !_detached) {
		_undrawn = true;
	}
	auto answer = RendererAnswer();
	reportInput(answer);
	if (_hosted) {
		answer.closed = base::take(_hosted->reportClosed);
	}
	const auto changes = base::take(_postedChanges);
	const auto stopped = bool(changes & Change::Stopped);
	const auto ordered = stopped
		|| (changes & (Change::Content | Change::Loaded))
		|| answer.cursor
		|| answer.selection
		|| answer.menu
		|| answer.claim
		|| answer.closed;
	if (_hosted) {
		answer.post = _hosted->pictures->send(ordered);
		if (!answer.post) {
			return;
		}
	} else if (!layout && !ordered) {
		return;
	} else {
		answer.layout = std::move(layout);
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
	} else if (_hosted) {
		_client = true;
		_hosted->blocked = false;
		if (routeAlive()) {
			_hosted->platform->setWidth(_size.width);
			_hosted->platform->setMaxHeight(_size.maxHeight);
		}
		return;
	}
	auto out = std::vector<twidget::Json>();
	_renderer->set_width(_size.width, out);
	_renderer->set_max_height(_size.maxHeight, out);
	_client = true;
	finish(out, {});
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

std::optional<RendererLayout> RendererWork::raster() {
	// A released core shows its last picture, whatever build it holds.
	if ((_detached || !dirty()) && geometryPainted()) {
		return std::nullopt;
	} else if (_hosted) {
		const auto size = frameSize().value_or(QSize());
		if (!size.isEmpty()
			&& !_hosted->blocked
			&& routeAlive()
			&& !sealedAtStop()) {
			requestPaint();
		}
		return std::nullopt;
	} else if (!_detached && !_stopped) {
		_backend->beginFrame();
	}
	const auto builds = _renderer->stats().builds;
	// WHY: the display list names backend font handles that a later build
	// may delete with an idle face, so it is rasterized at once and only
	// the pixels are kept.
	const auto list = _renderer->paint();
	const auto built = _renderer->stats().builds;
	if (built != builds) {
		flushQueued(); // paint() holds its build's output for the next call
		if (_renderer->stats().builds != built) {
			return std::nullopt;
		}
	}
	const auto computed = frameSize();
	const auto reported = DeviceSize(*_renderer);
	if (!computed || (reported != *computed)) {
		refuse(FrameSizeMismatch(reported, computed));
		return std::nullopt;
	}
	auto damage = std::vector<twlite::Rect>();
	const auto damaged = _renderer->damage(damage)
		? std::make_optional(std::move(damage))
		: std::nullopt;
	return paintList(list, damaged, *computed).layout;
}

RendererWork::ListPaint RendererWork::paintList(
		const std::vector<twlite::Command> &list,
		const std::optional<std::vector<twlite::Rect>> &damage,
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
		_frameRepaintable = false;
		if (!fits()) {
			dropCanvas();
		}
		return {};
	}
	const auto work = _backend->work();
	const auto started = crl::now();
	const auto inPlace = RepaintsInPlace(list, started);
	const auto area = (inPlace
		&& damage
		&& _frameRepaintable
		&& fits()
		&& (_painted.device == size)
		&& (_frame.size() == size))
		? RepaintArea(*damage, _dpr, corners, card, size, started)
		: std::nullopt;
	auto limit = area
		? repaintFrame(list, *area, corners, card, started)
		: std::nullopt;
	const auto whole = !limit;
	auto result = ListPaint();
	if (whole) {
		auto painted = PaintFrame(
			list,
			*_backend,
			size,
			_dpr,
			started,
			_hosted ? Acquiring(result.backing) : nullptr);
		limit = std::move(painted.limit);
		if (limit->isEmpty() || !fits()) {
			_frame = std::move(painted.image);
			if (!_frame.isNull()) {
				_frame = RoundFrame(std::move(_frame), corners, card);
				_frame.setDevicePixelRatio(ratio);
			}
		}
	}
	TakeEmojiWaits(_backend->emoji(), _emojiPainted);
	const auto refused = !limit->isEmpty();
	_frameRepaintable = inPlace && !refused && !_frame.isNull();
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
	if (whole && result.layout) {
		std::swap(_emojiWaits, _emojiPainted);
	} else {
		_emojiWaits.add(_emojiPainted);
	}
	if (refused) {
		refuse(std::move(*limit));
	}
	if (_detached || _hosted) {
		releaseCanvas();
	}
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

std::optional<QString> RendererWork::repaintFrame(
		const std::vector<twlite::Command> &list,
		QRect area,
		Images::CornersMaskRef corners,
		QSize card,
		crl::time started) {
	if (area.isEmpty()) {
		return QString();
	}
	const auto ratio = _frame.devicePixelRatio();
	_frame.setDevicePixelRatio(1.);
	const auto backup = _frame.copy(area);
	auto result = RepaintFrame(
		list,
		*_backend,
		_frame,
		area,
		_dpr,
		started);
	if (result && result->isEmpty()) {
		_frame = RoundFrame(std::move(_frame), corners, card, area);
	} else {
		auto p = QPainter(&_frame);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.drawImage(area.topLeft(), backup);
	}
	_frame.setDevicePixelRatio(ratio);
	return result;
}

void RendererWork::releaseCanvas() {
	_frame = QImage();
	_frameRepaintable = false;
}

void RendererWork::dropCanvas() {
	_frame = QImage();
	_painted = RendererGeometry();
	_frameRepaintable = false;
}

void RendererWork::deliver(RendererAnswer &&answer) {
	crl::on_main(_chat, [chat = _chat, answer = std::move(answer)]() mutable {
		chat->apply(std::move(answer));
	});
}

bool RendererWork::inboundFirst() const {
	return _hosted && _hosted->platform->requestWaiting();
}

bool RendererWork::sealedAtStop() const {
	return _hosted && _stopped && !_detached;
}

bool RendererWork::awaitingFrame() const {
	return _hosted
		&& (_hosted->paintFirst
			|| (_hosted->asked && !_hosted->asked->tick)
			|| sealedAtStop());
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
	} else if (auto clips = UnfoldedClips(list)) {
		refuse(std::move(*clips));
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
	auto painted = paintList(list, std::nullopt, device);
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
	if (_hosted && _hosted->held && !_backend->heldFor()) {
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
	closeRoute(true);
	const auto reason = limit;
	if (!_stopped) {
		stop(std::move(limit), true);
		refreshClock();
		post(Change::Stopped | Change::Repaint);
	}
	_host.diag(kPaintBoundsKind, detail ? detail : reason.toStdString());
}

void RendererWork::hostLoaded() {
	if (_ready || _stopped) {
		return;
	}
	_ready = true;
	_host.send(kReadyMessage);
	if (!_pendingSize.empty()) {
		_host.send(base::take(_pendingSize));
	}
	refreshClock();
}

void RendererWork::hostMessage(const twidget::Json &message) {
	const auto &type = message["type"].asString();
	if (_stopped || !_renderer) {
		return;
	} else if (type == "requestFrame") {
		if (!_frameRequested) {
			_idleRequest = canTick()
				&& !_clockOn
				&& (crl::now() >= _recentReplyUntil);
			if (_held) {
				dropHeld();
				stopClock();
			}
		}
		_frameRequested = true;
		refreshClock();
		return;
	}
	const auto wasDirty = _renderer->dirty();
	const auto quiet = (type == "ops")
		&& canTick()
		&& !_held
		&& (_frameRequested ? _idleRequest : !_clockOn)
		&& !_renderer->wants_tick();
	const auto builds = _renderer->stats().builds;
	auto out = _renderer->receive(message);
	auto changes = Changes();
	const auto load = RendererRules::AdvanceLoad(
		message,
		_hasContent,
		_acked,
		_loaded);
	if (RendererRules::DrawsContent(message) || (type == "anim")) {
		changes |= Change::Repaint;
	}
	if (load.content) {
		changes |= Change::Content;
	}
	if (load.loaded) {
		changes |= Change::Loaded;
		// a first height equal to the empty document's applies no change
		bindHeightAnchor();
		if (type == "ops") {
			Append(out, takeSettled());
		}
	}
	if (!wasDirty && _renderer->dirty()) {
		changes |= Change::Repaint;
	}
	// WHY: a clock started for the held part of a flush, or by a request
	// pending at it, first ticks next turn and builds it before the later
	// parts arrive, laying out twice, so held work waits one cadence tick.
	if (quiet
		&& (_renderer->stats().builds == builds)
		&& _renderer->wants_tick()) {
		_held = Held{ .since = crl::now(), .builds = builds };
		_heldTimer.callOnce(kHeldTick);
	}
	// A paint before the release would only redraw the cached picture.
	if (_held
		&& (_held->builds == _renderer->stats().builds)
		&& !_renderer->dirty()) {
		changes &= ~Change::Repaint;
	}
	finish(out, changes);
}

void RendererWork::hostTheme(bool dark) {
	if (_stopped || !_renderer) {
		return;
	}
	auto out = std::vector<twidget::Json>();
	_renderer->set_theme(dark, out);
	finish(out, Change::Repaint);
}

void RendererWork::freeze(QString reason, bool fatal) {
	if (_stopped) {
		return;
	}
	stop(std::move(reason), fatal);
	finish({}, Change::Stopped | Change::Repaint);
}

int64 RendererWork::elementAt(QPointF point) {
	const auto wasDirty = _renderer->dirty();
	auto out = _renderer->receive(ElementAtQuery(point));
	const auto result = TakeMessage(out, "result")["result"].asNumber();
	auto changes = forwardAll(out);
	if (!wasDirty && _renderer->dirty()) {
		changes |= Change::Repaint;
	}
	post(changes);
	return int64(result);
}

CursorStyles &RendererWork::cursorStyles() {
	if (!_cursor) {
		_cursor = std::make_unique<CursorStyles>(_renderer->dpr());
	}
	return *_cursor;
}

void RendererWork::holdPointer(QPointF point, int64 hit, bool secondary) {
	_pointer.position = _pointer.origin = point;
	_pointer.hit = _pointer.hover = _pointer.press = hit;
	_pointer.parsed = _renderer ? ParsedDocuments(*_renderer) : 0;
	_pointer.over = _pointer.hovering = _pointer.pressed = true;
	_pointer.moved = false;
	_pointer.active = !secondary;
	_pointer.secondary = secondary;
}

void RendererWork::trackHover() {
	const auto parsed = ParsedDocuments(*_renderer);
	if (_pointer.parsed != parsed) {
		_pointer.parsed = parsed;
		_pointer.hover = 0;
		_pointer.active = false;
	}
	if (_pointer.hover == _pointer.hit) {
		return;
	}
	_pointer.hover = _pointer.hit;
	_pointer.active = false;
}

std::vector<twidget::Json> RendererWork::leaveHover() {
	_cursor = nullptr;
	_pointer.hit = _pointer.hover = 0;
	_pointer.hovering = false;
	return _renderer->hover(kHoverLeave, 0., 0., 0);
}

void RendererWork::inputDone(
		const std::vector<twidget::Json> &out,
		bool wasDirty) {
	_backend->input();
	auto changes = forwardAll(out);
	if (!wasDirty && _renderer->dirty()) {
		changes |= Change::Repaint;
	}
	post(changes);
	refreshClock();
}

void RendererWork::resetInput() {
	if (!_detached) {
		cancelInput();
	}
	releaseInput();
}

void RendererWork::cancelInput() {
	if (_hosted) {
		_hosted->input->cancel(_stopped);
		return;
	} else if (_stopped
		|| !(_pointer.pressed || _pointer.hovering || _finger.pressed)) {
		return;
	}
	const auto wasDirty = _renderer->dirty();
	auto out = _pointer.pressed
		? Touch(*_renderer, kTouchCancel, _pointer.position, 0)
		: std::vector<twidget::Json>();
	if (_finger.pressed) {
		Append(out, Touch(
			*_renderer,
			kTouchCancel,
			_finger.position,
			0,
			kFingerPointer));
	}
	if (_pointer.hovering) {
		Append(out, leaveHover());
	}
	_pointer = Pointer();
	_finger = Finger();
	inputDone(out, wasDirty);
}

void RendererWork::releaseInput() {
	_cursor = nullptr;
	_pointer = Pointer();
	_finger = Finger();
	if (_hosted) {
		_hosted->input->reset();
	}
}

void RendererWork::stop(QString reason, bool fatal) {
	_stopped = true;
	_frameRequested = _rasterThrottled = false;
	_stopReason = std::move(reason);
	_stopFatal = fatal;
	releaseInput();
	if (_hosted) {
		_hosted->input->deactivate();
	}
}

void RendererWork::refuse(QString limit) {
	if (_hosted) {
		refuseRoute(std::move(limit));
		return;
	} else if (_detached || _stopped) {
		return;
	}
	_host.diag(kPaintBoundsKind, limit.toStdString());
	stop(std::move(limit), true);
	refreshClock();
	post(Change::Stopped | Change::Repaint);
}

void RendererWork::backendRefused() {
	PostToRenderThread(crl::guard(this, [=] {
		refuse(_backend->refusal());
	}));
}

std::vector<twidget::Json> RendererWork::takeSettled() {
	return RendererRules::TakeSettled(*_renderer);
}

void RendererWork::flushQueued() {
	auto changes = forwardAll(takeSettled());
	if (_renderer->dirty()) {
		changes |= Change::Repaint;
	}
	post(changes);
	refreshClock();
}

void RendererWork::flushBuilt(int builds) {
	if (_renderer->stats().builds != builds) {
		flushQueued();
	}
}

bool RendererWork::canTick() const {
	return _onScreen && _ready && !_detached && !_stopped && !_host.gone();
}

bool RendererWork::clockWanted() const {
	return canTick() && (_stepBusy || _frameRequested || wantsTick());
}

void RendererWork::refreshClock() {
	if (clockWanted()) {
		_wake.cancel();
		startClock();
	} else {
		dropHeld();
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
	if (!step(crl::now())) {
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
	if (_rasterThrottled || (_hosted && _hosted->held)) {
		const auto resume = std::max(held, kMinWakeDelay);
		delay = std::min(delay.value_or(resume), resume);
	}
	if (delay) {
		_wake.callOnce(*delay);
	} else {
		_wake.cancel();
	}
}

void RendererWork::dropHeld() {
	// A reader's build may still need a repaint when this hold ends.
	if (_held
		&& canTick()
		&& (_held->builds != _renderer->stats().builds)
		&& dirty()) {
		post(Change::Repaint);
	}
	_held = std::nullopt;
	_heldTimer.cancel();
	_idleRequest = false;
}

void RendererWork::releaseHeld() {
	dropHeld();
	_stepBusy = _frameRequested;
	if (clockWanted()) {
		tick();
	}
}

bool RendererWork::step(crl::time now) {
	_stepBusy = canTick() && (_frameRequested || wantsTick());
	if (!_stepBusy) {
		dropHeld();
		scheduleWake();
		return false;
	} else if (!_held) {
		tick();
		return true;
	} else if ((_held->builds == _renderer->stats().builds)
		&& (now < _held->since + kHeldTick)) {
		return true;
	}
	releaseHeld();
	if (clockWanted()) {
		return true;
	}
	scheduleWake();
	return false;
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
	} else if (_hosted) {
		hostedTick();
		return;
	}
	_backend->beginFrame();
	if (base::take(_rasterThrottled)) {
		schedule();
	}
	const auto wasDirty = _renderer->dirty();
	auto out = _renderer->tick();
	if (_finger.pressed) {
		Append(_finger.held, TakeContextMenus(out, "touch"));
	}
	auto changes = forwardAll(out);
	if (base::take(_frameRequested)) {
		forward(FrameMessage());
		_recentReplyUntil = crl::now() + kRecentReply;
	}
	if (!wasDirty && _renderer->dirty()) {
		changes |= Change::Repaint;
	}
	post(changes);
}

void RendererWork::finish(
		const std::vector<twidget::Json> &out,
		Changes changes) {
	changes |= forwardAll(out);
	refreshClock();
	post(changes);
}

RendererWork::Changes RendererWork::forwardAll(
		const std::vector<twidget::Json> &out) {
	auto changes = Changes();
	for (const auto &message : out) {
		if (message["type"].asString() == "size") {
			changes |= applySize(message);
		}
		forward(message);
	}
	return changes;
}

RendererWork::Changes RendererWork::applySize(
		const twidget::Json &message) {
	const auto &height = message["height"];
	return height.isNumber() ? applyHeight(height.asNumber()) : Changes();
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

void RendererWork::forward(const twidget::Json &message) {
	if (_detached || _stopped) {
		return;
	} else if (_ready) {
		_host.send(message.dump());
	} else if (RendererRules::KeptBeforeReady(message)) {
		_pendingSize = message.dump();
	}
}

void RendererWork::post(Changes changes) {
	if (changes) {
		_postedChanges |= changes;
		schedule();
	}
}

} // namespace Tgw
