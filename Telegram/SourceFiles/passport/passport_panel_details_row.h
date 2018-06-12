/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/widgets/labels.h"
#include "boxes/abstract_box.h"

namespace Ui {
class InputField;
class FlatLabel;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Passport {

class PanelController;

enum class PanelDetailsType {
	Text,
	Postcode,
	Country,
	Date,
	Gender,
};

class PanelDetailsRow : public Ui::RpWidget {
public:
	using Type = PanelDetailsType;

	PanelDetailsRow(
		QWidget *parent,
		const QString &label,
		int maxLabelWidth);

	static object_ptr<PanelDetailsRow> Create(
		QWidget *parent,
		Type type,
		not_null<PanelController*> controller,
		const QString &label,
		int maxLabelWidth,
		const QString &value,
		const QString &error,
		int limit = 0);
	static int LabelWidth(const QString &label);

	virtual bool setFocusFast();
	virtual rpl::producer<QString> value() const = 0;
	virtual QString valueCurrent() const = 0;
	void showError(base::optional<QString> error = base::none);
	bool errorShown() const;
	void hideError();
	void finishAnimating();

protected:
	int resizeGetHeight(int newWidth) override;

	void paintEvent(QPaintEvent *e) override;

private:
	virtual int resizeInner(int left, int top, int width) = 0;
	virtual void showInnerError() = 0;
	virtual void finishInnerAnimating() = 0;

	void startErrorAnimation(bool shown);

	QString _label;
	int _maxLabelWidth = 0;
	object_ptr<Ui::SlideWrap<Ui::FlatLabel>> _error = { nullptr };
	bool _errorShown = false;
	bool _errorHideSubscription = false;
	Animation _errorAnimation;

};

} // namespace Passport
