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

namespace Ui {
class ScrollArea;
} // namespace Ui

namespace Ui::Menu {
struct MenuCallback;
} // namespace Ui::Menu

namespace Window {
class SessionController;
} // namespace Window

namespace Settings {

enum class Container {
	Section,
	Layer,
};

class AbstractSection;

struct AbstractSectionFactory {
	[[nodiscard]] virtual object_ptr<AbstractSection> create(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<Ui::ScrollArea*> scroll,
		rpl::producer<Container> containerValue) const = 0;
	[[nodiscard]] virtual bool hasCustomTopBar() const {
		return false;
	}

	virtual ~AbstractSectionFactory() = default;
};

template <typename SectionType>
struct SectionFactory : AbstractSectionFactory {
	object_ptr<AbstractSection> create(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<Ui::ScrollArea*> scroll,
		rpl::producer<Container> containerValue
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

	[[nodiscard]] rpl::producer<Type> sectionShowOther() final override {
		return _showOtherRequests.events();
	}
	void showOther(Type type) {
		_showOtherRequests.fire_copy(type);
	}
	[[nodiscard]] Fn<void(Type)> showOtherMethod() {
		return crl::guard(this, [=](Type type) {
			showOther(type);
		});
	}

private:
	rpl::event_stream<Type> _showOtherRequests;

};

bool HasMenu(Type type);

} // namespace Settings
