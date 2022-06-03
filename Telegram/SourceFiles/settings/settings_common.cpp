/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_common.h"

#include "apiwrap.h"
#include "api/api_cloud_password.h"
#include "settings/cloud_password/settings_cloud_password_email_confirm.h"
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
#include "core/core_cloud_password.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "boxes/abstract_box.h"
#include "boxes/sessions_box.h"
#include "window/themes/window_theme_editor_box.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "main/main_session.h"
#include "main/main_domain.h"
#include "lottie/lottie_icon.h"
#include "base/options.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_menu_icons.h"

#include <QAction>

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
	} else if (const auto brush = descriptor.backgroundBrush) {
		const auto radius = (descriptor.type == IconType::Rounded)
			? st::settingsIconRadius
			: (std::min(_icon->width(), _icon->height()) / 2);
		_backgroundBrush.emplace(radius, std::move(*brush));
	}
}

void Icon::paint(QPainter &p, QPoint position) const {
	paint(p, position.x(), position.y());
}

void Icon::paint(QPainter &p, int x, int y) const {
	if (_background) {
		_background->paint(p, { { x, y }, _icon->size() });
	} else if (_backgroundBrush) {
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(_backgroundBrush->second);
		p.drawRoundedRect(
			QRect(QPoint(x, y), _icon->size()),
			_backgroundBrush->first,
			_backgroundBrush->first);
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

LottieIcon CreateLottieIcon(
		not_null<QWidget*> parent,
		Lottie::IconDescriptor &&descriptor,
		style::margins padding) {
	auto object = object_ptr<Ui::RpWidget>(parent);
	const auto raw = object.data();

	const auto width = descriptor.sizeOverride.width();
	raw->resize(QRect(
		QPoint(),
		descriptor.sizeOverride).marginsAdded(padding).size());

	auto owned = Lottie::MakeIcon(std::move(descriptor));
	const auto icon = owned.get();

	raw->lifetime().add([kept = std::move(owned)]{});
	const auto looped = raw->lifetime().make_state<bool>(true);

	const auto start = [=] {
		icon->animate([=] { raw->update(); }, 0, icon->framesCount() - 1);
	};
	const auto animate = [=](anim::repeat repeat) {
		*looped = (repeat == anim::repeat::loop);
		start();
	};
	raw->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(raw);
		const auto left = (raw->width() - width) / 2;
		icon->paint(p, left, padding.top());
		if (!icon->animating() && icon->frameIndex() > 0 && *looped) {
			start();
		}

	}, raw->lifetime());

	return { .widget = std::move(object), .animate = std::move(animate) };
}

void FillMenu(
		not_null<Window::SessionController*> controller,
		Type type,
		Fn<void(Type)> showOther,
		Ui::Menu::MenuCallback addAction) {
	const auto window = &controller->window();
	if (type == Chat::Id()) {
		addAction(
			tr::lng_settings_bg_theme_create(tr::now),
			[=] { window->show(Box(Window::Theme::CreateBox, window)); },
			&st::menuIconChangeColors);
	} else if (type == CloudPasswordEmailConfirmId()) {
		const auto api = &controller->session().api();
		if (const auto state = api->cloudPassword().stateCurrent()) {
			if (state->unconfirmedPattern.isEmpty()) {
				return;
			}
		}
		addAction(
			tr::lng_settings_password_abort(tr::now),
			[=] { api->cloudPassword().clearUnconfirmedPassword(); },
			&st::menuIconCancel);
	} else {
		const auto &list = Core::App().domain().accounts();
		if (list.size() < Core::App().domain().maxAccounts()) {
			addAction(tr::lng_menu_add_account(tr::now), [=] {
				Core::App().domain().addActivated(MTP::Environment{});
			}, &st::menuIconAddAccount);
		}
		if (!controller->session().supportMode()) {
			addAction(
				tr::lng_settings_information(tr::now),
				[=] { showOther(Information::Id()); },
				&st::menuIconInfo);
		}
		addAction({
			.text = tr::lng_settings_logout(tr::now),
			.handler = [=] { window->showLogoutConfirmation(); },
			.icon = &st::menuIconLeaveAttention,
			.isAttention = true,
		});
	}
}

} // namespace Settings
