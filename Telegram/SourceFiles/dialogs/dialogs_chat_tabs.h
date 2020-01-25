/*
This file is part of Bettergram.

For license and copyright information please follow this link:
https://github.com/bettergram/bettergram/blob/master/LEGAL
*/

#pragma once

#include "dialogs_entry.h"
#include "dialogs/dialogs_chat_tab_button.h"
#include "base/object_ptr.h"
#include "ui/rp_widget.h"

namespace Dialogs {

class ChatTabButton;

class ChatTabs : public Ui::RpWidget {
	Q_OBJECT

public:
	explicit ChatTabs(QWidget *parent);

	void selectTab(const EntryTypes &type);

	const EntryTypes& selectedTab() const;
	void unreadCountChanged(UnreadState counts[4]);

signals:
	void tabSelected(const Dialogs::EntryTypes &type);

public slots:

private slots:
	void onTabClicked(const EntryTypes &type);

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	EntryTypes _type;

	object_ptr<Dialogs::ChatTabButton> _privateButton;
	object_ptr<ChatTabButton> _botButton;
	object_ptr<ChatTabButton> _groupButton;
	object_ptr<ChatTabButton> _channelButton;

	QList<ChatTabButton*> _listButtons;

	void updateControlsGeometry();
};

} // namespace Dialogs
