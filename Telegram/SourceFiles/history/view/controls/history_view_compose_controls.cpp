/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_compose_controls.h"

#include "base/call_delayed.h"
#include "base/event_filter.h"
#include "base/platform/base_platform_info.h"
#include "base/qt_signal_producer.h"
#include "base/timer_rpl.h"
#include "base/unixtime.h"
#include "boxes/edit_caption_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "chat_helpers/message_field.h"
#include "menu/menu_send.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_section.h"
#include "chat_helpers/tabbed_selector.h"
#include "chat_helpers/field_autocomplete.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/ui_integration.h"
#include "data/notify/data_notify_settings.h"
#include "data/data_changes.h"
#include "data/data_drafts.h"
#include "data/data_messages.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_file_origin.h"
#include "data/data_forum_topic.h"
#include "data/data_peer_values.h"
#include "data/data_photo_media.h"
#include "data/data_premium_limits.h" // Data::PremiumLimits.
#include "data/stickers/data_stickers.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_web_page.h"
#include "storage/storage_account.h"
#include "apiwrap.h"
#include "api/api_chat_participants.h"
#include "ui/boxes/confirm_box.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/controls/history_view_characters_limit.h"
#include "history/view/controls/history_view_compose_media_edit_manager.h"
#include "history/view/controls/history_view_forward_panel.h"
#include "history/view/controls/history_view_draft_options.h"
#include "history/view/controls/history_view_voice_record_bar.h"
#include "history/view/controls/history_view_ttl_button.h"
#include "history/view/controls/history_view_webpage_processor.h"
#include "history/view/history_view_reply.h"
#include "history/view/history_view_webpage_preview.h"
#include "inline_bots/bot_attach_web_view.h"
#include "inline_bots/inline_results_widget.h"
#include "inline_bots/inline_bot_result.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "main/session/send_as_peers.h"
#include "media/audio/media_audio_capture.h"
#include "media/audio/media_audio.h"
#include "settings/settings_premium.h"
#include "ui/item_text_options.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/ui_utility.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/popup_menu.h"
#include "ui/text/format_values.h"
#include "ui/controls/emoji_button.h"
#include "ui/controls/send_button.h"
#include "ui/controls/send_as_button.h"
#include "ui/controls/silent_toggle.h"
#include "ui/chat/choose_send_as.h"
#include "ui/effects/spoiler_mess.h"
#include "webrtc/webrtc_environment.h"
#include "window/window_adaptive.h"
#include "window/window_session_controller.h"
#include "mainwindow.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_menu_icons.h"

namespace HistoryView {
namespace {

constexpr auto kSaveDraftTimeout = crl::time(1000);
constexpr auto kSaveDraftAnywayTimeout = 5 * crl::time(1000);
constexpr auto kSaveCloudDraftIdleTimeout = 14 * crl::time(1000);
constexpr auto kMouseEvents = {
	QEvent::MouseMove,
	QEvent::MouseButtonPress,
	QEvent::MouseButtonRelease
};
constexpr auto kRefreshSlowmodeLabelTimeout = crl::time(200);

constexpr auto kCommonModifiers = 0
	| Qt::ShiftModifier
	| Qt::MetaModifier
	| Qt::ControlModifier;

using FileChosen = ComposeControls::FileChosen;
using PhotoChosen = ComposeControls::PhotoChosen;
using MessageToEdit = ComposeControls::MessageToEdit;
using VoiceToSend = ComposeControls::VoiceToSend;
using SendActionUpdate = ComposeControls::SendActionUpdate;
using SetHistoryArgs = ComposeControls::SetHistoryArgs;
using VoiceRecordBar = Controls::VoiceRecordBar;
using ForwardPanel = Controls::ForwardPanel;

} // namespace

const ChatHelpers::PauseReason kDefaultPanelsLevel
	= ChatHelpers::PauseReason::TabbedPanel;

class FieldHeader final : public Ui::RpWidget {
public:
	FieldHeader(
		QWidget *parent,
		std::shared_ptr<ChatHelpers::Show> show,
		Fn<bool()> hasSendText);

	void setHistory(const SetHistoryArgs &args);
	void updateTopicRootId(MsgId topicRootId);
	void init();

	void editMessage(FullMsgId id, bool photoEditAllowed = false);
	void replyToMessage(FullReplyTo id);
	void updateForwarding(
		Data::Thread *thread,
		Data::ResolvedForwardDraft items);
	void previewReady(rpl::producer<Controls::WebpageParsed> parsed);
	void previewUnregister();

	void mediaEditManagerApply(SendMenu::Action action);

	[[nodiscard]] bool isDisplayed() const;
	[[nodiscard]] bool isEditingMessage() const;
	[[nodiscard]] bool readyToForward() const;
	[[nodiscard]] const HistoryItemsList &forwardItems() const;
	[[nodiscard]] const Data::ResolvedForwardDraft &forwardDraft() const;
	[[nodiscard]] FullReplyTo replyingToMessage() const;
	[[nodiscard]] FullMsgId editMsgId() const;
	[[nodiscard]] rpl::producer<FullMsgId> editMsgIdValue() const;
	[[nodiscard]] rpl::producer<FullReplyTo> jumpToItemRequests() const;
	[[nodiscard]] rpl::producer<> editPhotoRequests() const;
	[[nodiscard]] rpl::producer<> editOptionsRequests() const;
	[[nodiscard]] MessageToEdit queryToEdit();
	[[nodiscard]] SendMenu::Details saveMenuDetails(bool hasSendText) const;

	[[nodiscard]] FullReplyTo getDraftReply() const;
	[[nodiscard]] rpl::producer<> editCancelled() const {
		return _editCancelled.events();
	}
	[[nodiscard]] rpl::producer<> replyCancelled() const {
		return _replyCancelled.events();
	}
	[[nodiscard]] rpl::producer<> forwardCancelled() const {
		return _forwardCancelled.events();
	}
	[[nodiscard]] rpl::producer<> previewCancelled() const {
		return _previewCancelled.events();
	}

	[[nodiscard]] rpl::producer<bool> visibleChanged();

private:
	void updateControlsGeometry(QSize size);
	void updateVisible();
	void setShownMessage(HistoryItem *message);
	void resolveMessageData();
	void updateShownMessageText();
	void customEmojiRepaint();

	void paintWebPage(Painter &p, not_null<PeerData*> peer);
	void paintEditOrReplyToMessage(Painter &p);
	void paintForwardInfo(Painter &p);

	bool hasPreview() const;

	struct Preview {
		Controls::WebpageParsed parsed;
		Ui::Text::String title;
		Ui::Text::String description;
	};

	const std::shared_ptr<ChatHelpers::Show> _show;
	const Fn<bool()> _hasSendText;

	History *_history = nullptr;
	MsgId _topicRootId = 0;

	Preview _preview;
	rpl::event_stream<> _editCancelled;
	rpl::event_stream<> _replyCancelled;
	rpl::event_stream<> _forwardCancelled;
	rpl::event_stream<> _previewCancelled;
	rpl::lifetime _previewLifetime;

	rpl::variable<FullMsgId> _editMsgId;
	rpl::variable<FullReplyTo> _replyTo;
	std::unique_ptr<ForwardPanel> _forwardPanel;
	rpl::producer<> _toForwardUpdated;

	HistoryItem *_shownMessage = nullptr;
	Ui::Text::String _shownMessageName;
	Ui::Text::String _shownMessageText;
	std::unique_ptr<Ui::SpoilerAnimation> _shownPreviewSpoiler;
	Ui::Animations::Simple _inPhotoEditOver;
	bool _shownMessageHasPreview : 1 = false;
	bool _inPhotoEdit : 1 = false;
	bool _photoEditAllowed : 1 = false;
	bool _repaintScheduled : 1 = false;
	bool _inClickable : 1 = false;

	HistoryView::MediaEditManager _mediaEditManager;

	const not_null<Data::Session*> _data;
	const not_null<Ui::IconButton*> _cancel;

	QRect _clickableRect;
	QRect _shownMessagePreviewRect;

	rpl::event_stream<bool> _visibleChanged;
	rpl::event_stream<FullReplyTo> _jumpToItemRequests;
	rpl::event_stream<> _editOptionsRequests;
	rpl::event_stream<> _editPhotoRequests;

};

FieldHeader::FieldHeader(
	QWidget *parent,
	std::shared_ptr<ChatHelpers::Show> show,
	Fn<bool()> hasSendText)
: RpWidget(parent)
, _show(std::move(show))
, _hasSendText(std::move(hasSendText))
, _forwardPanel(
	std::make_unique<ForwardPanel>([=] { customEmojiRepaint(); }))
, _data(&_show->session().data())
, _cancel(Ui::CreateChild<Ui::IconButton>(this, st::historyReplyCancel)) {
	resize(QSize(parent->width(), st::historyReplyHeight));
	init();
}

void FieldHeader::setHistory(const SetHistoryArgs &args) {
	_history = *args.history;
	_topicRootId = args.topicRootId;
}

void FieldHeader::updateTopicRootId(MsgId topicRootId) {
	_topicRootId = topicRootId;
}

void FieldHeader::init() {
	sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		updateControlsGeometry(size);
	}, lifetime());

	_forwardPanel->itemsUpdated(
	) | rpl::start_with_next([=] {
		updateVisible();
	}, lifetime());

	paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(this);
		p.setInactive(_show->paused(Window::GifPauseReason::Any));
		p.fillRect(rect(), st::historyComposeAreaBg);

		const auto position = st::historyReplyIconPosition;
		if (_preview.parsed) {
			st::historyLinkIcon.paint(p, position, width());
		} else if (isEditingMessage()) {
			st::historyEditIcon.paint(p, position, width());
		} else if (const auto reply = replyingToMessage()) {
			if (!reply.quote.empty()) {
				st::historyQuoteIcon.paint(p, position, width());
			} else {
				st::historyReplyIcon.paint(p, position, width());
			}
		} else if (readyToForward()) {
			st::historyForwardIcon.paint(p, position, width());
		}

		if (_preview.parsed) {
			paintWebPage(
				p,
				_history ? _history->peer : _data->session().user());
		} else if (isEditingMessage() || replyingToMessage()) {
			paintEditOrReplyToMessage(p);
		} else if (readyToForward()) {
			paintForwardInfo(p);
		}
	}, lifetime());

	_editMsgId.value(
	) | rpl::start_with_next([=](FullMsgId value) {
		const auto shown = value ? value : _replyTo.current().messageId;
		setShownMessage(_data->message(shown));
	}, lifetime());

	_replyTo.value(
	) | rpl::start_with_next([=](const FullReplyTo &value) {
		if (!_editMsgId.current()) {
			setShownMessage(_data->message(value.messageId));
		}
	}, lifetime());

	_data->session().changes().messageUpdates(
		Data::MessageUpdate::Flag::Edited
		| Data::MessageUpdate::Flag::Destroyed
	) | rpl::filter([=](const Data::MessageUpdate &update) {
		return (update.item == _shownMessage);
	}) | rpl::start_with_next([=](const Data::MessageUpdate &update) {
		if (update.flags & Data::MessageUpdate::Flag::Destroyed) {
			if (_editMsgId.current() == update.item->fullId()) {
				_editCancelled.fire({});
			}
			if (_replyTo.current().messageId == update.item->fullId()) {
				_replyCancelled.fire({});
			}
		} else {
			updateShownMessageText();
		}
	}, lifetime());

	_cancel->addClickHandler([=] {
		if (hasPreview()) {
			_preview = {};
			_previewCancelled.fire({});
		} else if (_editMsgId.current()) {
			_editCancelled.fire({});
		} else if (_replyTo.current()) {
			_replyCancelled.fire({});
		} else if (readyToForward()) {
			_forwardCancelled.fire({});
		}
		updateVisible();
		update();
	});

	setMouseTracking(true);
	events(
	) | rpl::filter([=](not_null<QEvent*> event) {
		const auto type = event->type();
		const auto leaving = (type == QEvent::Leave);
		return (ranges::contains(kMouseEvents, type) || leaving)
			&& (isEditingMessage()
				|| readyToForward()
				|| replyingToMessage()
				|| _preview.parsed);
	}) | rpl::start_with_next([=](not_null<QEvent*> event) {
		const auto updateOver = [&](bool inClickable, bool inPhotoEdit) {
			if (_inClickable != inClickable) {
				_inClickable = inClickable;
				setCursor(_inClickable
					? style::cur_pointer
					: style::cur_default);
			}
			if (_inPhotoEdit != inPhotoEdit) {
				_inPhotoEdit = inPhotoEdit;
				_inPhotoEditOver.start(
					[=] { update(); },
					_inPhotoEdit ? 0. : 1.,
					_inPhotoEdit ? 1. : 0.,
					st::defaultMessageBar.duration);
			}
		};
		const auto type = event->type();
		if (type == QEvent::Leave) {
			updateOver(false, false);
			return;
		}
		const auto e = static_cast<QMouseEvent*>(event.get());
		const auto pos = e->pos();
		const auto inPreviewRect = _clickableRect.contains(pos);
		const auto inPhotoEdit = _shownMessageHasPreview
			&& _photoEditAllowed
			&& _shownMessagePreviewRect.contains(pos);

		if (type == QEvent::MouseMove) {
			updateOver(inPreviewRect, inPhotoEdit);
			return;
		}
		const auto isLeftButton = (e->button() == Qt::LeftButton);
		if (type == QEvent::MouseButtonPress) {
			if (isLeftButton && inPhotoEdit) {
				_editPhotoRequests.fire({});
			} else if (isLeftButton && inPreviewRect) {
				const auto reply = replyingToMessage();
				if (_preview.parsed) {
					_editOptionsRequests.fire({});
				} else if (isEditingMessage()) {
					_jumpToItemRequests.fire(FullReplyTo{
						.messageId = _editMsgId.current()
					});
				} else if (reply && (e->modifiers() & Qt::ControlModifier)) {
					_jumpToItemRequests.fire_copy(reply);
				} else if (reply || readyToForward()) {
					_editOptionsRequests.fire({});
				}
			} else if (!isLeftButton) {
				if (inPreviewRect && isEditingMessage()) {
					_mediaEditManager.showMenu(
						this,
						[=] { update(); },
						_hasSendText());
				} else if (const auto reply = replyingToMessage()) {
					_jumpToItemRequests.fire_copy(reply);
				} else if (readyToForward()) {
					_forwardPanel->editToNextOption();
				}
			}
		}
	}, lifetime());
}

void FieldHeader::updateShownMessageText() {
	Expects(_shownMessage != nullptr);

	const auto context = Core::MarkedTextContext{
		.session = &_data->session(),
		.customEmojiRepaint = [=] { customEmojiRepaint(); },
	};
	const auto reply = replyingToMessage();
	_shownMessageText.setMarkedText(
		st::messageTextStyle,
		((isEditingMessage() || reply.quote.empty())
			? _shownMessage->inReplyText()
			: reply.quote),
		Ui::DialogTextOptions(),
		context);
}

void FieldHeader::customEmojiRepaint() {
	if (_repaintScheduled) {
		return;
	}
	_repaintScheduled = true;
	update();
}

void FieldHeader::setShownMessage(HistoryItem *item) {
	_shownMessage = item;
	if (item) {
		updateShownMessageText();
	} else {
		_shownMessageText.clear();
		resolveMessageData();
	}
	if (isEditingMessage()) {
		_shownMessageName.setText(
			st::msgNameStyle,
			tr::lng_edit_message(tr::now),
			Ui::NameTextOptions());
	} else if (item) {
		const auto context = Core::MarkedTextContext{
			.session = &_history->session(),
			.customEmojiRepaint = [] {},
			.customEmojiLoopLimit = 1,
		};
		const auto replyTo = _replyTo.current();
		const auto quote = replyTo && !replyTo.quote.empty();
		_shownMessageName.setMarkedText(
			st::fwdTextStyle,
			HistoryView::Reply::ComposePreviewName(_history, item, quote),
			Ui::NameTextOptions(),
			context);
	} else {
		_shownMessageName.clear();
	}
	updateVisible();
	update();
}

void FieldHeader::resolveMessageData() {
	const auto id = isEditingMessage()
		? _editMsgId.current()
		: _replyTo.current().messageId;
	if (!id) {
		return;
	}
	const auto peer = _data->peer(id.peer);
	const auto itemId = id.msg;
	const auto callback = crl::guard(this, [=] {
		const auto now = isEditingMessage()
			? _editMsgId.current()
			: _replyTo.current().messageId;
		if (now == id && !_shownMessage) {
			if (const auto message = _data->message(peer, itemId)) {
				setShownMessage(message);
			} else if (isEditingMessage()) {
				_editCancelled.fire({});
			} else {
				_replyCancelled.fire({});
			}
		}
	});
	_data->session().api().requestMessageData(peer, itemId, callback);
}

void FieldHeader::previewReady(
		rpl::producer<Controls::WebpageParsed> parsed) {
	_previewLifetime.destroy();

	std::move(
		parsed
	) | rpl::start_with_next([=](Controls::WebpageParsed parsed) {
		_preview.parsed = std::move(parsed);
		_preview.title.setText(
			st::msgNameStyle,
			_preview.parsed.title,
			Ui::NameTextOptions());
		_preview.description.setText(
			st::messageTextStyle,
			_preview.parsed.description,
			Ui::DialogTextOptions());
		updateVisible();
	}, _previewLifetime);
}

void FieldHeader::previewUnregister() {
	_previewLifetime.destroy();
}

void FieldHeader::mediaEditManagerApply(SendMenu::Action action) {
	_mediaEditManager.apply(action);
}

void FieldHeader::paintWebPage(Painter &p, not_null<PeerData*> context) {
	Expects(!!_preview.parsed);

	const auto textTop = st::msgReplyPadding.top();
	auto previewLeft = st::historyReplySkip;

	const QRect to(
		previewLeft,
		(st::historyReplyHeight - st::historyReplyPreview) / 2,
		st::historyReplyPreview,
		st::historyReplyPreview);
	if (_preview.parsed.drawPreview(p, to)) {
		previewLeft += st::historyReplyPreview + st::msgReplyBarSkip;
	}
	const auto elidedWidth = width()
		- previewLeft
		- _cancel->width()
		- st::msgReplyPadding.right();

	p.setPen(st::historyReplyNameFg);
	_preview.title.drawElided(
		p,
		previewLeft,
		textTop,
		elidedWidth);

	p.setPen(st::historyComposeAreaFg);
	_preview.description.drawElided(
		p,
		previewLeft,
		textTop + st::msgServiceNameFont->height,
		elidedWidth);
}

void FieldHeader::paintEditOrReplyToMessage(Painter &p) {
	_repaintScheduled = false;

	const auto replySkip = st::historyReplySkip;
	const auto availableWidth = width()
		- replySkip
		- _cancel->width()
		- st::msgReplyPadding.right();

	if (!_shownMessage) {
		p.setFont(st::msgDateFont);
		p.setPen(st::historyComposeAreaFgService);
		const auto top = (st::historyReplyHeight - st::msgDateFont->height) / 2;
		p.drawText(
			replySkip,
			top + st::msgDateFont->ascent,
			st::msgDateFont->elided(
				tr::lng_profile_loading(tr::now),
				availableWidth));
		return;
	}

	const auto media = _shownMessage->media();
	_shownMessageHasPreview = media && media->hasReplyPreview();
	const auto preview = _mediaEditManager
		? _mediaEditManager.mediaPreview()
		: _shownMessageHasPreview
		? media->replyPreview()
		: nullptr;
	const auto spoilered = _mediaEditManager.spoilered();
	if (!spoilered) {
		_shownPreviewSpoiler = nullptr;
	} else if (!_shownPreviewSpoiler) {
		_shownPreviewSpoiler = std::make_unique<Ui::SpoilerAnimation>([=] {
			update();
		});
	}
	const auto previewSkipValue = st::historyReplyPreview
		+ st::msgReplyBarSkip;
	const auto previewSkip = _shownMessageHasPreview ? previewSkipValue : 0;
	const auto textLeft = replySkip + previewSkip;
	const auto textAvailableWidth = availableWidth - previewSkip;
	if (preview) {
		const auto overEdit = _photoEditAllowed
			? _inPhotoEditOver.value(_inPhotoEdit ? 1. : 0.)
			: 0.;
		const auto to = QRect(
			replySkip,
			(st::historyReplyHeight - st::historyReplyPreview) / 2,
			st::historyReplyPreview,
			st::historyReplyPreview);
		p.drawPixmap(to.x(), to.y(), preview->pixSingle(
			preview->size() / style::DevicePixelRatio(),
			{
				.options = Images::Option::RoundSmall,
				.outer = to.size(),
			}));
		if (_shownPreviewSpoiler) {
			if (overEdit > 0.) {
				p.setOpacity(1. - overEdit);
			}
			Ui::FillSpoilerRect(
				p,
				to,
				Ui::DefaultImageSpoiler().frame(
					_shownPreviewSpoiler->index(crl::now(), p.inactive())));
		}
		if (overEdit > 0.) {
			p.setOpacity(overEdit);
			p.fillRect(to, st::historyEditMediaBg);
			st::historyEditMedia.paintInCenter(p, to);
			p.setOpacity(1.);
		}

	}

	p.setPen(st::historyReplyNameFg);
	p.setFont(st::msgServiceNameFont);
	_shownMessageName.drawElided(
		p,
		textLeft,
		st::msgReplyPadding.top(),
		textAvailableWidth);

	p.setPen(st::historyComposeAreaFg);
	_shownMessageText.draw(p, {
		.position = QPoint(
			textLeft,
			st::msgReplyPadding.top() + st::msgServiceNameFont->height),
		.availableWidth = textAvailableWidth,
		.palette = &st::historyComposeAreaPalette,
		.spoiler = Ui::Text::DefaultSpoilerCache(),
		.now = crl::now(),
		.pausedEmoji = p.inactive() || On(PowerSaving::kEmojiChat),
		.pausedSpoiler = p.inactive() || On(PowerSaving::kChatSpoiler),
		.elisionLines = 1,
	});
}

void FieldHeader::paintForwardInfo(Painter &p) {
	_repaintScheduled = false;

	const auto replySkip = st::historyReplySkip;
	const auto availableWidth = width()
		- replySkip
		- _cancel->width()
		- st::msgReplyPadding.right();
	_forwardPanel->paint(p, replySkip, 0, availableWidth, width());
}

void FieldHeader::updateVisible() {
	isDisplayed() ? show() : hide();
	_visibleChanged.fire(isVisible());
}

rpl::producer<bool> FieldHeader::visibleChanged() {
	return _visibleChanged.events();
}

bool FieldHeader::isDisplayed() const {
	return isEditingMessage()
		|| readyToForward()
		|| replyingToMessage()
		|| hasPreview();
}

bool FieldHeader::isEditingMessage() const {
	return !!_editMsgId.current();
}

FullMsgId FieldHeader::editMsgId() const {
	return _editMsgId.current();
}

bool FieldHeader::readyToForward() const {
	return !_forwardPanel->empty();
}

const HistoryItemsList &FieldHeader::forwardItems() const {
	return _forwardPanel->items();
}

const Data::ResolvedForwardDraft &FieldHeader::forwardDraft() const {
	return _forwardPanel->draft();
}

FullReplyTo FieldHeader::replyingToMessage() const {
	return _replyTo.current();
}

bool FieldHeader::hasPreview() const {
	return !!_preview.parsed;
}

FullReplyTo FieldHeader::getDraftReply() const {
	return isEditingMessage()
		? FullReplyTo{ _editMsgId.current() }
		: _replyTo.current();
}

void FieldHeader::updateControlsGeometry(QSize size) {
	_cancel->moveToRight(0, 0);
	_clickableRect = QRect(
		0,
		0,
		width() - _cancel->width(),
		height());
	_shownMessagePreviewRect = QRect(
		st::historyReplySkip,
		(st::historyReplyHeight - st::historyReplyPreview) / 2,
		st::historyReplyPreview,
		st::historyReplyPreview);
}

void FieldHeader::editMessage(FullMsgId id, bool photoEditAllowed) {
	_photoEditAllowed = photoEditAllowed;
	_editMsgId = id;
	if (!id) {
		_mediaEditManager.cancel();
	} else if (const auto item = _show->session().data().message(id)) {
		_mediaEditManager.start(item);
	}
	if (!photoEditAllowed) {
		_inPhotoEdit = false;
		_inPhotoEditOver.stop();
	}
	update();
}

void FieldHeader::replyToMessage(FullReplyTo id) {
	_replyTo = id;
}

void FieldHeader::updateForwarding(
		Data::Thread *thread,
		Data::ResolvedForwardDraft items) {
	_forwardPanel->update(thread, std::move(items));
	updateControlsGeometry(size());
}

rpl::producer<FullMsgId> FieldHeader::editMsgIdValue() const {
	return _editMsgId.value();
}

rpl::producer<FullReplyTo> FieldHeader::jumpToItemRequests() const {
	return _jumpToItemRequests.events();
}

rpl::producer<> FieldHeader::editPhotoRequests() const {
	return _editPhotoRequests.events();
}

rpl::producer<> FieldHeader::editOptionsRequests() const {
	return _editOptionsRequests.events();
}

MessageToEdit FieldHeader::queryToEdit() {
	const auto item = _data->message(_editMsgId.current());
	if (!isEditingMessage() || !item) {
		return {};
	}
	return {
		.fullId = item->fullId(),
		.options = {
			.scheduled = item->isScheduled() ? item->date() : 0,
			.shortcutId = item->shortcutId(),
			.invertCaption = _mediaEditManager.invertCaption(),
		},
		.spoilered = _mediaEditManager.spoilered(),
	};
}

SendMenu::Details FieldHeader::saveMenuDetails(bool hasSendText) const {
	return isEditingMessage()
		? _mediaEditManager.sendMenuDetails(hasSendText)
		: SendMenu::Details();
}

ComposeControls::ComposeControls(
	not_null<Ui::RpWidget*> parent,
	ComposeControlsDescriptor descriptor)
: _st(descriptor.stOverride
	? *descriptor.stOverride
	: st::defaultComposeControls)
, _features(descriptor.features)
, _parent(parent)
, _panelsParent(descriptor.panelsParent
	? descriptor.panelsParent
	: _parent.get())
, _show(std::move(descriptor.show))
, _session(&_show->session())
, _regularWindow(descriptor.regularWindow)
, _ownedSelector((_regularWindow && _features.commonTabbedPanel)
	? nullptr
	: std::make_unique<ChatHelpers::TabbedSelector>(
		_panelsParent,
		ChatHelpers::TabbedSelectorDescriptor{
			.show = _show,
			.st = _st.tabbed,
			.level = descriptor.panelsLevel,
			.mode = ChatHelpers::TabbedSelector::Mode::Full,
			.features = _features,
		}))
, _selector((_regularWindow && _features.commonTabbedPanel)
	? _regularWindow->tabbedSelector()
	: not_null(_ownedSelector.get()))
, _mode(descriptor.mode)
, _wrap(std::make_unique<Ui::RpWidget>(parent))
, _send(std::make_shared<Ui::SendButton>(_wrap.get(), _st.send))
, _like(_features.likes
	? Ui::CreateChild<Ui::IconButton>(_wrap.get(), _st.like)
	: nullptr)
, _attachToggle(Ui::CreateChild<Ui::IconButton>(_wrap.get(), _st.attach))
, _tabbedSelectorToggle(Ui::CreateChild<Ui::EmojiButton>(
	_wrap.get(),
	_st.emoji))
, _fieldCustomPlaceholder(std::move(descriptor.customPlaceholder))
, _field(
	Ui::CreateChild<Ui::InputField>(
		_wrap.get(),
		_st.field,
		Ui::InputField::Mode::MultiLine,
		(_fieldCustomPlaceholder
			? rpl::duplicate(_fieldCustomPlaceholder)
			: tr::lng_message_ph())))
, _botCommandStart(_features.botCommandSend
	? Ui::CreateChild<Ui::IconButton>(
		_wrap.get(),
		st::historyBotCommandStart)
	: nullptr)
, _header(std::make_unique<FieldHeader>(
	_wrap.get(),
	_show,
	[=] { return HasSendText(_field); }))
, _voiceRecordBar(std::make_unique<VoiceRecordBar>(
	_wrap.get(),
	Controls::VoiceRecordBarDescriptor{
		.outerContainer = parent,
		.show = _show,
		.send = _send,
		.customCancelText = descriptor.voiceCustomCancelText,
		.stOverride = &_st.record,
		.recorderHeight = st::historySendSize.height(),
		.lockFromBottom = descriptor.voiceLockFromBottom,
	}))
, _sendMenuDetails(descriptor.sendMenuDetails)
, _unavailableEmojiPasted(std::move(descriptor.unavailableEmojiPasted))
, _saveDraftTimer([=] { saveDraft(); })
, _saveCloudDraftTimer([=] { saveCloudDraft(); }) {
	if (_st.radius > 0) {
		_backgroundRect.emplace(_st.radius, _st.bg);
	}
	if (descriptor.stickerOrEmojiChosen) {
		std::move(
			descriptor.stickerOrEmojiChosen
		) | rpl::start_to_stream(_stickerOrEmojiChosen, _wrap->lifetime());
	}
	if (descriptor.scheduledToggleValue) {
		std::move(
			descriptor.scheduledToggleValue
		) | rpl::start_with_next([=](bool hasScheduled) {
			if (!_scheduled && hasScheduled) {
				_scheduled = base::make_unique_q<Ui::IconButton>(
					_wrap.get(),
					st::historyScheduledToggle);
				_scheduled->show();
				_scheduled->clicks(
				) | rpl::filter(
					rpl::mappers::_1 == Qt::LeftButton
				) | rpl::to_empty | rpl::start_to_stream(
					_showScheduledRequests,
					_scheduled->lifetime());
				orderControls(); // Raise drag areas to the top.
				updateControlsVisibility();
				updateControlsGeometry(_wrap->size());
			} else if (_scheduled && !hasScheduled) {
				_scheduled = nullptr;
			}
		}, _wrap->lifetime());
	}
	init();
}

rpl::producer<> ComposeControls::showScheduledRequests() const {
	return _showScheduledRequests.events();
}

ComposeControls::~ComposeControls() {
	saveFieldToHistoryLocalDraft();
	unregisterDraftSources();
	setTabbedPanel(nullptr);
	session().api().request(_inlineBotResolveRequestId).cancel();
}

Main::Session &ComposeControls::session() const {
	return _show->session();
}

void ComposeControls::updateTopicRootId(MsgId topicRootId) {
	_topicRootId = topicRootId;
	_header->updateTopicRootId(_topicRootId);
}

void ComposeControls::updateShortcutId(BusinessShortcutId shortcutId) {
	unregisterDraftSources();
	_shortcutId = shortcutId;
	registerDraftSource();
}

void ComposeControls::setHistory(SetHistoryArgs &&args) {
	_showSlowmodeError = std::move(args.showSlowmodeError);
	_sendActionFactory = std::move(args.sendActionFactory);
	_slowmodeSecondsLeft = rpl::single(0)
		| rpl::then(std::move(args.slowmodeSecondsLeft));
	_sendDisabledBySlowmode = rpl::single(false)
		| rpl::then(std::move(args.sendDisabledBySlowmode));
	_liked = args.liked ? std::move(args.liked) : rpl::single(false);
	_writeRestriction = rpl::single(Controls::WriteRestriction())
		| rpl::then(std::move(args.writeRestriction));
	const auto history = *args.history;
	if (_history == history) {
		return;
	}
	unregisterDraftSources();
	_history = history;
	_topicRootId = args.topicRootId;
	_historyLifetime.destroy();
	_header->setHistory(args);
	registerDraftSource();
	_selector->setCurrentPeer(history ? history->peer.get() : nullptr);
	initFieldAutocomplete();
	initWebpageProcess();
	initWriteRestriction();
	initForwardProcess();
	updateBotCommandShown();
	updateLikeShown();
	updateMessagesTTLShown();
	updateControlsGeometry(_wrap->size());
	updateControlsVisibility();
	updateFieldPlaceholder();
	updateAttachBotsMenu();

	_sendAs = nullptr;
	_silent = nullptr;
	if (!_history) {
		return;
	}
	const auto peer = _history->peer;
	initSendAsButton(peer);
	if (peer->isChat() && peer->asChat()->noParticipantInfo()) {
		session().api().requestFullPeer(peer);
	} else if (const auto channel = peer->asMegagroup()) {
		if (!channel->mgInfo->botStatus) {
			session().api().chatParticipants().requestBots(channel);
		}
	} else if (hasSilentBroadcastToggle()) {
		_silent = std::make_unique<Ui::SilentToggle>(
			_wrap.get(),
			peer->asChannel());
	}
	session().local().readDraftsWithCursors(_history);
	applyDraft();
	orderControls();
}

void ComposeControls::setCurrentDialogsEntryState(
		Dialogs::EntryState state) {
	unregisterDraftSources();
	state.currentReplyTo.topicRootId = _topicRootId;
	_currentDialogsEntryState = state;
	updateForwarding();
	registerDraftSource();
}

PeerData *ComposeControls::sendAsPeer() const {
	return (_sendAs && _history)
		? session().sendAsPeers().resolveChosen(_history->peer).get()
		: nullptr;
}

void ComposeControls::move(int x, int y) {
	_wrap->move(x, y);
	if (_writeRestricted) {
		_writeRestricted->move(x, y);
	}
}

void ComposeControls::resizeToWidth(int width) {
	_wrap->resizeToWidth(width);
	if (_writeRestricted) {
		_writeRestricted->resizeToWidth(width);
	}
	updateHeight();
}

void ComposeControls::setAutocompleteBoundingRect(QRect rect) {
	if (_autocomplete) {
		_autocomplete->setBoundings(rect);
	}
}

rpl::producer<int> ComposeControls::height() const {
	using namespace rpl::mappers;
	return rpl::conditional(
		rpl::combine(
			_writeRestriction.value(),
			_hidden.value()) | rpl::map(!_1 && !_2),
		_wrap->heightValue(),
		rpl::single(_st.attach.height));
}

int ComposeControls::heightCurrent() const {
	return (_writeRestriction.current() || _hidden.current())
		? _st.attach.height
		: _wrap->height();
}

const HistoryItemsList &ComposeControls::forwardItems() const {
	return _header->forwardItems();
}

bool ComposeControls::focus() {
	if (_wrap->isHidden() || _field->isHidden() || isRecording()) {
		return false;
	}
	_field->setFocus();
	return true;
}

bool ComposeControls::focused() const {
	return Ui::InFocusChain(_wrap.get());
}

rpl::producer<bool> ComposeControls::focusedValue() const {
	return rpl::single(focused()) | rpl::then(_field->focusedChanges());
}

rpl::producer<bool> ComposeControls::tabbedPanelShownValue() const {
	return _tabbedPanel ? _tabbedPanel->shownValue() : rpl::single(false);
}

rpl::producer<> ComposeControls::cancelRequests() const {
	return _cancelRequests.events();
}

auto ComposeControls::scrollKeyEvents() const
-> rpl::producer<not_null<QKeyEvent*>> {
	return _scrollKeyEvents.events();
}

auto ComposeControls::editLastMessageRequests() const
-> rpl::producer<not_null<QKeyEvent*>> {
	return _editLastMessageRequests.events();
}

auto ComposeControls::replyNextRequests() const
-> rpl::producer<ReplyNextRequest> {
	return _replyNextRequests.events();
}

rpl::producer<> ComposeControls::focusRequests() const {
	return _focusRequests.events();
}

auto ComposeControls::sendContentRequests(SendRequestType requestType) const {
	auto filter = rpl::filter([=] {
		const auto type = (_mode == Mode::Normal)
			? Ui::SendButton::Type::Send
			: Ui::SendButton::Type::Schedule;
		const auto sendRequestType = _voiceRecordBar->isListenState()
			? SendRequestType::Voice
			: SendRequestType::Text;
		return (_send->type() == type) && (sendRequestType == requestType);
	});
	auto map = rpl::map_to(Api::SendOptions());
	return rpl::merge(
		_send->clicks() | filter | map,
		_field->submits() | filter | map,
		_sendCustomRequests.events());
}

rpl::producer<Api::SendOptions> ComposeControls::sendRequests() const {
	return sendContentRequests(SendRequestType::Text);
}

rpl::producer<VoiceToSend> ComposeControls::sendVoiceRequests() const {
	return _voiceRecordBar->sendVoiceRequests();
}

rpl::producer<QString> ComposeControls::sendCommandRequests() const {
	return _sendCommandRequests.events();
}

rpl::producer<MessageToEdit> ComposeControls::editRequests() const {
	auto toValue = rpl::map([=] { return _header->queryToEdit(); });
	auto filter = rpl::filter([=] {
		return _send->type() == Ui::SendButton::Type::Save;
	});
	return rpl::merge(
		_send->clicks() | filter | toValue,
		_field->submits() | filter | toValue);
}

rpl::producer<std::optional<bool>> ComposeControls::attachRequests() const {
	return rpl::merge(
		_attachToggle->clicks() | rpl::map_to(std::optional<bool>()),
		_attachRequests.events()
	) | rpl::filter([=] {
		if (isEditingMessage()) {
			_show->showBox(
				Ui::MakeInformBox(tr::lng_edit_caption_attach()));
			return false;
		}
		return true;
	});
}

void ComposeControls::setMimeDataHook(MimeDataHook hook) {
	_field->setMimeDataHook(std::move(hook));
}

bool ComposeControls::confirmMediaEdit(Ui::PreparedList &list) {
	if (!isEditingMessage() || !_regularWindow) {
		return false;
	} else if (_canReplaceMedia || _canAddMedia) {
		const auto queryToEdit = _header->queryToEdit();
		EditCaptionBox::StartMediaReplace(
			_regularWindow,
			_editingId,
			std::move(list),
			_field->getTextWithTags(),
			queryToEdit.spoilered,
			queryToEdit.options.invertCaption,
			crl::guard(_wrap.get(), [=] { cancelEditMessage(); }));
	} else {
		_show->showToast(tr::lng_edit_caption_attach(tr::now));
	}
	return true;
}

rpl::producer<FileChosen> ComposeControls::fileChosen() const {
	return _fileChosen.events();
}

rpl::producer<PhotoChosen> ComposeControls::photoChosen() const {
	return _photoChosen.events();
}

auto ComposeControls::inlineResultChosen() const
-> rpl::producer<InlineChosen> {
	return _inlineResultChosen.events();
}

void ComposeControls::showStarted() {
	if (_inlineResults) {
		_inlineResults->hideFast();
	}
	if (_tabbedPanel) {
		_tabbedPanel->hideFast();
	}
	if (_attachBotsMenu) {
		_attachBotsMenu->hideFast();
	}
	_voiceRecordBar->hideFast();
	if (_autocomplete) {
		_autocomplete->hideFast();
	}
	_wrap->hide();
	if (_writeRestricted) {
		_writeRestricted->hide();
	}
}

void ComposeControls::showFinished() {
	if (_inlineResults) {
		_inlineResults->hideFast();
	}
	if (_tabbedPanel) {
		_tabbedPanel->hideFast();
	}
	if (_attachBotsMenu) {
		_attachBotsMenu->hideFast();
	}
	_voiceRecordBar->hideFast();
	if (_autocomplete) {
		_autocomplete->hideFast();
	}
	updateWrappingVisibility();
	_voiceRecordBar->orderControls();
}

void ComposeControls::raisePanels() {
	if (_autocomplete) {
		_autocomplete->raise();
	}
	if (_inlineResults) {
		_inlineResults->raise();
	}
	if (_tabbedPanel) {
		_tabbedPanel->raise();
	}
	if (_attachBotsMenu) {
		_attachBotsMenu->raise();
	}
	if (_emojiSuggestions) {
		_emojiSuggestions->raise();
	}
}

void ComposeControls::showForGrab() {
	showFinished();
}

TextWithTags ComposeControls::getTextWithAppliedMarkdown() const {
	return _field->getTextWithAppliedMarkdown();
}

void ComposeControls::clear() {
	// Otherwise cancelReplyMessage() will save the draft.
	const auto saveTextDraft = !replyingToMessage();
	setFieldText(
		{},
		saveTextDraft ? TextUpdateEvent::SaveDraft : TextUpdateEvent());
	cancelReplyMessage();
	if (_preview) {
		_preview->apply({ .removed = true });
	}
}

void ComposeControls::setText(const TextWithTags &textWithTags) {
	setFieldText(textWithTags);
}

void ComposeControls::setFieldText(
		const TextWithTags &textWithTags,
		TextUpdateEvents events,
		FieldHistoryAction fieldHistoryAction) {
	_textUpdateEvents = events;
	_field->setTextWithTags(textWithTags, fieldHistoryAction);
	auto cursor = _field->textCursor();
	cursor.movePosition(QTextCursor::End);
	_field->setTextCursor(cursor);
	_textUpdateEvents = TextUpdateEvent::SaveDraft
		| TextUpdateEvent::SendTyping;

	checkCharsLimitation();

	if (_preview) {
		_preview->checkNow(false);
	}
}

void ComposeControls::saveFieldToHistoryLocalDraft() {
	const auto key = draftKeyCurrent();
	if (!_history || !key) {
		return;
	}
	const auto id = _header->getDraftReply();
	if (_preview && (id || !_field->empty())) {
		const auto key = draftKeyCurrent();
		_history->setDraft(
			key,
			std::make_unique<Data::Draft>(_field, id, _preview->draft()));
	} else {
		_history->clearDraft(draftKeyCurrent());
	}
}

void ComposeControls::clearFieldText(
		TextUpdateEvents events,
		FieldHistoryAction fieldHistoryAction) {
	setFieldText({}, events, fieldHistoryAction);
}

void ComposeControls::hidePanelsAnimated() {
	if (_autocomplete) {
		_autocomplete->hideAnimated();
	}
	if (_tabbedPanel) {
		_tabbedPanel->hideAnimated();
	}
	if (_attachBotsMenu) {
		_attachBotsMenu->hideAnimated();
	}
	if (_inlineResults) {
		_inlineResults->hideAnimated();
	}
}

void ComposeControls::hide() {
	showStarted();
	_hidden = true;
}

void ComposeControls::show() {
	if (_hidden.current()) {
		_hidden = false;
		showFinished();
		if (_autocomplete) {
			_autocomplete->requestRefresh();
		}
	}
}

void ComposeControls::init() {
	initField();
	initTabbedSelector();
	initSendButton();
	initWriteRestriction();
	initVoiceRecordBar();
	initKeyHandler();

	_hidden.changes(
	) | rpl::start_with_next([=] {
		updateWrappingVisibility();
	}, _wrap->lifetime());

	if (_botCommandStart) {
		_botCommandStart->setClickedCallback([=] { setText({ "/" }); });
	}

	if (_like) {
		_like->setClickedCallback([=] { _likeToggled.fire({}); });
		_liked.value(
		) | rpl::start_with_next([=](bool liked) {
			const auto icon = liked ? &_st.liked : nullptr;
			_like->setIconOverride(icon, icon);
		}, _like->lifetime());
	}

	_wrap->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		updateControlsGeometry(size);
	}, _wrap->lifetime());

	_wrap->geometryValue(
	) | rpl::start_with_next([=](QRect rect) {
		updateOuterGeometry(rect);
	}, _wrap->lifetime());

	_wrap->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		auto p = QPainter(_wrap.get());
		paintBackground(p, _wrap->rect(), clip);
	}, _wrap->lifetime());

	_header->editMsgIdValue(
	) | rpl::start_with_next([=](const auto &id) {
		unregisterDraftSources();
		updateSendButtonType();
		if (_history && updateSendAsButton()) {
			updateControlsVisibility();
			updateControlsGeometry(_wrap->size());
			orderControls();
		}
		registerDraftSource();
	}, _wrap->lifetime());

	_header->editPhotoRequests(
	) | rpl::start_with_next([=] {
		const auto queryToEdit = _header->queryToEdit();
		EditCaptionBox::StartPhotoEdit(
			_regularWindow,
			_photoEditMedia,
			_editingId,
			_field->getTextWithTags(),
			queryToEdit.spoilered,
			queryToEdit.options.invertCaption,
			crl::guard(_wrap.get(), [=] { cancelEditMessage(); }));
	}, _wrap->lifetime());

	_header->editOptionsRequests(
	) | rpl::start_with_next([=] {
		const auto history = _history;
		const auto topicRootId = _topicRootId;
		const auto reply = _header->replyingToMessage();
		const auto webpage = _preview->draft();

		const auto done = [=](
				FullReplyTo replyTo,
				Data::WebPageDraft webpage,
				Data::ForwardDraft forward) {
			if (replyTo) {
				replyToMessage(replyTo);
			} else {
				cancelReplyMessage();
			}
			history->setForwardDraft(topicRootId, std::move(forward));
			_preview->apply(webpage);
			_field->setFocus();
		};
		const auto replyToId = reply.messageId;
		const auto highlight = crl::guard(_wrap.get(), [=](FullReplyTo to) {
			_jumpToItemRequests.fire_copy(to);
		});

		using namespace HistoryView::Controls;
		EditDraftOptions({
			.show = _show,
			.history = history,
			.draft = Data::Draft(_field, reply, _preview->draft()),
			.usedLink = _preview->link(),
			.forward = _header->forwardDraft(),
			.links = _preview->links(),
			.resolver = _preview->resolver(),
			.done = done,
			.highlight = highlight,
			.clearOldDraft = [=] { ClearDraftReplyTo(
				history,
				topicRootId,
				replyToId); },
		});
	}, _wrap->lifetime());

	_header->previewCancelled(
	) | rpl::start_with_next([=] {
		if (_preview) {
			_preview->apply({ .removed = true });
		}
		_saveDraftText = true;
		_saveDraftStart = crl::now();
		saveDraft();
	}, _wrap->lifetime());

	_header->editCancelled(
	) | rpl::start_with_next([=] {
		cancelEditMessage();
	}, _wrap->lifetime());

	_header->replyCancelled(
	) | rpl::start_with_next([=] {
		cancelReplyMessage();
	}, _wrap->lifetime());

	_header->forwardCancelled(
	) | rpl::start_with_next([=] {
		cancelForward();
	}, _wrap->lifetime());

	_header->visibleChanged(
	) | rpl::start_with_next([=](bool shown) {
		updateHeight();
		if (shown) {
			raisePanels();
		}
	}, _wrap->lifetime());

	sendContentRequests(
		SendRequestType::Voice
	) | rpl::start_with_next([=](Api::SendOptions options) {
		_voiceRecordBar->requestToSendWithOptions(options);
	}, _wrap->lifetime());

	_header->editMsgIdValue(
	) | rpl::start_with_next([=](const auto &id) {
		_editingId = id;
	}, _wrap->lifetime());

	session().data().itemRemoved(
	) | rpl::filter([=](not_null<const HistoryItem*> item) {
		return (_editingId == item->fullId());
	}) | rpl::start_with_next([=] {
		cancelEditMessage();
	}, _wrap->lifetime());

	Core::App().materializeLocalDraftsRequests(
	) | rpl::start_with_next([=] {
		saveFieldToHistoryLocalDraft();
	}, _wrap->lifetime());

	Core::App().settings().sendSubmitWayValue(
	) | rpl::start_with_next([=] {
		updateSubmitSettings();
	}, _wrap->lifetime());

	session().attachWebView().attachBotsUpdates(
	) | rpl::start_with_next([=] {
		updateAttachBotsMenu();
	}, _wrap->lifetime());

	orderControls();
}

void ComposeControls::orderControls() {
	_voiceRecordBar->raise();
	_send->raise();
}

bool ComposeControls::showRecordButton() const {
	return (_recordAvailability != Webrtc::RecordAvailability::None)
		&& !_voiceRecordBar->isListenState()
		&& !_voiceRecordBar->isRecordingByAnotherBar()
		&& !HasSendText(_field)
		&& !readyToForward()
		&& !isEditingMessage();
}

void ComposeControls::clearListenState() {
	_voiceRecordBar->clearListenState();
}

void ComposeControls::initKeyHandler() {
	_wrap->events(
	) | rpl::filter([=](not_null<QEvent*> event) {
		return (event->type() == QEvent::KeyPress);
	}) | rpl::start_with_next([=](not_null<QEvent*> e) {
		auto keyEvent = static_cast<QKeyEvent*>(e.get());
		const auto key = keyEvent->key();
		const auto isCtrl = keyEvent->modifiers() == Qt::ControlModifier;
		const auto hasModifiers = (Qt::NoModifier !=
			(keyEvent->modifiers()
				& ~(Qt::KeypadModifier | Qt::GroupSwitchModifier)));
		if (key == Qt::Key_O && isCtrl) {
			_attachRequests.fire({});
			return;
		}
		if (key == Qt::Key_Up && !hasModifiers) {
			if (!isEditingMessage() && _field->empty()) {
				_editLastMessageRequests.fire(std::move(keyEvent));
				return;
			}
		}
		if (!hasModifiers
			&& ((key == Qt::Key_Up)
				|| (key == Qt::Key_Down)
				|| (key == Qt::Key_PageUp)
				|| (key == Qt::Key_PageDown))) {
			_scrollKeyEvents.fire(std::move(keyEvent));
		}
	}, _wrap->lifetime());

	base::install_event_filter(_wrap.get(), _field, [=](not_null<QEvent*> e) {
		using Result = base::EventFilterResult;
		if (e->type() != QEvent::KeyPress) {
			return Result::Continue;
		}
		const auto k = static_cast<QKeyEvent*>(e.get());

		if ((k->modifiers() & kCommonModifiers) == Qt::ControlModifier) {
			const auto isUp = (k->key() == Qt::Key_Up);
			const auto isDown = (k->key() == Qt::Key_Down);
			if (isUp || isDown) {
				if (Platform::IsMac()) {
					// Cmd + Up is used instead of Home.
					if ((isUp && (!_field->textCursor().atStart()))
						// Cmd + Down is used instead of End.
						|| (isDown && (!_field->textCursor().atEnd()))) {
						return Result::Continue;
					}
				}
				_replyNextRequests.fire({
					.replyId = replyingToMessage().messageId,
					.direction = (isDown
						? ReplyNextRequest::Direction::Next
						: ReplyNextRequest::Direction::Previous)
				});
				return Result::Cancel;
			}
		} else if (k->key() == Qt::Key_Escape) {
			return Result::Cancel;
		}
		return Result::Continue;
	});
}

void ComposeControls::initField() {
	_field->setMaxHeight(st::historyComposeFieldMaxHeight);
	updateSubmitSettings();
	_field->cancelled(
	) | rpl::start_with_next([=] {
		escape();
	}, _field->lifetime());
	_field->heightChanges(
	) | rpl::start_with_next([=] {
		updateHeight();
	}, _field->lifetime());
	_field->changes(
	) | rpl::start_with_next([=] {
		fieldChanged();
	}, _field->lifetime());
#ifdef Q_OS_MAC
	// Removed an ability to insert text from the menu bar
	// when the field is hidden.
	_field->shownValue(
	) | rpl::start_with_next([=](bool shown) {
		_field->setEnabled(shown);
	}, _field->lifetime());
#endif // Q_OS_MAC
	InitMessageField(_show, _field, [=](not_null<DocumentData*> emoji) {
		if (_history
			&& Data::AllowEmojiWithoutPremium(_history->peer, emoji)) {
			return true;
		}
		if (_unavailableEmojiPasted) {
			_unavailableEmojiPasted(emoji);
		}
		return false;
	});
	InitMessageFieldFade(_field, _st.field.textBg);
	_field->setEditLinkCallback(
		DefaultEditLinkCallback(_show, _field, &_st.boxField));
	_field->setEditLanguageCallback(DefaultEditLanguageCallback(_show));

	const auto rawTextEdit = _field->rawTextEdit().get();
	rpl::merge(
		_field->scrollTop().changes() | rpl::to_empty,
		base::qt_signal_producer(
			rawTextEdit,
			&QTextEdit::cursorPositionChanged)
	) | rpl::start_with_next([=] {
		saveDraftDelayed();
	}, _field->lifetime());
}

void ComposeControls::updateSubmitSettings() {
	const auto settings = _isInlineBot
		? Ui::InputField::SubmitSettings::None
		: Core::App().settings().sendSubmitWay();
	_field->setSubmitSettings(settings);
}

void ComposeControls::initFieldAutocomplete() {
	_emojiSuggestions = nullptr;
	_autocomplete = nullptr;
	if (!_history) {
		return;
	}
	ChatHelpers::InitFieldAutocomplete(_autocomplete, {
		.parent = _parent,
		.show = _show,
		.field = _field.get(),
		.stOverride = &_st.tabbed,
		.peer = _history->peer,
		.features = [=] {
			auto result = _features;
			if (_inlineBot && !_inlineLookingUpBot) {
				result.autocompleteMentions = false;
				result.autocompleteHashtags = false;
				result.autocompleteCommands = false;
			}
			if (isEditingMessage()) {
				result.autocompleteCommands = false;
				result.suggestStickersByEmoji = false;
			}
			return result;
		},
		.sendMenuDetails = [=] { return sendMenuDetails(); },
		.stickerChoosing = [=] {
			_sendActionUpdates.fire({
				.type = Api::SendProgressType::ChooseSticker,
			});
		},
		.stickerChosen = [=](ChatHelpers::FileChosen &&data) {
			if (!_showSlowmodeError || !_showSlowmodeError()) {
				setText({});
			}
			//_saveDraftText = true;
			//_saveDraftStart = crl::now();
			//saveDraft();
			// Won't be needed if SendInlineBotResult clears the cloud draft.
			//saveCloudDraft();
			_fileChosen.fire(std::move(data));
		},
		.setText = [=](TextWithTags text) { setText(text); },
		.sendBotCommand = [=](QString command) {
			_sendCommandRequests.fire_copy(command);
		},
	});
	const auto allow = [=](not_null<DocumentData*> emoji) {
		return Data::AllowEmojiWithoutPremium(_history->peer, emoji);
	};
	_emojiSuggestions.reset(Ui::Emoji::SuggestionsController::Init(
		_panelsParent,
		_field,
		_session,
		{
			.suggestCustomEmoji = true,
			.allowCustomWithoutPremium = allow,
			.st = &_st.suggestions,
		}));
}

void ComposeControls::updateFieldPlaceholder() {
	if (!isEditingMessage() && _isInlineBot) {
		_field->setPlaceholder(
			rpl::single(_inlineBot->botInfo->inlinePlaceholder.mid(1)),
			_inlineBot->username().size() + 2);
		return;
	}

	_field->setPlaceholder([&] {
		if (_fieldCustomPlaceholder) {
			return rpl::duplicate(_fieldCustomPlaceholder);
		} else if (isEditingMessage()) {
			return tr::lng_edit_message_text();
		} else if (!_history) {
			return tr::lng_message_ph();
		} else if (const auto channel = _history->peer->asChannel()) {
			if (channel->isBroadcast()) {
				return session().data().notifySettings().silentPosts(channel)
					? tr::lng_broadcast_silent_ph()
					: tr::lng_broadcast_ph();
			} else if (channel->adminRights() & ChatAdminRight::Anonymous) {
				return tr::lng_send_anonymous_ph();
			} else {
				return tr::lng_message_ph();
			}
		} else {
			return tr::lng_message_ph();
		}
	}());
	updateSendButtonType();
}

void ComposeControls::updateSilentBroadcast() {
	if (!_silent || !_history) {
		return;
	}
	const auto &peer = _history->peer;
	if (!session().data().notifySettings().silentPostsUnknown(peer)) {
		_silent->setChecked(
			session().data().notifySettings().silentPosts(peer));
		updateFieldPlaceholder();
	}
}

void ComposeControls::fieldChanged() {
	const auto typing = (!_inlineBot
		&& !_header->isEditingMessage()
		&& (_textUpdateEvents & TextUpdateEvent::SendTyping));
	updateSendButtonType();
	_hasSendText = HasSendText(_field);
	if (updateBotCommandShown() || updateLikeShown()) {
		updateControlsVisibility();
		updateControlsGeometry(_wrap->size());
	}
	InvokeQueued(_field.get(), [=] {
		updateInlineBotQuery();
		if ((!_autocomplete || !_autocomplete->stickersEmoji()) && typing) {
			_sendActionUpdates.fire({ Api::SendProgressType::Typing });
		}
	});

	checkCharsLimitation();

	_saveCloudDraftTimer.cancel();
	if (!(_textUpdateEvents & TextUpdateEvent::SaveDraft)) {
		return;
	}
	_saveDraftText = true;
	saveDraft(true);
}

void ComposeControls::saveDraftDelayed() {
	if (!(_textUpdateEvents & TextUpdateEvent::SaveDraft)) {
		return;
	}
	saveDraft(true);
}

Data::DraftKey ComposeControls::draftKey(DraftType type) const {
	using Section = Dialogs::EntryState::Section;
	using Key = Data::DraftKey;

	switch (_currentDialogsEntryState.section) {
	case Section::History:
	case Section::Replies:
		return (type == DraftType::Edit)
			? Key::LocalEdit(_topicRootId)
			: Key::Local(_topicRootId);
	case Section::Scheduled:
		return (type == DraftType::Edit)
			? Key::ScheduledEdit()
			: Key::Scheduled();
	case Section::ShortcutMessages:
		return (type == DraftType::Edit)
			? Key::ShortcutEdit(_shortcutId)
			: Key::Shortcut(_shortcutId);
	}
	return Key::None();
}

Data::DraftKey ComposeControls::draftKeyCurrent() const {
	return draftKey(isEditingMessage() ? DraftType::Edit : DraftType::Normal);
}

void ComposeControls::saveDraft(bool delayed) {
	if (delayed) {
		const auto now = crl::now();
		if (!_saveDraftStart) {
			_saveDraftStart = now;
			return _saveDraftTimer.callOnce(kSaveDraftTimeout);
		} else if (now - _saveDraftStart < kSaveDraftAnywayTimeout) {
			return _saveDraftTimer.callOnce(kSaveDraftTimeout);
		}
	}
	writeDrafts();
}

void ComposeControls::saveCloudDraft() {
	session().api().saveCurrentDraftToCloud();
}

void ComposeControls::writeDraftTexts() {
	Expects(_history != nullptr);

	session().local().writeDrafts(_history);
}

void ComposeControls::writeDraftCursors() {
	Expects(_history != nullptr);

	session().local().writeDraftCursors(_history);
}

void ComposeControls::unregisterDraftSources() {
	if (!_history) {
		return;
	}
	const auto normal = draftKey(DraftType::Normal);
	const auto edit = draftKey(DraftType::Edit);
	if (normal != Data::DraftKey::None()) {
		session().local().unregisterDraftSource(_history, normal);
	}
	if (edit != Data::DraftKey::None()) {
		session().local().unregisterDraftSource(_history, edit);
	}
}

void ComposeControls::registerDraftSource() {
	if (!_history || !_preview) {
		return;
	}
	const auto key = draftKeyCurrent();
	if (key != Data::DraftKey::None()) {
		const auto draft = [=] {
			return Storage::MessageDraft{
				_header->getDraftReply(),
				_field->getTextWithTags(),
				_preview->draft(),
			};
		};
		auto draftSource = Storage::MessageDraftSource{
			.draft = draft,
			.cursor = [=] { return MessageCursor(_field); },
		};
		session().local().registerDraftSource(
			_history,
			key,
			std::move(draftSource));
	}
}

void ComposeControls::writeDrafts() {
	const auto save = (_history != nullptr)
		&& (_saveDraftStart > 0)
		&& (draftKeyCurrent() != Data::DraftKey::None());
	_saveDraftStart = 0;
	_saveDraftTimer.cancel();
	if (save) {
		if (_saveDraftText) {
			writeDraftTexts();
		}
		writeDraftCursors();
	}
	_saveDraftText = false;

	if (!isEditingMessage() && !_inlineBot) {
		_saveCloudDraftTimer.callOnce(kSaveCloudDraftIdleTimeout);
	}
}

void ComposeControls::applyCloudDraft() {
	if (!isEditingMessage()) {
		applyDraft(Ui::InputField::HistoryAction::NewEntry);
	}
}

void ComposeControls::applyDraft(FieldHistoryAction fieldHistoryAction) {
	Expects(_history != nullptr);

	const auto editDraft = _history->draft(draftKey(DraftType::Edit));
	const auto draft = editDraft
		? editDraft
		: _history->draft(draftKey(DraftType::Normal));
	const auto editingId = (draft && draft == editDraft)
		? draft->reply.messageId
		: FullMsgId();

	InvokeQueued(_autocomplete.get(), [=] {
		if (_autocomplete) {
			_autocomplete->requestStickersUpdate();
		}
	});
	const auto guard = gsl::finally([&] {
		updateSendButtonType();
		updateReplaceMediaButton();
		updateFieldPlaceholder();
		updateControlsVisibility();
		updateControlsGeometry(_wrap->size());
	});

	const auto hadFocus = Ui::InFocusChain(_field);
	if (!draft) {
		clearFieldText(0, fieldHistoryAction);
		if (hadFocus) {
			_field->setFocus();
		}
		_header->editMessage({});
		_header->replyToMessage({});
		if (_preview) {
			_preview->apply({ .removed = true });
			_preview->setDisabled(false);
		}
		_canReplaceMedia = _canAddMedia = false;
		_photoEditMedia = nullptr;
		return;
	}

	_textUpdateEvents = 0;
	setFieldText(draft->textWithTags, 0, fieldHistoryAction);
	if (hadFocus) {
		_field->setFocus();
	}
	draft->cursor.applyTo(_field);
	_textUpdateEvents = TextUpdateEvent::SaveDraft | TextUpdateEvent::SendTyping;
	if (_preview) {
		_preview->apply(draft->webpage, draft != editDraft);
	}

	if (draft == editDraft) {
		const auto resolve = [=] {
			if (const auto item = _history->owner().message(editingId)) {
				const auto media = item->media();
				_canReplaceMedia = item->allowsEditMedia();
				if (media && media->allowsEditMedia()) {
					_canAddMedia = false;
				} else {
					_canAddMedia = base::take(_canReplaceMedia);
				}
				if (_canReplaceMedia || _canAddMedia) {
					// Invalidate the button, maybe icon has changed.
					_replaceMedia = nullptr;
				}
				_photoEditMedia = (_canReplaceMedia
					&& _regularWindow
					&& media->photo()
					&& !media->photo()->isNull())
					? media->photo()->createMediaView()
					: nullptr;
				if (_photoEditMedia) {
					_photoEditMedia->wanted(
						Data::PhotoSize::Large,
						item->fullId());
				}
				_header->editMessage(editingId, _photoEditMedia != nullptr);
				if (_preview) {
					_preview->apply(
						Data::WebPageDraft::FromItem(item),
						false);
					_preview->setDisabled(media && !media->webpage());
				}
				return true;
			}
			_canReplaceMedia = _canAddMedia = false;
			_photoEditMedia = nullptr;
			_header->editMessage(editingId, false);
			return false;
		};
		if (!resolve()) {
			const auto callback = crl::guard(_header.get(), [=] {
				if (_header->editMsgId() == editingId
					&& resolve()
					&& updateReplaceMediaButton()) {
					updateControlsVisibility();
					updateControlsGeometry(_wrap->size());
				}
			});
			_history->session().api().requestMessageData(
				_history->peer,
				editingId.msg,
				callback);
		}
		_header->replyToMessage({});
	} else {
		_canReplaceMedia = _canAddMedia = false;
		_photoEditMedia = nullptr;
		_header->replyToMessage(draft->reply);
		_header->editMessage({});
		if (_preview) {
			_preview->setDisabled(false);
		}
	}
	checkCharsLimitation();
}

void ComposeControls::cancelForward() {
	_history->setForwardDraft(_topicRootId, {});
	updateForwarding();
}

rpl::producer<SendActionUpdate> ComposeControls::sendActionUpdates() const {
	return rpl::merge(
		_sendActionUpdates.events(),
		_voiceRecordBar->sendActionUpdates());
}

void ComposeControls::initTabbedSelector() {
	if (!_regularWindow
		|| !_features.commonTabbedPanel
		|| _regularWindow->hasTabbedSelectorOwnership()) {
		createTabbedPanel();
	} else {
		setTabbedPanel(nullptr);
	}

	_tabbedSelectorToggle->addClickHandler([=] {
		if (_tabbedPanel && _tabbedPanel->isHidden()) {
			_tabbedPanel->showAnimated();
		} else {
			toggleTabbedSelectorMode();
		}
	});

	const auto wrap = _wrap.get();

	base::install_event_filter(wrap, _selector, [=](not_null<QEvent*> e) {
		if (_tabbedPanel && e->type() == QEvent::ParentChange) {
			setTabbedPanel(nullptr);
		}
		return base::EventFilterResult::Continue;
	});

	_selector->emojiChosen(
	) | rpl::start_with_next([=](ChatHelpers::EmojiChosen data) {
		Ui::InsertEmojiAtCursor(_field->textCursor(), data.emoji);
	}, wrap->lifetime());

	rpl::merge(
		_selector->fileChosen(),
		_selector->customEmojiChosen(),
		_stickerOrEmojiChosen.events()
	) | rpl::start_with_next([=](ChatHelpers::FileChosen &&data) {
		if (const auto info = data.document->sticker()
			; info && info->setType == Data::StickersType::Emoji) {
			if (data.document->isPremiumEmoji()
				&& !session().premium()
				&& (!_history
					|| !Data::AllowEmojiWithoutPremium(
						_history->peer,
						data.document))) {
				if (_unavailableEmojiPasted) {
					_unavailableEmojiPasted(data.document);
				}
			} else {
				Data::InsertCustomEmoji(_field, data.document);
			}
		} else {
			_fileChosen.fire(std::move(data));
		}
	}, wrap->lifetime());

	_selector->photoChosen(
	) | rpl::start_to_stream(_photoChosen, wrap->lifetime());

	_selector->inlineResultChosen(
	) | rpl::start_to_stream(_inlineResultChosen, wrap->lifetime());

	_selector->contextMenuRequested(
	) | rpl::start_with_next([=] {
		_selector->showMenuWithDetails(sendMenuDetails());
	}, wrap->lifetime());

	_selector->choosingStickerUpdated(
	) | rpl::start_with_next([=](ChatHelpers::TabbedSelector::Action action) {
		_sendActionUpdates.fire({
			.type = Api::SendProgressType::ChooseSticker,
			.cancel = (action == ChatHelpers::TabbedSelector::Action::Cancel),
		});
	}, wrap->lifetime());
}

void ComposeControls::initSendButton() {
	rpl::combine(
		_slowmodeSecondsLeft.value(),
		_sendDisabledBySlowmode.value()
	) | rpl::start_with_next([=] {
		updateSendButtonType();
	}, _send->lifetime());

	_send->finishAnimating();

	_send->clicks(
	) | rpl::filter([=] {
		return (_send->type() == Ui::SendButton::Type::Cancel);
	}) | rpl::start_with_next([=] {
		cancelInlineBot();
	}, _send->lifetime());

	const auto send = crl::guard(_send.get(), [=](Api::SendOptions options) {
		_sendCustomRequests.fire(std::move(options));
	});

	using namespace SendMenu;
	const auto sendAction = [=](Action action, Details details) {
		if (action.type == ActionType::CaptionUp
			|| action.type == ActionType::CaptionDown
			|| action.type == ActionType::SpoilerOn
			|| action.type == ActionType::SpoilerOff) {
			_header->mediaEditManagerApply(action);
		} else {
			SendMenu::DefaultCallback(_show, send)(action, details);
		}
	};

	SendMenu::SetupMenuAndShortcuts(
		_send.get(),
		_show,
		[=] { return sendButtonMenuDetails(); },
		sendAction);

	Core::App().mediaDevices().recordAvailabilityValue(
	) | rpl::start_with_next([=](Webrtc::RecordAvailability value) {
		_recordAvailability = value;
		updateSendButtonType();
	}, _send->lifetime());
}

void ComposeControls::initSendAsButton(not_null<PeerData*> peer) {
	using namespace rpl::mappers;

	// SendAsPeers::shouldChoose checks Data::CanSendAnything(PeerData*).
	rpl::combine(
		rpl::single(peer) | rpl::then(
			session().sendAsPeers().updated() | rpl::filter(_1 == peer)
		),
		Data::CanSendAnythingValue(peer, false)
	) | rpl::skip(1) | rpl::start_with_next([=] {
		if (updateSendAsButton()) {
			updateControlsVisibility();
			updateControlsGeometry(_wrap->size());
			orderControls();
		}
	}, _historyLifetime);

	updateSendAsButton();
}

void ComposeControls::cancelInlineBot() {
	const auto &textWithTags = _field->getTextWithTags();
	if (textWithTags.text.size() > _inlineBotUsername.size() + 2) {
		setFieldText(
			{ '@' + _inlineBotUsername + ' ', TextWithTags::Tags() },
			TextUpdateEvent::SaveDraft,
			Ui::InputField::HistoryAction::NewEntry);
	} else {
		clearFieldText(
			TextUpdateEvent::SaveDraft,
			Ui::InputField::HistoryAction::NewEntry);
	}
}

void ComposeControls::clearInlineBot() {
	if (_inlineBot || _inlineLookingUpBot) {
		_inlineBot = nullptr;
		_inlineLookingUpBot = false;
		inlineBotChanged();
		_field->finishAnimating();
	}
	if (_inlineResults) {
		_inlineResults->clearInlineBot();
	}
	if (_autocomplete) {
		_autocomplete->requestRefresh();
	}
}

void ComposeControls::inlineBotChanged() {
	const auto isInlineBot = (_inlineBot && !_inlineLookingUpBot);
	if (_isInlineBot != isInlineBot) {
		_isInlineBot = isInlineBot;
		updateFieldPlaceholder();
		updateSubmitSettings();
		if (_autocomplete) {
			_autocomplete->requestRefresh();
		}
	}
}

void SetupRestrictionView(
		not_null<Ui::RpWidget*> widget,
		not_null<const style::ComposeControls*> st,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<PeerData*> peer,
		rpl::producer<Controls::WriteRestriction> restriction,
		Fn<void(QPainter &p, QRect clip)> paintBackground) {
	struct State {
		std::unique_ptr<Ui::FlatLabel> label;
		std::unique_ptr<Ui::AbstractButton> button;
		std::unique_ptr<Ui::RoundButton> unlock;
		std::unique_ptr<Ui::RpWidget> icon;
		Fn<void()> updateGeometries;
	};
	const auto state = widget->lifetime().make_state<State>();
	state->updateGeometries = [=] {
		if (!state->label && state->button) {
			state->button->setGeometry(widget->rect());
		} else if (!state->label) {
			return;
		} else if (state->button) {
			const auto available = widget->width()
				- st->like.width
				- st::historySendRight
				- state->unlock->width()
				- st->premiumRequired.buttonSkip
				- st->premiumRequired.position.x();
			state->label->resizeToWidth(available);
			state->label->moveToLeft(
				st->premiumRequired.position.x(),
				st->premiumRequired.position.y(),
				widget->width());
			const auto left = st->premiumRequired.position.x()
				+ std::min(available, state->label->textMaxWidth())
				+ st->premiumRequired.buttonSkip;
			state->unlock->moveToLeft(
				left,
				st->premiumRequired.buttonTop,
				widget->width());
			state->button->setGeometry(QRect(
				QPoint(),
				QSize(left + state->unlock->width(), widget->height())));
			state->icon->moveToLeft(0, 0, widget->width());
		} else {
			const auto left = st::historySendRight;
			state->label->resizeToWidth(widget->width() - 2 * left);
			state->label->moveToLeft(
				left,
				(widget->height() - state->label->height()) / 2,
				widget->width());
		}
	};
	const auto makeLabel = [=](
			const QString &text,
			const style::FlatLabel &st) {
		auto label = std::make_unique<Ui::FlatLabel>(widget, text, st);
		label->show();
		label->setAttribute(Qt::WA_TransparentForMouseEvents);
		label->heightValue(
		) | rpl::start_with_next(
			state->updateGeometries,
			label->lifetime());
		return label;
	};
	const auto makeUnlock = [=](const QString &text, const QString &name) {
		using namespace Ui;
		auto unlock = std::make_unique<RoundButton>(
			widget,
			rpl::single(text),
			st->premiumRequired.button);
		unlock->show();
		unlock->setAttribute(Qt::WA_TransparentForMouseEvents);
		unlock->setTextTransform(RoundButton::TextTransform::NoTransform);
		unlock->setFullRadius(true);
		return unlock;
	};
	const auto makeIcon = [=] {
		auto icon = std::make_unique<Ui::RpWidget>(widget);
		icon->resize(st->premiumRequired.icon.size());
		icon->show();
		icon->paintRequest() | rpl::start_with_next([st, raw = icon.get()] {
			auto p = QPainter(raw);
			st->premiumRequired.icon.paint(p, {}, raw->width());
		}, icon->lifetime());
		return icon;
	};
	std::move(
		restriction
	) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](Controls::WriteRestriction value) {
		using Type = Controls::WriteRestriction::Type;
		if (const auto lifting = value.boostsToLift) {
			state->button = std::make_unique<Ui::FlatButton>(
				widget,
				tr::lng_restricted_boost_group(tr::now),
				st::historyComposeButton);
			state->button->setClickedCallback([=] {
				const auto window = show->resolveWindow();
				window->resolveBoostState(peer->asChannel(), lifting);
			});
		} else if (value.type == Type::Rights) {
			state->icon = nullptr;
			state->unlock = nullptr;
			state->button = nullptr;
			state->label = makeLabel(value.text, st->restrictionLabel);
		} else if (value.type == Type::PremiumRequired) {
			state->icon = makeIcon();
			state->unlock = makeUnlock(value.button, peer->shortName());
			state->button = std::make_unique<Ui::AbstractButton>(widget);
			state->button->setClickedCallback([=] {
				::Settings::ShowPremiumPromoToast(
					show,
					tr::lng_send_non_premium_message_toast(
						tr::now,
						lt_user,
						TextWithEntities{ peer->shortName() },
						lt_link,
						Ui::Text::Link(
							Ui::Text::Bold(
								tr::lng_send_non_premium_message_toast_link(
									tr::now))),
						Ui::Text::RichLangValue),
					u"require_premium"_q);
			});
			state->label = makeLabel(value.text, st->premiumRequired.label);
		}
		state->updateGeometries();
	}, widget->lifetime());

	widget->sizeValue(
	) | rpl::start_with_next(state->updateGeometries, widget->lifetime());

	widget->paintRequest() | rpl::start_with_next([=](QRect clip) {
		auto p = QPainter(widget);
		paintBackground(p, clip);
	}, widget->lifetime());
}

void ComposeControls::initWriteRestriction() {
	if (!_history) {
		const auto was = base::take(_writeRestricted);
		updateWrappingVisibility();
		return;
	}
	if (_like && _like->parentWidget() == _writeRestricted.get()) {
		// Fix a crash because of _like destruction with its parent.
		_like->setParent(_wrap.get());
	}
	_writeRestricted = std::make_unique<Ui::RpWidget>(_parent);
	_writeRestricted->move(_wrap->pos());
	_writeRestricted->resizeToWidth(_wrap->widthNoMargins());
	_writeRestricted->sizeValue() | rpl::start_with_next([=] {
		if (_like && _like->parentWidget() == _writeRestricted.get()) {
			updateControlsGeometry(_wrap->size());
		}
	}, _writeRestricted->lifetime());
	_writeRestricted->resize(
		_writeRestricted->width(),
		st::historyUnblock.height);
	const auto background = [=](QPainter &p, QRect clip) {
		paintBackground(p, _writeRestricted->rect(), clip);
	};
	SetupRestrictionView(
		_writeRestricted.get(),
		&_st,
		_show,
		_history->peer,
		_writeRestriction.value(),
		background);

	_writeRestriction.value(
	) | rpl::start_with_next([=] {
		updateWrappingVisibility();
	}, _writeRestricted->lifetime());
}

void ComposeControls::changeFocusedControl() {
	_focusRequests.fire({});
	if (_regularWindow) {
		_regularWindow->widget()->setInnerFocus();
	}
}

void ComposeControls::initVoiceRecordBar() {
	_voiceRecordBar->recordingStateChanges(
	) | rpl::start_with_next([=](bool active) {
		if (active) {
			_recording = true;
			changeFocusedControl();
		}
		_field->setVisible(!active);
		if (!active) {
			changeFocusedControl();
			_recording = false;
		}
	}, _wrap->lifetime());

	_voiceRecordBar->setStartRecordingFilter([=] {
		const auto error = [&]() -> Data::SendError {
			const auto peer = _history ? _history->peer.get() : nullptr;
			if (peer) {
				if (const auto error = Data::RestrictionError(
						peer,
						ChatRestriction::SendVoiceMessages)) {
					return error;
				}
			}
			return {};
		}();
		if (error) {
			Data::ShowSendErrorToast(_show, _history->peer, error);
			return true;
		} else if (_showSlowmodeError && _showSlowmodeError()) {
			return true;
		}
		return false;
	});

	_voiceRecordBar->recordingTipRequests(
	) | rpl::start_with_next([=] {
		Core::App().settings().setRecordVideoMessages(
			!Core::App().settings().recordVideoMessages());
		updateSendButtonType();
		switch (_send->type()) {
		case Ui::SendButton::Type::Record: {
			const auto both = Webrtc::RecordAvailability::VideoAndAudio;
			_show->showToast((_recordAvailability == both)
				? tr::lng_record_voice_tip(tr::now)
				: tr::lng_record_hold_tip(tr::now));
		} break;
		case Ui::SendButton::Type::Round:
			_show->showToast(tr::lng_record_video_tip(tr::now));
			break;
		}
	}, _wrap->lifetime());

	_voiceRecordBar->errors(
	) | rpl::start_with_next([=](::Media::Capture::Error error) {
		using Error = ::Media::Capture::Error;
		switch (error) {
		case Error::AudioInit:
		case Error::AudioTimeout:
			_show->showToast(tr::lng_record_audio_problem(tr::now));
			break;
		case Error::VideoInit:
		case Error::VideoTimeout:
			_show->showToast(tr::lng_record_video_problem(tr::now));
			break;
		default:
			_show->showToast(u"Unknown error."_q);
			break;
		}
	}, _wrap->lifetime());

	_voiceRecordBar->updateSendButtonTypeRequests(
	) | rpl::start_with_next([=] {
		updateSendButtonType();
	}, _wrap->lifetime());
}

void ComposeControls::updateWrappingVisibility() {
	const auto hidden = _hidden.current();
	const auto &restriction = _writeRestriction.current();
	const auto restricted = !restriction.empty() && _writeRestricted;
	if (_writeRestricted) {
		_writeRestricted->setVisible(!hidden && restricted);
	}
	_wrap->setVisible(!hidden && !restricted);
	using namespace Controls;
	if (_like) {
		const auto hidden = _like->isHidden();
		if (_writeRestricted
			&& restriction.type == WriteRestrictionType::PremiumRequired) {
			_like->setParent(_writeRestricted.get());
		} else {
			_like->setParent(_wrap.get());
		}
		if (!hidden) {
			_like->show();
			updateControlsGeometry(_wrap->size());
		}
	}
	if (!hidden && !restricted) {
		_wrap->raise();
	}
}

auto ComposeControls::computeSendButtonType() const {
	using Type = Ui::SendButton::Type;

	if (_header->isEditingMessage()) {
		return Type::Save;
	} else if (_isInlineBot) {
		return Type::Cancel;
	} else if (showRecordButton()) {
		const auto both = Webrtc::RecordAvailability::VideoAndAudio;
		const auto video = Core::App().settings().recordVideoMessages();
		return (video && _recordAvailability == both)
			? Type::Round
			: Type::Record;
	}
	return (_mode == Mode::Normal) ? Type::Send : Type::Schedule;
}

SendMenu::Details ComposeControls::sendMenuDetails() const {
	return !_history ? SendMenu::Details() : _sendMenuDetails();
}

SendMenu::Details ComposeControls::saveMenuDetails() const {
	return _header->saveMenuDetails(HasSendText(_field));
}

SendMenu::Details ComposeControls::sendButtonMenuDetails() const {
	return (computeSendButtonType() == Ui::SendButton::Type::Save)
		? saveMenuDetails()
		: (computeSendButtonType() == Ui::SendButton::Type::Send)
		? sendMenuDetails()
		: SendMenu::Details();
}

void ComposeControls::updateSendButtonType() {
	using Type = Ui::SendButton::Type;
	const auto type = computeSendButtonType();
	_send->setType(type);

	const auto delay = [&] {
		return (type != Type::Cancel && type != Type::Save)
			? _slowmodeSecondsLeft.current()
			: 0;
	}();
	_send->setSlowmodeDelay(delay);
	_send->setDisabled(_sendDisabledBySlowmode.current()
		&& (type == Type::Send
			|| type == Type::Record
			|| type == Type::Round));
}

void ComposeControls::finishAnimating() {
	_send->finishAnimating();
	_voiceRecordBar->finishAnimating();
}

void ComposeControls::updateControlsGeometry(QSize size) {
	// (_attachToggle|_replaceMedia) (_sendAs) -- _inlineResults ------ _tabbedPanel -- _fieldBarCancel
	// (_attachDocument|_attachPhoto) _field (_ttlInfo) (_scheduled) (_silent|_botCommandStart) _tabbedSelectorToggle _send

	const auto fieldWidth = size.width()
		- _attachToggle->width()
		- (_sendAs ? _sendAs->width() : 0)
		- st::historySendRight
		- _send->width()
		- _tabbedSelectorToggle->width()
		- (_likeShown ? _like->width() : 0)
		- (_botCommandShown ? _botCommandStart->width() : 0)
		- (_silent ? _silent->width() : 0)
		- (_scheduled ? _scheduled->width() : 0)
		- (_ttlInfo ? _ttlInfo->width() : 0);
	{
		const auto oldFieldHeight = _field->height();
		_field->resizeToWidth(fieldWidth);
		// If a height of the field is changed
		// then this method will be called with the updated size.
		if (oldFieldHeight != _field->height()) {
			return;
		}
	}

	const auto buttonsTop = size.height() - _attachToggle->height();

	auto left = st::historySendRight;
	if (_replaceMedia) {
		_replaceMedia->moveToLeft(left, buttonsTop);
	}
	_attachToggle->moveToLeft(left, buttonsTop);
	left += _attachToggle->width();
	if (_sendAs) {
		_sendAs->moveToLeft(left, buttonsTop);
		left += _sendAs->width();
	}
	_field->moveToLeft(
		left,
		size.height() - _field->height() - st::historySendPadding);

	_header->resizeToWidth(size.width());
	_header->moveToLeft(
		0,
		_field->y() - _header->height() - st::historySendPadding);

	auto right = st::historySendRight;
	_send->moveToRight(right, buttonsTop);
	right += _send->width();
	_tabbedSelectorToggle->moveToRight(right, buttonsTop);
	right += _tabbedSelectorToggle->width();
	if (_like) {
		using Type = Controls::WriteRestrictionType;
		if (_writeRestriction.current().type == Type::PremiumRequired) {
			_like->moveToRight(st::historySendRight, 0);
		} else {
			_like->moveToRight(right, buttonsTop);
			if (_likeShown) {
				right += _like->width();
			}
		}
	}
	if (_botCommandStart) {
		_botCommandStart->moveToRight(right, buttonsTop);
		if (_botCommandShown) {
			right += _botCommandStart->width();
		}
	}
	if (_silent) {
		_silent->moveToRight(right, buttonsTop);
		right += _silent->width();
	}
	if (_scheduled) {
		_scheduled->moveToRight(right, buttonsTop);
		right += _scheduled->width();
	}
	if (_ttlInfo) {
		_ttlInfo->move(size.width() - right - _ttlInfo->width(), buttonsTop);
	}

	_voiceRecordBar->resizeToWidth(size.width());
	_voiceRecordBar->moveToLeft(
		0,
		size.height() - _voiceRecordBar->height());
}

void ComposeControls::updateControlsVisibility() {
	if (_botCommandStart) {
		_botCommandStart->setVisible(_botCommandShown);
	}
	if (_like) {
		_like->setVisible(_likeShown);
	}
	if (_ttlInfo) {
		_ttlInfo->show();
	}
	if (_sendAs) {
		_sendAs->show();
	}
	if (_replaceMedia) {
		_replaceMedia->show();
		_attachToggle->hide();
	} else {
		_attachToggle->show();
	}
	if (_scheduled) {
		_scheduled->setVisible(!isEditingMessage());
	}
}

bool ComposeControls::updateLikeShown() {
	auto shown = _like && !HasSendText(_field);
	if (_likeShown != shown) {
		_likeShown = shown;
		return true;
	}
	return false;
}

bool ComposeControls::updateBotCommandShown() {
	auto shown = false;
	const auto peer = _history ? _history->peer.get() : nullptr;
	if (_botCommandStart
		&& peer
		&& ((peer->isChat() && peer->asChat()->botStatus > 0)
			|| (peer->isMegagroup() && peer->asChannel()->mgInfo->botStatus > 0)
			|| (peer->isUser() && peer->asUser()->isBot()))) {
		if (!HasSendText(_field)) {
			shown = true;
		}
	}
	if (_botCommandShown != shown) {
		_botCommandShown = shown;
		return true;
	}
	return false;
}

void ComposeControls::updateOuterGeometry(QRect rect) {
	if (_inlineResults) {
		_inlineResults->moveBottom(rect.y());
	}
	const auto bottom = rect.y() + rect.height() - _attachToggle->height();
	if (_tabbedPanel) {
		_tabbedPanel->moveBottomRight(bottom, rect.x() + rect.width());
	}
	if (_attachBotsMenu) {
		_attachBotsMenu->moveToLeft(0, bottom - _attachBotsMenu->height());
	}
}

void ComposeControls::updateMessagesTTLShown() {
	const auto peer = _history ? _history->peer.get() : nullptr;
	const auto shown = _features.ttlInfo
		&& peer
		&& (peer->messagesTTL() > 0);
	if (!shown && _ttlInfo) {
		_ttlInfo = nullptr;
		updateControlsVisibility();
		updateControlsGeometry(_wrap->size());
	} else if (shown && !_ttlInfo) {
		_ttlInfo = std::make_unique<Controls::TTLButton>(
			_wrap.get(),
			_show,
			peer);
		orderControls();
		updateControlsVisibility();
		updateControlsGeometry(_wrap->size());
	}
}

bool ComposeControls::updateSendAsButton() {
	const auto peer = _history ? _history->peer.get() : nullptr;
	if (!_features.sendAs
		|| !peer
		|| !_regularWindow
		|| isEditingMessage()
		|| !session().sendAsPeers().shouldChoose(peer)) {
		if (!_sendAs) {
			return false;
		}
		_sendAs = nullptr;
		return true;
	} else if (_sendAs) {
		return false;
	}
	_sendAs = std::make_unique<Ui::SendAsButton>(
		_wrap.get(),
		st::sendAsButton);
	Ui::SetupSendAsButton(
		_sendAs.get(),
		rpl::single(peer),
		_regularWindow);
	return true;
}

void ComposeControls::updateAttachBotsMenu() {
	_attachBotsMenu = nullptr;
	if (!_features.attachBotsMenu
		|| !_history
		|| !_sendActionFactory
		|| !_regularWindow) {
		return;
	}
	_attachBotsMenu = InlineBots::MakeAttachBotsMenu(
		_panelsParent,
		_regularWindow,
		_history->peer,
		_sendActionFactory,
		[=](bool compress) { _attachRequests.fire_copy(compress); });
	if (!_attachBotsMenu) {
		return;
	}
	_attachBotsMenu->setOrigin(
		Ui::PanelAnimation::Origin::BottomLeft);
	_attachToggle->installEventFilter(_attachBotsMenu.get());
	_attachBotsMenu->heightValue(
	) | rpl::start_with_next([=] {
		updateOuterGeometry(_wrap->geometry());
	}, _attachBotsMenu->lifetime());
}

void ComposeControls::paintBackground(QPainter &p, QRect full, QRect clip) {
	if (_backgroundRect) {
		//p.setCompositionMode(QPainter::CompositionMode_Source);
		//p.fillRect(clip, Qt::transparent);
		//p.setCompositionMode(QPainter::CompositionMode_SourceOver);
		//_backgroundRect->paint(p, _wrap->rect());
		auto hq = PainterHighQualityEnabler(p);
		p.setBrush(_st.bg);
		p.setPen(Qt::NoPen);
		p.drawRoundedRect(full, _st.radius, _st.radius);
	} else {
		p.fillRect(clip, _st.bg);
	}
}

void ComposeControls::escape() {
	if (const auto voice = _voiceRecordBar.get(); voice->isActive()) {
		voice->showDiscardBox(nullptr, anim::type::normal);
	} else {
		_cancelRequests.fire({});
	}
}

bool ComposeControls::pushTabbedSelectorToThirdSection(
		not_null<Data::Thread*> thread,
		const Window::SectionShow &params) {
	if (!_tabbedPanel || !_regularWindow || !_features.commonTabbedPanel) {
		return true;
	//} else if (!_canSendMessages) {
	//	Core::App().settings().setTabbedReplacedWithInfo(true);
	//	_window->showPeerInfo(_peer, params.withThirdColumn());
	//	return;
	}
	Core::App().settings().setTabbedReplacedWithInfo(false);
	_tabbedSelectorToggle->setColorOverrides(
		&st::historyAttachEmojiActive,
		&st::historyRecordVoiceFgActive,
		&st::historyRecordVoiceRippleBgActive);
	_regularWindow->resizeForThirdSection();
	_regularWindow->showSection(
		std::make_shared<ChatHelpers::TabbedMemento>(),
		params.withThirdColumn());
	return true;
}

bool ComposeControls::returnTabbedSelector() {
	createTabbedPanel();
	updateOuterGeometry(_wrap->geometry());
	return true;
}

void ComposeControls::createTabbedPanel() {
	using namespace ChatHelpers;
	auto descriptor = TabbedPanelDescriptor{
		.regularWindow = _regularWindow,
		.ownedSelector = (_ownedSelector
			? object_ptr<TabbedSelector>::fromRaw(_ownedSelector.release())
			: object_ptr<TabbedSelector>(nullptr)),
		.nonOwnedSelector = _ownedSelector ? nullptr : _selector.get(),
	};
	setTabbedPanel(std::make_unique<TabbedPanel>(
		_panelsParent,
		std::move(descriptor)));
	_tabbedPanel->setDesiredHeightValues(
		st::emojiPanHeightRatio,
		_st.tabbedHeightMin,
		_st.tabbedHeightMax);
}

void ComposeControls::setTabbedPanel(
		std::unique_ptr<ChatHelpers::TabbedPanel> panel) {
	_tabbedPanel = std::move(panel);
	if (const auto raw = _tabbedPanel.get()) {
		_tabbedSelectorToggle->installEventFilter(raw);
		_tabbedSelectorToggle->setColorOverrides(nullptr, nullptr, nullptr);
	} else {
		_tabbedSelectorToggle->setColorOverrides(
			&st::historyAttachEmojiActive,
			&st::historyRecordVoiceFgActive,
			&st::historyRecordVoiceRippleBgActive);
	}
}

void ComposeControls::toggleTabbedSelectorMode() {
	if (!_history || !_regularWindow || !_features.commonTabbedPanel) {
		return;
	}
	if (_tabbedPanel) {
		if (_regularWindow->canShowThirdSection()
				&& !_regularWindow->adaptive().isOneColumn()) {
			Core::App().settings().setTabbedSelectorSectionEnabled(true);
			Core::App().saveSettingsDelayed();
			const auto topic = _history->peer->forumTopicFor(_topicRootId);
			pushTabbedSelectorToThirdSection(
				(topic ? topic : (Data::Thread*)_history),
				Window::SectionShow::Way::ClearStack);
		} else {
			_tabbedPanel->toggleAnimated();
		}
	} else {
		_regularWindow->closeThirdSection();
	}
}

void ComposeControls::updateHeight() {
	const auto height = _field->height()
		+ (_header->isDisplayed() ? _header->height() : 0)
		+ 2 * st::historySendPadding;
	if (height != _wrap->height()) {
		_wrap->resize(_wrap->width(), height);
	}
}

void ComposeControls::editMessage(
		FullMsgId id,
		const TextSelection &selection) {
	if (const auto item = session().data().message(id)) {
		editMessage(item);
		SelectTextInFieldWithMargins(_field, selection);
	}
}

void ComposeControls::editMessage(not_null<HistoryItem*> item) {
	Expects(_history != nullptr);
	Expects(draftKeyCurrent() != Data::DraftKey::None());

	if (_voiceRecordBar->isActive()) {
		_show->showBox(Ui::MakeInformBox(tr::lng_edit_caption_voice()));
		return;
	}

	if (!isEditingMessage()) {
		saveFieldToHistoryLocalDraft();
	}
	const auto editData = PrepareEditText(item);
	const auto cursor = MessageCursor{
		int(editData.text.size()),
		int(editData.text.size()),
		Ui::kQFixedMax
	};
	const auto key = draftKey(DraftType::Edit);
	_history->setDraft(
		key,
		std::make_unique<Data::Draft>(
			editData,
			FullReplyTo{
				.messageId = item->fullId(),
				.topicRootId = key.topicRootId(),
			},
			cursor,
			Data::WebPageDraft::FromItem(item)));
	applyDraft();
	if (updateReplaceMediaButton()) {
		updateControlsVisibility();
		updateControlsGeometry(_wrap->size());
	}

	if (_autocomplete) {
		InvokeQueued(_autocomplete.get(), [=] {
			_autocomplete->requestRefresh();
		});
	}
}

bool ComposeControls::updateReplaceMediaButton() {
	if ((!_canReplaceMedia && !_canAddMedia) || !_regularWindow) {
		const auto result = (_replaceMedia != nullptr);
		_replaceMedia = nullptr;
		return result;
	} else if (_replaceMedia) {
		return false;
	}
	_replaceMedia = std::make_unique<Ui::IconButton>(
		_wrap.get(),
		_canReplaceMedia ? st::historyReplaceMedia : st::historyAddMedia);
	const auto hideDuration = st::historyReplaceMedia.ripple.hideDuration;
	_replaceMedia->setClickedCallback([=] {
		base::call_delayed(hideDuration, _wrap.get(), [=] {
			const auto queryToEdit = _header->queryToEdit();
			EditCaptionBox::StartMediaReplace(
				_regularWindow,
				_editingId,
				_field->getTextWithTags(),
				queryToEdit.spoilered,
				queryToEdit.options.invertCaption,
				crl::guard(_wrap.get(), [=] { cancelEditMessage(); }));
		});
	});
	return true;
}

void ComposeControls::cancelEditMessage() {
	Expects(_history != nullptr);
	Expects(draftKeyCurrent() != Data::DraftKey::None());

	_history->clearDraft(draftKey(DraftType::Edit));
	applyDraft();

	_saveDraftText = true;
	_saveDraftStart = crl::now();
	saveDraft();
}

void ComposeControls::maybeCancelEditMessage() {
	Expects(_history != nullptr);

	const auto item = _history->owner().message(_header->editMsgId());
	if (item && EditTextChanged(item, _field->getTextWithTags())) {
		const auto guard = _field.get();
		_show->show(Ui::MakeConfirmBox({
			.text = tr::lng_cancel_edit_post_sure(),
			.confirmed = crl::guard(guard, [this](Fn<void()> &&close) {
				cancelEditMessage();
				close();
			}),
			.confirmText = tr::lng_cancel_edit_post_yes(),
			.cancelText = tr::lng_cancel_edit_post_no(),
		}));
	} else {
		cancelEditMessage();
	}
}

void ComposeControls::replyToMessage(FullReplyTo id) {
	Expects(_history != nullptr);
	Expects(draftKeyCurrent() != Data::DraftKey::None());

	id.topicRootId = _topicRootId;
	if (!id) {
		cancelReplyMessage();
		return;
	}
	if (isEditingMessage()) {
		const auto key = draftKey(DraftType::Normal);
		Assert(key.topicRootId() == id.topicRootId);
		if (const auto localDraft = _history->draft(key)) {
			localDraft->reply = id;
		} else {
			_history->setDraft(
				key,
				std::make_unique<Data::Draft>(
					TextWithTags(),
					id,
					MessageCursor(),
					Data::WebPageDraft()));
		}
	} else {
		_header->replyToMessage(id);
	}

	_saveDraftText = true;
	_saveDraftStart = crl::now();
	saveDraft();
}

void ComposeControls::cancelReplyMessage() {
	const auto wasReply = replyingToMessage();
	_header->replyToMessage({});
	if (_history) {
		const auto key = draftKey(DraftType::Normal);
		if (const auto localDraft = _history->draft(key)) {
			if (localDraft->reply.messageId) {
				if (localDraft->textWithTags.text.isEmpty()) {
					_history->clearDraft(key);
				} else {
					localDraft->reply = {};
				}
			}
		}
		if (wasReply) {
			_saveDraftText = true;
			_saveDraftStart = crl::now();
			saveDraft();
		}
	}
}

void ComposeControls::updateForwarding() {
	const auto rootId = _topicRootId;
	const auto thread = (_history && rootId)
		? _history->peer->forumTopicFor(rootId)
		: (Data::Thread*)_history;
	_header->updateForwarding(thread, thread
		? _history->resolveForwardDraft(rootId)
		: Data::ResolvedForwardDraft());
	updateSendButtonType();
}

bool ComposeControls::handleCancelRequest() {
	if (_isInlineBot) {
		cancelInlineBot();
		return true;
	} else if (_autocomplete && !_autocomplete->isHidden()) {
		_autocomplete->hideAnimated();
		return true;
	} else if (isEditingMessage()) {
		maybeCancelEditMessage();
		return true;
	} else if (replyingToMessage()) {
		cancelReplyMessage();
		return true;
	} else if (readyToForward()) {
		cancelForward();
		return true;
	}
	return false;
}

void ComposeControls::tryProcessKeyInput(not_null<QKeyEvent*> e) {
	if (_field->isVisible() && !e->text().isEmpty()) {
		_field->setFocusFast();
		QCoreApplication::sendEvent(_field->rawTextEdit(), e);
	}
}

void ComposeControls::initWebpageProcess() {
	if (!_history) {
		_preview = nullptr;
		_header->previewUnregister();
		return;
	}

	_preview = std::make_unique<Controls::WebpageProcessor>(
		_history,
		_field);

	_preview->repaintRequests(
	) | rpl::start_with_next(crl::guard(_header.get(), [=] {
		_header->update();
	}), _historyLifetime);

	session().changes().peerUpdates(
		Data::PeerUpdate::Flag::Rights
		| Data::PeerUpdate::Flag::Notifications
		| Data::PeerUpdate::Flag::MessagesTTL
		| Data::PeerUpdate::Flag::FullInfo
	) | rpl::filter([peer = _history->peer](const Data::PeerUpdate &update) {
		return (update.peer.get() == peer);
	}) | rpl::map([](const Data::PeerUpdate &update) {
		return update.flags;
	}) | rpl::start_with_next([=](Data::PeerUpdate::Flags flags) {
		if (flags & Data::PeerUpdate::Flag::Rights) {
			_preview->checkNow(false);
			updateFieldPlaceholder();
		}
		if (flags & Data::PeerUpdate::Flag::Notifications) {
			updateSilentBroadcast();
		}
		if (flags & Data::PeerUpdate::Flag::MessagesTTL) {
			updateMessagesTTLShown();
		}
		if (flags & Data::PeerUpdate::Flag::FullInfo) {
			if (updateBotCommandShown()) {
				updateControlsVisibility();
				updateControlsGeometry(_wrap->size());
			}
		}
	}, _historyLifetime);

	_header->previewReady(_preview->parsedValue());
}

void ComposeControls::initForwardProcess() {
	using EntryUpdateFlag = Data::EntryUpdate::Flag;
	session().changes().entryUpdates(
		EntryUpdateFlag::ForwardDraft
	) | rpl::start_with_next([=](const Data::EntryUpdate &update) {
		if (const auto topic = update.entry->asTopic()) {
			if (topic->history() == _history
				&& topic->rootId() == _topicRootId) {
				updateForwarding();
			}
		}
	}, _wrap->lifetime());

	updateForwarding();
}

Data::WebPageDraft ComposeControls::webPageDraft() const {
	return _preview ? _preview->draft() : Data::WebPageDraft();
}

rpl::producer<FullReplyTo> ComposeControls::jumpToItemRequests() const {
	return rpl::merge(
		_header->jumpToItemRequests(),
		_jumpToItemRequests.events());
}

bool ComposeControls::isEditingMessage() const {
	return _header->isEditingMessage();
}

FullReplyTo ComposeControls::replyingToMessage() const {
	auto result = _header->replyingToMessage();
	result.topicRootId = _topicRootId;
	return result;
}

bool ComposeControls::readyToForward() const {
	return _header->readyToForward();
}

bool ComposeControls::isLockPresent() const {
	return _voiceRecordBar->isLockPresent();
}

bool ComposeControls::isTTLButtonShown() const {
	return _voiceRecordBar->isTTLButtonShown();
}

rpl::producer<bool> ComposeControls::lockShowStarts() const {
	return _voiceRecordBar->lockShowStarts();
}

rpl::producer<not_null<QEvent*>> ComposeControls::viewportEvents() const {
	return _voiceRecordBar->lockViewportEvents();
}

rpl::producer<> ComposeControls::likeToggled() const {
	return _likeToggled.events();
}

bool ComposeControls::isRecording() const {
	return _voiceRecordBar->isRecording();
}

bool ComposeControls::isRecordingPressed() const {
	return !_voiceRecordBar->isRecordingLocked()
		&& (!_voiceRecordBar->isHidden()
			|| (_send->isDown()
				&& (_send->type() == Ui::SendButton::Type::Record
					|| _send->type() == Ui::SendButton::Type::Round)));
}

rpl::producer<bool> ComposeControls::recordingActiveValue() const {
	return _voiceRecordBar->shownValue();
}

rpl::producer<bool> ComposeControls::hasSendTextValue() const {
	return _hasSendText.value();
}

rpl::producer<bool> ComposeControls::fieldMenuShownValue() const {
	return _field->menuShownValue();
}

not_null<Ui::RpWidget*> ComposeControls::likeAnimationTarget() const {
	Expects(_like != nullptr);

	return _like;
}

int ComposeControls::fieldCharacterCount() const {
	return Ui::ComputeFieldCharacterCount(_field);
}

bool ComposeControls::preventsClose(Fn<void()> &&continueCallback) const {
	if (_voiceRecordBar->isActive()) {
		_voiceRecordBar->showDiscardBox(std::move(continueCallback));
		return true;
	}
	return false;
}

bool ComposeControls::hasSilentBroadcastToggle() const {
	if (!_features.silentBroadcastToggle || !_history) {
		return false;
	}
	const auto &peer = _history->peer;
	return peer
		&& peer->isBroadcast()
		&& Data::CanSendAnything(peer)
		&& !session().data().notifySettings().silentPostsUnknown(peer);
}

void ComposeControls::updateInlineBotQuery() {
	if (!_history || !_regularWindow) {
		return;
	}
	const auto query = ParseInlineBotQuery(&session(), _field);
	if (_inlineBotUsername != query.username) {
		_inlineBotUsername = query.username;
		auto &api = session().api();
		if (_inlineBotResolveRequestId) {
			api.request(_inlineBotResolveRequestId).cancel();
			_inlineBotResolveRequestId = 0;
		}
		if (query.lookingUpBot) {
			_inlineBot = nullptr;
			_inlineLookingUpBot = true;
			const auto username = _inlineBotUsername;
			_inlineBotResolveRequestId = api.request(
				MTPcontacts_ResolveUsername(
					MTP_flags(0),
					MTP_string(username),
					MTP_string())
			).done([=](const MTPcontacts_ResolvedPeer &result) {
				Expects(result.type() == mtpc_contacts_resolvedPeer);

				const auto &data = result.c_contacts_resolvedPeer();
				const auto resolvedBot = [&]() -> UserData* {
					if (const auto user = session().data().processUsers(
							data.vusers())) {
						if (user->isBot()
							&& !user->botInfo->inlinePlaceholder.isEmpty()) {
							return user;
						}
					}
					return nullptr;
				}();
				session().data().processChats(data.vchats());

				_inlineBotResolveRequestId = 0;
				const auto query = ParseInlineBotQuery(&session(), _field);
				if (_inlineBotUsername == query.username) {
					applyInlineBotQuery(
						query.lookingUpBot ? resolvedBot : query.bot,
						query.query);
				} else {
					clearInlineBot();
				}

			}).fail([=] {
				_inlineBotResolveRequestId = 0;
				if (username == _inlineBotUsername) {
					clearInlineBot();
				}
			}).send();
		} else {
			applyInlineBotQuery(query.bot, query.query);
		}
	} else if (query.lookingUpBot) {
		if (!_inlineLookingUpBot) {
			applyInlineBotQuery(_inlineBot, query.query);
		}
	} else {
		applyInlineBotQuery(query.bot, query.query);
	}
}

void ComposeControls::applyInlineBotQuery(
		UserData *bot,
		const QString &query) {
	Expects(_regularWindow != nullptr);

	if (_history && bot) {
		if (_inlineBot != bot) {
			_inlineBot = bot;
			_inlineLookingUpBot = false;
			inlineBotChanged();
		}
		if (!_inlineResults) {
			_inlineResults = std::make_unique<InlineBots::Layout::Widget>(
				_panelsParent,
				_regularWindow);
			_inlineResults->setResultSelectedCallback([=](
					InlineBots::ResultSelected result) {
				if (result.open) {
					const auto request = result.result->openRequest();
					if (const auto photo = request.photo()) {
						_regularWindow->openPhoto(photo, {});
					} else if (const auto document = request.document()) {
						_regularWindow->openDocument(document, false, {});
					}
				} else {
					_inlineResultChosen.fire_copy(result);
				}
			});
			_inlineResults->setSendMenuDetails([=] {
				return sendMenuDetails();
			});
			_inlineResults->requesting(
			) | rpl::start_with_next([=](bool requesting) {
				_tabbedSelectorToggle->setLoading(requesting);
			}, _inlineResults->lifetime());
			updateOuterGeometry(_wrap->geometry());
		}
		_inlineResults->queryInlineBot(_inlineBot, _history->peer, query);
		if (_autocomplete) {
			_autocomplete->hideAnimated();
		}
	} else {
		clearInlineBot();
	}
}

Fn<void()> ComposeControls::restoreTextCallback(
		const QString &insertTextOnCancel) const {
	const auto cursor = _field->textCursor();
	const auto position = cursor.position();
	const auto anchor = cursor.anchor();
	const auto text = getTextWithAppliedMarkdown();

	_field->setTextWithTags({});

	return crl::guard(_field, [=] {
		_field->setTextWithTags(text);
		auto cursor = _field->textCursor();
		cursor.setPosition(anchor);
		if (position != anchor) {
			cursor.setPosition(position, QTextCursor::KeepAnchor);
		}
		_field->setTextCursor(cursor);
		if (Ui::InsertTextOnImageCancel(insertTextOnCancel)) {
			_field->textCursor().insertText(insertTextOnCancel);
		}
	});
}

Ui::InputField *ComposeControls::fieldForMention() const {
	return _writeRestriction.current() ? nullptr : _field.get();
}

TextWithEntities ComposeControls::prepareTextForEditMsg() const {
	if (!_history) {
		return {};
	}
	const auto textWithTags = getTextWithAppliedMarkdown();
	const auto prepareFlags = Ui::ItemTextOptions(
		_history,
		session().user()).flags;
	auto left = TextWithEntities {
		textWithTags.text,
		TextUtilities::ConvertTextTagsToEntities(textWithTags.tags) };
	TextUtilities::PrepareForSending(left, prepareFlags);
	return left;
}

void ComposeControls::checkCharsLimitation() {
	if (!_history || !isEditingMessage()) {
		_charsLimitation = nullptr;
		return;
	}
	const auto item = _history->owner().message(_header->editMsgId());
	if (!item) {
		_charsLimitation = nullptr;
		return;
	}
	const auto hasMediaWithCaption = item->media()
		&& item->media()->allowsEditCaption();
	const auto maxCaptionSize = !hasMediaWithCaption
		? MaxMessageSize
		: Data::PremiumLimits(&session()).captionLengthCurrent();
	const auto remove = Ui::ComputeFieldCharacterCount(_field)
		- maxCaptionSize;
	if (remove > 0) {
		if (!_charsLimitation) {
			using namespace Controls;
			_charsLimitation = base::make_unique_q<CharactersLimitLabel>(
				_wrap.get(),
				_send.get(),
				style::al_bottom);
			_charsLimitation->show();
			Data::AmPremiumValue(
				&session()
			) | rpl::start_with_next([=] {
				checkCharsLimitation();
			}, _charsLimitation->lifetime());
		}
		_charsLimitation->setLeft(remove);
	} else {
		_charsLimitation = nullptr;
	}
}

rpl::producer<int> SlowmodeSecondsLeft(not_null<PeerData*> peer) {
	return peer->session().changes().peerFlagsValue(
		peer,
		Data::PeerUpdate::Flag::Slowmode
	) | rpl::map([=] {
		return peer->slowmodeSecondsLeft();
	}) | rpl::map([=](int delay) -> rpl::producer<int> {
		auto start = rpl::single(delay);
		if (!delay) {
			return start;
		}
		return std::move(
			start
		) | rpl::then(base::timer_each(
			kRefreshSlowmodeLabelTimeout
		) | rpl::map([=] {
			return peer->slowmodeSecondsLeft();
		}) | rpl::take_while([=](int delay) {
			return delay > 0;
		})) | rpl::then(rpl::single(0));
	}) | rpl::flatten_latest();
}

rpl::producer<bool> SendDisabledBySlowmode(not_null<PeerData*> peer) {
	const auto history = peer->owner().history(peer);
	auto hasSendingMessage = peer->session().changes().historyFlagsValue(
		history,
		Data::HistoryUpdate::Flag::ClientSideMessages
	) | rpl::map([=] {
		return history->latestSendingMessage() != nullptr;
	}) | rpl::distinct_until_changed();

	using namespace rpl::mappers;
	const auto channel = peer->asChannel();
	return (!channel || channel->amCreator())
		? (rpl::single(false) | rpl::type_erased())
		: rpl::combine(
			channel->slowmodeAppliedValue(),
			std::move(hasSendingMessage),
			_1 && _2);
}

void ShowPhotoEditSpoilerMenu(
		not_null<Ui::RpWidget*> parent,
		not_null<HistoryItem*> item,
		const std::optional<bool> &override,
		Fn<void(bool)> callback) {
	const auto media = item->media();
	const auto hasPreview = media && media->hasReplyPreview();
	const auto preview = hasPreview ? media->replyPreview() : nullptr;
	if (!preview) {
		return;
	}
	const auto spoilered = override
		? (*override)
		: (preview && media->hasSpoiler());
	const auto menu = Ui::CreateChild<Ui::PopupMenu>(
		parent,
		st::popupMenuWithIcons);
	menu->addAction(
		spoilered
			? tr::lng_context_disable_spoiler(tr::now)
			: tr::lng_context_spoiler_effect(tr::now),
		[=] { callback(!spoilered); },
		spoilered ? &st::menuIconSpoilerOff : &st::menuIconSpoiler);
	menu->popup(QCursor::pos());
}

Image *MediaPreviewWithOverriddenSpoiler(
		not_null<HistoryItem*> item,
		bool spoiler) {
	if (const auto media = item->media()) {
		if (const auto photo = media->photo()) {
			return photo->getReplyPreview(
				item->fullId(),
				item->history()->peer,
				spoiler);
		} else if (const auto document = media->document()) {
			return document->getReplyPreview(
				item->fullId(),
				item->history()->peer,
				spoiler);
		}
	}
	return nullptr;
}

} // namespace HistoryView
