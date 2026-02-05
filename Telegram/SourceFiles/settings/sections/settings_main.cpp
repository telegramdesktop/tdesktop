/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_main.h"

#include "settings/settings_common_session.h"

#include "api/api_cloud_password.h"
#include "api/api_credits.h"
#include "api/api_global_privacy.h"
#include "api/api_peer_photo.h"
#include "api/api_premium.h"
#include "api/api_sensitive_content.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "base/platform/base_platform_info.h"
#include "boxes/language_box.h"
#include "boxes/star_gift_box.h"
#include "boxes/username_box.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "data/components/credits.h"
#include "data/components/promo_suggestions.h"
#include "data/data_chat_filters.h"
#include "data/data_cloud_themes.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "info/profile/info_profile_badge.h"
#include "info/profile/info_profile_emoji_status_panel.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_cloud_manager.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "settings/settings_builder.h"
#include "settings/cloud_password/settings_cloud_password_input.h"
#include "settings/sections/settings_advanced.h"
#include "settings/sections/settings_business.h"
#include "settings/sections/settings_calls.h"
#include "settings/sections/settings_chat.h"
#include "settings/settings_codes.h"
#include "settings/settings_faq_suggestions.h"
#include "settings/sections/settings_credits.h"
#include "settings/sections/settings_folders.h"
#include "settings/sections/settings_information.h"
#include "settings/sections/settings_notifications.h"
#include "settings/settings_power_saving.h"
#include "settings/sections/settings_premium.h"
#include "settings/sections/settings_privacy_security.h"
#include "settings/settings_scale_preview.h"
#include "storage/localstorage.h"
#include "ui/basic_click_handlers.h"
#include "ui/boxes/confirm_box.h"
#include "ui/boxes/peer_qr_box.h"
#include "ui/controls/userpic_button.h"
#include "ui/layers/generic_box.h"
#include "ui/new_badges.h"
#include "ui/power_saving.h"
#include "ui/rect.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_item_base.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/slide_wrap.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>

namespace Settings {
namespace {

using namespace Builder;

constexpr auto kSugValidatePhone = "VALIDATE_PHONE_NUMBER"_cs;

class Cover final : public Ui::FixedHeightWidget {
public:
	Cover(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<UserData*> user);
	~Cover();

	[[nodiscard]] not_null<Ui::UserpicButton*> userpic() const {
		return _userpic.data();
	}

private:
	void setupChildGeometry();
	void initViewers();
	void refreshNameGeometry(int newWidth);
	void refreshPhoneGeometry(int newWidth);
	void refreshUsernameGeometry(int newWidth);
	void refreshQrButtonGeometry(int newWidth);

	const not_null<Window::SessionController*> _controller;
	const not_null<UserData*> _user;
	Info::Profile::EmojiStatusPanel _emojiStatusPanel;
	Info::Profile::Badge _badge;

	object_ptr<Ui::UserpicButton> _userpic;
	object_ptr<Ui::FlatLabel> _name = { nullptr };
	object_ptr<Ui::FlatLabel> _phone = { nullptr };
	object_ptr<Ui::FlatLabel> _username = { nullptr };
	object_ptr<Ui::IconButton> _qrButton = { nullptr };

};

Cover::Cover(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<UserData*> user)
: FixedHeightWidget(
	parent,
	st::settingsPhotoTop
		+ st::infoProfileCover.photo.size.height()
		+ st::settingsPhotoBottom)
, _controller(controller)
, _user(user)
, _badge(
	this,
	st::infoPeerBadge,
	&user->session(),
	Info::Profile::BadgeContentForPeer(user),
	&_emojiStatusPanel,
	[=] {
		return controller->isGifPausedAtLeastFor(
			Window::GifPauseReason::Layer);
	},
	0, // customStatusLoopsLimit
	Info::Profile::BadgeType::Premium)
, _userpic(
	this,
	controller,
	_user,
	Ui::UserpicButton::Role::OpenPhoto,
	Ui::UserpicButton::Source::PeerPhoto,
	st::infoProfileCover.photo)
, _name(this, st::infoProfileCover.name)
, _phone(this, st::defaultFlatLabel)
, _username(this, st::infoProfileMegagroupCover.status) {
	_user->updateFull();

	_name->setSelectable(true);
	_name->setContextCopyText(tr::lng_profile_copy_fullname(tr::now));

	_phone->setSelectable(true);
	_phone->setContextCopyText(tr::lng_profile_copy_phone(tr::now));
	const auto hook = [=](Ui::FlatLabel::ContextMenuRequest request) {
		if (request.selection.empty()) {
			const auto c = [=] {
				auto phone = rpl::variable<TextWithEntities>(
					Info::Profile::PhoneValue(_user)).current().text;
				phone.replace(' ', QString()).replace('-', QString());
				TextUtilities::SetClipboardText({ phone });
			};
			request.menu->addAction(tr::lng_profile_copy_phone(tr::now), c);
		} else {
			_phone->fillContextMenu(request);
		}
	};
	_phone->setContextMenuHook(hook);

	initViewers();
	setupChildGeometry();

	_userpic->switchChangePhotoOverlay(_user->isSelf(), [=](
			Ui::UserpicButton::ChosenImage chosen) {
		auto &image = chosen.image;
		_userpic->showCustom(base::duplicate(image));
		_user->session().api().peerPhoto().upload(
			_user,
			{
				std::move(image),
				chosen.markup.documentId,
				chosen.markup.colors,
			});
	});

	_badge.setPremiumClickCallback([=] {
		_emojiStatusPanel.show(
			_controller,
			_badge.widget(),
			_badge.sizeTag());
	});
	_badge.updated() | rpl::on_next([=] {
		refreshNameGeometry(width());
	}, _name->lifetime());

	_qrButton.create(this, st::infoProfileLabeledButtonQr);
	_qrButton->setClickedCallback([=, show = controller->uiShow()] {
		Ui::DefaultShowFillPeerQrBoxCallback(show, _user);
	});
	Info::Profile::UsernamesValue(
		_user
	) | rpl::on_next([=](const auto &usernames) {
		_qrButton->setVisible(!usernames.empty());
		refreshNameGeometry(width());
		refreshQrButtonGeometry(width());
	}, _qrButton->lifetime());
}

Cover::~Cover() = default;

void Cover::setupChildGeometry() {
	using namespace rpl::mappers;
	widthValue(
	) | rpl::on_next([=](int newWidth) {
		_userpic->moveToLeft(
			st::settingsPhotoLeft,
			st::settingsPhotoTop,
			newWidth);
		refreshNameGeometry(newWidth);
		refreshPhoneGeometry(newWidth);
		refreshUsernameGeometry(newWidth);
		refreshQrButtonGeometry(newWidth);
	}, lifetime());
}

void Cover::initViewers() {
	Info::Profile::NameValue(
		_user
	) | rpl::on_next([=](const QString &name) {
		_name->setText(name);
		refreshNameGeometry(width());
	}, lifetime());

	Info::Profile::PhoneValue(
		_user
	) | rpl::on_next([=](const TextWithEntities &value) {
		_phone->setText(value.text);
		refreshPhoneGeometry(width());
	}, lifetime());

	Info::Profile::UsernameValue(
		_user
	) | rpl::on_next([=](const TextWithEntities &value) {
		_username->setMarkedText(tr::link(value.text.isEmpty()
			? tr::lng_settings_username_add(tr::now)
			: value.text));
		refreshUsernameGeometry(width());
	}, lifetime());

	_username->overrideLinkClickHandler([=] {
		if (_controller->showFrozenError()) {
			return;
		}
		const auto username = _user->username();
		if (username.isEmpty()) {
			_controller->show(Box(UsernamesBox, _user));
		} else {
			QGuiApplication::clipboard()->setText(
				_user->session().createInternalLinkFull(username));
			_controller->showToast(tr::lng_username_copied(tr::now));
		}
	});
}

void Cover::refreshNameGeometry(int newWidth) {
	const auto nameLeft = st::settingsNameLeft;
	const auto nameTop = st::settingsNameTop;
	const auto qrButtonWidth = (_qrButton && !_qrButton->isHidden())
		? (_qrButton->width() + st::settingsNameLeft)
		: 0;
	auto nameWidth = newWidth
		- nameLeft
		- st::infoProfileCover.rightSkip
		- qrButtonWidth;
	if (const auto width = _badge.widget() ? _badge.widget()->width() : 0) {
		nameWidth -= st::infoVerifiedCheckPosition.x() + width;
	}
	_name->resizeToNaturalWidth(nameWidth);
	_name->moveToLeft(nameLeft, nameTop, newWidth);
	const auto badgeLeft = nameLeft + _name->width();
	const auto badgeTop = nameTop;
	const auto badgeBottom = nameTop + _name->height();
	_badge.move(badgeLeft, badgeTop, badgeBottom);
}

void Cover::refreshPhoneGeometry(int newWidth) {
	const auto phoneLeft = st::settingsPhoneLeft;
	const auto phoneTop = st::settingsPhoneTop;
	const auto phoneWidth = newWidth
		- phoneLeft
		- st::infoProfileCover.rightSkip;
	_phone->resizeToWidth(phoneWidth);
	_phone->moveToLeft(phoneLeft, phoneTop, newWidth);
}

void Cover::refreshUsernameGeometry(int newWidth) {
	const auto usernameLeft = st::settingsUsernameLeft;
	const auto usernameTop = st::settingsUsernameTop;
	const auto usernameRight = st::infoProfileCover.rightSkip;
	const auto usernameWidth = newWidth - usernameLeft - usernameRight;
	_username->resizeToWidth(usernameWidth);
	_username->moveToLeft(usernameLeft, usernameTop, newWidth);
}

void Cover::refreshQrButtonGeometry(int newWidth) {
	if (!_qrButton) {
		return;
	}
	const auto buttonTop = (height() - _qrButton->height()) / 2;
	const auto buttonRight = st::infoProfileCover.rightSkip;
	const auto inset = st::infoProfileLabeledButtonQrInset;
	_qrButton->moveToRight(buttonRight - inset, buttonTop, newWidth);
}

void BuildSectionButtons(SectionBuilder &builder) {
	const auto session = builder.session();
	const auto controller = builder.controller();
	const auto showOther = builder.showOther();

	if (!session->supportMode()) {
		builder.addSectionButton({
			.title = tr::lng_settings_my_account(),
			.targetSection = InformationId(),
			.icon = { &st::menuIconProfile },
			.keywords = { u"profile"_q, u"edit"_q, u"information"_q },
		});
	}

	builder.addSectionButton({
		.title = tr::lng_settings_section_notify(),
		.targetSection = NotificationsId(),
		.icon = { &st::menuIconNotifications },
		.keywords = { u"alerts"_q, u"sounds"_q, u"badge"_q },
	});

	builder.addSectionButton({
		.title = tr::lng_settings_section_privacy(),
		.targetSection = PrivacySecurityId(),
		.icon = { &st::menuIconLock },
		.keywords = { u"security"_q, u"passcode"_q, u"password"_q, u"2fa"_q },
	});

	builder.addSectionButton({
		.title = tr::lng_settings_section_chat_settings(),
		.targetSection = ChatId(),
		.icon = { &st::menuIconChatBubble },
		.keywords = { u"themes"_q, u"appearance"_q, u"stickers"_q },
	});

	{ // Folders
		const auto preload = [=] {
			session->data().chatsFilters().requestSuggested();
		};
		const auto hasFilters = session->data().chatsFilters().has()
			|| session->settings().dialogsFiltersEnabled();

		auto shownProducer = hasFilters
			? rpl::single(true) | rpl::type_erased
			: (rpl::single(rpl::empty) | rpl::then(
				session->appConfig().refreshed()
			) | rpl::map([=] {
			const auto enabled = session->appConfig().get<bool>(
				u"dialog_filters_enabled"_q,
				false);
			if (enabled) {
				preload();
			}
			return enabled;
		}));

		if (hasFilters) {
			preload();
		}

		builder.addButton({
			.title = tr::lng_settings_section_filters(),
			.icon = { &st::menuIconShowInFolder },
			.onClick = [=] { showOther(FoldersId()); },
			.keywords = { u"filters"_q, u"tabs"_q },
			.shown = std::move(shownProducer),
		});
	}

	builder.addSectionButton({
		.title = tr::lng_settings_advanced(),
		.targetSection = AdvancedId(),
		.icon = { &st::menuIconManage },
		.keywords = { u"performance"_q, u"proxy"_q, u"experimental"_q },
	});

	builder.addSectionButton({
		.title = tr::lng_settings_section_devices(),
		.targetSection = CallsId(),
		.icon = { &st::menuIconUnmute },
		.keywords = { u"sessions"_q, u"calls"_q },
	});

	builder.addButton({
		.id = u"main/power"_q,
		.title = tr::lng_settings_power_menu(),
		.icon = { &st::menuIconPowerUsage },
		.onClick = [=] {
			controller->show(Box(PowerSavingBox, PowerSaving::Flags()));
		},
		.keywords = { u"battery"_q, u"animations"_q, u"power"_q, u"saving"_q },
	});

	builder.addButton({
		.id = u"main/language"_q,
		.title = tr::lng_settings_language(),
		.icon = { &st::menuIconTranslate },
		.label = rpl::single(
			Lang::GetInstance().id()
		) | rpl::then(
			Lang::GetInstance().idChanges()
		) | rpl::map([] { return Lang::GetInstance().nativeName(); }),
		.onClick = [=] {
			static auto Guard = base::binary_guard();
			Guard = LanguageBox::Show(controller);
		},
		.keywords = { u"translate"_q, u"localization"_q, u"language"_q },
	});
}

void BuildInterfaceScale(SectionBuilder &builder) {
	if (!HasInterfaceScale()) {
		return;
	}

	builder.addDivider();
	builder.addSkip();

	builder.add([](const WidgetContext &ctx) {
		const auto window = &ctx.controller->window();
		auto wrap = object_ptr<Ui::VerticalLayout>(ctx.container);
		SetupInterfaceScale(window, wrap.data());
		return SectionBuilder::WidgetToAdd{ .widget = std::move(wrap) };
	}, [] {
		return SearchEntry{
			.id = u"main/scale"_q,
			.title = tr::lng_settings_default_scale(tr::now),
			.keywords = { u"zoom"_q, u"size"_q, u"interface"_q, u"ui"_q },
		};
	});

	builder.addSkip();
}

void BuildPremiumSection(SectionBuilder &builder) {
	const auto session = builder.session();
	const auto controller = builder.controller();
	const auto showOther = builder.showOther();

	if (!session->premiumPossible()) {
		return;
	}

	builder.addDivider();
	builder.addSkip();

	builder.addPremiumButton({
		.id = u"main/premium"_q,
		.title = tr::lng_premium_summary_title(),
		.onClick = [=] {
			controller->setPremiumRef("settings");
			showOther(PremiumId());
		},
		.keywords = { u"subscription"_q },
	});

	session->credits().load();
	builder.addPremiumButton({
		.id = u"main/credits"_q,
		.title = tr::lng_settings_credits(),
		.label = session->credits().balanceValue(
		) | rpl::map([](CreditsAmount c) {
			return c
				? Lang::FormatCreditsAmountToShort(c).string
				: QString();
		}),
		.credits = true,
		.onClick = [=] {
			controller->setPremiumRef("settings");
			showOther(CreditsId());
		},
		.keywords = { u"stars"_q, u"balance"_q },
	});

	session->credits().tonLoad();
	builder.addButton({
		.id = u"main/currency"_q,
		.title = tr::lng_settings_currency(),
		.icon = { &st::menuIconTon },
		.label = session->credits().tonBalanceValue(
		) | rpl::map([](CreditsAmount c) {
			return c ? Lang::FormatCreditsAmountToShort(c).string : u""_q;
		}),
		.onClick = [=] {
			controller->setPremiumRef("settings");
			showOther(CurrencyId());
		},
		.keywords = { u"ton"_q, u"crypto"_q, u"wallet"_q },
		.shown = session->credits().tonBalanceValue(
		) | rpl::map([](CreditsAmount c) { return !c.empty(); }),
	});

	builder.addButton({
		.id = u"main/business"_q,
		.title = tr::lng_business_title(),
		.icon = { .icon = &st::menuIconShop },
		.onClick = [=] { showOther(BusinessId()); },
		.keywords = { u"work"_q, u"company"_q },
	});

	if (session->premiumCanBuy()) {
		builder.addButton({
			.id = u"main/send-gift"_q,
			.title = tr::lng_settings_gift_premium(),
			.icon = { .icon = &st::menuIconGiftPremium, .newBadge = true },
			.onClick = [=] { Ui::ChooseStarGiftRecipient(controller); },
			.keywords = { u"present"_q, u"send"_q },
		});
	}

	builder.addSkip();
}

void BuildHelpSection(SectionBuilder &builder) {
	builder.addDivider();
	builder.addSkip();

	const auto controller = builder.controller();
	builder.addButton({
		.id = u"main/faq"_q,
		.title = tr::lng_settings_faq(),
		.icon = { &st::menuIconFaq },
		.onClick = [=] { OpenFaq(controller); },
		.keywords = { u"help"_q, u"support"_q, u"questions"_q },
	});

	builder.addButton({
		.id = u"main/features"_q,
		.title = tr::lng_settings_features(),
		.icon = { &st::menuIconEmojiObjects },
		.onClick = [] {
			UrlClickHandler::Open(tr::lng_telegram_features_url(tr::now));
		},
		.keywords = { u"tips"_q, u"tutorial"_q },
	});

	builder.addButton({
		.id = u"main/ask-question"_q,
		.title = tr::lng_settings_ask_question(),
		.icon = { &st::menuIconDiscussion },
		.onClick = [=] { OpenAskQuestionConfirm(controller); },
		.keywords = { u"contact"_q, u"feedback"_q },
	});

	builder.addSkip();
}

void BuildValidationSuggestions(SectionBuilder &builder) {
	builder.add([](const WidgetContext &ctx) {
		const auto controller = ctx.controller.get();
		const auto showOther = ctx.showOther;
		auto wrap = object_ptr<Ui::VerticalLayout>(ctx.container);
		SetupValidatePhoneNumberSuggestion(controller, wrap.data(), showOther);
		return SectionBuilder::WidgetToAdd{ .widget = std::move(wrap) };
	});

	builder.add([](const WidgetContext &ctx) {
		const auto controller = ctx.controller.get();
		const auto showOther = ctx.showOther;
		auto wrap = object_ptr<Ui::VerticalLayout>(ctx.container);
		SetupValidatePasswordSuggestion(controller, wrap.data(), showOther);
		return SectionBuilder::WidgetToAdd{ .widget = std::move(wrap) };
	});
}

class Main final : public Section<Main> {
public:
	Main(QWidget *parent, not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

	void fillTopBarMenu(const Ui::Menu::MenuCallback &addAction) override;
	void showFinished() override;

protected:
	void keyPressEvent(QKeyEvent *e) override;

private:
	void setupContent();

	QPointer<Ui::UserpicButton> _userpic;

};

Main::Main(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> Main::title() {
	return tr::lng_menu_settings();
}

void Main::fillTopBarMenu(const Ui::Menu::MenuCallback &addAction) {
	const auto &list = Core::App().domain().accounts();
	if (list.size() < Core::App().domain().maxAccounts()) {
		addAction(tr::lng_menu_add_account(tr::now), [=] {
			Core::App().domain().addActivated(MTP::Environment{});
		}, &st::menuIconAddAccount);
	}
	if (!controller()->session().supportMode()) {
		addAction(
			tr::lng_settings_information(tr::now),
			[=] { showOther(InformationId()); },
			&st::menuIconEdit);
	}
	const auto window = &controller()->window();
	const auto logout = addAction({
		.text = tr::lng_settings_logout(tr::now),
		.handler = [=] { window->showLogoutConfirmation(); },
		.icon = &st::menuIconLeaveAttention,
		.isAttention = true,
	});
	logout->setProperty("highlight-control-id", u"settings/log-out"_q);
}

void Main::keyPressEvent(QKeyEvent *e) {
	crl::on_main(this, [=, text = e->text()]{
		CodesFeedString(controller(), text);
	});
	return Section::keyPressEvent(e);
}

void Main::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	const auto window = controller();
	const auto session = &window->session();
	const auto cover = content->add(object_ptr<Cover>(
		content,
		window,
		session->user()));
	_userpic = cover->userpic();

	const SectionBuildMethod buildMethod = [](
			not_null<Ui::VerticalLayout*> container,
			not_null<Window::SessionController*> controller,
			Fn<void(Type)> showOther,
			rpl::producer<> showFinished) {
		auto &lifetime = container->lifetime();
		const auto highlights = lifetime.make_state<HighlightRegistry>();
		const auto isPaused = Window::PausedIn(
			controller,
			Window::GifPauseReason::Layer);
		auto builder = SectionBuilder(WidgetContext{
			.container = container,
			.controller = controller,
			.showOther = std::move(showOther),
			.isPaused = isPaused,
			.highlights = highlights,
		});
		builder.addDivider();
		builder.addSkip();
		BuildValidationSuggestions(builder);
		BuildSectionButtons(builder);
		builder.addSkip();
		BuildInterfaceScale(builder);
		BuildPremiumSection(builder);
		BuildHelpSection(builder);

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
	build(content, buildMethod);

	Ui::ResizeFitChild(this, content);

	session->api().cloudPassword().reload();
	session->api().reloadContactSignupSilent();
	session->api().sensitiveContent().reload();
	session->api().globalPrivacy().reload();
	session->api().premium().reload();
	session->data().cloudThemes().refresh();
	session->faqSuggestions().request();
}

void Main::showFinished() {
	controller()->checkHighlightControl(u"profile-photo"_q, _userpic.data(), {
		.margin = st::settingsPhotoHighlightMargin,
		.shape = HighlightShape::Ellipse,
	});
	const auto emojiId = u"profile-photo/use-emoji"_q;
	if (controller()->takeHighlightControlId(emojiId)) {
		if (const auto popupMenu = _userpic->showChangePhotoMenu()) {
			const auto menu = popupMenu->menu();
			for (const auto action : menu->actions()) {
				const auto controlId = "highlight-control-id";
				if (action->property(controlId).toString() == emojiId) {
					if (const auto item = menu->itemForAction(action)) {
						HighlightWidget(item);
					}
					break;
				}
			}
		}
	}
	Section<Main>::showFinished();
}

const auto kMeta = BuildHelper({
	.id = Main::Id(),
	.parentId = nullptr,
	.title = &tr::lng_menu_settings,
	.icon = &st::menuIconSettings,
}, [](SectionBuilder &builder) {
	builder.addDivider();
	builder.addSkip();

	builder.add(nullptr, [] {
		return SearchEntry{
			.id = u"main/profile-photo"_q,
			.title = tr::lng_profile_set_photo_for(tr::now),
			.keywords = { u"photo"_q, u"avatar"_q, u"picture"_q, u"profile"_q },
			.icon = { &st::menuIconProfile },
			.deeplink = u"tg://settings/profile-photo"_q,
		};
	});

	BuildValidationSuggestions(builder);
	BuildSectionButtons(builder);

	builder.addSkip();

	BuildInterfaceScale(builder);
	BuildPremiumSection(builder);
	BuildHelpSection(builder);
});

} // namespace

void SetupLanguageButton(
		not_null<Window::Controller*> window,
		not_null<Ui::VerticalLayout*> container) {
	const auto button = AddButtonWithLabel(
		container,
		tr::lng_settings_language(),
		rpl::single(
			Lang::GetInstance().id()
		) | rpl::then(
			Lang::GetInstance().idChanges()
		) | rpl::map([] { return Lang::GetInstance().nativeName(); }),
		st::settingsButton,
		{ &st::menuIconTranslate });
	const auto guard = Ui::CreateChild<base::binary_guard>(button.get());
	button->addClickHandler([=] {
		const auto m = button->clickModifiers();
		if ((m & Qt::ShiftModifier) && (m & Qt::AltModifier)) {
			Lang::CurrentCloudManager().switchToLanguage({ u"#custom"_q });
		} else {
			*guard = LanguageBox::Show(window->sessionController());
		}
	});
}

void SetupValidatePhoneNumberSuggestion(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		Fn<void(Type)> showOther) {
	if (!controller->session().promoSuggestions().current(
			kSugValidatePhone.utf8())) {
		return;
	}
	const auto mainWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto content = mainWrap->entity();
	Ui::AddSubsectionTitle(
		content,
		tr::lng_settings_suggestion_phone_number_title(
			lt_phone,
			rpl::single(
				Ui::FormatPhone(controller->session().user()->phone()))),
		QMargins(
			st::boxRowPadding.left()
				- st::defaultSubsectionTitlePadding.left(),
			0,
			0,
			0));
	const auto label = content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			tr::lng_settings_suggestion_phone_number_about(
				lt_link,
				tr::lng_collectible_learn_more(tr::url(
					tr::lng_settings_suggestion_phone_number_about_link(
						tr::now))),
				tr::marked),
			st::boxLabel),
		st::boxRowPadding);
	label->setClickHandlerFilter([=, weak = base::make_weak(controller)](
			const auto &...) {
		UrlClickHandler::Open(
			tr::lng_settings_suggestion_phone_number_about_link(tr::now),
			QVariant::fromValue(ClickHandlerContext{
				.sessionWindow = weak,
			}));
		return false;
	});

	Ui::AddSkip(content);
	Ui::AddSkip(content);

	const auto wrap = content->add(
		object_ptr<Ui::FixedHeightWidget>(
			content,
			st::inviteLinkButton.height),
		st::inviteLinkButtonsPadding);
	const auto yes = Ui::CreateChild<Ui::RoundButton>(
		wrap,
		tr::lng_box_yes(),
		st::inviteLinkButton);
	yes->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	yes->setClickedCallback([=] {
		controller->session().promoSuggestions().dismiss(
			kSugValidatePhone.utf8());
		mainWrap->toggle(false, anim::type::normal);
	});
	const auto no = Ui::CreateChild<Ui::RoundButton>(
		wrap,
		tr::lng_box_no(),
		st::inviteLinkButton);
	no->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	no->setClickedCallback([=] {
		const auto sharedLabel = std::make_shared<base::weak_qptr<Ui::FlatLabel>>();
		const auto height = st::boxLabel.style.font->height;
		const auto customEmojiFactory = [=](
			QStringView data,
			const Ui::Text::MarkedContext &context
		) -> std::unique_ptr<Ui::Text::CustomEmoji> {
			auto repaint = [=] {
				if (*sharedLabel) {
					(*sharedLabel)->update();
				}
			};
			return Lottie::MakeEmoji(
				{ .name = u"change_number"_q, .sizeOverride = Size(height) },
				std::move(repaint));
		};

		controller->uiShow()->show(Box([=](not_null<Ui::GenericBox*> box) {
			box->addButton(tr::lng_box_ok(), [=] { box->closeBox(); });
			*sharedLabel = box->verticalLayout()->add(
				object_ptr<Ui::FlatLabel>(
					box->verticalLayout(),
					tr::lng_settings_suggestion_phone_number_change(
						lt_emoji,
						rpl::single(Ui::Text::SingleCustomEmoji(u"@"_q)),
						tr::marked),
					st::boxLabel,
					st::defaultPopupMenu,
					Ui::Text::MarkedContext{
						.customEmojiFactory = customEmojiFactory,
					}),
				st::boxPadding);
		}));
	});

	wrap->widthValue() | rpl::on_next([=](int width) {
		const auto buttonWidth = (width - st::inviteLinkButtonsSkip) / 2;
		yes->setFullWidth(buttonWidth);
		no->setFullWidth(buttonWidth);
		yes->moveToLeft(0, 0, width);
		no->moveToRight(0, 0, width);
	}, wrap->lifetime());
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);
}

void SetupValidatePasswordSuggestion(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		Fn<void(Type)> showOther) {
	if (!controller->session().promoSuggestions().current(
			Data::PromoSuggestions::SugValidatePassword())
		|| controller->session().promoSuggestions().current(
			kSugValidatePhone.utf8())) {
		return;
	}
	const auto mainWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto content = mainWrap->entity();
	Ui::AddSubsectionTitle(
		content,
		tr::lng_settings_suggestion_password_title(),
		QMargins(
			st::boxRowPadding.left()
				- st::defaultSubsectionTitlePadding.left(),
			0,
			0,
			0));
	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			tr::lng_settings_suggestion_password_about(),
			st::boxLabel),
		st::boxRowPadding);

	Ui::AddSkip(content);
	Ui::AddSkip(content);

	const auto wrap = content->add(
		object_ptr<Ui::FixedHeightWidget>(
			content,
			st::inviteLinkButton.height),
		st::inviteLinkButtonsPadding);
	const auto yes = Ui::CreateChild<Ui::RoundButton>(
		wrap,
		tr::lng_settings_suggestion_password_yes(),
		st::inviteLinkButton);
	yes->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	yes->setClickedCallback([=] {
		controller->session().promoSuggestions().dismiss(
			Data::PromoSuggestions::SugValidatePassword());
		mainWrap->toggle(false, anim::type::normal);
	});
	const auto no = Ui::CreateChild<Ui::RoundButton>(
		wrap,
		tr::lng_settings_suggestion_password_no(),
		st::inviteLinkButton);
	no->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	no->setClickedCallback([=] {
		showOther(Settings::CloudPasswordSuggestionInputId());
	});

	wrap->widthValue() | rpl::on_next([=](int width) {
		const auto buttonWidth = (width - st::inviteLinkButtonsSkip) / 2;
		yes->setFullWidth(buttonWidth);
		no->setFullWidth(buttonWidth);
		yes->moveToLeft(0, 0, width);
		no->moveToRight(0, 0, width);
	}, wrap->lifetime());
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);
}

bool HasInterfaceScale() {
	return true;
}

void SetupInterfaceScale(
		not_null<Window::Controller*> window,
		not_null<Ui::VerticalLayout*> container,
		bool icon) {
	if (!HasInterfaceScale()) {
		return;
	}

	const auto toggled = Ui::CreateChild<rpl::event_stream<bool>>(
		container.get());

	const auto switched = (cConfigScale() == style::kScaleAuto);
	const auto button = AddButtonWithIcon(
		container,
		tr::lng_settings_default_scale(),
		icon ? st::settingsButton : st::settingsButtonNoIcon,
		{ icon ? &st::menuIconShowInChat : nullptr }
	)->toggleOn(toggled->events_starting_with_copy(switched));

	const auto ratio = style::DevicePixelRatio();
	const auto scaleMin = style::kScaleMin;
	const auto scaleMax = style::MaxScaleForRatio(ratio);
	const auto scaleConfig = cConfigScale();
	const auto step = 5;
	Assert(!((scaleMax - scaleMin) % step));
	auto values = std::vector<int>();
	for (auto i = scaleMin; i != scaleMax; i += step) {
		values.push_back(i);
		if (scaleConfig > i && scaleConfig < i + step) {
			values.push_back(scaleConfig);
		}
	}
	values.push_back(scaleMax);
	const auto valuesCount = int(values.size());

	const auto valueFromScale = [=](int scale) {
		scale = cEvalScale(scale);
		auto result = 0;
		for (const auto value : values) {
			if (scale == value) {
				break;
			}
			++result;
		}
		return ((result == valuesCount) ? (result - 1) : result)
			/ float64(valuesCount - 1);
	};
	auto sliderWithLabel = MakeSliderWithLabel(
		container,
		st::settingsScale,
		st::settingsScaleLabel,
		st::normalFont->spacew * 2,
		st::settingsScaleLabel.style.font->width("300%"),
		true);
	container->add(
		std::move(sliderWithLabel.widget),
		icon ? st::settingsScalePadding : st::settingsBigScalePadding);
	const auto slider = sliderWithLabel.slider;
	const auto label = sliderWithLabel.label;
	slider->setAccessibleName(tr::lng_settings_scale(tr::now));

	const auto updateLabel = [=](int scale) {
		const auto labelText = [&](int scale) {
			if constexpr (Platform::IsMac()) {
				return QString::number(scale) + '%';
			} else {
				const auto handle = window->widget()->windowHandle();
				const auto ratio = handle->devicePixelRatio();
				return QString::number(base::SafeRound(scale * ratio)) + '%';
			}
		};
		label->setText(labelText(cEvalScale(scale)));
	};
	updateLabel(cConfigScale());

	const auto inSetScale = container->lifetime().make_state<bool>();
	const auto setScale = [=](int scale, const auto &repeatSetScale) -> void {
		if (*inSetScale) {
			return;
		}
		*inSetScale = true;
		const auto guard = gsl::finally([=] { *inSetScale = false; });

		updateLabel(scale);
		toggled->fire(scale == style::kScaleAuto);
		slider->setValue(valueFromScale(scale));
		if (cEvalScale(scale) != cEvalScale(cConfigScale())) {
			const auto confirmed = crl::guard(button, [=] {
				cSetConfigScale(scale);
				Local::writeSettings();
				Core::Restart();
			});
			const auto cancelled = crl::guard(button, [=](Fn<void()> close) {
				base::call_delayed(
					st::defaultSettingsSlider.duration,
					button,
					[=] { repeatSetScale(cConfigScale(), repeatSetScale); });
				close();
			});
			window->show(Ui::MakeConfirmBox({
				.text = tr::lng_settings_need_restart(),
				.confirmed = confirmed,
				.cancelled = cancelled,
				.confirmText = tr::lng_settings_restart_now(),
			}));
		} else if (scale != cConfigScale()) {
			cSetConfigScale(scale);
			Local::writeSettings();
		}
	};

	const auto shown = container->lifetime().make_state<bool>();
	const auto togglePreview = SetupScalePreview(window, slider);
	const auto toggleForScale = [=](int scale) {
		scale = cEvalScale(scale);
		const auto show = *shown
			? ScalePreviewShow::Update
			: ScalePreviewShow::Show;
		*shown = true;
		for (auto i = 0; i != valuesCount; ++i) {
			if (values[i] <= scale
				&& (i + 1 == valuesCount || values[i + 1] > scale)) {
				const auto x = (slider->width() * i) / (valuesCount - 1);
				togglePreview(show, scale, x);
				return;
			}
		}
		togglePreview(show, scale, slider->width() / 2);
	};
	const auto toggleHidePreview = [=] {
		togglePreview(ScalePreviewShow::Hide, 0, 0);
		*shown = false;
	};

	slider->setPseudoDiscrete(
		valuesCount,
		[=](int index) { return values[index]; },
		cConfigScale(),
		[=](int scale) { updateLabel(scale); toggleForScale(scale); },
		[=](int scale) { toggleHidePreview(); setScale(scale, setScale); });

	button->toggledValue(
	) | rpl::map([](bool checked) {
		return checked ? style::kScaleAuto : cEvalScale(cConfigScale());
	}) | rpl::on_next([=](int scale) {
		setScale(scale, setScale);
	}, button->lifetime());

	if (!icon) {
		Ui::AddSkip(container, st::settingsThumbSkip);
	}
}

Type MainId() {
	return Main::Id();
}

void OpenFaq(base::weak_ptr<Window::SessionController> weak) {
	UrlClickHandler::Open(
		tr::lng_settings_faq_link(tr::now),
		QVariant::fromValue(ClickHandlerContext{
			.sessionWindow = weak,
		}));
}

void OpenAskQuestionConfirm(not_null<Window::SessionController*> window) {
	const auto requestId = std::make_shared<mtpRequestId>();
	const auto sure = [=](Fn<void()> close) {
		if (*requestId) {
			return;
		}
		*requestId = window->session().api().request(
			MTPhelp_GetSupport()
		).done(crl::guard(window, [=](const MTPhelp_Support &result) {
			*requestId = 0;
			result.match([&](const MTPDhelp_support &data) {
				auto &owner = window->session().data();
				if (const auto user = owner.processUser(data.vuser())) {
					window->showPeerHistory(user);
				}
			});
			close();
		})).fail([=] {
			*requestId = 0;
			close();
		}).send();
	};
	window->show(Ui::MakeConfirmBox({
		.text = tr::lng_settings_ask_sure(),
		.confirmed = sure,
		.cancelled = [=](Fn<void()> close) {
			OpenFaq(window);
			close();
		},
		.confirmText = tr::lng_settings_ask_ok(),
		.cancelText = tr::lng_settings_faq_button(),
		.strictCancel = true,
	}));
}

} // namespace Settings
