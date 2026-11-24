/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "ui/controls/audio_disc_button.h"

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Editor {

struct AudioTrack;

class AudioDiscButton final : public Ui::AudioDiscButton {
public:
	explicit AudioDiscButton(QWidget *parent);
	~AudioDiscButton();

	void setTrack(std::shared_ptr<AudioTrack> track);

	[[nodiscard]] rpl::producer<> volumeChanges() const;
	[[nodiscard]] rpl::producer<> removeRequests() const;

protected:
	void contextMenuEvent(QContextMenuEvent *e) override;

private:
	std::shared_ptr<AudioTrack> _track;
	base::unique_qptr<Ui::PopupMenu> _menu;
	rpl::event_stream<> _volumeChanges;
	rpl::event_stream<> _removeRequests;

};

} // namespace Editor
