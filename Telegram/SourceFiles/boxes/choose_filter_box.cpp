/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/choose_filter_box.h"

#include "apiwrap.h"
#include "boxes/premium_limits_box.h"
#include "core/application.h" // primaryWindow
#include "data/data_chat_filters.h"
#include "data/data_session.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/filter_icons.h"
#include "ui/text/text_utilities.h" // Ui::Text::Bold
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_settings.h"
#include "styles/style_payments.h" // paymentsSectionButton
#include "styles/style_media_player.h" // mediaPlayerMenuCheck

namespace {

Data::ChatFilter ChangedFilter(
		const Data::ChatFilter &filter,
		not_null<History*> history,
		bool add) {
	auto always = base::duplicate(filter.always());
	if (add) {
		always.insert(history);
	} else {
		always.remove(history);
	}
	auto never = base::duplicate(filter.never());
	if (add) {
		never.remove(history);
	} else {
		never.insert(history);
	}
	return Data::ChatFilter(
		filter.id(),
		filter.title(),
		filter.iconEmoji(),
		filter.flags(),
		std::move(always),
		filter.pinned(),
		std::move(never));
}

void ChangeFilterById(
		FilterId filterId,
		not_null<History*> history,
		bool add) {
	Expects(filterId != 0);

	const auto list = history->owner().chatsFilters().list();
	const auto i = ranges::find(list, filterId, &Data::ChatFilter::id);
	if (i != end(list)) {
		const auto was = *i;
		const auto filter = ChangedFilter(was, history, add);
		history->owner().chatsFilters().set(filter);
		history->session().api().request(MTPmessages_UpdateDialogFilter(
			MTP_flags(MTPmessages_UpdateDialogFilter::Flag::f_filter),
			MTP_int(filter.id()),
			filter.tl()
		)).done([=, chat = history->peer->name(), name = filter.title()]{
			// Since only the primary window has dialogs list,
			// We can safely show toast there.
			if (const auto controller = Core::App().primaryWindow()) {
				auto text = (add
					? tr::lng_filters_toast_add
					: tr::lng_filters_toast_remove)(
						tr::now,
						lt_chat,
						Ui::Text::Bold(chat),
						lt_folder,
						Ui::Text::Bold(name),
						Ui::Text::WithEntities);
				Ui::Toast::Show(
					Window::Show(controller).toastParent(),
					{ .text = std::move(text), .st = &st::defaultToast });
			}
		}).fail([=](const MTP::Error &error) {
			// Revert filter on fail.
			history->owner().chatsFilters().set(was);
		}).send();
	}
}

} // namespace

ChooseFilterValidator::ChooseFilterValidator(not_null<History*> history)
: _history(history) {
}

bool ChooseFilterValidator::canAdd() const {
	for (const auto &filter : _history->owner().chatsFilters().list()) {
		if (filter.id() && !filter.contains(_history)) {
			return true;
		}
	}
	return false;
}

bool ChooseFilterValidator::canRemove(FilterId filterId) const {
	Expects(filterId != 0);

	const auto list = _history->owner().chatsFilters().list();
	const auto i = ranges::find(list, filterId, &Data::ChatFilter::id);
	if (i != end(list)) {
		const auto &filter = *i;
		return filter.contains(_history)
			&& ((filter.always().size() > 1) || filter.flags());
	}
	return false;
}

ChooseFilterValidator::LimitData ChooseFilterValidator::limitReached(
		FilterId filterId) const {
	Expects(filterId != 0);

	const auto list = _history->owner().chatsFilters().list();
	const auto i = ranges::find(list, filterId, &Data::ChatFilter::id);
	const auto limit = _history->owner().pinnedChatsLimit(nullptr, filterId);
	return {
		.reached = (i != end(list))
			&& !ranges::contains(i->always(), _history)
			&& (i->always().size() >= limit),
		.count = int(i->always().size()),
	};
}

void ChooseFilterValidator::add(FilterId filterId) const {
	ChangeFilterById(filterId, _history, true);
}

void ChooseFilterValidator::remove(FilterId filterId) const {
	ChangeFilterById(filterId, _history, false);
}

void FillChooseFilterMenu(
		not_null<Window::SessionController*> controller,
		not_null<Ui::PopupMenu*> menu,
		not_null<History*> history) {
	const auto weak = base::make_weak(controller);
	const auto validator = ChooseFilterValidator(history);
	for (const auto &filter : history->owner().chatsFilters().list()) {
		const auto id = filter.id();
		if (!id) {
			continue;
		}

		const auto contains = filter.contains(history);
		const auto action = menu->addAction(filter.title(), [=] {
			if (filter.contains(history)) {
				if (validator.canRemove(id)) {
					validator.remove(id);
				}
			} else if (const auto r = validator.limitReached(id); r.reached) {
				controller->show(Box(
					FilterChatsLimitBox,
					&controller->session(),
					r.count));
			} else if (validator.canAdd()) {
				validator.add(id);
			}
		}, contains ? &st::mediaPlayerMenuCheck : nullptr);
		action->setEnabled(contains
			? validator.canRemove(id)
			: validator.canAdd());
	}
	history->owner().chatsFilters().changed(
	) | rpl::start_with_next([=] {
		menu->hideMenu();
	}, menu->lifetime());
}
