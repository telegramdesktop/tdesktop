/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "styles/style_widgets.h"
#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "base/object_ptr.h"

#include <set>

namespace Ui {

class InputField;
class CrossButton;
class ScrollArea;

class MultiSelect : public RpWidget {
public:
	MultiSelect(
		QWidget *parent,
		const style::MultiSelect &st,
		rpl::producer<QString> placeholder = nullptr);

	QString getQuery() const;
	void setInnerFocus();
	void clearQuery();

	void setQueryChangedCallback(Fn<void(const QString &query)> callback);
	void setSubmittedCallback(Fn<void(Qt::KeyboardModifiers)> callback);
	void setCancelledCallback(Fn<void()> callback);
	void setResizedCallback(Fn<void()> callback);

	enum class AddItemWay {
		Default,
		SkipAnimation,
	};
	using PaintRoundImage = Fn<void(Painter &p, int x, int y, int outerWidth, int size)>;
	void addItem(uint64 itemId, const QString &text, style::color color, PaintRoundImage paintRoundImage, AddItemWay way = AddItemWay::Default);
	void addItemInBunch(uint64 itemId, const QString &text, style::color color, PaintRoundImage paintRoundImage);
	void finishItemsBunch();
	void setItemText(uint64 itemId, const QString &text);

	void setItemRemovedCallback(Fn<void(uint64 itemId)> callback);
	void removeItem(uint64 itemId);

	int getItemsCount() const;
	QVector<uint64> getItems() const;
	bool hasItem(uint64 itemId) const;

protected:
	int resizeGetHeight(int newWidth) override;
	bool eventFilter(QObject *o, QEvent *e) override;

private:
	void scrollTo(int activeTop, int activeBottom);

	const style::MultiSelect &_st;

	object_ptr<Ui::ScrollArea> _scroll;

	class Inner;
	QPointer<Inner> _inner;

	Fn<void()> _resizedCallback;
	Fn<void(const QString &query)> _queryChangedCallback;

};

} // namespace Ui
