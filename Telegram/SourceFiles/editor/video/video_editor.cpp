/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/video/video_editor.h"

#include "base/timer.h"
#include "editor/editor_crop.h"
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

#include <QtGui/QtEvents>
#include <QtWidgets/QApplication>

namespace Editor {
namespace {

using Media::View::FlipSizeByRotation;

constexpr auto kSeekThrottle = crl::time(120);

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
		? ((width() - total) / float64(count - 1))
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
, _data(descriptor.data) {
	_geometry.cropType = _data.editor.cropType;
	_geometry.cropMode = _data.editor.cropMode;
	_geometry.crop = [&] {
		const auto side = std::min(_dimensions.width(), _dimensions.height());
		return QRect(
			(_dimensions.width() - side) / 2,
			(_dimensions.height() - side) / 2,
			side,
			side);
	}();

	_crop = base::make_unique_q<Crop>(
		this,
		_geometry,
		_dimensions,
		_data.editor);

	setupControls();
	setupTimeline();
	setupStreaming();
	setupTapToPause();

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
		});
	_from = _timeline->from();
	_till = _timeline->till();
	_cover = _timeline->cover();
	_position = _from;

	const auto seekTimer = lifetime().make_state<base::Timer>();
	const auto pending = lifetime().make_state<crl::time>(-1);
	seekTimer->setCallback([=] {
		if (*pending >= 0) {
			const auto position = *pending;
			*pending = -1;
			restart(position);
		}
	});
	const auto seek = [=](crl::time position) {
		*pending = position;
		if (!seekTimer->isActive()) {
			seekTimer->callOnce(kSeekThrottle);
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
	}, _timeline->lifetime());

	_timeline->draggingChanges(
	) | rpl::on_next([=](bool dragging) {
		_dragging = dragging;
		if (dragging) {
			if (_instance) {
				_instance->pause();
			}
		} else {
			seekTimer->cancel();
			restart(_cover);
		}
	}, _timeline->lifetime());
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
	_confirm = base::make_unique_q<BarTextButton>(
		bar,
		(_data.editor.confirm.isEmpty()
			? tr::lng_box_done(tr::now)
			: _data.editor.confirm),
		st::mediaviewTextLinkFg);

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
	});
	_flip->setClickedCallback([=] {
		_geometry.flipped = !_geometry.flipped;
		applyGeometry();
	});
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
	const auto pressed = lifetime().make_state<std::optional<QPoint>>();
	const auto moved = lifetime().make_state<bool>(false);
	_crop->events(
	) | rpl::on_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::MouseButtonPress) {
			const auto mouse = static_cast<QMouseEvent*>(e.get());
			if (mouse->button() == Qt::LeftButton) {
				*pressed = mouse->pos();
				*moved = false;
			}
		} else if (type == QEvent::MouseMove) {
			if (pressed->has_value() && !*moved) {
				const auto mouse = static_cast<QMouseEvent*>(e.get());
				const auto shift = mouse->pos() - **pressed;
				if (shift.manhattanLength()
					>= QApplication::startDragDistance()) {
					*moved = true;
				}
			}
		} else if (type == QEvent::MouseButtonRelease) {
			const auto tapped = pressed->has_value() && !*moved;
			*pressed = std::nullopt;
			*moved = false;
			if (tapped) {
				togglePause();
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
	const auto controlsHeight = st::videoEditorControlsHeight;
	const auto contentRect = rect() - st::videoEditorContentMargins;
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

	const auto available = std::max(
		size.width() - st::videoEditorContentMargins.left() * 2,
		1);
	const auto barWidth = std::min(
		int(st::photoEditorButtonBarWidth),
		available);
	const auto barLeft = (size.width() - barWidth) / 2;

	_timeline->resizeToWidth(barWidth);
	_timeline->move(barLeft, st::videoEditorTimelineTop);

	_bar->setGeometry(
		barLeft,
		_timeline->y() + _timeline->height() + st::videoEditorBarSkip,
		barWidth,
		st::photoEditorButtonBarHeight);
	static_cast<ControlsBar*>(_bar.get())->layoutChildren();

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
