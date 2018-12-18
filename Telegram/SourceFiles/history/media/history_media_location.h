/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/media/history_media.h"

class LocationCoords;
struct LocationData;

class HistoryLocation : public HistoryMedia {
public:
	HistoryLocation(
		not_null<Element*> parent,
		not_null<LocationData*> location,
		const QString &title = QString(),
		const QString &description = QString());

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
	TextState textState(QPoint point, StateRequest request) const override;

	[[nodiscard]] TextSelection adjustSelection(
		TextSelection selection,
		TextSelectType type) const override;
	uint16 fullSelectionLength() const override {
		return _title.length() + _description.length();
	}
	bool hasTextForCopy() const override {
		return !_title.isEmpty() || !_description.isEmpty();
	}

	bool toggleSelectionByHandlerClick(const ClickHandlerPtr &p) const override {
		return p == _link;
	}
	bool dragItemByHandler(const ClickHandlerPtr &p) const override {
		return p == _link;
	}

	TextWithEntities selectedText(TextSelection selection) const override;

	bool needsBubble() const override;
	bool customInfoLayout() const override {
		return true;
	}

	bool skipBubbleTail() const override {
		return isBubbleBottom();
	}

private:
	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	TextSelection toDescriptionSelection(TextSelection selection) const;
	TextSelection fromDescriptionSelection(TextSelection selection) const;

	LocationData *_data;
	Text _title, _description;
	ClickHandlerPtr _link;

	int fullWidth() const;
	int fullHeight() const;

};
