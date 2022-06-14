/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_main.h"

#include "settings/settings_common.h"
#include "settings/settings_codes.h"
#include "settings/settings_chat.h"
#include "settings/settings_information.h"
#include "settings/settings_notifications.h"
#include "settings/settings_privacy_security.h"
#include "settings/settings_advanced.h"
#include "settings/settings_folders.h"
#include "settings/settings_calls.h"
#include "settings/settings_premium.h"
#include "boxes/language_box.h"
#include "boxes/username_box.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/about_box.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/buttons.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/special_buttons.h"
#include "info/profile/info_profile_cover.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "data/data_cloud_themes.h"
#include "data/data_chat_filters.h"
#include "data/data_peer_values.h" // Data::AmPremiumValue
#include "lang/lang_keys.h"
#include "lang/lang_instance.h"
#include "storage/localstorage.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "main/main_account.h"
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
#include "core/click_handler_types.h"
#include "core/application.h"
#include "base/call_delayed.h"
#include "base/platform/base_platform_info.h"
#include "facades.h"
#include "styles/style_settings.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

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

	object_ptr<Ui::UserpicButton> _userpic;
	object_ptr<Ui::FlatLabel> _name = { nullptr };
	object_ptr<Ui::FlatLabel> _phone = { nullptr };
	object_ptr<Ui::FlatLabel> _username = { nullptr };
	object_ptr<Ui::RpWidget> _badge = { nullptr };

};

Cover::Cover(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<UserData*> user)
: FixedHeightWidget(
	parent,
	st::settingsPhotoTop
		+ st::infoProfilePhoto.size.height()
		+ st::settingsPhotoBottom)
, _controller(controller)
, _user(user)
, _userpic(
	this,
	controller,
	_user,
	Ui::UserpicButton::Role::OpenPhoto,
	st::infoProfilePhoto)
, _name(this, st::infoProfileNameLabel)
, _phone(this, st::defaultFlatLabel)
, _username(this, st::infoProfileMegagroupStatusLabel) {
	_user->updateFull();

	_name->setSelectable(true);
	_name->setContextCopyText(tr::lng_profile_copy_fullname(tr::now));

	_phone->setSelectable(true);
	_phone->setContextCopyText(tr::lng_profile_copy_phone(tr::now));

	initViewers();
	setupChildGeometry();

	_userpic->switchChangePhotoOverlay(_user->isSelf());
	_userpic->uploadPhotoRequests(
	) | rpl::start_with_next([=] {
		_user->session().api().peerPhoto().upload(
			_user,
			_userpic->takeResultImage());
	}, _userpic->lifetime());

	Data::AmPremiumValue(
		&controller->session()
	) | rpl::start_with_next([=](bool hasPremium) {
		if (hasPremium && !_badge) {
			const auto icon = &st::infoPremiumStar;
			_badge.create(this);
			_badge->show();
			_badge->resize(icon->size());
			_badge->paintRequest(
			) | rpl::start_with_next([icon, check = _badge.data()] {
				Painter p(check);
				icon->paint(p, 0, 0, check->width());
			}, _badge->lifetime());
		} else if (!hasPremium && _badge) {
			_badge.destroy();
		}
	}, lifetime());
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
	) | rpl::start_with_next([=](const TextWithEntities &value) {
		_name->setText(value.text);
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

	_username->setClickHandlerFilter([=](auto&&...) {
		const auto username = _user->userName();
		if (username.isEmpty()) {
			_controller->show(Box<UsernameBox>(&_user->session()));
		} else {
			QGuiApplication::clipboard()->setText(
				_user->session().createInternalLinkFull(username));
			Ui::Toast::Show(
				Window::Show(_controller).toastParent(),
				tr::lng_username_copied(tr::now));
		}
		return false;
	});
}

void Cover::refreshNameGeometry(int newWidth) {
	const auto nameLeft = st::settingsNameLeft;
	const auto nameTop = st::settingsNameTop;
	const auto nameWidth = newWidth
		- nameLeft
		- st::infoProfileNameRight
		- (!_badge ? 0 : _badge->width() + st::infoVerifiedCheckPosition.x());
	_name->resizeToNaturalWidth(nameWidth);
	_name->moveToLeft(nameLeft, nameTop, newWidth);

	if (_badge) {
		const auto &pos = st::infoVerifiedCheckPosition;
		const auto badgeLeft = nameLeft + _name->width() + pos.x();
		const auto badgeTop = nameTop + pos.y();
		_badge->moveToLeft(badgeLeft, badgeTop, newWidth);
	}
}

void Cover::refreshPhoneGeometry(int newWidth) {
	const auto phoneLeft = st::settingsPhoneLeft;
	const auto phoneTop = st::settingsPhoneTop;
	const auto phoneWidth = newWidth - phoneLeft - st::infoProfileNameRight;
	_phone->resizeToWidth(phoneWidth);
	_phone->moveToLeft(phoneLeft, phoneTop, newWidth);
}

void Cover::refreshUsernameGeometry(int newWidth) {
	const auto usernameLeft = st::settingsUsernameLeft;
	const auto usernameTop = st::settingsUsernameTop;
	const auto usernameRight = st::infoProfileNameRight;
	const auto usernameWidth = newWidth - usernameLeft - usernameRight;
	_username->resizeToWidth(usernameWidth);
	_username->moveToLeft(usernameLeft, usernameTop, newWidth);
}

} // namespace

void SetupLanguageButton(
		not_null<Ui::VerticalLayout*> container,
		bool icon) {
	const auto button = AddButtonWithLabel(
		container,
		tr::lng_settings_language(),
		rpl::single(
			Lang::GetInstance().id()
		) | rpl::then(
			Lang::GetInstance().idChanges()
		) | rpl::map([] { return Lang::GetInstance().nativeName(); }),
		icon ? st::settingsButton : st::settingsButtonNoIcon,
		{ icon ? &st::settingsIconLanguage : nullptr, kIconDarkOrange });
	const auto guard = Ui::CreateChild<base::binary_guard>(button.get());
	button->addClickHandler([=] {
		const auto m = button->clickModifiers();
		if ((m & Qt::ShiftModifier) && (m & Qt::AltModifier)) {
			Lang::CurrentCloudManager().switchToLanguage({ qsl("#custom") });
		} else {
			*guard = LanguageBox::Show();
		}
	});
}

void SetupSections(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		Fn<void(Type)> showOther) {
	AddDivider(container);
	AddSkip(container);

	const auto addSection = [&](
			rpl::producer<QString> label,
			Type type,
			IconDescriptor &&descriptor) {
		AddButton(
			container,
			std::move(label),
			st::settingsButton,
			std::move(descriptor)
		)->addClickHandler([=] {
			if (type == PremiumId()) {
				controller->setPremiumRef("settings");
			}
			showOther(type);
		});
	};
	if (controller->session().supportMode()) {
		SetupSupport(controller, container);

		AddDivider(container);
		AddSkip(container);
	} else {
		addSection(
			tr::lng_settings_information(),
			Information::Id(),
			{ &st::settingsIconAccount, kIconLightOrange });
	}
	addSection(
		tr::lng_settings_section_notify(),
		Notifications::Id(),
		{ &st::settingsIconNotifications, kIconRed });
	addSection(
		tr::lng_settings_section_privacy(),
		PrivacySecurity::Id(),
		{ &st::settingsIconLock, kIconGreen });
	addSection(
		tr::lng_settings_section_chat_settings(),
		Chat::Id(),
		{ &st::settingsIconChat, kIconLightBlue });

	const auto preload = [=] {
		controller->session().data().chatsFilters().requestSuggested();
	};
	const auto account = &controller->session().account();
	const auto slided = container->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			container,
			CreateButton(
				container,
				tr::lng_settings_section_filters(),
				st::settingsButton,
				{ &st::settingsIconFolders, kIconDarkBlue }))
	)->setDuration(0);
	if (controller->session().data().chatsFilters().has()
		|| controller->session().settings().dialogsFiltersEnabled()) {
		slided->show(anim::type::instant);
		preload();
	} else {
		const auto enabled = [=] {
			const auto result = account->appConfig().get<bool>(
				"dialog_filters_enabled",
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
		{ &st::settingsIconGeneral, kIconPurple });
	addSection(
		tr::lng_settings_section_call_settings(),
		Calls::Id(),
		{ &st::settingsIconCalls, kIconGreen });

	SetupLanguageButton(container);

	if (controller->session().premiumPossible()) {
		AddSkip(container);
		AddDivider(container);
		AddSkip(container);

		const auto icon = &st::settingsPremiumIconStar;
		auto gradient = QLinearGradient(
			0,
			icon->height(),
			icon->width() + icon->width() / 3,
			0 - icon->height() / 3);
		gradient.setStops(QGradientStops{
			{ 0.0, st::premiumButtonBg1->c },
			{ 1.0, st::premiumButtonBg3->c },
		});
		addSection(
			tr::lng_premium_summary_title(),
			PremiumId(),
			{ .icon = icon, .backgroundBrush = QBrush(gradient) });
	}

	AddSkip(container);
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
	const auto button = AddButton(
		container,
		tr::lng_settings_default_scale(),
		icon ? st::settingsButton : st::settingsButtonNoIcon,
		{ icon ? &st::settingsIconInterfaceScale : nullptr, kIconLightOrange }
	)->toggleOn(toggled->events_starting_with_copy(switched));

	const auto slider = container->add(
		object_ptr<Ui::SettingsSlider>(container, st::settingsSlider),
		icon ? st::settingsScalePadding : st::settingsBigScalePadding);

	static const auto ScaleValues = [&] {
		auto values = (cIntRetinaFactor() > 1)
			? std::vector<int>{ 100, 110, 120, 130, 140, 150 }
			: std::vector<int>{ 100, 125, 150, 200, 250, 300 };
		if (cConfigScale() == style::kScaleAuto) {
			return values;
		}
		if (ranges::find(values, cConfigScale()) == end(values)) {
			values.push_back(cConfigScale());
		}
		return values;
	}();

	const auto sectionFromScale = [](int scale) {
		scale = cEvalScale(scale);
		auto result = 0;
		for (const auto value : ScaleValues) {
			if (scale == value) {
				break;
			}
			++result;
		}
		return (result == ScaleValues.size()) ? (result - 1) : result;
	};
	const auto inSetScale = container->lifetime().make_state<bool>();
	const auto setScale = [=](int scale, const auto &repeatSetScale) -> void {
		if (*inSetScale) {
			return;
		}
		*inSetScale = true;
		const auto guard = gsl::finally([=] { *inSetScale = false; });

		toggled->fire(scale == style::kScaleAuto);
		slider->setActiveSection(sectionFromScale(scale));
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

	const auto label = [](int scale) {
		if constexpr (Platform::IsMac()) {
			return QString::number(scale) + '%';
		} else {
			return QString::number(scale * cIntRetinaFactor()) + '%';
		}
	};
	const auto scaleByIndex = [](int index) {
		return *(ScaleValues.begin() + index);
	};

	for (const auto value : ScaleValues) {
		slider->addSection(label(value));
	}
	slider->setActiveSectionFast(sectionFromScale(cConfigScale()));
	slider->sectionActivated(
	) | rpl::map([=](int section) {
		return scaleByIndex(section);
	}) | rpl::filter([=](int scale) {
		return cEvalScale(scale) != cEvalScale(cConfigScale());
	}) | rpl::start_with_next([=](int scale) {
		setScale(
			(scale == cScreenScale()) ? style::kScaleAuto : scale,
			setScale);
	}, slider->lifetime());

	button->toggledValue(
	) | rpl::map([](bool checked) {
		return checked ? style::kScaleAuto : cEvalScale(cConfigScale());
	}) | rpl::start_with_next([=](int scale) {
		setScale(scale, setScale);
	}, button->lifetime());
}

void OpenFaq() {
	UrlClickHandler::Open(telegramFaqLink());
}

void SetupFaq(not_null<Ui::VerticalLayout*> container, bool icon) {
	AddButton(
		container,
		tr::lng_settings_faq(),
		icon ? st::settingsButton : st::settingsButtonNoIcon,
		{ icon ? &st::settingsIconFaq : nullptr, kIconLightBlue }
	)->addClickHandler(OpenFaq);
}

void SetupHelp(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);

	SetupFaq(container);

	AddButton(
		container,
		tr::lng_settings_features(),
		st::settingsButton,
		{ &st::settingsIconTips, kIconLightOrange }
	)->setClickedCallback([=] {
		UrlClickHandler::Open(tr::lng_telegram_features_url(tr::now));
	});

	const auto button = AddButton(
		container,
		tr::lng_settings_ask_question(),
		st::settingsButton,
		{ &st::settingsIconAskQuestion, kIconGreen });
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
						Ui::showPeerHistory(user, ShowAtUnreadMsgId);
					}
				});
			}).fail([=] {
				*requestId = 0;
			}).send();
		});
		auto box = Ui::MakeConfirmBox({
			.text = tr::lng_settings_ask_sure(),
			.confirmed = sure,
			.cancelled = OpenFaq,
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

	SetupSections(controller, content, [=](Type type) {
		_showOther.fire_copy(type);
	});
	if (HasInterfaceScale()) {
		AddDivider(content);
		AddSkip(content);
		SetupInterfaceScale(&controller->window(), content);
		AddSkip(content);
	}
	SetupHelp(controller, content);

	Ui::ResizeFitChild(this, content);

	// If we load this in advance it won't jump when we open its' section.
	controller->session().api().cloudPassword().reload();
	controller->session().api().reloadContactSignupSilent();
	controller->session().api().sensitiveContent().reload();
	controller->session().api().globalPrivacy().reload();
	controller->session().data().cloudThemes().refresh();
}

rpl::producer<Type> Main::sectionShowOther() {
	return _showOther.events();
}

} // namespace Settings
