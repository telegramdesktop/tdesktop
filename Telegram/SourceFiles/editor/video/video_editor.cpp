/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/video/video_editor.h"

#include "base/event_filter.h"
#include "base/timer.h"
#include "api/api_stories.h"
#include "editor/editor_caption_bar.h"
#include "editor/editor_crop.h"
#include "editor/video/video_quality_slider.h"
#include "editor/video/video_segment_player.h"
#include "editor/video/video_timeline.h"
#include "editor/video/video_timeline_seeker.h"
#include "lang/lang_keys.h"
#include "media/view/media_view_pip.h"
#include "ui/effects/ripple_animation.h"
#include "ui/layers/layer_widget.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/tooltip.h"
#include "ui/painter.h"
#include "ui/text/format_values.h"
#include "ui/ui_utility.h"
#include "styles/style_editor.h"
#include "styles/style_media_view.h"

#include <QtCore/QFileInfo>
#include <QtGui/QPainterPath>
#include <QtGui/QtEvents>
#include <QtWidgets/QApplication>

namespace Editor {
namespace {

using Media::View::FlipSizeByRotation;

constexpr auto kBubbleMinVisible = crl::time(600);

constexpr auto kBubbleDuration = crl::time(500);

constexpr auto kCaptureDuration = crl::time(420);

constexpr auto kMaxGifSize = 10 * 1024 * 1024;

constexpr auto kCanvasZoomMin = 0.5;
constexpr auto kCanvasZoomMax = 4.;
constexpr auto kCanvasWheelZoomBase = 1.0015;

[[nodiscard]] int64 SourceSize(
		const QString &path,
		const QByteArray &content) {
	return path.isEmpty()
		? int64(content.size())
		: QFileInfo(path).size();
}

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
		const style::color &fg,
		const style::RippleAnimation &ripple);

private:
	void paintEvent(QPaintEvent *e) override;
	[[nodiscard]] QImage prepareRippleMask() const override;
	[[nodiscard]] QRect rippleRect() const;

	const style::color &_fg;
	const QString _text;

};

BarTextButton::BarTextButton(
	not_null<Ui::RpWidget*> parent,
	const QString &text,
	const style::color &fg,
	const style::RippleAnimation &ripple)
: RippleButton(parent, ripple)
, _fg(fg)
, _text(text) {
	const auto &padding = st::photoEditorTextButtonPadding;
	resize(
		st::photoEditorButtonStyle.font->width(_text)
			+ padding.left()
			+ padding.right(),
		st::photoEditorButtonBarHeight);
}

QRect BarTextButton::rippleRect() const {
	const auto skip = st::videoEditorBarButtonRippleSkip;
	return rect().marginsRemoved({ skip, skip, skip, skip });
}

QImage BarTextButton::prepareRippleMask() const {
	const auto inner = rippleRect().size();
	return Ui::RippleAnimation::RoundRectMask(inner, inner.height() / 2);
}

void BarTextButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto inner = rippleRect();
	if (isOver() || isDown()) {
		const auto radius = inner.height() / 2.;
		p.setPen(Qt::NoPen);
		p.setBrush(st::photoEditorEdgeButtonBg);
		p.drawRoundedRect(inner, radius, radius);
	}
	paintRipple(p, inner.x(), inner.y());
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

} // namespace

VideoEditor::VideoEditor(
	not_null<QWidget*> parent,
	VideoEditorDescriptor descriptor)
: RpWidget(parent)
, _path(descriptor.path)
, _content(descriptor.content)
, _dimensions(descriptor.dimensions)
, _duration(std::max(descriptor.duration, crl::time(1)))
, _data(descriptor.data)
, _initial(descriptor.initial)
, _player(std::make_unique<SegmentPlayer>(
	_path,
	_content,
	SegmentPlayerOptions{ .keepAlpha = _data.webmSticker })) {
	_geometry = _initial.geometry;
	_gif = _initial.gif;
	_gifAllowed = (SourceSize(_path, _content) <= kMaxGifSize);
	_geometry.cropType = _data.editor.cropType;
	_geometry.cropMode = _data.editor.cropMode;
	if (_data.canvas && !_initial.canvasMediaRect.isEmpty()) {
		restoreCanvasPlacement(_initial.canvasMediaRect);
	}
	if (!_geometry.crop.isValid()
		&& !_data.exactSize.isEmpty()
		&& !_data.canvas) {
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
	if (_data.canvas) {
		_crop->hide();
	}

	setupControls();
	setupTimeline();
	setupQuality();
	setupSizeEstimate();
	setupStreaming();
	if (!_data.canvas) {
		setupTapToPause();
	}
	refreshCoverPreview();

	// The crop only reaches its saved rect after its own release handler.
	_crop->changes(
	) | rpl::on_next([=] {
		refreshQualityLevels();
		refreshSizeEstimate();
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
			.content = _content,
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
	_seeker = std::make_unique<TimelineSeeker>(
		_timeline.get(),
		_player.get());

	_timeline->trimChanges(
	) | rpl::on_next([=] {
		_from = _timeline->from();
		_till = _timeline->till();
		_cover = _timeline->cover();
		refreshSizeEstimate();
	}, _timeline->lifetime());

	_timeline->coverChanges(
	) | rpl::on_next([=](crl::time cover) {
		_cover = cover;
		updateBubble();
		refreshCoverPreview();
	}, _timeline->lifetime());

	_timeline->draggingChanges(
	) | rpl::on_next([=](bool dragging) {
		_dragging = dragging;
		if (dragging) {
			_draggingHead = _timeline->draggingHead();
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
	_quality->valueChanges(
	) | rpl::on_next([=] {
		refreshSizeEstimate();
	}, _quality->lifetime());
}

void VideoEditor::setupSizeEstimate() {
	const auto path = _path;
	crl::async([=, weak = base::make_weak(this)] {
		auto info = Media::Encode::ProbeSource(path);
		crl::on_main(weak, [=, info = std::move(info)]() mutable {
			_source = std::move(info);
			refreshSizeEstimate();
		});
	});
}

void VideoEditor::refreshSizeEstimate() {
	if (!_timeline || _source.empty()) {
		return;
	}
	const auto modifications = collect();
	const auto transcoded = _data.transcodeAlways
		|| VideoEdited(
			modifications,
			_dimensions,
			_duration,
			_source.hasAudio);
	const auto bytes = transcoded
		? Media::Encode::EstimateTranscodedSize(
			ComposeVideoSource(_path, modifications, _data, false),
			_source)
		: _source.fileSize;
	_timeline->setSizeLabel((bytes > 0)
		? ((transcoded ? QString::fromUtf8("~") : QString())
			+ Ui::FormatSizeText(bytes))
		: QString());
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

	const auto &buttonRipple = _data.editor.story
		? st::storiesComposeRippleLight
		: st::photoEditorRotateButton.ripple;
	_cancelButton = base::make_unique_q<BarTextButton>(
		bar,
		tr::lng_cancel(tr::now),
		st::mediaviewCaptionFg,
		buttonRipple);
	_rotate = base::make_unique_q<Ui::IconButton>(
		bar,
		st::photoEditorRotateButton);
	_flip = base::make_unique_q<Ui::IconButton>(
		bar,
		st::photoEditorFlipButton);
	if (!_data.removeAudio && _data.allowGif) {
		// When the audio is dropped anyway there is nothing to choose.
		_gifButton = base::make_unique_q<Ui::IconButton>(
			bar,
			st::videoEditorGifButton);
		setupGifButton();
	}
	_confirm = base::make_unique_q<BarTextButton>(
		bar,
		(_data.editor.confirm.isEmpty()
			? tr::lng_box_done(tr::now)
			: _data.editor.confirm),
		st::mediaviewTextLinkFg,
		buttonRipple);

	if (!_data.hint.isEmpty()) {
		_hint = base::make_unique_q<Ui::FlatLabel>(
			_controls.get(),
			rpl::single(_data.hint),
			st::videoEditorHint);
		_hint->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
	if (const auto &story = _data.editor.story) {
		_captionBar = base::make_unique_q<CaptionBar>(
			_controls.get(),
			this,
			*story);
		const auto bar = _captionBar.get();
		bar->heightValue() | rpl::skip(1) | rpl::on_next([=] {
			applyGeometry();
		}, bar->lifetime());
		bar->submits() | rpl::on_next([=] {
			_done.fire(collect());
		}, bar->lifetime());
		bar->cancels() | rpl::on_next([=] {
			setFocus();
		}, bar->lifetime());
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
		_canvasRestored = std::nullopt;
		applyGeometry();
		refreshSizeEstimate();
		invalidateCoverPreview();
	});
	_flip->setClickedCallback([=] {
		_geometry.flipped = !_geometry.flipped;
		applyGeometry();
		refreshSizeEstimate();
		invalidateCoverPreview();
	});
	_cancelButton->setClickedCallback([=] {
		_cancel.fire({});
	});
	_confirm->setClickedCallback([=] {
		_timeline->commitPendingEdit();
		_done.fire(collect());
	});
}

void VideoEditor::setupGifButton() {
	if (!_gifButton) {
		return;
	}
	const auto refresh = [=] {
		const auto icon = _gif
			? &st::videoEditorGifIconActive
			: nullptr;
		_gifButton->setIconOverride(icon, icon);
	};
	refresh();
	_gifButton->setClickedCallback([=] {
		if (!_gifAllowed) {
			return;
		}
		_gif = !_gif;
		refresh();
		refreshSizeEstimate();
	});
	if (_gifAllowed) {
		return;
	}
	_gifButton->setDisabled(true);
	_gifButton->setIconColorOverride(st::photoEditorButtonIconFgInactive->c);
	base::install_event_filter(_gifButton, [](not_null<QEvent*> e) {
		const auto type = e->type();
		return (type == QEvent::MouseButtonPress
			|| type == QEvent::MouseButtonDblClick)
			? base::EventFilterResult::Cancel
			: base::EventFilterResult::Continue;
	}, _gifButton->lifetime());
	_gifButton->events() | rpl::on_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::Enter) {
			showGifTooltip();
		} else if (type == QEvent::Leave) {
			hideGifTooltip();
		}
	}, _gifButton->lifetime());
}

void VideoEditor::showGifTooltip() {
	if (_gifAllowed || !_gifButton) {
		return;
	}
	if (!_gifTooltip) {
		const auto parent = parentWidget();
		if (!parent) {
			return;
		}
		const auto text = tr::marked(tr::lng_video_editor_gif_too_large(
			tr::now,
			lt_size,
			(QString::number(kMaxGifSize / (1024 * 1024))
				+ ' '
				+ tr::lng_size_megabytes(tr::now))));
		_gifTooltip = base::make_unique_q<Ui::ImportantTooltip>(
			parent,
			Ui::MakeNiceTooltipLabel(
				parent,
				rpl::single(text),
				st::videoEditorGifTooltipMaxWidth,
				st::defaultImportantTooltipLabel),
			st::defaultImportantTooltip);
		_gifTooltip->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
	const auto parent = _gifTooltip->parentWidget();
	_gifTooltip->pointAt(
		Ui::MapFrom(parent, _gifButton, _gifButton->rect()),
		RectPart::Top | RectPart::Center);
	_gifTooltip->toggleAnimated(true);
}

void VideoEditor::hideGifTooltip() {
	if (_gifTooltip) {
		_gifTooltip->toggleAnimated(false);
	}
}

void VideoEditor::setupStreaming() {
	_player->repaints(
	) | rpl::on_next([=] {
		update();
		if (_player->held()) {
			updateBubble();
		}
	}, lifetime());

	_player->start();
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
			state->pressed = std::nullopt;
			state->moved = false;
			if (tapped) {
				togglePause();
			}
		}
	}, lifetime());
}

void VideoEditor::togglePause() {
	if (!_player->valid()) {
		return;
	}
	_userPaused = !_userPaused;
	_player->setPaused(_userPaused);
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
	const auto content = _content;
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
		auto preview = ExtractCoverImage(
			path,
			content,
			mods,
			dimensions,
			side);
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
	if (_captionBar) {
		_captionBar->resizeToWidth(barWidth);
	}
	const auto captionHeight = _captionBar
		? (st::videoEditorCaptionSkip + _captionBar->height())
		: 0;
	const auto controlsHeight = st::videoEditorTimelineTop
		+ _timeline->height()
		+ (_hint ? (st::videoEditorHintSkip + _hint->height()) : 0)
		+ (quality ? (st::videoEditorQualitySkip + quality->height()) : 0)
		+ captionHeight
		+ st::videoEditorBarSkip
		+ st::photoEditorButtonBarHeight
		+ st::videoEditorBarBottomSkip;
	const auto contentRect = rect()
		- QMargins(skip, skip, skip, controlsHeight);
	if (contentRect.isEmpty()) {
		return;
	}
	_contentRect = contentRect;

	if (_data.canvas && !_data.exactSize.isEmpty()) {
		layoutCanvas(contentRect);
	} else {
		const auto m = _crop->cropMargins();
		const auto margins = QSize(
			m.left() + m.right(),
			m.top() + m.bottom());
		const auto frameSizeF = [&] {
			const auto rotated = FlipSizeByRotation(
				contentRect.size(),
				_geometry.angle);
			const auto forCrop = rotated - margins;
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
			geometry + m,
			QPoint(m.left(), m.top()),
			_geometry.angle,
			_geometry.flipped,
			frameSizeF);
		_crop->setCornersLevel(_geometry.cornersLevel);
		_innerRect = geometry;
	}

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
	if (_captionBar) {
		_captionBar->move(barLeft, below + st::videoEditorCaptionSkip);
		below = _captionBar->y() + _captionBar->height();
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
	const auto frame = _player->frame(frameRequestSize());
	if (frame.isNull()) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	if (_data.canvas) {
		paintCanvas(p);
		p.setClipRect(_canvasRect);
	}
	p.setTransform(_frameMatrix);
	p.drawImage(_frameRect, frame);
	p.resetTransform();
	if (_data.canvas) {
		p.setClipping(false);
	}

	if (_userPaused) {
		paintPlayBadge(p);
	}
}

void VideoEditor::paintCanvas(QPainter &p) {
	Expects(_data.canvas.has_value());

	if (!_canvasRect.isEmpty()) {
		Media::Encode::PaintCanvasBackground(p, _canvasRect, *_data.canvas);
	}
}

QSize VideoEditor::frameRequestSize() const {
	if (!_data.canvas) {
		return _frameRect.size();
	}
	const auto limit = _dimensions / style::DevicePixelRatio();
	const auto size = _frameRect.size();
	if (limit.isEmpty()
		|| ((size.width() <= limit.width())
			&& (size.height() <= limit.height()))) {
		return size;
	}
	return size.scaled(limit, Qt::KeepAspectRatio);
}

float64 VideoEditor::canvasScale() const {
	return _data.exactSize.isEmpty()
		? 1.
		: _canvasRect.width() / float64(_data.exactSize.width());
}

QRectF VideoEditor::canvasBasePlacement() const {
	const auto oriented = FlipSizeByRotation(_dimensions, _geometry.angle);
	return QRectF(Media::Encode::CanvasPlacement(
		oriented,
		_data.exactSize));
}

QRectF VideoEditor::canvasMediaRect() const {
	if (_canvasRestored) {
		return QRectF(*_canvasRestored);
	}
	const auto base = canvasBasePlacement();
	const auto size = base.size() * _canvasZoom;
	const auto center = base.center() + _canvasShift;
	return QRectF(
		center - QPointF(size.width() / 2., size.height() / 2.),
		size);
}

void VideoEditor::layoutCanvas(QRect contentRect) {
	const auto exact = _data.exactSize;
	const auto canvasSize = QSizeF(exact).scaled(
		contentRect.size(),
		Qt::KeepAspectRatio).toSize();
	_canvasRect = QRect(
		contentRect.x() + (contentRect.width() - canvasSize.width()) / 2,
		contentRect.y() + (contentRect.height() - canvasSize.height()) / 2,
		canvasSize.width(),
		canvasSize.height());
	const auto scale = canvasScale();
	const auto media = canvasMediaRect();
	_mediaRect = QRectF(
		_canvasRect.x() + media.x() * scale,
		_canvasRect.y() + media.y() * scale,
		media.width() * scale,
		media.height() * scale).toRect();

	const auto frameSize = FlipSizeByRotation(
		_mediaRect.size(),
		_geometry.angle);
	_frameRect = QRect(
		QPoint(-frameSize.width() / 2, -frameSize.height() / 2),
		frameSize);
	_frameMatrix.reset();
	_frameMatrix.translate(
		_mediaRect.x() + _mediaRect.width() / 2.,
		_mediaRect.y() + _mediaRect.height() / 2.);
	if (_geometry.flipped) {
		_frameMatrix.scale(-1, 1);
	}
	_frameMatrix.rotate(_geometry.angle);
	_innerRect = _mediaRect;
}

void VideoEditor::clampCanvasShift() {
	const auto exact = _data.exactSize;
	const auto base = canvasBasePlacement();
	const auto center = base.center() + _canvasShift;
	const auto clamped = QPointF(
		std::clamp(center.x(), 0., float64(exact.width())),
		std::clamp(center.y(), 0., float64(exact.height())));
	_canvasShift = clamped - base.center();
}

void VideoEditor::restoreCanvasPlacement(QRect rect) {
	const auto base = canvasBasePlacement();
	if (base.isEmpty() || rect.isEmpty()) {
		return;
	}
	_canvasZoom = std::clamp(
		rect.width() / base.width(),
		kCanvasZoomMin,
		kCanvasZoomMax);
	_canvasShift = QRectF(rect).center() - base.center();
	_canvasRestored = rect;
}

bool VideoEditor::edited() const {
	const auto now = collect();
	const auto &was = _initial;
	const auto till = [&](crl::time value) {
		return value ? value : _duration;
	};
	return (now.geometry.angle != was.geometry.angle)
		|| (now.geometry.flipped != was.geometry.flipped)
		|| (now.canvasMediaRect != was.canvasMediaRect)
		|| (now.from != was.from)
		|| (till(now.till) != till(was.till))
		|| (now.cover != was.cover)
		|| (now.gif != was.gif);
}

void VideoEditor::zoomCanvas(float64 factor, QPoint about) {
	const auto zoom = std::clamp(
		_canvasZoom * factor,
		kCanvasZoomMin,
		kCanvasZoomMax);
	const auto real = zoom / _canvasZoom;
	if (real == 1. || _canvasRect.isEmpty()) {
		return;
	}
	const auto point = QPointF(about - _canvasRect.topLeft())
		/ canvasScale();
	const auto base = canvasBasePlacement();
	const auto center = base.center() + _canvasShift;
	const auto shifted = point - (point - center) * real;
	_canvasZoom = zoom;
	_canvasShift = shifted - base.center();
	_canvasRestored = std::nullopt;
	clampCanvasShift();
	canvasChanged();
}

void VideoEditor::canvasChanged() {
	if (_contentRect.isEmpty()) {
		return;
	}
	layoutCanvas(_contentRect);
	invalidateCoverPreview();
	update();
}

void VideoEditor::mousePressEvent(QMouseEvent *e) {
	if (!_data.canvas
		|| (e->button() != Qt::LeftButton)
		|| !_canvasRect.contains(e->pos())) {
		return RpWidget::mousePressEvent(e);
	}
	_canvasPress = e->pos();
	_canvasDragShift = _canvasShift;
	_canvasMoved = false;
}

void VideoEditor::mouseMoveEvent(QMouseEvent *e) {
	if (!_canvasPress) {
		return RpWidget::mouseMoveEvent(e);
	}
	const auto delta = e->pos() - *_canvasPress;
	if (!_canvasMoved
		&& (delta.manhattanLength() < QApplication::startDragDistance())) {
		return;
	}
	if (!_canvasMoved) {
		_canvasMoved = true;
		setCursor(Qt::ClosedHandCursor);
	}
	_canvasShift = _canvasDragShift + QPointF(delta) / canvasScale();
	_canvasRestored = std::nullopt;
	clampCanvasShift();
	layoutCanvas(_contentRect);
	update();
}

void VideoEditor::mouseReleaseEvent(QMouseEvent *e) {
	if (!_canvasPress) {
		return RpWidget::mouseReleaseEvent(e);
	}
	const auto moved = _canvasMoved;
	_canvasPress = std::nullopt;
	_canvasMoved = false;
	if (moved) {
		unsetCursor();
		invalidateCoverPreview();
	} else {
		togglePause();
	}
}

void VideoEditor::wheelEvent(QWheelEvent *e) {
	const auto position = e->position().toPoint();
	if (!_data.canvas || !_canvasRect.contains(position)) {
		return RpWidget::wheelEvent(e);
	}
	const auto delta = e->angleDelta().y()
		? e->angleDelta().y()
		: e->angleDelta().x();
	if (delta) {
		zoomCanvas(std::pow(kCanvasWheelZoomBase, delta), position);
	}
	e->accept();
}

bool VideoEditor::eventHook(QEvent *e) {
	if (_data.canvas && (e->type() == QEvent::NativeGesture)) {
		const auto gesture = static_cast<QNativeGestureEvent*>(e);
		const auto position = gesture->position().toPoint();
		if ((gesture->gestureType() == Qt::ZoomNativeGesture)
			&& _canvasRect.contains(position)) {
			zoomCanvas(1. + gesture->value(), position);
			return true;
		}
	}
	return RpWidget::eventHook(e);
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
		_timeline->commitPendingEdit();
		_done.fire(collect());
	}
}

VideoModifications VideoEditor::collect() const {
	auto geometry = _geometry;
	geometry.crop = _crop->saveCropRect();
	const auto moved = _data.canvas
		&& ((_canvasZoom != 1.) || !_canvasShift.isNull());
	return {
		.geometry = std::move(geometry),
		.from = _timeline->from(),
		.till = _timeline->till(),
		.cover = _timeline->cover(),
		.quality = _quality ? _quality->value() : 0,
		.gif = _gif,
		.canvasMediaRect = (moved
			? Media::Encode::RoundedCanvasRect(canvasMediaRect())
			: QRect()),
	};
}

TextWithTags VideoEditor::caption() const {
	return _captionBar ? _captionBar->caption() : TextWithTags();
}

int VideoEditor::period() const {
	return _captionBar ? _captionBar->period() : 0;
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
