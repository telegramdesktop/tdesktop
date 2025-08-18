/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/generic_box.h"

void AboutBox(not_null<Ui::GenericBox*> box);
void ArchiveHintBox(
	not_null<Ui::GenericBox*> box,
	bool unarchiveOnNewMessage,
	Fn<void()> onUnarchive);

QString telegramFaqLink();
QString currentVersionText();
