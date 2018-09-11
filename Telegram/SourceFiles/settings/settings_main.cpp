/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_main.h"

#include "settings/settings_common.h"
#include "settings/settings_codes.h"
#include "boxes/language_box.h"
#include "boxes/confirm_box.h"
#include "boxes/about_box.h"
#include "boxes/photo_crop_box.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/discrete_sliders.h"
#include "info/profile/info_profile_button.h"
#include "info/profile/info_profile_cover.h"
#include "lang/lang_keys.h"
#include "storage/localstorage.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "core/file_utilities.h"
#include "styles/style_settings.h"

namespace Settings {

void SetupUploadPhotoButton(
		not_null<Ui::VerticalLayout*> container,
		not_null<UserData*> self) {
	AddDivider(container);
	AddSkip(container, st::settingsSetPhotoSkip);

	const auto upload = [=] {
		const auto imageExtensions = cImgExtensions();
		const auto filter = qsl("Image files (*")
			+ imageExtensions.join(qsl(" *"))
			+ qsl(");;")
			+ FileDialog::AllFilesFilter();
		const auto callback = [=](const FileDialog::OpenResult &result) {
			if (result.paths.isEmpty() && result.remoteContent.isEmpty()) {
				return;
			}

			const auto image = result.remoteContent.isEmpty()
				? App::readImage(result.paths.front())
				: App::readImage(result.remoteContent);
			if (image.isNull()
				|| image.width() > 10 * image.height()
				|| image.height() > 10 * image.width()) {
				Ui::show(Box<InformBox>(lang(lng_bad_photo)));
				return;
			}

			auto box = Ui::show(Box<PhotoCropBox>(image, self));
			box->ready(
			) | rpl::start_with_next([=](QImage &&image) {
				Auth().api().uploadPeerPhoto(self, std::move(image));
			}, box->lifetime());
		};
		FileDialog::GetOpenPath(
			container.get(),
			lang(lng_choose_image),
			filter,
			crl::guard(container, callback));
	};
	AddButton(
		container,
		lng_settings_upload,
		st::settingsSectionButton,
		&st::settingsIconSetPhoto
	)->addClickHandler(App::LambdaDelayed(
		st::settingsSectionButton.ripple.hideDuration,
		container,
		upload));

	AddSkip(container, st::settingsSetPhotoSkip);
}

void SetupLanguageButton(not_null<Ui::VerticalLayout*> container) {
	const auto button = AddButtonWithLabel(
		container,
		lng_settings_language,
		Lang::Viewer(lng_language_name),
		st::settingsSectionButton,
		&st::settingsIconLanguage);
	const auto guard = Ui::AttachAsChild(button, base::binary_guard());
	button->addClickHandler([=] {
		*guard = LanguageBox::Show();
	});
}

void SetupSections(
		not_null<Ui::VerticalLayout*> container,
		Fn<void(Type)> showOther) {
	AddDivider(container);
	AddSkip(container);

	const auto addSection = [&](
			LangKey label,
			Type type,
			const style::icon *icon) {
		AddButton(
			container,
			label,
			st::settingsSectionButton,
			icon
		)->addClickHandler([=] { showOther(type); });
	};
	addSection(
		lng_settings_section_info,
		Type::Information,
		&st::settingsIconInformation);
	addSection(
		lng_settings_section_notify,
		Type::Notifications,
		&st::settingsIconNotifications);
	addSection(
		lng_settings_section_privacy,
		Type::PrivacySecurity,
		&st::settingsIconPrivacySecurity);
	addSection(
		lng_settings_section_general,
		Type::General,
		&st::settingsIconGeneral);
	addSection(
		lng_settings_section_chat_settings,
		Type::Chat,
		&st::settingsIconChat);

	SetupLanguageButton(container);

	AddSkip(container);
}

bool HasInterfaceScale() {
	return !cRetina();
}

void SetupInterfaceScale(
		not_null<Ui::VerticalLayout*> container,
		bool icon) {
	if (!HasInterfaceScale()) {
		return;
	}

	const auto toggled = Ui::AttachAsChild(
		container,
		rpl::event_stream<bool>());

	const auto switched = (cConfigScale() == dbisAuto)
		|| (cConfigScale() == cScreenScale());
	const auto button = AddButton(
		container,
		lng_settings_default_scale,
		icon ? st::settingsSectionButton : st::settingsGeneralButton,
		icon ? &st::settingsIconInterfaceScale : nullptr
	)->toggleOn(toggled->events_starting_with_copy(switched));

	const auto slider = container->add(
		object_ptr<Ui::SettingsSlider>(container, st::settingsSlider),
		icon ? st::settingsScalePadding : st::settingsBigScalePadding);

	const auto inSetScale = Ui::AttachAsChild(container, false);
	const auto setScale = std::make_shared<Fn<void(DBIScale)>>();
	*setScale = [=](DBIScale scale) {
		if (*inSetScale) return;
		*inSetScale = true;
		const auto guard = gsl::finally([=] { *inSetScale = false; });

		if (scale == cScreenScale()) {
			scale = dbisAuto;
		}
		toggled->fire(scale == dbisAuto);
		const auto applying = scale;
		if (scale == dbisAuto) {
			scale = cScreenScale();
		}
		slider->setActiveSection(scale - 1);

		if (cEvalScale(scale) != cEvalScale(cRealScale())) {
			const auto confirmed = crl::guard(button, [=] {
				cSetConfigScale(applying);
				Local::writeSettings();
				App::restart();
			});
			const auto cancelled = crl::guard(button, [=] {
				App::CallDelayed(
					st::defaultSettingsSlider.duration,
					button,
					[=] { (*setScale)(cRealScale()); });
			});
			Ui::show(Box<ConfirmBox>(
				lang(lng_settings_need_restart),
				lang(lng_settings_restart_now),
				confirmed,
				cancelled));
		} else {
			cSetConfigScale(scale);
			Local::writeSettings();
		}
	};
	button->toggledValue(
	) | rpl::start_with_next([=](bool checked) {
		auto scale = checked ? dbisAuto : cEvalScale(cConfigScale());
		if (scale == cScreenScale()) {
			if (scale != cScale()) {
				scale = cScale();
			} else {
				switch (scale) {
				case dbisOne: scale = dbisOneAndQuarter; break;
				case dbisOneAndQuarter: scale = dbisOne; break;
				case dbisOneAndHalf: scale = dbisOneAndQuarter; break;
				case dbisTwo: scale = dbisOneAndHalf; break;
				}
			}
		}
		(*setScale)(scale);
	}, button->lifetime());

	const auto label = [](DBIScale scale) {
		switch (scale) {
		case dbisOne: return qsl("100%");
		case dbisOneAndQuarter: return qsl("125%");
		case dbisOneAndHalf: return qsl("150%");
		case dbisTwo: return qsl("200%");
		}
		Unexpected("Value in scale label.");
	};
	const auto scaleByIndex = [](int index) {
		switch (index) {
		case 0: return dbisOne;
		case 1: return dbisOneAndQuarter;
		case 2: return dbisOneAndHalf;
		case 3: return dbisTwo;
		}
		Unexpected("Index in scaleByIndex.");
	};

	slider->addSection(label(dbisOne));
	slider->addSection(label(dbisOneAndQuarter));
	slider->addSection(label(dbisOneAndHalf));
	slider->addSection(label(dbisTwo));
	slider->setActiveSectionFast(cEvalScale(cConfigScale()) - 1);
	slider->sectionActivated(
	) | rpl::start_with_next([=](int section) {
		(*setScale)(scaleByIndex(section));
	}, slider->lifetime());
}

void SetupFaq(not_null<Ui::VerticalLayout*> container, bool icon) {
	AddButton(
		container,
		lng_settings_faq,
		icon ? st::settingsSectionButton : st::settingsGeneralButton,
		icon ? &st::settingsIconFaq : nullptr
	)->addClickHandler([] {
		QDesktopServices::openUrl(telegramFaqLink());
	});
}

void SetupHelp(not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);

	SetupFaq(container);

	if (AuthSession::Exists()) {
		const auto button = AddButton(
			container,
			lng_settings_ask_question,
			st::settingsSectionButton);
		button->addClickHandler([=] {
			const auto ready = crl::guard(button, [](const MTPUser &data) {
				const auto users = MTP_vector<MTPUser>(1, data);
				if (const auto user = App::feedUsers(users)) {
					Ui::showPeerHistory(user, ShowAtUnreadMsgId);
				}
			});
			Auth().api().requestSupportContact(ready);
		});
	}

	AddSkip(container);
}

Main::Main(
	QWidget *parent,
	not_null<Window::Controller*> controller,
	not_null<UserData*> self)
: Section(parent)
, _self(self) {
	setupContent(controller);
}

void Main::keyPressEvent(QKeyEvent *e) {
	CodesFeedString(e->text());
	return Section::keyPressEvent(e);
}

void Main::setupContent(not_null<Window::Controller*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	const auto cover = content->add(object_ptr<Info::Profile::Cover>(
		content,
		_self,
		controller));
	cover->setOnlineCount(rpl::single(0));

	SetupUploadPhotoButton(content, _self);
	SetupSections(content, [=](Type type) {
		_showOther.fire_copy(type);
	});
	if (HasInterfaceScale()) {
		AddDivider(content);
		AddSkip(content);
		SetupInterfaceScale(content);
		AddSkip(content);
	}
	SetupHelp(content);

	Ui::ResizeFitChild(this, content);

	// If we load this in advance it won't jump when we open its' section.
	Auth().api().reloadPasswordState();
}

rpl::producer<Type> Main::sectionShowOther() {
	return _showOther.events();
}

} // namespace Settings
