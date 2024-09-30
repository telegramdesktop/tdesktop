/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_main.h"

#include "api/api_credits.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "settings/settings_advanced.h"
#include "settings/settings_business.h"
#include "settings/settings_calls.h"
#include "settings/settings_chat.h"
#include "settings/settings_codes.h"
#include "settings/settings_credits.h"
#include "settings/settings_folders.h"
#include "settings/settings_information.h"
#include "settings/settings_notifications.h"
#include "settings/settings_power_saving.h"
#include "settings/settings_premium.h"
#include "settings/settings_privacy_security.h"
#include "settings/settings_scale_preview.h"
#include "boxes/language_box.h"
#include "boxes/username_box.h"
#include "boxes/about_box.h"
#include "boxes/star_gift_box.h"
#include "ui/basic_click_handlers.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/userpic_button.h"
#include "ui/effects/premium_graphics.h"
#include "ui/effects/premium_top_bar.h" // Ui::Premium::ColorizedSvg.
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/new_badges.h"
#include "ui/rect.h"
#include "ui/vertical_list.h"
#include "info/profile/info_profile_badge.h"
#include "info/profile/info_profile_emoji_status_panel.h"
#include "data/components/credits.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "data/data_cloud_themes.h"
#include "data/data_chat_filters.h"
#include "lang/lang_cloud_manager.h"
#include "lang/lang_instance.h"
#include "storage/localstorage.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_app_config.h"
#include "apiwrap.h"
#include "api/api_peer_photo.h"
#include "api/api_cloud_password.h"
#include "api/api_global_privacy.h"
#include "api/api_sensitive_content.h"
#include "api/api_premium.h"
#include "info/profile/info_profile_values.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "base/call_delayed.h"
#include "base/platform/base_platform_info.h"
#include "styles/style_settings.h"
#include "styles/style_info.h"
#include "styles/style_menu_icons.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>
#include <QtGui/QWindow>

namespace Settings {
namespace {

class Cover final : public Ui::FixedHeightWidget {
public:
	Cover(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<UserData*> user);
	~Cover();

private:
	void setupChildGeometry();
	void initViewers();
	void refreshStatusText();
	void refreshNameGeometry(int newWidth);
	void refreshPhoneGeometry(int newWidth);
	void refreshUsernameGeometry(int newWidth);

	const not_null<Window::SessionController*> _controller;
	const not_null<UserData*> _user;
	Info::Profile::EmojiStatusPanel _emojiStatusPanel;
	Info::Profile::Badge _badge;

	object_ptr<Ui::UserpicButton> _userpic;
	object_ptr<Ui::FlatLabel> _name = { nullptr };
	object_ptr<Ui::FlatLabel> _phone = { nullptr };
	object_ptr<Ui::FlatLabel> _username = { nullptr };

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
	user,
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
	_badge.updated() | rpl::start_with_next([=] {
		refreshNameGeometry(width());
	}, _name->lifetime());
}

Cover::~Cover() = default;

void Cover::setupChildGeometry() {
	using namespace rpl::mappers;
	widthValue(
	) | rpl::start_with_next([=](int newWidth) {
		_userpic->moveToLeft(
			st::settingsPhotoLeft,
			st::settingsPhotoTop,
			newWidth);
		refreshNameGeometry(newWidth);
		refreshPhoneGeometry(newWidth);
		refreshUsernameGeometry(newWidth);
	}, lifetime());
}

void Cover::initViewers() {
	Info::Profile::NameValue(
		_user
	) | rpl::start_with_next([=](const QString &name) {
		_name->setText(name);
		refreshNameGeometry(width());
	}, lifetime());

	Info::Profile::PhoneValue(
		_user
	) | rpl::start_with_next([=](const TextWithEntities &value) {
		_phone->setText(value.text);
		refreshPhoneGeometry(width());
	}, lifetime());

	Info::Profile::UsernameValue(
		_user
	) | rpl::start_with_next([=](const TextWithEntities &value) {
		_username->setMarkedText(Ui::Text::Link(value.text.isEmpty()
			? tr::lng_settings_username_add(tr::now)
			: value.text));
		refreshUsernameGeometry(width());
	}, lifetime());

	_username->overrideLinkClickHandler([=] {
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
	auto nameWidth = newWidth
		- nameLeft
		- st::infoProfileCover.rightSkip;
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

[[nodiscard]] not_null<Ui::SettingsButton*> AddPremiumStar(
		not_null<Ui::SettingsButton*> button,
		bool credits) {
	const auto stops = credits
		? Ui::Premium::CreditsIconGradientStops()
		: Ui::Premium::ButtonGradientStops();

	const auto ministarsContainer = Ui::CreateChild<Ui::RpWidget>(button);
	const auto &buttonSt = button->st();
	const auto fullHeight = buttonSt.height
		+ rect::m::sum::v(buttonSt.padding);
	using MiniStars = Ui::Premium::ColoredMiniStars;
	const auto ministars = button->lifetime().make_state<MiniStars>(
		ministarsContainer,
		false);
	ministars->setColorOverride(stops);

	ministarsContainer->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(ministarsContainer);
		{
			constexpr auto kScale = 0.35;
			const auto r = ministarsContainer->rect();
			p.translate(r.center());
			p.scale(kScale, kScale);
			p.translate(-r.center());
		}
		ministars->paint(p);
	}, ministarsContainer->lifetime());

	const auto badge = Ui::CreateChild<Ui::RpWidget>(button.get());

	auto star = [&] {
		const auto factor = style::DevicePixelRatio();
		const auto size = Size(st::settingsButtonNoIcon.style.font->ascent);
		auto image = QImage(
			size * factor,
			QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(factor);
		image.fill(Qt::transparent);
		{
			auto p = QPainter(&image);
			auto star = QSvgRenderer(Ui::Premium::ColorizedSvg(stops));
			star.render(&p, Rect(size));
		}
		return image;
	}();
	badge->resize(star.size() / style::DevicePixelRatio());
	badge->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(badge);
		p.drawImage(0, 0, star);
	}, badge->lifetime());

	button->sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		badge->moveToLeft(
			button->st().iconLeft
				+ (st::menuIconShop.width() - badge->width()) / 2,
			(s.height() - badge->height()) / 2);
		ministarsContainer->moveToLeft(
			badge->x() - (fullHeight - badge->height()) / 2,
			0);
	}, badge->lifetime());

	ministarsContainer->resize(fullHeight, fullHeight);
	ministars->setCenter(ministarsContainer->rect());

	return button;
}

} // namespace

void SetupPowerSavingButton(
		not_null<Window::Controller*> window,
		not_null<Ui::VerticalLayout*> container) {
	const auto button = AddButtonWithIcon(
		container,
		tr::lng_settings_power_menu(),
		st::settingsButton,
		{ &st::menuIconPowerUsage });
	button->setClickedCallback([=] {
		window->show(Box(PowerSavingBox));
	});
}

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

void SetupSections(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		Fn<void(Type)> showOther) {
	Ui::AddDivider(container);
	Ui::AddSkip(container);

	const auto addSection = [&](
			rpl::producer<QString> label,
			Type type,
			IconDescriptor &&descriptor) {
		AddButtonWithIcon(
			container,
			std::move(label),
			st::settingsButton,
			std::move(descriptor)
		)->addClickHandler([=] {
			showOther(type);
		});
	};
	if (controller->session().supportMode()) {
		SetupSupport(controller, container);

		Ui::AddDivider(container);
		Ui::AddSkip(container);
	} else {
		addSection(
			tr::lng_settings_my_account(),
			Information::Id(),
			{ &st::menuIconProfile });
	}

	addSection(
		tr::lng_settings_section_notify(),
		Notifications::Id(),
		{ &st::menuIconNotifications });
	addSection(
		tr::lng_settings_section_privacy(),
		PrivacySecurity::Id(),
		{ &st::menuIconLock });
	addSection(
		tr::lng_settings_section_chat_settings(),
		Chat::Id(),
		{ &st::menuIconChatBubble });

	const auto preload = [=] {
		controller->session().data().chatsFilters().requestSuggested();
	};
	const auto account = &controller->session().account();
	const auto slided = container->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			container,
			CreateButtonWithIcon(
				container,
				tr::lng_settings_section_filters(),
				st::settingsButton,
				{ &st::menuIconShowInFolder }))
	)->setDuration(0);
	if (controller->session().data().chatsFilters().has()
		|| controller->session().settings().dialogsFiltersEnabled()) {
		slided->show(anim::type::instant);
		preload();
	} else {
		const auto enabled = [=] {
			const auto result = account->appConfig().get<bool>(
				u"dialog_filters_enabled"_q,
				false);
			if (result) {
				preload();
			}
			return result;
		};
		const auto preloadIfEnabled = [=](bool enabled) {
			if (enabled) {
				preload();
			}
		};
		slided->toggleOn(
			rpl::single(rpl::empty) | rpl::then(
				account->appConfig().refreshed()
			) | rpl::map(
				enabled
			) | rpl::before_next(preloadIfEnabled));
	}
	slided->entity()->setClickedCallback([=] {
		showOther(Folders::Id());
	});

	addSection(
		tr::lng_settings_advanced(),
		Advanced::Id(),
		{ &st::menuIconManage });
	addSection(
		tr::lng_settings_section_devices(),
		Calls::Id(),
		{ &st::menuIconUnmute });

	SetupPowerSavingButton(&controller->window(), container);
	SetupLanguageButton(&controller->window(), container);

	Ui::AddSkip(container);
}

void SetupPremium(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		Fn<void(Type)> showOther) {
	if (!controller->session().premiumPossible()) {
		return;
	}
	Ui::AddDivider(container);
	Ui::AddSkip(container);

	AddPremiumStar(
		AddButtonWithIcon(
			container,
			tr::lng_premium_summary_title(),
			st::settingsButton),
		false
	)->addClickHandler([=] {
		controller->setPremiumRef("settings");
		showOther(PremiumId());
	});
	{
		controller->session().credits().load();
		AddPremiumStar(
			AddButtonWithLabel(
				container,
				tr::lng_settings_credits(),
				controller->session().credits().balanceValue(
				) | rpl::map([=](uint64 c) {
					return c ? Lang::FormatCountToShort(c).string : QString{};
				}),
				st::settingsButton),
			true
		)->addClickHandler([=] {
			controller->setPremiumRef("settings");
			showOther(CreditsId());
		});
	}
	const auto button = AddButtonWithIcon(
		container,
		tr::lng_business_title(),
		st::settingsButton,
		{ .icon = &st::menuIconShop });
	button->addClickHandler([=] {
		showOther(BusinessId());
	});
	Ui::NewBadge::AddToRight(button);

	if (controller->session().premiumCanBuy()) {
		const auto button = AddButtonWithIcon(
			container,
			tr::lng_settings_gift_premium(),
			st::settingsButton,
			{ .icon = &st::menuIconGiftPremium }
		);
		button->addClickHandler([=] {
			Ui::ChooseStarGiftRecipient(controller);
		});
	}
	Ui::AddSkip(container);
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
	}) | rpl::start_with_next([=](int scale) {
		setScale(scale, setScale);
	}, button->lifetime());

	if (!icon) {
		Ui::AddSkip(container, st::settingsThumbSkip);
	}
}

void SetupHelp(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	Ui::AddDivider(container);
	Ui::AddSkip(container);

	AddButtonWithIcon(
		container,
		tr::lng_settings_faq(),
		st::settingsButton,
		{ &st::menuIconFaq }
	)->addClickHandler([=] {
		OpenFaq(controller);
	});

	AddButtonWithIcon(
		container,
		tr::lng_settings_features(),
		st::settingsButton,
		{ &st::menuIconEmojiObjects }
	)->setClickedCallback([=] {
		UrlClickHandler::Open(tr::lng_telegram_features_url(tr::now));
	});

	const auto button = AddButtonWithIcon(
		container,
		tr::lng_settings_ask_question(),
		st::settingsButton,
		{ &st::menuIconDiscussion });
	const auto requestId = button->lifetime().make_state<mtpRequestId>();
	button->lifetime().add([=] {
		if (*requestId) {
			controller->session().api().request(*requestId).cancel();
		}
	});
	button->addClickHandler([=] {
		const auto sure = crl::guard(button, [=] {
			if (*requestId) {
				return;
			}
			*requestId = controller->session().api().request(
				MTPhelp_GetSupport()
			).done([=](const MTPhelp_Support &result) {
				*requestId = 0;
				result.match([&](const MTPDhelp_support &data) {
					auto &owner = controller->session().data();
					if (const auto user = owner.processUser(data.vuser())) {
						controller->showPeerHistory(user);
					}
				});
			}).fail([=] {
				*requestId = 0;
			}).send();
		});
		auto box = Ui::MakeConfirmBox({
			.text = tr::lng_settings_ask_sure(),
			.confirmed = sure,
			.cancelled = [=](Fn<void()> close) {
				OpenFaq(controller);
				close();
			},
			.confirmText = tr::lng_settings_ask_ok(),
			.cancelText = tr::lng_settings_faq_button(),
			.strictCancel = true,
		});
		controller->show(std::move(box));
	});
}

Main::Main(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent)
, _controller(controller) {
	setupContent(controller);
	_controller->session().api().premium().reload();
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
	if (!_controller->session().supportMode()) {
		addAction(
			tr::lng_settings_information(tr::now),
			[=] { showOther(Information::Id()); },
			&st::menuIconInfo);
	}
	const auto window = &_controller->window();
	addAction({
		.text = tr::lng_settings_logout(tr::now),
		.handler = [=] { window->showLogoutConfirmation(); },
		.icon = &st::menuIconLeaveAttention,
		.isAttention = true,
	});
}

void Main::keyPressEvent(QKeyEvent *e) {
	crl::on_main(this, [=, text = e->text()]{
		CodesFeedString(_controller, text);
	});
	return Section::keyPressEvent(e);
}

void Main::setupContent(not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	content->add(object_ptr<Cover>(
		content,
		controller,
		controller->session().user()));

	SetupSections(controller, content, showOtherMethod());
	if (HasInterfaceScale()) {
		Ui::AddDivider(content);
		Ui::AddSkip(content);
		SetupInterfaceScale(&controller->window(), content);
		Ui::AddSkip(content);
	}
	SetupPremium(controller, content, showOtherMethod());
	SetupHelp(controller, content);

	Ui::ResizeFitChild(this, content);

	// If we load this in advance it won't jump when we open its' section.
	controller->session().api().cloudPassword().reload();
	controller->session().api().reloadContactSignupSilent();
	controller->session().api().sensitiveContent().reload();
	controller->session().api().globalPrivacy().reload();
	controller->session().data().cloudThemes().refresh();
}

void OpenFaq(base::weak_ptr<Window::SessionController> weak) {
	UrlClickHandler::Open(
		tr::lng_settings_faq_link(tr::now),
		QVariant::fromValue(ClickHandlerContext{
			.sessionWindow = weak,
		}));
}

} // namespace Settings
