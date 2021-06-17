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
#include "ui/toast/toast.h"
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
		Fn<bool(const Ui::PreparedList&)> checkResult,
		Fn<void(Ui::PreparedList)> callback) {
	auto showError = [](tr::phrase<> text) {
		Ui::Toast::Show(text(tr::now));
	};

	auto list = Storage::PreparedFileFromFilesDialog(
		std::move(result),
		checkResult,
		showError,
		st::sendMediaPreviewSize);

	if (!list) {
		return;
	}

	callback(std::move(*list));
}

rpl::producer<QString> FieldPlaceholder(
		const Ui::PreparedList &list,
		SendFilesWay way) {
	return list.canAddCaption(way.groupFiles() && way.sendImagesAsPhotos())
		? tr::lng_photo_caption()
		: tr::lng_photos_comment();
}

} // namespace

SendFilesBox::Block::Block(
	not_null<QWidget*> parent,
	not_null<std::vector<Ui::PreparedFile>*> items,
	int from,
	int till,
	Fn<bool()> gifPaused,
	SendFilesWay way)
: _items(items)
, _from(from)
, _till(till) {
	Expects(from >= 0);
	Expects(till > from);
	Expects(till <= items->size());

	const auto count = till - from;
	const auto my = gsl::make_span(*items).subspan(from, count);
	const auto &first = my.front();
	_isAlbum = (my.size() > 1);
	if (_isAlbum) {
		const auto preview = Ui::CreateChild<Ui::AlbumPreview>(
			parent.get(),
			my,
			way);
		_preview.reset(preview);
	} else {
		const auto media = Ui::SingleMediaPreview::Create(
			parent,
			gifPaused,
			first);
		if (media) {
			_isSingleMedia = true;
			_preview.reset(media);
		} else {
			_preview.reset(
				Ui::CreateChild<Ui::SingleFilePreview>(parent.get(), first));
		}
	}
	_preview->show();
}

int SendFilesBox::Block::fromIndex() const {
	return _from;
}

int SendFilesBox::Block::tillIndex() const {
	return _till;
}

object_ptr<Ui::RpWidget> SendFilesBox::Block::takeWidget() {
	return object_ptr<Ui::RpWidget>::fromRaw(_preview.get());
}

rpl::producer<int> SendFilesBox::Block::itemDeleteRequest() const {
	using namespace rpl::mappers;

	const auto preview = _preview.get();
	const auto from = _from;
	if (_isAlbum) {
		const auto album = static_cast<Ui::AlbumPreview*>(_preview.get());
		return album->thumbDeleted() | rpl::map(_1 + from);
	} else if (_isSingleMedia) {
		const auto media = static_cast<Ui::SingleMediaPreview*>(preview);
		return media->deleteRequests() | rpl::map([from] { return from; });
	} else {
		const auto single = static_cast<Ui::SingleFilePreview*>(preview);
		return single->deleteRequests() | rpl::map([from] { return from; });
	}
}

rpl::producer<int> SendFilesBox::Block::itemReplaceRequest() const {
	using namespace rpl::mappers;

	const auto preview = _preview.get();
	const auto from = _from;
	if (_isAlbum) {
		const auto album = static_cast<Ui::AlbumPreview*>(preview);
		return album->thumbChanged() | rpl::map(_1 + from);
	} else if (_isSingleMedia) {
		const auto media = static_cast<Ui::SingleMediaPreview*>(preview);
		return media->editRequests() | rpl::map([from] { return from; });
	} else {
		const auto single = static_cast<Ui::SingleFilePreview*>(preview);
		return single->editRequests() | rpl::map([from] { return from; });
	}
}

void SendFilesBox::Block::setSendWay(Ui::SendFilesWay way) {
	if (!_isAlbum) {
		return;
	}
	applyAlbumOrder();
	const auto album = static_cast<Ui::AlbumPreview*>(_preview.get());
	album->setSendWay(way);
}

void SendFilesBox::Block::applyAlbumOrder() {
	if (!_isAlbum) {
		return;
	}
	const auto album = static_cast<Ui::AlbumPreview*>(_preview.get());
	const auto order = album->takeOrder();
	const auto isIdentity = [&] {
		for (auto i = 0, count = int(order.size()); i != count; ++i) {
			if (order[i] != i) {
				return false;
			}
		}
		return true;
	}();
	if (isIdentity) {
		return;
	}

	auto elements = std::vector<Ui::PreparedFile>();
	elements.reserve(order.size());
	for (const auto index : order) {
		elements.push_back(std::move((*_items)[_from + index]));
	}
	for (auto i = 0, count = int(order.size()); i != count; ++i) {
		(*_items)[_from + i] = std::move(elements[i]);
	}
}

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
, _titleHeight(st::boxTitleHeight)
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
	enqueueNextPrepare();
}

void SendFilesBox::initPreview() {
	using namespace rpl::mappers;

	refreshControls();

	updateBoxSize();

	_dimensionsLifetime.destroy();
	_inner->resizeToWidth(st::boxWideWidth);

	rpl::combine(
		_inner->heightValue(),
		_footerHeight.value(),
		_titleHeight.value(),
		_1 + _2 + _3
	) | rpl::start_with_next([=](int height) {
		setDimensions(
			st::boxWideWidth,
			std::min(st::sendMediaPreviewHeightMax, height),
			true);
	}, _dimensionsLifetime);
}

void SendFilesBox::enqueueNextPrepare() {
	if (_preparing) {
		return;
	}
	while (!_list.filesToProcess.empty()
		&& _list.filesToProcess.front().information) {
		auto file = std::move(_list.filesToProcess.front());
		_list.filesToProcess.pop_front();
		addFile(std::move(file));
	}
	if (_list.filesToProcess.empty()) {
		return;
	}
	auto file = std::move(_list.filesToProcess.front());
	_list.filesToProcess.pop_front();
	const auto weak = Ui::MakeWeak(this);
	_preparing = true;
	crl::async([weak, file = std::move(file)]() mutable {
		Storage::PrepareDetails(file, st::sendMediaPreviewSize);
		crl::on_main([weak, file = std::move(file)]() mutable {
			if (weak) {
				weak->addPreparedAsyncFile(std::move(file));
			}
		});
	});
}

void SendFilesBox::setupShadows() {
	using namespace rpl::mappers;

	const auto topShadow = Ui::CreateChild<Ui::FadeShadow>(this);
	const auto bottomShadow = Ui::CreateChild<Ui::FadeShadow>(this);
	_scroll->geometryValue(
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

	topShadow->toggleOn(_scroll->scrollTopValue() | rpl::map(_1 > 0));
	bottomShadow->toggleOn(rpl::combine(
		_scroll->scrollTopValue(),
		_scroll->heightValue(),
		_inner->heightValue(),
		_1 + _2 < _3));
}

void SendFilesBox::prepare() {
	_send = addButton(
		(_sendType == Api::SendType::Normal
			? tr::lng_send_button()
			: tr::lng_create_group_next()),
		[=] { send({}); });
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
	setupShadows();

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
}

void SendFilesBox::refreshAllAfterChanges(int fromItem) {
	auto fromBlock = 0;
	for (auto count = int(_blocks.size()); fromBlock != count; ++fromBlock) {
		if (_blocks[fromBlock].tillIndex() >= fromItem) {
			break;
		}
	}
	generatePreviewFrom(fromBlock);
	_inner->resizeToWidth(st::boxWideWidth);
	refreshControls();
	captionResized();
}

void SendFilesBox::openDialogToAddFileToAlbum() {
	const auto checkResult = [=](const Ui::PreparedList &list) {
		if (_sendLimit != SendLimit::One) {
			return true;
		} else if (!_list.canBeSentInSlowmodeWith(list)) {
			Ui::Toast::Show(tr::lng_slowmode_no_many(tr::now));
			return false;
		}
		return true;
	};
	const auto callback = [=](FileDialog::OpenResult &&result) {
		FileDialogCallback(
			std::move(result),
			checkResult,
			[=](Ui::PreparedList list) { addFiles(std::move(list)); });
	};

	FileDialog::GetOpenPaths(
		this,
		tr::lng_choose_file(tr::now),
		FileDialog::AllOrImagesFilter(),
		crl::guard(this, callback));
}

void SendFilesBox::initSendWay() {
	_sendWay = [&] {
		auto result = Core::App().settings().sendFilesWay();
		if (_sendLimit == SendLimit::One) {
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
		for (auto &block : _blocks) {
			block.setSendWay(value);
		}
		setInnerFocus();
	}, lifetime());
}

void SendFilesBox::updateCaptionPlaceholder() {
	if (!_caption) {
		return;
	}
	const auto way = _sendWay.current();
	if (!_list.canAddCaption(way.groupFiles() && way.sendImagesAsPhotos())
		&& _sendLimit == SendLimit::One) {
		_caption->hide();
		if (_emojiToggle) {
			_emojiToggle->hide();
		}
	} else {
		_caption->setPlaceholder(FieldPlaceholder(_list, way));
		_caption->show();
		if (_emojiToggle) {
			_emojiToggle->show();
		}
	}
}

void SendFilesBox::preparePreview() {
	generatePreviewFrom(0);
}

void SendFilesBox::generatePreviewFrom(int fromBlock) {
	Expects(fromBlock <= _blocks.size());

	using Type = Ui::PreparedFile::Type;

	const auto eraseFrom = _blocks.begin() + fromBlock;
	for (auto i = eraseFrom; i != _blocks.end(); ++i) {
		i->applyAlbumOrder();
	}
	_blocks.erase(eraseFrom, _blocks.end());

	const auto fromItem = _blocks.empty() ? 0 : _blocks.back().tillIndex();
	Assert(fromItem <= _list.files.size());

	auto albumStart = -1;
	for (auto i = fromItem, till = int(_list.files.size()); i != till; ++i) {
		const auto type = _list.files[i].type;
		if (albumStart >= 0) {
			const auto albumCount = (i - albumStart);
			if ((type == Type::File)
				|| (type == Type::None)
				|| (type == Type::Music)
				|| (albumCount == Ui::MaxAlbumItems())) {
				pushBlock(std::exchange(albumStart, -1), i);
			} else {
				continue;
			}
		}
		if (type != Type::File
			&& type != Type::Music
			&& type != Type::None) {
			if (albumStart < 0) {
				albumStart = i;
			}
			continue;
		}
		pushBlock(i, i + 1);
	}
	if (albumStart >= 0) {
		pushBlock(albumStart, _list.files.size());
	}
}

void SendFilesBox::pushBlock(int from, int till) {
	const auto gifPaused = [controller = _controller] {
		return controller->isGifPausedAtLeastFor(
			Window::GifPauseReason::Layer);
	};
	const auto index = int(_blocks.size());
	_blocks.emplace_back(
		_inner.data(),
		&_list.files,
		from,
		till,
		gifPaused,
		_sendWay.current());
	auto &block = _blocks.back();
	const auto widget = _inner->add(
		block.takeWidget(),
		QMargins(0, _inner->count() ? st::sendMediaRowSkip : 0, 0, 0));

	const auto preventDelete =
		widget->lifetime().make_state<rpl::event_stream<int>>();

	block.itemDeleteRequest(
	) | rpl::filter([=] {
		return !_removingIndex;
	}) | rpl::start_with_next([=](int index) {
		_removingIndex = index;
		crl::on_main(this, [=] {
			const auto index = base::take(_removingIndex).value_or(-1);
			if (index < 0 || index >= _list.files.size()) {
				return;
			}
			// Prevent item delete if it is the only one.
			if (_list.files.size() == 1) {
				preventDelete->fire_copy(0);
				return;
			}
			_list.files.erase(_list.files.begin() + index);
			refreshAllAfterChanges(from);
		});
	}, widget->lifetime());

	rpl::merge(
		block.itemReplaceRequest(),
		preventDelete->events()
	) | rpl::start_with_next([=](int index) {
		const auto replace = [=](Ui::PreparedList list) {
			if (list.files.empty()) {
				return;
			}
			_list.files[index] = std::move(list.files.front());
			refreshAllAfterChanges(from);
		};
		const auto checkResult = [=](const Ui::PreparedList &list) {
			if (_sendLimit != SendLimit::One) {
				return true;
			}
			auto removing = std::move(_list.files[index]);
			std::swap(_list.files[index], _list.files.back());
			_list.files.pop_back();
			const auto result = _list.canBeSentInSlowmodeWith(list);
			_list.files.push_back(std::move(removing));
			std::swap(_list.files[index], _list.files.back());
			if (!result) {
				Ui::Toast::Show(tr::lng_slowmode_no_many(tr::now));
				return false;
			}
			return true;
		};
		const auto callback = [=](FileDialog::OpenResult &&result) {
			FileDialogCallback(
				std::move(result),
				checkResult,
				replace);
		};

		FileDialog::GetOpenPath(
			this,
			tr::lng_choose_file(tr::now),
			FileDialog::AllOrImagesFilter(),
			crl::guard(this, callback));
	}, widget->lifetime());
}

void SendFilesBox::refreshControls() {
	refreshTitleText();
	updateSendWayControlsVisibility();
	updateCaptionPlaceholder();
}

void SendFilesBox::setupSendWayControls() {
	_groupFiles.create(
		this,
		tr::lng_send_grouped(tr::now),
		_sendWay.current().groupFiles(),
		st::defaultBoxCheckbox);
	_sendImagesAsPhotos.create(
		this,
		tr::lng_send_compressed(tr::now),
		_sendWay.current().sendImagesAsPhotos(),
		st::defaultBoxCheckbox);

	_sendWay.changes(
	) | rpl::start_with_next([=](SendFilesWay value) {
		_groupFiles->setChecked(value.groupFiles());
		_sendImagesAsPhotos->setChecked(value.sendImagesAsPhotos());
	}, lifetime());

	_groupFiles->checkedChanges(
	) | rpl::start_with_next([=] {
		auto sendWay = _sendWay.current();
		sendWay.setGroupFiles(_groupFiles->checked());
		_sendWay = sendWay;
	}, lifetime());

	_sendImagesAsPhotos->checkedChanges(
	) | rpl::start_with_next([=] {
		auto sendWay = _sendWay.current();
		sendWay.setSendImagesAsPhotos(_sendImagesAsPhotos->checked());
		_sendWay = sendWay;
	}, lifetime());
}

void SendFilesBox::updateSendWayControlsVisibility() {
	const auto onlyOne = (_sendLimit == SendLimit::One);
	_groupFiles->setVisible(_list.hasGroupOption(onlyOne));
	_sendImagesAsPhotos->setVisible(
		_list.hasSendImagesAsPhotosOption(onlyOne));
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
	if (list.error != Ui::PreparedList::Error::None) {
		return false;
	}
	const auto count = int(_list.files.size());
	_list.filesToProcess.insert(
		_list.filesToProcess.end(),
		std::make_move_iterator(list.files.begin()),
		std::make_move_iterator(list.files.end()));
	_list.filesToProcess.insert(
		_list.filesToProcess.end(),
		std::make_move_iterator(list.filesToProcess.begin()),
		std::make_move_iterator(list.filesToProcess.end()));
	enqueueNextPrepare();
	if (_list.files.size() > count) {
		refreshAllAfterChanges(count);
	}
	return true;
}

void SendFilesBox::addPreparedAsyncFile(Ui::PreparedFile &&file) {
	Expects(file.information != nullptr);

	_preparing = false;
	const auto count = int(_list.files.size());
	addFile(std::move(file));
	enqueueNextPrepare();
	if (_list.files.size() > count) {
		refreshAllAfterChanges(count);
	}
	if (!_preparing && _whenReadySend) {
		_whenReadySend();
	}
}

void SendFilesBox::addFile(Ui::PreparedFile &&file) {
	// canBeSentInSlowmode checks for non empty filesToProcess.
	auto saved = base::take(_list.filesToProcess);
	_list.files.push_back(std::move(file));
	if (_sendLimit == SendLimit::One && !_list.canBeSentInSlowmode()) {
		_list.files.pop_back();
	}
	_list.filesToProcess = std::move(saved);
}

void SendFilesBox::refreshTitleText() {
	const auto count = int(_list.files.size());
	if (count > 1) {
		const auto imagesCount = ranges::count(
			_list.files,
			Ui::PreparedFile::Type::Photo,
			&Ui::PreparedFile::type);
		_titleText = (imagesCount == count)
			? tr::lng_send_images_selected(tr::now, lt_count, count)
			: tr::lng_send_files_selected(tr::now, lt_count, count);
		_titleHeight = st::boxTitleHeight;
	} else {
		_titleText = QString();
		_titleHeight = count ? st::boxPhotoPadding.top() : 0;
	}
}

void SendFilesBox::updateBoxSize() {
	auto footerHeight = 0;
	if (_caption) {
		footerHeight += st::boxPhotoCaptionSkip + _caption->height();
	}
	const auto pointers = {
		_groupFiles.data(),
		_sendImagesAsPhotos.data(),
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

		p.setFont(st::boxTitleFont);
		p.setPen(st::boxTitleFg);
		p.drawTextLeft(
			st::boxPhotoTitlePosition.x(),
			st::boxTitlePosition.y(),
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
			_emojiToggle->update();
		}
	}
	const auto pointers = {
		_groupFiles.data(),
		_sendImagesAsPhotos.data(),
	};
	for (const auto pointer : ranges::views::reverse(pointers)) {
		if (pointer && !pointer->isHidden()) {
			pointer->moveToLeft(
				st::boxPhotoPadding.left(),
				bottom - pointer->heightNoMargins());
			bottom -= st::boxPhotoCompressedSkip + pointer->heightNoMargins();
		}
	}
	_scroll->resize(width(), bottom - _titleHeight.current());
	_scroll->move(0, _titleHeight.current());
}

void SendFilesBox::setInnerFocus() {
	if (!_caption || _caption->isHidden()) {
		setFocus();
	} else {
		_caption->setFocusFast();
	}
}

void SendFilesBox::saveSendWaySettings() {
	auto way = _sendWay.current();
	auto oldWay = Core::App().settings().sendFilesWay();
	if (_groupFiles->isHidden()) {
		way.setGroupFiles(oldWay.groupFiles());
	}
	if (_list.overrideSendImagesAsPhotos == way.sendImagesAsPhotos()
		|| _sendImagesAsPhotos->isHidden()) {
		way.setSendImagesAsPhotos(oldWay.sendImagesAsPhotos());
	}
	if (way != oldWay) {
		Core::App().settings().setSendFilesWay(way);
		Core::App().saveSettingsDelayed();
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
	if (_preparing) {
		_whenReadySend = [=] {
			send(options, ctrlShiftEnter);
		};
		return;
	}

	saveSendWaySettings();

	for (auto &block : _blocks) {
		block.applyAlbumOrder();
	}
	_confirmed = true;
	if (_confirmedCallback) {
		auto caption = (_caption && !_caption->isHidden())
			? _caption->getTextWithAppliedMarkdown()
			: TextWithTags();
		_confirmedCallback(
			std::move(_list),
			_sendWay.current(),
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
