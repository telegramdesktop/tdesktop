/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/video/video_editor_layer.h"

#include "chat_helpers/compose/compose_show.h"
#include "core/file_utilities.h"
#include "editor/editor_layer_widget.h"
#include "editor/photo_editor_layer_widget.h"
#include "editor/video/video_editor.h"
#include "lang/lang_keys.h"
#include "media/media_video_frames.h"
#include "storage/storage_media_prepare.h"
#include "ui/boxes/confirm_box.h"
#include "ui/chat/attach/attach_extensions.h"
#include "ui/chat/attach/attach_prepare.h"
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
	if (!data.confirmVideo.isEmpty()) {
		data.confirm = data.confirmVideo;
	}
	auto hint = data.forOtherUser
		? tr::lng_profile_choose_frame_other(tr::now)
		: tr::lng_profile_choose_frame(tr::now);
	return {
		.editor = std::move(data),
		.hint = std::move(hint),
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
				ComposeVideoSource(path, mods, editorData, true)),
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

void OpenWithPreparedVideoFile(
		not_null<QWidget*> parent,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<Ui::PreparedFile*> file,
		int previewWidth,
		Fn<void(bool ok)> &&doneCallback,
		int sideLimit) {
	using VideoInfo = Ui::PreparedFileInformation::Video;
	const auto lookup = [=]() -> VideoInfo* {
		return file->information
			? std::get_if<VideoInfo>(&file->information->media)
			: nullptr;
	};
	const auto video = lookup();
	if (!file->canEditVideo() || !video || video->thumbnail.isNull()) {
		doneCallback(false);
		return;
	}
	const auto path = file->path;
	const auto dimensions = video->thumbnail.size();
	if (!AcceptableDimensions(dimensions)) {
		doneCallback(false);
		return;
	}

	const auto accepted = std::make_shared<bool>();
	auto callback = [=](VideoModifications mods) {
		*accepted = true;
		const auto video = lookup();
		if (!video) {
			doneCallback(false);
			return;
		}
		const auto coverChanged = (video->modifications.cover != mods.cover);
		video->modifications = mods;
		if (!coverChanged) {
			Storage::UpdateVideoDetails(*file, previewWidth, sideLimit);
			doneCallback(true);
			return;
		}
		const auto kept = video->thumbnail;
		crl::async([=, done = crl::guard(parent, [=](
				QImage &&frame,
				Storage::VideoDetails &&details) {
			const auto video = lookup();
			if (!video) {
				doneCallback(false);
				return;
			}
			if (!frame.isNull()) {
				video->thumbnail = std::move(frame);
			}
			Storage::ApplyVideoDetails(*file, std::move(details));
			doneCallback(true);
		})] {
			auto frame = Media::Video::ExtractFrame(
				path,
				mods.cover,
				dimensions);
			auto details = Storage::ComputeVideoDetails(
				frame.isNull() ? kept : frame,
				mods.geometry,
				previewWidth,
				sideLimit);
			crl::on_main([
				=,
				frame = std::move(frame),
				details = std::move(details)
			]() mutable {
				done(std::move(frame), std::move(details));
			});
		});
	};

	auto editor = base::make_unique_q<VideoEditor>(
		parent,
		VideoEditorDescriptor{
			.path = path,
			.dimensions = dimensions,
			.duration = video->duration,
			.data = VideoEditorData{ .allowQuality = true },
			.initial = video->modifications,
		});
	const auto raw = editor.get();
	auto layer = std::make_unique<LayerWidget>(parent, std::move(editor));
	InitVideoEditorLayer(layer.get(), raw, std::move(callback));
	QObject::connect(layer.get(), &QObject::destroyed, [=] {
		if (!*accepted) {
			doneCallback(false);
		}
	});
	show->showLayer(std::move(layer), Ui::LayerOption::KeepOther);
}

} // namespace Editor
