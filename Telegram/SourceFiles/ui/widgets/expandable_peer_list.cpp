/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/expandable_peer_list.h"

#include "data/data_peer.h"
#include "info/profile/info_profile_values.h"
#include "ui/controls/userpic_button.h"
#include "ui/rect.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/participants_check_view.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

namespace Ui {
namespace {

class Button final : public Ui::RippleButton {
public:
	Button(not_null<QWidget*> parent, int count);

	[[nodiscard]] not_null<Ui::AbstractCheckView*> checkView() const;

private:
	void paintEvent(QPaintEvent *event) override;
	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

	std::unique_ptr<Ui::AbstractCheckView> _view;

};

Button::Button(not_null<QWidget*> parent, int count)
: Ui::RippleButton(parent, st::defaultRippleAnimation)
, _view(std::make_unique<Ui::ParticipantsCheckView>(
	count,
	st::slideWrapDuration,
	false,
	[=] { update(); })) {
}

not_null<Ui::AbstractCheckView*> Button::checkView() const {
	return _view.get();
}

QImage Button::prepareRippleMask() const {
	return _view->prepareRippleMask();
}

QPoint Button::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos());
}

void Button::paintEvent(QPaintEvent *event) {
	auto p = QPainter(this);
	Ui::RippleButton::paintRipple(p, QPoint());
	_view->paint(p, 0, 0, width());
}

} // namespace

void AddExpandablePeerList(
		not_null<Ui::Checkbox*> checkbox,
		not_null<ExpandablePeerListController*> controller,
		not_null<Ui::VerticalLayout*> inner) {
	const auto &participants = controller->data.participants;
	const auto hideRightButton = controller->data.hideRightButton;
	const auto checkTopOnAllInner = controller->data.checkTopOnAllInner;
	const auto isSingle = controller->data.skipSingle
		? false
		: (participants.size() == 1);
	if (isSingle) {
		const auto p = participants.front();
		controller->collectRequests = [=] { return Participants{ p }; };
		return;
	}
	const auto count = int(participants.size());
	const auto button = !hideRightButton
		? Ui::CreateChild<Button>(inner, count)
		: nullptr;
	if (button) {
		button->resize(Ui::ParticipantsCheckView::ComputeSize(count));
	}

	const auto overlay = Ui::CreateChild<Ui::AbstractButton>(inner);

	checkbox->geometryValue(
	) | rpl::start_with_next([=](const QRect &rect) {
		overlay->setGeometry(rect);
		overlay->raise();

		if (button) {
			button->moveToRight(
				st::moderateBoxExpandRight,
				rect.top() + (rect.height() - button->height()) / 2,
				inner->width());
			button->raise();
		}
	}, overlay->lifetime());

	controller->toggleRequestsFromInner.events(
	) | rpl::start_with_next([=](bool toggled) {
		checkbox->setChecked(toggled);
	}, checkbox->lifetime());
	if (button) {
		button->setClickedCallback([=] {
			button->checkView()->setChecked(
				!button->checkView()->checked(),
				anim::type::normal);
			controller->toggleRequestsFromTop.fire_copy(
				button->checkView()->checked());
		});
	}
	overlay->setClickedCallback([=] {
		checkbox->setChecked(!checkbox->checked());
		controller->checkAllRequests.fire_copy(checkbox->checked());
	});
	{
		const auto wrap = inner->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				inner,
				object_ptr<Ui::VerticalLayout>(inner)));
		wrap->toggle(hideRightButton, anim::type::instant);

		controller->toggleRequestsFromTop.events(
		) | rpl::start_with_next([=](bool toggled) {
			wrap->toggle(toggled, anim::type::normal);
		}, wrap->lifetime());

		const auto container = wrap->entity();
		Ui::AddSkip(container);

		auto &lifetime = wrap->lifetime();
		const auto clicks = lifetime.make_state<rpl::event_stream<>>();
		const auto checkboxes = ranges::views::all(
			participants
		) | ranges::views::transform([&](not_null<PeerData*> peer) {
			const auto line = container->add(
				object_ptr<Ui::AbstractButton>(container));
			const auto &st = st::moderateBoxUserpic;
			line->resize(line->width(), st.size.height());

			using namespace Info::Profile;
			auto name = controller->data.bold
				? NameValue(peer) | rpl::map(Ui::Text::Bold)
				: NameValue(peer) | rpl::map(Ui::Text::WithEntities);
			const auto userpic
				= Ui::CreateChild<Ui::UserpicButton>(line, peer, st);
			const auto checkbox = Ui::CreateChild<Ui::Checkbox>(
				line,
				controller->data.messagesCounts
				? rpl::combine(
					std::move(name),
					rpl::duplicate(controller->data.messagesCounts)
				) | rpl::map([=](const auto &richName, const auto &map) {
					const auto it = map.find(peer->id);
					return (it == map.end() || !it->second)
						? richName
						: TextWithEntities{
							(u"(%1) "_q).arg(it->second)
						}.append(richName);
				})
				: std::move(name) | rpl::type_erased(),
				st::defaultBoxCheckbox,
				std::make_unique<Ui::CheckView>(
					st::defaultCheck,
					ranges::contains(controller->data.checked, peer->id)));
			checkbox->setCheckAlignment(style::al_left);
			rpl::combine(
				line->widthValue(),
				checkbox->widthValue()
			) | rpl::start_with_next([=](int width, int) {
				userpic->moveToLeft(
					st::boxRowPadding.left()
						+ checkbox->checkRect().width()
						+ st::defaultBoxCheckbox.textPosition.x(),
					0);
				const auto skip = st::defaultBoxCheckbox.textPosition.x();
				checkbox->resizeToWidth(width
					- rect::right(userpic)
					- skip
					- st::boxRowPadding.right());
				checkbox->moveToLeft(
					rect::right(userpic) + skip,
					((userpic->height() - checkbox->height()) / 2)
						+ st::defaultBoxCheckbox.margin.top());
			}, checkbox->lifetime());

			userpic->setAttribute(Qt::WA_TransparentForMouseEvents);
			checkbox->setAttribute(Qt::WA_TransparentForMouseEvents);

			line->setClickedCallback([=] {
				checkbox->setChecked(!checkbox->checked());
				clicks->fire({});
			});

			return checkbox;
		}) | ranges::to_vector;

		clicks->events(
		) | rpl::start_with_next([=] {
			controller->toggleRequestsFromInner.fire_copy(
				checkTopOnAllInner
				? ranges::all_of(checkboxes, &Ui::Checkbox::checked)
				: ranges::any_of(checkboxes, &Ui::Checkbox::checked));
		}, container->lifetime());

		controller->checkAllRequests.events(
		) | rpl::start_with_next([=](bool checked) {
			for (const auto &c : checkboxes) {
				c->setChecked(checked);
			}
		}, container->lifetime());

		controller->collectRequests = [=] {
			auto result = Participants();
			for (auto i = 0; i < checkboxes.size(); i++) {
				if (checkboxes[i]->checked()) {
					result.push_back(participants[i]);
				}
			}
			return result;
		};
	}
}

} // namespace Ui
