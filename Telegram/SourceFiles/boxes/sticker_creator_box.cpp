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
#include "info/channel_statistics/boosts/giveaway/boost_badge.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/emoji_config.h"
#include "ui/image/image.h"
#include "ui/image/image_prepare.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/layer_widget.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_editor.h"
#include "styles/style_layers.h"

#include <QtCore/QBuffer>
#include <QtGui/QImageReader>

namespace {

constexpr auto kStickerSide = 512;
constexpr auto kPreviewSide = 256;
constexpr auto kWebpQuality = 95;
constexpr auto kMaxEmojis = 7;

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
		const auto target = QRect(0, 0, width(), height());
		p.drawImage(target, _image);
	}

private:
	const QImage _image;

};

void OpenPhotoEditorForImage(
		std::shared_ptr<ChatHelpers::Show> show,
		QImage image,
		int side,
		Fn<void(QImage&&)> onDone) {
	if (image.isNull()) {
		show->showToast(tr::lng_stickers_create_open_failed(tr::now));
		return;
	}
	const auto sessionController = show->resolveWindow();
	if (!sessionController) {
		show->showToast(tr::lng_stickers_create_open_failed(tr::now));
		return;
	}
	const auto windowController = &sessionController->window();
	const auto parentWidget = sessionController->widget();

	if (image.width() <= 0
		|| image.height() <= 0
		|| (image.width() > 10 * image.height())
		|| (image.height() > 10 * image.width())) {
		show->showToast(tr::lng_stickers_create_open_failed(tr::now));
		return;
	}

	auto canvas = QImage(
		side,
		side,
		QImage::Format_ARGB32_Premultiplied);
	canvas.fill(Qt::transparent);
	const auto baseImage = std::make_shared<Image>(std::move(canvas));

	auto scene = std::make_shared<Editor::Scene>(
		QRectF(0, 0, side, side));

	const auto userPixmap = QPixmap::fromImage(std::move(image));
	const auto userSize = userPixmap.size();
	const auto fitted = userSize.scaled(
		QSize(side, side),
		Qt::KeepAspectRatio);
	const auto handle = st::photoEditorItemHandleSize;
	const auto itemSize = (userSize.width() >= userSize.height())
		? int((fitted.height() + handle)
			* userSize.width() / float64(userSize.height()))
		: (fitted.width() + handle);
	auto itemData = Editor::ItemBase::Data{
		.initialZoom = 1.0,
		.zPtr = scene->lastZ(),
		.size = itemSize,
		.x = side / 2,
		.y = side / 2,
		.imageSize = userSize,
	};
	auto imageItem = std::make_shared<Editor::ItemImage>(
		QPixmap(userPixmap),
		std::move(itemData));
	scene->addItem(std::move(imageItem));

	auto modifications = Editor::PhotoModifications{
		.crop = QRect(0, 0, side, side),
		.paint = std::move(scene),
	};

	auto editor = base::make_unique_q<Editor::PhotoEditor>(
		parentWidget,
		windowController,
		baseImage,
		std::move(modifications),
		Editor::EditorData{
			.exactSize = QSize(side, side),
			.cropType = Editor::EditorData::CropType::RoundedRect,
			.cropMode = Editor::EditorData::CropMode::Mask,
			.keepAspectRatio = true,
			.fixedCrop = true,
		});
	const auto raw = editor.get();

	auto applyModifications = [=, done = std::move(onDone)](
			const Editor::PhotoModifications &mods) mutable {
		auto result = Editor::ImageModified(baseImage->original(), mods);
		if (result.size() != QSize(side, side)) {
			result = result.scaled(
				side,
				side,
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

[[nodiscard]] QByteArray EncodeWebp(QImage image, int side) {
	if (image.size() != QSize(side, side)) {
		image = image.scaled(
			side,
			side,
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

void LoadStickerImage(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<DocumentData*> document,
		Fn<void(QImage)> done) {
	struct State {
		std::shared_ptr<Data::DocumentMedia> media;
		rpl::lifetime lifetime;
	};
	const auto state = std::make_shared<State>();
	state->media = document->createMediaView();
	state->media->checkStickerLarge();
	const auto finish = [=] {
		const auto large = state->media->getStickerLarge();
		auto image = large ? large->original() : QImage();
		state->lifetime.destroy();
		done(std::move(image));
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

} // namespace

namespace Api {
namespace {

void CreateMediaBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<ChatHelpers::Show> show,
		StickerSetIdentifier set,
		QImage image,
		Data::StickersType type,
		Fn<void(MTPmessages_StickerSet)> done) {
	const auto isEmoji = (type == Data::StickersType::Emoji);
	const auto side = SideForType(type);
	struct State {
		rpl::variable<bool> uploading = false;
		std::unique_ptr<StickerUpload> upload;
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

	const auto startUpload = [=, set = std::move(set), done = std::move(done)](
			) mutable {
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
		const auto bytes = EncodeWebp(image, side);
		if (bytes.isEmpty()) {
			show->showToast(
				tr::lng_stickers_create_upload_failed(tr::now));
			return;
		}

		const auto lockedWidth = state->addButton
			? state->addButton->width()
			: 0;
		state->uploading = true;
		if (state->addButton && lockedWidth > 0) {
			state->addButton->resizeToWidth(lockedWidth);
		}
		state->upload = std::make_unique<StickerUpload>(
			session,
			set,
			bytes,
			emoji,
			type);

		const auto doneCallback = done;
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

	const auto addButton = box->addButton(
		rpl::conditional(
			state->uploading.value(),
			rpl::single(QString()),
			tr::lng_box_done()),
		startUpload);
	state->addButton = addButton;
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });

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
		state->upload = nullptr;
	}, box->lifetime());
}

void RunImageEditorAndCreate(
		std::shared_ptr<ChatHelpers::Show> show,
		StickerSetIdentifier set,
		QImage image,
		Data::StickersType type,
		Fn<void(MTPmessages_StickerSet)> done) {
	OpenPhotoEditorForImage(
		show,
		std::move(image),
		kStickerSide,
		[=, set = std::move(set), done = std::move(done)](
				QImage &&prepared) mutable {
			show->showBox(Box(
				CreateMediaBox,
				show,
				std::move(set),
				std::move(prepared),
				type,
				std::move(done)));
		});
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
	if (!sticker || sticker->isWebm()) {
		show->showToast(tr::lng_emoji_adapt_no_video(tr::now));
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
