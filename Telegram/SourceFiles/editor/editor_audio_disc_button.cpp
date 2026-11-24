/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_audio_disc_button.h"

#include "editor/editor_audio_menu.h"
#include "editor/photo_editor_common.h"
#include "ui/widgets/popup_menu.h"

#include <QtGui/QContextMenuEvent>

namespace Editor {

AudioDiscButton::AudioDiscButton(QWidget *parent)
: Ui::AudioDiscButton(parent, FallbackRing::Full) {
}

AudioDiscButton::~AudioDiscButton() = default;

void AudioDiscButton::setTrack(std::shared_ptr<AudioTrack> track) {
	if (_track == track) {
		return;
	}
	_track = std::move(track);
	setCover(_track ? _track->cover : QImage());
}

rpl::producer<> AudioDiscButton::volumeChanges() const {
	return _volumeChanges.events();
}

rpl::producer<> AudioDiscButton::removeRequests() const {
	return _removeRequests.events();
}

void AudioDiscButton::contextMenuEvent(QContextMenuEvent *e) {
	if (!_track) {
		return;
	}
	_menu = CreateAudioMenu(
		this,
		_track,
		[=] { _volumeChanges.fire({}); },
		[=] { _removeRequests.fire({}); });
	_menu->popup(e->globalPos());
	e->accept();
}

} // namespace Editor
