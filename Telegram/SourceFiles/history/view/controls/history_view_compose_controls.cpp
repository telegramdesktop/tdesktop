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
#include "data/stickers/data_stickers.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_web_page.h"
#include "storage/storage_account.h"
#include "apiwrap.h"
#include "api/api_chat_participants.h"
#include "ui/boxes/confirm_box.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/controls/history_view_voice_record_bar.h"
#include "history/view/controls/history_view_ttl_button.h"
#include "history/view/history_view_webpage_preview.h"
#include "inline_bots/inline_results_widget.h"
#include "inline_bots/inline_bot_result.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "main/session/send_as_peers.h"
#include "media/audio/media_audio_capture.h"
#include "media/audio/media_audio.h"
#include "styles/style_chat.h"
#include "ui/text/text_options.h"
#include "ui/ui_utility.h"
#include "ui/widgets/input_fields.h"
#include "ui/text/format_values.h"
#include "ui/controls/emoji_button.h"
#include "ui/controls/send_button.h"
#include "ui/controls/send_as_button.h"
#include "ui/chat/choose_send_as.h"
#include "ui/special_buttons.h"
#include "window/window_adaptive.h"
#include "window/window_session_controller.h"
#include "mainwindow.h"

namespace HistoryView {
namespace {

constexpr auto kSaveDraftTimeout = crl::time(1000);
constexpr auto kSaveDraftAnywayTimeout = 5 * crl::time(1000);
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
using VoiceRecordBar = HistoryView::Controls::VoiceRecordBar;

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
	void refreshState(Data::PreviewState value);

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

void WebpageProcessor::refreshState(Data::PreviewState value) {
	// Save links from _field to _parsedLinks without generating preview.
	_previewState = Data::PreviewState::Cancelled;
	_fieldLinksParser.parseNow();
	_parsedLinks = _fieldLinksParser.list().current();
	_previewState = value;
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
	FieldHeader(QWidget *parent, not_null<Data::Session*> data);

	void setHistory(const SetHistoryArgs &args);
	void init();

	void editMessage(FullMsgId id);
	void replyToMessage(FullMsgId id);
	void previewRequested(
		rpl::producer<QString> title,
		rpl::producer<QString> description,
		rpl::producer<WebPageData*> page);

	[[nodiscard]] bool isDisplayed() const;
	[[nodiscard]] bool isEditingMessage() const;
	[[nodiscard]] FullMsgId replyingToMessage() const;
	[[nodiscard]] rpl::producer<FullMsgId> editMsgId() const;
	[[nodiscard]] rpl::producer<FullMsgId> scrollToItemRequests() const;
	[[nodiscard]] MessageToEdit queryToEdit();
	[[nodiscard]] WebPageId webPageId() const;

	[[nodiscard]] MsgId getDraftMessageId() const;
	[[nodiscard]] rpl::producer<> editCancelled() const {
		return _editCancelled.events();
	}
	[[nodiscard]] rpl::producer<> replyCancelled() const {
		return _replyCancelled.events();
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

	struct Preview {
		WebPageData *data = nullptr;
		Ui::Text::String title;
		Ui::Text::String description;
		bool cancelled = false;
	};

	History *_history = nullptr;
	rpl::variable<QString> _title;
	rpl::variable<QString> _description;

	Preview _preview;
	rpl::event_stream<> _editCancelled;
	rpl::event_stream<> _replyCancelled;
	rpl::event_stream<> _previewCancelled;

	bool hasPreview() const;

	rpl::variable<FullMsgId> _editMsgId;
	rpl::variable<FullMsgId> _replyToId;

	HistoryItem *_shownMessage = nullptr;
	Ui::Text::String _shownMessageName;
	Ui::Text::String _shownMessageText;
	int _shownMessageNameVersion = -1;
	bool _repaintScheduled = false;

	const not_null<Data::Session*> _data;
	const not_null<Ui::IconButton*> _cancel;

	QRect _clickableRect;

	rpl::event_stream<bool> _visibleChanged;
	rpl::event_stream<FullMsgId> _scrollToItemRequests;

};

FieldHeader::FieldHeader(QWidget *parent, not_null<Data::Session*> data)
: RpWidget(parent)
, _data(data)
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

	const auto leftIconPressed = lifetime().make_state<bool>(false);
	paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(this);
		p.fillRect(rect(), st::historyComposeAreaBg);

		const auto position = st::historyReplyIconPosition;
		if (isEditingMessage()) {
			st::historyEditIcon.paint(p, position, width());
		} else if (replyingToMessage()) {
			st::historyReplyIcon.paint(p, position, width());
		}

		(!ShowWebPagePreview(_preview.data) || *leftIconPressed)
			? paintEditOrReplyToMessage(p)
			: paintWebPage(
				p,
				_history ? _history->peer : _data->session().user());
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
	const auto inClickable = lifetime().make_state<bool>(false);
	events(
	) | rpl::filter([=](not_null<QEvent*> event) {
		return ranges::contains(kMouseEvents, event->type())
			&& (isEditingMessage() || replyingToMessage());
	}) | rpl::start_with_next([=](not_null<QEvent*> event) {
		const auto type = event->type();
		const auto e = static_cast<QMouseEvent*>(event.get());
		const auto pos = e ? e->pos() : mapFromGlobal(QCursor::pos());
		const auto inPreviewRect = _clickableRect.contains(pos);

		if (type == QEvent::MouseMove) {
			if (inPreviewRect != *inClickable) {
				*inClickable = inPreviewRect;
				setCursor(*inClickable
					? style::cur_pointer
					: style::cur_default);
			}
			return;
		}
		const auto isLeftIcon = (pos.x() < st::historyReplySkip);
		const auto isLeftButton = (e->button() == Qt::LeftButton);
		if (type == QEvent::MouseButtonPress) {
			if (isLeftButton && isLeftIcon) {
				*leftIconPressed = true;
				update();
			} else if (isLeftButton && inPreviewRect) {
				auto id = isEditingMessage()
					? _editMsgId.current()
					: replyingToMessage();
				_scrollToItemRequests.fire(std::move(id));
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

	std::move(
		title
	) | rpl::filter([=] {
		return !_preview.cancelled;
	}) | start_with_next([=](const QString &t) {
		_title = t;
	}, lifetime());

	std::move(
		description
	) | rpl::filter([=] {
		return !_preview.cancelled;
	}) | rpl::start_with_next([=](const QString &d) {
		_description = d;
	}, lifetime());

	std::move(
		page
	) | rpl::filter([=] {
		return !_preview.cancelled;
	}) | rpl::start_with_next([=](WebPageData *p) {
		_preview.data = p;
		updateVisible();
	}, lifetime());

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

	p.setPen(st::historyReplyNameFg);
	p.setFont(st::msgServiceNameFont);
	_shownMessageName.drawElided(
		p,
		replySkip,
		st::msgReplyPadding.top(),
		availableWidth);

	p.setPen(st::historyComposeAreaFg);
	p.setTextPalette(st::historyComposeAreaPalette);
	_shownMessageText.drawElided(
		p,
		replySkip,
		st::msgReplyPadding.top() + st::msgServiceNameFont->height,
		availableWidth);
	p.restoreTextPalette();
}

void FieldHeader::updateVisible() {
	isDisplayed() ? show() : hide();
	_visibleChanged.fire(isVisible());
}

rpl::producer<bool> FieldHeader::visibleChanged() {
	return _visibleChanged.events();
}

bool FieldHeader::isDisplayed() const {
	return isEditingMessage() || replyingToMessage() || hasPreview();
}

bool FieldHeader::isEditingMessage() const {
	return !!_editMsgId.current();
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
	_cancel->moveToRight(0, 0);
	_clickableRect = QRect(
		st::historyReplySkip,
		0,
		width() - st::historyReplySkip - _cancel->width(),
		height());
}

void FieldHeader::editMessage(FullMsgId id) {
	_editMsgId = id;
}

void FieldHeader::replyToMessage(FullMsgId id) {
	_replyToId = id;
}

rpl::producer<FullMsgId> FieldHeader::editMsgId() const {
	return _editMsgId.value();
}

rpl::producer<FullMsgId> FieldHeader::scrollToItemRequests() const {
	return _scrollToItemRequests.events();
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
	not_null<Window::SessionController*> window,
	Fn<void(not_null<DocumentData*>)> unavailableEmojiPasted,
	Mode mode,
	SendMenu::Type sendMenuType)
: _parent(parent)
, _window(window)
, _mode(mode)
, _wrap(std::make_unique<Ui::RpWidget>(parent))
, _writeRestricted(std::make_unique<Ui::RpWidget>(parent))
, _send(std::make_shared<Ui::SendButton>(_wrap.get()))
, _attachToggle(Ui::CreateChild<Ui::IconButton>(
	_wrap.get(),
	st::historyAttach))
, _tabbedSelectorToggle(Ui::CreateChild<Ui::EmojiButton>(
	_wrap.get(),
	st::historyAttachEmoji))
, _field(
	Ui::CreateChild<Ui::InputField>(
		_wrap.get(),
		st::historyComposeField,
		Ui::InputField::Mode::MultiLine,
		tr::lng_message_ph()))
, _botCommandStart(Ui::CreateChild<Ui::IconButton>(
	_wrap.get(),
	st::historyBotCommandStart))
, _autocomplete(std::make_unique<FieldAutocomplete>(
		parent,
		window))
, _header(std::make_unique<FieldHeader>(
		_wrap.get(),
		&_window->session().data()))
, _voiceRecordBar(std::make_unique<VoiceRecordBar>(
		_wrap.get(),
		parent,
		window,
		_send,
		st::historySendSize.height()))
, _sendMenuType(sendMenuType)
, _unavailableEmojiPasted(unavailableEmojiPasted)
, _saveDraftTimer([=] { saveDraft(); }) {
	init();
}

ComposeControls::~ComposeControls() {
	saveFieldToHistoryLocalDraft();
	unregisterDraftSources();
	setTabbedPanel(nullptr);
	session().api().request(_inlineBotResolveRequestId).cancel();
}

Main::Session &ComposeControls::session() const {
	return _window->session();
}

void ComposeControls::setHistory(SetHistoryArgs &&args) {
	// Right now only single non-null set of history is supported.
	// Otherwise initWebpageProcess should be updated / rewritten.
	Expects(!_history && *args.history);

	_showSlowmodeError = std::move(args.showSlowmodeError);
	_slowmodeSecondsLeft = rpl::single(0)
		| rpl::then(std::move(args.slowmodeSecondsLeft));
	_sendDisabledBySlowmode = rpl::single(false)
		| rpl::then(std::move(args.sendDisabledBySlowmode));
	_writeRestriction = rpl::single(std::optional<QString>())
		| rpl::then(std::move(args.writeRestriction));
	const auto history = *args.history;
	//if (_history == history) {
	//	return;
	//}
	unregisterDraftSources();
	_history = history;
	_header->setHistory(args);
	registerDraftSource();
	_window->tabbedSelector()->setCurrentPeer(
		history ? history->peer.get() : nullptr);
	initWebpageProcess();
	updateBotCommandShown();
	updateMessagesTTLShown();
	updateControlsGeometry(_wrap->size());
	updateControlsVisibility();
	updateFieldPlaceholder();
	updateSendAsButton();
	//if (!_history) {
	//	return;
	//}
	const auto peer = _history->peer;
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
}

void ComposeControls::setCurrentDialogsEntryState(Dialogs::EntryState state) {
	_currentDialogsEntryState = state;
	if (_inlineResults) {
		_inlineResults->setCurrentDialogsEntryState(state);
	}
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

bool ComposeControls::focus() {
	if (isRecording()) {
		return false;
	}
	_field->setFocus();
	return true;
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
	auto submits = base::qt_signal_producer(
		_field.get(),
		&Ui::InputField::submitted);
	return rpl::merge(
		_send->clicks() | filter | map,
		std::move(submits) | filter | map,
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
	auto submits = base::qt_signal_producer(
		_field.get(),
		&Ui::InputField::submitted);
	return rpl::merge(
		_send->clicks() | filter | toValue,
		std::move(submits) | filter | toValue);
}

rpl::producer<> ComposeControls::attachRequests() const {
	return rpl::merge(
		_attachToggle->clicks() | rpl::to_empty,
		_attachRequests.events()
	) | rpl::filter([=] {
		if (isEditingMessage()) {
			_window->show(Ui::MakeInformBox(tr::lng_edit_caption_attach()));
			return false;
		}
		return true;
	});
}

void ComposeControls::setMimeDataHook(MimeDataHook hook) {
	_field->setMimeDataHook(std::move(hook));
}

rpl::producer<FileChosen> ComposeControls::fileChosen() const {
	return _fileChosen.events();
}

rpl::producer<PhotoChosen> ComposeControls::photoChosen() const {
	return _photoChosen.events();
}

auto ComposeControls::inlineResultChosen() const
->rpl::producer<ChatHelpers::TabbedSelector::InlineChosen> {
	return _inlineResultChosen.events();
}

void ComposeControls::showStarted() {
	if (_inlineResults) {
		_inlineResults->hideFast();
	}
	if (_tabbedPanel) {
		_tabbedPanel->hideFast();
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
	if (!_history || key == Data::DraftKey::None()) {
		return;
	}
	const auto id = _header->getDraftMessageId();
	if (_preview && (id || !_field->empty())) {
		_history->setDraft(
			draftKeyCurrent(),
			std::make_unique<Data::Draft>(
				_field,
				_header->getDraftMessageId(),
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
		: ParseMentionHashtagBotCommandQuery(_field);
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
	initSendAsButton();
	initWriteRestriction();
	initVoiceRecordBar();
	initKeyHandler();

	_hidden.changes(
	) | rpl::start_with_next([=] {
		updateWrappingVisibility();
	}, _wrap->lifetime());

	_botCommandStart->setClickedCallback([=] { setText({ "/" }); });

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

	_header->editMsgId(
	) | rpl::start_with_next([=](const auto &id) {
		unregisterDraftSources();
		updateSendButtonType();
		if (_history && updateSendAsButton()) {
			updateControlsVisibility();
			updateControlsGeometry(_wrap->size());
		}
		registerDraftSource();
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

	{
		const auto lastMsgId = _wrap->lifetime().make_state<FullMsgId>();

		_header->editMsgId(
		) | rpl::filter([=](const auto &id) {
			return !!id;
		}) | rpl::start_with_next([=](const auto &id) {
			*lastMsgId = id;
		}, _wrap->lifetime());

		session().data().itemRemoved(
		) | rpl::filter([=](not_null<const HistoryItem*> item) {
			return item->id && ((*lastMsgId) == item->fullId());
		}) | rpl::start_with_next([=] {
			cancelEditMessage();
		}, _wrap->lifetime());
	}

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
		//&& !readyToForward()
		&& !isEditingMessage();
}

void ComposeControls::clearListenState() {
	_voiceRecordBar->clearListenState();
}

void ComposeControls::drawRestrictedWrite(Painter &p, const QString &error) {
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
		}
		return Result::Continue;
	});
}

void ComposeControls::initField() {
	_field->setMaxHeight(st::historyComposeFieldMaxHeight);
	updateSubmitSettings();
	//Ui::Connect(_field, &Ui::InputField::submitted, [=] { send(); });
	Ui::Connect(_field, &Ui::InputField::cancelled, [=] { escape(); });
	Ui::Connect(_field, &Ui::InputField::tabbed, [=] { fieldTabbed(); });
	Ui::Connect(_field, &Ui::InputField::resized, [=] { updateHeight(); });
	//Ui::Connect(_field, &Ui::InputField::focused, [=] { fieldFocused(); });
	Ui::Connect(_field, &Ui::InputField::changed, [=] { fieldChanged(); });
	InitMessageField(_window, _field, [=](not_null<DocumentData*> emoji) {
		if (_history && Data::AllowEmojiWithoutPremium(_history->peer)) {
			return true;
		}
		if (_unavailableEmojiPasted) {
			_unavailableEmojiPasted(emoji);
		}
		return false;
	});
	initAutocomplete();
	const auto allow = [=](const auto &) {
		return _history && Data::AllowEmojiWithoutPremium(_history->peer);
	};
	const auto suggestions = Ui::Emoji::SuggestionsController::Init(
		_parent,
		_field,
		&_window->session(),
		{ .suggestCustomEmoji = true, .allowCustomWithoutPremium = allow });
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
	const auto insertMention = [=](not_null<UserData*> user) {
		if (user->username.isEmpty()) {
			_field->insertTag(
				user->firstName.isEmpty() ? user->name() : user->firstName,
				PrepareMentionTag(user));
		} else {
			_field->insertTag('@' + user->username);
		}
	};

	_autocomplete->mentionChosen(
	) | rpl::start_with_next([=](FieldAutocomplete::MentionChosen data) {
		insertMention(data.user);
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
		_fileChosen.fire(FileChosen{
			.document = data.sticker,
			.options = data.options,
			.messageSendingFrom = base::take(data.messageSendingFrom),
		});
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

	_window->session().data().botCommandsChanges(
	) | rpl::filter([=](not_null<PeerData*> peer) {
		return _history && (_history->peer == peer);
	}) | rpl::start_with_next([=] {
		if (_autocomplete->clearFilteredBotCommands()) {
			checkAutocomplete();
		}
	}, _autocomplete->lifetime());

	_window->session().data().stickers().updated(
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
			_inlineBot->username.size() + 2);
		return;
	}

	_field->setPlaceholder([&] {
		if (isEditingMessage()) {
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
	if (!HasSendText(_field) && _preview) {
		_preview->setState(Data::PreviewState::Allowed);
	}
	if (updateBotCommandShown()) {
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
		return (type == DraftType::Edit) ? Key::LocalEdit() : Key::Local();
	case Section::Scheduled:
		return (type == DraftType::Edit)
			? Key::ScheduledEdit()
			: Key::Scheduled();
	case Section::Replies:
		return (type == DraftType::Edit)
			? Key::RepliesEdit(_currentDialogsEntryState.rootId)
			: Key::Replies(_currentDialogsEntryState.rootId);
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

	//if (!isEditingMessage() && !_inlineBot) {
	//	_saveCloudDraftTimer.callOnce(kSaveCloudDraftIdleTimeout);
	//}
}

void ComposeControls::applyDraft(FieldHistoryAction fieldHistoryAction) {
	Expects(_history != nullptr);

	InvokeQueued(_autocomplete.get(), [=] { updateStickersByEmoji(); });
	const auto guard = gsl::finally([&] {
		updateSendButtonType();
		updateControlsVisibility();
		updateControlsGeometry(_wrap->size());
	});

	const auto editDraft = _history->draft(draftKey(DraftType::Edit));
	const auto draft = editDraft
		? editDraft
		: _history->draft(draftKey(DraftType::Normal));
	if (!draft) {
		clearFieldText(0, fieldHistoryAction);
		_field->setFocus();
		_header->editMessage({});
		_header->replyToMessage({});
		return;
	}

	_textUpdateEvents = 0;
	setFieldText(draft->textWithTags, 0, fieldHistoryAction);
	_field->setFocus();
	draft->cursor.applyTo(_field);
	_textUpdateEvents = TextUpdateEvent::SaveDraft | TextUpdateEvent::SendTyping;
	if (_preview) {
		_preview->refreshState(draft->previewState);
	}

	if (draft == editDraft) {
		_header->editMessage({ _history->peer->id, draft->msgId });
		_header->replyToMessage({});
	} else {
		_header->replyToMessage({ _history->peer->id, draft->msgId });
		_header->editMessage({});
	}
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
	if (_window->hasTabbedSelectorOwnership()) {
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

	const auto selector = _window->tabbedSelector();
	const auto wrap = _wrap.get();

	base::install_event_filter(wrap, selector, [=](not_null<QEvent*> e) {
		if (_tabbedPanel && e->type() == QEvent::ParentChange) {
			setTabbedPanel(nullptr);
		}
		return base::EventFilterResult::Continue;
	});

	using EmojiChosen = ChatHelpers::TabbedSelector::EmojiChosen;
	selector->emojiChosen(
	) | rpl::start_with_next([=](EmojiChosen data) {
		Ui::InsertEmojiAtCursor(_field->textCursor(), data.emoji);
	}, wrap->lifetime());

	using FileChosen = ChatHelpers::TabbedSelector::FileChosen;
	selector->customEmojiChosen(
	) | rpl::start_with_next([=](FileChosen data) {
		Data::InsertCustomEmoji(_field, data.document);
	}, wrap->lifetime());

	selector->premiumEmojiChosen(
	) | rpl::start_with_next([=](FileChosen data) {
		if (_unavailableEmojiPasted) {
			_unavailableEmojiPasted(data.document);
		}
	}, wrap->lifetime());

	selector->fileChosen(
	) | rpl::start_to_stream(_fileChosen, wrap->lifetime());

	selector->photoChosen(
	) | rpl::start_to_stream(_photoChosen, wrap->lifetime());

	selector->inlineResultChosen(
	) | rpl::start_to_stream(_inlineResultChosen, wrap->lifetime());

	selector->contextMenuRequested(
	) | rpl::start_with_next([=] {
		selector->showMenuWithType(sendMenuType());
	}, wrap->lifetime());

	selector->choosingStickerUpdated(
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
		SendMenu::DefaultScheduleCallback(_wrap.get(), sendMenuType(), send));
}

void ComposeControls::initSendAsButton() {
	session().sendAsPeers().updated(
	) | rpl::filter([=](not_null<PeerData*> peer) {
		return _history && (peer == _history->peer);
	}) | rpl::start_with_next([=] {
		if (updateSendAsButton()) {
			updateControlsVisibility();
			updateControlsGeometry(_wrap->size());
		}
	}, _wrap->lifetime());
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

void ComposeControls::initVoiceRecordBar() {
	_voiceRecordBar->recordingStateChanges(
	) | rpl::start_with_next([=](bool active) {
		_field->setVisible(!active);
	}, _wrap->lifetime());

	_voiceRecordBar->setStartRecordingFilter([=] {
		const auto error = [&]() -> std::optional<QString> {
			const auto peer = _history ? _history->peer.get() : nullptr;
			if (!peer) {
				if (const auto error = Data::RestrictionError(
						peer,
						ChatRestriction::SendMedia)) {
					return error;
				}
				if (const auto error = Data::RestrictionError(
						peer,
						UserRestriction::SendVoiceMessages)) {
					return error;
				}
			}
			return std::nullopt;
		}();
		if (error) {
			_window->show(Ui::MakeInformBox(*error));
			return true;
		} else if (_showSlowmodeError && _showSlowmodeError()) {
			return true;
		}
		return false;
	});

	{
		auto geometry = rpl::merge(
			_wrap->geometryValue(),
			_send->geometryValue()
		) | rpl::map([=](QRect geometry) {
			auto r = _send->geometry();
			r.setY(r.y() + _wrap->y());
			return r;
		});
		_voiceRecordBar->setSendButtonGeometryValue(std::move(geometry));
	}

	{
		auto bottom = _wrap->geometryValue(
		) | rpl::map([=](QRect geometry) {
			return geometry.y() - st::historyRecordLockPosition.y();
		});
		_voiceRecordBar->setLockBottom(std::move(bottom));
	}

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
	// _attachToggle (_sendAs) -- _inlineResults ------ _tabbedPanel -- _fieldBarCancel
	// (_attachDocument|_attachPhoto) _field (_ttlInfo) (_silent|_botCommandStart) _tabbedSelectorToggle _send

	const auto fieldWidth = size.width()
		- _attachToggle->width()
		- (_sendAs ? _sendAs->width() : 0)
		- st::historySendRight
		- _send->width()
		- _tabbedSelectorToggle->width()
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
	_botCommandStart->moveToRight(right, buttonsTop);
	if (_botCommandShown) {
		right += _botCommandStart->width();
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
	_botCommandStart->setVisible(_botCommandShown);
	if (_ttlInfo) {
		_ttlInfo->show();
	}
	if (_sendAs) {
		_sendAs->show();
	}
}

bool ComposeControls::updateBotCommandShown() {
	auto shown = false;
	const auto peer = _history ? _history->peer.get() : nullptr;
	if (peer
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
	if (_tabbedPanel) {
		_tabbedPanel->moveBottomRight(
			rect.y() + rect.height() - _attachToggle->height(),
			rect.x() + rect.width());
	}
}

void ComposeControls::updateMessagesTTLShown() {
	const auto peer = _history ? _history->peer.get() : nullptr;
	const auto shown = peer && (peer->messagesTTL() > 0);
	if (!shown && _ttlInfo) {
		_ttlInfo = nullptr;
		updateControlsVisibility();
		updateControlsGeometry(_wrap->size());
	} else if (shown && !_ttlInfo) {
		_ttlInfo = std::make_unique<Controls::TTLButton>(
			_wrap.get(),
			std::make_shared<Window::Show>(_window),
			peer);
		orderControls();
		updateControlsVisibility();
		updateControlsGeometry(_wrap->size());
	}
}

bool ComposeControls::updateSendAsButton() {
	Expects(_history != nullptr);

	const auto peer = _history->peer;
	if (isEditingMessage() || !session().sendAsPeers().shouldChoose(peer)) {
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
		rpl::single(peer.get()),
		_window);
	return true;
}

void ComposeControls::paintBackground(QRect clip) {
	Painter p(_wrap.get());

	p.fillRect(clip, st::historyComposeAreaBg);
}

void ComposeControls::escape() {
	if (const auto voice = _voiceRecordBar.get(); voice->isActive()) {
		voice->showDiscardBox(nullptr, anim::type::normal);
	} else {
		_cancelRequests.fire({});
	}
}

bool ComposeControls::pushTabbedSelectorToThirdSection(
		not_null<PeerData*> peer,
		const Window::SectionShow &params) {
	if (!_tabbedPanel) {
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
	_window->resizeForThirdSection();
	_window->showSection(
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
	setTabbedPanel(std::make_unique<ChatHelpers::TabbedPanel>(
		_parent,
		_window,
		_window->tabbedSelector()));
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
	if (!_history) {
		return;
	}
	if (_tabbedPanel) {
		if (_window->canShowThirdSection()
				&& !_window->adaptive().isOneColumn()) {
			Core::App().settings().setTabbedSelectorSectionEnabled(true);
			Core::App().saveSettingsDelayed();
			pushTabbedSelectorToThirdSection(
				_history->peer,
				Window::SectionShow::Way::ClearStack);
		} else {
			_tabbedPanel->toggleAnimated();
		}
	} else {
		_window->closeThirdSection();
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
		_window->show(Ui::MakeInformBox(tr::lng_edit_caption_voice()));
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
	_history->setDraft(
		draftKey(DraftType::Edit),
		std::make_unique<Data::Draft>(
			editData,
			item->id,
			cursor,
			previewState));
	applyDraft();

	if (_autocomplete) {
		InvokeQueued(_autocomplete.get(), [=] { checkAutocomplete(); });
	}
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
					MessageCursor(),
					Data::PreviewState::Allowed));
		}
	} else {
		_header->replyToMessage(id);
	}

	_saveDraftText = true;
	_saveDraftStart = crl::now();
	saveDraft();
}

void ComposeControls::cancelReplyMessage() {
	Expects(_history != nullptr);
	Expects(draftKeyCurrent() != Data::DraftKey::None());

	const auto wasReply = replyingToMessage();
	_header->replyToMessage({});
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

bool ComposeControls::handleCancelRequest() {
	if (_isInlineBot) {
		cancelInlineBot();
		return true;
	} else if (isEditingMessage()) {
		cancelEditMessage();
		return true;
	} else if (_autocomplete && !_autocomplete->isHidden()) {
		_autocomplete->hideAnimated();
		return true;
	} else if (replyingToMessage()) {
		cancelReplyMessage();
		return true;
	}
	return false;
}

void ComposeControls::initWebpageProcess() {
	Expects(_history);

	auto &lifetime = _wrap->lifetime();
	_preview = std::make_unique<WebpageProcessor>(_history, _field);

	_preview->paintRequests(
	) | rpl::start_with_next(crl::guard(_header.get(), [=] {
		_header->update();
	}), lifetime);

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
	}, lifetime);

	_header->previewRequested(
		_preview->titleChanges(),
		_preview->descriptionChanges(),
		_preview->pageDataChanges());
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

bool ComposeControls::isLockPresent() const {
	return _voiceRecordBar->isLockPresent();
}

rpl::producer<bool> ComposeControls::lockShowStarts() const {
	return _voiceRecordBar->lockShowStarts();
}

rpl::producer<not_null<QEvent*>> ComposeControls::viewportEvents() const {
	return _voiceRecordBar->lockViewportEvents();
}

bool ComposeControls::isRecording() const {
	return _voiceRecordBar->isRecording();
}

bool ComposeControls::preventsClose(Fn<void()> &&continueCallback) const {
	if (_voiceRecordBar->isActive()) {
		_voiceRecordBar->showDiscardBox(std::move(continueCallback));
		return true;
	}
	return false;
}

bool ComposeControls::hasSilentBroadcastToggle() const {
	if (!_history) {
		return false;
	}
	const auto &peer = _history->peer;
	return peer
		&& peer->isChannel()
		&& !peer->isMegagroup()
		&& peer->canWrite()
		&& !session().data().notifySettings().silentPostsUnknown(peer);
}

void ComposeControls::updateInlineBotQuery() {
	if (!_history) {
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
	if (_history && bot) {
		if (_inlineBot != bot) {
			_inlineBot = bot;
			_inlineLookingUpBot = false;
			inlineBotChanged();
		}
		if (!_inlineResults) {
			_inlineResults = std::make_unique<InlineBots::Layout::Widget>(
				_parent,
				_window);
			_inlineResults->setCurrentDialogsEntryState(
				_currentDialogsEntryState);
			_inlineResults->setResultSelectedCallback([=](
					InlineBots::ResultSelected result) {
				if (result.open) {
					const auto request = result.result->openRequest();
					if (const auto photo = request.photo()) {
						_window->openPhoto(photo, FullMsgId());
					} else if (const auto document = request.document()) {
						_window->openDocument(document, FullMsgId());
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
