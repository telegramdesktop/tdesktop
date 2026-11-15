/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_renderer_work.h"

#include "core/src/renderer.h"
#include "tgw/tgw_backend.h"
#include "tgw/tgw_cursor.h"
#include "tgw/tgw_painter.h"
#include "tgw/tgw_touch.h"
#include "twidget/json.h"
#include "ui/style/style_core_scale.h"

#include "styles/style_basic.h"

namespace Tgw {
namespace {

constexpr auto kFallbackWidth = 1.;
constexpr auto kReadyMessage = "{\"type\":\"ready\"}";
constexpr auto kPaintBoundsKind = "paint-bounds";
constexpr auto kMinWakeDelay = crl::time(1);
constexpr auto kMaxWakeDelay = crl::time(std::numeric_limits<int>::max());
// Mirrors lib_ui's private kAnimationTick, the cadence every clock runs at.
constexpr auto kHeldTick = crl::time(1000) / st::universalDuration;
// A request this soon after a reply may continue a frame loop.
constexpr auto kRecentReply = crl::time(1000);
constexpr auto kTouchDown = 0;
constexpr auto kTouchMove = 1;
constexpr auto kTouchUp = 2;
constexpr auto kTouchCancel = 3;
constexpr auto kHoverMove = 0;
constexpr auto kHoverLeave = 1;
constexpr auto kMousePointer = 0;
constexpr auto kFingerPointer = 1;
constexpr auto kPrimaryButton = 0;
constexpr auto kSecondaryButton = 2;
constexpr auto kLocalQueryId = 0;
// Mirrors the private twidget::WidgetHost::kRenderAckQid, its post-load query.
constexpr auto kLoadAckQueryId = -1;

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

[[nodiscard]] bool ValidWidth(double width) {
	return std::isfinite(width) && (width > 0.);
}

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

// WHY: a widget may report damage of any size, so it is bounded before it
// meets an int, and one rectangle grown over the corner patches it touches
// keeps antialiased edges and masked corners as a whole paint makes them.
[[nodiscard]] std::optional<QRect> RepaintArea(
		const std::vector<twlite::Rect> &damage,
		float64 cssToDevice,
		Images::CornersMaskRef corners,
		QSize card,
		QSize frame) {
	const auto bounds = QRect(QPoint(), frame);
	const auto reach = QRectF(bounds).adjusted(-1., -1., 1., 1.);
	auto result = QRect();
	for (const auto &rect : damage) {
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
[[nodiscard]] bool RepaintsInPlace(const twlite::DisplayList &list) {
	auto filters = 0;
	for (const auto &command : list) {
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

[[nodiscard]] twidget::Json FrameMessage() {
	auto result = twidget::Json::object();
	result.set("type", "frame");
	result.set("time", twlite::Renderer::clock() * 1000.);
	return result;
}

[[nodiscard]] twidget::Json LocalQuery(twidget::Json query) {
	auto result = twidget::Json::object();
	result.set("type", "query");
	result.set("qid", kLocalQueryId);
	result.set("query", std::move(query));
	return result;
}

[[nodiscard]] twidget::Json ElementAtQuery(QPointF point) {
	auto query = twidget::Json::object();
	query.set("q", "elementFromPoint");
	query.set("x", point.x());
	query.set("y", point.y());
	return LocalQuery(std::move(query));
}

[[nodiscard]] twidget::Json ViewportQuery() {
	auto query = twidget::Json::object();
	query.set("q", "viewport");
	return LocalQuery(std::move(query));
}

[[nodiscard]] auto OfType(std::string_view type) {
	return [=](const twidget::Json &message) {
		return (message["type"].asString() == type);
	};
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

void Append(
		std::vector<twidget::Json> &to,
		std::vector<twidget::Json> &&from) {
	to.insert(
		end(to),
		std::make_move_iterator(begin(from)),
		std::make_move_iterator(end(from)));
}

[[nodiscard]] std::vector<twidget::Json> Touch(
		twlite::Renderer &renderer,
		int action,
		QPointF point,
		int modifiers,
		int pointer = kMousePointer,
		int button = kPrimaryButton) {
	return renderer.touch(
		action,
		point.x(),
		point.y(),
		pointer,
		true,
		(pointer == kMousePointer),
		modifiers,
		button);
}

[[nodiscard]] bool OutsideSlop(QPointF point, QPointF origin) {
	const auto delta = point - origin;
	return std::hypot(delta.x(), delta.y())
		> twidget::gesture::TOUCH_SLOP_PX;
}

[[nodiscard]] int ParsedDocuments(const twlite::Renderer &renderer) {
	const auto &stats = renderer.stats();
	return stats.builds - stats.patched;
}

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

RendererWork::RendererWork(
	double width,
	double dpr,
	Host host,
	Fn<void(Changes changes)> changed)
: _host(std::move(host))
, _changed(std::move(changed))
, _backend(std::make_unique<QtBackend>(_host.diag))
, _renderer(std::make_unique<CanvasFontRenderer>(
	ValidWidth(width) ? width : kFallbackWidth,
	dpr,
	false,
	_backend.get()))
, _clock([=](crl::time now) { return step(now); })
, _wake([=] { wake(); })
, _heldTimer([=] { releaseHeld(); refreshClock(); }) {
}

RendererWork::~RendererWork() {
	_clock.stop();
	_wake.cancel();
	releaseInput();
}

void RendererWork::detach() {
	resetInput();
	_renderer->clear_selection();
	_detached = true;
	_pendingSize = std::string();
	if (_loaded) {
		notifyPosted(forwardAll(takeSettled()));
	}
	refreshClock();
	_backend->releaseCanvasFonts();
}

bool RendererWork::attached() const {
	return !_detached;
}

void RendererWork::setOnScreen(bool onScreen) {
	if (_onScreen == onScreen) {
		return;
	}
	_onScreen = onScreen;
	_stepBusy = false;
	if (!onScreen) {
		resetInput();
	}
	refreshClock();
}

bool RendererWork::acceptsInput() const {
	return canTick() && _hasContent;
}

void RendererWork::pointerEnter() {
	if (acceptsInput() && !TouchBridge::Active()) {
		_pointer.over = true;
	}
}

void RendererWork::pointerMove(QPointF point, int modifiers) {
	if (!acceptsInput() || TouchBridge::Active()) {
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

void RendererWork::pointerLeave() {
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

void RendererWork::pointerPress(QPointF point, int modifiers) {
	if (!acceptsInput() || TouchBridge::Active()) {
		return;
	}
	menuRelease(modifiers);
	const auto hit = elementAt(point);
	const auto wasDirty = _renderer->dirty();
	auto out = Touch(*_renderer, kTouchDown, point, modifiers);
	holdPointer(point, hit, false);
	inputDone(out, wasDirty);
}

void RendererWork::pointerRelease(QPointF point, int modifiers) {
	if (!acceptsInput() || !_pointer.pressed) {
		return;
	}
	const auto wasDirty = _renderer->dirty();
	// A finger's press elsewhere took this one: it ends with no click.
	auto out = TouchBridge::Active()
		? Touch(*_renderer, kTouchCancel, _pointer.position, 0)
		: Touch(*_renderer, kTouchUp, point, modifiers);
	_pointer.position = point;
	_pointer.press = 0;
	_pointer.pressed = _pointer.secondary = false;
	_pointer.moved = _pointer.active = false;
	if (!_pointer.over) {
		Append(out, leaveHover());
	}
	inputDone(out, wasDirty);
}

bool RendererWork::menuPress(QPointF point, int modifiers) {
	if (!acceptsInput()
		|| TouchBridge::Active()
		|| _pointer.pressed
		|| _finger.pressed) {
		return false;
	}
	const auto hit = elementAt(point);
	if (hasSelection()) {
		const auto synced = (_pointer.parsed == ParsedDocuments(*_renderer));
		const auto hover = synced ? _pointer.hover : 0;
		// A core press drops the selection: keep it unless the hold is claimed.
		if (!cursorStyles().hold(*_renderer, hit, hover).value_or(false)) {
			return false;
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
	const auto claim = GestureClaim(out);
	const auto verdict = twidget::GestureArbiter().down(
		double(crl::now()),
		point.x(),
		point.y(),
		claim,
		twidget::PointerKind::Mouse,
		kSecondaryButton);
	const auto taken = (verdict.owner == twidget::GestureOwner::Widget);
	if (taken) {
		holdPointer(point, hit, true);
	} else {
		Append(out, Touch(*_renderer, kTouchCancel, point, 0));
	}
	inputDone(out, wasDirty);
	return taken;
}

void RendererWork::menuRelease(int modifiers) {
	if (_pointer.secondary) {
		pointerRelease(_pointer.position, modifiers);
	}
}

std::optional<twidget::WidgetGestureClaim> RendererWork::fingerPress(
		QPointF point,
		int modifiers) {
	if (!acceptsInput() || _finger.pressed) {
		return std::nullopt;
	}
	const auto wasDirty = _renderer->dirty();
	auto out = Touch(*_renderer, kTouchDown, point, modifiers, kFingerPointer);
	const auto claim = GestureClaim(out);
	_finger = { .position = point, .pressed = true };
	inputDone(out, wasDirty);
	return claim;
}

void RendererWork::fingerMove(QPointF point, int modifiers) {
	if (!_finger.pressed || !acceptsInput() || point == _finger.position) {
		return;
	}
	_finger.position = point;
	const auto wasDirty = _renderer->dirty();
	inputDone(
		Touch(*_renderer, kTouchMove, point, modifiers, kFingerPointer),
		wasDirty);
}

void RendererWork::fingerHold() {
	if (!_finger.pressed || !acceptsInput()) {
		return;
	}
	const auto wasDirty = _renderer->dirty();
	inputDone(_renderer->tick(), wasDirty);
}

void RendererWork::fingerRelease(QPointF point, int modifiers) {
	if (!_finger.pressed || !acceptsInput()) {
		return;
	}
	const auto wasDirty = _renderer->dirty();
	auto out = (point != _finger.position)
		? Touch(*_renderer, kTouchMove, point, modifiers, kFingerPointer)
		: std::vector<twidget::Json>();
	_finger = Finger();
	Append(out, Touch(*_renderer, kTouchUp, point, modifiers, kFingerPointer));
	inputDone(out, wasDirty);
}

void RendererWork::fingerCancel() {
	if (!_finger.pressed || !acceptsInput()) {
		return;
	}
	const auto position = base::take(_finger).position;
	const auto wasDirty = _renderer->dirty();
	inputDone(
		Touch(*_renderer, kTouchCancel, position, 0, kFingerPointer),
		wasDirty);
}

bool RendererWork::hovered() const {
	return _pointer.over;
}

bool RendererWork::pressed() const {
	return _pointer.pressed;
}

bool RendererWork::fingerPressed() const {
	return _finger.pressed;
}

bool RendererWork::handCursorAt(QPointF point) {
	if (!acceptsInput()) {
		return false;
	}
	const auto hit = elementAt(point);
	const auto synced = (_pointer.parsed == ParsedDocuments(*_renderer));
	const auto held = _pointer.pressed
		&& !_pointer.moved
		&& !OutsideSlop(point, _pointer.origin);
	const auto hover = held ? (synced ? _pointer.hover : 0) : hit;
	const auto active = (synced && _pointer.active && hover == _pointer.hover)
		? hover
		: 0;
	return cursorStyles().pointer(*_renderer, hit, hover, active);
}

bool RendererWork::hasSelection() const {
	return !_detached && !_stopped && _renderer->has_selection();
}

QString RendererWork::selectedText() const {
	return (_detached || _stopped)
		? QString()
		: QString::fromStdString(_renderer->selected_text());
}

void RendererWork::clearSelection() {
	const auto wasDirty = _renderer->dirty();
	if (_renderer->clear_selection()) {
		inputDone({}, wasDirty);
	}
}

void RendererWork::setWidth(double width) {
	if (!ValidWidth(width)) {
		return;
	}
	const auto was = _renderer->width();
	auto out = std::vector<twidget::Json>();
	_renderer->set_width(width, out);
	auto changes = Changes();
	if (_renderer->width() != was) {
		changes |= Change::Repaint;
	}
	finish(out, changes);
}

void RendererWork::setMaxHeight(double height) {
	const auto wasDirty = _renderer->dirty();
	const auto was = DeviceSize(*_renderer);
	auto out = std::vector<twidget::Json>();
	_renderer->set_max_height(height, out);
	auto changes = Changes();
	if ((DeviceSize(*_renderer) != was)
		|| (!wasDirty && _renderer->dirty())) {
		changes |= Change::Repaint;
	}
	finish(out, changes);
}

double RendererWork::width() const {
	return _renderer->width();
}

std::optional<double> RendererWork::height() const {
	return _height;
}

bool RendererWork::bottomAnchored() const {
	return _heightBottom;
}

bool RendererWork::hasContent() const {
	return _hasContent;
}

bool RendererWork::loaded() const {
	return _loaded;
}

bool RendererWork::stopped() const {
	return _stopped;
}

const QString &RendererWork::stopReason() const {
	return _stopReason;
}

bool RendererWork::stopFatal() const {
	return _stopFatal;
}

not_null<twlite::Renderer*> RendererWork::renderer() const {
	return _renderer.get();
}

const QImage &RendererWork::frame(
		Images::CornersMaskRef corners,
		QSize cardArea,
		bool autoHeight) {
	const auto ratio = style::DevicePixelRatio();
	const auto card = cardArea * ratio;
	const auto fits = [&] {
		return !_frame.isNull()
			&& (_frameArea == cardArea)
			&& (_frameCorners == corners)
			&& (_frame.devicePixelRatio() == ratio)
			&& FitsCard(_frame.size(), card, ratio, autoHeight);
	};
	// A released core shows its last picture, whatever build it holds.
	if ((_detached || !_renderer->dirty())
		&& (_frameSize == DeviceSize(*_renderer))
		&& (_frameCorners == corners)
		&& (_frameArea == cardArea)
		&& (_frame.isNull() || fits())) {
		return _frame;
	}
	const auto builds = _renderer->stats().builds;
	// WHY: the display list names backend font handles that a later build
	// may delete with an idle face, so it is rasterized at once and only
	// the pixels are kept.
	const auto list = _renderer->paint();
	const auto size = DeviceSize(*_renderer);
	const auto settled = (_renderer->stats().builds != builds);
	if (!FitsCard(size, card, ratio, autoHeight)) {
		// A size the list has not laid out yet keeps this card's picture.
		_frameRepaintable = false;
		if (settled) {
			flushQueued();
		}
		if (!fits()) {
			dropFrame();
		}
		return _frame;
	}
	const auto inPlace = RepaintsInPlace(list);
	auto damage = std::vector<twlite::Rect>();
	const auto area = (inPlace
		&& _frameRepaintable
		&& fits()
		&& (_frameSize == size)
		&& (_frame.size() == size)
		&& _renderer->damage(damage))
		? RepaintArea(damage, _renderer->dpr(), corners, card, size)
		: std::nullopt;
	auto limit = area ? repaintFrame(list, *area, corners, card) : std::nullopt;
	if (!limit) {
		auto painted = PaintFrame(list, *_backend, size, _renderer->dpr());
		limit = std::move(painted.limit);
		if (limit->isEmpty() || !fits()) {
			_frame = std::move(painted.image);
			if (!_frame.isNull()) {
				_frame = RoundFrame(std::move(_frame), corners, card);
				_frame.setDevicePixelRatio(ratio);
			}
		}
	}
	const auto refused = !limit->isEmpty();
	_frameRepaintable = inPlace && !refused && !_frame.isNull();
	_frameSize = size;
	_frameCorners = corners;
	_frameArea = cardArea;
	if (refused && !_detached && !_stopped) {
		_host.diag(kPaintBoundsKind, limit->toStdString());
		stop(std::move(*limit), true);
		refreshClock();
		notifyPosted(Change::Stopped | Change::Repaint);
	}
	if (settled) {
		flushQueued(); // paint() holds its build's output for the next call
	}
	return _frame;
}

std::optional<QString> RendererWork::repaintFrame(
		const std::vector<twlite::Command> &list,
		QRect area,
		Images::CornersMaskRef corners,
		QSize card) {
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
		_renderer->dpr());
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

QSize RendererWork::frameArea() const {
	return _frameArea;
}

void RendererWork::dropFrame() {
	_frame = QImage();
	_frameSize = QSize();
	_frameArea = QSize();
	_frameCorners = Images::CornersMaskRef();
	_frameRepaintable = false;
}

void RendererWork::refreshFrame() {
	// A paint-stopped core's replay is refused, so its last picture stays.
	_frameSize = QSize();
	_frameRepaintable = false;
}

void RendererWork::takeFrameFrom(RendererWork &other) {
	_frame = base::take(other._frame);
	_frameSize = base::take(other._frameSize);
	_frameArea = base::take(other._frameArea);
	_frameCorners = base::take(other._frameCorners);
	_frameRepaintable = false;
	other._frameRepaintable = false;
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
	if (_stopped) {
		return;
	} else if (type == "requestFrame") {
		if (!_frameRequested) {
			_idleRequest = canTick()
				&& !_clock.animating()
				&& (crl::now() >= _recentReplyUntil);
			if (_held) {
				dropHeld();
				_clock.stop();
			}
		}
		_frameRequested = true;
		refreshClock();
		return;
	} else if (type == "frozen") {
		// WHY: HtmlWidget::drawStopped() paints the themed stop pill; the
		// core's own freeze would rebuild its badge and fatal dimming into
		// the last frame, so the core never sees this message.
		stop(
			QString::fromStdString(message["reason"].asString()),
			message["fatal"].asBool());
		finish({}, Change::Stopped | Change::Repaint);
		return;
	}
	const auto wasDirty = _renderer->dirty();
	const auto quiet = (type == "ops")
		&& canTick()
		&& !_held
		&& (_frameRequested ? _idleRequest : !_clock.animating())
		&& !_renderer->wants_tick();
	const auto builds = _renderer->stats().builds;
	_backend->beginCanvasBatch();
	auto out = _renderer->receive(message);
	auto changes = Changes();
	if (type == "ops" && !message["ops"].arr().empty()) {
		changes |= Change::Repaint;
		if (!_hasContent) {
			_hasContent = true;
			changes |= Change::Content;
		}
	} else if (type == "anim") {
		changes |= Change::Repaint;
	} else if (type == "query"
		&& message["qid"].isNumber()
		&& (message["qid"].asInt() == kLoadAckQueryId)) {
		_acked = true;
	}
	if (_acked && _hasContent && !_loaded) {
		_loaded = true;
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
	if (_stopped) {
		return;
	}
	auto out = std::vector<twidget::Json>();
	_renderer->set_theme(dark, out);
	finish(out, Change::Repaint);
}

int64 RendererWork::elementAt(QPointF point) {
	const auto wasDirty = _renderer->dirty();
	auto out = _renderer->receive(ElementAtQuery(point));
	const auto result = TakeMessage(out, "result")["result"].asNumber();
	auto changes = forwardAll(out);
	if (!wasDirty && _renderer->dirty()) {
		changes |= Change::Repaint;
	}
	notifyPosted(changes);
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
	_pointer.parsed = ParsedDocuments(*_renderer);
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
	auto changes = forwardAll(out);
	if (!wasDirty && _renderer->dirty()) {
		changes |= Change::Repaint;
	}
	notifyPosted(changes);
	refreshClock();
}

void RendererWork::resetInput() {
	if (!_detached
		&& !_stopped
		&& (_pointer.pressed || _pointer.hovering || _finger.pressed)) {
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
	releaseInput();
}

void RendererWork::releaseInput() {
	_cursor = nullptr;
	_pointer = Pointer();
	_finger = Finger();
}

void RendererWork::stop(QString reason, bool fatal) {
	_stopped = true;
	_frameRequested = false;
	_stopReason = std::move(reason);
	_stopFatal = fatal;
	releaseInput();
}

std::vector<twidget::Json> RendererWork::takeSettled() {
	auto out = _renderer->receive(ViewportQuery());
	out.erase(ranges::remove_if(out, OfType("result")), end(out));
	return out;
}

void RendererWork::flushQueued() {
	if (_detached || _stopped) {
		return;
	}
	notifyPosted(forwardAll(takeSettled()));
	refreshClock();
}

bool RendererWork::canTick() const {
	return _onScreen && _ready && !_detached && !_stopped;
}

bool RendererWork::clockWanted() const {
	return canTick()
		&& (_stepBusy || _frameRequested || _renderer->wants_tick());
}

void RendererWork::refreshClock() {
	if (clockWanted()) {
		_wake.cancel();
		if (!_clock.animating()) {
			_clock.start();
		}
	} else {
		dropHeld();
		_clock.stop();
		scheduleWake();
	}
}

void RendererWork::scheduleWake() {
	const auto seconds = canTick()
		? _renderer->next_tick_in()
		: std::numeric_limits<double>::infinity();
	if (std::isfinite(seconds)) {
		_wake.callOnce(WakeDelay(seconds));
	} else {
		_wake.cancel();
	}
}

void RendererWork::dropHeld() {
	// A reader's build may still need a repaint when this hold ends.
	if (_held
		&& canTick()
		&& (_held->builds != _renderer->stats().builds)
		&& _renderer->dirty()) {
		crl::on_main(this, [=] {
			if (canTick() && _renderer->dirty()) {
				_changed(Change::Repaint);
			}
		});
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
	_stepBusy = canTick()
		&& (_frameRequested || _renderer->wants_tick());
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
	if (canTick()) {
		tick();
	}
	refreshClock();
}

void RendererWork::tick() {
	const auto wasDirty = _renderer->dirty();
	auto changes = forwardAll(_renderer->tick());
	if (base::take(_frameRequested)) {
		forward(FrameMessage());
		_recentReplyUntil = crl::now() + kRecentReply;
	}
	if (!wasDirty && _renderer->dirty()) {
		changes |= Change::Repaint;
	}
	notifyPosted(changes);
}

void RendererWork::finish(
		const std::vector<twidget::Json> &out,
		Changes changes) {
	changes |= forwardAll(out);
	refreshClock();
	if (changes) {
		_changed(changes);
	}
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
	if (!height.isNumber() || !std::isfinite(height.asNumber())) {
		return {};
	}
	const auto clamped = std::clamp(
		height.asNumber(),
		kMinHeight,
		kMaxHeight);
	if (_height == clamped) {
		return {};
	}
	bindHeightAnchor();
	_height = clamped;
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
	} else if (message["type"].asString() == "size") {
		_pendingSize = message.dump();
	}
}

void RendererWork::notifyPosted(Changes changes) {
	if (!changes) {
		return;
	}
	const auto queued = bool(_postedChanges);
	_postedChanges |= changes;
	if (!queued) {
		crl::on_main(this, [=] {
			_changed(base::take(_postedChanges));
		});
	}
}

} // namespace Tgw
