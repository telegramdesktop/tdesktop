/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/send_files_box.h"

#include "lang/lang_keys.h"
#include "storage/localstorage.h"
#include "storage/storage_media_prepare.h"
#include "mainwidget.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "mtproto/mtproto_config.h"
#include "chat_helpers/message_field.h"
#include "menu/menu_send.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "editor/photo_editor_layer_widget.h"
#include "history/history_drag_area.h"
#include "history/view/controls/history_view_characters_limit.h"
#include "history/view/history_view_schedule_box.h"
#include "core/mime_type.h"
#include "base/event_filter.h"
#include "base/call_delayed.h"
#include "boxes/premium_limits_box.h"
#include "boxes/premium_preview_box.h"
#include "ui/effects/scroll_content_shadow.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/popup_menu.h"
#include "ui/chat/attach/attach_album_preview.h"
#include "ui/chat/attach/attach_single_file_preview.h"
#include "ui/chat/attach/attach_single_media_preview.h"
#include "ui/grouped_layout.h"
#include "ui/toast/toast.h"
#include "ui/controls/emoji_button.h"
#include "ui/painter.h"
#include "lottie/lottie_single_player.h"
#include "data/data_document.h"
#include "data/data_user.h"
#include "data/data_peer_values.h" // Data::AmPremiumValue.
#include "data/data_premium_limits.h"
#include "data/stickers/data_stickers.h"
#include "data/stickers/data_custom_emoji.h"
#include "window/window_session_controller.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"

#include <QtCore/QMimeData>

namespace {

constexpr auto kMaxMessageLength = 4096;

using Ui::SendFilesWay;

[[nodiscard]] inline bool CanAddUrls(const QList<QUrl> &urls) {
	return !urls.isEmpty() && ranges::all_of(urls, &QUrl::isLocalFile);
}

[[nodiscard]] bool CanAddFiles(not_null<const QMimeData*> data) {
	return data->hasImage() || CanAddUrls(Core::ReadMimeUrls(data));
}

void FileDialogCallback(
		FileDialog::OpenResult &&result,
		Fn<bool(const Ui::PreparedList&)> checkResult,
		Fn<void(Ui::PreparedList)> callback,
		bool premium,
		std::shared_ptr<Ui::Show> show) {
	auto showError = [=](tr::phrase<> text) {
		show->showToast(text(tr::now));
	};

	auto list = Storage::PreparedFileFromFilesDialog(
		std::move(result),
		checkResult,
		showError,
		st::sendMediaPreviewSize,
		premium);

	if (!list) {
		return;
	}

	callback(std::move(*list));
}

rpl::producer<QString> FieldPlaceholder(
		const Ui::PreparedList &list,
		SendFilesWay way) {
	return list.canAddCaption(
			way.groupFiles() && way.sendImagesAsPhotos(),
			way.sendImagesAsPhotos())
		? tr::lng_photo_caption()
		: tr::lng_photos_comment();
}

} // namespace

SendFilesLimits DefaultLimitsForPeer(not_null<PeerData*> peer) {
	using Flag = SendFilesAllow;
	using Restriction = ChatRestriction;
	const auto allowByRestriction = [&](Restriction check, Flag allow) {
		return Data::RestrictionError(peer, check) ? Flag() : allow;
	};
	return Flag()
		| (peer->slowmodeApplied() ? Flag::OnlyOne : Flag())
		| (Data::AllowEmojiWithoutPremium(peer)
			? Flag::EmojiWithoutPremium
			: Flag())
		| allowByRestriction(Restriction::SendPhotos, Flag::Photos)
		| allowByRestriction(Restriction::SendVideos, Flag::Videos)
		| allowByRestriction(Restriction::SendMusic, Flag::Music)
		| allowByRestriction(Restriction::SendFiles, Flag::Files)
		| allowByRestriction(Restriction::SendStickers, Flag::Stickers)
		| allowByRestriction(Restriction::SendGifs, Flag::Gifs)
		| allowByRestriction(Restriction::SendOther, Flag::Texts);
}

SendFilesCheck DefaultCheckForPeer(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	return DefaultCheckForPeer(controller->uiShow(), peer);
}

SendFilesCheck DefaultCheckForPeer(
		std::shared_ptr<Ui::Show> show,
		not_null<PeerData*> peer) {
	return [=](
			const Ui::PreparedFile &file,
			bool compress,
			bool silent) {
		const auto error = Data::FileRestrictionError(peer, file, compress);
		if (error && !silent) {
			show->showToast(*error);
		}
		return !error.has_value();
	};
}

SendFilesBox::Block::Block(
	not_null<QWidget*> parent,
	const style::ComposeControls &st,
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
			st,
			my,
			way);
		_preview.reset(preview);
	} else {
		const auto media = Ui::SingleMediaPreview::Create(
			parent,
			st,
			gifPaused,
			first);
		if (media) {
			_isSingleMedia = true;
			_preview.reset(media);
		} else {
			_preview.reset(Ui::CreateChild<Ui::SingleFilePreview>(
				parent.get(),
				st,
				first));
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

rpl::producer<int> SendFilesBox::Block::itemModifyRequest() const {
	using namespace rpl::mappers;

	const auto preview = _preview.get();
	const auto from = _from;
	if (_isAlbum) {
		const auto album = static_cast<Ui::AlbumPreview*>(preview);
		return album->thumbModified() | rpl::map(_1 + from);
	} else if (_isSingleMedia) {
		const auto media = static_cast<Ui::SingleMediaPreview*>(preview);
		return media->modifyRequests() | rpl::map_to(from);
	} else {
		return rpl::never<int>();
	}
}

void SendFilesBox::Block::setSendWay(Ui::SendFilesWay way) {
	if (!_isAlbum) {
		if (_isSingleMedia) {
			const auto media = static_cast<Ui::SingleMediaPreview*>(
				_preview.get());
			media->setSendWay(way);
		}
		return;
	}
	applyChanges();
	const auto album = static_cast<Ui::AlbumPreview*>(_preview.get());
	album->setSendWay(way);
}

void SendFilesBox::Block::toggleSpoilers(bool enabled) {
	if (_isAlbum) {
		const auto album = static_cast<Ui::AlbumPreview*>(_preview.get());
		album->toggleSpoilers(enabled);
	} else if (_isSingleMedia) {
		const auto media = static_cast<Ui::SingleMediaPreview*>(
			_preview.get());
		media->setSpoiler(enabled);
	}
}

void SendFilesBox::Block::applyChanges() {
	if (!_isAlbum) {
		if (_isSingleMedia) {
			const auto media = static_cast<Ui::SingleMediaPreview*>(
				_preview.get());
			if (media->canHaveSpoiler()) {
				(*_items)[_from].spoiler = media->hasSpoiler();
			}
		}
		return;
	}
	const auto album = static_cast<Ui::AlbumPreview*>(_preview.get());
	const auto order = album->takeOrder();
	const auto guard = gsl::finally([&] {
		const auto spoilered = album->collectSpoileredIndices();
		for (auto i = 0, count = int(order.size()); i != count; ++i) {
			if (album->canHaveSpoiler(i)) {
				(*_items)[_from + i].spoiler = spoilered.contains(i);
			}
		}
	});
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
	not_null<PeerData*> toPeer,
	Api::SendType sendType,
	SendMenu::Details sendMenuDetails)
: SendFilesBox(nullptr, {
	.show = controller->uiShow(),
	.list = std::move(list),
	.caption = caption,
	.captionToPeer = toPeer,
	.limits = DefaultLimitsForPeer(toPeer),
	.check = DefaultCheckForPeer(controller, toPeer),
	.sendType = sendType,
	.sendMenuDetails = [=] { return sendMenuDetails; },
}) {
}

SendFilesBox::SendFilesBox(QWidget*, SendFilesBoxDescriptor &&descriptor)
: _show(std::move(descriptor.show))
, _st(descriptor.stOverride
	? *descriptor.stOverride
	: st::defaultComposeControls)
, _sendType(descriptor.sendType)
, _titleHeight(st::boxTitleHeight)
, _list(std::move(descriptor.list))
, _limits(descriptor.limits)
, _sendMenuDetails(prepareSendMenuDetails(descriptor))
, _sendMenuCallback(prepareSendMenuCallback())
, _captionToPeer(descriptor.captionToPeer)
, _check(std::move(descriptor.check))
, _confirmedCallback(std::move(descriptor.confirmed))
, _cancelledCallback(std::move(descriptor.cancelled))
, _caption(this, _st.files.caption, Ui::InputField::Mode::MultiLine)
, _prefilledCaptionText(std::move(descriptor.caption))
, _scroll(this, st::boxScroll)
, _inner(
	_scroll->setOwnedWidget(
		object_ptr<Ui::VerticalLayout>(_scroll.data()))) {
	enqueueNextPrepare();
}

Fn<SendMenu::Details()> SendFilesBox::prepareSendMenuDetails(
		const SendFilesBoxDescriptor &descriptor) {
	auto initial = descriptor.sendMenuDetails;
	return crl::guard(this, [=] {
		auto result = initial ? initial() : SendMenu::Details();
		result.spoiler = !hasSpoilerMenu()
			? SendMenu::SpoilerState::None
			: allWithSpoilers()
			? SendMenu::SpoilerState::Enabled
			: SendMenu::SpoilerState::Possible;
		const auto way = _sendWay.current();
		const auto canMoveCaption = _list.canMoveCaption(
			way.groupFiles() && way.sendImagesAsPhotos(),
			way.sendImagesAsPhotos()
		) && _caption && HasSendText(_caption);
		result.caption = !canMoveCaption
			? SendMenu::CaptionState::None
			: _invertCaption
			? SendMenu::CaptionState::Above
			: SendMenu::CaptionState::Below;
		return result;
	});
}

auto SendFilesBox::prepareSendMenuCallback()
-> Fn<void(MenuAction, MenuDetails)> {
	return crl::guard(this, [=](MenuAction action, MenuDetails details) {
		using Type = SendMenu::ActionType;
		switch (action.type) {
		case Type::CaptionDown: _invertCaption = false; break;
		case Type::CaptionUp: _invertCaption = true; break;
		case Type::SpoilerOn: toggleSpoilers(true); break;
		case Type::SpoilerOff: toggleSpoilers(false); break;
		default:
			SendMenu::DefaultCallback(
				_show,
				sendCallback())(
					action,
					details);
			break;
		}
	});
}

void SendFilesBox::initPreview() {
	using namespace rpl::mappers;

	refreshControls(true);

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
	const auto sideLimit = PhotoSideLimit(); // Get on main thread.
	crl::async([weak, sideLimit, file = std::move(file)]() mutable {
		Storage::PrepareDetails(file, st::sendMediaPreviewSize, sideLimit);
		crl::on_main([weak, file = std::move(file)]() mutable {
			if (weak) {
				weak->addPreparedAsyncFile(std::move(file));
			}
		});
	});
}

void SendFilesBox::prepare() {
	initSendWay();
	setupCaption();
	setupSendWayControls();
	preparePreview();
	initPreview();
	SetupShadowsToScrollContent(this, _scroll, _inner->heightValue());
	setCloseByOutsideClick(false);

	boxClosing() | rpl::start_with_next([=] {
		if (!_confirmed && _cancelledCallback) {
			_cancelledCallback();
		}
	}, lifetime());

	setupDragArea();
}

void SendFilesBox::setupDragArea() {
	// Avoid both drag areas appearing at one time.
	auto computeState = [=](const QMimeData *data) {
		using DragState = Storage::MimeDataState;
		const auto state = Storage::ComputeMimeDataState(data);
		return (state == DragState::PhotoFiles || state == DragState::Image)
			? (_sendWay.current().sendImagesAsPhotos()
				? DragState::Image
				: DragState::Files)
			: state;
	};
	const auto areas = DragArea::SetupDragAreaToContainer(
		this,
		CanAddFiles,
		[=](bool f) { _caption->setAcceptDrops(f); },
		[=] { updateControlsGeometry(); },
		std::move(computeState));

	const auto droppedCallback = [=](bool compress) {
		return [=](const QMimeData *data) {
			addFiles(data);
			_show->activate();
		};
	};
	areas.document->setDroppedCallback(droppedCallback(false));
	areas.photo->setDroppedCallback(droppedCallback(true));
}

void SendFilesBox::refreshAllAfterChanges(int fromItem, Fn<void()> perform) {
	auto fromBlock = 0;
	for (auto count = int(_blocks.size()); fromBlock != count; ++fromBlock) {
		if (_blocks[fromBlock].tillIndex() >= fromItem) {
			break;
		}
	}
	for (auto index = fromBlock; index < _blocks.size(); ++index) {
		_blocks[index].applyChanges();
	}
	if (perform) {
		perform();
	}
	generatePreviewFrom(fromBlock);
	{
		auto sendWay = _sendWay.current();
		sendWay.setHasCompressedStickers(_list.hasSticker());
		if (_limits & SendFilesAllow::OnlyOne) {
			if (_list.files.size() > 1) {
				sendWay.setGroupFiles(true);
			}
		}
		_sendWay = sendWay;
	}
	_inner->resizeToWidth(st::boxWideWidth);
	refreshControls();
	captionResized();
}

void SendFilesBox::openDialogToAddFileToAlbum() {
	const auto show = uiShow();
	const auto checkResult = [=](const Ui::PreparedList &list) {
		if (!(_limits & SendFilesAllow::OnlyOne)) {
			return true;
		} else if (!_list.canBeSentInSlowmodeWith(list)) {
			showToast(tr::lng_slowmode_no_many(tr::now));
			return false;
		}
		return true;
	};
	const auto callback = [=](FileDialog::OpenResult &&result) {
		const auto premium = _show->session().premium();
		FileDialogCallback(
			std::move(result),
			checkResult,
			[=](Ui::PreparedList list) { addFiles(std::move(list)); },
			premium,
			show);
	};

	FileDialog::GetOpenPaths(
		this,
		tr::lng_choose_file(tr::now),
		FileDialog::AllOrImagesFilter(),
		crl::guard(this, callback));
}

void SendFilesBox::refreshButtons() {
	clearButtons();

	_send = addButton(
		(_sendType == Api::SendType::Normal
			? tr::lng_send_button()
			: tr::lng_create_group_next()),
		[=] { send({}); });
	if (_sendType == Api::SendType::Normal) {
		SendMenu::SetupMenuAndShortcuts(
			_send,
			_show,
			_sendMenuDetails,
			_sendMenuCallback);
	}
	addButton(tr::lng_cancel(), [=] { closeBox(); });
	_addFile = addLeftButton(
		tr::lng_stickers_featured_add(),
		base::fn_delayed(st::historyAttach.ripple.hideDuration, this, [=] {
			openDialogToAddFileToAlbum();
		}));

	addMenuButton();
}

bool SendFilesBox::hasSendMenu(const SendMenu::Details &details) const {
	return (details.type != SendMenu::Type::Disabled)
		|| (details.spoiler != SendMenu::SpoilerState::None)
		|| (details.caption != SendMenu::CaptionState::None);
}

bool SendFilesBox::hasSpoilerMenu() const {
	return _list.hasSpoilerMenu(_sendWay.current().sendImagesAsPhotos());
}

void SendFilesBox::applyBlockChanges() {
	for (auto &block : _blocks) {
		block.applyChanges();
	}
}

bool SendFilesBox::allWithSpoilers() {
	applyBlockChanges();
	return ranges::all_of(_list.files, &Ui::PreparedFile::spoiler);
}

void SendFilesBox::toggleSpoilers(bool enabled) {
	for (auto &file : _list.files) {
		file.spoiler = enabled;
	}
	for (auto &block : _blocks) {
		block.toggleSpoilers(enabled);
	}
}

void SendFilesBox::addMenuButton() {
	const auto details = _sendMenuDetails();
	if (!hasSendMenu(details)) {
		return;
	}

	const auto top = addTopButton(_st.files.menu);
	top->setClickedCallback([=] {
		const auto &tabbed = _st.tabbed;
		_menu = base::make_unique_q<Ui::PopupMenu>(top, tabbed.menu);
		const auto position = QCursor::pos();
		SendMenu::FillSendMenu(
			_menu.get(),
			_show,
			_sendMenuDetails(),
			_sendMenuCallback,
			&_st.tabbed.icons,
			position);
		_menu->popup(position);
		return true;
	});
}

void SendFilesBox::initSendWay() {
	_sendWay = [&] {
		auto result = Core::App().settings().sendFilesWay();
		result.setHasCompressedStickers(_list.hasSticker());
		if ((_limits & SendFilesAllow::OnlyOne)
			&& (_list.files.size() > 1)) {
			result.setGroupFiles(true);
		}
		if (_list.overrideSendImagesAsPhotos == false) {
			if (!(_limits & SendFilesAllow::OnlyOne)
				|| !_list.hasSticker()) {
				result.setSendImagesAsPhotos(false);
			}
			return result;
		} else if (_list.overrideSendImagesAsPhotos == true) {
			result.setSendImagesAsPhotos(true);
			const auto silent = true;
			if (!checkWithWay(result, silent)) {
				result.setSendImagesAsPhotos(false);
			}
			return result;
		}
		const auto silent = true;
		if (!checkWithWay(result, silent)) {
			result.setSendImagesAsPhotos(!result.sendImagesAsPhotos());
		}
		return result;
	}();
	_sendWay.changes(
	) | rpl::start_with_next([=](SendFilesWay value) {
		const auto hidden = [&] {
			return !_caption || _caption->isHidden();
		};
		const auto was = hidden();
		updateCaptionPlaceholder();
		updateEmojiPanelGeometry();
		for (auto &block : _blocks) {
			block.setSendWay(value);
		}
		refreshButtons();
		if (was != hidden()) {
			updateBoxSize();
			updateControlsGeometry();
		}
		setInnerFocus();
	}, lifetime());
}

void SendFilesBox::updateCaptionPlaceholder() {
	if (!_caption) {
		return;
	}
	const auto way = _sendWay.current();
	if (!_list.canAddCaption(
			way.groupFiles() && way.sendImagesAsPhotos(),
			way.sendImagesAsPhotos())
		&& ((_limits & SendFilesAllow::OnlyOne)
			|| !(_limits & SendFilesAllow::Texts))) {
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

	_blocks.erase(_blocks.begin() + fromBlock, _blocks.end());

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
	const auto gifPaused = [show = _show] {
		return show->paused(Window::GifPauseReason::Layer);
	};
	_blocks.emplace_back(
		_inner.data(),
		_st,
		&_list.files,
		from,
		till,
		gifPaused,
		_sendWay.current());
	auto &block = _blocks.back();
	const auto widget = _inner->add(
		block.takeWidget(),
		QMargins(0, _inner->count() ? st::sendMediaRowSkip : 0, 0, 0));

	block.itemDeleteRequest(
	) | rpl::filter([=] {
		return !_removingIndex;
	}) | rpl::start_with_next([=](int index) {
		applyBlockChanges();

		_removingIndex = index;
		crl::on_main(this, [=] {
			const auto index = base::take(_removingIndex).value_or(-1);
			if (index < 0 || index >= _list.files.size()) {
				return;
			}
			// Just close the box if it is the only one.
			if (_list.files.size() == 1) {
				closeBox();
				return;
			}
			refreshAllAfterChanges(index, [&] {
				_list.files.erase(_list.files.begin() + index);
			});
		});
	}, widget->lifetime());

	const auto show = uiShow();
	block.itemReplaceRequest(
	) | rpl::start_with_next([=](int index) {
		applyBlockChanges();

		const auto replace = [=](Ui::PreparedList list) {
			if (list.files.empty()) {
				return;
			}
			refreshAllAfterChanges(from, [&] {
				_list.files[index] = std::move(list.files.front());
			});
		};
		const auto checkSlowmode = [=](const Ui::PreparedList &list) {
			if (list.files.empty() || !(_limits & SendFilesAllow::OnlyOne)) {
				return true;
			}
			auto removing = std::move(_list.files[index]);
			std::swap(_list.files[index], _list.files.back());
			_list.files.pop_back();
			const auto result = _list.canBeSentInSlowmodeWith(list);
			_list.files.push_back(std::move(removing));
			std::swap(_list.files[index], _list.files.back());
			if (!result) {
				show->showToast(tr::lng_slowmode_no_many(tr::now));
				return false;
			}
			return true;
		};
		const auto checkRights = [=](const Ui::PreparedList &list) {
			if (list.files.empty()) {
				return true;
			}
			auto removing = std::move(_list.files[index]);
			std::swap(_list.files[index], _list.files.back());
			_list.files.pop_back();
			auto way = _sendWay.current();
			const auto has = _list.hasSticker()
				|| list.files.front().isSticker();
			way.setHasCompressedStickers(has);
			if (_limits & SendFilesAllow::OnlyOne) {
				way.setGroupFiles(true);
			}
			const auto silent = true;
			if (!checkWith(list, way, silent)
				&& (!(_limits & SendFilesAllow::OnlyOne) || !has)) {
				way.setSendImagesAsPhotos(!way.sendImagesAsPhotos());
			}
			const auto result = checkWith(list, way);
			_list.files.push_back(std::move(removing));
			std::swap(_list.files[index], _list.files.back());
			if (!result) {
				return false;
			}
			_sendWay = way;
			return true;
		};
		const auto checkResult = [=](const Ui::PreparedList &list) {
			return checkSlowmode(list) && checkRights(list);
		};
		const auto callback = [=](FileDialog::OpenResult &&result) {
			const auto premium = _show->session().premium();
			FileDialogCallback(
				std::move(result),
				checkResult,
				replace,
				premium,
				show);
		};

		FileDialog::GetOpenPath(
			this,
			tr::lng_choose_file(tr::now),
			FileDialog::AllOrImagesFilter(),
			crl::guard(this, callback));
	}, widget->lifetime());

	const auto openedOnce = widget->lifetime().make_state<bool>(false);
	block.itemModifyRequest(
	) | rpl::start_with_next([=, show = _show](int index) {
		applyBlockChanges();

		if (!(*openedOnce)) {
			show->session().settings().incrementPhotoEditorHintShown();
			show->session().saveSettings();
		}
		*openedOnce = true;
		Editor::OpenWithPreparedFile(
			this,
			show,
			&_list.files[index],
			st::sendMediaPreviewSize,
			[=] { refreshAllAfterChanges(from); });
	}, widget->lifetime());
}

void SendFilesBox::refreshControls(bool initial) {
	refreshButtons();
	refreshTitleText();
	updateSendWayControls();
	updateCaptionPlaceholder();
}

void SendFilesBox::setupSendWayControls() {
	const auto groupFilesFirst = _sendWay.current().groupFiles();
	const auto asPhotosFirst = _sendWay.current().sendImagesAsPhotos();
	_groupFiles.create(
		this,
		tr::lng_send_grouped(tr::now),
		groupFilesFirst,
		_st.files.checkbox,
		_st.files.check);
	_sendImagesAsPhotos.create(
		this,
		tr::lng_send_compressed(tr::now),
		_sendWay.current().sendImagesAsPhotos(),
		_st.files.checkbox,
		_st.files.check);

	_sendWay.changes(
	) | rpl::start_with_next([=](SendFilesWay value) {
		_groupFiles->setChecked(value.groupFiles());
		_sendImagesAsPhotos->setChecked(value.sendImagesAsPhotos());
	}, lifetime());

	_groupFiles->checkedChanges(
	) | rpl::start_with_next([=](bool checked) {
		auto sendWay = _sendWay.current();
		if (sendWay.groupFiles() == checked) {
			return;
		}
		sendWay.setGroupFiles(checked);
		if (checkWithWay(sendWay)) {
			_sendWay = sendWay;
		} else {
			Ui::PostponeCall(_groupFiles.data(), [=] {
				_groupFiles->setChecked(!checked);
			});
		}
	}, lifetime());

	_sendImagesAsPhotos->checkedChanges(
	) | rpl::start_with_next([=](bool checked) {
		auto sendWay = _sendWay.current();
		if (sendWay.sendImagesAsPhotos() == checked) {
			return;
		}
		sendWay.setSendImagesAsPhotos(checked);
		if (checkWithWay(sendWay)) {
			_sendWay = sendWay;
		} else {
			Ui::PostponeCall(_sendImagesAsPhotos.data(), [=] {
				_sendImagesAsPhotos->setChecked(!checked);
			});
		}
	}, lifetime());

	_wayRemember.create(
		this,
		tr::lng_remember(tr::now),
		false,
		_st.files.checkbox,
		_st.files.check);
	_wayRemember->hide();
	rpl::combine(
		_groupFiles->checkedValue(),
		_sendImagesAsPhotos->checkedValue()
	) | rpl::start_with_next([=](bool groupFiles, bool asPhoto) {
		_wayRemember->setVisible(
			(groupFiles != groupFilesFirst) || (asPhoto != asPhotosFirst));
		captionResized();
	}, lifetime());

	_hintLabel.create(
		this,
		tr::lng_edit_photo_editor_hint(tr::now),
		st::editMediaHintLabel);
}

bool SendFilesBox::checkWithWay(Ui::SendFilesWay way, bool silent) const {
	return checkWith({}, way, silent);
}

bool SendFilesBox::checkWith(
		const Ui::PreparedList &added,
		Ui::SendFilesWay way,
		bool silent) const {
	if (!_check) {
		return true;
	}
	const auto compress = way.sendImagesAsPhotos();
	auto &already = _list.files;
	for (const auto &file : ranges::views::concat(already, added.files)) {
		if (!_check(file, compress, silent)) {
			return false;
		}
	}
	return true;
}

void SendFilesBox::updateSendWayControls() {
	const auto onlyOne = (_limits & SendFilesAllow::OnlyOne);
	_groupFiles->setVisible(_list.hasGroupOption(onlyOne));
	_sendImagesAsPhotos->setVisible(
		_list.hasSendImagesAsPhotosOption(onlyOne));
	_sendImagesAsPhotos->setText((_list.files.size() > 1)
		? tr::lng_send_compressed(tr::now)
		: tr::lng_send_compressed_one(tr::now));

	_hintLabel->setVisible(
		_show->session().settings().photoEditorHintShown()
			? _list.canHaveEditorHintLabel()
			: false);
}

void SendFilesBox::setupCaption() {
	const auto allow = [=](not_null<DocumentData*> emoji) {
		return _captionToPeer
			? Data::AllowEmojiWithoutPremium(_captionToPeer, emoji)
			: (_limits & SendFilesAllow::EmojiWithoutPremium);
	};
	const auto show = _show;
	InitMessageFieldHandlers(
		&show->session(),
		show,
		_caption.data(),
		[=] { return show->paused(Window::GifPauseReason::Layer); },
		allow,
		&_st.files.caption);
	Ui::Emoji::SuggestionsController::Init(
		getDelegate()->outerContainer(),
		_caption,
		&_show->session(),
		{
			.suggestCustomEmoji = true,
			.allowCustomWithoutPremium = allow,
			.st = &_st.suggestions,
		});

	if (!_prefilledCaptionText.text.isEmpty()) {
		_caption->setTextWithTags(
			_prefilledCaptionText,
			Ui::InputField::HistoryAction::Clear);

		auto cursor = _caption->textCursor();
		cursor.movePosition(QTextCursor::End);
		_caption->setTextCursor(cursor);
	}
	_caption->setSubmitSettings(
		Core::App().settings().sendSubmitWay());
	_caption->setMaxLength(kMaxMessageLength);

	_caption->heightChanges(
	) | rpl::start_with_next([=] {
		captionResized();
	}, _caption->lifetime());
	_caption->submits(
	) | rpl::start_with_next([=](Qt::KeyboardModifiers modifiers) {
		const auto ctrlShiftEnter = modifiers.testFlag(Qt::ShiftModifier)
			&& (modifiers.testFlag(Qt::ControlModifier)
				|| modifiers.testFlag(Qt::MetaModifier));
		send({}, ctrlShiftEnter);
	}, _caption->lifetime());
	_caption->cancelled(
	) | rpl::start_with_next([=] {
		closeBox();
	}, _caption->lifetime());
	_caption->setMimeDataHook([=](
			not_null<const QMimeData*> data,
			Ui::InputField::MimeAction action) {
		if (action == Ui::InputField::MimeAction::Check) {
			return CanAddFiles(data);
		} else if (action == Ui::InputField::MimeAction::Insert) {
			return addFiles(data);
		}
		Unexpected("action in MimeData hook.");
	});

	updateCaptionPlaceholder();
	setupEmojiPanel();

	rpl::single(rpl::empty_value()) | rpl::then(
		_caption->changes()
	) | rpl::start_with_next([=] {
		checkCharsLimitation();
	}, _caption->lifetime());
}

void SendFilesBox::checkCharsLimitation() {
	const auto limits = Data::PremiumLimits(&_show->session());
	const auto caption = (_caption && !_caption->isHidden())
		? _caption->getTextWithAppliedMarkdown()
		: TextWithTags();
	const auto remove = caption.text.size() - limits.captionLengthCurrent();
	if ((remove > 0) && _emojiToggle) {
		if (!_charsLimitation) {
			_charsLimitation = base::make_unique_q<CharactersLimitLabel>(
				this,
				_emojiToggle.data(),
				style::al_top);
			_charsLimitation->show();
			Data::AmPremiumValue(
				&_show->session()
			) | rpl::start_with_next([=] {
				checkCharsLimitation();
			}, _charsLimitation->lifetime());
		}
		_charsLimitation->setLeft(remove);
	} else {
		if (_charsLimitation) {
			_charsLimitation = nullptr;
		}
	}
}

void SendFilesBox::setupEmojiPanel() {
	Expects(_caption != nullptr);

	const auto container = getDelegate()->outerContainer();
	using Selector = ChatHelpers::TabbedSelector;
	_emojiPanel = base::make_unique_q<ChatHelpers::TabbedPanel>(
		container,
		ChatHelpers::TabbedPanelDescriptor{
			.ownedSelector = object_ptr<Selector>(
				nullptr,
				ChatHelpers::TabbedSelectorDescriptor{
					.show = _show,
					.st = _st.tabbed,
					.level = Window::GifPauseReason::Layer,
					.mode = ChatHelpers::TabbedSelector::Mode::EmojiOnly,
					.features = {
						.stickersSettings = false,
						.openStickerSets = false,
					},
				}),
		});
	_emojiPanel->setDesiredHeightValues(
		1.,
		st::emojiPanMinHeight / 2,
		st::emojiPanMinHeight);
	_emojiPanel->hide();
	_emojiPanel->selector()->setCurrentPeer(_captionToPeer);
	_emojiPanel->selector()->setAllowEmojiWithoutPremium(
		_limits & SendFilesAllow::EmojiWithoutPremium);
	_emojiPanel->selector()->emojiChosen(
	) | rpl::start_with_next([=](ChatHelpers::EmojiChosen data) {
		Ui::InsertEmojiAtCursor(_caption->textCursor(), data.emoji);
	}, lifetime());
	_emojiPanel->selector()->customEmojiChosen(
	) | rpl::start_with_next([=](ChatHelpers::FileChosen data) {
		const auto info = data.document->sticker();
		if (info
			&& info->setType == Data::StickersType::Emoji
			&& !_show->session().premium()
			&& !(_captionToPeer
				? Data::AllowEmojiWithoutPremium(
					_captionToPeer,
					data.document)
				: (_limits & SendFilesAllow::EmojiWithoutPremium))) {
			ShowPremiumPreviewBox(_show, PremiumFeature::AnimatedEmoji);
		} else {
			Data::InsertCustomEmoji(_caption.data(), data.document);
		}
	}, lifetime());

	const auto filterCallback = [=](not_null<QEvent*> event) {
		emojiFilterForGeometry(event);
		return base::EventFilterResult::Continue;
	};
	_emojiFilter.reset(base::install_event_filter(container, filterCallback));

	_emojiToggle.create(this, _st.files.emoji);
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

bool SendFilesBox::addFiles(not_null<const QMimeData*> data) {
	const auto premium = _show->session().premium();
	auto list = [&] {
		const auto urls = Core::ReadMimeUrls(data);
		auto result = CanAddUrls(urls)
			? Storage::PrepareMediaList(
				urls,
				st::sendMediaPreviewSize,
				premium)
			: Ui::PreparedList(
				Ui::PreparedList::Error::EmptyFile,
				QString());
		if (result.error == Ui::PreparedList::Error::None) {
			return result;
		} else if (auto read = Core::ReadMimeImage(data)) {
			return Storage::PrepareMediaFromImage(
				std::move(read.image),
				std::move(read.content),
				st::sendMediaPreviewSize);
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
	const auto lastOk = [&] {
		auto way = _sendWay.current();
		if (_limits & SendFilesAllow::OnlyOne) {
			way.setGroupFiles(true);
			if (!_list.canBeSentInSlowmode()) {
				return false;
			}
		} else if (!checkWithWay(way)) {
			return false;
		}
		_sendWay = way;
		return true;
	}();
	if (!lastOk) {
		_list.files.pop_back();
	}
	_list.filesToProcess = std::move(saved);
}

void SendFilesBox::refreshTitleText() {
	using Type = Ui::PreparedFile::Type;
	const auto count = int(_list.files.size());
	if (count > 1) {
		const auto imagesCount = ranges::count(
			_list.files,
			Type::Photo,
			&Ui::PreparedFile::type);
		_titleText = (imagesCount == count)
			? tr::lng_send_images_selected(tr::now, lt_count, count)
			: tr::lng_send_files_selected(tr::now, lt_count, count);
	} else {
		const auto type = _list.files.empty()
			? Type::None
			: _list.files.front().type;
		_titleText = (type == Type::Photo)
			? tr::lng_send_image(tr::now)
			: (type == Type::Video)
			? tr::lng_send_video(tr::now)
			: tr::lng_send_file(tr::now);
	}
	_titleHeight = st::boxTitleHeight;
}

void SendFilesBox::updateBoxSize() {
	auto footerHeight = 0;
	if (_caption && !_caption->isHidden()) {
		footerHeight += st::boxPhotoCaptionSkip + _caption->height();
	}
	const auto pairs = std::array<std::pair<RpWidget*, int>, 4>{ {
		{ _groupFiles.data(), st::boxPhotoCompressedSkip },
		{ _sendImagesAsPhotos.data(), st::boxPhotoCompressedSkip },
		{ _wayRemember.data(), st::boxPhotoCompressedSkip },
		{ _hintLabel.data(), st::editMediaLabelMargins.top() },
	} };
	for (const auto &pair : pairs) {
		const auto pointer = pair.first;
		if (pointer && !pointer->isHidden()) {
			footerHeight += pair.second + pointer->heightNoMargins();
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
		p.setPen(getDelegate()->style().title.textFg);
		p.drawTextLeft(
			st::boxPhotoTitlePosition.x(),
			st::boxTitlePosition.y() - st::boxTopMargin,
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
	if (_caption && !_caption->isHidden()) {
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
	const auto pairs = std::array<std::pair<RpWidget*, int>, 4>{ {
		{ _hintLabel.data(), st::editMediaLabelMargins.top() },
		{ _groupFiles.data(), st::boxPhotoCompressedSkip },
		{ _sendImagesAsPhotos.data(), st::boxPhotoCompressedSkip },
		{ _wayRemember.data(), st::boxPhotoCompressedSkip },
	} };
	for (const auto &pair : ranges::views::reverse(pairs)) {
		const auto pointer = pair.first;
		if (pointer && !pointer->isHidden()) {
			pointer->moveToLeft(
				st::boxPhotoPadding.left(),
				bottom - pointer->heightNoMargins());
			bottom -= pair.second + pointer->heightNoMargins();
		}
	}
	_scroll->resize(width(), bottom - _titleHeight.current());
	_scroll->move(0, _titleHeight.current());
}

void SendFilesBox::setInnerFocus() {
	if (_caption && !_caption->isHidden()) {
		_caption->setFocusFast();
	} else {
		BoxContent::setInnerFocus();
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

bool SendFilesBox::validateLength(const QString &text) const {
	const auto session = &_show->session();
	const auto limit = Data::PremiumLimits(session).captionLengthCurrent();
	const auto remove = int(text.size()) - limit;
	const auto way = _sendWay.current();
	if (remove <= 0
		|| !_list.canAddCaption(
			way.groupFiles() && way.sendImagesAsPhotos(),
			way.sendImagesAsPhotos())) {
		return true;
	}
	_show->showBox(
		Box(CaptionLimitReachedBox, session, remove, &_st.premium));
	return false;
}

void SendFilesBox::send(
		Api::SendOptions options,
		bool ctrlShiftEnter) {
	if ((_sendType == Api::SendType::Scheduled
		|| _sendType == Api::SendType::ScheduledToUser)
		&& !options.scheduled) {
		auto child = _sendMenuDetails();
		child.spoiler = SendMenu::SpoilerState::None;
		child.caption = SendMenu::CaptionState::None;
		return SendMenu::DefaultCallback(_show, sendCallback())(
			{ .type = SendMenu::ActionType::Schedule },
			child);
	}
	if (_preparing) {
		_whenReadySend = [=] {
			send(options, ctrlShiftEnter);
		};
		return;
	}

	if (_wayRemember && _wayRemember->checked()) {
		saveSendWaySettings();
	}

	for (auto &item : _list.files) {
		item.spoiler = false;
	}
	applyBlockChanges();

	Storage::ApplyModifications(_list);

	_confirmed = true;
	if (_confirmedCallback) {
		auto caption = (_caption && !_caption->isHidden())
			? _caption->getTextWithAppliedMarkdown()
			: TextWithTags();
		options.invertCaption = _invertCaption;
		if (!validateLength(caption.text)) {
			return;
		}
		_confirmedCallback(
			std::move(_list),
			_sendWay.current(),
			std::move(caption),
			options,
			ctrlShiftEnter);
	}
	closeBox();
}

Fn<void(Api::SendOptions)> SendFilesBox::sendCallback() {
	return crl::guard(this, [=](Api::SendOptions options) {
		send(options, false);
	});
}

SendFilesBox::~SendFilesBox() = default;
