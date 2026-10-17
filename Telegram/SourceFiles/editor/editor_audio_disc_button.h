/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "ui/effects/animations.h"
#include "ui/widgets/buttons.h"

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Editor {

struct AudioTrack;

class AudioDiscButton final : public Ui::RippleButton {
public:
	explicit AudioDiscButton(QWidget *parent);
	~AudioDiscButton();

	void setTrack(std::shared_ptr<AudioTrack> track);
	void setActive(bool active);

	[[nodiscard]] rpl::producer<> volumeChanges() const;
	[[nodiscard]] rpl::producer<> removeRequests() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	void onStateChanged(State was, StateChangeSource source) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	[[nodiscard]] float64 angle() const;

	std::shared_ptr<AudioTrack> _track;
	QImage _cover;
	Ui::Animations::Basic _spin;
	crl::time _spinStarted = 0;
	float64 _spinBase = 0.;
	bool _active = false;

	base::unique_qptr<Ui::PopupMenu> _menu;
	rpl::event_stream<> _volumeChanges;
	rpl::event_stream<> _removeRequests;

};

} // namespace Editor
