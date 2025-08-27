/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/box_content.h"

namespace Countries {
struct Info;
} // namespace Countries

namespace Ui {

class MultiSelect;
class RippleAnimation;

class CountrySelectBox : public BoxContent {
public:
	enum class Type {
		Phones,
		Countries,
	};
	struct Entry {
		QString country;
		QString iso2;
		QString code;
		QString alternativeName;
	};

	CountrySelectBox(QWidget*);
	CountrySelectBox(QWidget*, const QString &iso, Type type);

	[[nodiscard]] rpl::producer<QString> countryChosen() const;
	[[nodiscard]] rpl::producer<Entry> entryChosen() const;

protected:
	void prepare() override;
	void setInnerFocus() override;

	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void submit();
	void applyFilterUpdate(const QString &query);

	object_ptr<MultiSelect> _select;

	class Inner;
	object_ptr<Inner> _ownedInner;
	QPointer<Inner> _inner;

};

} // namespace Ui
