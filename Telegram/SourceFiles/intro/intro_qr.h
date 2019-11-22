/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/countryinput.h"
#include "intro/introwidget.h"
#include "mtproto/sender.h"
#include "base/timer.h"

namespace Ui {
class PhonePartInput;
class CountryCodeInput;
class RoundButton;
class FlatLabel;
} // namespace Ui

namespace Intro {

class QrWidget : public Widget::Step {
public:
	QrWidget(
		QWidget *parent,
		not_null<Main::Account*> account,
		not_null<Widget::Data*> data);

	void activate() override;
	void finished() override;
	void cancelled() override;
	void submit() override;
	rpl::producer<QString> nextButtonText() const override;

	bool hasBack() const override {
		return true;
	}

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	void refreshCode();
	void updateCodeGeometry();
	void checkForTokenUpdate(const MTPUpdates &updates);
	void checkForTokenUpdate(const MTPUpdate &update);
	void handleTokenResult(const MTPauth_LoginToken &result);
	void showTokenError(const RPCError &error);
	void importTo(MTP::DcId dcId, const QByteArray &token);
	void showToken(const QByteArray &token);
	void done(const MTPauth_Authorization &authorization);

	rpl::event_stream<QImage> _qrImages;
	not_null<Ui::RpWidget*> _code;
	base::Timer _refreshTimer;
	MTP::Sender _api;
	mtpRequestId _requestId = 0;
	bool _forceRefresh = false;

};

} // namespace Intro
