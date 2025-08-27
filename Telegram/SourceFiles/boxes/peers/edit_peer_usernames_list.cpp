/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_peer_usernames_list.h"

#include "api/api_user_names.h"
#include "apiwrap.h"
#include "base/event_filter.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/show.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "ui/text/text_utilities.h" // Ui::Text::RichLangValue.
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout_reorder.h"
#include "ui/ui_utility.h"
#include "styles/style_boxes.h" // contactsStatusFont.
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

#include <QtGui/QGuiApplication>

namespace {

class RightAction final : public Ui::RpWidget {
public:
	RightAction(not_null<Ui::RpWidget*> parent);

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;

};

RightAction::RightAction(not_null<Ui::RpWidget*> parent)
: RpWidget(parent) {
	setCursor(style::cur_sizeall);
	const auto &st = st::inviteLinkThreeDots;
	resize(st.width, st.height);
}

void RightAction::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	st::usernamesReorderIcon.paintInCenter(p, rect());
}

void RightAction::mousePressEvent(QMouseEvent *e) {
}

} // namespace

class UsernamesList::Row final : public Ui::SettingsButton {
public:
	Row(
		not_null<Ui::RpWidget*> parent,
		const Data::Username &data,
		std::shared_ptr<Ui::Show> show,
		QString status,
		QString link);

	[[nodiscard]] const Data::Username &username() const;
	[[nodiscard]] not_null<Ui::RpWidget*> rightAction() const;

	int resizeGetHeight(int newWidth) override;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const style::PeerListItem &_st;
	const Data::Username _data;
	const QString _status;
	const not_null<Ui::RpWidget*> _rightAction;
	const QRect _iconRect;
	std::shared_ptr<Ui::Show> _show;
	Ui::Text::String _title;
	base::unique_qptr<Ui::PopupMenu> _menu;

};

UsernamesList::Row::Row(
	not_null<Ui::RpWidget*> parent,
	const Data::Username &data,
	std::shared_ptr<Ui::Show> show,
	QString status,
	QString link)
: Ui::SettingsButton(parent, rpl::never<QString>())
, _st(st::inviteLinkListItem)
, _data(data)
, _status(std::move(status))
, _rightAction(Ui::CreateChild<RightAction>(this))
, _iconRect(
	_st.photoPosition.x() + st::inviteLinkIconSkip,
	_st.photoPosition.y() + st::inviteLinkIconSkip,
	_st.photoSize - st::inviteLinkIconSkip * 2,
	_st.photoSize - st::inviteLinkIconSkip * 2)
, _show(show)
, _title(_st.nameStyle, '@' + data.username) {
	base::install_event_filter(this, [=](not_null<QEvent*> e) {
		if (e->type() != QEvent::ContextMenu) {
			return base::EventFilterResult::Continue;
		}
		_menu = base::make_unique_q<Ui::PopupMenu>(
			this,
			st::popupMenuWithIcons);
		_menu->addAction(
			tr::lng_group_invite_context_copy(tr::now),
			[=] {
				QGuiApplication::clipboard()->setText(link);
				show->showToast(
					tr::lng_create_channel_link_copied(tr::now));
			},
			&st::menuIconCopy);
		_menu->popup(QCursor::pos());
		return base::EventFilterResult::Cancel;
	});

	_rightAction->setVisible(data.active);
	sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		_rightAction->moveToLeft(
			s.width() - _rightAction->width() - st::inviteLinkThreeDotsSkip,
			(s.height() - _rightAction->height()) / 2);
	}, _rightAction->lifetime());
}

const Data::Username &UsernamesList::Row::username() const {
	return _data;
}

not_null<Ui::RpWidget*> UsernamesList::Row::rightAction() const {
	return _rightAction;
}

int UsernamesList::Row::resizeGetHeight(int newWidth) {
	return _st.height;
}

void UsernamesList::Row::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);

	const auto paintOver = (isOver() || isDown()) && !isDisabled();
	Ui::SettingsButton::paintBg(p, e->rect(), paintOver);
	Ui::SettingsButton::paintRipple(p, 0, 0);

	const auto active = _data.active;

	const auto &color = active ? st::msgFile1Bg : st::windowSubTextFg;
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	{
		auto hq = PainterHighQualityEnabler(p);
		p.drawEllipse(_iconRect);
	}
	(!active
		? st::inviteLinkRevokedIcon
		: st::inviteLinkIcon).paintInCenter(p, _iconRect);

	p.setPen(_st.nameFg);
	_title.drawLeft(
		p,
		_st.namePosition.x(),
		_st.namePosition.y(),
		width(),
		width() - _st.namePosition.x());

	p.setPen(active
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
	std::shared_ptr<Ui::Show> show,
	Fn<void()> focusCallback)
: RpWidget(parent)
, _show(show)
, _peer(peer)
, _isBot(peer->isUser()
	&& peer->asUser()->botInfo
	&& peer->asUser()->botInfo->canEditInformation)
, _focusCallback(std::move(focusCallback)) {
	{
		auto &api = _peer->session().api();
		const auto usernames = api.usernames().cacheFor(_peer->id);
		if (!usernames.empty()) {
			rebuild(usernames);
		}
	}
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
		Ui::AddSkip(_container);
		_container->add(
			object_ptr<Ui::FlatLabel>(
				_container,
				_peer->isSelf()
					? tr::lng_usernames_subtitle()
					: tr::lng_channel_usernames_subtitle(),
				st::defaultSubsectionTitle),
			st::defaultSubsectionTitlePadding);
	}

	const auto content = _container->add(
		object_ptr<Ui::VerticalLayout>(_container));
	for (const auto &username : usernames) {
		const auto link = _peer->session().createInternalLinkFull(
			username.username);
		const auto status = (username.editable && _focusCallback)
			? tr::lng_usernames_edit(tr::now)
			: username.active
			? tr::lng_usernames_active(tr::now)
			: tr::lng_usernames_non_active(tr::now);
		const auto row = content->add(
			object_ptr<Row>(content, username, _show, status, link));
		_rows.push_back(row);
		row->addClickHandler([=] {
			if (_reordering
				|| (!_peer->isSelf() && !_peer->isChannel() && !_isBot)) {
				return;
			}

			if (username.editable) {
				if (_focusCallback) {
					_focusCallback();
				}
				return;
			}

			auto text = _peer->isSelf()
				? (username.active
					? tr::lng_usernames_deactivate_description()
					: tr::lng_usernames_activate_description())
				: _isBot
				? (username.active
					? tr::lng_bot_usernames_deactivate_description()
					: tr::lng_bot_usernames_activate_description())
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
						) | rpl::start_with_error_done([=](
								Api::Usernames::Error error) {
							if (error == Api::Usernames::Error::TooMuch) {
								constexpr auto kMaxUsernames = 10.;
								_show->showBox(
									Ui::MakeInformBox(
										tr::lng_usernames_activate_error(
											lt_count,
											rpl::single(kMaxUsernames),
											Ui::Text::RichLangValue)));
							}
							load();
							_toggleLifetime.destroy();
						}, [=] {
							load();
							_toggleLifetime.destroy();
						});
					});
					close();
				}),
				.confirmText = std::move(confirmText),
			};
			_show->showBox(Ui::MakeConfirmBox(std::move(args)));
		});
	}

	_reorder = std::make_unique<Ui::VerticalLayoutReorder>(content);
	_reorder->setMouseEventProxy([=](int i) {
		return _rows[i]->rightAction();
	});

	{
		const auto it = ranges::find_if(usernames, [&](
				const Data::Username username) {
			return !username.active;
		});
		if (it != end(usernames)) {
			const auto from = std::distance(begin(usernames), it);
			const auto length = std::distance(it, end(usernames));
			_reorder->addPinnedInterval(from, length);
			if (from == 1) {
				// Can't be reordered.
				_rows[0]->rightAction()->hide();
			}
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
		Ui::AddSkip(_container);
		Ui::AddDividerText(
			_container,
			_peer->isSelf()
				? tr::lng_usernames_description()
				: _isBot
				? tr::lng_bot_usernames_description()
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
