/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/media/history_media.h"

namespace Data {
struct Invoice;
} // namespace Data

class HistoryInvoice : public HistoryMedia {
public:
	HistoryInvoice(
		not_null<Element*> parent,
		not_null<Data::Invoice*> invoice);

	void refreshParentId(not_null<HistoryItem*> realParent) override;

	MsgId getReceiptMsgId() const {
		return _receiptMsgId;
	}
	QString getTitle() const {
		return _title.originalText();
	}

	bool hideMessageText() const override {
		return false;
	}

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
	TextState textState(QPoint point, StateRequest request) const override;

	[[nodiscard]] TextSelection adjustSelection(
		TextSelection selection,
		TextSelectType type) const override;
	uint16 fullSelectionLength() const override {
		return _title.length() + _description.length();
	}
	bool hasTextForCopy() const override {
		return false; // we do not add _title and _description in FullSelection text copy.
	}

	bool toggleSelectionByHandlerClick(const ClickHandlerPtr &p) const override {
		return _attach && _attach->toggleSelectionByHandlerClick(p);
	}
	bool dragItemByHandler(const ClickHandlerPtr &p) const override {
		return _attach && _attach->dragItemByHandler(p);
	}

	TextWithEntities selectedText(TextSelection selection) const override;

	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) override;

	bool needsBubble() const override {
		return true;
	}
	bool customInfoLayout() const override {
		return false;
	}

	HistoryMedia *attach() const {
		return _attach.get();
	}

private:
	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	void fillFromData(not_null<Data::Invoice*> invoice);

	TextSelection toDescriptionSelection(TextSelection selection) const;
	TextSelection fromDescriptionSelection(TextSelection selection) const;
	QMargins inBubblePadding() const;
	int bottomInfoPadding() const;

	std::unique_ptr<HistoryMedia> _attach;

	int _titleHeight = 0;
	int _descriptionHeight = 0;
	Text _title;
	Text _description;
	Text _status;

	MsgId _receiptMsgId = 0;

};

QString FillAmountAndCurrency(uint64 amount, const QString &currency);
