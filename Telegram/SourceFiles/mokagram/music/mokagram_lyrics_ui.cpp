// This file is part of mokaGram, https://github.com/ecronxy/mokagram
// (c) 2026 @ecronx (https://t.me/ecronx). GPLv3 with the attribution term
// of MOKAGRAM_NOTICE.md (section 7(b)): keep this notice when copying.
#include "mokagram/music/mokagram_lyrics_ui.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "mainwidget.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "mokagram/lang/mokagram_lang.h"
#include "mokagram/music/mokagram_music_ui.h"
#include "mokagram/music/mokagram_soundcloud.h"
#include "mokagram/plugins/mokagram_plugin_install.h"
#include "mokagram/settings/mokagram_settings.h"
#include "mokagram/settings/mokagram_settings_box.h"
#include "mokagram/ui/mokagram_box_parts.h"
#include "styles/style_chat.h"
#include "styles/style_window.h"
#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/resize_area.h"
#include "ui/widgets/buttons.h"
#include "window/window_columns.h"
#include "window/window_session_controller.h"

#include <QtCore/QPointer>
#include <QtGui/QGuiApplication>
#include <QtGui/QLinearGradient>
#include <QtGui/QTextLayout>
#include <map>
#include <set>

namespace Mokagram::Music {
namespace details {
namespace {

// Карточка комментария поверх плеера, в палитре и формах mokaGram.
class CommentHoverCard final : public Ui::RpWidget {
public:
	explicit CommentHoverCard(QWidget *parent) : RpWidget(parent) {
		setAttribute(Qt::WA_TransparentForMouseEvents);
		setFocusPolicy(Qt::NoFocus);
		hide();
	}
	void setComment(const TrackComment &comment, const QImage &avatar, int availableWidth) {
		const auto s = [](int v) { return style::ConvertScale(v); };
		_author = comment.author;
		const auto seconds = std::max<qint64>(0, comment.timestampMs / 1000);
		_time = u"%1:%2"_q.arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
		_avatar = avatar;
		const auto cardWidth = std::min(s(360), std::max(0, availableWidth));
		_body.clearLayout();
		_body.setText(comment.body);
		_body.setFont(st::normalFont->f);
		auto option = QTextOption();
		option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
		_body.setTextOption(option);
		_body.beginLayout();
		auto y = 0.;
		while (true) {
			auto line = _body.createLine();
			if (!line.isValid()) break;
			line.setLineWidth(std::max(1, cardWidth - s(32)));
			line.setPosition(QPointF(0., y));
			y += line.height();
		}
		_body.endLayout();
		resize(cardWidth, std::min(s(72) + int(std::ceil(y)),
			std::max(0, parentWidget()->height() - s(28))));
		setAccessibleName(_author + u"\n"_q + comment.body);
		update();
	}
protected:
	void paintEvent(QPaintEvent *) override {
		const auto s = [](int v) { return style::ConvertScale(v); };
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		BoxParts::PaintCard(p, rect());
		const auto avatarRect = QRect(s(16), s(16), s(28), s(28));
		p.save();
		auto clip = QPainterPath();
		clip.addEllipse(avatarRect);
		p.setClipPath(clip);
		if (!_avatar.isNull()) {
			p.drawImage(avatarRect, _avatar);
		} else {
			p.fillRect(avatarRect, st::windowBgActive);
			p.setPen(st::windowFg);
			p.setFont(st::semiboldFont);
			p.drawText(avatarRect, Qt::AlignCenter, _author.left(1).toUpper());
		}
		p.restore();
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		const auto timeWidth = st::normalFont->width(_time);
		p.drawText(QRect(width() - s(16) - timeWidth, s(16), timeWidth, s(28)), Qt::AlignVCenter, _time);
		p.setFont(st::semiboldFont);
		p.setPen(st::windowFg);
		const auto authorWidth = std::max(0, width() - s(76) - timeWidth);
		p.drawText(QRect(s(52), s(16), authorWidth, s(28)), Qt::AlignVCenter,
			st::semiboldFont->elided(_author, authorWidth));
		p.setClipRect(QRect(s(16), s(56), width() - s(32), std::max(0, height() - s(72))));
		_body.draw(&p, QPointF(s(16), s(56)));
	}
private:
	QString _author, _time;
	QImage _avatar;
	QTextLayout _body;
};

} // namespace

// Комментарии живут рядом с текстом в обоих вариантах плеера.
class TimedComments final : public Ui::AbstractButton {
public:
	explicit TimedComments(QWidget *parent) : AbstractButton(parent) {
		resize(style::ConvertScale(32), style::ConvertScale(32));
		setPointerCursor(true);
		setFocusPolicy(Qt::TabFocus);
		setAccessibleName(QString::fromUtf8(Mokagram::Lang::Tr("Комментарии SoundCloud")));
		setClickedCallback([=] {
			if (_active.empty()) return;
			if (_slots.empty()) return;
			_seekRequests.fire_copy(_comments[hoveredComment()].timestampMs);
		});
		Settings::Changed() | rpl::on_next([=](const QString &key) {
			if (key != u"lyrics.comments"_q) return;
			++_generation;
			_loading = false;
			_comments.clear();
			_active.clear();
			_avatars.clear();
			_pendingAvatars.clear();
			_next.clear();
			_note.clear();
			if (Settings::Bool(key) && validTrack()) load({});
			refresh();
		}, lifetime());
		Settings::RowIconsUpdated() | rpl::on_next([=] { update(); }, lifetime());
	}

	void setTrack(const QString &id) {
		if (id == _trackId) return;
		_trackId = id;
		++_generation;
		_loading = false;
		_comments.clear();
		_active.clear();
		_avatars.clear();
		_pendingAvatars.clear();
		_next.clear();
		_note.clear();
		if (Settings::Bool(u"lyrics.comments"_q) && validTrack()) load({});
		refresh();
	}
	void setTimelineDuration(qint64 duration) {
		if (_timelineDuration == duration) return;
		_timelineDuration = duration;
		refresh();
	}
	void setPosition(qint64 position) {
		if (_position == position) return;
		_position = position;
		refresh();
	}
	[[nodiscard]] int avatarCount() const {
		return int(_active.size());
	}
	[[nodiscard]] rpl::producer<> changes() const {
		return _changes.events();
	}
	[[nodiscard]] bool validTrack() const {
		return _trackId.toULongLong() && QString::number(_trackId.toULongLong()) == _trackId;
	}
	[[nodiscard]] rpl::producer<qint64> seekRequests() const {
		return _seekRequests.events();
	}

protected:
	void leaveEventHook(QEvent *event) override {
		if (_hoverCard) _hoverCard->hide();
		AbstractButton::leaveEventHook(event);
	}
	void hideEvent(QHideEvent *event) override {
		if (_hoverCard) _hoverCard->hide();
		AbstractButton::hideEvent(event);
	}
	void resizeEvent(QResizeEvent *event) override {
		layoutSlots();
		AbstractButton::resizeEvent(event);
	}
	void mouseMoveEvent(QMouseEvent *event) override {
		AbstractButton::mouseMoveEvent(event);
		for (auto i = 0; i < int(_slots.size()); ++i) {
			if (_slots[i].rect.contains(event->pos()) && _hoverIndex != i) {
				_hoverIndex = i;
				_hoverComment = 0;
				break;
			}
		}
		updateHoverCard();
	}
	void wheelEvent(QWheelEvent *event) override {
		if (_slots.empty()) return;
		const auto slot = std::clamp(_hoverIndex, 0, int(_slots.size()) - 1);
		const auto count = int(_slots[slot].comments.size());
		_hoverComment = (_hoverComment + (event->angleDelta().y() < 0 ? 1 : count - 1)) % count;
		updateHoverCard();
		update();
		event->accept();
	}
	void paintEvent(QPaintEvent *) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		for (auto i = 0; i < int(_slots.size()); ++i) {
			const auto &slot = _slots[i];
			const auto selected = i == _hoverIndex
				? std::clamp(_hoverComment, 0, int(slot.comments.size()) - 1) : 0;
			const auto &comment = _comments[slot.comments[selected]];
			p.save();
			auto clip = QPainterPath();
			clip.addEllipse(slot.rect);
			p.setClipPath(clip);
			const auto image = _avatars.find(comment.avatarUrl);
			if (image != _avatars.end() && !image->second.isNull()) {
				p.drawImage(slot.rect, image->second);
			} else {
				p.fillRect(slot.rect, st::windowBgActive);
				p.setPen(st::windowFg);
				p.setFont(st::semiboldFont);
				p.drawText(slot.rect, Qt::AlignCenter, comment.author.left(1).toUpper());
			}
			p.restore();
			if (slot.comments.size() > 1) {
				const auto badgeSize = style::ConvertScale(16);
				const auto badge = QRect(slot.rect.right() - badgeSize + 1,
					slot.rect.bottom() - badgeSize + 1, badgeSize, badgeSize);
				p.setPen(Qt::NoPen);
				p.setBrush(st::windowBg);
				p.drawEllipse(badge);
				p.setPen(st::windowFg);
				auto font = st::normalFont->f;
				font.setPixelSize(style::ConvertScale(10));
				p.setFont(font);
				p.drawText(badge, Qt::AlignCenter, slot.comments.size() > 99
					? u"99+"_q : QString::number(slot.comments.size()));
			}
		}
	}

private:
	struct Slot {
		QRect rect;
		std::vector<int> comments;
	};
	int hoveredComment() const {
		const auto &slot = _slots[std::clamp(_hoverIndex, 0, int(_slots.size()) - 1)];
		return slot.comments[std::clamp(_hoverComment, 0, int(slot.comments.size()) - 1)];
	}
	void ensureAvatar(int index) {
		const auto url = _comments[index].avatarUrl;
		if (url.isEmpty() || _avatars.contains(url) || !_pendingAvatars.insert(url).second) return;
		const auto generation = _generation;
		LoadImage(url, crl::guard(this, [=](QImage image) {
			if (generation != _generation) return;
			_pendingAvatars.erase(url);
			_avatars[url] = std::move(image);
			updateHoverCard();
			update();
		}));
	}
	void layoutSlots() {
		_slots.clear();
		const auto step = style::ConvertScale(32);
		const auto side = std::min(style::ConvertScale(28), height());
		const auto capacity = std::max(1, width() / step);
		auto groups = std::map<int, std::vector<int>>();
		for (auto i = 0; i < int(_active.size()); ++i) {
			const auto index = _active[i];
			const auto bin = _timelineDuration > 0
				? std::clamp(int(double(_comments[index].timestampMs) / _timelineDuration * capacity), 0, capacity - 1)
				: std::min(i, capacity - 1);
			groups[bin].push_back(index);
		}
		auto mask = QRegion();
		for (auto &[bin, comments] : groups) {
			const auto x = _timelineDuration > 0
				? int(double(_comments[comments.front()].timestampMs) / _timelineDuration * std::max(0, width() - side))
				: bin * step + (step - side) / 2;
			auto rect = QRect(std::clamp(x, 0, std::max(0, width() - side)),
				(height() - side) / 2, side, side);
			if (!_slots.empty() && _slots.back().rect.right() + style::ConvertScale(4) >= rect.left()) {
				auto &previous = _slots.back().comments;
				previous.insert(previous.end(), comments.begin(), comments.end());
				continue;
			}
			mask += QRegion(rect);
			_slots.push_back({ rect, std::move(comments) });
		}
		setMask(mask);
		for (const auto &slot : _slots) ensureAvatar(slot.comments.front());
	}
	void updateHoverCard() {
		if (_slots.empty() || !isOver() || isHidden()) {
			if (_hoverCard) _hoverCard->hide();
			return;
		}
		const auto slot = std::clamp(_hoverIndex, 0, int(_slots.size()) - 1);
		auto comment = _comments[hoveredComment()];
		const auto count = int(_slots[slot].comments.size());
		if (count > 1) comment.author += u" · %1/%2"_q
			.arg(std::clamp(_hoverComment, 0, count - 1) + 1).arg(count);
		ensureAvatar(hoveredComment());
		if (!_hoverCard) _hoverCard = base::make_unique_q<CommentHoverCard>(window());
		const auto image = _avatars.find(comment.avatarUrl);
		const auto root = window();
		const auto margin = style::ConvertScale(14);
		_hoverCard->setComment(comment, image == _avatars.end() ? QImage() : image->second,
			root->width() - 2 * margin);
		const auto anchor = mapTo(root, _slots[slot].rect.topLeft());
		const auto gap = style::ConvertScale(8);
		const auto top = anchor.y() - gap - _hoverCard->height();
		_hoverCard->move(
			std::clamp(anchor.x(), margin, std::max(margin, root->width() - margin - _hoverCard->width())),
			std::clamp(top >= margin ? top : anchor.y() + height() + gap,
				margin, std::max(margin, root->height() - margin - _hoverCard->height())));
		_hoverCard->show();
		_hoverCard->raise();
	}
	void refresh() {
		const auto wasActive = _active;
		_shownTimestamp = -1;
		_active.clear();
		for (auto index = 0; index < int(_comments.size()); ++index) {
			const auto &comment = _comments[index];
			if (comment.timestampMs < 0 || (_timelineDuration <= 0
				&& (comment.timestampMs > _position || _position - comment.timestampMs >= 7000))) continue;
			_shownTimestamp = std::max(_shownTimestamp, comment.timestampMs);
			_active.push_back(index);
		}
		std::stable_sort(_active.begin(), _active.end(), [&](int left, int right) {
			return _comments[left].timestampMs > _comments[right].timestampMs;
		});
		layoutSlots();
		updateHoverCard();
		update();
		if (wasActive != _active) _changes.fire({});
		// Следующая страница нужна, когда воспроизведение дошло до её таймкодов.
		if (!_loading && _note.isEmpty() && !_next.isEmpty()
			&& (_timelineDuration > 0 || _comments.empty() || _comments.back().timestampMs <= _position + 7000)) {
			load(_next);
		}
	}
	void load(const QString &cursor) {
		LOG(("[moka-dbg] comments load() track '%1' setting %2 valid %3").arg(_trackId).arg(Settings::Bool(u"lyrics.comments"_q) ? 1 : 0).arg(validTrack() ? 1 : 0));
		if (!Settings::Bool(u"lyrics.comments"_q) || !validTrack()) return;
		_loading = true;
		_note.clear();
		refresh();
		const auto generation = _generation;
		LoadTrackComments(_trackId, cursor, crl::guard(this, [=](TrackCommentsPage page) {
			if (generation != _generation) return;
			LOG(("[moka-dbg] comments load %1: %2 items, error '%3'").arg(_trackId).arg(page.comments.size()).arg(page.error));
			_loading = false;
			_note = std::move(page.error);
			if (_note.isEmpty()) {
				if (cursor.isEmpty()) _comments.clear();
				for (auto &comment : page.comments) {
					const auto exists = std::any_of(_comments.begin(), _comments.end(),
						[&](const TrackComment &c) { return c.id == comment.id; });
					if (!exists) _comments.push_back(std::move(comment));
				}
				_next = std::move(page.next);
			}
			refresh();
		}));
	}
	base::unique_qptr<CommentHoverCard> _hoverCard;
	QString _trackId, _next, _note;
	std::vector<TrackComment> _comments;
	std::vector<int> _active;
	std::vector<Slot> _slots;
	std::map<QString, QImage> _avatars;
	std::set<QString> _pendingAvatars;
	int _hoverIndex = 0;
	int _hoverComment = 0;
	qint64 _timelineDuration = 0;
	rpl::event_stream<> _changes;
	rpl::event_stream<qint64> _seekRequests;
	qint64 _shownTimestamp = -1;
	qint64 _position = 0;
	int _generation = 0;
	bool _loading = false;
};

[[nodiscard]] QImage CoverBackgroundForPlayback() {
	const auto current = Media::Player::instance()->current(AudioMsgId::Type::Song);
	const auto cover = CoverForPlayback(current);
	if (cover.isNull()) {
		return {};
	}
	static auto cachedKey = qint64();
	static auto cached = QImage();
	if (cachedKey != cover.cacheKey() || cached.isNull()) {
		cachedKey = cover.cacheKey();
		cached = Images::BlurLargeImage(
			cover.scaled(32, 32, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
				.convertToFormat(QImage::Format_ARGB32_Premultiplied),
			10);
		auto p = QPainter(&cached);
		p.fillRect(cached.rect(), QColor(0x14, 0x14, 0x15, 180));
	}
	return cached;
}

void CoverBackground::refresh(Fn<void()> repaint, bool visible) {
	const auto next = CoverBackgroundForPlayback();
	if (_to.cacheKey() == next.cacheKey()) {
		return;
	}
	const auto animate = visible
		&& !_to.isNull()
		&& !anim::Disabled()
		&& QGuiApplication::applicationState() == Qt::ApplicationActive
		&& !PowerSaving::On(PowerSaving::kAnimations);
	_from = animate ? frame() : QImage();
	_to = next;
	_animation.stop();
	if (animate) {
		_animation.start(
			std::move(repaint),
			0.,
			1.,
			kSwitchDuration,
			anim::easeOutCubic);
	} else {
		repaint();
	}
}

QImage CoverBackground::frame() const {
	const auto progress = (anim::Disabled()
		|| PowerSaving::On(PowerSaving::kAnimations))
		? 1.
		: _animation.value(1.);
	if (progress >= 1.) {
		return _to;
	}
	auto result = QImage(QSize(32, 32), QImage::Format_ARGB32_Premultiplied);
	result.fill(QColor(0x14, 0x14, 0x15));
	auto p = QPainter(&result);
	if (!_from.isNull()) {
		p.drawImage(result.rect(), _from);
	}
	p.setOpacity(progress);
	if (_to.isNull()) {
		p.fillRect(result.rect(), QColor(0x14, 0x14, 0x15));
	} else {
		p.drawImage(result.rect(), _to);
	}
	return result;
}

void CoverBackground::paint(QPainter &p, QRect area, QRect canvas) const {
	p.fillRect(area, QColor(0x14, 0x14, 0x15));
	const auto background = frame();
	if (background.isNull()) {
		return;
	}
	p.save();
	p.setClipRect(area, Qt::IntersectClip);
	p.drawImage(canvas, background);
	p.restore();
}

LyricsView::LyricsView(QWidget *parent)
: RpWidget(parent)
, _comments(Ui::CreateChild<TimedComments>(this)) {
	_comments->changes() | rpl::on_next([=] { layoutComments(); }, lifetime());
	_comments->seekRequests() | rpl::on_next([=](qint64 ms) {
		_lineClicked.fire_copy(ms);
	}, lifetime());
	Settings::Changed() | rpl::on_next([=](const QString &key) {
		if (key == u"lyrics.comments"_q) {
			_heights.clear();
			_layouts.clear();
			layoutComments();
			update();
		}
	}, lifetime());
	resize(width(), 280);
	setMouseTracking(true);
	_clock.start();
	_timer.setInterval(16);
	_timer.setTimerType(Qt::PreciseTimer);
	connect(&_timer, &QTimer::timeout, this, [=] { tick(); });
	connect(qApp, &QGuiApplication::applicationStateChanged, this, [=] {
		updateTimer();
	}, Qt::QueuedConnection);
	PowerSaving::Changes() | rpl::on_next([=] {
		updateTimer();
	}, lifetime());
}

LyricsView::~LyricsView() = default;

// Полноэкранный плеер: текст слева, крупнее, без своей подложки.
void LyricsView::setBig(bool big) {
	_big = big;
	_coverTint = false;
	_heights.clear();
	_layouts.clear();
	updateTimer();
	update();
}

// Полноэкранный плеер рисует сюда свой фон: тогда при перерисовке текста родитель не трогается.
void LyricsView::setBackground(Fn<void(QPainter&, QRect)> paint) {
	_background = std::move(paint);
	setAttribute(Qt::WA_OpaquePaintEvent, _background != nullptr);
}

void LyricsView::setCoverTint(bool on) {
	_coverTint = on;
	updateTimer();
	update();
}

void LyricsView::setLoading() {
	reset();
	layoutComments();
	_status = QString::fromUtf8(Mokagram::Lang::Tr("Ищу текст…"));
	updateTimer();
	update();
}

void LyricsView::setLyrics(Lyrics lyrics) {
	reset();
	_lyrics = std::move(lyrics);
	_status = _lyrics.found
		? QString()
		: _lyrics.unavailable
		? QString::fromUtf8(Mokagram::Lang::Tr("Сервис текста временно недоступен"))
		: QString::fromUtf8(Mokagram::Lang::Tr("Текст этой песни не найден"));
	layoutComments();
	_weights.assign(_lyrics.lines.size(), 0.f);
	updateTimer();
	tick();
	update();
}

void LyricsView::updateCover() {
	if (!_big) {
		_backdrop.refresh([=] { update(); }, isVisible());
	}
}

void LyricsView::setCommentsTrack(const QString &id) {
	_comments->setTrack(id);
	layoutComments();
}

void LyricsView::followPlayback() {
	_manualUntil = 0;
}

void LyricsView::setPosition(qint64 ms, bool playing) {
	const auto jumped = _big && std::abs(ms - livePosition()) > 1000;
	if (jumped) {
		followPlayback();
		_scrollReady = false;
	}
	_comments->setPosition(ms);
	updateCover();
	_position = ms;
	_positionAt = _clock.elapsed();
	_playing = playing;
	updateTimer();
	if (jumped || !_timer.isActive()) {
		tick();
	}
}

// Нажали на строку синхронного текста: время строки в мс (как в Spotify, перемотка к этой строке).
[[nodiscard]] rpl::producer<qint64> LyricsView::lineClicked() const {
	return _lineClicked.events();
}

void LyricsView::wheelEvent(QWheelEvent *e) {
	if (!_lyrics.found || _lyrics.lines.empty()) {
		e->ignore();
		return;
	}
	const auto delta = e->pixelDelta().isNull()
		? -e->angleDelta().y() / 120. * 48.
		: -double(e->pixelDelta().y());
	if (_lyrics.synced) {
		const auto last = int(_lyrics.lines.size()) - 1;
		const auto topMargin = double(style::ConvertScale(_big ? 32 : 16));
		const auto minScroll = anchorY() - topMargin;
		const auto maxScroll = std::max(centerOf(last), minScroll);
		_scroll = std::clamp(_scroll + delta, minScroll, maxScroll);
		_manualUntil = _clock.elapsed() + 3000;
	} else {
		auto total = 24.;
		for (auto i = 0; i != int(_lyrics.lines.size()); ++i) {
			total += heightOf(i);
		}
		_plainScroll = std::clamp(_plainScroll + delta, 0., std::max(total - lyricsHeight(), 0.));
	}
	e->accept();
	update();
}

void LyricsView::mouseMoveEvent(QMouseEvent *e) {
	setCursor((lineAt(e->pos().y()) >= 0) ? style::cur_pointer : style::cur_default);
}

void LyricsView::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	if (const auto index = lineAt(e->pos().y()); index >= 0) {
		_lineClicked.fire_copy(_lyrics.lines[index].timeMs);
	}
}

// Номер строки под точкой по вертикали (-1 нет), считается так же, как рисуется.
[[nodiscard]] int LyricsView::lineAt(int y) {
	if (y >= lyricsHeight() || !_lyrics.found || !_lyrics.synced || _lyrics.lines.empty()) {
		return -1;
	}
	auto top = anchorY() - _scroll;
	for (auto i = 0; i != int(_lyrics.lines.size()); ++i) {
		const auto lineHeight = heightOf(i);
		if (y >= top && y < top + lineHeight) {
			return i;
		}
		top += lineHeight;
		if (top > height()) {
			break;
		}
	}
	return -1;
}

void LyricsView::reset() {
	_lyrics = {};
	_current = -1;
	_scroll = 0.;
	_plainScroll = 0.;
	_manualUntil = 0;
	_scrollReady = false;
	_heights.clear();
	_layouts.clear();
	_weights.clear();
	_fill = 0.;
}

// Позиция трека между обновлениями плеера: плавнее подсветка строк.
[[nodiscard]] qint64 LyricsView::livePosition() const {
	return _position + (_playing
		? std::min<qint64>(_clock.elapsed() - _positionAt, 600)
		: 0);
}

void LyricsView::tick() {
	if (!isVisible()) {
		_last = _clock.elapsed();
		return;
	}
	const auto now = _clock.elapsed();
	const auto dt = std::clamp((now - _last) / 1000., 0., 0.1);
	_last = now;
	auto changed = false;
	auto needLayout = false;
	if (_lyrics.found && _lyrics.synced && !_lyrics.lines.empty()) {
		auto index = -1;
		const auto at = livePosition() + 120;
		for (auto i = 0; i != int(_lyrics.lines.size()); ++i) {
			if (_lyrics.lines[i].timeMs <= at) {
				index = i;
			} else {
				break;
			}
		}
		changed |= (_current != index);
		needLayout |= (_current != index);
		_current = index;
		auto target = centerOf(std::max(index, 0));
		const auto topMargin = double(style::ConvertScale(_big ? 32 : 16));
		target = std::max(target, anchorY() - topMargin);
		const auto snap = !_scrollReady;
		if (snap) {
			needLayout = true;
			_scroll = target;
			_scrollReady = true;
		}
		// Пружина без рывков: чем дальше строка, тем быстрее догоняем.
		// Пока пользователь листает текст сам (и 3 с после), автопрокрутка к строке не работает.
		if (now >= _manualUntil) {
			const auto was = _scroll;
			changed |= (std::abs(target - _scroll) > 0.1);
			_scroll += (target - _scroll)
				* (_timer.isActive() ? (1. - std::exp(-dt * 6.5)) : 1.);
			needLayout |= (_scroll != was);
		}
		const auto fade = _timer.isActive() && !snap
			? float(1. - std::exp(-dt * 9.))
			: 1.f;
		for (auto i = 0; i != int(_weights.size()); ++i) {
			const auto goal = (i == _current) ? 1.f : 0.f;
			changed |= (std::abs(goal - _weights[i]) > 0.001f);
			_weights[i] += (goal - _weights[i]) * fade;
		}
		const auto fill = (_current >= 0)
			? lineProgress(_current, at)
			: 0.;
		changed |= (std::abs(fill - _fill) > 0.0005);
		_fill = fill;
	}
	if (needLayout) {
		layoutComments();
	}
	if (changed) {
		update();
	}
}

// Доля спетого в строке (0..1). Пословных меток у lrc нет, поэтому темп
// угадываем: ~12 знаков/с, как поют обычно. Пауза в конце строки
// заливку не растягивает: строка заливается раньше, чем начнётся следующая.
[[nodiscard]] double LyricsView::lineProgress(int index, qint64 at) const {
	const auto &lines = _lyrics.lines;
	const auto start = lines[index].timeMs;
	auto span = qint64(std::max(int(lines[index].text.size()), 1)) * 85;
	if (index + 1 < int(lines.size())) {
		span = std::min(span, lines[index + 1].timeMs - start - 150);
	}
	span = std::max<qint64>(span, 250);
	return std::clamp((at - start) / double(span), 0., 1.);
}

[[nodiscard]] double LyricsView::anchorY() const {
	return lyricsHeight() * (_big ? 0.4 : 0.5);
}

[[nodiscard]] int LyricsView::textWidth() const {
	const auto gutter = Settings::Bool(u"lyrics.comments"_q) && _comments->validTrack()
		? style::ConvertScale(108) : 0;
	return std::max(1, width() - (_big ? 0 : style::ConvertScale(48)) - gutter);
}

[[nodiscard]] QFont LyricsView::lineFont() const {
	auto font = st::semiboldFont->f;
	font.setPixelSize(style::ConvertScale(_big ? 36 : 24));
	if (_big) {
		font.setWeight(QFont::DemiBold);
	}
	return font;
}

[[nodiscard]] const QTextLayout &LyricsView::layoutOf(int index) {
	if (_layouts.size() != _lyrics.lines.size()) {
		_layouts.clear();
		_layouts.resize(_lyrics.lines.size());
	}
	auto &layout = _layouts[index];
	if (!layout) {
		const auto &text = _lyrics.lines[index].text;
		layout = std::make_unique<QTextLayout>(
			text.isEmpty() ? u" "_q : text,
			lineFont());
		auto option = QTextOption(_big ? Qt::AlignLeft : Qt::AlignHCenter);
		option.setWrapMode(QTextOption::WordWrap);
		layout->setTextOption(option);
		layout->beginLayout();
		auto y = 0.;
		for (auto line = layout->createLine(); line.isValid(); line = layout->createLine()) {
			line.setLineWidth(textWidth());
			line.setPosition(QPointF(0., y));
			y += line.height();
		}
		layout->endLayout();
	}
	return *layout;
}

[[nodiscard]] int LyricsView::heightOf(int index) {
	if (_heights.size() != _lyrics.lines.size()) {
		_heights.assign(_lyrics.lines.size(), -1);
	}
	if (_heights[index] < 0) {
		_heights[index] = int(std::ceil(layoutOf(index).boundingRect().height()))
			+ style::ConvertScale(_big ? 26 : 24);
	}
	return _heights[index];
}

// Строка поверх фона; fill >= 0 заливает спетую часть белым слева направо с мягким краем.
void LyricsView::drawLayout(
		QPainter &p,
		const QTextLayout &layout,
		QPointF origin,
		double fill,
		double sung,
		double unsung) const {
	const auto white = [](double alpha) {
		auto color = QColor(255, 255, 255);
		color.setAlphaF(std::clamp(alpha, 0., 1.));
		return color;
	};
	const auto position = fill * layout.text().size();
	const auto feather = style::ConvertScale(_big ? 16 : 10);
	for (auto i = 0; i != layout.lineCount(); ++i) {
		const auto line = layout.lineAt(i);
		const auto from = line.textStart();
		const auto till = from + line.textLength();
		if (fill < 0. || position >= till) {
			p.setPen(white(sung));
		} else if (position <= from) {
			p.setPen(white(unsung));
		} else {
			const auto index = int(position);
			const auto left = line.cursorToX(index);
			const auto right = line.cursorToX(std::min(index + 1, till));
			const auto x = origin.x() + left + (right - left) * (position - index);
			auto gradient = QLinearGradient(x - feather, 0., x + feather, 0.);
			gradient.setColorAt(0., white(sung));
			gradient.setColorAt(1., white(unsung));
			p.setPen(QPen(QBrush(gradient), 1.));
		}
		line.draw(&p, origin);
	}
}

// Центр строки index в координатах содержимого.
[[nodiscard]] double LyricsView::centerOf(int index) {
	auto y = 0.;
	for (auto i = 0; i != index; ++i) {
		y += heightOf(i);
	}
	return y + heightOf(index) / 2.;
}

void LyricsView::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	auto rounded = QPainterPath();
	if (_big) {
		if (_background) {
			_background(p, e->rect());
		}
	} else {
		// Скругление нужно только подложке и затуханию, текст рисуется без маски.
		rounded.addRoundedRect(QRectF(rect()), 12, 12);
		p.save();
		p.setClipPath(rounded);
		if (_coverTint) {
			_backdrop.paint(p, rect(), rect());
		} else {
			p.fillRect(rect(), st::windowBgOver);
		}
		p.restore();
	}
	p.setClipRect(QRect(0, 0, width(), lyricsHeight()));
	if (!_lyrics.found) {
		p.setPen(_big ? QColor(255, 255, 255, 140) : st::windowSubTextFg->c);
		p.setFont(st::normalFont);
		p.drawText(QRect(0, 0, width(), lyricsHeight()), Qt::AlignCenter, _status);
		return;
	}
	const auto count = int(_lyrics.lines.size());
	auto top = _lyrics.synced ? (anchorY() - _scroll) : (12. - _plainScroll);
	p.setFont(lineFont());
	for (auto i = 0; i != count; ++i) {
		const auto lineHeight = heightOf(i);
		if (top + lineHeight >= -20 && top <= height() + 20) {
			const auto w = (_lyrics.synced && i < int(_weights.size()))
				? double(_weights[i])
				: 0.;
			// Дальние строки тусклее, уже спетые ещё тусклее, текущая белая.
			const auto distance = _lyrics.synced
				? std::abs(i - std::max(_current, 0))
				: 1;
			const auto past = _lyrics.synced && i < _current;
			const auto rest = !_lyrics.synced
				? 0.84
				: std::max(0.22, (past ? 0.34 : 0.44) - 0.04 * distance);
			const auto cy = top + lineHeight / 2.;
			auto edge = 1.;
			if (_big) {
				// Края текста растворяются в фоне плавно.
				const auto smooth = [](double t) {
					t = std::clamp(t, 0., 1.);
					return t * t * (3. - 2. * t);
				};
				edge = smooth(cy / (height() * 0.2))
					* smooth((height() - cy) / (height() * 0.28));
			}
			// Без масштабирования: текст с трансформацией не берётся из кэша глифов и тормозит.
			const auto origin = _big
				? QPointF(0., top + style::ConvertScale(13))
				: QPointF(style::ConvertScale(24), top + style::ConvertScale(12));
			drawLayout(
				p,
				layoutOf(i),
				origin,
				(_lyrics.synced && i == _current) ? _fill : -1.,
				(rest + (1. - rest) * w) * edge,
				(rest + (0.45 - rest) * w) * edge);
		}
		top += lineHeight;
		if (top > height() + 400) {
			break;
		}
	}
	if (_big) {
		return;
	}
	// Мягкое затухание сверху и снизу.
	p.setClipPath(rounded);
	auto fadeTop = QLinearGradient(0, 0, 0, 56);
	fadeTop.setColorAt(0., QColor(0, 0, 0, 140));
	fadeTop.setColorAt(1., QColor(0, 0, 0, 0));
	p.fillRect(QRect(0, 0, width(), 56), fadeTop);
	auto fadeBottom = QLinearGradient(0, height() - 56, 0, height());
	fadeBottom.setColorAt(0., QColor(0, 0, 0, 0));
	fadeBottom.setColorAt(1., QColor(0, 0, 0, 140));
	p.fillRect(QRect(0, height() - 56, width(), 56), fadeBottom);
}

int LyricsView::lyricsHeight() const {
	return height();
}

void LyricsView::layoutComments() {
	const auto shown = Settings::Bool(u"lyrics.comments"_q)
		&& _lyrics.found && _comments->validTrack() && _comments->avatarCount() > 0;
	_comments->setVisible(shown);
	if (!shown) return;
	const auto size = style::ConvertScale(32);
	const auto rowWidth = std::min(width(), style::ConvertScale(96));
	auto top = anchorY() - _scroll;
	if (_lyrics.synced && _current >= 0) {
		for (auto i = 0; i < _current; ++i) top += heightOf(i);
	}
	const auto lineHeight = _current >= 0 ? heightOf(_current) : size;
	_comments->setGeometry(std::max(0, width() - rowWidth - style::ConvertScale(8)),
		std::clamp(int(top + lineHeight / 2.) - size / 2, 0, std::max(0, height() - size)), rowWidth, size);
	_comments->raise();
}

void LyricsView::updateTimer() {
	const auto animate = isVisible()
		&& QGuiApplication::applicationState() == Qt::ApplicationActive
		&& _playing
		&& _lyrics.found
		&& _lyrics.synced
		&& !_lyrics.lines.empty()
		&& !PowerSaving::On(PowerSaving::kAnimations);
	if (animate && !_timer.isActive()) {
		_last = _clock.elapsed();
		_timer.start();
	} else if (!animate) {
		_timer.stop();
	}
}

void LyricsView::showEvent(QShowEvent *e) {
	RpWidget::showEvent(e);
	updateTimer();
}

void LyricsView::hideEvent(QHideEvent *e) {
	_timer.stop();
	RpWidget::hideEvent(e);
}

void LyricsView::resizeEvent(QResizeEvent *e) {
	layoutComments();
	_heights.clear();
	_layouts.clear();
	RpWidget::resizeEvent(e);
}

} // namespace details
namespace {

using details::LoadPlaybackArtwork;
using details::LyricsView;
using details::PlayingTrack;
using details::ResolveCommentsTrack;
using details::TimedComments;

constexpr auto kLyricsPanelWidth = 340;
constexpr auto kLyricsHeaderHeight = 56;

// Колонка справа от чата: название трека и синхронный текст.
[[nodiscard]] bool &ThirdColumnOpenedForLyrics();

class LyricsPanel final : public Ui::RpWidget {
public:
	LyricsPanel(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<Ui::RpWidget*> anchor)
	: RpWidget(parent.get())
	, _controller(controller)
	, _anchor(anchor)
	, _view(Ui::CreateChild<LyricsView>(this)) {
		Settings::PreloadRowIcons(&controller->session());
		_view->setCoverTint(Mokagram::Settings::Bool(u"lyrics.plasma"_q));
		_close = Mokagram::Plugins::MakeHeaderButton(this, true, [] {
			Mokagram::Settings::SetBool(u"lyrics.enabled"_q, false);
		});

		// Левый край тянется, как у третьей колонки: ширина общая с ней.
		_resize = Ui::CreateChild<Ui::ResizeArea>(this);
		_resize->addMoveLeftCallback([=](int globalLeft) {
			if (const auto parent = parentWidget()) {
				controller->columns().setThirdColumnWidth(
					parent->mapToGlobal(QPoint(parent->width(), 0)).x()
						- globalLeft);
			}
		});
		_resize->addMoveFinishedCallback([=] {
			controller->columns().setThirdColumnWidth(std::clamp(
				controller->columns().thirdColumnWidth(),
				st::columnMinimalWidthThird,
				st::columnMaximalWidthThird));
			Core::App().saveSettingsDelayed();
		});

		parent->events(
		) | rpl::filter([](not_null<QEvent*> e) {
			return e->type() == QEvent::Resize;
		}) | rpl::on_next([=] {
			updateGeometryFromAnchor();
		}, lifetime());
		anchor->events(
		) | rpl::filter([](not_null<QEvent*> e) {
			return e->type() == QEvent::Resize || e->type() == QEvent::Move;
		}) | rpl::on_next([=] {
			updateGeometryFromAnchor();
		}, lifetime());
		controller->adaptive().changes(
		) | rpl::on_next([=] {
			if (!controller->adaptive().isThreeColumn()) {
				ThirdColumnOpenedForLyrics() = false;
				Mokagram::Settings::SetBool(u"lyrics.enabled"_q, false);
				return;
			}
			updateGeometryFromAnchor();
		}, lifetime());
		controller->columns().thirdColumnWidthChanges(
		) | rpl::on_next([=](int) {
			updateGeometryFromAnchor();
		}, lifetime());
		anchor->shownValue(
		) | rpl::on_next([=](bool) {
			updateGeometryFromAnchor();
			refresh();
		}, lifetime());
		const auto weak = QPointer<LyricsPanel>(this);
		anchor->lifetime().add([=] {
			if (weak) {
				weak->deleteLater();
			}
		});

		Mokagram::Settings::Changed(
		) | rpl::on_next([=](const QString &key) {
			if (key == u"lyrics.plasma"_q) {
				_view->setCoverTint(Mokagram::Settings::Bool(key));
			} else if (key == u"lyrics.enabled"_q
				&& !Mokagram::Settings::Bool(key)) {
				deleteLater();
			}
		}, lifetime());

		const auto instance = Media::Player::instance();
		instance->updatedNotifier(
		) | rpl::on_next([=] {
			refresh();
		}, lifetime());
		QueueChanges(
		) | rpl::on_next([=] {
			refresh();
		}, lifetime());
		CoversChanged() | rpl::on_next([=] {
			_view->updateCover();
			_view->update();
		}, lifetime());

		// Нажатие на строку перематывает трек на её время.
		_view->lineClicked(
		) | rpl::on_next([=](qint64 ms) {
			const auto state = instance->getState(AudioMsgId::Type::Song);
			const auto lengthMs = state.length * 1000
				/ std::max<int64>(state.frequency, 1);
			if (lengthMs <= 0) {
				return;
			}
			instance->startSeeking(AudioMsgId::Type::Song);
			instance->finishSeeking(
				AudioMsgId::Type::Song,
				std::clamp(float64(ms) / float64(lengthMs), 0., 1.));
		}, lifetime());

		updateGeometryFromAnchor();
		refresh();
		raise();
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		p.fillRect(rect(), st::windowBg);
		p.fillRect(0, 0, st::lineWidth, height(), st::shadowFg);
		const auto left = 24;
		const auto available = width() - left - _close->width() - 16;
		p.setFont(st::semiboldFont);
		p.setPen(st::windowFg);
		p.drawText(
			QRect(left, 8, available, 22),
			Qt::AlignLeft | Qt::AlignVCenter,
			st::semiboldFont->elided(_title, available));
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			QRect(left, 30, available, 18),
			Qt::AlignLeft | Qt::AlignVCenter,
			st::normalFont->elided(_artist, available));
	}
	void resizeEvent(QResizeEvent *e) override {
		_view->setGeometry(
			0,
			kLyricsHeaderHeight,
			width(),
			std::max(height() - kLyricsHeaderHeight, 0));
		_close->move(
			width() - _close->width() - 8,
			(kLyricsHeaderHeight - _close->height()) / 2);
		_resize->setGeometry(0, 0, st::historyResizeWidth, height());
		_resize->raise();
		RpWidget::resizeEvent(e);
	}

private:
	// Встаём ровно на место третьей колонки: во всю высоту и её ширины, чат
	// при этом сужается штатно. Если колонку показать нельзя, висим под плеером.
	void updateGeometryFromAnchor() {
		const auto parent = parentWidget();
		if (!parent || !_anchor) {
			return;
		}
		const auto layout = _controller->computeColumnLayout();
		const auto docked = _controller->adaptive().isThreeColumn()
			&& layout.thirdWidth > 0;
		_resize->setVisible(docked);
		if (docked) {
			setGeometry(
				parent->width() - layout.thirdWidth,
				0,
				layout.thirdWidth,
				parent->height());
		} else {
			const auto top = _anchor->mapTo(
				parent,
				QPoint(0, _anchor->height())).y();
			const auto width = std::min(kLyricsPanelWidth, parent->width() / 2);
			setGeometry(
				parent->width() - width,
				top,
				width,
				std::max(parent->height() - top, 0));
		}
		raise();
	}
	void refresh() {
		const auto track = PlayingTrack();
		const auto shown = _anchor
			&& _anchor->isVisible()
			&& !track.id.isEmpty();
		setVisible(shown);
		if (!shown) {
			return;
		}
		if (_title != track.title || _artist != track.artist) {
			_title = track.title;
			_artist = track.artist;
			update();
		}
		const auto instance = Media::Player::instance();
		const auto state = instance->getState(AudioMsgId::Type::Song);
		_view->setPosition(
			state.position * 1000 / std::max<int64>(state.frequency, 1),
			state.state == Media::Player::State::Playing
				|| state.state == Media::Player::State::Starting
				|| state.state == Media::Player::State::Resuming);
		if (_lyricsFor == track.id) {
			return;
		}
		_lyricsFor = track.id;
		_view->setCommentsTrack(QString());
		ResolveCommentsTrack(track, crl::guard(this, [=, id = track.id](QString sc) {
			if (_lyricsFor == id) _view->setCommentsTrack(sc);
		}));
		_view->setLoading();
		const auto id = track.id;
		FetchLyrics(
			id,
			track.title,
			track.artist,
			track.durationMs,
			crl::guard(this, [=](Lyrics lyrics) {
				if (_lyricsFor == id) {
					_view->setLyrics(std::move(lyrics));
				}
			}));
	}

	const not_null<Window::SessionController*> _controller;
	const QPointer<Ui::RpWidget> _anchor;
	const not_null<LyricsView*> _view;
	Ui::AbstractButton *_close = nullptr;
	Ui::ResizeArea *_resize = nullptr;
	QString _lyricsFor;
	QString _title;
	QString _artist;

};

[[nodiscard]] QPointer<LyricsPanel> &LyricsPanelInstance() {
	static auto result = QPointer<LyricsPanel>();
	return result;
}

// Третью колонку открыли ради панели текста: при закрытии панели закроем и её.
[[nodiscard]] bool &ThirdColumnOpenedForLyrics() {
	static auto result = false;
	return result;
}

// Есть ли текст у играющего трека (для состояния кнопки в панели плеера).
struct LyricsAvailability {
	QString id;
	bool found = false;
	bool started = false;
	rpl::event_stream<> changes;
	rpl::lifetime lifetime;
};

[[nodiscard]] LyricsAvailability &Availability() {
	static auto result = new LyricsAvailability();
	return *result;
}

void RefreshAvailability() {
	auto &state = Availability();
	const auto track = PlayingTrack();
	if (state.id == track.id) {
		return;
	}
	state.id = track.id;
	state.found = false;
	state.changes.fire({});
	if (track.id.isEmpty()) {
		return;
	}
	const auto id = track.id;
	FetchLyrics(
		id,
		track.title,
		track.artist,
		track.durationMs,
		[=](Lyrics lyrics) {
			auto &state = Availability();
			if (state.id == id && state.found != lyrics.found) {
				state.found = lyrics.found;
				state.changes.fire({});
			}
		});
	LoadPlaybackArtwork(track);
}

} // namespace

void SyncLyricsPanel(
		not_null<Window::SessionController*> controller,
		not_null<Ui::RpWidget*> anchor) {
	auto &panel = LyricsPanelInstance();
	if (!Mokagram::Settings::Bool(u"lyrics.enabled"_q)) {
		if (panel) {
			panel->deleteLater();
			panel = nullptr;
		}
		if (base::take(ThirdColumnOpenedForLyrics())) {
			controller->closeThirdSection();
		}
		return;
	}
	if (!controller->adaptive().isThreeColumn()) {
		ThirdColumnOpenedForLyrics() = false;
		Mokagram::Settings::SetBool(u"lyrics.enabled"_q, false);
		return;
	}
	const auto parent = not_null<Ui::RpWidget*>(controller->content());
	if (panel && panel->parentWidget() != parent.get()) {
		panel->deleteLater();
		panel = nullptr;
	}
	if (!panel) {
		panel = Ui::CreateChild<LyricsPanel>(parent.get(), controller, anchor);
	}
}

void AttachCommentsToPlayer(
		not_null<Window::SessionController*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<Ui::RpWidget*> anchor,
		bool compactTimeline,
		Fn<bool()> available) {
	Settings::PreloadRowIcons(&controller->session());
	const auto button = Ui::CreateChild<TimedComments>(parent);
	const auto refresh = [=] {
		const auto track = PlayingTrack();
		const auto allowed = !available || available();
		const auto enabled = allowed && Settings::Bool(u"lyrics.comments"_q) && !LyricsAvailable();
		const auto state = Media::Player::instance()->getState(AudioMsgId::Type::Song);
		button->setTimelineDuration(std::max<qint64>(track.durationMs,
			state.length * 1000 / std::max<int64>(state.frequency, 1)));
		button->setTrack(enabled ? track.id : QString());
		const auto shown = enabled && button->validTrack() && button->avatarCount() > 0
			&& !anchor->isHidden();
		button->setVisible(shown);
		const auto size = style::ConvertScale(compactTimeline ? 24 : 28);
		const auto anchorTop = anchor->mapTo(parent, QPoint());
		button->setGeometry(anchorTop.x(),
			std::clamp(anchorTop.y() + (anchor->height() - size) / 2,
				0, std::max(0, parent->height() - size)), anchor->width(), size);
		button->setPosition(state.position * 1000 / std::max<int64>(state.frequency, 1));
		button->raise();
	};
	button->changes() | rpl::on_next(refresh, button->lifetime());
	button->seekRequests() | rpl::on_next([](qint64 ms) {
		const auto instance = Media::Player::instance();
		const auto state = instance->getState(AudioMsgId::Type::Song);
		const auto length = state.length * 1000 / std::max<int64>(state.frequency, 1);
		if (length <= 0) return;
		instance->startSeeking(AudioMsgId::Type::Song);
		instance->finishSeeking(AudioMsgId::Type::Song, std::clamp(double(ms) / length, 0., 1.));
	}, button->lifetime());
	Media::Player::instance()->updatedNotifier() | rpl::on_next(refresh, button->lifetime());
	QueueChanges() | rpl::on_next(refresh, button->lifetime());
	LyricsAvailabilityChanges() | rpl::on_next(refresh, button->lifetime());
	Settings::Changed() | rpl::on_next([=](const QString &key) {
		if (key == u"lyrics.comments"_q) refresh();
	}, button->lifetime());
	anchor->events() | rpl::on_next([=](not_null<QEvent*> event) {
		if (event->type() == QEvent::Move || event->type() == QEvent::Resize
			|| event->type() == QEvent::Show || event->type() == QEvent::Hide) refresh();
	}, button->lifetime());
	parent->events() | rpl::on_next([=](not_null<QEvent*> event) {
		if (event->type() == QEvent::Resize || event->type() == QEvent::Show) refresh();
	}, button->lifetime());
	crl::on_main(button, refresh);
}

bool LyricsAvailable() {
	return Availability().found;
}

rpl::producer<> LyricsAvailabilityChanges() {
	auto &state = Availability();
	if (!state.started) {
		state.started = true;
		Media::Player::instance()->updatedNotifier(
		) | rpl::on_next(RefreshAvailability, state.lifetime);
		QueueChanges(
		) | rpl::on_next(RefreshAvailability, state.lifetime);
		RefreshAvailability();
	}
	return state.changes.events();
}

void ToggleLyricsPanel(
		not_null<Window::SessionController*> controller,
		not_null<Ui::RpWidget*> anchor) {
	// Панель стоит на месте третьей колонки: если её нет, открываем (как кнопка
	// «Информация» в шапке чата) и при закрытии панели закроем обратно.
	if (!Mokagram::Settings::Bool(u"lyrics.enabled"_q)
		&& !controller->adaptive().isThreeColumn()
		&& controller->canShowThirdSection()) {
		Core::App().settings().setThirdSectionInfoEnabled(true);
		Core::App().saveSettingsDelayed();
		controller->resizeForThirdSection();
		controller->updateColumnLayout();
		ThirdColumnOpenedForLyrics() = true;
	}
	Mokagram::Settings::SetBool(
		u"lyrics.enabled"_q,
		!Mokagram::Settings::Bool(u"lyrics.enabled"_q));
	SyncLyricsPanel(controller, anchor);
}

} // namespace Mokagram::Music
