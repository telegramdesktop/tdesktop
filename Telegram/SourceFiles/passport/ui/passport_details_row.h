/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/widgets/labels.h"

namespace Ui {
class BoxContent;
class InputField;
class FlatLabel;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Passport::Ui {

using namespace ::Ui;

enum class PanelDetailsType {
	Text,
	Postcode,
	Country,
	Date,
	Gender,
};

class PanelDetailsRow : public RpWidget {
public:
	using Type = PanelDetailsType;

	PanelDetailsRow(
		QWidget *parent,
		const QString &label,
		int maxLabelWidth);

	static object_ptr<PanelDetailsRow> Create(
		QWidget *parent,
		Fn<void(object_ptr<BoxContent>)> showBox,
		const QString &defaultCountry,
		Type type,
		const QString &label,
		int maxLabelWidth,
		const QString &value,
		const QString &error,
		int limit = 0);
	static int LabelWidth(const QString &label);

	virtual bool setFocusFast();
	virtual rpl::producer<QString> value() const = 0;
	virtual QString valueCurrent() const = 0;
	void showError(std::optional<QString> error = std::nullopt);
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
	object_ptr<SlideWrap<FlatLabel>> _error = { nullptr };
	bool _errorShown = false;
	bool _errorHideSubscription = false;
	Animations::Simple _errorAnimation;

};

} // namespace Passport::Ui
