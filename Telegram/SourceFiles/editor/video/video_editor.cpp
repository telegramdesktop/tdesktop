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
#include "ui/layers/layer_widget.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/painter.h"
#include "styles/style_editor.h"

#include <QtGui/QtEvents>

namespace Editor {
namespace {

using Media::View::FlipSizeByRotation;

constexpr auto kSeekThrottle = crl::time(120);

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

	setupTimeline();
	setupControls();
	setupStreaming();

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
	_timeline = base::make_unique_q<VideoTimeline>(
		this,
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
		_paused = dragging;
		if (!dragging) {
			seekTimer->cancel();
			restart(_cover);
		}
	}, _timeline->lifetime());
}

void VideoEditor::setupControls() {
	_controls = base::make_unique_q<Ui::RpWidget>(this);
	const auto controls = _controls.get();

	_rotate = base::make_unique_q<Ui::IconButton>(
		controls,
		st::photoEditorRotateButton);
	_flip = base::make_unique_q<Ui::IconButton>(
		controls,
		st::photoEditorFlipButton);
	_cancelButton = base::make_unique_q<Ui::RoundButton>(
		controls,
		tr::lng_cancel(),
		st::videoEditorCancelButton);
	_confirm = base::make_unique_q<Ui::RoundButton>(
		controls,
		(_data.editor.confirm.isEmpty()
			? tr::lng_settings_save()
			: rpl::single(_data.editor.confirm)),
		st::videoEditorConfirmButton);

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
	update();
}

void VideoEditor::handleUpdate(Media::Streaming::Update &&update) {
	using namespace Media::Streaming;
	v::match(update.data, [&](Information &) {
		this->update();
	}, [&](PreloadedVideo) {
	}, [&](UpdateVideo &data) {
		_position = data.position;
		if (_position >= _till) {
			restart(_from);
			return;
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

	const auto timelineWidth = std::max(
		size.width() - st::videoEditorContentMargins.left() * 2,
		1);
	_timeline->resizeToWidth(timelineWidth);
	_timeline->move(
		st::videoEditorContentMargins.left(),
		controlsTop + st::videoEditorTimelineTop);

	const auto buttonsTop = st::videoEditorButtonsTop;
	const auto skip = st::videoEditorButtonSkip;
	_cancelButton->moveToLeft(
		st::videoEditorContentMargins.left(),
		buttonsTop,
		size.width());
	_confirm->moveToRight(
		st::videoEditorContentMargins.right(),
		buttonsTop,
		size.width());
	const auto middle = size.width() / 2;
	const auto pairWidth = _rotate->width() + skip + _flip->width();
	_rotate->move(middle - pairWidth / 2, buttonsTop - 6);
	_flip->move(_rotate->x() + _rotate->width() + skip, buttonsTop - 6);

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
		if (!_paused) {
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
