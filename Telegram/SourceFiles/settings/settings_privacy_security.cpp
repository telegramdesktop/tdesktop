/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_privacy_security.h"

#include "api/api_authorizations.h"
#include "api/api_cloud_password.h"
#include "api/api_self_destruct.h"
#include "api/api_sensitive_content.h"
#include "api/api_global_privacy.h"
#include "api/api_websites.h"
#include "settings/cloud_password/settings_cloud_password_email_confirm.h"
#include "settings/cloud_password/settings_cloud_password_input.h"
#include "settings/cloud_password/settings_cloud_password_start.h"
#include "settings/settings_blocked_peers.h"
#include "settings/settings_global_ttl.h"
#include "settings/settings_local_passcode.h"
#include "settings/settings_premium.h" // Settings::ShowPremium.
#include "settings/settings_privacy_controllers.h"
#include "settings/settings_websites.h"
#include "base/timer_rpl.h"
#include "boxes/passcode_box.h"
#include "boxes/sessions_box.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/self_destruction_box.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "ui/chat/chat_style.h"
#include "ui/effects/premium_top_bar.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/checkbox.h"
#include "ui/vertical_list.h"
#include "ui/rect.h"
#include "calls/calls_instance.h"
#include "core/update_checker.h"
#include "lang/lang_keys.h"
#include "data/data_session.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_peer_values.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "storage/storage_domain.h"
#include "window/window_session_controller.h"
#include "apiwrap.h"
#include "styles/style_settings.h"
#include "styles/style_menu_icons.h"
#include "styles/style_layers.h"

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
		auto star = QSvgRenderer(Ui::Premium::ColorizedSvg());
		star.render(&p, Rect(size));
	}
	return image;
}

void AddPremiumStar(
		not_null<Ui::SettingsButton*> button,
		not_null<Main::Session*> session,
		rpl::producer<QString> label,
		const QMargins &padding) {
	const auto badge = Ui::CreateChild<Ui::RpWidget>(button.get());
	badge->showOn(Data::AmPremiumValue(session));
	const auto sampleLeft = st::settingsColorSamplePadding.left();
	const auto badgeLeft = padding.left() + sampleLeft;

	auto star = PremiumStar();
	badge->resize(star.size() / style::DevicePixelRatio());
	badge->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(badge);
		p.drawImage(0, 0, star);
	}, badge->lifetime());

	rpl::combine(
		button->sizeValue(),
		std::move(label)
	) | rpl::start_with_next([=](const QSize &s, const QString &) {
		if (s.isNull()) {
			return;
		}
		badge->moveToLeft(
			button->fullTextWidth() + badgeLeft,
			(s.height() - badge->height()) / 2);
	}, badge->lifetime());
}

QString PrivacyBase(Privacy::Key key, Privacy::Option option) {
	using Key = Privacy::Key;
	using Option = Privacy::Option;
	switch (key) {
	case Key::CallsPeer2Peer:
		switch (option) {
		case Option::Everyone:
			return tr::lng_edit_privacy_calls_p2p_everyone(tr::now);
		case Option::Contacts:
			return tr::lng_edit_privacy_calls_p2p_contacts(tr::now);
		case Option::CloseFriends:
			return tr::lng_edit_privacy_close_friends(tr::now); // unused
		case Option::Nobody:
			return tr::lng_edit_privacy_calls_p2p_nobody(tr::now);
		}
		Unexpected("Value in Privacy::Option.");
	default:
		switch (option) {
		case Option::Everyone: return tr::lng_edit_privacy_everyone(tr::now);
		case Option::Contacts: return tr::lng_edit_privacy_contacts(tr::now);
		case Option::CloseFriends:
			return tr::lng_edit_privacy_close_friends(tr::now);
		case Option::Nobody: return tr::lng_edit_privacy_nobody(tr::now);
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
		if (const auto never = ExceptionUsersCount(value.never)) {
			add.push_back("-" + QString::number(never));
		}
		if (const auto always = ExceptionUsersCount(value.always)) {
			add.push_back("+" + QString::number(always));
		}
		if (!add.isEmpty()) {
			return PrivacyBase(key, value.option)
				+ " (" + add.join(", ") + ")";
		} else {
			return PrivacyBase(key, value.option);
		}
	});
}

#if 0 // Dead code.
void AddPremiumPrivacyButton(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> label,
		Privacy::Key key,
		Fn<std::unique_ptr<EditPrivacyController>()> controllerFactory) {
	const auto shower = Ui::CreateChild<rpl::lifetime>(container.get());
	const auto session = &controller->session();
	const auto &st = st::settingsButtonNoIcon;
	const auto button = container->add(object_ptr<Button>(
		container,
		rpl::duplicate(label),
		st));

	AddPremiumStar(button, session, rpl::duplicate(label), st.padding);

	struct State {
		State(QWidget *parent) : widget(parent) {
			widget.setAttribute(Qt::WA_TransparentForMouseEvents);
		}
		Ui::RpWidget widget;
	};
	const auto state = button->lifetime().make_state<State>(button);
	using WeakToast = base::weak_ptr<Ui::Toast::Instance>;
	const auto toast = std::make_shared<WeakToast>();

	{
		const auto rightLabel = Ui::CreateChild<Ui::FlatLabel>(
			button,
			st.rightLabel);

		state->widget.resize(st::settingsPremiumLock.size());
		state->widget.paintRequest(
		) | rpl::filter([=]() -> bool {
			return state->widget.x();
		}) | rpl::start_with_next([=] {
			auto p = QPainter(&state->widget);
			st::settingsPremiumLock.paint(p, 0, 0, state->widget.width());
		}, state->widget.lifetime());

		rpl::combine(
			button->sizeValue(),
			std::move(label),
			PrivacyString(session, key),
			Data::AmPremiumValue(session)
		) | rpl::start_with_next([=, &st](
				const QSize &buttonSize,
				const QString &button,
				const QString &text,
				bool premium) {
			const auto locked = !premium;
			const auto rightSkip = st::settingsButtonRightSkip;
			const auto lockSkip = st::settingsPremiumLockSkip;
			const auto available = buttonSize.width()
				- st.padding.left()
				- st.padding.right()
				- st.style.font->width(button)
				- rightSkip
				- (locked ? state->widget.width() + lockSkip : 0);
			rightLabel->setText(text);
			rightLabel->resizeToNaturalWidth(available);
			rightLabel->moveToRight(
				rightSkip,
				st.padding.top());
			state->widget.moveToRight(
				rightSkip + rightLabel->width() + lockSkip,
				(buttonSize.height() - state->widget.height()) / 2);
			state->widget.setVisible(locked);
		}, rightLabel->lifetime());
		rightLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
	}

	const auto showToast = [=] {
		auto link = Ui::Text::Link(
			Ui::Text::Semibold(
				tr::lng_settings_privacy_premium_link(tr::now)));
		(*toast) = controller->showToast({
			.text = tr::lng_settings_privacy_premium(
				tr::now,
				lt_link,
				link,
				Ui::Text::WithEntities),
			.st = &st::defaultMultilineToast,
			.duration = Ui::Toast::kDefaultDuration * 2,
			.multiline = true,
			.filter = crl::guard(&controller->session(), [=](
					const ClickHandlerPtr &,
					Qt::MouseButton button) {
				if (button == Qt::LeftButton) {
					if (const auto strong = toast->get()) {
						strong->hideAnimated();
						(*toast) = nullptr;
						Settings::ShowPremium(controller, QString());
						return true;
					}
				}
				return false;
			}),
		});
	};
	button->addClickHandler([=] {
		if (!session->premium()) {
			if (toast->empty()) {
				showToast();
			}
			return;
		}
		*shower = session->api().userPrivacy().value(
			key
		) | rpl::take(
			1
		) | rpl::start_with_next([=](const Privacy::Rule &value) {
			controller->show(Box<EditPrivacyBox>(
				controller,
				controllerFactory(),
				value));
		});
	});
}
#endif

void AddMessagesPrivacyButton(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	const auto session = &controller->session();
	const auto privacy = &session->api().globalPrivacy();
	auto label = rpl::conditional(
		privacy->newRequirePremium(),
		tr::lng_edit_privacy_premium(),
		tr::lng_edit_privacy_everyone());
	const auto &st = st::settingsButtonNoIcon;
	const auto button = AddButtonWithLabel(
		container,
		tr::lng_settings_messages_privacy(),
		rpl::duplicate(label),
		st,
		{});
	button->addClickHandler([=] {
		controller->show(Box(EditMessagesPrivacyBox, controller));
	});

	AddPremiumStar(button, session, rpl::duplicate(label), st.padding);
}

rpl::producer<int> BlockedPeersCount(not_null<::Main::Session*> session) {
	return session->api().blockedPeers().slice(
	) | rpl::map([](const Api::BlockedPeers::Slice &data) {
		return data.total;
	});
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
		[=] { return std::make_unique<LastSeenPrivacyController>(session); });
	add(
		tr::lng_settings_profile_photo_privacy(),
		Key::ProfilePhoto,
		[] { return std::make_unique<ProfilePhotoPrivacyController>(); });
	add(
		tr::lng_settings_bio_privacy(),
		Key::About,
		[] { return std::make_unique<AboutPrivacyController>(); });
	add(
		tr::lng_settings_forwards_privacy(),
		Key::Forwards,
		[=] { return std::make_unique<ForwardsPrivacyController>(
			controller); });
	add(
		tr::lng_settings_calls(),
		Key::Calls,
		[] { return std::make_unique<CallsPrivacyController>(); });
	add(
		tr::lng_settings_groups_invite(),
		Key::Invites,
		[] { return std::make_unique<GroupsInvitePrivacyController>(); });
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

	session->api().userPrivacy().reload(Api::UserPrivacy::Key::AddedByPhone);

	Ui::AddSkip(container, st::settingsPrivacySecurityPadding);
	Ui::AddDivider(container);
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
		{ &st::menuIconPermissions }
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

void SetupSensitiveContent(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<> updateTrigger) {
	using namespace rpl::mappers;

	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto inner = wrap->entity();

	Ui::AddSkip(inner);
	Ui::AddSubsectionTitle(inner, tr::lng_settings_sensitive_title());

	const auto session = &controller->session();

	std::move(
		updateTrigger
	) | rpl::start_with_next([=] {
		session->api().sensitiveContent().reload();
	}, container->lifetime());
	inner->add(object_ptr<Button>(
		inner,
		tr::lng_settings_sensitive_disable_filtering(),
		st::settingsButtonNoIcon
	))->toggleOn(
		session->api().sensitiveContent().enabled()
	)->toggledChanges(
	) | rpl::filter([=](bool toggled) {
		return toggled != session->api().sensitiveContent().enabledCurrent();
	}) | rpl::start_with_next([=](bool toggled) {
		session->api().sensitiveContent().update(toggled);
	}, container->lifetime());

	Ui::AddSkip(inner);
	Ui::AddDividerText(inner, tr::lng_settings_sensitive_about());

	wrap->toggleOn(session->api().sensitiveContent().canChange());
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
	) | rpl::start_with_next([=] {
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

void ClearPaymentInfoBoxBuilder(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
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

auto ClearPaymentInfoBox(not_null<Main::Session*> session) {
	return Box(ClearPaymentInfoBoxBuilder, session);
}

void SetupBotsAndWebsites(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, tr::lng_settings_security_bots());

	const auto session = &controller->session();
	container->add(object_ptr<Button>(
		container,
		tr::lng_settings_clear_payment_info(),
		st::settingsButtonNoIcon
	))->addClickHandler([=] {
		controller->show(ClearPaymentInfoBox(session));
	});

	Ui::AddSkip(container);
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
		showOther(Blocked::Id());
	});
	std::move(
		updateTrigger
	) | rpl::start_with_next([=] {
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
	) | rpl::start_with_next([=] {
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
		showOther(Websites::Id());
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
	) | rpl::start_with_next([=] {
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
		showOther(Sessions::Id());
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
	) | rpl::start_with_next([=] {
		session->api().selfDestruct().reload();
	}, container->lifetime());
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

} // namespace

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

object_ptr<Ui::BoxContent> EditCloudPasswordBox(not_null<Main::Session*> session) {
	const auto current = session->api().cloudPassword().stateCurrent();
	Assert(current.has_value());

	auto result = Box<PasscodeBox>(
		session,
		PasscodeBox::CloudFields::From(*current));
	const auto box = result.data();

	rpl::merge(
		box->newPasswordSet() | rpl::to_empty,
		box->passwordReloadNeeded()
	) | rpl::start_with_next([=] {
		session->api().cloudPassword().reload();
	}, box->lifetime());

	box->clearUnconfirmedPassword(
	) | rpl::start_with_next([=] {
		session->api().cloudPassword().clearUnconfirmedPassword();
	}, box->lifetime());

	return result;
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
	) | rpl::start_with_next([=] {
		session->api().cloudPassword().reload();
	}, box->lifetime());

	box->clearUnconfirmedPassword(
	) | rpl::start_with_next([=] {
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
		std::move(descriptor)
	);
	button->addClickHandler([=] {
		*shower = session->api().userPrivacy().value(
			key
		) | rpl::take(
			1
		) | rpl::start_with_next([=](const Privacy::Rule &value) {
			controller->show(Box<EditPrivacyBox>(
				controller,
				controllerFactory(),
				value));
		});
	});
	return button;
}

void SetupArchiveAndMute(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
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
	inner->add(object_ptr<Button>(
		inner,
		tr::lng_settings_auto_archive(),
		st::settingsButtonNoIcon
	))->toggleOn(
		privacy->archiveAndMute()
	)->toggledChanges(
	) | rpl::filter([=](bool toggled) {
		return toggled != privacy->archiveAndMuteCurrent();
	}) | rpl::start_with_next([=](bool toggled) {
		privacy->updateArchiveAndMute(toggled);
	}, container->lifetime());

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

PrivacySecurity::PrivacySecurity(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent) {
	setupContent(controller);
}

rpl::producer<QString> PrivacySecurity::title() {
	return tr::lng_settings_section_privacy();
}

void PrivacySecurity::setupContent(
		not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	auto updateOnTick = rpl::single(
	) | rpl::then(base::timer_each(kUpdateTimeout));
	const auto trigger = [=] {
		return rpl::duplicate(updateOnTick);
	};

	SetupSecurity(controller, content, trigger(), showOtherMethod());
	SetupPrivacy(controller, content, trigger());
#if !defined OS_MAC_STORE && !defined OS_WIN_STORE
	SetupSensitiveContent(controller, content, trigger());
#else // !OS_MAC_STORE && !OS_WIN_STORE
	AddDivider(content);
#endif // !OS_MAC_STORE && !OS_WIN_STORE
	SetupArchiveAndMute(controller, content);
	SetupBotsAndWebsites(controller, content);
	AddDivider(content);
	SetupSelfDestruction(controller, content, trigger());

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
