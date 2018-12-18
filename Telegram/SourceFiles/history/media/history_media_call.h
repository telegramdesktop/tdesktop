/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/media/history_media.h"

namespace Data {
enum class CallFinishReason : char;
struct Call;
} // namespace Data

class HistoryCall : public HistoryMedia {
public:
	HistoryCall(
		not_null<Element*> parent,
		not_null<Data::Call*> call);

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
	TextState textState(QPoint point, StateRequest request) const override;

	bool toggleSelectionByHandlerClick(const ClickHandlerPtr &p) const override {
		return true;
	}
	bool dragItemByHandler(const ClickHandlerPtr &p) const override {
		return false;
	}

	bool needsBubble() const override {
		return true;
	}
	bool customInfoLayout() const override {
		return true;
	}

	Data::CallFinishReason reason() const;

private:
	using FinishReason = Data::CallFinishReason;

	QSize countOptimalSize() override;

	FinishReason _reason;
	int _duration = 0;

	QString _text;
	QString _status;

	ClickHandlerPtr _link;

};
