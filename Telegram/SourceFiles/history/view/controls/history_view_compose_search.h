/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class RpWidget;
} // namespace Ui

class History;

namespace HistoryView {

class ComposeSearch final {
public:
	ComposeSearch(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> window,
		not_null<History*> history,
		PeerData *from = nullptr,
		const QString &query = QString());
	~ComposeSearch();

	void hideAnimated();
	void setInnerFocus();
	void setQuery(const QString &query);

	[[nodiscard]] rpl::producer<not_null<HistoryItem*>> activations() const;
	[[nodiscard]] rpl::producer<> destroyRequests() const;

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	class Inner;
	const std::unique_ptr<Inner> _inner;

};

} // namespace HistoryView
