/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "ui/layers/generic_box.h"

class History;

namespace Window {
class SessionController;
} // namespace Window

void NewForumTopicBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	not_null<History*> forum);

void EditForumTopicBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	not_null<History*> forum,
	MsgId rootId);
