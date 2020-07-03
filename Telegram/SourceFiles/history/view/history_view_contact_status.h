/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/shadow.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class FlatButton;
class IconButton;
} // namespace Ui

namespace HistoryView {

class ContactStatus final {
public:
	ContactStatus(
		not_null<Window::SessionController*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<PeerData*> peer);

	void show();
	void raise();

	void move(int x, int y);
	int height() const;
	rpl::producer<int> heightValue() const;

	rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	enum class State {
		None,
		ReportSpam,
		Add,
		AddOrBlock,
		UnarchiveOrBlock,
		UnarchiveOrReport,
		SharePhoneNumber,
	};

	class Bar : public Ui::RpWidget {
	public:
		Bar(QWidget *parent, const QString &name);

		void showState(State state);

		rpl::producer<> unarchiveClicks() const;
		rpl::producer<> addClicks() const;
		rpl::producer<> blockClicks() const;
		rpl::producer<> shareClicks() const;
		rpl::producer<> reportClicks() const;
		rpl::producer<> closeClicks() const;

	protected:
		void resizeEvent(QResizeEvent *e) override;

	private:
		void updateButtonsGeometry();

		QString _name;
		object_ptr<Ui::FlatButton> _add;
		object_ptr<Ui::FlatButton> _unarchive;
		object_ptr<Ui::FlatButton> _block;
		object_ptr<Ui::FlatButton> _share;
		object_ptr<Ui::FlatButton> _report;
		object_ptr<Ui::IconButton> _close;

	};

	void setupWidgets(not_null<Ui::RpWidget*> parent);
	void setupState(not_null<PeerData*> peer);
	void setupHandlers(not_null<PeerData*> peer);
	void setupAddHandler(not_null<UserData*> user);
	void setupBlockHandler(not_null<UserData*> user);
	void setupShareHandler(not_null<UserData*> user);
	void setupUnarchiveHandler(not_null<PeerData*> peer);
	void setupReportHandler(not_null<PeerData*> peer);
	void setupCloseHandler(not_null<PeerData*> peer);

	static rpl::producer<State> PeerState(not_null<PeerData*> peer);

	const not_null<Window::SessionController*> _controller;
	State _state = State::None;
	Ui::SlideWrap<Bar> _bar;
	Ui::PlainShadow _shadow;
	bool _shown = false;

	rpl::lifetime _lifetime;

};

} // namespace HistoryView
