/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/sticker_creator_box.h"

#include "api/api_stickers_creator.h"
#include "chat_helpers/compose/compose_show.h"
#include "chat_helpers/emoji_picker_overlay.h"
#include "core/file_utilities.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "editor/editor_layer_widget.h"
#include "editor/photo_editor.h"
#include "editor/photo_editor_common.h"
#include "editor/scene/scene.h"
#include "editor/scene/scene_item_image.h"
#include "editor/video/video_editor.h"
#include "editor/video/video_editor_common.h"
#include "editor/video/video_editor_layer.h"
#include "info/channel_statistics/boosts/giveaway/boost_badge.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "media/media_video_encode.h"
#include "media/media_video_frames.h"
#include "ui/emoji_config.h"
#include "ui/image/image.h"
#include "ui/image/image_prepare.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/layer_widget.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/rp_widget.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"

#include <QtCore/QBuffer>
#include <QtCore/QDir>
#include <QtCore/QTemporaryFile>
#include <QtGui/QImageReader>

namespace {

constexpr auto kStickerSide = 512;
constexpr auto kPreviewSide = 256;
constexpr auto kWebpQuality = 95;
constexpr auto kMaxEmojis = 7;
constexpr auto kEmojiVideoMaxDuration = crl::time(3000);
constexpr auto kEmojiVideoMinDuration = crl::time(200);
constexpr auto kEmojiVideoFps = 30.;
// Server limit for a webm custom emoji file.
constexpr auto kMaxEmojiWebmBytes = 64 * 1024;
constexpr auto kMaxOriginalRatio = 3.;
constexpr auto kSquareRatioEpsilon = 0.01;

[[nodiscard]] int SideForType(Data::StickersType type) {
	return (type == Data::StickersType::Emoji)
		? Api::kEmojiStickerSideMax
		: kStickerSide;
}

[[nodiscard]] QImage LoadImageFromFile(const QString &path) {
	auto reader = QImageReader(path);
	reader.setAutoTransform(true);
	auto image = reader.read();
	if (image.format() != QImage::Format_ARGB32_Premultiplied
		&& image.format() != QImage::Format_ARGB32) {
		image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	}
	return image;
}

class PreviewWidget final : public Ui::RpWidget {
public:
	PreviewWidget(QWidget *parent, QImage image)
	: RpWidget(parent)
	, _image(std::move(image)) {
		resize(kPreviewSide, kPreviewSide);
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto fitted = _image.size().scaled(
			size(),
			Qt::KeepAspectRatio);
		p.drawImage(style::centerrect(rect(), Rect(fitted)), _image);
	}

private:
	const QImage _image;

};

struct EditorState {
	std::shared_ptr<Image> canvas;
	Editor::PhotoModifications modifications;
	int side = 0;
	float64 originalRatio = 0.;
};

[[nodiscard]] std::shared_ptr<EditorState> PrepareEditorState(
		QImage image,
		int side) {
	if (image.isNull()
		|| image.width() <= 0
		|| image.height() <= 0
		|| (image.width() > 10 * image.height())
		|| (image.height() > 10 * image.width())) {
		return nullptr;
	}
	const auto ratio = std::clamp(
		image.width() / float64(image.height()),
		1. / kMaxOriginalRatio,
		kMaxOriginalRatio);

	auto canvas = QImage(
		side,
		side,
		QImage::Format_ARGB32_Premultiplied);
	canvas.fill(Qt::transparent);

	auto scene = std::make_shared<Editor::Scene>(
		QRectF(0, 0, side, side));

	const auto userPixmap = QPixmap::fromImage(std::move(image));
	const auto userSize = userPixmap.size();
	const auto fitted = userSize.scaled(
		QSize(side, side),
		Qt::KeepAspectRatio);
	auto itemData = Editor::ItemBase::Data{
		.initialZoom = 1.0,
		.zPtr = scene->lastZ(),
		.size = fitted.width(),
		.x = side / 2,
		.y = side / 2,
		.imageSize = userSize,
		.contentMargins = false,
	};
	auto imageItem = std::make_shared<Editor::ItemImage>(
		QPixmap(userPixmap),
		std::move(itemData));
	imageItem->setUndoable(false);
	scene->addItem(std::move(imageItem));

	return std::make_shared<EditorState>(EditorState{
		.canvas = std::make_shared<Image>(std::move(canvas)),
		.modifications = Editor::PhotoModifications{
			.crop = QRect(0, 0, side, side),
			.paint = std::move(scene),
		},
		.side = side,
		.originalRatio = (std::abs(ratio - 1.) < kSquareRatioEpsilon)
			? 0.
			: ratio,
	});
}

void ShowPhotoEditor(
		std::shared_ptr<ChatHelpers::Show> show,
		std::shared_ptr<EditorState> state,
		Fn<void(QImage&&)> onDone) {
	const auto sessionController = show->resolveWindow();
	if (!sessionController) {
		show->showToast(tr::lng_stickers_create_open_failed(tr::now));
		return;
	}
	const auto windowController = &sessionController->window();
	const auto parentWidget = sessionController->widget();
	const auto side = state->side;

	auto editor = base::make_unique_q<Editor::PhotoEditor>(
		parentWidget,
		windowController,
		state->canvas,
		state->modifications,
		Editor::EditorData{
			.exactSize = Size(side),
			.cropType = Editor::EditorData::CropType::RoundedRect,
			.cropMode = Editor::EditorData::CropMode::Mask,
			.originalRatio = state->originalRatio,
			.keepAspectRatio = true,
			.fixedCrop = true,
		});
	const auto raw = editor.get();

	auto applyModifications = [=, done = std::move(onDone)](
			const Editor::PhotoModifications &mods) mutable {
		state->modifications = mods;
		auto result = Editor::ImageModified(state->canvas->original(), mods);
		const auto target = result.size().scaled(
			Size(side),
			Qt::KeepAspectRatio);
		if (!target.isEmpty() && (result.size() != target)) {
			result = result.scaled(
				target,
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation);
		}
		Editor::ApplyShapeMask(result, mods);
		done(std::move(result));
	};

	auto layer = std::make_unique<Editor::LayerWidget>(
		parentWidget,
		std::move(editor));
	Editor::InitEditorLayer(layer.get(), raw, std::move(applyModifications));
	windowController->showLayer(
		std::move(layer),
		Ui::LayerOption::KeepOther);
}

[[nodiscard]] QImage Sharpened(QImage image) {
	constexpr auto kRadius = 1;
	constexpr auto kAmount = 0.7;
	if (image.isNull()) {
		return image;
	}
	if (image.format() != QImage::Format_ARGB32_Premultiplied) {
		image = std::move(image).convertToFormat(
			QImage::Format_ARGB32_Premultiplied);
	}
	auto blurred = Images::BlurLargeImage(QImage(image), kRadius);
	if (blurred.size() != image.size()) {
		return image;
	}
	const auto width = image.width();
	const auto height = image.height();
	for (auto y = 0; y != height; ++y) {
		const auto blur = reinterpret_cast<const QRgb*>(
			blurred.constScanLine(y));
		const auto line = reinterpret_cast<QRgb*>(image.scanLine(y));
		for (auto x = 0; x != width; ++x) {
			const auto origin = line[x];
			const auto soft = blur[x];
			const auto alpha = qAlpha(origin);
			const auto sharp = [&](int channel, int blurChannel) {
				const auto value = channel
					+ int(kAmount * (channel - blurChannel));
				return std::clamp(value, 0, alpha);
			};
			line[x] = qRgba(
				sharp(qRed(origin), qRed(soft)),
				sharp(qGreen(origin), qGreen(soft)),
				sharp(qBlue(origin), qBlue(soft)),
				alpha);
		}
	}
	return image;
}

[[nodiscard]] QSize FittedStickerSize(QSize size, int side) {
	return size.scaled(Size(side), Qt::KeepAspectRatio).expandedTo(Size(1));
}

[[nodiscard]] QByteArray EncodeWebp(QImage image, QSize size) {
	if (image.size() != size) {
		image = image.scaled(
			size,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
		image = Sharpened(std::move(image));
	}
	if (image.format() != QImage::Format_ARGB32) {
		image = image.convertToFormat(QImage::Format_ARGB32);
	}
	auto bytes = QByteArray();
	auto buffer = QBuffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	image.save(&buffer, "WEBP", kWebpQuality);
	return bytes;
}

void LoadStickerMedia(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<DocumentData*> document,
		Fn<void(std::shared_ptr<Data::DocumentMedia>)> done) {
	struct State {
		std::shared_ptr<Data::DocumentMedia> media;
		rpl::lifetime lifetime;
	};
	const auto state = std::make_shared<State>();
	state->media = document->createMediaView();
	state->media->checkStickerLarge();
	const auto finish = [=] {
		auto media = state->media;
		state->lifetime.destroy();
		done(std::move(media));
	};
	if (state->media->loaded()) {
		finish();
		return;
	}
	show->session().downloaderTaskFinished(
	) | rpl::filter([=] {
		return state->media->loaded();
	}) | rpl::on_next([=] {
		finish();
	}, state->lifetime);
}

void LoadStickerImage(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<DocumentData*> document,
		Fn<void(QImage)> done) {
	LoadStickerMedia(show, document, [done = std::move(done)](
			std::shared_ptr<Data::DocumentMedia> media) {
		const auto large = media->getStickerLarge();
		done(large ? large->original() : QImage());
	});
}

struct StickerVideoSource {
	std::shared_ptr<QTemporaryFile> file;
	QString path;
	Media::Video::FileInfo info;
	Editor::VideoEditorData editorData;
	Editor::VideoModifications modifications;
};

[[nodiscard]] std::shared_ptr<QTemporaryFile> WriteStickerTempFile(
		const QByteArray &bytes) {
	auto result = std::make_shared<QTemporaryFile>(
		QDir::tempPath() + u"/tdemoji_XXXXXX.webm"_q);
	if (!result->open() || result->write(bytes) != bytes.size()) {
		return nullptr;
	}
	result->close();
	return result;
}

[[nodiscard]] Editor::VideoEditorData EmojiVideoEditorData() {
	return {
		.editor = Editor::EditorData{
			.keepAspectRatio = true,
		},
		.exactSize = Size(Api::kEmojiStickerSideMax),
		.maxDuration = kEmojiVideoMaxDuration,
		.minDuration = kEmojiVideoMinDuration,
		.fpsLimit = kEmojiVideoFps,
		.removeAudio = true,
		.webmSticker = true,
	};
}

} // namespace

namespace Api {
namespace {

struct CreateMediaArgs {
	std::shared_ptr<ChatHelpers::Show> show;
	StickerSetIdentifier set;
	QImage image;
	std::shared_ptr<StickerVideoSource> video;
	Data::StickersType type = Data::StickersType::Stickers;
	std::vector<EmojiPtr> emoji;
	Fn<void(std::vector<EmojiPtr>)> back;
	Fn<void(MTPmessages_StickerSet)> done;
};

void CreateMediaBox(
		not_null<Ui::GenericBox*> box,
		CreateMediaArgs args) {
	const auto show = args.show;
	const auto type = args.type;
	const auto back = args.back;
	auto image = std::move(args.image);
	const auto isEmoji = (type == Data::StickersType::Emoji);
	const auto side = SideForType(type);
	struct State {
		rpl::variable<bool> uploading = false;
		std::unique_ptr<StickerUpload> upload;
		std::shared_ptr<std::atomic<bool>> cancelEncode;
		QPointer<Ui::RoundButton> addButton;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto session = &show->session();

	box->setTitle(isEmoji
		? tr::lng_emoji_create_image_title()
		: tr::lng_stickers_create_image_title());

	const auto inner = box->verticalLayout();

	auto pickerDescriptor = ChatHelpers::EmojiPickerOverlayDescriptor{
		.aboutText = (isEmoji
			? tr::lng_emoji_create_emoji_about(tr::now)
			: tr::lng_stickers_create_emoji_about(tr::now)),
		.maxSelected = kMaxEmojis,
		.allowExpand = true,
		.initialSelected = std::move(args.emoji),
	};
	const auto metrics = ChatHelpers::EmojiPickerOverlay::EstimateMetrics(
		pickerDescriptor.aboutText);
	const auto pickerCollapsed = metrics.collapsedHeight;
	const auto pickerTotalExpanded = metrics.totalExpandedHeight;
	const auto shadowExt = metrics.shadowExtent;

	constexpr auto kStickerOverlap = 24;
	const auto stickerTop = shadowExt.top()
		+ pickerCollapsed
		- kStickerOverlap;
	const auto holderHeight = std::max(
		stickerTop + kPreviewSide,
		pickerTotalExpanded);

	const auto previewHolder = inner->add(
		object_ptr<Ui::RpWidget>(inner),
		QMargins(0, 0, 0, 0),
		style::al_top);
	previewHolder->resize(st::boxWideWidth, holderHeight);
	const auto preview = Ui::CreateChild<PreviewWidget>(
		previewHolder,
		image);

	const auto picker = Ui::CreateChild<ChatHelpers::EmojiPickerOverlay>(
		previewHolder,
		std::move(pickerDescriptor));

	auto layoutOverlay = [=] {
		const auto bubbleW = std::min(
			previewHolder->width()
				- 2 * st::boxRowPadding.left()
				- shadowExt.left() - shadowExt.right(),
			int(kPreviewSide * 1.1));
		const auto totalW = bubbleW + shadowExt.left() + shadowExt.right();
		const auto x = (previewHolder->width() - totalW) / 2;
		picker->setGeometry(x, 0, totalW, pickerTotalExpanded);
		picker->raise();
	};

	previewHolder->widthValue(
	) | rpl::on_next([=](int width) {
		preview->move((width - kPreviewSide) / 2, stickerTop);
		layoutOverlay();
	}, preview->lifetime());

	Ui::AddSkip(inner);

	const auto video = args.video;
	const auto startUpload = [=,
			set = std::move(args.set),
			done = std::move(args.done)]() mutable {
		if (state->uploading.current()) {
			return;
		}
		auto emoji = QString();
		for (const auto one : picker->selected()) {
			emoji.append(one->text());
		}
		if (emoji.isEmpty()) {
			show->showToast(
				tr::lng_stickers_create_emoji_required(tr::now));
			return;
		}
		const auto lockUploading = [=] {
			const auto lockedWidth = state->addButton
				? state->addButton->width()
				: 0;
			state->uploading = true;
			if (state->addButton && lockedWidth > 0) {
				state->addButton->resizeToWidth(lockedWidth);
			}
		};
		const auto upload = [=, doneCallback = done](
				QByteArray bytes,
				QSize dimensions,
				crl::time videoDuration) {
			state->upload = std::make_unique<StickerUpload>(
				session,
				set,
				std::move(bytes),
				dimensions,
				emoji,
				type,
				videoDuration);
			state->upload->start(
				crl::guard(box, [=](MTPmessages_StickerSet result) {
					state->upload = nullptr;
					state->uploading = false;
					show->showToast(isEmoji
						? tr::lng_emoji_added(tr::now)
						: tr::lng_stickers_create_added(tr::now));
					if (doneCallback) {
						doneCallback(result);
					}
					box->closeBox();
				}),
				crl::guard(box, [=](QString err) {
					state->upload = nullptr;
					state->uploading = false;
					show->showToast(err.isEmpty()
						? tr::lng_stickers_create_upload_failed(tr::now)
						: err);
				}));
		};
		if (video) {
			lockUploading();
			const auto cancel = std::make_shared<std::atomic<bool>>(false);
			state->cancelEncode = cancel;
			crl::async([
				=,
				weak = base::make_weak(box),
				mods = video->modifications
			] {
				auto result = Media::Encode::RunWebmSticker(
					Editor::ComposeVideoSource(
						video->path,
						mods,
						video->editorData,
						false),
					kMaxEmojiWebmBytes,
					[=](float64) { return !cancel->load(); });
				crl::on_main(weak, [
					=,
					result = std::move(result)
				]() mutable {
					if (result.empty()
						|| result.bytes.size() > kMaxEmojiWebmBytes) {
						state->uploading = false;
						if (!cancel->load()) {
							show->showToast(tr::lng_bad_video(tr::now));
						}
						return;
					}
					upload(
						std::move(result.bytes),
						result.dimensions,
						std::clamp(
							result.duration,
							crl::time(1),
							kEmojiVideoMaxDuration));
				});
			});
			return;
		}
		const auto dimensions = FittedStickerSize(image.size(), side);
		auto bytes = EncodeWebp(image, dimensions);
		if (bytes.isEmpty()) {
			show->showToast(
				tr::lng_stickers_create_upload_failed(tr::now));
			return;
		}
		lockUploading();
		upload(std::move(bytes), dimensions, 0);
	};

	const auto addButton = box->addButton(
		rpl::conditional(
			state->uploading.value(),
			rpl::single(QString()),
			tr::lng_box_done()),
		startUpload);
	state->addButton = addButton;
	box->addButton(tr::lng_cancel(), [=] {
		if (back) {
			back(picker->selected());
		}
		box->closeBox();
	});

	{
		using namespace Info::Statistics;
		const auto loadingAnimation = InfiniteRadialAnimationWidget(
			addButton,
			addButton->height() / 2,
			&st::editStickerSetNameLoading);
		AddChildToWidgetCenter(addButton, loadingAnimation);
		loadingAnimation->showOn(state->uploading.value());
	}

	box->setWidth(st::boxWideWidth);

	box->boxClosing(
	) | rpl::on_next([=] {
		if (state->cancelEncode) {
			state->cancelEncode->store(true);
		}
		state->upload = nullptr;
	}, box->lifetime());
}

void ShowEditorThenCreate(
		std::shared_ptr<ChatHelpers::Show> show,
		StickerSetIdentifier set,
		std::shared_ptr<EditorState> state,
		Data::StickersType type,
		std::vector<EmojiPtr> emoji,
		Fn<void(MTPmessages_StickerSet)> done) {
	ShowPhotoEditor(show, state, [=](QImage &&prepared) {
		show->showBox(Box(CreateMediaBox, CreateMediaArgs{
			.show = show,
			.set = set,
			.image = std::move(prepared),
			.type = type,
			.emoji = emoji,
			.back = [=](std::vector<EmojiPtr> chosen) {
				ShowEditorThenCreate(show, set, state, type, chosen, done);
			},
			.done = done,
		}));
	});
}

void ShowVideoEditorThenCreate(
		std::shared_ptr<ChatHelpers::Show> show,
		StickerSetIdentifier set,
		std::shared_ptr<StickerVideoSource> video,
		std::vector<EmojiPtr> emoji,
		Fn<void(MTPmessages_StickerSet)> done) {
	const auto sessionController = show->resolveWindow();
	if (!sessionController) {
		show->showToast(tr::lng_stickers_create_open_failed(tr::now));
		return;
	}
	const auto windowController = &sessionController->window();
	const auto parentWidget = sessionController->widget();

	auto applyModifications = [=](Editor::VideoModifications mods) {
		video->modifications = mods;
		const auto path = video->path;
		const auto dimensions = video->info.dimensions;
		crl::async([=, weak = base::make_weak(parentWidget)] {
			auto preview = Editor::ExtractCoverImage(
				path,
				QByteArray(),
				mods,
				dimensions,
				kPreviewSide);
			crl::on_main(weak, [=, preview = std::move(preview)]() mutable {
				show->showBox(Box(CreateMediaBox, CreateMediaArgs{
					.show = show,
					.set = set,
					.image = std::move(preview),
					.video = video,
					.type = Data::StickersType::Emoji,
					.emoji = emoji,
					.back = [=](std::vector<EmojiPtr> chosen) {
						ShowVideoEditorThenCreate(
							show,
							set,
							video,
							chosen,
							done);
					},
					.done = done,
				}));
			});
		});
	};

	Editor::ShowVideoEditorLayer(
		parentWidget,
		windowController,
		Editor::VideoEditorDescriptor{
			.path = video->path,
			.dimensions = video->info.dimensions,
			.duration = video->info.duration,
			.data = video->editorData,
			.initial = video->modifications,
		},
		std::move(applyModifications));
}

void RunVideoEditorAndCreate(
		std::shared_ptr<ChatHelpers::Show> show,
		StickerSetIdentifier set,
		not_null<DocumentData*> document,
		std::shared_ptr<Data::DocumentMedia> media,
		Fn<void(MTPmessages_StickerSet)> done) {
	auto bytes = media->bytes();
	auto file = std::shared_ptr<QTemporaryFile>();
	auto path = QString();
	if (!bytes.isEmpty()) {
		file = WriteStickerTempFile(bytes);
		path = file ? file->fileName() : QString();
	}
	if (path.isEmpty()) {
		path = document->filepath(true);
	}
	const auto info = path.isEmpty()
		? Media::Video::FileInfo()
		: Media::Video::ReadFileInfo(path);
	if (!info.valid()) {
		show->showToast(tr::lng_bad_video(tr::now));
		return;
	}
	auto video = std::make_shared<StickerVideoSource>(StickerVideoSource{
		.file = std::move(file),
		.path = path,
		.info = info,
		.editorData = EmojiVideoEditorData(),
	});
	auto initial = std::vector<EmojiPtr>();
	if (const auto emoji = Ui::Emoji::Find(StickerEmojiOrDefault(document))) {
		initial.push_back(emoji);
	}
	ShowVideoEditorThenCreate(
		std::move(show),
		std::move(set),
		std::move(video),
		std::move(initial),
		std::move(done));
}

void RunImageEditorAndCreate(
		std::shared_ptr<ChatHelpers::Show> show,
		StickerSetIdentifier set,
		QImage image,
		Data::StickersType type,
		Fn<void(MTPmessages_StickerSet)> done) {
	auto state = PrepareEditorState(std::move(image), kStickerSide);
	if (!state) {
		show->showToast(tr::lng_stickers_create_open_failed(tr::now));
		return;
	}
	if (type != Data::StickersType::Stickers) {
		state->originalRatio = 0.;
	}
	ShowEditorThenCreate(
		std::move(show),
		std::move(set),
		std::move(state),
		type,
		{},
		std::move(done));
}

void ChooseImageThenCreate(
		std::shared_ptr<ChatHelpers::Show> show,
		StickerSetIdentifier set,
		Data::StickersType type,
		Fn<void(MTPmessages_StickerSet)> done) {
	const auto parent = QPointer<QWidget>(show->toastParent());

	const auto onChosen = [=, set = std::move(set), done = std::move(done)](
			FileDialog::OpenResult &&result) mutable {
		if (result.paths.isEmpty() && result.remoteContent.isEmpty()) {
			return;
		}
		const auto path = result.paths.isEmpty()
			? QString()
			: result.paths.front();
		auto image = path.isEmpty()
			? QImage::fromData(result.remoteContent)
			: LoadImageFromFile(path);
		RunImageEditorAndCreate(
			show,
			std::move(set),
			std::move(image),
			type,
			std::move(done));
	};

	FileDialog::GetOpenPath(
		parent,
		tr::lng_stickers_create_choose_image(tr::now),
		FileDialog::ImagesFilter(),
		std::move(onChosen));
}

} // namespace

void OpenCreateStickerFlow(
		std::shared_ptr<ChatHelpers::Show> show,
		StickerSetIdentifier set,
		Fn<void(MTPmessages_StickerSet)> done) {
	ChooseImageThenCreate(
		std::move(show),
		std::move(set),
		Data::StickersType::Stickers,
		std::move(done));
}

void OpenCreateEmojiFlow(
		std::shared_ptr<ChatHelpers::Show> show,
		StickerSetIdentifier set,
		Fn<void(MTPmessages_StickerSet)> done) {
	ChooseImageThenCreate(
		std::move(show),
		std::move(set),
		Data::StickersType::Emoji,
		std::move(done));
}

bool AdaptStickerToEmoji(
		std::shared_ptr<ChatHelpers::Show> show,
		StickerSetIdentifier set,
		not_null<DocumentData*> document,
		Fn<void(MTPmessages_StickerSet)> done) {
	const auto sticker = document->sticker();
	if (!sticker) {
		show->showToast(tr::lng_attach_failed(tr::now));
		return false;
	}
	if (sticker->isLottie()) {
		const auto emoji = StickerEmojiOrDefault(document);
		AddExistingStickerToSet(
			&show->session(),
			set,
			document,
			emoji,
			[=](MTPmessages_StickerSet result) {
				show->showToast(tr::lng_emoji_added(tr::now));
				if (done) {
					done(result);
				}
			},
			[=](QString err) {
				show->showToast(err.isEmpty()
					? tr::lng_attach_failed(tr::now)
					: err);
			});
		return true;
	}
	if (sticker->isWebm()) {
		LoadStickerMedia(
			show,
			document,
			[=, set = std::move(set), done = std::move(done)](
					std::shared_ptr<Data::DocumentMedia> media) mutable {
				RunVideoEditorAndCreate(
					show,
					std::move(set),
					document,
					std::move(media),
					std::move(done));
			});
		return true;
	}
	LoadStickerImage(
		show,
		document,
		[=, set = std::move(set), done = std::move(done)](
				QImage image) mutable {
			if (image.isNull()) {
				show->showToast(
					tr::lng_stickers_create_open_failed(tr::now));
				return;
			}
			RunImageEditorAndCreate(
				show,
				std::move(set),
				std::move(image),
				Data::StickersType::Emoji,
				std::move(done));
		});
	return true;
}

} // namespace Api
