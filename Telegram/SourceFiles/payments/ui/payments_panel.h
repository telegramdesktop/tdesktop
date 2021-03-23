/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class SeparatePanel;
} // namespace Ui

namespace Payments::Ui {

using namespace ::Ui;

class PanelDelegate;
struct Invoice;

class Panel final {
public:
	explicit Panel(not_null<PanelDelegate*> delegate);
	~Panel();

	void requestActivate();

	void showForm(const Invoice &invoice);

private:
	const not_null<PanelDelegate*> _delegate;
	std::unique_ptr<SeparatePanel> _widget;

};

} // namespace Payments::Ui
