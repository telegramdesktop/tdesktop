/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

namespace Ui {
class SeparatePanel;
class BoxContent;
} // namespace Ui

namespace Payments::Ui {

using namespace ::Ui;

class PanelDelegate;
struct Invoice;
struct RequestedInformation;
struct ShippingOptions;
enum class EditField;

class Panel final {
public:
	explicit Panel(not_null<PanelDelegate*> delegate);
	~Panel();

	void requestActivate();

	void showForm(
		const Invoice &invoice,
		const RequestedInformation &current,
		const ShippingOptions &options);
	void showEditInformation(
		const Invoice &invoice,
		const RequestedInformation &current,
		EditField field);
	void chooseShippingOption(const ShippingOptions &options);

	void showBox(object_ptr<Ui::BoxContent> box);
	void showToast(const QString &text);

private:
	const not_null<PanelDelegate*> _delegate;
	std::unique_ptr<SeparatePanel> _widget;

};

} // namespace Payments::Ui
