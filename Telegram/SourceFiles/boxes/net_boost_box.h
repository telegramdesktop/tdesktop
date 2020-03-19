#pragma once
/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.
For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

namespace Ui {
class RadiobuttonGroup;
class Radiobutton;
class FlatLabel;
} // namespace Ui

class NetBoostBox : public Ui::BoxContent {
public:
	NetBoostBox(QWidget* parent);

	static QString BoostLabel(int boost);

protected:
	void prepare() override;

private:
	void save();

	object_ptr<Ui::FlatLabel> _description = { nullptr };
	std::shared_ptr<Ui::RadiobuttonGroup> _boostGroup;

};