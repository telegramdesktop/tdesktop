/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_voice_record_bar.h"

#include "api/api_send_progress.h"
#include "base/event_filter.h"
#include "base/openssl_help.h"
#include "base/unixtime.h"
#include "boxes/confirm_box.h"
#include "core/application.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_session.h"
#include "history/history_item_components.h"
#include "history/view/controls/history_view_voice_record_button.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mainwidget.h" // MainWidget::stopAndClosePlayer
#include "mainwindow.h"
#include "media/audio/media_audio.h"
#include "media/audio/media_audio_capture.h"
#include "media/player/media_player_button.h"
#include "media/player/media_player_instance.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "styles/style_media_player.h"
#include "ui/controls/send_button.h"
#include "ui/effects/animation_value.h"
#include "ui/effects/ripple_animation.h"
#include "ui/text/format_values.h"
#include "window/window_session_controller.h"

namespace HistoryView::Controls {

namespace {

using SendActionUpdate = VoiceRecordBar::SendActionUpdate;
using VoiceToSend = VoiceRecordBar::VoiceToSend;

constexpr auto kAudioVoiceUpdateView = crl::time(200);
constexpr auto kLockDelay = crl::time(100);
constexpr auto kRecordingUpdateDelta = crl::time(100);
constexpr auto kAudioVoiceMaxLength = 100 * 60; // 100 minutes
constexpr auto kMaxSamples =
	::Media::Player::kDefaultFrequency * kAudioVoiceMaxLength;

constexpr auto kInactiveWaveformBarAlpha = int(255 * 0.6);

constexpr auto kPrecision = 10;

constexpr auto kLockArcAngle = 15.;

constexpr auto kHideWaveformBgOffset = 50;

enum class FilterType {
	Continue,
	ShowBox,
	Cancel,
};

[[nodiscard]] auto InactiveColor(const QColor &c) {
	return QColor(c.red(), c.green(), c.blue(), kInactiveWaveformBarAlpha);
}

[[nodiscard]] auto Progress(int low, int high) {
	return std::clamp(float64(low) / high, 0., 1.);
}

[[nodiscard]] auto Duration(int samples) {
	return samples / ::Media::Player::kDefaultFrequency;
}

[[nodiscard]] auto FormatVoiceDuration(int samples) {
	const int duration = kPrecision
		* (float64(samples) / ::Media::Player::kDefaultFrequency);
	const auto durationString = Ui::FormatDurationText(duration / kPrecision);
	const auto decimalPart = duration % kPrecision;
	return QString("%1%2%3")
		.arg(durationString)
		.arg(QLocale::system().decimalPoint())
		.arg(decimalPart);
}

[[nodiscard]] std::unique_ptr<VoiceData> ProcessCaptureResult(
		const ::Media::Capture::Result &data) {
	auto voiceData = std::make_unique<VoiceData>();
	voiceData->duration = Duration(data.samples);
	voiceData->waveform = data.waveform;
	voiceData->wavemax = voiceData->waveform.empty()
		? uchar(0)
		: *ranges::max_element(voiceData->waveform);
	return voiceData;
}

[[nodiscard]] not_null<DocumentData*> DummyDocument(
		not_null<Data::Session*> owner) {
	return owner->document(
		openssl::RandomValue<DocumentId>(),
		uint64(0),
		QByteArray(),
		base::unixtime::now(),
		QVector<MTPDocumentAttribute>(),
		QString(),
		QByteArray(),
		ImageWithLocation(),
		ImageWithLocation(),
		owner->session().mainDcId(),
		int32(0));
}

void PaintWaveform(
		Painter &p,
		not_null<const VoiceData*> voiceData,
		int availableWidth,
		const QColor &active,
		const QColor &inactive,
		float64 progress) {
	const auto wf = [&]() -> const VoiceWaveform* {
		if (voiceData->waveform.isEmpty()) {
			return nullptr;
		} else if (voiceData->waveform.at(0) < 0) {
			return nullptr;
		}
		return &voiceData->waveform;
	}();

	const auto samplesCount = wf
		? wf->size()
		: ::Media::Player::kWaveformSamplesCount;
	const auto activeWidth = std::round(availableWidth * progress);

	const auto &barWidth = st::historyRecordWaveformBar;
	const auto barFullWidth = barWidth + st::msgWaveformSkip;
	const auto totalBarsCountF = (float)availableWidth / barFullWidth;
	const auto totalBarsCount = int(totalBarsCountF);
	const auto samplesPerBar = samplesCount / totalBarsCountF;
	const auto barNormValue = (wf ? voiceData->wavemax : 0) + 1;
	const auto maxDelta = st::msgWaveformMax - st::msgWaveformMin;
	const auto &bottom = st::msgWaveformMax;

	p.setPen(Qt::NoPen);
	int barNum = 0;
	const auto paintBar = [&](const auto &barValue) {
		const auto barHeight = st::msgWaveformMin + barValue;
		const auto barTop = (bottom - barHeight) / 2.;
		const auto barLeft = barNum * barFullWidth;
		const auto rect = [&](const auto &l, const auto &w) {
			return QRectF(l, barTop, w, barHeight);
		};

		if ((barLeft < activeWidth) && (barLeft + barWidth > activeWidth)) {
			const auto leftWidth = activeWidth - barLeft;
			const auto rightWidth = barWidth - leftWidth;
			p.fillRect(rect(barLeft, leftWidth), active);
			p.fillRect(rect(activeWidth, rightWidth), inactive);
		} else {
			const auto &color = (barLeft >= activeWidth) ? inactive : active;
			p.fillRect(rect(barLeft, barWidth), color);
		}
		barNum++;
	};

	auto barCounter = 0.;
	auto nextBarNum = 0;

	auto sum = 0;
	auto maxValue = 0;

	for (auto i = 0; i < samplesCount; i++) {
		const auto value = wf ? wf->at(i) : 0;
		if (i != nextBarNum) {
			maxValue = std::max(maxValue, value);
			sum += totalBarsCount;
			continue;
		}

		// Compute height.
		sum += totalBarsCount - samplesCount;
		const auto isSumSmaller = (sum < (totalBarsCount + 1) / 2);
		if (isSumSmaller) {
			maxValue = std::max(maxValue, value);
		}
		const auto barValue = ((maxValue * maxDelta) + (barNormValue / 2))
			/ barNormValue;
		maxValue = isSumSmaller ? 0 : value;

		const auto lastBarNum = nextBarNum;
		while (lastBarNum == nextBarNum) {
			barCounter += samplesPerBar;
			nextBarNum = (int)barCounter;
			paintBar(barValue);
		}
	}
}

} // namespace

class ListenWrap final {
public:
	ListenWrap(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller,
		::Media::Capture::Result &&data,
		const style::font &font);

	void requestPaintProgress(float64 progress);
	rpl::producer<> stopRequests() const;
	::Media::Capture::Result *data() const;

	void playPause();

	rpl::lifetime &lifetime();

private:
	void init();
	void initPlayButton();
	void initPlayProgress();

	bool isInPlayer(const ::Media::Player::TrackState &state) const;
	bool isInPlayer() const;

	int computeTopMargin(int height) const;
	QRect computeWaveformRect(const QRect &centerRect) const;

	not_null<Ui::RpWidget*> _parent;

	const not_null<Window::SessionController*> _controller;
	const not_null<DocumentData*> _document;
	const std::unique_ptr<VoiceData> _voiceData;
	const std::shared_ptr<Data::DocumentMedia> _mediaView;
	const std::unique_ptr<::Media::Capture::Result> _data;
	const style::IconButton &_stDelete;
	const base::unique_qptr<Ui::IconButton> _delete;
	const style::font &_durationFont;
	const QString _duration;
	const int _durationWidth;
	const style::MediaPlayerButton &_playPauseSt;
	const base::unique_qptr<Ui::AbstractButton> _playPauseButton;
	const QColor _activeWaveformBar;
	const QColor _inactiveWaveformBar;

	bool _isShowAnimation = true;

	QRect _waveformBgRect;
	QRect _waveformBgFinalCenterRect;
	QRect _waveformFgRect;

	::Media::Player::PlayButtonLayout _playPause;

	anim::value _playProgress;

	rpl::variable<float64> _showProgress = 0.;

	rpl::lifetime _lifetime;

};

ListenWrap::ListenWrap(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::SessionController*> controller,
	::Media::Capture::Result &&data,
	const style::font &font)
: _parent(parent)
, _controller(controller)
, _document(DummyDocument(&_controller->session().data()))
, _voiceData(ProcessCaptureResult(data))
, _mediaView(_document->createMediaView())
, _data(std::make_unique<::Media::Capture::Result>(std::move(data)))
, _stDelete(st::historyRecordDelete)
, _delete(base::make_unique_q<Ui::IconButton>(parent, _stDelete))
, _durationFont(font)
, _duration(Ui::FormatDurationText(
	float64(_data->samples) / ::Media::Player::kDefaultFrequency))
, _durationWidth(_durationFont->width(_duration))
, _playPauseSt(st::mediaPlayerButton)
, _playPauseButton(base::make_unique_q<Ui::AbstractButton>(parent))
, _activeWaveformBar(st::historyRecordVoiceFgActiveIcon->c)
, _inactiveWaveformBar(InactiveColor(_activeWaveformBar))
, _playPause(_playPauseSt, [=] { _playPauseButton->update(); }) {
	init();
}

void ListenWrap::init() {
	auto deleteShow = _showProgress.value(
	) | rpl::map([](auto value) {
		return value == 1.;
	}) | rpl::distinct_until_changed();
	_delete->showOn(std::move(deleteShow));

	_parent->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		_waveformBgRect = QRect({ 0, 0 }, size)
			.marginsRemoved(st::historyRecordWaveformBgMargins);
		{
			const auto m = _stDelete.width + _waveformBgRect.height() / 2;
			_waveformBgFinalCenterRect = _waveformBgRect.marginsRemoved(
				style::margins(m, 0, m, 0));
		}
		{
			const auto &play = _playPauseSt.playOuter;
			const auto &final = _waveformBgFinalCenterRect;
			_playPauseButton->moveToLeft(
				final.x() - (final.height() - play.width()) / 2,
				final.y());
		}
		_waveformFgRect = computeWaveformRect(_waveformBgFinalCenterRect);
	}, _lifetime);

	_parent->paintRequest(
	) | rpl::start_with_next([=](const QRect &clip) {
		Painter p(_parent);
		PainterHighQualityEnabler hq(p);
		const auto progress = _showProgress.current();
		p.setOpacity(progress);
		if (progress > 0. && progress < 1.) {
			_stDelete.icon.paint(p, _stDelete.iconPosition, _parent->width());
		}

		{
			const auto hideOffset = _isShowAnimation
				? 0
				: anim::interpolate(kHideWaveformBgOffset, 0, progress);
			const auto deleteIconLeft = _stDelete.iconPosition.x();
			const auto bgRectRight = anim::interpolate(
				deleteIconLeft,
				_stDelete.width,
				_isShowAnimation ? progress : 1.);
			const auto bgRectLeft = anim::interpolate(
				_parent->width() - deleteIconLeft - _waveformBgRect.height(),
				_stDelete.width,
				_isShowAnimation ? progress : 1.);
			const auto bgRectMargins = style::margins(
				bgRectLeft - hideOffset,
				0,
				bgRectRight + hideOffset,
				0);
			const auto bgRect = _waveformBgRect.marginsRemoved(bgRectMargins);

			const auto horizontalMargin = bgRect.width() - bgRect.height();
			const auto bgLeftCircleRect = bgRect.marginsRemoved(
				style::margins(0, 0, horizontalMargin, 0));
			const auto bgRightCircleRect = bgRect.marginsRemoved(
				style::margins(horizontalMargin, 0, 0, 0));

			const auto halfHeight = bgRect.height() / 2;
			const auto bgCenterRect = bgRect.marginsRemoved(
				style::margins(halfHeight, 0, halfHeight, 0));

			if (!_isShowAnimation) {
				p.setOpacity(progress);
			}
			p.setPen(Qt::NoPen);
			p.setBrush(st::historyRecordCancelActive);
			QPainterPath path;
			path.setFillRule(Qt::WindingFill);
			path.addEllipse(bgLeftCircleRect);
			path.addEllipse(bgRightCircleRect);
			path.addRect(bgCenterRect);
			p.drawPath(path);

			// Duration paint.
			{
				p.setFont(_durationFont);
				p.setPen(st::historyRecordVoiceFgActiveIcon);

				const auto top = computeTopMargin(_durationFont->ascent);
				const auto rect = bgCenterRect.marginsRemoved(
					style::margins(
						bgCenterRect.width() - _durationWidth,
						top,
						0,
						top));
				p.drawText(rect, style::al_left, _duration);
			}

			// Waveform paint.
			{
				const auto rect = (progress == 1.)
					? _waveformFgRect
					: computeWaveformRect(bgCenterRect);
				if (rect.width() > 0) {
					p.translate(rect.topLeft());
					PaintWaveform(
						p,
						_voiceData.get(),
						rect.width(),
						_activeWaveformBar,
						_inactiveWaveformBar,
						_playProgress.current());
					p.resetTransform();
				}
			}
		}
	}, _lifetime);

	initPlayButton();
	initPlayProgress();
}

void ListenWrap::initPlayButton() {
	using namespace ::Media::Player;
	using State = TrackState;

	_mediaView->setBytes(_data->bytes);
	_document->type = VoiceDocument;

	const auto &play = _playPauseSt.playOuter;
	const auto &width = _waveformBgFinalCenterRect.height();
	_playPauseButton->resize(width, width);
	_playPauseButton->show();

	_playPauseButton->paintRequest(
	) | rpl::start_with_next([=](const QRect &clip) {
		Painter p(_playPauseButton);

		const auto progress = _showProgress.current();
		p.translate(width / 2, width / 2);
		if (progress < 1.) {
			p.scale(progress, progress);
		}
		p.translate(-play.width() / 2, -play.height() / 2);
		_playPause.paint(p, st::historyRecordVoiceFgActiveIcon);
	}, _playPauseButton->lifetime());

	_playPauseButton->setClickedCallback([=] {
		instance()->playPause({ _document, FullMsgId() });
	});

	const auto showPause = _lifetime.make_state<rpl::variable<bool>>(false);
	showPause->changes(
	) | rpl::start_with_next([=](bool pause) {
		_playPause.setState(pause
			? PlayButtonLayout::State::Pause
			: PlayButtonLayout::State::Play);
	}, _lifetime);

	instance()->updatedNotifier(
	) | rpl::start_with_next([=](const State &state) {
		if (isInPlayer(state)) {
			*showPause = ShowPauseIcon(state.state);
		} else if (showPause->current()) {
			*showPause = false;
		}
	}, _lifetime);

	instance()->stops(
		AudioMsgId::Type::Voice
	) | rpl::start_with_next([=] {
		*showPause = false;
	}, _lifetime);

	const auto weak = Ui::MakeWeak(_controller->content().get());
	_lifetime.add([=] {
		if (weak && isInPlayer()) {
			weak->stopAndClosePlayer();
		}
	});
}

void ListenWrap::initPlayProgress() {
	using namespace ::Media::Player;
	using State = TrackState;

	const auto animation = _lifetime.make_state<Ui::Animations::Basic>();
	const auto isPointer = _lifetime.make_state<rpl::variable<bool>>(false);
	const auto &voice = AudioMsgId::Type::Voice;

	const auto updateCursor = [=](const QPoint &p) {
		*isPointer = isInPlayer() ? _waveformFgRect.contains(p) : false;
	};

	rpl::merge(
		instance()->startsPlay(voice) | rpl::map_to(true),
		instance()->stops(voice) | rpl::map_to(false)
	) | rpl::start_with_next([=](bool play) {
		_parent->setMouseTracking(isInPlayer() && play);
		updateCursor(_parent->mapFromGlobal(QCursor::pos()));
	}, _lifetime);

	instance()->updatedNotifier(
	) | rpl::start_with_next([=](const State &state) {
		if (!isInPlayer(state)) {
			return;
		}
		const auto progress = state.length
			? Progress(state.position, state.length)
			: 0.;
		if (IsStopped(state.state)) {
			_playProgress = anim::value();
		} else {
			_playProgress.start(progress);
		}
		animation->start();
	}, _lifetime);

	auto animationCallback = [=](crl::time now) {
		if (anim::Disabled()) {
			now += kAudioVoiceUpdateView;
		}

		const auto dt = (now - animation->started())
			/ float64(kAudioVoiceUpdateView);
		if (dt >= 1.) {
			animation->stop();
			_playProgress.finish();
		} else {
			_playProgress.update(std::min(dt, 1.), anim::linear);
		}
		_parent->update(_waveformFgRect);
		return (dt < 1.);
	};
	animation->init(std::move(animationCallback));

	const auto isPressed = _lifetime.make_state<bool>(false);

	isPointer->changes(
	) | rpl::start_with_next([=](bool pointer) {
		_parent->setCursor(pointer ? style::cur_pointer : style::cur_default);
	}, _lifetime);

	_parent->events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return (e->type() == QEvent::MouseMove
			|| e->type() == QEvent::MouseButtonPress
			|| e->type() == QEvent::MouseButtonRelease);
	}) | rpl::start_with_next([=](not_null<QEvent*> e) {
		if (!isInPlayer()) {
			return;
		}

		const auto type = e->type();
		const auto isMove = (type == QEvent::MouseMove);
		const auto &pos = static_cast<QMouseEvent*>(e.get())->pos();
		if (*isPressed) {
			*isPointer = true;
		} else if (isMove) {
			updateCursor(pos);
		}
		if (type == QEvent::MouseButtonPress) {
			if (isPointer->current() && !(*isPressed)) {
				instance()->startSeeking(voice);
				*isPressed = true;
			}
		} else if (*isPressed) {
			const auto &rect = _waveformFgRect;
			const auto left = float64(pos.x() - rect.x());
			const auto progress = Progress(left, rect.width());
			const auto isRelease = (type == QEvent::MouseButtonRelease);
			if (isRelease || isMove) {
				_playProgress = anim::value(progress, progress);
				_parent->update(_waveformFgRect);
				if (isRelease) {
					instance()->finishSeeking(voice, progress);
					*isPressed = false;
				}
			}
		}

	}, _lifetime);
}


bool ListenWrap::isInPlayer(const ::Media::Player::TrackState &state) const {
	return (state.id && (state.id.audio() == _document));
}

bool ListenWrap::isInPlayer() const {
	using Type = AudioMsgId::Type;
	return isInPlayer(::Media::Player::instance()->getState(Type::Voice));
}

void ListenWrap::playPause() {
	_playPauseButton->clicked(Qt::NoModifier, Qt::LeftButton);
}

QRect ListenWrap::computeWaveformRect(const QRect &centerRect) const {
	const auto top = computeTopMargin(st::msgWaveformMax);
	const auto left = (_playPauseSt.playOuter.width() + centerRect.height())
		/ 2;
	const auto right = st::historyRecordWaveformRightSkip + _durationWidth;
	return centerRect.marginsRemoved(style::margins(left, top, right, top));
}

int ListenWrap::computeTopMargin(int height) const {
	return (_waveformBgRect.height() - height) / 2;
}

void ListenWrap::requestPaintProgress(float64 progress) {
	_isShowAnimation = (_showProgress.current() < progress);
	_showProgress = progress;
}

rpl::producer<> ListenWrap::stopRequests() const {
	return _delete->clicks() | rpl::to_empty;
}

::Media::Capture::Result *ListenWrap::data() const {
	return _data.get();
}

rpl::lifetime &ListenWrap::lifetime() {
	return _lifetime;
}

class RecordLock final : public Ui::RippleButton {
public:
	RecordLock(not_null<Ui::RpWidget*> parent);

	void requestPaintProgress(float64 progress);
	void requestPaintLockToStopProgress(float64 progress);

	[[nodiscard]] rpl::producer<> locks() const;
	[[nodiscard]] bool isLocked() const;
	[[nodiscard]] bool isStopState() const;

	[[nodiscard]] float64 lockToStopProgress() const;

protected:
	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	void init();

	void drawProgress(Painter &p);
	void setProgress(float64 progress);
	void startLockingAnimation(float64 to);

	const QRect _rippleRect;
	const QPen _arcPen;

	Ui::Animations::Simple _lockEnderAnimation;

	float64 _lockToStopProgress = 0.;
	rpl::variable<float64> _progress = 0.;
};

RecordLock::RecordLock(not_null<Ui::RpWidget*> parent)
: RippleButton(parent, st::defaultRippleAnimation)
, _rippleRect(QRect(
	0,
	0,
	st::historyRecordLockTopShadow.width(),
	st::historyRecordLockTopShadow.width())
		.marginsRemoved(st::historyRecordLockRippleMargin))
, _arcPen(
	st::historyRecordLockIconFg,
	st::historyRecordLockIconLineWidth,
	Qt::SolidLine,
	Qt::SquareCap,
	Qt::RoundJoin) {
	init();
}

void RecordLock::init() {
	shownValue(
	) | rpl::start_with_next([=](bool shown) {
		resize(
			st::historyRecordLockTopShadow.width(),
			st::historyRecordLockSize.height());
		if (!shown) {
			setCursor(style::cur_default);
			setAttribute(Qt::WA_TransparentForMouseEvents, true);
			_lockEnderAnimation.stop();
			_lockToStopProgress = 0.;
			_progress = 0.;
		}
	}, lifetime());

	paintRequest(
	) | rpl::start_with_next([=](const QRect &clip) {
		Painter p(this);
		if (isLocked()) {
			const auto top = anim::interpolate(
				0,
				height() - st::historyRecordLockTopShadow.height() * 2,
				_lockToStopProgress);
			p.translate(0, top);
			drawProgress(p);
			return;
		}
		drawProgress(p);
	}, lifetime());
}

void RecordLock::drawProgress(Painter &p) {
	const auto progress = _progress.current();

	const auto &originTop = st::historyRecordLockTop;
	const auto &originBottom = st::historyRecordLockBottom;
	const auto &originBody = st::historyRecordLockBody;
	const auto &shadowTop = st::historyRecordLockTopShadow;
	const auto &shadowBottom = st::historyRecordLockBottomShadow;
	const auto &shadowBody = st::historyRecordLockBodyShadow;
	const auto &shadowMargins = st::historyRecordLockMargin;

	const auto bottomMargin = anim::interpolate(
		0,
		rect().height() - shadowTop.height() - shadowBottom.height(),
		progress);

	const auto topMargin = anim::interpolate(
		rect().height() / 4,
		0,
		progress);

	const auto full = rect().marginsRemoved(
		style::margins(0, topMargin, 0, bottomMargin));
	const auto inner = full.marginsRemoved(shadowMargins);
	const auto content = inner.marginsRemoved(style::margins(
		0,
		originTop.height(),
		0,
		originBottom.height()));
	const auto contentShadow = full.marginsRemoved(style::margins(
		0,
		shadowTop.height(),
		0,
		shadowBottom.height()));

	const auto w = full.width();
	{
		shadowTop.paint(p, full.topLeft(), w);
		originTop.paint(p, inner.topLeft(), w);
	}
	{
		const auto shadowPos = QPoint(
			full.x(),
			contentShadow.y() + contentShadow.height());
		const auto originPos = QPoint(
			inner.x(),
			content.y() + content.height());
		shadowBottom.paint(p, shadowPos, w);
		originBottom.paint(p, originPos, w);
	}
	{
		shadowBody.fill(p, contentShadow);
		originBody.fill(p, content);
	}
	{
		const auto &arrow = st::historyRecordLockArrow;
		const auto arrowRect = QRect(
			inner.x(),
			content.y() + content.height() - arrow.height() / 2,
			inner.width(),
			arrow.height());
		p.setOpacity(1. - progress);
		arrow.paintInCenter(p, arrowRect);
		p.setOpacity(1.);
	}
	if (isLocked()) {
		paintRipple(p, _rippleRect.x(), _rippleRect.y());
	}
	{
		PainterHighQualityEnabler hq(p);
		const auto &arcOffset = st::historyRecordLockIconLineSkip;
		const auto &size = st::historyRecordLockIconSize;

		const auto arcWidth = size.width() - arcOffset * 2;
		const auto &arcHeight = st::historyRecordLockIconArcHeight;

		const auto &blockHeight = st::historyRecordLockIconBottomHeight;

		const auto blockRectWidth = anim::interpolateF(
			size.width(),
			st::historyRecordStopIconWidth,
			_lockToStopProgress);
		const auto blockRectHeight = anim::interpolateF(
			blockHeight,
			st::historyRecordStopIconWidth,
			_lockToStopProgress);
		const auto blockRectTop = anim::interpolateF(
			size.height() - blockHeight,
			std::round((size.height() - blockRectHeight) / 2.),
			_lockToStopProgress);

		const auto blockRect = QRectF(
			(size.width() - blockRectWidth) / 2,
			blockRectTop,
			blockRectWidth,
			blockRectHeight);
		const auto &lineHeight = st::historyRecordLockIconLineHeight;

		p.setPen(Qt::NoPen);
		p.setBrush(st::historyRecordLockIconFg);
		p.translate(
			inner.x() + (inner.width() - size.width()) / 2,
			inner.y() + (originTop.height() * 2 - size.height()) / 2);
		{
			const auto xRadius = anim::interpolate(2, 3, _lockToStopProgress);
			p.drawRoundedRect(blockRect, xRadius, 3);
		}

		const auto offsetTranslate = _lockToStopProgress *
			(lineHeight + arcHeight + _arcPen.width() * 2);
		p.translate(
			size.width() - arcOffset,
			blockRect.y() + offsetTranslate);

		if (progress < 1. && progress > 0.) {
			p.rotate(kLockArcAngle * progress);
		}

		p.setPen(_arcPen);
		const auto rLine = QLineF(0, 0, 0, -lineHeight);
		p.drawLine(rLine);

		p.drawArc(
			-arcWidth,
			rLine.dy() - arcHeight - _arcPen.width() + rLine.y1(),
			arcWidth,
			arcHeight * 2,
			0,
			180 * 16);

		const auto lockProgress = 1. - _lockToStopProgress;
		if (progress == 1. && lockProgress < 1.) {
			p.drawLine(
				-arcWidth,
				rLine.y2(),
				-arcWidth,
				rLine.dy() * lockProgress);
		}
	}
}

void RecordLock::startLockingAnimation(float64 to) {
	auto callback = [=](float64 value) { setProgress(value); };
	const auto &duration = st::historyRecordVoiceShowDuration;
	_lockEnderAnimation.start(std::move(callback), 0., to, duration);
}

void RecordLock::requestPaintProgress(float64 progress) {
	if (isHidden()
		|| isLocked()
		|| _lockEnderAnimation.animating()
		|| (_progress.current() == progress)) {
		return;
	}
	if (!_progress.current() && (progress > .3)) {
		startLockingAnimation(progress);
		return;
	}
	setProgress(progress);
}

void RecordLock::requestPaintLockToStopProgress(float64 progress) {
	_lockToStopProgress = progress;
	if (isStopState()) {
		setCursor(style::cur_pointer);
		setAttribute(Qt::WA_TransparentForMouseEvents, false);

		resize(
			st::historyRecordLockTopShadow.width(),
			st::historyRecordLockTopShadow.width());
	}
	update();
}

float64 RecordLock::lockToStopProgress() const {
	return _lockToStopProgress;
}

void RecordLock::setProgress(float64 progress) {
	_progress = progress;
	update();
}

bool RecordLock::isLocked() const {
	return _progress.current() == 1.;
}

bool RecordLock::isStopState() const {
	return isLocked() && (_lockToStopProgress == 1.);
}

rpl::producer<> RecordLock::locks() const {
	return _progress.changes(
	) | rpl::filter([=] { return isLocked(); }) | rpl::to_empty;
}

QImage RecordLock::prepareRippleMask() const {
	return Ui::RippleAnimation::ellipseMask(_rippleRect.size());
}

QPoint RecordLock::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos()) - _rippleRect.topLeft();
}

class CancelButton final : public Ui::RippleButton {
public:
	CancelButton(not_null<Ui::RpWidget*> parent, int height);

	void requestPaintProgress(float64 progress);

protected:
	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	void init();

	const int _width;
	const QRect _rippleRect;

	rpl::variable<float64> _showProgress = 0.;

	Ui::Text::String _text;

};

CancelButton::CancelButton(not_null<Ui::RpWidget*> parent, int height)
: Ui::RippleButton(parent, st::defaultLightButton.ripple)
, _width(st::historyRecordCancelButtonWidth)
, _rippleRect(QRect(0, (height - _width) / 2, _width, _width))
, _text(st::semiboldTextStyle, tr::lng_selected_clear(tr::now).toUpper()) {
	resize(_width, height);
	init();
}

void CancelButton::init() {
	_showProgress.value(
	) | rpl::map(rpl::mappers::_1 > 0.) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](bool hasProgress) {
		setVisible(hasProgress);
	}, lifetime());

	paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(this);

		p.setOpacity(_showProgress.current());

		paintRipple(p, _rippleRect.x(), _rippleRect.y());

		p.setPen(st::historyRecordCancelButtonFg);
		_text.draw(
			p,
			0,
			(height() - _text.minHeight()) / 2,
			width(),
			style::al_center);
	}, lifetime());
}

QImage CancelButton::prepareRippleMask() const {
	return Ui::RippleAnimation::ellipseMask(_rippleRect.size());
}

QPoint CancelButton::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos()) - _rippleRect.topLeft();
}

void CancelButton::requestPaintProgress(float64 progress) {
	_showProgress = progress;
	update();
}

VoiceRecordBar::VoiceRecordBar(
	not_null<Ui::RpWidget*> parent,
	not_null<Ui::RpWidget*> sectionWidget,
	not_null<Window::SessionController*> controller,
	std::shared_ptr<Ui::SendButton> send,
	int recorderHeight)
: RpWidget(parent)
, _sectionWidget(sectionWidget)
, _controller(controller)
, _send(send)
, _lock(std::make_unique<RecordLock>(sectionWidget))
, _level(std::make_unique<VoiceRecordButton>(
	sectionWidget,
	_controller->widget()->leaveEvents()))
, _cancel(std::make_unique<CancelButton>(this, recorderHeight))
, _startTimer([=] { startRecording(); })
, _message(
	st::historyRecordTextStyle,
	tr::lng_record_cancel(tr::now),
	TextParseOptions{ TextParseMultiline, 0, 0, Qt::LayoutDirectionAuto })
, _cancelFont(st::historyRecordFont) {
	resize(QSize(parent->width(), recorderHeight));
	init();
	hideFast();
}

VoiceRecordBar::VoiceRecordBar(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::SessionController*> controller,
	std::shared_ptr<Ui::SendButton> send,
	int recorderHeight)
: VoiceRecordBar(parent, parent, controller, send, recorderHeight) {
}

VoiceRecordBar::~VoiceRecordBar() {
	if (isRecording()) {
		stopRecording(StopType::Cancel);
	}
}

void VoiceRecordBar::updateMessageGeometry() {
	const auto left = _durationRect.x()
		+ _durationRect.width()
		+ st::historyRecordTextLeft;
	const auto right = width()
		- _send->width()
		- st::historyRecordTextRight;
	const auto textWidth = _message.maxWidth();
	const auto width = ((right - left) < textWidth)
		? st::historyRecordTextWidthForWrap
		: textWidth;
	const auto countLines = std::ceil((float)textWidth / width);
	const auto textHeight = _message.minHeight() * countLines;
	_messageRect = QRect(
		left + (right - left - width) / 2,
		(height() - textHeight) / 2,
		width,
		textHeight);
}

void VoiceRecordBar::updateLockGeometry() {
	const auto right = anim::interpolate(
		-_lock->width(),
		st::historyRecordLockPosition.x(),
		_showLockAnimation.value(_lockShowing.current() ? 1. : 0.));
	_lock->moveToRight(right, _lock->y());
}

void VoiceRecordBar::init() {
	// Keep VoiceRecordBar behind SendButton.
	rpl::single(
	) | rpl::then(
		_send->events(
		) | rpl::filter([](not_null<QEvent*> e) {
			return e->type() == QEvent::ZOrderChange;
		}) | rpl::to_empty
	) | rpl::start_with_next([=] {
		orderControls();
	}, lifetime());

	shownValue(
	) | rpl::start_with_next([=](bool show) {
		if (!show) {
			finish();
		}
	}, lifetime());

	sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		_centerY = size.height() / 2;
		{
			const auto maxD = st::historyRecordSignalRadius * 2;
			const auto point = _centerY - st::historyRecordSignalRadius;
			_redCircleRect = { point, point, maxD, maxD };
		}
		{
			const auto durationLeft = _redCircleRect.x()
				+ _redCircleRect.width()
				+ st::historyRecordDurationSkip;
			const auto &ascent = _cancelFont->ascent;
			_durationRect = QRect(
				durationLeft,
				_redCircleRect.y() - (ascent - _redCircleRect.height()) / 2,
				_cancelFont->width(FormatVoiceDuration(kMaxSamples)),
				ascent);
		}
		_cancel->moveToLeft((size.width() - _cancel->width()) / 2, 0);
		updateMessageGeometry();
		updateLockGeometry();
	}, lifetime());

	paintRequest(
	) | rpl::start_with_next([=](const QRect &clip) {
		Painter p(this);
		if (_showAnimation.animating()) {
			p.setOpacity(showAnimationRatio());
		}
		p.fillRect(clip, st::historyComposeAreaBg);

		p.setOpacity(std::min(p.opacity(), 1. - showListenAnimationRatio()));
		const auto opacity = p.opacity();
		_cancel->requestPaintProgress(_lock->isStopState()
			? (opacity * _lock->lockToStopProgress())
			: 0.);

		if (!opacity) {
			return;
		}
		if (clip.intersects(_messageRect)) {
			// The message should be painted first to avoid flickering.
			drawMessage(p, activeAnimationRatio());
		}
		if (clip.intersects(_durationRect)) {
			drawDuration(p);
		}
		if (clip.intersects(_redCircleRect)) {
			// Should be the last to be drawn.
			drawRedCircle(p);
		}
	}, lifetime());

	_inField.changes(
	) | rpl::start_with_next([=](bool value) {
		activeAnimate(value);
	}, lifetime());

	_lockShowing.changes(
	) | rpl::start_with_next([=](bool show) {
		const auto to = show ? 1. : 0.;
		const auto from = show ? 0. : 1.;
		const auto &duration = st::historyRecordLockShowDuration;
		_lock->show();
		auto callback = [=](float64 value) {
			updateLockGeometry();
			if (value == 0. && !show) {
				_lock->hide();
			} else if (value == 1. && show) {
				computeAndSetLockProgress(QCursor::pos());
			}
		};
		_showLockAnimation.start(std::move(callback), from, to, duration);
	}, lifetime());

	_lock->setClickedCallback([=] {
		if (!_lock->isStopState()) {
			return;
		}

		::Media::Capture::instance()->startedChanges(
		) | rpl::filter([=](bool capturing) {
			return !capturing && _listen;
		}) | rpl::take(1) | rpl::start_with_next([=] {
			_lockShowing = false;

			const auto to = 1.;
			const auto &duration = st::historyRecordVoiceShowDuration;
			auto callback = [=](float64 value) {
				_listen->requestPaintProgress(value);
				const auto reverseValue = to - value;
				_level->requestPaintProgress(reverseValue);
				update();
				if (to == value) {
					_recordingLifetime.destroy();
				}
			};
			_showListenAnimation.start(std::move(callback), 0., to, duration);
		}, lifetime());

		stopRecording(StopType::Listen);
	});

	_lock->locks(
	) | rpl::start_with_next([=] {
		_level->setType(VoiceRecordButton::Type::Send);

		_level->clicks(
		) | rpl::start_with_next([=] {
			stop(true);
		}, _recordingLifetime);

		rpl::single(
			false
		) | rpl::then(
			_level->actives()
		) | rpl::start_with_next([=](bool enter) {
			_inField = enter;
		}, _recordingLifetime);

		const auto &duration = st::historyRecordVoiceShowDuration;
		const auto from = 0.;
		const auto to = 1.;
		auto callback = [=](float64 value) {
			_lock->requestPaintLockToStopProgress(value);
			update();
		};
		_lockToStopAnimation.start(std::move(callback), from, to, duration);
	}, lifetime());

	_send->events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return isTypeRecord()
			&& !isRecording()
			&& !_showAnimation.animating()
			&& !_lock->isLocked()
			&& (e->type() == QEvent::MouseButtonPress
				|| e->type() == QEvent::MouseButtonRelease);
	}) | rpl::start_with_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::MouseButtonPress) {
			if (_startRecordingFilter && _startRecordingFilter()) {
				return;
			}
			_startTimer.callOnce(st::historyRecordVoiceShowDuration);
		} else if (e->type() == QEvent::MouseButtonRelease) {
			_startTimer.cancel();
		}
	}, lifetime());

	_listenChanges.events(
	) | rpl::filter([=] {
		return _listen != nullptr;
	}) | rpl::start_with_next([=] {
		_listen->stopRequests(
		) | rpl::take(1) | rpl::start_with_next([=] {
			hideAnimated();
		}, _listen->lifetime());

		_listen->lifetime().add([=] { _listenChanges.fire({}); });

		installListenStateFilter();
	}, lifetime());

	_cancel->setClickedCallback([=] {
		hideAnimated();
	});
}

void VoiceRecordBar::activeAnimate(bool active) {
	const auto to = active ? 1. : 0.;
	const auto &duration = st::historyRecordVoiceDuration;
	if (_activeAnimation.animating()) {
		_activeAnimation.change(to, duration);
	} else {
		auto callback = [=] {
			update(_messageRect);
			_level->requestPaintColor(activeAnimationRatio());
		};
		const auto from = active ? 0. : 1.;
		_activeAnimation.start(std::move(callback), from, to, duration);
	}
}

void VoiceRecordBar::visibilityAnimate(bool show, Fn<void()> &&callback) {
	const auto to = show ? 1. : 0.;
	const auto from = show ? 0. : 1.;
	const auto &duration = st::historyRecordVoiceShowDuration;
	auto animationCallback = [=, callback = std::move(callback)](auto value) {
		if (!_listen) {
			_level->requestPaintProgress(value);
		} else {
			_listen->requestPaintProgress(value);
		}
		update();
		if ((show && value == 1.) || (!show && value == 0.)) {
			if (callback) {
				callback();
			}
		}
	};
	_showAnimation.start(std::move(animationCallback), from, to, duration);
}

void VoiceRecordBar::setStartRecordingFilter(Fn<bool()> &&callback) {
	_startRecordingFilter = std::move(callback);
}

void VoiceRecordBar::setLockBottom(rpl::producer<int> &&bottom) {
	rpl::combine(
		std::move(bottom),
		_lock->sizeValue() | rpl::map_to(true) // Dummy value.
	) | rpl::start_with_next([=](int value, bool dummy) {
		_lock->moveToLeft(_lock->x(), value - _lock->height());
	}, lifetime());
}

void VoiceRecordBar::setSendButtonGeometryValue(
		rpl::producer<QRect> &&geometry) {
	std::move(
		geometry
	) | rpl::start_with_next([=](QRect r) {
		const auto center = (r.width() - _level->width()) / 2;
		_level->moveToLeft(r.x() + center, r.y() + center);
	}, lifetime());
}

void VoiceRecordBar::startRecording() {
	if (isRecording()) {
		return;
	}
	auto appearanceCallback = [=] {
		if(_showAnimation.animating()) {
			return;
		}

		using namespace ::Media::Capture;
		if (!instance()->available()) {
			stop(false);
			return;
		}

		_lockShowing = true;
		startRedCircleAnimation();

		_recording = true;
		_controller->widget()->setInnerFocus();
		instance()->start();
		instance()->updated(
		) | rpl::start_with_next_error([=](const Update &update) {
			recordUpdated(update.level, update.samples);
		}, [=] {
			stop(false);
		}, _recordingLifetime);
		_recordingLifetime.add([=] {
			_recording = false;
		});
	};
	visibilityAnimate(true, std::move(appearanceCallback));
	show();

	_inField = true;

	_send->events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return isTypeRecord()
			&& !_lock->isLocked()
			&& (e->type() == QEvent::MouseMove
				|| e->type() == QEvent::MouseButtonRelease);
	}) | rpl::start_with_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::MouseMove) {
			const auto mouse = static_cast<QMouseEvent*>(e.get());
			const auto globalPos = mouse->globalPos();
			const auto localPos = mapFromGlobal(globalPos);
			const auto inField = rect().contains(localPos);
			_inField = inField
				? inField
				: _level->inCircle(_level->mapFromGlobal(globalPos));

			if (_showLockAnimation.animating() || !hasDuration()) {
				return;
			}
			computeAndSetLockProgress(mouse->globalPos());
		} else if (type == QEvent::MouseButtonRelease) {
			stop(_inField.current());
		}
	}, _recordingLifetime);
}

void VoiceRecordBar::recordUpdated(quint16 level, int samples) {
	_level->requestPaintLevel(level);
	_recordingSamples = samples;
	if (samples < 0 || samples >= kMaxSamples) {
		stop(samples > 0 && _inField.current());
	}
	Core::App().updateNonIdle();
	update(_durationRect);
	_sendActionUpdates.fire({ Api::SendProgressType::RecordVoice });
}

void VoiceRecordBar::stop(bool send) {
	if (isHidden() && !send) {
		return;
	}
	auto disappearanceCallback = [=] {
		hide();

		stopRecording(send ? StopType::Send : StopType::Cancel);
	};
	_lockShowing = false;
	visibilityAnimate(false, std::move(disappearanceCallback));
}

void VoiceRecordBar::finish() {
	_recordingLifetime.destroy();
	_lockShowing = false;
	_inField = false;
	_redCircleProgress = 0.;
	_recordingSamples = 0;

	_showAnimation.stop();
	_lockToStopAnimation.stop();

	_listen = nullptr;

	_sendActionUpdates.fire({ Api::SendProgressType::RecordVoice, -1 });
	_controller->widget()->setInnerFocus();
}

void VoiceRecordBar::hideFast() {
	hide();
	_lock->hide();
	_level->hide();
	stopRecording(StopType::Cancel);
}

void VoiceRecordBar::stopRecording(StopType type) {
	using namespace ::Media::Capture;
	if (type == StopType::Cancel) {
		instance()->stop(crl::guard(this, [=](Result &&data) {
			_cancelRequests.fire({});
		}));
		return;
	}
	instance()->stop(crl::guard(this, [=](Result &&data) {
		if (data.bytes.isEmpty()) {
			// Close everything.
			stop(false);
			return;
		}

		Window::ActivateWindow(_controller);
		const auto duration = Duration(data.samples);
		if (type == StopType::Send) {
			_sendVoiceRequests.fire({ data.bytes, data.waveform, duration });
		} else if (type == StopType::Listen) {
			_listen = std::make_unique<ListenWrap>(
				this,
				_controller,
				std::move(data),
				_cancelFont);
			_listenChanges.fire({});

			_lockShowing = false;
		}
	}));
}

void VoiceRecordBar::drawDuration(Painter &p) {
	const auto duration = FormatVoiceDuration(_recordingSamples);
	p.setFont(_cancelFont);
	p.setPen(st::historyRecordDurationFg);

	p.drawText(_durationRect, style::al_left, duration);
}

void VoiceRecordBar::startRedCircleAnimation() {
	if (anim::Disabled()) {
		return;
	}
	const auto animation = _recordingLifetime
		.make_state<Ui::Animations::Basic>();
	animation->init([=](crl::time now) {
		const auto diffTime = now - animation->started();
		_redCircleProgress = std::abs(std::sin(diffTime / 400.));
		update(_redCircleRect);
		return true;
	});
	animation->start();
}

void VoiceRecordBar::drawRedCircle(Painter &p) {
	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::historyRecordVoiceFgInactive);

	const auto opacity = p.opacity();
	p.setOpacity(opacity * (1. - _redCircleProgress));
	const int radii = st::historyRecordSignalRadius * showAnimationRatio();
	const auto center = _redCircleRect.center() + QPoint(1, 1);
	p.drawEllipse(center, radii, radii);
	p.setOpacity(opacity);
}

void VoiceRecordBar::drawMessage(Painter &p, float64 recordActive) {
	p.setPen(
		anim::pen(
			st::historyRecordCancel,
			st::historyRecordCancelActive,
			1. - recordActive));

	const auto opacity = p.opacity();
	p.setOpacity(opacity * (1. - _lock->lockToStopProgress()));

	_message.draw(
		p,
		_messageRect.x(),
		_messageRect.y(),
		_messageRect.width(),
		style::al_center);

	p.setOpacity(opacity);
}

void VoiceRecordBar::requestToSendWithOptions(Api::SendOptions options) {
	if (isListenState()) {
		const auto data = _listen->data();
		_sendVoiceRequests.fire({
			data->bytes,
			data->waveform,
			Duration(data->samples),
			options });
	}
}

rpl::producer<SendActionUpdate> VoiceRecordBar::sendActionUpdates() const {
	return _sendActionUpdates.events();
}

rpl::producer<VoiceToSend> VoiceRecordBar::sendVoiceRequests() const {
	return _sendVoiceRequests.events();
}

rpl::producer<> VoiceRecordBar::cancelRequests() const {
	return _cancelRequests.events();
}

bool VoiceRecordBar::isRecording() const {
	return _recording.current();
}

bool VoiceRecordBar::isActive() const {
	return isRecording() || isListenState();
}

void VoiceRecordBar::hideAnimated() {
	if (isHidden()) {
		return;
	}
	_lockShowing = false;
	visibilityAnimate(false, [=] { hideFast(); });
}

void VoiceRecordBar::finishAnimating() {
	_showAnimation.stop();
}

rpl::producer<bool> VoiceRecordBar::recordingStateChanges() const {
	return _recording.changes();
}

rpl::producer<bool> VoiceRecordBar::lockShowStarts() const {
	return _lockShowing.changes();
}

rpl::producer<not_null<QEvent*>> VoiceRecordBar::lockViewportEvents() const {
	return _lock->events(
		) | rpl::filter([=](not_null<QEvent*> e) {
			return e->type() == QEvent::Wheel;
		});
}

rpl::producer<> VoiceRecordBar::updateSendButtonTypeRequests() const {
	return _listenChanges.events();
}

bool VoiceRecordBar::isLockPresent() const {
	return _lockShowing.current();
}

bool VoiceRecordBar::isListenState() const {
	return _listen != nullptr;
}

bool VoiceRecordBar::isTypeRecord() const {
	return (_send->type() == Ui::SendButton::Type::Record);
}

bool VoiceRecordBar::hasDuration() const {
	return _recordingSamples > 0;
}

float64 VoiceRecordBar::activeAnimationRatio() const {
	return _activeAnimation.value(_inField.current() ? 1. : 0.);
}

void VoiceRecordBar::clearListenState() {
	if (isListenState()) {
		hideAnimated();
	}
}

float64 VoiceRecordBar::showAnimationRatio() const {
	// There is no reason to set the final value to zero,
	// because at zero this widget is hidden.
	return _showAnimation.value(1.);
}

float64 VoiceRecordBar::showListenAnimationRatio() const {
	return _showListenAnimation.value(_listen ? 1. : 0.);
}

void VoiceRecordBar::computeAndSetLockProgress(QPoint globalPos) {
	const auto localPos = mapFromGlobal(globalPos);
	const auto lower = _lock->height();
	const auto higher = 0;
	_lock->requestPaintProgress(Progress(localPos.y(), higher - lower));
}

void VoiceRecordBar::orderControls() {
	stackUnder(_send.get());
	_level->raise();
	_lock->raise();
}

void VoiceRecordBar::installListenStateFilter() {
	auto keyFilterCallback = [=](not_null<QEvent*> e) {
		using Result = base::EventFilterResult;
		if (!(_send->type() == Ui::SendButton::Type::Send
			|| _send->type() == Ui::SendButton::Type::Schedule)) {
			return Result::Continue;
		}
		switch(e->type()) {
		case QEvent::KeyPress: {
			const auto keyEvent = static_cast<QKeyEvent*>(e.get());
			const auto key = keyEvent->key();
			const auto isSpace = (key == Qt::Key_Space);
			const auto isEnter = (key == Qt::Key_Enter
				|| key == Qt::Key_Return);
			if (isSpace && !keyEvent->isAutoRepeat() && _listen) {
				_listen->playPause();
				return Result::Cancel;
			}
			if (isEnter && !_warningShown) {
				requestToSendWithOptions({});
				return Result::Cancel;
			}
			return Result::Continue;
		}
		default: return Result::Continue;
		}
	};

	auto keyFilter = base::install_event_filter(
		QCoreApplication::instance(),
		std::move(keyFilterCallback));

	_listen->lifetime().make_state<base::unique_qptr<QObject>>(
		std::move(keyFilter));
}

void VoiceRecordBar::showDiscardBox(
		Fn<void()> &&callback,
		anim::type animated) {
	if (!isActive()) {
		return;
	}
	auto sure = [=, callback = std::move(callback)](Fn<void()> &&close) {
		if (animated == anim::type::instant) {
			hideFast();
		} else {
			hideAnimated();
		}
		close();
		_warningShown = false;
		if (callback) {
			callback();
		}
	};
	Ui::show(Box<ConfirmBox>(
		(isListenState()
			? tr::lng_record_listen_cancel_sure
			: tr::lng_record_lock_cancel_sure)(tr::now),
		tr::lng_record_lock_discard(tr::now),
		st::attentionBoxButton,
		std::move(sure)));
	_warningShown = true;
}

} // namespace HistoryView::Controls
