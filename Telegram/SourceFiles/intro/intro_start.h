/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "intro/intro_step.h"

namespace Ui {
class FlatLabel;
class LinkButton;
class RoundButton;
} // namespace Ui

namespace Intro {
namespace details {

class StartWidget : public Step {
public:
	StartWidget(
		QWidget *parent,
		not_null<Main::Account*> account,
		not_null<Data*> data);

	void submit() override;
	rpl::producer<QString> nextButtonText() const override;

};

} // namespace details
} // namespace Intro
