/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_compose_controls.h"

#include "ui/widgets/input_fields.h"
#include "ui/special_buttons.h"
#include "lang/lang_keys.h"
#include "core/event_filter.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_section.h"
#include "chat_helpers/tabbed_selector.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "window/window_session_controller.h"
#include "inline_bots/inline_results_widget.h"
#include "styles/style_history.h"

namespace HistoryView {

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
		tr::lng_message_ph())) {
	init();
}

ComposeControls::~ComposeControls() {
	setTabbedPanel(nullptr);
}

Main::Session &ComposeControls::session() const {
	return _window->session();
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

void ComposeControls::init() {
	initField();
	initTabbedSelector();

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
}

void ComposeControls::initField() {
	_field->setMaxHeight(st::historyComposeFieldMaxHeight);
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

	Core::InstallEventFilter(wrap, selector, [=](not_null<QEvent*> e) {
		if (_tabbedPanel && e->type() == QEvent::ParentChange) {
			setTabbedPanel(nullptr);
		}
		return false;
	});

	selector->emojiChosen(
	) | rpl::start_with_next([=](EmojiPtr emoji) {
		Ui::InsertEmojiAtCursor(_field->textCursor(), emoji);
	}, wrap->lifetime());

	selector->fileChosen(
	) | rpl::start_with_next([=](not_null<DocumentData*> document) {
		//sendExistingDocument(document);
	}, wrap->lifetime());

	selector->photoChosen(
	) | rpl::start_with_next([=](not_null<PhotoData*> photo) {
		//sendExistingPhoto(photo);
	}, wrap->lifetime());

	selector->inlineResultChosen(
	) | rpl::start_with_next([=](
			ChatHelpers::TabbedSelector::InlineChosen data) {
		//sendInlineResult(data.result, data.bot);
	}, wrap->lifetime());
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

void ComposeControls::pushTabbedSelectorToThirdSection(
		const Window::SectionShow &params) {
	if (!_tabbedPanel) {
		return;
	//} else if (!_canSendMessages) {
	//	session().settings().setTabbedReplacedWithInfo(true);
	//	_window->showPeerInfo(_peer, params.withThirdColumn());
	//	return;
	}
	session().settings().setTabbedReplacedWithInfo(false);
	_tabbedSelectorToggle->setColorOverrides(
		&st::historyAttachEmojiActive,
		&st::historyRecordVoiceFgActive,
		&st::historyRecordVoiceRippleBgActive);
	_window->resizeForThirdSection();
	_window->showSection(
		ChatHelpers::TabbedMemento(),
		params.withThirdColumn());
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
	if (_tabbedPanel) {
		if (_window->canShowThirdSection() && !Adaptive::OneColumn()) {
			session().settings().setTabbedSelectorSectionEnabled(true);
			session().saveSettingsDelayed();
			pushTabbedSelectorToThirdSection(
				Window::SectionShow::Way::ClearStack);
		} else {
			_tabbedPanel->toggleAnimated();
		}
	} else {
		_window->closeThirdSection();
	}
}

void ComposeControls::updateHeight() {
	const auto height = _field->height() + 2 * st::historySendPadding;
	_wrap->resize(_wrap->width(), height);
}

} // namespace HistoryView
