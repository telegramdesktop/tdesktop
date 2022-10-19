/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "menu/menu_send.h"

#include "api/api_common.h"
#include "base/event_filter.h"
#include "boxes/abstract_box.h"
#include "core/shortcuts.h"
#include "history/view/history_view_schedule_box.h"
#include "lang/lang_keys.h"
#include "ui/widgets/popup_menu.h"
#include "data/data_peer.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "history/history.h"
#include "history/history_unread_things.h"
#include "apiwrap.h"
#include "styles/style_menu_icons.h"

#include <QtWidgets/QApplication>

namespace SendMenu {

Fn<void()> DefaultSilentCallback(Fn<void(Api::SendOptions)> send) {
	return [=] { send({ .silent = true }); };
}

Fn<void()> DefaultScheduleCallback(
		not_null<Ui::RpWidget*> parent,
		Type type,
		Fn<void(Api::SendOptions)> send) {
	const auto weak = Ui::MakeWeak(parent);
	return [=] {
		Ui::show(
			HistoryView::PrepareScheduleBox(
				weak,
				type,
				[=](Api::SendOptions options) { send(options); }),
			Ui::LayerOption::KeepOther);
	};
}

FillMenuResult FillSendMenu(
		not_null<Ui::PopupMenu*> menu,
		Type type,
		Fn<void()> silent,
		Fn<void()> schedule) {
	if (!silent && !schedule) {
		return FillMenuResult::None;
	}
	const auto now = type;
	if (now == Type::Disabled
		|| (!silent && now == Type::SilentOnly)) {
		return FillMenuResult::None;
	}

	if (silent && now != Type::Reminder) {
		menu->addAction(
			tr::lng_send_silent_message(tr::now),
			silent,
			&st::menuIconMute);
	}
	if (schedule && now != Type::SilentOnly) {
		menu->addAction(
			(now == Type::Reminder
				? tr::lng_reminder_message(tr::now)
				: tr::lng_schedule_message(tr::now)),
			schedule,
			&st::menuIconSchedule);
	}
	return FillMenuResult::Success;
}

void SetupMenuAndShortcuts(
		not_null<Ui::RpWidget*> button,
		Fn<Type()> type,
		Fn<void()> silent,
		Fn<void()> schedule) {
	if (!silent && !schedule) {
		return;
	}
	const auto menu = std::make_shared<base::unique_qptr<Ui::PopupMenu>>();
	const auto showMenu = [=] {
		*menu = base::make_unique_q<Ui::PopupMenu>(
			button,
			st::popupMenuWithIcons);
		const auto result = FillSendMenu(*menu, type(), silent, schedule);
		const auto success = (result == FillMenuResult::Success);
		if (success) {
			(*menu)->popup(QCursor::pos());
		}
		return success;
	};
	base::install_event_filter(button, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::ContextMenu && showMenu()) {
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	});

	Shortcuts::Requests(
	) | rpl::filter([=] {
		return button->isActiveWindow();
	}) | rpl::start_with_next([=](not_null<Shortcuts::Request*> request) {
		using Command = Shortcuts::Command;

		const auto now = type();
		if (now == Type::Disabled
			|| (!silent && now == Type::SilentOnly)) {
			return;
		}
		(silent
			&& (now != Type::Reminder)
			&& request->check(Command::SendSilentMessage)
			&& request->handle([=] {
				silent();
				return true;
			}))
		||
		(schedule
			&& (now != Type::SilentOnly)
			&& request->check(Command::ScheduleMessage)
			&& request->handle([=] {
				schedule();
				return true;
			}))
		||
		(request->check(Command::JustSendMessage) && request->handle([=] {
			const auto post = [&](QEvent::Type type) {
				QApplication::postEvent(
					button,
					new QMouseEvent(
						type,
						QPointF(0, 0),
						Qt::LeftButton,
						Qt::LeftButton,
						Qt::NoModifier));
			};
			post(QEvent::MouseButtonPress);
			post(QEvent::MouseButtonRelease);
			return true;
		}));
	}, button->lifetime());
}

void SetupReadAllMenu(
		not_null<Ui::RpWidget*> button,
		Fn<Data::Thread*()> currentThread,
		const QString &text,
		Fn<void(not_null<Data::Thread*>, Fn<void()>)> sendReadRequest) {
	struct State {
		base::unique_qptr<Ui::PopupMenu> menu;
		base::flat_set<base::weak_ptr<Data::Thread>> sentForEntries;
	};
	const auto state = std::make_shared<State>();
	const auto showMenu = [=] {
		const auto thread = base::make_weak(currentThread());
		if (!thread) {
			return;
		}
		state->menu = base::make_unique_q<Ui::PopupMenu>(
			button,
			st::popupMenuWithIcons);
		state->menu->addAction(text, [=] {
			const auto strong = thread.get();
			if (!strong || !state->sentForEntries.emplace(thread).second) {
				return;
			}
			sendReadRequest(strong, [=] {
				state->sentForEntries.remove(thread);
			});
		}, &st::menuIconMarkRead);
		state->menu->popup(QCursor::pos());
	};

	base::install_event_filter(button, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::ContextMenu) {
			showMenu();
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	});
}

void SetupUnreadMentionsMenu(
		not_null<Ui::RpWidget*> button,
		Fn<Data::Thread*()> currentThread) {
	const auto text = tr::lng_context_mark_read_mentions_all(tr::now);
	const auto sendOne = [=](
			base::weak_ptr<Data::Thread> weakThread,
			Fn<void()> done,
			auto resend) -> void {
		const auto thread = weakThread.get();
		if (!thread) {
			done();
			return;
		}
		const auto peer = thread->peer();
		const auto topic = thread->asTopic();
		const auto rootId = topic ? topic->rootId() : 0;
		using Flag = MTPmessages_ReadMentions::Flag;
		peer->session().api().request(MTPmessages_ReadMentions(
			MTP_flags(rootId ? Flag::f_top_msg_id : Flag()),
			peer->input,
			MTP_int(rootId)
		)).done([=](const MTPmessages_AffectedHistory &result) {
			const auto offset = peer->session().api().applyAffectedHistory(
				peer,
				result);
			if (offset > 0) {
				resend(weakThread, done, resend);
			} else {
				done();
				peer->owner().history(peer)->clearUnreadMentionsFor(rootId);
			}
		}).fail(done).send();
	};
	const auto sendRequest = [=](
			not_null<Data::Thread*> thread,
			Fn<void()> done) {
		sendOne(base::make_weak(thread), std::move(done), sendOne);
	};
	SetupReadAllMenu(button, currentThread, text, sendRequest);
}

void SetupUnreadReactionsMenu(
		not_null<Ui::RpWidget*> button,
		Fn<Data::Thread*()> currentThread) {
	const auto text = tr::lng_context_mark_read_reactions_all(tr::now);
	const auto sendOne = [=](
			base::weak_ptr<Data::Thread> weakThread,
			Fn<void()> done,
			auto resend) -> void {
		const auto thread = weakThread.get();
		if (!thread) {
			done();
			return;
		}
		const auto topic = thread->asTopic();
		const auto peer = thread->peer();
		const auto rootId = topic ? topic->rootId() : 0;
		using Flag = MTPmessages_ReadReactions::Flag;
		peer->session().api().request(MTPmessages_ReadReactions(
			MTP_flags(rootId ? Flag::f_top_msg_id : Flag(0)),
			peer->input,
			MTP_int(rootId)
		)).done([=](const MTPmessages_AffectedHistory &result) {
			const auto offset = peer->session().api().applyAffectedHistory(
				peer,
				result);
			if (offset > 0) {
				resend(weakThread, done, resend);
			} else {
				done();
				peer->owner().history(peer)->clearUnreadReactionsFor(rootId);
			}
		}).fail(done).send();
	};
	const auto sendRequest = [=](
			not_null<Data::Thread*> thread,
			Fn<void()> done) {
		sendOne(base::make_weak(thread), std::move(done), sendOne);
	};
	SetupReadAllMenu(button, currentThread, text, sendRequest);
}

} // namespace SendMenu
