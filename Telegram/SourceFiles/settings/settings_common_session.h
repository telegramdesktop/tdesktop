/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_common.h"
#include "ui/rp_widget.h"
#include "base/object_ptr.h"
#include "settings/settings_type.h"

namespace Ui::Menu {
struct MenuCallback;
} // namespace Ui::Menu

namespace Window {
class SessionController;
} // namespace Window

namespace Settings {

class AbstractSection;

struct AbstractSectionFactory {
	[[nodiscard]] virtual object_ptr<AbstractSection> create(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller) const = 0;
	[[nodiscard]] virtual bool hasCustomTopBar() const {
		return false;
	}

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

void FillMenu(
	not_null<Window::SessionController*> controller,
	Type type,
	Fn<void(Type)> showOther,
	Ui::Menu::MenuCallback addAction);

} // namespace Settings
