/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/ui/calls_device_menu.h"

#include "lang/lang_keys.h"
#include "ui/widgets/menu/menu_item_base.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/vertical_layout.h"
#include "webrtc/webrtc_device_common.h"
#include "webrtc/webrtc_environment.h"
#include "styles/style_calls.h"
#include "styles/style_layers.h"

namespace Calls {
namespace {

class Subsection final : public Ui::Menu::ItemBase {
public:
	Subsection(
		not_null<RpWidget*> parent,
		const style::Menu &st,
		const QString &text);

	not_null<QAction*> action() const override;
	bool isEnabled() const override;

private:
	int contentHeight() const override;

	const style::Menu &_st;
	const base::unique_qptr<Ui::FlatLabel> _text;
	const not_null<QAction*> _dummyAction;

};

class Selector final : public Ui::Menu::ItemBase {
public:
	Selector(
		not_null<RpWidget*> parent,
		const style::Menu &st,
		rpl::producer<std::vector<Webrtc::DeviceInfo>> devices,
		rpl::producer<Webrtc::DeviceResolvedId> chosen,
		Fn<void(QString)> selected);

	not_null<QAction*> action() const override;
	bool isEnabled() const override;

private:
	int contentHeight() const override;
	[[nodiscard]] int registerId(const QString &id);

	const base::unique_qptr<Ui::ScrollArea> _scroll;
	const not_null<Ui::VerticalLayout*> _list;
	const not_null<QAction*> _dummyAction;

	base::flat_map<QString, int> _ids;

};

Subsection::Subsection(
	not_null<RpWidget*> parent,
	const style::Menu &st,
	const QString &text)
: Ui::Menu::ItemBase(parent, st)
, _st(st)
, _text(base::make_unique_q<Ui::FlatLabel>(
	this,
	text,
	st::callDeviceSelectionLabel))
, _dummyAction(new QAction(parent)) {
	setPointerCursor(false);

	initResizeHook(parent->sizeValue());

	_text->resizeToWidth(st::callDeviceSelectionLabel.minWidth);
	_text->moveToLeft(st.itemPadding.left(), st.itemPadding.top());
}

not_null<QAction*> Subsection::action() const {
	return _dummyAction;
}

bool Subsection::isEnabled() const {
	return false;
}

int Subsection::contentHeight() const {
	return _st.itemPadding.top()
		+ _text->height()
		+ _st.itemPadding.bottom();
}

Selector::Selector(
	not_null<RpWidget*> parent,
	const style::Menu &st,
	rpl::producer<std::vector<Webrtc::DeviceInfo>> devices,
	rpl::producer<Webrtc::DeviceResolvedId> chosen,
	Fn<void(QString)> selected)
: Ui::Menu::ItemBase(parent, st)
, _scroll(base::make_unique_q<Ui::ScrollArea>(this))
, _list(_scroll->setOwnedWidget(object_ptr<Ui::VerticalLayout>(this)))
, _dummyAction(new QAction(parent)) {
	setPointerCursor(false);

	initResizeHook(parent->sizeValue());

	const auto padding = st.itemPadding;
	const auto group = std::make_shared<Ui::RadiobuttonGroup>();
	std::move(
		chosen
	) | rpl::start_with_next([=](Webrtc::DeviceResolvedId id) {
		const auto value = id.isDefault() ? 0 : registerId(id.value);
		if (!group->hasValue() || group->current() != value) {
			group->setValue(value);
		}
	}, lifetime());

	group->setChangedCallback([=](int value) {
		if (value == 0) {
			selected({});
		} else {
			for (const auto &[id, index] : _ids) {
				if (index == value) {
					selected(id);
					break;
				}
			}
		}
	});

	std::move(
		devices
	) | rpl::start_with_next([=](const std::vector<Webrtc::DeviceInfo> &v) {
		while (_list->count()) {
			delete _list->widgetAt(0);
		}
		_list->add(
			object_ptr<Ui::Radiobutton>(
				_list.get(),
				group,
				0,
				tr::lng_settings_call_device_default(tr::now),
				st::groupCallCheckbox,
				st::groupCallRadio),
			padding);
		for (const auto &device : v) {
			if (device.inactive) {
				continue;
			}
			_list->add(
				object_ptr<Ui::Radiobutton>(
					_list.get(),
					group,
					registerId(device.id),
					device.name,
					st::groupCallCheckbox,
					st::groupCallRadio),
				padding);
		}
		resize(width(), contentHeight());
	}, lifetime());
}

not_null<QAction*> Selector::action() const {
	return _dummyAction;
}

bool Selector::isEnabled() const {
	return false;
}

int Selector::contentHeight() const {
	_list->resizeToWidth(width());
	if (_list->count() <= 3) {
		_scroll->resize(width(), _list->height());
	} else {
		_scroll->resize(
			width(),
			3.5 * st::defaultRadio.diameter);
	}
	return _scroll->height();
}

int Selector::registerId(const QString &id) {
	auto &result = _ids[id];
	if (!result) {
		result = int(_ids.size());
	}
	return result;
}

void AddDeviceSelection(
		not_null<Ui::PopupMenu*> menu,
		not_null<Webrtc::Environment*> environment,
		DeviceSelection type,
		Fn<void(QString)> selected) {
	const auto title = [&] {
		switch (type.type) {
		case Webrtc::DeviceType::Camera:
			return tr::lng_settings_call_camera(tr::now);
		case Webrtc::DeviceType::Playback:
			return tr::lng_settings_call_section_output(tr::now);
		case Webrtc::DeviceType::Capture:
			return tr::lng_settings_call_section_input(tr::now);
		}
		Unexpected("Type in AddDeviceSelection.");
	}();
	menu->addAction(
		base::make_unique_q<Subsection>(menu, menu->st().menu, title));
	menu->addAction(
		base::make_unique_q<Selector>(
			menu,
			menu->st().menu,
			environment->devicesValue(type.type),
			std::move(type.chosen),
			selected));
}

} // namespace

base::unique_qptr<Ui::PopupMenu> MakeDeviceSelectionMenu(
		not_null<Ui::RpWidget*> parent,
		not_null<Webrtc::Environment*> environment,
		std::vector<DeviceSelection> types,
		Fn<void(Webrtc::DeviceType, QString)> choose) {
	auto result = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::callDeviceSelectionMenu);
	const auto raw = result.get();
	for (auto type : types) {
		if (!raw->empty()) {
			raw->addSeparator();
		}
		const auto selected = [=, type = type.type](QString id) {
			choose(type, id);
		};
		AddDeviceSelection(raw, environment, std::move(type), selected);
	}
	return result;
}

} // namespace Calls
