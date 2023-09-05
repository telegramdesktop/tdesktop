/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "ui/layers/generic_box.h"

class UserData;

namespace Window {
class SessionController;
} // namespace Window

void EditContactBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> window,
	not_null<UserData*> user);
