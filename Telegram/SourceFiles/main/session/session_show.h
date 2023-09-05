/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "ui/layers/show.h"

namespace Main {

class Session;

class SessionShow : public Ui::Show {
public:
	[[nodiscard]] virtual Main::Session &session() const = 0;
};

[[nodiscard]] std::shared_ptr<SessionShow> MakeSessionShow(
	std::shared_ptr<Ui::Show> show,
	not_null<Session*> session);

} // namespace Main
