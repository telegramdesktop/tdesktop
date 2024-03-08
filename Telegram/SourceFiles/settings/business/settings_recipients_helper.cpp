/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/business/settings_recipients_helper.h"

#include "boxes/filters/edit_filter_chats_list.h"
#include "boxes/filters/edit_filter_chats_preview.h"
#include "data/business/data_shortcut_messages.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/widgets/checkbox.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

constexpr auto kAllExcept = 0;
constexpr auto kSelectedOnly = 1;

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

void EditBusinessChats(
		not_null<Window::SessionController*> window,
		BusinessChatsDescriptor &&descriptor) {
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
		(descriptor.include
			? tr::lng_filters_include_title()
			: tr::lng_filters_exclude_title()),
		options,
		TypesToFlags(descriptor.current.types) & options,
		base::flat_set<not_null<History*>>(begin(peers), end(peers)),
		100,
		nullptr);
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
			save(Data::BusinessChats{
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

not_null<FilterChatsPreview*> SetupBusinessChatsPreview(
		not_null<Ui::VerticalLayout*> container,
		not_null<rpl::variable<Data::BusinessChats>*> data) {
	const auto rules = data->current();

	const auto locked = std::make_shared<bool>();
	auto &&peers = data->current().list | ranges::views::transform([=](
			not_null<UserData*> user) {
		return user->owner().history(user);
	});
	const auto preview = container->add(object_ptr<FilterChatsPreview>(
		container,
		TypesToFlags(data->current().types),
		base::flat_set<not_null<History*>>(begin(peers), end(peers))));

	preview->flagRemoved(
	) | rpl::start_with_next([=](Flag flag) {
		*locked = true;
		*data = Data::BusinessChats{
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
		*data = Data::BusinessChats{
			data->current().types,
			std::move(list)
		};
		*locked = false;
	}, preview->lifetime());

	data->changes(
	) | rpl::filter([=] {
		return !*locked;
	}) | rpl::start_with_next([=](const Data::BusinessChats &rules) {
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

void AddBusinessRecipientsSelector(
		not_null<Ui::VerticalLayout*> container,
		BusinessRecipientsSelectorDescriptor &&descriptor) {
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, std::move(descriptor.title));

	auto &lifetime = container->lifetime();
	const auto controller = descriptor.controller;
	const auto data = descriptor.data;
	const auto change = [=](Fn<void(Data::BusinessRecipients&)> modify) {
		auto now = data->current();
		modify(now);
		*data = std::move(now);
	};
	const auto &current = data->current();
	const auto all = current.allButExcluded || current.included.empty();
	const auto group = std::make_shared<Ui::RadiobuttonGroup>(
		all ? kAllExcept : kSelectedOnly);
	container->add(
		object_ptr<Ui::Radiobutton>(
			container,
			group,
			kAllExcept,
			tr::lng_chatbots_all_except(tr::now),
			st::settingsChatbotsAccess),
		st::settingsChatbotsAccessMargins);
	container->add(
		object_ptr<Ui::Radiobutton>(
			container,
			group,
			kSelectedOnly,
			tr::lng_chatbots_selected(tr::now),
			st::settingsChatbotsAccess),
		st::settingsChatbotsAccessMargins);

	Ui::AddSkip(container, st::settingsChatbotsAccessSkip);
	Ui::AddDivider(container);

	const auto excludeWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container))
	)->setDuration(0);
	const auto excludeInner = excludeWrap->entity();

	Ui::AddSkip(excludeInner);
	Ui::AddSubsectionTitle(excludeInner, tr::lng_chatbots_excluded_title());
	const auto excludeAdd = AddButtonWithIcon(
		excludeInner,
		tr::lng_chatbots_exclude_button(),
		st::settingsChatbotsAdd,
		{ &st::settingsIconRemove, IconType::Round, &st::windowBgActive });
	excludeAdd->setClickedCallback([=] {
		const auto save = [=](Data::BusinessChats value) {
			change([&](Data::BusinessRecipients &data) {
				data.excluded = std::move(value);
			});
		};
		EditBusinessChats(controller, {
			.current = data->current().excluded,
			.save = crl::guard(excludeAdd, save),
			.include = false,
		});
	});

	const auto excluded = lifetime.make_state<
		rpl::variable<Data::BusinessChats>
	>(data->current().excluded);
	data->changes(
	) | rpl::start_with_next([=](const Data::BusinessRecipients &value) {
		*excluded = value.excluded;
	}, lifetime);
	excluded->changes(
	) | rpl::start_with_next([=](Data::BusinessChats &&value) {
		auto now = data->current();
		now.excluded = std::move(value);
		*data = std::move(now);
	}, lifetime);

	SetupBusinessChatsPreview(excludeInner, excluded);

	excludeWrap->toggleOn(data->value(
	) | rpl::map([](const Data::BusinessRecipients &value) {
		return value.allButExcluded;
	}));
	excludeWrap->finishAnimating();

	const auto includeWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container))
	)->setDuration(0);
	const auto includeInner = includeWrap->entity();

	Ui::AddSkip(includeInner);
	Ui::AddSubsectionTitle(includeInner, tr::lng_chatbots_included_title());
	const auto includeAdd = AddButtonWithIcon(
		includeInner,
		tr::lng_chatbots_include_button(),
		st::settingsChatbotsAdd,
		{ &st::settingsIconAdd, IconType::Round, &st::windowBgActive });
	includeAdd->setClickedCallback([=] {
		const auto save = [=](Data::BusinessChats value) {
			change([&](Data::BusinessRecipients &data) {
				data.included = std::move(value);
			});
		};
		EditBusinessChats(controller, {
			.current = data->current().included ,
			.save = crl::guard(includeAdd, save),
			.include = true,
		});
	});

	const auto included = lifetime.make_state<
		rpl::variable<Data::BusinessChats>
	>(data->current().included);
	data->changes(
	) | rpl::start_with_next([=](const Data::BusinessRecipients &value) {
		*included = value.included;
	}, lifetime);
	included->changes(
	) | rpl::start_with_next([=](Data::BusinessChats &&value) {
		change([&](Data::BusinessRecipients &data) {
			data.included = std::move(value);
		});
	}, lifetime);

	SetupBusinessChatsPreview(includeInner, included);
	included->value(
	) | rpl::start_with_next([=](const Data::BusinessChats &value) {
		if (value.empty() && group->current() == kSelectedOnly) {
			group->setValue(kAllExcept);
		}
	}, lifetime);

	includeWrap->toggleOn(data->value(
	) | rpl::map([](const Data::BusinessRecipients &value) {
		return !value.allButExcluded;
	}));
	includeWrap->finishAnimating();

	group->setChangedCallback([=](int value) {
		if (value == kSelectedOnly && data->current().included.empty()) {
			group->setValue(kAllExcept);
			const auto save = [=](Data::BusinessChats value) {
				change([&](Data::BusinessRecipients &data) {
					data.included = std::move(value);
				});
				group->setValue(kSelectedOnly);
			};
			EditBusinessChats(controller, {
				.save = crl::guard(includeAdd, save),
				.include = true,
			});
			return;
		}
		change([&](Data::BusinessRecipients &data) {
			data.allButExcluded = (value == kAllExcept);
		});
	});
}

int ShortcutsCount(not_null<Main::Session*> session) {
	const auto &shortcuts = session->data().shortcutMessages().shortcuts();
	auto result = 0;
	for (const auto &[_, shortcut] : shortcuts.list) {
		if (shortcut.count > 0) {
			++result;
		}
	}
	return result;
}

rpl::producer<int> ShortcutsCountValue(not_null<Main::Session*> session) {
	const auto messages = &session->data().shortcutMessages();
	return rpl::single(rpl::empty) | rpl::then(
		messages->shortcutsChanged()
	) | rpl::map([=] {
		return ShortcutsCount(session);
	});
}

int ShortcutMessagesCount(
		not_null<Main::Session*> session,
		const QString &name) {
	const auto &shortcuts = session->data().shortcutMessages().shortcuts();
	for (const auto &[_, shortcut] : shortcuts.list) {
		if (shortcut.name == name) {
			return shortcut.count;
		}
	}
	return 0;
}

rpl::producer<int> ShortcutMessagesCountValue(
		not_null<Main::Session*> session,
		const QString &name) {
	const auto messages = &session->data().shortcutMessages();
	return rpl::single(rpl::empty) | rpl::then(
		messages->shortcutsChanged()
	) | rpl::map([=] {
		return ShortcutMessagesCount(session, name);
	});
}

bool ShortcutExists(not_null<Main::Session*> session, const QString &name) {
	return ShortcutMessagesCount(session, name) > 0;
}

rpl::producer<bool> ShortcutExistsValue(
		not_null<Main::Session*> session,
		const QString &name) {
	return ShortcutMessagesCountValue(session, name)
		| rpl::map(rpl::mappers::_1 > 0);
}

int ShortcutsLimit(not_null<Main::Session*> session) {
	const auto appConfig = &session->account().appConfig();
	return appConfig->get<int>("quick_replies_limit", 100);
}

rpl::producer<int> ShortcutsLimitValue(not_null<Main::Session*> session) {
	const auto appConfig = &session->account().appConfig();
	return appConfig->value() | rpl::map([=] {
		return ShortcutsLimit(session);
	});
}

int ShortcutMessagesLimit(not_null<Main::Session*> session) {
	const auto appConfig = &session->account().appConfig();
	return appConfig->get<int>("quick_reply_messages_limit", 20);
}

rpl::producer<int> ShortcutMessagesLimitValue(
		not_null<Main::Session*> session) {
	const auto appConfig = &session->account().appConfig();
	return appConfig->value() | rpl::map([=] {
		return ShortcutMessagesLimit(session);
	});
}

BusinessShortcutId LookupShortcutId(
		not_null<Main::Session*> session,
		const QString &name) {
	const auto messages = &session->data().shortcutMessages();
	for (const auto &[id, shortcut] : messages->shortcuts().list) {
		if (shortcut.name == name) {
			return id;
		}
	}
	return {};
}

} // namespace Settings
