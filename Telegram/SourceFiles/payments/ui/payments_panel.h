/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

namespace Ui {
class RpWidget;
class SeparatePanel;
class BoxContent;
class Checkbox;
} // namespace Ui

namespace Webview {
class Window;
} // namespace Webview

namespace Payments::Ui {

using namespace ::Ui;

class PanelDelegate;
struct Invoice;
struct RequestedInformation;
struct ShippingOptions;
enum class InformationField;
enum class CardField;
class FormSummary;
class EditInformation;
class EditCard;
struct PaymentMethodDetails;
struct NativeMethodDetails;

class Panel final {
public:
	explicit Panel(not_null<PanelDelegate*> delegate);
	~Panel();

	void requestActivate();

	void showForm(
		const Invoice &invoice,
		const RequestedInformation &current,
		const PaymentMethodDetails &method,
		const ShippingOptions &options);
	void updateFormThumbnail(const QImage &thumbnail);
	void showEditInformation(
		const Invoice &invoice,
		const RequestedInformation &current,
		InformationField field);
	void showInformationError(
		const Invoice &invoice,
		const RequestedInformation &current,
		InformationField field);
	void showEditPaymentMethod(const PaymentMethodDetails &method);
	void showEditCard(
		const NativeMethodDetails &native,
		CardField field);
	void showCardError(
		const NativeMethodDetails &native,
		CardField field);
	void chooseShippingOption(const ShippingOptions &options);
	void chooseTips(const Invoice &invoice);
	void choosePaymentMethod(const PaymentMethodDetails &method);
	void askSetPassword();

	bool showWebview(
		const QString &url,
		bool allowBack,
		rpl::producer<QString> bottomText);

	[[nodiscard]] rpl::producer<> backRequests() const;

	void showBox(object_ptr<Ui::BoxContent> box);
	void showToast(const TextWithEntities &text);

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	bool createWebview();

	const not_null<PanelDelegate*> _delegate;
	std::unique_ptr<SeparatePanel> _widget;
	std::unique_ptr<Webview::Window> _webview;
	std::unique_ptr<RpWidget> _webviewBottom;
	QPointer<Checkbox> _saveWebviewInformation;
	QPointer<FormSummary> _weakFormSummary;
	QPointer<EditInformation> _weakEditInformation;
	QPointer<EditCard> _weakEditCard;

};

} // namespace Payments::Ui
