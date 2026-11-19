/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_hosted_glue.h"

#include "core/src/canvas.h"
#include "core/src/css.h"
#include "core/src/renderer_end.h"
#include "tgw/tgw_cursor.h"
#include "tgw/tgw_hosted_wire.h"
#include "tgw/tgw_renderer_rules.h"
#include "twidget/json.h"
#include "twidget/remote_hosted.h"
#include "twidget/renderer.h"
#include "twidget/sandbox_engine.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Tgw {
namespace {

namespace Remote = twlite::remote;
namespace Rules = RendererRules;

using Json = twidget::Json;

constexpr auto kAutoCursor = std::string_view("auto");
constexpr auto kHandCursor = std::string_view("pointer");

static_assert(HostedWire::kMaxMessageBytes
	== twidget::SandboxEngine::kRemotePlatformLimit);

struct Point {
	double x = 0.;
	double y = 0.;
};

struct CanvasSpelling {
	twlite::css::Font font;
	twlite::FontMetrics metrics;
	int id = 0;
};

[[nodiscard]] bool OutsideSlop(Point point, Point origin) {
	return Rules::OutsideSlop(point.x - origin.x, point.y - origin.y);
}

class Glue final : private Remote::RendererChannel {
public:
	Glue(
		std::shared_ptr<twidget::SandboxHostedPlatform> platform,
		bool dark,
		double width);
	Glue(const Glue &) = delete;
	Glue &operator=(const Glue &) = delete;
	~Glue();

	void attach(twidget::Renderer *renderer);
	void platformMessage(std::string message);
	void load();
	void hostMessage(const std::string &json);
	void setTheme(bool dark);
	void destroy();

private:
	struct Mouse {
		std::vector<Json> held;
		Point origin;
		std::uint64_t press = 0;
		std::int64_t hover = 0;
		int parsed = 0;
		bool pressed = false;
		bool secondary = false;
		bool moved = false;
		bool active = false;
	};
	struct Finger {
		std::vector<Json> held;
		std::uint64_t press = 0;
		bool active = false;
		bool unheldTick = false;
	};

	void send(std::string message) override;
	bool request(std::string message, std::string &answer) override;

	void run(std::function<void()> step);
	void control(std::string_view message);
	void open(double dpr);
	void applyTheme();
	void deliver(std::string_view message);
	void received(const Json &message);
	void becomeReady();
	void pressed(const Remote::Touch &touch);
	[[nodiscard]] std::uint64_t clicking(const Remote::Touch &touch) const;
	void pointed(
		const std::optional<Remote::Touch> &touch,
		const std::optional<Remote::Hover> &hover,
		std::vector<Json> &out);
	void trackPress(const Remote::Touch &touch, std::vector<Json> &out);
	void recheckCursor();
	void reportCursor(std::vector<Json> &out);
	[[nodiscard]] std::optional<bool> handAt(
		Point point,
		std::vector<Json> &out);
	[[nodiscard]] std::optional<std::int64_t> hitAt(
		Point point,
		std::vector<Json> &out);
	[[nodiscard]] std::optional<Json> localAnswer(
		Json query,
		std::vector<Json> &out);
	void holdMenus(const Remote::Touch &touch, std::vector<Json> &out);
	void holdTicked(std::vector<Json> &out);
	void menuDeliver(std::uint64_t press);
	void fingerHold(std::uint64_t press);
	void fingerRelease(std::uint64_t press);
	[[nodiscard]] int canvasFont(
		twlite::Backend &backend,
		const std::string &css,
		twlite::FontMetrics &metrics);
	void refuseCanvasFont();
	[[nodiscard]] bool live() const;
	[[nodiscard]] bool halted();
	void noteEndStop();
	void stop(std::string_view reason);
	void forward(const Json &message);
	void forwardAll(std::vector<Json> &&messages);
	void toHost(const std::string &json);
	void diag(const std::string &kind, const std::string &detail);

	const std::shared_ptr<twidget::SandboxHostedPlatform> _platform;
	const Remote::Caps _caps;
	const double _width = 0.;
	twidget::Renderer *_renderer = nullptr;
	std::unique_ptr<Remote::RendererEnd> _end;
	std::unique_ptr<CursorStyles> _styles;
	Rules::CanvasSpellings<CanvasSpelling> _spellings;
	std::unordered_set<std::string> _refusedSpellings;
	std::deque<std::function<void()>> _deferred;
	std::string _pendingSize;
	std::optional<Point> _pointer;
	Mouse _mouse;
	Finger _finger;
	std::uint64_t _downs = 0;
	std::uint64_t _pointers = 0;
	int _cursorBuilds = 0;
	int _canvasRequestsLeft = Rules::kMaxCanvasFontRequests;
	bool _hand = false;
	bool _dark = false;
	bool _loadCalled = false;
	bool _ready = false;
	bool _hasContent = false;
	bool _acked = false;
	bool _loaded = false;
	bool _frozen = false;
	bool _stopped = false;
	bool _running = false;
	bool _destroyed = false;
	bool _canvasFontRefused = false;

};

class HostedRenderer final : public twidget::Renderer {
public:
	explicit HostedRenderer(std::shared_ptr<Glue> glue);
	~HostedRenderer();

	void load() override;
	void send(const std::string &json) override;
	void setTheme(const std::string &theme) override;
	void setWidth(double width) override;
	void destroy() override;

private:
	const std::shared_ptr<Glue> _glue;

};

class NoCanvasHost final : public twlite::CanvasHost {
public:
	int canvas_font(
		const std::string &css,
		twlite::FontMetrics &metrics) override;
	float canvas_text_width(int font, const std::string &text) override;
	bool canvas_source(
		int64_t node,
		int &image,
		int &canvas,
		float &w,
		float &h) override;
	int64_t canvas_allowance(int id) override;

};

Glue::Glue(
	std::shared_ptr<twidget::SandboxHostedPlatform> platform,
	bool dark,
	double width)
: _platform(std::move(platform))
, _caps(HostedWire::EmbeddingCaps())
, _width(width)
, _dark(dark) {
}

Glue::~Glue() {
	_end = nullptr;
}

void Glue::attach(twidget::Renderer *renderer) {
	_renderer = renderer;
}

void Glue::platformMessage(std::string message) {
	run([this, message = std::move(message)] {
		if (!live()) {
			return;
		} else if (HostedWire::IsControl(message)) {
			control(message);
		} else if (!_end) {
			stop("a renderer message came before Open");
		} else {
			deliver(message);
		}
	});
}

void Glue::load() {
	run([this] {
		if (live()) {
			_loadCalled = true;
			becomeReady();
		}
	});
}

void Glue::hostMessage(const std::string &json) {
	if (!live() || _frozen) {
		return;
	}
	auto parsed = Json::parse(json);
	if (!parsed) {
		diag("renderer-message", "unparsed host message");
		return;
	}
	run([this, message = std::move(*parsed)] {
		if (live() && !_frozen) {
			received(message);
		}
	});
}

void Glue::setTheme(bool dark) {
	run([this, dark] {
		if (!live() || _frozen) {
			return;
		}
		_dark = dark;
		if (_end) {
			applyTheme();
		}
	});
}

void Glue::destroy() {
	if (std::exchange(_destroyed, true)) {
		return;
	}
	_renderer = nullptr;
	if (!_running) {
		_deferred.clear();
		_styles = nullptr;
		_end = nullptr;
	}
}

void Glue::send(std::string message) {
	if (!_stopped) {
		_platform->send(message);
	}
}

bool Glue::request(std::string message, std::string &answer) {
	if (_stopped) {
		return false;
	}
	auto result = _platform->request(message);
	if (!result) {
		return false;
	}
	answer = std::move(*result);
	return true;
}

void Glue::run(std::function<void()> step) {
	if (_running) {
		_deferred.push_back(std::move(step));
		return;
	}
	_running = true;
	step();
	while (!_deferred.empty() && !_destroyed) {
		auto next = std::move(_deferred.front());
		_deferred.pop_front();
		next();
	}
	_deferred.clear();
	_running = false;
	if (_destroyed) {
		_styles = nullptr;
		_end = nullptr;
	}
}

void Glue::control(std::string_view message) {
	using Kind = HostedWire::Kind;
	const auto kind = HostedWire::KindOf(message);
	if (!kind
		|| !HostedWire::ToHelper(*kind)
		|| HostedWire::IsTapControl(*kind)) {
		stop("a control the widget helper does not take");
		return;
	} else if (*kind == Kind::Open) {
		const auto dpr = HostedWire::DecodeOpen(message);
		if (_end) {
			stop("a second Open");
		} else if (!dpr) {
			stop("an Open that did not decode");
		} else {
			open(*dpr);
		}
		return;
	} else if (*kind == Kind::CanvasRefill) {
		if (HostedWire::IsExactly(message, Kind::CanvasRefill)) {
			_spellings.refill();
			_refusedSpellings.clear();
			_canvasRequestsLeft = Rules::kMaxCanvasFontRequests;
		} else {
			stop("a canvas refill that did not decode");
		}
		return;
	}
	const auto press = HostedWire::DecodePress(message);
	if (!_end) {
		stop("a press control came before Open");
	} else if (!press) {
		stop("a press control that did not decode");
	} else if (*kind == Kind::MenuDeliver) {
		menuDeliver(*press);
	} else if (*kind == Kind::FingerHold) {
		fingerHold(*press);
	} else {
		fingerRelease(*press);
	}
}

void Glue::open(double dpr) {
	auto &channel = static_cast<Remote::RendererChannel&>(*this);
	_end = std::make_unique<Remote::RendererEnd>(
		_width,
		Rules::DeviceRatio(dpr),
		false,
		channel,
		_caps);
	if (halted()) {
		return;
	}
	_end->renderer().set_canvas_font_hook([this](
			twlite::Backend &backend,
			const std::string &css,
			twlite::FontMetrics &metrics) {
		return canvasFont(backend, css, metrics);
	});
	applyTheme();
	becomeReady();
}

void Glue::applyTheme() {
	if (halted()) {
		return;
	}
	auto out = std::vector<Json>();
	_end->renderer().set_theme(_dark, out);
	_end->report(out);
	if (!halted()) {
		forwardAll(std::move(out));
		recheckCursor();
	}
}

void Glue::deliver(std::string_view message) {
	const auto kind = Remote::kind_of(message);
	auto touch = std::optional<Remote::Touch>();
	auto hover = std::optional<Remote::Hover>();
	auto frame = std::optional<Remote::FrameRequest>();
	auto release = std::uint64_t();
	if (kind == Remote::Kind::Touch) {
		auto decoded = Remote::Touch();
		if (Remote::decode(message, _caps, decoded)) {
			touch = decoded;
			release = clicking(decoded);
			pressed(decoded);
		}
	} else if (kind == Remote::Kind::Hover) {
		auto decoded = Remote::Hover();
		if (Remote::decode(message, _caps, decoded)) {
			hover = decoded;
		}
	} else if (kind == Remote::Kind::FrameRequest) {
		auto decoded = Remote::FrameRequest();
		if (Remote::decode(message, _caps, decoded)) {
			frame = decoded;
		}
	}
	if (frame && !frame->tick && (frame->paint != Remote::Paint::Skip)) {
		auto settled = Rules::TakeSettled(*_end);
		if (halted()) {
			return;
		}
		forwardAll(std::move(settled));
		if (!live()) {
			return;
		}
	}
	auto out = Remote::RendererEnd::Output();
	_end->deliver(message, out);
	if (halted()) {
		return;
	} else if ((kind == Remote::Kind::Touch)
		|| (kind == Remote::Kind::Hover)) {
		pointed(touch, hover, out.messages);
		if (!live()) {
			return;
		}
	}
	if (release
		&& std::any_of(
			out.messages.begin(),
			out.messages.end(),
			Rules::IsClick)) {
		_platform->send(HostedWire::EncodeClicked(release));
	}
	if (touch) {
		holdMenus(*touch, out.messages);
	} else if (frame && frame->tick) {
		holdTicked(out.messages);
	}
	forwardAll(std::move(out.messages));
	if (out.frame) {
		forward(Rules::FrameMessage());
	}
	recheckCursor();
}

void Glue::received(const Json &message) {
	const auto &type = message["type"].asString();
	if (type == "frozen") {
		_frozen = true;
		_styles = nullptr;
		_mouse = Mouse();
		_finger = Finger();
		return;
	} else if (!_end) {
		return;
	} else if (type == "requestFrame") {
		_end->want_frame();
		noteEndStop();
		return;
	}
	auto out = _end->receive(message);
	if (halted()) {
		return;
	}
	const auto load = Rules::AdvanceLoad(
		message,
		_hasContent,
		_acked,
		_loaded);
	if (load.content) {
		_platform->send(HostedWire::EncodeContent());
	}
	if (load.loaded) {
		_platform->send(HostedWire::EncodeLoaded());
		if (type == "ops") {
			Rules::Append(out, Rules::TakeSettled(*_end));
			if (halted()) {
				return;
			}
		}
	}
	forwardAll(std::move(out));
	recheckCursor();
}

void Glue::becomeReady() {
	if (_ready || !_loadCalled || !_end || !live()) {
		return;
	}
	_ready = true;
	const auto pending = std::exchange(_pendingSize, std::string());
	toHost(Rules::kReadyMessage);
	if (!pending.empty() && live()) {
		toHost(pending);
	}
}

void Glue::pressed(const Remote::Touch &touch) {
	const auto down = (touch.action == Rules::kTouchDown);
	const auto ended = (touch.action == Rules::kTouchUp)
		|| (touch.action == Rules::kTouchCancel);
	if (down) {
		++_downs;
	}
	if (touch.mouse) {
		if (down) {
			_mouse = Mouse{
				.press = _downs,
				.pressed = true,
				.secondary = (touch.button == Rules::kSecondaryButton),
			};
		} else if (ended) {
			_mouse = Mouse();
		}
	} else if (down) {
		_finger = Finger{ .press = _downs, .active = true };
	} else if (ended) {
		_finger = Finger();
	}
}

std::uint64_t Glue::clicking(const Remote::Touch &touch) const {
	return (touch.mouse
		&& (touch.action == Rules::kTouchUp)
		&& _mouse.pressed
		&& !_mouse.secondary)
		? _mouse.press
		: 0;
}

void Glue::pointed(
		const std::optional<Remote::Touch> &touch,
		const std::optional<Remote::Hover> &hover,
		std::vector<Json> &out) {
	++_pointers;
	if (hover) {
		_pointer = (hover->action == Rules::kHoverMove)
			? std::make_optional(Point{ .x = hover->x, .y = hover->y })
			: std::nullopt;
		if (!_pointer) {
			_styles = nullptr;
		}
	} else if (touch && touch->mouse) {
		if (touch->button == Rules::kPrimaryButton) {
			_pointer = Point{ .x = touch->x, .y = touch->y };
		}
		trackPress(*touch, out);
		if (halted()) {
			return;
		}
	}
	_hand = (_end->renderer().cursor() == kHandCursor);
	reportCursor(out);
}

void Glue::trackPress(const Remote::Touch &touch, std::vector<Json> &out) {
	if (!_mouse.pressed) {
		return;
	}
	const auto point = Point{ .x = touch.x, .y = touch.y };
	if (touch.action == Rules::kTouchDown) {
		_mouse.origin = point;
		_mouse.hover = hitAt(point, out).value_or(0);
		_mouse.parsed = Rules::ParsedDocuments(_end->renderer());
		_mouse.active = !_mouse.secondary;
		return;
	} else if (touch.action != Rules::kTouchMove) {
		return;
	}
	_mouse.moved = _mouse.moved || OutsideSlop(point, _mouse.origin);
	if (!_mouse.moved || !_mouse.active) {
		return;
	}
	_mouse.active = (_mouse.parsed == Rules::ParsedDocuments(_end->renderer()))
		&& (hitAt(point, out) == _mouse.hover);
}

void Glue::recheckCursor() {
	if (!_pointer
		|| !_end
		|| !live()
		|| (_end->renderer().stats().builds == _cursorBuilds)) {
		return;
	}
	auto out = std::vector<Json>();
	reportCursor(out);
	if (live()) {
		forwardAll(std::move(out));
	}
}

void Glue::reportCursor(std::vector<Json> &out) {
	if (!_pointer || _frozen) {
		_cursorBuilds = _end->renderer().stats().builds;
		return;
	}
	const auto hand = handAt(*_pointer, out);
	if (halted()) {
		return;
	}
	_cursorBuilds = _end->renderer().stats().builds;
	if (!hand || (*hand == _hand)) {
		return;
	}
	_hand = *hand;
	_platform->send(HostedWire::EncodeCursor(
		_pointers,
		_hand ? kHandCursor : kAutoCursor));
}

std::optional<bool> Glue::handAt(Point point, std::vector<Json> &out) {
	const auto hit = hitAt(point, out);
	if (!hit || halted()) {
		return std::nullopt;
	} else if (!*hit) {
		return false;
	}
	auto &renderer = _end->renderer();
	const auto held = _mouse.pressed
		&& !_mouse.moved
		&& !OutsideSlop(point, _mouse.origin);
	const auto targets = Rules::PointerTargets(
		*hit,
		held,
		(_mouse.parsed == Rules::ParsedDocuments(renderer)),
		_mouse.hover,
		_mouse.active);
	if (!_styles) {
		_styles = std::make_unique<CursorStyles>(renderer.dpr());
	}
	const auto builds = renderer.stats().builds;
	const auto result = _styles->pointer(
		renderer,
		*hit,
		targets.hover,
		targets.active);
	if (renderer.stats().builds != builds) {
		Rules::Append(out, Rules::TakeSettled(*_end));
	}
	return result;
}

std::optional<std::int64_t> Glue::hitAt(
		Point point,
		std::vector<Json> &out) {
	auto query = Json::object();
	query.set("q", "elementFromPoint");
	query.set("x", point.x);
	query.set("y", point.y);
	const auto id = localAnswer(std::move(query), out);
	if (!id || !id->isNumber()) {
		return std::nullopt;
	}
	return std::int64_t(id->asNumber());
}

std::optional<Json> Glue::localAnswer(Json query, std::vector<Json> &out) {
	auto messages = _end->receive(Rules::LocalQuery(std::move(query)));
	auto result = std::optional<Json>();
	for (auto &message : messages) {
		if (Rules::OfType("result")(message)) {
			result = message["result"];
		} else {
			out.push_back(std::move(message));
		}
	}
	return result;
}

void Glue::holdMenus(const Remote::Touch &touch, std::vector<Json> &out) {
	if (touch.mouse
		&& (touch.action == Rules::kTouchDown)
		&& (touch.button == Rules::kSecondaryButton)) {
		_mouse.held = Rules::TakeContextMenus(out, "mouse");
	}
}

void Glue::holdTicked(std::vector<Json> &out) {
	if (_finger.active && !std::exchange(_finger.unheldTick, false)) {
		Rules::Append(_finger.held, Rules::TakeContextMenus(out, "touch"));
	}
}

void Glue::menuDeliver(std::uint64_t press) {
	if (_mouse.pressed && _mouse.secondary && (_mouse.press == press)) {
		forwardAll(std::exchange(_mouse.held, {}));
	}
}

void Glue::fingerHold(std::uint64_t press) {
	if (_finger.active && (_finger.press == press)) {
		_finger.unheldTick = true;
		forwardAll(std::exchange(_finger.held, {}));
	}
}

void Glue::fingerRelease(std::uint64_t press) {
	if (_finger.active && (_finger.press == press)) {
		forwardAll(std::exchange(_finger.held, {}));
	}
}

int Glue::canvasFont(
		twlite::Backend &backend,
		const std::string &css,
		twlite::FontMetrics &metrics) {
	metrics = twlite::FontMetrics();
	const auto spelling = _spellings.find(css, [](const std::string &text) {
		return CanvasSpelling{ .font = twlite::css::parse_font(text) };
	});
	if (!spelling) {
		refuseCanvasFont();
		return 0;
	} else if (spelling->id) {
		metrics = spelling->metrics;
		return spelling->id;
	} else if (_refusedSpellings.contains(css)) {
		return 0;
	} else if (_canvasRequestsLeft <= 0) {
		refuseCanvasFont();
		return 0;
	}
	--_canvasRequestsLeft;
	const auto &font = spelling->font;
	const auto id = backend.create_canvas_font(
		font.family,
		font.size,
		font.weight,
		font.italic,
		metrics);
	if (!id) {
		metrics = twlite::FontMetrics();
		_refusedSpellings.insert(css);
		return 0;
	} else if ((metrics.em_ascent == 0.f) && (metrics.em_descent == 0.f)) {
		twlite::set_em_box(
			metrics,
			metrics.ascent,
			metrics.descent,
			font.size);
	}
	spelling->id = id;
	spelling->metrics = metrics;
	return id;
}

void Glue::refuseCanvasFont() {
	if (live() && !std::exchange(_canvasFontRefused, true)) {
		_platform->send(HostedWire::EncodeCanvasRefused());
	}
}

bool Glue::live() const {
	return !_stopped && !_destroyed;
}

bool Glue::halted() {
	noteEndStop();
	return !live();
}

void Glue::noteEndStop() {
	if (_end && _end->stopped()) {
		stop(_end->stop_reason());
	}
}

void Glue::stop(std::string_view reason) {
	if (std::exchange(_stopped, true)) {
		return;
	}
	_pendingSize = std::string();
	_styles = nullptr;
	_mouse = Mouse();
	_finger = Finger();
	_platform->send(HostedWire::EncodeStopped(reason));
}

void Glue::forward(const Json &message) {
	if (_frozen || !live()) {
		return;
	} else if (_ready) {
		toHost(message.dump());
	} else if (Rules::KeptBeforeReady(message)) {
		_pendingSize = message.dump();
	}
}

void Glue::forwardAll(std::vector<Json> &&messages) {
	for (const auto &message : messages) {
		forward(message);
	}
}

void Glue::toHost(const std::string &json) {
	if (!_destroyed && _renderer && _renderer->onMessage) {
		_renderer->onMessage(json);
	}
}

void Glue::diag(const std::string &kind, const std::string &detail) {
	if (!_destroyed && _renderer && _renderer->onDiag) {
		_renderer->onDiag(kind, detail);
	}
}

HostedRenderer::HostedRenderer(std::shared_ptr<Glue> glue)
: _glue(std::move(glue)) {
	_glue->attach(this);
}

HostedRenderer::~HostedRenderer() {
	_glue->destroy();
}

void HostedRenderer::load() {
	_glue->load();
}

void HostedRenderer::send(const std::string &json) {
	_glue->hostMessage(json);
}

void HostedRenderer::setTheme(const std::string &theme) {
	_glue->setTheme(theme == "dark");
}

void HostedRenderer::setWidth(double) {
}

void HostedRenderer::destroy() {
	_glue->destroy();
}

int NoCanvasHost::canvas_font(const std::string &, twlite::FontMetrics &) {
	return 0;
}

float NoCanvasHost::canvas_text_width(int, const std::string &) {
	return 0.f;
}

bool NoCanvasHost::canvas_source(int64_t, int &, int &, float &, float &) {
	return false;
}

int64_t NoCanvasHost::canvas_allowance(int) {
	return 0;
}

} // namespace

void PrepareHostedRenderers() {
	// Fills TwidgetCore's lazy base64 table before any glue thread runs.
	auto pixels = Json::object();
	pixels.set("b64", "AAAA");
	auto arguments = Json::array();
	arguments.push(std::move(pixels));
	auto host = NoCanvasHost();
	const auto canvas = twlite::Canvas(0, 0, 0, host);
	canvas.call("getTransform", arguments);
}

std::unique_ptr<twidget::Renderer> MakeHostedRenderer(
		const std::string &scheme,
		double width,
		std::shared_ptr<twidget::SandboxHostedPlatform> platform) {
	if (!platform) {
		return nullptr;
	}
	auto glue = std::make_shared<Glue>(platform, (scheme == "dark"), width);
	platform->onMessage = [weak = std::weak_ptr<Glue>(glue)](
			const std::string &message) {
		if (const auto strong = weak.lock()) {
			strong->platformMessage(message);
		}
	};
	return std::make_unique<HostedRenderer>(std::move(glue));
}

} // namespace Tgw
