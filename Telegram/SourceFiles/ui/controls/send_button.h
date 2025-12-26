/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/buttons.h"

namespace style {
struct SendButton;
struct IconButton;
struct RoundButton;
} // namespace style

namespace Ui {

class SendButton final : public RippleButton {
public:
	SendButton(QWidget *parent, const style::SendButton &st);

	static constexpr auto kSlowmodeDelayLimit = 100 * 60;

	enum class Type {
		Send,
		Schedule,
		Save,
		Record,
		Round,
		Cancel,
		Slowmode,
		EditPrice,
	};
	struct State {
		Type type = Type::Send;
		QColor fillBgOverride;
		int slowmodeDelay = 0;
		int starsToSend = 0;

		friend inline bool operator==(State, State) = default;
	};
	[[nodiscard]] Type type() const {
		return _state.type;
	}
	[[nodiscard]] State state() const {
		return _state;
	}
	void setState(State state);
	void finishAnimating();

protected:
	void paintEvent(QPaintEvent *e) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	struct StarsGeometry {
		QRect inner;
		QRect rounded;
		QRect outer;
	};
	[[nodiscard]] QPixmap grabContent();
	void updateSize();

	[[nodiscard]] StarsGeometry starsGeometry() const;

	void paintRecord(QPainter &p, bool over);
	void paintRound(QPainter &p, bool over);
	void paintSave(QPainter &p, bool over);
	void paintCancel(QPainter &p, bool over);
	void paintSend(QPainter &p, bool over);
	void paintSchedule(QPainter &p, bool over);
	void paintSlowmode(QPainter &p);
	void paintStarsToSend(QPainter &p, bool over);

	const style::SendButton &_st;

	State _state;
	QPixmap _contentFrom, _contentTo;

	Ui::Animations::Simple _stateChangeAnimation;
	int _stateChangeFromWidth = 0;

	QString _slowmodeDelayText;
	Ui::Text::String _starsToSendText;

};

struct SendStarButtonState {
	int count = 0;
	bool highlight = false;
};

class SendStarButton final : public RippleButton {
public:
	SendStarButton(
		QWidget *parent,
		const style::IconButton &st,
		const style::RoundButton &counterSt,
		rpl::producer<SendStarButtonState> state);

protected:
	void paintEvent(QPaintEvent *e) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	void setCount(int count);
	void highlight(bool enabled);

	const style::IconButton &_st;
	const style::RoundButton &_counterSt;

	QImage _frame;
	Ui::Text::String _starsText;
	Ui::Animations::Simple _highlight;
	int _count = 0;
	bool _highlighted = false;

};

} // namespace Ui
