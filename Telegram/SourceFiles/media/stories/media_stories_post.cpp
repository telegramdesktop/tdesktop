/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/stories/media_stories_post.h"

#include "media/stories/media_stories_discard_prompt.h"

#include "apiwrap.h"
#include "boxes/premium_preview_box.h"
#include "boxes/send_story_box.h"

#include "base/random.h"
#include "base/variant.h"
#include "chat_helpers/compose/compose_show.h"
#include "core/file_utilities.h"
#include "core/mime_type.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "data/data_story.h"
#include "data/data_story_drafts.h"
#include "data/data_user.h"
#include "editor/editor_enhance.h"
#include "editor/editor_layer_widget.h"
#include "editor/editor_message_source.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "editor/editor_story_areas.h"
#include "editor/photo_editor.h"
#include "editor/photo_editor_common.h"
#include "editor/scene/scene.h"
#include "editor/scene/scene_draft.h"
#include "editor/scene/scene_item_video.h"
#include "info/channel_statistics/boosts/info_boosts_widget.h"
#include "info/info_memento.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "media/media_video_canvas.h"
#include "media/media_video_encode.h"
#include "media/media_video_frames.h"
#include "storage/file_download_web.h"
#include "storage/localimageloader.h"
#include "storage/storage_media_prepare.h"
#include "ui/boxes/confirm_box.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/image/image.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include "styles/style_media_view.h"

namespace Media::Stories {
namespace {

constexpr auto kCanvasSampleSide = 240;

constexpr auto kDraftThumbnailSide = 320;

[[nodiscard]] int InitialPeriod(const StoryEditorSetup &setup) {
	return setup.period ? setup.period : Api::kStoryDefaultPeriod;
}

[[nodiscard]] QImage DraftThumbnail(const QImage &composed) {
	return composed.isNull()
		? QImage()
		: composed.scaled(
			kDraftThumbnailSide,
			kDraftThumbnailSide,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
}

[[nodiscard]] Data::StoryDrafts &Drafts(
		not_null<Window::SessionController*> controller) {
	return controller->session().data().stories().drafts();
}

void SaveStoryDraft(
		not_null<Window::SessionController*> controller,
		Data::StoryDraft draft,
		const Data::StoryDraftMedia &media,
		Fn<void()> close) {
	const auto id = Drafts(controller).save(std::move(draft), media);
	if (!id) {
		controller->showToast(tr::lng_story_draft_failed(tr::now));
		return;
	}
	controller->showToast(tr::lng_story_draft_saved(tr::now));
	close();
}

template <typename Widget>
void SetupStoryEditorClose(
		not_null<Window::SessionController*> controller,
		not_null<Widget*> raw,
		not_null<Ui::RpWidget*> promptParent,
		const StoryEditorSetup &setup,
		Fn<void()> close,
		Fn<void()> saveDraft) {
	raw->cancelRequests() | rpl::on_next([=] {
		const auto changed = raw->edited()
			|| (raw->caption() != setup.caption)
			|| (raw->period() != InitialPeriod(setup));
		if (!changed) {
			close();
			return;
		}
		ShowDiscardStoryPrompt(promptParent, {
			.draft = (setup.draftId != 0),
			.allowDraft = !setup.editing,
			.discard = [=] {
				if (setup.draftId) {
					Drafts(controller).remove(setup.draftId);
				}
				close();
			},
			.saveDraft = saveDraft,
		});
	}, raw->lifetime());
}

[[nodiscard]] Editor::StoryOptions StoryEditorOptions(
		not_null<Window::SessionController*> controller,
		const StoryEditorSetup &setup) {
	return {
		.show = controller->uiShow(),
		.caption = setup.caption,
		.period = setup.period ? setup.period : Api::kStoryDefaultPeriod,
		.periodChoice = !setup.editing,
		.sceneItems = setup.sceneItems,
		.sceneFiles = setup.sceneFiles,
		.storyAudio = setup.storyAudio,
		.storyAudioSelected = setup.storyAudioSelected,
		.storyLink = setup.storyLink,
	};
}

[[nodiscard]] QString StoryEditorConfirm(const StoryEditorSetup &setup) {
	return setup.editing
		? tr::lng_box_done(tr::now)
		: tr::lng_intro_next(tr::now);
}

[[nodiscard]] QImage EmptyStoryCanvas() {
	auto result = QImage(
		QSize(Api::kStoryPhotoWidth, Api::kStoryPhotoHeight),
		QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	return result;
}

[[nodiscard]] QImage StoryVideoThumbnail(
		const QString &path,
		const QByteArray &content,
		const Storage::PhotoEditorMedia &probed,
		QSize fallbackSize) {
	auto thumbnail = probed.image;
	if (thumbnail.isNull()) {
		thumbnail = Media::Video::ExtractFrame(
			path,
			content,
			0,
			QSize(kCanvasSampleSide, kCanvasSampleSide));
	}
	if (thumbnail.isNull()) {
		thumbnail = QImage(
			fallbackSize,
			QImage::Format_ARGB32_Premultiplied);
		thumbnail.fill(Qt::black);
	}
	return thumbnail;
}

[[nodiscard]] Editor::EditorData StoryPhotoEditorData(
		not_null<Window::SessionController*> controller,
		const StoryEditorSetup &setup,
		const QImage &media,
		const Media::Encode::CanvasBackground &background,
		const Editor::StoryVideoObject &videoObject) {
	auto story = StoryEditorOptions(controller, setup);
	story.canvasMedia = media;
	story.canvasBackground = background;
	story.videoObject = videoObject;
	story.roundVideo = videoObject.empty();
	return {
		.confirm = StoryEditorConfirm(setup),
		.exactSize = QSize(Api::kStoryPhotoWidth, Api::kStoryPhotoHeight),
		.keepAspectRatio = true,
		.composeAnimated = true,
		.composeSound = true,
		.hasSaveButton = true,
		.story = std::move(story),
	};
}

[[nodiscard]] QImage ComposeStoryPhoto(
		const QImage &image,
		const Media::Encode::CanvasBackground &background,
		const Editor::PhotoModifications &mods,
		bool withPaint = true) {
	auto media = image;
	if (mods.angle || mods.flipped) {
		auto transform = QTransform();
		if (mods.flipped) {
			transform.scale(-1, 1);
		}
		if (mods.angle) {
			transform.rotate(mods.angle);
		}
		media = media.transformed(transform);
	}
	if (mods.enhance > 0) {
		media = Editor::EnhanceImage(std::move(media), mods.enhance);
	}
	const auto canvas = QSize(Api::kStoryPhotoWidth, Api::kStoryPhotoHeight);
	auto result = Media::Encode::FitOnCanvas(
		media,
		canvas,
		background,
		mods.canvasMediaRect);
	if (withPaint && mods.paint) {
		auto paint = Editor::PhotoModifications();
		paint.paint = mods.paint;
		result = Editor::ImageModified(std::move(result), paint);
	}
	return result;
}

[[nodiscard]] Editor::StoryVideoObject VideoObjectFromScene(
		const std::shared_ptr<Editor::Scene> &scene,
		QRectF &rect) {
	if (!scene) {
		return {};
	}
	for (const auto &item : scene->items()) {
		const auto clip = item->videoClip();
		if (!clip || !item->isNormalStatus()) {
			continue;
		}
		const auto base = static_cast<Editor::ItemBase*>(item.get());
		const auto &source = *clip->source();
		const auto placement = base->placement();
		const auto width = placement.size;
		const auto height = width * placement.aspectRatio;
		const auto &position = placement.position;
		rect = QRectF(
			position.x() - width / 2.,
			position.y() - height / 2.,
			width,
			height);
		return {
			.path = source.path,
			.content = source.content,
			.thumbnail = source.thumbnail,
			.duration = clip->duration(),
			.hasAudio = clip->hasAudio(),
			.scale = placement.scale,
			.rotation = placement.rotation,
			.from = clip->trim().from,
			.till = clip->trim().till,
			.volume = clip->volume(),
			.flipped = base->flipped(),
		};
	}
	return {};
}

[[nodiscard]] int StoryOutputMaxSide(
		const QImage &image,
		bool videoBased,
		const Editor::PhotoModifications &mods) {
	auto longSide = videoBased
		? 0
		: std::max(image.width(), image.height());
	auto videoRect = QRectF();
	const auto video = VideoObjectFromScene(mods.paint, videoRect);
	longSide = std::max({
		longSide,
		video.thumbnail.width(),
		video.thumbnail.height(),
	});
	return (longSide >= Api::kStoryPhotoHeight)
		? Api::kStoryPhotoHeight
		: Api::kStoryVideoHeight;
}

[[nodiscard]] bool UntouchedVideoOnly(const Editor::Scene *scene) {
	if (!scene || scene->audio()) {
		return false;
	}
	auto normalCount = 0;
	auto video = (Editor::ItemVideo*)nullptr;
	for (const auto &item : scene->items()) {
		if (item->isNormalStatus()) {
			++normalCount;
			if (item->type() == Editor::ItemVideo::Type) {
				video = static_cast<Editor::ItemVideo*>(item.get());
			}
		}
	}
	return (normalCount == 1)
		&& video
		&& !video->roundVideo()
		&& video->atInitialState();
}

[[nodiscard]] bool StoryPassthroughDimensions(QSize size) {
	return (size == QSize(Api::kStoryVideoWidth, Api::kStoryVideoHeight))
		|| (size == QSize(Api::kStoryPhotoWidth, Api::kStoryPhotoHeight));
}

[[nodiscard]] bool PhotoStoryPassthroughFits(
		const QImage &image,
		const QString &originalPath,
		int64 fileSize = 0) {
	if (!StoryPassthroughDimensions(image.size())
		|| (fileSize > Api::kStoryPhotoMaxFileSize)) {
		return false;
	}
	const auto mime = Core::MimeTypeForFile(QFileInfo(originalPath)).name();
	return (mime == u"image/jpeg"_q);
}

[[nodiscard]] bool VideoStoryPlacementFits(
		const Editor::Scene &scene,
		const QRectF &rect) {
	const auto canvas = scene.canvasRect();
	if (canvas.isEmpty() || rect.isEmpty()) {
		return false;
	}
	return (rect.width() >= canvas.width() * 0.95)
		&& (rect.width() <= canvas.width() * 1.25)
		&& (rect.height() >= canvas.height() * 0.95)
		&& (rect.height() <= canvas.height() * 1.25)
		&& (std::abs(rect.center().x() - canvas.center().x())
			<= canvas.width() * 0.05)
		&& (std::abs(rect.center().y() - canvas.center().y())
			<= canvas.height() * 0.05);
}

[[nodiscard]] std::optional<Api::SendStoryMedia> StoryVideoPassthroughMedia(
		const Editor::Scene &scene,
		const QRectF &videoRect,
		const Editor::StoryVideoObject &video,
		QImage cover) {
	if (video.path.isEmpty()
		|| video.flipped
		|| (std::abs(video.volume - 1.) > 0.01)
		|| (video.from > 0)
		|| (video.till > 0
			&& video.duration > 0
			&& video.till < video.duration)
		|| (std::abs(video.rotation) > 0.01)
		|| (std::abs(video.scale - 1.) > 0.001)
		|| !VideoStoryPlacementFits(scene, videoRect)
		|| (Core::MimeTypeForFile(QFileInfo(video.path)).name()
			!= u"video/mp4"_q)) {
		return std::nullopt;
	}
	const auto info = Media::Encode::ProbeSource(video.path);
	if (info.empty()
		|| !info.videoH264
		|| (info.rotation != 0)
		|| (info.hasAudio && !info.audioRemuxable)
		|| (info.duration <= 0)
		|| (info.duration > Api::kStoryVideoMaxDuration)
		|| (info.fileSize > Api::kStoryVideoMaxFileSize)
		|| !Media::Encode::IsFaststartMp4(video.path)) {
		return std::nullopt;
	}
	if (!StoryPassthroughDimensions(info.display)) {
		return std::nullopt;
	}
	return Api::SendStoryMedia{
		Api::SendStoryVideo{
			.cover = std::move(cover),
			.path = video.path,
			.hasAudio = info.hasAudio,
		},
	};
}

[[nodiscard]] Api::SendStoryMedia StoryPhotoMedia(
		QImage composed,
		const QImage &image,
		bool videoBased,
		const Media::Encode::CanvasBackground &background,
		const Editor::PhotoModifications &mods,
		const QString &original) {
	const auto maxSide = StoryOutputMaxSide(image, videoBased, mods);
	if (maxSide < Api::kStoryPhotoHeight) {
		composed = composed.scaled(
			QSize(Api::kStoryVideoWidth, Api::kStoryVideoHeight),
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
	}
	auto videoRect = QRectF();
	const auto video = VideoObjectFromScene(mods.paint, videoRect);
	const auto videoStory = videoBased
		|| (!video.empty() && UntouchedVideoOnly(mods.paint.get()));
	if (videoStory) {
		const auto canvasTouched = mods.angle
			|| mods.flipped
			|| mods.crop.isValid()
			|| !mods.canvasMediaRect.isEmpty();
		if (!canvasTouched && UntouchedVideoOnly(mods.paint.get())) {
			if (auto passthrough = StoryVideoPassthroughMedia(
					*mods.paint,
					videoRect,
					video,
					composed)) {
				return std::move(*passthrough);
			}
		}
	} else if (!original.isEmpty()
		&& !mods
		&& PhotoStoryPassthroughFits(
			image,
			original,
			QFileInfo(original).size())) {
		return Api::SendStoryPhoto{
			.image = std::move(composed),
			.path = original,
		};
	}
	const auto animated = !video.empty()
		|| (mods.paint
			&& (mods.paint->hasAnimatedItems()
				|| mods.paint->hasSoundResult()));
	if (!animated) {
		return Api::SendStoryPhoto{ std::move(composed) };
	}
	auto paintOnly = Editor::PhotoModifications();
	paintOnly.paint = mods.paint;
	auto job = Editor::ComposeAnimatedJob(
		ComposeStoryPhoto(image, background, mods, false),
		paintOnly,
		{
			.maxSide = maxSide,
			.maxDuration = Api::kStoryVideoMaxDuration,
		});
	job.silentLoop = false;
	return Api::SendStoryVideo{
		.cover = std::move(composed),
		.job = std::make_shared<Media::Encode::Job>(std::move(job)),
	};
}

void SaveStoryMediaToFile(
		not_null<Window::SessionController*> controller,
		QPointer<Editor::PhotoEditor> editor,
		const Api::SendStoryMedia &media,
		const QString &path) {
	v::match(media, [&](const Api::SendStoryPhoto &data) {
		const auto sameExtension = !data.path.isEmpty()
			&& (QFileInfo(path).suffix().toLower()
				== QFileInfo(data.path).suffix().toLower());
		if (sameExtension) {
			if (QFileInfo(path) == QFileInfo(data.path)) {
				controller->showToast(tr::lng_story_save_file_done(tr::now));
				return;
			}
			if (QFile::exists(path)) {
				QFile::remove(path);
			}
			if (QFile::copy(data.path, path)) {
				controller->showToast(tr::lng_story_save_file_done(tr::now));
				return;
			}
		}
		const auto ok = data.image.save(path, nullptr, 90);
		if (ok) {
			controller->showToast(tr::lng_story_save_file_done(tr::now));
		} else {
			controller->showToast(tr::lng_story_save_file_failed(tr::now));
		}
	}, [&](const Api::SendStoryVideo &data) {
		if (!data.path.isEmpty()) {
			if (QFileInfo(path) == QFileInfo(data.path)) {
				controller->showToast(tr::lng_story_save_file_done(tr::now));
				return;
			}
			if (QFile::exists(path)) {
				QFile::remove(path);
			}
			const auto ok = QFile::copy(data.path, path);
			if (ok) {
				controller->showToast(tr::lng_story_save_file_done(tr::now));
			} else {
				controller->showToast(
					tr::lng_story_save_file_failed(tr::now));
			}
		} else if (data.job) {
			if (editor) {
				editor->setSaveEnabled(false);
			}
			controller->showToast(tr::lng_story_preparing_video(tr::now));
			auto copy = *data.job;
			crl::async([=, weak = base::make_weak(controller)]() mutable {
				const auto encoded = Media::Encode::Run(std::move(copy));
				const auto ok = !encoded.empty() && [&] {
					QFile file(path);
					if (!file.open(
							QIODevice::WriteOnly | QIODevice::Truncate)) {
						return false;
					}
					return (file.write(encoded.bytes)
						== encoded.bytes.size());
				}();
				crl::on_main(weak, [=] {
					if (editor) {
						editor->setSaveEnabled(true);
					}
					if (const auto strong = weak.get()) {
						strong->showToast(ok
							? tr::lng_story_save_file_done(tr::now)
							: tr::lng_story_save_file_failed(tr::now));
					}
				});
			});
		} else if (data.source) {
			if (editor) {
				editor->setSaveEnabled(false);
			}
			controller->showToast(tr::lng_story_preparing_video(tr::now));
			auto source = *data.source;
			crl::async([=, weak = base::make_weak(controller)] {
				const auto result = Media::Encode::TranscodeVideo(source);
				const auto ok = !result.empty() && [&] {
					if (QFile::exists(path)) {
						QFile::remove(path);
					}
					return QFile::copy(result.path, path);
				}();
				if (!result.path.isEmpty()) {
					QFile::remove(result.path);
				}
				crl::on_main(weak, [=] {
					if (editor) {
						editor->setSaveEnabled(true);
					}
					if (const auto strong = weak.get()) {
						strong->showToast(ok
							? tr::lng_story_save_file_done(tr::now)
							: tr::lng_story_save_file_failed(tr::now));
					}
				});
			});
		} else {
			controller->showToast(tr::lng_story_save_file_failed(tr::now));
		}
	}, [&](const Api::SendStoryRepost &) {
		controller->showToast(tr::lng_story_save_file_failed(tr::now));
	});
}

void SaveStoryToFile(
		not_null<Window::SessionController*> controller,
		QPointer<Editor::PhotoEditor> editor,
		const QImage &image,
		const Media::Encode::CanvasBackground &background,
		const Editor::PhotoModifications &mods,
		bool videoBased,
		const QString &original) {
	auto composed = ComposeStoryPhoto(image, background, mods);
	if (composed.isNull()) {
		controller->showToast(tr::lng_bad_photo(tr::now));
		return;
	}
	auto media = StoryPhotoMedia(
		composed,
		image,
		videoBased,
		background,
		mods,
		original);
	const auto isVideo = std::holds_alternative<Api::SendStoryVideo>(media);
	const auto filter = isVideo
		? (u"Video Files (*.mp4);;"_q + FileDialog::AllFilesFilter())
		: (u"JPEG Image (*.jpg);;PNG Image (*.png);;"_q
			+ FileDialog::AllFilesFilter());
	const auto defaultName = filedialogDefaultName(
		u"story"_q,
		isVideo ? u".mp4"_q : u".jpg"_q);
	const auto title = isVideo
		? tr::lng_save_video(tr::now)
		: tr::lng_save_photo(tr::now);

	FileDialog::GetWritePath(
		controller->widget().get(),
		title,
		filter,
		defaultName,
		crl::guard(controller, [=, media = std::move(media)](
				const QString &path) {
			if (path.isEmpty()) {
				return;
			}
			auto finalPath = path;
			if (isVideo) {
				if (!finalPath.endsWith(u".mp4"_q, Qt::CaseInsensitive)) {
					finalPath += u".mp4"_q;
				}
			} else {
				if (!finalPath.endsWith(u".jpg"_q, Qt::CaseInsensitive)
					&& !finalPath.endsWith(u".jpeg"_q, Qt::CaseInsensitive)
					&& !finalPath.endsWith(u".png"_q, Qt::CaseInsensitive)) {
					finalPath += u".jpg"_q;
				}
			}
			SaveStoryMediaToFile(controller, editor, media, finalPath);
		}));
}

[[nodiscard]] Editor::StoryAreas CollectAreas(
		const Editor::PhotoModifications &mods,
		QSize canvas) {
	return mods.paint
		? Editor::CollectStoryAreas(mods.paint.get(), canvas)
		: Editor::StoryAreas();
}

[[nodiscard]] QImage StillFromScene(
		const QImage &image,
		const Media::Encode::CanvasBackground &background,
		const Editor::PhotoModifications &mods) {
	if (!mods.paint) {
		return QImage();
	}
	const auto hasStatic = ranges::any_of(
		mods.paint->items(),
		[](const auto &item) {
			return item->isNormalStatus() && !item->videoClip();
		});
	if (!hasStatic) {
		return QImage();
	}
	auto hidden = std::vector<QGraphicsItem *>();
	for (const auto &item : mods.paint->items()) {
		if (item->videoClip() && item->isVisible()) {
			item->setVisible(false);
			hidden.push_back(item.get());
		}
	}
	auto still = ComposeStoryPhoto(image, background, mods, false);
	if (!hidden.empty()) {
		auto paintOnly = Editor::PhotoModifications();
		paintOnly.paint = mods.paint;
		still = Editor::ImageModified(std::move(still), paintOnly);
	}
	for (const auto item : hidden) {
		item->setVisible(true);
	}
	return still;
}

[[nodiscard]] bool AcceptableStoryFile(const Ui::PreparedFile &file) {
	using Type = Ui::PreparedFile::Type;
	return (file.type == Type::Photo)
		|| ((file.type == Type::Video) && file.canEditVideo());
}

[[nodiscard]] bool CheckStoryList(
		not_null<Window::SessionController*> controller,
		const Ui::PreparedList &list) {
	if (list.error != Ui::PreparedList::Error::None
		|| list.files.size() != 1
		|| !AcceptableStoryFile(list.files.front())) {
		controller->showToast(tr::lng_send_media_invalid_files(tr::now));
		return false;
	}
	return true;
}

struct CollectedStoryDraft {
	Data::StoryDraft draft;
	Data::StoryDraftMedia media;
	uint64 fingerprint = 0;
};

[[nodiscard]] CollectedStoryDraft CollectStoryDraftState(
		const QImage &image,
		const Media::Encode::CanvasBackground &background,
		const Editor::PhotoModifications &mods,
		const StoryEditorSetup &setup,
		TextWithTags caption,
		int period) {
	auto result = CollectedStoryDraft();
	const auto painted = mods.paint && ranges::any_of(
		mods.paint->items(),
		[](const auto &item) {
			return item->isNormalStatus() && item->bakes();
		});
	auto videoRect = QRectF();
	const auto video = VideoObjectFromScene(mods.paint, videoRect);
	const auto composed = ComposeStoryPhoto(image, background, mods);
	result.draft = Data::StoryDraft{
		.id = setup.draftId,
		.peer = setup.peer,
		.caption = std::move(caption),
		.period = period,
		.backgroundTop = background.valid() ? background.top : QColor(),
		.backgroundBottom = background.valid()
			? background.bottom
			: QColor(),
		.thumbnail = DraftThumbnail(composed),
	};
	auto &draft = result.draft;
	auto &media = result.media;
	if (mods.paint) {
		auto files = std::vector<Data::StoryDraftSceneFile>();
		const auto sink = Editor::SceneDraftFiles{
			.image = [&](const QImage &image) {
				files.push_back({ .image = image });
				return int(files.size()) - 1;
			},
			.file = [&](const QString &path,
					const QByteArray &content,
					const QString &suffix) {
				files.push_back({
					.path = path,
					.content = content,
					.suffix = suffix,
				});
				return int(files.size()) - 1;
			},
		};
		draft.scene = Editor::SerializeSceneItems(
			Editor::CollectSceneItems(mods.paint.get(), sink));
		media.sceneFiles = std::move(files);
		result.fingerprint = Editor::SceneFingerprint(mods.paint.get());
		if (const auto audio = mods.paint->audio()) {
			if (!audio->empty()) {
				auto &to = draft.audio;
				to.title = audio->title;
				to.performer = audio->performer;
				to.duration = audio->duration;
				to.from = audio->from;
				to.till = audio->till;
				to.volume = audio->volume;
				to.selected = mods.paint->audioSelected();
				media.audioPath = audio->path;
				media.audioContent = audio->content;
				media.audioCover = audio->cover;
			}
		}
	}
	const auto hasScene = !draft.scene.isEmpty();
	if (!video.empty()) {
		draft.video = true;
		draft.dimensions = video.thumbnail.size();
		draft.duration = video.duration;
		draft.edits = {
			.flipped = video.flipped,
			.from = video.from,
			.till = video.till,
		};
		draft.videoPlacement = {
			.rect = videoRect,
			.scale = video.scale,
			.rotation = video.rotation,
			.volume = video.volume,
		};
		media.photo = hasScene
			? ComposeStoryPhoto(image, background, {})
			: StillFromScene(image, background, mods);
		media.videoPath = video.path;
		media.videoContent = video.content;
	} else if (!painted || hasScene) {
		media.photo = image;
		draft.edits = {
			.angle = mods.angle,
			.flipped = mods.flipped,
			.canvasMediaRect = mods.canvasMediaRect,
			.enhance = mods.enhance,
		};
	} else {
		media.photo = composed;
	}
	if (!draft.video) {
		draft.dimensions = media.photo.size();
	}
	return result;
}

void OpenStoryPhotoEditor(
		not_null<Window::SessionController*> controller,
		QImage image,
		Editor::PhotoModifications initial,
		const StoryEditorSetup &setup,
		Editor::StoryVideoObject object,
		QString originalPath,
		Media::Encode::CanvasBackground background,
		StoryEditorDone done) {
	const auto parent = controller->widget();
	const auto show = controller->uiShow();
	const auto videoBased = !object.empty();
	const auto original = initial ? QString() : std::move(originalPath);
	background = background.valid()
		? background
		: (object.empty()
			? Media::Encode::DominantCanvasBackground(image)
			: Media::Encode::DominantCanvasBackground(object.thumbnail));
	const auto photo = std::make_shared<Image>(Media::Encode::FitOnCanvas(
		image,
		QSize(Api::kStoryPhotoWidth, Api::kStoryPhotoHeight),
		background));
	auto editor = base::make_unique_q<Editor::PhotoEditor>(
		parent,
		show,
		show,
		photo,
		std::move(initial),
		StoryPhotoEditorData(controller, setup, image, background, object));
	const auto raw = editor.get();
	auto layer = std::make_unique<Editor::LayerWidget>(
		parent,
		std::move(editor));
	const auto weak = base::make_weak(layer.get());
	const auto close = [=] {
		if (const auto strong = weak.get()) {
			strong->closeLayer();
		}
	};
	const auto rawPtr = not_null(raw);
	const auto layerPtr = layer.get();
	SetupStoryEditorClose(controller, rawPtr, layerPtr, setup, close, [=] {
		const auto mods = raw->collect();
		auto collected = CollectStoryDraftState(
			image,
			background,
			mods,
			setup,
			raw->caption(),
			raw->period());
		SaveStoryDraft(
			controller,
			std::move(collected.draft),
			collected.media,
			close);
	});
	raw->doneRequests(
	) | rpl::on_next([=](const Editor::PhotoModifications &mods) {
		auto result = ComposeStoryPhoto(image, background, mods);
		if (result.isNull()) {
			controller->showToast(tr::lng_bad_photo(tr::now));
			return;
		}
		auto areas = CollectAreas(mods, result.size());
		auto collected = CollectStoryDraftState(
			image,
			background,
			mods,
			setup,
			raw->caption(),
			raw->period());
		auto fingerprint = collected.fingerprint;
		done({
			.media = StoryPhotoMedia(
				std::move(result),
				image,
				videoBased,
				background,
				mods,
				original),
			.caption = raw->caption(),
			.period = raw->period(),
			.reactions = std::move(areas.reactions),
			.urls = std::move(areas.urls),
			.locations = std::move(areas.locations),
			.weather = std::move(areas.weather),
			.channelPosts = std::move(areas.channelPosts),
			.mediaChanged = raw->edited(),
			.source = std::make_shared<Data::StorySourceSave>(
				Data::StorySourceSave{
					.draft = std::move(collected.draft),
					.media = std::move(collected.media),
					.fingerprint = fingerprint,
				}),
		}, close);
	}, raw->lifetime());
	raw->saveRequests(
	) | rpl::on_next([=](const Editor::PhotoModifications &mods) {
		SaveStoryToFile(
			controller,
			raw,
			image,
			background,
			mods,
			videoBased,
			original);
	}, raw->lifetime());
	show->showLayer(std::move(layer), Ui::LayerOption::KeepOther);
}

void EditStoryFile(
		not_null<Window::SessionController*> controller,
		Ui::PreparedList &&list,
		const StoryEditorSetup &setup,
		StoryEditorDone done) {
	if (!CheckStoryList(controller, list)) {
		return;
	}
	auto &file = list.files.front();
	if (file.type == Ui::PreparedFile::Type::Photo) {
		using Image = Ui::PreparedFileInformation::Image;
		const auto image = std::get_if<Image>(&file.information->media);
		if (!image) {
			controller->showToast(tr::lng_send_media_invalid_files(tr::now));
			return;
		}
		OpenStoryPhotoEditor(
			controller,
			std::move(image->data),
			std::move(image->modifications),
			setup,
			{},
			file.path,
			{},
			std::move(done));
		return;
	}
	using Video = Ui::PreparedFileInformation::Video;
	const auto video = std::get_if<Video>(&file.information->media);
	if (!video || video->thumbnail.isNull()) {
		controller->showToast(tr::lng_send_media_invalid_files(tr::now));
		return;
	}
	OpenStoryPhotoEditor(
		controller,
		EmptyStoryCanvas(),
		{},
		setup,
		{
			.path = file.path,
			.content = file.content,
			.thumbnail = video->thumbnail,
			.duration = video->duration,
			.hasAudio = video->hasAudio,
			.till = (video->duration > Api::kStoryVideoMaxDuration)
				? Api::kStoryVideoMaxDuration
				: 0,
		},
		{},
		{},
		std::move(done));
}

[[nodiscard]] StoryEditorDone ShowSendBoxWhenEdited(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		uint64 draftId) {
	const auto drafts = &Drafts(controller);
	return [=](StoryEditorResult result, Fn<void()> closeEditor) {
		controller->show(Box(SendStoryBox, SendStoryBoxArgs{
			.controller = controller,
			.peer = peer,
			.media = std::move(result.media),
			.caption = std::move(result.caption),
			.period = result.period,
			.reactions = std::move(result.reactions),
			.urls = std::move(result.urls),
			.locations = std::move(result.locations),
			.weather = std::move(result.weather),
			.channelPosts = std::move(result.channelPosts),
			.source = std::move(result.source),
			.posted = closeEditor,
			.uploaded = draftId
				? Fn<void()>([=] { drafts->remove(draftId); })
				: nullptr,
		}));
	};
}

void WaitForStoryMedia(
		not_null<Window::SessionController*> controller,
		Fn<bool()> ready,
		Fn<bool()> failed,
		Fn<void()> done) {
	if (ready()) {
		done();
		return;
	}
	struct Waiting {
		rpl::lifetime lifetime;
		bool finished = false;
	};
	const auto waiting = std::make_shared<Waiting>();
	controller->session().downloaderTaskFinished(
	) | rpl::on_next(crl::guard(controller, [=] {
		const auto loaded = ready();
		if (waiting->finished || (!loaded && !failed())) {
			return;
		}
		waiting->finished = true;
		crl::on_main([=] {
			waiting->lifetime.destroy();
		});
		if (loaded) {
			done();
		}
	}), waiting->lifetime);
}

void ShowLimitBox(
		not_null<Window::SessionController*> controller,
		rpl::producer<TextWithEntities> text) {
	controller->show(Ui::MakeConfirmBox({
		.text = std::move(text),
		.confirmed = Fn<void(Fn<void()>)>([=](Fn<void()> close) {
			close();
			ShowPremiumPreviewBox(controller, PremiumFeature::Stories);
		}),
		.confirmText = tr::lng_premium_more_about(),
		.title = tr::lng_story_limit_title(),
	}));
}

constexpr auto kWebAppStoryShareMaxFileSize = int64(100 * 1024 * 1024);
constexpr auto kWebAppStoryShareFileLife = 24 * 60 * 60;

[[nodiscard]] StoryEditorSetup WebAppStorySetup(
		not_null<Window::SessionController*> controller,
		const WebAppStoryShare &args) {
	auto link = std::optional<Editor::LinkPreview>();
	if (!args.widgetLinkUrl.isEmpty() && controller->session().premium()) {
		link = Editor::LinkPreview{
			.url = args.widgetLinkUrl,
			.name = args.widgetLinkName,
		};
	}
	return {
		.caption = args.caption,
		.storyLink = std::move(link),
	};
}

void RemoveStaleWebAppStoryFiles() {
	const auto now = QDateTime::currentDateTime();
	const auto list = QDir(QDir::tempPath()).entryInfoList(
		{ u"tdesktop_story_share_*"_q },
		QDir::Files);
	for (const auto &info : list) {
		if (info.lastModified().secsTo(now) > kWebAppStoryShareFileLife) {
			QFile::remove(info.absoluteFilePath());
		}
	}
}

void OpenWebAppStoryDownload(
		not_null<Window::SessionController*> controller,
		not_null<UserData*> peer,
		const WebAppStoryShare &args) {
	static auto active = 0;
	const auto failToast = crl::guard(controller, [=] {
		controller->showToast(tr::lng_bot_share_story_failed(tr::now));
	});
	if (active > 0) {
		failToast();
		return;
	}
	RemoveStaleWebAppStoryFiles();
	const auto session = &controller->session();
	const auto path = QDir::tempPath()
		+ u"/tdesktop_story_share_"_q
		+ QString::number(base::RandomValue<uint64>(), 16);
	const auto holder = controller->lifetime().make_state<
		std::shared_ptr<webFileLoader>>();
	*holder = std::make_shared<webFileLoader>(
		session,
		args.mediaUrl,
		path,
		WebRequestType::FullLoad);
	const auto loader = holder->get();
	++active;
	loader->lifetime().add([] { --active; });
	const auto cancelled = controller->lifetime().make_state<bool>(false);
	const auto release = crl::guard(controller, [=] {
		crl::on_main(controller, [=] {
			*holder = nullptr;
		});
	});
	loader->updates(
	) | rpl::on_next_error_done(crl::guard(controller, [=] {
		if (*cancelled) {
			return;
		}
		const auto total = loader->fullSize();
		if ((total >= 0 && total > kWebAppStoryShareMaxFileSize)
			|| loader->currentOffset() > kWebAppStoryShareMaxFileSize) {
			*cancelled = true;
			loader->cancel();
			QFile::remove(path);
			failToast();
			release();
		}
	}), crl::guard(controller, [=](FileLoader::Error) {
		if (!*cancelled) {
			*cancelled = true;
			QFile::remove(path);
			failToast();
			release();
		}
	}), crl::guard(controller, [=] {
		if (*cancelled) {
			return;
		}
		release();
		controller->window().activate();
		PostStoryWithFile(
			controller,
			peer,
			path,
			WebAppStorySetup(controller, args));
	}), loader->lifetime());
	loader->start();
}

} // namespace

void ChooseStoryMedia(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		StoryEditorSetup setup,
		StoryEditorDone done) {
	const auto parent = controller->widget();
	const auto premium = controller->session().premium();
	const auto callback = crl::guard(controller, [=](
			FileDialog::OpenResult &&result) {
		const auto showError = [=](tr::phrase<> text) {
			controller->showToast(text(tr::now));
		};
		const auto check = [=](const Ui::PreparedList &list) {
			return CheckStoryList(controller, list);
		};
		auto list = Storage::PreparedFileFromFilesDialog(
			std::move(result),
			check,
			showError,
			st::sendMediaPreviewSize,
			premium);
		if (list) {
			EditStoryFile(controller, std::move(*list), setup, done);
		}
	});
	FileDialog::GetOpenPath(
		parent.get(),
		tr::lng_story_choose_media(tr::now),
		FileDialog::PhotoVideoFilesFilter(),
		callback);
}

bool CanPostStory(not_null<PeerData*> peer) {
	return peer->canPostStories()
		&& peer->session().appConfig().storiesPostingAvailable();
}

bool CanRepostStory(not_null<Data::Story*> story) {
	return !story->mine()
		&& story->canShare()
		&& (story->photo() || story->document())
		&& CanPostStory(story->session().user());
}

bool CanRepostMessageToStory(not_null<HistoryItem*> item) {
	const auto session = &item->history()->session();
	if (!CanPostStory(session->user())) {
		return false;
	} else if (item->isSponsored() || item->isService()) {
		return false;
	}
	const auto media = item->media();
	if (media && (media->poll() || media->sharedContact())) {
		return false;
	}
	const auto peer = item->history()->peer;
	const auto isBroadcastChannel = peer->isChannel() && !peer->isMegagroup();
	if (!isBroadcastChannel) {
		const auto forwarded = item->Get<HistoryMessageForwarded>();
		const auto savedFrom = forwarded ? forwarded->savedFromPeer : nullptr;
		const auto channel = savedFrom ? savedFrom->asChannel() : nullptr;
		const auto fwdBroadcast = channel
			&& !channel->isMegagroup()
			&& channel->isPublic();
		if (!fwdBroadcast) {
			return false;
		}
	}
	return item->allowsForward() && Editor::CanRenderMessage(item);
}

bool CanEditStory(not_null<Data::Story*> story) {
	const auto peer = story->peer();
	return (story->photo() || story->document())
		&& (story->mine() || peer->canEditStories());
}

bool CanEditStoryPrivacy(not_null<Data::Story*> story) {
	return story->mine() && story->peer()->isSelf();
}

void PostStory(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		CanSendStoryErrorContext ctx) {
	const auto session = &controller->session();
	if (ctx.setLoading) {
		ctx.setLoading(true);
	}
	session->api().stories().checkCanSend(peer, crl::guard(controller, [=
		, ctx = std::move(ctx)](Api::CanSendStory result) mutable {
		if (!result.allowed()) {
			ShowCanSendStoryError(controller, peer, result, std::move(ctx));
			return;
		}
		if (ctx.setLoading) {
			ctx.setLoading(false);
		}
		ChooseStoryMedia(
			controller,
			peer,
			StoryEditorSetup{ .peer = peer->id },
			ShowSendBoxWhenEdited(controller, peer, 0));
	}));
}

void PostStoryWithFile(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		const QString &path,
		StoryEditorSetup setup) {
	const auto session = &controller->session();
	setup.peer = peer->id;
	session->api().stories().checkCanSend(peer, crl::guard(controller, [=](
			Api::CanSendStory result) mutable {
		if (!result.allowed()) {
			ShowCanSendStoryError(controller, peer, result);
			return;
		}
		EditStoryFile(
			controller,
			Storage::PrepareMediaList(
				QStringList(path),
				st::sendMediaPreviewSize,
				session->premium()),
			std::move(setup),
			ShowSendBoxWhenEdited(controller, peer, 0));
	}));
}

void PostStoryFromWebApp(
		not_null<Window::SessionController*> controller,
		WebAppStoryShare args) {
	const auto session = &controller->session();
	const auto self = session->user();
	session->api().stories().checkCanSend(self, crl::guard(controller, [=](
			Api::CanSendStory result) {
		if (!result.allowed()) {
			ShowCanSendStoryError(controller, self, result);
			return;
		}
		OpenWebAppStoryDownload(controller, self, args);
	}));
}

void PostStoryWithMimeData(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		not_null<const QMimeData*> data) {
	const auto session = &controller->session();
	const auto setup = StoryEditorSetup{ .peer = peer->id };
	const auto done = ShowSendBoxWhenEdited(controller, peer, 0);
	if (data->hasImage()) {
		const auto read = Core::ReadMimeImage(data);
		auto bytes = read.content;
		auto image = read.image;
		session->api().stories().checkCanSend(peer, crl::guard(
			controller,
			[=](Api::CanSendStory result) mutable {
				if (!result.allowed()) {
					ShowCanSendStoryError(controller, peer, result);
					return;
				}
				EditStoryFile(
					controller,
					Storage::PrepareMediaFromImage(
						std::move(image),
						std::move(bytes),
						st::sendMediaPreviewSize),
					setup,
					done);
			}));
		return;
	}
	const auto urls = Core::ReadMimeUrls(data);
	session->api().stories().checkCanSend(peer, crl::guard(controller, [=](
			Api::CanSendStory result) {
		if (!result.allowed()) {
			ShowCanSendStoryError(controller, peer, result);
			return;
		}
		EditStoryFile(
			controller,
			Storage::PrepareMediaList(
				urls,
				st::sendMediaPreviewSize,
				session->premium()),
			setup,
			done);
	}));
}

void PostStoryDraft(
		not_null<Window::SessionController*> controller,
		uint64 draftId) {
	const auto session = &controller->session();
	const auto drafts = &Drafts(controller);
	const auto found = drafts->find(draftId);
	if (!found) {
		return;
	}
	const auto draft = *found;
	const auto peer = session->data().peer(draft.peer
		? draft.peer
		: session->userPeerId());
	session->api().stories().checkCanSend(peer, crl::guard(controller, [=](
			Api::CanSendStory result) {
		if (!result.allowed()) {
			ShowCanSendStoryError(controller, peer, result);
			return;
		}
		const auto setup = StoryEditorSetup{
			.caption = draft.caption,
			.period = draft.period,
			.peer = peer->id,
			.draftId = draft.id,
			.sceneItems = draft.scene,
			.sceneFiles = draft.sceneFiles,
			.storyAudio = Editor::AudioTrack{
				.path = draft.audioPath,
				.title = draft.audio.title,
				.performer = draft.audio.performer,
				.cover = draft.audioCover,
				.duration = draft.audio.duration,
				.from = draft.audio.from,
				.till = draft.audio.till,
				.volume = draft.audio.volume,
			},
			.storyAudioSelected = draft.audio.selected,
		};
		const auto done = ShowSendBoxWhenEdited(controller, peer, draft.id);
		const auto &edits = draft.edits;
		if (!draft.video) {
			auto image = QImage(draft.mediaPath);
			if (image.isNull()) {
				controller->showToast(tr::lng_story_draft_lost(tr::now));
				drafts->remove(draft.id);
				return;
			}
			auto initial = Editor::PhotoModifications();
			initial.angle = edits.angle;
			initial.flipped = edits.flipped;
			initial.canvasMediaRect = edits.canvasMediaRect;
			initial.enhance = edits.enhance;
			OpenStoryPhotoEditor(
				controller,
				std::move(image),
				std::move(initial),
				setup,
				{},
				draft.mediaPath,
				Media::Encode::CanvasBackground{
					.top = draft.backgroundTop,
					.bottom = draft.backgroundBottom,
				},
				done);
			return;
		}
		if (!draft.scene.isEmpty()) {
			auto canvas = EmptyStoryCanvas();
			if (!draft.stillPath.isEmpty()) {
				auto still = QImage(draft.stillPath);
				if (!still.isNull()) {
					canvas = std::move(still);
				}
			}
			OpenStoryPhotoEditor(
				controller,
				std::move(canvas),
				{},
				setup,
				{},
				{},
				Media::Encode::CanvasBackground{
					.top = draft.backgroundTop,
					.bottom = draft.backgroundBottom,
				},
				done);
			return;
		}
		const auto probed = Storage::ReadPhotoEditorMedia(
			draft.mediaPath,
			{});
		auto canvas = EmptyStoryCanvas();
		if (!draft.stillPath.isEmpty()) {
			auto still = QImage(draft.stillPath);
			if (!still.isNull()) {
				canvas = std::move(still);
			}
		}
		const auto &placement = draft.videoPlacement;
		OpenStoryPhotoEditor(
			controller,
			std::move(canvas),
			{},
			setup,
			{
				.path = draft.mediaPath,
				.thumbnail = StoryVideoThumbnail(
					draft.mediaPath,
					{},
					probed,
					draft.dimensions),
				.duration = (probed.videoDuration
					? probed.videoDuration
					: draft.duration),
				.hasAudio = probed.videoHasAudio,
				.position = placement.rect.isEmpty()
					? QPointF()
					: placement.rect.center(),
				.size = placement.rect.width(),
				.scale = placement.scale,
				.rotation = placement.rotation,
				.from = edits.from,
				.till = edits.till,
				.volume = placement.volume,
				.flipped = edits.flipped,
			},
			{},
			Media::Encode::CanvasBackground{
				.top = draft.backgroundTop,
				.bottom = draft.backgroundBottom,
			},
			done);
	}));
}

void EditStory(
		not_null<Window::SessionController*> controller,
		not_null<Data::Story*> story) {
	const auto session = &controller->session();
	const auto id = story->fullId();
	const auto origin = Data::FileOrigin(Data::FileOriginStory(id));
	const auto source = session->data().stories().editSources().find(
		story->peer()->id,
		id.story,
		story->photo() ? story->photo()->id : PhotoId(0),
		story->document() ? story->document()->id : DocumentId(0));
	const auto fromSource = (source.storyId == id.story);
	const auto setup = StoryEditorSetup{
		.caption = TextWithTags{
			story->caption().text,
			TextUtilities::ConvertEntitiesToTextTags(
				story->caption().entities),
		},
		.editing = true,
		.sceneItems = fromSource ? source.draft.scene : QJsonArray(),
		.sceneFiles = fromSource ? source.draft.sceneFiles : QStringList(),
		.storyAudio = fromSource
			? Editor::AudioTrack{
				.path = source.draft.audioPath,
				.title = source.draft.audio.title,
				.performer = source.draft.audio.performer,
				.duration = source.draft.audio.duration,
				.from = source.draft.audio.from,
				.till = source.draft.audio.till,
				.volume = source.draft.audio.volume,
			}
			: Editor::AudioTrack(),
		.storyAudioSelected = fromSource
			? source.draft.audio.selected
			: true,
	};
	const auto done = [=](StoryEditorResult result, Fn<void()> close) {
		const auto found = session->data().stories().lookup(id);
		if (found) {
			const auto captionChanged = (result.caption != setup.caption);
			const auto sources
				= &session->data().stories().editSources();
			const auto pending = result.source
				? base::RandomValue<uint64>()
				: uint64(0);
			if (pending) {
				sources->savePending(
					pending,
					*result.source,
					source.send);
			}
			if (captionChanged || result.mediaChanged) {
				session->api().stories().send({
					.peer = (*found)->peer(),
					.media = (result.mediaChanged
						? std::optional(std::move(result.media))
						: std::nullopt),
					.caption = std::move(result.caption),
					.reactions = std::move(result.reactions),
					.urls = std::move(result.urls),
					.locations = std::move(result.locations),
					.weather = std::move(result.weather),
					.editId = (*found)->id(),
					.editCaption = captionChanged,
					.sent = pending
						? Fn<void(StoryId, TimeId)>(
							[=](StoryId, TimeId expiresAt) {
								sources->finalize(
									pending,
									id.story,
									expiresAt);
							})
						: nullptr,
					.failed = pending
						? Fn<void(const QString &)>([=](const QString &) {
							sources->removePending(pending);
						})
						: nullptr,
				});
			} else if (pending) {
				sources->finalize(pending, id.story, (*found)->expires());
			}
		}
		close();
	};
	if (fromSource) {
		const auto background = Media::Encode::CanvasBackground{
			.top = source.draft.backgroundTop,
			.bottom = source.draft.backgroundBottom,
		};
		if (!source.draft.video) {
			auto image = QImage(source.draft.mediaPath);
			if (image.isNull()) {
				controller->showToast(tr::lng_story_draft_lost(tr::now));
				return;
			}
			auto initial = Editor::PhotoModifications();
			const auto &edits = source.draft.edits;
			initial.angle = edits.angle;
			initial.flipped = edits.flipped;
			initial.canvasMediaRect = edits.canvasMediaRect;
			initial.enhance = edits.enhance;
			OpenStoryPhotoEditor(
				controller,
				std::move(image),
				std::move(initial),
				setup,
				{},
				source.draft.mediaPath,
				background,
				done);
		} else {
			auto canvas = EmptyStoryCanvas();
			if (!source.draft.stillPath.isEmpty()) {
				auto still = QImage(source.draft.stillPath);
				if (!still.isNull()) {
					canvas = std::move(still);
				}
			}
			OpenStoryPhotoEditor(
				controller,
				std::move(canvas),
				{},
				setup,
				{},
				{},
				background,
				done);
		}
		return;
	}
	if (const auto photo = story->photo()) {
		const auto media = photo->createMediaView();
		media->wanted(Data::PhotoSize::Large, origin);
		const auto ready = [=] {
			return (media->image(Data::PhotoSize::Large) != nullptr);
		};
		const auto failed = [=] {
			return photo->failed(Data::PhotoSize::Large);
		};
		WaitForStoryMedia(controller, ready, failed, [=] {
			OpenStoryPhotoEditor(
				controller,
				media->image(Data::PhotoSize::Large)->original(),
				Editor::PhotoModifications(),
				setup,
				{},
				{},
				{},
				done);
		});
	} else if (const auto document = story->document()) {
		const auto media = document->createMediaView();
		document->save(origin, QString());
		const auto ready = [=] {
			return media->loaded();
		};
		const auto failed = [=] {
			return (document->status == FileDownloadFailed);
		};
		WaitForStoryMedia(controller, ready, failed, [=] {
			const auto path = document->filepath();
			const auto content = path.isEmpty()
				? media->bytes()
				: QByteArray();
			const auto probed = Storage::ReadPhotoEditorMedia(path, content);
			OpenStoryPhotoEditor(
				controller,
				EmptyStoryCanvas(),
				Editor::PhotoModifications(),
				setup,
				{
					.path = path,
					.content = content,
					.thumbnail = StoryVideoThumbnail(
						path,
						content,
						probed,
						document->dimensions),
					.duration = (probed.videoDuration
						? probed.videoDuration
						: document->duration()),
					.hasAudio = probed.videoHasAudio,
				},
				{},
				{},
				done);
		});
	}
}

void EditStoryPrivacy(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<Data::Story*> story) {
	show->showBox(Box(SendStoryBox, SendStoryBoxArgs{
		.peer = story->peer(),
		.editing = story,
		.show = std::move(show),
		.style = &st::storiesSendStoryBoxStyle,
	}));
}

void RepostStory(
		not_null<Window::SessionController*> controller,
		not_null<Data::Story*> story) {
	const auto session = &controller->session();
	const auto self = session->user();
	const auto id = story->fullId();
	session->api().stories().checkCanSend(self, crl::guard(controller, [=](
			Api::CanSendStory result) {
		if (!result.allowed()) {
			ShowCanSendStoryError(controller, self, result);
			return;
		}
		const auto story = session->data().stories().lookup(id);
		if (!story) {
			return;
		}
		controller->show(Box(SendStoryBox, SendStoryBoxArgs{
			.controller = controller,
			.peer = self,
			.media = Api::SendStoryRepost{
				.peer = (*story)->peer(),
				.id = (*story)->id(),
				.photo = (*story)->photo(),
				.document = (*story)->document(),
			},
		}));
	}));
}

void RepostMessageToStory(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item) {
	const auto session = &controller->session();
	const auto self = session->user();
	const auto itemId = item->fullId();
	session->api().stories().checkCanSend(self, crl::guard(controller, [=](
			Api::CanSendStory result) {
		if (!result.allowed()) {
			ShowCanSendStoryError(controller, self, result);
			return;
		}
		const auto item = session->data().message(itemId);
		if (!item) {
			return;
		}
		const auto renderItem = Editor::MessageToRender(item);
		const auto fullId = renderItem->fullId();
		auto items = Editor::SceneItems();
		items.messages.push_back({
			.peerBare = fullId.peer.value,
			.msgBare = fullId.msg.bare,
			.placement = {
				.position = QPointF(
					Api::kStoryPhotoWidth / 2.,
					Api::kStoryPhotoHeight / 2.),
				.rotation = 0.,
				.scale = 1.,
				.zValue = 1.,
			},
		});

		auto setup = StoryEditorSetup{
			.peer = self->id,
			.sceneItems = Editor::SerializeSceneItems(items),
		};

		auto background = Media::Encode::CanvasBackground();
		if (const auto media = renderItem->media()) {
			if (const auto photo = media->photo()) {
				if (const auto large = photo->createMediaView()) {
					const auto img = large->image(Data::PhotoSize::Large);
					if (img) {
						background = Media::Encode::DominantCanvasBackground(
							img->original());
					}
				}
			}
		}
		if (!background.valid()) {
			const auto &color1 = st::groupCallMuted1->c;
			const auto &color2 = st::groupCallLive1->c;
			background = Media::Encode::CanvasBackground{
				.top = color1.darker(250),
				.bottom = color2.darker(300),
			};
		}

		OpenStoryPhotoEditor(
			controller,
			EmptyStoryCanvas(),
			{},
			setup,
			{},
			{},
			background,
			ShowSendBoxWhenEdited(controller, self, 0));
	}));
}

void ShowCanSendStoryError(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		const Api::CanSendStory &result,
		CanSendStoryErrorContext ctx) {
	using Type = Api::CanSendStory::Type;
	const auto session = &controller->session();
	const auto premium = session->premium();
	const auto limits = Data::PremiumLimits(session);
	const auto stopLoading = [&] {
		if (ctx.setLoading) {
			ctx.setLoading(false);
		}
	};
	switch (result.type) {
	case Type::Allowed:
		return;
	case Type::PremiumRequired:
		stopLoading();
		ShowPremiumPreviewBox(controller, PremiumFeature::Stories);
		return;
	case Type::BoostsRequired:
		controller->showToast(tr::lng_story_boosts_required(tr::now));
		if (peer->isChannel()) {
			if (ctx.openBoosts) {
				ctx.openBoosts();
			} else {
				controller->showSection(Info::Boosts::Make(peer));
			}
		} else {
			stopLoading();
		}
		return;
	case Type::CountLimit:
		stopLoading();
		ShowLimitBox(controller, premium
			? tr::lng_story_limit_count_premium(
				lt_count,
				rpl::single(float64(limits.storiesExpiringPremium())),
				tr::rich)
			: tr::lng_story_limit_count(
				lt_count,
				rpl::single(float64(limits.storiesExpiringDefault())),
				tr::rich));
		return;
	case Type::WeeklyLimit:
		stopLoading();
		ShowLimitBox(controller, premium
			? tr::lng_story_limit_weekly_premium(
				lt_count,
				rpl::single(float64(limits.storiesSentWeeklyPremium())),
				tr::rich)
			: tr::lng_story_limit_weekly(
				lt_count,
				rpl::single(float64(limits.storiesSentWeeklyDefault())),
				tr::rich));
		return;
	case Type::MonthlyLimit:
		stopLoading();
		ShowLimitBox(controller, premium
			? tr::lng_story_limit_monthly_premium(
				lt_count,
				rpl::single(float64(limits.storiesSentMonthlyPremium())),
				tr::rich)
			: tr::lng_story_limit_monthly(
				lt_count,
				rpl::single(float64(limits.storiesSentMonthlyDefault())),
				tr::rich));
		return;
	case Type::LiveAlready:
		stopLoading();
		controller->showToast(tr::lng_story_live_already(tr::now));
		return;
	case Type::Error:
		stopLoading();
		controller->showToast(result.error.isEmpty()
			? tr::lng_story_upload_failed(tr::now)
			: result.error);
		return;
	}
}

void SetupStoryPostToasts(not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	session->api().stories().sent(
	) | rpl::on_next([=](const Api::StorySent &sent) {
		if (!sent.error.isEmpty()) {
			controller->showToast(tr::lng_story_upload_failed(tr::now)
				+ u" ("_q
				+ sent.error
				+ u")"_q);
		} else if (sent.edit) {
			controller->showToast(tr::lng_story_edited(tr::now));
		} else {
			controller->showToast(tr::lng_story_posted(tr::now));
		}
	}, controller->lifetime());
}

} // namespace Media::Stories
