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
#include "chat_helpers/compose/compose_show.h"
#include "core/shortcuts.h"
#include "history/view/media/history_view_sticker.h"
#include "history/view/reactions/history_view_reactions_selector.h"
#include "history/view/history_view_schedule_box.h"
#include "lang/lang_keys.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "data/data_peer.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_message_reactions.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "history/history.h"
#include "history/history_unread_things.h"
#include "apiwrap.h"
#include "window/themes/window_theme.h"
#include "window/section_widget.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_menu_icons.h"

#include <QtWidgets/QApplication>

namespace SendMenu {
namespace {

class EffectPreview final : public Ui::RpWidget {
public:
	EffectPreview(
		not_null<QWidget*> parent,
		std::shared_ptr<ChatHelpers::Show> show,
		Details details,
		QPoint position,
		const Data::Reaction &effect,
		Fn<void(Action, Details)> action);
private:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;

	void setupGeometry(QPoint position);
	void setupBackground();
	void setupSend(Details details);

	const std::shared_ptr<ChatHelpers::Show> _show;
	const std::shared_ptr<Ui::ChatTheme> _theme;
	const std::unique_ptr<Ui::ChatStyle> _chatStyle;
	const std::unique_ptr<Ui::FlatButton> _send;
	const Fn<void(Action, Details)> _actionWithEffect;
	QRect _inner;
	QImage _bg;

};

[[nodiscard]] Data::PossibleItemReactionsRef LookupPossibleEffects(
		not_null<Main::Session*> session) {
	auto result = Data::PossibleItemReactionsRef();
	const auto reactions = &session->data().reactions();
	const auto &effects = reactions->list(Data::Reactions::Type::Effects);
	const auto premiumPossible = session->premiumPossible();
	auto added = base::flat_set<Data::ReactionId>();
	result.recent.reserve(effects.size());
	for (const auto &reaction : effects) {
		if (premiumPossible || !reaction.premium) {
			if (added.emplace(reaction.id).second) {
				result.recent.push_back(&reaction);
			}
		}
	}
	return result;
}

void ShowEffectPreview(
		not_null<QWidget*> parent,
		std::shared_ptr<ChatHelpers::Show> show,
		Details details,
		QPoint position,
		const Data::Reaction &effect,
		Fn<void(Action, Details)> action) {
	const auto widget = Ui::CreateChild<EffectPreview>(
		parent,
		show,
		details,
		position,
		effect,
		action);
	widget->raise();
	widget->show();
}

[[nodiscard]] Fn<void(Action, Details)> ComposeActionWithEffect(
		Fn<void(Action, Details)> sendAction,
		EffectId id) {
	if (!id) {
		return sendAction;
	}
	return [=](Action action, Details details) {
		if (const auto options = std::get_if<Api::SendOptions>(&action)) {
			options->effectId = id;
		}
		sendAction(action, details);
	};
}

EffectPreview::EffectPreview(
	not_null<QWidget*> parent,
	std::shared_ptr<ChatHelpers::Show> show,
	Details details,
	QPoint position,
	const Data::Reaction &effect,
	Fn<void(Action, Details)> action)
: RpWidget(parent)
, _show(show)
, _theme(Window::Theme::DefaultChatThemeOn(lifetime()))
, _chatStyle(
	std::make_unique<Ui::ChatStyle>(
		_show->session().colorIndicesValue()))
, _send(
	std::make_unique<Ui::FlatButton>(
		this,
		u"Send with Effect"_q,AssertIsDebug()
		st::previewMarkRead))
, _actionWithEffect(ComposeActionWithEffect(action, effect.id.custom())) {
	setupGeometry(position);
	setupBackground();
	setupSend(details);
}

void EffectPreview::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.drawImage(0, 0, _bg);
}

void EffectPreview::mousePressEvent(QMouseEvent *e) {
	delete this;
}

void EffectPreview::setupGeometry(QPoint position) {
	const auto parent = parentWidget();
	const auto innerSize = HistoryView::Sticker::MessageEffectSize();
	const auto shadow = st::previewMenu.shadow;
	const auto extend = shadow.extend;
	_inner = QRect(QPoint(extend.left(), extend.top()), innerSize);
	const auto size = _inner.marginsAdded(extend).size();
	const auto left = std::max(
		std::min(
			position.x() - size.width() / 2,
			parent->width() - size.width()),
		0);
	const auto topMin = std::min((parent->height() - size.height()) / 2, 0);
	const auto top = std::max(
		std::min(
			position.y() - size.height() / 2,
			parent->height() - size.height()),
		topMin);
	setGeometry(left, top, size.width(), size.height() + _send->height());
	_send->setGeometry(0, size.height(), size.width(), _send->height());
}

void EffectPreview::setupBackground() {
	const auto ratio = style::DevicePixelRatio();
	_bg = QImage(
		_inner.size() * ratio,
		QImage::Format_ARGB32_Premultiplied);

	const auto paint = [=] {
		auto p = QPainter(&_bg);
		Window::SectionWidget::PaintBackground(
			p,
			_theme.get(),
			QSize(width(), height() * 5),
			QRect(QPoint(), size()));
	};
	paint();
	_theme->repaintBackgroundRequests() | rpl::start_with_next([=] {
		paint();
		update();
	}, lifetime());
}

void EffectPreview::setupSend(Details details) {
	_send->setClickedCallback([=] {
		_actionWithEffect(Api::SendOptions(), details);
	});
	const auto type = details.type;
	SetupMenuAndShortcuts(_send.get(), _show, [=] {
		return Details{ .type = type };
	}, _actionWithEffect);
}

} // namespace

Fn<void(Action, Details)> DefaultCallback(
		std::shared_ptr<ChatHelpers::Show> show,
		Fn<void(Api::SendOptions)> send) {
	const auto guard = Ui::MakeWeak(show->toastParent());
	return [=](Action action, Details details) {
		if (const auto options = std::get_if<Api::SendOptions>(&action)) {
			send(*options);
		} else if (v::get<ActionType>(action) == ActionType::Send) {
			send({});
		} else {
			using namespace HistoryView;
			auto box = PrepareScheduleBox(guard, show, details, send);
			const auto weak = Ui::MakeWeak(box.data());
			show->showBox(std::move(box));
			if (const auto strong = weak.data()) {
				strong->setCloseByOutsideClick(false);
			}
		}
	};
}

FillMenuResult FillSendMenu(
		not_null<Ui::PopupMenu*> menu,
		std::shared_ptr<ChatHelpers::Show> showForEffect,
		Details details,
		Fn<void(Action, Details)> action,
		const style::ComposeIcons *iconsOverride,
		std::optional<QPoint> desiredPositionOverride) {
	const auto type = details.type;
	if (type == Type::Disabled || !action) {
		return FillMenuResult::Skipped;
	}
	const auto &icons = iconsOverride
		? *iconsOverride
		: st::defaultComposeIcons;

	if (type != Type::Reminder) {
		menu->addAction(
			tr::lng_send_silent_message(tr::now),
			[=] { action(Api::SendOptions{ .silent = true }, details); },
			&icons.menuMute);
	}
	if (type != Type::SilentOnly) {
		menu->addAction(
			(type == Type::Reminder
				? tr::lng_reminder_message(tr::now)
				: tr::lng_schedule_message(tr::now)),
			[=] { action(ActionType::Schedule, details); },
			&icons.menuSchedule);
	}
	if (type == Type::ScheduledToUser) {
		menu->addAction(
			tr::lng_scheduled_send_until_online(tr::now),
			[=] { action(Api::DefaultSendWhenOnlineOptions(), details); },
			&icons.menuWhenOnline);
	}

	using namespace HistoryView::Reactions;
	const auto position = desiredPositionOverride.value_or(QCursor::pos());
	const auto selector = (showForEffect && details.effectAllowed)
		? AttachSelectorToMenu(
			menu,
			position,
			st::reactPanelEmojiPan,
			showForEffect,
			LookupPossibleEffects(&showForEffect->session()),
			{ tr::lng_effect_add_title(tr::now) })
		: base::make_unexpected(AttachSelectorResult::Skipped);
	if (!selector) {
		if (selector.error() == AttachSelectorResult::Failed) {
			return FillMenuResult::Failed;
		}
		menu->prepareGeometryFor(position);
		return FillMenuResult::Prepared;
	}

	(*selector)->chosen(
	) | rpl::start_with_next([=](ChosenReaction chosen) {
		const auto &reactions = showForEffect->session().data().reactions();
		const auto &effects = reactions.list(Data::Reactions::Type::Effects);
		const auto i = ranges::find(effects, chosen.id, &Data::Reaction::id);
		if (i != end(effects)) {
			ShowEffectPreview(
				menu,
				showForEffect,
				details,
				menu->mapFromGlobal(chosen.globalGeometry.center()),
				*i,
				action);
		}
	}, menu->lifetime());

	return FillMenuResult::Prepared;
}

void SetupMenuAndShortcuts(
		not_null<Ui::RpWidget*> button,
		std::shared_ptr<ChatHelpers::Show> show,
		Fn<Details()> details,
		Fn<void(Action, Details)> action) {
	const auto menu = std::make_shared<base::unique_qptr<Ui::PopupMenu>>();
	const auto showMenu = [=] {
		*menu = base::make_unique_q<Ui::PopupMenu>(
			button,
			st::popupMenuWithIcons);
		const auto result = FillSendMenu(*menu, show, details(), action);
		if (result != FillMenuResult::Prepared) {
			return false;
		}
		(*menu)->popupPrepared();
		return true;
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

		const auto now = details().type;
		if (now == Type::Disabled) {
			return;
		}
		((now != Type::Reminder)
			&& request->check(Command::SendSilentMessage)
			&& request->handle([=] {
				action(Api::SendOptions{ .silent = true }, details());
				return true;
			}))
		||
		((now != Type::SilentOnly)
			&& request->check(Command::ScheduleMessage)
			&& request->handle([=] {
				action(ActionType::Schedule, details());
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
