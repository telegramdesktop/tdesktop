// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/settings/settings_ayu.h"

#include "lang_auto.h"
#include "ayu/ayu_settings.h"
#include "ayu/ui/ayu_userpic.h"
#include "ayu/ui/settings/ayu_builder.h"
#include "ayu/ui/settings/settings_ayu_utils.h"
#include "ayu/ui/settings/settings_main.h"
#include "boxes/peer_list_box.h"
#include "core/application.h"
#include "data/data_user.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_ayu_styles.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "ui/text/text.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/menu/menu_item_base.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

namespace Settings {

using namespace Builder;
using namespace AyBuilder;

namespace {

struct GhostPickerState {
	rpl::variable<uint64> selectedUserId;
	base::unique_qptr<Ui::PopupMenu> menu;
	Ui::LinkButton *pickerButton = nullptr;
	Fn<void()> refreshCheckboxes;
};

class AccountAction final : public Ui::Menu::ItemBase {
public:
	AccountAction(
		not_null<Ui::Menu::Menu*> parent,
		const style::Menu &st,
		UserData *user,
		Fn<void()> callback)
	: ItemBase(parent, st)
	, _dummyAction(Ui::CreateChild<QAction>(parent.get()))
	, _user(user)
	, _st(st)
	, _height(st::defaultWhoRead.photoSkip * 2 + st::defaultWhoRead.photoSize) {
		setAcceptBoth(true);
		fitToMenuWidth();

		_text.setText(_st.itemStyle, user->name());
		const auto goodWidth = st::defaultWhoRead.nameLeft
			+ _text.maxWidth()
			+ _st.itemPadding.right();
		setMinWidth(std::clamp(goodWidth, _st.widthMin, _st.widthMax));

		setActionTriggered(std::move(callback));

		paintRequest(
		) | rpl::on_next([=] {
			paint(Painter(this));
		}, lifetime());

		enableMouseSelecting();
	}

	not_null<QAction*> action() const override { return _dummyAction; }
	bool isEnabled() const override { return true; }

protected:
	int contentHeight() const override { return _height; }

private:
	void paint(Painter &&p) {
		const auto selected = isSelected();
		if (selected && _st.itemBgOver->c.alpha() < 255) {
			p.fillRect(0, 0, width(), _height, _st.itemBg);
		}
		const auto bg = selected ? _st.itemBgOver : _st.itemBg;
		p.fillRect(0, 0, width(), _height, bg);
		if (isEnabled()) {
			paintRipple(p, 0, 0);
		}

		const auto photoSize = st::defaultWhoRead.photoSize;
		const auto photoLeft = st::defaultWhoRead.photoLeft;
		const auto photoTop = (_height - photoSize) / 2;
		_user->paintUserpicLeft(p, _userpicView, photoLeft, photoTop, width(), photoSize);

		p.setPen(selected ? _st.itemFgOver : _st.itemFg);
		_text.drawLeftElided(
			p,
			st::defaultWhoRead.nameLeft,
			(_height - _st.itemStyle.font->height) / 2,
			width() - st::defaultWhoRead.nameLeft - _st.itemPadding.right(),
			width());
	}

	const not_null<QAction*> _dummyAction;
	UserData *_user = nullptr;
	mutable Ui::PeerUserpicView _userpicView;
	const style::Menu &_st;
	Ui::Text::String _text;
	const int _height;
};

class GlobalAction final : public Ui::Menu::ItemBase {
public:
	GlobalAction(
		not_null<Ui::Menu::Menu*> parent,
		const style::Menu &st,
		const QString &text,
		Fn<void()> callback)
	: ItemBase(parent, st)
	, _dummyAction(Ui::CreateChild<QAction>(parent.get()))
	, _st(st)
	, _height(st::defaultWhoRead.photoSkip * 2 + st::defaultWhoRead.photoSize) {
		setAcceptBoth(true);
		fitToMenuWidth();

		_text.setText(_st.itemStyle, text);
		const auto goodWidth = st::defaultWhoRead.nameLeft
			+ _text.maxWidth()
			+ _st.itemPadding.right();
		setMinWidth(std::clamp(goodWidth, _st.widthMin, _st.widthMax));

		setActionTriggered(std::move(callback));

		paintRequest(
		) | rpl::on_next([=] {
			paint(Painter(this));
		}, lifetime());

		enableMouseSelecting();
	}

	not_null<QAction*> action() const override { return _dummyAction; }
	bool isEnabled() const override { return true; }

protected:
	int contentHeight() const override { return _height; }

private:
	void paint(Painter &&p) {
		const auto selected = isSelected();
		if (selected && _st.itemBgOver->c.alpha() < 255) {
			p.fillRect(0, 0, width(), _height, _st.itemBg);
		}
		const auto bg = selected ? _st.itemBgOver : _st.itemBg;
		p.fillRect(0, 0, width(), _height, bg);
		if (isEnabled()) {
			paintRipple(p, 0, 0);
		}

		const auto photoSize = st::defaultWhoRead.photoSize;
		const auto photoLeft = st::defaultWhoRead.photoLeft;
		const auto photoTop = (_height - photoSize) / 2;
		{
			auto hq = PainterHighQualityEnabler(p);
			auto rect = QRectF(photoLeft, photoTop, photoSize, photoSize);
			auto gradient = QLinearGradient(rect.topLeft(), rect.bottomLeft());
			gradient.setStops({
				{ 0., st::historyPeer5UserpicBg->c },
				{ 1., st::historyPeer5UserpicBg2->c },
			});
			p.setPen(Qt::NoPen);
			p.setBrush(gradient);
			AyuUserpic::PaintShape(p, rect);
		}
		{
			const auto fontsize = (photoSize * 13) / 33;
			auto font = st::historyPeerUserpicFont->f;
			font.setPixelSize(fontsize);
			p.setFont(font);
			p.setBrush(Qt::NoBrush);
			p.setPen(st::historyPeerUserpicFg);
			p.drawText(
				QRect(photoLeft, photoTop, photoSize, photoSize),
				u"GS"_q,
				QTextOption(style::al_center));
		}

		p.setPen(selected ? _st.itemFgOver : _st.itemFg);
		_text.drawLeftElided(
			p,
			st::defaultWhoRead.nameLeft,
			(_height - _st.itemStyle.font->height) / 2,
			width() - st::defaultWhoRead.nameLeft - _st.itemPadding.right(),
			width());
	}

	const not_null<QAction*> _dummyAction;
	const style::Menu &_st;
	Ui::Text::String _text;
	const int _height;
};

QString GetAccountName(uint64 userId) {
	for (const auto &account : Core::App().domain().orderedAccounts()) {
		if (account->sessionExists()
			&& account->session().userId().bare == userId) {
			return account->session().user()->name();
		}
	}
	return QString("Unknown");
}

QString PickerLabel(uint64 userId) {
	return (userId == 0)
		? tr::ayu_GhostModeGlobalSettings(tr::now)
		: GetAccountName(userId);
}

void selectGhostProfile(GhostPickerState *state, uint64 userId) {
	if (state->selectedUserId.current() == userId) {
		return;
	}

	auto wasGlobal = (state->selectedUserId.current() == 0);
	auto nowGlobal = (userId == 0);

	AyuSettings::getInstance().setUseGlobalGhostMode(nowGlobal);

	state->selectedUserId = userId;

	state->pickerButton->setText(PickerLabel(userId));

	state->refreshCheckboxes();

	if (wasGlobal != nowGlobal) {
		Ui::Toast::Show(nowGlobal
			? tr::ayu_GhostModeSwitchedToGlobalSettings(tr::now)
			: tr::ayu_GhostModeSwitchedToIndividualSettings(tr::now));
	}
}

void BuildGhostEssentials(SectionBuilder &builder) {
	builder.add([](const BuildContext &ctx) {
		v::match(ctx, [&](const WidgetContext &wctx) {
			const auto container = wctx.container;
			const auto controller = wctx.controller;

			auto activeCount = 0;
			for (const auto &account : Core::App().domain().orderedAccounts()) {
				if (account->sessionExists()) {
					++activeCount;
				}
			}

			if (activeCount <= 1 && !AyuSettings::getInstance().useGlobalGhostMode()) {
				auto userId = controller->session().userId().bare;
				auto &src = AyuSettings::ghost(userId);
				auto &dst = AyuSettings::ghost(0);
				dst.setSendReadMessages(src.sendReadMessages());
				dst.setSendReadStories(src.sendReadStories());
				dst.setSendOnlinePackets(src.sendOnlinePackets());
				dst.setSendUploadProgress(src.sendUploadProgress());
				dst.setSendOfflinePacketAfterOnline(src.sendOfflinePacketAfterOnline());
				dst.setMarkReadAfterAction(src.markReadAfterAction());
				dst.setUseScheduledMessages(src.useScheduledMessages());
				dst.setSendWithoutSound(src.sendWithoutSound());
				dst.setSendReadMessagesLocked(src.sendReadMessagesLocked());
				dst.setSendReadStoriesLocked(src.sendReadStoriesLocked());
				dst.setSendOnlinePacketsLocked(src.sendOnlinePacketsLocked());
				dst.setSendUploadProgressLocked(src.sendUploadProgressLocked());
				dst.setSendOfflinePacketAfterOnlineLocked(src.sendOfflinePacketAfterOnlineLocked());
				AyuSettings::getInstance().setUseGlobalGhostMode(true);
			}

			const auto isGlobal = AyuSettings::getInstance().useGlobalGhostMode();
			auto initialUserId = isGlobal
				? uint64(0)
				: controller->session().userId().bare;

			const auto state = container->lifetime().make_state<GhostPickerState>();
			state->selectedUserId = initialUserId;

			const auto title = AddSubsectionTitle(container, tr::ayu_GhostEssentialsHeader());

			const auto pickerButton = Ui::CreateChild<Ui::LinkButton>(
				container.get(),
				PickerLabel(initialUserId),
				st::ghostPickerButton);
			state->pickerButton = pickerButton;

			const auto arrow = Ui::CreateChild<Ui::AbstractButton>(container.get());
			{
				const auto &icon = st::ghostPickerArrow;
				arrow->resize(icon.size());
				arrow->paintRequest(
				) | rpl::on_next([=, &icon] {
					auto p = QPainter(arrow);
					icon.paint(p, 0, 0, arrow->width());
				}, arrow->lifetime());
			}
			arrow->setCursor(style::cur_pointer);

			const auto showPicker = activeCount > 1;
			pickerButton->setVisible(showPicker);
			arrow->setVisible(showPicker);

			rpl::combine(
				title->geometryValue(),
				container->widthValue(),
				pickerButton->naturalWidthValue()
			) | rpl::on_next([=](QRect r, int width, int natural) {
				pickerButton->resizeToNaturalWidth(width / 2);
				pickerButton->moveToRight(
					st::defaultSubsectionTitlePadding.right() + arrow->width() + st::normalFont->spacew / 2,
					r.y() + (r.height() - pickerButton->height()) / 2,
					width);
				arrow->moveToLeft(
					pickerButton->x() + pickerButton->width() + st::normalFont->spacew / 2,
					r.y() + (r.height() - arrow->height()) / 2);
			}, pickerButton->lifetime());

			std::vector checkboxes{
				NestedEntry{
					tr::ayu_DontReadMessages(tr::now),
					[state] { return !AyuSettings::ghost(state->selectedUserId.current()).sendReadMessages(); },
					[state](bool v) { AyuSettings::ghost(state->selectedUserId.current()).setSendReadMessages(!v); },
					[state] { return AyuSettings::ghost(state->selectedUserId.current()).sendReadMessagesLocked(); },
					[state](bool v) { AyuSettings::ghost(state->selectedUserId.current()).setSendReadMessagesLocked(v); }
				},
				NestedEntry{
					tr::ayu_DontReadStories(tr::now),
					[state] { return !AyuSettings::ghost(state->selectedUserId.current()).sendReadStories(); },
					[state](bool v) { AyuSettings::ghost(state->selectedUserId.current()).setSendReadStories(!v); },
					[state] { return AyuSettings::ghost(state->selectedUserId.current()).sendReadStoriesLocked(); },
					[state](bool v) { AyuSettings::ghost(state->selectedUserId.current()).setSendReadStoriesLocked(v); }
				},
				NestedEntry{
					tr::ayu_DontSendOnlinePackets(tr::now),
					[state] { return !AyuSettings::ghost(state->selectedUserId.current()).sendOnlinePackets(); },
					[state](bool v) { AyuSettings::ghost(state->selectedUserId.current()).setSendOnlinePackets(!v); },
					[state] { return AyuSettings::ghost(state->selectedUserId.current()).sendOnlinePacketsLocked(); },
					[state](bool v) { AyuSettings::ghost(state->selectedUserId.current()).setSendOnlinePacketsLocked(v); }
				},
				NestedEntry{
					tr::ayu_DontSendUploadProgress(tr::now),
					[state] { return !AyuSettings::ghost(state->selectedUserId.current()).sendUploadProgress(); },
					[state](bool v) { AyuSettings::ghost(state->selectedUserId.current()).setSendUploadProgress(!v); },
					[state] { return AyuSettings::ghost(state->selectedUserId.current()).sendUploadProgressLocked(); },
					[state](bool v) { AyuSettings::ghost(state->selectedUserId.current()).setSendUploadProgressLocked(v); }
				},
				NestedEntry{
					tr::ayu_SendOfflinePacketAfterOnline(tr::now),
					[state] { return AyuSettings::ghost(state->selectedUserId.current()).sendOfflinePacketAfterOnline(); },
					[state](bool v) { AyuSettings::ghost(state->selectedUserId.current()).setSendOfflinePacketAfterOnline(v); },
					[state] { return AyuSettings::ghost(state->selectedUserId.current()).sendOfflinePacketAfterOnlineLocked(); },
					[state](bool v) { AyuSettings::ghost(state->selectedUserId.current()).setSendOfflinePacketAfterOnlineLocked(v); }
				},
			};

			auto collapsible = AddCollapsibleToggle(
				container, tr::ayu_GhostModeToggle(), std::move(checkboxes), true);
			state->refreshCheckboxes = std::move(collapsible.refresh);
			if (wctx.highlights && collapsible.widget) {
				wctx.highlights->push_back(std::make_pair(
					u"ayu/ghostModeToggle"_q,
					HighlightEntry{ collapsible.widget, {} }));
			}

			const auto markReadButton = AddButtonWithIcon(
				container,
				tr::ayu_MarkReadAfterAction(),
				st::settingsButtonNoIcon
			);
			if (wctx.highlights) {
				wctx.highlights->push_back(std::make_pair(
					u"ayu/markReadAfterAction"_q,
					HighlightEntry{ markReadButton.get(), {} }));
			}
			markReadButton->toggleOn(
				state->selectedUserId.value()
				| rpl::map([](uint64 id) {
					return AyuSettings::ghost(id).markReadAfterActionValue();
				}) | rpl::flatten_latest()
			)->toggledValue(
			) | rpl::filter(
				[=](bool enabled) {
					return enabled != AyuSettings::ghost(state->selectedUserId.current()).markReadAfterAction();
				}
			) | on_next(
				[=](bool enabled) {
					auto &ghost = AyuSettings::ghost(state->selectedUserId.current());
					ghost.setMarkReadAfterAction(enabled);
					if (enabled) {
						ghost.setUseScheduledMessages(false);
					}
				},
				container->lifetime());
			AddSkip(container);
			AddDividerText(container, tr::ayu_MarkReadAfterActionDescription());

			AddSkip(container);
			const auto scheduleButton = AddButtonWithIcon(
				container,
				tr::ayu_UseScheduledMessages(),
				st::settingsButtonNoIcon
			);
			if (wctx.highlights) {
				wctx.highlights->push_back(std::make_pair(
					u"ayu/useScheduledMessages"_q,
					HighlightEntry{ scheduleButton.get(), {} }));
			}
			scheduleButton->toggleOn(
				state->selectedUserId.value()
				| rpl::map([](uint64 id) {
					return AyuSettings::ghost(id).useScheduledMessagesValue();
				}) | rpl::flatten_latest()
			)->toggledValue(
			) | rpl::filter(
				[=](bool enabled) {
					return enabled != AyuSettings::ghost(state->selectedUserId.current()).useScheduledMessages();
				}
			) | on_next(
				[=](bool enabled) {
					auto &ghost = AyuSettings::ghost(state->selectedUserId.current());
					ghost.setUseScheduledMessages(enabled);
					if (enabled) {
						ghost.setMarkReadAfterAction(false);
					}
				},
				container->lifetime());
			AddSkip(container);
			AddDividerText(container, tr::ayu_UseScheduledMessagesDescription());

			AddSkip(container);
			const auto silentButton = AddButtonWithIcon(
				container,
				tr::ayu_SendWithoutSoundByDefault(),
				st::settingsButtonNoIcon
			);
			if (wctx.highlights) {
				wctx.highlights->push_back(std::make_pair(
					u"ayu/sendWithoutSound"_q,
					HighlightEntry{ silentButton.get(), {} }));
			}
			silentButton->toggleOn(
				state->selectedUserId.value()
				| rpl::map([](uint64 id) {
					return AyuSettings::ghost(id).sendWithoutSoundValue();
				}) | rpl::flatten_latest()
			)->toggledValue(
			) | rpl::filter(
				[=](bool enabled) {
					return enabled != AyuSettings::ghost(state->selectedUserId.current()).sendWithoutSound();
				}
			) | on_next(
				[=](bool enabled) {
					AyuSettings::ghost(state->selectedUserId.current()).setSendWithoutSound(enabled);
				},
				container->lifetime());
			AddSkip(container);
			AddDividerText(container, tr::ayu_SendWithoutSoundByDefaultDescription());

			auto showMenu = [=] {
				state->menu = base::make_unique_q<Ui::PopupMenu>(
					pickerButton,
					st::defaultPopupMenu);

				state->menu->addAction(
					base::make_unique_q<GlobalAction>(
						state->menu->menu(),
						st::defaultPopupMenu.menu,
						tr::ayu_GhostModeGlobalSettings(tr::now),
						[=] { selectGhostProfile(state, 0); }));

				for (const auto &account : Core::App().domain().orderedAccounts()) {
					if (!account->sessionExists()) {
						continue;
					}
					auto user = account->session().user();
					auto id = account->session().userId().bare;
					state->menu->addAction(
						base::make_unique_q<AccountAction>(
							state->menu->menu(),
							st::defaultPopupMenu.menu,
							user,
							[=] { selectGhostProfile(state, id); }));
				}

				state->menu->popup(
					pickerButton->mapToGlobal(
						QPoint(pickerButton->width(), pickerButton->height())));
			};
			pickerButton->setClickedCallback(showMenu);
			arrow->setClickedCallback(showMenu);
		}, [&](const SearchContext &sctx) {
			sctx.entries->push_back({
				.id = u"ayu/ghostModeToggle"_q,
				.title = tr::ayu_GhostModeToggle(tr::now),
				.section = sctx.sectionId,
			});
			sctx.entries->push_back({
				.id = u"ayu/markReadAfterAction"_q,
				.title = tr::ayu_MarkReadAfterAction(tr::now),
				.section = sctx.sectionId,
			});
			sctx.entries->push_back({
				.id = u"ayu/useScheduledMessages"_q,
				.title = tr::ayu_UseScheduledMessages(tr::now),
				.section = sctx.sectionId,
			});
			sctx.entries->push_back({
				.id = u"ayu/sendWithoutSound"_q,
				.title = tr::ayu_SendWithoutSoundByDefault(tr::now),
				.section = sctx.sectionId,
			});
		});
	});
}

void BuildSpyEssentials(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	builder.addSubsectionTitle(tr::ayu_SpyEssentialsHeader());

	ayu.addSettingToggle({
		.id = u"ayu/saveDeletedMessages"_q,
		.title = tr::ayu_SaveDeletedMessages(),
		.getter = &AyuSettings::saveDeletedMessages,
		.setter = &AyuSettings::setSaveDeletedMessages,
	});
	ayu.addSettingToggle({
		.id = u"ayu/saveMessagesHistory"_q,
		.title = tr::ayu_SaveMessagesHistory(),
		.getter = &AyuSettings::saveMessagesHistory,
		.setter = &AyuSettings::setSaveMessagesHistory,
	});

	ayu.addSectionDivider();

	ayu.addSettingToggle({
		.id = u"ayu/saveForBots"_q,
		.title = tr::ayu_MessageSavingSaveForBots(),
		.getter = &AyuSettings::saveForBots,
		.setter = &AyuSettings::setSaveForBots,
	});
}

void BuildOther(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	builder.addSubsectionTitle(tr::ayu_MessageSavingOtherHeader());

	ayu.addSettingToggle({
		.id = u"ayu/localPremium"_q,
		.title = tr::ayu_LocalPremium(),
		.getter = &AyuSettings::localPremium,
		.setter = &AyuSettings::setLocalPremium,
	});
	ayu.addSettingToggle({
		.id = u"ayu/disableAds"_q,
		.title = tr::ayu_DisableAds(),
		.getter = &AyuSettings::disableAds,
		.setter = &AyuSettings::setDisableAds,
	});
}

const auto kMeta = BuildHelper({
	.id = AyuGhost::Id(),
	.parentId = AyuMain::Id(),
	.title = u"TeleForge"_q,
	.icon = &st::menuIconGroupReactions,
}, [](SectionBuilder &builder) {
	auto ayu = AyuSectionBuilder(builder);

	builder.addSkip();
	BuildGhostEssentials(builder);

	builder.addSkip();
	BuildSpyEssentials(builder, ayu);

	ayu.addSectionDivider();
	BuildOther(builder, ayu);
	builder.addSkip();
});

} // namespace

rpl::producer<QString> AyuGhost::title() {
	return rpl::single(QString("TeleForge"));
}

AyuGhost::AyuGhost(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller)
, _controller(controller) {
	setupContent();
}

void AyuGhost::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type AyuGhostId() {
	return AyuGhost::Id();
}

} // namespace Settings
