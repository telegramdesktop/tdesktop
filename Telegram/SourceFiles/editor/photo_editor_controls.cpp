/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/photo_editor_controls.h"

#include "ui/cached_round_corners.h"
#include "ui/widgets/buttons.h"
#include "styles/style_editor.h"

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
, _buttonsContainer(base::make_unique_q<HorizontalContainer>(this))
, _rotateButton(base::make_unique_q<Ui::IconButton>(
	_buttonsContainer,
	st::photoEditorRotateButton))
, _flipButton(base::make_unique_q<Ui::IconButton>(
	_buttonsContainer,
	st::photoEditorFlipButton))
, _paintModeButton(base::make_unique_q<Ui::IconButton>(
	_buttonsContainer,
	st::photoEditorPaintModeButton)) {

	_buttonsContainer->updateChildrenPosition();

	paintRequest(
	) | rpl::start_with_next([=](const QRect &clip) {
		Painter p(this);

		Ui::FillRoundRect(
			p,
			_buttonsContainer->geometry(),
			st::mediaviewSaveMsgBg,
			Ui::MediaviewSaveCorners);

	}, lifetime());

	sizeValue(
	) | rpl::start_with_next([=](const QSize &size) {

		_buttonsContainer->moveToLeft(
			(size.width() - _buttonsContainer->width()) / 2,
			0);

	}, lifetime());

}

rpl::producer<int> PhotoEditorControls::rotateRequests() const {
	return _rotateButton->clicks() | rpl::map([] { return 90; });
}

rpl::producer<> PhotoEditorControls::flipRequests() const {
	return _flipButton->clicks() | rpl::to_empty;
}

rpl::producer<> PhotoEditorControls::paintModeRequests() const {
	return _paintModeButton->clicks() | rpl::to_empty;
}

} // namespace Editor
