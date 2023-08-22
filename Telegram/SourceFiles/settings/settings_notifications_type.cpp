/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_notifications_type.h"

#include "api/api_ringtones.h"
#include "apiwrap.h"
#include "base/unixtime.h"
#include "boxes/ringtones_box.h"
#include "data/notify/data_notify_settings.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

using Notify = Data::DefaultNotify;

template <Notify kType>
[[nodiscard]] Type Id() {
	return &NotificationsTypeMetaImplementation<kType>::Meta;
}

[[nodiscard]] rpl::producer<QString> Title(Notify type) {
	switch (type) {
	case Notify::User: return tr::lng_notification_title_private_chats();
	case Notify::Group: return tr::lng_notification_title_groups();
	case Notify::Broadcast: return tr::lng_notification_title_channels();
	}
	Unexpected("Type in Title.");
}

void SetupChecks(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		Notify type) {
	AddSubsectionTitle(container, Title(type));

	const auto session = &controller->session();
	const auto settings = &session->data().notifySettings();

	const auto enabled = container->add(
		CreateButton(
			container,
			tr::lng_notification_enable(),
			st::settingsButton,
			{ &st::menuIconNotifications }));
	enabled->toggleOn(NotificationsEnabledForTypeValue(session, type));

	const auto soundWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	soundWrap->toggleOn(enabled->toggledValue());
	soundWrap->finishAnimating();

	const auto soundInner = soundWrap->entity();
	const auto soundValue = [=] {
		const auto sound = settings->defaultSettings(type).sound();
		return !sound || !sound->none;
	};
	const auto sound = soundInner->add(
		CreateButton(
			soundInner,
			tr::lng_notification_sound(),
			st::settingsButton,
			{ &st::menuIconUnmute }));
	sound->toggleOn(rpl::single(
		soundValue()
	) | rpl::then(settings->defaultUpdates(
		type
	) | rpl::map([=] { return soundValue(); })));

	const auto toneWrap = soundInner->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	toneWrap->toggleOn(sound->toggledValue());
	toneWrap->finishAnimating();

	const auto toneInner = toneWrap->entity();
	const auto toneLabel = toneInner->lifetime(
	).make_state<rpl::event_stream<QString>>();
	const auto toneValue = [=] {
		const auto sound = settings->defaultSettings(type).sound();
		return sound.value_or(Data::NotifySound());
	};
	const auto label = [=] {
		const auto now = toneValue();
		return !now.id
			? tr::lng_ringtones_box_default(tr::now)
			: ExtractRingtoneName(session->data().document(now.id));
	};
	settings->defaultUpdates(
		Data::DefaultNotify::User
	) | rpl::start_with_next([=] {
		toneLabel->fire(label());
	}, toneInner->lifetime());
	session->api().ringtones().listUpdates(
	) | rpl::start_with_next([=] {
		toneLabel->fire(label());
	}, toneInner->lifetime());

	const auto tone = AddButtonWithLabel(
		toneInner,
		tr::lng_notification_tone(),
		toneLabel->events_starting_with(label()),
		st::settingsButton,
		{ &st::menuIconSoundOn });

	enabled->toggledValue(
	) | rpl::filter([=](bool value) {
		return (value != NotificationsEnabledForType(session, type));
	}) | rpl::start_with_next([=](bool value) {
		settings->defaultUpdate(type, Data::MuteValue{
			.unmute = value,
			.forever = !value,
		});
	}, sound->lifetime());

	sound->toggledValue(
	) | rpl::filter([=](bool enabled) {
		const auto sound = settings->defaultSettings(type).sound();
		return (!sound || !sound->none) != enabled;
	}) | rpl::start_with_next([=](bool enabled) {
		const auto value = Data::NotifySound{ .none = !enabled };
		settings->defaultUpdate(type, {}, {}, value);
	}, sound->lifetime());

	tone->setClickedCallback([=] {
		controller->show(Box(RingtonesBox, session, toneValue(), [=](
				Data::NotifySound sound) {
			settings->defaultUpdate(type, {}, {}, sound);
		}));
	});
}

void SetupExceptions(
	not_null<Ui::VerticalLayout*> container,
	not_null<Window::SessionController*> controller,
	Notify type) {
}

} // namespace

Type NotificationsTypeId(Notify type) {
	switch (type) {
	case Notify::User: return Id<Notify::User>();
	case Notify::Group: return Id<Notify::Group>();
	case Notify::Broadcast: return Id<Notify::Broadcast>();
	}
	Unexpected("Type in NotificationTypeId.");
}

NotificationsType::NotificationsType(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	Notify type)
: AbstractSection(parent)
, _type(type) {
	setupContent(controller);
}

rpl::producer<QString> NotificationsType::title() {
	switch (_type) {
	case Notify::User: return tr::lng_notification_private_chats();
	case Notify::Group: return tr::lng_notification_groups();
	case Notify::Broadcast: return tr::lng_notification_channels();
	}
	Unexpected("Type in NotificationsType.");
}

Type NotificationsType::id() const {
	return NotificationsTypeId(_type);
}

void NotificationsType::setupContent(
		not_null<Window::SessionController*> controller) {
	const auto container = Ui::CreateChild<Ui::VerticalLayout>(this);

	AddSkip(container, st::settingsPrivacySkip);
	SetupChecks(container, controller, _type);

	AddSkip(container);
	AddDivider(container);
	AddSkip(container);

	SetupExceptions(container, controller, _type);

	Ui::ResizeFitChild(this, container);
}

bool NotificationsEnabledForType(
		not_null<Main::Session*> session,
		Notify type) {
	const auto settings = &session->data().notifySettings();
	const auto until = settings->defaultSettings(type).muteUntil();
	return until && (*until <= base::unixtime::now());
}

rpl::producer<bool> NotificationsEnabledForTypeValue(
		not_null<Main::Session*> session,
		Data::DefaultNotify type) {
	const auto settings = &session->data().notifySettings();
	return rpl::single(
		rpl::empty
	) | rpl::then(
		settings->defaultUpdates(type)
	) | rpl::map([=] {
		return NotificationsEnabledForType(session, type);
	});
}

} // namespace Settings
