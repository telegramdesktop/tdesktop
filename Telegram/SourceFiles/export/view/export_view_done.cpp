/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/view/export_view_done.h"

#include "lang/lang_keys.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "platform/platform_specific.h"
#include "styles/style_widgets.h"
#include "styles/style_export.h"
#include "styles/style_boxes.h"

namespace Export {
namespace View {

DoneWidget::DoneWidget(QWidget *parent)
: RpWidget(parent) {
	setupContent();
}

void DoneWidget::setupContent() {
	initFooter();

	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	const auto label = content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			"Done! " + textcmdLink(1, "Press here") + " to view your data.",
			Ui::FlatLabel::InitType::Rich,
			st::defaultFlatLabel),
		st::exportSettingPadding);
	label->setLink(1, std::make_shared<LambdaClickHandler>([=] {
		_showClicks.fire({});
	}));

	sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		content->resizeToWidth(size.width());
	}, lifetime());
}

rpl::producer<> DoneWidget::showClicks() const {
	return _showClicks.events();
}

rpl::producer<> DoneWidget::closeClicks() const {
	return _close->clicks();
}

void DoneWidget::initFooter() {
	const auto buttonsPadding = st::boxButtonPadding;
	const auto buttonsHeight = buttonsPadding.top()
		+ st::defaultBoxButton.height
		+ buttonsPadding.bottom();
	const auto buttons = Ui::CreateChild<Ui::FixedHeightWidget>(
		this,
		buttonsHeight);

	sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		buttons->resizeToWidth(size.width());
		buttons->moveToLeft(0, size.height() - buttons->height());
	}, lifetime());

	_close = Ui::CreateChild<Ui::RoundButton>(
		buttons,
		langFactory(lng_close),
		st::defaultBoxButton);
	_close->show();

	buttons->widthValue(
	) | rpl::start_with_next([=] {
		const auto right = st::boxButtonPadding.right();
		const auto top = st::boxButtonPadding.top();
		_close->moveToRight(right, top);
	}, _close->lifetime());
}

} // namespace View
} // namespace Export
