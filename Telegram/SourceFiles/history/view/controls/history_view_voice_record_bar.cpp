/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_voice_record_bar.h"

#include "api/api_send_progress.h"
#include "base/event_filter.h"
#include "base/random.h"
#include "base/unixtime.h"
#include "ui/boxes/confirm_box.h"
#include "calls/calls_instance.h"
#include "chat_helpers/compose/compose_show.h"
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
#include "media/streaming/media_streaming_instance.h"
#include "media/streaming/media_streaming_round_preview.h"
#include "storage/storage_account.h"
#include "ui/controls/round_video_recorder.h"
#include "ui/controls/send_button.h"
#include "ui/effects/animation_value.h"
#include "ui/effects/animation_value_f.h"
#include "ui/effects/ripple_animation.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/dynamic_image.h"
#include "ui/painter.h"
#include "ui/widgets/tooltip.h"
#include "ui/rect.h"
#include "ui/ui_utility.h"
#include "webrtc/webrtc_video_track.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"
#include "styles/style_media_player.h"

#include <tgcalls/VideoCaptureInterface.h>

namespace HistoryView::Controls {
namespace {

constexpr auto kAudioVoiceUpdateView = crl::time(200);
constexpr auto kAudioVoiceMaxLength = 100 * 60; // 100 minutes
constexpr auto kMaxSamples
	= ::Media::Player::kDefaultFrequency * kAudioVoiceMaxLength;
constexpr auto kMinSamples
	= ::Media::Player::kDefaultFrequency / 5; // 0.2 seconds

constexpr auto kInactiveWaveformBarAlpha = int(255 * 0.6);

constexpr auto kPrecision = 10;

constexpr auto kLockArcAngle = 15.;

constexpr auto kHideWaveformBgOffset = 50;

enum class FilterType {
	Continue,
	ShowBox,
	Cancel,
};

class SoundedPreview final : public Ui::DynamicImage {
public:
	SoundedPreview(
		not_null<DocumentData*> document,
		rpl::producer<> repaints);
	std::shared_ptr<DynamicImage> clone() override;
	QImage image(int size) override;
	void subscribeToUpdates(Fn<void()> callback) override;

private:
	const not_null<DocumentData*> _document;
	QImage _roundingMask;
	Fn<void()> _repaint;
	rpl::lifetime _lifetime;

};

SoundedPreview::SoundedPreview(
	not_null<DocumentData*> document,
	rpl::producer<> repaints)
: _document(document) {
	std::move(repaints) | rpl::start_with_next([=] {
		if (const auto onstack = _repaint) {
			onstack();
		}
	}, _lifetime);
}

std::shared_ptr<Ui::DynamicImage> SoundedPreview::clone() {
	Unexpected("ListenWrap::videoPreview::clone.");
}

QImage SoundedPreview::image(int size) {
	const auto player = ::Media::Player::instance();
	const auto streamed = player->roundVideoPreview(_document);
	if (!streamed) {
		return {};
	}

	const auto full = QSize(size, size) * style::DevicePixelRatio();
	if (_roundingMask.size() != full) {
		_roundingMask = Images::EllipseMask(full);
	}
	const auto frame = streamed->frameWithInfo({
		.resize = full,
		.outer = full,
		.mask = _roundingMask,
	});
	return frame.image;
}

void SoundedPreview::subscribeToUpdates(Fn<void()> callback) {
	_repaint = std::move(callback);
}

[[nodiscard]] auto InactiveColor(const QColor &c) {
	return QColor(c.red(), c.green(), c.blue(), kInactiveWaveformBarAlpha);
}

[[nodiscard]] auto Progress(int low, int high) {
	return std::clamp(float64(low) / high, 0., 1.);
}

[[nodiscard]] auto FormatVoiceDuration(int samples) {
	const int duration = kPrecision
		* (float64(samples) / ::Media::Player::kDefaultFrequency);
	const auto durationString = Ui::FormatDurationText(duration / kPrecision);
	const auto decimalPart = QString::number(duration % kPrecision);
	return durationString + QLocale().decimalPoint() + decimalPart;
}

[[nodiscard]] std::unique_ptr<VoiceData> ProcessCaptureResult(
		const VoiceWaveform &waveform) {
	auto voiceData = std::make_unique<VoiceData>();
	voiceData->waveform = waveform;
	voiceData->wavemax = voiceData->waveform.empty()
		? uchar(0)
		: *ranges::max_element(voiceData->waveform);
	return voiceData;
}

[[nodiscard]] not_null<DocumentData*> DummyDocument(
		not_null<Data::Session*> owner) {
	return owner->document(
		base::RandomValue<DocumentId>(),
		uint64(0),
		QByteArray(),
		base::unixtime::now(),
		QVector<MTPDocumentAttribute>(),
		QString(),
		InlineImageLocation(),
		ImageWithLocation(),
		ImageWithLocation(),
		false, // isPremiumSticker
		owner->session().mainDcId(),
		int32(0));
}

void PaintWaveform(
		QPainter &p,
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
	const auto activeWidth = base::SafeRound(availableWidth * progress);

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

void FillWithMinithumbs(
		QPainter &p,
		not_null<const Ui::RoundVideoResult*> data,
		QRect rect,
		float64 progress) {
	if (!data->minithumbsCount || !data->minithumbSize || rect.isEmpty()) {
		return;
	}
	const auto size = rect.height();
	const auto single = data->minithumbSize;
	const auto perrow = data->minithumbs.width() / single;
	const auto thumbs = (rect.width() + size - 1) / size;
	if (!thumbs || !perrow) {
		return;
	}
	for (auto i = 0; i != thumbs - 1; ++i) {
		const auto index = (i * data->minithumbsCount) / thumbs;
		p.drawImage(
			QRect(rect.x() + i * size, rect.y(), size, size),
			data->minithumbs,
			QRect(
				(index % perrow) * single,
				(index / perrow) * single,
				single,
				single));
	}
	const auto last = rect.width() - (thumbs - 1) * size;
	const auto index = ((thumbs - 1) * data->minithumbsCount) / thumbs;
	p.drawImage(
		QRect(rect.x() + (thumbs - 1) * size, rect.y(), last, size),
		data->minithumbs,
		QRect(
			(index % perrow) * single,
			(index / perrow) * single,
			(last * single) / size,
			single));
}

[[nodiscard]] QRect DrawLockCircle(
		QPainter &p,
		const QRect &widgetRect,
		const style::RecordBarLock &st,
		float64 progress) {
	const auto &originTop = st.originTop;
	const auto &originBottom = st.originBottom;
	const auto &originBody = st.originBody;
	const auto &shadowTop = st.shadowTop;
	const auto &shadowBottom = st.shadowBottom;
	const auto &shadowBody = st.shadowBody;
	const auto &shadowMargins = st::historyRecordLockMargin;

	const auto bottomMargin = anim::interpolate(
		0,
		widgetRect.height() - shadowTop.height() - shadowBottom.height(),
		progress);

	const auto topMargin = anim::interpolate(
		widgetRect.height() / 4,
		0,
		progress);

	const auto full = widgetRect - QMargins(0, topMargin, 0, bottomMargin);
	const auto inner = full - shadowMargins;
	const auto content = inner
		- style::margins(0, originTop.height(), 0, originBottom.height());
	const auto contentShadow = full
		- style::margins(0, shadowTop.height(), 0, shadowBottom.height());

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
	if (progress < 1.) {
		const auto &arrow = st.arrow;
		const auto arrowRect = QRect(
			inner.x(),
			content.y() + content.height() - arrow.height() / 2,
			inner.width(),
			arrow.height());
		p.setOpacity(1. - progress);
		arrow.paintInCenter(p, arrowRect);
		p.setOpacity(1.);
	}

	return inner;
}

class TTLButton final : public Ui::RippleButton {
public:
	TTLButton(
		not_null<Ui::RpWidget*> parent,
		const style::RecordBar &st,
		bool recordingVideo);

	void clearState() override;

protected:
	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	const style::RecordBar &_st;
	const QRect _rippleRect;

	Ui::Animations::Simple _activeAnimation;
	base::unique_qptr<Ui::ImportantTooltip> _tooltip;

};

TTLButton::TTLButton(
	not_null<Ui::RpWidget*> parent,
	const style::RecordBar &st,
	bool recordingVideo)
: RippleButton(parent, st.lock.ripple)
, _st(st)
, _rippleRect(Rect(Size(st::historyRecordLockTopShadow.width()))
	- (st::historyRecordLockRippleMargin)) {
	QWidget::resize(Size(st::historyRecordLockTopShadow.width()));
	Ui::AbstractButton::setDisabled(true);

	Ui::AbstractButton::setClickedCallback([=] {
		Ui::AbstractButton::setDisabled(!Ui::AbstractButton::isDisabled());
		const auto isActive = !Ui::AbstractButton::isDisabled();
		_activeAnimation.start(
			[=] { update(); },
			isActive ? 0. : 1.,
			isActive ? 1. : 0.,
			st::universalDuration);
	});

	Ui::RpWidget::shownValue() | rpl::start_with_next([=](bool shown) {
		if (!shown) {
			_tooltip = nullptr;
			return;
		} else if (_tooltip) {
			return;
		}
		auto text = rpl::conditional(
			Core::App().settings().ttlVoiceClickTooltipHiddenValue(),
			(recordingVideo
				? tr::lng_record_once_active_video
				: tr::lng_record_once_active_tooltip)(
					Ui::Text::RichLangValue),
			tr::lng_record_once_first_tooltip(
				Ui::Text::RichLangValue));
		_tooltip.reset(Ui::CreateChild<Ui::ImportantTooltip>(
			parent.get(),
			object_ptr<Ui::PaddingWrap<Ui::FlatLabel>>(
				parent.get(),
				Ui::MakeNiceTooltipLabel(
					parent,
					std::move(text),
					st::historyMessagesTTLLabel.minWidth,
					st::ttlMediaImportantTooltipLabel),
				st::defaultImportantTooltip.padding),
			st::historyRecordTooltip));
		Ui::RpWidget::geometryValue(
		) | rpl::start_with_next([=](const QRect &r) {
			if (r.isEmpty()) {
				return;
			}
			_tooltip->pointAt(r, RectPart::Right, [=](QSize size) {
				return QPoint(
					r.left()
						- size.width()
						- st::defaultImportantTooltip.padding.left(),
					r.top()
						+ r.height()
						- size.height()
						+ st::historyRecordTooltip.padding.top());
			});
		}, _tooltip->lifetime());
		_tooltip->show();
		if (!Core::App().settings().ttlVoiceClickTooltipHidden()) {
			clicks(
			) | rpl::take(1) | rpl::start_with_next([=] {
				Core::App().settings().setTtlVoiceClickTooltipHidden(true);
			}, _tooltip->lifetime());
			_tooltip->toggleAnimated(true);
		} else {
			_tooltip->toggleFast(false);
		}

		clicks(
		) | rpl::start_with_next([=] {
			const auto toggled = !Ui::AbstractButton::isDisabled();
			_tooltip->toggleAnimated(toggled);

			if (toggled) {
				constexpr auto kTimeout = crl::time(3000);
				_tooltip->hideAfter(kTimeout);
			}
		}, _tooltip->lifetime());

		Ui::RpWidget::geometryValue(
		) | rpl::map([=](const QRect &r) {
			return (r.left() + r.width() > parentWidget()->width());
		}) | rpl::distinct_until_changed(
		) | rpl::start_with_next([=](bool toHide) {
			const auto isFirstTooltip
				= !Core::App().settings().ttlVoiceClickTooltipHidden();
			if (isFirstTooltip || toHide) {
				_tooltip->toggleAnimated(!toHide);
			}
		}, _tooltip->lifetime());
	}, lifetime());

	paintRequest(
	) | rpl::start_with_next([=](const QRect &clip) {
		auto p = QPainter(this);

		const auto inner = DrawLockCircle(p, rect(), _st.lock, 1.);

		Ui::RippleButton::paintRipple(p, _rippleRect.x(), _rippleRect.y());

		const auto activeProgress = _activeAnimation.value(
			!Ui::AbstractButton::isDisabled() ? 1 : 0);

		p.setOpacity(1. - activeProgress);
		st::historyRecordVoiceOnceInactive.paintInCenter(p, inner);

		if (activeProgress) {
			p.setOpacity(activeProgress);
			st::historyRecordVoiceOnceBg.paintInCenter(p, inner);
			st::historyRecordVoiceOnceFg.paintInCenter(p, inner);
		}

	}, lifetime());
}

void TTLButton::clearState() {
	Ui::AbstractButton::setDisabled(true);
	QWidget::update();
	Ui::RpWidget::hide();
}

QImage TTLButton::prepareRippleMask() const {
	return Ui::RippleAnimation::EllipseMask(_rippleRect.size());
}

QPoint TTLButton::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos()) - _rippleRect.topLeft();
}

} // namespace

class ListenWrap final {
public:
	ListenWrap(
		not_null<Ui::RpWidget*> parent,
		const style::RecordBar &st,
		not_null<Main::Session*> session,
		not_null<Ui::RoundVideoResult*> data,
		const style::font &font);

	void requestPaintProgress(float64 progress);
	[[nodiscard]] rpl::producer<> stopRequests() const;

	void playPause();
	[[nodiscard]] std::shared_ptr<Ui::DynamicImage> videoPreview();

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void init();
	void initPlayButton();
	void initPlayProgress();

	[[nodiscard]] bool isInPlayer(
		const ::Media::Player::TrackState &state) const;
	[[nodiscard]] bool isInPlayer() const;

	[[nodiscard]] int computeTopMargin(int height) const;
	[[nodiscard]] QRect computeWaveformRect(const QRect &centerRect) const;

	const not_null<Ui::RpWidget*> _parent;

	const style::RecordBar &_st;
	const not_null<Main::Session*> _session;
	const not_null<DocumentData*> _document;
	const std::unique_ptr<VoiceData> _voiceData;
	const std::shared_ptr<Data::DocumentMedia> _mediaView;
	const not_null<Ui::RoundVideoResult*> _data;
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
	rpl::event_stream<> _videoRepaints;

	rpl::lifetime _lifetime;

};

ListenWrap::ListenWrap(
	not_null<Ui::RpWidget*> parent,
	const style::RecordBar &st,
	not_null<Main::Session*> session,
	not_null<Ui::RoundVideoResult*> data,
	const style::font &font)
: _parent(parent)
, _st(st)
, _session(session)
, _document(DummyDocument(&session->data()))
, _voiceData(ProcessCaptureResult(data->waveform))
, _mediaView(_document->createMediaView())
, _data(data)
, _delete(base::make_unique_q<Ui::IconButton>(parent, _st.remove))
, _durationFont(font)
, _duration(Ui::FormatDurationText(_data->duration / 1000))
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
			const auto m = _st.remove.width + _waveformBgRect.height() / 2;
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
		auto p = QPainter(_parent);
		auto hq = PainterHighQualityEnabler(p);
		const auto progress = _showProgress.current();
		p.setOpacity(progress);
		const auto &remove = _st.remove;
		if (progress > 0. && progress < 1.) {
			remove.icon.paint(p, remove.iconPosition, _parent->width());
		}

		{
			const auto hideOffset = _isShowAnimation
				? 0
				: anim::interpolate(kHideWaveformBgOffset, 0, progress);
			const auto deleteIconLeft = remove.iconPosition.x();
			const auto bgRectRight = anim::interpolate(
				deleteIconLeft,
				remove.width,
				_isShowAnimation ? progress : 1.);
			const auto bgRectLeft = anim::interpolate(
				_parent->width() - deleteIconLeft - _waveformBgRect.height(),
				remove.width,
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
			} else {
				p.fillRect(bgRect, _st.bg);
			}
			p.setPen(Qt::NoPen);
			p.setBrush(_st.cancelActive);
			auto path = QPainterPath();
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
			const auto waveformRect = (progress == 1.)
				? _waveformFgRect
				: computeWaveformRect(bgCenterRect);
			if (!waveformRect.isEmpty()) {
				const auto playProgress = _playProgress.current();
				if (_data->minithumbs.isNull()) {
					p.translate(waveformRect.topLeft());
					PaintWaveform(
						p,
						_voiceData.get(),
						waveformRect.width(),
						_activeWaveformBar,
						_inactiveWaveformBar,
						playProgress);
					p.resetTransform();
				} else {
					FillWithMinithumbs(
						p,
						_data,
						waveformRect,
						playProgress);
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

	_mediaView->setBytes(_data->content);
	_document->size = _data->content.size();
	_document->type = _data->minithumbs.isNull()
		? VoiceDocument
		: RoundVideoDocument;

	const auto &play = _playPauseSt.playOuter;
	const auto &width = _waveformBgFinalCenterRect.height();
	_playPauseButton->resize(width, width);
	_playPauseButton->show();

	_playPauseButton->paintRequest(
	) | rpl::start_with_next([=](const QRect &clip) {
		auto p = QPainter(_playPauseButton);

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
			if (!_data->minithumbs.isNull()) {
				_videoRepaints.fire({});
			}
		} else if (showPause->current()) {
			*showPause = false;
		}
	}, _lifetime);

	instance()->stops(
		AudioMsgId::Type::Voice
	) | rpl::start_with_next([=] {
		*showPause = false;
	}, _lifetime);

	_lifetime.add([=] {
		const auto current = instance()->current(AudioMsgId::Type::Voice);
		if (current.audio() == _document) {
			instance()->stop(AudioMsgId::Type::Voice, true);
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

std::shared_ptr<Ui::DynamicImage> ListenWrap::videoPreview() {
	return std::make_shared<SoundedPreview>(
		_document,
		_videoRepaints.events());
}

rpl::lifetime &ListenWrap::lifetime() {
	return _lifetime;
}

class RecordLock final : public Ui::RippleButton {
public:
	RecordLock(
		not_null<Ui::RpWidget*> parent,
		const style::RecordBarLock &st);

	void requestPaintProgress(float64 progress);
	void requestPaintLockToStopProgress(float64 progress);
	void requestPaintPauseToInputProgress(float64 progress);
	void setVisibleTopPart(int part);
	void setRecordingVideo(bool value);

	[[nodiscard]] rpl::producer<> locks() const;
	[[nodiscard]] bool isLocked() const;
	[[nodiscard]] bool isStopState() const;

	[[nodiscard]] float64 lockToStopProgress() const;

protected:
	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	void init();

	void drawProgress(QPainter &p);
	void setProgress(float64 progress);
	void startLockingAnimation(float64 to);

	const style::RecordBarLock &_st;
	const QRect _rippleRect;
	const QPen _arcPen;

	Ui::Animations::Simple _lockEnderAnimation;

	float64 _lockToStopProgress = 0.;
	float64 _pauseToInputProgress = 0.;
	rpl::variable<float64> _progress = 0.;
	int _visibleTopPart = -1;
	bool _recordingVideo = false;

};

RecordLock::RecordLock(
	not_null<Ui::RpWidget*> parent,
	const style::RecordBarLock &st)
: RippleButton(parent, st.ripple)
, _st(st)
, _rippleRect(Rect(Size(st::historyRecordLockTopShadow.width()))
	- (st::historyRecordLockRippleMargin))
, _arcPen(
	QColor(Qt::white),
	st::historyRecordLockIconLineWidth,
	Qt::SolidLine,
	Qt::SquareCap,
	Qt::RoundJoin) {
	init();
}

void RecordLock::setVisibleTopPart(int part) {
	_visibleTopPart = part;
}

void RecordLock::setRecordingVideo(bool value) {
	_recordingVideo = value;
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
			_pauseToInputProgress = 0.;
			_progress = 0.;
		}
	}, lifetime());

	paintRequest(
	) | rpl::start_with_next([=](const QRect &clip) {
		if (!_visibleTopPart) {
			return;
		}
		auto p = QPainter(this);
		if (_visibleTopPart > 0 && _visibleTopPart < height()) {
			p.setClipRect(0, 0, width(), _visibleTopPart);
		}
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

void RecordLock::drawProgress(QPainter &p) {
	const auto progress = _progress.current();

	const auto inner = DrawLockCircle(p, rect(), _st, progress);

	if (isLocked()) {
		Ui::RippleButton::paintRipple(p, _rippleRect.x(), _rippleRect.y());
	}
	{
		const auto &arcOffset = st::historyRecordLockIconLineSkip;
		const auto &size = st::historyRecordLockIconSize;

		const auto arcWidth = size.width() - arcOffset * 2;
		const auto &arcHeight = st::historyRecordLockIconArcHeight;

		const auto &blockHeight = st::historyRecordLockIconBottomHeight;

		const auto blockRectWidth = anim::interpolateToF(
			size.width(),
			st::historyRecordStopIconWidth,
			_lockToStopProgress);
		const auto blockRectHeight = anim::interpolateToF(
			blockHeight,
			st::historyRecordStopIconWidth,
			_lockToStopProgress);
		const auto blockRectTop = anim::interpolateToF(
			size.height() - blockHeight,
			base::SafeRound((size.height() - blockRectHeight) / 2.),
			_lockToStopProgress);

		const auto blockRect = QRectF(
			(size.width() - blockRectWidth) / 2,
			blockRectTop,
			blockRectWidth,
			blockRectHeight);
		const auto &lineHeight = st::historyRecordLockIconLineHeight;

		const auto lockTranslation = QPoint(
			(inner.width() - size.width()) / 2,
			(_st.originTop.height() * 2 - size.height()) / 2);
		const auto xRadius = anim::interpolateF(2, 3, _lockToStopProgress);

		const auto pauseLineOffset = blockRectWidth / 2
			+ st::historyRecordLockIconLineWidth;
		if (_lockToStopProgress == 1.) {
			// Paint the block.
			auto hq = PainterHighQualityEnabler(p);
			p.translate(inner.topLeft() + lockTranslation);
			p.setPen(Qt::NoPen);
			p.setBrush(_st.fg);
			if (_pauseToInputProgress > 0.) {
				p.setOpacity(_pauseToInputProgress);
				const auto &icon = _recordingVideo
					? st::historyRecordLockRound
					: st::historyRecordLockInput;
				icon.paintInCenter(p, blockRect.toRect());
				p.setOpacity(1. - _pauseToInputProgress);
			}
			p.drawRoundedRect(
				blockRect - QMargins(0, 0, pauseLineOffset, 0),
				xRadius,
				3);
			p.drawRoundedRect(
				blockRect - QMargins(pauseLineOffset, 0, 0, 0),
				xRadius,
				3);
		} else {
			// Paint an animation frame.
			auto frame = QImage(
				inner.size() * style::DevicePixelRatio(),
				QImage::Format_ARGB32_Premultiplied);
			frame.setDevicePixelRatio(style::DevicePixelRatio());
			frame.fill(Qt::transparent);

			auto q = QPainter(&frame);
			auto hq = PainterHighQualityEnabler(q);

			q.setPen(Qt::NoPen);
			q.setBrush(_arcPen.brush());

			q.translate(lockTranslation);
			{
				const auto offset = anim::interpolateF(
					0,
					pauseLineOffset,
					_lockToStopProgress);
				q.drawRoundedRect(
					blockRect - QMarginsF(0, 0, offset, 0),
					xRadius,
					3);
				q.drawRoundedRect(
					blockRect - QMarginsF(offset, 0, 0, 0),
					xRadius,
					3);
			}

			const auto offsetTranslate = _lockToStopProgress *
				(lineHeight + arcHeight + _arcPen.width() * 2);
			q.translate(
				size.width() - arcOffset,
				blockRect.y() + offsetTranslate);

			if (progress < 1. && progress > 0.) {
				q.rotate(kLockArcAngle * progress);
			}

			const auto lockProgress = 1. - _lockToStopProgress;
			{
				auto arcPen = _arcPen;
				arcPen.setWidthF(_arcPen.widthF() * lockProgress);
				q.setPen(arcPen);
			}
			const auto rLine = QLineF(0, 0, 0, -lineHeight);
			q.drawLine(rLine);

			q.drawArc(
				-arcWidth,
				rLine.dy() - arcHeight - _arcPen.width() + rLine.y1(),
				arcWidth,
				arcHeight * 2,
				0,
				arc::kHalfLength);

			if (progress == 1. && lockProgress < 1.) {
				q.drawLine(
					-arcWidth,
					rLine.y2(),
					-arcWidth,
					rLine.dy() * lockProgress);
			}
			q.end();

			p.drawImage(
				inner.topLeft(),
				style::colorizeImage(frame, _st.fg));
		}
	}
}

void RecordLock::startLockingAnimation(float64 to) {
	_lockEnderAnimation.start(
		[=](float64 value) { setProgress(value); },
		0.,
		to,
		st::universalDuration);
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

void RecordLock::requestPaintPauseToInputProgress(float64 progress) {
	_pauseToInputProgress = progress;
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
	return Ui::RippleAnimation::EllipseMask(_rippleRect.size());
}

QPoint RecordLock::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos()) - _rippleRect.topLeft();
}

class CancelButton final : public Ui::RippleButton {
public:
	CancelButton(
		not_null<Ui::RpWidget*> parent,
		const style::RecordBar &st,
		int height);

	void requestPaintProgress(float64 progress);

protected:
	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	void init();

	const style::RecordBar &_st;
	const int _width;
	const QRect _rippleRect;

	rpl::variable<float64> _showProgress = 0.;

	Ui::Text::String _text;

};

CancelButton::CancelButton(
	not_null<Ui::RpWidget*> parent,
	const style::RecordBar &st,
	int height)
: Ui::RippleButton(parent, st.cancelRipple)
, _st(st)
, _width(st::historyRecordCancelButtonWidth)
, _rippleRect(QRect(0, (height - _width) / 2, _width, _width))
, _text(st::semiboldTextStyle, tr::lng_selected_clear(tr::now)) {
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
		auto p = QPainter(this);

		p.setOpacity(_showProgress.current());

		Ui::RippleButton::paintRipple(p, _rippleRect.x(), _rippleRect.y());

		p.setPen(_st.cancelActive);
		_text.draw(p, {
			.position = QPoint(0, (height() - _text.minHeight()) / 2),
			.outerWidth = width(),
			.availableWidth = width(),
			.align = style::al_center,
		});
	}, lifetime());
}

QImage CancelButton::prepareRippleMask() const {
	return Ui::RippleAnimation::EllipseMask(_rippleRect.size());
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
	VoiceRecordBarDescriptor &&descriptor)
: RpWidget(parent)
, _st(descriptor.stOverride ? *descriptor.stOverride : st::defaultRecordBar)
, _outerContainer(descriptor.outerContainer)
, _show(std::move(descriptor.show))
, _send(std::move(descriptor.send))
, _lock(std::make_unique<RecordLock>(_outerContainer, _st.lock))
, _level(std::make_unique<VoiceRecordButton>(_outerContainer, _st))
, _cancel(std::make_unique<CancelButton>(this, _st, descriptor.recorderHeight))
, _startTimer([=] { startRecording(); })
, _message(
	st::historyRecordTextStyle,
	(!descriptor.customCancelText.isEmpty()
		? descriptor.customCancelText
		: tr::lng_record_cancel(tr::now)),
	TextParseOptions{ TextParseMultiline, 0, 0, Qt::LayoutDirectionAuto })
, _lockFromBottom(descriptor.lockFromBottom)
, _cancelFont(st::historyRecordFont) {
	resize(QSize(parent->width(), descriptor.recorderHeight));
	init();
	hideFast();
}

VoiceRecordBar::VoiceRecordBar(
	not_null<Ui::RpWidget*> parent,
	std::shared_ptr<ChatHelpers::Show> show,
	std::shared_ptr<Ui::SendButton> send,
	int recorderHeight)
: VoiceRecordBar(parent, {
	.outerContainer = parent,
	.show = std::move(show),
	.send = std::move(send),
	.recorderHeight = recorderHeight,
}) {
}

VoiceRecordBar::~VoiceRecordBar() {
	if (isActive()) {
		stopRecording(StopType::Cancel);
	}
}

void VoiceRecordBar::updateMessageGeometry() {
	const auto left = rect::right(_durationRect) + st::historyRecordTextLeft;
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
	const auto parent = parentWidget();
	const auto me = Ui::MapFrom(_outerContainer, parent, geometry());
	const auto finalTop = me.y()
		- st::historyRecordLockPosition.y()
		- _lock->height();
	const auto finalRight = _outerContainer->width()
		- rect::right(me)
		+ st::historyRecordLockPosition.x();
	const auto progress = _showLockAnimation.value(
		_lockShowing.current() ? 1. : 0.);
	if (_lockFromBottom) {
		const auto top = anim::interpolate(me.y(), finalTop, progress);
		_lock->moveToRight(finalRight, top);
		_lock->setVisibleTopPart(me.y() - top);
	} else {
		const auto from = -_lock->width();
		const auto right = anim::interpolate(from, finalRight, progress);
		_lock->moveToRight(right, finalTop);
	}
}

void VoiceRecordBar::updateTTLGeometry(
		TTLAnimationType type,
		float64 progress) {
	if (!_ttlButton) {
		return;
	}
	const auto parent = parentWidget();
	const auto me = Ui::MapFrom(_outerContainer, parent, geometry());
	const auto anyTop = me.y() - st::historyRecordLockPosition.y();
	const auto lockHiddenProgress = (_lockShowing.current() || !_fullRecord)
		? 0.
		: (1. - _showLockAnimation.value(0.));
	const auto ttlFrom = anyTop
		- _ttlButton->height()
		- (_ttlButton->height() * (1. - lockHiddenProgress));
	if (type == TTLAnimationType::RightLeft) {
		const auto finalRight = _outerContainer->width()
			- rect::right(me)
			+ st::historyRecordLockPosition.x();

		const auto from = -_ttlButton->width();
		const auto right = anim::interpolate(from, finalRight, progress);
		_ttlButton->moveToRight(right, ttlFrom);
#if 0
	} else if (type == TTLAnimationType::TopBottom) {
		const auto ttlFrom = anyTop - _ttlButton->height() * 2;
		const auto ttlTo = anyTop - _lock->height();
		_ttlButton->moveToLeft(
			_ttlButton->x(),
			anim::interpolate(ttlFrom, ttlTo, 1. - progress));
#endif
	} else if (type == TTLAnimationType::RightTopStatic) {
		_ttlButton->moveToRight(-_ttlButton->width(), ttlFrom);
	}
}

void VoiceRecordBar::init() {
	if (_st.radius > 0) {
		_backgroundRect.emplace(_st.radius, _st.bg);
	}

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
	}, lifetime());

	paintRequest(
	) | rpl::start_with_next([=](const QRect &clip) {
		auto p = QPainter(this);
		if (_showAnimation.animating()) {
			p.setOpacity(showAnimationRatio());
		}
		if (_backgroundRect) {
			_backgroundRect->paint(p, rect());
		} else {
			p.fillRect(clip, _st.bg);
		}

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
			if (_fullRecord && !show) {
				updateTTLGeometry(TTLAnimationType::RightLeft, 1.);
			}
		};
		_showLockAnimation.start(std::move(callback), from, to, duration);
	}, lifetime());

	const auto setLevelAsSend = [=] {
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
	};

	const auto paintShowListenCallback = [=](float64 value) {
		_listen->requestPaintProgress(value);
		_level->requestPaintProgress(1. - value);
		_lock->requestPaintPauseToInputProgress(value);
		update();
	};

	_lock->setClickedCallback([=] {
		if (isListenState()) {
			startRecording();
			_showListenAnimation.stop();
			_showListenAnimation.start([=](float64 value) {
				_listen->requestPaintProgress(1.);
				paintShowListenCallback(value);
				if (!value) {
					_listen = nullptr;
				}
			}, 1., 0., st::universalDuration * 2);
			setLevelAsSend();

			return;
		}
		if (!_lock->isStopState()) {
			return;
		}

		stopRecording(StopType::Listen);
	});

	_paused.value() | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](bool paused) {
		if (!paused) {
			return;
		}

		const auto to = 1.;
		auto callback = [=](float64 value) {
			paintShowListenCallback(value);
			if (to == value) {
				_recordingLifetime.destroy();
			}
		};
		_showListenAnimation.stop();
		_showListenAnimation.start(
			std::move(callback),
			0.,
			to,
			st::universalDuration);
	}, lifetime());

	_lock->locks(
	) | rpl::start_with_next([=] {
		if (_hasTTLFilter && _hasTTLFilter()) {
			if (!_ttlButton) {
				_ttlButton = std::make_unique<TTLButton>(
					_outerContainer,
					_st,
					_recordingVideo);
			}
			_ttlButton->show();
		}
		updateTTLGeometry(TTLAnimationType::RightTopStatic, 0);

		setLevelAsSend();

		auto callback = [=](float64 value) {
			_lock->requestPaintLockToStopProgress(value);
			update();
			updateTTLGeometry(TTLAnimationType::RightLeft, value);
		};
		_lockToStopAnimation.start(
			std::move(callback),
			0.,
			1.,
			st::universalDuration);
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
			_recordingTipRequire = crl::now();
			_recordingVideo = (_send->type() == Ui::SendButton::Type::Round);
			_fullRecord = false;
			_ttlButton = nullptr;
			_lock->setRecordingVideo(_recordingVideo);
			_startTimer.callOnce(st::universalDuration);
		} else if (e->type() == QEvent::MouseButtonRelease) {
			checkTipRequired();
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

	initLockGeometry();
	initLevelGeometry();
}

void VoiceRecordBar::activeAnimate(bool active) {
	const auto to = active ? 1. : 0.;
	if (_activeAnimation.animating()) {
		_activeAnimation.change(to, st::universalDuration);
	} else {
		auto callback = [=] {
			update(_messageRect);
			_level->requestPaintColor(activeAnimationRatio());
		};
		_activeAnimation.start(
			std::move(callback),
			active ? 0. : 1.,
			to,
			st::universalDuration);
	}
}

void VoiceRecordBar::visibilityAnimate(bool show, Fn<void()> &&callback) {
	if (_send->type() == Ui::SendButton::Type::Round) {
		_level->setType(VoiceRecordButton::Type::Round);
	} else {
		_level->setType(VoiceRecordButton::Type::Record);
	}
	const auto to = show ? 1. : 0.;
	const auto from = show ? 0. : 1.;
	auto animationCallback = [=, callback = std::move(callback)](auto value) {
		if (!_listen) {
			_level->requestPaintProgress(value);
		} else {
			_listen->requestPaintProgress(value);
		}
		update();
		if (!show) {
			updateTTLGeometry(TTLAnimationType::RightLeft, value);
		}
		if ((show && value == 1.) || (!show && value == 0.)) {
			if (callback) {
				callback();
			}
		}
	};
	_showAnimation.start(
		std::move(animationCallback),
		from,
		to,
		st::universalDuration);
}

void VoiceRecordBar::setStartRecordingFilter(FilterCallback &&callback) {
	_startRecordingFilter = std::move(callback);
}

void VoiceRecordBar::setTTLFilter(FilterCallback &&callback) {
	_hasTTLFilter = std::move(callback);
}

void VoiceRecordBar::initLockGeometry() {
	const auto parent = static_cast<Ui::RpWidget*>(parentWidget());
	rpl::merge(
		_lock->heightValue() | rpl::to_empty,
		geometryValue() | rpl::to_empty,
		parent->geometryValue() | rpl::to_empty
	) | rpl::start_with_next([=] {
		updateLockGeometry();
	}, lifetime());
	parent->geometryValue(
	) | rpl::start_with_next([=] {
		updateTTLGeometry(TTLAnimationType::RightLeft, 1.);
	}, lifetime());
}

void VoiceRecordBar::initLevelGeometry() {
	rpl::combine(
		_send->geometryValue(),
		geometryValue(),
		static_cast<Ui::RpWidget*>(parentWidget())->geometryValue()
	) | rpl::start_with_next([=](QRect send, auto, auto) {
		const auto mapped = Ui::MapFrom(
			_outerContainer,
			_send->parentWidget(),
			send);
		const auto center = (send.width() - _level->width()) / 2;
		_level->moveToLeft(mapped.x() + center, mapped.y() + center);
	}, lifetime());
}

void VoiceRecordBar::startRecording() {
	if (isRecording()) {
		return;
	}
	auto appearanceCallback = [=] {
		if (_showAnimation.animating()) {
			return;
		}

		using namespace ::Media::Capture;
		if (_recordingVideo && !createVideoRecorder()) {
			stop(false);
			return;
		}
		if (!instance()->available()) {
			stop(false);
			return;
		}

		_lockShowing = true;
		startRedCircleAnimation();

		_recording = true;
		if (_paused.current()) {
			_paused = false;
			instance()->pause(false, nullptr);
			if (_videoRecorder) {
				_videoRecorder->resume({
					.video = std::move(_data),
				});
			}
		} else {
			instance()->start(_videoRecorder
				? _videoRecorder->audioChunkProcessor()
				: nullptr);
		}
		instance()->updated(
		) | rpl::start_with_next_error([=](const Update &update) {
			recordUpdated(update.level, update.samples);
		}, [=] {
			stop(false);
		}, _recordingLifetime);
		if (_videoRecorder) {
			_videoRecorder->updated(
			) | rpl::start_with_next_error([=](const Update &update) {
				recordUpdated(update.level, update.samples);
				if (update.finished) {
					_fullRecord = true;
					stopRecording(StopType::Listen);
					_lockShowing = false;
				}
			}, [=](Error error) {
				stop(false);
				_errors.fire_copy(error);
			}, _recordingLifetime);
		}
		_recordingLifetime.add([=] {
			_recording = false;
		});
	};
	visibilityAnimate(true, std::move(appearanceCallback));
	show();

	_inField = true;

	_send->events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return (e->type() == QEvent::MouseMove
			|| e->type() == QEvent::MouseButtonRelease)
			&& isTypeRecord()
			&& !_lock->isLocked();
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
			checkTipRequired();
			stop(_inField.current());
		}
	}, _recordingLifetime);
}

void VoiceRecordBar::checkTipRequired() {
	const auto require = base::take(_recordingTipRequire);
	const auto duration = st::universalDuration
		+ (kMinSamples * crl::time(1000)
			/ ::Media::Player::kDefaultFrequency);
	if (require && (require + duration > crl::now())) {
		_recordingTipRequests.fire({});
	}
}

void VoiceRecordBar::recordUpdated(quint16 level, int samples) {
	_level->requestPaintLevel(level);
	_recordingSamples = samples;
	if (samples < 0 || samples >= kMaxSamples) {
		stop(samples > 0 && _inField.current());
	}
	Core::App().updateNonIdle();
	update(_durationRect);
	const auto type = _recordingVideo
		? Api::SendProgressType::RecordRound
		: Api::SendProgressType::RecordVoice;
	_sendActionUpdates.fire({ type });
}

void VoiceRecordBar::stop(bool send) {
	if (isHidden() && !send) {
		return;
	}
	const auto ttlBeforeHide = peekTTLState();
	auto disappearanceCallback = [=] {
		hide();

		const auto type = send ? StopType::Send : StopType::Cancel;
		stopRecording(type, ttlBeforeHide);
	};
	visibilityAnimate(false, std::move(disappearanceCallback));
}

void VoiceRecordBar::finish() {
	_recordingLifetime.destroy();
	_lockShowing = false;
	_inField = false;
	_redCircleProgress = 0.;
	_recordingSamples = 0;
	_paused = false;

	_showAnimation.stop();
	_lockToStopAnimation.stop();

	_listen = nullptr;

	[[maybe_unused]] const auto s = takeTTLState();

	const auto type = _recordingVideo
		? Api::SendProgressType::RecordRound
		: Api::SendProgressType::RecordVoice;
	_sendActionUpdates.fire({ type, -1 });

	_data = {};
}

void VoiceRecordBar::hideFast() {
	hide();
	_lock->hide();
	_level->hide();
	[[maybe_unused]] const auto s = takeTTLState();
}

void VoiceRecordBar::stopRecording(StopType type, bool ttlBeforeHide) {
	using namespace ::Media::Capture;
	if (type == StopType::Cancel) {
		if (_videoRecorder) {
			_videoRecorder->hide();
		}
		instance()->stop(crl::guard(this, [=](Result &&data) {
			_cancelRequests.fire({});
		}));
	} else if (type == StopType::Listen) {
		if (const auto recorder = _videoRecorder.get()) {
			const auto weak = base::make_weak(recorder);
			recorder->pause([=](Ui::RoundVideoResult data) {
				crl::on_main(weak, [=, data = std::move(data)]() mutable {
					window()->raise();
					window()->activateWindow();

					_paused = true;
					_data = std::move(data);
					_listen = std::make_unique<ListenWrap>(
						this,
						_st,
						&_show->session(),
						&_data,
						_cancelFont);
					_listenChanges.fire({});

					using SilentPreview = ::Media::Streaming::RoundPreview;
					recorder->showPreview(
						std::make_shared<SilentPreview>(
							_data.content,
							recorder->previewSize()),
						_listen->videoPreview());
				});
			});
			instance()->pause(true);
		} else {
			instance()->pause(true, crl::guard(this, [=](Result &&data) {
				if (data.bytes.isEmpty()) {
					// Close everything.
					stop(false);
					return;
				}
				_paused = true;
				_data = Ui::RoundVideoResult{
					.content = std::move(data.bytes),
					.waveform = std::move(data.waveform),
					.duration = data.duration,
				};

				window()->raise();
				window()->activateWindow();
				_listen = std::make_unique<ListenWrap>(
					this,
					_st,
					&_show->session(),
					&_data,
					_cancelFont);
				_listenChanges.fire({});
			}));
		}
	} else if (type == StopType::Send) {
		if (_videoRecorder) {
			const auto weak = Ui::MakeWeak(this);
			_videoRecorder->hide([=](Ui::RoundVideoResult data) {
				crl::on_main([=, data = std::move(data)]() mutable {
					if (weak) {
						window()->raise();
						window()->activateWindow();
						const auto options = Api::SendOptions{
							.ttlSeconds = (ttlBeforeHide
								? std::numeric_limits<int>::max()
								: 0),
						};
						_sendVoiceRequests.fire({
							.bytes = data.content,
							//.waveform = {},
							.duration = data.duration,
							.options = options,
							.video = true,
						});
					}
				});
			});
		}
		instance()->stop(crl::guard(this, [=](Result &&data) {
			if (data.bytes.isEmpty()) {
				// Close everything.
				stop(false);
				return;
			}
			_data = Ui::RoundVideoResult{
				.content = std::move(data.bytes),
				.waveform = std::move(data.waveform),
				.duration = data.duration,
			};

			window()->raise();
			window()->activateWindow();
			const auto options = Api::SendOptions{
				.ttlSeconds = (ttlBeforeHide
					? std::numeric_limits<int>::max()
					: 0),
			};
			_sendVoiceRequests.fire({
				.bytes = _data.content,
				.waveform = _data.waveform,
				.duration = _data.duration,
				.options = options,
			});
		}));
	}
}

void VoiceRecordBar::drawDuration(QPainter &p) {
	const auto duration = FormatVoiceDuration(_recordingSamples);
	p.setFont(_cancelFont);
	p.setPen(_st.durationFg);

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

void VoiceRecordBar::drawRedCircle(QPainter &p) {
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::historyRecordVoiceFgInactive);

	const auto opacity = p.opacity();
	p.setOpacity(opacity * (1. - _redCircleProgress));
	const int radii = st::historyRecordSignalRadius * showAnimationRatio();
	const auto center = _redCircleRect.center() + QPoint(1, 1);
	p.drawEllipse(center, radii, radii);
	p.setOpacity(opacity);
}

void VoiceRecordBar::drawMessage(QPainter &p, float64 recordActive) {
	p.setPen(anim::pen(_st.cancel, _st.cancelActive, 1. - recordActive));

	const auto opacity = p.opacity();
	p.setOpacity(opacity * (1. - _lock->lockToStopProgress()));

	_message.draw(p, {
		.position = _messageRect.topLeft(),
		.outerWidth = _messageRect.width(),
		.availableWidth = _messageRect.width(),
		.align = style::al_center,
	});

	p.setOpacity(opacity);
}

void VoiceRecordBar::requestToSendWithOptions(Api::SendOptions options) {
	if (isListenState()) {
		if (takeTTLState()) {
			options.ttlSeconds = std::numeric_limits<int>::max();
		}
		_sendVoiceRequests.fire({
			.bytes = _data.content,
			.waveform = _data.waveform,
			.duration = _data.duration,
			.options = options,
			.video = !_data.minithumbs.isNull(),
		});
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
	return _recording.current() && !_paused.current();
}

bool VoiceRecordBar::isRecordingLocked() const {
	return isRecording() && _lock->isLocked();
}

bool VoiceRecordBar::isActive() const {
	return isRecording() || isListenState();
}

void VoiceRecordBar::hideAnimated() {
	if (isHidden()) {
		return;
	}
	_lockShowing = false;
	visibilityAnimate(false, [=] {
		hideFast();
		stopRecording(StopType::Cancel);
	});
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
	return rpl::merge(
		::Media::Capture::instance()->startedChanges(
		) | rpl::filter([=] {
			// Perhaps a voice is recording from another place.
			return !isActive();
		}) | rpl::to_empty,
		_listenChanges.events());
}

rpl::producer<> VoiceRecordBar::recordingTipRequests() const {
	return _recordingTipRequests.events();
}

auto VoiceRecordBar::errors() const -> rpl::producer<Error> {
	return _errors.events();
}

bool VoiceRecordBar::isLockPresent() const {
	return _lockShowing.current();
}

bool VoiceRecordBar::isListenState() const {
	return _listen != nullptr;
}

bool VoiceRecordBar::isTypeRecord() const {
	return (_send->type() == Ui::SendButton::Type::Record)
		|| (_send->type() == Ui::SendButton::Type::Round);
}

bool VoiceRecordBar::isRecordingByAnotherBar() const {
	return !isRecording() && ::Media::Capture::instance()->started();
}

bool VoiceRecordBar::isTTLButtonShown() const {
	return _ttlButton && !_ttlButton->isHidden();
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
	const auto value = _showListenAnimation.value(_listen ? 1. : 0.);
	if (_paused.current()) {
		return value * value;
	}
	return value;
}

void VoiceRecordBar::computeAndSetLockProgress(QPoint globalPos) {
	const auto localPos = mapFromGlobal(globalPos);
	const auto lower = _lock->height();
	const auto higher = 0;
	_lock->requestPaintProgress(Progress(localPos.y(), higher - lower));
}

bool VoiceRecordBar::peekTTLState() const {
	return _ttlButton && !_ttlButton->isDisabled();
}

bool VoiceRecordBar::takeTTLState() const {
	if (!_ttlButton) {
		return false;
	}
	const auto hasTtl = !_ttlButton->isDisabled();
	_ttlButton->clearState();
	return hasTtl;
}

void VoiceRecordBar::orderControls() {
	stackUnder(_send.get());
	_lock->raise();
	_level->raise();
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
	if (!isActive() || _showAnimation.animating()) {
		return;
	}
	auto sure = [=, callback = std::move(callback)](Fn<void()> &&close) {
		if (animated == anim::type::instant) {
			hideFast();
			stopRecording(StopType::Cancel);
		} else {
			hideAnimated();
		}
		close();
		_warningShown = false;
		if (callback) {
			callback();
		}
	};
	_show->showBox(Ui::MakeConfirmBox({
		.text = (isListenState()
			? (_recordingVideo
				? tr::lng_record_listen_cancel_sure_round
				: tr::lng_record_listen_cancel_sure)
			: (_recordingVideo
				? tr::lng_record_lock_cancel_sure_round
				: tr::lng_record_lock_cancel_sure))(),
		.confirmed = std::move(sure),
		.confirmText = tr::lng_record_lock_discard(),
		.confirmStyle = &st::attentionBoxButton,
	}));
	_warningShown = true;
}

bool VoiceRecordBar::createVideoRecorder() {
	if (_videoRecorder) {
		return true;
	}
	const auto hiding = [=](not_null<Ui::RoundVideoRecorder*> which) {
		if (_videoRecorder.get() == which) {
			_videoHiding.push_back(base::take(_videoRecorder));
		}
	};
	const auto hidden = [=](not_null<Ui::RoundVideoRecorder*> which) {
		if (_videoRecorder.get() == which) {
			_videoRecorder = nullptr;
		}
		_videoHiding.erase(
			ranges::remove(
				_videoHiding,
				which.get(),
				&std::unique_ptr<Ui::RoundVideoRecorder>::get),
			end(_videoHiding));
	};
	auto capturer = Core::App().calls().getVideoCapture();
	auto track = std::make_shared<Webrtc::VideoTrack>(
		Webrtc::VideoState::Active);
	capturer->setOutput(track->sink());
	capturer->setPreferredAspectRatio(1.);
	_videoCapturerLifetime = track->stateValue(
	) | rpl::start_with_next([=](Webrtc::VideoState state) {
		capturer->setState((state == Webrtc::VideoState::Active)
			? tgcalls::VideoState::Active
			: tgcalls::VideoState::Inactive);
	});
	_videoRecorder = std::make_unique<Ui::RoundVideoRecorder>(
		Ui::RoundVideoRecorderDescriptor{
			.container = _outerContainer,
			.hiding = hiding,
			.hidden = hidden,
			.capturer = std::move(capturer),
			.track = std::move(track),
			.placeholder = _show->session().local().readRoundPlaceholder(),
		});
	_videoRecorder->placeholderUpdates(
	) | rpl::start_with_next([=](QImage &&placeholder) {
		_show->session().local().writeRoundPlaceholder(placeholder);
	}, _videoCapturerLifetime);

	return true;
}

} // namespace HistoryView::Controls
