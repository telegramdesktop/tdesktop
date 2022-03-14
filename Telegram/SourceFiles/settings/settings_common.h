/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/round_rect.h"
#include "base/object_ptr.h"

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class VerticalLayout;
class FlatLabel;
class SettingsButton;
class AbstractButton;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace style {
struct FlatLabel;
struct SettingsButton;
} // namespace style

namespace Settings {

extern const char kOptionMonoSettingsIcons[];

enum class Type {
	Main,
	Information,
	Notifications,
	PrivacySecurity,
	Sessions,
	Advanced,
	Chat,
	Folders,
	Calls,
	Experimental,
};

using Button = Ui::SettingsButton;

class Section : public Ui::RpWidget {
public:
	using RpWidget::RpWidget;

	virtual rpl::producer<Type> sectionShowOther() {
		return nullptr;
	}
	virtual void sectionSaveChanges(FnMut<void()> done) {
		done();
	}

};

inline constexpr auto kIconRed = 1;
inline constexpr auto kIconGreen = 2;
inline constexpr auto kIconLightOrange = 3;
inline constexpr auto kIconLightBlue = 4;
inline constexpr auto kIconDarkBlue = 5;
inline constexpr auto kIconPurple = 6;
inline constexpr auto kIconDarkOrange = 8;
inline constexpr auto kIconGray = 9;

enum class IconType {
	Rounded,
	Round,
};

struct IconDescriptor {
	const style::icon *icon = nullptr;
	int color = 0; // settingsIconBg{color}, 9 for settingsIconBgArchive.
	IconType type = IconType::Rounded;
	const style::color *background = nullptr;

	explicit operator bool() const {
		return (icon != nullptr);
	}
};

class Icon final {
public:
	explicit Icon(IconDescriptor descriptor);

	void paint(QPainter &p, QPoint position) const;
	void paint(QPainter &p, int x, int y) const;

	[[nodiscard]] int width() const;
	[[nodiscard]] int height() const;
	[[nodiscard]] QSize size() const;

private:
	not_null<const style::icon*> _icon;
	std::optional<Ui::RoundRect> _background;

};

object_ptr<Section> CreateSection(
	Type type,
	not_null<QWidget*> parent,
	not_null<Window::SessionController*> controller);

void AddSkip(not_null<Ui::VerticalLayout*> container);
void AddSkip(not_null<Ui::VerticalLayout*> container, int skip);
void AddDivider(not_null<Ui::VerticalLayout*> container);
void AddDividerText(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<QString> text);
void AddButtonIcon(
	not_null<Ui::AbstractButton*> button,
	const style::SettingsButton &st,
	IconDescriptor &&descriptor);
object_ptr<Button> CreateButton(
	not_null<QWidget*> parent,
	rpl::producer<QString> text,
	const style::SettingsButton &st,
	IconDescriptor &&descriptor = {});
not_null<Button*> AddButton(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<QString> text,
	const style::SettingsButton &st,
	IconDescriptor &&descriptor = {});
not_null<Button*> AddButtonWithLabel(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<QString> text,
	rpl::producer<QString> label,
	const style::SettingsButton &st,
	IconDescriptor &&descriptor = {});
void CreateRightLabel(
	not_null<Button*> button,
	rpl::producer<QString> label,
	const style::SettingsButton &st,
	rpl::producer<QString> buttonText);
not_null<Ui::FlatLabel*> AddSubsectionTitle(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<QString> text,
	style::margins addPadding = {},
	const style::FlatLabel *st = nullptr);

using MenuCallback = Fn<QAction*(
	const QString &text,
	Fn<void()> handler,
	const style::icon *icon)>;

void FillMenu(
	not_null<Window::SessionController*> controller,
	Type type,
	Fn<void(Type)> showOther,
	MenuCallback addAction);

} // namespace Settings
