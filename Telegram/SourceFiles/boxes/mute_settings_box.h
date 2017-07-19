/* This file is part of Telegram Desktop, the official desktop version of
 * Telegram messaging app, see https://desktop.telegram.org
 *
 * This code is in Public Domain, see license terms in .github/CONTRIBUTING.md
 * Copyright (C) 2017, Nicholas Guriev <guriev-ns@ya.ru>
 */
#pragma once

#include "boxes/abstract_box.h"

/* This class implements a dialog-box with radio-buttons for pick duration of
 * turning off notifications from a chat. The widget is opened by a context menu
 * in the left list of dialogues. */
class MuteSettingsBox : public BoxContent {
	Q_OBJECT

  public:
	MuteSettingsBox(QWidget *parent, gsl::not_null<PeerData*> peer)
	  : _peer(peer) {
	}

  protected:
	void prepare() override;

  private:
	gsl::not_null<PeerData*> _peer;
};
// vi: ts=4 tw=80
