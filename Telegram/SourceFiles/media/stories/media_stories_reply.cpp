/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/stories/media_stories_reply.h"

#include "api/api_common.h"
#include "api/api_sending.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "base/timer_rpl.h"
#include "base/unixtime.h"
#include "boxes/premium_limits_box.h"
#include "boxes/send_files_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "chat_helpers/tabbed_selector.h"
#include "core/file_utilities.h"
#include "core/mime_type.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_document.h"
#include "data/data_message_reaction_id.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/view/controls/compose_controls_common.h"
#include "history/view/controls/history_view_compose_controls.h"
#include "history/history_item_helpers.h"
#include "history/history.h"
#include "inline_bots/inline_bot_result.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "media/stories/media_stories_controller.h"
#include "media/stories/media_stories_stealth.h"
#include "menu/menu_send.h"
#include "storage/localimageloader.h"
#include "storage/storage_account.h"
#include "storage/storage_media_prepare.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/round_rect.h"
#include "window/section_widget.h"
#include "styles/style_boxes.h" // sendMediaPreviewSize.
#include "styles/style_chat_helpers.h"
#include "styles/style_media_view.h"

namespace Media::Stories {
namespace {

[[nodiscard]] rpl::producer<QString> PlaceholderText(
		const std::shared_ptr<ChatHelpers::Show> &show) {
	return show->session().data().stories().stealthModeValue(
	) | rpl::map([](Data::StealthMode value) {
		return value.enabledTill;
	}) | rpl::distinct_until_changed() | rpl::map([](TimeId till) {
		return rpl::single(
			rpl::empty
		) | rpl::then(
			base::timer_each(250)
		) | rpl::map([=] {
			return till - base::unixtime::now();
		}) | rpl::take_while([](TimeId left) {
			return left > 0;
		}) | rpl::then(
			rpl::single(0)
		) | rpl::map([](TimeId left) {
			return left
				? tr::lng_stealth_mode_countdown(
					lt_left,
					rpl::single(TimeLeftText(left)))
				: tr::lng_story_reply_ph();
		}) | rpl::flatten_latest();
	}) | rpl::flatten_latest();
}

} // namespace

class ReplyArea::Cant final : public Ui::RpWidget {
public:
	explicit Cant(not_null<QWidget*> parent);

private:
	void paintEvent(QPaintEvent *e) override;

	Ui::RoundRect _bg;

};

ReplyArea::Cant::Cant(not_null<QWidget*> parent)
: RpWidget(parent)
, _bg(st::storiesRadius, st::storiesComposeBg) {
	show();
}

void ReplyArea::Cant::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	_bg.paint(p, rect());

	p.setPen(st::storiesComposeGrayText);
	p.setFont(st::normalFont);
	p.drawText(
		rect(),
		tr::lng_stories_cant_reply(tr::now),
		style::al_center);
}

ReplyArea::ReplyArea(not_null<Controller*> controller)
: _controller(controller)
, _controls(std::make_unique<HistoryView::ComposeControls>(
	_controller->wrap(),
	HistoryView::ComposeControlsDescriptor{
		.stOverride = &st::storiesComposeControls,
		.show = _controller->uiShow(),
		.unavailableEmojiPasted = [=](not_null<DocumentData*> emoji) {
			showPremiumToast(emoji);
		},
		.mode = HistoryView::ComposeControlsMode::Normal,
		.sendMenuType = SendMenu::Type::SilentOnly,
		.stickerOrEmojiChosen = _controller->stickerOrEmojiChosen(),
		.customPlaceholder = PlaceholderText(_controller->uiShow()),
		.voiceCustomCancelText = tr::lng_record_cancel_stories(tr::now),
		.voiceLockFromBottom = true,
		.features = {
			.likes = true,
			.sendAs = false,
			.ttlInfo = false,
			.botCommandSend = false,
			.silentBroadcastToggle = false,
			.attachBotsMenu = false,
			.inlineBots = false,
			.megagroupSet = false,
			.stickersSettings = false,
			.openStickerSets = false,
			.autocompleteHashtags = false,
			.autocompleteMentions = false,
			.autocompleteCommands = false,
		},
	}
)) {
	initGeometry();
	initActions();
	_controls->hide();
}

ReplyArea::~ReplyArea() {
}

void ReplyArea::initGeometry() {
	rpl::combine(
		_controller->layoutValue(),
		_controls->height()
	) | rpl::start_with_next([=](const Layout &layout, int height) {
		const auto content = layout.content;
		_controls->resizeToWidth(layout.controlsWidth);
		if (_controls->heightCurrent() == height) {
			const auto position = layout.controlsBottomPosition
				- QPoint(0, height);
			_controls->move(position.x(), position.y());
			const auto &tabbed = st::storiesComposeControls.tabbed;
			const auto upper = QRect(
				position.x(),
				content.y(),
				layout.controlsWidth,
				(position.y()
					+ tabbed.autocompleteBottomSkip
					- content.y()));
			_controls->setAutocompleteBoundingRect(
				layout.autocompleteRect.intersected(upper));
		}
	}, _lifetime);
}

void ReplyArea::sendReaction(const Data::ReactionId &id) {
	Expects(_data.peer != nullptr);

	auto message = Api::MessageToSend(prepareSendAction({}));
	if (const auto emoji = id.emoji(); !emoji.isEmpty()) {
		message.textWithTags = { emoji };
	} else if (const auto customId = id.custom()) {
		const auto document = _data.peer->owner().document(customId);
		if (const auto sticker = document->sticker()) {
			const auto text = sticker->alt;
			const auto id = Data::SerializeCustomEmojiId(customId);
			message.textWithTags = {
				text,
				{ { 0, int(text.size()), Ui::InputField::CustomEmojiLink(id) } }
			};
		}
	}
	if (!message.textWithTags.empty()) {
		send(std::move(message), {}, true);
	}
}

void ReplyArea::send(Api::SendOptions options) {
	const auto webPageId = _controls->webPageId();

	auto message = Api::MessageToSend(prepareSendAction(options));
	message.textWithTags = _controls->getTextWithAppliedMarkdown();
	message.webPageId = webPageId;

	send(std::move(message), options);
}

void ReplyArea::send(
		Api::MessageToSend message,
		Api::SendOptions options,
		bool skipToast) {
	const auto error = GetErrorTextForSending(
		_data.peer,
		{
			.topicRootId = MsgId(0),
			.text = &message.textWithTags,
			.ignoreSlowmodeCountdown = (options.scheduled != 0),
		});
	if (!error.isEmpty()) {
		_controller->uiShow()->showToast(error);
	}

	session().api().sendMessage(std::move(message));

	finishSending(skipToast);
	_controls->clear();
}

void ReplyArea::sendVoice(VoiceToSend &&data) {
	auto action = prepareSendAction(data.options);
	session().api().sendVoiceMessage(
		data.bytes,
		data.waveform,
		data.duration,
		std::move(action));

	_controls->clearListenState();
	finishSending();
}

bool ReplyArea::sendExistingDocument(
		not_null<DocumentData*> document,
		Api::SendOptions options,
		std::optional<MsgId> localId) {
	Expects(_data.peer != nullptr);

	const auto show = _controller->uiShow();
	const auto error = Data::RestrictionError(
		_data.peer,
		ChatRestriction::SendStickers);
	if (error) {
		show->showToast(*error);
		return false;
	} else if (Window::ShowSendPremiumError(show, document)) {
		return false;
	}

	Api::SendExistingDocument(
		Api::MessageToSend(prepareSendAction(options)),
		document,
		localId);

	_controls->cancelReplyMessage();
	finishSending();
	return true;
}

void ReplyArea::sendExistingPhoto(not_null<PhotoData*> photo) {
	sendExistingPhoto(photo, {});
}

bool ReplyArea::sendExistingPhoto(
		not_null<PhotoData*> photo,
		Api::SendOptions options) {
	Expects(_data.peer != nullptr);

	const auto show = _controller->uiShow();
	const auto error = Data::RestrictionError(
		_data.peer,
		ChatRestriction::SendPhotos);
	if (error) {
		show->showToast(*error);
		return false;
	}

	Api::SendExistingPhoto(
		Api::MessageToSend(prepareSendAction(options)),
		photo);

	_controls->cancelReplyMessage();
	finishSending();
	return true;
}

void ReplyArea::sendInlineResult(
		not_null<InlineBots::Result*> result,
		not_null<UserData*> bot) {
	const auto errorText = result->getErrorOnSend(history());
	if (!errorText.isEmpty()) {
		_controller->uiShow()->showToast(errorText);
		return;
	}
	sendInlineResult(result, bot, {}, std::nullopt);
}

void ReplyArea::sendInlineResult(
		not_null<InlineBots::Result*> result,
		not_null<UserData*> bot,
		Api::SendOptions options,
		std::optional<MsgId> localMessageId) {
	auto action = prepareSendAction(options);
	action.generateLocal = true;
	session().api().sendInlineResult(bot, result, action, localMessageId);

	auto &bots = cRefRecentInlineBots();
	const auto index = bots.indexOf(bot);
	if (index) {
		if (index > 0) {
			bots.removeAt(index);
		} else if (bots.size() >= RecentInlineBotsLimit) {
			bots.resize(RecentInlineBotsLimit - 1);
		}
		bots.push_front(bot);
		bot->session().local().writeRecentHashtagsAndBots();
	}
	finishSending();
	_controls->clear();
}

void ReplyArea::finishSending(bool skipToast) {
	_controls->hidePanelsAnimated();
	_controller->unfocusReply();
	if (!skipToast) {
		_controller->uiShow()->showToast(
			tr::lng_stories_reply_sent(tr::now));
	}
}

void ReplyArea::uploadFile(
		const QByteArray &fileContent,
		SendMediaType type) {
	session().api().sendFile(fileContent, type, prepareSendAction({}));
}

bool ReplyArea::showSendingFilesError(
		const Ui::PreparedList &list) const {
	return showSendingFilesError(list, std::nullopt);
}

bool ReplyArea::showSendingFilesError(
		const Ui::PreparedList &list,
		std::optional<bool> compress) const {
	const auto text = [&] {
		const auto peer = _data.peer;
		const auto error = Data::FileRestrictionError(peer, list, compress);
		if (error) {
			return *error;
		}
		using Error = Ui::PreparedList::Error;
		switch (list.error) {
		case Error::None: return QString();
		case Error::EmptyFile:
		case Error::Directory:
		case Error::NonLocalUrl: return tr::lng_send_image_empty(
			tr::now,
			lt_name,
			list.errorData);
		case Error::TooLargeFile: return u"(toolarge)"_q;
		}
		return tr::lng_forward_send_files_cant(tr::now);
	}();
	if (text.isEmpty()) {
		return false;
	} else if (text == u"(toolarge)"_q) {
		const auto fileSize = list.files.back().size;
		_controller->uiShow()->showBox(Box(
			FileSizeLimitBox,
			&session(),
			fileSize,
			&st::storiesComposePremium));
		return true;
	}

	_controller->uiShow()->showToast(text);
	return true;
}

not_null<History*> ReplyArea::history() const {
	Expects(_data.peer != nullptr);

	return _data.peer->owner().history(_data.peer);
}

Api::SendAction ReplyArea::prepareSendAction(
		Api::SendOptions options) const {
	Expects(_data.peer != nullptr);

	auto result = Api::SendAction(history(), options);
	result.options.sendAs = _controls->sendAsPeer();
	result.replyTo.storyId = { .peer = _data.peer->id, .story = _data.id };
	return result;
}

void ReplyArea::chooseAttach(
		std::optional<bool> overrideSendImagesAsPhotos) {
	_chooseAttachRequest = false;
	if (!_data.peer) {
		return;
	}
	const auto peer = not_null(_data.peer);
	if (const auto error = Data::AnyFileRestrictionError(peer)) {
		_controller->uiShow()->showToast(*error);
		return;
	}

	const auto filter = (overrideSendImagesAsPhotos == true)
		? FileDialog::ImagesOrAllFilter()
		: FileDialog::AllOrImagesFilter();
	const auto weak = make_weak(&_shownPeerGuard);
	const auto callback = [=](FileDialog::OpenResult &&result) {
		const auto guard = gsl::finally([&] {
			_choosingAttach = false;
		});
		if (!weak
			|| (result.paths.isEmpty() && result.remoteContent.isEmpty())) {
			return;
		} else if (!result.remoteContent.isEmpty()) {
			auto read = Images::Read({
				.content = result.remoteContent,
			});
			if (!read.image.isNull() && !read.animated) {
				confirmSendingFiles(
					std::move(read.image),
					std::move(result.remoteContent),
					overrideSendImagesAsPhotos);
			} else {
				uploadFile(result.remoteContent, SendMediaType::File);
			}
		} else {
			const auto premium = session().premium();
			auto list = Storage::PrepareMediaList(
				result.paths,
				st::sendMediaPreviewSize,
				premium);
			list.overrideSendImagesAsPhotos = overrideSendImagesAsPhotos;
			confirmSendingFiles(std::move(list));
		}
	};

	_choosingAttach = true;
	FileDialog::GetOpenPaths(
		_controller->wrap().get(),
		tr::lng_choose_files(tr::now),
		filter,
		crl::guard(this, callback),
		crl::guard(this, [=] { _choosingAttach = false; }));
}

bool ReplyArea::confirmSendingFiles(
		not_null<const QMimeData*> data,
		std::optional<bool> overrideSendImagesAsPhotos,
		const QString &insertTextOnCancel) {
	const auto hasImage = data->hasImage();
	const auto premium = session().user()->isPremium();

	if (const auto urls = Core::ReadMimeUrls(data); !urls.empty()) {
		auto list = Storage::PrepareMediaList(
			urls,
			st::sendMediaPreviewSize,
			premium);
		if (list.error != Ui::PreparedList::Error::NonLocalUrl) {
			if (list.error == Ui::PreparedList::Error::None
				|| !hasImage) {
				const auto emptyTextOnCancel = QString();
				list.overrideSendImagesAsPhotos = overrideSendImagesAsPhotos;
				confirmSendingFiles(std::move(list), emptyTextOnCancel);
				return true;
			}
		}
	}

	if (auto read = Core::ReadMimeImage(data)) {
		confirmSendingFiles(
			std::move(read.image),
			std::move(read.content),
			overrideSendImagesAsPhotos,
			insertTextOnCancel);
		return true;
	}
	return false;
}

bool ReplyArea::confirmSendingFiles(
		Ui::PreparedList &&list,
		const QString &insertTextOnCancel) {
	if (_controls->confirmMediaEdit(list)) {
		return true;
	} else if (showSendingFilesError(list)) {
		return false;
	}

	const auto show = _controller->uiShow();
	auto confirmed = [=](auto &&...args) {
		sendingFilesConfirmed(std::forward<decltype(args)>(args)...);
	};
	auto box = Box<SendFilesBox>(SendFilesBoxDescriptor{
		.show = show,
		.list = std::move(list),
		.caption = _controls->getTextWithAppliedMarkdown(),
		.limits = DefaultLimitsForPeer(_data.peer),
		.check = DefaultCheckForPeer(show, _data.peer),
		.sendType = Api::SendType::Normal,
		.sendMenuType = SendMenu::Type::SilentOnly,
		.stOverride = &st::storiesComposeControls,
		.confirmed = crl::guard(this, confirmed),
		.cancelled = _controls->restoreTextCallback(insertTextOnCancel),
	});
	if (const auto shown = show->show(std::move(box))) {
		shown->setCloseByOutsideClick(false);
	}

	return true;
}

void ReplyArea::sendingFilesConfirmed(
		Ui::PreparedList &&list,
		Ui::SendFilesWay way,
		TextWithTags &&caption,
		Api::SendOptions options,
		bool ctrlShiftEnter) {
	Expects(list.filesToProcess.empty());

	if (showSendingFilesError(list, way.sendImagesAsPhotos())) {
		return;
	}
	auto groups = DivideByGroups(
		std::move(list),
		way,
		_data.peer->slowmodeApplied());
	const auto type = way.sendImagesAsPhotos()
		? SendMediaType::Photo
		: SendMediaType::File;
	auto action = prepareSendAction(options);
	action.clearDraft = false;
	if ((groups.size() != 1 || !groups.front().sentWithCaption())
		&& !caption.text.isEmpty()) {
		auto message = Api::MessageToSend(action);
		message.textWithTags = base::take(caption);
		session().api().sendMessage(std::move(message));
	}
	for (auto &group : groups) {
		const auto album = (group.type != Ui::AlbumType::None)
			? std::make_shared<SendingAlbum>()
			: nullptr;
		session().api().sendFiles(
			std::move(group.list),
			type,
			base::take(caption),
			album,
			action);
	}
	finishSending();
}

bool ReplyArea::confirmSendingFiles(
		QImage &&image,
		QByteArray &&content,
		std::optional<bool> overrideSendImagesAsPhotos,
		const QString &insertTextOnCancel) {
	if (image.isNull()) {
		return false;
	}

	auto list = Storage::PrepareMediaFromImage(
		std::move(image),
		std::move(content),
		st::sendMediaPreviewSize);
	list.overrideSendImagesAsPhotos = overrideSendImagesAsPhotos;
	return confirmSendingFiles(std::move(list), insertTextOnCancel);
}

void ReplyArea::initActions() {
	_controls->cancelRequests(
	) | rpl::start_with_next([=] {
		_controller->unfocusReply();
	}, _lifetime);

	_controls->sendRequests(
	) | rpl::start_with_next([=](Api::SendOptions options) {
		send(options);
	}, _lifetime);

	_controls->sendVoiceRequests(
	) | rpl::start_with_next([=](VoiceToSend &&data) {
		sendVoice(std::move(data));
	}, _lifetime);

	_controls->attachRequests(
	) | rpl::filter([=] {
		return !_chooseAttachRequest;
	}) | rpl::start_with_next([=](std::optional<bool> overrideCompress) {
		_chooseAttachRequest = true;
		base::call_delayed(
			st::storiesAttach.ripple.hideDuration,
			this,
			[=] { chooseAttach(overrideCompress); });
	}, _lifetime);

	_controls->fileChosen(
	) | rpl::start_with_next([=](ChatHelpers::FileChosen data) {
		_controller->uiShow()->hideLayer();
		const auto localId = data.messageSendingFrom.localId;
		sendExistingDocument(data.document, data.options, localId);
	}, _lifetime);

	_controls->photoChosen(
	) | rpl::start_with_next([=](ChatHelpers::PhotoChosen chosen) {
		sendExistingPhoto(chosen.photo, chosen.options);
	}, _lifetime);

	_controls->inlineResultChosen(
	) | rpl::start_with_next([=](ChatHelpers::InlineChosen chosen) {
		const auto localId = chosen.messageSendingFrom.localId;
		sendInlineResult(chosen.result, chosen.bot, chosen.options, localId);
	}, _lifetime);

	_controls->likeToggled(
	) | rpl::start_with_next([=] {
		_controller->toggleLiked();
	}, _lifetime);

	_controls->setMimeDataHook([=](
			not_null<const QMimeData*> data,
			Ui::InputField::MimeAction action) {
		if (action == Ui::InputField::MimeAction::Check) {
			return Core::CanSendFiles(data);
		} else if (action == Ui::InputField::MimeAction::Insert) {
			return confirmSendingFiles(
				data,
				std::nullopt,
				Core::ReadMimeText(data));
		}
		Unexpected("action in MimeData hook.");
	});

	_controls->lockShowStarts(
	) | rpl::start_with_next([=] {
	}, _lifetime);

	_controls->show();
	_controls->finishAnimating();
	_controls->showFinished();
}

void ReplyArea::show(
		ReplyAreaData data,
		rpl::producer<Data::ReactionId> likedValue) {
	if (_data == data) {
		return;
	}
	const auto peerChanged = (_data.peer != data.peer);
	_data = data;
	if (!peerChanged) {
		if (_data.peer) {
			_controls->clear();
		}
		return;
	}
	invalidate_weak_ptrs(&_shownPeerGuard);
	const auto peer = data.peer;
	const auto history = peer ? peer->owner().history(peer).get() : nullptr;
	_controls->setHistory({
		.history = history,
		.liked = std::move(
			likedValue
		) | rpl::map([](const Data::ReactionId &id) {
			return !id.empty();
		}),
	});
	_controls->clear();
	const auto hidden = peer
		&& (!peer->isUser() || peer->isSelf() || peer->isServiceUser());
	const auto cant = !peer;
	if (!hidden && !cant) {
		_controls->show();
	} else {
		_controls->hide();
		if (cant) {
			_cant = std::make_unique<Cant>(_controller->wrap());
			_controller->layoutValue(
			) | rpl::start_with_next([=](const Layout &layout) {
				const auto height = st::storiesComposeControls.attach.height;
				const auto position = layout.controlsBottomPosition
					- QPoint(0, height);
				_cant->setGeometry(
					{ position, QSize{ layout.controlsWidth, height } });
			}, _cant->lifetime());
		} else {
			_cant = nullptr;
		}
	}
}

Main::Session &ReplyArea::session() const {
	Expects(_data.peer != nullptr);

	return _data.peer->session();
}

bool ReplyArea::focused() const {
	return _controls->focused();
}

rpl::producer<bool> ReplyArea::focusedValue() const {
	return _controls->focusedValue();
}

rpl::producer<bool> ReplyArea::hasSendTextValue() const {
	return _controls->hasSendTextValue();
}

rpl::producer<bool> ReplyArea::activeValue() const {
	using namespace rpl::mappers;
	return rpl::combine(
		_controls->focusedValue(),
		_controls->recordingActiveValue(),
		_controls->tabbedPanelShownValue(),
		_controls->fieldMenuShownValue(),
		_choosingAttach.value(),
		_1 || _2 || _3 || _4 || _5
	) | rpl::distinct_until_changed();
}

bool ReplyArea::ignoreWindowMove(QPoint position) const {
	return _controls->isRecordingPressed();
}

void ReplyArea::tryProcessKeyInput(not_null<QKeyEvent*> e) {
	_controls->tryProcessKeyInput(e);
}

not_null<Ui::RpWidget*> ReplyArea::likeAnimationTarget() const {
	return _controls->likeAnimationTarget();
}

void ReplyArea::showPremiumToast(not_null<DocumentData*> emoji) {
	// #TODO stories
}

} // namespace Media::Stories
