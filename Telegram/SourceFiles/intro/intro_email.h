/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "intro/intro_step.h"

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace MTP {
class Sender;
} // namespace MTP

namespace Intro {
namespace details {

class EmailWidget final : public Step {
public:
	EmailWidget(
		QWidget *parent,
		not_null<Main::Account*> account,
		not_null<Data*> data);

	void setInnerFocus() override;
	void activate() override;
	void finished() override;
	void cancelled() override;
	void submit() override;

	bool hasBack() const override {
		return true;
	}

private:
	object_ptr<Ui::VerticalLayout> _inner;
	Fn<void()> _submitCallback = nullptr;
	rpl::event_stream<> _showFinished;
	rpl::event_stream<> _setFocus;
	mtpRequestId _sentRequest = 0;

};

} // namespace details
} // namespace Intro
