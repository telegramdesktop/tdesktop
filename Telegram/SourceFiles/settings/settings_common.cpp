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
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/labels.h"
#include "info/profile/info_profile_button.h"
#include "boxes/abstract_box.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "styles/style_boxes.h"
#include "styles/style_settings.h"

namespace Settings {

object_ptr<Section> CreateSection(
		Type type,
		not_null<QWidget*> parent,
		Window::Controller *controller,
		UserData *self) {
	switch (type) {
	case Type::Main:
		return object_ptr<Main>(parent, controller, self);
	case Type::Information:
		return object_ptr<Information>(parent, controller, self);
	case Type::Notifications:
		return object_ptr<Notifications>(parent, self);
	case Type::PrivacySecurity:
		return object_ptr<PrivacySecurity>(parent, self);
	case Type::Advanced:
		return object_ptr<Advanced>(parent, self);
	case Type::Chat:
		return object_ptr<Chat>(parent, self);
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
	container->add(object_ptr<BoxContentDivider>(container));
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

not_null<Button*> AddButton(
		not_null<Ui::VerticalLayout*> container,
		LangKey text,
		const style::InfoProfileButton &st,
		const style::icon *leftIcon,
		int iconLeft) {
	return AddButton(container, Lang::Viewer(text), st, leftIcon, iconLeft);
}

not_null<Button*> AddButton(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text,
		const style::InfoProfileButton &st,
		const style::icon *leftIcon,
		int iconLeft) {
	const auto result = container->add(object_ptr<Button>(
		container,
		std::move(text),
		st));
	if (leftIcon) {
		const auto icon = Ui::CreateChild<Ui::RpWidget>(result);
		icon->setAttribute(Qt::WA_TransparentForMouseEvents);
		icon->resize(leftIcon->size());
		result->sizeValue(
		) | rpl::start_with_next([=](QSize size) {
			icon->moveToLeft(
				iconLeft ? iconLeft : st::settingsSectionIconLeft,
				(size.height() - icon->height()) / 2,
				size.width());
		}, icon->lifetime());
		icon->paintRequest(
		) | rpl::start_with_next([=] {
			Painter p(icon);
			const auto width = icon->width();
			const auto paintOver = (result->isOver() || result->isDown())
				&& !result->isDisabled();
			if (paintOver) {
				leftIcon->paint(p, QPoint(), width, st::menuIconFgOver->c);
			} else {
				leftIcon->paint(p, QPoint(), width);
			}
		}, icon->lifetime());
	}
	return result;
}

void CreateRightLabel(
		not_null<Button*> button,
		rpl::producer<QString> label,
		const style::InfoProfileButton &st,
		LangKey buttonText) {
	const auto name = Ui::CreateChild<Ui::FlatLabel>(
		button.get(),
		st::settingsButtonRight);
	rpl::combine(
		button->widthValue(),
		Lang::Viewer(buttonText),
		std::move(label)
	) | rpl::start_with_next([=, &st](
			int width,
			const QString &button,
			const QString &text) {
		const auto available = width
			- st.padding.left()
			- st.padding.right()
			- st.font->width(button)
			- st::settingsButtonRightSkip;
		name->setText(text);
		name->resizeToNaturalWidth(available);
		name->moveToRight(st::settingsButtonRightSkip, st.padding.top());
	}, name->lifetime());
	name->setAttribute(Qt::WA_TransparentForMouseEvents);
}

not_null<Button*> AddButtonWithLabel(
		not_null<Ui::VerticalLayout*> container,
		LangKey text,
		rpl::producer<QString> label,
		const style::InfoProfileButton &st,
		const style::icon *leftIcon,
		int iconLeft) {
	const auto button = AddButton(container, text, st, leftIcon, iconLeft);
	CreateRightLabel(button, std::move(label), st, text);
	return button;
}

void AddSubsectionTitle(
		not_null<Ui::VerticalLayout*> container,
		LangKey text) {
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			Lang::Viewer(text),
			st::settingsSubsectionTitle),
		st::settingsSubsectionTitlePadding);
}

void FillMenu(Fn<void(Type)> showOther, MenuCallback addAction) {
	addAction(
		lang(lng_settings_information),
		[=] { showOther(Type::Information); });
	addAction(
		lang(lng_settings_logout),
		[=] { App::wnd()->onLogout(); });
}

} // namespace Settings
