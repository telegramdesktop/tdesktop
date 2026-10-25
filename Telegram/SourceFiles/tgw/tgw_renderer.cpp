/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_renderer.h"

#include "base/platform/base_platform_info.h"
#include "tgw/tgw_backend.h"
#include "tgw/tgw_cursor.h"
#include "tgw/tgw_painter.h"
#include "tgw/tgw_touch.h"
#include "twidget/json.h"
#include "twidget/sdk_policy.h"
#include "TwidgetCore/src/renderer.h"
#include "ui/style/style_core_scale.h"

#include <mutex>

namespace Tgw {

class HostLink final : public std::enable_shared_from_this<HostLink> {
public:
	explicit HostLink(not_null<RendererCore*> core);

	[[nodiscard]] bool attach(not_null<twidget::Renderer*> proxy);
	void detach(not_null<twidget::Renderer*> proxy);
	void close();
	void toHost(const std::string &json);
	void diag(const std::string &kind, const std::string &detail);

	void postLoaded();
	void postMessage(twidget::Json message);
	void postTheme(bool dark);
	void postLinkRequest(RendererCore::LinkRequest request);

private:
	[[nodiscard]] RendererCore *current() const;

	const base::weak_ptr<RendererCore> _core;
	mutable std::mutex _mutex;
	twidget::Renderer *_proxy = nullptr;
	bool _closed = false;

};

namespace {

constexpr auto kFallbackWidth = 1.;
constexpr auto kReadyMessage = "{\"type\":\"ready\"}";
constexpr auto kMinWakeDelay = crl::time(1);
constexpr auto kMaxWakeDelay = crl::time(std::numeric_limits<int>::max());
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
constexpr auto kDomAlt = 1;
constexpr auto kDomCtrl = 2;
constexpr auto kDomMeta = 4;
constexpr auto kDomShift = 8;

class HostRenderer final : public twidget::Renderer {
public:
	explicit HostRenderer(std::shared_ptr<HostLink> link);
	~HostRenderer();

	void load() override;
	void send(const std::string &json) override;
	void setTheme(const std::string &theme) override;
	void setWidth(double width) override;
	void destroy() override;

private:
	const std::shared_ptr<HostLink> _link;

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

[[nodiscard]] QImage RoundFrame(
		QImage &&frame,
		Images::CornersMaskRef corners,
		QSize card) {
	const auto bounds = QRect(QPoint(), frame.size());
	for (auto i = 0; i != 4; ++i) {
		const auto mask = corners.p[i];
		if (!mask
			|| (mask->width() > card.width())
			|| (mask->height() > card.height())) {
			continue;
		}
		const auto right = (i == Images::kTopRight)
			|| (i == Images::kBottomRight);
		const auto bottom = (i == Images::kBottomLeft)
			|| (i == Images::kBottomRight);
		const auto patch = QRect(
			QPoint(
				right ? (card.width() - mask->width()) : 0,
				bottom ? (card.height() - mask->height()) : 0),
			mask->size());
		const auto target = patch.intersected(bounds);
		if (target.isEmpty()) {
			continue;
		}
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
		const twidget::Json &message) {
	return {
		.x = message["x"].asBool(),
		.hold = message["hold"].asBool(),
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

HostRenderer::HostRenderer(std::shared_ptr<HostLink> link)
: _link(std::move(link)) {
}

HostRenderer::~HostRenderer() {
	_link->detach(this);
}

void HostRenderer::load() {
	if (_link->attach(this)) {
		_link->postLoaded();
	}
}

void HostRenderer::send(const std::string &json) {
	if (auto message = twidget::Json::parse(json)) {
		_link->postMessage(std::move(*message));
	} else if (onDiag) {
		onDiag("renderer-message", "unparsed host message");
	}
}

void HostRenderer::setTheme(const std::string &theme) {
	_link->postTheme(theme == "dark");
}

void HostRenderer::setWidth(double) {
}

void HostRenderer::destroy() {
	_link->detach(this);
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

int DomModifiers(Qt::KeyboardModifiers modifiers) {
	const auto mac = Platform::IsMac();
	const auto bits = std::array{
		std::pair(Qt::AltModifier, kDomAlt),
		std::pair(mac ? Qt::MetaModifier : Qt::ControlModifier, kDomCtrl),
		std::pair(mac ? Qt::ControlModifier : Qt::MetaModifier, kDomMeta),
		std::pair(Qt::ShiftModifier, kDomShift),
	};
	auto result = 0;
	for (const auto &[modifier, bit] : bits) {
		if (modifiers.testFlag(modifier)) {
			result |= bit;
		}
	}
	return result;
}

HostLink::HostLink(not_null<RendererCore*> core)
: _core(core) {
}

bool HostLink::attach(not_null<twidget::Renderer*> proxy) {
	auto lock = std::lock_guard(_mutex);
	if (_closed) {
		return false;
	}
	_proxy = proxy.get();
	return true;
}

void HostLink::detach(not_null<twidget::Renderer*> proxy) {
	auto lock = std::lock_guard(_mutex);
	if (_proxy == proxy.get()) {
		_proxy = nullptr;
	}
}

void HostLink::close() {
	auto lock = std::lock_guard(_mutex);
	_closed = true;
	_proxy = nullptr;
}

void HostLink::toHost(const std::string &json) {
	auto lock = std::lock_guard(_mutex);
	if (_proxy && _proxy->onMessage) {
		_proxy->onMessage(json);
	}
}

void HostLink::diag(const std::string &kind, const std::string &detail) {
	auto lock = std::lock_guard(_mutex);
	if (_proxy && _proxy->onDiag) {
		_proxy->onDiag(kind, detail);
	}
}

RendererCore *HostLink::current() const {
	auto lock = std::lock_guard(_mutex);
	return _closed ? nullptr : _core.get();
}

void HostLink::postLoaded() {
	crl::on_main([link = shared_from_this()] {
		if (const auto core = link->current()) {
			core->hostLoaded();
		}
	});
}

void HostLink::postMessage(twidget::Json message) {
	crl::on_main([link = shared_from_this(), message = std::move(message)] {
		if (const auto core = link->current()) {
			core->hostMessage(message);
		}
	});
}

void HostLink::postTheme(bool dark) {
	crl::on_main([link = shared_from_this(), dark] {
		if (const auto core = link->current()) {
			core->hostTheme(dark);
		}
	});
}

void HostLink::postLinkRequest(RendererCore::LinkRequest request) {
	crl::on_main([link = shared_from_this(), request = std::move(request)] {
		if (const auto core = link->current()) {
			core->hostLinkRequest(request);
		}
	});
}

RendererCore::RendererCore(double width, double dpr)
: _link(std::make_shared<HostLink>(this))
, _backend(std::make_unique<QtBackend>(
	[link = _link](const std::string &kind, const std::string &detail) {
		link->diag(kind, detail);
	}))
, _renderer(std::make_unique<CanvasFontRenderer>(
	ValidWidth(width) ? width : kFallbackWidth,
	dpr,
	false,
	_backend.get()))
, _clock([=] { return step(); })
, _wake([=] { wake(); }) {
}

RendererCore::~RendererCore() {
	_clock.stop();
	_wake.cancel();
	releaseInput();
	_link->close();
}

twidget::RendererFactory RendererCore::hostFactory() const {
	return [link = _link](
			std::shared_ptr<twidget::SandboxResources>,
			const std::string &theme,
			double) {
		link->postTheme(theme == "dark");
		return std::unique_ptr<twidget::Renderer>(
			std::make_unique<HostRenderer>(link));
	};
}

auto RendererCore::hostActionHandler() const
-> Fn<void(const twidget::WidgetAction &action)> {
	return [link = _link](const twidget::WidgetAction &action) {
		using Action = twidget::WidgetAction;
		const auto telegram = (action.kind == Action::OpenTelegramLink);
		if (telegram || action.kind == Action::OpenLink) {
			link->postLinkRequest({
				.url = action.url,
				.telegram = telegram,
			});
		}
	};
}

void RendererCore::detach() {
	resetInput();
	_renderer->clear_selection();
	_detached = true;
	_pendingSize = std::string();
	if (_loaded) {
		notifyPosted(forwardAll(takeSettled()));
	}
	refreshClock();
	_link->close();
	_backend->releaseCanvasFonts();
}

bool RendererCore::attached() const {
	return !_detached;
}

void RendererCore::setOnScreen(bool onScreen) {
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

bool RendererCore::acceptsInput() const {
	return canTick() && _hasContent;
}

void RendererCore::pointerEnter() {
	if (acceptsInput() && !TouchBridge::Active()) {
		_pointer.over = true;
	}
}

void RendererCore::pointerMove(QPointF point, int modifiers) {
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

void RendererCore::pointerLeave() {
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

void RendererCore::pointerPress(QPointF point, int modifiers) {
	if (!acceptsInput() || TouchBridge::Active()) {
		return;
	}
	menuRelease(modifiers);
	const auto hit = elementAt(point);
	const auto wasDirty = _renderer->dirty();
	auto out = Touch(*_renderer, kTouchDown, point, modifiers);
	out.erase(ranges::remove_if(out, OfType("gesture")), end(out));
	holdPointer(point, hit, false);
	inputDone(out, wasDirty);
}

void RendererCore::pointerRelease(QPointF point, int modifiers) {
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

bool RendererCore::menuPress(QPointF point, int modifiers) {
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
	const auto claim = GestureClaim(TakeMessage(out, "gesture"));
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

void RendererCore::menuRelease(int modifiers) {
	if (_pointer.secondary) {
		pointerRelease(_pointer.position, modifiers);
	}
}

std::optional<twidget::WidgetGestureClaim> RendererCore::fingerPress(
		QPointF point,
		int modifiers) {
	if (!acceptsInput() || _finger.pressed) {
		return std::nullopt;
	}
	const auto wasDirty = _renderer->dirty();
	auto out = Touch(*_renderer, kTouchDown, point, modifiers, kFingerPointer);
	const auto claim = GestureClaim(TakeMessage(out, "gesture"));
	_finger = { .position = point, .pressed = true };
	inputDone(out, wasDirty);
	return claim;
}

void RendererCore::fingerMove(QPointF point, int modifiers) {
	if (!_finger.pressed || !acceptsInput() || point == _finger.position) {
		return;
	}
	_finger.position = point;
	const auto wasDirty = _renderer->dirty();
	inputDone(
		Touch(*_renderer, kTouchMove, point, modifiers, kFingerPointer),
		wasDirty);
}

void RendererCore::fingerHold() {
	if (!_finger.pressed || !acceptsInput()) {
		return;
	}
	const auto wasDirty = _renderer->dirty();
	inputDone(_renderer->tick(), wasDirty);
}

void RendererCore::fingerRelease(QPointF point, int modifiers) {
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

void RendererCore::fingerCancel() {
	if (!_finger.pressed || !acceptsInput()) {
		return;
	}
	const auto position = base::take(_finger).position;
	const auto wasDirty = _renderer->dirty();
	inputDone(
		Touch(*_renderer, kTouchCancel, position, 0, kFingerPointer),
		wasDirty);
}

bool RendererCore::hovered() const {
	return _pointer.over;
}

bool RendererCore::pressed() const {
	return _pointer.pressed;
}

bool RendererCore::fingerPressed() const {
	return _finger.pressed;
}

bool RendererCore::handCursorAt(QPointF point) {
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

bool RendererCore::hasSelection() const {
	return !_detached && !_stopped && _renderer->has_selection();
}

QString RendererCore::selectedText() const {
	return (_detached || _stopped)
		? QString()
		: QString::fromStdString(_renderer->selected_text());
}

void RendererCore::clearSelection() {
	const auto wasDirty = _renderer->dirty();
	if (_renderer->clear_selection()) {
		inputDone({}, wasDirty);
	}
}

void RendererCore::setWidth(double width) {
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

void RendererCore::setMaxHeight(double height) {
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

double RendererCore::width() const {
	return _renderer->width();
}

std::optional<double> RendererCore::height() const {
	return _height;
}

bool RendererCore::hasContent() const {
	return _hasContent;
}

bool RendererCore::loaded() const {
	return _loaded;
}

bool RendererCore::stopped() const {
	return _stopped;
}

const QString &RendererCore::stopReason() const {
	return _stopReason;
}

bool RendererCore::stopFatal() const {
	return _stopFatal;
}

not_null<twlite::Renderer*> RendererCore::renderer() const {
	return _renderer.get();
}

const QImage &RendererCore::frame(
		Images::CornersMaskRef corners,
		QSize cardArea,
		bool autoHeight) {
	const auto ratio = style::DevicePixelRatio();
	if (!_renderer->dirty()
		&& (_frameSize == DeviceSize(*_renderer))
		&& (_frameCorners == corners)
		&& (_frameArea == cardArea)
		&& (_frame.isNull()
			|| ((_frame.devicePixelRatio() == ratio)
				&& (_frame.size() == _frameSize)))) {
		return _frame;
	}
	const auto builds = _renderer->stats().builds;
	// WHY: the display list names backend font handles that a later build
	// may delete with an idle face, so it is rasterized at once and only
	// the pixels are kept.
	const auto list = _renderer->paint();
	const auto size = DeviceSize(*_renderer);
	const auto settled = (_renderer->stats().builds != builds);
	const auto card = cardArea * ratio;
	if (!FitsCard(size, card, ratio, autoHeight)) {
		// A size the list has not laid out yet keeps this card's picture.
		if (settled) {
			flushQueued();
		}
		const auto keep = !_frame.isNull()
			&& (_frameArea == cardArea)
			&& (_frameCorners == corners)
			&& (_frame.devicePixelRatio() == ratio)
			&& FitsCard(_frame.size(), card, ratio, autoHeight);
		if (!keep) {
			dropFrame();
		}
		return _frame;
	}
	_frame = PaintFrame(list, *_backend, size, _renderer->dpr());
	_frameSize = size;
	_frameCorners = corners;
	_frameArea = cardArea;
	if (!_frame.isNull()) {
		_frame = RoundFrame(std::move(_frame), corners, card);
		_frame.setDevicePixelRatio(ratio);
	}
	if (settled) {
		flushQueued(); // paint() holds its build's output for the next call
	}
	return _frame;
}

void RendererCore::dropFrame() {
	_frame = QImage();
	_frameSize = QSize();
	_frameArea = QSize();
	_frameCorners = Images::CornersMaskRef();
}

void RendererCore::takeFrameFrom(RendererCore &other) {
	_frame = base::take(other._frame);
	_frameSize = base::take(other._frameSize);
	_frameArea = base::take(other._frameArea);
	_frameCorners = base::take(other._frameCorners);
}

rpl::producer<RendererCore::Changes> RendererCore::updates() const {
	return _updates.events();
}

rpl::producer<RendererCore::LinkRequest> RendererCore::linkRequests() const {
	return _linkRequests.events();
}

void RendererCore::hostLoaded() {
	if (_ready || _stopped) {
		return;
	}
	_ready = true;
	_link->toHost(kReadyMessage);
	if (!_pendingSize.empty()) {
		_link->toHost(base::take(_pendingSize));
	}
	refreshClock();
}

void RendererCore::hostMessage(const twidget::Json &message) {
	const auto &type = message["type"].asString();
	if (_stopped) {
		return;
	} else if (type == "requestFrame") {
		_frameRequested = true;
		refreshClock();
		return;
	} else if (type == "frozen") {
		// WHY: HtmlWidget::drawStopped() paints the themed stop pill; the
		// core's own freeze would rebuild its badge and fatal dimming into
		// the last frame, so the core never sees this message.
		_stopped = true;
		_frameRequested = false;
		_stopReason = QString::fromStdString(message["reason"].asString());
		_stopFatal = message["fatal"].asBool();
		releaseInput();
		finish({}, Change::Stopped | Change::Repaint);
		return;
	}
	const auto wasDirty = _renderer->dirty();
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
		if (type == "ops") {
			Append(out, takeSettled());
		}
	}
	if (!wasDirty && _renderer->dirty()) {
		changes |= Change::Repaint;
	}
	finish(out, changes);
}

void RendererCore::hostTheme(bool dark) {
	if (_stopped) {
		return;
	}
	auto out = std::vector<twidget::Json>();
	_renderer->set_theme(dark, out);
	finish(out, Change::Repaint);
}

void RendererCore::hostLinkRequest(const LinkRequest &request) {
	if (!_stopped) {
		_linkRequests.fire_copy(request);
	}
}

int64 RendererCore::elementAt(QPointF point) {
	auto out = _renderer->receive(ElementAtQuery(point));
	const auto result = TakeMessage(out, "result")["result"].asNumber();
	notifyPosted(forwardAll(out));
	return int64(result);
}

CursorStyles &RendererCore::cursorStyles() {
	if (!_cursor) {
		_cursor = std::make_unique<CursorStyles>(_renderer->dpr());
	}
	return *_cursor;
}

void RendererCore::holdPointer(QPointF point, int64 hit, bool secondary) {
	_pointer.position = _pointer.origin = point;
	_pointer.hit = _pointer.hover = _pointer.press = hit;
	_pointer.parsed = ParsedDocuments(*_renderer);
	_pointer.over = _pointer.hovering = _pointer.pressed = true;
	_pointer.moved = false;
	_pointer.active = !secondary;
	_pointer.secondary = secondary;
}

void RendererCore::trackHover() {
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

std::vector<twidget::Json> RendererCore::leaveHover() {
	_cursor = nullptr;
	_pointer.hit = _pointer.hover = 0;
	_pointer.hovering = false;
	return _renderer->hover(kHoverLeave, 0., 0., 0);
}

void RendererCore::inputDone(
		const std::vector<twidget::Json> &out,
		bool wasDirty) {
	auto changes = forwardAll(out);
	if (!wasDirty && _renderer->dirty()) {
		changes |= Change::Repaint;
	}
	notifyPosted(changes);
	refreshClock();
}

void RendererCore::resetInput() {
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

void RendererCore::releaseInput() {
	_cursor = nullptr;
	_pointer = Pointer();
	_finger = Finger();
}

std::vector<twidget::Json> RendererCore::takeSettled() {
	auto out = _renderer->receive(ViewportQuery());
	out.erase(ranges::remove_if(out, OfType("result")), end(out));
	return out;
}

void RendererCore::flushQueued() {
	if (_detached || _stopped) {
		return;
	}
	notifyPosted(forwardAll(takeSettled()));
	refreshClock();
}

bool RendererCore::canTick() const {
	return _onScreen && _ready && !_detached && !_stopped;
}

bool RendererCore::clockWanted() const {
	return canTick()
		&& (_stepBusy || _frameRequested || _renderer->wants_tick());
}

void RendererCore::refreshClock() {
	if (clockWanted()) {
		_wake.cancel();
		if (!_clock.animating()) {
			_clock.start();
		}
	} else {
		_clock.stop();
		scheduleWake();
	}
}

void RendererCore::scheduleWake() {
	const auto seconds = canTick()
		? _renderer->next_tick_in()
		: std::numeric_limits<double>::infinity();
	if (std::isfinite(seconds)) {
		_wake.callOnce(WakeDelay(seconds));
	} else {
		_wake.cancel();
	}
}

bool RendererCore::step() {
	_stepBusy = canTick()
		&& (_frameRequested || _renderer->wants_tick());
	if (!_stepBusy) {
		scheduleWake();
		return false;
	}
	tick();
	return true;
}

void RendererCore::wake() {
	if (canTick()) {
		tick();
	}
	refreshClock();
}

void RendererCore::tick() {
	const auto wasDirty = _renderer->dirty();
	auto changes = forwardAll(_renderer->tick());
	if (base::take(_frameRequested)) {
		forward(FrameMessage());
	}
	if (!wasDirty && _renderer->dirty()) {
		changes |= Change::Repaint;
	}
	notifyPosted(changes);
}

void RendererCore::finish(
		const std::vector<twidget::Json> &out,
		Changes changes) {
	changes |= forwardAll(out);
	refreshClock();
	if (changes) {
		_updates.fire_copy(changes);
	}
}

RendererCore::Changes RendererCore::forwardAll(
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

RendererCore::Changes RendererCore::applySize(
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
	_height = clamped;
	return Change::Height;
}

void RendererCore::forward(const twidget::Json &message) {
	if (_detached || _stopped) {
		return;
	} else if (_ready) {
		_link->toHost(message.dump());
	} else if (message["type"].asString() == "size") {
		_pendingSize = message.dump();
	}
}

void RendererCore::notifyPosted(Changes changes) {
	if (!changes) {
		return;
	}
	const auto queued = bool(_postedChanges);
	_postedChanges |= changes;
	if (!queued) {
		crl::on_main(this, [=] {
			_updates.fire(base::take(_postedChanges));
		});
	}
}

} // namespace Tgw
