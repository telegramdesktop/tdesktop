/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"

namespace style {
struct PeerList;
} // namespace style

class PeerListDummy final : public Ui::RpWidget {
public:
	PeerListDummy(QWidget *parent, int count, const style::PeerList &st);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const style::PeerList &_st;
	int _count = 0;

	std::vector<Ui::Animations::Simple> _animations;

};
