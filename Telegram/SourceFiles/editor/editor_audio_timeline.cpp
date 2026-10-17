/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_audio_timeline.h"

#include "editor/editor_audio_menu.h"
#include "editor/photo_editor_common.h"
#include "editor/video/video_segment_player.h"
#include "editor/video/video_timeline_seeker.h"
#include "media/media_audio_waveform.h"
#include "ui/widgets/popup_menu.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "styles/style_editor.h"

#include <QtCore/QFileInfo>
#include <QtGui/QContextMenuEvent>
#include <QtGui/QLinearGradient>

namespace Editor {
namespace {

constexpr auto kWaveformBarDuration = crl::time(20);
constexpr auto kMaxWaveformBars = 16384;
constexpr auto kLoadedDuration = crl::time(600);

[[nodiscard]] TrimTimelineDescriptor TrimDescriptor(const AudioTrack &track) {
	return {
		.duration = track.duration,
		.from = track.from,
		.till = track.till,
		.trimOnly = true,
	};
}

[[nodiscard]] QString TitleFor(const AudioTrack &track) {
	return !track.title.isEmpty()
		? track.title
		: !track.path.isEmpty()
		? QFileInfo(track.path).completeBaseName()
		: QString();
}

} // namespace

AudioTimeline::AudioTimeline(
	not_null<Ui::RpWidget*> parent,
	std::shared_ptr<AudioTrack> track)
: TrimTimeline(parent, TrimDescriptor(*track))
, _track(std::move(track))
, _title(TitleFor(*_track))
, _performer(_track->performer) {
	_track->from = from();
	_track->till = till();

	trimChanges(
	) | rpl::on_next([=] {
		_track->from = from();
		_track->till = till();
	}, lifetime());

	loadWaveform();
}

AudioTimeline::~AudioTimeline() {
	if (_waveformCancel) {
		_waveformCancel->store(true);
	}
}

const std::shared_ptr<AudioTrack> &AudioTimeline::track() const {
	return _track;
}

void AudioTimeline::setPlaying(bool playing) {
	if (playing == (_player != nullptr)) {
		return;
	} else if (!playing) {
		_seeker = nullptr;
		_player = nullptr;
		return;
	}
	_player = std::make_unique<SegmentPlayer>(
		_track->path,
		_track->content,
		SegmentPlayerOptions{ .audio = true, .volume = _track->volume });
	_seeker = std::make_unique<TimelineSeeker>(this, _player.get());
	_player->start();
}

void AudioTimeline::refreshTrim() {
	setTrim(_track->from, _track->till);
}

void AudioTimeline::refreshVolume() {
	if (_player) {
		_player->setVolume(_track->volume);
	}
}

rpl::producer<> AudioTimeline::removeRequests() const {
	return _removeRequests.events();
}

void AudioTimeline::contextMenuEvent(QContextMenuEvent *e) {
	_menu = CreateAudioMenu(
		this,
		_track,
		[=] { refreshVolume(); },
		[=] { _removeRequests.fire({}); });
	_menu->popup(e->globalPos());
	e->accept();
}

void AudioTimeline::loadWaveform() {
	const auto count = int(std::clamp(
		duration() / kWaveformBarDuration,
		crl::time(1),
		crl::time(kMaxWaveformBars)));
	const auto weak = base::make_weak(this);
	_waveformCancel = std::make_shared<std::atomic<bool>>(false);
	const auto cancel = _waveformCancel;
	crl::async([=, path = _track->path, content = _track->content] {
		auto result = std::make_shared<Media::Audio::Waveform>(
			Media::Audio::LoadWaveform(path, content, count, cancel));
		crl::on_main(weak, [=] {
			if (result->empty()) {
				return;
			}
			_waveform = result;
			_waveformMax = 0;
			for (const auto bar : result->bars) {
				accumulate_max(_waveformMax, int(bar));
			}
			_loadedAnimation.start(
				[=] { update(); },
				0.,
				1.,
				kLoadedDuration);
			update();
		});
	});
}

void AudioTimeline::paintStrip(QPainter &p, const QRect &strip) {
	p.fillRect(strip, st::videoTimelinePlaceholderBg);
	paintWaveform(p, strip);
	paintText(p, strip);
}

void AudioTimeline::paintWaveform(QPainter &p, const QRect &strip) {
	if (!_waveform || _waveform->bars.empty() || !_waveformMax) {
		return;
	}
	const auto loaded = _loadedAnimation.value(1.);
	if (loaded <= 0.) {
		return;
	}
	const auto &bars = _waveform->bars;
	const auto count = int(bars.size());
	const auto slots = std::max(
		strip.width() / st::photoEditorAudioTimelineBarSkip,
		1);
	const auto spacing = strip.width() / float64(slots);
	const auto from = float64(visibleFrom());
	const auto span = visibleSpan();
	const auto barDuration = float64(duration()) / count;
	const auto barWidth = float64(st::photoEditorAudioTimelineBarWidth);
	const auto minHeight = float64(st::photoEditorAudioTimelineBarMin);
	const auto ratio = st::photoEditorAudioTimelineBarRatio;
	auto color = st::videoTimelineFg->c;
	color.setAlphaF(st::photoEditorAudioTimelineBarOpacity * loaded);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	const auto bottom = float64(rect::bottom(strip));
	for (auto i = 0; i != slots; ++i) {
		const auto slotFrom = (from + i * span / slots) / barDuration;
		const auto slotTill = (from + (i + 1) * span / slots) / barDuration;
		const auto first = std::clamp(
			int(std::floor(slotFrom)),
			0,
			count - 1);
		const auto last = std::clamp(
			int(std::ceil(slotTill)) - 1,
			first,
			count - 1);
		auto peak = uint16(0);
		for (auto j = first; j <= last; ++j) {
			accumulate_max(peak, bars[j]);
		}
		const auto x = strip.x()
			+ i * spacing
			+ (spacing - barWidth) / 2.;
		const auto height = std::max(
			peak / float64(_waveformMax) * strip.height() * ratio,
			minHeight);
		p.drawRoundedRect(
			QRectF(x, bottom - height, barWidth, height),
			barWidth / 2.,
			barWidth / 2.);
	}
}

void AudioTimeline::paintText(QPainter &p, const QRect &strip) {
	const auto left = std::max(xAt(from()), strip.x());
	const auto right = std::min(xAt(till()), rect::right(strip));
	const auto skip = st::photoEditorAudioTimelineTextSkip;
	const auto available = right - left - 2 * skip;
	const auto icon = st::photoEditorAudioTimelineIcon;
	if (available <= icon) {
		return;
	}
	const auto &authorFont = st::photoEditorAudioTimelineAuthorFont;
	const auto &titleFont = st::photoEditorAudioTimelineFont;
	const auto dotSkip = (_performer.isEmpty() || _title.isEmpty())
		? 0
		: int(st::photoEditorAudioTimelineDotSkip);
	const auto full = icon
		+ st::photoEditorAudioTimelineIconSkip
		+ (_performer.isEmpty() ? 0 : authorFont->width(_performer))
		+ dotSkip
		+ (_title.isEmpty() ? 0 : titleFont->width(_title));
	const auto shown = std::min(full, available);
	const auto x = (left + right) / 2 - shown / 2;
	const auto centerY = rect::center(strip).y();
	if (full <= available) {
		paintTextLine(p, x, centerY, shown);
		return;
	}
	const auto ratio = style::DevicePixelRatio();
	auto image = QImage(
		QSize(shown, strip.height()) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio);
	image.fill(Qt::transparent);
	{
		auto q = QPainter(&image);
		auto hq = PainterHighQualityEnabler(q);
		paintTextLine(q, 0, strip.height() / 2, shown);
		const auto fade = float64(st::photoEditorAudioTimelineFade);
		auto gradient = QLinearGradient(shown - fade, 0, shown, 0);
		gradient.setColorAt(0., QColor(0, 0, 0, 255));
		gradient.setColorAt(1., QColor(0, 0, 0, 0));
		q.setCompositionMode(QPainter::CompositionMode_DestinationIn);
		q.fillRect(QRectF(shown - fade, 0, fade, strip.height()), gradient);
	}
	p.drawImage(x, strip.y(), image);
}

void AudioTimeline::paintTextLine(
		QPainter &p,
		int x,
		int centerY,
		int width) {
	const auto icon = st::photoEditorAudioTimelineIcon;
	const auto image = st::photoEditorAudioDiscIcon.instance(
		st::videoTimelineFg->c);
	p.setClipRect(x, centerY - height(), width, height() * 2);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	p.drawImage(
		QRectF(x, centerY - icon / 2., icon, icon),
		image);
	x += icon + st::photoEditorAudioTimelineIconSkip;
	p.setPen(st::videoTimelineFg);
	if (!_performer.isEmpty()) {
		const auto &font = st::photoEditorAudioTimelineAuthorFont;
		p.setFont(font);
		p.drawText(
			QPointF(x, centerY - font->height / 2. + font->ascent),
			_performer);
		x += font->width(_performer);
	}
	if (!_performer.isEmpty() && !_title.isEmpty()) {
		const auto dotSkip = st::photoEditorAudioTimelineDotSkip;
		const auto radius = float64(st::photoEditorAudioTimelineDotRadius);
		auto dot = st::videoTimelineFg->c;
		dot.setAlphaF(dot.alphaF() / 2.);
		p.setPen(Qt::NoPen);
		p.setBrush(dot);
		p.drawEllipse(QPointF(x + dotSkip / 2., centerY), radius, radius);
		p.setPen(st::videoTimelineFg);
		x += dotSkip;
	}
	if (!_title.isEmpty()) {
		const auto &font = st::photoEditorAudioTimelineFont;
		p.setFont(font);
		p.drawText(
			QPointF(x, centerY - font->height / 2. + font->ascent),
			_title);
	}
	p.setClipping(false);
}

} // namespace Editor
