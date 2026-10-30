/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/stories/media_stories_post.h"

#include "apiwrap.h"
#include "boxes/premium_preview_box.h"
#include "boxes/send_story_box.h"
#include "core/file_utilities.h"
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
#include "editor/editor_layer_widget.h"
#include "editor/editor_story_areas.h"
#include "editor/photo_editor.h"
#include "editor/photo_editor_common.h"
#include "editor/scene/scene.h"
#include "editor/scene/scene_item_video.h"
#include "info/channel_statistics/boosts/info_boosts_widget.h"
#include "info/info_memento.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "media/media_video_canvas.h"
#include "media/media_video_encode.h"
#include "media/media_video_frames.h"
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

struct DiscardStoryArgs {
	bool draft = false;
	bool allowDraft = true;
	Fn<void()> discard;
	Fn<void()> saveDraft;
};

void ShowDiscardStoryBox(
		not_null<Window::SessionController*> controller,
		DiscardStoryArgs args) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		Ui::ConfirmBox(box, {
			.text = tr::lng_story_discard_text(),
			.confirmed = Fn<void(Fn<void()>)>([=](Fn<void()> close) {
				close();
				args.discard();
			}),
			.confirmText = (args.draft
				? tr::lng_story_delete_draft()
				: tr::lng_story_discard_button()),
			.confirmStyle = &st::attentionBoxButton,
			.title = tr::lng_story_discard_title(),
		});
		if (args.allowDraft) {
			box->addLeftButton(
				(args.draft
					? tr::lng_story_keep_draft()
					: tr::lng_story_save_draft()),
				[=] {
					box->closeBox();
					args.saveDraft();
				});
		}
	}));
}

template <typename Widget>
void SetupStoryEditorClose(
		not_null<Window::SessionController*> controller,
		not_null<Widget*> raw,
		const StoryEditorSetup &setup,
		Fn<void()> close,
		Fn<void()> saveDraft) {
	raw->cancelRequests() | rpl::on_next([=] {
		const auto changed = raw->edited()
			|| (raw->caption() != setup.caption)
			|| (raw->period() != InitialPeriod(setup));
		if (!changed && !setup.draftId) {
			close();
			return;
		}
		ShowDiscardStoryBox(controller, {
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
	story.musicButton = true;
	story.videoObject = videoObject;
	return {
		.confirm = StoryEditorConfirm(setup),
		.exactSize = QSize(Api::kStoryPhotoWidth, Api::kStoryPhotoHeight),
		.keepAspectRatio = true,
		.composeAnimated = true,
		.composeSound = true,
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

[[nodiscard]] Media::Encode::MusicTrack MusicTrackFor(
		const Editor::StoryAudio &audio) {
	return {
		.path = audio.path,
		.bytes = audio.content,
		.position = audio.position,
		.from = audio.from,
		.till = (audio.till > audio.from) ? audio.till : audio.duration,
		.volume = audio.volume,
	};
}

[[nodiscard]] Api::SendStoryMedia StoryPhotoMedia(
		QImage composed,
		const Editor::PhotoModifications &mods,
		const Editor::StoryAudio &audio) {
	const auto animated = mods.paint && mods.paint->hasAnimatedItems();
	if (audio.empty() && !animated) {
		return Api::SendStoryPhoto{ std::move(composed) };
	}
	auto job = Media::Encode::Job();
	if (animated) {
		auto paintOnly = Editor::PhotoModifications();
		paintOnly.paint = mods.paint;
		job = Editor::ComposeAnimatedJob(composed, paintOnly, {
			.maxSide = Api::kStoryVideoHeight,
			.maxDuration = Api::kStoryVideoMaxDuration,
		});
	} else {
		job.source = Media::Encode::StillSource{
			.base = composed.scaled(
				QSize(Api::kStoryVideoWidth, Api::kStoryVideoHeight),
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation),
			.fps = 30.,
		};
	}
	auto &still = v::get<Media::Encode::StillSource>(job.source);
	if (!audio.empty()) {
		still.music.push_back(MusicTrackFor(audio));
		still.duration = std::clamp(
			audio.length(),
			crl::time(1000),
			Api::kStoryVideoMaxDuration);
	}
	job.silentLoop = false;
	return Api::SendStoryVideo{
		.cover = std::move(composed),
		.job = std::make_shared<Media::Encode::Job>(std::move(job)),
	};
}

[[nodiscard]] bool UntouchedVideoOnly(const Editor::Scene *scene) {
	if (!scene || scene->audio()) {
		return false;
	}
	const auto items = scene->items();
	if (items.size() != 1) {
		return false;
	}
	const auto &item = items.front();
	return (item->type() == Editor::ItemVideo::Type)
		&& item->isNormalStatus()
		&& static_cast<Editor::ItemVideo*>(item.get())->atInitialState();
}

[[nodiscard]] bool PhotoUnchanged(const Editor::PhotoModifications &mods) {
	return !mods.angle
		&& !mods.flipped
		&& mods.canvasMediaRect.isEmpty()
		&& (!mods.paint || UntouchedVideoOnly(mods.paint.get()));
}

[[nodiscard]] Editor::StoryAreas CollectAreas(
		const Editor::PhotoModifications &mods,
		QSize canvas) {
	return mods.paint
		? Editor::CollectStoryAreas(mods.paint.get(), canvas)
		: Editor::StoryAreas();
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

void OpenStoryPhotoEditor(
		not_null<Window::SessionController*> controller,
		QImage image,
		Editor::PhotoModifications initial,
		const StoryEditorSetup &setup,
		Editor::StoryVideoObject object,
		StoryEditorDone done) {
	const auto parent = controller->widget();
	const auto show = controller->uiShow();
	const auto background = object.empty()
		? Media::Encode::DominantCanvasBackground(image)
		: Media::Encode::DominantCanvasBackground(object.thumbnail);
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
	SetupStoryEditorClose(controller, not_null(raw), setup, close, [=] {
		const auto mods = raw->collect();
		const auto painted = mods.paint && !mods.paint->items().empty();
		auto videoRect = QRectF();
		const auto video = VideoObjectFromScene(mods.paint, videoRect);
		const auto composed = ComposeStoryPhoto(image, background, mods);
		auto draft = Data::StoryDraft{
			.id = setup.draftId,
			.peer = setup.peer,
			.caption = raw->caption(),
			.period = raw->period(),
			.thumbnail = DraftThumbnail(composed),
		};
		auto media = Data::StoryDraftMedia();
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
			media.photo = StillFromScene(image, background, mods);
			media.videoPath = video.path;
			media.videoContent = video.content;
		} else if (painted) {
			media.photo = composed;
		} else {
			media.photo = image;
			draft.edits = {
				.angle = mods.angle,
				.flipped = mods.flipped,
				.canvasMediaRect = mods.canvasMediaRect,
			};
		}
		if (!draft.video) {
			draft.dimensions = media.photo.size();
		}
		SaveStoryDraft(controller, std::move(draft), media, close);
	});
	raw->doneRequests(
	) | rpl::on_next([=](const Editor::PhotoModifications &mods) {
		auto result = ComposeStoryPhoto(image, background, mods);
		if (result.isNull()) {
			controller->showToast(tr::lng_bad_photo(tr::now));
			return;
		}
		auto areas = CollectAreas(mods, result.size());
		const auto audio = raw->audio();
		done({
			.media = StoryPhotoMedia(std::move(result), mods, audio),
			.caption = raw->caption(),
			.period = raw->period(),
			.reactions = std::move(areas.reactions),
			.urls = std::move(areas.urls),
			.locations = std::move(areas.locations),
			.weather = std::move(areas.weather),
			.music = audio.document,
			.mediaChanged = !PhotoUnchanged(mods) || !audio.empty(),
		}, close);
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
		std::move(done));
}

[[nodiscard]] StoryEditorDone ShowSendBoxWhenEdited(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		uint64 draftId) {
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
			.music = result.music,
			.posted = [=] {
				if (draftId) {
					Drafts(controller).remove(draftId);
				}
				closeEditor();
			},
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
		not_null<PeerData*> peer) {
	const auto session = &controller->session();
	session->api().stories().checkCanSend(peer, crl::guard(controller, [=](
			Api::CanSendStory result) {
		if (!result.allowed()) {
			ShowCanSendStoryError(controller, peer, result);
			return;
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
		const QString &path) {
	const auto session = &controller->session();
	session->api().stories().checkCanSend(peer, crl::guard(controller, [=](
			Api::CanSendStory result) {
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
			StoryEditorSetup{ .peer = peer->id },
			ShowSendBoxWhenEdited(controller, peer, 0));
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
			OpenStoryPhotoEditor(
				controller,
				std::move(image),
				std::move(initial),
				setup,
				{},
				done);
			return;
		}
		const auto probed = Storage::ReadPhotoEditorMedia(draft.mediaPath, {});
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
			done);
	}));
}

void EditStory(
		not_null<Window::SessionController*> controller,
		not_null<Data::Story*> story) {
	const auto session = &controller->session();
	const auto id = story->fullId();
	const auto origin = Data::FileOrigin(Data::FileOriginStory(id));
	const auto setup = StoryEditorSetup{
		.caption = TextWithTags{
			story->caption().text,
			TextUtilities::ConvertEntitiesToTextTags(
				story->caption().entities),
		},
		.editing = true,
	};
	const auto done = [=](StoryEditorResult result, Fn<void()> close) {
		const auto found = session->data().stories().lookup(id);
		if (found) {
			const auto captionChanged = (result.caption != setup.caption);
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
					.music = result.music,
					.editId = (*found)->id(),
					.editCaption = captionChanged,
				});
			}
		}
		close();
	};
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
				done);
		});
	}
}

void EditStoryPrivacy(
		not_null<Window::SessionController*> controller,
		not_null<Data::Story*> story) {
	controller->show(Box(SendStoryBox, SendStoryBoxArgs{
		.controller = controller,
		.peer = story->peer(),
		.editing = story,
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

void ShowCanSendStoryError(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		const Api::CanSendStory &result) {
	using Type = Api::CanSendStory::Type;
	const auto session = &controller->session();
	const auto premium = session->premium();
	const auto limits = Data::PremiumLimits(session);
	switch (result.type) {
	case Type::Allowed:
		return;
	case Type::PremiumRequired:
		ShowPremiumPreviewBox(controller, PremiumFeature::Stories);
		return;
	case Type::BoostsRequired:
		controller->showToast(tr::lng_story_boosts_required(tr::now));
		if (peer->isChannel()) {
			controller->showSection(Info::Boosts::Make(peer));
		}
		return;
	case Type::CountLimit:
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
		controller->showToast(tr::lng_story_live_already(tr::now));
		return;
	case Type::Error:
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
