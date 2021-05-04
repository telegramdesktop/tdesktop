/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/photo_editor_layer_widget.h"

#include "app.h" // readImage
#include "boxes/confirm_box.h" // InformBox
#include "editor/photo_editor.h"
#include "storage/storage_media_prepare.h"
#include "ui/chat/attach/attach_prepare.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

namespace Editor {
namespace {

constexpr auto kProfilePhotoSize = 640;

} // namespace

void OpenWithPreparedFile(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<Ui::PreparedFile*> file,
		int previewWidth,
		Fn<void()> &&doneCallback) {

	if (file->type != Ui::PreparedFile::Type::Photo) {
		return;
	}
	using ImageInfo = Ui::PreparedFileInformation::Image;
	const auto image = std::get_if<ImageInfo>(&file->information->media);
	if (!image) {
		return;
	}

	auto callback = [=, done = std::move(doneCallback)](
			const PhotoModifications &mods) {
		image->modifications = mods;
		Storage::UpdateImageDetails(*file, previewWidth);
		done();
	};
	auto copy = image->data;
	const auto fileImage = std::make_shared<Image>(std::move(copy));
	controller->showLayer(
		std::make_unique<LayerWidget>(
			parent,
			&controller->window(),
			fileImage,
			image->modifications,
			std::move(callback)),
		Ui::LayerOption::KeepOther);
}

void PrepareProfilePhoto(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::Controller*> controller,
		Fn<void(QImage &&image)> &&doneCallback) {
	const auto resizeToMinSize = [=](
			QImage &&image,
			Qt::AspectRatioMode mode) {
		const auto &minSize = kProfilePhotoSize;
		if ((image.width() < minSize) || (image.height() < minSize)) {
			return image.scaled(
				minSize,
				minSize,
				mode,
				Qt::SmoothTransformation);
		}
		return std::move(image);
	};

	const auto callback = [=, done = std::move(doneCallback)](
			const FileDialog::OpenResult &result) {
		if (result.paths.isEmpty() && result.remoteContent.isEmpty()) {
			return;
		}

		auto image = result.remoteContent.isEmpty()
			? App::readImage(result.paths.front())
			: App::readImage(result.remoteContent);
		if (image.isNull()
			|| (image.width() > (10 * image.height()))
			|| (image.height() > (10 * image.width()))) {
			controller->show(Box<InformBox>(tr::lng_bad_photo(tr::now)));
			return;
		}
		image = resizeToMinSize(
			std::move(image),
			Qt::KeepAspectRatioByExpanding);
		const auto fileImage = std::make_shared<Image>(std::move(image));

		auto applyModifications = [=, done = std::move(done)](
				const PhotoModifications &mods) {
			done(resizeToMinSize(
				ImageModified(fileImage->original(), mods),
				Qt::KeepAspectRatio));
		};

		auto crop = [&] {
			const auto &i = fileImage;
			const auto minSide = std::min(i->width(), i->height());
			return QRect(
				(i->width() - minSide) / 2,
				(i->height() - minSide) / 2,
				minSide,
				minSide);
		}();

		controller->showLayer(
			std::make_unique<LayerWidget>(
				parent,
				controller,
				fileImage,
				PhotoModifications{ .crop = std::move(crop) },
				std::move(applyModifications),
				EditorData{
					.cropType = EditorData::CropType::Ellipse,
					.keepAspectRatio = true, }),
			Ui::LayerOption::KeepOther);
	};
	FileDialog::GetOpenPath(
		parent.get(),
		tr::lng_choose_image(tr::now),
		FileDialog::ImagesOrAllFilter(),
		crl::guard(parent, callback));
}

LayerWidget::LayerWidget(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::Controller*> window,
	std::shared_ptr<Image> photo,
	PhotoModifications modifications,
	Fn<void(PhotoModifications)> &&doneCallback,
	EditorData data)
: Ui::LayerWidget(parent)
, _content(base::make_unique_q<PhotoEditor>(
	this,
	window,
	photo,
	std::move(modifications),
	std::move(data))) {

	paintRequest(
	) | rpl::start_with_next([=](const QRect &clip) {
		Painter p(this);

		p.fillRect(clip, st::photoEditorBg);
	}, lifetime());

	_content->cancelRequests(
	) | rpl::start_with_next([=] {
		closeLayer();
	}, lifetime());

	_content->doneRequests(
	) | rpl::start_with_next([=, done = std::move(doneCallback)](
			const PhotoModifications &mods) {
		done(mods);
		closeLayer();
	}, lifetime());

	sizeValue(
	) | rpl::start_with_next([=](const QSize &size) {
		_content->resize(size);
	}, lifetime());
}

void LayerWidget::parentResized() {
	resizeToWidth(parentWidget()->width());
}

void LayerWidget::keyPressEvent(QKeyEvent *e) {
	_content->handleKeyPress(e);
}

int LayerWidget::resizeGetHeight(int newWidth) {
	return parentWidget()->height();
}

bool LayerWidget::closeByOutsideClick() const {
	return false;
}

} // namespace Editor
