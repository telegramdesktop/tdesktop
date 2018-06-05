/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "styles/style_widgets.h"

QString findValidCode(QString fullCode);

namespace Ui {
class MultiSelect;
class RippleAnimation;
} // namespace Ui

class CountryInput : public TWidget {
	Q_OBJECT

public:
	CountryInput(QWidget *parent, const style::InputField &st);

	QString iso() const {
		return _chosenIso;
	}

public slots:
	void onChooseCode(const QString &code);
	bool onChooseCountry(const QString &country);

signals:
	void codeChanged(const QString &code);

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	void setText(const QString &newText);

	const style::InputField &_st;
	bool _active = false;
	QString _text;
	QString _chosenIso;
	QPainterPath _placeholderPath;

};

class CountrySelectBox : public BoxContent {
	Q_OBJECT

public:
	enum class Type {
		Phones,
		Countries,
	};

	CountrySelectBox(QWidget*);
	CountrySelectBox(QWidget*, const QString &iso, Type type);

	static QString NameByISO(const QString &iso);
	static QString ISOByPhone(const QString &phone);

signals:
	void countryChosen(const QString &iso);

protected:
	void prepare() override;
	void setInnerFocus() override;

	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private slots:
	void onSubmit();

private:
	void onFilterUpdate(const QString &query);

	Type _type = Type::Phones;
	object_ptr<Ui::MultiSelect> _select;

	class Inner;
	QPointer<Inner> _inner;

};

// This class is hold in header because it requires Qt preprocessing.
class CountrySelectBox::Inner : public TWidget {
	Q_OBJECT

public:
	Inner(QWidget *parent, Type type);

	void updateFilter(QString filter = QString());

	void selectSkip(int32 dir);
	void selectSkipPage(int32 h, int32 dir);

	void chooseCountry();

	void refresh();

	~Inner();

signals:
	void countryChosen(const QString &iso);
	void mustScrollTo(int ymin, int ymax);

protected:
	void paintEvent(QPaintEvent *e) override;
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	void updateSelected() {
		updateSelected(mapFromGlobal(QCursor::pos()));
	}
	void updateSelected(QPoint localPos);
	void updateSelectedRow();
	void updateRow(int index);
	void setPressed(int pressed);

	Type _type = Type::Phones;
	int _rowHeight = 0;

	int _selected = -1;
	int _pressed = -1;
	QString _filter;
	bool _mouseSelection = false;

	std::vector<std::unique_ptr<Ui::RippleAnimation>> _ripples;

};
