/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/media/history_media.h"

namespace Data {
class Media;
} // namespace Data

class HistoryWebPage : public HistoryMedia {
public:
	HistoryWebPage(
		not_null<Element*> parent,
		not_null<WebPageData*> data);

	void refreshParentId(not_null<HistoryItem*> realParent) override;

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
	TextState textState(QPoint point, StateRequest request) const override;

	bool hideMessageText() const override {
		return false;
	}

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

	bool isDisplayed() const override;
	PhotoData *getPhoto() const override {
		return _attach ? _attach->getPhoto() : nullptr;
	}
	DocumentData *getDocument() const override {
		return _attach ? _attach->getDocument() : nullptr;
	}
	void stopAnimation() override {
		if (_attach) _attach->stopAnimation();
	}

	not_null<WebPageData*> webpage() {
		return _data;
	}

	bool needsBubble() const override {
		return true;
	}
	bool customInfoLayout() const override {
		return false;
	}
	bool allowsFastShare() const override {
		return true;
	}

	HistoryMedia *attach() const {
		return _attach.get();
	}

	~HistoryWebPage();

private:
	void playAnimation(bool autoplay) override;
	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	TextSelection toDescriptionSelection(TextSelection selection) const;
	TextSelection fromDescriptionSelection(TextSelection selection) const;
	QMargins inBubblePadding() const;
	int bottomInfoPadding() const;
	bool isLogEntryOriginal() const;

	not_null<WebPageData*> _data;
	std::vector<std::unique_ptr<Data::Media>> _collage;
	ClickHandlerPtr _openl;
	std::unique_ptr<HistoryMedia> _attach;

	bool _asArticle = false;
	int _dataVersion = -1;
	int _titleLines = 0;
	int _descriptionLines = 0;

	Text _title, _description;
	int _siteNameWidth = 0;

	QString _duration;
	int _durationWidth = 0;

	int _pixw = 0;
	int _pixh = 0;

};
