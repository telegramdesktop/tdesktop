/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/history_view_element.h"

class HistoryMessage;

namespace HistoryView {

class Message : public Element {
public:
	Message(not_null<HistoryMessage*> data, Context context);

	void draw(
		Painter &p,
		QRect clip,
		TextSelection selection,
		TimeMs ms) const override;
	bool hasPoint(QPoint point) const override;
	HistoryTextState getState(
		QPoint point,
		HistoryStateRequest request) const override;
	void updatePressed(QPoint point) override;

	// hasFromPhoto() returns true even if we don't display the photo
	// but we need to skip a place at the left side for this photo
	bool displayFromPhoto() const override;
	bool hasFromPhoto() const override;

	bool hasFromName() const override;
	bool displayFromName() const override;

private:
	not_null<HistoryMessage*> message() const;

	void fromNameUpdated(int width) const;

	void paintFromName(Painter &p, QRect &trect, bool selected) const;
	void paintForwardedInfo(Painter &p, QRect &trect, bool selected) const;
	void paintReplyInfo(Painter &p, QRect &trect, bool selected) const;
	// this method draws "via @bot" if it is not painted in forwarded info or in from name
	void paintViaBotIdInfo(Painter &p, QRect &trect, bool selected) const;
	void paintText(Painter &p, QRect &trect, TextSelection selection) const;

	bool getStateFromName(
		QPoint point,
		QRect &trect,
		not_null<HistoryTextState*> outResult) const;
	bool getStateForwardedInfo(
		QPoint point,
		QRect &trect,
		not_null<HistoryTextState*> outResult,
		HistoryStateRequest request) const;
	bool getStateReplyInfo(
		QPoint point,
		QRect &trect,
		not_null<HistoryTextState*> outResult) const;
	bool getStateViaBotIdInfo(
		QPoint point,
		QRect &trect,
		not_null<HistoryTextState*> outResult) const;
	bool getStateText(
		QPoint point,
		QRect &trect,
		not_null<HistoryTextState*> outResult,
		HistoryStateRequest request) const;

	void updateMediaInBubbleState();
	QRect countGeometry() const;

	int resizeContentGetHeight(int newWidth);
	QSize performCountOptimalSize() override;
	QSize performCountCurrentSize(int newWidth) override;

};

} // namespace HistoryView
