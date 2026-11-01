/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/variant.h"
#include "ui/effects/animations.h"
#include "ui/effects/message_sending_animation_controller.h"
#include "ui/rp_widget.h"

struct TextWithTags;

namespace Ui {

class InputField;

[[nodiscard]] std::optional<MessageSendingAnimationFrom> PrepareTextSending(
	not_null<InputField*> field,
	const TextWithTags &sending,
	MsgId localId,
	bool scheduled);
[[nodiscard]] base::unique_qptr<RpWidget> ShowSendingFieldText(
	not_null<RpWidget*> parent,
	const MessageSendingAnimationFrom::FieldText &source);

class MessageSendingTextFlight final : public RpWidget {
public:
	using FieldText = MessageSendingAnimationFrom::FieldText;
	using SendingInfoTo = MessageSendingAnimationController::SendingInfoTo;

	struct Morph {
		QImage text;
		QRect bubble;
		QRect from;
		QPoint cut;
		QRect info;
		int edgeRow = 0;
	};
	struct Prepared {
		FieldText source;
		QRect sourceRect;
		QRect textRect;
		QRect bubbleRect;
		QSize viewSize;
		std::optional<Morph> morph;
	};
	struct Refused {
	};
	using Preparation = std::variant<v::null_t, Refused, Prepared>;
	[[nodiscard]] static Preparation Prepare(
		const FieldText &source,
		const SendingInfoTo &to);

	MessageSendingTextFlight(
		not_null<RpWidget*> parent,
		Prepared &&prepared,
		SendingInfoTo &&to);

	[[nodiscard]] MessageSendingReveal reveal() const;
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
	void paintMorph(
		QPainter &p,
		not_null<HistoryView::Element*> view,
		const ChatPaintContext &context,
		float64 progress) const;

	SendingInfoTo _toInfo;
	const QImage _source;
	const QRect _sourceRect;
	QRect _textRect;
	QRect _bounds;
	QSize _viewSize;
	std::optional<Morph> _morph;
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
