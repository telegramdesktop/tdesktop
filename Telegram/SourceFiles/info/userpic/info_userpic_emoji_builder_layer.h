/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/layer_widget.h"

#include "ui/cached_round_corners.h"

namespace Ui {
class RpWidget;
} // namespace Ui

namespace UserpicBuilder {

class LayerWidget : public Ui::LayerWidget {
public:
	LayerWidget();

	void setContent(not_null<Ui::RpWidget*> content);

	void parentResized() override;
	bool closeByOutsideClick() const override;

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	const Ui::CornersPixmaps _corners;
	Ui::RpWidget *_content;

};

} // namespace UserpicBuilder
