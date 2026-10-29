/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "ui/effects/animations.h"
#include "ui/effects/message_sending_animation_controller.h"
#include "ui/rp_widget.h"

struct TextWithTags;

namespace Ui {

class InputField;

[[nodiscard]] auto GrabSendingFieldText(
	not_null<InputField*> field,
	const TextWithTags &sending)
-> std::optional<MessageSendingAnimationFrom::FieldText>;

class MessageSendingTextFlight final : public RpWidget {
public:
	using FieldText = MessageSendingAnimationFrom::FieldText;
	using SendingInfoTo = MessageSendingAnimationController::SendingInfoTo;

	struct Prepared {
		FieldText source;
		QRect textRect;
		QRect bubbleRect;
		QSize viewSize;
	};
	[[nodiscard]] static std::optional<Prepared> Prepare(
		const FieldText &source,
		const SendingInfoTo &to);
	[[nodiscard]] static MessageSendingReveal Reveal();

	MessageSendingTextFlight(
		not_null<RpWidget*> parent,
		Prepared &&prepared,
		SendingInfoTo &&to);

	[[nodiscard]] rpl::producer<> destroyRequests() const;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void tick(float64 progress);
	void refresh();
	void refreshBounds(not_null<HistoryView::Element*> view);
	void finish();
	[[nodiscard]] QPoint clampedTarget(int height) const;
	[[nodiscard]] ChatPaintContext prepareContext(
		not_null<HistoryView::Element*> view) const;

	SendingInfoTo _toInfo;
	const QImage _source;
	const QRect _sourceRect;
	const QRect _textRect;
	QRect _bounds;
	QSize _viewSize;
	const QPoint _start;
	QPoint _position;
	std::optional<QPoint> _target;
	std::optional<QPoint> _residual;
	int _bandTop = 0;
	int _bandBottom = 0;
	float64 _progress = 0.;
	Animations::Simple _animation;
	base::Timer _giveUpTimer;
	rpl::event_stream<> _destroyRequests;

};

} // namespace Ui
