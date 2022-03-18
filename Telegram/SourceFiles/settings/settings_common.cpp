/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_common.h"

#include "settings/settings_chat.h"
#include "settings/settings_advanced.h"
#include "settings/settings_information.h"
#include "settings/settings_main.h"
#include "settings/settings_notifications.h"
#include "settings/settings_privacy_security.h"
#include "settings/settings_folders.h"
#include "settings/settings_calls.h"
#include "settings/settings_experimental.h"
#include "core/application.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/widgets/buttons.h"
#include "boxes/abstract_box.h"
#include "boxes/sessions_box.h"
#include "window/themes/window_theme_editor_box.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "main/main_session.h"
#include "main/main_domain.h"
#include "base/options.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_menu_icons.h"

namespace Settings {
namespace {

base::options::toggle OptionMonoSettingsIcons({
	.id = kOptionMonoSettingsIcons,
	.name = "Mono settings and menu icons",
	.description = "Use a single color for settings and main menu icons.",
});

} // namespace

const char kOptionMonoSettingsIcons[] = "mono-settings-icons";

Icon::Icon(IconDescriptor descriptor) : _icon(descriptor.icon) {
	const auto background = [&] {
		if (OptionMonoSettingsIcons.value()) {
			return &st::transparent;
		}
		if (descriptor.color > 0) {
			const auto list = std::array{
				&st::settingsIconBg1,
				&st::settingsIconBg2,
				&st::settingsIconBg3,
				&st::settingsIconBg4,
				&st::settingsIconBg5,
				&st::settingsIconBg6,
				(const style::color*)nullptr,
				&st::settingsIconBg8,
				&st::settingsIconBgArchive,
			};
			Assert(descriptor.color < 10 && descriptor.color != 7);
			return list[descriptor.color - 1];
		}
		return descriptor.background;
	}();
	if (background) {
		const auto radius = (descriptor.type == IconType::Rounded)
			? st::settingsIconRadius
			: (std::min(_icon->width(), _icon->height()) / 2);
		_background.emplace(radius, *background);
	}
}

void Icon::paint(QPainter &p, QPoint position) const {
	paint(p, position.x(), position.y());
}

void Icon::paint(QPainter &p, int x, int y) const {
	if (_background) {
		_background->paint(p, { { x, y }, _icon->size() });
	}
	if (OptionMonoSettingsIcons.value()) {
		_icon->paint(p, { x, y }, 2 * x + _icon->width(), st::menuIconFg->c);
	} else {
		_icon->paint(p, { x, y }, 2 * x + _icon->width());
	}
}

int Icon::width() const {
	return _icon->width();
}

int Icon::height() const {
	return _icon->height();
}

QSize Icon::size() const {
	return _icon->size();
}

object_ptr<Section> CreateSection(
		Type type,
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller) {
	switch (type) {
	case Type::Main:
		return object_ptr<Main>(parent, controller);
	case Type::Information:
		return object_ptr<Information>(parent, controller);
	case Type::Notifications:
		return object_ptr<Notifications>(parent, controller);
	case Type::PrivacySecurity:
		return object_ptr<PrivacySecurity>(parent, controller);
	case Type::Sessions:
		return object_ptr<Sessions>(parent, controller);
	case Type::Advanced:
		return object_ptr<Advanced>(parent, controller);
	case Type::Folders:
		return object_ptr<Folders>(parent, controller);
	case Type::Chat:
		return object_ptr<Chat>(parent, controller);
	case Type::Calls:
		return object_ptr<Calls>(parent, controller);
	case Type::Experimental:
		return object_ptr<Experimental>(parent, controller);
	}
	Unexpected("Settings section type in Widget::createInnerWidget.");
}

void AddSkip(not_null<Ui::VerticalLayout*> container) {
	AddSkip(container, st::settingsSectionSkip);
}

void AddSkip(not_null<Ui::VerticalLayout*> container, int skip) {
	container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		skip));
}

void AddDivider(not_null<Ui::VerticalLayout*> container) {
	container->add(object_ptr<Ui::BoxContentDivider>(container));
}

void AddDividerText(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text) {
	container->add(object_ptr<Ui::DividerLabel>(
		container,
		object_ptr<Ui::FlatLabel>(
			container,
			std::move(text),
			st::boxDividerLabel),
		st::settingsDividerLabelPadding));
}

void AddButtonIcon(
		not_null<Ui::AbstractButton*> button,
		const style::SettingsButton &st,
		IconDescriptor &&descriptor) {
	Expects(descriptor.icon != nullptr);

	struct IconWidget {
		IconWidget(QWidget *parent, IconDescriptor &&descriptor)
		: widget(parent)
		, icon(std::move(descriptor)) {
		}
		Ui::RpWidget widget;
		Icon icon;
	};
	const auto icon = button->lifetime().make_state<IconWidget>(
		button,
		std::move(descriptor));
	icon->widget.setAttribute(Qt::WA_TransparentForMouseEvents);
	icon->widget.resize(icon->icon.size());
	button->sizeValue(
	) | rpl::start_with_next([=, left = st.iconLeft](QSize size) {
		icon->widget.moveToLeft(
			left,
			(size.height() - icon->widget.height()) / 2,
			size.width());
	}, icon->widget.lifetime());
	icon->widget.paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(&icon->widget);
		icon->icon.paint(p, 0, 0);
	}, icon->widget.lifetime());
}

object_ptr<Button> CreateButton(
		not_null<QWidget*> parent,
		rpl::producer<QString> text,
		const style::SettingsButton &st,
		IconDescriptor &&descriptor) {
	auto result = object_ptr<Button>(parent, std::move(text), st);
	const auto button = result.data();
	if (descriptor) {
		AddButtonIcon(button, st, std::move(descriptor));
	}
	return result;
}

not_null<Button*> AddButton(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text,
		const style::SettingsButton &st,
		IconDescriptor &&descriptor) {
	return container->add(
		CreateButton(container, std::move(text), st, std::move(descriptor)));
}

void CreateRightLabel(
		not_null<Button*> button,
		rpl::producer<QString> label,
		const style::SettingsButton &st,
		rpl::producer<QString> buttonText) {
	const auto name = Ui::CreateChild<Ui::FlatLabel>(
		button.get(),
		st.rightLabel);
	rpl::combine(
		button->widthValue(),
		std::move(buttonText),
		std::move(label)
	) | rpl::start_with_next([=, &st](
			int width,
			const QString &button,
			const QString &text) {
		const auto available = width
			- st.padding.left()
			- st.padding.right()
			- st.style.font->width(button)
			- st::settingsButtonRightSkip;
		name->setText(text);
		name->resizeToNaturalWidth(available);
		name->moveToRight(st::settingsButtonRightSkip, st.padding.top());
	}, name->lifetime());
	name->setAttribute(Qt::WA_TransparentForMouseEvents);
}

not_null<Button*> AddButtonWithLabel(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text,
		rpl::producer<QString> label,
		const style::SettingsButton &st,
		IconDescriptor &&descriptor) {
	const auto button = AddButton(
		container,
		rpl::duplicate(text),
		st,
		std::move(descriptor));
	CreateRightLabel(button, std::move(label), st, std::move(text));
	return button;
}

not_null<Ui::FlatLabel*> AddSubsectionTitle(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text,
		style::margins addPadding,
		const style::FlatLabel *st) {
	return container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			std::move(text),
			st ? *st : st::settingsSubsectionTitle),
		st::settingsSubsectionTitlePadding + addPadding);
}

void FillMenu(
		not_null<Window::SessionController*> controller,
		Type type,
		Fn<void(Type)> showOther,
		MenuCallback addAction) {
	const auto window = &controller->window();
	if (type == Type::Chat) {
		addAction(
			tr::lng_settings_bg_theme_create(tr::now),
			[=] { window->show(Box(Window::Theme::CreateBox, window)); },
			&st::menuIconChangeColors);
	} else {
		const auto &list = Core::App().domain().accounts();
		if (list.size() < ::Main::Domain::kMaxAccounts) {
			addAction(tr::lng_menu_add_account(tr::now), [=] {
				Core::App().domain().addActivated(MTP::Environment{});
			}, &st::menuIconAddAccount);
		}
		if (!controller->session().supportMode()) {
			addAction(
				tr::lng_settings_information(tr::now),
				[=] { showOther(Type::Information); },
				&st::menuIconInfo);
		}
		addAction(
			tr::lng_settings_logout(tr::now),
			[=] { window->showLogoutConfirmation(); },
			&st::menuIconLeave);
	}
}

} // namespace Settings
