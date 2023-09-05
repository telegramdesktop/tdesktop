/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "ui/countryinput.h"
#include "intro/intro_step.h"
#include "base/timer.h"

namespace Intro {
namespace details {

class QrWidget final : public Step {
public:
	QrWidget(
		QWidget *parent,
		not_null<Main::Account*> account,
		not_null<Data*> data);

	void activate() override;
	void finished() override;
	void cancelled() override;
	void submit() override;
	rpl::producer<QString> nextButtonText() const override;

	bool hasBack() const override {
		return true;
	}

private:
	int errorTop() const override;

	void sendCheckPasswordRequest();
	void setupControls();
	void refreshCode();
	void checkForTokenUpdate(const MTPUpdates &updates);
	void checkForTokenUpdate(const MTPUpdate &update);
	void handleTokenResult(const MTPauth_LoginToken &result);
	void showTokenError(const MTP::Error &error);
	void importTo(MTP::DcId dcId, const QByteArray &token);
	void showToken(const QByteArray &token);
	void done(const MTPauth_Authorization &authorization);

	rpl::event_stream<QByteArray> _qrCodes;
	base::Timer _refreshTimer;
	mtpRequestId _requestId = 0;
	bool _forceRefresh = false;

};

[[nodiscard]] QImage TelegramLogoImage();

} // namespace details
} // namespace Intro
