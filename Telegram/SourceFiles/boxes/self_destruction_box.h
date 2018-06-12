/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "mtproto/sender.h"

namespace Ui {
class RadiobuttonGroup;
class Radiobutton;
class FlatLabel;
} // namespace Ui

class SelfDestructionBox : public BoxContent, private MTP::Sender {
	Q_OBJECT

public:
	SelfDestructionBox(QWidget*) {
	}

protected:
	void prepare() override;

private:
	std::vector<int> _ttlValues;
	object_ptr<Ui::FlatLabel> _description = { nullptr };
	std::shared_ptr<Ui::RadiobuttonGroup> _ttlGroup;
	std::vector<object_ptr<Ui::Radiobutton>> _options;

};
