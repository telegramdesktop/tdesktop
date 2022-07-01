/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_media.h"

class ReplyMarkupClickHandler;

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

	bool toggleSelectionByHandlerClick(const ClickHandlerPtr &p) const override {
		return _attach && _attach->toggleSelectionByHandlerClick(p);
	}
	bool dragItemByHandler(const ClickHandlerPtr &p) const override {
		return _attach && _attach->dragItemByHandler(p);
	}

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
	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	TextSelection toDescriptionSelection(TextSelection selection) const;
	TextSelection fromDescriptionSelection(TextSelection selection) const;
	QMargins inBubblePadding() const;
	int bottomInfoPadding() const;

	not_null<GameData*> _data;
	std::shared_ptr<ReplyMarkupClickHandler> _openl;
	std::unique_ptr<Media> _attach;

	int _titleLines, _descriptionLines;

	Ui::Text::String _title, _description;

	int _gameTagWidth = 0;

};

} // namespace HistoryView
