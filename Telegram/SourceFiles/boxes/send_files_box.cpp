/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/send_files_box.h"

#include "lang/lang_keys.h"
#include "storage/localimageloader.h"
#include "storage/localstorage.h"
#include "storage/storage_media_prepare.h"
#include "iv/iv_instance.h"
#include "mainwidget.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "mtproto/mtproto_config.h"
#include "chat_helpers/message_field.h"
#include "menu/menu_checked_action.h"
#include "menu/menu_send.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "chat_helpers/field_autocomplete.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "editor/photo_editor_layer_widget.h"
#include "history/history_drag_area.h"
#include "history/view/controls/history_view_characters_limit.h"
#include "history/view/controls/history_view_compose_ai_button.h"
#include "history/view/history_view_schedule_box.h"
#include "core/mime_type.h"
#include "core/ui_integration.h"
#include "base/event_filter.h"
#include "base/call_delayed.h"
#include "boxes/premium_limits_box.h"
#include "boxes/premium_preview_box.h"
#include "boxes/send_gif_with_caption_box.h"
#include "boxes/send_credits_box.h"
#include "ui/effects/scroll_content_shadow.h"
#include "ui/widgets/fields/number_input.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/popup_menu.h"
#include "ui/chat/attach/attach_album_preview.h"
#include "ui/chat/attach/attach_single_file_preview.h"
#include "ui/chat/attach/attach_single_media_preview.h"
#include "ui/grouped_layout.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/controls/compose_ai_button_factory.h"
#include "ui/controls/emoji_button.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "ui/ui_utility.h"
#include "lottie/lottie_single_player.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_user.h"
#include "data/data_peer_values.h" // Data::AmPremiumValue.
#include "data/data_premium_limits.h"
#include "data/stickers/data_stickers.h"
#include "data/stickers/data_custom_emoji.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QMimeData>

namespace {

constexpr auto kMaxMessageLength = 4096;
constexpr auto kMaxDisplayNameLength = 64;

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

void RenameFileBox(
		not_null<Ui::GenericBox*> box,
		const QString &currentName,
		Fn<void(QString)> apply) {
	box->setTitle(tr::lng_rename_file());
	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::settingsDeviceName,
		rpl::single(QString()),
		currentName));
	const auto extension = [&] {
		if (currentName.isEmpty()) {
			return u".png"_q;
		}
		const auto dot = currentName.lastIndexOf('.');
		return (dot >= 0) ? currentName.mid(dot) : QString();
	}();
	const auto nameWithoutExt = extension.isEmpty()
		? currentName
		: currentName.left(currentName.size() - extension.size());
	const auto maxNameLength = kMaxDisplayNameLength - extension.size();
	field->setMaxLength((maxNameLength > 0) ? maxNameLength : 0);
	field->setText(nameWithoutExt);
	field->selectAll();
	box->setFocusCallback([=] {
		field->setFocusFast();
	});
	const auto save = [=] {
		const auto newName = field->getLastText().trimmed();
		if (newName.isEmpty()) {
			field->showError();
			return;
		}
		if ((newName.size() + extension.size()) > kMaxDisplayNameLength) {
			field->showError();
			return;
		}
		const auto weak = base::make_weak(box);
		apply(newName + extension);
		if (const auto strong = weak.get()) {
			strong->closeBox();
		}
	};
	field->submits() | rpl::on_next([=] {
		save();
	}, box->lifetime());
	box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

void EditFileCaptionBox(
		not_null<Ui::GenericBox*> box,
		const style::ComposeControls &st,
		PeerData *captionToPeer,
		TextWithTags currentCaption,
		Fn<bool(TextWithTags)> apply) {
	box->setTitle(tr::lng_context_upload_edit_caption());
	const auto wrap = box->addRow(
		object_ptr<Ui::RpWidget>(box),
		st::boxRowPadding);
	const auto field = Ui::CreateChild<Ui::InputField>(
		wrap,
		st.files.caption,
		Ui::InputField::Mode::MultiLine,
		tr::lng_photo_caption());
	field->setMaxLength(kMaxMessageLength);
	field->setSubmitSettings(Core::App().settings().sendSubmitWay());
	Ui::ResizeFitChild(wrap, field);
	if (const auto window = Core::App().findWindow(box)) {
		const auto controller = window->sessionController();
		const auto allow = [=](not_null<DocumentData*> emoji) {
			return captionToPeer
				&& Data::AllowEmojiWithoutPremium(captionToPeer, emoji);
		};
		Ui::SetupCaptionFieldInBox(
			box,
			controller,
			field,
			captionToPeer,
			allow,
			PremiumFeature::EmojiStatus);
		if (controller) {
			const auto chatStyle = InitMessageFieldHandlers({
				.session = &controller->session(),
				.show = controller->uiShow(),
				.field = field,
				.customEmojiPaused = [=] {
					return controller->isGifPausedAtLeastFor(
						Window::GifPauseReason::Layer);
				},
				.allowPremiumEmoji = allow,
				.fieldStyle = &st.files.caption,
			});
			const auto aiButton = Ui::SetupCaptionAiButton({
				.parent = field->parentWidget(),
				.field = field,
				.session = &controller->session(),
				.show = controller->uiShow(),
				.chatStyle = chatStyle,
			});
			rpl::combine(
				box->sizeValue(),
				field->geometryValue()
			) | rpl::on_next([=](QSize, QRect) {
				Ui::UpdateCaptionAiButtonGeometry(aiButton, field);
				aiButton->raise();
			}, aiButton->lifetime());
		}
	}
	field->setTextWithTags(std::move(currentCaption));

	box->setFocusCallback([=] {
		field->setFocusFast();
	});
	const auto save = [=] {
		const auto text = field->getTextWithAppliedMarkdown();
		if (!apply(text)) {
			return;
		}
		box->closeBox();
	};
	field->submits() | rpl::on_next([=] {
		save();
	}, box->lifetime());
	box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

void EditPriceBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		uint64 price,
		Fn<void(uint64)> apply) {
	box->setTitle(tr::lng_paid_title());
	AddSubsectionTitle(
		box->verticalLayout(),
		tr::lng_paid_enter_cost(),
		(st::boxRowPadding - QMargins(
			st::defaultSubsectionTitlePadding.left(),
			0,
			st::defaultSubsectionTitlePadding.right(),
			0)));
	const auto limit = session->appConfig().get<int>(
		u"stars_paid_post_amount_max"_q,
		10'000);
	const auto wrap = box->addRow(object_ptr<Ui::FixedHeightWidget>(
		box,
		st::editTagField.heightMin));
	auto owned = object_ptr<Ui::NumberInput>(
		wrap,
		st::editTagField,
		tr::lng_paid_cost_placeholder(),
		price ? QString::number(price) : QString(),
		limit);
	const auto field = owned.data();
	wrap->widthValue() | rpl::on_next([=](int width) {
		field->move(0, 0);
		field->resize(width, field->height());
		wrap->resize(width, field->height());
	}, wrap->lifetime());
	field->paintRequest() | rpl::on_next([=](QRect clip) {
		auto p = QPainter(field);
		st::paidStarIcon.paint(p, 0, st::paidStarIconTop, field->width());
	}, field->lifetime());
	field->selectAll();
	box->setFocusCallback([=] {
		field->setFocusFast();
	});
	const auto about = box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_paid_about(
				lt_link,
				tr::lng_paid_about_link(tr::link),
				tr::marked),
			st::paidAmountAbout),
		st::boxRowPadding + QMargins(0, st::sendMediaRowSkip, 0, 0));
	about->setClickHandlerFilter([=](const auto &...) {
		Core::App().iv().openWithIvPreferred(
			session,
			tr::lng_paid_about_link_url(tr::now));
		return false;
	});

	const auto save = [=] {
		const auto now = field->getLastText().toULongLong();
		if (now > limit) {
			field->showError();
			return;
		}
		const auto weak = base::make_weak(box);
		apply(now);
		if (const auto strong = weak.get()) {
			strong->closeBox();
		}
	};

	QObject::connect(field, &Ui::NumberInput::submitted, box, save);

	box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

[[nodiscard]] bool SkipCaption(
		const Ui::PreparedFile &file,
		const Ui::SendFilesWay &way) {
	return way.sendImagesAsPhotos()
		? (file.type == Ui::PreparedFile::Type::Photo
			|| file.type == Ui::PreparedFile::Type::Video)
		: file.isSticker();
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
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<PeerData*> peer) {
	return [=](
			const Ui::PreparedFile &file,
			bool compress,
			bool silent) {
		const auto error = Data::FileRestrictionError(peer, file, compress);
		if (error && !silent) {
			Data::ShowSendErrorToast(show, peer, error);
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
	const Ui::Text::MarkedContext &captionContext,
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
			captionContext,
			way);
		_preview.reset(preview);
	} else {
		const auto media = way.sendImagesAsPhotos()
			? Ui::SingleMediaPreview::Create(
				parent,
				st,
				gifPaused,
				first)
			: nullptr;
		if (media) {
			_isSingleMedia = true;
			media->setSendWay(way);
			media->setCanShowHighQualityBadge(first.canUseHighQualityPhoto());
			_preview.reset(media);
		} else {
			_preview.reset(Ui::CreateChild<Ui::SingleFilePreview>(
				parent.get(),
				st,
				first,
				captionContext));
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

rpl::producer<> SendFilesBox::Block::orderUpdated() const {
	if (_isAlbum) {
		const auto album = static_cast<Ui::AlbumPreview*>(_preview.get());
		return album->orderUpdated();
	}
	return rpl::never<>();
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

QImage SendFilesBox::Block::generatePriceTagBackground() const {
	const auto preview = _preview.get();
	if (_isAlbum) {
		const auto album = static_cast<Ui::AlbumPreview*>(preview);
		return album->generatePriceTagBackground();
	} else if (_isSingleMedia) {
		const auto media = static_cast<Ui::SingleMediaPreview*>(preview);
		return media->generatePriceTagBackground();
	}
	return QImage();
}

bool SendFilesBox::Block::setSingleFileDisplayName(
		const QString &displayName) {
	if (_isAlbum || _isSingleMedia) {
		return false;
	}
	const auto single = static_cast<Ui::SingleFilePreview*>(_preview.get());
	single->setDisplayName(displayName);
	return true;
}

bool SendFilesBox::Block::setSingleFileCaption(
		int index,
		const TextWithTags &caption) {
	if (_isSingleMedia || index < _from || index >= _till) {
		return false;
	}
	if (_isAlbum) {
		const auto album = static_cast<Ui::AlbumPreview*>(_preview.get());
		album->setCaption(index - _from, caption);
		return true;
	}
	const auto single = static_cast<Ui::SingleFilePreview*>(_preview.get());
	single->setCaption(caption);
	return true;
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
	.toPeer = toPeer,
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
, _toPeer(descriptor.toPeer)
, _check(std::move(descriptor.check))
, _confirmedCallback(std::move(descriptor.confirmed))
, _cancelledCallback(std::move(descriptor.cancelled))
, _caption(
	this,
	_st.files.caption,
	Ui::InputField::Mode::MultiLine,
	tr::lng_photo_caption())
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
		) && HasSendText(_caption);
		result.caption = !canMoveCaption
			? SendMenu::CaptionState::None
			: _invertCaption
			? SendMenu::CaptionState::Above
			: SendMenu::CaptionState::Below;
		result.photoQuality = !hasSendLargePhotosOption()
			? SendMenu::PhotoQualityState::None
			: way.sendLargePhotos()
			? SendMenu::PhotoQualityState::High
			: SendMenu::PhotoQualityState::Standard;
		result.price = canChangePrice()
			? _price.current()
			: std::optional<uint64>();
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
		case Type::PhotoQualityOn: setSendLargePhotos(true); break;
		case Type::PhotoQualityOff: setSendLargePhotos(false); break;
		case Type::SpoilerOn: toggleSpoilers(true); break;
		case Type::SpoilerOff: toggleSpoilers(false); break;
		case Type::ChangePrice: changePrice(); break;
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
	) | rpl::on_next([=](int height) {
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
	const auto weak = base::make_weak(this);
	_preparing = true;
	const auto sideLimit = PhotoSideLimit(_sendWay.current().sendLargePhotos());
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

	boxClosing() | rpl::on_next([=] {
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
		return (state == DragState::PhotoFiles
			|| state == DragState::Image
			|| state == DragState::MediaFiles)
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

bool SendFilesBox::setDisplayNameInSingleFilePreview(
		int fileIndex,
		const QString &displayName) {
	for (auto &block : _blocks) {
		if (fileIndex < block.fromIndex() || fileIndex >= block.tillIndex()) {
			continue;
		}
		return block.setSingleFileDisplayName(displayName);
	}
	return false;
}

bool SendFilesBox::setCaptionInSingleFilePreview(
		int fileIndex,
		const TextWithTags &caption) {
	for (auto &block : _blocks) {
		if (fileIndex < block.fromIndex() || fileIndex >= block.tillIndex()) {
			continue;
		}
		return block.setSingleFileCaption(fileIndex, caption);
	}
	return false;
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

void SendFilesBox::refreshMessagesCount() {
	_messagesCount = _list.files.size();
}

void SendFilesBox::refreshButtons() {
	clearButtons();

	_send = addButton(
		(_sendType == Api::SendType::Normal
			? tr::lng_send_button()
			: tr::lng_create_group_next()),
		[=] { send({}); });
	refreshMessagesCount();

	const auto perMessage = _toPeer->starsPerMessageChecked();
	if (perMessage > 0) {
		_send->setText(PaidSendButtonText(_messagesCount.value(
		) | rpl::map(rpl::mappers::_1 * perMessage)));
	}
	if (_sendType == Api::SendType::Normal) {
		SendMenu::SetupMenuAndShortcuts(
			_send,
			_show,
			_sendMenuDetails,
			_sendMenuCallback,
			&_st.tabbed.menu,
			&_st.tabbed.icons);
	}
	addButton(tr::lng_cancel(), [=] {
		requestToTakeTextWithTags();
		closeBox();
	});
	_addFile = addLeftButton(
		tr::lng_stickers_featured_add(),
		base::fn_delayed(st::historyAttach.ripple.hideDuration, this, [=] {
			openDialogToAddFileToAlbum();
		}));

	addMenuButton();
}

bool SendFilesBox::hasSendMenu(const MenuDetails &details) const {
	return (details.type != SendMenu::Type::Disabled)
		|| (details.spoiler != SendMenu::SpoilerState::None)
		|| (details.caption != SendMenu::CaptionState::None)
		|| (details.photoQuality != SendMenu::PhotoQualityState::None)
		|| details.price.has_value();
}

bool SendFilesBox::hasSpoilerMenu() const {
	return !hasPrice()
		&& _list.hasSpoilerMenu(_sendWay.current().sendImagesAsPhotos());
}

bool SendFilesBox::hasSendLargePhotosOption() const {
	return _list.hasSendLargePhotosOption(
		_sendWay.current().sendImagesAsPhotos());
}

bool SendFilesBox::canChangePrice() const {
	const auto way = _sendWay.current();
	const auto broadcast = _toPeer->asBroadcast();
	return broadcast
		&& broadcast->canPostPaidMedia()
		&& _list.canChangePrice(
			way.groupFiles() && way.sendImagesAsPhotos(),
			way.sendImagesAsPhotos());
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

void SendFilesBox::setSendLargePhotos(bool enabled) {
	auto way = _sendWay.current();
	if (way.sendLargePhotos() == enabled) {
		return;
	}
	way.setSendLargePhotos(enabled);
	_sendWay = way;
}

void SendFilesBox::changePrice() {
	const auto weak = base::make_weak(this);
	const auto session = &_show->session();
	const auto now = _price.current();
	_show->show(Box(EditPriceBox, session, now, [=](uint64 price) {
		if (weak && price != now) {
			_price = price;
			refreshPriceTag();
		}
	}));
}

bool SendFilesBox::hasPrice() const {
	return canChangePrice() && _price.current() > 0;
}

void SendFilesBox::refreshPriceTag() {
	const auto resetSpoilers = hasPrice() || _priceTag;
	if (resetSpoilers) {
		for (auto &file : _list.files) {
			file.spoiler = false;
		}
		for (auto &block : _blocks) {
			block.toggleSpoilers(hasPrice());
		}
	}
	if (!hasPrice()) {
		_priceTag = nullptr;
		_priceTagBg = QImage();
	} else if (!_priceTag) {
		_priceTag = std::make_unique<Ui::RpWidget>(_inner.data());
		const auto raw = _priceTag.get();

		raw->show();
		raw->paintRequest() | rpl::on_next([=] {
			if (_priceTagBg.isNull()) {
				_priceTagBg = preparePriceTagBg(raw->size());
			}
			QPainter(raw).drawImage(0, 0, _priceTagBg);
		}, raw->lifetime());

		auto price = _price.value() | rpl::map([=](uint64 amount) {
			auto result = Ui::Text::Colorized(Ui::CreditsEmoji());
			result.append(Lang::FormatCountDecimal(amount));
			return result;
		});
		auto text = tr::lng_paid_price(
			lt_price,
			std::move(price),
			tr::marked);
		const auto label = Ui::CreateChild<Ui::FlatLabel>(
			raw,
			QString(),
			st::paidTagLabel);
		std::move(
			text
		) | rpl::on_next([=](const TextWithEntities &text) {
			label->setMarkedText(text);
		}, label->lifetime());
		label->show();
		label->sizeValue() | rpl::on_next([=](QSize size) {
			const auto inner = QRect(QPoint(), size);
			const auto rect = inner.marginsAdded(st::paidTagPadding);
			raw->resize(rect.size());
			label->move(-rect.topLeft());
		}, label->lifetime());
		_inner->sizeValue() | rpl::on_next([=](QSize size) {
			raw->move(
				(size.width() - raw->width()) / 2,
				(size.height() - raw->height()) / 2);
		}, raw->lifetime());
	} else {
		_priceTag->raise();
		_priceTag->update();
		_priceTagBg = QImage();
	}
}

QImage SendFilesBox::preparePriceTagBg(QSize size) const {
	const auto ratio = style::DevicePixelRatio();
	const auto outer = _blocks.empty()
		? size
		: _inner->widgetAt(0)->geometry().size();
	auto bg = _blocks.empty()
		? QImage()
		: _blocks.front().generatePriceTagBackground();
	if (bg.isNull()) {
		bg = QImage(ratio, ratio, QImage::Format_ARGB32_Premultiplied);
		bg.fill(Qt::black);
	}

	auto result = QImage(size * ratio, QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::black);
	auto p = QPainter(&result);
	auto hq = PainterHighQualityEnabler(p);
	p.drawImage(
		QRect(
			(size.width() - outer.width()) / 2,
			(size.height() - outer.height()) / 2,
			outer.width(),
			outer.height()),
		bg);
	p.fillRect(QRect(QPoint(), size), st::msgDateImgBg);
	p.end();

	const auto radius = std::min(size.width(), size.height()) / 2;
	return Images::Round(std::move(result), Images::CornersMask(radius));
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
		_menu->setForcedOrigin(Ui::PanelAnimation::Origin::TopRight);
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
	) | rpl::on_next([=](SendFilesWay value) {
		const auto hidden = [&] {
			return _caption->isHidden();
		};
		const auto was = hidden();
		updateCaptionVisibility();
		updateEmojiPanelGeometry();
		applyBlockChanges();
		generatePreviewFrom(0);
		_inner->resizeToWidth(st::boxWideWidth);
		refreshControls();
		captionResized();
		if (was != hidden()) {
			updateBoxSize();
			updateControlsGeometry();
		}
		setInnerFocus();
	}, lifetime());
}

void SendFilesBox::updateCaptionVisibility() {
	const auto way = _sendWay.current();
	const auto can = _list.canAddCaption(way.sendImagesAsPhotos());
	_caption->setVisible(can);
	if (_emojiToggle) {
		_emojiToggle->setVisible(can);
	}
	if (_aiButton) {
		_aiButton->setVisible(can
			&& Ui::HasEnoughLinesForAi(&_show->session(), _caption.data()));
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
	const auto captionContext = Core::TextContext({
		.session = &_show->session(),
	});
	_blocks.emplace_back(
		_inner.data(),
		_st,
		&_list.files,
		from,
		till,
		captionContext,
		gifPaused,
		_sendWay.current());
	auto &block = _blocks.back();
	const auto widget = _inner->add(
		block.takeWidget(),
		QMargins(0, _inner->count() ? st::sendMediaRowSkip : 0, 0, 0));
	struct State {
		base::unique_qptr<Ui::PopupMenu> menu;
	};
	const auto state = widget->lifetime().make_state<State>();
	const auto openedOnce = widget->lifetime().make_state<bool>(false);
	const auto openInPhotoEditor = [=, show = _show](int index) {
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
			[=](bool ok) {
				if (ok) {
					refreshAllAfterChanges(from);
				}
			},
			PhotoSideLimit(true));
	};
	const auto replaceAttachment = [=, show = _show](int index) {
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
	};
	const auto editCover = [=, show = _show](int index) {
		applyBlockChanges();

		const auto replace = [=](Ui::PreparedList list) {
			if (list.files.empty()) {
				return;
			}
			auto &entry = _list.files[index];
			const auto video = entry.information
				? std::get_if<Ui::PreparedFileInformation::Video>(
					&entry.information->media)
				: nullptr;
			if (!video) {
				return;
			}
			auto old = std::shared_ptr<Ui::PreparedFile>(
				std::move(entry.videoCover));
			entry.videoCover = std::make_unique<Ui::PreparedFile>(
				std::move(list.files.front()));
			Editor::OpenWithPreparedFile(
				this,
				show,
				entry.videoCover.get(),
				st::sendMediaPreviewSize,
				crl::guard(this, [=](bool ok) {
					if (!ok) {
						_list.files[index].videoCover = old
							? std::make_unique<Ui::PreparedFile>(
								std::move(*old))
							: nullptr;
					}
					refreshAllAfterChanges(from);
				}),
				PhotoSideLimit(true),
				video->thumbnail.size());
		};
		const auto checkResult = [=](const Ui::PreparedList &list) {
			if (list.files.empty()) {
				return true;
			}
			if (list.files.front().type != Ui::PreparedFile::Type::Photo) {
				show->showToast(tr::lng_choose_cover_bad(tr::now));
				return false;
			}
			return true;
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
			tr::lng_choose_cover(tr::now),
			FileDialog::ImagesFilter(),
			crl::guard(this, callback));
	};
	const auto clearCover = [=](int index) {
		applyBlockChanges();
		refreshAllAfterChanges(from, [&] {
			auto &entry = _list.files[index];
			entry.videoCover = nullptr;
		});
	};
	const auto showContextMenu = [=](int fileIndex, QPoint globalPosition) {
		if (from >= till
			|| fileIndex < from
			|| fileIndex >= till
			|| fileIndex >= _list.files.size()) {
			return false;
		}
		state->menu = base::make_unique_q<Ui::PopupMenu>(
			widget,
			_st.tabbed.menu);
		state->menu->setForcedOrigin(Ui::PanelAnimation::Origin::TopRight);
		const auto &file = _list.files[fileIndex];
		state->menu->addAction(tr::lng_attach_replace(tr::now), [=] {
			replaceAttachment(fileIndex);
		}, &st::menuIconReplace);
		const auto canOpenPhotoEditor = true
			&& _sendWay.current().sendImagesAsPhotos()
			&& (file.type == Ui::PreparedFile::Type::Photo);
		if (canOpenPhotoEditor) {
			state->menu->addAction(tr::lng_context_draw(tr::now), [=] {
				openInPhotoEditor(fileIndex);
			}, &st::menuIconDraw);
		}
		const auto canEditFileData = !SkipCaption(
			file,
			_sendWay.current());
		if (canEditFileData) {
			state->menu->addAction(tr::lng_rename_file(tr::now), [=] {
				auto &file = _list.files[fileIndex];
				_show->show(Box(RenameFileBox, file.displayName, [=](
						QString newName) {
					const auto displayName = std::move(newName);
					_list.files[fileIndex].displayName = displayName;
					if (!setDisplayNameInSingleFilePreview(
							fileIndex,
							displayName)) {
						refreshAllAfterChanges(from);
					}
				}));
			}, &st::menuIconEdit);
			state->menu->addAction(
				tr::lng_context_upload_edit_caption(tr::now),
				[=] {
					auto &file = _list.files[fileIndex];
					const auto count = int(_list.files.size());
					const auto sync = (fileIndex + 1 == count);
					_show->show(Box(
						EditFileCaptionBox,
						_st,
						_toPeer,
						sync ? fieldText() : file.caption,
						[=](TextWithTags text) {
							if (!validateLength(text.text)) {
								return false;
							}
							if (sync) {
								_caption->setTextWithTags(
									base::take(text));
							}
							_list.files[fileIndex].caption = text;
							if (!setCaptionInSingleFilePreview(
									fileIndex,
									text)) {
								refreshAllAfterChanges(from);
							}
							return true;
						}));
				},
					&st::menuIconCaptionShow);
		}
		const auto canToggleSpoiler = !hasPrice()
			&& _sendWay.current().sendImagesAsPhotos();
		if (canToggleSpoiler) {
			const auto spoilered = file.spoiler;
			const auto &icons = _st.tabbed.icons;
			Menu::AddCheckedAction(
				state->menu.get(),
				tr::lng_context_spoiler_effect(tr::now),
				[=] {
					applyBlockChanges();
					refreshAllAfterChanges(from, [&] {
						auto &entry = _list.files[fileIndex];
						entry.spoiler = !spoilered;
					});
				},
				&icons.menuSpoiler,
				spoilered);
		}
		const auto canEditCover = file.isVideoFile()
			&& (_toPeer->isBroadcast() || _toPeer->isSelf());
		if (canEditCover) {
			state->menu->addAction(tr::lng_context_edit_cover(tr::now), [=] {
				editCover(fileIndex);
			}, &st::menuIconEdit);

			if (file.videoCover != nullptr) {
				state->menu->addAction(
					tr::lng_context_clear_cover(tr::now),
					[=] { clearCover(fileIndex); },
					&st::menuIconCancel);
			}
		}
		if (state->menu->empty()) {
			state->menu = nullptr;
			return false;
		}
		state->menu->popup(globalPosition);
		return true;
	};

	block.itemDeleteRequest(
	) | rpl::filter([=] {
		return !_removingIndex;
	}) | rpl::on_next([=](int index) {
		applyBlockChanges();

		_removingIndex = index;
		crl::on_main(this, [=] {
			const auto index = base::take(_removingIndex).value_or(-1);
			if (index < 0 || index >= _list.files.size()) {
				return;
			}
			// Just close the box if it is the only one.
			if (_list.files.size() == 1) {
				requestToTakeTextWithTags();
				closeBox();
				return;
			}
			refreshAllAfterChanges(index, [&] {
				_list.files.erase(_list.files.begin() + index);
				if (index == _list.files.size()) {
					auto &last = _list.files.back();
					const auto was = base::take(last.caption);
					if (fieldText().empty() && !last.isSticker()) {
						_caption->setTextWithTags(was);
					}
				}
			});
		});
	}, widget->lifetime());

	block.itemReplaceRequest(
	) | rpl::on_next([=](int index) {
		showContextMenu(index, QCursor::pos());
	}, widget->lifetime());

	block.itemModifyRequest(
	) | rpl::on_next([=](int index) {
		openInPhotoEditor(index);
	}, widget->lifetime());

	block.orderUpdated() | rpl::on_next([=]{
		if (_priceTag) {
			_priceTagBg = QImage();
			_priceTag->update();
		}
	}, widget->lifetime());

	base::install_event_filter(widget, [=](
			not_null<QEvent*> e) {
		if (e->type() == QEvent::ContextMenu) {
			const auto mouse = static_cast<QContextMenuEvent*>(e.get());
			if (from >= till || from >= _list.files.size()) {
				return base::EventFilterResult::Continue;
			}
			auto fileIndex = from;
			if (const auto album = dynamic_cast<Ui::AlbumPreview*>(widget)) {
				const auto indexInBlock = album->indexFromPoint(mouse->pos());
				if (indexInBlock < 0) {
					return base::EventFilterResult::Continue;
				}
				fileIndex += indexInBlock;
			}
			if (fileIndex >= till || fileIndex >= _list.files.size()) {
				return base::EventFilterResult::Continue;
			}
			if (showContextMenu(fileIndex, mouse->globalPos())) {
				return base::EventFilterResult::Cancel;
			}
		}
		return base::EventFilterResult::Continue;
	}, widget->lifetime());
}

void SendFilesBox::refreshControls(bool initial) {
	refreshButtons();
	refreshPriceTag();
	refreshTitleText();
	updateSendWayControls();
	updateCaptionVisibility();
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
		tr::lng_send_as_documents(tr::now),
		!_sendWay.current().sendImagesAsPhotos(),
		_st.files.checkbox,
		_st.files.check);

	_sendWay.changes(
	) | rpl::on_next([=](SendFilesWay value) {
		_groupFiles->setChecked(value.groupFiles());
		_sendImagesAsPhotos->setChecked(!value.sendImagesAsPhotos());
	}, lifetime());

	_groupFiles->checkedChanges(
	) | rpl::on_next([=](bool checked) {
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
	) | rpl::on_next([=](bool checked) {
		auto sendWay = _sendWay.current();
		if (sendWay.sendImagesAsPhotos() == !checked) {
			return;
		}
		sendWay.setSendImagesAsPhotos(!checked);
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
	) | rpl::on_next([=](bool groupFiles, bool asDocuments) {
		_wayRemember->setVisible(
			(groupFiles != groupFilesFirst)
				|| ((!asDocuments) != asPhotosFirst));
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
		? tr::lng_send_as_documents(tr::now)
		: tr::lng_send_as_documents_one(tr::now));

	_hintLabel->setVisible(
		_show->session().settings().photoEditorHintShown()
			? _list.canHaveEditorHintLabel()
			: false);
}

void SendFilesBox::setupCaption() {
	const auto allow = [=](not_null<DocumentData*> emoji) {
		return Data::AllowEmojiWithoutPremium(_toPeer, emoji);
	};
	const auto show = _show;
	const auto chatStyle = InitMessageFieldHandlers({
		.session = &show->session(),
		.show = show,
		.field = _caption.data(),
		.customEmojiPaused = [=] {
			return show->paused(Window::GifPauseReason::Layer);
		},
		.allowPremiumEmoji = allow,
		.fieldStyle = &_st.files.caption,
	});
	setupCaptionAutocomplete();
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
	) | rpl::on_next([=] {
		captionResized();
	}, _caption->lifetime());
	_caption->submits(
	) | rpl::on_next([=](Qt::KeyboardModifiers modifiers) {
		const auto ctrlShiftEnter = modifiers.testFlag(Qt::ShiftModifier)
			&& (modifiers.testFlag(Qt::ControlModifier)
				|| modifiers.testFlag(Qt::MetaModifier));
		send({}, ctrlShiftEnter);
	}, _caption->lifetime());
	_caption->cancelled(
	) | rpl::on_next([=] {
		requestToTakeTextWithTags();
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

	updateCaptionVisibility();
	setupEmojiPanel();

	rpl::single(rpl::empty_value()) | rpl::then(
		_caption->changes()
	) | rpl::on_next([=] {
		checkCharsLimitation();
		refreshMessagesCount();
	}, _caption->lifetime());

	_aiButton = Ui::SetupCaptionAiButton({
		.parent = this,
		.field = _caption.data(),
		.session = &_show->session(),
		.show = _show,
		.chatStyle = chatStyle,
	});
}

void SendFilesBox::setupCaptionAutocomplete() {
	const auto parent = getDelegate()->outerContainer();
	ChatHelpers::InitFieldAutocomplete(_autocomplete, {
		.parent = parent,
		.show = _show,
		.field = _caption.data(),
		.peer = _toPeer,
		.features = [=] {
			auto result = ChatHelpers::ComposeFeatures();
			result.autocompleteCommands = false;
			result.suggestStickersByEmoji = false;
			return result;
		},
		.sendMenuDetails = _sendMenuDetails,
	});
	const auto raw = _autocomplete.get();
	const auto scheduled = std::make_shared<bool>();
	const auto recountPostponed = [=] {
		if (*scheduled) {
			return;
		}
		*scheduled = true;
		Ui::PostponeCall(raw, [=] {
			*scheduled = false;

			auto field = Ui::MapFrom(parent, this, _caption->geometry());
			_autocomplete->setBoundings(QRect(
				field.x() - _caption->x(),
				st::defaultBox.margin.top(),
				width(),
				(field.y()
					+ _st.files.caption.textMargins.top()
					+ _st.files.caption.placeholderShift
					+ _st.files.caption.placeholderFont->height
					- st::defaultBox.margin.top())));
		});
	};
	for (auto w = (QWidget*)_caption.data(); w; w = w->parentWidget()) {
		base::install_event_filter(raw, w, [=](not_null<QEvent*> e) {
			if (e->type() == QEvent::Move || e->type() == QEvent::Resize) {
				recountPostponed();
			}
			return base::EventFilterResult::Continue;
		});
		if (w == parent) {
			break;
		}
	}
}

TextWithTags SendFilesBox::fieldText() const {
	return !_caption->isHidden()
		? _caption->getTextWithAppliedMarkdown()
		: TextWithTags();
}

void SendFilesBox::checkCharsLimitation() {
	const auto limits = Data::PremiumLimits(&_show->session());
	const auto caption = fieldText();
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
			) | rpl::on_next([=] {
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
	_emojiPanel->selector()->setCurrentPeer(_toPeer);
	_emojiPanel->selector()->setAllowEmojiWithoutPremium(
		_limits & SendFilesAllow::EmojiWithoutPremium);
	_emojiPanel->selector()->emojiChosen(
	) | rpl::on_next([=](ChatHelpers::EmojiChosen data) {
		Ui::InsertEmojiAtCursor(_caption->textCursor(), data.emoji);
	}, lifetime());
	_emojiPanel->selector()->customEmojiChosen(
	) | rpl::on_next([=](ChatHelpers::FileChosen data) {
		const auto info = data.document->sticker();
		if (info
			&& info->setType == Data::StickersType::Emoji
			&& !_show->session().premium()
			&& !Data::AllowEmojiWithoutPremium(_toPeer, data.document)) {
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
	if (!_caption->isHidden()) {
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
	if (!_caption->isHidden()) {
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
		if (_aiButton) {
			Ui::UpdateCaptionAiButtonGeometry(_aiButton, _caption.data());
			_aiButton->raise();
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

void SendFilesBox::showFinished() {
	if (const auto raw = _autocomplete.get()) {
		InvokeQueued(raw, [=] {
			raw->raise();
		});
	}
}

rpl::producer<TextWithTags> SendFilesBox::takeTextWithTagsRequests() const {
	return _textWithTagsRequests.events();
}

void SendFilesBox::requestToTakeTextWithTags() const {
	if (!_caption->isHidden()) {
		_textWithTagsRequests.fire_copy(_caption->getTextWithTags());
	}
}

void SendFilesBox::setInnerFocus() {
	if (!_caption->isHidden()) {
		_caption->setFocusFast();
	} else {
		BoxContent::setInnerFocus();
	}
}

void SendFilesBox::saveSendWaySettings(bool rememberAll) {
	auto way = _sendWay.current();
	auto oldWay = Core::App().settings().sendFilesWay();
	if (!rememberAll) {
		way.setGroupFiles(oldWay.groupFiles());
		way.setSendImagesAsPhotos(oldWay.sendImagesAsPhotos());
	} else if (_groupFiles->isHidden()) {
		way.setGroupFiles(oldWay.groupFiles());
	}
	if (rememberAll
		&& (_list.overrideSendImagesAsPhotos == way.sendImagesAsPhotos()
			|| _sendImagesAsPhotos->isHidden())) {
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
	if (remove <= 0) {
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
		child.photoQuality = SendMenu::PhotoQualityState::None;
		child.price = std::nullopt;
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

	const auto way = _sendWay.current();

	for (auto &item : _list.files) {
		item.spoiler = false;
	}
	applyBlockChanges();
	for (auto &item : _list.files) {
		item.sendLargePhotos = way.sendLargePhotos();
	}

	Storage::ApplyModifications(_list);

	_confirmed = true;
	if (_confirmedCallback) {
		auto caption = fieldText();
		if (!validateLength(caption.text)) {
			return;
		}
		for (const auto &file : _list.files) {
			if (!SkipCaption(file, way)
				&& !validateLength(file.caption.text)) {
				return;
			}
		}
		saveSendWaySettings(_wayRemember && _wayRemember->checked());
		options.invertCaption = _invertCaption;
		options.price = hasPrice() ? _price.current() : 0;
		if (options.price > 0) {
			for (auto &file : _list.files) {
				file.spoiler = false;
			}
		}
		for (auto &file : _list.files) {
			if (SkipCaption(file, way)) {
				file.caption = {};
			}
		}

		Assert(_list.filesToProcess.empty());

		auto groups = DivideByGroups(
			std::move(_list),
			way,
			(_limits & SendFilesAllow::OnlyOne));
		auto bundle = PrepareFilesBundle(
			std::move(groups),
			way,
			ctrlShiftEnter);

		if (!bundle->groups.empty()) {
			auto &group = bundle->groups.back();
			auto &files = group.list.files;
			if (!files.empty()) {
				auto &captioned = (group.type == Ui::AlbumType::PhotoVideo)
					? files.front()
					: files.back();
				if (!captioned.isSticker() || way.sendImagesAsPhotos()) {
					captioned.caption = std::move(caption);
				}
			}
		}

		_confirmedCallback(std::move(bundle), options);
	}
	closeBox();
}

Fn<void(Api::SendOptions)> SendFilesBox::sendCallback() {
	return crl::guard(this, [=](Api::SendOptions options) {
		send(options, false);
	});
}

SendFilesBox::~SendFilesBox() = default;
