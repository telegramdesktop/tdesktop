/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/edit_caption_box.h"

#include "apiwrap.h"
#include "main/main_session.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "core/event_filter.h"
#include "core/file_utilities.h"
#include "core/mime_type.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_photo.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "layout.h"
#include "media/clip/media_clip_reader.h"
#include "storage/storage_media_prepare.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_history.h"
#include "ui/image/image.h"
#include "ui/special_buttons.h"
#include "ui/text_options.h"
#include "ui/widgets/input_fields.h"
#include "window/window_session_controller.h"
#include "ui/widgets/checkbox.h"
#include "confirm_box.h"

EditCaptionBox::EditCaptionBox(
	QWidget*,
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item)
: _controller(controller)
, _msgId(item->fullId()) {
	Expects(item->media() != nullptr);
	Expects(item->media()->allowsEditCaption());

	_isAllowedEditMedia = item->media()->allowsEditMedia();
	_isAlbum = !item->groupId().empty();

	QSize dimensions;
	auto image = (Image*)nullptr;
	DocumentData *doc = nullptr;

	const auto media = item->media();
	if (const auto photo = media->photo()) {
		_photo = true;
		dimensions = QSize(photo->width(), photo->height());
		image = photo->large();
	} else if (const auto document = media->document()) {
		image = document->thumbnail();
		dimensions = image
			? image->size()
			: document->dimensions;
		if (document->isAnimation()) {
			_gifw = document->dimensions.width();
			_gifh = document->dimensions.height();
			_animated = true;
		} else if (document->isVideoFile()) {
			_animated = true;
		} else {
			_doc = true;
		}
		doc = document;
	}
	const auto editData = PrepareEditText(item);

	if (!_animated && (dimensions.isEmpty() || doc || !image)) {
		if (!image) {
			_thumbw = 0;
		} else {
			const auto tw = image->width(), th = image->height();
			if (tw > th) {
				_thumbw = (tw * st::msgFileThumbSize) / th;
			} else {
				_thumbw = st::msgFileThumbSize;
			}
			_thumbnailImage = image;
			_refreshThumbnail = [=] {
				const auto options = Images::Option::Smooth
					| Images::Option::RoundedSmall
					| Images::Option::RoundedTopLeft
					| Images::Option::RoundedTopRight
					| Images::Option::RoundedBottomLeft
					| Images::Option::RoundedBottomRight;
				_thumb = App::pixmapFromImageInPlace(Images::prepare(
					image->pix(_msgId).toImage(),
					_thumbw * cIntRetinaFactor(),
					0,
					options,
					st::msgFileThumbSize,
					st::msgFileThumbSize));
			};
		}

		if (doc) {
			const auto nameString = doc->isVoiceMessage()
				? tr::lng_media_audio(tr::now)
				: doc->composeNameString();
			setName(nameString, doc->size);
			_isImage = doc->isImage();
			_isAudio = (doc->isVoiceMessage() || doc->isAudioFile());
		}
		if (_refreshThumbnail) {
			_refreshThumbnail();
		}
	} else {
		if (!image) {
			image = Image::BlankMedia();
		}
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
			_thumbnailImage = image;
			_refreshThumbnail = [=] {
				const auto options = Images::Option::Smooth
					| Images::Option::Blurred;
				_thumb = image->pixNoCache(
					_msgId,
					maxW * cIntRetinaFactor(),
					maxH * cIntRetinaFactor(),
					options,
					maxW,
					maxH);
			};
			prepareGifPreview(doc);
		} else {
			maxW = dimensions.width();
			maxH = dimensions.height();
			_thumbnailImage = image;
			_refreshThumbnail = [=] {
				_thumb = image->pixNoCache(
					_msgId,
					maxW * cIntRetinaFactor(),
					maxH * cIntRetinaFactor(),
					Images::Option::Smooth,
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

		if (doc && doc->isAnimation()) {
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

	_thumbnailImageLoaded = _thumbnailImage
		? _thumbnailImage->loaded()
		: true;
	subscribe(_controller->session().downloaderTaskFinished(), [=] {
		if (!_thumbnailImageLoaded
			&& _thumbnailImage
			&& _thumbnailImage->loaded()) {
			_thumbnailImageLoaded = true;
			_refreshThumbnail();
			update();
		}
		if (doc && doc->isAnimation() && doc->loaded() && !_gifPreview) {
			prepareGifPreview(doc);
		}
	});

	_field.create(
		this,
		st::confirmCaptionArea,
		Ui::InputField::Mode::MultiLine,
		tr::lng_photo_caption(),
		editData);
	_field->setMaxLength(Global::CaptionLengthMax());
	_field->setSubmitSettings(Ui::InputField::SubmitSettings::Both);
	_field->setInstantReplaces(Ui::InstantReplaces::Default());
	_field->setInstantReplacesEnabled(
		_controller->session().settings().replaceEmojiValue());
	_field->setMarkdownReplacesEnabled(rpl::single(true));
	_field->setEditLinkCallback(
		DefaultEditLinkCallback(&_controller->session(), _field));

	auto r = object_ptr<Ui::SlideWrap<Ui::Checkbox>>(
		this,
		object_ptr<Ui::Checkbox>(
			this,
			tr::lng_send_file(tr::now),
			false,
			st::defaultBoxCheckbox),
		st::editMediaCheckboxMargins);
	_wayWrap = r.data();
	_wayWrap->toggle(false, anim::type::instant);

	r->entity()->checkedChanges(
	) | rpl::start_with_next([&](bool checked) {
		_asFile = checked;
	}, _wayWrap->lifetime());
}

bool EditCaptionBox::emojiFilter(not_null<QEvent*> event) {
	const auto type = event->type();
	if (type == QEvent::Move || type == QEvent::Resize) {
		// updateEmojiPanelGeometry uses not only container geometry, but
		// also container children geometries that will be updated later.
		crl::on_main(this, [=] { updateEmojiPanelGeometry(); });
	}
	return false;
}

void EditCaptionBox::updateEmojiPanelGeometry() {
	const auto parent = _emojiPanel->parentWidget();
	const auto global = _emojiToggle->mapToGlobal({ 0, 0 });
	const auto local = parent->mapFromGlobal(global);
	_emojiPanel->moveBottomRight(
		local.y(),
		local.x() + _emojiToggle->width() * 3);
}

void EditCaptionBox::prepareGifPreview(DocumentData* document) {
	const auto isListEmpty = _preparedList.files.empty();
	if (_gifPreview) {
		return;
	} else if (!document && isListEmpty) {
		return;
	}
	const auto callback = [=](Media::Clip::Notification notification) {
		clipCallback(notification);
	};
	if (document && document->isAnimation() && document->loaded()) {
		_gifPreview = Media::Clip::MakeReader(
			document,
			_msgId,
			callback);
	} else if (!isListEmpty) {
		const auto file = &_preparedList.files.front();
		if (file->path.isEmpty()) {
			_gifPreview = Media::Clip::MakeReader(
				file->content,
				callback);
		} else {
			_gifPreview = Media::Clip::MakeReader(
				file->path,
				callback);
		}
	}
	if (_gifPreview) _gifPreview->setAutoplay();
}

void EditCaptionBox::clipCallback(Media::Clip::Notification notification) {
	using namespace Media::Clip;
	switch (notification) {
	case NotificationReinit: {
		if (_gifPreview && _gifPreview->state() == State::Error) {
			_gifPreview.setBad();
		}

		if (_gifPreview && _gifPreview->ready() && !_gifPreview->started()) {
			const auto calculateGifDimensions = [&]() {
				const auto scaled = QSize(
					_gifPreview->width(),
					_gifPreview->height()).scaled(
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
			const auto s = QSize(_gifw, _gifh);
			_gifPreview->start(s.width(), s.height(), s.width(), s.height(), ImageRoundRadius::None, RectPart::None);
		}

		update();
	} break;

	case NotificationRepaint: {
		if (_gifPreview && !_gifPreview->currentDisplayed()) {
			update();
		}
	} break;
	}
}

void EditCaptionBox::updateEditPreview() {
	using Info = FileMediaInformation;

	const auto file = &_preparedList.files.front();
	const auto fileMedia = &file->information->media;

	const auto fileinfo = QFileInfo(file->path);
	const auto filename = fileinfo.fileName();

	_isImage = fileIsImage(filename, file->mime);
	_isAudio = false;
	_animated = false;
	_photo = false;
	_doc = false;
	_gifPreview = nullptr;
	_thumbw = _thumbh = _thumbx = 0;
	_gifw = _gifh = _gifx = 0;

	auto isGif = false;
	auto shouldAsDoc = true;
	auto docPhotoSize = QSize();
	if (const auto image = base::get_if<Info::Image>(fileMedia)) {
		shouldAsDoc = !Storage::ValidateThumbDimensions(
			image->data.width(),
			image->data.height());
		if (shouldAsDoc) {
			docPhotoSize.setWidth(image->data.width());
			docPhotoSize.setHeight(image->data.height());
		}
		isGif = image->animated;
		_animated = isGif;
		_photo = !isGif && !shouldAsDoc;
		_isImage = true;
	} else if (const auto video = base::get_if<Info::Video>(fileMedia)) {
		isGif = video->isGifv;
		_animated = true;
		shouldAsDoc = false;
	}
	if (shouldAsDoc) {
		auto nameString = filename;
		if (const auto song = base::get_if<Info::Song>(fileMedia)) {
			nameString = DocumentData::ComposeNameString(
				filename,
				song->title,
				song->performer);
			_isAudio = true;
		}

		const auto getExt = [&] {
			auto patterns = Core::MimeTypeForName(file->mime).globPatterns();
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

	const auto showCheckbox = _photo && !_isAlbum;
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
			prepareGifPreview();
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
		if (result.paths.isEmpty() && result.remoteContent.isEmpty()) {
			return;
		}

		const auto isValidFile = [](QString mimeType) {
			if (mimeType == qstr("image/webp")) {
				Ui::show(
					Box<InformBox>(tr::lng_edit_media_invalid_file(tr::now)),
					LayerOption::KeepOther);
				return false;
			}
			return true;
		};

		if (!result.remoteContent.isEmpty()) {

			auto list = Storage::PrepareMediaFromImage(
				QImage(),
				std::move(result.remoteContent),
				st::sendMediaPreviewSize);

			if (!isValidFile(list.files.front().mime)) {
				return;
			}

			if (_isAlbum) {
				const auto albumMimes = {
					"image/jpeg",
					"image/png",
					"video/mp4",
				};
				const auto file = &list.files.front();
				if (ranges::find(albumMimes, file->mime) == end(albumMimes)
					|| file->type == Storage::PreparedFile::AlbumType::None) {
					Ui::show(
						Box<InformBox>(tr::lng_edit_media_album_error(tr::now)),
						LayerOption::KeepOther);
					return;
				}
			}

			_preparedList = std::move(list);
		} else if (!result.paths.isEmpty()) {
			auto list = Storage::PrepareMediaList(
				QStringList(result.paths.front()),
				st::sendMediaPreviewSize);

			// Don't rewrite _preparedList if new list is not valid for album.
			if (_isAlbum) {
				using Info = FileMediaInformation;

				const auto media = &list.files.front().information->media;
				const auto valid = media->match([&](const Info::Image &data) {
					return Storage::ValidateThumbDimensions(
						data.data.width(),
						data.data.height())
						&& !data.animated;
				}, [&](Info::Video &data) {
					data.isGifv = false;
					return true;
				}, [](auto &&other) {
					return false;
				});
				if (!valid) {
					Ui::show(
						Box<InformBox>(tr::lng_edit_media_album_error(tr::now)),
						LayerOption::KeepOther);
					return;
				}
			}
			const auto info = QFileInfo(result.paths.front());
			if (!isValidFile(Core::MimeTypeForFile(info).name())) {
				return;
			}

			_preparedList = std::move(list);
		} else {
			return;
		}

		updateEditPreview();
	};

	const auto buttonCallback = [=] {
		const auto filters = _isAlbum
			? QStringList(qsl("Image and Video Files (*.png *.jpg *.mp4)"))
			: QStringList(FileDialog::AllFilesFilter());
		FileDialog::GetOpenPath(
			this,
			tr::lng_choose_file(tr::now),
			filters.join(qsl(";;")),
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
		App::LambdaDelayed(st::historyAttach.ripple.hideDuration, this, [=] {
		buttonCallback();
	}));
}

void EditCaptionBox::prepare() {
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
			}
			if (data->hasImage()) {
				const auto image = qvariant_cast<QImage>(data->imageData());
				if (!image.isNull()) {
					return true;
				}
			}
			if (const auto urls = data->urls(); !urls.empty()) {
				if (ranges::find_if(
					urls,
					[](const QUrl &url) { return !url.isLocalFile(); }
				) == urls.end()) {
					return true;
				}
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
}

bool EditCaptionBox::fileFromClipboard(not_null<const QMimeData*> data) {
	if (!_isAllowedEditMedia) {
		return false;
	}
	using Error = Storage::PreparedList::Error;

	auto list = [&] {
		auto url = QList<QUrl>();
		auto canAddUrl = false;
		// When we edit media, we need only 1 file.
		if (data->hasUrls()) {
			const auto first = data->urls().front();
			url.push_front(first);
			canAddUrl = first.isLocalFile();
		}
		auto result = canAddUrl
			? Storage::PrepareMediaList(url, st::sendMediaPreviewSize)
			: Storage::PreparedList(
				Error::EmptyFile,
				QString());
		if (result.error == Error::None) {
			return result;
		} else if (data->hasImage()) {
			auto image = qvariant_cast<QImage>(data->imageData());
			if (!image.isNull()) {
				_isImage = true;
				_photo = true;
				return Storage::PrepareMediaFromImage(
					std::move(image),
					QByteArray(),
					st::sendMediaPreviewSize);
			}
		}
		return result;
	}();
	if (list.error != Error::None || list.files.empty()) {
		return false;
	}
	if (list.files.front().type == Storage::PreparedFile::AlbumType::None
		&& _isAlbum) {
		Ui::show(
			Box<InformBox>(tr::lng_edit_media_album_error(tr::now)),
			LayerOption::KeepOther);
		return false;
	}

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

	_emojiFilter.reset(Core::InstallEventFilter(
		container,
		[=](not_null<QEvent*> event) { return emojiFilter(event); }));

	_emojiToggle.create(this, st::boxAttachEmoji);
	_emojiToggle->installEventFilter(_emojiPanel);
	_emojiToggle->addClickHandler([=] {
		_emojiPanel->toggleAnimated();
	});
}

void EditCaptionBox::updateBoxSize() {
	auto newHeight = st::boxPhotoPadding.top() + st::boxPhotoCaptionSkip + _field->height() + errorTopSkip() + st::normalFont->height;
	if (_photo) {
		newHeight += _wayWrap->height() / 2;
	}
	if (_photo || _animated) {
		newHeight += std::max(_thumbh, _gifh);
	} else if (_thumbw) {
		newHeight += 0 + st::msgFileThumbSize + 0;
	} else if (_doc) {
		newHeight += 0 + st::msgFileSize + 0;
	} else {
		newHeight += st::boxTitleFont->height;
	}
	setDimensions(st::boxWideWidth, newHeight, true);
}

int EditCaptionBox::errorTopSkip() const {
	return (st::boxButtonPadding.top() / 2);
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
		if (_gifPreview && _gifPreview->started()) {
			const auto s = QSize(_gifw, _gifh);
			const auto paused = _controller->isGifPausedAtLeastFor(Window::GifPauseReason::Layer);
			const auto frame = _gifPreview->current(s.width(), s.height(), s.width(), s.height(), ImageRoundRadius::None, RectPart::None, paused ? 0 : crl::now());
			p.drawPixmap(_gifx, st::boxPhotoPadding.top(), frame);
		} else {
			const auto offset = _gifh ? ((_gifh - _thumbh) / 2) : 0;
			p.drawPixmap(_thumbx, st::boxPhotoPadding.top() + offset, _thumb);
		}
		if (_animated && !_gifPreview) {
			QRect inner(_thumbx + (_thumbw - st::msgFileSize) / 2, st::boxPhotoPadding.top() + (th - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
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
		const auto w = width() - st::boxPhotoPadding.left() - st::boxPhotoPadding.right();
		const auto h = _thumbw ? (0 + st::msgFileThumbSize + 0) : (0 + st::msgFileSize + 0);
		auto nameleft = 0, nametop = 0, nameright = 0, statustop = 0;
		if (_thumbw) {
			nameleft = 0 + st::msgFileThumbSize + st::msgFileThumbPadding.right();
			nametop = st::msgFileThumbNameTop - st::msgFileThumbPadding.top();
			nameright = 0;
			statustop = st::msgFileThumbStatusTop - st::msgFileThumbPadding.top();
		} else {
			nameleft = 0 + st::msgFileSize + st::msgFilePadding.right();
			nametop = st::msgFileNameTop - st::msgFilePadding.top();
			nameright = 0;
			statustop = st::msgFileStatusTop - st::msgFilePadding.top();
		}
		const auto editButton = _isAllowedEditMedia
			? _editMedia->width() + st::editMediaButtonSkip
			: 0;
		const auto namewidth = w - nameleft - editButton;
		const auto x = (width() - w) / 2, y = st::boxPhotoPadding.top();

//		App::roundRect(p, x, y, w, h, st::msgInBg, MessageInCorners, &st::msgInShadow);

		if (_thumbw) {
			QRect rthumb(rtlrect(x + 0, y + 0, st::msgFileThumbSize, st::msgFileThumbSize, width()));
			p.drawPixmap(rthumb.topLeft(), _thumb);
		} else {
			const QRect inner(rtlrect(x + 0, y + 0, st::msgFileSize, st::msgFileSize, width()));
			p.setPen(Qt::NoPen);
			p.setBrush(st::msgFileInBg);

			{
				PainterHighQualityEnabler hq(p);
				p.drawEllipse(inner);
			}

			const auto icon = &(_isAudio ? st::historyFileInPlay : _isImage ? st::historyFileInImage : st::historyFileInDocument);
			icon->paintInCenter(p, inner);
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

	auto flags = MTPmessages_EditMessage::Flag::f_message | 0;
	if (_previewCancelled) {
		flags |= MTPmessages_EditMessage::Flag::f_no_webpage;
	}
	const auto textWithTags = _field->getTextWithAppliedMarkdown();
	auto sending = TextWithEntities{
		textWithTags.text,
		ConvertTextTagsToEntities(textWithTags.tags)
	};
	const auto prepareFlags = Ui::ItemTextOptions(
		item->history(),
		_controller->session().user()).flags;
	TextUtilities::PrepareForSending(sending, prepareFlags);
	TextUtilities::Trim(sending);

	const auto sentEntities = TextUtilities::EntitiesToMTP(
		sending.entities,
		TextUtilities::ConvertOption::SkipLocal);
	if (!sentEntities.v.isEmpty()) {
		flags |= MTPmessages_EditMessage::Flag::f_entities;
	}

	if (!_preparedList.files.empty()) {
		const auto textWithTags = _field->getTextWithAppliedMarkdown();
		auto sending = TextWithEntities{
			textWithTags.text,
			ConvertTextTagsToEntities(textWithTags.tags)
		};
		item->setText(sending);

		_controller->session().api().editMedia(
			std::move(_preparedList),
			(!_asFile && _photo) ? SendMediaType::Photo : SendMediaType::File,
			_field->getTextWithAppliedMarkdown(),
			Api::SendAction(item->history()),
			item->fullId().msg);
		closeBox();
		return;
	}

	_saveRequestId = MTP::send(
		MTPmessages_EditMessage(
			MTP_flags(flags),
			item->history()->peer->input,
			MTP_int(item->id),
			MTP_string(sending.text),
			MTPInputMedia(),
			MTPReplyMarkup(),
			sentEntities,
			MTP_int(0)), // schedule_date
		rpcDone(&EditCaptionBox::saveDone),
		rpcFail(&EditCaptionBox::saveFail));
}

void EditCaptionBox::saveDone(const MTPUpdates &updates) {
	_saveRequestId = 0;
	const auto controller = _controller;
	closeBox();
	controller->session().api().applyUpdates(updates);
}

bool EditCaptionBox::saveFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	_saveRequestId = 0;
	const auto &type = error.type();
	if (type == qstr("MESSAGE_ID_INVALID")
		|| type == qstr("CHAT_ADMIN_REQUIRED")
		|| type == qstr("MESSAGE_EDIT_TIME_EXPIRED")) {
		_error = tr::lng_edit_error(tr::now);
	} else if (type == qstr("MESSAGE_NOT_MODIFIED")) {
		closeBox();
		return true;
	} else if (type == qstr("MESSAGE_EMPTY")) {
		_field->setFocus();
		_field->showError();
	} else {
		_error = tr::lng_edit_error(tr::now);
	}
	update();
	return true;
}

void EditCaptionBox::setName(QString nameString, qint64 size) {
	_name.setText(
		st::semiboldTextStyle,
		nameString,
		Ui::NameTextOptions());
	_status = formatSizeText(size);
}

void EditCaptionBox::keyPressEvent(QKeyEvent *e) {
	if ((e->key() == Qt::Key_E || e->key() == Qt::Key_O)
		 && e->modifiers() == Qt::ControlModifier) {
		_editMediaClicks.fire({});
	} else {
		e->ignore();
	}
}
