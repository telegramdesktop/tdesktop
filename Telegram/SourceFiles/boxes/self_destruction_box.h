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
public:
	SelfDestructionBox(QWidget*, rpl::producer<int> preloaded);

	static QString DaysLabel(int days);

protected:
	void prepare() override;

private:
	void gotCurrent(int days);
	void showContent();

	bool _prepared = false;
	std::vector<int> _ttlValues;
	object_ptr<Ui::FlatLabel> _description = { nullptr };
	object_ptr<Ui::FlatLabel> _loading;
	std::shared_ptr<Ui::RadiobuttonGroup> _ttlGroup;

};
