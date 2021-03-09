/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/edit_caption_box.h"

#include "apiwrap.h"
#include "api/api_editing.h"
#include "api/api_text_entities.h"
#include "main/main_session.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "base/event_filter.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/file_utilities.h"
#include "core/mime_type.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_photo.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "data/data_streaming.h"
#include "data/data_file_origin.h"
#include "data/data_photo_media.h"
#include "data/data_document_media.h"
#include "history/history.h"
#include "history/history_drag_area.h"
#include "history/history_item.h"
#include "history/view/media/history_view_document.h" // DrawThumbnailAsSongCover
#include "platform/platform_specific.h"
#include "lang/lang_keys.h"
#include "media/streaming/media_streaming_instance.h"
#include "media/streaming/media_streaming_player.h"
#include "media/streaming/media_streaming_document.h"
#include "media/streaming/media_streaming_loader_local.h"
#include "platform/platform_file_utilities.h"
#include "storage/localimageloader.h"
#include "storage/storage_media_prepare.h"
#include "mtproto/mtproto_config.h"
#include "ui/image/image.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/checkbox.h"
#include "ui/text/format_values.h"
#include "ui/text/text_options.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/controls/emoji_button.h"
#include "ui/toast/toast.h"
#include "ui/cached_round_corners.h"
#include "window/window_session_controller.h"
#include "confirm_box.h"
#include "apiwrap.h"
#include "app.h" // App::pixmapFromImageInPlace.
#include "facades.h" // App::LambdaDelayed.
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_chat.h"

#include <QtCore/QMimeData>

namespace {

using namespace ::Media::Streaming;
using Data::PhotoSize;

auto ListFromMimeData(not_null<const QMimeData*> data) {
	using Error = Ui::PreparedList::Error;
	auto result = data->hasUrls()
		? Storage::PrepareMediaList(
			// When we edit media, we need only 1 file.
			data->urls().mid(0, 1),
			st::sendMediaPreviewSize)
		: Ui::PreparedList(Error::EmptyFile, QString());
	if (result.error == Error::None) {
		return result;
	} else if (data->hasImage()) {
		auto image = Platform::GetImageFromClipboard();
		if (image.isNull()) {
			image = qvariant_cast<QImage>(data->imageData());
		}
		if (!image.isNull()) {
			return Storage::PrepareMediaFromImage(
				std::move(image),
				QByteArray(),
				st::sendMediaPreviewSize);
		}
	}
	return result;
}

} // namespace

EditCaptionBox::EditCaptionBox(
	QWidget*,
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item)
: _controller(controller)
, _msgId(item->fullId()) {
	Expects(item->media() != nullptr);
	Expects(item->media()->allowsEditCaption());

	_isAllowedEditMedia = item->media()->allowsEditMedia();
	auto dimensions = QSize();
	const auto media = item->media();

	if (!item->groupId().empty()) {
		if (media->photo()) {
			_albumType = Ui::AlbumType::PhotoVideo;
		} else if (const auto document = media->document()) {
			if (document->isVideoFile()) {
				_albumType = Ui::AlbumType::PhotoVideo;
			} else if (document->isSong()) {
				_albumType = Ui::AlbumType::Music;
			} else {
				_albumType = Ui::AlbumType::File;
			}
		}
	}

	if (const auto photo = media->photo()) {
		_photoMedia = photo->createMediaView();
		_photoMedia->wanted(PhotoSize::Large, _msgId);
		dimensions = _photoMedia->size(PhotoSize::Large);
		if (dimensions.isEmpty()) {
			dimensions = QSize(1, 1);
		}
		_photo = true;
	} else if (const auto document = media->document()) {
		_documentMedia = document->createMediaView();
		_documentMedia->thumbnailWanted(_msgId);
		dimensions = _documentMedia->thumbnail()
			? _documentMedia->thumbnail()->size()
			: document->dimensions;
		if (document->isAnimation()) {
			_gifw = style::ConvertScale(document->dimensions.width());
			_gifh = style::ConvertScale(document->dimensions.height());
			_animated = true;
		} else if (document->isVideoFile()) {
			_animated = true;
		} else {
			_doc = true;
		}
	} else {
		Unexpected("Photo or document should be set.");
	}
	const auto editData = PrepareEditText(item);

	const auto computeImage = [=] {
		if (_documentMedia) {
			return _documentMedia->thumbnail();
		} else if (const auto large = _photoMedia->image(PhotoSize::Large)) {
			return large;
		} else if (const auto thumbnail = _photoMedia->image(
				PhotoSize::Thumbnail)) {
			return thumbnail;
		} else if (const auto small = _photoMedia->image(PhotoSize::Small)) {
			return small;
		} else {
			return _photoMedia->thumbnailInline();
		}
	};

	if (!_animated && _documentMedia) {
		if (dimensions.isEmpty()) {
			_thumbw = 0;
			_thumbnailImageLoaded = true;
		} else {
			const auto thumbSize = (!media->document()->isSongWithCover()
				? st::msgFileThumbLayout
				: st::msgFileLayout).thumbSize;
			const auto tw = dimensions.width(), th = dimensions.height();
			if (tw > th) {
				_thumbw = (tw * thumbSize) / th;
			} else {
				_thumbw = thumbSize;
			}
			_refreshThumbnail = [=] {
				const auto image = computeImage();
				if (!image) {
					return;
				}
				if (media->document()->isSongWithCover()) {
					const auto size = QSize(thumbSize, thumbSize);
					_thumb = QPixmap(size);
					_thumb.fill(Qt::transparent);
					Painter p(&_thumb);

					HistoryView::DrawThumbnailAsSongCover(
						p,
						_documentMedia,
						QRect(QPoint(), size));
				} else {
					const auto options = Images::Option::Smooth
						| Images::Option::RoundedSmall
						| Images::Option::RoundedTopLeft
						| Images::Option::RoundedTopRight
						| Images::Option::RoundedBottomLeft
						| Images::Option::RoundedBottomRight;
					_thumb = App::pixmapFromImageInPlace(Images::prepare(
						image->original(),
						_thumbw * cIntRetinaFactor(),
						0,
						options,
						thumbSize,
						thumbSize));
				}
				_thumbnailImageLoaded = true;
			};
			_refreshThumbnail();
		}

		if (_documentMedia) {
			const auto document = _documentMedia->owner();
			const auto nameString = document->isVoiceMessage()
				? tr::lng_media_audio(tr::now)
				: document->composeNameString();
			setName(nameString, document->size);
			_isImage = document->isImage();
			_isAudio = document->isVoiceMessage()
				|| document->isAudioFile();
		}
	} else {
		auto maxW = 0, maxH = 0;
		const auto limitW = st::sendMediaPreviewSize;
		auto limitH = std::min(st::confirmMaxHeight, _gifh ? _gifh : INT_MAX);
		if (_animated) {
			maxW = std::max(dimensions.width(), 1);
			maxH = std::max(dimensions.height(), 1);
			if (maxW * limitH > maxH * limitW) {
				if (maxW < limitW) {
					maxH = maxH * limitW / maxW;
					maxW = limitW;
				}
			} else {
				if (maxH < limitH) {
					maxW = maxW * limitH / maxH;
					maxH = limitH;
				}
			}
			_refreshThumbnail = [=] {
				const auto image = computeImage();
				const auto use = image ? image : Image::BlankMedia().get();
				const auto options = Images::Option::Smooth
					| Images::Option::Blurred;
				_thumb = use->pixNoCache(
					maxW * cIntRetinaFactor(),
					maxH * cIntRetinaFactor(),
					options,
					maxW,
					maxH);
				_thumbnailImageLoaded = true;
			};
		} else {
			Assert(_photoMedia != nullptr);

			maxW = dimensions.width();
			maxH = dimensions.height();
			_refreshThumbnail = [=] {
				const auto image = computeImage();
				const auto photo = _photoMedia->image(Data::PhotoSize::Large);
				const auto use = photo
					? photo
					: image
					? image
					: Image::BlankMedia().get();
				const auto options = Images::Option::Smooth
					| (photo
						? Images::Option(0)
						: Images::Option::Blurred);
				_thumbnailImageLoaded = (photo != nullptr);
				_thumb = use->pixNoCache(
					maxW * cIntRetinaFactor(),
					maxH * cIntRetinaFactor(),
					options,
					maxW,
					maxH);
			};
		}
		_refreshThumbnail();

		const auto resizeDimensions = [&](int &thumbWidth, int &thumbHeight, int &thumbX) {
			auto tw = thumbWidth, th = thumbHeight;
			if (!tw || !th) {
				tw = th = 1;
			}

			// Edit media button takes place on thumb preview
			// And its height can be greater than height of thumb.
			const auto minThumbHeight = st::editMediaButtonSize
				+ st::editMediaButtonSkip * 2;
			const auto minThumbWidth = minThumbHeight * tw / th;

			if (thumbWidth < st::sendMediaPreviewSize) {
				thumbWidth = (thumbWidth > minThumbWidth)
					? thumbWidth
					: minThumbWidth;
			} else {
				thumbWidth = st::sendMediaPreviewSize;
			}
			const auto maxThumbHeight = std::min(int(std::round(1.5 * thumbWidth)), limitH);
			thumbHeight = int(std::round(th * float64(thumbWidth) / tw));
			if (thumbHeight > maxThumbHeight) {
				thumbWidth = int(std::round(thumbWidth * float64(maxThumbHeight) / thumbHeight));
				thumbHeight = maxThumbHeight;
				if (thumbWidth < 10) {
					thumbWidth = 10;
				}
			}
			thumbX = (st::boxWideWidth - thumbWidth) / 2;
		};

		if (_documentMedia && _documentMedia->owner()->isAnimation()) {
			resizeDimensions(_gifw, _gifh, _gifx);
		}
		limitH = std::min(st::confirmMaxHeight, _gifh ? _gifh : INT_MAX);

		_thumbw = _thumb.width();
		_thumbh = _thumb.height();
		// If thumb's and resized gif's sizes are equal,
		// Then just take made values.
		if (_thumbw == _gifw && _thumbh == _gifh) {
			_thumbx = (st::boxWideWidth - _thumbw) / 2;
		} else {
			resizeDimensions(_thumbw, _thumbh, _thumbx);
		}

		const auto prepareBasicThumb = _refreshThumbnail;
		const auto scaleThumbDown = [=] {
			_thumb = App::pixmapFromImageInPlace(_thumb.toImage().scaled(
				_thumbw * cIntRetinaFactor(),
				_thumbh * cIntRetinaFactor(),
				Qt::KeepAspectRatio,
				Qt::SmoothTransformation));
			_thumb.setDevicePixelRatio(cRetinaFactor());
		};
		_refreshThumbnail = [=] {
			prepareBasicThumb();
			scaleThumbDown();
		};
		scaleThumbDown();
	}
	Assert(_animated || _photo || _doc);
	Assert(_thumbnailImageLoaded || _refreshThumbnail);

	if (!_thumbnailImageLoaded) {
		_controller->session().downloaderTaskFinished(
		) | rpl::start_with_next([=] {
			if (_thumbnailImageLoaded
				|| (_photoMedia && !_photoMedia->image(PhotoSize::Large))
				|| (_documentMedia && !_documentMedia->thumbnail())) {
				return;
			}
			_refreshThumbnail();
			update();
		}, lifetime());
	}
	_field.create(
		this,
		st::confirmCaptionArea,
		Ui::InputField::Mode::MultiLine,
		tr::lng_photo_caption(),
		editData);
	_field->setMaxLength(
		_controller->session().serverConfig().captionLengthMax);
	_field->setSubmitSettings(
		Core::App().settings().sendSubmitWay());
	_field->setInstantReplaces(Ui::InstantReplaces::Default());
	_field->setInstantReplacesEnabled(
		Core::App().settings().replaceEmojiValue());
	_field->setMarkdownReplacesEnabled(rpl::single(true));
	_field->setEditLinkCallback(
		DefaultEditLinkCallback(_controller, _field));

	InitSpellchecker(_controller, _field);

	auto r = object_ptr<Ui::SlideWrap<Ui::Checkbox>>(
		this,
		object_ptr<Ui::Checkbox>(
			this,
			tr::lng_send_compressed(tr::now),
			true,
			st::defaultBoxCheckbox),
		st::editMediaCheckboxMargins);
	_wayWrap = r.data();
	_wayWrap->toggle(false, anim::type::instant);

	r->entity()->checkedChanges(
	) | rpl::start_with_next([&](bool checked) {
		_asFile = !checked;
	}, _wayWrap->lifetime());

	_controller->session().data().itemRemoved(
		_msgId
	) | rpl::start_with_next([=] {
		closeBox();
	}, lifetime());
}

EditCaptionBox::~EditCaptionBox() = default;

void EditCaptionBox::emojiFilterForGeometry(not_null<QEvent*> event) {
	const auto type = event->type();
	if (type == QEvent::Move || type == QEvent::Resize) {
		// updateEmojiPanelGeometry uses not only container geometry, but
		// also container children geometries that will be updated later.
		crl::on_main(this, [=] { updateEmojiPanelGeometry(); });
	}
}

void EditCaptionBox::updateEmojiPanelGeometry() {
	const auto parent = _emojiPanel->parentWidget();
	const auto global = _emojiToggle->mapToGlobal({ 0, 0 });
	const auto local = parent->mapFromGlobal(global);
	_emojiPanel->moveBottomRight(
		local.y(),
		local.x() + _emojiToggle->width() * 3);
}

void EditCaptionBox::prepareStreamedPreview() {
	const auto isListEmpty = _preparedList.files.empty();
	if (_streamed) {
		return;
	} else if (!_documentMedia && isListEmpty) {
		return;
	}
	const auto document = _documentMedia
		? _documentMedia->owner().get()
		: nullptr;
	if (document && document->isAnimation()) {
		setupStreamedPreview(
			document->owner().streaming().sharedDocument(
				document,
				_msgId));
	} else if (!isListEmpty) {
		const auto file = &_preparedList.files.front();
		auto loader = file->path.isEmpty()
			? MakeBytesLoader(file->content)
			: MakeFileLoader(file->path);
		setupStreamedPreview(std::make_shared<Document>(std::move(loader)));
	}
}

void EditCaptionBox::setupStreamedPreview(std::shared_ptr<Document> shared) {
	if (!shared) {
		return;
	}
	_streamed = std::make_unique<Instance>(
		std::move(shared),
		[=] { update(); });
	_streamed->lockPlayer();
	_streamed->player().updates(
	) | rpl::start_with_next_error([=](Update &&update) {
		handleStreamingUpdate(std::move(update));
	}, [=](Error &&error) {
		handleStreamingError(std::move(error));
	}, _streamed->lifetime());

	if (_streamed->ready()) {
		streamingReady(base::duplicate(_streamed->info()));
	}
	checkStreamedIsStarted();
}

void EditCaptionBox::handleStreamingUpdate(Update &&update) {
	v::match(update.data, [&](Information &update) {
		streamingReady(std::move(update));
	}, [&](const PreloadedVideo &update) {
	}, [&](const UpdateVideo &update) {
		this->update();
	}, [&](const PreloadedAudio &update) {
	}, [&](const UpdateAudio &update) {
	}, [&](const WaitingForData &update) {
	}, [&](MutedByOther) {
	}, [&](Finished) {
	});
}

void EditCaptionBox::handleStreamingError(Error &&error) {
}

void EditCaptionBox::streamingReady(Information &&info) {
	const auto calculateGifDimensions = [&]() {
		const auto scaled = QSize(
			info.video.size.width(),
			info.video.size.height()
		).scaled(
			st::sendMediaPreviewSize * cIntRetinaFactor(),
			st::confirmMaxHeight * cIntRetinaFactor(),
			Qt::KeepAspectRatio);
		_thumbw = _gifw = scaled.width();
		_thumbh = _gifh = scaled.height();
		_thumbx = _gifx = (st::boxWideWidth - _gifw) / 2;
		updateBoxSize();
	};
	// If gif file is not mp4,
	// Its dimension values will be known only after reading.
	if (_gifw <= 0 || _gifh <= 0) {
		calculateGifDimensions();
	}
}

void EditCaptionBox::updateEditPreview() {
	using Info = Ui::PreparedFileInformation;

	const auto file = &_preparedList.files.front();
	const auto fileMedia = &file->information->media;

	const auto fileinfo = QFileInfo(file->path);
	const auto filename = fileinfo.fileName();

	const auto mime = file->information->filemime;
	_isImage = Core::FileIsImage(filename, mime);
	_isAudio = false;
	_animated = false;
	_photo = false;
	_doc = false;
	_streamed = nullptr;
	_thumbw = _thumbh = _thumbx = 0;
	_gifw = _gifh = _gifx = 0;

	auto isGif = false;
	auto shouldAsDoc = true;
	auto docPhotoSize = QSize();
	if (const auto image = std::get_if<Info::Image>(fileMedia)) {
		shouldAsDoc = !Ui::ValidateThumbDimensions(
			image->data.width(),
			image->data.height()
		) || (_albumType == Ui::AlbumType::File);
		if (shouldAsDoc) {
			docPhotoSize.setWidth(image->data.width());
			docPhotoSize.setHeight(image->data.height());
		}
		isGif = image->animated;
		_animated = isGif;
		_photo = !isGif && !shouldAsDoc;
		_isImage = true;
	} else if (const auto video = std::get_if<Info::Video>(fileMedia)) {
		isGif = video->isGifv;
		_animated = true;
		shouldAsDoc = false;
	}
	if (shouldAsDoc) {
		auto nameString = filename;
		if (const auto song = std::get_if<Info::Song>(fileMedia)) {
			nameString = Ui::ComposeNameString(
				filename,
				song->title,
				song->performer);
			_isAudio = true;

			if (auto cover = song->cover; !cover.isNull()) {
				_thumb = Ui::PrepareSongCoverForThumbnail(
					cover,
					st::msgFileLayout.thumbSize);
				_thumbw = _thumb.width() / cIntRetinaFactor();
				_thumbh = _thumb.height() / cIntRetinaFactor();
			}
		}

		const auto getExt = [&] {
			auto patterns = Core::MimeTypeForName(mime).globPatterns();
			if (!patterns.isEmpty()) {
				return patterns.front().replace('*', QString());
			}
			return QString();
		};
		setName(
			nameString.isEmpty()
				? filedialogDefaultName(
					_isImage ? qsl("image") : qsl("file"),
					getExt(),
					QString(),
					true)
				: nameString,
			fileinfo.size()
				? fileinfo.size()
				: _preparedList.files.front().content.size());
		// Show image dimensions if it should be sent as doc.
		if (_isImage && docPhotoSize.isValid()) {
			_status = qsl("%1x%2")
				.arg(docPhotoSize.width())
				.arg(docPhotoSize.height());
		}
		_doc = true;
	}

	const auto showCheckbox = _photo && (_albumType == Ui::AlbumType::None);
	_wayWrap->toggle(showCheckbox, anim::type::instant);

	if (!_doc) {
		_thumb = App::pixmapFromImageInPlace(
			file->preview.scaled(
				st::sendMediaPreviewSize * cIntRetinaFactor(),
				(st::confirmMaxHeight - (showCheckbox
					? st::confirmMaxHeightSkip
					: 0)) * cIntRetinaFactor(),
				Qt::KeepAspectRatio));
		_thumbw = _thumb.width() / cIntRetinaFactor();
		_thumbh = _thumb.height() / cIntRetinaFactor();
		_thumbx = (st::boxWideWidth - _thumbw) / 2;
		if (isGif) {
			_gifw = _thumbw;
			_gifh = _thumbh;
			_gifx = _thumbx;
			prepareStreamedPreview();
		}
	}
	updateEditMediaButton();
	captionResized();
}

void EditCaptionBox::updateEditMediaButton() {
	const auto icon = _doc
		? &st::editMediaButtonIconFile
		: &st::editMediaButtonIconPhoto;
	const auto color = _doc ? &st::windowBgRipple : &st::callFingerprintBg;
	_editMedia->setIconOverride(icon);
	_editMedia->setRippleColorOverride(color);
	_editMedia->setForceRippled(!_doc, anim::type::instant);
}

void EditCaptionBox::createEditMediaButton() {
	const auto callback = [=](FileDialog::OpenResult &&result) {
		auto showError = [](tr::phrase<> t) {
			Ui::Toast::Show(t(tr::now));
		};

		const auto checkResult = [=](const Ui::PreparedList &list) {
			if (list.files.size() != 1) {
				return false;
			}
			const auto &file = list.files.front();
			const auto mime = file.information->filemime;
			if (Core::IsMimeSticker(mime)) {
				showError(tr::lng_edit_media_invalid_file);
				return false;
			} else if (_albumType != Ui::AlbumType::None
				&& !file.canBeInAlbumType(_albumType)) {
				showError(tr::lng_edit_media_album_error);
				return false;
			}
			return true;
		};
		auto list = Storage::PreparedFileFromFilesDialog(
			std::move(result),
			checkResult,
			showError,
			st::sendMediaPreviewSize);

		if (list) {
			setPreparedList(std::move(*list));
		}
	};

	const auto buttonCallback = [=] {
		const auto filters = (_albumType == Ui::AlbumType::PhotoVideo)
			? FileDialog::PhotoVideoFilesFilter()
			: FileDialog::AllFilesFilter();
		FileDialog::GetOpenPath(
			this,
			tr::lng_choose_file(tr::now),
			filters,
			crl::guard(this, callback));
	};

	_editMediaClicks.events(
	) | rpl::start_with_next(
		buttonCallback,
		lifetime());

	// Create edit media button.
	_editMedia.create(this, st::editMediaButton);
	updateEditMediaButton();
	_editMedia->setClickedCallback(
		App::LambdaDelayed(
			st::historyAttach.ripple.hideDuration,
			this,
			buttonCallback));
}

void EditCaptionBox::prepare() {
	if (_animated) {
		prepareStreamedPreview();
	}

	addButton(tr::lng_settings_save(), [this] { save(); });
	if (_isAllowedEditMedia) {
		createEditMediaButton();
	} else {
		_preparedList.files.clear();
	}
	addButton(tr::lng_cancel(), [this] { closeBox(); });

	updateBoxSize();
	connect(_field, &Ui::InputField::submitted, [=] { save(); });
	connect(_field, &Ui::InputField::cancelled, [=] { closeBox(); });
	connect(_field, &Ui::InputField::resized, [=] { captionResized(); });
	_field->setMimeDataHook([=](
			not_null<const QMimeData*> data,
			Ui::InputField::MimeAction action) {
		if (action == Ui::InputField::MimeAction::Check) {
			if (!data->hasText() && !_isAllowedEditMedia) {
				return false;
			} else if (Storage::ValidateEditMediaDragData(data, _albumType)) {
				return true;
			}
			return data->hasText();
		} else if (action == Ui::InputField::MimeAction::Insert) {
			return fileFromClipboard(data);
		}
		Unexpected("action in MimeData hook.");
	});
	Ui::Emoji::SuggestionsController::Init(
		getDelegate()->outerContainer(),
		_field,
		&_controller->session());

	setupEmojiPanel();

	auto cursor = _field->textCursor();
	cursor.movePosition(QTextCursor::End);
	_field->setTextCursor(cursor);

	setupDragArea();
}

bool EditCaptionBox::fileFromClipboard(not_null<const QMimeData*> data) {
	return setPreparedList(ListFromMimeData(data));
}

bool EditCaptionBox::setPreparedList(Ui::PreparedList &&list) {
	if (!_isAllowedEditMedia) {
		return false;
	}
	using Error = Ui::PreparedList::Error;
	using Type = Ui::PreparedFile::Type;
	if (list.error != Error::None || list.files.empty()) {
		return false;
	}
	auto file = &list.files.front();
	const auto invalidForAlbum = (_albumType != Ui::AlbumType::None)
		&& !file->canBeInAlbumType(_albumType);
	if (_albumType == Ui::AlbumType::PhotoVideo) {
		using Video = Ui::PreparedFileInformation::Video;
		if (const auto video = std::get_if<Video>(&file->information->media)) {
			video->isGifv = false;
		}
	}
	if (invalidForAlbum) {
		Ui::Toast::Show(tr::lng_edit_media_album_error(tr::now));
		return false;
	}
	_photo = _isImage = (file->type == Type::Photo);
	_preparedList = std::move(list);
	updateEditPreview();
	return true;
}

void EditCaptionBox::captionResized() {
	updateBoxSize();
	resizeEvent(0);
	updateEmojiPanelGeometry();
	update();
}

void EditCaptionBox::setupEmojiPanel() {
	const auto container = getDelegate()->outerContainer();
	_emojiPanel = base::make_unique_q<ChatHelpers::TabbedPanel>(
		container,
		_controller,
		object_ptr<ChatHelpers::TabbedSelector>(
			nullptr,
			_controller,
			ChatHelpers::TabbedSelector::Mode::EmojiOnly));
	_emojiPanel->setDesiredHeightValues(
		1.,
		st::emojiPanMinHeight / 2,
		st::emojiPanMinHeight);
	_emojiPanel->hide();
	_emojiPanel->selector()->emojiChosen(
	) | rpl::start_with_next([=](EmojiPtr emoji) {
		Ui::InsertEmojiAtCursor(_field->textCursor(), emoji);
	}, lifetime());

	const auto filterCallback = [=](not_null<QEvent*> event) {
		emojiFilterForGeometry(event);
		return base::EventFilterResult::Continue;
	};
	_emojiFilter.reset(base::install_event_filter(container, filterCallback));

	_emojiToggle.create(this, st::boxAttachEmoji);
	_emojiToggle->installEventFilter(_emojiPanel);
	_emojiToggle->addClickHandler([=] {
		_emojiPanel->toggleAnimated();
	});
}


void EditCaptionBox::setupDragArea() {
	auto enterFilter = [=](not_null<const QMimeData*> data) {
		return !_isAllowedEditMedia
			? false
			: Storage::ValidateEditMediaDragData(data, _albumType);
	};
	// Avoid both drag areas appearing at one time.
	auto computeState = [=](const QMimeData *data) {
		const auto state = Storage::ComputeMimeDataState(data);
		return (state == Storage::MimeDataState::PhotoFiles)
			? Storage::MimeDataState::Image
			: state;
	};
	const auto areas = DragArea::SetupDragAreaToContainer(
		this,
		std::move(enterFilter),
		[=](bool f) { _field->setAcceptDrops(f); },
		nullptr,
		std::move(computeState));

	const auto droppedCallback = [=](bool compress) {
		return [=](const QMimeData *data) {
			fileFromClipboard(data);
			Window::ActivateWindow(_controller);
		};
	};
	areas.document->setDroppedCallback(droppedCallback(false));
	areas.photo->setDroppedCallback(droppedCallback(true));
}

bool EditCaptionBox::isThumbedLayout() const {
	return (_thumbw && !_isAudio);
}

void EditCaptionBox::updateBoxSize() {
	auto newHeight = st::boxPhotoPadding.top() + st::boxPhotoCaptionSkip + _field->height() + errorTopSkip() + st::normalFont->height;
	if (_photo) {
		newHeight += _wayWrap->height() / 2;
	}
	const auto &st = isThumbedLayout()
		? st::msgFileThumbLayout
		: st::msgFileLayout;
	if (_photo || _animated) {
		newHeight += std::max(_thumbh, _gifh);
	} else if (isThumbedLayout() || _doc) {
		newHeight += 0 + st.thumbSize + 0;
	} else {
		newHeight += st::boxTitleFont->height;
	}
	setDimensions(st::boxWideWidth, newHeight, true);
}

int EditCaptionBox::errorTopSkip() const {
	return (st::defaultBox.buttonPadding.top() / 2);
}

void EditCaptionBox::checkStreamedIsStarted() {
	if (!_streamed) {
		return;
	} else if (_streamed->paused()) {
		_streamed->resume();
	}
	if (!_streamed->active() && !_streamed->failed()) {
		startStreamedPlayer();
	}
}

void EditCaptionBox::startStreamedPlayer() {
	auto options = ::Media::Streaming::PlaybackOptions();
	options.audioId = _documentMedia
		? AudioMsgId(_documentMedia->owner(), _msgId)
		: AudioMsgId();
	options.waitForMarkAsShown = true;
	//if (!_streamed->withSound) {
	options.mode = ::Media::Streaming::Mode::Video;
	options.loop = true;
	//}
	_streamed->play(options);
}

void EditCaptionBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	if (_photo || _animated) {
		const auto th = std::max(_gifh, _thumbh);
		if (_thumbx > st::boxPhotoPadding.left()) {
			p.fillRect(st::boxPhotoPadding.left(), st::boxPhotoPadding.top(), _thumbx - st::boxPhotoPadding.left(), th, st::confirmBg);
		}
		if (_thumbx + _thumbw < width() - st::boxPhotoPadding.right()) {
			p.fillRect(_thumbx + _thumbw, st::boxPhotoPadding.top(), width() - st::boxPhotoPadding.right() - _thumbx - _thumbw, th, st::confirmBg);
		}
		checkStreamedIsStarted();
		if (_streamed
			&& _streamed->player().ready()
			&& !_streamed->player().videoSize().isEmpty()) {
			const auto s = QSize(_gifw, _gifh);
			const auto paused = _controller->isGifPausedAtLeastFor(Window::GifPauseReason::Layer);

			auto request = ::Media::Streaming::FrameRequest();
			request.outer = s * cIntRetinaFactor();
			request.resize = s * cIntRetinaFactor();
			p.drawImage(
				QRect(_gifx, st::boxPhotoPadding.top(), _gifw, _gifh),
				_streamed->frame(request));
			if (!paused) {
				_streamed->markFrameShown();
			}
		} else {
			const auto offset = _gifh ? ((_gifh - _thumbh) / 2) : 0;
			p.drawPixmap(_thumbx, st::boxPhotoPadding.top() + offset, _thumb);
		}
		if (_animated && !_streamed) {
			const auto &st = st::msgFileLayout;
			QRect inner(_thumbx + (_thumbw - st.thumbSize) / 2, st::boxPhotoPadding.top() + (th - st.thumbSize) / 2, st.thumbSize, st.thumbSize);
			p.setPen(Qt::NoPen);
			p.setBrush(st::msgDateImgBg);

			{
				PainterHighQualityEnabler hq(p);
				p.drawEllipse(inner);
			}

			const auto icon = &st::historyFileInPlay;
			icon->paintInCenter(p, inner);
		}
	} else if (_doc) {
		const auto &st = isThumbedLayout()
			? st::msgFileThumbLayout
			: st::msgFileLayout;
		const auto w = width() - st::boxPhotoPadding.left() - st::boxPhotoPadding.right();
		const auto h = 0 + st.thumbSize + 0;
		const auto nameleft = 0 + st.thumbSize + st.padding.right();
		const auto nametop = st.nameTop - st.padding.top();
		const auto nameright = 0;
		const auto statustop = st.statusTop - st.padding.top();
		const auto editButton = _isAllowedEditMedia
			? _editMedia->width() + st::editMediaButtonSkip
			: 0;
		const auto namewidth = w - nameleft - editButton;
		const auto x = (width() - w) / 2, y = st::boxPhotoPadding.top();

//		Ui::FillRoundCorner(p, x, y, w, h, st::msgInBg, Ui::MessageInCorners, &st::msgInShadow);

		const auto rthumb = style::rtlrect(x + 0, y + 0, st.thumbSize, st.thumbSize, width());
		if (isThumbedLayout()) {
			p.drawPixmap(rthumb.topLeft(), _thumb);
		} else {
			p.setPen(Qt::NoPen);

			if (_isAudio && _thumbw) {
				p.drawPixmap(rthumb.topLeft(), _thumb);
			} else {
				p.setBrush(st::msgFileInBg);
				PainterHighQualityEnabler hq(p);
				p.drawEllipse(rthumb);
			}

			const auto icon = &(_isAudio
				? (_thumbw ? st::historyFileSongPlay : st::historyFileInPlay)
				: _isImage
				? st::historyFileInImage
				: st::historyFileInDocument);
			icon->paintInCenter(p, rthumb);
		}
		p.setFont(st::semiboldFont);
		p.setPen(st::historyFileNameInFg);
		_name.drawLeftElided(p, x + nameleft, y + nametop, namewidth, width());

		const auto &status = st::mediaInFg;
		p.setFont(st::normalFont);
		p.setPen(status);
		p.drawTextLeft(x + nameleft, y + statustop, width(), _status);
	} else {
		p.setFont(st::boxTitleFont);
		p.setPen(st::boxTextFg);
		p.drawTextLeft(_field->x(), st::boxPhotoPadding.top(), width(), tr::lng_edit_message(tr::now));
	}

	if (!_error.isEmpty()) {
		p.setFont(st::normalFont);
		p.setPen(st::boxTextFgError);
		p.drawTextLeft(_field->x(), _field->y() + _field->height() + errorTopSkip(), width(), _error);
	}

	if (_isAllowedEditMedia) {
		_editMedia->moveToRight(
			st::boxPhotoPadding.right() + (_doc
				? st::editMediaButtonFileSkipRight
				: st::editMediaButtonSkip),
			st::boxPhotoPadding.top() + (_doc
				? st::editMediaButtonFileSkipTop
				: st::editMediaButtonSkip));
	}
}

void EditCaptionBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	if (_photo) {
		_wayWrap->resize(st::sendMediaPreviewSize, _wayWrap->height());
		_wayWrap->moveToLeft(
			st::boxPhotoPadding.left(),
			st::boxPhotoPadding.top() + _thumbh);
	}

	_field->resize(st::sendMediaPreviewSize, _field->height());
	_field->moveToLeft(st::boxPhotoPadding.left(), height() - st::normalFont->height - errorTopSkip() - _field->height());
	_emojiToggle->moveToLeft(
		(st::boxPhotoPadding.left()
			+ st::sendMediaPreviewSize
			- _emojiToggle->width()),
		_field->y() + st::boxAttachEmojiTop);
}

void EditCaptionBox::setInnerFocus() {
	_field->setFocusFast();
}

void EditCaptionBox::save() {
	if (_saveRequestId) return;

	const auto item = _controller->session().data().message(_msgId);
	if (!item) {
		_error = tr::lng_edit_deleted(tr::now);
		update();
		return;
	}

	const auto textWithTags = _field->getTextWithAppliedMarkdown();
	const auto sending = TextWithEntities{
		textWithTags.text,
		TextUtilities::ConvertTextTagsToEntities(textWithTags.tags)
	};

	auto options = Api::SendOptions();
	options.scheduled = item->isScheduled() ? item->date() : 0;

	if (!_preparedList.files.empty()) {
		auto action = Api::SendAction(item->history());
		action.options = options;
		action.replaceMediaOf = item->fullId().msg;

		_controller->session().api().editMedia(
			std::move(_preparedList),
			(!_asFile && _photo) ? SendMediaType::Photo : SendMediaType::File,
			_field->getTextWithAppliedMarkdown(),
			action);
		closeBox();
		return;
	}

	const auto done = crl::guard(this, [=](const MTPUpdates &updates) {
		_saveRequestId = 0;
		closeBox();
	});

	const auto fail = crl::guard(this, [=](const MTP::Error &error) {
		_saveRequestId = 0;
		const auto &type = error.type();
		if (ranges::contains(Api::kDefaultEditMessagesErrors, type)) {
			_error = tr::lng_edit_error(tr::now);
			update();
		} else if (type == u"MESSAGE_NOT_MODIFIED"_q) {
			closeBox();
		} else if (type == u"MESSAGE_EMPTY"_q) {
			_field->setFocus();
			_field->showError();
			update();
		} else {
			_error = tr::lng_edit_error(tr::now);
			update();
		}
	});

	lifetime().add([=] {
		if (_saveRequestId) {
			auto &session = _controller->session();
			session.api().request(base::take(_saveRequestId)).cancel();
		}
	});

	_saveRequestId = Api::EditCaption(item, sending, options, done, fail);
}

void EditCaptionBox::setName(QString nameString, qint64 size) {
	_name.setText(
		st::semiboldTextStyle,
		nameString,
		Ui::NameTextOptions());
	_status = Ui::FormatSizeText(size);
}

void EditCaptionBox::keyPressEvent(QKeyEvent *e) {
	if ((e->key() == Qt::Key_E || e->key() == Qt::Key_O)
		 && e->modifiers() == Qt::ControlModifier) {
		_editMediaClicks.fire({});
	} else {
		e->ignore();
	}
}
