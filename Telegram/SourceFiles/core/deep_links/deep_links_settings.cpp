/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/deep_links/deep_links_router.h"

#include "apiwrap.h"
#include "base/binary_guard.h"
#include "boxes/add_contact_box.h"
#include "boxes/gift_credits_box.h"
#include "boxes/language_box.h"
#include "boxes/stickers_box.h"
#include "chat_helpers/emoji_sets_manager.h"
#include "boxes/edit_privacy_box.h"
#include "boxes/peers/edit_peer_color_box.h"
#include "info/bot/earn/info_bot_earn_widget.h"
#include "info/bot/starref/info_bot_starref_common.h"
#include "info/bot/starref/info_bot_starref_join_widget.h"
#include "settings/settings_privacy_controllers.h"
#include "ui/chat/chat_style.h"
#include "boxes/star_gift_box.h"
#include "ui/boxes/confirm_box.h"
#include "ui/widgets/buttons.h"
#include "boxes/username_box.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "data/data_user.h"
#include "data/notify/data_notify_settings.h"
#include "info/info_memento.h"
#include "info/peer_gifts/info_peer_gifts_widget.h"
#include "info/stories/info_stories_widget.h"
#include "lang/lang_keys.h"
#include "ui/boxes/peer_qr_box.h"
#include "ui/layers/generic_box.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "storage/storage_domain.h"
#include "settings/sections/settings_active_sessions.h"
#include "settings/sections/settings_advanced.h"
#include "settings/sections/settings_blocked_peers.h"
#include "settings/sections/settings_business.h"
#include "settings/sections/settings_calls.h"
#include "settings/sections/settings_chat.h"
#include "settings/sections/settings_passkeys.h"
#include "data/components/passkeys.h"
#include "calls/calls_box_controller.h"
#include "settings/sections/settings_credits.h"
#include "settings/sections/settings_folders.h"
#include "settings/sections/settings_global_ttl.h"
#include "settings/sections/settings_information.h"
#include "settings/sections/settings_local_passcode.h"
#include "settings/sections/settings_main.h"
#include "settings/cloud_password/settings_cloud_password_email_confirm.h"
#include "settings/cloud_password/settings_cloud_password_input.h"
#include "settings/cloud_password/settings_cloud_password_start.h"
#include "api/api_cloud_password.h"
#include "core/core_cloud_password.h"
#include "settings/sections/settings_notifications.h"
#include "settings/sections/settings_notifications_type.h"
#include "settings/settings_power_saving.h"
#include "settings/sections/settings_premium.h"
#include "ui/power_saving.h"
#include "settings/sections/settings_privacy_security.h"
#include "settings/sections/settings_websites.h"
#include "boxes/connection_box.h"
#include "boxes/local_storage_box.h"
#include "mainwindow.h"
#include "window/window_session_controller.h"

namespace Core::DeepLinks {
namespace {

Result ShowLanguageBox(const Context &ctx, const QString &highlightId = QString()) {
	static auto Guard = base::binary_guard();
	if (!highlightId.isEmpty() && ctx.controller) {
		ctx.controller->setHighlightControlId(highlightId);
	}
	Guard = LanguageBox::Show(ctx.controller, highlightId);
	return Result::Handled;
}

Result ShowPowerSavingBox(
		const Context &ctx,
		PowerSaving::Flags highlightFlags = PowerSaving::Flags()) {
	if (!ctx.controller) {
		return Result::NeedsAuth;
	}
	ctx.controller->show(
		Box(::Settings::PowerSavingBox, highlightFlags),
		Ui::LayerOption::KeepOther,
		anim::type::normal);
	return Result::Handled;
}

Result ShowMainMenuWithHighlight(const Context &ctx, const QString &highlightId) {
	if (!ctx.controller) {
		return Result::NeedsAuth;
	}
	ctx.controller->setHighlightControlId(highlightId);
	ctx.controller->widget()->showMainMenu();
	return Result::Handled;
}

Result ShowSavedMessages(const Context &ctx) {
	if (!ctx.controller) {
		return Result::NeedsAuth;
	}
	ctx.controller->showPeerHistory(
		ctx.controller->session().userPeerId(),
		Window::SectionShow::Way::Forward);
	return Result::Handled;
}

Result ShowFaq(const Context &ctx) {
	::Settings::OpenFaq(
		ctx.controller ? base::make_weak(ctx.controller) : nullptr);
	return Result::Handled;
}

void ShowQrBox(not_null<Window::SessionController*> controller) {
	const auto user = controller->session().user();
	controller->uiShow()->show(Box(
		Ui::FillPeerQrBox,
		user.get(),
		std::nullopt,
		rpl::single(QString())));
}

Result ShowPeerColorBox(
		const Context &ctx,
		PeerColorTab tab,
		const QString &highlightId = QString()) {
	if (!ctx.controller) {
		return Result::NeedsAuth;
	}
	if (!highlightId.isEmpty()) {
		ctx.controller->setHighlightControlId(highlightId);
	}
	ctx.controller->show(Box(
		EditPeerColorBox,
		ctx.controller,
		ctx.controller->session().user(),
		std::shared_ptr<Ui::ChatStyle>(),
		std::shared_ptr<Ui::ChatTheme>(),
		tab));
	return Result::Handled;
}

Result HandleQrCode(const Context &ctx, bool highlightCopy) {
	if (!ctx.controller) {
		return Result::NeedsAuth;
	}

	if (highlightCopy) {
		ctx.controller->setHighlightControlId(u"self-qr-code/copy"_q);
	}

	const auto user = ctx.controller->session().user();
	if (!user->username().isEmpty()) {
		ShowQrBox(ctx.controller);
	} else {
		const auto controller = ctx.controller;
		controller->uiShow()->show(Box(
			UsernamesBoxWithCallback,
			user,
			[=] { ShowQrBox(controller); }));
	}
	return Result::Handled;
}

Result ShowEditName(
		const Context &ctx,
		EditNameBox::Focus focus = EditNameBox::Focus::FirstName) {
	if (!ctx.controller) {
		return Result::NeedsAuth;
	}
	if (ctx.controller->showFrozenError()) {
		return Result::Handled;
	}
	ctx.controller->show(Box<EditNameBox>(
		ctx.controller->session().user(),
		focus));
	return Result::Handled;
}

Result ShowEditUsername(const Context &ctx) {
	if (!ctx.controller) {
		return Result::NeedsAuth;
	}
	if (ctx.controller->showFrozenError()) {
		return Result::Handled;
	}
	ctx.controller->show(Box(UsernamesBox, ctx.controller->session().user()));
	return Result::Handled;
}

Result OpenInternalUrl(const Context &ctx, const QString &url) {
	if (!ctx.controller) {
		return Result::NeedsAuth;
	}
	Core::App().openInternalUrl(
		url,
		QVariant::fromValue(ClickHandlerContext{
			.sessionWindow = base::make_weak(ctx.controller),
		}));
	return Result::Handled;
}

Result ShowMyProfile(const Context &ctx) {
	if (!ctx.controller) {
		return Result::NeedsAuth;
	}
	ctx.controller->showSection(
		Info::Stories::Make(ctx.controller->session().user()));
	return Result::Handled;
}

Result ShowLogOutMenu(const Context &ctx) {
	if (!ctx.controller) {
		return Result::NeedsAuth;
	}
	ctx.controller->setHighlightControlId(u"settings/log-out"_q);
	ctx.controller->showSettings(::Settings::MainId());
	return Result::Handled;
}

Result ShowPasskeys(const Context &ctx, bool highlightCreate) {
	if (!ctx.controller) {
		return Result::NeedsAuth;
	}
	const auto controller = ctx.controller;
	const auto session = &controller->session();
	const auto showBox = [=] {
		if (highlightCreate) {
			controller->setHighlightControlId(u"passkeys/create"_q);
		}
		if (session->passkeys().list().empty()) {
			controller->show(Box([=](not_null<Ui::GenericBox*> box) {
				::Settings::PasskeysNoneBox(box, session);
				box->boxClosing() | rpl::on_next([=] {
					if (!session->passkeys().list().empty()) {
						controller->showSettings(::Settings::PasskeysId());
					}
				}, box->lifetime());
			}));
		} else {
			controller->showSettings(::Settings::PasskeysId());
		}
	};
	if (session->passkeys().listKnown()) {
		showBox();
	} else {
		session->passkeys().requestList(
		) | rpl::take(1) | rpl::on_next([=] {
			showBox();
		}, controller->lifetime());
	}
	return Result::Handled;
}

Result ShowAutoDeleteSetCustom(const Context &ctx) {
	if (!ctx.controller) {
		return Result::NeedsAuth;
	}
	ctx.controller->setHighlightControlId(u"auto-delete/set-custom"_q);
	ctx.controller->showSettings(::Settings::GlobalTTLId());
	return Result::Handled;
}

Result ShowNotificationType(
		const Context &ctx,
		Data::DefaultNotify type,
		const QString &highlightId = QString()) {
	if (!ctx.controller) {
		return Result::NeedsAuth;
	}
	if (!highlightId.isEmpty()) {
		ctx.controller->setHighlightControlId(highlightId);
	}
	ctx.controller->showSettings(::Settings::NotificationsType::Id(type));
	return Result::Handled;
}

using PrivacyKey = Api::UserPrivacy::Key;

template <typename ControllerFactory>
Result ShowPrivacyBox(
		const Context &ctx,
		PrivacyKey key,
		ControllerFactory controllerFactory,
		const QString &highlightControl = QString()) {
	if (!ctx.controller) {
		return Result::NeedsAuth;
	}
	const auto controller = ctx.controller;
	const auto session = &controller->session();
	if (!highlightControl.isEmpty()) {
		controller->setHighlightControlId(highlightControl);
	}
	const auto shower = std::make_shared<rpl::lifetime>();
	*shower = session->api().userPrivacy().value(
		key
	) | rpl::take(
		1
	) | rpl::on_next(crl::guard(controller, [=, shower = shower](
			const Api::UserPrivacy::Rule &value) {
		controller->show(Box<EditPrivacyBox>(
			controller,
			controllerFactory(),
			value));
	}));
	session->api().userPrivacy().reload(key);
	return Result::Handled;
}

} // namespace

void RegisterSettingsHandlers(Router &router) {
	router.add(u"settings"_q, {
		.path = QString(),
		.action = SettingsSection{ ::Settings::MainId() },
	});

	router.add(u"settings"_q, {
		.path = u"edit"_q,
		.action = SettingsSection{ ::Settings::InformationId() },
	});

	router.add(u"settings"_q, {
		.path = u"my-profile"_q,
		.action = CodeBlock{ ShowMyProfile },
	});

	router.add(u"settings"_q, {
		.path = u"my-profile/edit"_q,
		.action = SettingsSection{ ::Settings::InformationId() },
	});

	router.add(u"settings"_q, {
		.path = u"my-profile/posts"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			ctx.controller->setHighlightControlId(u"my-profile/posts"_q);
			return ShowMyProfile(ctx);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"my-profile/posts/add-album"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			ctx.controller->setHighlightControlId(u"my-profile/posts/add-album"_q);
			return ShowMyProfile(ctx);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"my-profile/gifts"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			ctx.controller->showSection(
				Info::PeerGifts::Make(ctx.controller->session().user()));
			return Result::Handled;
		}},
	});

	router.add(u"settings"_q, {
		.path = u"my-profile/archived-posts"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			ctx.controller->showSection(Info::Stories::Make(
				ctx.controller->session().user(),
				Info::Stories::ArchiveId()));
			return Result::Handled;
		}},
	});

	router.add(u"settings"_q, {
		.path = u"emoji-status"_q,
		.action = AliasTo{ u"chats"_q, u"emoji-status"_q },
	});

	router.add(u"settings"_q, {
		.path = u"profile-color"_q,
		.action = AliasTo{ u"settings"_q, u"edit/your-color"_q },
	});

	router.add(u"settings"_q, {
		.path = u"profile-color/profile"_q,
		.action = AliasTo{ u"settings"_q, u"edit/your-color"_q },
	});

	router.add(u"settings"_q, {
		.path = u"profile-color/profile/add-icons"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPeerColorBox(
				ctx,
				PeerColorTab::Profile,
				u"profile-color/add-icons"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"profile-color/profile/use-gift"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPeerColorBox(
				ctx,
				PeerColorTab::Profile,
				u"profile-color/use-gift"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"profile-color/profile/reset"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPeerColorBox(
				ctx,
				PeerColorTab::Profile,
				u"profile-color/reset"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"profile-color/name"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPeerColorBox(ctx, PeerColorTab::Name);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"profile-color/name/add-icons"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPeerColorBox(
				ctx,
				PeerColorTab::Name,
				u"profile-color/add-icons"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"profile-color/name/use-gift"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPeerColorBox(
				ctx,
				PeerColorTab::Name,
				u"profile-color/use-gift"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"profile-photo"_q,
		.action = AliasTo{ u"settings"_q, u"edit/set-photo"_q },
	});

	router.add(u"settings"_q, {
		.path = u"profile-photo/use-emoji"_q,
		.action = SettingsControl{
			::Settings::MainId(),
			u"profile-photo/use-emoji"_q,
		},
	});

	router.add(u"settings"_q, {
		.path = u"devices"_q,
		.action = SettingsSection{ ::Settings::SessionsId() },
	});

	router.add(u"settings"_q, {
		.path = u"folders"_q,
		.action = SettingsSection{ ::Settings::FoldersId() },
	});

	router.add(u"settings"_q, {
		.path = u"notifications"_q,
		.action = SettingsSection{ ::Settings::NotificationsId() },
	});

	router.add(u"settings"_q, {
		.path = u"privacy"_q,
		.action = SettingsSection{ ::Settings::PrivacySecurityId() },
	});

	router.add(u"settings"_q, {
		.path = u"privacy/blocked"_q,
		.action = SettingsSection{ ::Settings::BlockedPeersId() },
	});

	router.add(u"settings"_q, {
		.path = u"privacy/blocked/block-user"_q,
		.action = SettingsControl{
			::Settings::BlockedPeersId(),
			u"privacy/blocked/block-user"_q,
		},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/active-websites"_q,
		.action = SettingsSection{ ::Settings::WebsitesId() },
	});

	router.add(u"settings"_q, {
		.path = u"privacy/active-websites/disconnect-all"_q,
		.action = SettingsControl{
			::Settings::WebsitesId(),
			u"websites/disconnect-all"_q,
		},
	});

	const auto openPasscode = [](const Context &ctx, const QString &highlight) {
		if (!ctx.controller) {
			return Result::NeedsAuth;
		}
		if (!highlight.isEmpty()) {
			ctx.controller->setHighlightControlId(highlight);
		}
		const auto &local = ctx.controller->session().domain().local();
		if (local.hasLocalPasscode()) {
			ctx.controller->showSettings(::Settings::LocalPasscodeCheckId());
		} else {
			ctx.controller->showSettings(::Settings::LocalPasscodeCreateId());
		}
		return Result::Handled;
	};
	router.add(u"settings"_q, {
		.path = u"privacy/passcode"_q,
		.action = CodeBlock{ [=](const Context &ctx) {
			return openPasscode(ctx, QString());
		}},
	});
	router.add(u"settings"_q, {
		.path = u"privacy/passcode/disable"_q,
		.action = CodeBlock{ [=](const Context &ctx) {
			return openPasscode(ctx, u"passcode/disable"_q);
		}},
	});
	router.add(u"settings"_q, {
		.path = u"privacy/passcode/change"_q,
		.action = CodeBlock{ [=](const Context &ctx) {
			return openPasscode(ctx, u"passcode/change"_q);
		}},
	});
	router.add(u"settings"_q, {
		.path = u"privacy/passcode/auto-lock"_q,
		.action = CodeBlock{ [=](const Context &ctx) {
			return openPasscode(ctx, u"passcode/auto-lock"_q);
		}},
	});
	router.add(u"settings"_q, {
		.path = u"privacy/passcode/face-id"_q,
		.action = CodeBlock{ [=](const Context &ctx) {
			return openPasscode(ctx, u"passcode/biometrics"_q);
		}},
	});
	router.add(u"settings"_q, {
		.path = u"privacy/passcode/fingerprint"_q,
		.action = AliasTo{ u"settings"_q, u"privacy/passcode/face-id"_q },
	});

	router.add(u"settings"_q, {
		.path = u"privacy/auto-delete"_q,
		.action = SettingsSection{ ::Settings::GlobalTTLId() },
	});

	const auto openCloudPassword = [](const Context &ctx, const QString &highlight) {
		if (!ctx.controller) {
			return Result::NeedsAuth;
		}
		ctx.controller->showCloudPassword(highlight);
		return Result::Handled;
	};
	router.add(u"settings"_q, {
		.path = u"privacy/2sv"_q,
		.action = CodeBlock{ [=](const Context &ctx) {
			return openCloudPassword(ctx, QString());
		}},
	});
	router.add(u"settings"_q, {
		.path = u"privacy/2sv/change"_q,
		.action = CodeBlock{ [=](const Context &ctx) {
			return openCloudPassword(ctx, u"2sv/change"_q);
		}},
	});
	router.add(u"settings"_q, {
		.path = u"privacy/2sv/disable"_q,
		.action = CodeBlock{ [=](const Context &ctx) {
			return openCloudPassword(ctx, u"2sv/disable"_q);
		}},
	});
	router.add(u"settings"_q, {
		.path = u"privacy/2sv/change-email"_q,
		.action = CodeBlock{ [=](const Context &ctx) {
			return openCloudPassword(ctx, u"2sv/change-email"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/passkey"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPasskeys(ctx, false);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/passkey/create"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPasskeys(ctx, true);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/auto-delete/set-custom"_q,
		.action = CodeBlock{ ShowAutoDeleteSetCustom },
	});

	router.add(u"settings"_q, {
		.path = u"privacy/phone-number"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::PhoneNumber,
				[=] { return std::make_unique<::Settings::PhoneNumberPrivacyController>(ctx.controller); });
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/phone-number/never"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::PhoneNumber,
				[=] { return std::make_unique<::Settings::PhoneNumberPrivacyController>(ctx.controller); },
				u"privacy/never"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/phone-number/always"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::PhoneNumber,
				[=] { return std::make_unique<::Settings::PhoneNumberPrivacyController>(ctx.controller); },
				u"privacy/always"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/last-seen"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::LastSeen,
				[=] { return std::make_unique<::Settings::LastSeenPrivacyController>(&ctx.controller->session()); });
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/last-seen/never"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::LastSeen,
				[=] { return std::make_unique<::Settings::LastSeenPrivacyController>(&ctx.controller->session()); },
				u"privacy/never"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/last-seen/always"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::LastSeen,
				[=] { return std::make_unique<::Settings::LastSeenPrivacyController>(&ctx.controller->session()); },
				u"privacy/always"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/last-seen/hide-read-time"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::LastSeen,
				[=] { return std::make_unique<::Settings::LastSeenPrivacyController>(&ctx.controller->session()); },
				u"privacy/hide-read-time"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/profile-photos"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::ProfilePhoto,
				[=] { return std::make_unique<::Settings::ProfilePhotoPrivacyController>(); });
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/profile-photos/never"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::ProfilePhoto,
				[=] { return std::make_unique<::Settings::ProfilePhotoPrivacyController>(); },
				u"privacy/never"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/profile-photos/always"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::ProfilePhoto,
				[=] { return std::make_unique<::Settings::ProfilePhotoPrivacyController>(); },
				u"privacy/always"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/profile-photos/set-public"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::ProfilePhoto,
				[=] { return std::make_unique<::Settings::ProfilePhotoPrivacyController>(); },
				u"privacy/set-public"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/profile-photos/update-public"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::ProfilePhoto,
				[=] { return std::make_unique<::Settings::ProfilePhotoPrivacyController>(); },
				u"privacy/update-public"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/profile-photos/remove-public"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::ProfilePhoto,
				[=] { return std::make_unique<::Settings::ProfilePhotoPrivacyController>(); },
				u"privacy/remove-public"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/bio"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::About,
				[=] { return std::make_unique<::Settings::AboutPrivacyController>(); });
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/bio/never"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::About,
				[=] { return std::make_unique<::Settings::AboutPrivacyController>(); },
				u"privacy/never"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/bio/always"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::About,
				[=] { return std::make_unique<::Settings::AboutPrivacyController>(); },
				u"privacy/always"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/gifts"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::GiftsAutoSave,
				[=] { return std::make_unique<::Settings::GiftsAutoSavePrivacyController>(); });
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/gifts/show-icon"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::GiftsAutoSave,
				[=] { return std::make_unique<::Settings::GiftsAutoSavePrivacyController>(); },
				u"privacy/show-icon"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/gifts/never"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::GiftsAutoSave,
				[=] { return std::make_unique<::Settings::GiftsAutoSavePrivacyController>(); },
				u"privacy/never"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/gifts/always"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::GiftsAutoSave,
				[=] { return std::make_unique<::Settings::GiftsAutoSavePrivacyController>(); },
				u"privacy/always"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/gifts/accepted-types"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::GiftsAutoSave,
				[=] { return std::make_unique<::Settings::GiftsAutoSavePrivacyController>(); },
				u"privacy/accepted-types"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/birthday"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::Birthday,
				[=] { return std::make_unique<::Settings::BirthdayPrivacyController>(); });
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/birthday/add"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return OpenInternalUrl(ctx, u"internal:edit_birthday"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/birthday/never"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::Birthday,
				[=] { return std::make_unique<::Settings::BirthdayPrivacyController>(); },
				u"privacy/never"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/birthday/always"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::Birthday,
				[=] { return std::make_unique<::Settings::BirthdayPrivacyController>(); },
				u"privacy/always"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/saved-music"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::SavedMusic,
				[=] { return std::make_unique<::Settings::SavedMusicPrivacyController>(); });
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/saved-music/never"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::SavedMusic,
				[=] { return std::make_unique<::Settings::SavedMusicPrivacyController>(); },
				u"privacy/never"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/saved-music/always"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::SavedMusic,
				[=] { return std::make_unique<::Settings::SavedMusicPrivacyController>(); },
				u"privacy/always"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/forwards"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::Forwards,
				[=] { return std::make_unique<::Settings::ForwardsPrivacyController>(ctx.controller); });
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/forwards/never"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::Forwards,
				[=] { return std::make_unique<::Settings::ForwardsPrivacyController>(ctx.controller); },
				u"privacy/never"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/forwards/always"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::Forwards,
				[=] { return std::make_unique<::Settings::ForwardsPrivacyController>(ctx.controller); },
				u"privacy/always"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/calls"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::Calls,
				[=] { return std::make_unique<::Settings::CallsPrivacyController>(); });
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/calls/never"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::Calls,
				[=] { return std::make_unique<::Settings::CallsPrivacyController>(); },
				u"privacy/never"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/calls/always"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::Calls,
				[=] { return std::make_unique<::Settings::CallsPrivacyController>(); },
				u"privacy/always"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/calls/p2p"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::CallsPeer2Peer,
				[=] { return std::make_unique<::Settings::CallsPeer2PeerPrivacyController>(); });
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/calls/p2p/never"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::CallsPeer2Peer,
				[=] { return std::make_unique<::Settings::CallsPeer2PeerPrivacyController>(); },
				u"privacy/never"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/calls/p2p/always"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::CallsPeer2Peer,
				[=] { return std::make_unique<::Settings::CallsPeer2PeerPrivacyController>(); },
				u"privacy/always"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/voice"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::Voices,
				[=] { return std::make_unique<::Settings::VoicesPrivacyController>(&ctx.controller->session()); });
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/voice/never"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::Voices,
				[=] { return std::make_unique<::Settings::VoicesPrivacyController>(&ctx.controller->session()); },
				u"privacy/never"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/voice/always"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::Voices,
				[=] { return std::make_unique<::Settings::VoicesPrivacyController>(&ctx.controller->session()); },
				u"privacy/always"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/messages"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			ctx.controller->show(Box(EditMessagesPrivacyBox, ctx.controller, QString()));
			return Result::Handled;
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/messages/set-price"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			ctx.controller->show(Box(
				EditMessagesPrivacyBox,
				ctx.controller,
				u"privacy/set-price"_q));
			return Result::Handled;
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/messages/remove-fee"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			ctx.controller->show(Box(
				EditMessagesPrivacyBox,
				ctx.controller,
				u"privacy/remove-fee"_q));
			return Result::Handled;
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/invites"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::Invites,
				[=] { return std::make_unique<::Settings::GroupsInvitePrivacyController>(); });
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/invites/never"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::Invites,
				[=] { return std::make_unique<::Settings::GroupsInvitePrivacyController>(); },
				u"privacy/never"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/invites/always"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPrivacyBox(
				ctx,
				PrivacyKey::Invites,
				[=] { return std::make_unique<::Settings::GroupsInvitePrivacyController>(); },
				u"privacy/always"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/self-destruct"_q,
		.action = SettingsControl{
			::Settings::PrivacySecurityId(),
			u"privacy/self_destruct"_q,
		},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/data-settings/suggest-contacts"_q,
		.action = SettingsControl{
			::Settings::PrivacySecurityId(),
			u"privacy/top_peers"_q,
		},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/data-settings/clear-payment-info"_q,
		.action = SettingsControl{
			::Settings::PrivacySecurityId(),
			u"privacy/bots_payment"_q,
		},
	});

	router.add(u"settings"_q, {
		.path = u"privacy/archive-and-mute"_q,
		.action = SettingsControl{
			::Settings::PrivacySecurityId(),
			u"privacy/archive_and_mute"_q,
		},
	});

	router.add(u"settings"_q, {
		.path = u"data/storage"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			LocalStorageBox::Show(ctx.controller);
			return Result::Handled;
		}},
	});
	router.add(u"settings"_q, {
		.path = u"data/storage/clear-cache"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			LocalStorageBox::Show(ctx.controller, u"storage/clear-cache"_q);
			return Result::Handled;
		}},
	});
	router.add(u"settings"_q, {
		.path = u"data/max-cache"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			LocalStorageBox::Show(ctx.controller, u"storage/max-cache"_q);
			return Result::Handled;
		}},
	});

	router.add(u"settings"_q, {
		.path = u"data/show-18-content"_q,
		.action = SettingsControl{
			::Settings::ChatId(),
			u"chat/show-18-content"_q,
		},
	});

	router.add(u"settings"_q, {
		.path = u"data/proxy"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			ProxiesBoxController::Show(ctx.controller);
			return Result::Handled;
		}},
	});
	router.add(u"settings"_q, {
		.path = u"data/proxy/add-proxy"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			ProxiesBoxController::Show(ctx.controller, u"proxy/add-proxy"_q);
			return Result::Handled;
		}},
	});
	router.add(u"settings"_q, {
		.path = u"data/proxy/share-list"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			ProxiesBoxController::Show(ctx.controller, u"proxy/share-list"_q);
			return Result::Handled;
		}},
	});

	router.add(u"settings"_q, {
		.path = u"appearance"_q,
		.action = SettingsSection{ ::Settings::ChatId() },
	});

	router.add(u"settings"_q, {
		.path = u"power-saving"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPowerSavingBox(ctx);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"power-saving/stickers"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPowerSavingBox(ctx, PowerSaving::kStickersPanel);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"power-saving/emoji"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPowerSavingBox(ctx, PowerSaving::kEmojiPanel);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"power-saving/effects"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPowerSavingBox(ctx, PowerSaving::kChatBackground);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"appearance/themes"_q,
		.action = SettingsControl{
			::Settings::ChatId(),
			u"chat/themes"_q,
		},
	});

	router.add(u"settings"_q, {
		.path = u"appearance/themes/edit"_q,
		.action = SettingsControl{
			::Settings::ChatId(),
			u"chat/themes-edit"_q,
		},
	});

	router.add(u"settings"_q, {
		.path = u"appearance/themes/create"_q,
		.action = SettingsControl{
			::Settings::ChatId(),
			u"chat/themes-create"_q,
		},
	});

	router.add(u"settings"_q, {
		.path = u"appearance/wallpapers"_q,
		.action = SettingsControl{
			::Settings::ChatId(),
			u"chat/wallpapers"_q,
		},
	});

	router.add(u"settings"_q, {
		.path = u"appearance/wallpapers/set"_q,
		.action = SettingsControl{
			::Settings::ChatId(),
			u"chat/wallpapers-set"_q,
		},
	});

	router.add(u"settings"_q, {
		.path = u"appearance/wallpapers/choose-photo"_q,
		.action = SettingsControl{
			::Settings::ChatId(),
			u"chat/wallpapers-choose-photo"_q,
		},
	});

	router.add(u"settings"_q, {
		.path = u"appearance/your-color"_q,
		.action = AliasTo{ u"settings"_q, u"profile-color"_q },
	});

	router.add(u"settings"_q, {
		.path = u"appearance/your-color/profile"_q,
		.action = AliasTo{ u"settings"_q, u"profile-color/profile"_q },
	});

	router.add(u"settings"_q, {
		.path = u"appearance/your-color/profile/add-icons"_q,
		.action = AliasTo{ u"settings"_q, u"profile-color/profile/add-icons"_q },
	});

	router.add(u"settings"_q, {
		.path = u"appearance/your-color/profile/use-gift"_q,
		.action = AliasTo{ u"settings"_q, u"profile-color/profile/use-gift"_q },
	});

	router.add(u"settings"_q, {
		.path = u"appearance/your-color/profile/reset"_q,
		.action = AliasTo{ u"settings"_q, u"profile-color/profile/reset"_q },
	});

	router.add(u"settings"_q, {
		.path = u"appearance/your-color/name"_q,
		.action = AliasTo{ u"settings"_q, u"profile-color/name"_q },
	});

	router.add(u"settings"_q, {
		.path = u"appearance/your-color/name/add-icons"_q,
		.action = AliasTo{ u"settings"_q, u"profile-color/name/add-icons"_q },
	});

	router.add(u"settings"_q, {
		.path = u"appearance/your-color/name/use-gift"_q,
		.action = AliasTo{ u"settings"_q, u"profile-color/name/use-gift"_q },
	});

	router.add(u"settings"_q, {
		.path = u"appearance/night-mode"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowMainMenuWithHighlight(ctx, u"main-menu/night-mode"_q);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"appearance/auto-night-mode"_q,
		.action = SettingsControl{
			::Settings::ChatId(),
			u"chat/auto-night-mode"_q,
		},
	});

	router.add(u"settings"_q, {
		.path = u"appearance/text-size"_q,
		.action = SettingsControl{
			::Settings::MainId(),
			u"main/scale"_q,
		},
	});

	router.add(u"settings"_q, {
		.path = u"appearance/animations"_q,
		.action = AliasTo{ u"settings"_q, u"power-saving"_q },
	});

	router.add(u"settings"_q, {
		.path = u"appearance/stickers-and-emoji"_q,
		.action = SettingsControl{
			::Settings::ChatId(),
			u"chat/stickers-emoji"_q,
		},
	});

	router.add(u"settings"_q, {
		.path = u"appearance/stickers-and-emoji/edit"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			ctx.controller->show(Box<StickersBox>(
				ctx.controller->uiShow(),
				StickersBox::Section::Installed));
			return Result::Handled;
		}},
	});

	router.add(u"settings"_q, {
		.path = u"appearance/stickers-and-emoji/trending"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			ctx.controller->show(Box<StickersBox>(
				ctx.controller->uiShow(),
				StickersBox::Section::Featured));
			return Result::Handled;
		}},
	});

	router.add(u"settings"_q, {
		.path = u"appearance/stickers-and-emoji/archived"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			ctx.controller->show(Box<StickersBox>(
				ctx.controller->uiShow(),
				StickersBox::Section::Archived));
			return Result::Handled;
		}},
	});

	router.add(u"settings"_q, {
		.path = u"appearance/stickers-and-emoji/emoji"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			ctx.controller->show(
				Box<Ui::Emoji::ManageSetsBox>(&ctx.controller->session()));
			return Result::Handled;
		}},
	});

	router.add(u"settings"_q, {
		.path = u"appearance/stickers-and-emoji/emoji/suggest"_q,
		.action = SettingsControl{
			::Settings::ChatId(),
			u"chat/suggest-animated-emoji"_q,
		},
	});

	router.add(u"settings"_q, {
		.path = u"appearance/stickers-and-emoji/emoji/quick-reaction"_q,
		.action = SettingsControl{
			::Settings::ChatId(),
			u"chat/quick-reaction"_q,
		},
	});

	router.add(u"settings"_q, {
		.path = u"appearance/stickers-and-emoji/emoji/quick-reaction/choose"_q,
		.action = SettingsControl{
			::Settings::ChatId(),
			u"chat/quick-reaction-choose"_q,
		},
	});

	router.add(u"settings"_q, {
		.path = u"appearance/stickers-and-emoji/suggest-by-emoji"_q,
		.action = SettingsControl{
			::Settings::ChatId(),
			u"chat/suggest-by-emoji"_q,
		},
	});

	router.add(u"settings"_q, {
		.path = u"appearance/stickers-and-emoji/emoji/large"_q,
		.action = SettingsControl{
			::Settings::ChatId(),
			u"chat/large-emoji"_q,
		},
	});

	router.add(u"settings"_q, {
		.path = u"language"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowLanguageBox(ctx);
		}},
		.requiresAuth = false,
	});

	router.add(u"settings"_q, {
		.path = u"language/show-button"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowLanguageBox(ctx, u"language/show-button"_q);
		}},
		.requiresAuth = false,
	});

	router.add(u"settings"_q, {
		.path = u"language/translate-chats"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowLanguageBox(ctx, u"language/translate-chats"_q);
		}},
		.requiresAuth = false,
	});

	router.add(u"settings"_q, {
		.path = u"language/do-not-translate"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowLanguageBox(ctx, u"language/do-not-translate"_q);
		}},
		.requiresAuth = false,
	});

	router.add(u"settings"_q, {
		.path = u"premium"_q,
		.action = SettingsSection{ ::Settings::PremiumId() },
	});

	router.add(u"settings"_q, {
		.path = u"stars"_q,
		.action = SettingsSection{ ::Settings::CreditsId() },
	});

	router.add(u"settings"_q, {
		.path = u"stars/top-up"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			static auto handler = ::Settings::BuyStarsHandler();
			handler.handler(ctx.controller->uiShow())();
			return Result::Handled;
		}},
	});

	router.add(u"settings"_q, {
		.path = u"stars/stats"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			const auto self = ctx.controller->session().user();
			ctx.controller->showSection(Info::BotEarn::Make(self));
			return Result::Handled;
		}},
	});

	router.add(u"settings"_q, {
		.path = u"stars/gift"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			Ui::ShowGiftCreditsBox(ctx.controller, nullptr);
			return Result::Handled;
		}},
	});

	router.add(u"settings"_q, {
		.path = u"stars/earn"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			const auto self = ctx.controller->session().user();
			if (Info::BotStarRef::Join::Allowed(self)) {
				ctx.controller->showSection(Info::BotStarRef::Join::Make(self));
			}
			return Result::Handled;
		}},
	});

	router.add(u"settings"_q, {
		.path = u"ton"_q,
		.action = SettingsSection{ ::Settings::CurrencyId() },
	});

	router.add(u"settings"_q, {
		.path = u"business"_q,
		.action = SettingsSection{ ::Settings::BusinessId() },
	});

	router.add(u"settings"_q, {
		.path = u"business/do-not-hide-ads"_q,
		.action = SettingsControl{
			::Settings::BusinessId(),
			u"business/sponsored"_q,
		},
	});

	router.add(u"settings"_q, {
		.path = u"send-gift"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			Ui::ChooseStarGiftRecipient(ctx.controller);
			return Result::Handled;
		}},
	});

	router.add(u"settings"_q, {
		.path = u"send-gift/self"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			Ui::ShowStarGiftBox(ctx.controller, ctx.controller->session().user());
			return Result::Handled;
		}},
	});

	router.add(u"settings"_q, {
		.path = u"saved-messages"_q,
		.action = CodeBlock{ ShowSavedMessages },
	});

	router.add(u"settings"_q, {
		.path = u"calls"_q,
		.action = SettingsSection{ ::Settings::CallsId() },
	});

	router.add(u"settings"_q, {
		.path = u"calls/all"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			Calls::ShowCallsBox(ctx.controller);
			return Result::Handled;
		}},
	});

	router.add(u"settings"_q, {
		.path = u"faq"_q,
		.action = CodeBlock{ ShowFaq },
		.requiresAuth = false,
	});

	router.add(u"settings"_q, {
		.path = u"ask-question"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			::Settings::OpenAskQuestionConfirm(ctx.controller);
			return Result::Handled;
		}},
	});

	router.add(u"settings"_q, {
		.path = u"features"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			UrlClickHandler::Open(tr::lng_telegram_features_url(tr::now));
			return Result::Handled;
		}},
		.requiresAuth = false,
	});

	router.add(u"settings"_q, {
		.path = u"search"_q,
		.action = SettingsSection{ ::Settings::MainId() },
	});

	router.add(u"settings"_q, {
		.path = u"qr-code"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return HandleQrCode(ctx, false);
		}},
	});

	router.add(u"settings"_q, {
		.path = u"qr-code/share"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return HandleQrCode(ctx, true);
		}},
	});

	// Edit profile deep links.
	router.add(u"settings"_q, {
		.path = u"edit/set-photo"_q,
		.action = SettingsControl{
			::Settings::MainId(),
			u"profile-photo"_q,
		},
	});
	router.add(u"settings"_q, {
		.path = u"edit/first-name"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowEditName(ctx, EditNameBox::Focus::FirstName);
		}},
	});
	router.add(u"settings"_q, {
		.path = u"edit/last-name"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowEditName(ctx, EditNameBox::Focus::LastName);
		}},
	});
	router.add(u"settings"_q, {
		.path = u"edit/bio"_q,
		.action = SettingsControl{
			::Settings::InformationId(),
			u"edit/bio"_q,
		},
	});
	router.add(u"settings"_q, {
		.path = u"edit/birthday"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return OpenInternalUrl(ctx, u"internal:edit_birthday"_q);
		}},
	});
	router.add(u"settings"_q, {
		.path = u"edit/change-number"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			ctx.controller->show(
				Ui::MakeInformBox(tr::lng_change_phone_error()));
			return Result::Handled;
		}},
	});
	router.add(u"settings"_q, {
		.path = u"edit/username"_q,
		.action = CodeBlock{ ShowEditUsername },
	});
	router.add(u"settings"_q, {
		.path = u"edit/your-color"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowPeerColorBox(ctx, PeerColorTab::Profile);
		}},
	});
	router.add(u"settings"_q, {
		.path = u"edit/channel"_q,
		.action = SettingsControl{
			::Settings::InformationId(),
			u"edit/channel"_q,
		},
	});
	router.add(u"settings"_q, {
		.path = u"edit/add-account"_q,
		.action = SettingsControl{
			::Settings::InformationId(),
			u"edit/add-account"_q,
		},
	});
	router.add(u"settings"_q, {
		.path = u"edit/log-out"_q,
		.action = CodeBlock{ ShowLogOutMenu },
	});

	// Calls deep links.
	router.add(u"settings"_q, {
		.path = u"calls/start-call"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			Calls::ShowCallsBox(ctx.controller, true);
			return Result::Handled;
		}},
	});

	// Devices (sessions) deep links.
	router.add(u"settings"_q, {
		.path = u"devices/terminate-sessions"_q,
		.action = SettingsControl{
			::Settings::SessionsId(),
			u"devices/terminate-sessions"_q,
		},
	});
	router.add(u"settings"_q, {
		.path = u"devices/auto-terminate"_q,
		.action = SettingsControl{
			::Settings::SessionsId(),
			u"devices/auto-terminate"_q,
		},
	});

	// Folders deep links.
	router.add(u"settings"_q, {
		.path = u"folders/create"_q,
		.action = SettingsControl{
			::Settings::FoldersId(),
			u"folders/create"_q,
		},
	});
	router.add(u"settings"_q, {
		.path = u"folders/add-recommended"_q,
		.action = SettingsControl{
			::Settings::FoldersId(),
			u"folders/add-recommended"_q,
		},
	});
	router.add(u"settings"_q, {
		.path = u"folders/show-tags"_q,
		.action = SettingsControl{
			::Settings::FoldersId(),
			u"folders/show-tags"_q,
		},
	});
	router.add(u"settings"_q, {
		.path = u"folders/tab-view"_q,
		.action = SettingsControl{
			::Settings::FoldersId(),
			u"folders/tab-view"_q,
		},
	});

	// Notifications deep links.
	router.add(u"settings"_q, {
		.path = u"notifications/accounts"_q,
		.action = SettingsControl{
			::Settings::NotificationsId(),
			u"notifications/accounts"_q,
		},
	});

	// Notification type deep links - Private Chats.
	router.add(u"settings"_q, {
		.path = u"notifications/private-chats"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowNotificationType(ctx, Data::DefaultNotify::User);
		}},
	});
	router.add(u"settings"_q, {
		.path = u"notifications/private-chats/edit"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowNotificationType(ctx, Data::DefaultNotify::User);
		}},
	});
	router.add(u"settings"_q, {
		.path = u"notifications/private-chats/show"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowNotificationType(
				ctx,
				Data::DefaultNotify::User,
				u"notifications/type/show"_q);
		}},
	});
	router.add(u"settings"_q, {
		.path = u"notifications/private-chats/sound"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowNotificationType(
				ctx,
				Data::DefaultNotify::User,
				u"notifications/type/sound"_q);
		}},
	});
	router.add(u"settings"_q, {
		.path = u"notifications/private-chats/add-exception"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowNotificationType(
				ctx,
				Data::DefaultNotify::User,
				u"notifications/type/add-exception"_q);
		}},
	});
	router.add(u"settings"_q, {
		.path = u"notifications/private-chats/delete-exceptions"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowNotificationType(
				ctx,
				Data::DefaultNotify::User,
				u"notifications/type/delete-exceptions"_q);
		}},
	});

	// Notification type deep links - Groups.
	router.add(u"settings"_q, {
		.path = u"notifications/groups"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowNotificationType(ctx, Data::DefaultNotify::Group);
		}},
	});
	router.add(u"settings"_q, {
		.path = u"notifications/groups/edit"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowNotificationType(ctx, Data::DefaultNotify::Group);
		}},
	});
	router.add(u"settings"_q, {
		.path = u"notifications/groups/show"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowNotificationType(
				ctx,
				Data::DefaultNotify::Group,
				u"notifications/type/show"_q);
		}},
	});
	router.add(u"settings"_q, {
		.path = u"notifications/groups/sound"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowNotificationType(
				ctx,
				Data::DefaultNotify::Group,
				u"notifications/type/sound"_q);
		}},
	});
	router.add(u"settings"_q, {
		.path = u"notifications/groups/add-exception"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowNotificationType(
				ctx,
				Data::DefaultNotify::Group,
				u"notifications/type/add-exception"_q);
		}},
	});
	router.add(u"settings"_q, {
		.path = u"notifications/groups/delete-exceptions"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowNotificationType(
				ctx,
				Data::DefaultNotify::Group,
				u"notifications/type/delete-exceptions"_q);
		}},
	});

	// Notification type deep links - Channels.
	router.add(u"settings"_q, {
		.path = u"notifications/channels"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowNotificationType(ctx, Data::DefaultNotify::Broadcast);
		}},
	});
	router.add(u"settings"_q, {
		.path = u"notifications/channels/edit"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowNotificationType(ctx, Data::DefaultNotify::Broadcast);
		}},
	});
	router.add(u"settings"_q, {
		.path = u"notifications/channels/show"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowNotificationType(
				ctx,
				Data::DefaultNotify::Broadcast,
				u"notifications/type/show"_q);
		}},
	});
	router.add(u"settings"_q, {
		.path = u"notifications/channels/sound"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowNotificationType(
				ctx,
				Data::DefaultNotify::Broadcast,
				u"notifications/type/sound"_q);
		}},
	});
	router.add(u"settings"_q, {
		.path = u"notifications/channels/add-exception"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowNotificationType(
				ctx,
				Data::DefaultNotify::Broadcast,
				u"notifications/type/add-exception"_q);
		}},
	});
	router.add(u"settings"_q, {
		.path = u"notifications/channels/delete-exceptions"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowNotificationType(
				ctx,
				Data::DefaultNotify::Broadcast,
				u"notifications/type/delete-exceptions"_q);
		}},
	});

	// Other notification deep links.
	router.add(u"settings"_q, {
		.path = u"notifications/include-muted-chats"_q,
		.action = SettingsControl{
			::Settings::NotificationsId(),
			u"notifications/include-muted-chats"_q,
		},
	});
	router.add(u"settings"_q, {
		.path = u"notifications/count-unread-messages"_q,
		.action = SettingsControl{
			::Settings::NotificationsId(),
			u"notifications/count-unread-messages"_q,
		},
	});
	router.add(u"settings"_q, {
		.path = u"notifications/new-contacts"_q,
		.action = SettingsControl{
			::Settings::NotificationsId(),
			u"notifications/events/joined"_q,
		},
	});
	router.add(u"settings"_q, {
		.path = u"notifications/pinned-messages"_q,
		.action = SettingsControl{
			::Settings::NotificationsId(),
			u"notifications/events/pinned"_q,
		},
	});
}

} // namespace Core::DeepLinks
