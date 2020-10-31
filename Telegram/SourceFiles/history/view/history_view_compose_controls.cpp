/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_compose_controls.h"

#include "base/event_filter.h"
#include "base/qt_signal_producer.h"
#include "base/unixtime.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_section.h"
#include "chat_helpers/tabbed_selector.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_changes.h"
#include "data/data_messages.h"
#include "data/data_session.h"
#include "data/data_web_page.h"
#include "facades.h"
#include "boxes/confirm_box.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_webpage_preview.h"
#include "inline_bots/inline_results_widget.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "media/audio/media_audio_capture.h"
#include "media/audio/media_audio.h"
#include "styles/style_chat.h"
#include "ui/text/text_options.h"
#include "ui/ui_utility.h"
#include "ui/widgets/input_fields.h"
#include "ui/text/format_values.h"
#include "ui/controls/emoji_button.h"
#include "ui/controls/send_button.h"
#include "window/window_session_controller.h"
#include "mainwindow.h"

namespace HistoryView {
namespace {

constexpr auto kRecordingUpdateDelta = crl::time(100);
constexpr auto kMouseEvents = {
	QEvent::MouseMove,
	QEvent::MouseButtonPress,
	QEvent::MouseButtonRelease
};

using FileChosen = ComposeControls::FileChosen;
using PhotoChosen = ComposeControls::PhotoChosen;
using MessageToEdit = ComposeControls::MessageToEdit;
using VoiceToSend = ComposeControls::VoiceToSend;
using SendActionUpdate = ComposeControls::SendActionUpdate;

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

class FieldHeader final : public Ui::RpWidget {
public:
	FieldHeader(QWidget *parent, not_null<Data::Session*> data);

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

	[[nodiscard]] rpl::producer<bool> visibleChanged();

private:
	void updateControlsGeometry(QSize size);
	void updateVisible();
	void setShownMessage(HistoryItem *message);
	void updateShownMessageText();

	void paintWebPage(Painter &p);
	void paintEditOrReplyToMessage(Painter &p);

	struct Preview {
		WebPageData *data = nullptr;
		Ui::Text::String title;
		Ui::Text::String description;
	};

	rpl::variable<QString> _title;
	rpl::variable<QString> _description;

	Preview _preview;

	bool hasPreview() const;

	rpl::variable<FullMsgId> _editMsgId;
	rpl::variable<FullMsgId> _replyToId;

	HistoryItem *_shownMessage = nullptr;
	Ui::Text::String _shownMessageName;
	Ui::Text::String _shownMessageText;
	int _shownMessageNameVersion = -1;

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
			: paintWebPage(p);
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
				editMessage({});
			}
			if (_replyToId.current() == update.item->fullId()) {
				replyToMessage({});
			}
		} else {
			updateShownMessageText();
		}
	}, lifetime());

	_cancel->addClickHandler([=] {
		if (hasPreview()) {
			_preview = {};
			update();
		} else if (_editMsgId.current()) {
			editMessage({});
		} else if (_replyToId.current()) {
			replyToMessage({});
		}
		updateVisible();
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
			TextUtilities::Clean(d),
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

	_shownMessageText.setText(
		st::messageTextStyle,
		_shownMessage->inReplyText(),
		Ui::DialogTextOptions());
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

void FieldHeader::previewRequested(
	rpl::producer<QString> title,
	rpl::producer<QString> description,
	rpl::producer<WebPageData*> page) {

	std::move(
		title
	) | rpl::start_with_next([=](const QString &t) {
		_title = t;
	}, lifetime());

	std::move(
		description
	) | rpl::start_with_next([=](const QString &d) {
		_description = d;
	}, lifetime());

	std::move(
		page
	) | rpl::start_with_next([=](WebPageData *p) {
		_preview.data = p;
		updateVisible();
	}, lifetime());

}

void FieldHeader::paintWebPage(Painter &p) {
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
	if (HistoryView::DrawWebPageDataPreview(p, _preview.data, to)) {
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
	Expects(_shownMessage != nullptr);

	const auto replySkip = st::historyReplySkip;
	const auto availableWidth = width()
		- replySkip
		- _cancel->width()
		- st::msgReplyPadding.right();

	if (!isEditingMessage()) {
		const auto user = _shownMessage->displayFrom()
			? _shownMessage->displayFrom()
			: _shownMessage->author().get();
		if (user->nameVersion > _shownMessageNameVersion) {
			_shownMessageName.setText(
				st::msgNameStyle,
				user->name,
				Ui::NameTextOptions());
			_shownMessageNameVersion = user->nameVersion;
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
		item->fullId(),
		{
			item->isScheduled() ? item->date() : 0,
			false,
			false,
			!hasPreview(),
		},
	};
}

ComposeControls::ComposeControls(
	not_null<QWidget*> parent,
	not_null<Window::SessionController*> window,
	Mode mode)
: _parent(parent)
, _window(window)
, _mode(mode)
, _wrap(std::make_unique<Ui::RpWidget>(parent))
, _writeRestricted(std::make_unique<Ui::RpWidget>(parent))
, _send(Ui::CreateChild<Ui::SendButton>(_wrap.get()))
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
, _header(std::make_unique<FieldHeader>(
		_wrap.get(),
		&_window->session().data()))
, _textUpdateEvents(TextUpdateEvent::SendTyping)
, _recordCancelWidth(st::historyRecordFont->width(tr::lng_record_cancel(tr::now)))
, _recordingAnimation([=](crl::time now) {
	return recordingAnimationCallback(now);
}) {
	init();
}

ComposeControls::~ComposeControls() {
	if (_recording) {
		stopRecording(false);
	}
	setTabbedPanel(nullptr);
}

Main::Session &ComposeControls::session() const {
	return _window->session();
}

void ComposeControls::setHistory(SetHistoryArgs &&args) {
	_showSlowmodeError = std::move(args.showSlowmodeError);
	_slowmodeSecondsLeft = rpl::single(0)
		| rpl::then(std::move(args.slowmodeSecondsLeft));
	_sendDisabledBySlowmode = rpl::single(false)
		| rpl::then(std::move(args.sendDisabledBySlowmode));
	_writeRestriction = rpl::single(std::optional<QString>())
		| rpl::then(std::move(args.writeRestriction));
	const auto history = *args.history;
	if (_history == history) {
		return;
	}
	_history = history;
	_window->tabbedSelector()->setCurrentPeer(
		history ? history->peer.get() : nullptr);
	initWebpageProcess();
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
	if (_recording) {
		return false;
	}
	_field->setFocus();
	return true;
}

void ComposeControls::updateControlsVisibility() {
	if (_recording) {
		_field->hide();
		_tabbedSelectorToggle->hide();
		//_botKeyboardShow->hide();
		//_botKeyboardHide->hide();
		//_botCommandStart->hide();
		_attachToggle->hide();
		//if (_silent) {
		//	_silent->hide();
		//}
		//if (_scheduled) {
		//	_scheduled->hide();
		//}
		//if (_kbShown) {
		//	_kbScroll->show();
		//} else {
		//	_kbScroll->hide();
		//}
	} else {
		_field->show();
		//if (_kbShown) {
		//	_kbScroll->show();
		//	_tabbedSelectorToggle->hide();
		//	_botKeyboardHide->show();
		//	_botKeyboardShow->hide();
		//	_botCommandStart->hide();
		//} else if (_kbReplyTo) {
		//	_kbScroll->hide();
		//	_tabbedSelectorToggle->show();
		//	_botKeyboardHide->hide();
		//	_botKeyboardShow->hide();
		//	_botCommandStart->hide();
		//} else {
		//	_kbScroll->hide();
		//	_tabbedSelectorToggle->show();
		//	_botKeyboardHide->hide();
		//	if (_keyboard->hasMarkup()) {
		//		_botKeyboardShow->show();
		//		_botCommandStart->hide();
		//	} else {
		//		_botKeyboardShow->hide();
		//		if (_cmdStartShown) {
		//			_botCommandStart->show();
		//		} else {
		//			_botCommandStart->hide();
		//		}
		//	}
		//}
		_tabbedSelectorToggle->show();
		_attachToggle->show();
		//if (_silent) {
		//	_silent->show();
		//}
		//if (_scheduled) {
		//	_scheduled->show();
		//}
		//updateFieldPlaceholder();
	}

}

bool ComposeControls::recordingAnimationCallback(crl::time now) {
	const auto dt = anim::Disabled()
		? 1.
		: ((now - _recordingAnimation.started())
			/ float64(kRecordingUpdateDelta));
	if (dt >= 1.) {
		_recordingLevel.finish();
	} else {
		_recordingLevel.update(dt, anim::linear);
	}
	if (!anim::Disabled()) {
		_wrap->update(_attachToggle->geometry());
	}
	return (dt < 1.);
}

rpl::producer<> ComposeControls::cancelRequests() const {
	return _cancelRequests.events();
}

rpl::producer<not_null<QKeyEvent*>> ComposeControls::keyEvents() const {
	return _wrap->events(
	) | rpl::map([=](not_null<QEvent*> e) -> not_null<QKeyEvent*> {
		return static_cast<QKeyEvent*>(e.get());
	}) | rpl::filter([=](not_null<QEvent*> event) {
		return (event->type() == QEvent::KeyPress);
	});
}

rpl::producer<> ComposeControls::sendRequests() const {
	auto filter = rpl::filter([=] {
		const auto type = (_mode == Mode::Normal)
			? Ui::SendButton::Type::Send
			: Ui::SendButton::Type::Schedule;
		return (_send->type() == type);
	});
	auto submits = base::qt_signal_producer(
		_field.get(),
		&Ui::InputField::submitted);
	return rpl::merge(
		_send->clicks() | filter | rpl::to_empty,
		std::move(submits) | filter | rpl::to_empty);
}

rpl::producer<VoiceToSend> ComposeControls::sendVoiceRequests() const {
	return _sendVoiceRequests.events();
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
	return _attachToggle->clicks() | rpl::to_empty;
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
	updateWrappingVisibility();
}

void ComposeControls::showForGrab() {
	showFinished();
}

TextWithTags ComposeControls::getTextWithAppliedMarkdown() const {
	return _field->getTextWithAppliedMarkdown();
}

void ComposeControls::clear() {
	setText(TextWithTags());
	cancelReplyMessage();
}

void ComposeControls::setText(const TextWithTags &textWithTags) {
	_textUpdateEvents = TextUpdateEvents();
	_field->setTextWithTags(textWithTags, Ui::InputField::HistoryAction::Clear/*fieldHistoryAction*/);
	auto cursor = _field->textCursor();
	cursor.movePosition(QTextCursor::End);
	_field->setTextCursor(cursor);
	_textUpdateEvents = /*TextUpdateEvent::SaveDraft
		| */TextUpdateEvent::SendTyping;

	//previewCancel();
	//_previewCancelled = false;
}

void ComposeControls::hidePanelsAnimated() {
	//_fieldAutocomplete->hideAnimated();
	if (_tabbedPanel) {
		_tabbedPanel->hideAnimated();
	}
	if (_inlineResults) {
		_inlineResults->hideAnimated();
	}
}

void ComposeControls::init() {
	initField();
	initTabbedSelector();
	initSendButton();
	initWriteRestriction();

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
		if (_header->isEditingMessage()) {
			setTextFromEditingMessage(session().data().message(id));
		} else {
			setText(_localSavedText);
			_localSavedText = {};
		}
		updateSendButtonType();
	}, _wrap->lifetime());

	_header->visibleChanged(
	) | rpl::start_with_next([=] {
		updateHeight();
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
}

void ComposeControls::recordDone(
		QByteArray result,
		VoiceWaveform waveform,
		int samples) {
	if (result.isEmpty()) {
		return;
	}

	Window::ActivateWindow(_window);
	const auto duration = samples / ::Media::Player::kDefaultFrequency;
	_sendVoiceRequests.fire({ result, waveform, duration });
}

void ComposeControls::recordUpdated(quint16 level, int samples) {
	if (!_recording) {
		return;
	}

	_recordingLevel.start(level);
	_recordingAnimation.start();
	_recordingSamples = samples;
	if (samples < 0 || samples >= ::Media::Player::kDefaultFrequency * AudioVoiceMsgMaxLength) {
		stopRecording(samples > 0 && _inField);
	}
	Core::App().updateNonIdle();
	_wrap->update();
	_sendActionUpdates.fire({ Api::SendProgressType::RecordVoice });
}

void ComposeControls::recordStartCallback() {
	using namespace ::Media::Capture;
	const auto error = _history
		? Data::RestrictionError(_history->peer, ChatRestriction::f_send_media)
		: std::nullopt;
	if (error) {
		Ui::show(Box<InformBox>(*error));
		return;
	} else if (_showSlowmodeError && _showSlowmodeError()) {
		return;
	} else if (!instance()->available()) {
		return;
	}

	instance()->start();
	instance()->updated(
	) | rpl::start_with_next_error([=](const Update &update) {
		recordUpdated(update.level, update.samples);
	}, [=] {
		stopRecording(false);
	}, _recordingLifetime);

	_recording = _inField = true;
	updateControlsVisibility();
	_window->widget()->setInnerFocus();

	_wrap->update();

	_send->setRecordActive(true);
}

void ComposeControls::recordStopCallback(bool active) {
	stopRecording(active);
}

void ComposeControls::recordUpdateCallback(QPoint globalPos) {
	updateOverStates(_wrap->mapFromGlobal(globalPos));
}

void ComposeControls::stopRecording(bool send) {
	if (send) {
		::Media::Capture::instance()->stop(crl::guard(_wrap.get(), [=](
				const ::Media::Capture::Result &result) {
			recordDone(result.bytes, result.waveform, result.samples);
		}));
	} else {
		::Media::Capture::instance()->stop();
	}

	_recordingLevel = anim::value();
	_recordingAnimation.stop();

	_recordingLifetime.destroy();
	_recording = false;
	_recordingSamples = 0;
	_sendActionUpdates.fire({ Api::SendProgressType::RecordVoice, -1 });

	updateControlsVisibility();
	_window->widget()->setInnerFocus();

	_wrap->update();
	_send->setRecordActive(false);
}

bool ComposeControls::showRecordButton() const {
	return ::Media::Capture::instance()->available()
		&& !HasSendText(_field)
		//&& !readyToForward()
		&& !isEditingMessage();
}

void ComposeControls::drawRecording(Painter &p, float64 recordActive) {
	p.setPen(Qt::NoPen);
	p.setBrush(st::historyRecordSignalColor);

	auto delta = qMin(_recordingLevel.current() / 0x4000, 1.);
	auto d = 2 * qRound(st::historyRecordSignalMin + (delta * (st::historyRecordSignalMax - st::historyRecordSignalMin)));
	{
		PainterHighQualityEnabler hq(p);
		p.drawEllipse(_attachToggle->x() + (_tabbedSelectorToggle->width() - d) / 2, _attachToggle->y() + (_attachToggle->height() - d) / 2, d, d);
	}

	auto duration = Ui::FormatDurationText(_recordingSamples / ::Media::Player::kDefaultFrequency);
	p.setFont(st::historyRecordFont);

	p.setPen(st::historyRecordDurationFg);
	p.drawText(_attachToggle->x() + _tabbedSelectorToggle->width(), _attachToggle->y() + st::historyRecordTextTop + st::historyRecordFont->ascent, duration);

	int32 left = _attachToggle->x() + _tabbedSelectorToggle->width() + st::historyRecordFont->width(duration) + ((_send->width() - st::historyRecordVoice.width()) / 2);
	int32 right = _wrap->width() - _send->width();

	p.setPen(anim::pen(st::historyRecordCancel, st::historyRecordCancelActive, 1. - recordActive));
	p.drawText(left + (right - left - _recordCancelWidth) / 2, _attachToggle->y() + st::historyRecordTextTop + st::historyRecordFont->ascent, tr::lng_record_cancel(tr::now));
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

void ComposeControls::setTextFromEditingMessage(not_null<HistoryItem*> item) {
	if (!_header->isEditingMessage()) {
		return;
	}
	_localSavedText = getTextWithAppliedMarkdown();
	const auto t = item->originalText();
	const auto text = TextWithTags{
		t.text,
		TextUtilities::ConvertEntitiesToTextTags(t.entities)
	};
	setText(text);
}

void ComposeControls::initField() {
	_field->setMaxHeight(st::historyComposeFieldMaxHeight);
	_field->setSubmitSettings(Core::App().settings().sendSubmitWay());
	//Ui::Connect(_field, &Ui::InputField::submitted, [=] { send(); });
	Ui::Connect(_field, &Ui::InputField::cancelled, [=] { escape(); });
	//Ui::Connect(_field, &Ui::InputField::tabbed, [=] { fieldTabbed(); });
	Ui::Connect(_field, &Ui::InputField::resized, [=] { updateHeight(); });
	//Ui::Connect(_field, &Ui::InputField::focused, [=] { fieldFocused(); });
	Ui::Connect(_field, &Ui::InputField::changed, [=] { fieldChanged(); });
	InitMessageField(_window, _field);
	const auto suggestions = Ui::Emoji::SuggestionsController::Init(
		_parent,
		_field,
		&_window->session());
	_raiseEmojiSuggestions = [=] { suggestions->raise(); };
	InitSpellchecker(_window, _field);
}

void ComposeControls::fieldChanged() {
	if (/*!_inlineBot
		&& */!_header->isEditingMessage()
		&& (_textUpdateEvents & TextUpdateEvent::SendTyping)) {
		_sendActionUpdates.fire({ Api::SendProgressType::Typing });
	}
	updateSendButtonType();
	if (showRecordButton()) {
		//_previewCancelled = false;
	}
}

rpl::producer<SendActionUpdate> ComposeControls::sendActionUpdates() const {
	return _sendActionUpdates.events();
}

void ComposeControls::initTabbedSelector() {
	if (_window->hasTabbedSelectorOwnership()) {
		createTabbedPanel();
	} else {
		setTabbedPanel(nullptr);
	}

	_tabbedSelectorToggle->addClickHandler([=] {
		toggleTabbedSelectorMode();
	});

	const auto selector = _window->tabbedSelector();
	const auto wrap = _wrap.get();

	base::install_event_filter(wrap, selector, [=](not_null<QEvent*> e) {
		if (_tabbedPanel && e->type() == QEvent::ParentChange) {
			setTabbedPanel(nullptr);
		}
		return base::EventFilterResult::Continue;
	});

	selector->emojiChosen(
	) | rpl::start_with_next([=](EmojiPtr emoji) {
		Ui::InsertEmojiAtCursor(_field->textCursor(), emoji);
	}, wrap->lifetime());

	selector->fileChosen(
	) | rpl::start_to_stream(_fileChosen, wrap->lifetime());

	selector->photoChosen(
	) | rpl::start_to_stream(_photoChosen, wrap->lifetime());

	selector->inlineResultChosen(
	) | rpl::start_to_stream(_inlineResultChosen, wrap->lifetime());
}

void ComposeControls::initSendButton() {
	rpl::combine(
		_slowmodeSecondsLeft.value(),
		_sendDisabledBySlowmode.value()
	) | rpl::start_with_next([=] {
		updateSendButtonType();
	}, _send->lifetime());

	_send->finishAnimating();
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

void ComposeControls::updateWrappingVisibility() {
	const auto restricted = _writeRestriction.current().has_value();
	_writeRestricted->setVisible(restricted);
	_wrap->setVisible(!restricted);
	if (!restricted) {
		_wrap->raise();
	}
}

void ComposeControls::updateSendButtonType() {
	using Type = Ui::SendButton::Type;
	const auto type = [&] {
		if (_header->isEditingMessage()) {
			return Type::Save;
		//} else if (_isInlineBot) {
		//	return Type::Cancel;
		} else if (showRecordButton()) {
			return Type::Record;
		}
		return (_mode == Mode::Normal) ? Type::Send : Type::Schedule;
	}();
	_send->setType(type);

	const auto delay = [&] {
		return (type != Type::Cancel && type != Type::Save)
			? _slowmodeSecondsLeft.current()
			: 0;
	}();
	_send->setSlowmodeDelay(delay);
	_send->setDisabled(_sendDisabledBySlowmode.current()
		&& (type == Type::Send || type == Type::Record));

	_send->setRecordStartCallback([=] { recordStartCallback(); });
	_send->setRecordStopCallback([=](bool active) { recordStopCallback(active); });
	_send->setRecordUpdateCallback([=](QPoint globalPos) { recordUpdateCallback(globalPos); });
	_send->setRecordAnimationCallback([=] { _wrap->update(); });
}

void ComposeControls::finishAnimating() {
	_send->finishAnimating();
	_recordingAnimation.stop();
}

void ComposeControls::updateControlsGeometry(QSize size) {
	// _attachToggle -- _inlineResults ------ _tabbedPanel -- _fieldBarCancel
	// (_attachDocument|_attachPhoto) _field _tabbedSelectorToggle _send

	const auto fieldWidth = size.width()
		- _attachToggle->width()
		- st::historySendRight
		- _send->width()
		- _tabbedSelectorToggle->width();
	_field->resizeToWidth(fieldWidth);

	const auto buttonsTop = size.height() - _attachToggle->height();

	auto left = 0;
	_attachToggle->moveToLeft(left, buttonsTop);
	left += _attachToggle->width();
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

void ComposeControls::updateOverStates(QPoint pos) {
	const auto inField = _wrap->rect().contains(pos);
	if (inField != _inField && _recording) {
		_inField = inField;
		_send->setRecordActive(_inField);
	}
}

void ComposeControls::paintBackground(QRect clip) {
	Painter p(_wrap.get());

	p.fillRect(clip, st::historyComposeAreaBg);
	if (!_field->isHidden() || _recording) {
		//drawField(p, clip);
		if (!_send->isHidden() && _recording) {
			drawRecording(p, _send->recordActiveRatio());
		}
	}
}

void ComposeControls::escape() {
	_cancelRequests.fire({});
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
		ChatHelpers::TabbedMemento(),
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
		if (_window->canShowThirdSection() && !Adaptive::OneColumn()) {
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
	cancelEditMessage();
	_header->editMessage(id);
}

void ComposeControls::cancelEditMessage() {
	_header->editMessage({});
}

void ComposeControls::replyToMessage(FullMsgId id) {
	cancelReplyMessage();
	_header->replyToMessage(id);
}

void ComposeControls::cancelReplyMessage() {
	_header->replyToMessage({});
}

void ComposeControls::initWebpageProcess() {
	Expects(_history);
	const auto peer = _history->peer;
	auto &lifetime = _wrap->lifetime();
	const auto requestRepaint = crl::guard(_header.get(), [=] {
		_header->update();
	});

	const auto parsedLinks = lifetime.make_state<QStringList>();
	const auto previewLinks = lifetime.make_state<QString>();
	const auto previewData = lifetime.make_state<WebPageData*>(nullptr);
	using PreviewCache = std::map<QString, WebPageId>;
	const auto previewCache = lifetime.make_state<PreviewCache>();
	const auto previewRequest = lifetime.make_state<mtpRequestId>(0);
	const auto previewCancelled = lifetime.make_state<bool>(false);
	const auto mtpSender =
		lifetime.make_state<MTP::Sender>(&_window->session().mtp());

	const auto title = std::make_shared<rpl::event_stream<QString>>();
	const auto description = std::make_shared<rpl::event_stream<QString>>();
	const auto pageData = std::make_shared<rpl::event_stream<WebPageData*>>();

	const auto previewTimer = lifetime.make_state<base::Timer>();

	const auto updatePreview = [=] {
		previewTimer->cancel();
		auto t = QString();
		auto d = QString();
		if (ShowWebPagePreview(*previewData)) {
			if (const auto till = (*previewData)->pendingTill) {
				t = tr::lng_preview_loading(tr::now);
				d = (*previewLinks).splitRef(' ').at(0).toString();

				const auto timeout = till - base::unixtime::now();
				previewTimer->callOnce(
					std::max(timeout, 0) * crl::time(1000));
			} else {
				const auto preview = ProcessWebPageData(*previewData);
				t = preview.title;
				d = preview.description;
			}
		}
		title->fire_copy(t);
		description->fire_copy(d);
		pageData->fire_copy(*previewData);
		requestRepaint();
	};

	const auto gotPreview = crl::guard(_wrap.get(), [=](
			const auto &result,
			QString links) {
		if (*previewRequest) {
			*previewRequest = 0;
		}
		result.match([=](const MTPDmessageMediaWebPage &d) {
			const auto page = _history->owner().processWebpage(d.vwebpage());
			previewCache->insert({ links, page->id });
			auto &till = page->pendingTill;
			if (till > 0 && till <= base::unixtime::now()) {
				till = -1;
			}
			if (links == *previewLinks && !*previewCancelled) {
				*previewData = (page->id && page->pendingTill >= 0)
					? page.get()
					: nullptr;
				updatePreview();
			}
		}, [=](const MTPDmessageMediaEmpty &d) {
			previewCache->insert({ links, 0 });
			if (links == *previewLinks && !*previewCancelled) {
				*previewData = nullptr;
				updatePreview();
			}
		}, [](const auto &d) {
		});
	});

	const auto previewCancel = [=] {
		mtpSender->request(base::take(*previewRequest)).cancel();
		*previewData = nullptr;
		previewLinks->clear();
		updatePreview();
	};

	const auto getWebPagePreview = [=] {
		const auto links = *previewLinks;
		*previewRequest = mtpSender->request(MTPmessages_GetWebPagePreview(
			MTP_flags(0),
			MTP_string(links),
			MTPVector<MTPMessageEntity>()
		)).done([=](const MTPMessageMedia &result) {
			gotPreview(result, links);
		}).send();
	};

	const auto checkPreview = crl::guard(_wrap.get(), [=] {
		const auto previewRestricted = peer
			&& peer->amRestricted(ChatRestriction::f_embed_links);
		if (/*_previewCancelled ||*/ previewRestricted) {
			previewCancel();
			return;
		}
		const auto newLinks = parsedLinks->join(' ');
		if (*previewLinks == newLinks) {
			return;
		}
		mtpSender->request(base::take(*previewRequest)).cancel();
		*previewLinks = newLinks;
		if (previewLinks->isEmpty()) {
			if (ShowWebPagePreview(*previewData)) {
				previewCancel();
			}
		} else {
			const auto i = previewCache->find(*previewLinks);
			if (i == previewCache->end()) {
				getWebPagePreview();
			} else if (i->second) {
				*previewData = _history->owner().webpage(i->second);
				updatePreview();
			} else if (ShowWebPagePreview(*previewData)) {
				previewCancel();
			}
		}
	});

	previewTimer->setCallback([=] {
		if (!ShowWebPagePreview(*previewData) || previewLinks->isEmpty()) {
			return;
		}
		getWebPagePreview();
	});

	_window->session().changes().peerUpdates(
		Data::PeerUpdate::Flag::Rights
	) | rpl::filter([=](const Data::PeerUpdate &update) {
		return (update.peer.get() == peer);
	}) | rpl::start_with_next(checkPreview, lifetime);

	_window->session().downloaderTaskFinished(
	) | rpl::filter([=] {
		return (*previewData)
			&& ((*previewData)->document || (*previewData)->photo);
	}) | rpl::start_with_next((
		requestRepaint
	), lifetime);

	_window->session().data().webPageUpdates(
	) | rpl::filter([=](not_null<WebPageData*> page) {
		return (*previewData == page.get());
	}) | rpl::start_with_next([=] {
		updatePreview();
	}, lifetime);

	const auto fieldLinksParser =
		lifetime.make_state<MessageLinksParser>(_field);

	fieldLinksParser->list().changes(
	) | rpl::start_with_next([=](QStringList &&parsed) {
		*parsedLinks = std::move(parsed);

		checkPreview();
	}, lifetime);

	_header->previewRequested(
		title->events(),
		description->events(),
		pageData->events());
}

WebPageId ComposeControls::webPageId() const {
	return _header->webPageId();
}

rpl::producer<Data::MessagePosition> ComposeControls::scrollRequests() const {
	return _header->scrollToItemRequests(
		) | rpl::map([=](FullMsgId id) -> Data::MessagePosition {
			if (const auto item = _window->session().data().message(id)) {
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

} // namespace HistoryView
