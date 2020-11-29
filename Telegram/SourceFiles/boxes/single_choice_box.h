/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include <vector>

namespace Ui {
class Radiobutton;
} // namespace Ui

namespace style {
struct Checkbox;
} // namespace style

class SingleChoiceBox : public Ui::BoxContent {
public:
	SingleChoiceBox(
		QWidget*,
		rpl::producer<QString> title,
		const std::vector<QString> &optionTexts,
		int initialSelection,
		Fn<void(int)> callback,
		const style::Checkbox *st = nullptr,
		const style::Radio *radioSt = nullptr);

protected:
	void prepare() override;

private:
	rpl::producer<QString> _title;
	std::vector<QString> _optionTexts;
	int _initialSelection = 0;
	Fn<void(int)> _callback;
	const style::Checkbox &_st;
	const style::Radio &_radioSt;

};

