/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

namespace Ui {
class VerticalLayout;
} // namespace Ui

struct PollData;

class CreatePollBox : public BoxContent {
public:
	CreatePollBox(QWidget*);

	rpl::producer<PollData> submitRequests() const;
	void submitFailed(const QString &error);

	void setInnerFocus() override;

protected:
	void prepare() override;

private:
	object_ptr<Ui::RpWidget> setupContent();
	not_null<Ui::InputField*> setupQuestion(
		not_null<Ui::VerticalLayout*> container);

	Fn<void()> _setInnerFocus;
	Fn<rpl::producer<bool>()> _dataIsValidValue;
	rpl::event_stream<PollData> _submitRequests;

};
