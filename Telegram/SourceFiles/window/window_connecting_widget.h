/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/unique_qptr.h"
#include "ui/effects/animations.h"

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Main {
class Account;
} // namespace Main

namespace Window {

class ConnectionState {
public:
	ConnectionState(
		not_null<Ui::RpWidget*> parent,
		not_null<Main::Account*> account,
		rpl::producer<bool> shown);

	void raise();
	void setForceHidden(bool hidden);

	rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	class Widget;
	struct State {
		enum class Type {
			Connected,
			Connecting,
			Waiting,
		};
		Type type = Type::Connected;
		bool useProxy = false;
		bool underCursor = false;
		bool updateReady = false;
		int waitTillRetry = 0;

		bool operator==(const State &other) const;

	};
	struct Layout {
		bool visible = false;
		bool hasRetry = false;
		bool proxyEnabled = false;
		bool progressShown = false;
		int contentWidth = 0;
		QString text;
		int textWidth = 0;

	};

	void createWidget();
	void finishAnimating();
	void refreshState();
	void applyState(const State &state);
	void changeVisibilityWithLayout(const Layout &layout);
	Layout computeLayout(const State &state) const;
	void setLayout(const Layout &layout);
	float64 currentVisibility() const;
	rpl::producer<float64> visibility() const;
	void updateWidth();
	void updateVisibility();
	void refreshProgressVisibility();

	const not_null<Main::Account*> _account;
	not_null<Ui::RpWidget*> _parent;
	base::unique_qptr<Widget> _widget;
	bool _forceHidden = false;
	base::Timer _refreshTimer;
	State _state;
	Layout _currentLayout;
	crl::time _connectingStartedAt = 0;
	Ui::Animations::Simple _contentWidth;
	Ui::Animations::Simple _visibility;

	rpl::event_stream<float64> _visibilityValues;
	rpl::lifetime _lifetime;

};

} // namespace Window
