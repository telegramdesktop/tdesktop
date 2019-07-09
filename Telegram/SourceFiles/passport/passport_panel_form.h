/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

class BoxContentDivider;

namespace Ui {
class ScrollArea;
class FadeShadow;
class RoundButton;
class FlatLabel;
class UserpicButton;
} // namespace Ui

namespace Passport {

class PanelController;

class PanelForm : public Ui::RpWidget {
public:
	PanelForm(
		QWidget *parent,
		not_null<PanelController*> controller);

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	class Row;

	void setupControls();
	not_null<Ui::RpWidget*> setupContent();
	void updateControlsGeometry();

	not_null<PanelController*> _controller;

	object_ptr<Ui::ScrollArea> _scroll;
	object_ptr<Ui::FadeShadow> _topShadow;
	object_ptr<Ui::FadeShadow> _bottomShadow;
	object_ptr<Ui::RoundButton> _submit;

	QPointer<Ui::UserpicButton> _userpic;
	QPointer<Ui::FlatLabel> _about1;
	QPointer<Ui::FlatLabel> _about2;
	std::vector<QPointer<Row>> _rows;

};

} // namespace Passport
