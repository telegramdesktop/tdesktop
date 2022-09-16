/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_privacy_security.h"

#include "api/api_authorizations.h"
#include "api/api_blocked_peers.h"
#include "api/api_cloud_password.h"
#include "api/api_self_destruct.h"
#include "api/api_sensitive_content.h"
#include "api/api_global_privacy.h"
#include "settings/cloud_password/settings_cloud_password_email_confirm.h"
#include "settings/cloud_password/settings_cloud_password_input.h"
#include "settings/cloud_password/settings_cloud_password_start.h"
#include "settings/settings_blocked_peers.h"
#include "settings/settings_common.h"
#include "settings/settings_local_passcode.h"
#include "settings/settings_premium.h" // Settings::ShowPremium.
#include "settings/settings_privacy_controllers.h"
#include "base/timer_rpl.h"
#include "boxes/edit_privacy_box.h"
#include "boxes/passcode_box.h"
#include "boxes/auto_lock_box.h"
#include "boxes/sessions_box.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/self_destruction_box.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "ui/chat/chat_style.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/layers/generic_box.h"
#include "calls/calls_instance.h"
#include "core/core_cloud_password.h"
#include "core/update_checker.h"
#include "base/platform/base_platform_last_input.h"
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
#include "facades.h"
#include "styles/style_settings.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

#include <QtGui/QGuiApplication>

namespace Settings {
namespace {

constexpr auto kUpdateTimeout = 60 * crl::time(1000);

using Privacy = Api::UserPrivacy;

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
		case Option::Nobody:
			return tr::lng_edit_privacy_calls_p2p_nobody(tr::now);
		}
		Unexpected("Value in Privacy::Option.");
	default:
		switch (option) {
		case Option::Everyone: return tr::lng_edit_privacy_everyone(tr::now);
		case Option::Contacts: return tr::lng_edit_privacy_contacts(tr::now);
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

void AddPremiumPrivacyButton(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> label,
		IconDescriptor &&descriptor,
		Privacy::Key key,
		Fn<std::unique_ptr<EditPrivacyController>()> controllerFactory) {
	const auto shower = Ui::CreateChild<rpl::lifetime>(container.get());
	const auto session = &controller->session();
	const auto &st = st::settingsButton;
	const auto button = AddButton(
		container,
		rpl::duplicate(label),
		st,
		std::move(descriptor));
	struct State {
		State(QWidget *parent) : widget(parent) {
			widget.setAttribute(Qt::WA_TransparentForMouseEvents);
		}
		Ui::RpWidget widget;
	};
	const auto state = button->lifetime().make_state<State>(button.get());
	using WeakToast = base::weak_ptr<Ui::Toast::Instance>;
	const auto toast = std::make_shared<WeakToast>();

	{
		const auto rightLabel = Ui::CreateChild<Ui::FlatLabel>(
			button.get(),
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
			tr::lng_settings_privacy_premium_link(tr::now));
		link.entities.push_back(
			EntityInText(EntityType::Semibold, 0, link.text.size()));
		const auto config = Ui::Toast::Config{
			.text = tr::lng_settings_privacy_premium(
				tr::now,
				lt_link,
				link,
				Ui::Text::WithEntities),
			.st = &st::defaultMultilineToast,
			.durationMs = Ui::Toast::kDefaultDuration * 2,
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
		};
		(*toast) = Ui::Toast::Show(
			Window::Show(controller).toastParent(),
			config);
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
			controller->show(
				Box<EditPrivacyBox>(controller, controllerFactory(), value),
				Ui::LayerOption::KeepOther);
		});
	});
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
	AddSkip(container, st::settingsPrivacySkip);
	AddSubsectionTitle(container, tr::lng_settings_privacy_title());

	const auto session = &controller->session();

	using Key = Privacy::Key;
	const auto add = [&](
			rpl::producer<QString> label,
			IconDescriptor &&descriptor,
			Key key,
			auto controllerFactory) {
		AddPrivacyButton(
			controller,
			container,
			std::move(label),
			std::move(descriptor),
			key,
			controllerFactory);
	};
	add(
		tr::lng_settings_phone_number_privacy(),
		{ &st::settingsIconCalls, kIconGreen },
		Key::PhoneNumber,
		[=] { return std::make_unique<PhoneNumberPrivacyController>(
			controller); });
	add(
		tr::lng_settings_last_seen(),
		{ &st::settingsIconOnline, kIconLightBlue },
		Key::LastSeen,
		[=] { return std::make_unique<LastSeenPrivacyController>(session); });
	add(
		tr::lng_settings_forwards_privacy(),
		{ &st::settingsIconForward, kIconLightOrange },
		Key::Forwards,
		[=] { return std::make_unique<ForwardsPrivacyController>(
			controller); });
	add(
		tr::lng_settings_profile_photo_privacy(),
		{ &st::settingsIconAccount, kIconRed },
		Key::ProfilePhoto,
		[] { return std::make_unique<ProfilePhotoPrivacyController>(); });
	add(
		tr::lng_settings_calls(),
		{ &st::settingsIconVideoCalls, kIconGreen },
		Key::Calls,
		[] { return std::make_unique<CallsPrivacyController>(); });
	AddPremiumPrivacyButton(
		controller,
		container,
		tr::lng_settings_voices_privacy(),
		{ &st::settingsPremiumIconVoice, kIconRed },
		Key::Voices,
		[=] { return std::make_unique<VoicesPrivacyController>(session); });
	add(
		tr::lng_settings_groups_invite(),
		{ &st::settingsIconGroup, kIconDarkBlue },
		Key::Invites,
		[] { return std::make_unique<GroupsInvitePrivacyController>(); });

	session->api().userPrivacy().reload(Api::UserPrivacy::Key::AddedByPhone);

	AddSkip(container, st::settingsPrivacySecurityPadding);
	AddDividerText(container, tr::lng_settings_group_privacy_about());
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

	AddSkip(inner);
	AddSubsectionTitle(inner, tr::lng_settings_new_unknown());

	const auto session = &controller->session();

	const auto privacy = &session->api().globalPrivacy();
	privacy->reload();
	AddButton(
		inner,
		tr::lng_settings_auto_archive(),
		st::settingsButtonNoIcon
	)->toggleOn(
		privacy->archiveAndMute()
	)->toggledChanges(
	) | rpl::filter([=](bool toggled) {
		return toggled != privacy->archiveAndMuteCurrent();
	}) | rpl::start_with_next([=](bool toggled) {
		privacy->update(toggled);
	}, container->lifetime());

	AddSkip(inner);
	AddDividerText(inner, tr::lng_settings_auto_archive_about());

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
		{ &st::settingsIconLock, kIconGreen }
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
		{ &st::settingsIconKey, kIconLightBlue }
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

	AddSkip(container);
	AddDividerText(container, tr::lng_settings_cloud_password_start_about());
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

	AddSkip(inner);
	AddSubsectionTitle(inner, tr::lng_settings_sensitive_title());

	const auto session = &controller->session();

	std::move(
		updateTrigger
	) | rpl::start_with_next([=] {
		session->api().sensitiveContent().reload();
	}, container->lifetime());
	AddButton(
		inner,
		tr::lng_settings_sensitive_disable_filtering(),
		st::settingsButtonNoIcon
	)->toggleOn(
		session->api().sensitiveContent().enabled()
	)->toggledChanges(
	) | rpl::filter([=](bool toggled) {
		return toggled != session->api().sensitiveContent().enabledCurrent();
	}) | rpl::start_with_next([=](bool toggled) {
		session->api().sensitiveContent().update(toggled);
	}, container->lifetime());

	AddSkip(inner);
	AddDividerText(inner, tr::lng_settings_sensitive_about());

	wrap->toggleOn(session->api().sensitiveContent().canChange());
}

void SetupSelfDestruction(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<> updateTrigger) {
	AddSkip(container);
	AddSubsectionTitle(container, tr::lng_settings_destroy_title());

	const auto session = &controller->session();

	std::move(
		updateTrigger
	) | rpl::start_with_next([=] {
		session->api().selfDestruct().reload();
	}, container->lifetime());
	const auto label = [&] {
		return session->api().selfDestruct().days(
		) | rpl::map(
			SelfDestructionBox::DaysLabel
		);
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
			session->api().selfDestruct().days()));
	});

	AddSkip(container);
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
	AddSkip(container);
	AddSubsectionTitle(container, tr::lng_settings_security_bots());

	const auto session = &controller->session();
	AddButton(
		container,
		tr::lng_settings_clear_payment_info(),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		controller->show(ClearPaymentInfoBox(session));
	});

	AddSkip(container);
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
		{ &st::settingsIconMinus, kIconRed });
	blockedPeers->addClickHandler([=] {
		showOther(Blocked::Id());
	});
	std::move(
		updateTrigger
	) | rpl::start_with_next([=] {
		session->api().blockedPeers().reload();
	}, blockedPeers->lifetime());
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

	auto count = controller->session().api().authorizations().totalChanges(
	) | rpl::map([](int count) {
		return count ? QString::number(count) : QString();
	});

	AddButtonWithLabel(
		container,
		tr::lng_settings_show_sessions(),
		std::move(count),
		st::settingsButton,
		{ &st::settingsIconLaptop, kIconLightOrange }
	)->addClickHandler([=] {
		showOther(Sessions::Id());
	});
}

void SetupSecurity(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<> updateTrigger,
		Fn<void(Type)> showOther) {
	AddSkip(container, st::settingsPrivacySkip);
	AddSubsectionTitle(container, tr::lng_settings_security());

	SetupBlockedList(
		controller,
		container,
		rpl::duplicate(updateTrigger),
		showOther);
	SetupSessionsList(
		controller,
		container,
		rpl::duplicate(updateTrigger),
		showOther);
	SetupLocalPasscode(controller, container, showOther);
	SetupCloudPassword(controller, container, showOther);
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

void AddPrivacyButton(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> label,
		IconDescriptor &&descriptor,
		Privacy::Key key,
		Fn<std::unique_ptr<EditPrivacyController>()> controllerFactory) {
	const auto shower = Ui::CreateChild<rpl::lifetime>(container.get());
	const auto session = &controller->session();
	AddButtonWithLabel(
		container,
		std::move(label),
		PrivacyString(session, key),
		st::settingsButton,
		std::move(descriptor)
	)->addClickHandler([=] {
		*shower = session->api().userPrivacy().value(
			key
		) | rpl::take(
			1
		) | rpl::start_with_next([=](const Privacy::Rule &value) {
			controller->show(
				Box<EditPrivacyBox>(controller, controllerFactory(), value),
				Ui::LayerOption::KeepOther);
		});
	});
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

rpl::producer<Type> PrivacySecurity::sectionShowOther() {
	return _showOther.events();
}

void PrivacySecurity::setupContent(
		not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	auto updateOnTick = rpl::single(
	) | rpl::then(base::timer_each(kUpdateTimeout));
	const auto trigger = [=] {
		return rpl::duplicate(updateOnTick);
	};

	SetupPrivacy(controller, content, trigger());
	SetupSecurity(controller, content, trigger(), [=](Type type) {
		_showOther.fire_copy(type);
	});
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
