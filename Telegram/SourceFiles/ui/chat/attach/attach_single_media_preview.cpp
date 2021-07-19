/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/attach/attach_single_media_preview.h"

#include "editor/photo_editor_common.h"
#include "ui/chat/attach/attach_prepare.h"
#include "core/mime_type.h"
#include "lottie/lottie_single_player.h"

namespace Ui {

SingleMediaPreview *SingleMediaPreview::Create(
		QWidget *parent,
		Fn<bool()> gifPaused,
		const PreparedFile &file,
		AttachControls::Type type) {
	auto preview = QImage();
	bool animated = false;
	bool animationPreview = false;
	if (const auto image = std::get_if<PreparedFileInformation::Image>(
			&file.information->media)) {
		preview = Editor::ImageModified(image->data, image->modifications);
		animated = animationPreview = image->animated;
	} else if (const auto video = std::get_if<PreparedFileInformation::Video>(
			&file.information->media)) {
		preview = video->thumbnail;
		animated = true;
		animationPreview = video->isGifv;
	}
	if (preview.isNull()) {
		return nullptr;
	} else if (!animated && !ValidateThumbDimensions(
			preview.width(),
			preview.height())) {
		return nullptr;
	}
	return CreateChild<SingleMediaPreview>(
		parent,
		std::move(gifPaused),
		preview,
		animated,
		Core::IsMimeSticker(file.information->filemime),
		animationPreview ? file.path : QString(),
		type);
}

SingleMediaPreview::SingleMediaPreview(
	QWidget *parent,
	Fn<bool()> gifPaused,
	QImage preview,
	bool animated,
	bool sticker,
	const QString &animatedPreviewPath,
	AttachControls::Type type)
: AbstractSingleMediaPreview(parent, type)
, _gifPaused(std::move(gifPaused))
, _sticker(sticker) {
	Expects(!preview.isNull());
	setAnimated(animated);

	preparePreview(preview);
	prepareAnimatedPreview(animatedPreviewPath, animated);
	updatePhotoEditorButton();
}

bool SingleMediaPreview::drawBackground() const {
	return !_sticker;
}

bool SingleMediaPreview::tryPaintAnimation(Painter &p) {
	if (_gifPreview && _gifPreview->started()) {
		auto s = QSize(previewWidth(), previewHeight());
		auto paused = _gifPaused();
		auto frame = _gifPreview->current(
			s.width(),
			s.height(),
			s.width(),
			s.height(),
			ImageRoundRadius::None,
			RectPart::None,
			paused ? 0 : crl::now());
		p.drawPixmap(previewLeft(), previewTop(), frame);
		return true;
	} else if (_lottiePreview && _lottiePreview->ready()) {
		const auto frame = _lottiePreview->frame();
		const auto size = frame.size() / style::DevicePixelRatio();
		p.drawImage(
			QRect(
				previewLeft() + (previewWidth() - size.width()) / 2,
				(previewHeight() - size.height()) / 2,
				size.width(),
				size.height()),
			frame);
		_lottiePreview->markFrameShown();
		return true;
	}
	return false;
}

bool SingleMediaPreview::isAnimatedPreviewReady() const {
	return _gifPreview || _lottiePreview;
}

void SingleMediaPreview::prepareAnimatedPreview(
		const QString &animatedPreviewPath,
		bool animated) {
	if (_sticker && animated) {
		const auto box = QSize(previewWidth(), previewHeight())
			* style::DevicePixelRatio();
		_lottiePreview = std::make_unique<Lottie::SinglePlayer>(
			Lottie::ReadContent(QByteArray(), animatedPreviewPath),
			Lottie::FrameRequest{ box });
		_lottiePreview->updates(
		) | rpl::start_with_next([=] {
			update();
		}, lifetime());
	} else if (!animatedPreviewPath.isEmpty()) {
		auto callback = [=](Media::Clip::Notification notification) {
			clipCallback(notification);
		};
		_gifPreview = Media::Clip::MakeReader(
			animatedPreviewPath,
			std::move(callback));
	}
}

void SingleMediaPreview::clipCallback(
		Media::Clip::Notification notification) {
	using namespace Media::Clip;
	switch (notification) {
	case NotificationReinit: {
		if (_gifPreview && _gifPreview->state() == State::Error) {
			_gifPreview.setBad();
		}

		if (_gifPreview && _gifPreview->ready() && !_gifPreview->started()) {
			const auto s = QSize(previewWidth(), previewHeight());
			_gifPreview->start(
				s.width(),
				s.height(),
				s.width(),
				s.height(),
				ImageRoundRadius::None,
				RectPart::None);
		}

		update();
	} break;

	case NotificationRepaint: {
		if (_gifPreview && !_gifPreview->currentDisplayed()) {
			update();
		}
	} break;
	}
}

} // namespace Ui
