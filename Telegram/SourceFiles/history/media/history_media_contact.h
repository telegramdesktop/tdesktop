/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/media/history_media.h"

class HistoryContact : public HistoryMedia {
public:
	HistoryContact(
		not_null<Element*> parent,
		UserId userId,
		const QString &first,
		const QString &last,
		const QString &phone);
	~HistoryContact();

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
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

private:
	QSize countOptimalSize() override;

	UserId _userId = 0;
	UserData *_contact = nullptr;

	int _phonew = 0;
	QString _fname, _lname, _phone;
	Text _name;
	std::unique_ptr<Ui::EmptyUserpic> _photoEmpty;

	ClickHandlerPtr _linkl;
	int _linkw = 0;
	QString _link;

};
