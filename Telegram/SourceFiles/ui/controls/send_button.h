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
	void setRecordActive(bool recordActive);
	void setSlowmodeDelay(int seconds);
	void finishAnimating();

	void setRecordStartCallback(Fn<void()> callback) {
		_recordStartCallback = std::move(callback);
	}
	void setRecordUpdateCallback(Fn<void(QPoint globalPos)> callback) {
		_recordUpdateCallback = std::move(callback);
	}
	void setRecordStopCallback(Fn<void(bool active)> callback) {
		_recordStopCallback = std::move(callback);
	}
	void setRecordAnimationCallback(Fn<void()> callback) {
		_recordAnimationCallback = std::move(callback);
	}

	[[nodiscard]] float64 recordActiveRatio() {
		return _a_recordActive.value(_recordActive ? 1. : 0.);
	}

protected:
	void mouseMoveEvent(QMouseEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void onStateChanged(State was, StateChangeSource source) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	void recordAnimationCallback();
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
	bool _recordActive = false;
	QPixmap _contentFrom, _contentTo;

	Ui::Animations::Simple _a_typeChanged;
	Ui::Animations::Simple _a_recordActive;

	bool _recording = false;
	Fn<void()> _recordStartCallback;
	Fn<void(bool active)> _recordStopCallback;
	Fn<void(QPoint globalPos)> _recordUpdateCallback;
	Fn<void()> _recordAnimationCallback;

	int _slowmodeDelay = 0;
	QString _slowmodeDelayText;

};

} // namespace Ui
