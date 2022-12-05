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
} // namespace Ui

namespace HistoryView {

class Contact : public Media {
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

	bool toggleSelectionByHandlerClick(const ClickHandlerPtr &p) const override {
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

	const QString &fname() const {
		return _fname;
	}
	const QString &lname() const {
		return _lname;
	}
	const QString &phone() const {
		return _phone;
	}

	// Should be called only by Data::Session.
	void updateSharedContactUserId(UserId userId) override;

	void unloadHeavyPart() override;
	bool hasHeavyPart() const override;

private:
	QSize countOptimalSize() override;

	UserId _userId = 0;
	UserData *_contact = nullptr;

	int _phonew = 0;
	QString _fname, _lname, _phone;
	Ui::Text::String _name;
	std::unique_ptr<Ui::EmptyUserpic> _photoEmpty;
	mutable Ui::PeerUserpicView _userpic;

	ClickHandlerPtr _linkl;
	int _linkw = 0;
	QString _link;

};

} // namespace HistoryView
