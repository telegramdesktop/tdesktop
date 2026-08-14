/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/video/video_editor_layer.h"

#include "core/file_utilities.h"
#include "editor/editor_layer_widget.h"
#include "editor/photo_editor_layer_widget.h"
#include "editor/video/video_editor.h"
#include "lang/lang_keys.h"
#include "media/media_video_frames.h"
#include "ui/boxes/confirm_box.h"
#include "ui/chat/attach/attach_extensions.h"
#include "ui/image/image_prepare.h"
#include "window/window_controller.h"

namespace Editor {
namespace {

constexpr auto kMaxSideRatio = 10;

[[nodiscard]] bool AcceptableDimensions(QSize size) {
	return !size.isEmpty()
		&& (size.width() <= size.height() * kMaxSideRatio)
		&& (size.height() <= size.width() * kMaxSideRatio);
}

} // namespace

VideoEditorData ProfileVideoEditorData(EditorData data) {
	data.keepAspectRatio = true;
	return {
		.editor = std::move(data),
		.exactSize = QSize(kProfileVideoSide, kProfileVideoSide),
		.maxDuration = kProfileVideoMaxDuration,
		.minDuration = kProfileVideoMinDuration,
		.fpsLimit = kProfileVideoFps,
		.removeAudio = true,
	};
}

void PrepareProfileVideo(
		not_null<QWidget*> parent,
		not_null<Window::Controller*> controller,
		EditorData data,
		const QString &path,
		Fn<void(ProfileMedia&&)> &&doneCallback) {
	const auto info = Media::Video::ReadFileInfo(path);
	if (!info.valid() || !AcceptableDimensions(info.dimensions)) {
		controller->show(Ui::MakeInformBox(tr::lng_bad_video()));
		return;
	}
	auto editorData = ProfileVideoEditorData(std::move(data));

	auto applyModifications = [=, done = std::move(doneCallback)](
			VideoModifications mods) mutable {
		auto preview = ExtractCoverImage(
			path,
			mods,
			info.dimensions,
			kProfileVideoSide);
		if (preview.isNull()) {
			controller->show(Ui::MakeInformBox(tr::lng_bad_video()));
			return;
		}
		const auto side = kProfileVideoSide;
		preview = preview.scaled(
			side,
			side,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
		done({
			.image = std::move(preview),
			.video = std::make_shared<Media::Encode::VideoSource>(
				ComposeVideoSource(path, mods, editorData)),
		});
	};

	auto editor = base::make_unique_q<VideoEditor>(
		parent,
		VideoEditorDescriptor{
			.path = path,
			.dimensions = info.dimensions,
			.duration = info.duration,
			.data = editorData,
		});
	const auto raw = editor.get();
	auto layer = std::make_unique<LayerWidget>(parent, std::move(editor));
	InitVideoEditorLayer(layer.get(), raw, std::move(applyModifications));
	controller->showLayer(std::move(layer), Ui::LayerOption::KeepOther);
}

void PrepareProfileMediaFromFile(
		not_null<QWidget*> parent,
		not_null<Window::Controller*> controller,
		EditorData data,
		Fn<void(ProfileMedia&&)> &&doneCallback) {
	const auto callback = [=, done = std::move(doneCallback)](
			const FileDialog::OpenResult &result) mutable {
		if (result.paths.isEmpty() && result.remoteContent.isEmpty()) {
			return;
		}
		const auto path = result.paths.isEmpty()
			? QString()
			: result.paths.front();

		auto image = Images::Read({
			.path = path,
			.content = result.remoteContent,
			.forceOpaque = true,
		}).image;
		if (!image.isNull()) {
			auto photoDone = [done](QImage &&image) mutable {
				done({ .image = std::move(image) });
			};
			PrepareProfilePhoto(
				parent,
				controller,
				data,
				std::move(photoDone),
				std::move(image));
			return;
		}
		const auto byExtension = ranges::any_of(
			Ui::ImageExtensions(),
			[&](const QString &extension) {
				return path.endsWith(extension, Qt::CaseInsensitive);
			});
		if (path.isEmpty() || byExtension) {
			controller->show(Ui::MakeInformBox(tr::lng_bad_photo()));
			return;
		}
		PrepareProfileVideo(
			parent,
			controller,
			data,
			path,
			std::move(done));
	};
	FileDialog::GetOpenPath(
		parent.get(),
		tr::lng_choose_image(tr::now),
		FileDialog::PhotoVideoFilesFilter(),
		crl::guard(parent, callback));
}

} // namespace Editor
