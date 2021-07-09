/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/edit_caption_box.h"

#include "api/api_editing.h"
#include "api/api_text_entities.h"
#include "apiwrap.h"
#include "base/event_filter.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/file_utilities.h"
#include "core/mime_type.h"
#include "data/data_document.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "editor/photo_editor_layer_widget.h"
#include "history/history_drag_area.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "mtproto/mtproto_config.h"
#include "platform/platform_specific.h"
#include "storage/localimageloader.h" // SendMediaType
#include "storage/storage_media_prepare.h"
#include "ui/chat/attach/attach_item_single_file_preview.h"
#include "ui/chat/attach/attach_item_single_media_preview.h"
#include "ui/chat/attach/attach_single_file_preview.h"
#include "ui/chat/attach/attach_single_media_preview.h"
#include "ui/controls/emoji_button.h"
#include "ui/image/image.h"
#include "ui/toast/toast.h"
#include "ui/ui_utility.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"

#include <QtCore/QMimeData>

namespace {

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

Ui::AlbumType ComputeAlbumType(not_null<HistoryItem*> item) {
	if (item->groupId().empty()) {
		return Ui::AlbumType();
	}
	const auto media = item->media();

	if (media->photo()) {
		return Ui::AlbumType::PhotoVideo;
	} else if (const auto document = media->document()) {
		if (document->isVideoFile()) {
			return Ui::AlbumType::PhotoVideo;
		} else if (document->isSong()) {
			return Ui::AlbumType::Music;
		} else {
			return Ui::AlbumType::File;
		}
	}
	return Ui::AlbumType();
}

} // namespace

EditCaptionBox::EditCaptionBox(
	QWidget*,
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item)
: _controller(controller)
, _historyItem(item)
, _isAllowedEditMedia(item->media()
	? item->media()->allowsEditMedia()
	: false)
, _albumType(ComputeAlbumType(item))
, _controls(base::make_unique_q<Ui::VerticalLayout>(this))
, _scroll(base::make_unique_q<Ui::ScrollArea>(this, st::boxScroll))
, _field(base::make_unique_q<Ui::InputField>(
	this,
	st::confirmCaptionArea,
	Ui::InputField::Mode::MultiLine,
	tr::lng_photo_caption(),
	PrepareEditText(item)))
, _emojiToggle(base::make_unique_q<Ui::EmojiButton>(
	this,
	st::boxAttachEmoji))
, _topShadow(base::make_unique_q<Ui::FadeShadow>(this))
, _bottomShadow(base::make_unique_q<Ui::FadeShadow>(this)) {
	Expects(item->media() != nullptr);
	Expects(item->media()->allowsEditCaption());

	_controller->session().data().itemRemoved(
		_historyItem->fullId()
	) | rpl::start_with_next([=] {
		closeBox();
	}, lifetime());
}

EditCaptionBox::~EditCaptionBox() = default;

void EditCaptionBox::prepare() {
	addButton(tr::lng_settings_save(), [=] { save(); });
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	updateBoxSize();

	setupField();
	setupEmojiPanel();

	rebuildPreview();
	setupEditEventHandler();
	setupShadows();

	setupControls();
	setupPhotoEditorEventHandler();

	setupDragArea();

	captionResized();
}

void EditCaptionBox::rebuildPreview() {
	const auto gifPaused = [controller = _controller] {
		return controller->isGifPausedAtLeastFor(
			Window::GifPauseReason::Layer);
	};

	if (_preparedList.files.empty()) {
		const auto media = _historyItem->media();
		const auto photo = media->photo();
		const auto document = media->document();
		if (photo || document->isVideoFile() || document->isAnimation()) {
			_isPhoto = true;
			const auto media = Ui::CreateChild<Ui::ItemSingleMediaPreview>(
				this,
				gifPaused,
				_historyItem,
				Ui::AttachControls::Type::EditOnly);
			_photoMedia = media->sharedPhotoMedia();
			_content.reset(media);
		} else {
			_isPhoto = false;
			_content.reset(Ui::CreateChild<Ui::ItemSingleFilePreview>(
				this,
				_historyItem,
				Ui::AttachControls::Type::EditOnly));
		}
	} else {
		const auto &file = _preparedList.files.front();

		const auto media = Ui::SingleMediaPreview::Create(
			this,
			gifPaused,
			file,
			Ui::AttachControls::Type::EditOnly);
		if (media) {
			_isPhoto = media->isPhoto();
			_content.reset(media);
		} else {
			_isPhoto = false;
			_content.reset(Ui::CreateChild<Ui::SingleFilePreview>(
				this,
				file,
				Ui::AttachControls::Type::EditOnly));
		}
	}
	Assert(_content != nullptr);

	rpl::combine(
		_content->heightValue(),
		_footerHeight.value(),
		rpl::single(st::boxPhotoPadding.top()),
		rpl::mappers::_1 + rpl::mappers::_2 + rpl::mappers::_3
	) | rpl::start_with_next([=](int height) {
		setDimensions(
			st::boxWideWidth,
			std::min(st::sendMediaPreviewHeightMax, height),
			true);
	}, _content->lifetime());

	_content->editRequests(
	) | rpl::start_to_stream(_editMediaClicks, _content->lifetime());

	_content->modifyRequests(
	) | rpl::start_to_stream(_photoEditorOpens, _content->lifetime());

	_content->heightValue(
	) | rpl::start_to_stream(_contentHeight, _content->lifetime());

	_scroll->setOwnedWidget(
		object_ptr<Ui::RpWidget>::fromRaw(_content.get()));

	captionResized();
}

void EditCaptionBox::setupField() {
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
	_field->setMaxHeight(st::confirmCaptionArea.heightMax);

	InitSpellchecker(_controller, _field);

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
		Unexpected("Action in MimeData hook.");
	});
	Ui::Emoji::SuggestionsController::Init(
		getDelegate()->outerContainer(),
		_field,
		&_controller->session());

	auto cursor = _field->textCursor();
	cursor.movePosition(QTextCursor::End);
	_field->setTextCursor(cursor);
}

void EditCaptionBox::setupShadows() {
	using namespace rpl::mappers;

	const auto _topShadow = Ui::CreateChild<Ui::FadeShadow>(this);
	const auto _bottomShadow = Ui::CreateChild<Ui::FadeShadow>(this);
	_scroll->geometryValue(
	) | rpl::start_with_next([=](const QRect &geometry) {
		_topShadow->resizeToWidth(geometry.width());
		_topShadow->move(
			geometry.x(),
			geometry.y());
		_bottomShadow->resizeToWidth(geometry.width());
		_bottomShadow->move(
			geometry.x(),
			geometry.y() + geometry.height() - st::lineWidth);
	}, _topShadow->lifetime());

	_topShadow->toggleOn(_scroll->scrollTopValue() | rpl::map(_1 > 0));
	_bottomShadow->toggleOn(rpl::combine(
		_scroll->scrollTopValue(),
		_scroll->heightValue(),
		_contentHeight.events(),
		_1 + _2 < _3));
}

void EditCaptionBox::setupControls() {
	auto hintLabelToggleOn = _isPhoto.value(
	) | rpl::map([=](bool value) {
		return _controller->session().settings().photoEditorHintShown()
			? value
			: false;
	});

	_controls->add(object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
		this,
		object_ptr<Ui::FlatLabel>(
			this,
			tr::lng_edit_photo_editor_hint(tr::now),
			st::editMediaHintLabel),
		st::editMediaLabelMargins)
	)->toggleOn(std::move(hintLabelToggleOn), anim::type::instant);

	_controls->add(object_ptr<Ui::SlideWrap<Ui::Checkbox>>(
		this,
		object_ptr<Ui::Checkbox>(
			this,
			tr::lng_send_compressed(tr::now),
			true,
			st::defaultBoxCheckbox),
		st::editMediaCheckboxMargins)
	)->toggleOn(
		_isPhoto.value(
		) | rpl::map([=](bool value) {
			return value && (_albumType == Ui::AlbumType::None);
		}),
		anim::type::instant
	)->entity()->checkedChanges(
	) | rpl::start_with_next([&](bool checked) {
		_asFile = !checked;
	}, _controls->lifetime());

	_controls->resizeToWidth(st::sendMediaPreviewSize);
}

void EditCaptionBox::setupEditEventHandler() {
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
}

void EditCaptionBox::setupPhotoEditorEventHandler() {
	_photoEditorOpens.events(
	) | rpl::start_with_next([=, controller = _controller] {
		const auto previewWidth = st::sendMediaPreviewSize;
		if (!_preparedList.files.empty()) {
			Editor::OpenWithPreparedFile(
				this,
				controller,
				&_preparedList.files.front(),
				previewWidth,
				[=] { rebuildPreview(); });
		} else if (_photoMedia) {
			const auto large = _photoMedia->image(Data::PhotoSize::Large);
			if (!large) {
				return;
			}
			auto callback = [=](const Editor::PhotoModifications &mods) {
				if (!mods || !_photoMedia) {
					return;
				}
				const auto large = _photoMedia->image(Data::PhotoSize::Large);
				if (!large) {
					return;
				}
				auto copy = large->original();
				_preparedList = Storage::PrepareMediaFromImage(
					std::move(copy),
					QByteArray(),
					previewWidth);

				using ImageInfo = Ui::PreparedFileInformation::Image;
				auto &file = _preparedList.files.front();
				const auto image = std::get_if<ImageInfo>(
					&file.information->media);

				image->modifications = mods;
				Storage::UpdateImageDetails(file, previewWidth);
				rebuildPreview();
			};
			const auto fileImage = std::make_shared<Image>(*large);
			controller->showLayer(
				std::make_unique<Editor::LayerWidget>(
					this,
					&controller->window(),
					fileImage,
					Editor::PhotoModifications(),
					std::move(callback)),
				Ui::LayerOption::KeepOther);
		}
	}, lifetime());
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

	_emojiToggle->installEventFilter(_emojiPanel);
	_emojiToggle->addClickHandler([=] {
		_emojiPanel->toggleAnimated();
	});
}

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

bool EditCaptionBox::fileFromClipboard(not_null<const QMimeData*> data) {
	return setPreparedList(ListFromMimeData(data));
}

bool EditCaptionBox::setPreparedList(Ui::PreparedList &&list) {
	if (!_isAllowedEditMedia) {
		return false;
	}
	using Error = Ui::PreparedList::Error;
	if (list.error != Error::None || list.files.empty()) {
		return false;
	}
	auto file = &list.files.front();
	const auto invalidForAlbum = (_albumType != Ui::AlbumType::None)
		&& !file->canBeInAlbumType(_albumType);
	if (_albumType == Ui::AlbumType::PhotoVideo) {
		using Video = Ui::PreparedFileInformation::Video;
		if (const auto video = std::get_if<Video>(
				&file->information->media)) {
			video->isGifv = false;
		}
	}
	if (invalidForAlbum) {
		Ui::Toast::Show(tr::lng_edit_media_album_error(tr::now));
		return false;
	}
	_preparedList = std::move(list);
	rebuildPreview();
	return true;
}

void EditCaptionBox::captionResized() {
	updateBoxSize();
	resizeEvent(0);
	updateEmojiPanelGeometry();
	update();
}

void EditCaptionBox::updateBoxSize() {
	auto footerHeight = 0;
	if (_field) {
		footerHeight += st::boxPhotoCaptionSkip + _field->height();
	}
	if (_controls && !_controls->isHidden()) {
		footerHeight += _controls->heightNoMargins();
	}
	_footerHeight = footerHeight;
}

int EditCaptionBox::errorTopSkip() const {
	return (st::defaultBox.buttonPadding.top() / 2);
}

void EditCaptionBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	if (!_error.isEmpty()) {
		p.setFont(st::normalFont);
		p.setPen(st::boxTextFgError);
		p.drawTextLeft(
			_field->x(),
			_field->y() + _field->height() + errorTopSkip(),
			width(),
			_error);
	}

}

void EditCaptionBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	auto bottom = height();

	_field->resize(st::sendMediaPreviewSize, _field->height());
	_field->moveToLeft(
		st::boxPhotoPadding.left(),
		bottom - _field->height());
	bottom -= st::boxPhotoCaptionSkip + _field->height();

	_emojiToggle->moveToLeft(
		(st::boxPhotoPadding.left()
			+ st::sendMediaPreviewSize
			- _emojiToggle->width()),
		_field->y() + st::boxAttachEmojiTop);
	_emojiToggle->update();

	if (!_controls->isHidden()) {
		_controls->moveToLeft(
			st::boxPhotoPadding.left(),
			bottom - _controls->heightNoMargins());
		bottom -= _controls->heightNoMargins();
	}
	_scroll->resize(width(), bottom - st::boxPhotoPadding.top());
	_scroll->move(0, st::boxPhotoPadding.top());

	if (_content) {
		_content->resize(_scroll->width(), _content->height());
	}
}

void EditCaptionBox::setInnerFocus() {
	_field->setFocusFast();
}

void EditCaptionBox::save() {
	if (_saveRequestId) {
		return;
	}

	const auto item = _controller->session().data().message(
		_historyItem->fullId());
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

		if (Storage::ApplyModifications(_preparedList)) {
			_controller->session().settings().incrementPhotoEditorHintShown();
			_controller->session().saveSettings();
		}

		_controller->session().api().editMedia(
			std::move(_preparedList),
			(!_asFile && _isPhoto.current())
				? SendMediaType::Photo
				: SendMediaType::File,
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

void EditCaptionBox::keyPressEvent(QKeyEvent *e) {
	const auto ctrl = e->modifiers().testFlag(Qt::ControlModifier);
	if ((e->key() == Qt::Key_E) && ctrl) {
		_photoEditorOpens.fire({});
	} else if ((e->key() == Qt::Key_O) && ctrl) {
		_editMediaClicks.fire({});
	} else {
		e->ignore();
	}
}
