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

namespace Ui {
class RippleAnimation;
} // namespace Ui

namespace HistoryView {

class WebPage : public Media {
public:
	WebPage(
		not_null<Element*> parent,
		not_null<WebPageData*> data,
		MediaWebPageFlags flags);

	[[nodiscard]] static bool HasButton(not_null<WebPageData*> data);

	void refreshParentId(not_null<HistoryItem*> realParent) override;

	void draw(Painter &p, const PaintContext &context) const override;
	TextState textState(QPoint point, StateRequest request) const override;

	bool hideMessageText() const override {
		return false;
	}

	[[nodiscard]] TextSelection adjustSelection(
		TextSelection selection,
		TextSelectType type) const override;
	uint16 fullSelectionLength() const override;
	bool hasTextForCopy() const override {
		// We do not add _title and _description in FullSelection text copy.
		return false;
	}
	QString additionalInfoString() const override;

	bool toggleSelectionByHandlerClick(
		const ClickHandlerPtr &p) const override;
	bool allowTextSelectionByHandler(
		const ClickHandlerPtr &p) const override;
	bool dragItemByHandler(const ClickHandlerPtr &p) const override;

	TextForMimeData selectedText(TextSelection selection) const override;

	void clickHandlerActiveChanged(
		const ClickHandlerPtr &p, bool active) override;
	void clickHandlerPressedChanged(
		const ClickHandlerPtr &p, bool pressed) override;

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

	[[nodiscard]] TextSelection toTitleSelection(
		TextSelection selection) const;
	[[nodiscard]] TextSelection fromTitleSelection(
		TextSelection selection) const;
	[[nodiscard]] TextSelection toDescriptionSelection(
		TextSelection selection) const;
	[[nodiscard]] TextSelection fromDescriptionSelection(
		TextSelection selection) const;
	[[nodiscard]] QMargins inBubblePadding() const;
	[[nodiscard]] QMargins innerMargin() const;
	[[nodiscard]] int bottomInfoPadding() const;
	[[nodiscard]] bool isLogEntryOriginal() const;

	[[nodiscard]] ClickHandlerPtr replaceAttachLink(
		const ClickHandlerPtr &link) const;
	[[nodiscard]] bool asArticle() const;

	const style::QuoteStyle &_st;
	const not_null<WebPageData*> _data;
	std::vector<std::unique_ptr<Data::Media>> _collage;
	ClickHandlerPtr _openl;
	std::unique_ptr<Media> _attach;
	mutable std::shared_ptr<Data::PhotoMedia> _photoMedia;
	mutable std::unique_ptr<Ui::RippleAnimation> _ripple;

	int _dataVersion = -1;
	int _siteNameLines = 0;
	int _descriptionLines = 0;
	uint32 _titleLines : 31 = 0;
	uint32 _asArticle : 1 = 0;

	Ui::Text::String _siteName;
	Ui::Text::String _title;
	Ui::Text::String _description;

	QString _openButton;
	QString _duration;
	int _openButtonWidth = 0;
	int _durationWidth = 0;

	mutable QPoint _lastPoint;
	int _pixw = 0;
	int _pixh = 0;

	const MediaWebPageFlags _flags;

};

} // namespace HistoryView
