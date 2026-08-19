/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {
struct StoryAlbum;
} // namespace Data

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Window {
class SessionNavigation;
} // namespace Window

namespace Info::Stories {

void NewAlbumBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionNavigation*> navigation,
	not_null<PeerData*> peer,
	StoryId addId,
	Fn<void(Data::StoryAlbum)> added);

void EditAlbumNameBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionNavigation*> navigation,
	not_null<PeerData*> peer,
	int id,
	QString current,
	Fn<void(QString)> done);

} // namespace Info::Stories
