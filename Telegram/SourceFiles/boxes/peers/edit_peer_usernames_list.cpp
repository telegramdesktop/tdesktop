/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_peer_usernames_list.h"

#include "api/api_user_names.h"
#include "apiwrap.h"
#include "data/data_peer.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/show.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout_reorder.h"
#include "styles/style_boxes.h" // contactsStatusFont.
#include "styles/style_info.h"
#include "styles/style_settings.h"

class UsernamesList::Row final : public Ui::SettingsButton {
public:
	Row(not_null<Ui::RpWidget*> parent, const Data::Username &data);

	[[nodiscard]] const Data::Username &username() const;

	int resizeGetHeight(int newWidth) override;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const style::PeerListItem &_st;
	const Data::Username _data;
	const QString _status;
	const QRect _iconRect;
	Ui::Text::String _title;

};

UsernamesList::Row::Row(
	not_null<Ui::RpWidget*> parent,
	const Data::Username &data)
: Ui::SettingsButton(parent, rpl::never<QString>())
, _st(st::inviteLinkListItem)
, _data(data)
, _status(data.active
	? tr::lng_usernames_active(tr::now)
	: tr::lng_usernames_non_active(tr::now))
, _iconRect(
	_st.photoPosition.x() + st::inviteLinkIconSkip,
	_st.photoPosition.y() + st::inviteLinkIconSkip,
	_st.photoSize - st::inviteLinkIconSkip * 2,
	_st.photoSize - st::inviteLinkIconSkip * 2)
, _title(_st.nameStyle, '@' + data.username) {
}

const Data::Username &UsernamesList::Row::username() const {
	return _data;
}

int UsernamesList::Row::resizeGetHeight(int newWidth) {
	return _st.height;
}

void UsernamesList::Row::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);

	const auto paintOver = (isOver() || isDown()) && !isDisabled();
	Ui::SettingsButton::paintBg(p, e->rect(), paintOver);
	Ui::SettingsButton::paintRipple(p, 0, 0);

	const auto &color = _data.active ? st::msgFile1Bg : st::windowSubTextFg;
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	{
		auto hq = PainterHighQualityEnabler(p);
		p.drawEllipse(_iconRect);
	}
	(!_data.active
		? st::inviteLinkRevokedIcon
		: st::inviteLinkIcon).paintInCenter(p, _iconRect);

	p.setPen(_st.nameFg);
	_title.drawLeft(
		p,
		_st.namePosition.x(),
		_st.namePosition.y(),
		width(),
		width() - _st.namePosition.x());

	p.setPen(_data.active
		? _st.statusFgActive
		: paintOver
		? _st.statusFgOver
		: _st.statusFg);
	p.setFont(st::contactsStatusFont);
	p.drawTextLeft(
		_st.statusPosition.x(),
		_st.statusPosition.y(),
		width() - _st.statusPosition.x(),
		_status);
}

UsernamesList::UsernamesList(
	not_null<Ui::RpWidget*> parent,
	not_null<PeerData*> peer,
	std::shared_ptr<Ui::Show> show)
: RpWidget(parent)
, _show(show)
, _peer(peer) {
	load();
}

void UsernamesList::load() {
	_loadLifetime = _peer->session().api().usernames().loadUsernames(
		_peer
	) | rpl::start_with_next([=](const Data::Usernames &usernames) {
		if (usernames.empty()) {
			_container = nullptr;
			resize(0, 0);
		} else {
			rebuild(usernames);
		}
	});
}

void UsernamesList::rebuild(const Data::Usernames &usernames) {
	if (_reorder) {
		_reorder->cancel();
	}
	_rows.clear();
	_rows.reserve(usernames.size());
	_container = base::make_unique_q<Ui::VerticalLayout>(this);

	{
		Settings::AddSkip(_container);
		_container->add(
			object_ptr<Ui::FlatLabel>(
				_container,
				_peer->isSelf()
					? tr::lng_usernames_subtitle()
					: tr::lng_channel_usernames_subtitle(),
				st::settingsSubsectionTitle),
			st::settingsSubsectionTitlePadding);
	}

	const auto content = _container->add(
		object_ptr<Ui::VerticalLayout>(_container));
	for (const auto &username : usernames) {
		const auto row = content->add(
			object_ptr<Row>(content, username));
		_rows.push_back(row);
		row->addClickHandler([=] {
			if (_reordering || (!_peer->isSelf() && !_peer->isChannel())) {
				return;
			}

			if (username.username == _peer->userName()) {
				_show->showBox(
					Ui::MakeInformBox(_peer->isSelf()
						? tr::lng_usernames_deactivate_error()
						: tr::lng_channel_usernames_deactivate_error()),
					Ui::LayerOption::KeepOther);
				return;
			}

			auto text = _peer->isSelf()
				? (username.active
					? tr::lng_usernames_deactivate_description()
					: tr::lng_usernames_activate_description())
				: (username.active
					? tr::lng_channel_usernames_deactivate_description()
					: tr::lng_channel_usernames_activate_description());

			auto confirmText = username.active
				? tr::lng_usernames_deactivate_confirm()
				: tr::lng_usernames_activate_confirm();

			auto args = Ui::ConfirmBoxArgs{
				.text = std::move(text),
				.confirmed = crl::guard(this, [=](Fn<void()> close) {
					auto &api = _peer->session().api();
					_toggleLifetime = api.usernames().reorder(
						_peer,
						order()
					) | rpl::start_with_done([=] {
						auto &api = _peer->session().api();
						_toggleLifetime = api.usernames().toggle(
							_peer,
							username.username,
							!username.active
						) | rpl::start_with_done([=] {
							load();
						});
					});
					close();
				}),
				.confirmText = std::move(confirmText),
			};
			_show->showBox(
				Ui::MakeConfirmBox(std::move(args)),
				Ui::LayerOption::KeepOther);
		});
	}

	_reorder = std::make_unique<Ui::VerticalLayoutReorder>(content);

	{
		const auto it = ranges::find_if(usernames, [&](
				const Data::Username username) {
			return !username.active;
		});
		if (it != end(usernames)) {
			const auto from = std::distance(begin(usernames), it);
			const auto length = std::distance(it, end(usernames));
			_reorder->addPinnedInterval(from, length);
		}
	}
	_reorder->start();

	_reorder->updates(
	) | rpl::start_with_next([=](Ui::VerticalLayoutReorder::Single data) {
		using State = Ui::VerticalLayoutReorder::State;
		if (data.state == State::Started) {
			++_reordering;
		} else {
			Ui::PostponeCall(content, [=] {
				--_reordering;
			});
			if (data.state == State::Applied) {
				base::reorder(
					_rows,
					data.oldPosition,
					data.newPosition);
			}
		}
	}, content->lifetime());

	{
		Settings::AddSkip(_container);
		Settings::AddDividerText(
			_container,
			_peer->isSelf()
				? tr::lng_usernames_description()
				: tr::lng_channel_usernames_description());
	}

	Ui::ResizeFitChild(this, _container.get());
	content->show();
	_container->show();
}

std::vector<QString> UsernamesList::order() const {
	return ranges::views::all(
		_rows
	) | ranges::views::filter([](not_null<Row*> row) {
		return row->username().active;
	}) | ranges::views::transform([](not_null<Row*> row) {
		return row->username().username;
	}) | ranges::to_vector;
}

rpl::producer<> UsernamesList::save() {
	return _peer->session().api().usernames().reorder(_peer, order());
}
