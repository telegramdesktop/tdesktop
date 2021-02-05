/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/photo_editor_controls.h"

#include "ui/cached_round_corners.h"
namespace Editor {

class HorizontalContainer final : public Ui::RpWidget {
public:
	HorizontalContainer(not_null<Ui::RpWidget*> parent);

	void updateChildrenPosition();

};

HorizontalContainer::HorizontalContainer(not_null<Ui::RpWidget*> parent)
: RpWidget(parent) {
}

void HorizontalContainer::updateChildrenPosition() {
	auto left = 0;
	auto height = 0;
	for (auto child : RpWidget::children()) {
		if (child->isWidgetType()) {
			const auto widget = static_cast<QWidget*>(child);
			widget->move(left, 0);
			left += widget->width();
			height = std::max(height, widget->height());
		}
	}
	resize(left, height);
}

PhotoEditorControls::PhotoEditorControls(
	not_null<Ui::RpWidget*> parent,
	bool doneControls)
: RpWidget(parent)
, _buttonsContainer(base::make_unique_q<HorizontalContainer>(this)) {

	_buttonsContainer->updateChildrenPosition();

	sizeValue(
	) | rpl::start_with_next([=](const QSize &size) {

		_buttonsContainer->moveToLeft(
			(size.width() - _buttonsContainer->width()) / 2,
			0);

	}, lifetime());

}
} // namespace Editor
