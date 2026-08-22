/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/video/video_editor.h"

#include "base/timer.h"
#include "editor/editor_crop.h"
#include "editor/video/video_quality_slider.h"
#include "editor/video/video_timeline.h"
#include "lang/lang_keys.h"
#include "media/streaming/media_streaming_document.h"
#include "media/streaming/media_streaming_instance.h"
#include "media/streaming/media_streaming_loader_local.h"
#include "media/streaming/media_streaming_player.h"
#include "media/view/media_view_pip.h"
#include "ui/effects/ripple_animation.h"
#include "ui/layers/layer_widget.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/painter.h"
#include "styles/style_editor.h"

#include <QtGui/QPainterPath>
#include <QtGui/QtEvents>
#include <QtWidgets/QApplication>

namespace Editor {
namespace {

using Media::View::FlipSizeByRotation;

constexpr auto kSeekThrottle = crl::time(120);

constexpr auto kBubbleMinVisible = crl::time(600);

constexpr auto kBubbleDuration = crl::time(500);

constexpr auto kCaptureDuration = crl::time(420);

class ControlsBar final : public Ui::RpWidget {
public:
	using RpWidget::RpWidget;

	void layoutChildren();

private:
	void paintEvent(QPaintEvent *e) override;

};

void ControlsBar::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto radius = std::min(width(), height()) / 2.;
	p.setPen(Qt::NoPen);
	p.setBrush(st::roundedBg);
	p.drawRoundedRect(rect(), radius, radius);
}

void ControlsBar::layoutChildren() {
	auto widgets = std::vector<QWidget*>();
	for (const auto child : children()) {
		if (child->isWidgetType()) {
			widgets.push_back(static_cast<QWidget*>(child));
		}
	}
	if (widgets.empty() || width() <= 0) {
		return;
	}
	auto total = 0;
	for (const auto widget : widgets) {
		total += widget->width();
	}
	const auto count = int(widgets.size());
	const auto step = (count > 1)
		? std::max((width() - total) / float64(count - 1), 0.)
		: 0.;
	auto left = 0.;
	for (const auto widget : widgets) {
		widget->move(
			int(base::SafeRound(left)),
			(height() - widget->height()) / 2);
		left += widget->width() + step;
	}
}

class BarTextButton final : public Ui::RippleButton {
public:
	BarTextButton(
		not_null<Ui::RpWidget*> parent,
		const QString &text,
		const style::color &fg);

private:
	void paintEvent(QPaintEvent *e) override;
	[[nodiscard]] QImage prepareRippleMask() const override;

	const style::color &_fg;
	const QString _text;

};

BarTextButton::BarTextButton(
	not_null<Ui::RpWidget*> parent,
	const QString &text,
	const style::color &fg)
: RippleButton(parent, st::photoEditorRotateButton.ripple)
, _fg(fg)
, _text(text) {
	const auto &padding = st::photoEditorTextButtonPadding;
	resize(
		st::photoEditorButtonStyle.font->width(_text)
			+ padding.left()
			+ padding.right(),
		st::photoEditorButtonBarHeight);
}

QImage BarTextButton::prepareRippleMask() const {
	return Ui::RippleAnimation::RoundRectMask(size(), height() / 2);
}

void BarTextButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	if (isOver() || isDown()) {
		const auto radius = height() / 2.;
		p.setPen(Qt::NoPen);
		p.setBrush(st::photoEditorEdgeButtonBg);
		p.drawRoundedRect(rect(), radius, radius);
	}
	paintRipple(p, 0, 0);
	p.setPen(_fg);
	p.setFont(st::photoEditorButtonStyle.font);
	p.drawText(rect(), Qt::AlignCenter, _text);
}

[[nodiscard]] float64 ShapeRadius(
		QSizeF size,
		EditorData::CropType type,
		RoundedCornersLevel corners) {
	const auto shorter = std::min(size.width(), size.height());
	if (type == EditorData::CropType::Ellipse) {
		return shorter / 2.;
	}
	const auto multiplier = (type == EditorData::CropType::RoundedRect)
		? RoundedCornersMultiplier(corners)
		: RoundedCornersMultiplier(RoundedCornersLevel::Small);
	return shorter * multiplier;
}

[[nodiscard]] QPainterPath ShapePath(
		const QRectF &rect,
		EditorData::CropType type,
		RoundedCornersLevel corners,
		float64 rounding = 1.) {
	auto result = QPainterPath();
	if (rect.isEmpty()) {
		return result;
	}
	const auto radius = ShapeRadius(rect.size(), type, corners) * rounding;
	if (radius <= 0.) {
		result.addRect(rect);
	} else {
		result.addRoundedRect(rect, radius, radius);
	}
	return result;
}

[[nodiscard]] Media::Streaming::FrameRequest FrameRequestFor(QSize size) {
	auto result = Media::Streaming::FrameRequest();
	result.resize = size * style::DevicePixelRatio();
	result.outer = result.resize;
	return result;
}

} // namespace

VideoEditor::VideoEditor(
	not_null<QWidget*> parent,
	VideoEditorDescriptor descriptor)
: RpWidget(parent)
, _path(descriptor.path)
, _dimensions(descriptor.dimensions)
, _duration(std::max(descriptor.duration, crl::time(1)))
, _data(descriptor.data)
, _initial(descriptor.initial) {
	_geometry = _initial.geometry;
	_gif = _initial.gif;
	_geometry.cropType = _data.editor.cropType;
	_geometry.cropMode = _data.editor.cropMode;
	if (!_geometry.crop.isValid() && !_data.exactSize.isEmpty()) {
		// A fixed size result starts from the largest crop that fits it.
		const auto side = std::min(_dimensions.width(), _dimensions.height());
		_geometry.crop = QRect(
			(_dimensions.width() - side) / 2,
			(_dimensions.height() - side) / 2,
			side,
			side);
	}

	_crop = base::make_unique_q<Crop>(
		this,
		_geometry,
		_dimensions,
		_data.editor);

	setupControls();
	setupTimeline();
	setupQuality();
	setupStreaming();
	setupTapToPause();
	refreshCoverPreview();

	_crop->events(
	) | rpl::filter([](not_null<QEvent*> e) {
		return (e->type() == QEvent::MouseButtonRelease);
	}) | rpl::on_next([=] {
		refreshQualityLevels();
		invalidateCoverPreview();
	}, _crop->lifetime());

	sizeValue(
	) | rpl::filter([=](QSize size) {
		return !size.isEmpty();
	}) | rpl::on_next([=] {
		applyGeometry();
	}, lifetime());

	paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(this);
		paint(p);
	}, lifetime());
}

VideoEditor::~VideoEditor() = default;

void VideoEditor::setupTimeline() {
	// A sibling of the controls container would never see a mouse press.
	_timeline = base::make_unique_q<VideoTimeline>(
		_controls.get(),
		VideoTimelineDescriptor{
			.path = _path,
			.dimensions = _dimensions,
			.duration = _duration,
			.maxDuration = _data.maxDuration,
			.minDuration = _data.minDuration,
			.from = _initial.from,
			.till = _initial.till,
			.cover = _initial.cover,
		});
	_from = _timeline->from();
	_till = _timeline->till();
	_cover = _timeline->cover();
	_position = _from;

	struct State {
		base::Timer timer;
		crl::time pending = -1;
	};
	const auto state = lifetime().make_state<State>();
	state->timer.setCallback([=] {
		if (state->pending >= 0) {
			const auto position = state->pending;
			state->pending = -1;
			restart(position);
		}
	});
	const auto seek = [=](crl::time position) {
		state->pending = position;
		if (!state->timer.isActive()) {
			state->timer.callOnce(kSeekThrottle);
		}
	};

	_timeline->trimChanges(
	) | rpl::on_next([=](crl::time edge) {
		_from = _timeline->from();
		_till = _timeline->till();
		_cover = _timeline->cover();
		seek(edge);
	}, _timeline->lifetime());

	_timeline->coverChanges(
	) | rpl::on_next([=](crl::time cover) {
		_cover = cover;
		seek(cover);
		updateBubble();
		refreshCoverPreview();
	}, _timeline->lifetime());

	_timeline->draggingChanges(
	) | rpl::on_next([=](bool dragging) {
		_dragging = dragging;
		if (dragging) {
			_draggingHead = _timeline->draggingHead();
			if (_instance) {
				_instance->pause();
			}
		} else {
			state->timer.cancel();
			const auto latest = (state->pending >= 0)
				? state->pending
				: _position;
			state->pending = -1;
			const auto resume = std::clamp(latest, _from, _till);
			restart((resume >= _till) ? _from : resume);
		}
		if (!dragging && _draggingHead) {
			_draggingHead = false;
			startCapture();
			return;
		}
		updateBubble();
	}, _timeline->lifetime());
}

void VideoEditor::setupQuality() {
	if (!_data.allowQuality) {
		return;
	}
	_quality = base::make_unique_q<VideoQualitySlider>(_controls.get());
	refreshQualityLevels();
	_quality->setValue(_initial.quality);
}

void VideoEditor::refreshQualityLevels() {
	if (!_quality) {
		return;
	}
	auto geometry = _geometry;
	geometry.crop = _crop->saveCropRect();
	const auto was = _quality->hasChoice();
	_quality->setLevels(
		VideoQualityLevels(EditedFrameSize(_dimensions, geometry)));
	if ((_quality->hasChoice() != was) && !size().isEmpty()) {
		applyGeometry();
	}
}

void VideoEditor::setupControls() {
	_controls = base::make_unique_q<Ui::RpWidget>(this);
	_bar = base::make_unique_q<ControlsBar>(_controls.get());
	const auto bar = _bar.get();

	_cancelButton = base::make_unique_q<BarTextButton>(
		bar,
		tr::lng_cancel(tr::now),
		st::mediaviewCaptionFg);
	_rotate = base::make_unique_q<Ui::IconButton>(
		bar,
		st::photoEditorRotateButton);
	_flip = base::make_unique_q<Ui::IconButton>(
		bar,
		st::photoEditorFlipButton);
	if (!_data.removeAudio) {
		// When the audio is dropped anyway there is nothing to choose.
		_gifButton = base::make_unique_q<Ui::IconButton>(
			bar,
			st::videoEditorGifButton);
	}
	_confirm = base::make_unique_q<BarTextButton>(
		bar,
		(_data.editor.confirm.isEmpty()
			? tr::lng_box_done(tr::now)
			: _data.editor.confirm),
		st::mediaviewTextLinkFg);

	if (!_data.hint.isEmpty()) {
		_hint = base::make_unique_q<Ui::FlatLabel>(
			_controls.get(),
			rpl::single(_data.hint),
			st::videoEditorHint);
		_hint->setAttribute(Qt::WA_TransparentForMouseEvents);
	}

	_bubble = base::make_unique_q<Ui::RpWidget>(this);
	_bubble->setAttribute(Qt::WA_TransparentForMouseEvents);
	_bubble->hide();
	_bubble->paintRequest(
	) | rpl::on_next([=] {
		const auto shown = _bubbleShown.value(_bubbleVisible ? 1. : 0.);
		if (_bubblePreview.isNull() || shown <= 0.) {
			return;
		}
		auto p = QPainter(_bubble.get());
		auto hq = PainterHighQualityEnabler(p);
		p.setOpacity(shown);

		const auto size = QSizeF(bubbleSize());
		const auto dotY = float64(_bubble->height());
		const auto grown = QSizeF(
			size.width() * shown,
			size.height() * shown);
		const auto centre = QPointF(
			size.width() / 2.,
			dotY + (size.height() / 2. - dotY) * shown);
		const auto rect = QRectF(
			centre.x() - grown.width() / 2.,
			centre.y() - grown.height() / 2.,
			grown.width(),
			grown.height());
		const auto type = _geometry.cropType;
		const auto corners = _geometry.cornersLevel;
		p.setClipPath(ShapePath(rect, type, corners));
		p.drawImage(rect, _bubblePreview);
		p.setClipping(false);
		p.setBrush(Qt::NoBrush);
		const auto border = st::videoEditorBubbleBorder;
		const auto half = border / 2.;
		p.setPen(QPen(st::mediaviewControlFg, border));
		p.drawPath(ShapePath(
			rect.adjusted(half, half, -half, -half),
			type,
			corners));
	}, _bubble->lifetime());

	_bubbleHideTimer.setCallback([=] { updateBubble(); });

	_capture = base::make_unique_q<Ui::RpWidget>(this);
	_capture->setAttribute(Qt::WA_TransparentForMouseEvents);
	_capture->hide();
	_capture->paintRequest(
	) | rpl::on_next([=] {
		if (_bubblePreview.isNull()) {
			return;
		}
		const auto value = _captureShown.value(1.);
		auto p = QPainter(_capture.get());
		auto hq = PainterHighQualityEnabler(p);

		const auto from = captureFrom();

		const auto to = QRect(_bubble->pos(), bubbleSize());
		if (from.isEmpty() || to.isEmpty()) {
			return;
		}
		const auto lerp = [&](int a, int b) {
			return int(base::SafeRound(a + (b - a) * value));
		};
		const auto rect = QRectF(QRect(
			lerp(from.x(), to.x()),
			lerp(from.y(), to.y()),
			lerp(from.width(), to.width()),
			lerp(from.height(), to.height())));
		const auto type = _geometry.cropType;
		const auto corners = _geometry.cornersLevel;
		p.setClipPath(ShapePath(rect, type, corners, value));
		p.drawImage(rect, _bubblePreview);
		p.setClipping(false);

		p.setOpacity(value * value);
		p.setBrush(Qt::NoBrush);
		const auto border = st::videoEditorBubbleBorder;
		const auto half = border / 2.;
		p.setPen(QPen(st::mediaviewControlFg, border));
		p.drawPath(ShapePath(
			rect.adjusted(half, half, -half, -half),
			type,
			corners,
			value));
		p.setOpacity(1.);

		const auto flash = std::clamp(1. - value * 3., 0., 1.);
		if (flash > 0. && !from.isEmpty()) {
			p.setOpacity(flash);
			p.fillRect(from, st::videoEditorFlashFg);
		}
	}, _capture->lifetime());

	if (!_data.editor.about.text.isEmpty()) {
		_about = base::make_unique_q<Ui::FlatLabel>(
			this,
			rpl::single(_data.editor.about),
			st::videoEditorAbout);
		_about->setAttribute(Qt::WA_TransparentForMouseEvents);
	}

	_rotate->setClickedCallback([=] {
		_geometry.angle += 90;
		while (_geometry.angle >= 360) {
			_geometry.angle -= 360;
		}
		applyGeometry();
		invalidateCoverPreview();
	});
	_flip->setClickedCallback([=] {
		_geometry.flipped = !_geometry.flipped;
		applyGeometry();
		invalidateCoverPreview();
	});
	if (_gifButton) {
		const auto refresh = [=] {
			const auto icon = _gif
				? &st::videoEditorGifIconActive
				: nullptr;
			_gifButton->setIconOverride(icon, icon);
		};
		refresh();
		_gifButton->setClickedCallback([=] {
			_gif = !_gif;
			refresh();
		});
	}
	_cancelButton->setClickedCallback([=] {
		_cancel.fire({});
	});
	_confirm->setClickedCallback([=] {
		_done.fire(collect());
	});
}

void VideoEditor::setupStreaming() {
	using namespace Media::Streaming;

	auto loader = MakeFileLoader(_path);
	if (!loader) {
		return;
	}
	_instance = std::make_unique<Instance>(
		std::make_shared<Document>(std::move(loader)),
		nullptr);
	if (!_instance->valid()) {
		_instance = nullptr;
		return;
	}
	_instance->lockPlayer();
	_instance->player().updates(
	) | rpl::on_next_error([=](Update &&update) {
		handleUpdate(std::move(update));
	}, [=](Error &&) {
		_instance = nullptr;
		update();
	}, _instance->lifetime());

	restart(_from);
}

bool VideoEditor::held() const {
	return _dragging || _userPaused;
}

void VideoEditor::setupTapToPause() {
	struct State {
		std::optional<QPoint> pressed;
		bool moved = false;
	};
	const auto state = lifetime().make_state<State>();
	_crop->events(
	) | rpl::on_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::MouseButtonPress) {
			const auto mouse = static_cast<QMouseEvent*>(e.get());
			if (mouse->button() == Qt::LeftButton) {
				state->pressed = mouse->pos();
				state->moved = false;
			}
		} else if (type == QEvent::MouseMove) {
			if (state->pressed.has_value() && !state->moved) {
				const auto mouse = static_cast<QMouseEvent*>(e.get());
				const auto shift = mouse->pos() - *state->pressed;
				if (shift.manhattanLength()
					>= QApplication::startDragDistance()) {
					state->moved = true;
				}
			}
		} else if (type == QEvent::MouseButtonRelease) {
			const auto tapped = state->pressed.has_value() && !state->moved;
			const auto dragged = state->pressed.has_value() && state->moved;
			state->pressed = std::nullopt;
			state->moved = false;
			if (tapped) {
				togglePause();
			} else if (dragged) {
				invalidateCoverPreview();
			}
		}
	}, lifetime());
}

void VideoEditor::togglePause() {
	if (!_instance) {
		return;
	}
	_userPaused = !_userPaused;
	if (_userPaused) {
		_instance->pause();
	} else {
		_instance->resume();
	}
	update();
}

void VideoEditor::invalidateCoverPreview() {
	_bubbleCover = -1;
	refreshCoverPreview();
}

void VideoEditor::refreshCoverPreview() {
	if (_bubbleBusy || _bubbleCover == _cover) {
		return;
	}
	_bubbleCover = _cover;
	_bubbleBusy = true;

	const auto cover = _cover;
	const auto path = _path;
	const auto dimensions = _dimensions;
	const auto side = st::videoEditorBubbleSize
		* style::DevicePixelRatio()
		* 2;
	auto mods = VideoModifications{
		.geometry = _geometry,
		.cover = _cover,
	};
	mods.geometry.crop = _crop->saveCropRect();

	crl::async([=, weak = base::make_weak(this)] {
		auto preview = ExtractCoverImage(path, mods, dimensions, side);
		crl::on_main(weak, [=, preview = std::move(preview)]() mutable {
			_bubbleBusy = false;
			if (!preview.isNull() && cover == _bubbleCover) {
				_bubblePreview = std::move(preview);
				updateBubble();
			}
			refreshCoverPreview();
		});
	});
}

QRect VideoEditor::captureFrom() const {
	if (!_crop || _innerRect.isEmpty()) {
		return {};
	}
	return _crop->paintRect().translated(_crop->pos());
}

QSize VideoEditor::bubbleSize() const {
	const auto side = st::videoEditorBubbleSize;
	const auto preview = _bubblePreview.size();
	if (preview.isEmpty()) {
		return QSize(side, side);
	}
	const auto scaled = preview.scaled(side, side, Qt::KeepAspectRatio);
	return QSize(
		std::max(scaled.width(), 1),
		std::max(scaled.height(), 1));
}

void VideoEditor::startCapture() {
	if (!_capture || _bubblePreview.isNull() || captureFrom().isEmpty()) {
		return;
	}
	_bubbleShown.stop();
	_bubbleVisible = false;
	_bubble->hide();

	_capture->setGeometry(rect());
	_capture->show();
	_capture->raise();
	_captureShown.start([=] {
		_capture->update();
		if (!_captureShown.animating()) {
			_capture->hide();
			_bubbleVisible = true;
			_bubbleShownAt = crl::now();
			_bubble->show();
			_bubble->raise();
			_bubble->update();
			_bubbleHideTimer.callOnce(kBubbleMinVisible);
		}
	}, 0., 1., kCaptureDuration, anim::easeOutQuint);
	_capture->update();
}

void VideoEditor::updateBubble() {
	if (!_bubble || !_timeline) {
		return;
	}
	auto visible = _dragging
		&& _timeline->draggingHead()
		&& !_bubblePreview.isNull();
	if (_captureShown.animating()) {
		if (!visible) {
			return; // Let the flight finish on its own.
		}
		_captureShown.stop();
		_capture->hide();
	}
	if (!visible && _bubbleVisible) {
		const auto shownFor = crl::now() - _bubbleShownAt;
		if (shownFor < kBubbleMinVisible) {
			if (!_bubbleHideTimer.isActive()) {
				_bubbleHideTimer.callOnce(kBubbleMinVisible - shownFor);
			}
			visible = true;
		}
	}
	if (_bubbleVisible != visible) {
		_bubbleVisible = visible;
		if (visible) {
			_bubbleShownAt = crl::now();
			_bubbleHideTimer.cancel();
		}
		_bubbleShown.start(
			[=] {
				_bubble->update();
				if (!_bubbleVisible && !_bubbleShown.animating()) {
					_bubble->hide();
				}
			},
			visible ? 0. : 1.,
			visible ? 1. : 0.,
			kBubbleDuration,
			anim::easeOutQuint);
	}
	if (!visible && !_bubbleShown.animating()) {
		_bubble->hide();
		return;
	} else if (_bubblePreview.isNull()) {
		return;
	}
	const auto size = bubbleSize();
	const auto dot = _timeline->mapTo(this, _timeline->coverDot());
	const auto top = _timeline->mapTo(this, QPoint()).y()
		- st::videoEditorBubbleSkip
		- size.height();
	_bubble->setGeometry(
		std::clamp(
			dot.x() - size.width() / 2,
			0,
			std::max(width() - size.width(), 0)),
		top,
		size.width(),
		std::max(dot.y() - top, size.height()));
	_bubble->show();
	_bubble->raise();
	_bubble->update();
}

void VideoEditor::restart(crl::time position) {
	if (!_instance) {
		return;
	}
	using namespace Media::Streaming;
	if (!_frameRect.isEmpty()
		&& _instance->player().ready()
		&& !_instance->player().videoSize().isEmpty()) {
		_lastFrame = _instance->frame(
			FrameRequestFor(_frameRect.size())).copy();
	}
	_position = std::clamp(position, _from, _till);
	auto options = PlaybackOptions();
	options.mode = Mode::Video;
	options.position = _position;
	options.loop = false;
	_instance->play(options);
	if (held()) {
		_instance->pause();
	}
	if (!_dragging && _timeline) {
		_timeline->setPlaybackPosition(_position);
	}
	update();
}

void VideoEditor::handleUpdate(Media::Streaming::Update &&update) {
	using namespace Media::Streaming;
	v::match(update.data, [&](Information &) {
		this->update();
	}, [&](PreloadedVideo) {
	}, [&](UpdateVideo &data) {
		_position = data.position;
		if (held()) {
			_instance->pause();
			this->update();
			updateBubble();
			return;
		}
		if (_position >= _till) {
			restart(_from);
			return;
		}
		if (_timeline) {
			_timeline->setPlaybackPosition(_position);
		}
		this->update();
	}, [&](PreloadedAudio) {
	}, [&](UpdateAudio) {
	}, [&](WaitingForData) {
	}, [&](SpeedEstimate) {
	}, [&](MutedByOther) {
	}, [&](Finished) {
		restart(_from);
	});
}

void VideoEditor::applyGeometry() {
	const auto size = this->size();
	if (size.isEmpty()) {
		return;
	}
	refreshQualityLevels();
	const auto quality = (_quality && _quality->hasChoice())
		? _quality.get()
		: nullptr;
	const auto skip = st::videoEditorContentSkip;
	const auto available = std::max(size.width() - skip * 2, 1);
	const auto barWidth = std::min(
		int(st::photoEditorButtonBarWidth),
		available);
	const auto barLeft = (size.width() - barWidth) / 2;
	_timeline->resizeToWidth(barWidth);
	if (_hint) {
		_hint->resizeToWidth(barWidth);
	}
	if (quality) {
		quality->resizeToWidth(barWidth);
	}
	const auto controlsHeight = st::videoEditorTimelineTop
		+ _timeline->height()
		+ (_hint ? (st::videoEditorHintSkip + _hint->height()) : 0)
		+ (quality ? (st::videoEditorQualitySkip + quality->height()) : 0)
		+ st::videoEditorBarSkip
		+ st::photoEditorButtonBarHeight
		+ st::videoEditorBarBottomSkip;
	const auto contentRect = rect()
		- QMargins(skip, skip, skip, controlsHeight);
	if (contentRect.isEmpty()) {
		return;
	}

	const auto frameSizeF = [&] {
		const auto rotated = FlipSizeByRotation(
			contentRect.size(),
			_geometry.angle);
		const auto m = _crop->cropMargins();
		const auto forCrop = rotated
			- QSize(m.left() + m.right(), m.top() + m.bottom());
		const auto original = QSizeF(_dimensions);
		if ((original.width() > forCrop.width())
			|| (original.height() > forCrop.height())) {
			return original.scaled(forCrop, Qt::KeepAspectRatio);
		}
		return original;
	}();
	const auto frameSize = QSize(
		int(frameSizeF.width()),
		int(frameSizeF.height()));
	_frameRect = QRect(
		QPoint(-frameSize.width() / 2, -frameSize.height() / 2),
		frameSize);

	_frameMatrix.reset();
	_frameMatrix.translate(
		contentRect.x() + contentRect.width() / 2,
		contentRect.y() + contentRect.height() / 2);
	if (_geometry.flipped) {
		_frameMatrix.scale(-1, 1);
	}
	_frameMatrix.rotate(_geometry.angle);

	const auto geometry = _frameMatrix.mapRect(_frameRect);
	_crop->applyTransform(
		geometry + _crop->cropMargins(),
		_geometry.angle,
		_geometry.flipped,
		frameSizeF);
	_crop->setCornersLevel(_geometry.cornersLevel);
	_innerRect = geometry;

	const auto controlsTop = size.height() - controlsHeight;
	_controls->setGeometry(0, controlsTop, size.width(), controlsHeight);

	_timeline->move(barLeft, st::videoEditorTimelineTop);

	auto below = _timeline->y() + _timeline->height();
	if (_hint) {
		_hint->move(barLeft, below + st::videoEditorHintSkip);
		below = _hint->y() + _hint->height();
	}
	if (quality) {
		quality->move(barLeft, below + st::videoEditorQualitySkip);
		below = quality->y() + quality->height();
	}
	_bar->setGeometry(
		barLeft,
		below + st::videoEditorBarSkip,
		barWidth,
		st::photoEditorButtonBarHeight);
	static_cast<ControlsBar*>(_bar.get())->layoutChildren();
	updateBubble();

	if (_about) {
		const auto margin = st::videoEditorAboutMargin;
		_about->resizeToWidth(size.width() - margin.left() - margin.right());
		_about->move(margin.left(), margin.top());
	}

	update();
}

void VideoEditor::paint(QPainter &p) {
	if (_frameRect.isEmpty()) {
		return;
	}
	auto frame = QImage();
	if (_instance
		&& _instance->player().ready()
		&& !_instance->player().videoSize().isEmpty()) {
		frame = _instance->frame(FrameRequestFor(_frameRect.size()));
		if (!held()) {
			// Marking a frame shown lets the player walk past a pause.
			_instance->markFrameShown();
		}
	}
	if (frame.isNull()) {
		frame = _lastFrame;
	}
	if (frame.isNull()) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	p.setTransform(_frameMatrix);
	p.drawImage(_frameRect, frame);
	p.resetTransform();

	if (_userPaused) {
		paintPlayBadge(p);
	}
}

void VideoEditor::paintPlayBadge(QPainter &p) {
	const auto side = st::videoEditorPlayBadgeSize;
	const auto center = _innerRect.isEmpty()
		? rect().center()
		: _innerRect.center();
	const auto badge = QRect(
		center.x() - side / 2,
		center.y() - side / 2,
		side,
		side);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::videoEditorPlayBadgeBg);
	p.drawEllipse(badge);
	const auto &icon = st::videoEditorPlayIcon;
	icon.paintInCenter(p, badge);
}

void VideoEditor::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		_cancel.fire({});
	} else if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		_done.fire(collect());
	}
}

VideoModifications VideoEditor::collect() const {
	auto geometry = _geometry;
	geometry.crop = _crop->saveCropRect();
	return {
		.geometry = std::move(geometry),
		.from = _timeline->from(),
		.till = _timeline->till(),
		.cover = _timeline->cover(),
		.quality = _quality ? _quality->value() : 0,
		.gif = _gif,
	};
}

void InitVideoEditorLayer(
		not_null<Ui::LayerWidget*> layer,
		not_null<VideoEditor*> editor,
		Fn<void(VideoModifications)> doneCallback) {
	editor->cancelRequests(
	) | rpl::on_next([=] {
		layer->closeLayer();
	}, editor->lifetime());

	const auto weak = base::make_weak(layer.get());
	editor->doneRequests(
	) | rpl::on_next([=, done = std::move(doneCallback)](
			const VideoModifications &mods) {
		done(mods);
		if (const auto strong = weak.get()) {
			strong->closeLayer();
		}
	}, editor->lifetime());
}

} // namespace Editor
