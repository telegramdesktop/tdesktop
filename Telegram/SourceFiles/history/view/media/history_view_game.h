/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_media.h"

class ReplyMarkupClickHandler;

namespace Ui {
class RippleAnimation;
} // namespace Ui

namespace HistoryView {

class Game : public Media {
public:
	Game(
		not_null<Element*> parent,
		not_null<GameData*> data,
		const TextWithEntities &consumed);

	void refreshParentId(not_null<HistoryItem*> realParent) override;

	void draw(Painter &p, const PaintContext &context) const override;
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

	bool toggleSelectionByHandlerClick(
		const ClickHandlerPtr &p) const override;
	bool allowTextSelectionByHandler(
		const ClickHandlerPtr &p) const override;
	bool dragItemByHandler(const ClickHandlerPtr &p) const override;

	TextForMimeData selectedText(TextSelection selection) const override;

	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) override;

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

	not_null<GameData*> game() {
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

	Media *attach() const {
		return _attach.get();
	}

	void parentTextUpdated() override;

	bool hasHeavyPart() const override;
	void unloadHeavyPart() override;

	~Game();

private:
	void playAnimation(bool autoplay) override;
	[[nodiscard]] QSize countOptimalSize() override;
	[[nodiscard]] QSize countCurrentSize(int newWidth) override;

	[[nodiscard]] TextSelection toDescriptionSelection(
		TextSelection selection) const;
	[[nodiscard]] TextSelection fromDescriptionSelection(
		TextSelection selection) const;
	[[nodiscard]] QMargins inBubblePadding() const;
	[[nodiscard]] QMargins innerMargin() const;
	[[nodiscard]] int bottomInfoPadding() const;

	const style::QuoteStyle &_st;
	const not_null<GameData*> _data;
	std::shared_ptr<ReplyMarkupClickHandler> _openl;
	std::unique_ptr<Media> _attach;
	mutable std::unique_ptr<Ui::RippleAnimation> _ripple;

	mutable QPoint _lastPoint;
	int _gameTagWidth = 0;
	int _descriptionLines = 0;
	int _titleLines = 0;

	Ui::Text::String _title;
	Ui::Text::String _description;

};

} // namespace HistoryView
