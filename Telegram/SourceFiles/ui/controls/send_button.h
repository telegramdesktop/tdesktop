/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/buttons.h"

namespace Ui {

class SendButton final : public RippleButton {
public:
	SendButton(QWidget *parent);

	static constexpr auto kSlowmodeDelayLimit = 100 * 60;

	enum class Type {
		Send,
		Schedule,
		Save,
		Record,
		Cancel,
		Slowmode,
	};
	[[nodiscard]] Type type() const {
		return _type;
	}
	void setType(Type state);
	void setSlowmodeDelay(int seconds);
	void finishAnimating();

	void requestPaintRecord(float64 progress);

protected:
	void paintEvent(QPaintEvent *e) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	[[nodiscard]] QPixmap grabContent();
	[[nodiscard]] bool isSlowmode() const;

	void paintRecord(Painter &p, bool over);
	void paintSave(Painter &p, bool over);
	void paintCancel(Painter &p, bool over);
	void paintSend(Painter &p, bool over);
	void paintSchedule(Painter &p, bool over);
	void paintSlowmode(Painter &p);

	Type _type = Type::Send;
	Type _afterSlowmodeType = Type::Send;
	QPixmap _contentFrom, _contentTo;

	Ui::Animations::Simple _a_typeChanged;

	float64 _recordProgress = 0.;

	int _slowmodeDelay = 0;
	QString _slowmodeDelayText;

};

} // namespace Ui
