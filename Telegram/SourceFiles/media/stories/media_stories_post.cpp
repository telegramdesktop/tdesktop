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
#include "data/data_user.h"
#include "editor/editor_layer_widget.h"
#include "editor/photo_editor.h"
#include "editor/photo_editor_common.h"
#include "editor/scene/scene.h"
#include "editor/video/video_editor.h"
#include "editor/video/video_editor_common.h"
#include "info/channel_statistics/boosts/info_boosts_widget.h"
#include "info/info_memento.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "media/media_video_encode.h"
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

namespace Media::Stories {
namespace {

struct StoryVideoSource {
	QString path;
	QByteArray content;
	QSize dimensions;
	crl::time duration = 0;
	Editor::VideoModifications initial;
};

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

[[nodiscard]] Editor::EditorData StoryPhotoEditorData(
		not_null<Window::SessionController*> controller,
		const StoryEditorSetup &setup) {
	return {
		.confirm = StoryEditorConfirm(setup),
		.exactSize = QSize(Api::kStoryPhotoWidth, Api::kStoryPhotoHeight),
		.keepAspectRatio = true,
		.story = StoryEditorOptions(controller, setup),
	};
}

[[nodiscard]] Editor::VideoEditorData StoryVideoEditorData(
		not_null<Window::SessionController*> controller,
		const StoryEditorSetup &setup) {
	return {
		.editor = Editor::EditorData{
			.confirm = StoryEditorConfirm(setup),
			.keepAspectRatio = true,
			.story = StoryEditorOptions(controller, setup),
		},
		.hint = tr::lng_story_cover_hint(tr::now),
		.exactSize = QSize(Api::kStoryVideoWidth, Api::kStoryVideoHeight),
		.maxDuration = Api::kStoryVideoMaxDuration,
		.allowGif = false,
		.transcodeAlways = true,
	};
}

[[nodiscard]] bool PhotoUnchanged(
		const Editor::PhotoModifications &mods,
		QSize size) {
	const auto full = QRect(QPoint(), size);
	return !mods.angle
		&& !mods.flipped
		&& (!mods.crop.isValid() || (mods.crop == full))
		&& (!mods.paint || mods.paint->items().empty());
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
		StoryEditorDone done) {
	const auto parent = controller->widget();
	const auto show = controller->uiShow();
	const auto size = image.size();
	const auto photo = std::make_shared<Image>(std::move(image));
	auto editor = base::make_unique_q<Editor::PhotoEditor>(
		parent,
		show,
		show,
		photo,
		std::move(initial),
		StoryPhotoEditorData(controller, setup));
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
	raw->cancelRequests() | rpl::on_next(close, raw->lifetime());
	raw->doneRequests(
	) | rpl::on_next([=](const Editor::PhotoModifications &mods) {
		auto result = Editor::ImageModified(photo->original(), mods);
		if (result.isNull()) {
			controller->showToast(tr::lng_bad_photo(tr::now));
			return;
		}
		done({
			.media = Api::SendStoryPhoto{ std::move(result) },
			.caption = raw->caption(),
			.period = raw->period(),
			.mediaChanged = !PhotoUnchanged(mods, size),
		}, close);
	}, raw->lifetime());
	show->showLayer(std::move(layer), Ui::LayerOption::KeepOther);
}

void OpenStoryVideoEditor(
		not_null<Window::SessionController*> controller,
		StoryVideoSource video,
		const StoryEditorSetup &setup,
		StoryEditorDone done) {
	const auto parent = controller->widget();
	const auto show = controller->uiShow();
	const auto data = StoryVideoEditorData(controller, setup);
	const auto path = video.path;
	const auto content = video.content;
	const auto dimensions = video.dimensions;
	const auto duration = video.duration;
	auto editor = base::make_unique_q<Editor::VideoEditor>(
		parent,
		Editor::VideoEditorDescriptor{
			.path = path,
			.content = content,
			.dimensions = dimensions,
			.duration = duration,
			.data = data,
			.initial = video.initial,
		});
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
	raw->cancelRequests() | rpl::on_next(close, raw->lifetime());
	raw->doneRequests(
	) | rpl::on_next([=](Editor::VideoModifications mods) {
		auto source = Editor::ComposeVideoSource(path, mods, data, true);
		source.bytes = path.isEmpty() ? content : QByteArray();
		const auto caption = raw->caption();
		const auto period = raw->period();
		const auto changed = Editor::VideoEdited(
			mods,
			dimensions,
			duration,
			true);
		crl::async([=, ready = crl::guard(controller, [=](
				QImage &&cover) mutable {
			if (cover.isNull()) {
				controller->showToast(tr::lng_bad_video(tr::now));
				return;
			}
			done({
				.media = Api::SendStoryVideo{
					std::make_shared<Media::Encode::VideoSource>(source),
					cover,
				},
				.caption = caption,
				.period = period,
				.mediaChanged = changed,
			}, close);
		})]() mutable {
			auto cover = Editor::ExtractCoverImage(
				path,
				content,
				mods,
				dimensions,
				Api::kStoryVideoWidth);
			crl::on_main([
				ready = std::move(ready),
				cover = std::move(cover)
			]() mutable {
				ready(std::move(cover));
			});
		});
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
			std::move(done));
		return;
	}
	using Video = Ui::PreparedFileInformation::Video;
	const auto video = std::get_if<Video>(&file.information->media);
	if (!video || video->thumbnail.isNull()) {
		controller->showToast(tr::lng_send_media_invalid_files(tr::now));
		return;
	}
	OpenStoryVideoEditor(
		controller,
		StoryVideoSource{
			.path = file.path,
			.content = file.content,
			.dimensions = video->thumbnail.size(),
			.duration = video->duration,
			.initial = video->modifications,
		},
		setup,
		std::move(done));
}

[[nodiscard]] StoryEditorDone ShowSendBoxWhenEdited(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	return [=](StoryEditorResult result, Fn<void()> closeEditor) {
		controller->show(Box(SendStoryBox, SendStoryBoxArgs{
			.controller = controller,
			.peer = peer,
			.media = std::move(result.media),
			.caption = std::move(result.caption),
			.period = result.period,
			.posted = std::move(closeEditor),
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
			StoryEditorSetup(),
			ShowSendBoxWhenEdited(controller, peer));
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
			StoryEditorSetup(),
			ShowSendBoxWhenEdited(controller, peer));
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
			OpenStoryVideoEditor(
				controller,
				StoryVideoSource{
					.path = path,
					.content = path.isEmpty() ? media->bytes() : QByteArray(),
					.dimensions = document->dimensions,
					.duration = document->duration(),
				},
				setup,
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
