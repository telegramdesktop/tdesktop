/*
This file is part of Bettergram.

For license and copyright information please follow this link:
https://github.com/bettergram/bettergram/blob/master/LEGAL
*/

#pragma once

#include "dialogs_entry.h"

namespace Dialogs {

class ChatTabButton;

class ChatTabs : public TWidget {
	Q_OBJECT

public:
	explicit ChatTabs(QWidget *parent);

	void selectTab(const EntryTypes &type);

	const EntryTypes& selectedTab() const;

signals:
	void tabSelected(const Dialogs::EntryTypes &type);

public slots:

private slots:
	void onTabClicked(const EntryTypes &type);

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	EntryTypes _type;

	object_ptr<ChatTabButton> _oneOnOneButton;
	object_ptr<ChatTabButton> _botButton;
	object_ptr<ChatTabButton> _groupButton;
	object_ptr<ChatTabButton> _announcementButton;

	QList<ChatTabButton*> _listButtons;

	void updateControlsGeometry();
};

} // namespace Dialogs
