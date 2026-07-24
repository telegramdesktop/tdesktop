/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_no_forwards_request.h"

#include "data/data_peer.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/view/history_view_element.h"
#include "history/view/media/history_view_media_generic.h"
#include "lang/lang_keys.h"
#include "ui/chat/chat_style.h"
#include "ui/painter.h"
#include "ui/text/text_utilities.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

class BulletPointPart final : public MediaGenericTextPart {
public:
	BulletPointPart(
		TextWithEntities text,
		QMargins margins);

	void draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const override;

private:
	int _bulletLeft = 0;
	int _bulletTop = 0;

};

BulletPointPart::BulletPointPart(
	TextWithEntities text,
	QMargins margins)
: MediaGenericTextPart(
	std::move(text),
	QMargins(
		margins.left() + st::chatSuggestBulletLeftExtra + st::historyGroupAboutBulletSkip,
		margins.top(),
		margins.right(),
		margins.bottom()),
	st::serviceTextStyle,
	{},
	{},
	style::al_left)
, _bulletLeft(margins.left() + st::chatSuggestBulletLeftExtra)
, _bulletTop(margins.top()) {
}

void BulletPointPart::draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const {
	MediaGenericTextPart::draw(p, owner, context, outerWidth);

	const auto &font = st::serviceTextStyle.font;
	const auto size = st::mediaUnreadSize;
	const auto top = _bulletTop + (font->height - size) / 2;

	p.setBrush(context.st->msgServiceFg());

	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);
	p.drawEllipse(_bulletLeft, top, size, size);
}

} // namespace

auto GenerateNoForwardsRequestMedia(
	not_null<Element*> parent,
	not_null<const HistoryServiceNoForwardsRequest*> request)
-> Fn<void(
	not_null<MediaGeneric*>,
	Fn<void(std::unique_ptr<MediaGenericPart>)>)> {
	return [=](
			not_null<MediaGeneric*> media,
			Fn<void(std::unique_ptr<MediaGenericPart>)> push) {
		const auto item = parent->data();
		const auto from = item->from();

		auto pushText = [&](
				TextWithEntities text,
				QMargins margins,
				style::align align = style::al_left) {
			push(std::make_unique<MediaGenericTextPart>(
				std::move(text),
				margins,
				st::serviceTextStyle,
				base::flat_map<uint16, ClickHandlerPtr>(),
				Ui::Text::MarkedContext(),
				align));
		};

		pushText(
			(from->isSelf()
				? tr::lng_action_no_forwards_request_you(
					tr::now,
					tr::marked)
				: tr::lng_action_no_forwards_request(
					tr::now,
					lt_from,
					tr::bold(from->shortName()),
					tr::marked)),
			st::chatSuggestInfoTitleMargin,
			style::al_top);

		const auto features = {
			tr::lng_action_no_forwards_feature_forwarding(tr::now),
			tr::lng_action_no_forwards_feature_saving(tr::now),
			tr::lng_action_no_forwards_feature_copying(tr::now),
		};
		auto isLast = false;
		auto index = 0;
		const auto count = int(features.size());
		for (const auto &feature : features) {
			isLast = (++index == count);
			push(std::make_unique<BulletPointPart>(
				TextWithEntities{ feature },
				isLast
					? st::chatSuggestInfoLastMargin
					: st::chatSuggestInfoMiddleMargin));
		}
	};
}

} // namespace HistoryView
