/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

struct PollData;

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Main {
class Session;
} // namespace Main

class CreatePollBox : public BoxContent {
public:
	CreatePollBox(QWidget*, not_null<Main::Session*> session);

	rpl::producer<PollData> submitRequests() const;
	void submitFailed(const QString &error);

	void setInnerFocus() override;

protected:
	void prepare() override;

private:
	object_ptr<Ui::RpWidget> setupContent();
	not_null<Ui::InputField*> setupQuestion(
		not_null<Ui::VerticalLayout*> container);

	const not_null<Main::Session*> _session;
	Fn<void()> _setInnerFocus;
	Fn<rpl::producer<bool>()> _dataIsValidValue;
	rpl::event_stream<PollData> _submitRequests;

};
