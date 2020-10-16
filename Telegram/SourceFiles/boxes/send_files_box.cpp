/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/send_files_box.h"

#include "platform/platform_specific.h"
#include "lang/lang_keys.h"
#include "storage/localstorage.h"
#include "storage/storage_media_prepare.h"
#include "mainwidget.h"
#include "main/main_session.h"
#include "mtproto/mtproto_config.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/send_context_menu.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "confirm_box.h"
#include "history/history_drag_area.h"
#include "history/view/history_view_schedule_box.h"
#include "core/file_utilities.h"
#include "core/mime_type.h"
#include "base/event_filter.h"
#include "ui/effects/animations.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/chat/attach/attach_send_files_way.h"
#include "ui/chat/attach/attach_album_preview.h"
#include "ui/chat/attach/attach_single_file_preview.h"
#include "ui/chat/attach/attach_single_media_preview.h"
#include "ui/text/format_values.h"
#include "ui/grouped_layout.h"
#include "ui/text/text_options.h"
#include "ui/controls/emoji_button.h"
#include "lottie/lottie_single_player.h"
#include "data/data_document.h"
#include "media/clip/media_clip_reader.h"
#include "api/api_common.h"
#include "window/window_session_controller.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "facades.h" // App::LambdaDelayed.
#include "app.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"

#include <QtCore/QMimeData>

namespace {

using Ui::SendFilesWay;

inline bool CanAddUrls(const QList<QUrl> &urls) {
	return !urls.isEmpty() && ranges::all_of(urls, &QUrl::isLocalFile);
}

inline bool IsSingleItem(const Ui::PreparedList &list) {
	return list.files.size() == 1;
}

void FileDialogCallback(
	FileDialog::OpenResult &&result,
	bool isAlbum,
	Fn<void(Ui::PreparedList)> callback) {
	auto showBoxErrorCallback = [](tr::phrase<> text) {
		Ui::show(Box<InformBox>(text(tr::now)), Ui::LayerOption::KeepOther);
	};

	auto list = Storage::PreparedFileFromFilesDialog(
		std::move(result),
		isAlbum,
		std::move(showBoxErrorCallback),
		st::sendMediaPreviewSize);

	if (!list) {
		return;
	}

	callback(std::move(*list));
}

rpl::producer<QString> FieldPlaceholder(
		const Ui::PreparedList &list,
		SendFilesWay way) {
	return list.canAddCaption(way.groupMediaInAlbums())
		? tr::lng_photo_caption()
		: tr::lng_photos_comment();
}

} // namespace

SendFilesBox::SendFilesBox(
	QWidget*,
	not_null<Window::SessionController*> controller,
	Ui::PreparedList &&list,
	const TextWithTags &caption,
	SendLimit limit,
	Api::SendType sendType,
	SendMenu::Type sendMenuType)
: _controller(controller)
, _sendType(sendType)
, _list(std::move(list))
, _sendLimit(limit)
, _sendMenuType(sendMenuType)
, _caption(
	this,
	st::confirmCaptionArea,
	Ui::InputField::Mode::MultiLine,
	nullptr,
	caption)
, _scroll(this, st::boxScroll)
, _inner(
	_scroll->setOwnedWidget(
		object_ptr<Ui::VerticalLayout>(_scroll.data()))) {
}

void SendFilesBox::initPreview() {
	using namespace rpl::mappers;

	setupControls();

	updateBoxSize();

	_dimensionsLifetime.destroy();
	_inner->resizeToWidth(st::boxWideWidth);

	rpl::combine(
		_inner->heightValue(),
		_footerHeight.value(),
		_titleHeight + _1 + _2
	) | rpl::start_with_next([=](int height) {
		setDimensions(
			st::boxWideWidth,
			std::min(st::sendMediaPreviewHeightMax, height),
			true);
	}, _dimensionsLifetime);
}

//void SendFilesBox::prepareSingleFilePreview() {
//	const auto &file = _list.files[0];
//	const auto controller = _controller;
//	const auto media = Ui::SingleMediaPreview::Create(this, [=] {
//		return controller->isGifPausedAtLeastFor(
//			Window::GifPauseReason::Layer);
//	}, file);
//	if (media) {
//		_preview = media;
//		initPreview(media->desiredHeightValue());
//	} else {
//		const auto preview = Ui::CreateChild<Ui::SingleFilePreview>(this, file);
//		_preview = preview;
//		initPreview(preview->desiredHeightValue());
//	}
//}

//void SendFilesBox::prepareAlbumPreview() {
//	addThumbButtonHandlers(wrap); // #TODO files
//
//	setupShadows(wrap, _albumPreview);
//
//	initPreview(_albumPreview->desiredHeightValue());
//
//	crl::on_main([=] {
//		wrap->scrollToY(_lastScrollTop);
//		_lastScrollTop = 0;
//	});
//}

void SendFilesBox::addThumbButtonHandlers(not_null<Ui::ScrollArea*> wrap) {
	// #TODO files
	//_albumPreview->thumbDeleted(
	//) | rpl::start_with_next([=](auto index) {
	//	_lastScrollTop = wrap->scrollTop();

	//	_list.files.erase(_list.files.begin() + index);
	//	applyAlbumOrder();

	//	if (_preview) {
	//		_preview->deleteLater();
	//	}
	//	_albumPreview = nullptr;

	//	refreshAllAfterAlbumChanges();
	//}, _albumPreview->lifetime());

	//_albumPreview->thumbChanged(
	//) | rpl::start_with_next([=](auto index) {
	//	_lastScrollTop = wrap->scrollTop();

	//	const auto callback = [=](FileDialog::OpenResult &&result) {
	//		FileDialogCallback(
	//			std::move(result),
	//			true,
	//			[=] (auto list) {
	//				_list.files[index] = std::move(list.files.front());
	//				applyAlbumOrder();

	//				if (_preview) {
	//					_preview->deleteLater();
	//				}
	//				_albumPreview = nullptr;

	//				refreshAllAfterAlbumChanges();
	//			});
	//	};

	//	FileDialog::GetOpenPath(
	//		this,
	//		tr::lng_choose_file(tr::now),
	//		FileDialog::AlbumFilesFilter(),
	//		crl::guard(this, callback));

	//}, _albumPreview->lifetime());
}

void SendFilesBox::setupShadows(
		not_null<Ui::ScrollArea*> wrap,
		not_null<Ui::AlbumPreview*> content) {
	using namespace rpl::mappers;

	const auto topShadow = Ui::CreateChild<Ui::FadeShadow>(this);
	const auto bottomShadow = Ui::CreateChild<Ui::FadeShadow>(this);
	wrap->geometryValue(
	) | rpl::start_with_next_done([=](const QRect &geometry) {
		topShadow->resizeToWidth(geometry.width());
		topShadow->move(
			geometry.x(),
			geometry.y());
		bottomShadow->resizeToWidth(geometry.width());
		bottomShadow->move(
			geometry.x(),
			geometry.y() + geometry.height() - st::lineWidth);
	}, [t = Ui::MakeWeak(topShadow), b = Ui::MakeWeak(bottomShadow)] {
		Ui::DestroyChild(t.data());
		Ui::DestroyChild(b.data());
	}, topShadow->lifetime());

	topShadow->toggleOn(wrap->scrollTopValue() | rpl::map(_1 > 0));
	bottomShadow->toggleOn(rpl::combine(
		wrap->scrollTopValue(),
		wrap->heightValue(),
		content->heightValue(),
		_1 + _2 < _3));
}

void SendFilesBox::prepare() {
	_send = addButton(tr::lng_send_button(), [=] { send({}); });
	if (_sendType == Api::SendType::Normal) {
		SendMenu::SetupMenuAndShortcuts(
			_send,
			[=] { return _sendMenuType; },
			[=] { sendSilent(); },
			[=] { sendScheduled(); });
	}
	addButton(tr::lng_cancel(), [=] { closeBox(); });
	initSendWay();
	setupCaption();
	setupSendWayControls();
	preparePreview();
	initPreview();

	_scroll->show();
	_inner->show();

	boxClosing() | rpl::start_with_next([=] {
		if (!_confirmed && _cancelledCallback) {
			_cancelledCallback();
		}
	}, lifetime());

	_addFile = addLeftButton(
		tr::lng_stickers_featured_add(),
		App::LambdaDelayed(st::historyAttach.ripple.hideDuration, this, [=] {
			openDialogToAddFileToAlbum();
		}));
	setupDragArea();
}

void SendFilesBox::setupDragArea() {
	// Avoid both drag areas appearing at one time.
	auto computeState = [=](const QMimeData *data) {
		using DragState = Storage::MimeDataState;
		const auto state = Storage::ComputeMimeDataState(data);
		return (state == DragState::PhotoFiles)
			? DragState::Image
			: (state == DragState::Files
				&& !Storage::ValidateDragData(data, true))
			? DragState::None
			: state;
	};
	const auto areas = DragArea::SetupDragAreaToContainer(
		this,
		[=](not_null<const QMimeData*> d) { return canAddFiles(d); },
		[=](bool f) { _caption->setAcceptDrops(f); },
		[=] { updateControlsGeometry(); },
		std::move(computeState));

	const auto droppedCallback = [=](bool compress) {
		return [=](const QMimeData *data) {
			addFiles(data);
			Window::ActivateWindow(_controller);
		};
	};
	areas.document->setDroppedCallback(droppedCallback(false));
	areas.photo->setDroppedCallback(droppedCallback(true));
	_albumChanged.events(
	) | rpl::start_with_next([=] {
		areas.document->raise();
		areas.photo->raise();
	}, lifetime());
}

void SendFilesBox::refreshAllAfterAlbumChanges() {
	preparePreview();
	captionResized();
	_albumChanged.fire({});
}

void SendFilesBox::openDialogToAddFileToAlbum() {
	const auto callback = [=](FileDialog::OpenResult &&result) {
		FileDialogCallback(
			std::move(result),
			true,
			[=] (auto list) {
				addFiles(std::move(list));
			});
	};

	FileDialog::GetOpenPaths(
		this,
		tr::lng_choose_file(tr::now),
		FileDialog::AlbumFilesFilter(),
		crl::guard(this, callback));
}

void SendFilesBox::initSendWay() {
	_sendWay = [&] {
		auto result = Core::App().settings().sendFilesWay();
		if (_sendLimit == SendLimit::One) {
			result.setGroupMediaInAlbums(true);
			result.setGroupFiles(true);
			return result;
		} else if (_list.overrideSendImagesAsPhotos == false) {
			result.setSendImagesAsPhotos(false);
			return result;
		} else if (_list.overrideSendImagesAsPhotos == true) {
			result.setSendImagesAsPhotos(true);
			return result;
		}
		return result;
	}();
	_sendWay.changes(
	) | rpl::start_with_next([=](SendFilesWay value) {
		updateCaptionPlaceholder();
		updateEmojiPanelGeometry();
		setInnerFocus();
	}, lifetime());
}

void SendFilesBox::updateCaptionPlaceholder() {
	if (!_caption) {
		return;
	}
	const auto sendWay = _sendWay.current();
	if (!_list.canAddCaption(sendWay.groupMediaInAlbums())
		&& _sendLimit == SendLimit::One) {
		_caption->hide();
		if (_emojiToggle) {
			_emojiToggle->hide();
		}
	} else {
		_caption->setPlaceholder(FieldPlaceholder(_list, sendWay));
		_caption->show();
		if (_emojiToggle) {
			_emojiToggle->show();
		}
	}
}

void SendFilesBox::preparePreview() {
	using Type = Ui::PreparedFile::AlbumType;

	_blocks.clear();
	auto albumStart = -1;
	const auto finishAlbum = [&](int till) {
		if (albumStart < 0) {
			return;
		}
		const auto count = (till - albumStart);
		const auto preview = _inner->add(object_ptr<Ui::AlbumPreview>(
			this,
			gsl::make_span(_list.files).subspan(albumStart, count),
			_sendWay.current()));

		auto &block = _blocks.emplace_back();
		block.fromIndex = albumStart;
		block.tillIndex = albumStart + count;
		block.preview.reset(preview);
		block.preview->show();

		_sendWay.changes(
		) | rpl::start_with_next([=](SendFilesWay value) {
			applyAlbumOrder(preview, albumStart);
			preview->setSendWay(value);
		}, preview->lifetime());

		albumStart = -1;
	};
	const auto finishSingle = [&](int index) {
		const auto &file = _list.files[index];
		const auto controller = _controller;
		auto &block = _blocks.emplace_back();
		block.fromIndex = index;
		block.tillIndex = index + 1;
		const auto media = Ui::SingleMediaPreview::Create(this, [=] {
			return controller->isGifPausedAtLeastFor(
				Window::GifPauseReason::Layer);
		}, file);
		if (media) {
			block.preview.reset(
				_inner->add(object_ptr<Ui::SingleMediaPreview>::fromRaw(
					media)));
		} else {
			block.preview.reset(
				_inner->add(object_ptr<Ui::SingleFilePreview>(this, file)));
		}
		block.preview->show();
	};
	for (auto i = 0, count = int(_list.files.size()); i != count; ++i) {
		const auto type = _list.files[i].type;
		if (albumStart >= 0) {
			const auto albumCount = (i - albumStart);
			if (type == Type::File || (albumCount == Ui::MaxAlbumItems())) {
				finishAlbum(i);
			} else {
				continue;
			}
		}
		if (type != Type::File) {
			if (albumStart < 0) {
				albumStart = i;
			}
			continue;
		}
		finishSingle(i);
	}
	finishAlbum(_list.files.size());
}

void SendFilesBox::setupControls() {
	setupTitleText();
	updateSendWayControlsVisibility();
}

void SendFilesBox::setupSendWayControls() {
	// #TODO files
	_groupMediaInAlbums.create(
		this,
		"Group media in albums",
		_sendWay.current().groupMediaInAlbums(),
		st::defaultBoxCheckbox);
	_sendImagesAsPhotos.create(
		this,
		"Send images as photos",
		_sendWay.current().sendImagesAsPhotos(),
		st::defaultBoxCheckbox);
	_groupFiles.create(
		this,
		"Group files",
		_sendWay.current().groupFiles(),
		st::defaultBoxCheckbox);

	_sendWay.changes(
	) | rpl::start_with_next([=](SendFilesWay value) {
		_groupMediaInAlbums->setChecked(value.groupMediaInAlbums());
		_sendImagesAsPhotos->setChecked(value.sendImagesAsPhotos());
		_groupFiles->setChecked(value.groupFiles());
	}, lifetime());

	_groupMediaInAlbums->checkedChanges(
	) | rpl::start_with_next([=] {
		auto sendWay = _sendWay.current();
		sendWay.setGroupMediaInAlbums(_groupMediaInAlbums->checked());
		_sendWay = sendWay;
	}, lifetime());

	_sendImagesAsPhotos->checkedChanges(
	) | rpl::start_with_next([=] {
		auto sendWay = _sendWay.current();
		sendWay.setSendImagesAsPhotos(_sendImagesAsPhotos->checked());
		_sendWay = sendWay;
	}, lifetime());

	_groupFiles->checkedChanges(
	) | rpl::start_with_next([=] {
		auto sendWay = _sendWay.current();
		sendWay.setGroupFiles(_groupFiles->checked());
		_sendWay = sendWay;
	}, lifetime());
}

void SendFilesBox::updateSendWayControlsVisibility() {
	if (_sendLimit == SendLimit::One) {
		return;
	}
	const auto onlyOne = (_sendLimit == SendLimit::One);
	_groupMediaInAlbums->setVisible(!onlyOne);
	_sendImagesAsPhotos->setVisible(/*_list.hasImagesForCompression()*/true); // #TODO files
	_groupFiles->setVisible(!onlyOne);
}

void SendFilesBox::applyAlbumOrder(
		not_null<Ui::AlbumPreview*> preview,
		int from) {
	const auto order = preview->takeOrder();
	const auto isDefault = [&] {
		for (auto i = 0, count = int(order.size()); i != count; ++i) {
			if (order[i] != i) {
				return false;
			}
		}
		return true;
	}();
	if (isDefault) {
		return;
	}

	auto elements = std::vector<Ui::PreparedFile>();
	elements.reserve(order.size());
	for (const auto index : order) {
		elements.push_back(std::move(_list.files[from + index]));
	}
	for (auto i = 0, count = int(order.size()); i != count; ++i) {
		_list.files[from + i] = std::move(elements[i]);
	}
}

void SendFilesBox::setupCaption() {
	_caption->setMaxLength(
		_controller->session().serverConfig().captionLengthMax);
	_caption->setSubmitSettings(
		Core::App().settings().sendSubmitWay());
	connect(_caption, &Ui::InputField::resized, [=] {
		captionResized();
	});
	connect(_caption, &Ui::InputField::submitted, [=](
			Qt::KeyboardModifiers modifiers) {
		const auto ctrlShiftEnter = modifiers.testFlag(Qt::ShiftModifier)
			&& (modifiers.testFlag(Qt::ControlModifier)
				|| modifiers.testFlag(Qt::MetaModifier));
		send({}, ctrlShiftEnter);
	});
	connect(_caption, &Ui::InputField::cancelled, [=] { closeBox(); });
	_caption->setMimeDataHook([=](
			not_null<const QMimeData*> data,
			Ui::InputField::MimeAction action) {
		if (action == Ui::InputField::MimeAction::Check) {
			return canAddFiles(data);
		} else if (action == Ui::InputField::MimeAction::Insert) {
			return addFiles(data);
		}
		Unexpected("action in MimeData hook.");
	});
	_caption->setInstantReplaces(Ui::InstantReplaces::Default());
	_caption->setInstantReplacesEnabled(
		Core::App().settings().replaceEmojiValue());
	_caption->setMarkdownReplacesEnabled(rpl::single(true));
	_caption->setEditLinkCallback(
		DefaultEditLinkCallback(_controller, _caption));
	Ui::Emoji::SuggestionsController::Init(
		getDelegate()->outerContainer(),
		_caption,
		&_controller->session());

	InitSpellchecker(_controller, _caption);

	updateCaptionPlaceholder();
	setupEmojiPanel();
}

void SendFilesBox::setupEmojiPanel() {
	Expects(_caption != nullptr);

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
		Ui::InsertEmojiAtCursor(_caption->textCursor(), emoji);
	}, lifetime());

	const auto filterCallback = [=](not_null<QEvent*> event) {
		emojiFilterForGeometry(event);
		return base::EventFilterResult::Continue;
	};
	_emojiFilter.reset(base::install_event_filter(container, filterCallback));

	_emojiToggle.create(this, st::boxAttachEmoji);
	_emojiToggle->setVisible(!_caption->isHidden());
	_emojiToggle->installEventFilter(_emojiPanel);
	_emojiToggle->addClickHandler([=] {
		_emojiPanel->toggleAnimated();
	});
}

void SendFilesBox::emojiFilterForGeometry(not_null<QEvent*> event) {
	const auto type = event->type();
	if (type == QEvent::Move || type == QEvent::Resize) {
		// updateEmojiPanelGeometry uses not only container geometry, but
		// also container children geometries that will be updated later.
		crl::on_main(this, [=] { updateEmojiPanelGeometry(); });
	}
}

void SendFilesBox::updateEmojiPanelGeometry() {
	const auto parent = _emojiPanel->parentWidget();
	const auto global = _emojiToggle->mapToGlobal({ 0, 0 });
	const auto local = parent->mapFromGlobal(global);
	_emojiPanel->moveBottomRight(
		local.y(),
		local.x() + _emojiToggle->width() * 3);
}

void SendFilesBox::captionResized() {
	updateBoxSize();
	updateControlsGeometry();
	updateEmojiPanelGeometry();
	update();
}

bool SendFilesBox::canAddFiles(not_null<const QMimeData*> data) const {
	return (data->hasUrls() && CanAddUrls(data->urls())) || data->hasImage();
	//const auto urls = data->hasUrls() ? data->urls() : QList<QUrl>();
	//auto filesCount = CanAddUrls(urls) ? urls.size() : 0;
	//if (!filesCount && data->hasImage()) {
	//	++filesCount;
	//}

	//if (_list.files.size() + filesCount > Ui::MaxAlbumItems()) { // #TODO files
	//	return false;
	//} else if (_list.files.size() > 1 && !_albumPreview) {
	//	return false;
	//} else if (!IsFirstAlbumItem(_list)) {
	//	return false;
	//}
	//return true;
}

bool SendFilesBox::addFiles(not_null<const QMimeData*> data) {
	auto list = [&] {
		const auto urls = data->hasUrls() ? data->urls() : QList<QUrl>();
		auto result = CanAddUrls(urls)
			? Storage::PrepareMediaList(urls, st::sendMediaPreviewSize)
			: Ui::PreparedList(
				Ui::PreparedList::Error::EmptyFile,
				QString());
		if (result.error == Ui::PreparedList::Error::None) {
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
	}();
	return addFiles(std::move(list));
}

bool SendFilesBox::addFiles(Ui::PreparedList list) {
	const auto sumFiles = _list.files.size() + list.files.size();
	const auto cutToAlbumSize = (sumFiles > Ui::MaxAlbumItems());
	if (list.error != Ui::PreparedList::Error::None) {
		return false;
	//} else if (!IsSingleItem(list) && !list.albumIsPossible) { // #TODO files
	//	return false;
	//} else if (!IsFirstAlbumItem(list)) {
	//	return false;
	//} else if (_list.files.size() > 1 && !_albumPreview) {
	//	return false;
	//} else if (!IsFirstAlbumItem(_list)) {
	//	return false;
	}
	//applyAlbumOrder();
	//delete base::take(_preview);
	//_albumPreview = nullptr;
	return false;
	_list.mergeToEnd(std::move(list), cutToAlbumSize);

	refreshAllAfterAlbumChanges();
	return true;
}

void SendFilesBox::setupTitleText() {
	if (_list.files.size() > 1) {
		const auto onlyImages = false;/* #TODO files (_compressConfirm != CompressConfirm::None)
			&& (_albumVideosCount == 0);*/
		_titleText = onlyImages
			? tr::lng_send_images_selected(tr::now, lt_count, _list.files.size())
			: tr::lng_send_files_selected(tr::now, lt_count, _list.files.size());
		_titleHeight = st::boxTitleHeight;
	} else {
		_titleText = QString();
		_titleHeight = 0;
	}
}

void SendFilesBox::updateBoxSize() {
	auto footerHeight = 0;
	if (_caption) {
		footerHeight += st::boxPhotoCaptionSkip + _caption->height();
	}
	const auto pointers = {
		_groupMediaInAlbums.data(),
		_sendImagesAsPhotos.data(),
		_groupFiles.data()
	};
	for (auto pointer : pointers) {
		if (pointer && !pointer->isHidden()) {
			footerHeight += st::boxPhotoCompressedSkip
				+ pointer->heightNoMargins();
		}
	}
	_footerHeight = footerHeight;
}

void SendFilesBox::keyPressEvent(QKeyEvent *e) {
	if (e->matches(QKeySequence::Open)) {
		openDialogToAddFileToAlbum();
	} else if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		const auto modifiers = e->modifiers();
		const auto ctrl = modifiers.testFlag(Qt::ControlModifier)
			|| modifiers.testFlag(Qt::MetaModifier);
		const auto shift = modifiers.testFlag(Qt::ShiftModifier);
		send({}, ctrl && shift);
	} else {
		BoxContent::keyPressEvent(e);
	}
}

void SendFilesBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	if (!_titleText.isEmpty()) {
		Painter p(this);

		p.setFont(st::boxPhotoTitleFont);
		p.setPen(st::boxTitleFg);
		p.drawTextLeft(
			st::boxPhotoTitlePosition.x(),
			st::boxPhotoTitlePosition.y(),
			width(),
			_titleText);
	}
}

void SendFilesBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);
	updateControlsGeometry();
}

void SendFilesBox::updateControlsGeometry() {
	auto bottom = height();
	if (_caption) {
		_caption->resize(st::sendMediaPreviewSize, _caption->height());
		_caption->moveToLeft(
			st::boxPhotoPadding.left(),
			bottom - _caption->height());
		bottom -= st::boxPhotoCaptionSkip + _caption->height();

		if (_emojiToggle) {
			_emojiToggle->moveToLeft(
				(st::boxPhotoPadding.left()
					+ st::sendMediaPreviewSize
					- _emojiToggle->width()),
				_caption->y() + st::boxAttachEmojiTop);
		}
	}
	const auto pointers = {
		_groupMediaInAlbums.data(),
		_sendImagesAsPhotos.data(),
		_groupFiles.data()
	};
	for (const auto pointer : ranges::view::reverse(pointers)) {
		if (pointer && !pointer->isHidden()) {
			pointer->moveToLeft(
				st::boxPhotoPadding.left(),
				bottom - pointer->heightNoMargins());
			bottom -= st::boxPhotoCompressedSkip + pointer->heightNoMargins();
		}
	}
	_scroll->resize(width(), bottom - _titleHeight);
	_scroll->move(0, _titleHeight);
}

void SendFilesBox::setInnerFocus() {
	if (!_caption || _caption->isHidden()) {
		setFocus();
	} else {
		_caption->setFocusFast();
	}
}

void SendFilesBox::send(
		Api::SendOptions options,
		bool ctrlShiftEnter) {
	if ((_sendType == Api::SendType::Scheduled
		|| _sendType == Api::SendType::ScheduledToUser)
		&& !options.scheduled) {
		return sendScheduled();
	}

	auto way = _sendWay.current();
	auto oldWay = Core::App().settings().sendFilesWay();
	if (_list.overrideSendImagesAsPhotos == way.sendImagesAsPhotos()
		|| _sendImagesAsPhotos->isHidden()) {
		way.setSendImagesAsPhotos(oldWay.sendImagesAsPhotos());
	}
	if (_groupMediaInAlbums->isHidden()) {
		way.setGroupMediaInAlbums(oldWay.groupMediaInAlbums());
	}
	if (_groupFiles->isHidden()) {
		way.setGroupFiles(oldWay.groupFiles());
	}
	if (way != oldWay) {
		Core::App().settings().setSendFilesWay(way);
		Core::App().saveSettingsDelayed();
	}

	//applyAlbumOrder(); // #TODO files
	_confirmed = true;
	if (_confirmedCallback) {
		auto caption = (_caption && !_caption->isHidden())
			? _caption->getTextWithAppliedMarkdown()
			: TextWithTags();
		_confirmedCallback(
			std::move(_list),
			way,
			std::move(caption),
			options,
			ctrlShiftEnter);
	}
	closeBox();
}

void SendFilesBox::sendSilent() {
	auto options = Api::SendOptions();
	options.silent = true;
	send(options);
}

void SendFilesBox::sendScheduled() {
	const auto type = (_sendType == Api::SendType::ScheduledToUser)
		? SendMenu::Type::ScheduledToUser
		: _sendMenuType;
	const auto callback = [=](Api::SendOptions options) { send(options); };
	Ui::show(
		HistoryView::PrepareScheduleBox(this, type, callback),
		Ui::LayerOption::KeepOther);
}

SendFilesBox::~SendFilesBox() = default;
