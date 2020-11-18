/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/abstract_button.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

namespace HistoryView::Controls {

class RecordCircle;

class VoiceRecordButton final : public Ui::AbstractButton {
public:
	VoiceRecordButton(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<> leaveWindowEventProducer);
	~VoiceRecordButton();

	enum class Type {
		Send,
		Record,
	};

	void setType(Type state);

	void requestPaintColor(float64 progress);
	void requestPaintProgress(float64 progress);
	void requestPaintLevel(quint16 level);

	[[nodiscard]] rpl::producer<bool> actives() const;

	[[nodiscard]] bool inCircle(const QPoint &localPos) const;

private:
	void init();

	rpl::event_stream<crl::time> _recordAnimationTicked;
	std::unique_ptr<RecordCircle> _recordCircle;

	const int _center;

	rpl::variable<float64> _showProgress = 0.;
	rpl::variable<float64> _colorProgress = 0.;
	rpl::variable<bool> _inCircle = false;
	rpl::variable<Type> _state = Type::Record;

	// This can animate for a very long time (like in music playing),
	// so it should be a Basic, not a Simple animation.
	Ui::Animations::Basic _recordingAnimation;
};

} // namespace HistoryView::Controls
