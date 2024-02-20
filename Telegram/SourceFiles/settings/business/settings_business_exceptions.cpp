/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/business/settings_business_exceptions.h"

#include "boxes/filters/edit_filter_chats_list.h"
#include "boxes/filters/edit_filter_chats_preview.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

namespace Settings {
namespace {

using Flag = Data::ChatFilter::Flag;
using Flags = Data::ChatFilter::Flags;

[[nodiscard]] Flags TypesToFlags(Data::BusinessChatTypes types) {
	using Type = Data::BusinessChatType;
	return ((types & Type::Contacts) ? Flag::Contacts : Flag())
		| ((types & Type::NonContacts) ? Flag::NonContacts : Flag())
		| ((types & Type::NewChats) ? Flag::NewChats : Flag())
		| ((types & Type::ExistingChats) ? Flag::ExistingChats : Flag());
}

[[nodiscard]] Data::BusinessChatTypes FlagsToTypes(Flags flags) {
	using Type = Data::BusinessChatType;
	return ((flags & Flag::Contacts) ? Type::Contacts : Type())
		| ((flags & Flag::NonContacts) ? Type::NonContacts : Type())
		| ((flags & Flag::NewChats) ? Type::NewChats : Type())
		| ((flags & Flag::ExistingChats) ? Type::ExistingChats : Type());
}

} // namespace

void EditBusinessExceptions(
		not_null<Window::SessionController*> window,
		BusinessExceptionsDescriptor &&descriptor) {
	const auto session = &window->session();
	const auto options = Flag::ExistingChats
		| Flag::NewChats
		| Flag::Contacts
		| Flag::NonContacts;
	auto &&peers = descriptor.current.list | ranges::views::transform([=](
			not_null<UserData*> user) {
		return user->owner().history(user);
	});
	auto controller = std::make_unique<EditFilterChatsListController>(
		session,
		(descriptor.allow
			? tr::lng_filters_include_title()
			: tr::lng_filters_exclude_title()),
		options,
		TypesToFlags(descriptor.current.types) & options,
		base::flat_set<not_null<History*>>(begin(peers), end(peers)),
		[=](int count) {
			return nullptr; AssertIsDebug();
		});
	const auto rawController = controller.get();
	const auto save = descriptor.save;
	auto initBox = [=](not_null<PeerListBox*> box) {
		box->setCloseByOutsideClick(false);
		box->addButton(tr::lng_settings_save(), crl::guard(box, [=] {
			const auto peers = box->collectSelectedRows();
			auto &&users = ranges::views::all(
				peers
			) | ranges::views::transform([=](not_null<PeerData*> peer) {
				return not_null(peer->asUser());
			}) | ranges::to_vector;
			save(Data::BusinessExceptions{
				.types = FlagsToTypes(rawController->chosenOptions()),
				.list = std::move(users),
			});
			box->closeBox();
		}));
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	};
	window->show(
		Box<PeerListBox>(std::move(controller), std::move(initBox)));
}

not_null<FilterChatsPreview*> SetupBusinessExceptionsPreview(
		not_null<Ui::VerticalLayout*> content,
		not_null<rpl::variable<Data::BusinessExceptions>*> data) {
	const auto rules = data->current();

	const auto locked = std::make_shared<bool>();
	auto &&peers = data->current().list | ranges::views::transform([=](
			not_null<UserData*> user) {
		return user->owner().history(user);
	});
	const auto preview = content->add(object_ptr<FilterChatsPreview>(
		content,
		TypesToFlags(data->current().types),
		base::flat_set<not_null<History*>>(begin(peers), end(peers))));

	preview->flagRemoved(
	) | rpl::start_with_next([=](Flag flag) {
		*locked = true;
		*data = Data::BusinessExceptions{
			data->current().types & ~FlagsToTypes(flag),
			data->current().list
		};
		*locked = false;
	}, preview->lifetime());

	preview->peerRemoved(
	) | rpl::start_with_next([=](not_null<History*> history) {
		auto list = data->current().list;
		list.erase(
			ranges::remove(list, not_null(history->peer->asUser())),
			end(list));

		*locked = true;
		*data = Data::BusinessExceptions{
			data->current().types,
			std::move(list)
		};
		*locked = false;
	}, preview->lifetime());

	data->changes(
	) | rpl::filter([=] {
		return !*locked;
	}) | rpl::start_with_next([=](const Data::BusinessExceptions &rules) {
		auto &&peers = rules.list | ranges::views::transform([=](
				not_null<UserData*> user) {
			return user->owner().history(user);
		});
		preview->updateData(
			TypesToFlags(rules.types),
			base::flat_set<not_null<History*>>(begin(peers), end(peers)));
	}, preview->lifetime());

	return preview;
}

} // namespace Settings
