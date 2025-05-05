/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/bank_card_click_handler.h"

#include "core/click_handler_types.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "mtproto/sender.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/widgets/menu/menu_multiline_action.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_calls.h"
#include "styles/style_chat.h" // popupMenuExpandedSeparator.
#include "styles/style_menu_icons.h"

namespace {

struct State final {
	State(not_null<Main::Session*> session) : sender(&session->mtp()) {
	}
	MTP::Sender sender;
};

struct BankCardData final {
	QString title;
	std::vector<EntityLinkData> links;
};

enum class Status {
	Loading,
	Resolved,
	Failed,
};

void RequestResolveBankCard(
		not_null<State*> state,
		const QString &bankCard,
		Fn<void(BankCardData)> done,
		Fn<void(QString)> fail) {
	state->sender.request(MTPpayments_GetBankCardData(
		MTP_string(bankCard)
	)).done([=](const MTPpayments_BankCardData &result) {
		auto bankCardData = BankCardData{
			.title = qs(result.data().vtitle()),
		};
		for (const auto &tl : result.data().vopen_urls().v) {
			const auto url = qs(tl.data().vurl());
			const auto name = qs(tl.data().vname());

			bankCardData.links.emplace_back(EntityLinkData{
				.text = name,
				.data = url,
			});
		}
		done(std::move(bankCardData));
	}).fail([=](const MTP::Error &error) {
		fail(error.type());
	}).send();
}

class ResolveBankCardAction final : public Ui::Menu::ItemBase {
public:
	ResolveBankCardAction(
		not_null<Ui::RpWidget*> parent,
		const style::Menu &st);

	void setStatus(Status status);

	bool isEnabled() const override;
	not_null<QAction*> action() const override;

protected:
	int contentHeight() const override;

	void paintEvent(QPaintEvent *e) override;

private:
	void paint(Painter &p);

	const not_null<QAction*> _dummyAction;
	const style::Menu &_st;
	const int _height = 0;
	Status _status = Status::Loading;

	Ui::Text::String _text;

};

ResolveBankCardAction::ResolveBankCardAction(
	not_null<Ui::RpWidget*> parent,
	const style::Menu &st)
: ItemBase(parent, st)
, _dummyAction(Ui::CreateChild<QAction>(parent))
, _st(st)
, _height(st::groupCallJoinAsPhotoSize) {
	setAcceptBoth(true);
	initResizeHook(parent->sizeValue());
	setStatus(Status::Loading);
}

void ResolveBankCardAction::setStatus(Status status) {
	_status = status;
	if (status == Status::Resolved) {
		resize(width(), 0);
	} else if (status == Status::Failed) {
		_text.setText(_st.itemStyle, tr::lng_attach_failed(tr::now));
	} else if (status == Status::Loading) {
		_text.setText(_st.itemStyle, tr::lng_contacts_loading(tr::now));
	}
	update();
}

void ResolveBankCardAction::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	const auto selected = false;
	const auto height = contentHeight();
	if (selected && _st.itemBgOver->c.alpha() < 255) {
		p.fillRect(0, 0, width(), height, _st.itemBg);
	}
	p.fillRect(0, 0, width(), height, selected ? _st.itemBgOver : _st.itemBg);

	const auto &padding = st::groupCallJoinAsPadding;
	{
		p.setPen(selected ? _st.itemFgShortcutOver : _st.itemFgShortcut);
		const auto w = width() - padding.left() - padding.right();
		_text.draw(p, Ui::Text::PaintContext{
			.position = QPoint(
				(width() - w) / 2,
				(height - _text.countHeight(w)) / 2),
			.outerWidth = w,
			.availableWidth = w,
			.align = style::al_center,
			.elisionLines = 2,
		});
	}
}

bool ResolveBankCardAction::isEnabled() const {
	return false;
}

not_null<QAction*> ResolveBankCardAction::action() const {
	return _dummyAction;
}

int ResolveBankCardAction::contentHeight() const {
	if (_status == Status::Resolved) {
		return 0;
	}
	return _height;
}

} // namespace

BankCardClickHandler::BankCardClickHandler(
	not_null<Main::Session*> session,
	QString text)
: _session(session)
, _text(text) {
}

void BankCardClickHandler::onClick(ClickContext context) const {
	if (context.button != Qt::LeftButton) {
		return;
	}
	const auto my = context.other.value<ClickHandlerContext>();
	const auto controller = my.sessionWindow.get();
	const auto pos = QCursor::pos();
	if (!controller) {
		return;
	}
	const auto menu = Ui::CreateChild<Ui::PopupMenu>(
		controller->content(),
		st::popupMenuWithIcons);

	const auto bankCard = _text;

	const auto copy = [bankCard, show = controller->uiShow()] {
		TextUtilities::SetClipboardText(
			TextForMimeData::Simple(bankCard));
		show->showToast(tr::lng_context_bank_card_copied(tr::now));
	};

	menu->addAction(
		tr::lng_context_bank_card_copy(tr::now),
		copy,
		&st::menuIconCopy);

	auto resolveBankCardAction = base::make_unique_q<ResolveBankCardAction>(
		menu,
		menu->st().menu);
	const auto resolveBankCardRaw = resolveBankCardAction.get();

	menu->addSeparator(&st::popupMenuExpandedSeparator.menu.separator);

	menu->addAction(std::move(resolveBankCardAction));

	const auto addTitle = [=](const QString &name) {
		auto button = base::make_unique_q<Ui::Menu::MultilineAction>(
			menu,
			menu->st().menu,
			st::historyHasCustomEmoji,
			st::historyBankCardMenuMultilinePosition,
			TextWithEntities{ name });
		button->setClickedCallback(copy);
		menu->addAction(std::move(button));
	};

	const auto state = menu->lifetime().make_state<State>(
		&controller->session());
	RequestResolveBankCard(
		state,
		bankCard,
		[=](BankCardData data) {
			resolveBankCardRaw->setStatus(Status::Resolved);
			for (auto &link : data.links) {
				menu->addAction(
					base::take(link.text),
					[u = base::take(link.data)] { UrlClickHandler::Open(u); },
					&st::menuIconPayment);
			}
			if (!data.title.isEmpty()) {
				addTitle(base::take(data.title));
			}
		},
		[=](const QString &) {
			resolveBankCardRaw->setStatus(Status::Failed);
		});

	menu->popup(pos);
}

auto BankCardClickHandler::getTextEntity() const -> TextEntity {
	return { EntityType::BankCard };
}

QString BankCardClickHandler::tooltip() const {
	return _text;
}
