/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/abstract_button.h"
#include "base/timer.h"

namespace Ui {
class LinkButton;
} // namespace Ui

namespace Window {

class ConnectingWidget
	: public Ui::AbstractButton
	, private base::Subscriber {
public:
	ConnectingWidget(QWidget *parent);

	rpl::producer<float64> visibility() const;

	void finishAnimating();
	void setForceHidden(bool hidden);
	void setVisibleHook(bool visible) override;

	static base::unique_qptr<ConnectingWidget> CreateDefaultWidget(
		Ui::RpWidget *parent,
		rpl::producer<bool> shown);

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(State was, StateChangeSource source) override;

private:
	class ProxyIcon;
	struct State {
		enum class Type {
			Connected,
			Connecting,
			Waiting,
		};
		Type type = Type::Connected;
		bool useProxy = false;
		bool underCursor = false;
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
	void updateRetryGeometry();
	void updateWidth();
	void updateVisibility();
	void refreshState();
	void applyState(const State &state);
	void changeVisibilityWithLayout(const Layout &layout);
	void refreshRetryLink(bool hasRetry);
	Layout computeLayout(const State &state) const;
	void setLayout(const Layout &layout);
	float64 currentVisibility() const;

	QRect innerRect() const;
	QRect contentRect() const;
	QRect textRect() const;

	base::Timer _refreshTimer;
	State _state;
	Layout _currentLayout;
	TimeMs _connectingStartedAt = 0;
	Animation _contentWidth;
	Animation _visibility;
	base::unique_qptr<Ui::LinkButton> _retry;
	QPointer<Ui::RpWidget> _progress;
	QPointer<ProxyIcon> _proxyIcon;
	bool _forceHidden = false;
	bool _realHidden = false;

	rpl::event_stream<float64> _visibilityValues;

};

rpl::producer<bool> AdaptiveIsOneColumn();

} // namespace Window
