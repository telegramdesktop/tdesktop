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
#include "media/audio/media_audio_edit.h"
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
#include "ui/widgets/fields/input_field.h" // ShouldSubmit.
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

constexpr auto kPrecision = 10;

constexpr auto kLockArcAngle = 15.;

constexpr auto kHideWaveformBgOffset = 50;
constexpr auto kTrimPlaybackEpsilon = 0.0001;

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
	std::move(repaints) | rpl::on_next([=] {
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

[[nodiscard]] QString FormatTrimDuration(crl::time duration) {
	auto result = Ui::FormatDurationText(duration / 1000);
	if ((result.size() == 5)
		&& (result[0] == QChar('0'))
		&& (result[2] == QChar(':'))) {
		result.remove(0, 1);
	}
	return result;
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

[[nodiscard]] VoiceWaveform ResampleWaveformToRange(
		const VoiceWaveform &source,
		float64 left,
		float64 right) {
	if (source.isEmpty() || (source[0] < 0)) {
		return {};
	}
	const auto size = int(source.size());
	if (size <= 0) {
		return {};
	}
	left = std::clamp(left, 0., 1.);
	right = std::clamp(right, left, 1.);
	if ((right - left) <= 0.) {
		return {};
	}
	const auto begin = left * size;
	const auto end = right * size;
	const auto range = end - begin;
	if (range <= 0.) {
		return {};
	}
	auto result = VoiceWaveform(size, 0);
	for (auto i = 0; i != size; ++i) {
		const auto segmentStart = begin + (range * i) / size;
		const auto segmentEnd = begin + (range * (i + 1)) / size;
		const auto from = std::clamp(
			int(std::floor(segmentStart)),
			0,
			size - 1);
		const auto till = std::clamp(
			int(std::ceil(segmentEnd)) - 1,
			from,
			size - 1);
		auto peak = uchar(0);
		for (auto j = from; j <= till; ++j) {
			peak = std::max(peak, uchar(source[j]));
		}
		result[i] = char(peak);
	}
	return result;
}

[[nodiscard]] VoiceWaveform ResampleWaveformToSize(
		const VoiceWaveform &source,
		int targetSize) {
	if (source.isEmpty() || (source[0] < 0) || (targetSize <= 0)) {
		return {};
	}
	const auto sourceSize = int(source.size());
	if (sourceSize <= 0) {
		return {};
	}
	if (sourceSize == targetSize) {
		return source;
	}
	auto result = VoiceWaveform(targetSize, 0);
	for (auto i = 0; i != targetSize; ++i) {
		const auto segmentStart = (float64(sourceSize) * i) / targetSize;
		const auto segmentEnd = (float64(sourceSize) * (i + 1)) / targetSize;
		const auto from = std::clamp(
			int(std::floor(segmentStart)),
			0,
			sourceSize - 1);
		const auto till = std::clamp(
			int(std::ceil(segmentEnd)) - 1,
			from,
			sourceSize - 1);
		auto peak = uchar(0);
		for (auto j = from; j <= till; ++j) {
			peak = std::max(peak, uchar(source[j]));
		}
		result[i] = char(peak);
	}
	return result;
}

[[nodiscard]] VoiceWaveform MergeWaveformsByDuration(
		const VoiceWaveform &first,
		crl::time firstDuration,
		const VoiceWaveform &second,
		crl::time secondDuration) {
	const auto totalDuration = firstDuration + secondDuration;
	if (totalDuration <= 0) {
		return {};
	}
	const auto targetSize = int(::Media::Player::kWaveformSamplesCount);
	if (targetSize <= 0) {
		return {};
	}
	auto firstSize = int(
		((firstDuration * targetSize) + (totalDuration / 2))
		/ totalDuration);
	if ((firstDuration > 0) && (secondDuration > 0)) {
		firstSize = std::clamp(firstSize, 1, targetSize - 1);
	} else {
		firstSize = std::clamp(firstSize, 0, targetSize);
	}
	const auto secondSize = targetSize - firstSize;
	auto result = VoiceWaveform();
	result.reserve(targetSize);
	if (firstSize > 0) {
		const auto part = ResampleWaveformToSize(first, firstSize);
		if (part.isEmpty()) {
			return {};
		}
		for (const auto value : part) {
			result.push_back(value);
		}
	}
	if (secondSize > 0) {
		const auto part = ResampleWaveformToSize(second, secondSize);
		if (part.isEmpty()) {
			return {};
		}
		for (const auto value : part) {
			result.push_back(value);
		}
	}
	return (int(result.size()) == targetSize) ? result : VoiceWaveform();
}

[[nodiscard]] Ui::RoundVideoResult ToRoundVideoResult(
		::Media::Capture::Result &&data) {
	return Ui::RoundVideoResult{
		.content = std::move(data.bytes),
		.waveform = std::move(data.waveform),
		.duration = data.duration,
	};
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

	Ui::RpWidget::shownValue() | rpl::on_next([=](bool shown) {
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
					tr::rich),
			tr::lng_record_once_first_tooltip(
				tr::rich));
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
		) | rpl::on_next([=](const QRect &r) {
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
						- st::historyRecordTooltipSkip
						+ st::historyRecordTooltip.padding.top());
			});
		}, _tooltip->lifetime());
		_tooltip->show();
		if (!Core::App().settings().ttlVoiceClickTooltipHidden()) {
			clicks(
			) | rpl::take(1) | rpl::on_next([=] {
				Core::App().settings().setTtlVoiceClickTooltipHidden(true);
			}, _tooltip->lifetime());
			_tooltip->toggleAnimated(true);
		} else {
			_tooltip->toggleFast(false);
		}

		clicks(
		) | rpl::on_next([=] {
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
		) | rpl::on_next([=](bool toHide) {
			const auto isFirstTooltip
				= !Core::App().settings().ttlVoiceClickTooltipHidden();
			if (isFirstTooltip || toHide) {
				_tooltip->toggleAnimated(!toHide);
			}
		}, _tooltip->lifetime());
	}, lifetime());

	paintRequest(
	) | rpl::on_next([=](const QRect &clip) {
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
	setAccessibleName((recordingVideo
		? tr::lng_in_dlg_video_message_ttl
		: tr::lng_in_dlg_voice_message_ttl)(tr::now));
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
		std::shared_ptr<Ui::SendButton> send,
		not_null<Main::Session*> session,
		not_null<Ui::RoundVideoResult*> data,
		bool allowTrim,
		const style::font &font);

	void requestPaintProgress(float64 progress);
	void prepareForSendAnimation();
	[[nodiscard]] rpl::producer<> stopRequests() const;
	void applyTrimBeforeSend();

	void playPause();
	[[nodiscard]] std::shared_ptr<Ui::DynamicImage> videoPreview();

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	struct TrimGeometry {
		QRect frame;
		QRect leftHandle;
		QRect rightHandle;
	};

	struct TrimRange {
		crl::time from = 0;
		crl::time till = 0;
	};

	struct TrimBoundaries {
		float64 left = 0.;
		float64 right = 1.;
	};

	void init();
	void initPlayButton();
	void initPlayProgress();
	void applyTrimSelection(bool resetSelection);
	void updateControlGeometry();
	void updateTrimGeometry();
	[[nodiscard]] TrimGeometry computeTrimGeometry(
		const QRect &trimRect) const;
	void updateDurationText();

	[[nodiscard]] bool isInPlayer(
		const ::Media::Player::TrackState &state) const;
	[[nodiscard]] bool isInPlayer() const;
	[[nodiscard]] bool canTrim() const;
	[[nodiscard]] float64 trimProgressFromPosition(int x) const;
	[[nodiscard]] float64 minimumTrimProgress() const;
	[[nodiscard]] float64 minimumControlTrimProgress() const;
	[[nodiscard]] crl::time selectedDuration() const;
	[[nodiscard]] std::optional<TrimRange> selectedTrimRange() const;
	[[nodiscard]] TrimBoundaries selectedTrimBoundaries() const;

	[[nodiscard]] int computeTopMargin(int height) const;
	[[nodiscard]] QRect computeWaveformRect(const QRect &centerRect) const;

	const not_null<Ui::RpWidget*> _parent;

	const style::RecordBar &_st;
	const std::shared_ptr<Ui::SendButton> _send;
	const not_null<Main::Session*> _session;
	const not_null<DocumentData*> _document;
	const std::unique_ptr<VoiceData> _voiceData;
	const std::shared_ptr<Data::DocumentMedia> _mediaView;
	const not_null<Ui::RoundVideoResult*> _data;
	const bool _allowTrim = false;
	const base::unique_qptr<Ui::IconButton> _delete;
	const style::font &_durationFont;
	QString _duration;
	int _durationWidth = 0;
	const style::MediaPlayerButton &_playPauseSt;
	const base::unique_qptr<Ui::AbstractButton> _playPauseButton;
	const QColor _activeWaveformBar;
	const QColor _inactiveWaveformBar;

	bool _isShowAnimation = true;

	QRect _waveformBgRect;
	QRect _waveformBgFinalCenterRect;
	QRect _waveformFgRect;
	QRect _controlRect;
	bool _controlHasDuration = true;
	QRect _trimFrameRect;
	QRect _trimLeftHandleRect;
	QRect _trimRightHandleRect;
	float64 _trimLeftProgress = 0.;
	float64 _trimRightProgress = 1.;

	::Media::Player::PlayButtonLayout _playPause;

	anim::value _playProgress;

	rpl::variable<float64> _showProgress = 0.;
	rpl::event_stream<> _videoRepaints;
	QImage _sendAnimationCache;
	bool _useSendAnimationCache = false;
	bool _playPauseHiddenForSendAnimation = false;

	rpl::lifetime _lifetime;

};

ListenWrap::ListenWrap(
	not_null<Ui::RpWidget*> parent,
	const style::RecordBar &st,
	std::shared_ptr<Ui::SendButton> send,
	not_null<Main::Session*> session,
	not_null<Ui::RoundVideoResult*> data,
	bool allowTrim,
	const style::font &font)
: _parent(parent)
, _st(st)
, _send(send)
, _session(session)
, _document(DummyDocument(&session->data()))
, _voiceData(ProcessCaptureResult(data->waveform))
, _mediaView(_document->createMediaView())
, _data(data)
, _allowTrim(allowTrim)
, _delete(base::make_unique_q<Ui::IconButton>(parent, _st.remove))
, _durationFont(font)
, _duration(FormatTrimDuration(_data->duration))
, _durationWidth(_durationFont->width(_duration))
, _playPauseSt(st::mediaPlayerButton)
, _playPauseButton(base::make_unique_q<Ui::AbstractButton>(parent))
, _activeWaveformBar(st::historyRecordVoiceFgActiveIcon->c)
, _inactiveWaveformBar(
	anim::with_alpha(
		_activeWaveformBar,
		st::historyRecordWaveformInactiveAlpha))
, _playPause(_playPauseSt, [=] { _playPauseButton->update(); }) {
	_delete->setAccessibleName(tr::lng_record_lock_delete(tr::now));
	init();
}

void ListenWrap::init() {
	auto deleteShow = _showProgress.value(
	) | rpl::map([](auto value) {
		return value == 1.;
	}) | rpl::distinct_until_changed();
	_delete->showOn(std::move(deleteShow));

	rpl::combine(
		_parent->sizeValue(),
		_send->widthValue()
	) | rpl::on_next([=](QSize size, int send) {
		_waveformBgRect = QRect({ 0, 0 }, size)
			.marginsRemoved(st::historyRecordWaveformBgMargins);
		{
			const auto left = _st.remove.width;
			const auto right = send;
			_waveformBgFinalCenterRect = _waveformBgRect.marginsRemoved(
				style::margins(left, 0, right, 0));
		}
		_waveformFgRect = computeWaveformRect(_waveformBgFinalCenterRect);
		updateTrimGeometry();
		updateControlGeometry();
	}, _lifetime);

	_parent->paintRequest(
	) | rpl::on_next([=](const QRect &clip) {
		auto p = QPainter(_parent);
		auto hq = PainterHighQualityEnabler(p);
		const auto progress = _showProgress.current();
		const auto useSendAnimationCache = _useSendAnimationCache
			&& !_sendAnimationCache.isNull();
		p.setOpacity(progress);
		const auto &remove = _st.remove;
		if (progress > 0. && progress < 1. && !useSendAnimationCache) {
			remove.icon.paint(p, remove.iconPosition, _parent->width());
		}

		{
			const auto hideOffset = _isShowAnimation
				? 0
				: anim::interpolate(kHideWaveformBgOffset, 0, progress);
			const auto deleteIconLeft = remove.iconPosition.x();
			const auto bgRectRight = anim::interpolate(
				deleteIconLeft,
				_send->width(),
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
			const auto radius = st::historyRecordWaveformBgRadius;
			const auto bgCenterRect = bgRect.marginsRemoved(
				style::margins(radius, 0, radius, 0));
			if (useSendAnimationCache) {
				p.save();
				p.setClipRect(bgRect);
				p.drawImage(bgRect.topLeft(), _sendAnimationCache);
				p.restore();
				return;
			}
			const auto trimGeometry = (progress == 1.)
				? TrimGeometry{
					.frame = _trimFrameRect,
					.leftHandle = _trimLeftHandleRect,
					.rightHandle = _trimRightHandleRect,
				}
				: computeTrimGeometry(bgCenterRect);
			const auto &trimFrameRect = trimGeometry.frame;
			const auto &trimLeftHandleRect = trimGeometry.leftHandle;
			const auto &trimRightHandleRect = trimGeometry.rightHandle;

			if (!_isShowAnimation) {
				p.setOpacity(progress);
			} else {
				p.fillRect(bgRect, _st.bg);
			}
			p.setPen(Qt::NoPen);
			p.setBrush(anim::with_alpha(
				_st.cancelActive->c,
				st::historyRecordWaveformOutsideAlpha));
			p.drawRoundedRect(bgRect, radius, radius);
			if (canTrim() && !trimFrameRect.isEmpty()) {
				const auto activeBgRect = trimFrameRect.intersected(bgRect);
				auto clipPath = QPainterPath();
				clipPath.addRoundedRect(bgRect, radius, radius);
				p.save();
				p.setClipPath(clipPath);
				if (activeBgRect.isEmpty()) {
					p.fillRect(bgRect, _st.cancelActive);
				} else {
					if (activeBgRect.x() > bgRect.x()) {
						p.fillRect(
							QRect(
								bgRect.x(),
								bgRect.y(),
								activeBgRect.x() - bgRect.x(),
								bgRect.height()),
							_st.cancelActive);
					}
					if (rect::right(activeBgRect) < rect::right(bgRect)) {
						p.fillRect(
							QRect(
								rect::right(activeBgRect) + 1,
								bgRect.y(),
								rect::right(bgRect)
									- rect::right(activeBgRect),
								bgRect.height()),
							_st.cancelActive);
					}
				}
				p.restore();
			}

			// Waveform paint.
			const auto waveformRect = (progress == 1.)
				? _waveformFgRect
				: computeWaveformRect(bgCenterRect);
			if (!waveformRect.isEmpty()) {
				const auto playProgress = _playProgress.current();
				const auto paintWaveform = [&](
						QRect rect,
						float64 opacity,
						const QColor &activeBar,
						const QColor &inactiveBar) {
					if (rect.isEmpty() || (opacity <= 0.)) {
						return;
					}
					p.save();
					p.setClipRect(rect);
					p.setOpacity(p.opacity() * opacity);
					if (_data->minithumbs.isNull()) {
						p.translate(waveformRect.topLeft());
						PaintWaveform(
							p,
							_voiceData.get(),
							waveformRect.width(),
							activeBar,
							inactiveBar,
							playProgress);
					} else {
						FillWithMinithumbs(
							p,
							_data,
							waveformRect,
							playProgress);
					}
					p.restore();
				};
				const auto activeWaveformRect = trimFrameRect.intersected(
					waveformRect);
				if (canTrim() && !activeWaveformRect.isEmpty()) {
					const auto outsideOpacity = std::clamp(
						st::historyRecordWaveformOutsideAlpha,
						0.,
						1.);
					paintWaveform(
						activeWaveformRect,
						1.,
						_activeWaveformBar,
						_inactiveWaveformBar);
					paintWaveform(
						QRect(
							waveformRect.x(),
							waveformRect.y(),
							std::max(
								0,
								activeWaveformRect.x() - waveformRect.x()),
							waveformRect.height()),
						outsideOpacity,
						_inactiveWaveformBar,
						_inactiveWaveformBar);
					paintWaveform(
						QRect(
							rect::right(activeWaveformRect) + 1,
							waveformRect.y(),
							std::max(
								0,
								rect::right(waveformRect)
									- rect::right(activeWaveformRect)),
							waveformRect.height()),
						outsideOpacity,
						_inactiveWaveformBar,
						_inactiveWaveformBar);
				} else {
					paintWaveform(
						waveformRect,
						1.,
						_activeWaveformBar,
						_inactiveWaveformBar);
				}
				if (canTrim() && !trimFrameRect.isEmpty()) {
					p.setPen(Qt::NoPen);
					const auto inner = st::historyRecordTrimHandleInnerSize;
					const auto drawInner = [&](const QRect &handle) {
						const auto width = std::min(
							inner.width(),
							handle.width());
						const auto height = std::min(
							inner.height(),
							handle.height());
						const auto x = handle.x()
							+ (handle.width() - width) / 2;
						const auto y = handle.y()
							+ (handle.height() - height) / 2;
						p.drawRoundedRect(
							QRect(x, y, width, height),
							width / 2.,
							width / 2.);
					};
					p.setBrush(_activeWaveformBar);
					drawInner(trimLeftHandleRect);
					drawInner(trimRightHandleRect);
					p.setBrush(_st.bg);
					const auto lineTop = trimLeftHandleRect.y();
					const auto lineHeight = trimLeftHandleRect.height();
					const auto leftLineX = trimFrameRect.x();
					const auto rightLineX = rect::right(trimFrameRect);
					if (lineHeight > 0) {
						p.fillRect(leftLineX, lineTop, 1, lineHeight, _st.bg);
						if (rightLineX != leftLineX) {
							p.fillRect(
								rightLineX,
								lineTop,
								1,
								lineHeight,
								_st.bg);
						}
					}
				}

				if (!_controlRect.isEmpty()) {
					p.setPen(Qt::NoPen);
					p.setBrush(_st.cancelActive);
					p.drawRoundedRect(
						_controlRect,
						_controlRect.height() / 2.,
						_controlRect.height() / 2.);

					if (_controlHasDuration) {
						p.setFont(_durationFont);
						p.setPen(st::historyRecordVoiceFgActiveIcon);
						const auto ascent = _durationFont->ascent;
						const auto left = rect::right(_playPauseButton)
							/*+ st::historyRecordCenterControlTextSkip*/;
						const auto top = _controlRect.y()
							+ (_controlRect.height() - ascent) / 2;
						p.drawText(
							QRect(left, top, _durationWidth, ascent),
							style::al_left,
							_duration);
					}
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
	updateControlGeometry();
	_playPauseButton->show();
	_playPauseButton->setAccessibleName(tr::lng_record_lock_play(tr::now));

	_playPauseButton->paintRequest(
	) | rpl::on_next([=](const QRect &clip) {
		auto p = QPainter(_playPauseButton);
		const auto size = _playPauseButton->size();

		const auto progress = _showProgress.current()
			* st::historyRecordCenterControlIconScale;
		p.translate(size.width() / 2, size.height() / 2);
		p.scale(progress, progress);
		p.translate(-play.width() / 2, -play.height() / 2);
		_playPause.paint(p, st::historyRecordVoiceFgActiveIcon);
	}, _playPauseButton->lifetime());

	_playPauseButton->setClickedCallback([=] {
		playPause();
	});

	const auto showPause = _lifetime.make_state<rpl::variable<bool>>(false);
	showPause->changes(
	) | rpl::on_next([=](bool pause) {
		_playPauseButton->setAccessibleName(pause
			? tr::lng_record_lock_pause(tr::now)
			: tr::lng_record_lock_play(tr::now));
		_playPause.setState(pause
			? PlayButtonLayout::State::Pause
			: PlayButtonLayout::State::Play);
	}, _lifetime);

	instance()->updatedNotifier(
	) | rpl::on_next([=](const State &state) {
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
	) | rpl::on_next([=] {
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
	enum class DragMode {
		None,
		Seek,
		TrimLeft,
		TrimRight,
	};

	const auto animation = _lifetime.make_state<Ui::Animations::Basic>();
	const auto dragMode = _lifetime.make_state<DragMode>(DragMode::None);
	const auto trimPlaybackSeekInProgress = _lifetime.make_state<bool>(false);
	const auto &voice = AudioMsgId::Type::Voice;
	const auto stopPlayingPreviewOnTrim = [=] {
		const auto state = instance()->getState(voice);
		if (isInPlayer(state) && ShowPauseIcon(state.state)) {
			instance()->stop(voice, true);
		}
	};
	const auto canSeekAt = [=](const QPoint &p) {
		return isInPlayer()
			&& _waveformFgRect.contains(p)
			&& (_controlRect.isEmpty() || !_controlRect.contains(p));
	};

	const auto updateCursor = [=](const QPoint &p) {
		if (canTrim()
			&& (_trimLeftHandleRect.contains(p)
				|| _trimRightHandleRect.contains(p))) {
			_parent->setCursor(style::cur_sizehor);
		} else if (canSeekAt(p)) {
			_parent->setCursor(style::cur_pointer);
		} else {
			_parent->setCursor(style::cur_default);
		}
	};
	_parent->setMouseTracking(canTrim());

	rpl::merge(
		instance()->startsPlay(voice) | rpl::map_to(true),
		instance()->stops(voice) | rpl::map_to(false)
	) | rpl::on_next([=](bool play) {
		_parent->setMouseTracking(canTrim() || (isInPlayer() && play));
		updateCursor(_parent->mapFromGlobal(QCursor::pos()));
	}, _lifetime);

	instance()->updatedNotifier(
	) | rpl::on_next([=](const State &state) {
		if (*trimPlaybackSeekInProgress) {
			return;
		}
		if (!isInPlayer(state)) {
			return;
		} else if (!_isShowAnimation && (_showProgress.current() < 1.)) {
			return;
		}
		const auto [leftBoundary, rightBoundary] = selectedTrimBoundaries();
		const auto playbackTrimmed = canTrim()
			&& ((leftBoundary > kTrimPlaybackEpsilon)
				|| (rightBoundary < (1. - kTrimPlaybackEpsilon)));
		const auto length = int(state.length);
		const auto position = std::min(state.position, int64(length));
		auto progress = length
			? Progress(position, length)
			: 0.;
		if (playbackTrimmed && length > 0) {
			if (ShowPauseIcon(state.state)
				&& (progress < (leftBoundary - kTrimPlaybackEpsilon))) {
				*trimPlaybackSeekInProgress = true;
				instance()->startSeeking(voice);
				instance()->finishSeeking(voice, leftBoundary);
				*trimPlaybackSeekInProgress = false;
				return;
			}
			if (ShowPauseIcon(state.state)
				&& (progress >= (rightBoundary - kTrimPlaybackEpsilon))) {
				instance()->stop(voice, true);
				_playProgress = anim::value(leftBoundary, leftBoundary);
				_parent->update(_waveformFgRect);
				return;
			}
			progress = std::clamp(progress, leftBoundary, rightBoundary);
		}
		if (IsStopped(state.state)) {
			_playProgress = playbackTrimmed
				? anim::value(leftBoundary, leftBoundary)
				: anim::value();
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

	_parent->events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return (e->type() == QEvent::MouseMove
			|| e->type() == QEvent::MouseButtonPress
			|| e->type() == QEvent::MouseButtonRelease);
	}) | rpl::on_next([=](not_null<QEvent*> e) {
		if (!isInPlayer() && !canTrim()) {
			return;
		}

		const auto type = e->type();
		const auto &pos = static_cast<QMouseEvent*>(e.get())->pos();
		if ((type == QEvent::MouseMove) && (*dragMode == DragMode::None)) {
			updateCursor(pos);
		}

		if (type == QEvent::MouseButtonPress) {
			if (canTrim() && _trimLeftHandleRect.contains(pos)) {
				stopPlayingPreviewOnTrim();
				*dragMode = DragMode::TrimLeft;
				_parent->setCursor(style::cur_sizehor);
				return;
			} else if (canTrim() && _trimRightHandleRect.contains(pos)) {
				stopPlayingPreviewOnTrim();
				*dragMode = DragMode::TrimRight;
				_parent->setCursor(style::cur_sizehor);
				return;
			}
			if (canSeekAt(pos)) {
				instance()->startSeeking(voice);
				*dragMode = DragMode::Seek;
			}
			return;
		}

		const auto isRelease = (type == QEvent::MouseButtonRelease);
		if (*dragMode == DragMode::Seek) {
			if (isRelease || (type == QEvent::MouseMove)) {
				auto progress = trimProgressFromPosition(pos.x());
				if (canTrim()) {
					const auto [left, right] = selectedTrimBoundaries();
					progress = std::clamp(progress, left, right);
				}
				_playProgress = anim::value(progress, progress);
				_parent->update(_waveformFgRect);
				if (isRelease) {
					instance()->finishSeeking(voice, progress);
					*dragMode = DragMode::None;
					updateCursor(pos);
				}
			}
			return;
		}

		if ((*dragMode == DragMode::TrimLeft)
			|| (*dragMode == DragMode::TrimRight)) {
			if (isRelease || (type == QEvent::MouseMove)) {
				const auto progress = trimProgressFromPosition(pos.x());
				const auto minDelta = minimumTrimProgress();
				if (*dragMode == DragMode::TrimLeft) {
					_trimLeftProgress = std::clamp(
						progress,
						0.,
						std::max(0., _trimRightProgress - minDelta));
				} else {
					_trimRightProgress = std::clamp(
						progress,
						std::min(1., _trimLeftProgress + minDelta),
						1.);
				}
				updateDurationText();
				updateTrimGeometry();
				updateControlGeometry();
				_parent->update();
				if (isRelease) {
					*dragMode = DragMode::None;
					updateCursor(pos);
				}
			}
			return;
		}

		if (isRelease) {
			updateCursor(pos);
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

bool ListenWrap::canTrim() const {
	return _allowTrim;
}

float64 ListenWrap::trimProgressFromPosition(int x) const {
	const auto width = _waveformBgFinalCenterRect.width() - 1;
	if (width <= 0) {
		return 0.;
	}
	return std::clamp(
		float64(x - _waveformBgFinalCenterRect.x()) / width,
		0.,
		1.);
}

float64 ListenWrap::minimumTrimProgress() const {
	const auto samplesProgress = [&] {
		const auto samples = int((_data->duration
			* ::Media::Player::kDefaultFrequency) / crl::time(1000));
		if (samples <= 0) {
			return 0.;
		}
		return std::clamp(
			float64(kMinSamples) / samples,
			0.,
			1.);
	}();
	return std::max(samplesProgress, minimumControlTrimProgress());
}

float64 ListenWrap::minimumControlTrimProgress() const {
	if (!canTrim() || _waveformBgFinalCenterRect.isEmpty()) {
		return 0.;
	}
	const auto trimRect = _waveformBgFinalCenterRect;
	const auto handleWidth = std::max(
		1,
		std::min(
			st::historyRecordTrimHandleWidth,
			trimRect.width() / 2));
	const auto previewRange = std::max(
		1,
		trimRect.width() - (handleWidth * 2));
	const auto controlHeight = std::min(
		st::historyRecordCenterControlHeight,
		trimRect.height());
	const auto iconWidth = controlHeight;
	const auto minControlWidth = (st::historyRecordCenterControlPadding * 2)
		+ iconWidth
		+ st::historyRecordCenterControlMinimumProgressPadding * 2;
	return std::clamp(float64(minControlWidth) / previewRange, 0., 1.);
}

crl::time ListenWrap::selectedDuration() const {
	if (!canTrim()) {
		return _data->duration;
	}
	if (const auto range = selectedTrimRange()) {
		return std::max(crl::time(0), range->till - range->from);
	}
	return _data->duration;
}

std::optional<ListenWrap::TrimRange> ListenWrap::selectedTrimRange() const {
	if (!canTrim()) {
		return std::nullopt;
	}
	const auto left = std::clamp(_trimLeftProgress, 0., 1.);
	const auto right = std::clamp(_trimRightProgress, left, 1.);
	if ((left <= 0.) && (right >= 1.)) {
		return std::nullopt;
	}
	const auto currentSamples = int((_data->duration
		* ::Media::Player::kDefaultFrequency) / crl::time(1000));
	if (currentSamples <= 0) {
		return std::nullopt;
	}
	const auto fromSamples = base::SafeRound(currentSamples * left);
	const auto tillSamples = base::SafeRound(currentSamples * right);
	if (tillSamples <= fromSamples) {
		return std::nullopt;
	}
	const auto from = (fromSamples * crl::time(1000))
		/ ::Media::Player::kDefaultFrequency;
	const auto till = (tillSamples * crl::time(1000))
		/ ::Media::Player::kDefaultFrequency;
	if (till <= from) {
		return std::nullopt;
	}
	return TrimRange{ .from = crl::time(from), .till = crl::time(till) };
}

ListenWrap::TrimBoundaries ListenWrap::selectedTrimBoundaries() const {
	const auto dur = _data->duration;
	if (const auto range = selectedTrimRange(); range && (dur > 0)) {
		const auto left = std::clamp(float64(range->from) / dur, 0., 1.);
		const auto right = std::clamp(float64(range->till) / dur, left, 1.);
		return { left, right };
	}
	return { 0., 1. };
}

ListenWrap::TrimGeometry ListenWrap::computeTrimGeometry(
		const QRect &trimRect) const {
	auto result = TrimGeometry();
	if (!canTrim() || trimRect.isEmpty()) {
		return result;
	}
	const auto width = trimRect.width();
	if (width <= 0) {
		return result;
	}
	const auto handleWidth = std::max(
		1,
		std::min(
			st::historyRecordTrimHandleWidth,
			width / 2));
	const auto previewRange = std::max(1, width - (handleWidth * 2));
	const auto minBoundary = trimRect.x() + handleWidth;
	const auto maxBoundary = trimRect.right() - handleWidth + 1;
	if (maxBoundary < minBoundary) {
		return result;
	}
	const auto leftProgress = std::clamp(_trimLeftProgress, 0., 1.);
	const auto rightProgress = std::clamp(
		_trimRightProgress,
		leftProgress,
		1.);
	const auto leftBoundary = std::clamp(
		minBoundary + int(base::SafeRound(previewRange * leftProgress)),
		minBoundary,
		maxBoundary);
	const auto rightBoundary = std::clamp(
		minBoundary + int(base::SafeRound(previewRange * rightProgress)),
		leftBoundary,
		maxBoundary);
	result.leftHandle = QRect(
		leftBoundary - handleWidth,
		trimRect.y(),
		handleWidth,
		trimRect.height());
	result.rightHandle = QRect(
		rightBoundary,
		trimRect.y(),
		handleWidth,
		trimRect.height());
	const auto previewLeft = leftBoundary;
	const auto previewRight = std::max(previewLeft, rightBoundary - 1);
	result.frame = QRect(
		QPoint(previewLeft, trimRect.y()),
		QPoint(previewRight, rect::bottom(trimRect)));
	return result;
}

void ListenWrap::updateControlGeometry() {
	const auto availableRect = (canTrim() && !_trimFrameRect.isEmpty())
		? _trimFrameRect
		: _waveformBgFinalCenterRect;
	if (availableRect.isEmpty()) {
		_controlRect = QRect();
		_controlHasDuration = false;
		return;
	}
	const auto controlHeight = std::min(
		st::historyRecordCenterControlHeight,
		availableRect.height());
	const auto iconWidth = controlHeight;
	const auto iconOnlyWidth = (st::historyRecordCenterControlPadding * 2)
		+ iconWidth;
	const auto fullWidth = iconOnlyWidth
		+ st::historyRecordCenterControlTextSkip
		+ _durationWidth;
	const auto skip = st::historyRecordCenterControlMinimumProgressPadding;
	_controlHasDuration = (availableRect.width() - skip * 2 >= fullWidth);
	auto controlWidth = _controlHasDuration ? fullWidth : iconOnlyWidth;
	controlWidth = std::min(controlWidth, availableRect.width());
	if (controlWidth <= 0 || controlHeight <= 0) {
		_controlRect = QRect();
		_controlHasDuration = false;
		return;
	}
	_controlRect = QRect(
		availableRect.x() + (availableRect.width() - controlWidth) / 2,
		availableRect.y() + (availableRect.height() - controlHeight) / 2,
		controlWidth,
		controlHeight);
	_playPauseButton->resize(iconWidth, controlHeight);
	const auto iconLeft = _controlHasDuration
		? (_controlRect.x() + st::historyRecordCenterControlPadding)
		: (_controlRect.x() + (_controlRect.width() - iconWidth) / 2);
	_playPauseButton->moveToLeft(
		iconLeft,
		_controlRect.y());
}

void ListenWrap::updateTrimGeometry() {
	if (!canTrim() || _waveformBgFinalCenterRect.isEmpty()) {
		_trimFrameRect = QRect();
		_trimLeftHandleRect = QRect();
		_trimRightHandleRect = QRect();
		return;
	}
	const auto minDelta = minimumTrimProgress();
	if ((_trimRightProgress - _trimLeftProgress) < minDelta) {
		const auto center = (_trimLeftProgress + _trimRightProgress) / 2.;
		const auto half = minDelta / 2.;
		auto left = center - half;
		auto right = center + half;
		if (left < 0.) {
			right = std::min(1., right - left);
			left = 0.;
		}
		if (right > 1.) {
			left = std::max(0., left - (right - 1.));
			right = 1.;
		}
		_trimLeftProgress = left;
		_trimRightProgress = right;
	}
	const auto geometry = computeTrimGeometry(_waveformBgFinalCenterRect);
	_trimFrameRect = geometry.frame;
	_trimLeftHandleRect = geometry.leftHandle;
	_trimRightHandleRect = geometry.rightHandle;
}

void ListenWrap::applyTrimSelection(bool resetSelection) {
	if (!canTrim()) {
		return;
	}
	const auto range = selectedTrimRange();
	if (!range) {
		return;
	}
	const auto [waveLeft, waveRight] = selectedTrimBoundaries();
	auto waveform = ResampleWaveformToRange(
		_data->waveform,
		waveLeft,
		waveRight);
	const auto from = range->from;
	const auto till = range->till;
	const auto selected = till - from;
	const auto selectedSamples = int((selected
		* ::Media::Player::kDefaultFrequency) / crl::time(1000));
	if (selectedSamples < kMinSamples) {
		return;
	}
	const auto trimmed = ::Media::TrimAudioToRange(_data->content, from, till);
	if (trimmed.content.isEmpty()) {
		return;
	}
	if (isInPlayer()) {
		::Media::Player::instance()->stop(AudioMsgId::Type::Voice, true);
	}
	_data->content = std::move(trimmed.content);
	if (waveform.isEmpty()) {
		waveform = std::move(trimmed.waveform);
	}
	_data->waveform = std::move(waveform);
	_data->duration = trimmed.duration;
	_mediaView->setBytes(_data->content);
	_document->size = _data->content.size();
	_voiceData->waveform = _data->waveform;
	_voiceData->wavemax = _voiceData->waveform.empty()
		? uchar(0)
		: *ranges::max_element(_voiceData->waveform);
	if (resetSelection) {
		_trimLeftProgress = 0.;
		_trimRightProgress = 1.;
	}
	updateDurationText();
	_waveformFgRect = computeWaveformRect(_waveformBgFinalCenterRect);
	updateTrimGeometry();
	updateControlGeometry();
	_playProgress = anim::value();
	_parent->update();
}

void ListenWrap::updateDurationText() {
	_duration = FormatTrimDuration(selectedDuration());
	_durationWidth = _durationFont->width(_duration);
}

void ListenWrap::applyTrimBeforeSend() {
	applyTrimSelection(true);
}

void ListenWrap::prepareForSendAnimation() {
	if (_waveformBgRect.isEmpty()) {
		return;
	}
	const auto cacheRect = _waveformBgRect
		- style::margins(_st.remove.width, 0, _send->width(), 0);
	if (cacheRect.isEmpty()) {
		return;
	}
	const auto deleteVisible = _delete->isVisible();
	if (deleteVisible) {
		_delete->hide();
	}
	_sendAnimationCache = Ui::GrabWidgetToImage(_parent, cacheRect);
	if (deleteVisible) {
		_delete->show();
	}
	_useSendAnimationCache = !_sendAnimationCache.isNull();
	if (_useSendAnimationCache && _playPauseButton->isVisible()) {
		_playPauseButton->hide();
		_playPauseHiddenForSendAnimation = true;
	}
	if (_useSendAnimationCache) {
		_parent->update(cacheRect);
	}
}

void ListenWrap::playPause() {
	::Media::Player::instance()->playPause({ _document, FullMsgId() });
}

QRect ListenWrap::computeWaveformRect(const QRect &centerRect) const {
	const auto top = computeTopMargin(st::msgWaveformMax);
	const auto left = st::historyRecordTrimHandleWidth;
	return centerRect - style::margins(left, top, left, top);
}

int ListenWrap::computeTopMargin(int height) const {
	return (_waveformBgRect.height() - height) / 2;
}

void ListenWrap::requestPaintProgress(float64 progress) {
	_isShowAnimation = (_showProgress.current() < progress);
	if (_isShowAnimation && _useSendAnimationCache) {
		_useSendAnimationCache = false;
		_sendAnimationCache = QImage();
		if (_playPauseHiddenForSendAnimation) {
			_playPauseButton->show();
			_playPauseHiddenForSendAnimation = false;
		}
	}
	if (!_isShowAnimation && (progress < 1.)) {
		const auto value = _playProgress.current();
		_playProgress = anim::value(value, value);
	}
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
	) | rpl::on_next([=](bool shown) {
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
	) | rpl::on_next([=](const QRect &clip) {
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
	setAccessibleName(tr::lng_record_lock(tr::now));
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
	setAccessibleName(tr::lng_record_cancel_recording(tr::now));
	resize(_width, height);
	init();
}

void CancelButton::init() {
	_showProgress.value(
	) | rpl::map(rpl::mappers::_1 > 0.) | rpl::distinct_until_changed(
	) | rpl::on_next([=](bool hasProgress) {
		setVisible(hasProgress);
	}, lifetime());

	paintRequest(
	) | rpl::on_next([=] {
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
	) | rpl::on_next([=] {
		orderControls();
	}, lifetime());

	shownValue(
	) | rpl::on_next([=](bool show) {
		if (!show) {
			finish();
		}
	}, lifetime());

	sizeValue(
	) | rpl::on_next([=](QSize size) {
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
	) | rpl::on_next([=](const QRect &clip) {
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
	) | rpl::on_next([=](bool value) {
		activeAnimate(value);
	}, lifetime());

	_lockShowing.changes(
	) | rpl::on_next([=](bool show) {
		const auto to = show ? 1. : 0.;
		const auto from = show ? 0. : 1.;
		const auto &duration = st::historyRecordLockShowDuration;
		_lock->show();
		auto callback = [=](float64 value) {
			updateLockGeometry();
			if (value == 0. && !show) {
				_lock->hide();
			} else if (value == 1. && show) {
				_lock->requestPaintProgress(calcLockProgress(QCursor::pos()));
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
		) | rpl::on_next([=] {
			stop(true);
		}, _recordingLifetime);

		rpl::single(
			false
		) | rpl::then(
			_level->actives()
		) | rpl::on_next([=](bool enter) {
			_inField = enter;
		}, _recordingLifetime);
	};

	const auto paintShowListenCallback = [=](float64 value) {
		if (_listen) {
			_listen->requestPaintProgress(value);
		}
		_level->requestPaintProgress(1. - value);
		_lock->requestPaintPauseToInputProgress(value);
		update();
	};

	_lock->setClickedCallback([=] {
		if (isListenState()) {
			applyListenTrimForResume();
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
	) | rpl::on_next([=](bool paused) {
		_lock->setAccessibleName(paused
			? tr::lng_record_lock_resume(tr::now)
			: tr::lng_record_lock(tr::now));
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
	) | rpl::on_next([=] {
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
			_level->requestPaintColor(activeAnimationRatio());
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
	}) | rpl::on_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::MouseButtonPress) {
			if (_startRecordingFilter && _startRecordingFilter()) {
				return;
			}
			prepareOnSendPress();
			_startTimer.callOnce(st::universalDuration);
		} else if (e->type() == QEvent::MouseButtonRelease) {
			checkTipRequired();
			_startTimer.cancel();
		}
	}, lifetime());

	_listenChanges.events(
	) | rpl::filter([=] {
		return _listen != nullptr;
	}) | rpl::on_next([=] {
		_listen->stopRequests(
		) | rpl::take(1) | rpl::on_next([=] {
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

void VoiceRecordBar::prepareOnSendPress() {
	_recordingTipRequire = crl::now();
	_recordingVideo = (_send->type() == Ui::SendButton::Type::Round);
	_fullRecord = false;
	_ttlButton = nullptr;
	clearResumeState();
	_lock->setRecordingVideo(_recordingVideo);
}

void VoiceRecordBar::applyListenTrimForResume() {
	const auto beforeDuration = _data.duration;
	const auto beforeSize = _data.content.size();
	_listen->applyTrimBeforeSend();
	_resumeFromTrimmedListen = (_data.duration != beforeDuration)
		|| (_data.content.size() != beforeSize);
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

void VoiceRecordBar::setPauseInsteadSend(bool pauseInsteadSend) {
	_pauseInsteadSend = pauseInsteadSend;
}

void VoiceRecordBar::initLockGeometry() {
	const auto parent = static_cast<Ui::RpWidget*>(parentWidget());
	rpl::merge(
		_lock->heightValue() | rpl::to_empty,
		geometryValue() | rpl::to_empty,
		parent->geometryValue() | rpl::to_empty
	) | rpl::on_next([=] {
		updateLockGeometry();
	}, lifetime());
	parent->geometryValue(
	) | rpl::on_next([=] {
		updateTTLGeometry(TTLAnimationType::RightLeft, 1.);
	}, lifetime());
}

void VoiceRecordBar::initLevelGeometry() {
	rpl::combine(
		_send->geometryValue(),
		geometryValue(),
		static_cast<Ui::RpWidget*>(parentWidget())->geometryValue()
	) | rpl::on_next([=](QRect send, auto, auto) {
		const auto mapped = Ui::MapFrom(
			_outerContainer,
			_send->parentWidget(),
			send);
		const auto center = (send.width() - _level->width()) / 2;
		_level->moveToLeft(mapped.x() + center, mapped.y() + center);
	}, lifetime());
}

void VoiceRecordBar::startRecordingAndLock(bool round) {
	{
		auto sendState = _send->state();
		sendState.type = round
			? Ui::SendButton::Type::Round
			: Ui::SendButton::Type::Record;
		_send->setState(std::move(sendState));
	}
	if (_startRecordingFilter && _startRecordingFilter()) {
		return;
	}
	prepareOnSendPress();

	_lock->show();
	_lock->requestPaintProgress(1.);
	startRecording();
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
			if (_videoRecorder) {
				instance()->pause(false, nullptr);
				_videoRecorder->resume({
					.video = std::move(_data),
				});
				clearResumePrefix();
			} else {
				instance()->pause(false, nullptr);
				if (_resumeFromTrimmedListen && (_pausedRawDuration > 0)) {
					setupResumePrefixFromCurrentData();
					_recordingSamples = _resumePrefixSamples;
				} else {
					clearResumePrefix();
				}
				_resumeFromTrimmedListen = false;
				update(_durationRect);
			}
		} else {
			clearResumePrefix();
			instance()->start(_videoRecorder
				? _videoRecorder->audioChunkProcessor()
				: nullptr);
		}
		instance()->updated(
		) | rpl::on_next_error([=](const Update &update) {
			recordUpdated(update.level, update.samples);
		}, [=] {
			stop(false);
		}, _recordingLifetime);
		if (_videoRecorder) {
			_videoRecorder->updated(
			) | rpl::on_next_error([=](const Update &update) {
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

	struct FloatingState {
		Ui::Animations::Basic animation;
		float64 animationProgress = 0;
		float64 cursorProgress = 0;
		bool lockCapturedByInput = false;
		float64 frameCounter = 0;
		rpl::lifetime lifetime;
	};
	const auto stateOwned
		= _recordingLifetime.make_state<std::unique_ptr<FloatingState>>(
			std::make_unique<FloatingState>());
	const auto state = stateOwned->get();

	_lock->locks() | rpl::on_next([=] {
		stateOwned->reset();
	}, state->lifetime);

	constexpr auto kAnimationThreshold = 0.35;
	const auto calcStateRatio = [=](float64 counter) {
		return (1 - std::cos(std::fmod(counter, 2 * M_PI))) * 0.5;
	};
	state->animation.init([=](crl::time now) {
		if (state->cursorProgress > kAnimationThreshold) {
			state->lockCapturedByInput = true;
		}
		if (state->lockCapturedByInput) {
			if (state->cursorProgress < 0.01) {
				state->lockCapturedByInput = false;
				state->frameCounter = 0;
			} else {
				_lock->requestPaintProgress(state->cursorProgress);
				return;
			}
		}
		const auto progress = anim::interpolateF(
			state->cursorProgress,
			kAnimationThreshold,
			calcStateRatio(state->frameCounter));
		state->frameCounter += 0.01;
		_lock->requestPaintProgress(progress);
	});
	state->animation.start();
	if (hasDuration()) {
		stateOwned->reset();
	}

	_send->events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return (e->type() == QEvent::MouseMove
			|| e->type() == QEvent::MouseButtonRelease)
			&& isTypeRecord()
			&& !_lock->isLocked();
	}) | rpl::on_next([=](not_null<QEvent*> e) {
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
			const auto inputProgress = calcLockProgress(mouse->globalPos());
			if (inputProgress > state->animationProgress) {
				state->cursorProgress = inputProgress;
			}
		} else if (type == QEvent::MouseButtonRelease) {
			checkTipRequired();
			stop(_inField.current());
		}
	}, _recordingLifetime);

	_listenChanges.events_starting_with(
		rpl::empty_value()
	) | rpl::filter([=] {
		return _listen == nullptr;
	}) | rpl::on_next([=] {
		auto keyFilterCallback = [=](not_null<QEvent*> e) {
			using Result = base::EventFilterResult;
			if (_send->type() != Ui::SendButton::Type::Record
				&& _send->type() != Ui::SendButton::Type::Round) {
				return Result::Continue;
			}
			switch(e->type()) {
			case QEvent::KeyPress: {
				if (!_warningShown
					&& isRecordingLocked()
					&& Ui::ShouldSubmit(
						static_cast<QKeyEvent*>(e.get()),
						Core::App().settings().sendSubmitWay())) {
					stop(true);
					return Result::Cancel;
				}
				return Result::Continue;
			}
			default: return Result::Continue;
			}
		};

		_keyFilterInRecordingState = base::unique_qptr{
			base::install_event_filter(
				QCoreApplication::instance(),
				std::move(keyFilterCallback)).get()
		};
	}, lifetime());
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
	const auto resumedSamples = std::max(0, samples - _resumeRawSamples);
	const auto totalSamples = _resumePrefixSamples + resumedSamples;
	_recordingSamples = totalSamples;
	if (totalSamples < 0 || totalSamples >= kMaxSamples) {
		stop(totalSamples > 0 && _inField.current());
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
	} else if (send && _pauseInsteadSend) {
		_fullRecord = true;
		stopRecording(StopType::Listen);
		_lockShowing = false;
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
	_keyFilterInRecordingState = nullptr;
}

void VoiceRecordBar::clearResumePrefix() {
	_resumePrefixData = {};
	_resumePrefixSamples = 0;
	_resumeRawSamples = 0;
	_resumeRawDuration = 0;
	_resumeFromTrimmedListen = false;
}

void VoiceRecordBar::clearResumeState() {
	_pausedRawDuration = 0;
	clearResumePrefix();
}

void VoiceRecordBar::setupResumePrefixFromCurrentData() {
	_resumePrefixData = _data;
	_resumePrefixSamples = samplesFromDuration(_resumePrefixData.duration);
	_resumeRawDuration = _pausedRawDuration;
	_resumeRawSamples = samplesFromDuration(_resumeRawDuration);
}

int VoiceRecordBar::samplesFromDuration(crl::time duration) const {
	return int((duration * ::Media::Player::kDefaultFrequency)
		/ crl::time(1000));
}

Ui::RoundVideoResult VoiceRecordBar::mergeWithResumePrefix(
		Ui::RoundVideoResult data) {
	if (_recordingVideo || _resumePrefixData.content.isEmpty()) {
		return data;
	}
	if (data.content.isEmpty()) {
		return _resumePrefixData;
	}
	const auto tail = (_resumeRawDuration > 0)
		? ::Media::TrimAudioToRange(
			data.content,
			_resumeRawDuration,
			data.duration)
		: ::Media::AudioEditResult();
	if ((_resumeRawDuration > 0) && tail.content.isEmpty()) {
		return _resumePrefixData;
	}
	const auto combined = ::Media::ConcatAudio(
		_resumePrefixData.content,
		(_resumeRawDuration > 0) ? tail.content : data.content);
	if (combined.content.isEmpty()) {
		return _resumePrefixData;
	}
	const auto tailDuration = (_resumeRawDuration > 0)
		? tail.duration
		: data.duration;
	const auto duration = combined.duration
		? combined.duration
		: (_resumePrefixData.duration + tailDuration);
	auto waveform = MergeWaveformsByDuration(
		_resumePrefixData.waveform,
		_resumePrefixData.duration,
		(_resumeRawDuration > 0) ? tail.waveform : data.waveform,
		tailDuration);
	if (waveform.isEmpty()) {
		waveform = std::move(combined.waveform);
	}
	return Ui::RoundVideoResult{
		.content = std::move(combined.content),
		.waveform = std::move(waveform),
		.duration = duration,
	};
}

void VoiceRecordBar::stopRecording(StopType type, bool ttlBeforeHide) {
	using namespace ::Media::Capture;
	if (type == StopType::Cancel) {
		clearResumeState();
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
						_send,
						&_show->session(),
						&_data,
						false,
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
				const auto rawDuration = data.duration;
				auto combined = mergeWithResumePrefix(
					ToRoundVideoResult(std::move(data)));
				clearResumePrefix();
				if (combined.content.isEmpty()) {
					// Close everything.
					stop(false);
					return;
				}
				_pausedRawDuration = rawDuration;
				_paused = true;
				_data = std::move(combined);

				window()->raise();
				window()->activateWindow();
				_listen = std::make_unique<ListenWrap>(
					this,
					_st,
					_send,
					&_show->session(),
					&_data,
					true,
					_cancelFont);
				_listenChanges.fire({});
			}));
		}
	} else if (type == StopType::Send) {
		if (_videoRecorder) {
			const auto weak = base::make_weak(this);
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
			_pausedRawDuration = 0;
			auto combined = mergeWithResumePrefix(
				ToRoundVideoResult(std::move(data)));
			clearResumePrefix();
			if (combined.content.isEmpty()) {
				// Close everything.
				stop(false);
				return;
			}
			_data = std::move(combined);

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
	const auto animation
		= _recordingLifetime.make_state<Ui::Animations::Basic>();
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
		if (_listen) {
			_listen->prepareForSendAnimation();
			_listen->applyTrimBeforeSend();
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
	if (isRecordingLocked()) {
		return 1.;
	}
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
	_lock->requestPaintProgress(calcLockProgress(globalPos));
}

float64 VoiceRecordBar::calcLockProgress(QPoint globalPos) {
	const auto localPos = mapFromGlobal(globalPos);
	const auto lower = _lock->height();
	const auto higher = 0;
	return Progress(localPos.y(), higher - lower);
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
	_keyFilterInRecordingState = nullptr;
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
	) | rpl::on_next([=](Webrtc::VideoState state) {
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
	) | rpl::on_next([=](QImage &&placeholder) {
		_show->session().local().writeRoundPlaceholder(placeholder);
	}, _videoCapturerLifetime);

	return true;
}

} // namespace HistoryView::Controls
