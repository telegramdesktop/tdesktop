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
#include "data/data_chat_filters.h"
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "data/data_unread_value.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_folders.h"
#include "ui/widgets/chat_filters_tabs_slider.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/slide_wrap.h"
#include "window/window_controller.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_dialogs.h" // dialogsSearchTabs
#include "styles/style_menu_icons.h"

#include <QScrollBar>

namespace Ui {
namespace {

struct State final {
	Ui::Animations::Simple animation;
	std::optional<FilterId> lastFilterId = std::nullopt;
	rpl::lifetime unreadLifetime;
	base::unique_qptr<Ui::PopupMenu> menu;

	Api::RemoveComplexChatFilter removeApi;
	bool waitingSuggested = false;
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
		addAction(
			tr::lng_filters_context_remove(tr::now),
			std::move(showRemoveBox),
			&st::menuIconDelete);
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

} // namespace

not_null<Ui::RpWidget*> AddChatFiltersTabsStrip(
		not_null<Ui::RpWidget*> parent,
		not_null<Main::Session*> session,
		rpl::producer<int> multiSelectHeightValue,
		Fn<void(int)> setAddedTopScrollSkip,
		Fn<void(FilterId)> choose,
		bool trackActiveChatsFilter) {
	const auto window = Core::App().findWindow(parent);
	const auto controller = window ? window->sessionController() : nullptr;

	const auto &scrollSt = st::defaultScrollArea;
	const auto wrap = Ui::CreateChild<Ui::SlideWrap<Ui::RpWidget>>(
		parent,
		object_ptr<Ui::RpWidget>(parent));
	if (!controller) {
		return wrap;
	}
	const auto container = wrap->entity();
	const auto scroll = Ui::CreateChild<Ui::ScrollArea>(container, scrollSt);
	const auto sliderPadding = st::dialogsSearchTabsPadding;
	const auto slider = scroll->setOwnedWidget(
		object_ptr<Ui::PaddingWrap<Ui::ChatsFiltersTabs>>(
			parent,
			object_ptr<Ui::ChatsFiltersTabs>(parent, st::dialogsSearchTabs),
			QMargins(sliderPadding, 0, sliderPadding, 0)))->entity();
	const auto state = wrap->lifetime().make_state<State>();
	wrap->toggle(false, anim::type::instant);
	container->sizeValue() | rpl::start_with_next([=](const QSize &s) {
		scroll->resize(s + QSize(0, scrollSt.deltax * 4));
	}, scroll->lifetime());
	rpl::combine(
		parent->widthValue(),
		slider->heightValue()
	) | rpl::start_with_next([=](int w, int h) {
		container->resize(w, h);
	}, wrap->lifetime());
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
		choose(filter.id());
	};

	const auto filterByIndex = [=](int index) -> const Data::ChatFilter& {
		const auto &list = session->data().chatsFilters().list();
		Assert(index >= 0 && index < list.size());
		return list[index];
	};

	const auto rebuild = [=] {
		const auto &list = session->data().chatsFilters().list();
		auto sections = ranges::views::all(
			list
		) | ranges::views::transform([](const Data::ChatFilter &filter) {
			return filter.title().isEmpty()
				? tr::lng_filters_all(tr::now)
				: filter.title();
		}) | ranges::to_vector;
		slider->setSections(std::move(sections));
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
			}, slider->lifetime());
		}
		{
			auto includeMuted = Data::IncludeMutedCounterFoldersValue();
			state->unreadLifetime.destroy();
			for (auto i = 0; i < list.size(); i++) {
				rpl::combine(
					Data::UnreadStateValue(session, list[i].id()),
					rpl::duplicate(includeMuted)
				) | rpl::start_with_next([=](
						const Dialogs::UnreadState &state,
						bool includeMuted) {
					const auto muted = (state.chatsMuted + state.marksMuted);
					const auto count = (state.chats + state.marks)
						- (includeMuted ? 0 : muted);
					slider->setUnreadCount(i, count);
					slider->fitWidthToSections();
				}, state->unreadLifetime);
			}
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
		if (trackActiveChatsFilter) {
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
			}, slider->lifetime());
		}
		slider->sectionActivated() | rpl::distinct_until_changed(
		) | rpl::start_with_next([=](int index) {
			const auto &filter = filterByIndex(index);
			state->lastFilterId = filter.id();
			scrollToIndex(index, anim::type::normal);
			applyFilter(filter);
		}, wrap->lifetime());
		slider->contextMenuRequested() | rpl::start_with_next([=](int index) {
			ShowMenu(wrap, controller, state, index);
		}, slider->lifetime());
		wrap->toggle((list.size() > 1), anim::type::instant);
	};
	session->data().chatsFilters().changed(
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

	{
		std::move(
			multiSelectHeightValue
		) | rpl::start_with_next([=](int height) {
			wrap->moveToLeft(0, height);
		}, wrap->lifetime());
		wrap->heightValue() | rpl::start_with_next([=](int height) {
			setAddedTopScrollSkip(height);
		}, wrap->lifetime());
		parent->widthValue() | rpl::start_with_next([=](int w) {
			wrap->resizeToWidth(w);
		}, wrap->lifetime());
	}

	return wrap;
}

} // namespace Ui
