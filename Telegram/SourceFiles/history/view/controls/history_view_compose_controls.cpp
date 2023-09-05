/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_compose_controls.h"

#include "base/event_filter.h"
#include "base/platform/base_platform_info.h"
#include "base/qt_signal_producer.h"
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
#include "history/view/controls/history_view_voice_record_bar.h"
#include "history/view/controls/history_view_ttl_button.h"
#include "history/view/controls/history_view_forward_panel.h"
#include "history/view/history_view_webpage_preview.h"
#include "inline_bots/bot_attach_web_view.h"
#include "inline_bots/inline_results_widget.h"
#include "inline_bots/inline_bot_result.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "main/session/send_as_peers.h"
#include "media/audio/media_audio_capture.h"
#include "media/audio/media_audio.h"
#include "ui/text/text_options.h"
#include "ui/ui_utility.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/text/format_values.h"
#include "ui/controls/emoji_button.h"
#include "ui/controls/send_button.h"
#include "ui/controls/send_as_button.h"
#include "ui/controls/silent_toggle.h"
#include "ui/chat/choose_send_as.h"
#include "window/window_adaptive.h"
#include "window/window_session_controller.h"
#include "mainwindow.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"

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

[[nodiscard]] auto ShowWebPagePreview(WebPageData *page) {
	return page && (page->pendingTill >= 0);
}

WebPageText ProcessWebPageData(WebPageData *page) {
	auto previewText = HistoryView::TitleAndDescriptionFromWebPage(page);
	if (previewText.title.isEmpty()) {
		if (page->document) {
			previewText.title = tr::lng_attach_file(tr::now);
		} else if (page->photo) {
			previewText.title = tr::lng_attach_photo(tr::now);
		}
	}
	return previewText;
}

} // namespace

class WebpageProcessor final {
public:
	WebpageProcessor(
		not_null<History*> history,
		not_null<Ui::InputField*> field);

	void cancel();
	void checkPreview();

	[[nodiscard]] Data::PreviewState state() const;
	void setState(Data::PreviewState value);
	void refreshState(Data::PreviewState value, bool disable);

	[[nodiscard]] rpl::producer<> paintRequests() const;
	[[nodiscard]] rpl::producer<QString> titleChanges() const;
	[[nodiscard]] rpl::producer<QString> descriptionChanges() const;
	[[nodiscard]] rpl::producer<WebPageData*> pageDataChanges() const;

private:
	void updatePreview();
	void getWebPagePreview();

	const not_null<History*> _history;
	MTP::Sender _api;
	MessageLinksParser _fieldLinksParser;

	Data::PreviewState _previewState = Data::PreviewState();

	QStringList _parsedLinks;
	QString _previewLinks;

	WebPageData *_previewData = nullptr;
	std::map<QString, WebPageId> _previewCache;

	mtpRequestId _previewRequest = 0;

	rpl::event_stream<> _paintRequests;
	rpl::event_stream<QString> _titleChanges;
	rpl::event_stream<QString> _descriptionChanges;
	rpl::event_stream<WebPageData*> _pageDataChanges;

	base::Timer _timer;

	rpl::lifetime _lifetime;

};

WebpageProcessor::WebpageProcessor(
	not_null<History*> history,
	not_null<Ui::InputField*> field)
: _history(history)
, _api(&history->session().mtp())
, _fieldLinksParser(field)
, _previewState(Data::PreviewState::Allowed)
, _timer([=] {
	if (!ShowWebPagePreview(_previewData)
		|| _previewLinks.isEmpty()) {
		return;
	}
	getWebPagePreview();
}) {

	_history->session().downloaderTaskFinished(
	) | rpl::filter([=] {
		return _previewData
			&& (_previewData->document || _previewData->photo);
	}) | rpl::start_with_next([=] {
		_paintRequests.fire({});
	}, _lifetime);

	_history->owner().webPageUpdates(
	) | rpl::filter([=](not_null<WebPageData*> page) {
		return (_previewData == page.get());
	}) | rpl::start_with_next([=] {
		updatePreview();
	}, _lifetime);

	_fieldLinksParser.list().changes(
	) | rpl::start_with_next([=](QStringList &&parsed) {
		if (_previewState == Data::PreviewState::EmptyOnEdit
			&& _parsedLinks != parsed) {
			_previewState = Data::PreviewState::Allowed;
		}
		_parsedLinks = std::move(parsed);

		checkPreview();
	}, _lifetime);
}

rpl::producer<> WebpageProcessor::paintRequests() const {
	return _paintRequests.events();
}

Data::PreviewState WebpageProcessor::state() const {
	return _previewState;
}

void WebpageProcessor::setState(Data::PreviewState value) {
	_previewState = value;
}

void WebpageProcessor::refreshState(
		Data::PreviewState value,
		bool disable) {
	// Save links from _field to _parsedLinks without generating preview.
	_previewState = Data::PreviewState::Cancelled;
	_fieldLinksParser.setDisabled(disable);
	_fieldLinksParser.parseNow();
	_parsedLinks = _fieldLinksParser.list().current();
	_previewState = value;
	checkPreview();
}

void WebpageProcessor::cancel() {
	_api.request(base::take(_previewRequest)).cancel();
	_previewData = nullptr;
	_previewLinks.clear();
	updatePreview();
}

void WebpageProcessor::updatePreview() {
	_timer.cancel();
	auto t = QString();
	auto d = QString();
	if (ShowWebPagePreview(_previewData)) {
		if (const auto till = _previewData->pendingTill) {
			t = tr::lng_preview_loading(tr::now);
			d = QStringView(_previewLinks).split(' ').at(0).toString();

			const auto timeout = till - base::unixtime::now();
			_timer.callOnce(
				std::max(timeout, 0) * crl::time(1000));
		} else {
			const auto preview = ProcessWebPageData(_previewData);
			t = preview.title;
			d = preview.description;
		}
	}
	_titleChanges.fire_copy(t);
	_descriptionChanges.fire_copy(d);
	_pageDataChanges.fire_copy(_previewData);
	_paintRequests.fire({});
}

void WebpageProcessor::getWebPagePreview() {
	const auto links = _previewLinks;
	_previewRequest = _api.request(
		MTPmessages_GetWebPagePreview(
			MTP_flags(0),
			MTP_string(links),
			MTPVector<MTPMessageEntity>()
	)).done([=](const MTPMessageMedia &result) {
		_previewRequest = 0;
		result.match([=](const MTPDmessageMediaWebPage &d) {
			const auto page = _history->owner().processWebpage(d.vwebpage());
			_previewCache.insert({ links, page->id });
			auto &till = page->pendingTill;
			if (till > 0 && till <= base::unixtime::now()) {
				till = -1;
			}
			if (links == _previewLinks
				&& _previewState == Data::PreviewState::Allowed) {
				_previewData = (page->id && page->pendingTill >= 0)
					? page.get()
					: nullptr;
				updatePreview();
			}
		}, [=](const MTPDmessageMediaEmpty &d) {
			_previewCache.insert({ links, 0 });
			if (links == _previewLinks
				&& _previewState == Data::PreviewState::Allowed) {
				_previewData = nullptr;
				updatePreview();
			}
		}, [](const auto &d) {
		});
	}).fail([=] {
		_previewRequest = 0;
	}).send();
}

void WebpageProcessor::checkPreview() {
	const auto previewRestricted = _history->peer
		&& _history->peer->amRestricted(ChatRestriction::EmbedLinks);
	if (_previewState != Data::PreviewState::Allowed
		|| previewRestricted) {
		cancel();
		return;
	}
	const auto newLinks = _parsedLinks.join(' ');
	if (_previewLinks == newLinks) {
		return;
	}
	_api.request(base::take(_previewRequest)).cancel();
	_previewLinks = newLinks;
	if (_previewLinks.isEmpty()) {
		if (ShowWebPagePreview(_previewData)) {
			cancel();
		}
	} else {
		const auto i = _previewCache.find(_previewLinks);
		if (i == _previewCache.end()) {
			getWebPagePreview();
		} else if (i->second) {
			_previewData = _history->owner().webpage(i->second);
			updatePreview();
		} else if (ShowWebPagePreview(_previewData)) {
			cancel();
		}
	}
}

rpl::producer<QString> WebpageProcessor::titleChanges() const {
	return _titleChanges.events();
}

rpl::producer<QString> WebpageProcessor::descriptionChanges() const {
	return _descriptionChanges.events();
}

rpl::producer<WebPageData*> WebpageProcessor::pageDataChanges() const {
	return _pageDataChanges.events();
}

class FieldHeader final : public Ui::RpWidget {
public:
	FieldHeader(
		QWidget *parent,
		std::shared_ptr<ChatHelpers::Show> show);

	void setHistory(const SetHistoryArgs &args);
	void init();

	void editMessage(FullMsgId id, bool photoEditAllowed = false);
	void replyToMessage(FullMsgId id);
	void updateForwarding(
		Data::Thread *thread,
		Data::ResolvedForwardDraft items);
	void previewRequested(
		rpl::producer<QString> title,
		rpl::producer<QString> description,
		rpl::producer<WebPageData*> page);
	void previewUnregister();

	[[nodiscard]] bool isDisplayed() const;
	[[nodiscard]] bool isEditingMessage() const;
	[[nodiscard]] bool readyToForward() const;
	[[nodiscard]] const HistoryItemsList &forwardItems() const;
	[[nodiscard]] FullMsgId replyingToMessage() const;
	[[nodiscard]] FullMsgId editMsgId() const;
	[[nodiscard]] rpl::producer<FullMsgId> editMsgIdValue() const;
	[[nodiscard]] rpl::producer<FullMsgId> scrollToItemRequests() const;
	[[nodiscard]] rpl::producer<> editPhotoRequests() const;
	[[nodiscard]] MessageToEdit queryToEdit();
	[[nodiscard]] WebPageId webPageId() const;

	[[nodiscard]] MsgId getDraftMessageId() const;
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
		WebPageData *data = nullptr;
		Ui::Text::String title;
		Ui::Text::String description;
		bool cancelled = false;
	};

	const std::shared_ptr<ChatHelpers::Show> _show;
	History *_history = nullptr;
	rpl::variable<QString> _title;
	rpl::variable<QString> _description;

	Preview _preview;
	rpl::event_stream<> _editCancelled;
	rpl::event_stream<> _replyCancelled;
	rpl::event_stream<> _forwardCancelled;
	rpl::event_stream<> _previewCancelled;
	rpl::lifetime _previewLifetime;

	rpl::variable<FullMsgId> _editMsgId;
	rpl::variable<FullMsgId> _replyToId;
	std::unique_ptr<ForwardPanel> _forwardPanel;
	rpl::producer<> _toForwardUpdated;

	HistoryItem *_shownMessage = nullptr;
	Ui::Text::String _shownMessageName;
	Ui::Text::String _shownMessageText;
	std::unique_ptr<Ui::SpoilerAnimation> _shownPreviewSpoiler;
	Ui::Animations::Simple _inPhotoEditOver;
	int _shownMessageNameVersion = -1;
	bool _shownMessageHasPreview : 1 = false;
	bool _inPhotoEdit : 1 = false;
	bool _photoEditAllowed : 1 = false;
	bool _repaintScheduled : 1 = false;
	bool _inClickable : 1 = false;

	const not_null<Data::Session*> _data;
	const not_null<Ui::IconButton*> _cancel;

	QRect _clickableRect;
	QRect _shownMessagePreviewRect;

	rpl::event_stream<bool> _visibleChanged;
	rpl::event_stream<FullMsgId> _scrollToItemRequests;
	rpl::event_stream<> _editPhotoRequests;

};

FieldHeader::FieldHeader(
	QWidget *parent,
	std::shared_ptr<ChatHelpers::Show> show)
: RpWidget(parent)
, _show(std::move(show))
, _forwardPanel(
	std::make_unique<ForwardPanel>([=] { customEmojiRepaint(); }))
, _data(&_show->session().data())
, _cancel(Ui::CreateChild<Ui::IconButton>(this, st::historyReplyCancel)) {
	resize(QSize(parent->width(), st::historyReplyHeight));
	init();
}

void FieldHeader::setHistory(const SetHistoryArgs &args) {
	_history = *args.history;
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

	const auto leftIconPressed = lifetime().make_state<bool>(false);
	paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(this);
		p.setInactive(_show->paused(Window::GifPauseReason::Any));
		p.fillRect(rect(), st::historyComposeAreaBg);

		const auto position = st::historyReplyIconPosition;
		if (isEditingMessage()) {
			st::historyEditIcon.paint(p, position, width());
		} else if (readyToForward()) {
			st::historyForwardIcon.paint(p, position, width());
		} else if (replyingToMessage()) {
			st::historyReplyIcon.paint(p, position, width());
		}

		(ShowWebPagePreview(_preview.data) && !*leftIconPressed)
			? paintWebPage(
				p,
				_history ? _history->peer : _data->session().user())
			: (isEditingMessage() || !readyToForward())
			? paintEditOrReplyToMessage(p)
			: paintForwardInfo(p);
	}, lifetime());

	_editMsgId.value(
	) | rpl::start_with_next([=](FullMsgId value) {
		const auto shown = value ? value : _replyToId.current();
		setShownMessage(_data->message(shown));
	}, lifetime());

	_replyToId.value(
	) | rpl::start_with_next([=](FullMsgId value) {
		if (!_editMsgId.current()) {
			setShownMessage(_data->message(value));
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
			if (_replyToId.current() == update.item->fullId()) {
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
		} else if (readyToForward()) {
			_forwardCancelled.fire({});
		} else if (_replyToId.current()) {
			_replyCancelled.fire({});
		}
		updateVisible();
		update();
	});

	_title.value(
	) | rpl::start_with_next([=](const auto &t) {
		_preview.title.setText(
			st::msgNameStyle,
			t,
			Ui::NameTextOptions());
	}, lifetime());

	_description.value(
	) | rpl::start_with_next([=](const auto &d) {
		_preview.description.setText(
			st::messageTextStyle,
			d,
			Ui::DialogTextOptions());
	}, lifetime());

	setMouseTracking(true);
	events(
	) | rpl::filter([=](not_null<QEvent*> event) {
		const auto type = event->type();
		const auto leaving = (type == QEvent::Leave);
		return (ranges::contains(kMouseEvents, type) || leaving)
			&& (isEditingMessage()
				|| readyToForward()
				|| replyingToMessage());
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
		const auto pos = e ? e->pos() : mapFromGlobal(QCursor::pos());
		const auto inPreviewRect = _clickableRect.contains(pos);
		const auto inPhotoEdit = _shownMessageHasPreview
			&& _photoEditAllowed
			&& _shownMessagePreviewRect.contains(pos);

		if (type == QEvent::MouseMove) {
			updateOver(inPreviewRect, inPhotoEdit);
			return;
		}
		const auto isLeftIcon = (pos.x() < st::historyReplySkip);
		const auto isLeftButton = (e->button() == Qt::LeftButton);
		if (type == QEvent::MouseButtonPress) {
			if (isLeftButton && isLeftIcon && !inPreviewRect) {
				*leftIconPressed = true;
				update();
			} else if (isLeftButton && inPhotoEdit) {
				_editPhotoRequests.fire({});
			} else if (isLeftButton && inPreviewRect) {
				if (!isEditingMessage() && readyToForward()) {
					_forwardPanel->editOptions(_show);
				} else {
					auto id = isEditingMessage()
						? _editMsgId.current()
						: replyingToMessage();
					_scrollToItemRequests.fire(std::move(id));
				}
			}
		} else if (type == QEvent::MouseButtonRelease) {
			if (isLeftButton && *leftIconPressed) {
				*leftIconPressed = false;
				update();
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
	_shownMessageText.setMarkedText(
		st::messageTextStyle,
		_shownMessage->inReplyText(),
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
		if (item->fullId() == _editMsgId.current()) {
			_preview = {};
			if (const auto media = item->media()) {
				if (const auto page = media->webpage()) {
					const auto preview = ProcessWebPageData(page);
					_title = preview.title;
					_description = preview.description;
					_preview.data = page;
				}
			}
		}
	} else {
		_shownMessageText.clear();
		resolveMessageData();
	}
	if (isEditingMessage()) {
		_shownMessageName.setText(
			st::msgNameStyle,
			tr::lng_edit_message(tr::now),
			Ui::NameTextOptions());
	} else {
		_shownMessageName.clear();
		_shownMessageNameVersion = -1;
	}
	updateVisible();
	update();
}

void FieldHeader::resolveMessageData() {
	const auto id = (isEditingMessage() ? _editMsgId : _replyToId).current();
	if (!id) {
		return;
	}
	const auto peer = _data->peer(id.peer);
	const auto itemId = id.msg;
	const auto callback = crl::guard(this, [=] {
		const auto now = (isEditingMessage()
			? _editMsgId
			: _replyToId).current();
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

void FieldHeader::previewRequested(
		rpl::producer<QString> title,
		rpl::producer<QString> description,
		rpl::producer<WebPageData*> page) {
	_previewLifetime.destroy();

	std::move(
		title
	) | rpl::filter([=] {
		return !_preview.cancelled;
	}) | rpl::start_with_next([=](const QString &t) {
		_title = t;
	}, _previewLifetime);

	std::move(
		description
	) | rpl::filter([=] {
		return !_preview.cancelled;
	}) | rpl::start_with_next([=](const QString &d) {
		_description = d;
	}, _previewLifetime);

	std::move(
		page
	) | rpl::filter([=] {
		return !_preview.cancelled;
	}) | rpl::start_with_next([=](WebPageData *p) {
		_preview.data = p;
		updateVisible();
	}, _previewLifetime);
}

void FieldHeader::previewUnregister() {
	_previewLifetime.destroy();
}

void FieldHeader::paintWebPage(Painter &p, not_null<PeerData*> context) {
	Expects(ShowWebPagePreview(_preview.data));

	const auto textTop = st::msgReplyPadding.top();
	auto previewLeft = st::historyReplySkip + st::webPageLeft;
	p.fillRect(
		st::historyReplySkip,
		textTop,
		st::webPageBar,
		st::msgReplyBarSize.height(),
		st::msgInReplyBarColor);

	const QRect to(
		previewLeft,
		textTop,
		st::msgReplyBarSize.height(),
		st::msgReplyBarSize.height());
	if (HistoryView::DrawWebPageDataPreview(p, _preview.data, context, to)) {
		previewLeft += st::msgReplyBarSize.height()
			+ st::msgReplyBarSkip
			- st::msgReplyBarSize.width()
			- st::msgReplyBarPos.x();
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
		const auto top = (st::msgReplyPadding.top()
			+ (st::msgReplyBarSize.height() - st::msgDateFont->height) / 2);
		p.drawText(
			replySkip,
			top + st::msgDateFont->ascent,
			st::msgDateFont->elided(
				tr::lng_profile_loading(tr::now),
				availableWidth));
		return;
	}

	if (!isEditingMessage()) {
		const auto user = _shownMessage->displayFrom()
			? _shownMessage->displayFrom()
			: _shownMessage->author().get();
		if (_shownMessageNameVersion < user->nameVersion()) {
			_shownMessageName.setText(
				st::msgNameStyle,
				user->name(),
				Ui::NameTextOptions());
			_shownMessageNameVersion = user->nameVersion();
		}
	}

	const auto media = _shownMessage->media();
	_shownMessageHasPreview = media && media->hasReplyPreview();
	const auto preview = _shownMessageHasPreview
		? media->replyPreview()
		: nullptr;
	const auto spoilered = preview && media->hasSpoiler();
	if (!spoilered) {
		_shownPreviewSpoiler = nullptr;
	} else if (!_shownPreviewSpoiler) {
		_shownPreviewSpoiler = std::make_unique<Ui::SpoilerAnimation>([=] {
			update();
		});
	}
	const auto previewSkipValue = st::msgReplyBarSize.height()
		+ st::msgReplyBarSkip
		- st::msgReplyBarSize.width()
		- st::msgReplyBarPos.x();
	const auto previewSkip = _shownMessageHasPreview ? previewSkipValue : 0;
	const auto textLeft = replySkip + previewSkip;
	const auto textAvailableWidth = availableWidth - previewSkip;
	if (preview) {
		const auto overEdit = _photoEditAllowed
			? _inPhotoEditOver.value(_inPhotoEdit ? 1. : 0.)
			: 0.;
		const auto to = QRect(
			replySkip,
			st::msgReplyPadding.top(),
			st::msgReplyBarSize.height(),
			st::msgReplyBarSize.height());
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

FullMsgId FieldHeader::replyingToMessage() const {
	return _replyToId.current();
}

bool FieldHeader::hasPreview() const {
	return ShowWebPagePreview(_preview.data);
}

WebPageId FieldHeader::webPageId() const {
	return hasPreview() ? _preview.data->id : CancelledWebPageId;
}

MsgId FieldHeader::getDraftMessageId() const {
	return (isEditingMessage() ? _editMsgId : _replyToId).current().msg;
}

void FieldHeader::updateControlsGeometry(QSize size) {
	const auto isReadyToForward = readyToForward();
	const auto skip = isReadyToForward ? 0 : st::historyReplySkip;
	_cancel->moveToRight(0, 0);
	_clickableRect = QRect(
		skip,
		0,
		width() - skip - _cancel->width(),
		height());
	_shownMessagePreviewRect = QRect(
		st::historyReplySkip,
		st::msgReplyPadding.top(),
		st::msgReplyBarSize.height(),
		st::msgReplyBarSize.height());
}

void FieldHeader::editMessage(FullMsgId id, bool photoEditAllowed) {
	_photoEditAllowed = photoEditAllowed;
	_editMsgId = id;
	if (!photoEditAllowed) {
		_inPhotoEdit = false;
		_inPhotoEditOver.stop();
	}
	update();
}

void FieldHeader::replyToMessage(FullMsgId id) {
	_replyToId = id;
}

void FieldHeader::updateForwarding(
		Data::Thread *thread,
		Data::ResolvedForwardDraft items) {
	_forwardPanel->update(thread, std::move(items));
	if (readyToForward()) {
		replyToMessage({});
	}
	updateControlsGeometry(size());
}

rpl::producer<FullMsgId> FieldHeader::editMsgIdValue() const {
	return _editMsgId.value();
}

rpl::producer<FullMsgId> FieldHeader::scrollToItemRequests() const {
	return _scrollToItemRequests.events();
}

rpl::producer<> FieldHeader::editPhotoRequests() const {
	return _editPhotoRequests.events();
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
			.removeWebPageId = !hasPreview(),
		},
	};
}

ComposeControls::ComposeControls(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::SessionController*> controller,
	Fn<void(not_null<DocumentData*>)> unavailableEmojiPasted,
	Mode mode,
	SendMenu::Type sendMenuType)
: ComposeControls(parent, ComposeControlsDescriptor{
	.show = controller->uiShow(),
	.unavailableEmojiPasted = std::move(unavailableEmojiPasted),
	.mode = mode,
	.sendMenuType = sendMenuType,
	.regularWindow = controller,
	.stickerOrEmojiChosen = controller->stickerOrEmojiChosen(),
}) {
}

ComposeControls::ComposeControls(
	not_null<Ui::RpWidget*> parent,
	ComposeControlsDescriptor descriptor)
: _st(descriptor.stOverride
	? *descriptor.stOverride
	: st::defaultComposeControls)
, _features(descriptor.features)
, _parent(parent)
, _show(std::move(descriptor.show))
, _session(&_show->session())
, _regularWindow(descriptor.regularWindow)
, _ownedSelector(_regularWindow
	? nullptr
	: std::make_unique<ChatHelpers::TabbedSelector>(
		_parent,
		ChatHelpers::TabbedSelectorDescriptor{
			.show = _show,
			.st = _st.tabbed,
			.level = Window::GifPauseReason::TabbedPanel,
			.mode = ChatHelpers::TabbedSelector::Mode::Full,
			.features = _features,
		}))
, _selector(_regularWindow
	? _regularWindow->tabbedSelector()
	: not_null(_ownedSelector.get()))
, _mode(descriptor.mode)
, _wrap(std::make_unique<Ui::RpWidget>(parent))
, _writeRestricted(std::make_unique<Ui::RpWidget>(parent))
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
, _autocomplete(std::make_unique<FieldAutocomplete>(
	parent,
	_show,
	&_st.tabbed))
, _header(std::make_unique<FieldHeader>(_wrap.get(), _show))
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
, _sendMenuType(descriptor.sendMenuType)
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
	init();
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

void ComposeControls::setHistory(SetHistoryArgs &&args) {
	_showSlowmodeError = std::move(args.showSlowmodeError);
	_sendActionFactory = std::move(args.sendActionFactory);
	_slowmodeSecondsLeft = rpl::single(0)
		| rpl::then(std::move(args.slowmodeSecondsLeft));
	_sendDisabledBySlowmode = rpl::single(false)
		| rpl::then(std::move(args.sendDisabledBySlowmode));
	_liked = args.liked ? std::move(args.liked) : rpl::single(false);
	_writeRestriction = rpl::single(std::optional<QString>())
		| rpl::then(std::move(args.writeRestriction));
	const auto history = *args.history;
	if (_history == history) {
		return;
	}
	unregisterDraftSources();
	_history = history;
	_historyLifetime.destroy();
	_header->setHistory(args);
	registerDraftSource();
	_selector->setCurrentPeer(history ? history->peer.get() : nullptr);
	initWebpageProcess();
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

void ComposeControls::setCurrentDialogsEntryState(Dialogs::EntryState state) {
	unregisterDraftSources();
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
	_writeRestricted->move(x, y);
}

void ComposeControls::resizeToWidth(int width) {
	_wrap->resizeToWidth(width);
	_writeRestricted->resizeToWidth(width);
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
		_writeRestriction.value() | rpl::map(!_1),
		_wrap->heightValue(),
		_writeRestricted->heightValue());
}

int ComposeControls::heightCurrent() const {
	return _writeRestriction.current()
		? _writeRestricted->height()
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
	} else if (_canReplaceMedia) {
		EditCaptionBox::StartMediaReplace(
			_regularWindow,
			_editingId,
			std::move(list),
			_field->getTextWithTags(),
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
	if (_voiceRecordBar) {
		_voiceRecordBar->hideFast();
	}
	if (_autocomplete) {
		_autocomplete->hideFast();
	}
	_wrap->hide();
	_writeRestricted->hide();
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
	if (_voiceRecordBar) {
		_voiceRecordBar->hideFast();
	}
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
	if (_raiseEmojiSuggestions) {
		_raiseEmojiSuggestions();
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

	if (_preview) {
		_preview->cancel();
		_preview->setState(Data::PreviewState::Allowed);
	}
}

void ComposeControls::saveFieldToHistoryLocalDraft() {
	const auto key = draftKeyCurrent();
	if (!_history || !key) {
		return;
	}
	const auto id = _header->getDraftMessageId();
	if (_preview && (id || !_field->empty())) {
		const auto key = draftKeyCurrent();
		_history->setDraft(
			key,
			std::make_unique<Data::Draft>(
				_field,
				_header->getDraftMessageId(),
				key.topicRootId(),
				_preview->state()));
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

void ComposeControls::checkAutocomplete() {
	if (!_history) {
		return;
	}

	const auto peer = _history->peer;
	const auto autocomplete = _isInlineBot
		? AutocompleteQuery()
		: ParseMentionHashtagBotCommandQuery(_field, _features);
	if (!autocomplete.query.isEmpty()) {
		if (autocomplete.query[0] == '#'
			&& cRecentWriteHashtags().isEmpty()
			&& cRecentSearchHashtags().isEmpty()) {
			peer->session().local().readRecentHashtagsAndBots();
		} else if (autocomplete.query[0] == '@'
			&& cRecentInlineBots().isEmpty()) {
			peer->session().local().readRecentHashtagsAndBots();
		} else if (autocomplete.query[0] == '/'
			&& peer->isUser()
			&& !peer->asUser()->isBot()) {
			return;
		}
	}
	_autocomplete->showFiltered(
		peer,
		autocomplete.query,
		autocomplete.fromStart);
}

void ComposeControls::hide() {
	showStarted();
	_hidden = true;
}

void ComposeControls::show() {
	if (_hidden.current()) {
		_hidden = false;
		showFinished();
		checkAutocomplete();
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
		paintBackground(clip);
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
		EditCaptionBox::StartPhotoEdit(
			_regularWindow,
			_photoEditMedia,
			_editingId,
			_field->getTextWithTags(),
			crl::guard(_wrap.get(), [=] { cancelEditMessage(); }));
	}, _wrap->lifetime());

	_header->previewCancelled(
	) | rpl::start_with_next([=] {
		if (_preview) {
			_preview->setState(Data::PreviewState::Cancelled);
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
	return ::Media::Capture::instance()->available()
		&& !_voiceRecordBar->isListenState()
		&& !_voiceRecordBar->isRecordingByAnotherBar()
		&& !HasSendText(_field)
		&& !readyToForward()
		&& !isEditingMessage();
}

void ComposeControls::clearListenState() {
	_voiceRecordBar->clearListenState();
}

void ComposeControls::drawRestrictedWrite(QPainter &p, const QString &error) {
	p.fillRect(_writeRestricted->rect(), st::historyReplyBg);

	p.setFont(st::normalFont);
	p.setPen(st::windowSubTextFg);
	p.drawText(
		_writeRestricted->rect().marginsRemoved(
			QMargins(st::historySendPadding, 0, st::historySendPadding, 0)),
		error,
		style::al_center);
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
					.replyId = replyingToMessage(),
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
	_field->tabbed(
	) | rpl::start_with_next([=] {
		fieldTabbed();
	}, _field->lifetime());
	_field->heightChanges(
	) | rpl::start_with_next([=] {
		updateHeight();
	}, _field->lifetime());
	_field->changes(
	) | rpl::start_with_next([=] {
		fieldChanged();
	}, _field->lifetime());
	InitMessageField(_show, _field, [=](not_null<DocumentData*> emoji) {
		if (_history && Data::AllowEmojiWithoutPremium(_history->peer)) {
			return true;
		}
		if (_unavailableEmojiPasted) {
			_unavailableEmojiPasted(emoji);
		}
		return false;
	});
	_field->setEditLinkCallback(
		DefaultEditLinkCallback(_show, _field, &_st.boxField));
	initAutocomplete();
	const auto allow = [=](const auto &) {
		return _history && Data::AllowEmojiWithoutPremium(_history->peer);
	};
	const auto suggestions = Ui::Emoji::SuggestionsController::Init(
		_parent,
		_field,
		_session,
		{
			.suggestCustomEmoji = true,
			.allowCustomWithoutPremium = allow,
			.st = &_st.suggestions,
		});
	_raiseEmojiSuggestions = [=] { suggestions->raise(); };

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

void ComposeControls::initAutocomplete() {
	const auto insertHashtagOrBotCommand = [=](
			const QString &string,
			FieldAutocomplete::ChooseMethod method) {
		// Send bot command at once, if it was not inserted by pressing Tab.
		if (string.at(0) == '/' && method != FieldAutocomplete::ChooseMethod::ByTab) {
			_sendCommandRequests.fire_copy(string);
			setText(
				_field->getTextWithTagsPart(_field->textCursor().position()));
		} else {
			_field->insertTag(string);
		}
	};

	_autocomplete->mentionChosen(
	) | rpl::start_with_next([=](FieldAutocomplete::MentionChosen data) {
		const auto user = data.user;
		if (data.mention.isEmpty()) {
			_field->insertTag(
				user->firstName.isEmpty() ? user->name() : user->firstName,
				PrepareMentionTag(user));
		} else {
			_field->insertTag('@' + data.mention);
		}
	}, _autocomplete->lifetime());

	_autocomplete->hashtagChosen(
	) | rpl::start_with_next([=](FieldAutocomplete::HashtagChosen data) {
		insertHashtagOrBotCommand(data.hashtag, data.method);
	}, _autocomplete->lifetime());

	_autocomplete->botCommandChosen(
	) | rpl::start_with_next([=](FieldAutocomplete::BotCommandChosen data) {
		insertHashtagOrBotCommand(data.command, data.method);
	}, _autocomplete->lifetime());

	_autocomplete->stickerChosen(
	) | rpl::start_with_next([=](FieldAutocomplete::StickerChosen data) {
		if (!_showSlowmodeError || !_showSlowmodeError()) {
			setText({});
		}
		//_saveDraftText = true;
		//_saveDraftStart = crl::now();
		//saveDraft();
		//saveCloudDraft(); // won't be needed if SendInlineBotResult will clear the cloud draft
		_fileChosen.fire(std::move(data));
	}, _autocomplete->lifetime());

	_autocomplete->choosingProcesses(
	) | rpl::start_with_next([=](FieldAutocomplete::Type type) {
		if (type == FieldAutocomplete::Type::Stickers) {
			_sendActionUpdates.fire({
				.type = Api::SendProgressType::ChooseSticker,
			});
		}
	}, _autocomplete->lifetime());

	_autocomplete->setSendMenuType([=] { return sendMenuType(); });

	//_autocomplete->setModerateKeyActivateCallback([=](int key) {
	//	return _keyboard->isHidden()
	//		? false
	//		: _keyboard->moderateKeyActivate(key);
	//});

	_field->rawTextEdit()->installEventFilter(_autocomplete.get());

	_session->data().botCommandsChanges(
	) | rpl::filter([=](not_null<PeerData*> peer) {
		return _history && (_history->peer == peer);
	}) | rpl::start_with_next([=] {
		if (_autocomplete->clearFilteredBotCommands()) {
			checkAutocomplete();
		}
	}, _autocomplete->lifetime());

	_session->data().stickers().updated(
		Data::StickersType::Stickers
	) | rpl::start_with_next([=] {
		updateStickersByEmoji();
	}, _autocomplete->lifetime());

	QObject::connect(
		_field->rawTextEdit(),
		&QTextEdit::cursorPositionChanged,
		_autocomplete.get(),
		[=] { checkAutocomplete(); },
		Qt::QueuedConnection);

	_autocomplete->hideFast();
}

bool ComposeControls::updateStickersByEmoji() {
	if (!_history) {
		return false;
	}
	const auto emoji = [&] {
		const auto errorForStickers = Data::RestrictionError(
			_history->peer,
			ChatRestriction::SendStickers);
		if (!isEditingMessage() && !errorForStickers) {
			const auto &text = _field->getTextWithTags().text;
			auto length = 0;
			if (const auto emoji = Ui::Emoji::Find(text, &length)) {
				if (text.size() <= length) {
					return emoji;
				}
			}
		}
		return EmojiPtr(nullptr);
	}();
	_autocomplete->showStickers(emoji);
	return (emoji != nullptr);
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
	if (!_hasSendText.current() && _preview) {
		_preview->setState(Data::PreviewState::Allowed);
	}
	if (updateBotCommandShown() || updateLikeShown()) {
		updateControlsVisibility();
		updateControlsGeometry(_wrap->size());
	}
	InvokeQueued(_autocomplete.get(), [=] {
		updateInlineBotQuery();
		const auto choosingSticker = updateStickersByEmoji();
		if (!choosingSticker && typing) {
			_sendActionUpdates.fire({ Api::SendProgressType::Typing });
		}
	});

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
			? Key::LocalEdit(_currentDialogsEntryState.rootId)
			: Key::Local(_currentDialogsEntryState.rootId);
	case Section::Scheduled:
		return (type == DraftType::Edit)
			? Key::ScheduledEdit()
			: Key::Scheduled();
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
				_header->getDraftMessageId(),
				_field->getTextWithTags(),
				_preview->state(),
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
	const auto editingId = (draft == editDraft)
		? FullMsgId{ _history->peer->id, draft ? draft->msgId : 0 }
		: FullMsgId();

	InvokeQueued(_autocomplete.get(), [=] { updateStickersByEmoji(); });
	const auto guard = gsl::finally([&] {
		updateSendButtonType();
		updateReplaceMediaButton();
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
		_preview->refreshState(Data::PreviewState::Allowed, false);
		_canReplaceMedia = false;
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
		const auto disablePreview = (editDraft != nullptr);
		_preview->refreshState(draft->previewState, disablePreview);
	}

	if (draft == editDraft) {
		const auto resolve = [=] {
			if (const auto item = _history->owner().message(editingId)) {
				const auto media = item->media();
				const auto disablePreview = media && !media->webpage();
				_canReplaceMedia = media && media->allowsEditMedia();
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
				_preview->refreshState(_preview->state(), disablePreview);
				return true;
			}
			_canReplaceMedia = false;
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
		_canReplaceMedia = false;
		_photoEditMedia = nullptr;
		_header->replyToMessage({ _history->peer->id, draft->msgId });
		if (_header->replyingToMessage()) {
			cancelForward();
		}
		_header->editMessage({});
	}
}

void ComposeControls::cancelForward() {
	_history->setForwardDraft(
		_currentDialogsEntryState.rootId,
		{});
	updateForwarding();
}

void ComposeControls::fieldTabbed() {
	if (!_autocomplete->isHidden()) {
		_autocomplete->chooseSelected(FieldAutocomplete::ChooseMethod::ByTab);
	}
}

rpl::producer<SendActionUpdate> ComposeControls::sendActionUpdates() const {
	return rpl::merge(
		_sendActionUpdates.events(),
		_voiceRecordBar->sendActionUpdates());
}

void ComposeControls::initTabbedSelector() {
	if (!_regularWindow || _regularWindow->hasTabbedSelectorOwnership()) {
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
					|| !Data::AllowEmojiWithoutPremium(_history->peer))) {
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
		_selector->showMenuWithType(sendMenuType());
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

	const auto send = [=](Api::SendOptions options) {
		_sendCustomRequests.fire(std::move(options));
	};

	SendMenu::SetupMenuAndShortcuts(
		_send.get(),
		[=] { return sendButtonMenuType(); },
		SendMenu::DefaultSilentCallback(send),
		SendMenu::DefaultScheduleCallback(_wrap.get(), sendMenuType(), send),
		SendMenu::DefaultWhenOnlineCallback(send));
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
	checkAutocomplete();
}

void ComposeControls::inlineBotChanged() {
	const auto isInlineBot = (_inlineBot && !_inlineLookingUpBot);
	if (_isInlineBot != isInlineBot) {
		_isInlineBot = isInlineBot;
		updateFieldPlaceholder();
		updateSubmitSettings();
		checkAutocomplete();
	}
}

void ComposeControls::initWriteRestriction() {
	_writeRestricted->resize(
		_writeRestricted->width(),
		st::historyUnblock.height);
	_writeRestricted->paintRequest(
	) | rpl::start_with_next([=] {
		if (const auto error = _writeRestriction.current()) {
			auto p = Painter(_writeRestricted.get());
			drawRestrictedWrite(p, *error);
		}
	}, _wrap->lifetime());

	_writeRestriction.value(
	) | rpl::filter([=] {
		return _wrap->isHidden() || _writeRestricted->isHidden();
	}) | rpl::start_with_next([=] {
		updateWrappingVisibility();
	}, _wrap->lifetime());
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
		const auto error = [&]() -> std::optional<QString> {
			const auto peer = _history ? _history->peer.get() : nullptr;
			if (peer) {
				if (const auto error = Data::RestrictionError(
						peer,
						ChatRestriction::SendVoiceMessages)) {
					return error;
				}
			}
			return std::nullopt;
		}();
		if (error) {
			_show->showToast(*error);
			return true;
		} else if (_showSlowmodeError && _showSlowmodeError()) {
			return true;
		}
		return false;
	});

	_voiceRecordBar->updateSendButtonTypeRequests(
	) | rpl::start_with_next([=] {
		updateSendButtonType();
	}, _wrap->lifetime());
}

void ComposeControls::updateWrappingVisibility() {
	const auto hidden = _hidden.current();
	const auto restricted = _writeRestriction.current().has_value();
	_writeRestricted->setVisible(!hidden && restricted);
	_wrap->setVisible(!hidden && !restricted);
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
		return Type::Record;
	}
	return (_mode == Mode::Normal) ? Type::Send : Type::Schedule;
}

SendMenu::Type ComposeControls::sendMenuType() const {
	return !_history ? SendMenu::Type::Disabled : _sendMenuType;
}

SendMenu::Type ComposeControls::sendButtonMenuType() const {
	return (computeSendButtonType() == Ui::SendButton::Type::Send)
		? sendMenuType()
		: SendMenu::Type::Disabled;
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
		&& (type == Type::Send || type == Type::Record));
}

void ComposeControls::finishAnimating() {
	_send->finishAnimating();
	_voiceRecordBar->finishAnimating();
}

void ComposeControls::updateControlsGeometry(QSize size) {
	// (_attachToggle|_replaceMedia) (_sendAs) -- _inlineResults ------ _tabbedPanel -- _fieldBarCancel
	// (_attachDocument|_attachPhoto) _field (_ttlInfo) (_silent|_botCommandStart) _tabbedSelectorToggle _send

	const auto fieldWidth = size.width()
		- _attachToggle->width()
		- (_sendAs ? _sendAs->width() : 0)
		- st::historySendRight
		- _send->width()
		- _tabbedSelectorToggle->width()
		- (_likeShown ? _like->width() : 0)
		- (_botCommandShown ? _botCommandStart->width() : 0)
		- (_silent ? _silent->width() : 0)
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
		_like->moveToRight(right, buttonsTop);
		if (_likeShown) {
			right += _like->width();
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
		_parent,
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

void ComposeControls::paintBackground(QRect clip) {
	Painter p(_wrap.get());

	if (_backgroundRect) {
		//p.setCompositionMode(QPainter::CompositionMode_Source);
		//p.fillRect(clip, Qt::transparent);
		//p.setCompositionMode(QPainter::CompositionMode_SourceOver);
		//_backgroundRect->paint(p, _wrap->rect());
		auto hq = PainterHighQualityEnabler(p);
		p.setBrush(_st.bg);
		p.setPen(Qt::NoPen);
		p.drawRoundedRect(_wrap->rect(), _st.radius, _st.radius);
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
	if (!_tabbedPanel || !_regularWindow) {
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
		_parent,
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
	if (!_history || !_regularWindow) {
		return;
	}
	if (_tabbedPanel) {
		if (_regularWindow->canShowThirdSection()
				&& !_regularWindow->adaptive().isOneColumn()) {
			Core::App().settings().setTabbedSelectorSectionEnabled(true);
			Core::App().saveSettingsDelayed();
			const auto topic = _history->peer->forumTopicFor(
				_currentDialogsEntryState.rootId);
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

void ComposeControls::editMessage(FullMsgId id) {
	if (const auto item = session().data().message(id)) {
		editMessage(item);
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
		QFIXED_MAX
	};
	const auto previewPage = [&]() -> WebPageData* {
		if (const auto media = item->media()) {
			return media->webpage();
		}
		return nullptr;
	}();
	const auto previewState = previewPage
		? Data::PreviewState::Allowed
		: Data::PreviewState::EmptyOnEdit;
	const auto key = draftKey(DraftType::Edit);
	_history->setDraft(
		key,
		std::make_unique<Data::Draft>(
			editData,
			item->id,
			key.topicRootId(),
			cursor,
			previewState));
	applyDraft();
	if (updateReplaceMediaButton()) {
		updateControlsVisibility();
		updateControlsGeometry(_wrap->size());
	}

	if (_autocomplete) {
		InvokeQueued(_autocomplete.get(), [=] { checkAutocomplete(); });
	}
}

bool ComposeControls::updateReplaceMediaButton() {
	if (!_canReplaceMedia || !_regularWindow) {
		const auto result = (_replaceMedia != nullptr);
		_replaceMedia = nullptr;
		return result;
	} else if (_replaceMedia) {
		return false;
	}
	_replaceMedia = std::make_unique<Ui::IconButton>(
		_wrap.get(),
		st::historyReplaceMedia);
	_replaceMedia->setClickedCallback([=] {
		EditCaptionBox::StartMediaReplace(
			_regularWindow,
			_editingId,
			_field->getTextWithTags(),
			crl::guard(_wrap.get(), [=] { cancelEditMessage(); }));
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

void ComposeControls::replyToMessage(FullMsgId id) {
	Expects(_history != nullptr);
	Expects(draftKeyCurrent() != Data::DraftKey::None());

	if (!id) {
		cancelReplyMessage();
		return;
	}
	if (isEditingMessage()) {
		const auto key = draftKey(DraftType::Normal);
		if (const auto localDraft = _history->draft(key)) {
			localDraft->msgId = id.msg;
		} else {
			_history->setDraft(
				key,
				std::make_unique<Data::Draft>(
					TextWithTags(),
					id.msg,
					key.topicRootId(),
					MessageCursor(),
					Data::PreviewState::Allowed));
		}
	} else {
		_header->replyToMessage(id);
		if (_header->replyingToMessage()) {
			cancelForward();
		}
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
			if (localDraft->msgId) {
				if (localDraft->textWithTags.text.isEmpty()) {
					_history->clearDraft(key);
				} else {
					localDraft->msgId = 0;
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
	const auto rootId = _currentDialogsEntryState.rootId;
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
	} else if (readyToForward()) {
		cancelForward();
		return true;
	} else if (replyingToMessage()) {
		cancelReplyMessage();
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

	_preview = std::make_unique<WebpageProcessor>(_history, _field);

	_preview->paintRequests(
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
			_preview->checkPreview();
			updateStickersByEmoji();
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

	_header->previewRequested(
		_preview->titleChanges(),
		_preview->descriptionChanges(),
		_preview->pageDataChanges());
}

void ComposeControls::initForwardProcess() {
	using EntryUpdateFlag = Data::EntryUpdate::Flag;
	session().changes().entryUpdates(
		EntryUpdateFlag::ForwardDraft
	) | rpl::start_with_next([=](const Data::EntryUpdate &update) {
		if (const auto topic = update.entry->asTopic()) {
			if (topic->history() == _history
				&& topic->rootId() == _currentDialogsEntryState.rootId) {
				updateForwarding();
			}
		}
	}, _wrap->lifetime());

	updateForwarding();
}

WebPageId ComposeControls::webPageId() const {
	return _header->webPageId();
}

rpl::producer<Data::MessagePosition> ComposeControls::scrollRequests() const {
	return _header->scrollToItemRequests(
		) | rpl::map([=](FullMsgId id) -> Data::MessagePosition {
			if (const auto item = session().data().message(id)) {
				return item->position();
			}
			return {};
		});
}

bool ComposeControls::isEditingMessage() const {
	return _header->isEditingMessage();
}

FullMsgId ComposeControls::replyingToMessage() const {
	return _header->replyingToMessage();
}

bool ComposeControls::readyToForward() const {
	return _header->readyToForward();
}

bool ComposeControls::isLockPresent() const {
	return _voiceRecordBar->isLockPresent();
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
			|| (_send->type() == Ui::SendButton::Type::Record
				&& _send->isDown()));
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
				MTPcontacts_ResolveUsername(MTP_string(username))
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
				_parent,
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
			_inlineResults->setSendMenuType([=] { return sendMenuType(); });
			_inlineResults->requesting(
			) | rpl::start_with_next([=](bool requesting) {
				_tabbedSelectorToggle->setLoading(requesting);
			}, _inlineResults->lifetime());
			updateOuterGeometry(_wrap->geometry());
		}
		_inlineResults->queryInlineBot(_inlineBot, _history->peer, query);
		if (!_autocomplete->isHidden()) {
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
		if (!insertTextOnCancel.isEmpty()) {
			_field->textCursor().insertText(insertTextOnCancel);
		}
	});
}

} // namespace HistoryView
