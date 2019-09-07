/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "api/api_common.h"
#include "data/data_poll.h"

struct PollData;

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Main {
class Session;
} // namespace Main

class CreatePollBox : public BoxContent {
public:
	struct Result {
		PollData poll;
		Api::SendOptions options;
	};

	CreatePollBox(
		QWidget*,
		not_null<Main::Session*> session,
		Api::SendType sendType);

	rpl::producer<Result> submitRequests() const;
	void submitFailed(const QString &error);

	void setInnerFocus() override;

protected:
	void prepare() override;

private:
	object_ptr<Ui::RpWidget> setupContent();
	not_null<Ui::InputField*> setupQuestion(
		not_null<Ui::VerticalLayout*> container);

	const not_null<Main::Session*> _session;
	const Api::SendType _sendType = Api::SendType();
	Fn<void()> _setInnerFocus;
	Fn<rpl::producer<bool>()> _dataIsValidValue;
	rpl::event_stream<Result> _submitRequests;

};
