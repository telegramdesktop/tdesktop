/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_notifications_reactions.h"

#include "api/api_reactions_notify_settings.h"
#include "apiwrap.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/sections/settings_notifications.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "settings/settings_notifications_common.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

using NotifyFrom = Api::ReactionsNotifyFrom;
using namespace Builder;

[[nodiscard]] rpl::producer<QString> FromLabel(NotifyFrom from) {
	switch (from) {
	case NotifyFrom::None:
		return tr::lng_notification_reactions_from_nobody();
	case NotifyFrom::Contacts:
		return tr::lng_notification_reactions_from_contacts();
	case NotifyFrom::All:
		return tr::lng_notification_reactions_from_all();
	}
	Unexpected("Value in FromLabel.");
}

void ShowFromBox(
		not_null<Window::SessionController*> controller,
		NotifyFrom current,
		Fn<void(NotifyFrom)> done) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_notification_reactions_from());

		const auto initial = (current == NotifyFrom::None)
			? int(NotifyFrom::All)
			: int(current);
		const auto group = std::make_shared<Ui::RadiobuttonGroup>(initial);

		const auto addOption = [&](NotifyFrom value, const QString &label) {
			box->addRow(
				object_ptr<Ui::Radiobutton>(
					box,
					group,
					int(value),
					label,
					st::defaultBoxCheckbox),
				st::boxOptionListPadding
					+ QMargins(
						st::boxPadding.left(),
						0,
						st::boxPadding.right(),
						st::boxOptionListSkip));
		};
		addOption(
			NotifyFrom::All,
			tr::lng_notification_reactions_from_all(tr::now));
		addOption(
			NotifyFrom::Contacts,
			tr::lng_notification_reactions_from_contacts(tr::now));
		box->addButton(tr::lng_box_ok(), [=] {
			done(NotifyFrom(group->current()));
			box->closeBox();
		});
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	}));
}

void AddToggleRow(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		rpl::producer<QString> title,
		const style::icon *icon,
		rpl::producer<NotifyFrom> fromValue,
		Fn<NotifyFrom()> fromCurrent,
		Fn<void(NotifyFrom)> updateFrom) {
	auto forToggle = rpl::duplicate(fromValue);
	auto status = std::move(
		fromValue
	) | rpl::map([](NotifyFrom from) {
		return FromLabel(from);
	}) | rpl::flatten_latest();

	const auto [button, toggleButton, checkView] = SetupSplitToggle(
		container,
		std::move(title),
		icon,
		fromCurrent() != NotifyFrom::None,
		std::move(status));

	std::move(
		forToggle
	) | rpl::on_next([=](NotifyFrom from) {
		checkView->setChecked(
			from != NotifyFrom::None,
			anim::type::normal);
	}, button->lifetime());

	toggleButton->clicks(
	) | rpl::on_next([=] {
		const auto enabled = !checkView->checked();
		updateFrom(enabled ? NotifyFrom::All : NotifyFrom::None);
	}, toggleButton->lifetime());

	button->setClickedCallback([=] {
		if (fromCurrent() == NotifyFrom::None) {
			updateFrom(NotifyFrom::All);
			return;
		}
		ShowFromBox(controller, fromCurrent(), [=](NotifyFrom from) {
			updateFrom(from);
		});
	});
}

void BuildNotificationsReactionsContent(SectionBuilder &builder) {
	builder.addSkip(st::settingsCheckboxesSkip);
	builder.addSubsectionTitle({
		.id = u"notifications/reactions/about"_q,
		.title = tr::lng_notification_reactions_notify_about(),
	});

	builder.add([](const WidgetContext &ctx) {
		const auto session = &ctx.controller->session();
		auto &rs = session->api().reactionsNotifySettings();

		AddToggleRow(
			ctx.container,
			ctx.controller,
			tr::lng_notification_reactions_messages_full(),
			&st::menuIconMarkUnread,
			rs.messagesFrom(),
			[session] {
				return session->api().reactionsNotifySettings()
					.messagesFromCurrent();
			},
			[session](NotifyFrom from) {
				session->api().reactionsNotifySettings()
					.updateMessagesFrom(from);
			});

		AddToggleRow(
			ctx.container,
			ctx.controller,
			tr::lng_notification_reactions_poll_votes_full(),
			&st::menuIconCreatePoll,
			rs.pollVotesFrom(),
			[session] {
				return session->api().reactionsNotifySettings()
					.pollVotesFromCurrent();
			},
			[session](NotifyFrom from) {
				session->api().reactionsNotifySettings()
					.updatePollVotesFrom(from);
			});

		return SectionBuilder::WidgetToAdd{};
	}, [] {
		return SearchEntry{
			.id = u"notifications/reactions/messages"_q,
			.title = tr::lng_notification_reactions_messages_full(tr::now),
			.keywords = { u"reactions"_q, u"messages"_q },
		};
	});

	builder.addSkip(st::settingsCheckboxesSkip);
	builder.addDivider();
	builder.addSkip(st::settingsCheckboxesSkip);
	builder.addSubsectionTitle({
		.id = u"notifications/reactions/settings"_q,
		.title = tr::lng_notification_reactions_settings(),
	});

	builder.add([](const WidgetContext &ctx) {
		const auto session = &ctx.controller->session();
		auto &rs = session->api().reactionsNotifySettings();

		const auto showSender = AddButtonWithIcon(
			ctx.container,
			tr::lng_notification_reactions_show_sender(),
			st::settingsButtonNoIcon
		)->toggleOn(rs.showPreviews());

		showSender->toggledChanges(
		) | rpl::filter([session](bool checked) {
			return (checked
				!= session->api().reactionsNotifySettings()
					.showPreviewsCurrent());
		}) | rpl::on_next([session](bool checked) {
			session->api().reactionsNotifySettings()
				.updateShowPreviews(checked);
		}, showSender->lifetime());

		return SectionBuilder::WidgetToAdd{};
	}, [] {
		return SearchEntry{
			.id = u"notifications/reactions/preview"_q,
			.title = tr::lng_notification_reactions_show_sender(tr::now),
			.keywords = { u"sender"_q, u"preview"_q },
		};
	});
}

const auto kMeta = BuildHelper({
	.id = NotificationsReactions::Id(),
	.parentId = NotificationsId(),
	.title = &tr::lng_notification_reactions,
	.icon = &st::menuIconGroupReactions,
}, [](SectionBuilder &builder) {
	BuildNotificationsReactionsContent(builder);
});

} // namespace

NotificationsReactions::NotificationsReactions(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent(controller);
}

rpl::producer<QString> NotificationsReactions::title() {
	return tr::lng_notification_reactions_title();
}

void NotificationsReactions::setupContent(
		not_null<Window::SessionController*> controller) {
	const auto container = Ui::CreateChild<Ui::VerticalLayout>(this);

	const SectionBuildMethod buildMethod = [](
			not_null<Ui::VerticalLayout*> container,
			not_null<Window::SessionController*> controller,
			Fn<void(Type)> showOther,
			rpl::producer<> showFinished) {
		auto &lifetime = container->lifetime();
		const auto highlights
			= lifetime.make_state<HighlightRegistry>();

		const auto session = &controller->session();
		auto &rs = session->api().reactionsNotifySettings();
		rs.reload();

		auto builder = SectionBuilder(WidgetContext{
			.container = container,
			.controller = controller,
			.showOther = std::move(showOther),
			.isPaused = Window::PausedIn(
				controller,
				Window::GifPauseReason::Layer),
			.highlights = highlights,
		});
		BuildNotificationsReactionsContent(builder);

		std::move(showFinished) | rpl::on_next([=] {
			for (const auto &[id, entry] : *highlights) {
				if (entry.widget) {
					controller->checkHighlightControl(
						id,
						entry.widget,
						base::duplicate(entry.args));
				}
			}
		}, lifetime);
	};

	build(container, buildMethod);

	Ui::ResizeFitChild(this, container);
}

} // namespace Settings
