/*
This file is part of mokaGram, a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mokagram/music/mokagram_island.h"
#ifdef Q_OS_MAC
#include "mokagram/music/mokagram_island_mac.h"
#endif
#include "mokagram/effects/mokagram_black_hole.h"

#include "mokagram/music/mokagram_island_glyphs.h"
#include "mokagram/music/mokagram_music_library.h"
#include "mokagram/music/mokagram_music_ui.h"
#include "mokagram/music/mokagram_soundcloud.h"
#include "mokagram/lang/mokagram_lang.h"
#include "mokagram/settings/mokagram_settings.h"
#include "media/media_common.h"
#include "media/player/media_player_instance.h"
#include "media/audio/media_audio.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "window/window_adaptive.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/shortcuts.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "ui/effects/animations.h"
#include "ui/text/format_values.h"
#include "ui/text/format_song_document_name.h"
#include "ui/widgets/popup_menu.h"
#include "base/timer.h"
#include "styles/style_info.h"
#include "styles/style_widgets.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QPointer>
#include <QtGui/QPainterPath>
#include <QtGui/QRegion>
#include <QtSvg/QSvgRenderer>

namespace Mokagram::Music {
namespace {

constexpr auto kType = AudioMsgId::Type::Song;
constexpr auto kHoverDelay = crl::time(220);
#ifdef Q_OS_MAC
constexpr auto kLeaveDelay = crl::time(1000);
#else
constexpr auto kLeaveDelay = crl::time(450);
#endif
constexpr auto kSwapDuration = crl::time(320);
constexpr auto kPressInDuration = crl::time(90);
constexpr auto kPressOutDuration = crl::time(320);
constexpr auto kSubstep = 1. / 240.;
constexpr auto kWheelStep = 0.05;
constexpr auto kPausedCoverScale = 0.86;

struct Motion {
	double response = 0.5;
	double damping = 0.82;
};

// Пружины 1 в 1 из islandcn, lib/island/motion.ts.
constexpr auto kAppear = Motion{ 0.483, 0.62 };
// Mokagram: manual opening uses islandcn pressExpand, not alert expand.
constexpr auto kExpand = Motion{ 0.387, 0.792 };
constexpr auto kExpandBody = Motion{ 0.388, 0.791 };
constexpr auto kExpandCorner = Motion{ 0.32, 0.831 };
constexpr auto kCollapse = Motion{ 0.479, 0.894 };
constexpr auto kDismiss = Motion{ 0.432, 1.019 };
constexpr auto kResize = Motion{ 0.5, 0.82 };

// Размеры в точках iPhone из lib/island/metrics.ts.
constexpr auto kIdleWidth = 95;
#ifdef Q_OS_MAC
constexpr auto kCompactHeight = 32;
#else
constexpr auto kCompactHeight = 37;
#endif
constexpr auto kCompactMinWidth = 150;
constexpr auto kCompactMaxWidth = 260;
constexpr auto kCompactPadding = 7;
constexpr auto kCompactCover = 24;
constexpr auto kExpandedWidth = 372;
constexpr auto kExpandedHeight = 202;
constexpr auto kExpandedRadius = 46;
constexpr auto kPadding = 18;
constexpr auto kCover = 52;
constexpr auto kSmall = 22;
constexpr auto kSmallGap = 14;
constexpr auto kProgressY = 88;
constexpr auto kTransportY = 130;
constexpr auto kVolumeY = 172;
constexpr auto kBars = 5;
constexpr auto kBarWidth = 3;
constexpr auto kBarGap = 2;

const auto kLiked = QColor(0xFF, 0x3B, 0x5C);
const auto kMuted = QColor(0x8e, 0x8e, 0x93);

[[nodiscard]] int S(int value) {
	return style::ConvertScale(value);
}

[[nodiscard]] double Lerp(double a, double b, double t) {
	return a + (b - a) * t;
}

[[nodiscard]] QRectF Lerp(const QRectF &a, const QRectF &b, double t) {
	return QRectF(
		Lerp(a.x(), b.x(), t),
		Lerp(a.y(), b.y(), t),
		Lerp(a.width(), b.width(), t),
		Lerp(a.height(), b.height(), t));
}

[[nodiscard]] int BarsWidth() {
	return S(kBars * kBarWidth + (kBars - 1) * kBarGap);
}

class Spring final {
public:
	void jump(double value) {
		_value = _target = value;
		_velocity = 0.;
	}
	void to(double target, Motion motion) {
		_target = target;
		_motion = motion;
	}
	[[nodiscard]] double value() const {
		return _value;
	}
	[[nodiscard]] double target() const {
		return _target;
	}
	[[nodiscard]] bool settled() const {
		return (_value == _target) && (_velocity == 0.);
	}
	void step(double dt, double epsilon) {
		const auto k = std::pow(2. * M_PI / _motion.response, 2);
		const auto c = 4. * M_PI * _motion.damping / _motion.response;
		_velocity += (-k * (_value - _target) - c * _velocity) * dt;
		_value += _velocity * dt;
		if (std::abs(_value - _target) < epsilon
			&& std::abs(_velocity) < epsilon * 10.) {
			jump(_target);
		}
	}

private:
	double _value = 0.;
	double _velocity = 0.;
	double _target = 0.;
	Motion _motion = kResize;

};

enum class Glyph {
	Pause,
	Play,
	Forward,
	Back,
	Output,
	Speaker,
	Repeat,
	RepeatOne,
	Shuffle,
	Lyrics,
	Queue,
	Heart,
	HeartFill,
	Filter,
};

[[nodiscard]] const char *GlyphSvg(Glyph glyph) {
	using namespace IslandGlyphs;
	switch (glyph) {
	case Glyph::Pause: return kGlyphPause;
	case Glyph::Play: return kGlyphPlay;
	case Glyph::Forward: return kGlyphForward;
	case Glyph::Back: return kGlyphBack;
	case Glyph::Output: return kGlyphOutput;
	case Glyph::Speaker: return kGlyphSpeaker;
	case Glyph::Repeat: return kGlyphRepeat;
	case Glyph::RepeatOne: return kGlyphRepeatOne;
	case Glyph::Shuffle: return kGlyphShuffle;
	case Glyph::Lyrics: return kGlyphLyrics;
	case Glyph::Queue: return kGlyphQueue;
	case Glyph::Heart: return kGlyphHeart;
	case Glyph::HeartFill: return kGlyphHeartFill;
	case Glyph::Filter: return kGlyphFilter;
	}
	return kGlyphPlay;
}

// Значок в квадрат size по центру c, растр кэшируется по размеру и цвету.
void PaintGlyph(
		QPainter &p,
		Glyph glyph,
		QPointF c,
		int size,
		QColor color = QColor(Qt::white)) {
	using Key = std::tuple<int, int, QRgb>;
	static auto cache = base::flat_map<Key, QImage>();
	const auto ratio = style::DevicePixelRatio();
	const auto key = Key(int(glyph), size, color.rgba());
	auto i = cache.find(key);
	if (i == end(cache)) {
		auto image = QImage(
			QSize(size, size) * ratio,
			QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(ratio);
		image.fill(Qt::transparent);
		auto q = QPainter(&image);
		auto renderer = QSvgRenderer(QByteArray(GlyphSvg(glyph)));
		renderer.render(&q, QRectF(0, 0, size, size));
		q.setCompositionMode(QPainter::CompositionMode_SourceIn);
		q.fillRect(QRectF(0, 0, size, size), color);
		q.end();
		i = cache.emplace(key, std::move(image)).first;
	}
	p.drawImage(QPointF(c.x() - size / 2., c.y() - size / 2.), i->second);
}

void PaintGlyphScaled(
		QPainter &p,
		Glyph glyph,
		QPointF c,
		int size,
		double scale,
		QColor color = QColor(Qt::white)) {
	if (scale == 1.) {
		PaintGlyph(p, glyph, c, size, color);
		return;
	}
	p.save();
	p.translate(c);
	p.scale(scale, scale);
	PaintGlyph(p, glyph, QPointF(), size, color);
	p.restore();
}

class Island final : public Ui::RpWidget {
public:
	Island(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<Ui::RpWidget*> anchor);

	void relayout();
	void appear();
	[[nodiscard]] std::optional<IslandShot> shot();

protected:
	void paintEvent(QPaintEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;

private:
	enum class Part {
		None,
		Body,
		Cover,
		Heart,
		Shuffle,
		Previous,
		Play,
		Next,
		Repeat,
		Progress,
		Speaker,
		Volume,
		Filter,
		Lyrics,
		Queue,
	};

	[[nodiscard]] bool cursorNear() const;
#ifdef Q_OS_MAC
	void updateDesktopHover();
#endif
	void dismiss();
	void expand(bool expanded);
	void refreshTrack();
	void refreshState(const Media::Player::TrackState &state);
	void startFrames();
	bool frame();
	void applyGeometry();
	void setVolume(double volume, bool finished);
	void dragTo(QPoint point);
	void activate(Part part);
	void step(int delta);
	void showFilterMenu();
	void animatePress(Part part, bool down);
	[[nodiscard]] bool owned() const;
	[[nodiscard]] bool previousAvailable() const;
	[[nodiscard]] bool nextAvailable() const;
	[[nodiscard]] bool areaVisible() const;
	[[nodiscard]] int expandedWidth() const;
	[[nodiscard]] int compactWidth() const;
	[[nodiscard]] double expanded() const;
	[[nodiscard]] QRectF shapeRect() const;
	[[nodiscard]] QRectF frameRect(int width) const;
	[[nodiscard]] QRectF compactFrame() const;
	[[nodiscard]] QRectF fullFrame() const;
	[[nodiscard]] QRectF coverRect() const;
	[[nodiscard]] QRectF barsRect() const;
	[[nodiscard]] QRectF progressRect() const;
	[[nodiscard]] QRectF volumeRect() const;
	[[nodiscard]] QPointF center(Part part) const;
	[[nodiscard]] Part partAt(QPoint point) const;
	[[nodiscard]] static double RatioIn(const QRectF &rect, QPoint point);
	void paintCover(QPainter &p);
	void paintBars(QPainter &p);
	void paintCompactTitle(QPainter &p);
	void paintHeart(QPainter &p);
	void paintExpanded(QPainter &p);
	void paintTrack(QPainter &p, const QRectF &rect, double ratio);
	void paintPressed(QPainter &p, Part part, int radius);
	void paintToggle(QPainter &p, Part part, Glyph glyph, bool active, bool enabled);
	void paintPlay(QPainter &p);

	const not_null<Window::SessionController*> _controller;
	const not_null<Ui::RpWidget*> _anchor;
	Ui::Animations::Basic _animation;
	Ui::Animations::Simple _swap;
	Ui::Animations::Simple _press;
	base::Timer _hoverTimer;
	base::Timer _leaveTimer;
#ifdef Q_OS_MAC
	base::Timer _desktopTimer;
	base::Timer _proximityTimer;
#endif
	base::unique_qptr<Ui::PopupMenu> _menu;
	std::shared_ptr<Data::DocumentMedia> _media;
	Spring _width;
	Spring _height;
	Spring _radius;
	Spring _expanded;
	Spring _alpha;
	Spring _level;
	Spring _coverScale;
	crl::time _lastFrame = 0;
	double _phase = 0.;
	bool _shown = false;
	bool _wantExpanded = false;
	Part _dragging = Part::None;
	Part _pressed = Part::None;
	Part _highlighted = Part::None;
	double _seekProgress = 0.;
	AudioMsgId _track;
	QString _title;
	QString _artist;
	crl::time _position = 0;
	crl::time _length = 0;
	bool _playing = false;
	bool _playingKnown = false;
	int _shapeTop = 0;
	double _centerX = 0.;
	double _shapeLeft = 0.;
	bool _moving = false;
	int _originX = 0;
	Ui::Animations::Simple _heartPop;

};

Island::Island(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::SessionController*> controller,
	not_null<Ui::RpWidget*> anchor)
: RpWidget(
#ifdef Q_OS_MAC
	nullptr
#else
	parent
#endif
)
, _controller(controller)
, _anchor(anchor)
, _animation([=] { return frame(); })
, _hoverTimer([=] { expand(true); })
, _leaveTimer([=] {
	if (_dragging != Part::None || (_menu && !_menu->isHidden())) {
		return;
	}
	// Native window changes may deliver a leave event without cursor movement.
	if (cursorNear()) {
		return;
	}
	expand(false);
}) {
#ifdef Q_OS_MAC
	// The desktop panel has an independent native window, owned by this session.
	QObject::connect(parent, &QObject::destroyed, this, [=] { delete this; });
	setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
	setAttribute(Qt::WA_TranslucentBackground);
	setAttribute(Qt::WA_ShowWithoutActivating);
	setAttribute(Qt::WA_MacAlwaysShowToolWindow);
	InitDesktopIsland(this);
	_desktopTimer.setCallback([=] { relayout(); });
	_desktopTimer.callEach(1000);
	_proximityTimer.setCallback([=] { updateDesktopHover(); });
	_proximityTimer.callEach(50);
#endif
	setMouseTracking(true);
	hide();
	_coverScale.jump(1.);

	const auto instance = Media::Player::instance();
	instance->updatedNotifier(
	) | rpl::filter([](const Media::Player::TrackState &state) {
		return state.id.type() == kType;
	}) | rpl::on_next([=](const Media::Player::TrackState &state) {
		refreshState(state);
	}, lifetime());
	instance->trackChanged(
	) | rpl::filter([](AudioMsgId::Type type) {
		return type == kType;
	}) | rpl::on_next([=] {
		refreshTrack();
	}, lifetime());
	instance->stops(kType) | rpl::on_next([=] {
		dismiss();
	}, lifetime());
	QueueChanges() | rpl::on_next([=] {
		refreshTrack();
	}, lifetime());

	auto &settings = Core::App().settings();
	rpl::merge(
		CoversChanged(),
		LikesChanged(),
		LyricsAvailabilityChanges(),
		_controller->session().downloaderTaskFinished(),
		settings.songVolumeChanges() | rpl::to_empty,
		settings.playerRepeatModeValue() | rpl::to_empty,
		settings.playerOrderModeValue() | rpl::to_empty,
		Mokagram::Settings::Changed() | rpl::to_empty
	) | rpl::on_next([=] {
		update();
	}, lifetime());

	rpl::merge(
		_anchor->geometryValue() | rpl::to_empty,
		_anchor->shownValue() | rpl::to_empty,
		_controller->mainSectionShownChanges() | rpl::to_empty,
		_controller->adaptive().value()
	) | rpl::on_next([=] {
		relayout();
	}, lifetime());

	refreshTrack();
	refreshState(instance->getState(kType));
}

bool Island::owned() const {
	return OwnsTrack(_track);
}

bool Island::previousAvailable() const {
	return owned()
		? PreviousAvailable()
		: Media::Player::instance()->previousAvailable(kType);
}

bool Island::nextAvailable() const {
	return owned()
		? NextAvailable()
		: Media::Player::instance()->nextAvailable(kType);
}

void Island::step(int delta) {
	if (owned()) {
		Step(delta);
	} else if (delta > 0) {
		Media::Player::instance()->next(kType);
	} else {
		Media::Player::instance()->previous(kType);
	}
}

bool Island::areaVisible() const {
#ifdef Q_OS_MAC
	return true;
#else
	return !_controller->adaptive().isOneColumn()
		|| _controller->mainSectionShown();
#endif
}

int Island::expandedWidth() const {
#ifdef Q_OS_MAC
	return std::max(S(kCompactMinWidth), std::min(
		S(kExpandedWidth), DesktopIslandArea().width() - S(24)));
#else
	return std::max(
		std::min(S(kExpandedWidth), _anchor->width() - S(24)),
		S(kCompactMinWidth));
#endif
}

int Island::compactWidth() const {
#ifdef Q_OS_MAC
	return std::max(S(kCompactMinWidth), DesktopIslandNotchWidth() + S(80));
#else
	// Mokagram: keep the chat header clear in narrow windows.
	if (!isWindow() && _anchor->width() < S(600)) {
		return S(kCompactHeight);
	}
	const auto font = style::font(S(13), style::FontFlag::Bold, 0);
	const auto chrome = S(kCompactPadding + kCompactCover + 10)
		+ S(10) + BarsWidth() + S(kCompactPadding + 4);
	const auto wanted = chrome + font->width(_title);
	return std::min(
		std::clamp(wanted, S(kCompactMinWidth), S(kCompactMaxWidth)),
		expandedWidth());
#endif
}

double Island::expanded() const {
	return std::clamp(_expanded.value(), 0., 1.);
}

void Island::appear() {
	if (_shown) {
		return;
	}
	_shown = true;
	if (isHidden()) {
		_width.jump(S(kIdleWidth));
		_height.jump(S(kCompactHeight));
		_radius.jump(S(kCompactHeight) / 2.);
		_expanded.jump(0.);
		_alpha.jump(0.);
	}
	_width.to(_wantExpanded ? expandedWidth() : compactWidth(), kAppear);
	_alpha.to(1., kAppear);
	Shortcuts::ToggleMediaShortcuts(true);
	relayout();
	startFrames();
}

void Island::dismiss() {
	if (!_shown) {
		return;
	}
	_shown = false;
	_wantExpanded = false;
	_hoverTimer.cancel();
	_leaveTimer.cancel();
	_width.to(S(kIdleWidth), kDismiss);
	_height.to(S(kCompactHeight), kDismiss);
	_radius.to(S(kCompactHeight) / 2., kDismiss);
	_expanded.to(0., kDismiss);
	_alpha.to(0., kDismiss);
	startFrames();
}

void Island::expand(bool expanded) {
	_hoverTimer.cancel();
	if (!_shown || _wantExpanded == expanded) {
		return;
	}
	_wantExpanded = expanded;
	const auto motion = expanded ? kExpand : kCollapse;
	_width.to(expanded ? expandedWidth() : compactWidth(), motion);
	_height.to(
		S(expanded ? kExpandedHeight : kCompactHeight),
		expanded ? kExpandBody : motion);
	_radius.to(
		expanded ? S(kExpandedRadius) : (S(kCompactHeight) / 2.),
		expanded ? kExpandCorner : motion);
	_expanded.to(expanded ? 1. : 0., expanded ? kExpandBody : motion);
	startFrames();
}

void Island::refreshTrack() {
	_track = Media::Player::instance()->current(kType);
	const auto document = _track.audio();
	if (!document) {
		_media = nullptr;
		_title = _artist = QString();
		update();
		return;
	}
	if (!_media || _media->owner() != document) {
		_media = document->createMediaView();
		_media->thumbnailWanted(Data::FileOrigin(_track.contextId()));
	}
	if (!OwnsTrack(_track) || !CurrentTrackInfo(_title, _artist)) {
		const auto name = Ui::Text::FormatSongNameFor(document).composedName();
		_title = name.title;
		_artist = name.performer;
	}
	if (_title.isEmpty()) {
		_title = document->filename();
	}
	if (_shown && !_wantExpanded && _width.target() != compactWidth()) {
		_width.to(compactWidth(), kResize);
		startFrames();
	}
	update();
}

void Island::refreshState(const Media::Player::TrackState &state) {
	if (state.id != _track) {
		refreshTrack();
	}
	const auto frequency = std::max(state.frequency, 1);
	_position = (state.position * 1000LL) / frequency;
	_length = (state.length * 1000LL) / frequency;
	if (!_length && state.id.audio()) {
		_length = state.id.audio()->duration();
	}
	const auto playing = Media::Player::ShowPauseIcon(state.state);
	if (!_playingKnown || _playing != playing) {
		const auto animated = _playingKnown && isVisible();
		_playingKnown = true;
		_playing = playing;
		_level.to(playing ? 1. : 0., kResize);
		_coverScale.to(playing ? 1. : kPausedCoverScale, kExpand);
		if (animated) {
			_swap.start([=] { update(); }, 0., 1., kSwapDuration, anim::easeOutCubic);
		} else {
			_coverScale.jump(playing ? 1. : kPausedCoverScale);
		}
		startFrames();
	}
	update();
}

void Island::startFrames() {
	if (!_moving) {
		_moving = true;
		applyGeometry();
	}
	if (!_animation.animating()) {
		_lastFrame = crl::now();
		_animation.start();
	}
}

bool Island::frame() {
	const auto now = crl::now();
	const auto elapsed = std::min((now - _lastFrame) / 1000., 0.05);
	auto left = elapsed;
	_lastFrame = now;
	while (left > 0.) {
		const auto dt = std::min(left, kSubstep);
		left -= dt;
		_width.step(dt, 0.1);
		_height.step(dt, 0.1);
		_radius.step(dt, 0.1);
		_expanded.step(dt, 0.001);
		_alpha.step(dt, 0.001);
		_level.step(dt, 0.001);
		_coverScale.step(dt, 0.001);
	}
	const auto moving = !_width.settled()
		|| !_height.settled()
		|| !_radius.settled()
		|| !_expanded.settled()
		|| !_alpha.settled()
		|| !_level.settled()
		|| !_coverScale.settled();
	const auto bars = _playing
		&& !PowerSaving::On(PowerSaving::kAnimations);
	if (bars) {
		_phase += elapsed;
	}
	_moving = moving;
	if (!_shown && _alpha.value() <= 0.) {
		hide();
		_media = nullptr;
		Shortcuts::ToggleMediaShortcuts(false);
		return false;
	}
	applyGeometry();
	update();
	return moving || bars;
}

void Island::relayout() {
	if (!_shown && isHidden()) {
		return;
	}
	if (!areaVisible() || (!isWindow() && _anchor->width() <= 0)) {
		hide();
		return;
	}
#ifdef Q_OS_MAC
	const auto area = DesktopIslandArea();
	if (area.isEmpty()) {
		hide();
		return;
	}
	_shapeTop = area.y() + S(8);
#else
	const auto area = _anchor->geometry();
	_shapeTop = area.y() + (st::topBarHeight - S(kCompactHeight)) / 2;
#endif
	_centerX = area.x() + area.width() / 2.;
	const auto target = _wantExpanded ? expandedWidth() : compactWidth();
	if (_shown && _width.target() != target) {
		_width.to(target, kResize);
		startFrames();
	}
	applyGeometry();
	if (_shown || _alpha.value() > 0.) {
		show();
		// Чёрная дыра растёт из островка и лежит над ним, пока идёт.
		if (!isWindow() && !Mokagram::BlackHole::Running()) {
			raise();
		}
	}
}

// WHY: пока форма движется, окно стоит в наибольшем размере, иначе
// сдвиг окна каждый кадр Qt догоняет копией старой картинки и всё дрожит.
// В покое окно обжимается по форме, чтобы не перекрывать чат.
void Island::applyGeometry() {
	// Keep the native tracking area stable while the shape expands or collapses.
	const auto stableCanvas = isWindow() || _moving;
	const auto shape = stableCanvas
		? double(expandedWidth() + S(48))
		: _width.value();
	const auto height = stableCanvas
		? double(S(kExpandedHeight + 24))
		: _height.value();
	const auto left = _centerX - shape / 2.;
	const auto x = int(std::floor(left));
	const auto right = int(std::ceil(left + shape));
	_shapeLeft = _centerX - _width.value() / 2. - x;
	_originX = x;
	const auto rect = QRect(x, _shapeTop, right - x, int(std::ceil(height)));
	if (geometry() != rect) {
		setGeometry(rect);
	}
#ifdef Q_OS_MAC
	// Transparent parts of the stable canvas must pass clicks to other apps.
	auto path = QPainterPath();
	const auto radius = std::clamp(_radius.value(), 0., _height.value() / 2.);
	path.addRoundedRect(shapeRect(), radius, radius);
	setMask(QRegion(path.toFillPolygon().toPolygon()));
#endif
}

std::optional<IslandShot> Island::shot() {
	// Desktop coordinates must not enter effects painted inside the chat.
	if (isWindow()) {
		return std::nullopt;
	}
	if (isHidden() || _alpha.value() < 0.5) {
		return std::nullopt;
	}
	return IslandShot{
		.shape = shapeRect().translated(pos()),
		.image = Ui::GrabWidgetToImage(this),
		.geometry = geometry(),
	};
}

QRectF Island::shapeRect() const {
	return QRectF(_shapeLeft, 0., _width.value(), _height.value());
}

QRectF Island::frameRect(int width) const {
	const auto left = std::round(_centerX - width / 2.) - _originX;
	return QRectF(left, 0., width, S(kExpandedHeight));
}

QRectF Island::compactFrame() const {
	return frameRect(compactWidth());
}

QRectF Island::fullFrame() const {
	return frameRect(expandedWidth());
}

QRectF Island::coverRect() const {
	const auto c = compactFrame();
	const auto f = fullFrame();
	const auto small = S(kCompactCover);
	const auto compact = QRectF(
		c.x() + S(kCompactPadding),
		c.y() + (S(kCompactHeight) - small) / 2.,
		small,
		small);
	const auto full = QRectF(
		f.x() + S(kPadding),
		f.y() + S(kPadding),
		S(kCover),
		S(kCover));
	return Lerp(compact, full, expanded());
}

QRectF Island::barsRect() const {
	const auto c = compactFrame();
	const auto f = fullFrame();
	const auto width = BarsWidth();
	const auto height = S(16);
	const auto compact = QRectF(
		c.x() + c.width() - S(kCompactPadding + 4) - width,
		c.y() + (S(kCompactHeight) - height) / 2.,
		width,
		height);
	const auto full = QRectF(
		f.x() + f.width() - S(kPadding) - width,
		f.y() + S(kPadding) + (S(kCover) - height) / 2.,
		width,
		height);
	return Lerp(compact, full, expanded());
}

QRectF Island::progressRect() const {
	const auto f = fullFrame();
	const auto font = style::font(S(12), 0, 0);
	const auto time = font->width(u"-00:00"_q) + S(8);
	const auto left = f.x() + S(kPadding) + time;
	const auto right = f.x() + f.width() - S(kPadding) - time;
	return QRectF(
		left,
		f.y() + S(kProgressY) - S(3),
		std::max(right - left, 0.),
		S(6));
}

QRectF Island::volumeRect() const {
	const auto f = fullFrame();
	const auto left = f.x() + S(kPadding + 20 + 10);
	const auto right = center(Part::Filter).x()
		- S(kSmall / 2 + kSmallGap + 20 + 10);
	return QRectF(
		left,
		f.y() + S(kVolumeY) - S(3),
		std::max(right - left, 0.),
		S(6));
}

QPointF Island::center(Part part) const {
	const auto f = fullFrame();
	const auto middle = f.x() + f.width() / 2.;
	const auto right = f.x() + f.width() - S(kPadding) - S(kSmall) / 2.;
	const auto slot = S(kSmall + kSmallGap);
	const auto transport = f.y() + S(kTransportY);
	const auto volume = f.y() + S(kVolumeY);
	switch (part) {
	case Part::Heart: return {
		f.x() + f.width() - S(kPadding) - BarsWidth() - S(kSmallGap) - S(kSmall) / 2.,
		f.y() + S(kPadding + kCover / 2) };
	case Part::Shuffle: return { middle - S(132), transport };
	case Part::Previous: return { middle - S(66), transport };
	case Part::Play: return { middle, transport };
	case Part::Next: return { middle + S(66), transport };
	case Part::Repeat: return { middle + S(132), transport };
	case Part::Speaker: return { f.x() + S(kPadding + 10), volume };
	case Part::Filter: return { right - 2 * slot, volume };
	case Part::Lyrics: return { right - slot, volume };
	case Part::Queue: return { right, volume };
	default: return { middle, transport };
	}
}

Island::Part Island::partAt(QPoint point) const {
	const auto shape = shapeRect();
	auto path = QPainterPath();
	const auto radius = std::clamp(_radius.value(), 0., shape.height() / 2.);
	path.addRoundedRect(shape, radius, radius);
	if (!path.contains(QPointF(point))) {
		return Part::None;
	} else if (expanded() < 0.6) {
		return Part::Body;
	}
	const auto near = [&](Part part, int hit) {
		const auto c = center(part);
		return std::abs(point.x() - c.x()) <= hit
			&& std::abs(point.y() - c.y()) <= hit;
	};
	for (const auto part : { Part::Previous, Part::Play, Part::Next }) {
		if (near(part, S(24))) {
			return part;
		}
	}
	for (const auto part : {
			Part::Heart,
			Part::Shuffle,
			Part::Repeat,
			Part::Speaker,
			Part::Filter,
			Part::Lyrics,
			Part::Queue }) {
		if (near(part, S(16))) {
			return part;
		}
	}
	if (progressRect().adjusted(0, -S(10), 0, S(10)).contains(QPointF(point))) {
		return Part::Progress;
	} else if (volumeRect().adjusted(-S(4), -S(10), S(4), S(10)).contains(QPointF(point))) {
		return Part::Volume;
	}
	const auto info = QRectF(
		shape.x(),
		shape.y(),
		shape.width(),
		S(kPadding + kCover + 8));
	return info.contains(QPointF(point)) ? Part::Cover : Part::Body;
}

double Island::RatioIn(const QRectF &rect, QPoint point) {
	return rect.width() > 0.
		? std::clamp((point.x() - rect.x()) / rect.width(), 0., 1.)
		: 0.;
}

void Island::setVolume(double volume, bool finished) {
	auto &settings = Core::App().settings();
	volume = std::clamp(volume, 0., 1.);
	if (volume > 0. && finished) {
		settings.setRememberedSongVolume(volume);
	}
	if (volume != settings.songVolume()) {
		Media::Player::mixer()->setSongVolume(volume);
		settings.setSongVolume(volume);
	}
	if (finished) {
		Core::App().saveSettingsDelayed();
	}
}

void Island::animatePress(Part part, bool down) {
	if (down) {
		_highlighted = part;
		_press.start([=] { update(); }, _press.value(0.), 1., kPressInDuration);
	} else if (_highlighted != Part::None) {
		_press.start([=] {
			update();
			if (!_press.animating()) {
				_highlighted = Part::None;
			}
		}, _press.value(1.), 0., kPressOutDuration, anim::easeOutCubic);
	}
}

// Progress and volume fill: the cover accent lifted to a readable
// lightness, white when cover colors are off.
[[nodiscard]] QColor TrackColor() {
	constexpr auto kMinLightness = 0.62;
	const auto accent = CurrentCoverAccent();
	if (!accent.isValid()) {
		return QColor(255, 255, 255);
	}
	auto hsl = accent.toHsl();
	return QColor::fromHslF(
		hsl.hslHueF(),
		hsl.hslSaturationF(),
		std::max(double(hsl.lightnessF()), kMinLightness));
}

void Island::paintEvent(QPaintEvent *e) {
	const auto alpha = std::clamp(_alpha.value(), 0., 1.);
	if (alpha <= 0.) {
		return;
	}
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	p.setOpacity(alpha);
	const auto shape = shapeRect();
	const auto radius = std::clamp(_radius.value(), 0., shape.height() / 2.);
	auto path = QPainterPath();
	path.addRoundedRect(shape, radius, radius);
	p.fillPath(path, QColor(0, 0, 0));
	p.setClipPath(path);
	paintCover(p);
	// Mokagram: fade header details as the island shrinks to its cover.
	const auto details = std::clamp(
		(shape.width() - S(kCompactHeight))
			/ double(S(kCompactMinWidth - kCompactHeight)),
		0.,
		1.);
	p.setOpacity(alpha * details);
	paintBars(p);
	paintHeart(p);
	const auto progress = expanded();
	const auto compact = 1. - std::clamp(progress / 0.2, 0., 1.);
	if (compact > 0.) {
		p.setOpacity(alpha * details * compact);
		paintCompactTitle(p);
	}
	const auto content = std::clamp((progress - 0.2) / 0.6, 0., 1.);
	if (content > 0.) {
		p.setOpacity(alpha * content);
		paintExpanded(p);
	}
}

void Island::paintCover(QPainter &p) {
	const auto e = expanded();
	const auto base = coverRect();
	const auto scale = Lerp(1., _coverScale.value(), e);
	const auto rect = QRectF(
		base.center().x() - base.width() * scale / 2.,
		base.center().y() - base.height() * scale / 2.,
		base.width() * scale,
		base.height() * scale);
	const auto radius = Lerp(S(7), S(12), e) * scale;
	auto path = QPainterPath();
	path.addRoundedRect(rect, radius, radius);
	const auto cover = CoverForPlayback(_track);
	if (cover.isNull()) {
		p.fillPath(path, QColor(0x28, 0x28, 0x28));
		return;
	}
	p.save();
	p.setClipPath(path, Qt::IntersectClip);
	const auto side = std::min(cover.width(), cover.height());
	const auto source = QRect(
		(cover.width() - side) / 2,
		(cover.height() - side) / 2,
		side,
		side);
	p.drawImage(rect, cover, source);
	p.restore();
}

void Island::paintBars(QPainter &p) {
	const auto rect = barsRect();
	const auto accent = CurrentCoverAccent();
	const auto color = accent.isValid() ? accent : QColor(Qt::white);
	const auto level = std::clamp(_level.value(), 0., 1.);
	const auto bar = S(kBarWidth);
	const auto gap = S(kBarGap);
	static constexpr auto kSpeed = std::array{ 9.1, 6.7, 11.3, 7.9, 10.2 };
	static constexpr auto kShift = std::array{ 0.0, 1.7, 0.6, 2.4, 1.1 };
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	for (auto i = 0; i != kBars; ++i) {
		const auto wave = 0.5 + 0.5 * std::sin(_phase * kSpeed[i] + kShift[i]);
		const auto value = Lerp(0.22, 0.3 + 0.7 * wave, level);
		const auto height = std::max(rect.height() * value, double(bar));
		const auto x = rect.x() + i * (bar + gap);
		const auto y = rect.y() + (rect.height() - height) / 2.;
		p.drawRoundedRect(QRectF(x, y, bar, height), bar / 2., bar / 2.);
	}
}

void Island::paintHeart(QPainter &p) {
	const auto e = expanded();
	if (e <= 0.) {
		return;
	}
	const auto c = compactFrame();
	const auto from = QPointF(
		c.x() + c.width() - S(kCompactPadding + 4) - BarsWidth() / 2.,
		c.y() + S(kCompactHeight) / 2.);
	const auto to = center(Part::Heart);
	const auto at = QPointF(Lerp(from.x(), to.x(), e), Lerp(from.y(), to.y(), e));
	const auto track = CurrentQueueTrack();
	const auto liked = !track.id.isEmpty() && IsLiked(track.id);
	const auto opacity = p.opacity();
	const auto state = track.id.isEmpty() ? 0.3 : liked ? 1. : 0.6;
	p.setOpacity(opacity * state * std::clamp(e / 0.5, 0., 1.));
	const auto pop = 1. + 0.3 * std::sin(_heartPop.value(1.) * M_PI);
	const auto press = (_highlighted == Part::Heart)
		? _press.value(_pressed == Part::Heart ? 1. : 0.)
		: 0.;
	PaintGlyphScaled(
		p,
		liked ? Glyph::HeartFill : Glyph::Heart,
		at,
		S(kSmall),
		Lerp(0.3, 1., e) * pop * (1. - 0.12 * press),
		liked ? kLiked : QColor(Qt::white));
	p.setOpacity(opacity);
}

void Island::paintCompactTitle(QPainter &p) {
#ifdef Q_OS_MAC
	return;
#endif
	const auto c = compactFrame();
	const auto font = style::font(S(13), style::FontFlag::Bold, 0);
	const auto left = c.x() + S(kCompactPadding + kCompactCover + 10);
	const auto right = c.x() + c.width()
		- S(kCompactPadding + 4) - BarsWidth() - S(10);
	const auto width = int(std::max(right - left, 0.));
	p.setPen(Qt::white);
	p.setFont(font);
	p.drawText(
		QRectF(left, c.y(), width, S(kCompactHeight)),
		Qt::AlignLeft | Qt::AlignVCenter,
		font->elided(_title, width));
}

void Island::paintTrack(QPainter &p, const QRectF &rect, double ratio) {
	const auto line = rect.height() / 2.;
	p.setPen(Qt::NoPen);
	const auto color = TrackColor();
	auto track = color;
	track.setAlpha(64);
	p.setBrush(track);
	p.drawRoundedRect(rect, line, line);
	p.setBrush(color);
	p.drawRoundedRect(
		QRectF(rect.x(), rect.y(), rect.width() * ratio, rect.height()),
		line,
		line);
}

void Island::paintPressed(QPainter &p, Part part, int radius) {
	if (_highlighted != part) {
		return;
	}
	const auto value = _press.value(_pressed == part ? 1. : 0.);
	if (value <= 0.) {
		return;
	}
	const auto c = center(part);
	const auto r = radius * Lerp(0.8, 1., value);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(255, 255, 255, int(38 * value)));
	p.drawEllipse(c, r, r);
}

void Island::paintToggle(
		QPainter &p,
		Part part,
		Glyph glyph,
		bool active,
		bool enabled) {
	const auto c = center(part);
	const auto opacity = p.opacity();
	if (active) {
		const auto r = S(15);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(255, 255, 255, 46));
		p.drawEllipse(c, r, r);
	}
	paintPressed(p, part, S(17));
	p.setOpacity(opacity * (!enabled ? 0.3 : active ? 1. : 0.6));
	const auto press = (_highlighted == part)
		? _press.value(_pressed == part ? 1. : 0.)
		: 0.;
	PaintGlyphScaled(p, glyph, c, S(kSmall), 1. - 0.12 * press);
	p.setOpacity(opacity);
}

void Island::paintPlay(QPainter &p) {
	const auto c = center(Part::Play);
	const auto opacity = p.opacity();
	const auto press = (_highlighted == Part::Play)
		? _press.value(_pressed == Part::Play ? 1. : 0.)
		: 0.;
	const auto base = 1. - 0.12 * press;
	const auto t = _swap.value(1.);
	const auto now = _playing ? Glyph::Pause : Glyph::Play;
	const auto was = _playing ? Glyph::Play : Glyph::Pause;
	if (t < 1.) {
		p.setOpacity(opacity * (1. - t));
		PaintGlyphScaled(p, was, c, S(30), base * Lerp(1., 0.4, t));
	}
	p.setOpacity(opacity * t);
	PaintGlyphScaled(p, now, c, S(30), base * Lerp(0.4, 1., t));
	p.setOpacity(opacity);
}

void Island::paintExpanded(QPainter &p) {
	const auto f = fullFrame();
	const auto heart = center(Part::Heart);
	const auto left = int(f.x() + S(kPadding + kCover + 12));
	const auto width = std::max(
		int(heart.x() - S(kSmall) / 2. - S(10)) - left,
		0);
	const auto titleFont = style::font(S(15), style::FontFlag::Bold, 0);
	const auto artistFont = style::font(S(13), 0, 0);
	const auto timeFont = style::font(S(12), 0, 0);
	const auto top = int(f.y() + S(kPadding) + (S(kCover)
		- titleFont->height - S(2) - artistFont->height) / 2.);
	p.setPen(Qt::white);
	p.setFont(titleFont);
	p.drawText(
		QRect(left, top, width, titleFont->height),
		Qt::AlignLeft | Qt::AlignVCenter,
		titleFont->elided(_title, width));
	p.setPen(kMuted);
	p.setFont(artistFont);
	p.drawText(
		QRect(left, top + titleFont->height + S(2), width, artistFont->height),
		Qt::AlignLeft | Qt::AlignVCenter,
		artistFont->elided(_artist, width));

	const auto opacity = p.opacity();
	const auto progress = progressRect();
	const auto ratio = (_dragging == Part::Progress)
		? _seekProgress
		: (_length > 0)
		? std::clamp(_position / double(_length), 0., 1.)
		: 0.;
	const auto played = crl::time(base::SafeRound(ratio * _length));
	paintTrack(p, progress, ratio);
	p.setPen(kMuted);
	p.setFont(timeFont);
	const auto timeTop = int(progress.center().y()) - timeFont->height / 2;
	const auto timeWidth = int(progress.x() - f.x()) - S(kPadding) - S(8);
	p.drawText(
		QRect(int(f.x()) + S(kPadding), timeTop, timeWidth, timeFont->height),
		Qt::AlignLeft | Qt::AlignVCenter,
		Ui::FormatDurationText(played / 1000));
	p.drawText(
		QRect(int(progress.x() + progress.width()) + S(8), timeTop, timeWidth, timeFont->height),
		Qt::AlignRight | Qt::AlignVCenter,
		u"-"_q + Ui::FormatDurationText(std::max(_length - played, crl::time(0)) / 1000));

	const auto &settings = Core::App().settings();
	const auto skip = [&](Part part, Glyph glyph, bool enabled) {
		const auto press = (_highlighted == part)
			? _press.value(_pressed == part ? 1. : 0.)
			: 0.;
		p.setOpacity(opacity * (enabled ? 1. : 0.4));
		PaintGlyphScaled(p, glyph, center(part), S(34), 1. - 0.12 * press);
		p.setOpacity(opacity);
	};
	paintToggle(
		p,
		Part::Shuffle,
		Glyph::Shuffle,
		settings.playerOrderMode() == Media::OrderMode::Shuffle,
		true);
	skip(Part::Previous, Glyph::Back, previousAvailable());
	paintPlay(p);
	skip(Part::Next, Glyph::Forward, nextAvailable());
	const auto repeat = settings.playerRepeatMode();
	paintToggle(
		p,
		Part::Repeat,
		(repeat == Media::RepeatMode::One) ? Glyph::RepeatOne : Glyph::Repeat,
		repeat != Media::RepeatMode::None,
		true);

	const auto volume = volumeRect();
	const auto speaker = center(Part::Speaker);
	paintPressed(p, Part::Speaker, S(15));
	p.setOpacity(opacity * 0.6);
	PaintGlyph(p, Glyph::Speaker, speaker, S(20));
	PaintGlyph(
		p,
		Glyph::Output,
		QPointF(volume.x() + volume.width() + S(10 + 10), speaker.y()),
		S(20));
	p.setOpacity(opacity);
	paintTrack(p, volume, std::clamp(settings.songVolume(), 0., 1.));
	paintToggle(
		p,
		Part::Filter,
		Glyph::Filter,
		Mokagram::Settings::Int(u"player.audio_filter"_q) != 0,
		true);
	const auto lyrics = Mokagram::Settings::Bool(u"lyrics.enabled"_q);
	paintToggle(
		p,
		Part::Lyrics,
		Glyph::Lyrics,
		lyrics,
		lyrics || LyricsAvailable());
	paintToggle(p, Part::Queue, Glyph::Queue, false, true);
}


bool Island::cursorNear() const {
	const auto margin = isWindow() ? S(16) : 0;
	return shapeRect().adjusted(-margin, -margin, margin, margin)
		.contains(mapFromGlobal(QCursor::pos()));
}

#ifdef Q_OS_MAC
void Island::updateDesktopHover() {
	if (!_shown || isHidden()) {
		return;
	}
	// Observe proximity without extending the native mask or intercepting clicks.
	if (cursorNear()) {
		_leaveTimer.cancel();
		if (!_wantExpanded && !_hoverTimer.isActive()) {
			_hoverTimer.callOnce(kHoverDelay);
		}
	} else {
		_hoverTimer.cancel();
		if (_wantExpanded && !_leaveTimer.isActive()) {
			_leaveTimer.callOnce(kLeaveDelay);
		}
	}
}
#endif

void Island::enterEventHook(QEnterEvent *e) {
#ifdef Q_OS_MAC
	updateDesktopHover();
#else
	if (partAt(mapFromGlobal(QCursor::pos())) != Part::None) {
		_leaveTimer.cancel();
		if (!_wantExpanded) {
			_hoverTimer.callOnce(kHoverDelay);
		}
	}

#endif
	RpWidget::enterEventHook(e);
}

void Island::leaveEventHook(QEvent *e) {
#ifdef Q_OS_MAC
	updateDesktopHover();
#else
	_hoverTimer.cancel();
	if (_wantExpanded) {
		_leaveTimer.callOnce(kLeaveDelay);
	}
	setCursor(style::cur_default);
#endif
	RpWidget::leaveEventHook(e);
}

void Island::dragTo(QPoint point) {
	if (_dragging == Part::Progress) {
		_seekProgress = RatioIn(progressRect(), point);
		Media::Player::instance()->updateSeeking(kType, _seekProgress);
	} else if (_dragging == Part::Volume) {
		setVolume(RatioIn(volumeRect(), point), false);
	}
	update();
}

void Island::mouseMoveEvent(QMouseEvent *e) {
	const auto part = partAt(e->pos());
#ifdef Q_OS_MAC
	updateDesktopHover();
#else
	if (part == Part::None) {
		_hoverTimer.cancel();
		if (_wantExpanded && _dragging == Part::None && !_leaveTimer.isActive()) {
			_leaveTimer.callOnce(kLeaveDelay);
		}
	} else {
		_leaveTimer.cancel();
		if (!_wantExpanded && !_hoverTimer.isActive()) {
			_hoverTimer.callOnce(kHoverDelay);
		}
	}
#endif
	if (_dragging != Part::None) {
		dragTo(e->pos());
	}
	const auto clickable = (part != Part::None)
		&& (part != Part::Body || !_wantExpanded);
	setCursor(clickable ? style::cur_pointer : style::cur_default);
}

void Island::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	_pressed = partAt(e->pos());
	if (_pressed == Part::Progress && _length > 0) {
		_dragging = Part::Progress;
		Media::Player::instance()->startSeeking(kType);
		dragTo(e->pos());
	} else if (_pressed == Part::Volume) {
		_dragging = Part::Volume;
		dragTo(e->pos());
	} else if (_pressed != Part::Body
		&& _pressed != Part::Cover
		&& _pressed != Part::None) {
		animatePress(_pressed, true);
	}
	update();
}

void Island::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	const auto pressed = std::exchange(_pressed, Part::None);
	const auto dragging = std::exchange(_dragging, Part::None);
	animatePress(pressed, false);
	if (dragging == Part::Progress) {
		_seekProgress = RatioIn(progressRect(), e->pos());
		Media::Player::instance()->finishSeeking(kType, _seekProgress);
	} else if (dragging == Part::Volume) {
		setVolume(RatioIn(volumeRect(), e->pos()), true);
	} else if (pressed != Part::None && pressed == partAt(e->pos())) {
		activate(pressed);
	}
	update();
}

void Island::showFilterMenu() {
	_menu = base::make_unique_q<Ui::PopupMenu>(this, st::popupMenuWithIcons);
	for (const auto &entry : Mokagram::Settings::Entries()) {
		if (entry.key != u"player.audio_filter"_q) {
			continue;
		}
		const auto current = Mokagram::Settings::Int(entry.key);
		const auto icons = std::array{
			&st::menuIconSoundOn,
			&st::menuIconBoosts,
			&st::menuIconStartStream,
			&st::menuIconSettings,
			&st::menuIconSoundSelect,
			&st::menuIconPalette,
			&st::menuIconPhone,
		};
		for (auto i = 0; i != int(entry.choices.size()); ++i) {
			const auto label = entry.choices[i]
				+ (i == current ? u"  ✓"_q : QString());
			_menu->addAction(label, [=] {
				Mokagram::Settings::SetInt(u"player.audio_filter"_q, i);
			}, (i < int(icons.size())) ? icons[i] : nullptr);
		}
		break;
	}
	const auto c = center(Part::Filter);
	_menu->popup(mapToGlobal(QPoint(int(c.x()), int(c.y()) + S(16))));
}

void Island::activate(Part part) {
	const auto instance = Media::Player::instance();
	auto &settings = Core::App().settings();
	switch (part) {
	case Part::Body:
		expand(true);
		break;
	case Part::Cover:
		expand(false);
		ShowFullscreenPlayer(_controller);
		break;
	case Part::Heart:
		if (const auto track = CurrentQueueTrack(); !track.id.isEmpty()) {
			const auto liked = !IsLiked(track.id);
			SetLiked(track, liked);
			if (liked) {
				_heartPop.start([=] { update(); }, 0., 1., crl::time(360), anim::easeOutCubic);
			}
		}
		break;
	case Part::Previous:
		step(-1);
		break;
	case Part::Play:
		instance->playPauseCancelClicked(kType);
		break;
	case Part::Next:
		step(1);
		break;
	case Part::Shuffle:
		settings.setPlayerOrderMode(
			(settings.playerOrderMode() == Media::OrderMode::Shuffle)
				? Media::OrderMode::Default
				: Media::OrderMode::Shuffle);
		Core::App().saveSettingsDelayed();
		break;
	case Part::Repeat:
		settings.setPlayerRepeatMode([&] {
			switch (settings.playerRepeatMode()) {
			case Media::RepeatMode::None: return Media::RepeatMode::One;
			case Media::RepeatMode::One: return Media::RepeatMode::All;
			case Media::RepeatMode::All: return Media::RepeatMode::None;
			}
			return Media::RepeatMode::None;
		}());
		Core::App().saveSettingsDelayed();
		break;
	case Part::Speaker:
		setVolume(
			(settings.songVolume() > 0.)
				? 0.
				: settings.rememberedSongVolume(),
			true);
		break;
	case Part::Filter:
		showFilterMenu();
		break;
	case Part::Lyrics:
		if (Mokagram::Settings::Bool(u"lyrics.enabled"_q) || LyricsAvailable()) {
			ToggleLyricsPanel(_controller, this);
		}
		break;
	case Part::Queue:
		expand(false);
		ShowPlayerBox(_controller);
		break;
	default:
		break;
	}
	update();
}

void Island::wheelEvent(QWheelEvent *e) {
	const auto delta = e->angleDelta().y();
	if (!delta) {
		return;
	}
	const auto volume = Core::App().settings().songVolume();
	setVolume(volume + kWheelStep * (delta / 120.), true);
	e->accept();
}

void Island::contextMenuEvent(QContextMenuEvent *e) {
	if (partAt(e->pos()) == Part::None) {
		return;
	}
	ShowCurrentTrackMenu(_controller, e->globalPos(), this, true);
	e->accept();
}

[[nodiscard]] base::flat_map<QWidget*, QPointer<Island>> &Islands() {
	static auto result = base::flat_map<QWidget*, QPointer<Island>>();
	return result;
}

} // namespace

std::optional<IslandShot> GrabIsland(not_null<QWidget*> parent) {
	auto &islands = Islands();
	const auto i = islands.find(parent.get());
	const auto island = (i != end(islands)) ? i->second.data() : nullptr;
	return island ? island->shot() : std::nullopt;
}

void UpdateIsland(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<Ui::RpWidget*> anchor,
		bool create) {
	auto &islands = Islands();
	auto island = islands[parent.get()].data();
	if (Mokagram::Settings::Int(u"player.presentation"_q) != 1) {
		if (island) {
			delete island;
		}
		islands.remove(parent.get());
		return;
	}
	if (!island) {
		if (!create) {
			islands.remove(parent.get());
			return;
		}
		island = Ui::CreateChild<Island>(parent.get(), controller, anchor);
		islands[parent.get()] = island;
	}
	if (create) {
		island->appear();
	}
	island->relayout();
}

} // namespace Mokagram::Music
