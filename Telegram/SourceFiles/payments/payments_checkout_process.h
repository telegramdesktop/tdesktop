/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "payments/ui/payments_panel_delegate.h"
#include "base/weak_ptr.h"

class HistoryItem;

namespace Main {
class Session;
} // namespace Main

namespace Payments::Ui {
class Panel;
class WebviewWindow;
} // namespace Payments::Ui

namespace Payments {

class Form;
struct FormUpdate;
struct FormError;
struct SendError;

class CheckoutProcess final
	: public base::has_weak_ptr
	, private Ui::PanelDelegate {
	struct PrivateTag {};

public:
	static void Start(not_null<const HistoryItem*> item);

	CheckoutProcess(
		not_null<Main::Session*> session,
		FullMsgId itemId,
		PrivateTag);
	~CheckoutProcess();

	void requestActivate();

private:
	[[nodiscard]] not_null<PanelDelegate*> panelDelegate();

	void handleFormUpdate(const FormUpdate &update);
	void handleFormError(const FormError &error);
	void handleSendError(const SendError &error);

	void panelRequestClose() override;
	void panelCloseSure() override;
	void panelSubmit() override;
	void panelWebviewMessage(const QJsonDocument &message) override;
	bool panelWebviewNavigationAttempt(const QString &uri) override;

	const not_null<Main::Session*> _session;
	const std::unique_ptr<Form> _form;
	const std::unique_ptr<Ui::Panel> _panel;
	std::unique_ptr<Ui::WebviewWindow> _webviewWindow;

	rpl::lifetime _lifetime;

};

} // namespace Payments
