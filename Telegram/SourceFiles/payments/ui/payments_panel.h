/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "base/weak_ptr.h"

namespace Ui {
class Show;
class RpWidget;
class SeparatePanel;
class BoxContent;
class Checkbox;
} // namespace Ui

namespace Webview {
struct Available;
struct ThemeParams;
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
struct PaymentMethodAdditional;
struct NativeMethodDetails;

class Panel final : public base::has_weak_ptr {
public:
	explicit Panel(not_null<PanelDelegate*> delegate);
	~Panel();

	void requestActivate();
	void toggleProgress(bool shown);

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
	void showAdditionalMethod(
		const PaymentMethodAdditional &method,
		const QString &provider,
		bool canSaveInformation);
	void showEditCard(const NativeMethodDetails &native, CardField field);
	void showEditCardByUrl(
		const QString &url,
		const QString &provider,
		bool canSaveInformation);
	void showCardError(const NativeMethodDetails &native, CardField field);
	void chooseShippingOption(const ShippingOptions &options);
	void chooseTips(const Invoice &invoice);
	void choosePaymentMethod(const PaymentMethodDetails &method);
	void askSetPassword();
	void showCloseConfirm();
	void showWarning(const QString &bot, const QString &provider);
	void requestTermsAcceptance(
		const QString &username,
		const QString &url,
		bool recurring);

	bool showWebview(
		const QString &url,
		bool allowBack,
		rpl::producer<QString> bottomText);
	void updateThemeParams(const Webview::ThemeParams &params);

	[[nodiscard]] rpl::producer<> backRequests() const;
	[[nodiscard]] rpl::producer<QString> savedMethodChosen() const;

	void showBox(object_ptr<Ui::BoxContent> box);
	void showToast(TextWithEntities &&text);
	void showCriticalError(const TextWithEntities &text);
	[[nodiscard]] std::shared_ptr<Show> uiShow();

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	struct Progress;
	struct WebviewWithLifetime;

	bool createWebview(const Webview::ThemeParams &params);
	void showWebviewProgress();
	void hideWebviewProgress();
	void showWebviewError(
		const QString &text,
		const Webview::Available &information);
	void setTitle(rpl::producer<QString> title);

	[[nodiscard]] bool progressWithBackground() const;
	[[nodiscard]] QRect progressRect() const;
	void setupProgressGeometry();
	void updateFooterHeight();

	const not_null<PanelDelegate*> _delegate;
	std::unique_ptr<SeparatePanel> _widget;
	std::unique_ptr<WebviewWithLifetime> _webview;
	std::unique_ptr<RpWidget> _webviewBottom;
	rpl::variable<int> _footerHeight;
	std::unique_ptr<Progress> _progress;
	QPointer<Checkbox> _saveWebviewInformation;
	QPointer<FormSummary> _weakFormSummary;
	rpl::variable<int> _formScrollTop;
	QPointer<EditInformation> _weakEditInformation;
	QPointer<EditCard> _weakEditCard;
	rpl::event_stream<QString> _savedMethodChosen;
	bool _themeUpdateScheduled = false;
	bool _webviewProgress = false;
	bool _testMode = false;

};

} // namespace Payments::Ui
