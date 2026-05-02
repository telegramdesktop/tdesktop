/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/mac/global_menu_mac.h"

#include "core/application.h"
#include "core/sandbox.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "window/main_window.h"
#include "main/main_session.h"
#include "history/history_inner_widget.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/about_box.h"
#include "lang/lang_keys.h"
#include "base/platform/base_platform_info.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/fields/input_field.h"

#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMenu>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QTextEdit>
#include <QtGui/QAction>
#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

#include <Cocoa/Cocoa.h>

namespace Platform {
namespace {

class Manager final {
public:
	void create();
	void destroy();
	void requestUpdate();
	[[nodiscard]] auto markdownStateChanges() const
		-> rpl::producer<Ui::MarkdownEnabledState>;

private:
	void buildMenu();
	void buildAppleMenu(QMenu *main);
	void buildFileMenu(QMenu *file);
	void buildEditMenu(QMenu *edit);
	void buildWindowMenu(QMenu *window);
	void retranslate();
	void ensureLanguageBound();
	void recomputeState();
	[[nodiscard]] bool clipboardHasText();
	[[nodiscard]] Window::Controller *resolveActiveWindow() const;

	template <typename Callback>
	void withActiveWindow(Callback callback) {
		const auto window = resolveActiveWindow();
		if (!window) {
			return;
		}
		if (window->widget()->isHidden()) {
			window->widget()->showFromTray();
		} else {
			window->activate();
		}
		callback(window);
	}

	std::unique_ptr<QMenuBar> _menuBar;
	QAction *_logout = nullptr;
	QAction *_undo = nullptr;
	QAction *_redo = nullptr;
	QAction *_cut = nullptr;
	QAction *_copy = nullptr;
	QAction *_paste = nullptr;
	QAction *_delete = nullptr;
	QAction *_selectAll = nullptr;
	QAction *_contacts = nullptr;
	QAction *_addContact = nullptr;
	QAction *_newGroup = nullptr;
	QAction *_newChannel = nullptr;
	QAction *_showTelegram = nullptr;
	QAction *_fullScreen = nullptr;
	QAction *_emoji = nullptr;
	QAction *_bold = nullptr;
	QAction *_italic = nullptr;
	QAction *_underline = nullptr;
	QAction *_strikeOut = nullptr;
	QAction *_blockquote = nullptr;
	QAction *_monospace = nullptr;
	QAction *_clearFormat = nullptr;

	NSPasteboard *_pasteboard = nullptr;
	int _pasteboardChangeCount = -1;
	bool _pasteboardHasText = false;

	rpl::event_stream<> _updateRequests;
	rpl::event_stream<Ui::MarkdownEnabledState> _markdownChanges;
	rpl::lifetime _lifetime;
	bool _languageBound = false;

};

void SendKeySequence(
		Qt::Key key,
		Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
	const auto focused = QApplication::focusWidget();
	if (qobject_cast<QLineEdit*>(focused)
		|| qobject_cast<QTextEdit*>(focused)
		|| dynamic_cast<HistoryInner*>(focused)) {
		QApplication::postEvent(
			focused,
			new QKeyEvent(QEvent::KeyPress, key, modifiers));
		QApplication::postEvent(
			focused,
			new QKeyEvent(QEvent::KeyRelease, key, modifiers));
	}
}

void ForceDisabled(QAction *action, bool disabled) {
	if (action->isEnabled()) {
		if (disabled) action->setDisabled(true);
	} else if (!disabled) {
		action->setDisabled(false);
	}
}

Window::Controller *Manager::resolveActiveWindow() const {
	if (!Core::IsAppLaunched()) {
		return nullptr;
	}
	const auto active = Core::App().activeWindow();
	return active ? active : Core::App().activePrimaryWindow();
}

bool Manager::clipboardHasText() {
	const auto current = static_cast<int>([_pasteboard changeCount]);
	if (_pasteboardChangeCount != current) {
		_pasteboardChangeCount = current;
		_pasteboardHasText = !QGuiApplication::clipboard()->text().isEmpty();
	}
	return _pasteboardHasText;
}

void Manager::retranslate() {
	if (_logout) {
		_logout->setText(tr::lng_mac_menu_logout(tr::now));
	}
	if (_delete) {
		_delete->setText(tr::lng_mac_menu_delete(tr::now));
	}
	if (_contacts) {
		_contacts->setText(tr::lng_mac_menu_contacts(tr::now));
	}
	if (_addContact) {
		_addContact->setText(tr::lng_mac_menu_add_contact(tr::now));
	}
	if (_newGroup) {
		_newGroup->setText(tr::lng_mac_menu_new_group(tr::now));
	}
	if (_newChannel) {
		_newChannel->setText(tr::lng_mac_menu_new_channel(tr::now));
	}
	if (_showTelegram) {
		_showTelegram->setText(tr::lng_mac_menu_show(tr::now));
	}
	if (_fullScreen) {
		_fullScreen->setText(tr::lng_mac_menu_fullscreen(tr::now));
	}
	if (_emoji) {
		_emoji->setText(tr::lng_mac_menu_emoji_and_symbols(
			tr::now,
			Ui::Text::FixAmpersandInAction));
	}
	if (_bold) {
		_bold->setText(tr::lng_menu_formatting_bold(tr::now));
	}
	if (_italic) {
		_italic->setText(tr::lng_menu_formatting_italic(tr::now));
	}
	if (_underline) {
		_underline->setText(tr::lng_menu_formatting_underline(tr::now));
	}
	if (_strikeOut) {
		_strikeOut->setText(tr::lng_menu_formatting_strike_out(tr::now));
	}
	if (_blockquote) {
		_blockquote->setText(tr::lng_menu_formatting_blockquote(tr::now));
	}
	if (_monospace) {
		_monospace->setText(tr::lng_menu_formatting_monospace(tr::now));
	}
	if (_clearFormat) {
		_clearFormat->setText(tr::lng_menu_formatting_clear(tr::now));
	}
}

void Manager::ensureLanguageBound() {
	if (_languageBound || !Core::IsAppLaunched()) {
		return;
	}
	_languageBound = true;
	retranslate();
	Lang::Updated() | rpl::on_next([this] {
		retranslate();
	}, _lifetime);
}

void Manager::recomputeState() {
	const auto window = resolveActiveWindow();
	if (!window) {
		return;
	}
	const auto widget = window->widget();
	if (!widget->positionInited()) {
		return;
	}

	const auto focused = QApplication::focusWidget();
	auto canUndo = false;
	auto canRedo = false;
	auto canCut = false;
	auto canCopy = false;
	auto canPaste = false;
	auto canDelete = false;
	auto canSelectAll = false;
	const auto clipboardHasText = this->clipboardHasText();
	auto markdownState = Ui::MarkdownEnabledState();
	if (const auto edit = qobject_cast<QLineEdit*>(focused)) {
		canCut = canCopy = canDelete = edit->hasSelectedText();
		canSelectAll = !edit->text().isEmpty();
		canUndo = edit->isUndoAvailable();
		canRedo = edit->isRedoAvailable();
		canPaste = clipboardHasText;
	} else if (const auto edit = qobject_cast<QTextEdit*>(focused)) {
		canCut = canCopy = canDelete = edit->textCursor().hasSelection();
		canSelectAll = !edit->document()->isEmpty();
		canUndo = edit->document()->isUndoAvailable();
		canRedo = edit->document()->isRedoAvailable();
		canPaste = clipboardHasText;
		if (canCopy) {
			if (const auto inputField = dynamic_cast<Ui::InputField*>(
					focused->parentWidget())) {
				markdownState = inputField->markdownEnabledState();
			}
		}
	} else if (const auto list = dynamic_cast<HistoryInner*>(focused)) {
		canCopy = list->canCopySelected();
		canDelete = list->canDeleteSelected();
	}

	_markdownChanges.fire_copy(markdownState);

	widget->updateIsActive();
	const auto controller = window->sessionController();
	const auto logged = (controller != nullptr);
	const auto inactive = !logged || window->locked();
	const auto support = logged && controller->session().supportMode();
	ForceDisabled(_logout, !logged && !Core::App().passcodeLocked());
	ForceDisabled(_undo, !canUndo);
	ForceDisabled(_redo, !canRedo);
	ForceDisabled(_cut, !canCut);
	ForceDisabled(_copy, !canCopy);
	ForceDisabled(_paste, !canPaste);
	ForceDisabled(_delete, !canDelete);
	ForceDisabled(_selectAll, !canSelectAll);
	ForceDisabled(_contacts, inactive || support);
	ForceDisabled(_addContact, inactive);
	ForceDisabled(_newGroup, inactive || support);
	ForceDisabled(_newChannel, inactive || support);
	ForceDisabled(_showTelegram, widget->isActive());

	const auto disabled = [&](const QString &tag) {
		return !markdownState.enabledForTag(tag);
	};
	using Field = Ui::InputField;
	ForceDisabled(_bold, disabled(Field::kTagBold));
	ForceDisabled(_italic, disabled(Field::kTagItalic));
	ForceDisabled(_underline, disabled(Field::kTagUnderline));
	ForceDisabled(_strikeOut, disabled(Field::kTagStrikeOut));
	ForceDisabled(_blockquote, disabled(Field::kTagBlockquote));
	ForceDisabled(
		_monospace,
		disabled(Field::kTagPre) || disabled(Field::kTagCode));
	ForceDisabled(_clearFormat, markdownState.disabled());
}

void Manager::buildAppleMenu(QMenu *main) {
	{
		auto callback = [this] {
			withActiveWindow([](not_null<Window::Controller*> window) {
				window->show(Box(AboutBox));
			});
		};
		const auto about = main->addAction(
			u"About Telegram"_q,
			std::move(callback));
		about->setMenuRole(QAction::AboutQtRole);
	}

	main->addSeparator();
	{
		auto callback = [this] {
			withActiveWindow([](not_null<Window::Controller*> window) {
				window->showSettings();
			});
		};
		const auto preferences = main->addAction(
			u"Preferences..."_q,
			_menuBar.get(),
			std::move(callback),
			QKeySequence(Qt::ControlModifier | Qt::Key_Comma));
		preferences->setMenuRole(QAction::PreferencesRole);
		preferences->setShortcutContext(Qt::WidgetShortcut);
	}
}

void Manager::buildFileMenu(QMenu *file) {
	auto callback = [this] {
		withActiveWindow([](not_null<Window::Controller*> window) {
			window->showLogoutConfirmation();
		});
	};
	_logout = file->addAction(
		u"Log Out"_q,
		_menuBar.get(),
		std::move(callback));
}

void Manager::buildEditMenu(QMenu *edit) {
	const auto receiver = _menuBar.get();
	_undo = edit->addAction(
		u"Undo"_q,
		receiver,
		[] { SendKeySequence(Qt::Key_Z, Qt::ControlModifier); },
		QKeySequence::Undo);
	_undo->setShortcutContext(Qt::WidgetShortcut);
	_redo = edit->addAction(
		u"Redo"_q,
		receiver,
		[] {
			SendKeySequence(
				Qt::Key_Z,
				Qt::ControlModifier | Qt::ShiftModifier);
		},
		QKeySequence::Redo);
	_redo->setShortcutContext(Qt::WidgetShortcut);
	edit->addSeparator();
	_cut = edit->addAction(
		u"Cut"_q,
		receiver,
		[] { SendKeySequence(Qt::Key_X, Qt::ControlModifier); },
		QKeySequence::Cut);
	_cut->setShortcutContext(Qt::WidgetShortcut);
	_copy = edit->addAction(
		u"Copy"_q,
		receiver,
		[] { SendKeySequence(Qt::Key_C, Qt::ControlModifier); },
		QKeySequence::Copy);
	_copy->setShortcutContext(Qt::WidgetShortcut);
	_paste = edit->addAction(
		u"Paste"_q,
		receiver,
		[] { SendKeySequence(Qt::Key_V, Qt::ControlModifier); },
		QKeySequence::Paste);
	_paste->setShortcutContext(Qt::WidgetShortcut);
	_delete = edit->addAction(
		u"Delete"_q,
		receiver,
		[] { SendKeySequence(Qt::Key_Delete); },
		QKeySequence(Qt::ControlModifier | Qt::Key_Backspace));
	_delete->setShortcutContext(Qt::WidgetShortcut);

	edit->addSeparator();
	_bold = edit->addAction(
		u"Bold"_q,
		receiver,
		[] { SendKeySequence(Qt::Key_B, Qt::ControlModifier); },
		QKeySequence::Bold);
	_bold->setShortcutContext(Qt::WidgetShortcut);
	_italic = edit->addAction(
		u"Italic"_q,
		receiver,
		[] { SendKeySequence(Qt::Key_I, Qt::ControlModifier); },
		QKeySequence::Italic);
	_italic->setShortcutContext(Qt::WidgetShortcut);
	_underline = edit->addAction(
		u"Underline"_q,
		receiver,
		[] { SendKeySequence(Qt::Key_U, Qt::ControlModifier); },
		QKeySequence::Underline);
	_underline->setShortcutContext(Qt::WidgetShortcut);
	_strikeOut = edit->addAction(
		u"Strikethrough"_q,
		receiver,
		[] {
			SendKeySequence(
				Qt::Key_X,
				Qt::ControlModifier | Qt::ShiftModifier);
		},
		Ui::kStrikeOutSequence);
	_strikeOut->setShortcutContext(Qt::WidgetShortcut);
	_blockquote = edit->addAction(
		u"Quote"_q,
		receiver,
		[] {
			SendKeySequence(
				Qt::Key_Period,
				Qt::ControlModifier | Qt::ShiftModifier);
		},
		Ui::kBlockquoteSequence);
	_blockquote->setShortcutContext(Qt::WidgetShortcut);
	_monospace = edit->addAction(
		u"Monospace"_q,
		receiver,
		[] {
			SendKeySequence(
				Qt::Key_M,
				Qt::ControlModifier | Qt::ShiftModifier);
		},
		Ui::kMonospaceSequence);
	_monospace->setShortcutContext(Qt::WidgetShortcut);
	_clearFormat = edit->addAction(
		u"Clear formatting"_q,
		receiver,
		[] {
			SendKeySequence(
				Qt::Key_N,
				Qt::ControlModifier | Qt::ShiftModifier);
		},
		Ui::kClearFormatSequence);
	_clearFormat->setShortcutContext(Qt::WidgetShortcut);

	edit->addSeparator();
	_selectAll = edit->addAction(
		u"Select All"_q,
		receiver,
		[] { SendKeySequence(Qt::Key_A, Qt::ControlModifier); },
		QKeySequence::SelectAll);
	_selectAll->setShortcutContext(Qt::WidgetShortcut);

	if (!Platform::IsMac26_0OrGreater()) {
		edit->addSeparator();
		_emoji = edit->addAction(
			u"Emoji & Symbols"_q,
			receiver,
			[] { [NSApp orderFrontCharacterPalette:nil]; },
			QKeySequence(Qt::MetaModifier
				| Qt::ControlModifier
				| Qt::Key_Space));
		_emoji->setShortcutContext(Qt::WidgetShortcut);
	}
}

void Manager::buildWindowMenu(QMenu *window) {
	const auto receiver = _menuBar.get();
	_fullScreen = window->addAction(
		u"Toggle Full Screen"_q,
		receiver,
		[this] {
			withActiveWindow([](not_null<Window::Controller*> w) {
				const auto view = reinterpret_cast<NSView*>(
					w->widget()->winId());
				NSWindow *nsWindow = [view window];
				[nsWindow toggleFullScreen:nsWindow];
			});
		},
		QKeySequence(Qt::MetaModifier | Qt::ControlModifier | Qt::Key_F));
	_fullScreen->setShortcutContext(Qt::WidgetShortcut);
	window->addSeparator();

	_contacts = window->addAction(u"Contacts"_q);
	QObject::connect(_contacts, &QAction::triggered, _contacts, [this] {
		withActiveWindow([](not_null<Window::Controller*> w) {
			const auto sc = w->sessionController();
			if (!sc || w->locked()) {
				return;
			}
			sc->show(PrepareContactsBox(sc));
		});
	});
	{
		auto callback = [this] {
			withActiveWindow([](not_null<Window::Controller*> w) {
				const auto sc = w->sessionController();
				if (!sc || w->locked()) {
					return;
				}
				sc->showAddContact();
			});
		};
		_addContact = window->addAction(
			u"Add Contact"_q,
			receiver,
			std::move(callback));
	}
	window->addSeparator();
	{
		auto callback = [this] {
			withActiveWindow([](not_null<Window::Controller*> w) {
				const auto sc = w->sessionController();
				if (!sc || w->locked()) {
					return;
				}
				sc->showNewGroup();
			});
		};
		_newGroup = window->addAction(
			u"New Group"_q,
			receiver,
			std::move(callback));
	}
	{
		auto callback = [this] {
			withActiveWindow([](not_null<Window::Controller*> w) {
				const auto sc = w->sessionController();
				if (!sc || w->locked()) {
					return;
				}
				sc->showNewChannel();
			});
		};
		_newChannel = window->addAction(
			u"New Channel"_q,
			receiver,
			std::move(callback));
	}
	window->addSeparator();
	_showTelegram = window->addAction(
		u"Show Telegram"_q,
		receiver,
		[this] {
			if (const auto w = resolveActiveWindow()) {
				w->widget()->showFromTray();
			}
		});
}

void Manager::buildMenu() {
	buildAppleMenu(_menuBar->addMenu(u"Telegram"_q));
	buildFileMenu(_menuBar->addMenu(u"File"_q));
	buildEditMenu(_menuBar->addMenu(u"Edit"_q));
	buildWindowMenu(_menuBar->addMenu(u"Window"_q));
}

void Manager::create() {
	Expects(_menuBar == nullptr);

	_pasteboard = [NSPasteboard generalPasteboard];
	_menuBar = std::make_unique<QMenuBar>();

	buildMenu();

	_updateRequests.events() | rpl::on_next([this] {
		ensureLanguageBound();
		recomputeState();
	}, _lifetime);
}

void Manager::destroy() {
	_lifetime.destroy();
	_menuBar.reset();
	_languageBound = false;
	_logout = _undo = _redo = _cut = _copy = _paste = _delete
		= _selectAll = _contacts = _addContact = _newGroup
		= _newChannel = _showTelegram = _fullScreen = _emoji
		= _bold = _italic = _underline
		= _strikeOut = _blockquote = _monospace = _clearFormat
		= nullptr;
	_pasteboard = nullptr;
	_pasteboardChangeCount = -1;
	_pasteboardHasText = false;
}

void Manager::requestUpdate() {
	if (!_menuBar) {
		return;
	}
	_updateRequests.fire({});
}

auto Manager::markdownStateChanges() const
-> rpl::producer<Ui::MarkdownEnabledState> {
	return _markdownChanges.events();
}

Manager GlobalMenu;

} // namespace

void CreateGlobalMenu() {
	GlobalMenu.create();
}

void DestroyGlobalMenu() {
	GlobalMenu.destroy();
}

void RequestUpdateGlobalMenu() {
	GlobalMenu.requestUpdate();
}

rpl::producer<Ui::MarkdownEnabledState> GlobalMenuMarkdownState() {
	return GlobalMenu.markdownStateChanges();
}

} // namespace Platform
