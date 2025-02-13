/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/photo_editor_layer_widget.h"

#include "lang/lang_keys.h"
#include "ui/boxes/confirm_box.h" // InformBox
#include "editor/editor_layer_widget.h"
#include "editor/photo_editor.h"
#include "storage/localimageloader.h"
#include "storage/storage_media_prepare.h"
#include "ui/chat/attach/attach_prepare.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include <QtGui/QGuiApplication>

namespace Editor {

void OpenWithPreparedFile(
		not_null<QWidget*> parent,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<Ui::PreparedFile*> file,
		int previewWidth,
		Fn<void(bool ok)> &&doneCallback,
		QSize exactSize) {
	using ImageInfo = Ui::PreparedFileInformation::Image;
	const auto image = std::get_if<ImageInfo>(&file->information->media);
	if (!image) {
		doneCallback(false);
		return;
	}
	const auto photoType = (file->type == Ui::PreparedFile::Type::Photo);
	const auto modifiedFileType = (file->type == Ui::PreparedFile::Type::File)
		&& !image->modifications.empty();
	if (!photoType && !modifiedFileType) {
		doneCallback(false);
		return;
	}

	const auto sideLimit = PhotoSideLimit();
	const auto accepted = std::make_shared<bool>();
	auto callback = [=](const PhotoModifications &mods) {
		*accepted = true;
		image->modifications = mods;
		Storage::UpdateImageDetails(*file, previewWidth, sideLimit);
		{
			using namespace Ui;
			const auto size = file->preview.size();
			file->type = ValidateThumbDimensions(size.width(), size.height())
				? PreparedFile::Type::Photo
				: PreparedFile::Type::File;
		}
		doneCallback(true);
	};
	auto copy = image->data;
	const auto fileImage = std::make_shared<Image>(std::move(copy));
	const auto keepRatio = !exactSize.isEmpty();
	auto editor = base::make_unique_q<PhotoEditor>(
		parent,
		show,
		show,
		fileImage,
		image->modifications,
		EditorData{ .exactSize = exactSize, .keepAspectRatio = keepRatio });
	const auto raw = editor.get();
	auto layer = std::make_unique<LayerWidget>(parent, std::move(editor));
	InitEditorLayer(layer.get(), raw, std::move(callback));
	QObject::connect(layer.get(), &QObject::destroyed, [=] {
		if (!*accepted) {
			doneCallback(false);
		}
	});
	show->showLayer(std::move(layer), Ui::LayerOption::KeepOther);
}

void PrepareProfilePhoto(
		not_null<QWidget*> parent,
		not_null<Window::Controller*> controller,
		EditorData data,
		Fn<void(QImage &&image)> &&doneCallback,
		QImage &&image) {
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

	if (image.isNull()
		|| (image.width() > (10 * image.height()))
		|| (image.height() > (10 * image.width()))) {
		controller->show(Ui::MakeInformBox(tr::lng_bad_photo()));
		return;
	}
	image = resizeToMinSize(
		std::move(image),
		Qt::KeepAspectRatioByExpanding);
	const auto fileImage = std::make_shared<Image>(std::move(image));

	auto applyModifications = [=, done = std::move(doneCallback)](
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

	auto editor = base::make_unique_q<PhotoEditor>(
		parent,
		controller,
		fileImage,
		PhotoModifications{ .crop = std::move(crop) },
		data);
	const auto raw = editor.get();
	auto layer = std::make_unique<LayerWidget>(parent, std::move(editor));
	InitEditorLayer(layer.get(), raw, std::move(applyModifications));
	controller->showLayer(std::move(layer), Ui::LayerOption::KeepOther);
}

void PrepareProfilePhotoFromFile(
		not_null<QWidget*> parent,
		not_null<Window::Controller*> controller,
		EditorData data,
		Fn<void(QImage &&image)> &&doneCallback) {
	const auto callback = [=, done = std::move(doneCallback)](
			const FileDialog::OpenResult &result) mutable {
		if (result.paths.isEmpty() && result.remoteContent.isEmpty()) {
			return;
		}

		auto image = Images::Read({
			.path = result.paths.isEmpty() ? QString() : result.paths.front(),
			.content = result.remoteContent,
			.forceOpaque = true,
		}).image;
		PrepareProfilePhoto(
			parent,
			controller,
			data,
			std::move(done),
			std::move(image));
	};
	FileDialog::GetOpenPath(
		parent.get(),
		tr::lng_choose_image(tr::now),
		FileDialog::ImagesOrAllFilter(),
		crl::guard(parent, callback));
}

} // namespace Editor
