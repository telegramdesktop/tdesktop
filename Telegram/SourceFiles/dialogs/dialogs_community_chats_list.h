/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "dialogs/dialogs_community_rows_view.h"
#include "ui/rp_widget.h"

class History;
class PeerData;

namespace style {
struct DialogRow;
} // namespace style

namespace Data {
class CommunityInfo;
} // namespace Data

namespace Window {
class SessionController;
} // namespace Window

namespace Dialogs {

class Row;

enum class CommunityChatsKind : uchar {
	Joined,
	Viewable,
};

class CommunityChatsList final : public Ui::RpWidget {
public:
	CommunityChatsList(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<Data::CommunityInfo*> community,
		CommunityChatsKind kind);
	~CommunityChatsList();

	[[nodiscard]] rpl::producer<not_null<History*>> chatChosen() const;
	[[nodiscard]] rpl::producer<int> countValue() const;

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	void rebuild();
	void updateSelected(QPoint local);
	void setSelected(int selected);
	void setPressed(int pressed);

	const not_null<Window::SessionController*> _controller;
	const not_null<Data::CommunityInfo*> _community;
	const CommunityChatsKind _kind;
	const not_null<const style::DialogRow*> _st;

	CommunityRowsView _view;
	int _selected = -1;
	int _pressed = -1;
	rpl::variable<int> _count = 0;
	rpl::event_stream<not_null<History*>> _chatChosen;

};

} // namespace Dialogs
