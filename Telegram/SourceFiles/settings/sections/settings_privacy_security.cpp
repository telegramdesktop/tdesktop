/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_privacy_security.h"

#include "settings/settings_common_session.h"

#include "api/api_authorizations.h"
#include "api/api_blocked_peers.h"
#include "api/api_cloud_password.h"
#include "api/api_global_privacy.h"
#include "api/api_self_destruct.h"
#include "api/api_sensitive_content.h"
#include "api/api_websites.h"
#include "apiwrap.h"
#include "base/system_unlock.h"
#include "base/timer_rpl.h"
#include "boxes/edit_privacy_box.h"
#include "boxes/passcode_box.h"
#include "boxes/self_destruction_box.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/core_cloud_password.h"
#include "core/core_settings.h"
#include "core/update_checker.h"
#include "data/components/passkeys.h"
#include "data/components/top_peers.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "history/view/media/history_view_media_common.h"
#include "ui/chat/chat_style.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "platform/platform_webauthn.h"
#include "settings/settings_builder.h"
#include "settings/cloud_password/settings_cloud_password_email_confirm.h"
#include "settings/cloud_password/settings_cloud_password_input.h"
#include "settings/cloud_password/settings_cloud_password_start.h"
#include "settings/sections/settings_main.h"
#include "settings/sections/settings_active_sessions.h"
#include "settings/sections/settings_blocked_peers.h"
#include "settings/sections/settings_global_ttl.h"
#include "settings/sections/settings_local_passcode.h"
#include "settings/sections/settings_passkeys.h"
#include "settings/sections/settings_premium.h"
#include "settings/settings_privacy_controllers.h"
#include "settings/sections/settings_websites.h"
#include "storage/storage_domain.h"
#include "ui/boxes/confirm_box.h"
#include "ui/effects/premium_graphics.h"
#include "ui/layers/generic_box.h"
#include "ui/rect.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QtGui/QGuiApplication>
#include <QtSvg/QSvgRenderer>

namespace Settings {
namespace {

constexpr auto kUpdateTimeout = 60 * crl::time(1000);

using Privacy = Api::UserPrivacy;

[[nodiscard]] QImage PremiumStar() {
	const auto factor = style::DevicePixelRatio();
	const auto size = Size(st::settingsButtonNoIcon.style.font->ascent);
	auto image = QImage(
		size * factor,
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(factor);
	image.fill(Qt::transparent);
	{
		auto p = QPainter(&image);
		auto star = QSvgRenderer(
			Ui::Premium::ColorizedSvg(Ui::Premium::ButtonGradientStops()));
		star.render(&p, Rect(size));
	}
	return image;
}

void AddPremiumStar(
		not_null<Ui::SettingsButton*> button,
		not_null<::Main::Session*> session,
		rpl::producer<QString> label,
		const QMargins &padding) {
	const auto badge = Ui::CreateChild<Ui::RpWidget>(button.get());
	badge->showOn(Data::AmPremiumValue(session));
	const auto sampleLeft = st::settingsColorSamplePadding.left();
	const auto badgeLeft = padding.left() + sampleLeft;

	auto star = PremiumStar();
	badge->resize(star.size() / style::DevicePixelRatio());
	badge->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(badge);
		p.drawImage(0, 0, star);
	}, badge->lifetime());

	rpl::combine(
		button->sizeValue(),
		std::move(label)
	) | rpl::on_next([=](const QSize &s, const QString &) {
		if (s.isNull()) {
			return;
		}
		badge->moveToLeft(
			button->fullTextWidth() + badgeLeft,
			(s.height() - badge->height()) / 2);
	}, badge->lifetime());
}

QString PrivacyBase(Privacy::Key key, const Privacy::Rule &rule) {
	using Key = Privacy::Key;
	using Option = Privacy::Option;
	switch (key) {
	case Key::CallsPeer2Peer:
		switch (rule.option) {
		case Option::Everyone:
			return tr::lng_edit_privacy_calls_p2p_everyone(tr::now);
		case Option::Contacts:
			return tr::lng_edit_privacy_calls_p2p_contacts(tr::now);
		case Option::Nobody:
			return tr::lng_edit_privacy_calls_p2p_nobody(tr::now);
		}
		[[fallthrough]];
	default:
		switch (rule.option) {
		case Option::Everyone:
			return rule.never.miniapps
				? tr::lng_edit_privacy_no_miniapps(tr::now)
				: tr::lng_edit_privacy_everyone(tr::now);
		case Option::Contacts:
			return rule.always.premiums
				? tr::lng_edit_privacy_contacts_and_premium(tr::now)
				: rule.always.miniapps
				? tr::lng_edit_privacy_contacts_and_miniapps(tr::now)
				: tr::lng_edit_privacy_contacts(tr::now);
		case Option::CloseFriends:
			return tr::lng_edit_privacy_close_friends(tr::now);
		case Option::Nobody:
			return rule.always.premiums
				? tr::lng_edit_privacy_premium(tr::now)
				: rule.always.miniapps
				? tr::lng_edit_privacy_miniapps(tr::now)
				: tr::lng_edit_privacy_nobody(tr::now);
		}
		Unexpected("Value in Privacy::Option.");
	}
}

rpl::producer<QString> PrivacyString(
		not_null<::Main::Session*> session,
		Privacy::Key key) {
	session->api().userPrivacy().reload(key);
	return session->api().userPrivacy().value(
		key
	) | rpl::map([=](const Privacy::Rule &value) {
		auto add = QStringList();
		if (const auto never = ExceptionUsersCount(value.never.peers)) {
			add.push_back("-" + QString::number(never));
		}
		if (const auto always = ExceptionUsersCount(value.always.peers)) {
			add.push_back("+" + QString::number(always));
		}
		if (!add.isEmpty()) {
			return PrivacyBase(key, value)
				+ " (" + add.join(", ") + ")";
		} else {
			return PrivacyBase(key, value);
		}
	});
}

void AddMessagesPrivacyButton(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	const auto session = &controller->session();
	const auto privacy = &session->api().globalPrivacy();
	auto label = rpl::combine(
		privacy->newRequirePremium(),
		privacy->newChargeStars()
	) | rpl::map([=](bool requirePremium, int chargeStars) {
		return chargeStars
			? tr::lng_edit_privacy_paid()
			: requirePremium
			? tr::lng_edit_privacy_contacts_and_premium()
			: tr::lng_edit_privacy_everyone();
	}) | rpl::flatten_latest();
	const auto &st = st::settingsButtonNoIcon;
	const auto button = AddButtonWithLabel(
		container,
		tr::lng_settings_messages_privacy(),
		rpl::duplicate(label),
		st,
		{});
	button->addClickHandler([=] {
		controller->show(Box(EditMessagesPrivacyBox, controller, QString()));
	});
	if (!session->appConfig().newRequirePremiumFree()) {
		AddPremiumStar(button, session, rpl::duplicate(label), st.padding);
	}
}

rpl::producer<int> BlockedPeersCount(not_null<::Main::Session*> session) {
	return session->api().blockedPeers().slice(
	) | rpl::map([](const Api::BlockedPeers::Slice &data) {
		return data.total;
	});
}

void ClearPaymentInfoBoxBuilder(
		not_null<Ui::GenericBox*> box,
		not_null<::Main::Session*> session) {
	box->setTitle(tr::lng_clear_payment_info_title());

	const auto checkboxPadding = style::margins(
		st::boxRowPadding.left(),
		st::boxRowPadding.left(),
		st::boxRowPadding.right(),
		st::boxRowPadding.bottom());
	const auto label = box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_clear_payment_info_sure(),
		st::boxLabel));
	const auto shipping = box->addRow(
		object_ptr<Ui::Checkbox>(
			box,
			tr::lng_clear_payment_info_shipping(tr::now),
			true,
			st::defaultBoxCheckbox),
		checkboxPadding);
	const auto payment = box->addRow(
		object_ptr<Ui::Checkbox>(
			box,
			tr::lng_clear_payment_info_payment(tr::now),
			true,
			st::defaultBoxCheckbox),
		checkboxPadding);

	using Flags = MTPpayments_ClearSavedInfo::Flags;
	const auto flags = box->lifetime().make_state<Flags>();

	box->addButton(tr::lng_clear_payment_info_clear(), [=] {
		using Flag = Flags::Enum;
		*flags = (shipping->checked() ? Flag::f_info : Flag(0))
			| (payment->checked() ? Flag::f_credentials : Flag(0));
		delete label;
		delete shipping;
		delete payment;
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_clear_payment_info_confirm(),
			st::boxLabel));
		box->clearButtons();
		box->addButton(tr::lng_clear_payment_info_clear(), [=] {
			session->api().request(MTPpayments_ClearSavedInfo(
				MTP_flags(*flags)
			)).send();
			box->closeBox();
		}, st::attentionBoxButton);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}, st::attentionBoxButton);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void SetupLocalPasscode(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		Fn<void(Type)> showOther) {
	auto has = rpl::single(rpl::empty) | rpl::then(
		controller->session().domain().local().localPasscodeChanged()
	) | rpl::map([=] {
		return controller->session().domain().local().hasLocalPasscode();
	});
	auto label = rpl::combine(
		tr::lng_settings_cloud_password_on(),
		tr::lng_settings_cloud_password_off(),
		std::move(has),
		[](const QString &on, const QString &off, bool has) {
			return has ? on : off;
		});
	AddButtonWithLabel(
		container,
		tr::lng_settings_passcode_title(),
		std::move(label),
		st::settingsButton,
		{ &st::menuIconLock }
	)->addClickHandler([=] {
		if (controller->session().domain().local().hasLocalPasscode()) {
			showOther(LocalPasscodeCheckId());
		} else {
			showOther(LocalPasscodeCreateId());
		}
	});
}

void SetupCloudPassword(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		Fn<void(Type)> showOther) {
	using namespace rpl::mappers;
	using State = Core::CloudPasswordState;

	enum class PasswordState {
		Loading,
		On,
		Off,
		Unconfirmed,
	};
	const auto session = &controller->session();
	auto passwordState = rpl::single(
		PasswordState::Loading
	) | rpl::then(session->api().cloudPassword().state(
	) | rpl::map([](const State &state) {
		return (!state.unconfirmedPattern.isEmpty())
			? PasswordState::Unconfirmed
			: state.hasPassword
			? PasswordState::On
			: PasswordState::Off;
	})) | rpl::distinct_until_changed();

	auto label = rpl::duplicate(
		passwordState
	) | rpl::map([=](PasswordState state) {
		return (state == PasswordState::Loading)
			? tr::lng_profile_loading(tr::now)
			: (state == PasswordState::On)
			? tr::lng_settings_cloud_password_on(tr::now)
			: tr::lng_settings_cloud_password_off(tr::now);
	});

	AddButtonWithLabel(
		container,
		tr::lng_settings_cloud_password_start_title(),
		std::move(label),
		st::settingsButton,
		{ &st::menuIcon2SV }
	)->addClickHandler([=, passwordState = base::duplicate(passwordState)] {
		const auto state = rpl::variable<PasswordState>(
			base::duplicate(passwordState)).current();
		if (state == PasswordState::Loading) {
			return;
		} else if (state == PasswordState::On) {
			showOther(CloudPasswordInputId());
		} else if (state == PasswordState::Off) {
			showOther(CloudPasswordStartId());
		} else if (state == PasswordState::Unconfirmed) {
			showOther(CloudPasswordEmailConfirmId());
		}
	});

	const auto reloadOnActivation = [=](Qt::ApplicationState state) {
		if (/*label->toggled() && */state == Qt::ApplicationActive) {
			controller->session().api().cloudPassword().reload();
		}
	};
	QObject::connect(
		static_cast<QGuiApplication*>(QCoreApplication::instance()),
		&QGuiApplication::applicationStateChanged,
		container,
		reloadOnActivation);

	session->api().cloudPassword().reload();
}

void SetupPasskeys(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	const auto session = &controller->session();
	if (!session->passkeys().possible()) {
		return;
	}
	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	auto label = rpl::combine(
		tr::lng_profile_loading(),
		(rpl::single(rpl::empty_value())
			| rpl::then(session->passkeys().requestList())) | rpl::map([=] {
			return session->passkeys().list().size();
		})
	) | rpl::map([=](const QString &loading, int count) {
		return !session->passkeys().listKnown()
			? loading
			: count == 1
			? session->passkeys().list().front().name
			: count
			? QString::number(count)
			: tr::lng_settings_cloud_password_off(tr::now);
	});
	AddButtonWithLabel(
		wrap->entity(),
		tr::lng_settings_passkeys_title(),
		std::move(label),
		st::settingsButton,
		{ &st::menuIconPermissions }
	)->addClickHandler([=] {
		if (!session->passkeys().listKnown()) {
			return;
		}
		const auto count = session->passkeys().list().size();
		if (count == 0) {
			controller->show(Box([=](not_null<Ui::GenericBox*> box) {
				PasskeysNoneBox(box, session);
				box->boxClosing() | rpl::on_next([=] {
					if (session->passkeys().list().size()) {
						controller->showSettings(PasskeysId());
					}
				}, box->lifetime());
			}));
		} else {
			controller->showSettings(PasskeysId());
		}
	});
	wrap->toggleOn(
		(rpl::single(rpl::empty_value())
			| rpl::then(session->passkeys().requestList())) | rpl::map([=] {
			return Platform::WebAuthn::IsSupported()
				|| !session->passkeys().list().empty();
		}));
	wrap->finishAnimating();
}

void SetupLoginEmail(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		Fn<void(Type)> showOther) {
	using namespace rpl::mappers;
	using State = Core::CloudPasswordState;

	const auto session = &controller->session();
	auto passwordState = session->api().cloudPassword().state(
	) | rpl::map([](const State &state) {
		return !state.loginEmailPattern.isEmpty();
	}) | rpl::distinct_until_changed();

	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	wrap->toggleOn(rpl::duplicate(passwordState));
	wrap->finishAnimating();

	auto email = session->api().cloudPassword().state(
	) | rpl::map([](const State &state) { return state.loginEmailPattern; });
	auto text = tr::lng_settings_cloud_login_email_section_title();
	auto labelText = rpl::duplicate(email) | rpl::map([](QString email) {
		if (email.contains(' ')) {
			return tr::lng_settings_cloud_password_off(tr::now, tr::rich);
		}
		return Ui::Text::WrapEmailPattern(
			email.replace(QRegularExpression("\\*{4,}"), "****"));
	});
	const auto &st = st::settingsButtonRightLabelSpoiler;
	const auto button = AddButtonWithIcon(
		wrap->entity(),
		rpl::duplicate(text),
		st,
		{ &st::menuIconRecoveryEmail });
	CreateRightLabel(button, std::move(labelText), st, std::move(text));

	button->addClickHandler([=] {
		UrlClickHandler::Open(u"tg://settings/login_email"_q);
	});

	const auto reloadOnActivation = [=](Qt::ApplicationState state) {
		if (wrap->toggled() && state == Qt::ApplicationActive) {
			controller->session().api().cloudPassword().reload();
		}
	};
	QObject::connect(
		static_cast<QGuiApplication*>(QCoreApplication::instance()),
		&QGuiApplication::applicationStateChanged,
		container,
		reloadOnActivation);

	session->api().cloudPassword().reload();
}

void SetupBlockedList(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<> updateTrigger,
		Fn<void(Type)> showOther) {
	const auto session = &controller->session();
	auto blockedCount = rpl::combine(
		BlockedPeersCount(session),
		tr::lng_settings_no_blocked_users()
	) | rpl::map([](int count, const QString &none) {
		return count ? QString::number(count) : none;
	});
	const auto blockedPeers = AddButtonWithLabel(
		container,
		tr::lng_settings_blocked_users(),
		std::move(blockedCount),
		st::settingsButton,
		{ &st::menuIconBlock });
	blockedPeers->addClickHandler([=] {
		showOther(BlockedPeersId());
	});
	std::move(
		updateTrigger
	) | rpl::on_next([=] {
		session->api().blockedPeers().reload();
	}, blockedPeers->lifetime());
}

void SetupWebsitesList(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<> updateTrigger,
		Fn<void(Type)> showOther) {
	std::move(
		updateTrigger
	) | rpl::on_next([=] {
		controller->session().api().websites().reload();
	}, container->lifetime());

	auto count = controller->session().api().websites().totalValue();
	auto countText = rpl::duplicate(
		count
	) | rpl::filter(rpl::mappers::_1 > 0) | rpl::map([](int count) {
		return QString::number(count);
	});

	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto inner = wrap->entity();

	AddButtonWithLabel(
		inner,
		tr::lng_settings_logged_in(),
		std::move(countText),
		st::settingsButton,
		{ &st::menuIconIpAddress }
	)->addClickHandler([=] {
		showOther(WebsitesId());
	});

	wrap->toggleOn(std::move(count) | rpl::map(rpl::mappers::_1 > 0));
	wrap->finishAnimating();
}

void SetupSessionsList(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<> updateTrigger,
		Fn<void(Type)> showOther) {
	std::move(
		updateTrigger
	) | rpl::on_next([=] {
		controller->session().api().authorizations().reload();
	}, container->lifetime());

	auto count = controller->session().api().authorizations().totalValue(
	) | rpl::map([](int count) {
		return count ? QString::number(count) : QString();
	});

	AddButtonWithLabel(
		container,
		tr::lng_settings_show_sessions(),
		std::move(count),
		st::settingsButton,
		{ &st::menuIconDevices }
	)->addClickHandler([=] {
		showOther(SessionsId());
	});

	Ui::AddSkip(container);
	Ui::AddDividerText(container, tr::lng_settings_sessions_about());
}

void SetupGlobalTTLList(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<> updateTrigger,
		Fn<void(Type)> showOther) {
	const auto session = &controller->session();
	auto ttlLabel = rpl::combine(
		session->api().selfDestruct().periodDefaultHistoryTTL(),
		tr::lng_settings_ttl_after_off()
	) | rpl::map([](int ttl, const QString &none) {
		return ttl ? Ui::FormatTTL(ttl) : none;
	});
	const auto globalTTLButton = AddButtonWithLabel(
		container,
		tr::lng_settings_ttl_title(),
		std::move(ttlLabel),
		st::settingsButton,
		{ &st::menuIconTTL });
	globalTTLButton->addClickHandler([=] {
		showOther(GlobalTTLId());
	});
	std::move(
		updateTrigger
	) | rpl::on_next([=] {
		session->api().selfDestruct().reload();
	}, container->lifetime());
}

} // namespace

rpl::producer<QString> PrivacyButtonLabel(
		not_null<::Main::Session*> session,
		Privacy::Key key) {
	return PrivacyString(session, key);
}

void AddPrivacyPremiumStar(
		not_null<Ui::SettingsButton*> button,
		not_null<::Main::Session*> session,
		rpl::producer<QString> label,
		const QMargins &padding) {
	const auto badge = Ui::CreateChild<Ui::RpWidget>(button.get());
	badge->showOn(Data::AmPremiumValue(session));
	const auto sampleLeft = st::settingsColorSamplePadding.left();
	const auto badgeLeft = padding.left() + sampleLeft;

	const auto factor = style::DevicePixelRatio();
	const auto size = Size(st::settingsButtonNoIcon.style.font->ascent);
	auto starImage = QImage(
		size * factor,
		QImage::Format_ARGB32_Premultiplied);
	starImage.setDevicePixelRatio(factor);
	starImage.fill(Qt::transparent);
	{
		auto p = QPainter(&starImage);
		auto star = QSvgRenderer(
			Ui::Premium::ColorizedSvg(Ui::Premium::ButtonGradientStops()));
		star.render(&p, Rect(size));
	}

	badge->resize(starImage.size() / style::DevicePixelRatio());
	badge->paintRequest(
	) | rpl::on_next([=, star = std::move(starImage)] {
		auto p = QPainter(badge);
		p.drawImage(0, 0, star);
	}, badge->lifetime());

	rpl::combine(
		button->sizeValue(),
		std::move(label)
	) | rpl::on_next([=](const QSize &s, const QString &) {
		if (s.isNull()) {
			return;
		}
		badge->moveToLeft(
			button->fullTextWidth() + badgeLeft,
			(s.height() - badge->height()) / 2);
	}, badge->lifetime());
}

void SetupPrivacy(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<> updateTrigger) {
	Ui::AddSkip(container, st::settingsPrivacySkip);
	Ui::AddSubsectionTitle(container, tr::lng_settings_privacy_title());

	const auto session = &controller->session();

	using Key = Privacy::Key;
	const auto add = [&](
			rpl::producer<QString> label,
			Key key,
			auto controllerFactory) {
		return AddPrivacyButton(
			controller,
			container,
			std::move(label),
			{},
			key,
			controllerFactory);
	};
	add(
		tr::lng_settings_phone_number_privacy(),
		Key::PhoneNumber,
		[=] { return std::make_unique<PhoneNumberPrivacyController>(
			controller); });
	add(
		tr::lng_settings_last_seen(),
		Key::LastSeen,
		[=] { return std::make_unique<LastSeenPrivacyController>(
			session); });
	add(
		tr::lng_settings_profile_photo_privacy(),
		Key::ProfilePhoto,
		[] { return std::make_unique<ProfilePhotoPrivacyController>(); });
	add(
		tr::lng_settings_forwards_privacy(),
		Key::Forwards,
		[=] { return std::make_unique<ForwardsPrivacyController>(
			controller); });
	add(
		tr::lng_settings_calls(),
		Key::Calls,
		[] { return std::make_unique<CallsPrivacyController>(); });
	{
		const auto &phrase = tr::lng_settings_voices_privacy;
		const auto &st = st::settingsButtonNoIcon;
		auto callback = [=] {
			return std::make_unique<VoicesPrivacyController>(session);
		};
		const auto voices = add(phrase(), Key::Voices, std::move(callback));
		AddPremiumStar(voices, session, phrase(), st.padding);
	}
	AddMessagesPrivacyButton(controller, container);
	add(
		tr::lng_settings_birthday_privacy(),
		Key::Birthday,
		[] { return std::make_unique<BirthdayPrivacyController>(); });
	add(
		tr::lng_settings_gifts_privacy(),
		Key::GiftsAutoSave,
		[=] { return std::make_unique<GiftsAutoSavePrivacyController>(); });
	add(
		tr::lng_settings_bio_privacy(),
		Key::About,
		[] { return std::make_unique<AboutPrivacyController>(); });
	add(
		tr::lng_settings_saved_music_privacy(),
		Key::SavedMusic,
		[] { return std::make_unique<SavedMusicPrivacyController>(); });
	add(
		tr::lng_settings_groups_invite(),
		Key::Invites,
		[] { return std::make_unique<GroupsInvitePrivacyController>(); });

	session->api().userPrivacy().reload(
		Api::UserPrivacy::Key::AddedByPhone);

	Ui::AddSkip(container, st::settingsPrivacySecurityPadding);
	Ui::AddDivider(container);
}

void SetupTopPeers(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, tr::lng_settings_top_peers_title());

	const auto session = &controller->session();

	container->add(object_ptr<Button>(
		container,
		tr::lng_settings_top_peers_suggest(),
		st::settingsButtonNoIcon
	))->toggleOn(rpl::single(
		rpl::empty
	) | rpl::then(
		session->topPeers().updates()
	) | rpl::map([=] {
		return !session->topPeers().disabled();
	}))->toggledChanges(
	) | rpl::filter([=](bool enabled) {
		return enabled == session->topPeers().disabled();
	}) | rpl::on_next([=](bool enabled) {
		session->topPeers().toggleDisabled(!enabled);
	}, container->lifetime());

	Ui::AddSkip(container);
	Ui::AddDividerText(container, tr::lng_settings_top_peers_about());
}

void SetupSelfDestruction(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<> updateTrigger) {
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, tr::lng_settings_destroy_title());

	const auto session = &controller->session();

	std::move(
		updateTrigger
	) | rpl::on_next([=] {
		session->api().selfDestruct().reload();
	}, container->lifetime());
	const auto label = [&] {
		return session->api().selfDestruct().daysAccountTTL(
		) | rpl::map(SelfDestructionBox::DaysLabel);
	};

	AddButtonWithLabel(
		container,
		tr::lng_settings_destroy_if(),
		label(),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		controller->show(Box<SelfDestructionBox>(
			session,
			SelfDestructionBox::Type::Account,
			session->api().selfDestruct().daysAccountTTL()));
	});

	Ui::AddSkip(container);
}

object_ptr<Ui::BoxContent> ClearPaymentInfoBox(not_null<::Main::Session*> session) {
	return Box(ClearPaymentInfoBoxBuilder, session);
}

void SetupBotsAndWebsites(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		HighlightRegistry *highlights) {
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, tr::lng_settings_security_bots());

	const auto session = &controller->session();
	const auto button = container->add(object_ptr<Button>(
		container,
		tr::lng_settings_clear_payment_info(),
		st::settingsButtonNoIcon
	));
	button->addClickHandler([=] {
		controller->show(ClearPaymentInfoBox(session));
	});

	if (highlights) {
		highlights->push_back({ u"privacy/bots_payment"_q, { button } });
	}

	Ui::AddSkip(container);
	Ui::AddDivider(container);
}

void SetupConfirmationExtensions(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	if (Core::App().settings().noWarningExtensions().empty()
		&& Core::App().settings().ipRevealWarning()) {
		return;
	}

	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, tr::lng_settings_file_confirmations());

	container->add(object_ptr<Button>(
		container,
		tr::lng_settings_edit_extensions(),
		st::settingsButtonNoIcon
	))->addClickHandler([=] {
		controller->show(Box(OpenFileConfirmationsBox));
	});

	Ui::AddSkip(container);
	Ui::AddDividerText(container, tr::lng_settings_edit_extensions_about());
}

void SetupSecurity(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<> updateTrigger,
		Fn<void(Type)> showOther) {
	Ui::AddSkip(container, st::settingsPrivacySkip);
	Ui::AddSubsectionTitle(container, tr::lng_settings_security());

	SetupCloudPassword(controller, container, showOther);
	SetupGlobalTTLList(
		controller,
		container,
		rpl::duplicate(updateTrigger),
		showOther);
	SetupLocalPasscode(controller, container, showOther);
	SetupPasskeys(controller, container);
	SetupLoginEmail(controller, container, showOther);
	SetupBlockedList(
		controller,
		container,
		rpl::duplicate(updateTrigger),
		showOther);
	SetupWebsitesList(
		controller,
		container,
		rpl::duplicate(updateTrigger),
		showOther);
	SetupSessionsList(
		controller,
		container,
		rpl::duplicate(updateTrigger),
		showOther);
}

void SetupSensitiveContent(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<> updateTrigger,
		HighlightRegistry *highlights) {
	using namespace rpl::mappers;

	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto inner = wrap->entity();

	Ui::AddSkip(inner);
	Ui::AddSubsectionTitle(inner, tr::lng_settings_sensitive_title());

	const auto show = controller->uiShow();
	const auto session = &controller->session();
	const auto disable = inner->lifetime().make_state<rpl::event_stream<>>();

	std::move(
		updateTrigger
	) | rpl::on_next([=] {
		session->api().sensitiveContent().reload();
	}, container->lifetime());
	const auto button = inner->add(object_ptr<Button>(
		inner,
		tr::lng_settings_sensitive_disable_filtering(),
		st::settingsButtonNoIcon));
	button->toggleOn(rpl::merge(
		session->api().sensitiveContent().enabled(),
		disable->events() | rpl::map_to(false)
	))->toggledChanges(
	) | rpl::filter([=](bool toggled) {
		return toggled != session->api().sensitiveContent().enabledCurrent();
	}) | rpl::on_next([=](bool toggled) {
		if (toggled && session->appConfig().ageVerifyNeeded()) {
			disable->fire({});

			HistoryView::ShowAgeVerificationRequired(
				show,
				session,
				[] {});
		} else {
			session->api().sensitiveContent().update(toggled);
		}
	}, container->lifetime());

	if (highlights) {
		highlights->push_back({ u"chat/show-18-content"_q, { button } });
	}

	Ui::AddSkip(inner);
	Ui::AddDividerText(inner, tr::lng_settings_sensitive_about());

	wrap->toggleOn(session->api().sensitiveContent().canChange());
}

int ExceptionUsersCount(const std::vector<not_null<PeerData*>> &exceptions) {
	const auto add = [](int already, not_null<PeerData*> peer) {
		if (const auto chat = peer->asChat()) {
			return already + chat->count;
		} else if (const auto channel = peer->asChannel()) {
			return already + channel->membersCount();
		}
		return already + 1;
	};
	return ranges::accumulate(exceptions, 0, add);
}

bool CheckEditCloudPassword(not_null<::Main::Session*> session) {
	const auto current = session->api().cloudPassword().stateCurrent();
	Assert(current.has_value());

	return !current->outdatedClient;
}

object_ptr<Ui::BoxContent> EditCloudPasswordBox(not_null<::Main::Session*> session) {
	const auto current = session->api().cloudPassword().stateCurrent();
	Assert(current.has_value());

	auto result = Box<PasscodeBox>(
		session,
		PasscodeBox::CloudFields::From(*current));
	const auto box = result.data();

	rpl::merge(
		box->newPasswordSet() | rpl::to_empty,
		box->passwordReloadNeeded()
	) | rpl::on_next([=] {
		session->api().cloudPassword().reload();
	}, box->lifetime());

	box->clearUnconfirmedPassword(
	) | rpl::on_next([=] {
		session->api().cloudPassword().clearUnconfirmedPassword();
	}, box->lifetime());

	return result;
}

void OpenFileConfirmationsBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(tr::lng_settings_file_confirmations());

	const auto settings = &Core::App().settings();
	const auto &list = settings->noWarningExtensions();
	const auto text = QStringList(begin(list), end(list)).join(' ');
	const auto layout = box->verticalLayout();
	const auto extensions = box->addRow(
		object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			Ui::InputField::Mode::MultiLine,
			tr::lng_settings_edit_extensions(),
			TextWithTags{ text }),
		st::boxRowPadding + QMargins(0, 0, 0, st::settingsPrivacySkip));
	Ui::AddDividerText(layout, tr::lng_settings_edit_extensions_about());
	Ui::AddSkip(layout);
	const auto ip = layout->add(object_ptr<Ui::SettingsButton>(
		box,
		tr::lng_settings_edit_ip_confirm(),
		st::settingsButtonNoIcon
	))->toggleOn(rpl::single(settings->ipRevealWarning()));
	Ui::AddSkip(layout);
	Ui::AddDividerText(layout, tr::lng_settings_edit_ip_confirm_about());

	box->setFocusCallback([=] {
		extensions->setFocusFast();
	});

	box->addButton(tr::lng_settings_save(), [=] {
		const auto extensionsList = extensions->getLastText()
			.mid(0, 10240)
			.split(' ', Qt::SkipEmptyParts)
			.mid(0, 1024);
		auto extensionsSet = base::flat_set<QString>(
			extensionsList.begin(),
			extensionsList.end());
		const auto ipRevealWarning = ip->toggled();
		if (extensionsSet != settings->noWarningExtensions()
			|| ipRevealWarning != settings->ipRevealWarning()) {
			settings->setNoWarningExtensions(std::move(extensionsSet));
			settings->setIpRevealWarning(ipRevealWarning);
			Core::App().saveSettingsDelayed();
		}
		box->closeBox();

	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void RemoveCloudPassword(not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	const auto current = session->api().cloudPassword().stateCurrent();
	Assert(current.has_value());

	if (!current->hasPassword) {
		session->api().cloudPassword().clearUnconfirmedPassword();
		return;
	}
	auto fields = PasscodeBox::CloudFields::From(*current);
	fields.turningOff = true;
	auto box = Box<PasscodeBox>(session, fields);

	rpl::merge(
		box->newPasswordSet() | rpl::to_empty,
		box->passwordReloadNeeded()
	) | rpl::on_next([=] {
		session->api().cloudPassword().reload();
	}, box->lifetime());

	box->clearUnconfirmedPassword(
	) | rpl::on_next([=] {
		session->api().cloudPassword().clearUnconfirmedPassword();
	}, box->lifetime());

	controller->show(std::move(box));
}

object_ptr<Ui::BoxContent> CloudPasswordAppOutdatedBox() {
	const auto callback = [=](Fn<void()> &&close) {
		Core::UpdateApplication();
		close();
	};
	return Ui::MakeConfirmBox({
		.text = tr::lng_passport_app_out_of_date(),
		.confirmed = callback,
		.confirmText = tr::lng_menu_update(),
	});
}

not_null<Ui::SettingsButton*> AddPrivacyButton(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> label,
		IconDescriptor &&descriptor,
		Privacy::Key key,
		Fn<std::unique_ptr<EditPrivacyController>()> controllerFactory,
		const style::SettingsButton *stOverride) {
	const auto shower = Ui::CreateChild<rpl::lifetime>(container.get());
	const auto session = &controller->session();
	const auto button = AddButtonWithLabel(
		container,
		std::move(label),
		PrivacyString(session, key),
		stOverride ? *stOverride : st::settingsButtonNoIcon,
		std::move(descriptor));
	button->addClickHandler([=] {
		*shower = session->api().userPrivacy().value(
			key
		) | rpl::take(
			1
		) | rpl::on_next(crl::guard(controller, [=](
				const Privacy::Rule &value) {
			controller->show(Box<EditPrivacyBox>(
				controller,
				controllerFactory(),
				value));
		}));
	});
	return button;
}

void SetupArchiveAndMute(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		HighlightRegistry *highlights) {
	using namespace rpl::mappers;

	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto inner = wrap->entity();

	Ui::AddSkip(inner);
	Ui::AddSubsectionTitle(inner, tr::lng_settings_new_unknown());

	const auto session = &controller->session();

	const auto privacy = &session->api().globalPrivacy();
	privacy->reload();
	const auto button = inner->add(object_ptr<Button>(
		inner,
		tr::lng_settings_auto_archive(),
		st::settingsButtonNoIcon
	));
	button->toggleOn(
		privacy->archiveAndMute()
	)->toggledChanges(
	) | rpl::filter([=](bool toggled) {
		return toggled != privacy->archiveAndMuteCurrent();
	}) | rpl::on_next([=](bool toggled) {
		privacy->updateArchiveAndMute(toggled);
	}, container->lifetime());

	if (highlights) {
		highlights->push_back({ u"privacy/archive_and_mute"_q, { button } });
	}

	Ui::AddSkip(inner);
	Ui::AddDividerText(inner, tr::lng_settings_auto_archive_about());

	auto shown = rpl::single(
		false
	) | rpl::then(session->api().globalPrivacy().showArchiveAndMute(
	) | rpl::filter(_1) | rpl::take(1));
	auto premium = Data::AmPremiumValue(&controller->session());

	using namespace rpl::mappers;
	wrap->toggleOn(rpl::combine(
		std::move(shown),
		std::move(premium),
		_1 || _2));
}

namespace {

using namespace Builder;

void BuildSecuritySection(
		SectionBuilder &builder,
		rpl::producer<> updateTrigger) {
	const auto controller = builder.controller();
	const auto showOther = builder.showOther();
	const auto session = builder.session();

	builder.addSkip(st::settingsPrivacySkip);
	builder.addSubsectionTitle({
		.id = u"security/section"_q,
		.title = tr::lng_settings_security(),
		.keywords = { u"security"_q, u"password"_q, u"passcode"_q },
	});

	using State = Core::CloudPasswordState;
	enum class PasswordState {
		Loading,
		On,
		Off,
		Unconfirmed,
	};
	auto passwordState = rpl::single(
		PasswordState::Loading
	) | rpl::then(session->api().cloudPassword().state(
	) | rpl::map([](const State &state) {
		return (!state.unconfirmedPattern.isEmpty())
			? PasswordState::Unconfirmed
			: state.hasPassword
			? PasswordState::On
			: PasswordState::Off;
	})) | rpl::distinct_until_changed();

	auto cloudPasswordLabel = rpl::duplicate(
		passwordState
	) | rpl::map([=](PasswordState state) {
		return (state == PasswordState::Loading)
			? tr::lng_profile_loading(tr::now)
			: (state == PasswordState::On)
			? tr::lng_settings_cloud_password_on(tr::now)
			: tr::lng_settings_cloud_password_off(tr::now);
	});

	builder.addButton({
		.id = u"security/cloud_password"_q,
		.title = tr::lng_settings_cloud_password_start_title(),
		.icon = { &st::menuIcon2SV },
		.label = std::move(cloudPasswordLabel),
		.onClick = [=, passwordState = base::duplicate(passwordState)] {
			const auto state = rpl::variable<PasswordState>(
				base::duplicate(passwordState)).current();
			if (state == PasswordState::Loading) {
				return;
			} else if (state == PasswordState::On) {
				showOther(CloudPasswordInputId());
			} else if (state == PasswordState::Off) {
				showOther(CloudPasswordStartId());
			} else if (state == PasswordState::Unconfirmed) {
				showOther(CloudPasswordEmailConfirmId());
			}
		},
		.keywords = { u"password"_q, u"2fa"_q, u"two-factor"_q },
	});

	session->api().cloudPassword().reload();

	auto ttlLabel = rpl::combine(
		session->api().selfDestruct().periodDefaultHistoryTTL(),
		tr::lng_settings_ttl_after_off()
	) | rpl::map([](int ttl, const QString &none) {
		return ttl ? Ui::FormatTTL(ttl) : none;
	});

	builder.addButton({
		.id = u"security/ttl"_q,
		.title = tr::lng_settings_ttl_title(),
		.icon = { &st::menuIconTTL },
		.label = std::move(ttlLabel),
		.onClick = [showOther] {
			showOther(GlobalTTLId());
		},
		.keywords = { u"ttl"_q, u"auto-delete"_q, u"timer"_q },
	});

	builder.add([session, updateTrigger = rpl::duplicate(updateTrigger)](const WidgetContext &ctx) mutable {
		std::move(updateTrigger) | rpl::on_next([=] {
			session->api().selfDestruct().reload();
		}, ctx.container->lifetime());
		return SectionBuilder::WidgetToAdd{};
	});

	auto passcodeHas = rpl::single(rpl::empty) | rpl::then(
		session->domain().local().localPasscodeChanged()
	) | rpl::map([=] {
		return session->domain().local().hasLocalPasscode();
	});
	auto passcodeLabel = rpl::combine(
		tr::lng_settings_cloud_password_on(),
		tr::lng_settings_cloud_password_off(),
		rpl::duplicate(passcodeHas)
	) | rpl::map([](const QString &on, const QString &off, bool has) {
		return has ? on : off;
	});

	builder.addButton({
		.id = u"security/passcode"_q,
		.title = tr::lng_settings_passcode_title(),
		.icon = { &st::menuIconLock },
		.label = std::move(passcodeLabel),
		.onClick = [=, passcodeHas = std::move(passcodeHas)]() mutable {
			if (rpl::variable<bool>(std::move(passcodeHas)).current()) {
				showOther(LocalPasscodeCheckId());
			} else {
				showOther(LocalPasscodeCreateId());
			}
		},
		.keywords = { u"passcode"_q, u"lock"_q, u"pin"_q },
	});

	if (session->passkeys().possible()) {
		auto passkeysLabel = rpl::combine(
			tr::lng_profile_loading(),
			(rpl::single(rpl::empty_value())
				| rpl::then(session->passkeys().requestList())) | rpl::map([=] {
				return session->passkeys().list().size();
			})
		) | rpl::map([=](const QString &loading, int count) {
			return !session->passkeys().listKnown()
				? loading
				: count == 1
				? session->passkeys().list().front().name
				: count
				? QString::number(count)
				: tr::lng_settings_cloud_password_off(tr::now);
		});

		auto passkeysShown = (rpl::single(rpl::empty_value())
			| rpl::then(session->passkeys().requestList())) | rpl::map([=] {
			return Platform::WebAuthn::IsSupported()
				|| !session->passkeys().list().empty();
		});

		builder.addButton({
			.id = u"security/passkeys"_q,
			.title = tr::lng_settings_passkeys_title(),
			.icon = { &st::menuIconPermissions },
			.label = std::move(passkeysLabel),
			.onClick = [=] {
				if (!session->passkeys().listKnown()) {
					return;
				}
				const auto count = session->passkeys().list().size();
				if (count == 0) {
					controller->show(Box([=](not_null<Ui::GenericBox*> box) {
						PasskeysNoneBox(box, session);
						box->boxClosing() | rpl::on_next([=] {
							if (session->passkeys().list().size()) {
								controller->showSettings(PasskeysId());
							}
						}, box->lifetime());
					}));
				} else {
					controller->showSettings(PasskeysId());
				}
			},
			.keywords = { u"passkeys"_q, u"biometric"_q },
			.shown = std::move(passkeysShown),
		});
	}

	auto blockedCount = rpl::combine(
		session->api().blockedPeers().slice(
		) | rpl::map([](const Api::BlockedPeers::Slice &data) {
			return data.total;
		}),
		tr::lng_settings_no_blocked_users()
	) | rpl::map([](int count, const QString &none) {
		return count ? QString::number(count) : none;
	});

	builder.addButton({
		.id = u"security/blocked"_q,
		.title = tr::lng_settings_blocked_users(),
		.icon = { &st::menuIconBlock },
		.label = std::move(blockedCount),
		.onClick = [=] {
			showOther(BlockedPeersId());
		},
		.keywords = { u"blocked"_q, u"ban"_q },
	});

	builder.add([session, updateTrigger = rpl::duplicate(updateTrigger)](const WidgetContext &ctx) mutable {
		std::move(updateTrigger) | rpl::on_next([=] {
			session->api().blockedPeers().reload();
		}, ctx.container->lifetime());
		return SectionBuilder::WidgetToAdd{};
	});

	auto websitesCount = session->api().websites().totalValue();
	auto websitesShown = rpl::duplicate(websitesCount) | rpl::map(
		rpl::mappers::_1 > 0);
	auto websitesLabel = rpl::duplicate(
		websitesCount
	) | rpl::filter(rpl::mappers::_1 > 0) | rpl::map([](int count) {
		return QString::number(count);
	});

	builder.addButton({
		.id = u"security/websites"_q,
		.title = tr::lng_settings_logged_in(),
		.icon = { &st::menuIconIpAddress },
		.label = std::move(websitesLabel),
		.onClick = [=] {
			showOther(WebsitesId());
		},
		.keywords = { u"websites"_q, u"bots"_q, u"logged"_q },
		.shown = std::move(websitesShown),
	});

	builder.add([session, updateTrigger = rpl::duplicate(updateTrigger)](const WidgetContext &ctx) mutable {
		std::move(updateTrigger) | rpl::on_next([=] {
			session->api().websites().reload();
		}, ctx.container->lifetime());
		return SectionBuilder::WidgetToAdd{};
	});

	auto sessionsCount = session->api().authorizations().totalValue(
	) | rpl::map([](int count) {
		return count ? QString::number(count) : QString();
	});

	builder.addButton({
		.id = u"security/sessions"_q,
		.title = tr::lng_settings_show_sessions(),
		.icon = { &st::menuIconDevices },
		.label = std::move(sessionsCount),
		.onClick = [=] {
			showOther(SessionsId());
		},
		.keywords = { u"sessions"_q, u"devices"_q, u"active"_q },
	});

	builder.add([session, updateTrigger = std::move(updateTrigger)](const WidgetContext &ctx) mutable {
		std::move(updateTrigger) | rpl::on_next([=] {
			session->api().authorizations().reload();
		}, ctx.container->lifetime());
		return SectionBuilder::WidgetToAdd{};
	});

	builder.addSkip();
	builder.addDividerText(tr::lng_settings_sessions_about());
}

void BuildPrivacySection(SectionBuilder &builder) {
	const auto controller = builder.controller();
	const auto session = builder.session();

	builder.addSkip(st::settingsPrivacySkip);
	builder.addSubsectionTitle({
		.id = u"privacy/section"_q,
		.title = tr::lng_settings_privacy_title(),
		.keywords = { u"privacy"_q, u"visibility"_q },
	});

	using Key = Privacy::Key;

	builder.addPrivacyButton({
		.id = u"privacy/phone_number"_q,
		.title = tr::lng_settings_phone_number_privacy(),
		.key = Key::PhoneNumber,
		.controllerFactory = [=] {
			return std::make_unique<PhoneNumberPrivacyController>(controller);
		},
		.keywords = { u"phone"_q, u"number"_q },
	});

	builder.addPrivacyButton({
		.id = u"privacy/last_seen"_q,
		.title = tr::lng_settings_last_seen(),
		.key = Key::LastSeen,
		.controllerFactory = [=] {
			return std::make_unique<LastSeenPrivacyController>(session);
		},
		.keywords = { u"last seen"_q, u"online"_q },
	});

	builder.addPrivacyButton({
		.id = u"privacy/profile_photo"_q,
		.title = tr::lng_settings_profile_photo_privacy(),
		.key = Key::ProfilePhoto,
		.controllerFactory = [] {
			return std::make_unique<ProfilePhotoPrivacyController>();
		},
		.keywords = { u"photo"_q, u"avatar"_q },
	});

	builder.addPrivacyButton({
		.id = u"privacy/forwards"_q,
		.title = tr::lng_settings_forwards_privacy(),
		.key = Key::Forwards,
		.controllerFactory = [=] {
			return std::make_unique<ForwardsPrivacyController>(controller);
		},
		.keywords = { u"forwards"_q, u"link"_q },
	});

	builder.addPrivacyButton({
		.id = u"privacy/calls"_q,
		.title = tr::lng_settings_calls(),
		.key = Key::Calls,
		.controllerFactory = [] {
			return std::make_unique<CallsPrivacyController>();
		},
		.keywords = { u"calls"_q, u"voice"_q },
	});

	builder.addPrivacyButton({
		.id = u"privacy/voices"_q,
		.title = tr::lng_settings_voices_privacy(),
		.key = Key::Voices,
		.controllerFactory = [=] {
			return std::make_unique<VoicesPrivacyController>(session);
		},
		.premium = true,
		.keywords = { u"voice"_q, u"messages"_q },
	});

	const auto privacy = &session->api().globalPrivacy();
	auto messagesLabel = rpl::combine(
		privacy->newRequirePremium(),
		privacy->newChargeStars()
	) | rpl::map([=](bool requirePremium, int chargeStars) {
		return chargeStars
			? tr::lng_edit_privacy_paid()
			: requirePremium
			? tr::lng_edit_privacy_contacts_and_premium()
			: tr::lng_edit_privacy_everyone();
	}) | rpl::flatten_latest();

	const auto messagesPremium = !session->appConfig().newRequirePremiumFree();
	const auto messagesButton = builder.addButton({
		.id = u"privacy/messages"_q,
		.title = tr::lng_settings_messages_privacy(),
		.st = &st::settingsButtonNoIcon,
		.label = rpl::duplicate(messagesLabel),
		.onClick = [=] {
			controller->show(Box(EditMessagesPrivacyBox, controller, QString()));
		},
		.keywords = { u"messages"_q, u"new"_q, u"unknown"_q },
	});
	if (messagesPremium && messagesButton) {
		AddPrivacyPremiumStar(
			messagesButton,
			session,
			std::move(messagesLabel),
			st::settingsButtonNoIcon.padding);
	}

	builder.addPrivacyButton({
		.id = u"privacy/birthday"_q,
		.title = tr::lng_settings_birthday_privacy(),
		.key = Key::Birthday,
		.controllerFactory = [] {
			return std::make_unique<BirthdayPrivacyController>();
		},
		.keywords = { u"birthday"_q, u"age"_q },
	});

	builder.addPrivacyButton({
		.id = u"privacy/gifts"_q,
		.title = tr::lng_settings_gifts_privacy(),
		.key = Key::GiftsAutoSave,
		.controllerFactory = [] {
			return std::make_unique<GiftsAutoSavePrivacyController>();
		},
		.keywords = { u"gifts"_q },
	});

	builder.addPrivacyButton({
		.id = u"privacy/bio"_q,
		.title = tr::lng_settings_bio_privacy(),
		.key = Key::About,
		.controllerFactory = [] {
			return std::make_unique<AboutPrivacyController>();
		},
		.keywords = { u"bio"_q, u"about"_q },
	});

	builder.addPrivacyButton({
		.id = u"privacy/saved_music"_q,
		.title = tr::lng_settings_saved_music_privacy(),
		.key = Key::SavedMusic,
		.controllerFactory = [] {
			return std::make_unique<SavedMusicPrivacyController>();
		},
		.keywords = { u"music"_q, u"saved"_q },
	});

	builder.addPrivacyButton({
		.id = u"privacy/groups"_q,
		.title = tr::lng_settings_groups_invite(),
		.key = Key::Invites,
		.controllerFactory = [] {
			return std::make_unique<GroupsInvitePrivacyController>();
		},
		.keywords = { u"groups"_q, u"invite"_q },
	});

	session->api().userPrivacy().reload(Privacy::Key::AddedByPhone);

	builder.addSkip(st::settingsPrivacySecurityPadding);
	builder.addDivider();
}

void BuildArchiveAndMuteSection(SectionBuilder &builder) {
	const auto session = builder.session();
	const auto privacy = &session->api().globalPrivacy();

	privacy->reload();

	auto shown = rpl::single(
		false
	) | rpl::then(privacy->showArchiveAndMute(
	) | rpl::filter(rpl::mappers::_1) | rpl::take(1));
	auto premium = Data::AmPremiumValue(session);

	builder.scope([&] {
		builder.addSkip();
		builder.addSubsectionTitle({
			.id = u"privacy/new_unknown"_q,
			.title = tr::lng_settings_new_unknown(),
			.keywords = { u"unknown"_q, u"archive"_q, u"mute"_q },
		});

		const auto toggle = builder.addButton({
			.id = u"privacy/archive_and_mute"_q,
			.title = tr::lng_settings_auto_archive(),
			.st = &st::settingsButtonNoIcon,
			.toggled = privacy->archiveAndMute(),
			.keywords = { u"archive"_q, u"mute"_q, u"unknown"_q },
		});

		if (toggle) {
			toggle->toggledChanges(
			) | rpl::filter([=](bool toggled) {
				return toggled != privacy->archiveAndMuteCurrent();
			}) | rpl::on_next([=](bool toggled) {
				privacy->updateArchiveAndMute(toggled);
			}, toggle->lifetime());
		}

		builder.addSkip();
		builder.addDividerText(tr::lng_settings_auto_archive_about());
	}, rpl::combine(
		std::move(shown),
		std::move(premium),
		rpl::mappers::_1 || rpl::mappers::_2));
}

void BuildBotsAndWebsitesSection(SectionBuilder &builder) {
	const auto controller = builder.controller();
	const auto session = builder.session();

	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"privacy/bots"_q,
		.title = tr::lng_settings_security_bots(),
		.keywords = { u"bots"_q, u"payment"_q, u"websites"_q },
	});

	builder.addButton({
		.id = u"privacy/bots_payment"_q,
		.title = tr::lng_settings_clear_payment_info(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] {
			controller->show(ClearPaymentInfoBox(session));
		},
		.keywords = { u"payment"_q, u"bots"_q, u"clear"_q },
	});

	builder.addSkip();
	builder.addDivider();
}

void BuildTopPeersSection(SectionBuilder &builder) {
	const auto session = builder.session();

	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"privacy/top_peers"_q,
		.title = tr::lng_settings_top_peers_title(),
		.keywords = { u"suggest"_q, u"contacts"_q, u"frequent"_q },
	});

	const auto toggle = builder.addButton({
		.id = u"privacy/top_peers_toggle"_q,
		.title = tr::lng_settings_top_peers_suggest(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(
			rpl::empty
		) | rpl::then(
			session->topPeers().updates()
		) | rpl::map([=] {
			return !session->topPeers().disabled();
		}),
		.keywords = { u"suggest"_q, u"contacts"_q },
	});

	if (toggle) {
		toggle->toggledChanges(
		) | rpl::filter([=](bool enabled) {
			return enabled == session->topPeers().disabled();
		}) | rpl::on_next([=](bool enabled) {
			session->topPeers().toggleDisabled(!enabled);
		}, toggle->lifetime());
	}

	builder.addSkip();
	builder.addDividerText(tr::lng_settings_top_peers_about());
}

void BuildSelfDestructionSection(
		SectionBuilder &builder,
		rpl::producer<> updateTrigger) {
	const auto controller = builder.controller();
	const auto session = builder.session();

	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"privacy/self_destruct"_q,
		.title = tr::lng_settings_destroy_title(),
		.keywords = { u"delete"_q, u"destroy"_q, u"inactive"_q, u"account"_q },
	});

	builder.add([session, updateTrigger = std::move(updateTrigger)](const WidgetContext &ctx) mutable {
		std::move(updateTrigger) | rpl::on_next([=] {
			session->api().selfDestruct().reload();
		}, ctx.container->lifetime());
		return SectionBuilder::WidgetToAdd{};
	});

	auto label = session->api().selfDestruct().daysAccountTTL(
	) | rpl::map(SelfDestructionBox::DaysLabel);

	builder.addButton({
		.id = u"privacy/self_destruct_button"_q,
		.title = tr::lng_settings_destroy_if(),
		.st = &st::settingsButtonNoIcon,
		.label = std::move(label),
		.onClick = [=] {
			controller->show(Box<SelfDestructionBox>(
				session,
				SelfDestructionBox::Type::Account,
				session->api().selfDestruct().daysAccountTTL()));
		},
		.keywords = { u"delete"_q, u"destroy"_q, u"inactive"_q },
	});

	builder.addSkip();
}

void BuildConfirmationExtensions(SectionBuilder &builder) {
	const auto controller = builder.controller();
	const auto hasExtensions = !Core::App().settings().noWarningExtensions().empty()
		|| !Core::App().settings().ipRevealWarning();

	if (!hasExtensions) {
		return;
	}

	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"privacy/file_confirmations"_q,
		.title = tr::lng_settings_file_confirmations(),
		.keywords = { u"extensions"_q, u"files"_q, u"confirmations"_q },
	});

	builder.addButton({
		.id = u"privacy/file_confirmations_button"_q,
		.title = tr::lng_settings_edit_extensions(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] {
			controller->show(Box(OpenFileConfirmationsBox));
		},
		.keywords = { u"extensions"_q, u"files"_q, u"confirmations"_q },
	});

	builder.addSkip();
	builder.addDividerText(tr::lng_settings_edit_extensions_about());
}

void BuildPrivacySecuritySectionContent(SectionBuilder &builder) {
	auto updateOnTick = rpl::single(
	) | rpl::then(base::timer_each(kUpdateTimeout));
	const auto trigger = [&] {
		return rpl::duplicate(updateOnTick);
	};

	BuildSecuritySection(builder, trigger());
	BuildPrivacySection(builder);
	BuildArchiveAndMuteSection(builder);
	BuildBotsAndWebsitesSection(builder);
	BuildConfirmationExtensions(builder);
	BuildTopPeersSection(builder);
	BuildSelfDestructionSection(builder, trigger());
}

class PrivacySecurity : public Section<PrivacySecurity> {
public:
	PrivacySecurity(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();

};

const auto kMeta = BuildHelper({
	.id = PrivacySecurity::Id(),
	.parentId = MainId(),
	.title = &tr::lng_settings_section_privacy,
	.icon = &st::menuIconLock,
}, [](SectionBuilder &builder) {
	BuildPrivacySecuritySectionContent(builder);
});

const SectionBuildMethod kPrivacySecuritySection = kMeta.build;

PrivacySecurity::PrivacySecurity(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
	[[maybe_unused]] auto preload = base::SystemUnlockStatus();
}

rpl::producer<QString> PrivacySecurity::title() {
	return tr::lng_settings_section_privacy();
}

void PrivacySecurity::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kPrivacySecuritySection);
	Ui::ResizeFitChild(this, content);
}

} // namespace

Type PrivacySecurityId() {
	return PrivacySecurity::Id();
}

} // namespace Settings
