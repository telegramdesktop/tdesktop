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
#include "settings/settings_type.h"

namespace anim {
enum class repeat : uchar;
} // namespace anim

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class VerticalLayout;
class FlatLabel;
class SettingsButton;
class AbstractButton;
class MediaSlider;
} // namespace Ui

namespace Ui::Menu {
struct MenuCallback;
} // namespace Ui::Menu

namespace Window {
class SessionController;
} // namespace Window

namespace style {
struct FlatLabel;
struct SettingsButton;
struct MediaSlider;
} // namespace style

namespace Lottie {
struct IconDescriptor;
} // namespace Lottie

namespace Settings {

using Button = Ui::SettingsButton;

class AbstractSection;

struct AbstractSectionFactory {
	[[nodiscard]] virtual object_ptr<AbstractSection> create(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller) const = 0;

	virtual ~AbstractSectionFactory() = default;
};

template <typename SectionType>
struct SectionFactory : AbstractSectionFactory {
	object_ptr<AbstractSection> create(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller
	) const final override {
		return object_ptr<SectionType>(parent, controller);
	}

	[[nodiscard]] static const std::shared_ptr<SectionFactory> &Instance() {
		static const auto result = std::make_shared<SectionFactory>();
		return result;
	}
};

class AbstractSection : public Ui::RpWidget {
public:
	using RpWidget::RpWidget;

	[[nodiscard]] virtual Type id() const = 0;
	[[nodiscard]] virtual rpl::producer<Type> sectionShowOther() {
		return nullptr;
	}
	[[nodiscard]] virtual rpl::producer<> sectionShowBack() {
		return nullptr;
	}
	[[nodiscard]] virtual rpl::producer<std::vector<Type>> removeFromStack() {
		return nullptr;
	}
	[[nodiscard]] virtual rpl::producer<QString> title() = 0;
	virtual void sectionSaveChanges(FnMut<void()> done) {
		done();
	}
	virtual void showFinished() {
	}
	virtual void setInnerFocus() {
		setFocus();
	}
	[[nodiscard]] virtual const Ui::RoundRect *bottomSkipRounding() const {
		return nullptr;
	}
	[[nodiscard]] virtual QPointer<Ui::RpWidget> createPinnedToTop(
			not_null<QWidget*> parent) {
		return nullptr;
	}
	[[nodiscard]] virtual QPointer<Ui::RpWidget> createPinnedToBottom(
			not_null<Ui::RpWidget*> parent) {
		return nullptr;
	}
	[[nodiscard]] virtual bool hasFlexibleTopBar() const {
		return false;
	}
	virtual void setStepDataReference(std::any &data) {
	}
};

template <typename SectionType>
class Section : public AbstractSection {
public:
	using AbstractSection::AbstractSection;

	[[nodiscard]] static Type Id() {
		return SectionFactory<SectionType>::Instance();
	}
	[[nodiscard]] Type id() const final override {
		return Id();
	}
};

enum class IconType {
	Rounded,
	Round,
	Simple,
};

struct IconDescriptor {
	const style::icon *icon = nullptr;
	IconType type = IconType::Rounded;
	const style::color *background = nullptr;
	std::optional<QBrush> backgroundBrush; // Can be useful for gradients.

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
	std::optional<std::pair<int, QBrush>> _backgroundBrush;

};

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
void AddDividerTextWithLottie(
	not_null<Ui::VerticalLayout*> parent,
	rpl::producer<> showFinished,
	rpl::producer<TextWithEntities> text,
	const QString &lottie);

struct LottieIcon {
	object_ptr<Ui::RpWidget> widget;
	Fn<void(anim::repeat repeat)> animate;
};
[[nodiscard]] LottieIcon CreateLottieIcon(
	not_null<QWidget*> parent,
	Lottie::IconDescriptor &&descriptor,
	style::margins padding = {});

void FillMenu(
	not_null<Window::SessionController*> controller,
	Type type,
	Fn<void(Type)> showOther,
	Ui::Menu::MenuCallback addAction);

struct SliderWithLabel {
	object_ptr<Ui::RpWidget> widget;
	not_null<Ui::MediaSlider*> slider;
	not_null<Ui::FlatLabel*> label;
};
[[nodiscard]] SliderWithLabel MakeSliderWithLabel(
	QWidget *parent,
	const style::MediaSlider &sliderSt,
	const style::FlatLabel &labelSt,
	int skip,
	int minLabelWidth = 0);

} // namespace Settings
