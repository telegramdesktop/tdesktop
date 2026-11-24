/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"

class DocumentData;

namespace Ui {
class AudioDiscButton;
class PopupMenu;
} // namespace Ui

namespace Media::Stories {

class Controller;

class MusicDisc final {
public:
	MusicDisc(
		not_null<Controller*> controller,
		not_null<DocumentData*> music,
		FullStoryId story);
	~MusicDisc();

	void setPlaying(bool playing);

private:
	void showMenu();

	const not_null<Controller*> _controller;
	const not_null<DocumentData*> _music;
	const FullStoryId _story;
	const QString _title;
	const base::unique_qptr<Ui::AudioDiscButton> _button;
	base::unique_qptr<Ui::PopupMenu> _menu;

};

} // namespace Media::Stories
