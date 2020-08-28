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
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_webpage_preview.h"
#include "inline_bots/inline_results_widget.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "styles/style_history.h"
#include "ui/special_buttons.h"
#include "ui/text_options.h"
#include "ui/ui_utility.h"
#include "ui/widgets/input_fields.h"
#include "window/window_session_controller.h"

namespace HistoryView {

namespace {

using FileChosen = ComposeControls::FileChosen;
using PhotoChosen = ComposeControls::PhotoChosen;
using MessageToEdit = ComposeControls::MessageToEdit;

constexpr auto kMouseEvent = {
	QEvent::MouseMove,
	QEvent::MouseButtonPress,
	QEvent::MouseButtonRelease
};

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

class FieldHeader : public Ui::RpWidget {
public:
	FieldHeader(QWidget *parent, not_null<Data::Session*> data);

	void init();

	void editMessage(FullMsgId edit);
	void previewRequested(
		rpl::producer<QString> title,
		rpl::producer<QString> description,
		rpl::producer<WebPageData*> page);

	bool isDisplayed() const;
	bool isEditingMessage() const;
	rpl::producer<FullMsgId> editMsgId() const;
	rpl::producer<FullMsgId> scrollToItemRequests() const;
	MessageToEdit queryToEdit();
	WebPageId webPageId() const;

	rpl::producer<bool> visibleChanged();

protected:

private:
	void updateControlsGeometry(QSize size);
	void updateVisible();

	void paintWebPage(Painter &p);
	void paintEditMessage(Painter &p);

	struct Preview {
		WebPageData *data = nullptr;
		Ui::Text::String title;
		Ui::Text::String description;
	};

	rpl::variable<QString> _title;
	rpl::variable<QString> _description;

	Preview _preview;

	bool hasPreview() const;

	Ui::Text::String _editMsgText;
	rpl::variable<FullMsgId> _editMsgId;

	const not_null<Data::Session*> _data;
	const not_null<Ui::IconButton*> _cancel;

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

		if (isEditingMessage()) {
			const auto position = st::historyReplyIconPosition;
			st::historyEditIcon.paint(p, position, width());
		}

		(!ShowWebPagePreview(_preview.data) || *leftIconPressed)
			? paintEditMessage(p)
			: paintWebPage(p);
	}, lifetime());

	const auto checkPreview = [=](not_null<const HistoryItem*> item) {
		_preview = {};
		if (const auto media = item->media()) {
			if (const auto page = media->webpage()) {
				const auto preview = ProcessWebPageData(page);
				_title = preview.title;
				_description = preview.description;
				_preview.data = page;
			}
		}
	};

	_editMsgId.value(
	) | rpl::start_with_next([=] {
		updateVisible();
		if (const auto item = _data->message(_editMsgId.current())) {
			_editMsgText.setText(
				st::messageTextStyle,
				item->inReplyText(),
				Ui::DialogTextOptions());
			checkPreview(item);
		}
	}, lifetime());

	_cancel->addClickHandler([=] {
		if (hasPreview()) {
			_preview = {};
			update();
		} else {
			_editMsgId = {};
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
		return ranges::contains(kMouseEvent, event->type())
			&& isEditingMessage();
	}) | rpl::start_with_next([=](not_null<QEvent*> event) {
		const auto type = event->type();
		const auto e = static_cast<QMouseEvent*>(event.get());
		const auto pos = e ? e->pos() : mapFromGlobal(QCursor::pos());
		const auto inPreviewRect = QRect(
			st::historyReplySkip,
			0,
			width() - st::historyReplySkip - _cancel->width(),
			height()).contains(pos);

		if (type == QEvent::MouseMove) {
			const auto inEdit = inPreviewRect;

			if (inEdit != *inClickable) {
				*inClickable = inEdit;
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
				_scrollToItemRequests.fire(_editMsgId.current());
			}
		} else if (type == QEvent::MouseButtonRelease) {
			if (isLeftButton && *leftIconPressed) {
				*leftIconPressed = false;
				update();
			}
		}
	}, lifetime());
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

void FieldHeader::paintEditMessage(Painter &p) {
	const auto replySkip = st::historyReplySkip;
	p.setPen(st::historyReplyNameFg);
	p.setFont(st::msgServiceNameFont);
	p.drawTextLeft(
		replySkip,
		st::msgReplyPadding.top(),
		width(),
		tr::lng_edit_message(tr::now));

	p.setPen(st::historyComposeAreaFg);
	p.setTextPalette(st::historyComposeAreaPalette);
	_editMsgText.drawElided(
		p,
		replySkip,
		st::msgReplyPadding.top() + st::msgServiceNameFont->height,
		width() - replySkip - _cancel->width() - st::msgReplyPadding.right());
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
	return isEditingMessage() || hasPreview();
}

bool FieldHeader::isEditingMessage() const {
	return !!_editMsgId.current();
}

bool FieldHeader::hasPreview() const {
	return ShowWebPagePreview(_preview.data);
}

WebPageId FieldHeader::webPageId() const {
	return hasPreview() ? _preview.data->id : CancelledWebPageId;
}

void FieldHeader::updateControlsGeometry(QSize size) {
	_cancel->moveToRight(0, 0);
}

void FieldHeader::editMessage(FullMsgId id) {
	_editMsgId = id;
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
		&_window->session().data())) {
	init();
}

ComposeControls::~ComposeControls() {
	setTabbedPanel(nullptr);
}

Main::Session &ComposeControls::session() const {
	return _window->session();
}

void ComposeControls::setHistory(History *history) {
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
}

void ComposeControls::resizeToWidth(int width) {
	_wrap->resizeToWidth(width);
	updateHeight();
}

rpl::producer<int> ComposeControls::height() const {
	return _wrap->heightValue();
}

int ComposeControls::heightCurrent() const {
	return _wrap->height();
}

void ComposeControls::focus() {
	_field->setFocus();
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
}

void ComposeControls::showFinished() {
	if (_inlineResults) {
		_inlineResults->hideFast();
	}
	if (_tabbedPanel) {
		_tabbedPanel->hideFast();
	}
	_wrap->show();
}

void ComposeControls::showForGrab() {
	showFinished();
}

TextWithTags ComposeControls::getTextWithAppliedMarkdown() const {
	return _field->getTextWithAppliedMarkdown();
}

void ComposeControls::clear() {
	setText(TextWithTags());
}

void ComposeControls::setText(const TextWithTags &textWithTags) {
	//_textUpdateEvents = events;
	_field->setTextWithTags(textWithTags, Ui::InputField::HistoryAction::Clear/*fieldHistoryAction*/);
	auto cursor = _field->textCursor();
	cursor.movePosition(QTextCursor::End);
	_field->setTextCursor(cursor);
	//_textUpdateEvents = TextUpdateEvent::SaveDraft
	//	| TextUpdateEvent::SendTyping;

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
			setTextFromEditingMessage(_window->session().data().message(id));
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

		_window->session().data().itemRemoved(
		) | rpl::filter([=](not_null<const HistoryItem*> item) {
			return item->id && ((*lastMsgId) == item->fullId());
		}) | rpl::start_with_next([=] {
			cancelEditMessage();
		}, _wrap->lifetime());
	}
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
	//Ui::Connect(_field, &Ui::InputField::changed, [=] { fieldChanged(); });
	InitMessageField(_window, _field);
	const auto suggestions = Ui::Emoji::SuggestionsController::Init(
		_parent,
		_field,
		&_window->session());
	_raiseEmojiSuggestions = [=] { suggestions->raise(); };
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
	updateSendButtonType();
	_send->finishAnimating();
}

void ComposeControls::updateSendButtonType() {
	using Type = Ui::SendButton::Type;
	const auto type = [&] {
		if (_header->isEditingMessage()) {
			return Type::Save;
		//} else if (_isInlineBot) {
		//	return Type::Cancel;
		//} else if (showRecordButton()) {
		//	return Type::Record;
		}
		return (_mode == Mode::Normal) ? Type::Send : Type::Schedule;
	}();
	_send->setType(type);

	const auto delay = [&] {
		return /*(type != Type::Cancel && type != Type::Save && _peer)
			? _peer->slowmodeSecondsLeft()
			: */0;
	}();
	_send->setSlowmodeDelay(delay);
	//_send->setDisabled(_peer
	//	&& _peer->slowmodeApplied()
	//	&& (_history->latestSendingMessage() != nullptr)
	//	&& (type == Type::Send || type == Type::Record));

	//if (delay != 0) {
	//	base::call_delayed(
	//		kRefreshSlowmodeLabelTimeout,
	//		this,
	//		[=] { updateSendButtonType(); });
	//}
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

void ComposeControls::paintBackground(QRect clip) {
	Painter p(_wrap.get());

	p.fillRect(clip, st::historyComposeAreaBg);
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

void ComposeControls::editMessage(FullMsgId edit) {
	cancelEditMessage();
	_header->editMessage(std::move(edit));
}

void ComposeControls::cancelEditMessage() {
	_header->editMessage({});
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

} // namespace HistoryView
