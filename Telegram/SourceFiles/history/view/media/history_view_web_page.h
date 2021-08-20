/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_media.h"

namespace Data {
class Media;
class PhotoMedia;
} // namespace Data

namespace HistoryView {

class WebPage : public Media {
public:
	WebPage(
		not_null<Element*> parent,
		not_null<WebPageData*> data);

	void refreshParentId(not_null<HistoryItem*> realParent) override;

	void draw(Painter &p, const PaintContext &context) const override;
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

	TextForMimeData selectedText(TextSelection selection) const override;

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
	void checkAnimation() override {
		if (_attach) _attach->checkAnimation();
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
	bool enforceBubbleWidth() const override;

	Media *attach() const {
		return _attach.get();
	}

	bool hasHeavyPart() const override;
	void unloadHeavyPart() override;

	~WebPage();

private:
	void playAnimation(bool autoplay) override;
	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	void ensurePhotoMediaCreated() const;

	TextSelection toTitleSelection(TextSelection selection) const;
	TextSelection fromTitleSelection(TextSelection selection) const;
	TextSelection toDescriptionSelection(TextSelection selection) const;
	TextSelection fromDescriptionSelection(TextSelection selection) const;
	QMargins inBubblePadding() const;
	int bottomInfoPadding() const;
	bool isLogEntryOriginal() const;

	QString displayedSiteName() const;
	ClickHandlerPtr replaceAttachLink(const ClickHandlerPtr &link) const;
	bool asArticle() const;

	not_null<WebPageData*> _data;
	std::vector<std::unique_ptr<Data::Media>> _collage;
	ClickHandlerPtr _openl;
	std::unique_ptr<Media> _attach;
	mutable std::shared_ptr<Data::PhotoMedia> _photoMedia;

	bool _asArticle = false;
	int _dataVersion = -1;
	int _siteNameLines = 0;
	int _titleLines = 0;
	int _descriptionLines = 0;

	Ui::Text::String _siteName, _title, _description;

	QString _duration;
	int _durationWidth = 0;

	int _pixw = 0;
	int _pixh = 0;

};

} // namespace HistoryView
