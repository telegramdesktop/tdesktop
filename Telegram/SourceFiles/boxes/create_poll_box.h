/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/box_content.h"
#include "api/api_common.h"
#include "data/data_poll.h"
#include "base/flags.h"

struct PollData;

namespace ChatHelpers {
class TabbedPanel;
} // namespace ChatHelpers

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace SendMenu {
struct Details;
} // namespace SendMenu

class CreatePollBox : public Ui::BoxContent {
public:
	struct Result {
		PollData poll;
		Api::SendOptions options;
	};

	CreatePollBox(
		QWidget*,
		not_null<Window::SessionController*> controller,
		PollData::Flags chosen,
		PollData::Flags disabled,
		Api::SendType sendType,
		SendMenu::Details sendMenuDetails);

	[[nodiscard]] rpl::producer<Result> submitRequests() const;
	void submitFailed(const QString &error);

	void setInnerFocus() override;

protected:
	void prepare() override;

private:
	enum class Error {
		Question = 0x01,
		Options  = 0x02,
		Correct  = 0x04,
		Other    = 0x08,
		Solution = 0x10,
	};
	friend constexpr inline bool is_flag_type(Error) { return true; }
	using Errors = base::flags<Error>;

	[[nodiscard]] object_ptr<Ui::RpWidget> setupContent();
	[[nodiscard]] not_null<Ui::InputField*> setupQuestion(
		not_null<Ui::VerticalLayout*> container);
	[[nodiscard]] not_null<Ui::InputField*> setupSolution(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<bool> shown);

	const not_null<Window::SessionController*> _controller;
	const PollData::Flags _chosen = PollData::Flags();
	const PollData::Flags _disabled = PollData::Flags();
	const Api::SendType _sendType = Api::SendType();
	const Fn<SendMenu::Details()> _sendMenuDetails;
	base::unique_qptr<ChatHelpers::TabbedPanel> _emojiPanel;
	Fn<void()> _setInnerFocus;
	Fn<rpl::producer<bool>()> _dataIsValidValue;
	rpl::event_stream<Result> _submitRequests;

};
