/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/chat_filters_tabs_strip.h"

#include "api/api_chat_filters_remove_manager.h"
#include "boxes/filters/edit_filter_box.h"
#include "boxes/premium_limits_box.h"
#include "core/application.h"
#include "core/ui_integration.h"
#include "data/data_chat_filters.h"
#include "data/data_peer_values.h" // Data::AmPremiumValue.
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "data/data_unread_value.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_folders.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/power_saving.h"
#include "ui/ui_utility.h"
#include "ui/widgets/chat_filters_tabs_slider_reorder.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/slide_wrap.h"
#include "window/window_controller.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_dialogs.h" // dialogsSearchTabs
#include "styles/style_media_player.h" // mediaPlayerMenuCheck
#include "styles/style_menu_icons.h"

#include <QScrollBar>

namespace Ui {
namespace {

struct State final {
	Ui::Animations::Simple animation;
	std::optional<FilterId> lastFilterId = std::nullopt;
	rpl::lifetime rebuildLifetime;
	rpl::lifetime reorderLifetime;
	base::unique_qptr<Ui::PopupMenu> menu;

	Api::RemoveComplexChatFilter removeApi;
	bool waitingSuggested = false;

	std::unique_ptr<Ui::ChatsFiltersTabsReorder> reorder;
	bool ignoreRefresh = false;
};

void ShowMenu(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<State*> state,
		int index) {
	const auto session = &controller->session();

	auto id = FilterId(0);
	{
		const auto &list = session->data().chatsFilters().list();
		if (index < 0 || index >= list.size()) {
			return;
		}
		id = list[index].id();
	}
	state->menu = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::popupMenuWithIcons);
	const auto addAction = Ui::Menu::CreateAddActionCallback(
		state->menu.get());

	if (id) {
		addAction(
			tr::lng_filters_context_edit(tr::now),
			[=] { EditExistingFilter(controller, id); },
			&st::menuIconEdit);

		Window::MenuAddMarkAsReadChatListAction(
			controller,
			[=] { return session->data().chatsFilters().chatsList(id); },
			addAction);

		auto showRemoveBox = [=] {
			state->removeApi.request(Ui::MakeWeak(parent), controller, id);
		};
		addAction({
			.text = tr::lng_filters_context_remove(tr::now),
			.handler = std::move(showRemoveBox),
			.icon = &st::menuIconDeleteAttention,
			.isAttention = true,
		});
	} else {
		auto customUnreadState = [=] {
			return Data::MainListMapUnreadState(
				session,
				session->data().chatsList()->unreadState());
		};
		Window::MenuAddMarkAsReadChatListAction(
			controller,
			[=] { return session->data().chatsList(); },
			addAction,
			std::move(customUnreadState));

		auto openFiltersSettings = [=] {
			const auto filters = &session->data().chatsFilters();
			if (filters->suggestedLoaded()) {
				controller->showSettings(Settings::Folders::Id());
			} else if (!state->waitingSuggested) {
				state->waitingSuggested = true;
				filters->requestSuggested();
				filters->suggestedUpdated(
				) | rpl::take(1) | rpl::start_with_next([=] {
					controller->showSettings(Settings::Folders::Id());
				}, parent->lifetime());
			}
		};
		addAction(
			tr::lng_filters_setup_menu(tr::now),
			std::move(openFiltersSettings),
			&st::menuIconEdit);
	}
	if (state->menu->empty()) {
		state->menu = nullptr;
		return;
	}
	state->menu->popup(QCursor::pos());
}

void ShowFiltersListMenu(
		not_null<Ui::RpWidget*> parent,
		not_null<Main::Session*> session,
		not_null<State*> state,
		int active,
		Fn<void(int)> changeActive) {
	const auto &list = session->data().chatsFilters().list();

	state->menu = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::popupMenuWithIcons);

	const auto reorderAll = session->user()->isPremium();
	const auto maxLimit = (reorderAll ? 1 : 0)
		+ Data::PremiumLimits(session).dialogFiltersCurrent();
	const auto premiumFrom = (reorderAll ? 0 : 1) + maxLimit;

	for (auto i = 0; i < list.size(); ++i) {
		const auto title = list[i].title();
		const auto text = title.text.empty()
			? tr::lng_filters_all_short(tr::now)
			: title.text.text;
		const auto callback = [=] {
			if (i != active) {
				changeActive(i);
			}
		};
		const auto icon = (i == active)
			? &st::mediaPlayerMenuCheck
			: nullptr;
		const auto action = Ui::Menu::CreateAction(
			state->menu.get(),
			text,
			callback);
		auto item = base::make_unique_q<Ui::Menu::Action>(
			state->menu.get(),
			state->menu->st().menu,
			action,
			icon,
			icon);
		action->setEnabled(i < premiumFrom);
		if (!title.text.empty()) {
			const auto context = Core::MarkedTextContext{
				.session = session,
				.customEmojiRepaint = [raw = item.get()] { raw->update(); },
				.customEmojiLoopLimit = title.isStatic ? -1 : 0,
			};
			item->setMarkedText(title.text, QString(), context);
		}
		state->menu->addAction(std::move(item));
	}
	session->data().chatsFilters().changed() | rpl::start_with_next([=] {
		state->menu->hideMenu();
	}, state->menu->lifetime());

	if (state->menu->empty()) {
		state->menu = nullptr;
		return;
	}
	state->menu->popup(QCursor::pos());
}

} // namespace

not_null<Ui::RpWidget*> AddChatFiltersTabsStrip(
		not_null<Ui::RpWidget*> parent,
		not_null<Main::Session*> session,
		Fn<void(FilterId)> choose,
		ChatHelpers::PauseReason pauseLevel,
		Window::SessionController *controller,
		bool trackActiveFilterAndUnreadAndReorder) {

	const auto wrap = Ui::CreateChild<Ui::SlideWrap<Ui::RpWidget>>(
		parent,
		object_ptr<Ui::RpWidget>(parent));
	if (!controller) {
		const auto window = Core::App().findWindow(parent);
		controller = window ? window->sessionController() : nullptr;
		if (!controller) {
			return wrap;
		}
	}
	const auto container = wrap->entity();
	const auto scroll = Ui::CreateChild<Ui::ScrollArea>(
		container,
		st::dialogsTabsScroll,
		true);
	const auto slider = scroll->setOwnedWidget(
		object_ptr<Ui::ChatsFiltersTabs>(
			parent,
			trackActiveFilterAndUnreadAndReorder
				? st::dialogsSearchTabs
				: st::chatsFiltersTabs));
	const auto state = wrap->lifetime().make_state<State>();
	const auto reassignUnreadValue = [=] {
		const auto &list = session->data().chatsFilters().list();
		auto includeMuted = Data::IncludeMutedCounterFoldersValue();
		for (auto i = 0; i < list.size(); i++) {
			rpl::combine(
				Data::UnreadStateValue(session, list[i].id()),
				rpl::duplicate(includeMuted)
			) | rpl::start_with_next([=](
					const Dialogs::UnreadState &state,
					bool includeMuted) {
				const auto chats = state.chats;
				const auto chatsMuted = state.chatsMuted;
				const auto muted = (chatsMuted + state.marksMuted);
				const auto count = (chats + state.marks)
					- (includeMuted ? 0 : muted);
				const auto isMuted = includeMuted && (count == muted);
				slider->setUnreadCount(i, count, isMuted);
				slider->fitWidthToSections();
			}, state->reorderLifetime);
		}
	};
	if (trackActiveFilterAndUnreadAndReorder) {
		using Reorder = Ui::ChatsFiltersTabsReorder;
		state->reorder = std::make_unique<Reorder>(slider, scroll);
		const auto applyReorder = [=](
				int oldPosition,
				int newPosition) {
			if (newPosition == oldPosition) {
				return;
			}

			const auto filters = &session->data().chatsFilters();
			const auto &list = filters->list();
			if (!session->user()->isPremium()) {
				if (list[0].id() != FilterId()) {
					filters->moveAllToFront();
				}
			}
			Assert(oldPosition >= 0 && oldPosition < list.size());
			Assert(newPosition >= 0 && newPosition < list.size());

			auto order = ranges::views::all(
				list
			) | ranges::views::transform(
				&Data::ChatFilter::id
			) | ranges::to_vector;
			base::reorder(order, oldPosition, newPosition);

			state->ignoreRefresh = true;
			filters->saveOrder(order);
			state->ignoreRefresh = false;
		};

		state->reorder->updates(
		) | rpl::start_with_next([=](const Reorder::Single &data) {
			if (data.state == Reorder::State::Started) {
				slider->setReordering(slider->reordering() + 1);
			} else {
				Ui::PostponeCall(slider, [=] {
					slider->setReordering(slider->reordering() - 1);
				});
				if (data.state == Reorder::State::Applied) {
					applyReorder(data.oldPosition, data.newPosition);
					state->reorderLifetime.destroy();
					reassignUnreadValue();
				}
			}
		}, slider->lifetime());
	}
	wrap->toggle(false, anim::type::instant);
	scroll->setCustomWheelProcess([=](not_null<QWheelEvent*> e) {
		const auto pixelDelta = e->pixelDelta();
		const auto angleDelta = e->angleDelta();
		if (std::abs(pixelDelta.x()) + std::abs(angleDelta.x())) {
			return false;
		}
		const auto bar = scroll->horizontalScrollBar();
		const auto y = pixelDelta.y() ? pixelDelta.y() : angleDelta.y();
		bar->setValue(bar->value() - y);
		return true;
	});

	const auto scrollToIndex = [=](int index, anim::type type) {
		const auto to = index
			? (slider->centerOfSection(index) - scroll->width() / 2)
			: 0;
		const auto bar = scroll->horizontalScrollBar();
		state->animation.stop();
		if (type == anim::type::instant) {
			bar->setValue(to);
		} else {
			state->animation.start(
				[=](float64 v) { bar->setValue(v); },
				bar->value(),
				std::min(to, bar->maximum()),
				st::defaultTabsSlider.duration);
		}
	};

	const auto applyFilter = [=](const Data::ChatFilter &filter) {
		if (slider->reordering()) {
			return;
		}
		choose(filter.id());
	};

	const auto filterByIndex = [=](int index) -> const Data::ChatFilter& {
		const auto &list = session->data().chatsFilters().list();
		Assert(index >= 0 && index < list.size());
		return list[index];
	};

	const auto rebuild = [=] {
		const auto &list = session->data().chatsFilters().list();
		if ((list.size() <= 1 && !slider->width()) || state->ignoreRefresh) {
			return;
		}
		const auto context = Core::MarkedTextContext{
			.session = session,
			.customEmojiRepaint = [=] { slider->update(); },
		};
		const auto paused = [=] {
			return On(PowerSaving::kEmojiChat)
				|| controller->isGifPausedAtLeastFor(pauseLevel);
		};
		const auto sectionsChanged = slider->setSectionsAndCheckChanged(
			ranges::views::all(
				list
			) | ranges::views::transform([](const Data::ChatFilter &filter) {
				auto title = filter.title();
				return title.text.empty()
					? TextWithEntities{ tr::lng_filters_all_short(tr::now) }
					: title.isStatic
					? Data::ForceCustomEmojiStatic(title.text)
					: title.text;
			}) | ranges::to_vector, context, paused);
		if (!sectionsChanged) {
			return;
		}
		state->rebuildLifetime.destroy();
		slider->fitWidthToSections();
		{
			const auto reorderAll = session->user()->isPremium();
			const auto maxLimit = (reorderAll ? 1 : 0)
				+ Data::PremiumLimits(session).dialogFiltersCurrent();
			const auto premiumFrom = (reorderAll ? 0 : 1) + maxLimit;
			slider->setLockedFrom((premiumFrom >= list.size())
				? 0
				: premiumFrom);
			slider->lockedClicked() | rpl::start_with_next([=] {
				controller->show(Box(FiltersLimitBox, session, std::nullopt));
			}, state->rebuildLifetime);
			if (state->reorder) {
				state->reorder->cancel();
				state->reorder->clearPinnedIntervals();
				if (!reorderAll) {
					state->reorder->addPinnedInterval(0, 1);
				}
				state->reorder->addPinnedInterval(
					premiumFrom,
					std::max(1, int(list.size()) - maxLimit));
			}
		}
		if (trackActiveFilterAndUnreadAndReorder) {
			reassignUnreadValue();
		}
		[&] {
			const auto lookingId = state->lastFilterId.value_or(list[0].id());
			for (auto i = 0; i < list.size(); i++) {
				const auto &filter = list[i];
				if (filter.id() == lookingId) {
					const auto wasLast = !!state->lastFilterId;
					state->lastFilterId = filter.id();
					slider->setActiveSectionFast(i);
					scrollToIndex(
						i,
						wasLast ? anim::type::normal : anim::type::instant);
					applyFilter(filter);
					return;
				}
			}
			if (list.size()) {
				const auto index = 0;
				const auto &filter = filterByIndex(index);
				state->lastFilterId = filter.id();
				slider->setActiveSectionFast(index);
				scrollToIndex(index, anim::type::instant);
				applyFilter(filter);
			}
		}();
		if (trackActiveFilterAndUnreadAndReorder) {
			controller->activeChatsFilter(
			) | rpl::start_with_next([=](FilterId id) {
				const auto &list = session->data().chatsFilters().list();
				for (auto i = 0; i < list.size(); ++i) {
					if (list[i].id() == id) {
						slider->setActiveSection(i);
						scrollToIndex(i, anim::type::normal);
						break;
					}
				}
				state->reorder->finishReordering();
			}, state->rebuildLifetime);
		}
		rpl::single(-1) | rpl::then(
			slider->sectionActivated()
		) | rpl::combine_previous(
		) | rpl::start_with_next([=](int was, int index) {
			if (slider->reordering()) {
				return;
			}
			const auto &filter = filterByIndex(index);
			if (was != index) {
				state->lastFilterId = filter.id();
				scrollToIndex(index, anim::type::normal);
			}
			applyFilter(filter);
		}, state->rebuildLifetime);
		slider->contextMenuRequested() | rpl::start_with_next([=](int index) {
			if (trackActiveFilterAndUnreadAndReorder) {
				ShowMenu(wrap, controller, state, index);
			} else {
				ShowFiltersListMenu(
					wrap,
					session,
					state,
					slider->activeSection(),
					[=](int i) { slider->setActiveSection(i); });
			}
		}, state->rebuildLifetime);
		wrap->toggle((list.size() > 1), anim::type::instant);

		if (state->reorder) {
			state->reorder->start();
		}
	};
	rpl::combine(
		session->data().chatsFilters().changed(),
		Data::AmPremiumValue(session) | rpl::to_empty
	) | rpl::start_with_next(rebuild, wrap->lifetime());
	rebuild();

	session->data().chatsFilters().isChatlistChanged(
	) | rpl::start_with_next([=](FilterId id) {
		if (!id || !state->lastFilterId || (id != state->lastFilterId)) {
			return;
		}
		for (const auto &filter : session->data().chatsFilters().list()) {
			if (filter.id() == id) {
				applyFilter(filter);
				return;
			}
		}
	}, wrap->lifetime());

	rpl::combine(
		parent->widthValue() | rpl::filter(rpl::mappers::_1 > 0),
		slider->heightValue() | rpl::filter(rpl::mappers::_1 > 0)
	) | rpl::start_with_next([=](int w, int h) {
		scroll->resize(w, h);
		container->resize(w, h);
		wrap->resize(w, h);
	}, wrap->lifetime());

	return wrap;
}

} // namespace Ui
