/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_sponsored_click_handler.h"

#include "core/click_handler_types.h"

namespace HistoryView {

ClickHandlerPtr SponsoredLink(const QString &link, bool isInternal) {
	class ClickHandler final : public UrlClickHandler {
	public:
		ClickHandler(const QString &link, bool isInternal)
		: UrlClickHandler(link, false)
		, _isInternal(isInternal) {
		}

		QString copyToClipboardContextItemText() const override final {
			return QString();
		}

		QString tooltip() const override final {
			return _isInternal ? QString() : url();
		}

	private:
		const bool _isInternal;

	};

	return std::make_shared<ClickHandler>(link, isInternal);
}

} // namespace HistoryView
