/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/view/export_view_settings.h"

#include "lang/lang_keys.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/padding_wrap.h"
#include "platform/platform_specific.h"
#include "styles/style_widgets.h"
#include "styles/style_export.h"
#include "styles/style_boxes.h"

namespace Export {
namespace View {

SettingsWidget::SettingsWidget(QWidget *parent)
: RpWidget(parent) {
	if (Global::DownloadPath().isEmpty()) {
		_data.path = psDownloadPath();
	} else if (Global::DownloadPath() == qsl("tmp")) {
		_data.path = cTempDir();
	} else {
		_data.path = Global::DownloadPath();
	}
	setupContent();
}

void SettingsWidget::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	const auto buttonsPadding = st::boxButtonPadding;
	const auto buttonsHeight = buttonsPadding.top()
		+ st::defaultBoxButton.height
		+ buttonsPadding.bottom();
	const auto buttons = Ui::CreateChild<Ui::FixedHeightWidget>(
		this,
		buttonsHeight);
	const auto refreshButtonsCallback = [=] {
		refreshButtons(buttons);
	};

	const auto addOption = [&](LangKey key, Types types) {
		const auto checkbox = content->add(
			object_ptr<Ui::Checkbox>(
				content,
				lang(key),
				((_data.types & types) == types),
				st::defaultBoxCheckbox),
			st::exportSettingPadding);
		base::ObservableViewer(
			checkbox->checkedChanged
		) | rpl::start_with_next([=](bool checked) {
			if (checked) {
				_data.types |= types;
			} else {
				_data.types &= ~types;
			}
			refreshButtonsCallback();
		}, lifetime());
	};
	addOption(lng_export_option_info, Type::PersonalInfo | Type::Avatars);
	addOption(lng_export_option_contacts, Type::Contacts);
	addOption(lng_export_option_sessions, Type::Sessions);
	refreshButtonsCallback();

	sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		content->resizeToWidth(size.width());
		buttons->resizeToWidth(size.width());
		buttons->moveToLeft(0, size.height() - buttons->height());
	}, lifetime());
}

void SettingsWidget::refreshButtons(not_null<Ui::RpWidget*> container) {
	container->hideChildren();
	const auto children = container->children();
	for (const auto child : children) {
		if (child->isWidgetType()) {
			child->deleteLater();
		}
	}
	const auto start = _data.types
		? Ui::CreateChild<Ui::RoundButton>(
			container.get(),
			langFactory(lng_export_start),
			st::defaultBoxButton)
		: nullptr;
	if (start) {
		_startClicks = start->clicks();

		container->sizeValue(
		) | rpl::start_with_next([=](QSize size) {
			const auto right = st::boxButtonPadding.right();
			const auto top = st::boxButtonPadding.top();
			start->moveToRight(right, top);
		}, start->lifetime());
	}

	const auto cancel = Ui::CreateChild<Ui::RoundButton>(
		container.get(),
		langFactory(lng_cancel),
		st::defaultBoxButton);
	_cancelClicks = cancel->clicks();

	rpl::combine(
		container->sizeValue(),
		start ? start->widthValue() : rpl::single(0)
	) | rpl::start_with_next([=](QSize size, int width) {
		const auto right = st::boxButtonPadding.right()
			+ (width ? width + st::boxButtonPadding.left() : 0);
		const auto top = st::boxButtonPadding.top();
		cancel->moveToRight(right, top);
	}, cancel->lifetime());
}

rpl::producer<Settings> SettingsWidget::startClicks() const {
	return _startClicks.value(
	) | rpl::map([](Wrap &&wrap) {
		return std::move(wrap.value);
	}) | rpl::flatten_latest(
	) | rpl::map([=] {
		return _data;
	});
}

rpl::producer<> SettingsWidget::cancelClicks() const {
	return _cancelClicks.value(
	) | rpl::map([](Wrap &&wrap) {
		return std::move(wrap.value);
	}) | rpl::flatten_latest();
}

} // namespace View
} // namespace Export
