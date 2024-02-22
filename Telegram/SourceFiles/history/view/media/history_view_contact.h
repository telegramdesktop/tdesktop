/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_media.h"
#include "ui/userpic_view.h"

namespace Ui {
class EmptyUserpic;
class RippleAnimation;
} // namespace Ui

namespace HistoryView {

class Contact final : public Media {
public:
	Contact(
		not_null<Element*> parent,
		UserId userId,
		const QString &first,
		const QString &last,
		const QString &phone);
	~Contact();

	void draw(Painter &p, const PaintContext &context) const override;
	TextState textState(QPoint point, StateRequest request) const override;

	bool toggleSelectionByHandlerClick(
			const ClickHandlerPtr &p) const override {
		return true;
	}
	bool dragItemByHandler(const ClickHandlerPtr &p) const override {
		return true;
	}

	bool needsBubble() const override {
		return true;
	}
	bool customInfoLayout() const override {
		return false;
	}

	// Should be called only by Data::Session.
	void updateSharedContactUserId(UserId userId) override;

	void unloadHeavyPart() override;
	bool hasHeavyPart() const override;

private:
	QSize countOptimalSize() override;

	void clickHandlerPressedChanged(
		const ClickHandlerPtr &p, bool pressed) override;

	[[nodiscard]] QMargins inBubblePadding() const;
	[[nodiscard]] QMargins innerMargin() const;
	[[nodiscard]] int bottomInfoPadding() const;

	[[nodiscard]] TextSelection toTitleSelection(
		TextSelection selection) const;
	[[nodiscard]] TextSelection toDescriptionSelection(
		TextSelection selection) const;

	const style::QuoteStyle &_st;
	const int _pixh;

	UserId _userId = 0;
	UserData *_contact = nullptr;

	Ui::Text::String _nameLine;
	Ui::Text::String _phoneLine;
	Ui::Text::String _infoLine;

	struct Button {
		QString text;
		int width = 0;
		ClickHandlerPtr link;
		mutable std::unique_ptr<Ui::RippleAnimation> ripple;
	};
	std::vector<Button> _buttons;
	Button _mainButton;

	std::unique_ptr<Ui::EmptyUserpic> _photoEmpty;
	mutable Ui::PeerUserpicView _userpic;
	mutable QPoint _lastPoint;

};

} // namespace HistoryView
