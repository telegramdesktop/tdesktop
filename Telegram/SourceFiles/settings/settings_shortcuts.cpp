/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_shortcuts.h"

#include "base/event_filter.h"
#include "core/application.h"
#include "core/shortcuts.h"
#include "lang/lang_keys.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <qpa/qplatformintegration.h>
#include <private/qguiapplication_p.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
#include <qpa/qplatformkeymapper.h>
#endif

namespace Settings {
namespace {

namespace S = ::Shortcuts;

struct Labeled {
	S::Command command = {};
	rpl::producer<QString> label;
};

[[nodiscard]] std::vector<Labeled> Entries() {
	using C = S::Command;
	const auto pinned = [](int index) {
		return tr::lng_shortcuts_chat_pinned_n(
			lt_index,
			rpl::single(QString::number(index)));
	};
	const auto account = [](int index) {
		return tr::lng_shortcuts_show_account_n(
			lt_index,
			rpl::single(QString::number(index)));
	};
	const auto folder = [](int index) {
		return tr::lng_shortcuts_show_folder_n(
			lt_index,
			rpl::single(QString::number(index)));
	};
	const auto separator = Labeled{ C(), nullptr };
	return {
		{ C::Close, tr::lng_shortcuts_close() },
		{ C::Lock, tr::lng_shortcuts_lock() },
		{ C::Minimize, tr::lng_shortcuts_minimize() },
		{ C::Quit, tr::lng_shortcuts_quit() },
		separator,
		{ C::Search, tr::lng_shortcuts_search() },
		separator,
		{ C::ChatPrevious, tr::lng_shortcuts_chat_previous() },
		{ C::ChatNext, tr::lng_shortcuts_chat_next() },
		{ C::ChatFirst, tr::lng_shortcuts_chat_first() },
		{ C::ChatLast, tr::lng_shortcuts_chat_last() },
		{ C::ChatSelf, tr::lng_shortcuts_chat_self() },
		separator,
		{ C::ChatPinned1, pinned(1) },
		{ C::ChatPinned2, pinned(2) },
		{ C::ChatPinned3, pinned(3) },
		{ C::ChatPinned4, pinned(4) },
		{ C::ChatPinned5, pinned(5) },
		{ C::ChatPinned6, pinned(6) },
		{ C::ChatPinned7, pinned(7) },
		{ C::ChatPinned8, pinned(8) },
		separator,
		{ C::ShowAccount1, account(1) },
		{ C::ShowAccount2, account(2) },
		{ C::ShowAccount3, account(3) },
		{ C::ShowAccount4, account(4) },
		{ C::ShowAccount5, account(5) },
		{ C::ShowAccount6, account(6) },
		separator,
		{ C::ShowAllChats, tr::lng_shortcuts_show_all_chats() },
		{ C::ShowFolder1, folder(1) },
		{ C::ShowFolder2, folder(2) },
		{ C::ShowFolder3, folder(3) },
		{ C::ShowFolder4, folder(4) },
		{ C::ShowFolder5, folder(5) },
		{ C::ShowFolder6, folder(6) },
		{ C::ShowFolderLast, tr::lng_shortcuts_show_folder_last() },
		{ C::FolderNext, tr::lng_shortcuts_folder_next() },
		{ C::FolderPrevious, tr::lng_shortcuts_folder_previous() },
		{ C::ShowArchive, tr::lng_shortcuts_archive() },
		{ C::ShowContacts, tr::lng_shortcuts_contacts() },
		separator,
		{ C::ReadChat, tr::lng_shortcuts_read_chat() },
		{ C::ArchiveChat, tr::lng_shortcuts_archive_chat() },
		{ C::ShowScheduled, tr::lng_shortcuts_scheduled() },
		{ C::ShowChatMenu, tr::lng_shortcuts_show_chat_menu() },
		separator,
		{ C::JustSendMessage, tr::lng_shortcuts_just_send() },
		{ C::SendSilentMessage, tr::lng_shortcuts_silent_send() },
		{ C::ScheduleMessage, tr::lng_shortcuts_schedule() },
		separator,
		{ C::MediaViewerFullscreen, tr::lng_shortcuts_media_fullscreen() },
		separator,
		{ C::MediaPlay, tr::lng_shortcuts_media_play() },
		{ C::MediaPause, tr::lng_shortcuts_media_pause() },
		{ C::MediaPlayPause, tr::lng_shortcuts_media_play_pause() },
		{ C::MediaStop, tr::lng_shortcuts_media_stop() },
		{ C::MediaPrevious, tr::lng_shortcuts_media_previous() },
		{ C::MediaNext, tr::lng_shortcuts_media_next() },
	};
}

[[nodiscard]] QString ToString(const QKeySequence &key) {
	auto result = key.toString();
#ifdef Q_OS_MAC
	result = result.replace(u"Ctrl+"_q, QString() + QChar(0x2318));
	result = result.replace(u"Meta+"_q, QString() + QChar(0x2303));
	result = result.replace(u"Alt+"_q, QString() + QChar(0x2325));
	result = result.replace(u"Shift+"_q, QString() + QChar(0x21E7));
#endif // Q_OS_MAC
	return result;
}

[[nodiscard]] Fn<void()> SetupShortcutsContent(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> content) {
	const auto &defaults = S::KeysDefaults();
	const auto &currents = S::KeysCurrents();

	struct Button {
		S::Command command;
		std::unique_ptr<Ui::SettingsButton> widget;
		rpl::variable<QKeySequence> key;
		rpl::variable<bool> removed;
	};
	struct Entry {
		S::Command command;
		rpl::producer<QString> label;
		std::vector<QKeySequence> original;
		std::vector<QKeySequence> now;
		Ui::VerticalLayout *wrap = nullptr;
		std::vector<std::unique_ptr<Button>> buttons;
	};
	struct State {
		std::vector<Entry> entries;
		rpl::variable<bool> modified;
		rpl::variable<Button*> recording;
		rpl::variable<QKeySequence> lastKey;
		Fn<void(S::Command command)> showMenuFor;
	};
	const auto state = content->lifetime().make_state<State>();
	const auto labeled = Entries();
	auto &entries = state->entries = ranges::views::all(
		labeled
	) | ranges::views::transform([](Labeled labeled) {
		return Entry{ labeled.command, std::move(labeled.label) };
	}) | ranges::to_vector;

	for (const auto &[keys, commands] : defaults) {
		for (const auto command : commands) {
			const auto i = ranges::find(entries, command, &Entry::command);
			if (i != end(entries)) {
				i->original.push_back(keys);
			}
		}
	}

	for (const auto &[keys, commands] : currents) {
		for (const auto command : commands) {
			const auto i = ranges::find(entries, command, &Entry::command);
			if (i != end(entries)) {
				i->now.push_back(keys);
			}
		}
	}

	const auto checkModified = [=] {
		for (const auto &entry : state->entries) {
			auto original = entry.original;
			auto now = entry.now;
			ranges::sort(original);
			ranges::sort(now);
			if (original != now) {
				state->modified = true;
				return;
			}
		}
		state->modified = false;
	};
	checkModified();

	const auto menu = std::make_shared<QPointer<Ui::PopupMenu>>();
	const auto fill = [=](Entry &entry) {
		auto index = 0;
		if (entry.original.empty()) {
			entry.original.push_back(QKeySequence());
		}
		if (entry.now.empty()) {
			entry.now.push_back(QKeySequence());
		}
		for (const auto &now : entry.now) {
			if (index < entry.buttons.size()) {
				entry.buttons[index]->key = now;
				entry.buttons[index]->removed = false;
			} else {
				auto button = std::make_unique<Button>(Button{
					.command = entry.command,
					.key = now,
				});
				const auto raw = button.get();
				const auto widget = entry.wrap->add(
					object_ptr<Ui::SettingsButton>(
						entry.wrap,
						rpl::duplicate(entry.label),
						st::settingsButtonNoIcon));
				const auto keys = Ui::CreateChild<Ui::FlatLabel>(
					widget,
					st::settingsButtonNoIcon.rightLabel);
				keys->show();
				rpl::combine(
					widget->widthValue(),
					rpl::duplicate(entry.label),
					button->key.value(),
					state->recording.value(),
					button->removed.value()
				) | rpl::start_with_next([=](
						int width,
						const QString &button,
						const QKeySequence &key,
						Button *recording,
						bool removed) {
					const auto &st = st::settingsButtonNoIcon;
					const auto available = width
						- st.padding.left()
						- st.padding.right()
						- st.style.font->width(button)
						- st::settingsButtonRightSkip;
					keys->setMarkedText((recording == raw)
						? Ui::Text::Italic(
							tr::lng_shortcuts_recording(tr::now))
						: key.isEmpty()
						? TextWithEntities()
						: removed
						? Ui::Text::Wrapped(
							TextWithEntities{ ToString(key) },
							EntityType::StrikeOut)
						: TextWithEntities{ ToString(key) });
					keys->setTextColorOverride((recording == raw)
						? st::boxTextFgGood->c
						: removed
						? st::attentionButtonFg->c
						: std::optional<QColor>());
					keys->resizeToNaturalWidth(available);
					keys->moveToRight(
						st::settingsButtonRightSkip,
						st.padding.top());
				}, keys->lifetime());
				keys->setAttribute(Qt::WA_TransparentForMouseEvents);

				widget->setAcceptBoth(true);
				widget->clicks(
				) | rpl::start_with_next([=](Qt::MouseButton button) {
					if (const auto strong = *menu) {
						strong->hideMenu();
						return;
					}
					if (button == Qt::RightButton) {
						state->showMenuFor(raw->command);
					} else {
						S::Pause();
						state->recording = raw;
					}
				}, widget->lifetime());

				button->widget.reset(widget);
				entry.buttons.push_back(std::move(button));
			}
			++index;
		}
		while (entry.wrap->count() > index) {
			entry.buttons.pop_back();
		}
	};
	state->showMenuFor = [=](S::Command command) {
		*menu = Ui::CreateChild<Ui::PopupMenu>(
			content,
			st::popupMenuWithIcons);
		(*menu)->addAction(tr::lng_shortcuts_add_another(tr::now), [=] {
			const auto i = ranges::find(
				state->entries,
				command,
				&Entry::command);
			if (i != end(state->entries)) {
				S::Pause();
				const auto j = ranges::find(i->now, QKeySequence());
				if (j != end(i->now)) {
					state->recording = i->buttons[j - begin(i->now)].get();
				} else {
					i->now.push_back(QKeySequence());
					fill(*i);
					state->recording = i->buttons.back().get();
				}
			}
		}, &st::menuIconTopics);
		(*menu)->popup(QCursor::pos());
	};

	const auto stopRecording = [=](std::optional<QKeySequence> result = {}) {
		const auto button = state->recording.current();
		if (!button) {
			return;
		}
		state->recording = nullptr;
		InvokeQueued(content, [=] {
			InvokeQueued(content, [=] {
				// Let all the shortcut events propagate first.
				S::Unpause();
			});
		});
		auto was = button->key.current();
		const auto now = result.value_or(was);
		if (now == was) {
			if (!now.isEmpty() && (!result || !button->removed.current())) {
				return;
			}
			was = QKeySequence();
			button->removed = false;
		}

		auto changed = false;
		const auto command = button->command;
		for (auto &entry : state->entries) {
			const auto i = ranges::find(
				entry.buttons,
				button,
				&std::unique_ptr<Button>::get);
			if (i != end(entry.buttons)) {
				const auto index = i - begin(entry.buttons);
				if (now.isEmpty()) {
					entry.now.erase(begin(entry.now) + index);
				} else {
					const auto i = ranges::find(entry.now, now);
					if (i == end(entry.now)) {
						entry.now[index] = now;
					} else if (i != begin(entry.now) + index) {
						std::swap(entry.now[index], *i);
						entry.now.erase(i);
					}
				}
				fill(entry);
				checkModified();
			} else if (now != was) {
				const auto i = now.isEmpty()
					? end(entry.now)
					: ranges::find(entry.now, now);
				if (i != end(entry.now)) {
					entry.buttons[i - begin(entry.now)]->removed = true;
				}
				const auto j = was.isEmpty()
					? end(entry.now)
					: ranges::find(entry.now, was);
				if (j != end(entry.now)) {
					entry.buttons[j - begin(entry.now)]->removed = false;
					S::Change(was, now, command, entry.command);
					was = QKeySequence();
					changed = true;
				}
			}
		}
		if (!changed) {
			S::Change(was, now, command);
		}
	};
	base::install_event_filter(content, qApp, [=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::ShortcutOverride && state->recording.current()) {
			if (!content->window()->isActiveWindow()) {
				return base::EventFilterResult::Continue;
			}
			const auto key = static_cast<QKeyEvent*>(e.get());
			const auto m = key->modifiers();
			const auto integration = QGuiApplicationPrivate::platformIntegration();
			const auto k = key->key();
			const auto clear = !m
				&& (k == Qt::Key_Backspace || k == Qt::Key_Delete);
			if (k == Qt::Key_Control
				|| k == Qt::Key_Shift
				|| k == Qt::Key_Alt
				|| k == Qt::Key_Meta) {
				return base::EventFilterResult::Cancel;
			} else if (!m && !clear && !S::AllowWithoutModifiers(k)) {
				if (k != Qt::Key_Escape) {
					// Intercept this KeyPress event.
					stopRecording();
				}
				return base::EventFilterResult::Cancel;
			}
			const auto r = [&] {
				auto result = int(k);
				if (m & Qt::ShiftModifier) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
					const auto mapper = integration->keyMapper();
					const auto list = mapper->possibleKeyCombinations(key);
					for (const auto &possible : list) {
						if (possible.keyboardModifiers() == m) {
							return int(possible.key());
						}
					}
#else // Qt >= 6.7.0
					const auto keys = integration->possibleKeys(key);
					for (const auto possible : keys) {
						if (possible > int(m)) {
							return possible - int(m);
						}
					}
#endif // Qt < 6.7.0
				}
				return result;
			}();
			stopRecording(clear ? QKeySequence() : QKeySequence(r | m));
			return base::EventFilterResult::Cancel;
		} else if (type == QEvent::KeyPress && state->recording.current()) {
			if (!content->window()->isActiveWindow()) {
				return base::EventFilterResult::Continue;
			}
			if (static_cast<QKeyEvent*>(e.get())->key() == Qt::Key_Escape) {
				stopRecording();
				return base::EventFilterResult::Cancel;
			}
		}
		return base::EventFilterResult::Continue;
	});

	const auto modifiedWrap = content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			content,
			object_ptr<Ui::VerticalLayout>(content)));
	const auto modifiedInner = modifiedWrap->entity();
	AddDivider(modifiedInner);
	AddSkip(modifiedInner);
	const auto reset = modifiedInner->add(object_ptr<Ui::SettingsButton>(
		modifiedInner,
		tr::lng_shortcuts_reset(),
		st::settingsButtonNoIcon));
	reset->setClickedCallback([=] {
		stopRecording();
		for (auto &entry : state->entries) {
			if (entry.now != entry.original) {
				entry.now = entry.original;
				fill(entry);
			}
		}
		checkModified();
		S::ResetToDefaults();
	});
	AddSkip(modifiedInner);
	AddDivider(modifiedInner);
	modifiedWrap->toggleOn(state->modified.value());

	AddSkip(content);
	for (auto &entry : entries) {
		if (!entry.label) {
			AddSkip(content);
			AddDivider(content);
			AddSkip(content);
			continue;
		}
		entry.wrap = content->add(object_ptr<Ui::VerticalLayout>(content));
		fill(entry);
	}

	return [=] {
	};
}

} // namespace

Shortcuts::Shortcuts(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent) {
	setupContent(controller);
}

Shortcuts::~Shortcuts() {
	if (!Core::Quitting()) {
		_save();
	}
}

rpl::producer<QString> Shortcuts::title() {
	return tr::lng_settings_shortcuts();
}

void Shortcuts::setupContent(
		not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	_save = SetupShortcutsContent(controller, content);

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
