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
class PasscodeBox;

namespace Core {
struct CloudPasswordState;
} // namespace Core

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Payments::Ui {
class Panel;
enum class InformationField;
enum class CardField;
} // namespace Payments::Ui

namespace Payments {

class Form;
struct FormUpdate;
struct Error;

enum class Mode {
	Payment,
	Receipt,
};

class CheckoutProcess final
	: public base::has_weak_ptr
	, private Ui::PanelDelegate {
	struct PrivateTag {};

public:
	static void Start(
		not_null<const HistoryItem*> item,
		Mode mode,
		Fn<void()> reactivate);
	[[nodiscard]] static bool TakePaymentStarted(
		not_null<const HistoryItem*> item);
	static void ClearAll();

	CheckoutProcess(
		not_null<PeerData*> peer,
		MsgId itemId,
		Mode mode,
		Fn<void()> reactivate,
		PrivateTag);
	~CheckoutProcess();

private:
	enum class SubmitState {
		None,
		Validating,
		Validated,
		Finishing,
	};
	[[nodiscard]] not_null<PanelDelegate*> panelDelegate();

	static void RegisterPaymentStart(not_null<CheckoutProcess*> process);
	static void UnregisterPaymentStart(not_null<CheckoutProcess*> process);

	void setReactivateCallback(Fn<void()> reactivate);
	void requestActivate();
	void closeAndReactivate();
	void close();

	void handleFormUpdate(const FormUpdate &update);
	void handleError(const Error &error);

	void showForm();
	void showEditInformation(Ui::InformationField field);
	void showInformationError(Ui::InformationField field);
	void showCardError(Ui::CardField field);
	void chooseShippingOption();
	void chooseTips();
	void editPaymentMethod();

	void requestSetPassword();
	void requestPassword();
	void getPasswordState(
		Fn<void(const Core::CloudPasswordState&)> callback);

	void performInitialSilentValidation();

	void panelRequestClose() override;
	void panelCloseSure() override;
	void panelSubmit() override;
	void panelTrustAndSubmit() override;
	void panelWebviewMessage(
		const QJsonDocument &message,
		bool saveInformation) override;
	bool panelWebviewNavigationAttempt(const QString &uri) override;
	void panelSetPassword() override;
	void panelOpenUrl(const QString &url) override;

	void panelCancelEdit() override;
	void panelEditPaymentMethod() override;
	void panelEditShippingInformation() override;
	void panelEditName() override;
	void panelEditEmail() override;
	void panelEditPhone() override;
	void panelChooseShippingOption() override;
	void panelChangeShippingOption(const QString &id) override;
	void panelChooseTips() override;
	void panelChangeTips(int64 value) override;

	void panelValidateInformation(Ui::RequestedInformation data) override;
	void panelValidateCard(
		Ui::UncheckedCardDetails data,
		bool saveInformation) override;
	void panelShowBox(object_ptr<Ui::BoxContent> box) override;

	QString panelWebviewDataPath() override;

	const not_null<Main::Session*> _session;
	const std::unique_ptr<Form> _form;
	const std::unique_ptr<Ui::Panel> _panel;
	QPointer<PasscodeBox> _enterPasswordBox;
	Fn<void()> _reactivate;
	SubmitState _submitState = SubmitState::None;
	bool _initialSilentValidation = false;

	rpl::lifetime _gettingPasswordState;
	rpl::lifetime _lifetime;

};

} // namespace Payments
