/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/filters/edit_filter_box.h"

#include "boxes/filters/edit_filter_chats_list.h"
#include "boxes/filters/edit_filter_chats_preview.h"
#include "boxes/filters/edit_filter_links.h"
#include "boxes/premium_limits_box.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/text/text_options.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/effects/panel_animation.h"
#include "ui/filter_icons.h"
#include "ui/filter_icon_panel.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "data/data_channel.h"
#include "data/data_chat_filters.h"
#include "data/data_peer.h"
#include "data/data_peer_values.h" // Data::AmPremiumValue.
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "settings/settings_common.h"
#include "base/event_filter.h"
#include "lang/lang_keys.h"
#include "history/history.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "apiwrap.h"
#include "styles/style_settings.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_window.h"
#include "styles/style_chat.h"
#include "styles/style_menu_icons.h"

namespace {

using namespace Settings;

constexpr auto kMaxFilterTitleLength = 12;

using Flag = Data::ChatFilter::Flag;
using Flags = Data::ChatFilter::Flags;
using ExceptionPeersRef = const base::flat_set<not_null<History*>> &;
using ExceptionPeersGetter = ExceptionPeersRef(Data::ChatFilter::*)() const;

struct NameEditing {
	not_null<Ui::InputField*> field;
	bool custom = false;
	bool settingDefault = false;
};

not_null<FilterChatsPreview*> SetupChatsPreview(
		not_null<Ui::VerticalLayout*> content,
		not_null<rpl::variable<Data::ChatFilter>*> data,
		Fn<void(const Data::ChatFilter&)> updateDefaultTitle,
		Flags flags,
		ExceptionPeersGetter peers) {
	const auto rules = data->current();
	const auto preview = content->add(object_ptr<FilterChatsPreview>(
		content,
		rules.flags() & flags,
		(rules.*peers)()));

	preview->flagRemoved(
	) | rpl::start_with_next([=](Flag flag) {
		const auto rules = data->current();
		auto computed = Data::ChatFilter(
			rules.id(),
			rules.title(),
			rules.iconEmoji(),
			(rules.flags() & ~flag),
			rules.always(),
			rules.pinned(),
			rules.never());
		updateDefaultTitle(computed);
		*data = std::move(computed);
	}, preview->lifetime());

	preview->peerRemoved(
	) | rpl::start_with_next([=](not_null<History*> history) {
		const auto rules = data->current();
		auto always = rules.always();
		auto pinned = rules.pinned();
		auto never = rules.never();
		always.remove(history);
		pinned.erase(ranges::remove(pinned, history), end(pinned));
		never.remove(history);
		auto computed = Data::ChatFilter(
			rules.id(),
			rules.title(),
			rules.iconEmoji(),
			rules.flags(),
			std::move(always),
			std::move(pinned),
			std::move(never));
		updateDefaultTitle(computed);
		*data = std::move(computed);
	}, preview->lifetime());

	return preview;
}

void EditExceptions(
		not_null<Window::SessionController*> window,
		not_null<QObject*> context,
		Flags options,
		not_null<rpl::variable<Data::ChatFilter>*> data,
		Fn<void(const Data::ChatFilter&)> updateDefaultTitle,
		Fn<void()> refresh) {
	const auto include = (options & Flag::Contacts) != Flags(0);
	const auto rules = data->current();
	const auto session = &window->session();
	const auto limit = Data::PremiumLimits(
		session
	).dialogFiltersChatsCurrent();
	const auto showLimitReached = [=] {
		window->show(Box(FilterChatsLimitBox, session, limit, include));
	};
	auto controller = std::make_unique<EditFilterChatsListController>(
		session,
		(include
			? tr::lng_filters_include_title()
			: tr::lng_filters_exclude_title()),
		options,
		rules.flags() & options,
		include ? rules.always() : rules.never(),
		limit,
		showLimitReached);
	const auto rawController = controller.get();
	auto initBox = [=](not_null<PeerListBox*> box) {
		box->setCloseByOutsideClick(false);
		box->addButton(tr::lng_settings_save(), crl::guard(context, [=] {
			const auto peers = box->collectSelectedRows();
			const auto rules = data->current();
			auto &&histories = ranges::views::all(
				peers
			) | ranges::views::transform([=](not_null<PeerData*> peer) {
				return window->session().data().history(peer);
			});
			auto changed = base::flat_set<not_null<History*>>{
				histories.begin(),
				histories.end()
			};
			auto removeFrom = include ? rules.never() : rules.always();
			for (const auto &history : changed) {
				removeFrom.remove(history);
			}
			auto pinned = rules.pinned();
			pinned.erase(ranges::remove_if(pinned, [&](
					not_null<History*> history) {
				const auto contains = changed.contains(history);
				return include ? !contains : contains;
			}), end(pinned));
			auto computed = Data::ChatFilter(
				rules.id(),
				rules.title(),
				rules.iconEmoji(),
				((rules.flags() & ~options)
					| rawController->chosenOptions()),
				include ? std::move(changed) : std::move(removeFrom),
				std::move(pinned),
				include ? std::move(removeFrom) : std::move(changed));
			updateDefaultTitle(computed);
			*data = computed;
			refresh();
			box->closeBox();
		}));
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	};
	window->window().show(
		Box<PeerListBox>(std::move(controller), std::move(initBox)));
}

void CreateIconSelector(
		not_null<QWidget*> outer,
		not_null<QWidget*> box,
		not_null<QWidget*> parent,
		not_null<Ui::InputField*> input,
		not_null<rpl::variable<Data::ChatFilter>*> data) {
	const auto rules = data->current();
	const auto toggle = Ui::CreateChild<Ui::AbstractButton>(parent.get());
	toggle->resize(st::windowFilterIconToggleSize);

	const auto type = toggle->lifetime().make_state<Ui::FilterIcon>();
	data->value(
	) | rpl::map([=](const Data::ChatFilter &filter) {
		return Ui::ComputeFilterIcon(filter);
	}) | rpl::start_with_next([=](Ui::FilterIcon icon) {
		*type = icon;
		toggle->update();
	}, toggle->lifetime());

	input->geometryValue(
	) | rpl::start_with_next([=](QRect geometry) {
		const auto left = geometry.x() + geometry.width() - toggle->width();
		const auto position = st::windowFilterIconTogglePosition;
		toggle->move(
			left - position.x(),
			geometry.y() + position.y());
	}, toggle->lifetime());

	toggle->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(toggle);
		const auto icons = Ui::LookupFilterIcon(*type);
		icons.normal->paintInCenter(
			p,
			toggle->rect(),
			st::dialogsUnreadBgMuted->c);
	}, toggle->lifetime());

	const auto panel = toggle->lifetime().make_state<Ui::FilterIconPanel>(
		outer);
	toggle->installEventFilter(panel);
	toggle->addClickHandler([=] {
		panel->toggleAnimated();
	});
	panel->chosen(
	) | rpl::filter([=](Ui::FilterIcon icon) {
		return icon != Ui::ComputeFilterIcon(data->current());
	}) | rpl::start_with_next([=](Ui::FilterIcon icon) {
		panel->hideAnimated();
		const auto rules = data->current();
		*data = Data::ChatFilter(
			rules.id(),
			rules.title(),
			Ui::LookupFilterIcon(icon).emoji,
			rules.flags(),
			rules.always(),
			rules.pinned(),
			rules.never());
	}, panel->lifetime());

	const auto updatePanelGeometry = [=] {
		const auto global = toggle->mapToGlobal({
			toggle->width(),
			toggle->height()
		});
		const auto local = outer->mapFromGlobal(global);
		const auto position = st::windwoFilterIconPanelPosition;
		const auto padding = panel->innerPadding();
		panel->move(
			local.x() - panel->width() + position.x() + padding.right(),
			local.y() + position.y() - padding.top());
	};

	const auto filterForGeometry = [=](not_null<QEvent*> event) {
		const auto type = event->type();
		if (type == QEvent::Move || type == QEvent::Resize) {
			// updatePanelGeometry uses not only container geometry, but
			// also container children geometries that will be updated later.
			crl::on_main(panel, [=] { updatePanelGeometry(); });
		}
		return base::EventFilterResult::Continue;
	};

	const auto installFilterForGeometry = [&](not_null<QWidget*> target) {
		panel->lifetime().make_state<base::unique_qptr<QObject>>(
			base::install_event_filter(target, filterForGeometry));
	};
	installFilterForGeometry(outer);
	installFilterForGeometry(box);
}

[[nodiscard]] QString DefaultTitle(const Data::ChatFilter &filter) {
	using Icon = Ui::FilterIcon;
	const auto icon = Ui::ComputeDefaultFilterIcon(filter);
	switch (icon) {
	case Icon::Private:
		return (filter.flags() & Data::ChatFilter::Flag::NonContacts)
			? tr::lng_filters_name_people(tr::now)
			: tr::lng_filters_include_contacts(tr::now);
	case Icon::Groups:
		return tr::lng_filters_include_groups(tr::now);
	case Icon::Channels:
		return tr::lng_filters_include_channels(tr::now);
	case Icon::Bots:
		return tr::lng_filters_include_bots(tr::now);
	case Icon::Unread:
		return tr::lng_filters_name_unread(tr::now);
	case Icon::Unmuted:
		return tr::lng_filters_name_unmuted(tr::now);
	}
	return QString();
}

not_null<Ui::SettingsButton*> AddToggledButton(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<bool> shown,
		rpl::producer<QString> text,
		const style::SettingsButton &st,
		IconDescriptor &&descriptor) {
	const auto toggled = container->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			container,
			CreateButtonWithIcon(
				container,
				std::move(text),
				st,
				std::move(descriptor)))
	)->toggleOn(std::move(shown), anim::type::instant)->setDuration(0);
	return toggled->entity();
}

[[nodiscard]] QString TrimDefaultTitle(const QString &title) {
	return (title.size() <= kMaxFilterTitleLength) ? title : QString();
}

} // namespace

void EditFilterBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> window,
		const Data::ChatFilter &filter,
		Fn<void(const Data::ChatFilter &)> doneCallback,
		Fn<void(
			const Data::ChatFilter &data,
			Fn<void(Data::ChatFilter)> next)> saveAnd) {
	using namespace rpl::mappers;

	struct State {
		rpl::variable<Data::ChatFilter> rules;
		rpl::variable<std::vector<Data::ChatFilterLink>> links;
		rpl::variable<bool> hasLinks;
		rpl::variable<bool> chatlist;
		rpl::variable<bool> creating;
	};
	const auto owner = &window->session().data();
	const auto state = box->lifetime().make_state<State>(State{
		.rules = filter,
		.chatlist = filter.chatlist(),
		.creating = filter.title().isEmpty(),
	});
	state->links = owner->chatsFilters().chatlistLinks(filter.id()),
	state->hasLinks = state->links.value() | rpl::map([=](const auto &v) {
		return !v.empty();
	});
	state->hasLinks.value() | rpl::filter(
		_1
	) | rpl::start_with_next([=] {
		state->chatlist = true;
	}, box->lifetime());

	const auto data = &state->rules;

	owner->chatsFilters().isChatlistChanged(
	) | rpl::filter([=](FilterId id) {
		return (id == data->current().id());
	}) | rpl::start_with_next([=](FilterId id) {
		const auto filters = &owner->chatsFilters();
		const auto &list = filters->list();
		const auto i = ranges::find(list, id, &Data::ChatFilter::id);
		if (i == end(list)) {
			return;
		}
		*data = data->current().withChatlist(i->chatlist(), i->hasMyLinks());
		if (!i->chatlist() && !state->hasLinks.current()) {
			state->chatlist = false;
		}
	}, box->lifetime());

	box->setWidth(st::boxWideWidth);
	box->setTitle(rpl::conditional(
		state->creating.value(),
		tr::lng_filters_new(),
		tr::lng_filters_edit()));
	box->setCloseByOutsideClick(false);

	Data::AmPremiumValue(
		&window->session()
	) | rpl::start_with_next([=] {
		box->closeBox();
	}, box->lifetime());

	const auto content = box->verticalLayout();
	const auto name = content->add(
		object_ptr<Ui::InputField>(
			box,
			st::windowFilterNameInput,
			tr::lng_filters_new_name(),
			filter.title()),
		st::markdownLinkFieldPadding);
	name->setMaxLength(kMaxFilterTitleLength);
	name->setInstantReplaces(Ui::InstantReplaces::Default());
	name->setInstantReplacesEnabled(
		Core::App().settings().replaceEmojiValue());
	Ui::Emoji::SuggestionsController::Init(
		box->getDelegate()->outerContainer(),
		name,
		&window->session());

	const auto nameEditing = box->lifetime().make_state<NameEditing>(
		NameEditing{ name });

	state->creating.value(
	) | rpl::filter(!_1) | rpl::start_with_next([=] {
		nameEditing->custom = true;
	}, box->lifetime());

	name->changes(
	) | rpl::start_with_next([=] {
		if (!nameEditing->settingDefault) {
			nameEditing->custom = true;
		}
	}, name->lifetime());
	const auto updateDefaultTitle = [=](const Data::ChatFilter &filter) {
		if (nameEditing->custom) {
			return;
		}
		const auto title = TrimDefaultTitle(DefaultTitle(filter));
		if (nameEditing->field->getLastText() != title) {
			nameEditing->settingDefault = true;
			nameEditing->field->setText(title);
			nameEditing->settingDefault = false;
		}
	};

	const auto outer = box->getDelegate()->outerContainer();
	CreateIconSelector(
		outer,
		box,
		content,
		name,
		data);

	constexpr auto kTypes = Flag::Contacts
		| Flag::NonContacts
		| Flag::Groups
		| Flag::Channels
		| Flag::Bots;
	constexpr auto kExcludeTypes = Flag::NoMuted
		| Flag::NoArchived
		| Flag::NoRead;

	box->setFocusCallback([=] {
		name->setFocusFast();
	});

	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, tr::lng_filters_include());

	const auto includeAdd = AddButtonWithIcon(
		content,
		tr::lng_filters_add_chats(),
		st::settingsButtonActive,
		{ &st::settingsIconAdd, IconType::Round, &st::windowBgActive });

	const auto include = SetupChatsPreview(
		content,
		data,
		updateDefaultTitle,
		kTypes,
		&Data::ChatFilter::always);

	Ui::AddSkip(content);
	Ui::AddDividerText(content, tr::lng_filters_include_about());
	Ui::AddSkip(content);

	auto excludeWrap = content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			content,
			object_ptr<Ui::VerticalLayout>(content))
	)->setDuration(0);
	excludeWrap->toggleOn(state->chatlist.value() | rpl::map(!_1));
	const auto excludeInner = excludeWrap->entity();

	Ui::AddSubsectionTitle(excludeInner, tr::lng_filters_exclude());

	const auto excludeAdd = AddButtonWithIcon(
		excludeInner,
		tr::lng_filters_remove_chats(),
		st::settingsButtonActive,
		{ &st::settingsIconRemove, IconType::Round, &st::windowBgActive });

	const auto exclude = SetupChatsPreview(
		excludeInner,
		data,
		updateDefaultTitle,
		kExcludeTypes,
		&Data::ChatFilter::never);

	Ui::AddSkip(excludeInner);
	Ui::AddDividerText(excludeInner, tr::lng_filters_exclude_about());
	Ui::AddSkip(excludeInner);

	const auto collect = [=]() -> std::optional<Data::ChatFilter> {
		const auto title = name->getLastText().trimmed();
		const auto rules = data->current();
		if (title.isEmpty()) {
			name->showError();
			box->scrollToY(0);
			return {};
		} else if (!(rules.flags() & kTypes) && rules.always().empty()) {
			window->window().showToast(tr::lng_filters_empty(tr::now));
			return {};
		} else if ((rules.flags() == (kTypes | Flag::NoArchived))
			&& rules.always().empty()
			&& rules.never().empty()) {
			window->window().showToast(tr::lng_filters_default(tr::now));
			return {};
		}
		return rules.withTitle(title);
	};

	Ui::AddSubsectionTitle(
		content,
		rpl::conditional(
			state->hasLinks.value(),
			tr::lng_filters_link_has(),
			tr::lng_filters_link()));

	state->hasLinks.changes() | rpl::start_with_next([=] {
		content->resizeToWidth(content->widthNoMargins());
	}, content->lifetime());

	if (filter.chatlist()) {
		window->session().data().chatsFilters().reloadChatlistLinks(
			filter.id());
	}

	const auto createLink = AddToggledButton(
		content,
		state->hasLinks.value() | rpl::map(!rpl::mappers::_1),
		tr::lng_filters_link_create(),
		st::settingsButtonActive,
		{ &st::settingsFolderShareIcon, IconType::Simple });
	const auto addLink = AddToggledButton(
		content,
		state->hasLinks.value(),
		tr::lng_group_invite_add(),
		st::settingsButtonActive,
		{ &st::settingsIconAdd, IconType::Round, &st::windowBgActive });

	SetupFilterLinks(
		content,
		window,
		state->links.value(),
		[=] { return collect().value_or(Data::ChatFilter()); });

	rpl::merge(
		createLink->clicks(),
		addLink->clicks()
	) | rpl::filter(
		(rpl::mappers::_1 == Qt::LeftButton)
	) | rpl::start_with_next([=](Qt::MouseButton button) {
		const auto result = collect();
		if (!result || !GoodForExportFilterLink(window, *result)) {
			return;
		}
		const auto shared = CollectFilterLinkChats(*result);
		if (shared.empty()) {
			window->show(ShowLinkBox(window, *result, {}));
			return;
		}
		saveAnd(*result, crl::guard(box, [=](Data::ChatFilter updated) {
			state->creating = false;

			// Comparison of ChatFilter-s don't take id into account!
			data->force_assign(updated);
			const auto id = updated.id();
			state->links = owner->chatsFilters().chatlistLinks(id);
			ExportFilterLink(id, shared, crl::guard(box, [=](
					Data::ChatFilterLink link) {
				Expects(link.id == id);

				*data = data->current().withChatlist(true, true);
				window->show(ShowLinkBox(window, updated, link));
			}), crl::guard(box, [=](QString error) {
				const auto session = &window->session();
				if (error == u"CHATLISTS_TOO_MUCH"_q) {
					window->show(Box(ShareableFiltersLimitBox, session));
				} else if (error == u"INVITES_TOO_MUCH"_q) {
					window->show(Box(FilterLinksLimitBox, session));
				} else if (error == u"CHANNELS_TOO_MUCH"_q) {
					window->show(Box(ChannelsLimitBox, session));
				} else if (error == u"USER_CHANNELS_TOO_MUCH"_q) {
					window->showToast(
						{ tr::lng_filters_link_group_admin_error(tr::now) });
				} else {
					window->show(ShowLinkBox(window, updated, { .id = id }));
				}
			}));
		}));
	}, createLink->lifetime());
	Ui::AddSkip(content);
	Ui::AddDividerText(
		content,
		rpl::conditional(
			state->hasLinks.value(),
			tr::lng_filters_link_about_many(),
			tr::lng_filters_link_about()));

	const auto show = box->uiShow();
	const auto refreshPreviews = [=] {
		include->updateData(
			data->current().flags() & kTypes,
			data->current().always());
		exclude->updateData(
			data->current().flags() & kExcludeTypes,
			data->current().never());
	};
	includeAdd->setClickedCallback([=] {
		EditExceptions(
			window,
			box,
			kTypes | (state->chatlist.current() ? Flag::Chatlist : Flag()),
			data,
			updateDefaultTitle,
			refreshPreviews);
	});
	excludeAdd->setClickedCallback([=] {
		EditExceptions(
			window,
			box,
			kExcludeTypes,
			data,
			updateDefaultTitle,
			refreshPreviews);
	});

	const auto save = [=] {
		if (const auto result = collect()) {
			box->closeBox();
			doneCallback(*result);
		}
	};

	box->addButton(rpl::conditional(
		state->creating.value(),
		tr::lng_filters_create_button(),
		tr::lng_settings_save()
	), save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void EditExistingFilter(
		not_null<Window::SessionController*> window,
		FilterId id) {
	Expects(id != 0);

	const auto session = &window->session();
	const auto &list = session->data().chatsFilters().list();
	const auto i = ranges::find(list, id, &Data::ChatFilter::id);
	if (i == end(list)) {
		return;
	}
	const auto doneCallback = [=](const Data::ChatFilter &result) {
		Expects(id == result.id());

		const auto tl = result.tl();
		session->data().chatsFilters().apply(MTP_updateDialogFilter(
			MTP_flags(MTPDupdateDialogFilter::Flag::f_filter),
			MTP_int(id),
			tl));
		session->api().request(MTPmessages_UpdateDialogFilter(
			MTP_flags(MTPmessages_UpdateDialogFilter::Flag::f_filter),
			MTP_int(id),
			tl
		)).send();
	};
	const auto saveAnd = [=](
			const Data::ChatFilter &data,
			Fn<void(Data::ChatFilter)> next) {
		doneCallback(data);
		next(data);
	};
	window->window().show(Box(
		EditFilterBox,
		window,
		*i,
		crl::guard(session, doneCallback),
		crl::guard(session, saveAnd)));
}
