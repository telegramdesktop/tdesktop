/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_suggest_options.h"

#include "base/unixtime.h"
#include "data/data_channel.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/boxes/choose_date_time.h"
#include "ui/widgets/fields/number_input.h"
#include "ui/widgets/buttons.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_settings.h"

namespace HistoryView {
namespace {

struct EditOptionsArgs {
	int starsLimit = 0;
	QString channelName;
	SuggestPostOptions values;
	Fn<void(SuggestPostOptions)> save;
};

void EditOptionsBox(
		not_null<Ui::GenericBox*> box,
		EditOptionsArgs &&args) {
	struct State {
		rpl::variable<TimeId> date;
	};
	const auto state = box->lifetime().make_state<State>();
	state->date = args.values.date;

	box->setTitle(tr::lng_suggest_options_title());

	const auto container = box->verticalLayout();

	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, tr::lng_suggest_options_price());

	const auto wrap = box->addRow(object_ptr<Ui::FixedHeightWidget>(
		box,
		st::editTagField.heightMin));
	auto owned = object_ptr<Ui::NumberInput>(
		wrap,
		st::editTagField,
		tr::lng_paid_cost_placeholder(),
		args.values.stars ? QString::number(args.values.stars) : QString(),
		args.starsLimit);
	const auto field = owned.data();
	wrap->widthValue() | rpl::start_with_next([=](int width) {
		field->move(0, 0);
		field->resize(width, field->height());
		wrap->resize(width, field->height());
	}, wrap->lifetime());
	field->paintRequest() | rpl::start_with_next([=](QRect clip) {
		auto p = QPainter(field);
		st::paidStarIcon.paint(p, 0, st::paidStarIconTop, field->width());
	}, field->lifetime());
	field->selectAll();
	box->setFocusCallback([=] {
		field->setFocusFast();
	});

	Ui::AddSkip(container);
	Ui::AddSkip(container);
	Ui::AddDividerText(
		container,
		tr::lng_suggest_options_price_about(
			lt_channel,
			rpl::single(args.channelName)));
	Ui::AddSkip(container);

	const auto time = Settings::AddButtonWithLabel(
		container,
		tr::lng_suggest_options_date(),
		state->date.value() | rpl::map([](TimeId date) {
			return date
				? langDateTime(base::unixtime::parse(date))
				: tr::lng_suggest_options_date_any(tr::now);
		}),
		st::settingsButtonNoIcon);

	time->setClickedCallback([=] {
		const auto weak = std::make_shared<QPointer<Ui::BoxContent>>();
		const auto parentWeak = Ui::MakeWeak(box);
		const auto done = [=](TimeId result) {
			if (parentWeak) {
				state->date = result;
			}
			if (const auto strong = weak->data()) {
				strong->closeBox();
			}
		};
		auto dateBox = Box(Ui::ChooseDateTimeBox, Ui::ChooseDateTimeBoxArgs{
			.title = tr::lng_suggest_options_date(),
			.submit = tr::lng_settings_save(),
			.done = done,
			.min = [] { return base::unixtime::now() + 1; },
			.time = (state->date.current()
				? state->date.current()
				: (base::unixtime::now() + 86400)),
		});
		*weak = dateBox.data();
		box->uiShow()->show(std::move(dateBox));
	});

	Ui::AddSkip(container);
	Ui::AddDividerText(container, tr::lng_suggest_options_date_about());
	AssertIsDebug()//tr::lng_suggest_options_offer
	const auto save = [=] {
		const auto now = uint32(field->getLastText().toULongLong());
		if (now > args.starsLimit) {
			field->showError();
			return;
		}
		const auto weak = Ui::MakeWeak(box);
		args.save({ .stars = now, .date = state->date.current()});
		if (const auto strong = weak.data()) {
			strong->closeBox();
		}
	};

	QObject::connect(field, &Ui::NumberInput::submitted, box, save);

	box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

} // namespace

SuggestOptions::SuggestOptions(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	SuggestPostOptions values)
: _controller(controller)
, _peer(peer)
, _values(values) {
	updateTexts();
}

SuggestOptions::~SuggestOptions() = default;

void SuggestOptions::paintBar(QPainter &p, int x, int y, int outerWidth) {
	st::historyDirectMessage.icon.paint(
		p,
		QPoint(x, y) + st::historySuggestIconPosition,
		outerWidth);

	x += st::historyReplySkip;
	auto available = outerWidth
		- x
		- st::historyReplyCancel.width
		- st::msgReplyPadding.right();
	p.setPen(st::windowActiveTextFg);
	_title.draw(p, {
		.position = QPoint(x, y + st::msgReplyPadding.top()),
		.availableWidth = available,
	});
	p.setPen(st::windowSubTextFg);
	_text.draw(p, {
		.position = QPoint(
			x,
			y + st::msgReplyPadding.top() + st::msgServiceNameFont->height),
		.availableWidth = available,
	});
}

void SuggestOptions::edit() {
	const auto apply = [=](SuggestPostOptions values) {
		_values = values;
		updateTexts();
		_updates.fire({});
	};
	const auto broadcast = _peer->monoforumBroadcast();
	const auto &appConfig = _peer->session().appConfig();
	_controller->show(Box(EditOptionsBox, EditOptionsArgs{
		.starsLimit = appConfig.suggestedPostStarsMax(),
		.channelName = (broadcast ? broadcast : _peer.get())->shortName(),
		.values = _values,
		.save = apply,
	}));
}

void SuggestOptions::updateTexts() {
	_title.setText(
		st::semiboldTextStyle,
		tr::lng_suggest_bar_title(tr::now));
	_text.setMarkedText(st::defaultTextStyle, composeText());
}

TextWithEntities SuggestOptions::composeText() const {
	if (!_values.stars && !_values.date) {
		return tr::lng_suggest_bar_text(tr::now, Ui::Text::WithEntities);
	} else if (!_values.date) {
		return tr::lng_suggest_bar_priced(
			tr::now,
			lt_amount,
			TextWithEntities{ QString::number(_values.stars) + " stars" },
			Ui::Text::WithEntities);
	} else if (!_values.stars) {
		return tr::lng_suggest_bar_dated(
			tr::now,
			lt_date,
			TextWithEntities{
				langDateTime(base::unixtime::parse(_values.date)),
			},
			Ui::Text::WithEntities);
	}
	return tr::lng_suggest_bar_priced_dated(
		tr::now,
		lt_amount,
		TextWithEntities{ QString::number(_values.stars) + " stars," },
		lt_date,
		TextWithEntities{
			langDateTime(base::unixtime::parse(_values.date)),
		},
		Ui::Text::WithEntities);
}

SuggestPostOptions SuggestOptions::values() const {
	auto result = _values;
	result.exists = 1;
	return result;
}

rpl::producer<> SuggestOptions::updates() const {
	return _updates.events();
}

rpl::lifetime &SuggestOptions::lifetime() {
	return _lifetime;
}

} // namespace HistoryView
