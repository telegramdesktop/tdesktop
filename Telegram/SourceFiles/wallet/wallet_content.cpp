/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_content.h"

#include "api/api_cloud_password.h"
#include "api/api_common.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "base/debug_log.h"
#include "base/event_filter.h"
#include "base/invoke_queued.h"
#include "base/qthelp_regex.h"
#include "base/qthelp_url.h"
#include "base/random.h"
#include "base/timer.h"
#include "base/unique_qptr.h"
#include "base/unixtime.h"
#include "boxes/passcode_box.h"
#include "boxes/peer_list_box.h"
#include "boxes/peer_list_controllers.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/credits_amount.h"
#include "core/local_url_handlers.h"
#include "core/ton_explorer_url.h"
#include "core/ui_integration.h"
#include "data/components/credits.h"
#include "data/components/promo_suggestions.h"
#include "data/components/recent_money_recipients.h"
#include "data/components/recent_peers.h"
#include "data/components/top_peers.h"
#include "data/data_changes.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/ui/dialogs_pill.h"
#include "history/history.h"
#include "info/channel_statistics/boosts/giveaway/boost_badge.h" // InfiniteRadialAnimationWidget.
#include "info/channel_statistics/earn/earn_icons.h"
#include "info/profile/info_profile_values.h"
#include "inline_bots/bot_attach_web_view.h"
#include "lang/lang_hardcoded.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/session/session_show.h"
#include "main/main_app_config.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mtproto/mtproto_response.h"
#include "qr/qr_generate.h"
#include "settings/cloud_password/settings_cloud_password_common.h"
#include "settings/sections/settings_credits.h"
#include "settings/settings_common.h"
#include "storage/storage_account.h"
#include "storage/storage_domain.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/feature_list.h"
#include "ui/controls/table_rows.h"
#include "ui/controls/ton_common.h"
#include "ui/effects/ripple_animation.h"
#include "ui/effects/star_burst.h"
#include "ui/effects/unique_gift_message_bubble.h"
#include "ui/image/image_prepare.h"
#include "ui/layers/generic_box.h"
#include "ui/text/custom_emoji_helper.h"
#include "ui/text/format_values.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/glare_tooltip.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/multi_select.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/separate_panel.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/table_layout.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/basic_click_handlers.h"
#include "ui/empty_userpic.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/round_rect.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_amount_field.h"
#include "wallet/wallet_amount_painter.h"
#include "wallet/wallet_card_angle.h"
#include "wallet/wallet_card_gradient.h"
#include "wallet/wallet_chat_show.h"
#include "wallet/wallet_collectible_media.h"
#include "wallet/wallet_collectibles.h"
#include "wallet/wallet_comment.h"
#include "wallet/wallet_custody.h"
#include "wallet/wallet_diamond_flight.h"
#include "wallet/wallet_fiat.h"
#include "wallet/wallet_key_protection.h"
#include "wallet/wallet_onramp.h"
#include "wallet/wallet_palette.h"
#include "wallet/wallet_rates.h"
#include "wallet/wallet_sending_effects.h"
#include "wallet/wallet_session.h"
#include "wallet/wallet_ton_connect.h"
#include "wallet/wallet_ton_connect_box.h"
#include "wallet/wallet_unlock.h"
#include "wallet/wallet_user_addresses.h"
#include "window/themes/window_theme.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include <QtCore/QLocale>
#include <QtCore/QUrl>
#include <QtCore/QtMath>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>
#include <QtGui/QPainterPath>
#include <QtSvg/QSvgRenderer>
#include <QtWidgets/QTextEdit>

#include <array>

#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_giveaway.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_wallet.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

namespace Wallet {
namespace {

constexpr auto kAddressLength = 48;
constexpr auto kAddressGroup = 4;
constexpr auto kAddressGroupsPerLine = 6;
constexpr auto kReceiveGroupsPerLine = 4;
constexpr auto kReceiveLines = kAddressLength
	/ kAddressGroup
	/ kReceiveGroupsPerLine;
constexpr auto kSendUserCardGroupsPerLine = 6;
constexpr auto kQrQuietZoneModules = 4;
constexpr auto kShortAddressChars = 4;
constexpr auto kGaslessDailyTransfersDefault = 5;
constexpr auto kMinus = QChar(0x2212);
constexpr auto kImportWordCountShort = 12;
constexpr auto kImportWordCountLong = 24;
constexpr auto kImportSuggestionsLimit = 3;
constexpr auto kBackupWriteDownDelay = 30 * crl::time(1000);
constexpr auto kBackupQuizWordCount = 3;
constexpr auto kCoverBodyPart = 0.90;
constexpr auto kCoverTitleScale = 0.05;
constexpr auto kCardFoldMinHeight = 1.;
constexpr auto kIntroTooltipShownPref = "wallet_intro_tooltip_shown"_cs;
constexpr auto kWalletIntroGlares = 2;
constexpr auto kUndatedRowDate = std::numeric_limits<TimeId>::max();
constexpr auto kTransactionLookupInterval = crl::time(1000);
constexpr auto kTransactionLookupAttempts = 10;
constexpr auto kMaxFiatUnits = 999'999'999LL;
constexpr auto kMaxAmountNano = 999'999'999'999'999'999LL;
constexpr auto kSendUserLoadTimeout = 30 * crl::time(1000);
constexpr auto kRecipientSearchLimit = 64;
constexpr auto kNameBusyRetryDelay = crl::time(500);
constexpr auto kSendOwnerLookupDelay = crl::time(500);
constexpr auto kSendRefusalRetries = 3;
constexpr auto kCommentPasswordStateTimeout = 30 * crl::time(1000);
constexpr auto kSigningReadyTimeout = 30 * crl::time(1000);
constexpr auto kHeldSendKeyTimeout = 60 * crl::time(1000);
constexpr auto kCustodyResolveTimeout = 20 * crl::time(1000);
constexpr auto kGramDigits = 9;
constexpr auto kSendingRowGlareDuration = crl::time(1450);
constexpr auto kSendingRowGlarePause = crl::time(1040);
constexpr auto kSendingRowGlareLag = 0.4;
constexpr auto kSendingRowGlareBorder = 0.5;
constexpr auto kSendingRowGlareBackground = 0.12;
constexpr auto kSendingRowEntranceDuration = crl::time(370);
constexpr auto kSendingRowEntrancePeak = crl::time(180);
constexpr auto kSendingRowEntranceDamping = 0.57;
constexpr auto kSendingRowEntranceScale = 0.93;
constexpr auto kSendingRowFlightDuration = crl::time(480);
constexpr auto kSendingRowBumpPress = crl::time(115);
constexpr auto kSendingRowBumpRelease = crl::time(220);
constexpr auto kSendingRowBumpRebound = 0.35;
constexpr auto kSendingRowBumpTaper = crl::time(460);
constexpr auto kSendingRowBumpDuration = crl::time(560);
constexpr auto kSendingRowBumpDrop = 0.04;
constexpr auto kSendingRowBumpShrink = 0.012;
constexpr auto kSendingRowBumpSwell = 0.15;
constexpr auto kSendingRowSettlePill = crl::time(110);
constexpr auto kSendingRowSettleBadgeFrom = crl::time(65);
constexpr auto kSendingRowSettleBadgeTill = crl::time(230);
constexpr auto kSendingRowSettleLabelFrom = crl::time(35);
constexpr auto kSendingRowSettleLabelTill = crl::time(230);
constexpr auto kSendingRowSettleAmount = crl::time(170);
constexpr auto kSendingRowSettleAmountFadeFrom = crl::time(110);
constexpr auto kSendingRowSettleDiamondMove = crl::time(130);
constexpr auto kSendingRowSettleDiamondSwellTill = crl::time(100);
constexpr auto kSendingRowSettleDiamondSwell = 1.1;
constexpr auto kSendingRowSettleDiamond = crl::time(370);
constexpr auto kSendingRowSettleDiamondFadeFrom = crl::time(230);
constexpr auto kSendingRowSettlePaintWait = crl::time(1000);
constexpr auto kSendingRowBurstDelay = crl::time(50);
constexpr auto kSendingRowBurstSpread = crl::time(160);
constexpr auto kSendingRowBurstLifeMin = crl::time(450);
constexpr auto kSendingRowBurstLifeMax = crl::time(650);
constexpr auto kRowEmojiDiamondLeft = 7. / 72.;
constexpr auto kRowEmojiDiamondTop = 12. / 72.;
constexpr auto kRowEmojiDiamondRight = 65. / 72.;
constexpr auto kRowEmojiDiamondBottom = 62. / 72.;

class BalanceInk;
class Card;
struct CardFold;
class InfoIsland;
class InfoIslandEntry;
class SendingHistoryRow;
struct SendingRow;

class KeyContext final
	: public Main::SessionShow
	, public std::enable_shared_from_this<KeyContext> {
public:
	KeyContext(
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<CommentScope> scope,
		Fn<bool()> current,
		Fn<void(KeyAuthorization)> done);

	void showOrHideBoxOrLayer(
		std::variant<
			v::null_t,
			object_ptr<Ui::BoxContent>,
			std::unique_ptr<Ui::LayerWidget>> &&layer,
		Ui::LayerOptions options,
		anim::type animated) const override;
	not_null<QWidget*> toastParent() const override;
	bool valid() const override;
	operator bool() const override;
	Main::Session &session() const override;

	[[nodiscard]] std::shared_ptr<CommentScope> scope() const;
	// The show this context wraps, for a box that must outlive a prompt
	// closing under it instead of being read as the end of the attempt.
	[[nodiscard]] std::shared_ptr<Main::SessionShow> plain() const;
	[[nodiscard]] CustodyInstaller installer();
	void acceptClosed();
	void allowPromptRetry(base::weak_qptr<Ui::BoxContent> box);
	void cancelOnClose(
		base::weak_qptr<Ui::BoxContent> box,
		bool allowSuccessor = false);
	void closePrompt(base::weak_qptr<Ui::BoxContent> box);
	void ready(KeyAuthorization auth);
	void cancel();
	[[nodiscard]] rpl::lifetime &lifetime();

private:
	struct Prompt {
		base::weak_qptr<Ui::BoxContent> box;
		bool closing = false;
		bool accepted = false;
		bool cancelOnClose = false;
		bool allowSuccessor = true;
	};

	void promptClosed(const std::shared_ptr<Prompt> &prompt);
	void finish(KeyAuthorization auth);

	const std::shared_ptr<Main::SessionShow> _show;
	const base::weak_ptr<Main::Session> _session;
	const std::shared_ptr<CommentScope> _scope;
	const Fn<bool()> _current;
	Fn<void(KeyAuthorization)> _done;
	mutable std::vector<std::shared_ptr<Prompt>> _prompts;
	bool _finished = false;
	bool _installing = false;
	rpl::lifetime _lifetime;

};

class EncryptedCommentLabel final : public Ui::FlatLabel {
public:
	EncryptedCommentLabel(
		QWidget *parent,
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		TransferItem item,
		Fn<bool()> originCurrent);

	QString accessibilityName() override;

private:
	const TextWithEntities _cover;
	const bool _revealable = false;
	bool _closed = false;
	bool _revealed = false;
	TransferComment _comment;

};

class Content final : public Ui::RpWidget {
public:
	Content(
		not_null<Ui::SeparatePanel*> panel,
		std::shared_ptr<Main::SessionShow> show);
	~Content();

	void flySendDiamond(
		const std::string &operationId,
		not_null<Ui::TonAmountInput*> amount);

protected:
	void focusInEvent(QFocusEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	struct Regions {
		int reserve = 0;
		int scrollTop = 0;
		int scrollHeight = 0;
	};

	void setupContent();
	void setupPinned();
	void setupInfoIsland();
	void setupWaltEntry(not_null<InfoIsland*> island);
	void setupEarningsEntry(not_null<InfoIsland*> island);
	void setupOldWalletEntry(not_null<InfoIsland*> island);
	void setupProtectRow();
	void setupBalance();
	void setupTabs(rpl::producer<bool> collectiblesShown);
	void setupStrip();
	void setupListsLoading();
	void setupCustodyEntry(not_null<InfoIsland*> island);
	void paintTitle(QPainter &p, float64 fold);
	void updateRegions();
	void updatePinned();
	void updateVisibleArea();
	void checkLoadMore();
	bool revealSendingRow();
	[[nodiscard]] int pinnedMax() const;
	[[nodiscard]] int pinnedMin() const;
	[[nodiscard]] Regions countRegions(int columnHeight) const;
	[[nodiscard]] QRect cardRest() const;
	[[nodiscard]] float64 foldProgress() const;
	[[nodiscard]] const CardFold &cardFold() const;
	[[nodiscard]] QRegion cardOutline() const;

	const std::shared_ptr<Main::SessionShow> _show;
	Ui::SeparatePanel *_panel = nullptr;
	object_ptr<Ui::ScrollArea> _scroll;
	SingleQueuedInvokation _loadMoreCheck;
	std::unique_ptr<BalanceInk> _ink;
	Ui::Text::String _title;
	base::unique_qptr<Ui::RpWidget> _titleBalance;
	Ui::RpWidget *_container = nullptr;
	Ui::PaddingWrap<Ui::VerticalLayout> *_column = nullptr;
	Ui::RpWidget *_pinnedBackground = nullptr;
	Ui::RpWidget *_pinned = nullptr;
	Ui::VerticalLayout *_pinnedInner = nullptr;
	Ui::RpWidget *_pinnedBalance = nullptr;
	Ui::PlainShadow *_headerShadow = nullptr;
	Ui::SlideWrap<Ui::SettingsSlider> *_tabsWrap = nullptr;
	Ui::PlainShadow *_tabsShadow = nullptr;
	Ui::PlainShadow *_stripShadow = nullptr;
	Ui::RpWidget *_strip = nullptr;
	Ui::RpWidget *_listsLoading = nullptr;
	Ui::FlatLabel *_custodyBarLabel = nullptr;
	Ui::FixedHeightWidget *_cardPlaceholder = nullptr;
	Card *_card = nullptr;
	Ui::AbstractButton *_cardButton = nullptr;
	std::unique_ptr<SendingRow> _sendingRow;
	std::unique_ptr<DiamondFlight> _diamondFlight;
	QRect _paintedInk;
	int _reserve = 0;
	int _paintedHeight = -1;
	int _paintedMin = -1;
	bool _tabsShown = false;
	bool _stripShown = false;

};

struct CardFold {
	QRect rest;
	QPolygonF quad;
	QTransform transform;
	float64 topY = 0.;
	float64 bottomY = 0.;
	float64 opacity = 1.;
	float64 fold = 0.;
	bool valid = false;
};

// CardFold is expressed entirely in Content coordinates: `rest` is the
// card's rest rect there, `quad` the folded quadrilateral it is painted
// as, and `transform` maps the first onto the second. A Card paints with
// a widget-local painter, so it reconciles with p.translate(-x(), -y())
// before applying `transform` and draws into `rest`; paintedQuad() and
// paintedOutline() return Content coordinates for the same reason.
[[nodiscard]] CardFold ComputeCardFold(QRect cardRest, float64 fold);

class Card final : public Ui::RpWidget {
public:
	Card(
		QWidget *parent,
		std::shared_ptr<Main::SessionShow> show,
		rpl::producer<TextWithEntities> name);

	void setFold(const CardFold &fold);
	[[nodiscard]] const CardFold &fold() const;
	[[nodiscard]] QPolygonF paintedQuad() const;
	[[nodiscard]] QPolygonF paintedOutline() const;
	void invalidateCache();
	void followCursor();

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	[[nodiscard]] QRect restRect() const;
	[[nodiscard]] QRectF paintedRect() const;
	[[nodiscard]] float64 paintAngle();
	void paintContent(Painter &p, float64 angle);
	void validateCache(float64 angle);
	void refreshAddress();

	const std::shared_ptr<Main::SessionShow> _show;
	style::TextStyle _nameStyle;
	Ui::Text::String _name;
	QString _addressLine1;
	QString _addressLine2;
	CardFold _fold;
	CardBackground _background;
	QImage _cache;
	float64 _cacheAngle = 0.;
	std::unique_ptr<CardAngle> _angle;

};

struct BalancePalette {
	QColor mark;
	QColor amount;
	QColor secondary;
};

enum class BalanceStyle : uchar {
	Balance,
	Minus,
	Plus,
	Exact,
};

class BalanceInk final {
public:
	BalanceInk();

	void setContent(
		CreditsAmount amount,
		const QString &fiat,
		BalanceStyle style = BalanceStyle::Balance);
	void setOuterWidth(int outerWidth);
	void playMark(Fn<void()> repaint);
	void refresh();

	void paint(
		QPainter &p,
		const CardFold &fold,
		const QRegion &cardOutline,
		QRect clip) const;

	[[nodiscard]] QRect boundingRect(const CardFold &fold) const;
	[[nodiscard]] QRect markRect(QRect cardRest) const;
	[[nodiscard]] QRect markPaintRect(const CardFold &fold) const;

private:
	[[nodiscard]] QRectF amountRect(const CardFold &fold) const;
	[[nodiscard]] QRectF fiatRect(const CardFold &fold) const;
	[[nodiscard]] QRectF markVisible(float64 fold) const;
	[[nodiscard]] QRectF markDrawRect(
		float64 fold,
		const QRectF &box,
		const QRectF &visible) const;
	void paintMark(
		QPainter &p,
		float64 fold,
		const QImage &mono,
		bool card) const;
	void paintPass(
		QPainter &p,
		const CardFold &fold,
		const BalancePalette &palette,
		const QImage &mark,
		bool card,
		float64 secondaryOpacity) const;
	[[nodiscard]] QTransform groupTransform(const CardFold &fold) const;

	std::unique_ptr<Lottie::Icon> _markLottie;
	Fn<void()> _markRepaint;
	Wallet::AmountPainter _painter;
	QPainterPath _fiat;
	QImage _markCard;
	QImage _markSettled;
	QRectF _markFrame;
	QRectF _markLottieVisible;
	QRectF _markMonoVisible;
	CreditsAmount _balance;
	QString _fiatText;
	float64 _markTop = 0.;
	float64 _fiatWidth = 0.;
	int _outerWidth = 0;
	BalanceStyle _style = BalanceStyle::Balance;

};

class InfoIslandEntry final : public Ui::SettingsButton {
public:
	InfoIslandEntry(
		QWidget *parent,
		rpl::producer<QString> text,
		const style::SettingsButton &st);
	InfoIslandEntry(
		QWidget *parent,
		std::nullptr_t,
		const style::SettingsButton &st);

	void setRounding(RectParts corners, int radius);
	void setMinimalHeight(int height);

protected:
	int resizeGetHeight(int newWidth) override;
	QImage prepareRippleMask() const override;

private:
	RectParts _corners;
	int _radius = 0;
	int _minimalHeight = 0;

};

class InfoIsland final : public Ui::VerticalLayout {
public:
	explicit InfoIsland(QWidget *parent);

	not_null<Ui::SlideWrap<InfoIslandEntry>*> add(
		object_ptr<InfoIslandEntry> entry);
	[[nodiscard]] rpl::producer<bool> anyShownValue() const;

protected:
	int resizeGetHeight(int newWidth) override;

private:
	void refreshRounding();
	void paintPill(QPainter &p);
	[[nodiscard]] QRect pillRect() const;

	std::vector<Ui::SlideWrap<InfoIslandEntry>*> _entries;
	Ui::MultiSlideTracker _tracker;
	Ui::BoxShadow _shadow;
	QMargins _extend;

};

[[nodiscard]] int PillRadius(QRect pill) {
	return std::min({
		st::walletCardRadius,
		pill.width() / 2,
		pill.height() / 2,
	});
}

InfoIslandEntry::InfoIslandEntry(
	QWidget *parent,
	rpl::producer<QString> text,
	const style::SettingsButton &st)
: Ui::SettingsButton(parent, std::move(text), st) {
}

InfoIslandEntry::InfoIslandEntry(
	QWidget *parent,
	std::nullptr_t,
	const style::SettingsButton &st)
: Ui::SettingsButton(parent, nullptr, st) {
}

void InfoIslandEntry::setRounding(RectParts corners, int radius) {
	if (_corners == corners && _radius == radius) {
		return;
	}
	_corners = corners;
	_radius = radius;
	finishAnimating();
	update();
}

void InfoIslandEntry::setMinimalHeight(int height) {
	if (_minimalHeight == height) {
		return;
	}
	_minimalHeight = height;
	resizeToWidth(width());
}

int InfoIslandEntry::resizeGetHeight(int newWidth) {
	return std::max(
		Ui::SettingsButton::resizeGetHeight(newWidth),
		_minimalHeight);
}

QImage InfoIslandEntry::prepareRippleMask() const {
	if (_radius <= 0 || !_corners) {
		return Ui::RippleAnimation::RectMask(size());
	}
	// The filled RoundRectMask overload starts from a fully opaque mask and
	// only cuts out the corners it is handed, so a null one stays square.
	// Images::CornersMaskRef holds bare pointers into the array it is built
	// from, which is why that array must outlive the call reading them.
	const auto masks = Images::CornersMask(_radius);
	auto corners = Images::CornersMaskRef();
	const auto fill = [&](RectPart corner, int index) {
		if (_corners & corner) {
			corners.p[index] = &masks[index];
		}
	};
	fill(RectPart::TopLeft, Images::kTopLeft);
	fill(RectPart::TopRight, Images::kTopRight);
	fill(RectPart::BottomLeft, Images::kBottomLeft);
	fill(RectPart::BottomRight, Images::kBottomRight);
	return Ui::RippleAnimation::RoundRectMask(size(), corners);
}

InfoIsland::InfoIsland(QWidget *parent)
: Ui::VerticalLayout(parent)
, _shadow(st::walletInfoIslandShadow)
, _extend(_shadow.extend()) {
	Ui::AddSkip(this, _extend.top());
	Ui::AddSkip(this, _extend.bottom());
	paintOn([=](QPainter &p) {
		paintPill(p);
	});
}

not_null<Ui::SlideWrap<InfoIslandEntry>*> InfoIsland::add(
		object_ptr<InfoIslandEntry> entry) {
	// The row margins are vertically zero on purpose: VerticalLayout counts
	// a row margin even for a zero-height child, so any non-zero one would
	// leak height from a hidden entry and grow the island where it must
	// contribute nothing at all.
	const auto wrap = insert(
		count() - 1,
		object_ptr<Ui::SlideWrap<InfoIslandEntry>>(this, std::move(entry)),
		style::margins(
			st::walletIslandMargin.left(),
			0,
			st::walletIslandMargin.right(),
			0));
	_entries.push_back(wrap);
	_tracker.track(wrap);

	// VerticalLayout registers its own heightValue handler inside insert,
	// so that one runs first and has already repositioned the rows and
	// resized the island by the time this one does, which is what makes
	// the wrap's height and y current here. heightValue also emits once
	// on subscription, and refreshing the rounding is idempotent.
	wrap->heightValue(
	) | rpl::on_next([=] {
		refreshRounding();
	}, wrap->lifetime());

	return wrap;
}

rpl::producer<bool> InfoIsland::anyShownValue() const {
	return _tracker.atLeastOneShownValue();
}

int InfoIsland::resizeGetHeight(int newWidth) {
	const auto result = Ui::VerticalLayout::resizeGetHeight(newWidth);
	refreshRounding();
	return result;
}

void InfoIsland::refreshRounding() {
	auto visible = std::vector<not_null<InfoIslandEntry*>>();
	visible.reserve(_entries.size());
	for (const auto &wrap : _entries) {
		if (wrap->height() > 0) {
			visible.push_back(wrap->entity());
		}
	}
	const auto shown = int(visible.size());
	const auto radius = PillRadius(pillRect());
	for (auto i = 0; i != shown; ++i) {
		const auto first = !i;
		const auto last = (i == shown - 1);
		visible[i]->setRounding((first && last)
			? RectParts(RectPart::AllCorners)
			: first
			? (RectPart::TopLeft | RectPart::TopRight)
			: last
			? (RectPart::BottomLeft | RectPart::BottomRight)
			: RectParts(), radius);
	}
	update();
}

QRect InfoIsland::pillRect() const {
	const auto &margin = st::walletIslandMargin;
	return QRect(
		margin.left(),
		_extend.top(),
		width() - margin.left() - margin.right(),
		height() - _extend.top() - _extend.bottom());
}

void InfoIsland::paintPill(QPainter &p) {
	const auto pill = pillRect();
	if (pill.isEmpty()) {
		return;
	}
	Dialogs::PaintPillBackground(p, _shadow, pill, PillRadius(pill));
	auto first = true;
	for (const auto &wrap : _entries) {
		if (wrap->height() <= 0) {
			continue;
		} else if (first) {
			first = false;
			continue;
		}
		p.fillRect(
			pill.x(),
			wrap->y(),
			pill.width(),
			st::lineWidth,
			st::shadowFg);
	}
}

KeyContext::KeyContext(
	std::shared_ptr<Main::SessionShow> show,
	std::shared_ptr<CommentScope> scope,
	Fn<bool()> current,
	Fn<void(KeyAuthorization)> done)
: _show(std::move(show))
, _session(base::make_weak(&_show->session()))
, _scope(std::move(scope))
, _current(std::move(current))
, _done(std::move(done)) {
}

void KeyContext::showOrHideBoxOrLayer(
		std::variant<
			v::null_t,
			object_ptr<Ui::BoxContent>,
			std::unique_ptr<Ui::LayerWidget>> &&layer,
		Ui::LayerOptions,
		anim::type animated) const {
	const auto self = std::const_pointer_cast<KeyContext>(
		shared_from_this());
	if (!valid()) {
		self->cancel();
		return;
	}
	const auto content = std::get_if<object_ptr<Ui::BoxContent>>(&layer);
	if (!content || !*content) {
		self->cancel();
		return;
	}
	const auto prompt = std::make_shared<Prompt>();
	self->acceptClosed();
	prompt->box = content->data();
	_prompts.push_back(prompt);
	const auto weak = std::weak_ptr(self);
	// Native gates report success either from boxClosing() or immediately
	// after closeBox() returns. Register before their deferred preparation,
	// then let the native continuation accept its closed prompts before
	// deciding whether dismissal was terminal. Cloud gates may also show
	// their successor before closing, while an uncontinued dismissal retires
	// the entire comment attempt and closes its remaining owned prompts.
	const auto closed = [weak, prompt] {
		prompt->closing = true;
		if (prompt->cancelOnClose) {
			if (const auto strong = weak.lock()) {
				strong->promptClosed(prompt);
			}
			return;
		}
		crl::on_main([weak, prompt] {
			if (const auto strong = weak.lock()) {
				strong->promptClosed(prompt);
			}
		});
	};
	prompt->box->boxClosing() | rpl::on_next(
		closed,
		prompt->box->lifetime());
	prompt->box->lifetime().add(closed);
	_show->showOrHideBoxOrLayer(
		std::move(layer),
		Ui::LayerOption::KeepOther,
		animated);
}

not_null<QWidget*> KeyContext::toastParent() const {
	return _show->toastParent();
}

bool KeyContext::valid() const {
	return !_finished
		&& _session
		&& _show->valid()
		&& _current()
		&& (!_scope || _session->wallet().commentScopeCurrent(_scope));
}

KeyContext::operator bool() const {
	return valid();
}

Main::Session &KeyContext::session() const {
	Expects(_session != nullptr);

	return *_session;
}

std::shared_ptr<CommentScope> KeyContext::scope() const {
	return _scope;
}

std::shared_ptr<Main::SessionShow> KeyContext::plain() const {
	return _show;
}

CustodyInstaller KeyContext::installer() {
	const auto self = shared_from_this();
	// WHY: the ladder stores the key on this device, which is right to
	// finish even once the action that asked for it has expired, so it is
	// judged by the window it lives in and not by that action. Handing it
	// this context instead made every unrelated wallet event close the
	// chooser with nothing stored and nothing said, and the next press
	// started the whole restore again.
	const auto native = MakeCustodyInstaller(_show);
	return [=](CustodyInstallRequest request) {
		if (!self->valid()) {
			request.ready({});
			self->cancel();
			return;
		}
		self->_installing = true;
		request.passcodeCreated = [
			self,
			created = std::move(request.passcodeCreated)
		](quint32 previousEpoch, quint32 epoch) {
			// Judged like the rest of the ladder: the vault transition has
			// to be sound, and whether the action that asked for the key is
			// still there decides nothing about storing it.
			if (!created || !created(previousEpoch, epoch)) {
				return false;
			}
			self->acceptClosed();
			return true;
		};
		request.ready = [=, ready = std::move(request.ready)](
				CustodyInstall result) {
			self->_installing = false;
			self->acceptClosed();
			const auto installed = result.grant != nullptr;
			ready(std::move(result));
			if (!installed) {
				self->cancel();
			}
		};
		native(std::move(request));
	};
}

void KeyContext::acceptClosed() {
	for (const auto &prompt : _prompts) {
		if (prompt->closing) {
			prompt->accepted = true;
		}
	}
}

void KeyContext::allowPromptRetry(
		base::weak_qptr<Ui::BoxContent> box) {
	for (const auto &prompt : _prompts) {
		if (prompt->box == box) {
			prompt->accepted = true;
			return;
		}
	}
}

void KeyContext::cancelOnClose(
		base::weak_qptr<Ui::BoxContent> box,
		bool allowSuccessor) {
	if (!box) {
		cancel();
		return;
	}
	for (const auto &prompt : _prompts) {
		if (prompt->box == box) {
			prompt->cancelOnClose = true;
			prompt->allowSuccessor = allowSuccessor;
			if (prompt->closing) {
				promptClosed(prompt);
			}
			return;
		}
	}
}

void KeyContext::closePrompt(base::weak_qptr<Ui::BoxContent> box) {
	for (const auto &prompt : _prompts) {
		if (prompt->box == box) {
			prompt->accepted = true;
			if (box && !prompt->closing && box->hasDelegate()) {
				box->closeBox();
			}
			return;
		}
	}
}

void KeyContext::ready(KeyAuthorization auth) {
	if (!valid() || !auth.grant) {
		cancel();
		return;
	}
	finish(std::move(auth));
}

void KeyContext::cancel() {
	finish({});
}

void KeyContext::finish(KeyAuthorization auth) {
	if (_finished) {
		return;
	}
	_finished = true;
	const auto scope = _scope;
	auto lifetime = base::take(_lifetime);
	const auto done = base::take(_done);
	const auto prompts = base::take(_prompts);
	if (!auth.grant && scope) {
		scope->cancel();
	}
	lifetime.destroy();
	for (const auto &prompt : ranges::views::reverse(prompts)) {
		prompt->accepted = true;
		if (const auto box = prompt->box.get()) {
			if (!prompt->closing && box->hasDelegate()) {
				box->closeBox();
			}
		}
	}
	if (done) {
		done(std::move(auth));
	}
}

rpl::lifetime &KeyContext::lifetime() {
	return _lifetime;
}

void KeyContext::promptClosed(const std::shared_ptr<Prompt> &prompt) {
	if (_finished || prompt->accepted) {
		return;
	} else if (_installing) {
		// The install ladder is this press continuing, not the user
		// abandoning it, and it owns no prompt of this context any more.
		prompt->accepted = true;
		return;
	}
	auto later = false;
	for (const auto &other : _prompts) {
		if (prompt->allowSuccessor
			&& later
			&& other->box
			&& !other->closing) {
			prompt->accepted = true;
			return;
		}
		later = later || (other == prompt);
	}
	cancel();
}

EncryptedCommentLabel::EncryptedCommentLabel(
	QWidget *parent,
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<Main::SessionShow> show,
	TransferItem item,
	Fn<bool()> originCurrent)
: FlatLabel(parent, st::walletCommentLabel)
, _cover(TransferCommentCover(item))
, _revealable(EncryptedCommentRevealable(item))
, _comment(&show->session(), std::move(item), [
		this,
		originCurrent = std::move(originCurrent)] {
	return !_closed && (!originCurrent || originCurrent());
}) {
	setContextCopyText(QString());
	setSelectable(!_revealable);
	setMarkedText(_cover);
	setContextMenuHook([weak = base::make_weak(this)](ContextMenuRequest request) {
		if (!weak || !weak->_comment.plaintext()) {
			return;
		}
		request.menu->addAction(tr::lng_context_copy_text(tr::now), [weak] {
			if (weak) {
				if (const auto &text = weak->_comment.plaintext()) {
					TextUtilities::SetClipboardText(TextForMimeData::Simple(*text));
				}
			}
		});
	});
	setClickHandlerFilter([=](const ClickHandlerPtr &, Qt::MouseButton button) {
		if (button != Qt::LeftButton) {
			return false;
		} else if (!_revealable) {
			return true;
		} else if (!_comment.plaintext()) {
			_comment.activate(show);
		}
		return false;
	});
	setAnimationsPausedCallback([] {
		return On(PowerSaving::kChatSpoiler)
			? WhichAnimationsPaused::Spoiler
			: WhichAnimationsPaused::None;
	});
	_comment.changes() | rpl::on_next([=] {
		if (_closed) {
			return; // the closing reset must not flash the cover as it fades
		}
		const auto revealed = _comment.plaintext().has_value();
		if (revealed == _revealed) {
			return;
		}
		_revealed = revealed;
		if (const auto &text = _comment.plaintext()) {
			setText(*text);
			setSelectable(true);
		} else {
			setSelectable(false);
			setContextCopyText(QString());
			setMarkedText(_cover);
		}
	}, lifetime());
	box->boxClosing() | rpl::on_next([=] {
		_closed = true;
		_comment.reset();
	}, lifetime());
}

QString EncryptedCommentLabel::accessibilityName() {
	const auto &text = _comment.plaintext();
	return text
		? *text
		: _revealable
		? tr::lng_action_gram_transfer_encrypted_comment(tr::now)
		: _cover.text;
}

[[nodiscard]] int PanelCardWidth() {
	return st::walletPanelSize.width()
		- st::walletCardMargin.left()
		- st::walletCardMargin.right();
}

[[nodiscard]] QRect CardQrRect(int cardWidth) {
	return QRect(
		cardWidth - st::walletCardQrRight - st::walletCardQrSize.width(),
		st::walletCardQrTop,
		st::walletCardQrSize.width(),
		st::walletCardQrSize.height());
}

[[nodiscard]] QRect TransferCardInfoRect(int cardWidth) {
	return QRect(
		cardWidth - st::walletCardContentLeft - st::walletCardQrSize.width(),
		st::walletCardQrTop,
		st::walletCardQrSize.width(),
		st::walletCardQrSize.height());
}

// WHY: the plate under this glyph is fixed brand appearance no theme can
// move, and windowSubTextFg over it measured 1.90:1 in the day theme;
// this grey clears 2.0:1 over every point of the plate the glyph covers.
[[nodiscard]] QColor CardQrIconFg() {
	return QColor(0x73, 0x73, 0x73);
}

[[nodiscard]] QString GroupedAddressLine(
		const QString &address,
		int offset) {
	auto groups = QStringList();
	for (auto i = 0; i != kAddressGroupsPerLine; ++i) {
		groups.append(address.mid(
			offset + i * kAddressGroup,
			kAddressGroup));
	}
	return groups.join(QChar(' '));
}

[[nodiscard]] QStringList TransferCardLines(
		const QString &destination,
		int recipients) {
	if (recipients >= 2) {
		const auto summary = tr::lng_wallet_connect_request_recipients(
			tr::now,
			lt_count,
			recipients);
		return { summary };
	}
	auto result = QStringList();
	const auto perLine = kAddressGroup * kAddressGroupsPerLine;
	for (auto offset = 0; offset < destination.size(); offset += perLine) {
		result.append(GroupedAddressLine(destination, offset).trimmed());
	}
	return result;
}

// The sheet's address presentation is the friendly form, so a raw address
// the engine cannot convert is a reading this sheet does not have and
// builds no row. Substituting the raw form would show a different kind of
// address without saying so.
[[nodiscard]] std::optional<QString> DetailsFriendlyAddress(
		const TransferItem &item) {
	const auto friendly = FormatFriendly(
		item.counterparty,
		item.counterpartyBounceable);
	if (friendly.isEmpty()) {
		return std::nullopt;
	}
	return friendly;
}

[[nodiscard]] TextWithEntities DetailsAddressValue(
		const QString &address) {
	auto groups = QStringList();
	for (auto offset = 0; offset < address.size(); offset += kAddressGroup) {
		groups.append(address.mid(offset, kAddressGroup));
	}
	return Ui::Text::Wrapped(
		{ groups.join(QChar(' ')) },
		EntityType::Code,
		{});
}

[[nodiscard]] Fn<void()> CopyAddressCallback(
		std::shared_ptr<Ui::Show> show,
		const QString &address) {
	return CopyTextCallback(
		std::move(show),
		address,
		tr::lng_gift_unique_address_copied(tr::now));
}

} // namespace

Fn<void()> CopyTextCallback(
		std::shared_ptr<Ui::Show> show,
		QString text,
		QString toast) {
	return [=] {
		TextUtilities::SetClipboardText(TextForMimeData::Simple(text));
		show->showToast({
			.text = { toast },
			.iconLottie = u"toast/copy"_q,
			.iconLottieSize = st::toastLottieIconSize,
		});
	};
}

object_ptr<Ui::FlatLabel> AddressValueLabel(
		not_null<QWidget*> parent,
		std::shared_ptr<Ui::Show> show,
		const QString &address) {
	auto result = object_ptr<Ui::FlatLabel>(
		parent,
		rpl::single(DetailsAddressValue(address)),
		st::walletDetailsAddressLabel);
	result->setTryMakeSimilarLines(true);
	const auto copy = CopyAddressCallback(std::move(show), address);
	result->setClickHandlerFilter([=](const auto &...) {
		copy();
		return false;
	});
	return result;
}

namespace {

[[nodiscard]] object_ptr<Ui::FlatLabel> NameValueLabel(
		not_null<Ui::RpWidget*> parent,
		std::shared_ptr<Ui::Show> show,
		const QString &name,
		const QString &address) {
	auto result = object_ptr<Ui::FlatLabel>(
		parent,
		rpl::single(tr::link(name)),
		st::defaultTableValue);
	const auto copy = CopyAddressCallback(std::move(show), address);
	result->setClickHandlerFilter([=](const auto &...) {
		copy();
		return false;
	});
	return result;
}

[[nodiscard]] QString OnrampProvider(const TransferItem &item) {
	return (item.kind == TransferItem::Kind::Onramp)
		? item.provider
		: QString();
}

enum class RowAvatar {
	Peer,
	In,
	Out,
	KeyChange,
	Gear,
};

struct HistoryRowContent {
	QString title;
	QString subtitle;
	QString date;
	int64 amountNano = 0;
	bool incoming = false;
	bool pending = false;
	bool failed = false;
	RowAvatar avatar = RowAvatar::Out;
	PeerData *peer = nullptr;
	bool itemAmount = false;
	QString collectible;

	friend bool operator==(
		const HistoryRowContent &,
		const HistoryRowContent &) = default;
};

struct SendingRow {
	std::string operationId;
	Ui::VerticalLayout *slot = nullptr;
	TransferItem item;
	HistoryRowContent content;
	SendingHistoryRow *look = nullptr;
	// Owed by the hand-over or the creation's postponed call, whichever runs first
	bool revealPending = false;
};

[[nodiscard]] QString ShortAddressForm(
		const QString &full,
		int chars = kShortAddressChars) {
	return full.left(chars) + QChar(0x2026) + full.right(chars);
}

[[nodiscard]] QString ShortAddress(const QString &address) {
	if (address.isEmpty()) {
		return QString();
	}
	const auto full = FormatFriendly(address, true);
	return ShortAddressForm(full);
}

[[nodiscard]] QString CounterpartyAddress(const TransferItem &item) {
	return item.counterparty.isEmpty()
		? QString()
		: FormatFriendly(item.counterparty, item.counterpartyBounceable);
}

void SetAmountColor(
		not_null<Ui::FlatLabel*> major,
		not_null<Ui::FlatLabel*> minor,
		const style::color &color) {
	rpl::single(rpl::empty) | rpl::then(
		style::PaletteChanged()
	) | rpl::on_next([=] {
		const auto scope = WindowPaletteScope(major);
		major->setTextColorOverride(color->c);
		minor->setTextColorOverride(color->c);
	}, major->lifetime());
}

[[nodiscard]] QString GramMajorPart(int64 amountNano) {
	return QString::number(amountNano / Ui::kNanosInOne);
}

[[nodiscard]] QString GramMinorPart(int64 amountNano) {
	const auto tiny = TinyAmountFraction(amountNano, kGramDigits);
	if (!tiny.isEmpty()) {
		return QString(QLocale().decimalPoint()) + tiny;
	}
	const auto cents = std::abs(amountNano % Ui::kNanosInOne)
		/ (Ui::kNanosInOne / 100);
	return QString(QLocale().decimalPoint())
		+ u"%1"_q.arg(cents, 2, 10, QChar('0'));
}

struct RowAmountText {
	QString major;
	TextWithEntities minor;
	Ui::Text::MarkedContext context;
};

[[nodiscard]] QString RowAmountSign(bool incoming) {
	return incoming ? u"+"_q : QString(kMinus);
}

[[nodiscard]] QString RowAmountWhole(int64 amountNano, const QString &sign) {
	return (amountNano ? sign : QString()) + GramMajorPart(amountNano);
}

[[nodiscard]] RowAmountText PrepareRowAmountText(
		int64 amountNano,
		const QString &sign) {
	auto helper = Ui::Text::CustomEmojiHelper();
	auto minor = tr::marked(GramMinorPart(amountNano));
	minor.append(helper.paletteDependent({
		.factory = [] {
			return Ui::Earn::IconCurrencyTwoTone(
				st::walletRowMarkSize,
				st::windowActiveTextFg->c);
		},
		.margin = st::walletRowIconMargin,
	}));
	return {
		.major = RowAmountWhole(amountNano, sign),
		.minor = std::move(minor),
		.context = helper.context(),
	};
}

[[nodiscard]] RowAmountText PrepareRowItemAmountText(bool incoming) {
	return {
		.major = ((incoming ? QChar('+') : kMinus)
			+ tr::lng_wallet_row_items(tr::now, lt_count, 1)),
		.minor = Ui::Text::IconEmoji(incoming
			? &st::walletRowItemMarkIn
			: &st::walletRowItemMarkOut),
	};
}

[[nodiscard]] const style::color &RowAmountColor(
		bool incoming,
		bool pending,
		bool failed) {
	return (pending || failed)
		? st::windowSubTextFg
		: incoming
		? st::boxTextFgGood
		: st::windowBoldFg;
}

void SetRowAmountText(
		not_null<Ui::FlatLabel*> major,
		not_null<Ui::FlatLabel*> minor,
		int64 amountNano,
		const QString &sign) {
	auto text = PrepareRowAmountText(amountNano, sign);
	major->setText(text.major);
	minor->setMarkedText(std::move(text.minor), std::move(text.context));
}

void SetRowAmount(
		not_null<Ui::FlatLabel*> major,
		not_null<Ui::FlatLabel*> minor,
		int64 amountNano,
		bool incoming,
		bool pending,
		bool failed) {
	SetRowAmountText(
		major,
		minor,
		amountNano,
		RowAmountSign(incoming));
	SetAmountColor(major, minor, RowAmountColor(incoming, pending, failed));
}

void SetRowItemAmount(
		not_null<Ui::FlatLabel*> major,
		not_null<Ui::FlatLabel*> minor,
		bool incoming,
		bool failed) {
	auto text = PrepareRowItemAmountText(incoming);
	major->setText(text.major);
	minor->setMarkedText(std::move(text.minor), std::move(text.context));
	SetAmountColor(major, minor, RowAmountColor(incoming, false, failed));
}

void PaintRowAvatar(Painter &p, QRect rect, RowAvatar avatar) {
	auto hq = PainterHighQualityEnabler(p);
	const auto in = (avatar == RowAvatar::In);
	const auto flat = (avatar == RowAvatar::KeyChange)
		|| (avatar == RowAvatar::Gear);
	if (flat) {
		p.setBrush(st::historyPeerArchiveUserpicBg);
	} else {
		const auto &top = in
			? st::historyPeer2UserpicBg
			: st::historyPeer4UserpicBg;
		const auto &bottom = in
			? st::historyPeer2UserpicBg2
			: st::historyPeer4UserpicBg2;
		auto gradient = QLinearGradient(
			rect.topLeft(),
			rect.bottomLeft());
		gradient.setStops({ { 0., top->c }, { 1., bottom->c } });
		p.setBrush(gradient);
	}
	p.setPen(Qt::NoPen);
	p.drawEllipse(rect);
	const auto icon = in
		? &st::walletRowArrowIn
		: (avatar == RowAvatar::KeyChange)
		? &st::walletRowKeyIcon
		: (avatar == RowAvatar::Gear)
		? &st::walletRowGearIcon
		: &st::walletRowArrowOut;
	icon->paintInCenter(p, rect);
}

[[nodiscard]] int LabelLineHeight(const style::FlatLabel &st) {
	return std::max(st.style.font->height, st.style.lineHeight);
}

struct HistoryRowHeights {
	int title = 0;
	std::optional<int> subtitle;
	int date = 0;
	int amount = 0;
};

struct HistoryRowLine {
	int top = 0;
	int lines = 0;
};

struct HistoryRowLayout {
	HistoryRowLine title;
	HistoryRowLine subtitle;
	HistoryRowLine date;
	int height = 0;
	int avatarCenter = 0;
	int amountTop = 0;
	int titleWidth = 0;
	int textWidth = 0;
	int chipTop = 0;
};

[[nodiscard]] int HistoryRowTitleSkip(int amountWidth) {
	return amountWidth + st::walletRowSkip;
}

[[nodiscard]] HistoryRowLayout ComputeHistoryRowLayout(
		const HistoryRowHeights &heights) {
	const auto &padding = st::walletRowPadding;
	auto result = HistoryRowLayout();
	auto top = padding.top();
	result.title = {
		.top = top,
		.lines = heights.title / LabelLineHeight(st::walletRowTitleLabel),
	};
	top += heights.title;
	if (heights.subtitle) {
		top += st::walletRowSkip;
		result.subtitle = {
			.top = top,
			.lines = (*heights.subtitle
				/ LabelLineHeight(st::walletRowSubtitleLabel)),
		};
		top += *heights.subtitle;
	}
	top += st::walletRowSkip;
	result.date = {
		.top = top,
		.lines = heights.date / LabelLineHeight(st::walletRowDateLabel),
	};
	result.height = top + heights.date + padding.bottom();
	result.avatarCenter = heights.subtitle
		? (padding.top()
			+ (heights.title + st::walletRowSkip + *heights.subtitle) / 2)
		: (result.height / 2);
	result.amountTop = padding.top() + (heights.title - heights.amount) / 2;
	return result;
}

struct HistoryRowText {
	Ui::Text::String title;
	Ui::Text::String subtitle;
	Ui::Text::String date;
	Ui::Text::String major;
	Ui::Text::String minor;
	bool subtitleShown = false;
	bool chip = false;
};

// Built as Ui::FlatLabel builds its text, so it measures as the label does.
[[nodiscard]] Ui::Text::String HistoryRowLabelText(
		const style::FlatLabel &st,
		const QString &text) {
	return Ui::Text::String(
		st.style,
		text,
		kPlainTextOptions,
		st.minWidth ? st.minWidth : Ui::kQFixedMax);
}

[[nodiscard]] int HistoryRowLabelHeight(
		const style::FlatLabel &st,
		const Ui::Text::String &text,
		int width,
		bool breakEverywhere) {
	const auto full = text.countHeight(width, breakEverywhere);
	return st.maxHeight ? std::min(full, st.maxHeight) : full;
}

[[nodiscard]] HistoryRowText PrepareHistoryRowText(
		const HistoryRowContent &content,
		Fn<void()> repaint = nullptr) {
	auto amount = content.itemAmount
		? PrepareRowItemAmountText(content.incoming)
		: PrepareRowAmountText(
			content.amountNano,
			RowAmountSign(content.incoming));
	if (repaint) {
		amount.context.repaint = std::move(repaint);
	}
	auto result = HistoryRowText{
		.title = HistoryRowLabelText(st::walletRowTitleLabel, content.title),
		.subtitle = HistoryRowLabelText(
			st::walletRowSubtitleLabel,
			content.subtitle),
		.date = HistoryRowLabelText(st::walletRowDateLabel, content.date),
		.major = HistoryRowLabelText(
			st::walletRowAmountMajorLabel,
			amount.major),
		.subtitleShown = !content.subtitle.isEmpty(),
		.chip = content.itemAmount,
	};
	result.minor.setMarkedText(
		st::walletRowAmountMinorLabel.style,
		amount.minor,
		kMarkupTextOptions,
		amount.context);
	return result;
}

[[nodiscard]] HistoryRowLayout MeasureHistoryRow(
		const HistoryRowText &text,
		int width) {
	const auto &padding = st::walletRowPadding;
	const auto textWidth = std::max(
		width - padding.left() - padding.right(),
		0);
	const auto titleWidth = std::max(
		textWidth - HistoryRowTitleSkip(
			text.major.maxWidth() + text.minor.maxWidth()),
		0);
	auto result = ComputeHistoryRowLayout({
		.title = HistoryRowLabelHeight(
			st::walletRowTitleLabel,
			text.title,
			titleWidth,
			true),
		.subtitle = (text.subtitleShown
			? std::make_optional(HistoryRowLabelHeight(
				st::walletRowSubtitleLabel,
				text.subtitle,
				textWidth,
				true))
			: std::nullopt),
		.date = HistoryRowLabelHeight(
			st::walletRowDateLabel,
			text.date,
			textWidth,
			false),
		.amount = HistoryRowLabelHeight(
			st::walletRowAmountMajorLabel,
			text.major,
			text.major.maxWidth(),
			false),
	});
	result.titleWidth = titleWidth;
	result.textWidth = textWidth;
	if (text.chip) {
		result.chipTop = result.height
			- padding.bottom()
			+ st::walletChipTopSkip;
		result.height += st::walletChipTopSkip + st::walletRowIconSize;
	}
	return result;
}

struct RowAmountPlacement {
	QPoint major;
	QPoint minor;
};

[[nodiscard]] RowAmountPlacement PlaceRowAmount(
		const HistoryRowText &text,
		int amountTop,
		int width) {
	const auto rtl = style::RightToLeft();
	const auto mirror = [&](int left, int size) {
		return rtl ? (width - left - size) : left;
	};
	const auto majorWidth = text.major.maxWidth();
	const auto minorWidth = text.minor.maxWidth();
	const auto minorLeft = width - st::walletRowPadding.right() - minorWidth;
	return {
		.major = QPoint(
			mirror(minorLeft - majorWidth, majorWidth),
			amountTop),
		.minor = QPoint(
			mirror(minorLeft, minorWidth),
			amountTop + st::walletRowAmountMinorSkip),
	};
}

struct HistoryRowChipState {
	Gram::NftKind kind = Gram::NftKind::Generic;
	Ui::Text::String title;
	Ui::Text::String subtitle;
	int natural = 0;
};

class HistoryRowButton final : public Ui::SettingsButton {
public:
	using Ui::SettingsButton::SettingsButton;

	void setPaintUnderRipple(Fn<void(Painter&)> paint);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	Fn<void(Painter&)> _paintUnderRipple;

};

void HistoryRowButton::setPaintUnderRipple(Fn<void(Painter&)> paint) {
	_paintUnderRipple = std::move(paint);
	update();
}

void HistoryRowButton::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto over = (isOver() || isDown()) && !isDisabled();
	paintBg(p, e->rect(), over);
	if (_paintUnderRipple) {
		_paintUnderRipple(p);
	}
	paintRipple(p, 0, 0);
	const auto outerw = width();
	paintText(p, over, outerw);
	paintToggle(p, outerw);
}

void PaintHistoryRowChipPlate(
		Painter &p,
		int outerWidth,
		const HistoryRowChipState &state) {
	const auto side = st::walletRowIconSize;
	const auto radius = st::walletCollectibleThumbRadius;
	const auto plate = std::min(state.natural, outerWidth);
	const auto plateLeft = style::RightToLeft() ? (outerWidth - plate) : 0;
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::windowBgOver);
	p.drawRoundedRect(
		QRect(plateLeft, 0, plate, side),
		radius,
		radius);
}

void PaintHistoryRowChipArtwork(
		Painter &p,
		int outerWidth,
		const HistoryRowChipState &state,
		const std::shared_ptr<CollectibleMedia> &media,
		const QString &address) {
	const auto side = st::walletRowIconSize;
	const auto radius = st::walletCollectibleThumbRadius;
	const auto rtl = style::RightToLeft();
	const auto square = QRect(rtl ? (outerWidth - side) : 0, 0, side, side);
	const auto dark = (state.kind == Gram::NftKind::TelegramUsername)
		|| (state.kind == Gram::NftKind::TelegramNumber);
	if (dark) {
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::callBgOpaque);
		p.drawRoundedRect(square, radius, radius);
	}
	if (state.kind == Gram::NftKind::TelegramUsername) {
		st::walletChipUsernameIcon.paintInCenter(p, square);
	} else if (state.kind == Gram::NftKind::TelegramNumber) {
		st::walletChipNumberIcon.paintInCenter(p, square);
	} else {
		media->paintArtwork(p, address, square, outerWidth, radius);
	}
}

void PaintHistoryRowChipText(
		Painter &p,
		int outerWidth,
		const HistoryRowChipState &state) {
	const auto side = st::walletRowIconSize;
	const auto plate = std::min(state.natural, outerWidth);
	const auto rtl = style::RightToLeft();
	const auto plateLeft = rtl ? (outerWidth - plate) : 0;
	const auto available = plate
		- side
		- st::walletChipTextSkip
		- st::walletChipPadding.right();
	if (available <= 0) {
		return;
	}
	const auto textLeft = rtl
		? (plateLeft + st::walletChipPadding.right())
		: (side + st::walletChipTextSkip);
	const auto titleHeight = st::walletCollectibleTitleStyle.font->height;
	const auto subtitleHeight = st::walletRowDateLabel.style.font->height;
	const auto top = (side
		- titleHeight
		- st::walletRowSkip
		- subtitleHeight) / 2;
	p.setPen(st::windowBoldFg);
	state.title.draw(p, {
		.position = { textLeft, top },
		.outerWidth = outerWidth,
		.availableWidth = available,
		.palette = &st::walletCollectibleTitlePalette,
		.elisionLines = 1,
	});
	p.setPen(st::windowSubTextFg);
	state.subtitle.draw(p, {
		.position = { textLeft, top + titleHeight + st::walletRowSkip },
		.outerWidth = outerWidth,
		.availableWidth = available,
		.elisionLines = 1,
	});
}

void TrackHistoryRowChip(
		not_null<HistoryRowChipState*> state,
		std::shared_ptr<CollectibleMedia> media,
		QString address,
		Fn<void()> repaint,
		rpl::lifetime &lifetime) {
	const auto refresh = [=] {
		const auto view = media->view(address);
		state->kind = view.kind;
		using Kind = Gram::NftKind;
		state->title.setMarkedText(
			st::walletCollectibleTitleStyle,
			((view.kind == Kind::TelegramUsername)
				? Ui::Text::Semibold('@' + view.key)
				: (view.kind == Kind::TelegramNumber)
				? Ui::Text::Semibold(Ui::FormatPhone(view.key))
				: CollectibleTitleText(view)));
		state->subtitle.setText(
			st::walletRowDateLabel.style,
			CollectibleKindText(view));
		state->natural = st::walletRowIconSize
			+ st::walletChipTextSkip
			+ std::max(state->title.maxWidth(), state->subtitle.maxWidth())
			+ st::walletChipPadding.right();
		repaint();
	};
	const auto mine = [=](const QString &changed) {
		return (changed == address);
	};
	media->changed(
	) | rpl::filter(mine) | rpl::on_next(refresh, lifetime);
	media->repaint(
	) | rpl::filter(mine) | rpl::on_next(repaint, lifetime);
	media->resolveBackground(address);
	refresh();
}

void AddHistoryRowChip(
		not_null<Ui::VerticalLayout*> inner,
		not_null<HistoryRowButton*> button,
		std::shared_ptr<CollectibleMedia> media,
		QString address) {
	Ui::AddSkip(inner, st::walletChipTopSkip);
	const auto chip = inner->add(object_ptr<Ui::FixedHeightWidget>(
		inner,
		st::walletRowIconSize));
	chip->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto state = chip->lifetime().make_state<HistoryRowChipState>();
	const auto repaint = [=] {
		chip->update();
		button->update(Ui::MapFrom(button, chip, chip->rect()));
	};
	chip->paintRequest(
	) | rpl::on_next([=] {
		auto p = Painter(chip);
		PaintHistoryRowChipArtwork(p, chip->width(), *state, media, address);
		PaintHistoryRowChipText(p, chip->width(), *state);
	}, chip->lifetime());
	button->setPaintUnderRipple([=](Painter &p) {
		const auto origin = Ui::MapFrom(button, chip, QPoint());
		p.translate(origin);
		PaintHistoryRowChipPlate(p, chip->width(), *state);
		p.translate(-origin);
	});
	TrackHistoryRowChip(state, media, address, repaint, chip->lifetime());
}

not_null<Ui::RpWidget*> AddHistoryRow(
		not_null<Ui::VerticalLayout*> list,
		const HistoryRowContent &content,
		Fn<void()> clicked,
		std::shared_ptr<CollectibleMedia> media = nullptr) {
	const auto wrap = list->add(
		object_ptr<Ui::PaddingWrap<Ui::VerticalLayout>>(
			list,
			object_ptr<Ui::VerticalLayout>(list),
			st::walletRowPadding));
	const auto inner = wrap->entity();
	inner->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto button = Ui::CreateChild<HistoryRowButton>(
		wrap,
		rpl::single(QString()));
	button->setClickedCallback(std::move(clicked));
	const auto major = Ui::CreateChild<Ui::FlatLabel>(
		wrap,
		st::walletRowAmountMajorLabel);
	major->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto minor = Ui::CreateChild<Ui::FlatLabel>(
		wrap,
		st::walletRowAmountMinorLabel);
	minor->setAttribute(Qt::WA_TransparentForMouseEvents);
	if (content.itemAmount) {
		SetRowItemAmount(major, minor, content.incoming, content.failed);
	} else {
		SetRowAmount(
			major,
			minor,
			content.amountNano,
			content.incoming,
			content.pending,
			content.failed);
	}
	const auto title = inner->add(
		object_ptr<Ui::FlatLabel>(
			inner,
			content.title,
			st::walletRowTitleLabel),
		{ 0, 0, HistoryRowTitleSkip(major->width() + minor->width()), 0 });
	title->setBreakEverywhere(true);
	auto subtitle = (Ui::FlatLabel*)nullptr;
	if (!content.subtitle.isEmpty()) {
		Ui::AddSkip(inner, st::walletRowSkip);
		subtitle = inner->add(object_ptr<Ui::FlatLabel>(
			inner,
			content.subtitle,
			st::walletRowSubtitleLabel));
		subtitle->setBreakEverywhere(true);
	}
	Ui::AddSkip(inner, st::walletRowSkip);
	const auto date = inner->add(object_ptr<Ui::FlatLabel>(
		inner,
		content.date,
		st::walletRowDateLabel));
	const auto hasChip = content.itemAmount && (media != nullptr);
	if (hasChip) {
		AddHistoryRowChip(inner, button, media, content.collectible);
	}

	const auto circle = Ui::CreateChild<Ui::RpWidget>(wrap);
	circle->resize(st::walletRowIconSize, st::walletRowIconSize);
	circle->setAttribute(Qt::WA_TransparentForMouseEvents);
	if (const auto peer = content.peer) {
		const auto userpic = circle->lifetime().make_state<
			Ui::PeerUserpicView>(peer->createUserpicView());
		peer->session().downloaderTaskFinished(
		) | rpl::on_next([=] {
			circle->update();
		}, circle->lifetime());
		circle->paintRequest(
		) | rpl::on_next([=] {
			auto p = Painter(circle);
			peer->paintUserpicLeft(
				p,
				*userpic,
				0,
				0,
				circle->width(),
				circle->width());
		}, circle->lifetime());
	} else {
		const auto avatar = content.avatar;
		circle->paintRequest(
		) | rpl::on_next([=] {
			auto p = Painter(circle);
			PaintRowAvatar(p, circle->rect(), avatar);
		}, circle->lifetime());
	}
	Ui::ToggleChildrenVisibility(wrap, true);
	wrap->geometryValue(
	) | rpl::on_next([=](const QRect &g) {
		const auto layout = ComputeHistoryRowLayout({
			.title = title->height(),
			.subtitle = (subtitle
				? std::make_optional(subtitle->height())
				: std::nullopt),
			.date = date->height(),
			.amount = major->height(),
		});
		circle->moveToLeft(
			st::walletRowIconLeft,
			layout.avatarCenter - circle->height() / 2);
		minor->moveToRight(
			st::walletRowPadding.right(),
			layout.amountTop + st::walletRowAmountMinorSkip);
		major->moveToRight(
			st::walletRowPadding.right() + minor->width(),
			layout.amountTop);
		button->resize(g.size());
		button->lower();
	}, wrap->lifetime());
	return wrap;
}

struct SendingRowLayout {
	QRect pill;
	int radius = 0;
	QRect avatar;
	QPointF badge;
	QPoint diamond;
	QPointF amount;
	int titleWidth = 0;
	int textWidth = 0;
};

struct SendingRowSettleTarget {
	QPoint major;
	QPoint minor;
	int boundary = 0;
	QPointF anchor;
	QRectF diamond;
	int titleWidth = 0;
	int textWidth = 0;
};

struct SendingRowSettleProgress {
	crl::time elapsed = 0;
	float64 pill = 0.;
	float64 badge = 1.;
	float64 label = 0.;
	float64 amount = 0.;
	float64 amountFade = 0.;
	float64 emoji = 0.;
};

struct SendingRowSettleRequest {
	HistoryRowContent content;
	Fn<void()> done;
	bool burst = false;
};

class SendingHistoryRow final : public Ui::AbstractButton {
public:
	SendingHistoryRow(
		not_null<Ui::RpWidget*> parent,
		not_null<Ui::RpWidget*> layer,
		not_null<Ui::RpWidget*> bounds,
		HistoryRowContent content,
		std::shared_ptr<CollectibleMedia> media);

	void setContent(HistoryRowContent content);
	void awaitDiamond();
	void landDiamond(std::unique_ptr<Lottie::Icon> icon, crl::time loopStarted);
	void cancelDiamondAwait();
	void scheduleBump(crl::time at);
	void settle(HistoryRowContent content, bool burst, Fn<void()> done);

	[[nodiscard]] bool surfaceShown() const;
	[[nodiscard]] bool inView() const;
	[[nodiscard]] bool settling() const;
	[[nodiscard]] bool settled() const;
	[[nodiscard]] QRectF diamondTarget(crl::time now) const;

	QString accessibilityName() override;

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(int visibleTop, int visibleBottom) override;

private:
	struct Settle;

	[[nodiscard]] const HistoryRowLayout &rowLayout() const;
	void updateRowLayout();
	[[nodiscard]] SendingRowLayout layout() const;
	[[nodiscard]] SendingRowSettleTarget settleTarget() const;
	[[nodiscard]] crl::time settleDuration() const;
	[[nodiscard]] crl::time settleElapsed(crl::time now) const;
	[[nodiscard]] SendingRowSettleProgress settleProgress(
		crl::time now) const;
	[[nodiscard]] QRectF diamondCanvas(
		const SendingRowLayout &layout,
		const SendingRowSettleTarget &target,
		crl::time elapsed) const;
	[[nodiscard]] QTransform surfaceTransform(crl::time now) const;
	[[nodiscard]] int surfaceSkip() const;
	void updateSurfaceGeometry();
	void paintSurface();
	void paintAvatar(
		Painter &p,
		const SendingRowLayout &layout,
		float64 cutout,
		float64 swap);
	void paintTexts(
		Painter &p,
		const SendingRowLayout &layout,
		const SendingRowSettleTarget &target,
		const SendingRowSettleProgress &progress);
	void paintSettleAmount(
		Painter &p,
		const SendingRowLayout &layout,
		const SendingRowSettleTarget &target,
		const SendingRowSettleProgress &progress,
		crl::time now);
	void paintItemAmount(
		Painter &p,
		const SendingRowSettleProgress &progress,
		crl::time now);
	void paintChip(Painter &p);
	void paintDiamond(QPainter &p, QRectF canvas, crl::time now);
	void startAnimation();
	void startSettle(HistoryRowContent content, bool burst, Fn<void()> done);
	[[nodiscard]] bool glareOwed(crl::time now) const;
	void tickGlare(crl::time now);
	void releaseSettleWait();

	const not_null<Ui::RpWidget*> _layer;
	const not_null<Ui::RpWidget*> _bounds;
	const std::shared_ptr<CollectibleMedia> _media;
	HistoryRowContent _content;
	HistoryRowText _text;
	HistoryRowLayout _layout;
	HistoryRowChipState _chip;
	AmountPainter _amount;
	std::unique_ptr<Lottie::Icon> _diamond;
	std::unique_ptr<Ui::PeerUserpicView> _userpic;
	rpl::lifetime _userpicLifetime;
	Ui::BoxShadow _shadow;
	GlareCycle _glare;
	Ui::Animations::Basic _animation;
	base::unique_qptr<Ui::RpWidget> _surface;
	rpl::lifetime _chipLifetime;
	std::unique_ptr<Settle> _settle;
	std::optional<SendingRowSettleRequest> _waiting;
	crl::time _started = 0;
	crl::time _diamondStarted = 0;
	crl::time _bumpAt = 0;
	int _digitsHeight = 0;
	bool _inView = true;
	bool _diamondAway = false;
	bool _glareSeen = false;

};

[[nodiscard]] int DigitsHeight(const style::font &font) {
	return int(base::SafeRound(
		-font->metrics().tightBoundingRect(u"0123456789"_q).top()));
}

[[nodiscard]] float64 SettleRamp(crl::time t, crl::time from, crl::time till) {
	return std::clamp(float64(t - from) / (till - from), 0., 1.);
}

[[nodiscard]] int SendingRowDiamondCanvas() {
	return GramDiamondCanvas(st::walletSendingRowAmountFont);
}

[[nodiscard]] int SendingRowDiamondLeft(int width) {
	const auto canvas = SendingRowDiamondCanvas();
	const auto drawnRight = width - st::walletRowPadding.right();
	return int(base::SafeRound(drawnRight - canvas * kGramDiamondRight));
}

[[nodiscard]] float64 SendingRowAmountRight(int width) {
	const auto canvas = SendingRowDiamondCanvas();
	return SendingRowDiamondLeft(width)
		+ canvas * kGramDiamondLeft
		- st::walletSendingRowDiamondSkip;
}

[[nodiscard]] float64 SendingRowEntrance(crl::time elapsed) {
	const auto z = kSendingRowEntranceDamping;
	const auto wd = M_PI / kSendingRowEntrancePeak;
	const auto wn = wd / std::sqrt(1. - z * z);
	const auto t = float64(elapsed);
	return 1. - std::exp(-z * wn * t)
		* (std::cos(wd * t) + (z * wn / wd) * std::sin(wd * t));
}

[[nodiscard]] float64 SendingRowBump(crl::time elapsed) {
	if (elapsed < 0 || elapsed >= kSendingRowBumpDuration) {
		return 0.;
	}
	const auto t = float64(elapsed);
	if (elapsed <= kSendingRowBumpPress) {
		return std::sin(M_PI / 2. * t / kSendingRowBumpPress);
	}
	const auto u = t - kSendingRowBumpPress;
	const auto w = M_PI / kSendingRowBumpRelease;
	const auto sigma = -std::log(kSendingRowBumpRebound)
		/ kSendingRowBumpRelease;
	const auto bump = std::exp(-sigma * u)
		* (std::cos(w * u) + (sigma / w) * std::sin(w * u));
	if (elapsed <= kSendingRowBumpTaper) {
		return bump;
	}
	const auto x = (t - kSendingRowBumpTaper)
		/ (kSendingRowBumpDuration - kSendingRowBumpTaper);
	return bump * (1. - x * x * (3. - 2. * x));
}

[[nodiscard]] ClockStyle SendingRowClockStyle() {
	return {
		.size = st::walletSendingRowClockSize,
		.stroke = st::walletSendingRowClockStroke,
		.minuteHand = st::walletSendingRowClockMinuteHand,
		.hourHand = st::walletSendingRowClockHourHand,
	};
}

[[nodiscard]] Ui::StarBurstDescriptor SendingRowBurstDescriptor(
		QColor color) {
	const auto mirror = style::RightToLeft() ? -1. : 1.;
	return {
		.sides = {
			{
				.sign = -mirror,
				.count = 30,
				.angle = { -8., 18. },
				.reach = { 0.15, 0.55 },
			},
			{
				.sign = mirror,
				.count = 6,
				.angle = { -35., 5. },
				.reach = { 0.03, 0.10 },
			},
		},
		.delay = kSendingRowBurstDelay,
		.spread = kSendingRowBurstSpread,
		.lifeMin = kSendingRowBurstLifeMin,
		.lifeMax = kSendingRowBurstLifeMax,
		.fall = { 0., 0.04 },
		.startX = { 0., 0.5 },
		.startY = { -0.3, 0.5 },
		.size = { 0.008, 0.027 },
		.alpha = { 0.45, 0.95 },
		.twinkle = { 0.1, 1.9 },
		.appearTill = 0.2,
		.fadeAfter = 0.55,
		.deformation = 0.1,
		.color = color,
	};
}

struct SendingHistoryRow::Settle {
	HistoryRowContent content;
	HistoryRowText text;
	HistoryRowLayout layout;
	std::unique_ptr<Ui::StarBurst> burst;
	std::unique_ptr<Ui::PeerUserpicView> userpic;
	rpl::lifetime userpicLifetime;
	crl::time started = 0;
	base::Timer finish;
	Fn<void()> done;
	float64 scale = 1.;
	int fraction = 0;
	bool finished = false;
};

// WHY: the surface paints outside the row, so it lives in `layer`, an
// unclipped ancestor of `bounds` (the list's wrap), and follows `bounds`:
// hidden with it and cut at its bottom edge, so a collapsing list hides it.
SendingHistoryRow::SendingHistoryRow(
	not_null<Ui::RpWidget*> parent,
	not_null<Ui::RpWidget*> layer,
	not_null<Ui::RpWidget*> bounds,
	HistoryRowContent content,
	std::shared_ptr<CollectibleMedia> media)
: Ui::AbstractButton(parent.get())
, _layer(layer)
, _bounds(bounds)
, _media(std::move(media))
, _shadow(st::walletInfoIslandShadow)
, _surface(Ui::CreateChild<Ui::RpWidget>(layer.get())) {
	Expects(_media || !content.itemAmount);

	_digitsHeight = DigitsHeight(st::walletSendingRowAmountFont);
	if (!content.itemAmount) {
		const auto canvas = SendingRowDiamondCanvas();
		_diamond = Lottie::MakeIcon({
			.name = u"gram"_q,
			.sizeOverride = { canvas, canvas },
			.frame = -1,
			.limitFps = true,
		});
	}
	_animation.init([=](crl::time now) {
		if (!_inView || !isVisible() || anim::Disabled()) {
			releaseSettleWait();
			return false;
		}
		tickGlare(now);
		if (_waiting && !glareOwed(now)) {
			auto waiting = *base::take(_waiting);
			startSettle(
				std::move(waiting.content),
				waiting.burst,
				std::move(waiting.done));
		}
		_surface->update();
		return true;
	});

	_surface->setAttribute(Qt::WA_TransparentForMouseEvents);
	_surface->raise();
	_surface->show();
	_surface->paintRequest(
	) | rpl::on_next([=] {
		paintSurface();
	}, _surface->lifetime());
	rpl::combine(
		geometryValue(),
		parent->geometryValue(),
		bounds->geometryValue()
	) | rpl::on_next([=] {
		updateSurfaceGeometry();
	}, lifetime());
	rpl::combine(
		shownValue(),
		bounds->shownValue()
	) | rpl::on_next([=](bool shown, bool boundsShown) {
		_surface->setVisible(shown && boundsShown);
	}, lifetime());

	setContent(std::move(content));
}

int SendingHistoryRow::surfaceSkip() const {
	return (_settle && _settle->burst) ? st::walletSendingRowBurstOutset : 0;
}

void SendingHistoryRow::updateSurfaceGeometry() {
	const auto extend = _shadow.extend();
	const auto shift = st::walletSendingRowEntranceShift;
	const auto skip = surfaceSkip();
	auto geometry = Ui::MapFrom(_layer, this, rect()).marginsAdded({
		0,
		shift + extend.top() + skip,
		0,
		shift + extend.bottom() + skip,
	});
	const auto limit = Ui::MapFrom(_layer, _bounds, _bounds->rect());
	geometry.setHeight(std::clamp(
		limit.y() + limit.height() - geometry.y(),
		0,
		geometry.height()));
	_surface->setGeometry(geometry);
}

void SendingHistoryRow::setContent(HistoryRowContent content) {
	const auto peerChanged = !_userpic || (_content.peer != content.peer);
	const auto chipChanged = (_content.collectible != content.collectible);
	_content = std::move(content);
	_text = PrepareHistoryRowText(_content);
	if (!_content.itemAmount) {
		const auto amountNano = _content.amountNano;
		const auto &font = st::walletSendingRowAmountFont;
		_amount.setContent({ .big = font, .small = font }, {
			.whole = RowAmountWhole(
				amountNano,
				RowAmountSign(_content.incoming)),
			.fraction = GramMinorPart(amountNano),
		});
	}
	if (chipChanged) {
		_chipLifetime.destroy();
		_chip = HistoryRowChipState();
		if (_content.itemAmount) {
			TrackHistoryRowChip(
				&_chip,
				_media,
				_content.collectible,
				[=] { _surface->update(); },
				_chipLifetime);
		}
	}
	if (peerChanged) {
		_userpicLifetime.destroy();
		if (const auto peer = _content.peer) {
			_userpic = std::make_unique<Ui::PeerUserpicView>(
				peer->createUserpicView());
			peer->session().downloaderTaskFinished(
			) | rpl::on_next([=] {
				_surface->update();
			}, _userpicLifetime);
		} else {
			_userpic = nullptr;
		}
	}
	accessibilityNameChanged();
	updateRowLayout();
	_surface->update();
}

void SendingHistoryRow::awaitDiamond() {
	_diamondAway = true;
	_surface->update();
}

void SendingHistoryRow::landDiamond(
		std::unique_ptr<Lottie::Icon> icon,
		crl::time loopStarted) {
	if (icon) {
		_diamond = std::move(icon);
		_diamondStarted = loopStarted;
	}
	_diamondAway = false;
	_bumpAt = (anim::Disabled() || _settle) ? 0 : crl::now();
	startAnimation();
	_surface->update();
}

void SendingHistoryRow::cancelDiamondAwait() {
	_diamondAway = false;
	_surface->update();
}

void SendingHistoryRow::scheduleBump(crl::time at) {
	_bumpAt = anim::Disabled() ? 0 : at;
	startAnimation();
}

void SendingHistoryRow::settle(
		HistoryRowContent content,
		bool burst,
		Fn<void()> done) {
	if (!_settle && (_waiting || glareOwed(crl::now()))) {
		_waiting = SendingRowSettleRequest{
			.content = std::move(content),
			.done = std::move(done),
			.burst = burst,
		};
		startAnimation();
		return;
	}
	startSettle(std::move(content), burst, std::move(done));
}

void SendingHistoryRow::startSettle(
		HistoryRowContent content,
		bool burst,
		Fn<void()> done) {
	if (!_settle) {
		_settle = std::make_unique<Settle>();
		_settle->finish.setCallback([=] {
			_settle->finished = true;
			crl::on_main(this, [=] {
				if (const auto done = _settle ? _settle->done : nullptr) {
					done();
				}
			});
		});
		if (burst && !anim::Disabled()) {
			const auto scope = WindowPaletteScope(this);
			_settle->burst = Ui::StarBurst::Make(
				SendingRowBurstDescriptor(st::windowActiveTextFg->c));
		}
		_settle->finish.callOnce(settleDuration() + kSendingRowSettlePaintWait);
		updateSurfaceGeometry();
	}
	const auto peer = content.peer;
	if (!peer || peer == _content.peer) {
		_settle->userpicLifetime.destroy();
		_settle->userpic = nullptr;
	} else if (!_settle->userpic || _settle->content.peer != peer) {
		_settle->userpicLifetime.destroy();
		_settle->userpic = std::make_unique<Ui::PeerUserpicView>(
			peer->createUserpicView());
		peer->session().downloaderTaskFinished(
		) | rpl::on_next([=] {
			_surface->update();
		}, _settle->userpicLifetime);
	}
	_settle->done = std::move(done);
	_settle->content = std::move(content);
	const auto &entry = _settle->content;
	_settle->text = PrepareHistoryRowText(entry, [=] {
		_surface->update();
	});
	const auto &font = st::walletRowAmountMinorLabel.style.font;
	_settle->fraction = font->width(GramMinorPart(entry.amountNano));
	_settle->scale = DigitsHeight(st::walletRowAmountMajorLabel.style.font)
		/ float64(_digitsHeight);
	accessibilityNameChanged();
	updateRowLayout();
	startAnimation();
	_surface->update();
}

bool SendingHistoryRow::glareOwed(crl::time now) const {
	return _started
		&& !anim::Disabled()
		&& _inView
		&& isVisible()
		&& (!_glareSeen || _glare.progress(now).has_value());
}

// WHY: a pass counts only when this run of the frame callback, which stops
// out of view, in a hidden window and with animations off, saw it whole after
// the first paint; none is born under a settle, so no pass is cut by one.
void SendingHistoryRow::tickGlare(crl::time now) {
	if (_settle) {
		return;
	}
	if (_started
		&& (_glare.birth > _started)
		&& (_glare.birth >= _animation.started())
		&& (now > _glare.death)) {
		_glareSeen = true;
	}
	_glare.tick(now, kSendingRowGlareDuration, kSendingRowGlarePause);
}

// The list decides again, as when a result finds the row in this state.
void SendingHistoryRow::releaseSettleWait() {
	if (auto waiting = base::take(_waiting)) {
		crl::on_main(this, [done = std::move(waiting->done)] {
			if (done) {
				done();
			}
		});
	}
}

bool SendingHistoryRow::surfaceShown() const {
	return _surface && !_surface->isHidden();
}

bool SendingHistoryRow::inView() const {
	return _inView;
}

bool SendingHistoryRow::settling() const {
	return (_settle && !_settle->finished) || _waiting.has_value();
}

bool SendingHistoryRow::settled() const {
	return _settle && _settle->finished;
}

QRectF SendingHistoryRow::diamondTarget(crl::time now) const {
	return surfaceTransform(now).mapRect(diamondCanvas(
		layout(),
		settleTarget(),
		settleProgress(now).elapsed));
}

int SendingHistoryRow::resizeGetHeight(int newWidth) {
	_amount.setAvailableWidth(int(SendingRowAmountRight(newWidth))
		- st::walletRowPadding.left()
		- st::walletSendingRowTextMinWidth);
	_layout = MeasureHistoryRow(_text, newWidth);
	if (_settle) {
		_settle->layout = MeasureHistoryRow(_settle->text, newWidth);
	}
	return rowLayout().height;
}

const HistoryRowLayout &SendingHistoryRow::rowLayout() const {
	return _settle ? _settle->layout : _layout;
}

void SendingHistoryRow::updateRowLayout() {
	if (const auto w = width()) {
		resizeToWidth(w);
	}
}

void SendingHistoryRow::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	const auto inView = (visibleBottom > visibleTop);
	if (_inView == inView) {
		return;
	}
	_inView = inView;
	if (_inView && !_animation.animating()) {
		_surface->update();
	}
}

QString SendingHistoryRow::accessibilityName() {
	const auto &amount = _amount.parts();
	const auto &content = _settle ? _settle->content : _content;
	auto parts = QStringList();
	for (const auto &text : {
		content.title,
		content.subtitle,
		content.date,
		(_content.itemAmount
			? (_settle ? _settle->text : _text).major.toString()
			: _settle
			? (_settle->text.major.toString()
				+ GramMinorPart(content.amountNano))
			: QString(amount.whole + amount.fraction)),
	}) {
		if (!text.isEmpty()) {
			parts.push_back(text);
		}
	}
	return parts.join(u", "_q);
}

SendingRowLayout SendingHistoryRow::layout() const {
	const auto w = width();
	const auto h = height();
	const auto rtl = style::RightToLeft();
	const auto mirror = [&](float64 left, float64 width) {
		return rtl ? (w - left - width) : left;
	};
	const auto &padding = st::walletRowPadding;
	const auto outset = st::walletSendingRowOutset;
	const auto pillLeft = st::walletRowIconLeft - outset;
	const auto pillRight = w - padding.right() + outset;
	auto result = SendingRowLayout();
	result.pill = QRect(
		int(mirror(pillLeft, pillRight - pillLeft)),
		0,
		pillRight - pillLeft,
		h);
	result.radius = PillRadius(result.pill);

	const auto size = st::walletRowIconSize;
	const auto center = rowLayout().avatarCenter;
	result.avatar = QRect(
		int(mirror(st::walletRowIconLeft, size)),
		center - size / 2,
		size,
		size);
	const auto shift = st::walletSendingRowClockShift;
	result.badge = QRectF(result.avatar).center()
		+ QPointF(rtl ? -shift : shift, shift);
	if (_content.itemAmount) {
		result.titleWidth = _layout.titleWidth;
		result.textWidth = _layout.textWidth;
		return result;
	}

	const auto canvas = SendingRowDiamondCanvas();
	const auto drawnLeft = SendingRowDiamondLeft(w)
		+ canvas * kGramDiamondLeft;
	const auto drawnWidth = canvas * (kGramDiamondRight - kGramDiamondLeft);
	result.diamond = QPoint(
		int(base::SafeRound(mirror(drawnLeft, drawnWidth)
			- canvas * kGramDiamondLeft)),
		int(base::SafeRound(h / 2.
			- canvas * (kGramDiamondTop + kGramDiamondBottom) / 2.)));

	const auto amountWidth = _amount.size().width();
	const auto amountLeft = SendingRowAmountRight(w) - amountWidth;
	const auto digitsCenter = (_amount.baseline() - _digitsHeight / 2.)
		* _amount.scale();
	result.amount = QPointF(
		mirror(amountLeft, amountWidth),
		h / 2. - digitsCenter);
	result.textWidth = std::max(
		int(amountLeft) - st::walletRowSkip - padding.left(),
		0);
	result.titleWidth = result.textWidth;
	return result;
}

SendingRowSettleTarget SendingHistoryRow::settleTarget() const {
	auto result = SendingRowSettleTarget();
	if (!_settle) {
		return result;
	}
	const auto place = PlaceRowAmount(
		_settle->text,
		_settle->layout.amountTop,
		width());
	result.major = place.major;
	result.minor = place.minor;
	result.boundary = result.minor.x() + _settle->fraction;
	const auto &font = st::walletRowAmountMinorLabel.style.font;
	result.anchor = QPointF(result.boundary, result.minor.y() + font->ascent);
	const auto emoji = st::emojiSize;
	const auto skip = (emoji - Ui::Text::AdjustCustomEmojiSize(emoji)) / 2;
	const auto &margin = st::walletRowIconMargin;
	const auto size = float64(st::walletRowMarkSize);
	const auto image = QPointF(
		result.boundary + margin.left() + skip,
		result.minor.y() + (font->height - emoji) / 2 + skip + margin.top());
	result.diamond = QRectF(
		image.x() + size * kRowEmojiDiamondLeft,
		image.y() + size * kRowEmojiDiamondTop,
		size * (kRowEmojiDiamondRight - kRowEmojiDiamondLeft),
		size * (kRowEmojiDiamondBottom - kRowEmojiDiamondTop));
	result.textWidth = _settle->layout.textWidth;
	result.titleWidth = _settle->layout.titleWidth;
	return result;
}

crl::time SendingHistoryRow::settleDuration() const {
	return (_settle && _settle->burst)
		? _settle->burst->duration()
		: kSendingRowSettleDiamond;
}

crl::time SendingHistoryRow::settleElapsed(crl::time now) const {
	return !_settle
		? crl::time(0)
		: _settle->finished
		? settleDuration()
		: _settle->started
		? (now - _settle->started)
		: crl::time(0);
}

SendingRowSettleProgress SendingHistoryRow::settleProgress(
		crl::time now) const {
	auto result = SendingRowSettleProgress();
	if (!_settle) {
		return result;
	}
	const auto t = anim::Disabled()
		? kSendingRowSettleDiamond
		: settleElapsed(now);
	const auto out = [](float64 x) {
		return anim::easeOutCubic(1., x);
	};
	result.elapsed = t;
	result.pill = out(SettleRamp(t, 0, kSendingRowSettlePill));
	result.badge = 1. - SettleRamp(
		t,
		kSendingRowSettleBadgeFrom,
		kSendingRowSettleBadgeTill);
	result.label = SettleRamp(
		t,
		kSendingRowSettleLabelFrom,
		kSendingRowSettleLabelTill);
	result.amount = out(SettleRamp(t, 0, kSendingRowSettleAmount));
	result.amountFade = SettleRamp(
		t,
		kSendingRowSettleAmountFadeFrom,
		kSendingRowSettleAmount);
	result.emoji = SettleRamp(
		t,
		kSendingRowSettleDiamondFadeFrom,
		kSendingRowSettleDiamond);
	return result;
}

QRectF SendingHistoryRow::diamondCanvas(
		const SendingRowLayout &layout,
		const SendingRowSettleTarget &target,
		crl::time elapsed) const {
	const auto canvas = float64(SendingRowDiamondCanvas());
	const auto middle = QPointF(
		(kGramDiamondLeft + kGramDiamondRight) / 2.,
		(kGramDiamondTop + kGramDiamondBottom) / 2.);
	auto centre = QPointF(layout.diamond) + middle * canvas;
	auto scale = 1.;
	if (_settle) {
		const auto t = elapsed;
		const auto swell = kSendingRowSettleDiamondSwell;
		centre += (target.diamond.center() - centre) * anim::easeOutCubic(
			1.,
			SettleRamp(t, 0, kSendingRowSettleDiamondMove));
		if (t < kSendingRowSettleDiamondSwellTill) {
			scale = 1. + (swell - 1.) * anim::easeOutCubic(
				1.,
				SettleRamp(t, 0, kSendingRowSettleDiamondSwellTill));
		} else {
			const auto x = SettleRamp(
				t,
				kSendingRowSettleDiamondSwellTill,
				kSendingRowSettleDiamond);
			const auto end = target.diamond.width()
				/ (canvas * (kGramDiamondRight - kGramDiamondLeft));
			scale = swell + (end - swell) * x * x * (3. - 2. * x);
		}
	}
	const auto side = canvas * scale;
	return QRectF(centre - middle * side, QSizeF(side, side));
}

QTransform SendingHistoryRow::surfaceTransform(crl::time now) const {
	auto result = QTransform();
	if (anim::Disabled()) {
		return result;
	}
	const auto pill = QRectF(layout().pill);
	const auto center = pill.center();
	const auto fade = 1. - settleProgress(now).pill;
	const auto elapsed = _started ? (now - _started) : crl::time(0);
	if (elapsed < kSendingRowEntranceDuration) {
		const auto progress = 1. - (1. - SendingRowEntrance(elapsed)) * fade;
		const auto scale = kSendingRowEntranceScale
			+ (1. - kSendingRowEntranceScale) * progress;
		const auto offset = st::walletSendingRowEntranceShift
			* (1. - progress);
		result.translate(center.x(), center.y() + offset);
		result.scale(scale, scale);
		result.translate(-center.x(), -center.y());
	}
	const auto bump = (_bumpAt ? SendingRowBump(now - _bumpAt) : 0.) * fade;
	if (bump != 0.) {
		const auto scale = 1. - kSendingRowBumpShrink * bump;
		const auto drop = (kSendingRowBumpDrop - kSendingRowBumpShrink / 2.)
			* pill.height()
			* bump;
		result.translate(center.x(), center.y() + drop);
		result.scale(scale, scale);
		result.translate(-center.x(), -center.y());
	}
	return result;
}

void SendingHistoryRow::paintSurface() {
	auto p = Painter(_surface.get());
	const auto now = crl::now();
	if (!_started) {
		_started = now;
		_glare.death = now + kSendingRowEntranceDuration;
	}
	if (!_diamondStarted) {
		_diamondStarted = _started;
	}
	if (_settle && !_settle->started && !_settle->finished) {
		_settle->started = now;
		_settle->finish.callOnce(settleDuration());
	}
	startAnimation();
	auto hq = PainterHighQualityEnabler(p);
	const auto layout = this->layout();
	const auto target = settleTarget();
	const auto progress = settleProgress(now);
	p.translate(
		0,
		(st::walletSendingRowEntranceShift
			+ _shadow.extend().top()
			+ surfaceSkip()));
	p.setTransform(surfaceTransform(now), true);
	const auto disabled = anim::Disabled();
	const auto elapsed = now - _started;

	const auto fade = 1. - progress.pill;
	const auto inset = int(base::SafeRound(
		st::walletSendingRowOutset * progress.pill));
	const auto pill = layout.pill.marginsRemoved({ inset, 0, inset, 0 });
	const auto radius = PillRadius(pill);
	const auto accent = st::windowActiveTextFg->c;
	p.setOpacity(fade);
	if (fade > 0.) {
		Dialogs::PaintPillBackground(p, _shadow, pill, radius);
	}
	if (const auto glare = (disabled || fade <= 0.)
			? std::optional<float64>()
			: _glare.progress(now)) {
		const auto stroke = float64(st::walletSendingRowGlareStroke);
		const auto half = stroke / 2.;
		const auto pillWidth = float64(pill.width());
		const auto pillHeight = float64(pill.height());
		const auto slope = kSendingRowGlareLag * pillWidth / pillHeight;
		PaintGlare(
			p,
			QRectF(pill).marginsAdded({ half, half, half, half }),
			radius + half,
			ComputeGlareBand(
				*glare,
				pillWidth + slope * pillHeight,
				st::walletSendingRowGlareWidth),
			{
				.stroke = stroke,
				.slope = slope,
				.border = kSendingRowGlareBorder,
				.background = kSendingRowGlareBackground,
			},
			accent);
	}
	p.setOpacity(1.);
	paintAvatar(
		p,
		layout,
		(st::walletSendingRowClockSize / 2. + st::walletSendingRowClockCutout)
			* progress.badge,
		progress.label);
	if (progress.badge > 0.) {
		p.setOpacity(progress.badge);
		PaintClock(
			p,
			SendingRowClockStyle(),
			layout.badge,
			SendingClockPose(disabled ? 0 : elapsed),
			accent);
		p.setOpacity(1.);
	}
	paintTexts(p, layout, target, progress);
	if (_content.itemAmount) {
		paintItemAmount(p, progress, now);
		paintChip(p);
		return;
	}
	if (_settle) {
		paintSettleAmount(p, layout, target, progress, now);
	} else {
		_amount.paint(p, layout.amount, { .digits = st::windowBoldFg->c });
	}
	if (const auto burst = (_settle && !disabled)
			? _settle->burst.get()
			: nullptr) {
		const auto canvas = SendingRowDiamondCanvas();
		burst->paint(p, {
			.origin = target.diamond.center(),
			.emitter = canvas * (kGramDiamondRight - kGramDiamondLeft),
			.extent = float64(width()),
			.elapsed = progress.elapsed,
		});
	}
	if (progress.emoji < 1.) {
		p.setOpacity(1. - progress.emoji);
		paintDiamond(
			p,
			diamondCanvas(layout, target, progress.elapsed),
			now);
		p.setOpacity(1.);
	}
}

void SendingHistoryRow::paintAvatar(
		Painter &p,
		const SendingRowLayout &layout,
		float64 cutout,
		float64 swap) {
	p.save();
	if (cutout > 0.) {
		auto clip = QPainterPath();
		clip.addRect(QRectF(layout.avatar));
		auto cut = QPainterPath();
		cut.addEllipse(layout.badge, cutout, cutout);
		p.setClipPath(clip.subtracted(cut));
	}
	const auto paint = [&](
			const HistoryRowContent &content,
			Ui::PeerUserpicView *userpic) {
		if (content.peer && userpic) {
			content.peer->paintUserpic(
				p,
				*userpic,
				layout.avatar.x(),
				layout.avatar.y(),
				layout.avatar.width());
		} else {
			PaintRowAvatar(p, layout.avatar, content.avatar);
		}
	};
	const auto next = _settle ? &_settle->content : nullptr;
	const auto same = !next
		|| (next->peer
			? (next->peer == _content.peer)
			: (!_content.peer && next->avatar == _content.avatar));
	if (same) {
		paint(_content, _userpic.get());
	} else {
		if (swap < 1.) {
			paint(_content, _userpic.get());
		}
		if (swap > 0.) {
			p.setOpacity(swap);
			paint(*next, _settle->userpic.get());
		}
	}
	p.restore();
}

void SendingHistoryRow::paintTexts(
		Painter &p,
		const SendingRowLayout &layout,
		const SendingRowSettleTarget &target,
		const SendingRowSettleProgress &progress) {
	const auto left = st::walletRowPadding.left();
	const auto draw = [&](
			const Ui::Text::String &text,
			const style::FlatLabel &st,
			HistoryRowLine line,
			bool breakEverywhere,
			int available,
			float64 opacity) {
		if (opacity <= 0. || !line.lines) {
			return;
		}
		p.setOpacity(opacity);
		p.setPen(st.textFg);
		text.draw(p, {
			.position = { left, line.top },
			.outerWidth = width(),
			.availableWidth = available,
			.elisionLines = line.lines,
			.elisionBreakEverywhere = breakEverywhere,
		});
	};
	const auto settle = _settle.get();
	const auto line = [&](
			const Ui::Text::String &text,
			HistoryRowLine textLine,
			const Ui::Text::String *next,
			HistoryRowLine nextLine,
			bool same,
			const style::FlatLabel &st,
			bool breakEverywhere,
			int available) {
		if (!next) {
			draw(text, st, textLine, breakEverywhere, available, 1.);
		} else if (same) {
			draw(*next, st, nextLine, breakEverywhere, available, 1.);
		} else {
			draw(
				text,
				st,
				textLine,
				breakEverywhere,
				available,
				1. - progress.label);
			draw(
				*next,
				st,
				nextLine,
				breakEverywhere,
				available,
				progress.label);
		}
	};
	const auto titleWidth = settle
		? int(base::SafeRound(layout.titleWidth
			+ (target.titleWidth - layout.titleWidth) * progress.amount))
		: layout.titleWidth;
	const auto textWidth = settle
		? int(base::SafeRound(layout.textWidth
			+ (target.textWidth - layout.textWidth) * progress.amount))
		: layout.textWidth;
	line(
		_text.title,
		_layout.title,
		settle ? &settle->text.title : nullptr,
		settle ? settle->layout.title : HistoryRowLine(),
		settle && (settle->content.title == _content.title),
		st::walletRowTitleLabel,
		true,
		titleWidth);
	line(
		_text.subtitle,
		_layout.subtitle,
		settle ? &settle->text.subtitle : nullptr,
		settle ? settle->layout.subtitle : HistoryRowLine(),
		settle && (settle->content.subtitle == _content.subtitle),
		st::walletRowSubtitleLabel,
		true,
		textWidth);
	line(
		_text.date,
		_layout.date,
		settle ? &settle->text.date : nullptr,
		settle ? settle->layout.date : HistoryRowLine(),
		settle && (settle->content.date == _content.date),
		st::walletRowDateLabel,
		false,
		textWidth);
	p.setOpacity(1.);
}

void SendingHistoryRow::paintSettleAmount(
		Painter &p,
		const SendingRowLayout &layout,
		const SendingRowSettleTarget &target,
		const SendingRowSettleProgress &progress,
		crl::time now) {
	const auto &content = _settle->content;
	const auto &color = RowAmountColor(
		content.incoming,
		content.pending,
		content.failed);
	if (progress.amountFade < 1.) {
		const auto from = _amount.scale();
		const auto natural = QPointF(
			_amount.naturalWidth(),
			_amount.baseline());
		const auto start = layout.amount + natural * from;
		const auto anchor = start + (target.anchor - start) * progress.amount;
		const auto scale = from + (_settle->scale - from) * progress.amount;
		p.save();
		p.setOpacity(1. - progress.amountFade);
		p.translate(anchor);
		p.scale(scale, scale);
		p.translate(-natural);
		_amount.paint(p, {
			.digits = anim::color(st::windowBoldFg, color, progress.amount),
		});
		p.restore();
	}
	const auto draw = [&](
			const Ui::Text::String &text,
			QPoint position,
			const style::FlatLabel &st,
			QRect clip,
			float64 opacity) {
		if (opacity <= 0.) {
			return;
		}
		p.save();
		p.setOpacity(opacity);
		p.setClipRect(clip, Qt::IntersectClip);
		p.setPen(color);
		text.draw(p, {
			.position = position,
			.availableWidth = text.maxWidth(),
			.palette = &st.palette,
			.now = now,
		});
		p.restore();
	};
	const auto h = height();
	draw(
		_settle->text.major,
		target.major,
		st::walletRowAmountMajorLabel,
		rect(),
		progress.amountFade);
	draw(
		_settle->text.minor,
		target.minor,
		st::walletRowAmountMinorLabel,
		QRect(0, 0, target.boundary, h),
		progress.amountFade);
	draw(
		_settle->text.minor,
		target.minor,
		st::walletRowAmountMinorLabel,
		QRect(target.boundary, 0, width() - target.boundary, h),
		progress.emoji);
}

void SendingHistoryRow::paintItemAmount(
		Painter &p,
		const SendingRowSettleProgress &progress,
		crl::time now) {
	const auto &text = _settle ? _settle->text : _text;
	const auto &next = _settle ? _settle->content : _content;
	const auto place = PlaceRowAmount(text, rowLayout().amountTop, width());
	p.setPen(anim::color(
		RowAmountColor(_content.incoming, false, _content.failed),
		RowAmountColor(next.incoming, false, next.failed),
		progress.label));
	const auto draw = [&](
			const Ui::Text::String &string,
			QPoint position,
			const style::FlatLabel &st) {
		string.draw(p, {
			.position = position,
			.availableWidth = string.maxWidth(),
			.palette = &st.palette,
			.now = now,
		});
	};
	draw(text.major, place.major, st::walletRowAmountMajorLabel);
	draw(text.minor, place.minor, st::walletRowAmountMinorLabel);
}

void SendingHistoryRow::paintChip(Painter &p) {
	const auto &padding = st::walletRowPadding;
	const auto outer = std::max(width() - padding.left() - padding.right(), 0);
	const auto origin = QPoint(
		style::RightToLeft() ? padding.right() : padding.left(),
		rowLayout().chipTop);
	p.translate(origin);
	PaintHistoryRowChipPlate(p, outer, _chip);
	PaintHistoryRowChipArtwork(p, outer, _chip, _media, _content.collectible);
	PaintHistoryRowChipText(p, outer, _chip);
	p.translate(-origin);
}

void SendingHistoryRow::paintDiamond(
		QPainter &p,
		QRectF canvas,
		crl::time now) {
	if (_diamondAway || !_diamond || !_diamond->valid()) {
		return;
	}
	AdvanceSendingDiamond(_diamond.get(), _diamondStarted, now);
	const auto size = SendingRowDiamondCanvas();
	const auto fade = 1. - settleProgress(now).pill;
	const auto bump = (_bumpAt && !anim::Disabled())
		? (SendingRowBump(now - _bumpAt) * fade)
		: 0.;
	const auto swell = 1. + kSendingRowBumpSwell * std::max(bump, 0.);
	const auto centre = canvas.topLeft() + QPointF(
		canvas.width() * (kGramDiamondLeft + kGramDiamondRight) / 2.,
		canvas.height() * (kGramDiamondTop + kGramDiamondBottom) / 2.);
	const auto target = QRectF(
		centre + (canvas.topLeft() - centre) * swell,
		canvas.size() * swell);
	p.drawImage(target, _diamond->frame(QSize(size, size), nullptr).image);
}

void SendingHistoryRow::startAnimation() {
	if (anim::Disabled() || _animation.animating()) {
		return;
	}
	_animation.start();
}

not_null<SendingHistoryRow*> AddSendingHistoryRow(
		not_null<Ui::VerticalLayout*> slot,
		not_null<Ui::RpWidget*> layer,
		not_null<Ui::RpWidget*> bounds,
		const HistoryRowContent &content,
		std::shared_ptr<CollectibleMedia> media,
		Fn<void()> clicked) {
	const auto result = slot->add(object_ptr<SendingHistoryRow>(
		slot,
		layer,
		bounds,
		content,
		std::move(media)));
	result->setClickedCallback(std::move(clicked));
	return result;
}

[[nodiscard]] bool ShowsCollectible(const TransferItem &item) {
	return (item.kind == TransferItem::Kind::Collectible)
		&& !item.collectible.isEmpty();
}

[[nodiscard]] QString RowStatusSubtitle(TransferItem::Status status) {
	using Status = TransferItem::Status;
	switch (status) {
	case Status::Pending:
		return tr::lng_channel_earn_history_pending(tr::now);
	case Status::Failure:
		return tr::lng_channel_earn_history_failed(tr::now);
	case Status::Success:
		return QString();
	}
	Unexpected("Status in RowStatusSubtitle.");
}

[[nodiscard]] HistoryRowContent RowContentFromItem(
		const TransferItem &item,
		not_null<Main::Session*> session) {
	using Kind = TransferItem::Kind;
	const auto date = item.date
		? langDateTime(base::unixtime::parse(*item.date))
		: QString();
	if (ShowsCollectible(item)) {
		const auto peer = item.counterpartyPeer
			? session->data().peerLoaded(PeerId(item.counterpartyPeer))
			: nullptr;
		const auto hasCounterparty = !item.counterparty.isEmpty();
		const auto domain = hasCounterparty
			? item.counterpartyName.trimmed()
			: QString();
		const auto kindText = item.incoming
			? tr::lng_wallet_row_collectible_in(tr::now)
			: tr::lng_wallet_row_collectible_out(tr::now);
		const auto titleIsKind = !peer && !hasCounterparty;
		const auto statusText = RowStatusSubtitle(item.status);
		return {
			.title = (peer
				? peer->name()
				: !domain.isEmpty()
				? domain
				: hasCounterparty
				? ShortAddressForm(CounterpartyAddress(item))
				: kindText),
			.subtitle = (!statusText.isEmpty()
				? statusText
				: titleIsKind
				? QString()
				: kindText),
			.date = date,
			.incoming = item.incoming,
			.failed = (item.status == TransferItem::Status::Failure),
			.avatar = (peer
				? RowAvatar::Peer
				: item.incoming
				? RowAvatar::In
				: RowAvatar::Out),
			.peer = peer,
			.itemAmount = true,
			.collectible = item.collectible,
		};
	}
	const auto pending
		= (item.status == TransferItem::Status::Pending);
	const auto failed
		= (item.status == TransferItem::Status::Failure);
	const auto statusText = RowStatusSubtitle(item.status);
	const auto transfer = (item.kind == Kind::Transfer)
		|| (item.kind == Kind::PeerTransfer)
		|| (item.kind == Kind::Onramp);
	const auto address = transfer
		? CounterpartyAddress(item)
		: QString();
	const auto domain = !address.isEmpty()
		? item.counterpartyName.trimmed()
		: QString();
	const auto provider = !address.isEmpty()
		? OnrampProvider(item)
		: QString();
	if (!provider.isEmpty()) {
		return {
			.title = provider,
			.subtitle = (!statusText.isEmpty()
				? statusText
				: item.incoming
				? tr::lng_wallet_row_topup(tr::now)
				: tr::lng_wallet_row_withdrawal(tr::now)),
			.date = date,
			.amountNano = item.amountNano,
			.incoming = item.incoming,
			.pending = pending,
			.failed = failed,
			.avatar = (item.incoming ? RowAvatar::In : RowAvatar::Out),
		};
	}
	if (item.kind == Kind::PeerTransfer && item.counterpartyPeer) {
		const auto peer = session->data().peerLoaded(
			PeerId(item.counterpartyPeer));
		if (peer) {
			return {
				.title = peer->name(),
				.subtitle = (!statusText.isEmpty()
					? statusText
					: !domain.isEmpty()
					? domain
					: item.incoming
					? tr::lng_wallet_row_incoming(tr::now)
					: tr::lng_wallet_row_outgoing(tr::now)),
				.date = date,
				.amountNano = item.amountNano,
				.incoming = item.incoming,
				.pending = pending,
				.failed = failed,
				.avatar = RowAvatar::Peer,
				.peer = peer,
			};
		}
	}
	if (item.kind == Kind::KeyChange) {
		// A zero served amount leaves the paid fee as the wallet's outflow.
		const auto outflow = (!item.amountNano
			&& item.feeNano
			&& *item.feeNano > 0)
			? *item.feeNano
			: item.amountNano;
		return {
			.title = tr::lng_wallet_row_key_change(tr::now),
			.subtitle = statusText,
			.date = date,
			.amountNano = outflow,
			.incoming = item.incoming,
			.pending = pending,
			.failed = failed,
			.avatar = RowAvatar::KeyChange,
		};
	}
	const auto collectible = (item.kind == Kind::Collectible);
	const auto hasCounterparty = transfer
		? !address.isEmpty()
		: !item.counterparty.isEmpty();
	const auto kindText = collectible
		? (item.incoming
			? tr::lng_wallet_row_collectible_in(tr::now)
			: tr::lng_wallet_row_collectible_out(tr::now))
		: item.incoming
		? tr::lng_wallet_row_deposit(tr::now)
		: tr::lng_wallet_row_withdrawal(tr::now);
	return {
		.title = (hasCounterparty
			? (!domain.isEmpty()
				? domain
				: ShortAddressForm(transfer
					? address
					: CounterpartyAddress(item)))
			: kindText),
		.subtitle = (!statusText.isEmpty()
			? statusText
			: hasCounterparty
			? kindText
			: QString()),
		.date = date,
		.amountNano = item.amountNano,
		.incoming = item.incoming,
		.pending = pending,
		.failed = failed,
		.avatar = (item.incoming ? RowAvatar::In : RowAvatar::Out),
	};
}

// WHY: it plays once when the box finishes showing and a click replays it
// once it has stopped, with no pointer cursor or anything else saying so,
// because finding that out is the whole of it.
void AddWalletLottie(
		not_null<Ui::GenericBox*> box,
		const style::margins &margin = style::margins(),
		Ui::VerticalLayout *container = nullptr) {
	const auto into = container ? container : box->verticalLayout().get();
	const auto size = st::walletDetailsLottieSize;
	auto icon = Settings::CreateLottieIcon(
		into,
		{
			.name = u"gram"_q,
			.sizeOverride = { size, size },
		},
		margin);
	const auto raw = icon.widget.data();
	const auto animate = icon.animate;
	const auto animating = icon.animating;
	into->add(std::move(icon.widget));
	const auto replay = Ui::CreateChild<Ui::AbstractButton>(raw);
	replay->setPointerCursor(false);
	replay->setClickedCallback([=] {
		if (!animating()) {
			animate(anim::repeat::once);
		}
	});
	// The rect the icon paints into: centered in the row, under the top margin.
	raw->sizeValue() | rpl::on_next([=](QSize outer) {
		replay->setGeometry(
			(outer.width() - size) / 2,
			margin.top(),
			size,
			size);
	}, replay->lifetime());
	box->showFinishes() | rpl::on_next([=] {
		animate(anim::repeat::once);
	}, raw->lifetime());
}

void AddDetailsAmountHeader(
		not_null<Ui::VerticalLayout*> layout,
		const TransferItem &item,
		int topSkip,
		int bottomSkip,
		rpl::producer<FiatRate> rate = nullptr) {
	const auto container = layout->add(
		object_ptr<Ui::RpWidget>(layout),
		style::margins(0, topSkip, 0, bottomSkip),
		style::al_top);
	auto formatted = Ui::FormatTonAmount(item.amountNano);
	const auto negativeSign = QString(QLocale::system().negativeSign());
	if (item.amountNano < 0 && formatted.wholeString.startsWith(negativeSign)) {
		formatted.wholeString.remove(0, negativeSign.size());
	}
	const auto major = Ui::CreateChild<Ui::FlatLabel>(
		container,
		(!item.amountNano
			? QString()
			: item.incoming
			? u"+"_q
			: QString(kMinus)) + formatted.wholeString,
		st::walletDetailsAmountMajorLabel);
	const auto minor = Ui::CreateChild<Ui::FlatLabel>(
		container,
		formatted.nanoString.isEmpty()
			? QString()
			: (formatted.separator + formatted.nanoString),
		st::walletDetailsAmountMinorLabel);
	// The unit is spelled out instead of drawn as the currency mark: the
	// mark is the animation above the amount now, and saying it twice in
	// one header only makes the line harder to read.
	const auto ticker = Ui::CreateChild<Ui::FlatLabel>(
		container,
		tr::lng_action_gram_transfer_ticker(
			tr::now,
			lt_count,
			std::abs(item.amountNano / float64(Ui::kNanosInOne))).toUpper(),
		st::walletDetailsTickerLabel);
	const auto subdued = (item.status == TransferItem::Status::Pending)
		|| (item.status == TransferItem::Status::Failure);
	const auto &color = subdued
		? st::windowSubTextFg
		: item.incoming
		? st::boxTextFgGood
		: st::windowFg;
	rpl::single(rpl::empty) | rpl::then(
		style::PaletteChanged()
	) | rpl::on_next([=] {
		const auto scope = WindowPaletteScope(container);
		major->setTextColorOverride(color->c);
		minor->setTextColorOverride(color->c);
	}, container->lifetime());
	const auto amountNano = item.amountNano;
	const auto fiat = rate
		? Ui::CreateChild<Ui::FlatLabel>(
			container,
			st::walletDetailsFiatLabel)
		: nullptr;
	const auto relayout = [=] {
		const auto majorSize = major->size();
		const auto minorSize = minor->size();
		const auto tickerSize = ticker->size();
		const auto tickerSkip = st::walletDetailsTickerSkip;
		const auto amountWidth = majorSize.width()
			+ minorSize.width()
			+ tickerSkip
			+ tickerSize.width();
		// The smaller labels are dropped by the difference in font size so
		// that all three sit on one baseline with the whole amount.
		const auto minorSkip = st::walletDetailsAmountMinorSkip;
		const auto amountHeight = std::max({
			majorSize.height(),
			minorSkip + minorSize.height(),
			minorSkip + tickerSize.height(),
		});
		const auto withFiat = (fiat != nullptr);
		const auto width = std::max(
			amountWidth,
			withFiat ? fiat->width() : 0);
		const auto height = amountHeight + (withFiat
			? st::walletDetailsFiatSkip + fiat->height()
			: 0);
		container->resize(width, height);
		container->setNaturalWidth(width);
		const auto left = (width - amountWidth) / 2;
		major->moveToLeft(left, 0, width);
		minor->moveToLeft(left + majorSize.width(), minorSkip, width);
		ticker->moveToLeft(
			left + majorSize.width() + minorSize.width() + tickerSkip,
			minorSkip,
			width);
		if (withFiat) {
			fiat->moveToLeft(
				(width - fiat->width()) / 2,
				amountHeight + st::walletDetailsFiatSkip,
				width);
		}
	};
	if (fiat) {
		std::move(rate) | rpl::on_next([=](const FiatRate &value) {
			fiat->setText(FormatFiat(amountNano, value));
			relayout();
		}, fiat->lifetime());
	}
	rpl::combine(
		major->sizeValue(),
		minor->sizeValue(),
		ticker->sizeValue()
	) | rpl::on_next(relayout, container->lifetime());
}

void AddDetailsCollectibleHeader(
		not_null<Ui::VerticalLayout*> layout,
		not_null<Main::Session*> session,
		std::shared_ptr<CollectibleMedia> media,
		const TransferItem &item,
		int bottomSkip,
		bool withCollection = true) {
	const auto container = layout->add(
		object_ptr<Ui::RpWidget>(layout),
		style::margins(0, st::walletDetailsAmountTopSkip, 0, bottomSkip),
		style::al_top);
	const auto address = item.collectible;
	const auto available = st::boxWideWidth
		- st::giveawayGiftCodeTableMargin.left()
		- st::giveawayGiftCodeTableMargin.right();
	const auto arrowWidth = st::walletDetailsCollectionArrowSkip
		+ st::walletDetailsCollectionArrow.width();
	const auto artwork = Ui::CreateChild<Ui::RpWidget>(container);
	artwork->resize(
		st::walletDetailsCollectibleSize,
		st::walletDetailsCollectibleSize);
	artwork->setAttribute(Qt::WA_TransparentForMouseEvents);
	artwork->paintRequest(
	) | rpl::on_next([=] {
		auto p = Painter(artwork);
		media->paint(
			p,
			address,
			artwork->rect(),
			artwork->width(),
			st::walletDetailsCollectibleRadius);
	}, artwork->lifetime());
	const auto name = Ui::CreateChild<Ui::FlatLabel>(
		container,
		st::walletCollectibleTitleLabel);
	name->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto collection = Ui::CreateChild<Ui::AbstractButton>(container);
	const auto collectionLabel = Ui::CreateChild<Ui::FlatLabel>(
		collection,
		st::walletDetailsCollectionLabel);
	collectionLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
	collection->hide();
	collection->setClickedCallback([=] {
		const auto contract = media->collection(address);
		if (!contract.isEmpty()) {
			UrlClickHandler::Open(Core::TonExplorerUrl(
				session,
				FormatFriendly(contract, true)));
		}
	});
	collection->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(collection);
		const auto &arrow = st::walletDetailsCollectionArrow;
		arrow.paint(
			p,
			collectionLabel->width() + st::walletDetailsCollectionArrowSkip,
			(collection->height() - arrow.height()) / 2,
			collection->width());
	}, collection->lifetime());
	collectionLabel->sizeValue(
	) | rpl::on_next([=](QSize size) {
		collection->resize(size.width() + arrowWidth, size.height());
		collectionLabel->moveToLeft(0, 0, collection->width());
	}, collection->lifetime());
	const auto relayout = [=] {
		const auto hasCollection = withCollection
			&& !media->collection(address).isEmpty();
		collection->setVisible(hasCollection);
		const auto nameTop = st::walletDetailsCollectibleSize
			+ st::walletDetailsCollectibleNameSkip;
		const auto collectionTop = nameTop
			+ name->height()
			+ st::walletDetailsCollectionSkip;
		const auto height = hasCollection
			? (collectionTop + collection->height())
			: (nameTop + name->height());
		container->resize(available, height);
		container->setNaturalWidth(available);
		artwork->moveToLeft((available - artwork->width()) / 2, 0, available);
		name->moveToLeft((available - name->width()) / 2, nameTop, available);
		if (hasCollection) {
			collection->moveToLeft(
				(available - collection->width()) / 2,
				collectionTop,
				available);
		}
	};
	const auto apply = [=] {
		const auto view = media->view(address);
		name->setMarkedText(CollectibleTitleText(view));
		name->resizeToNaturalWidth(available);
		const auto contract = media->collection(address);
		if (!contract.isEmpty()) {
			collectionLabel->setText(view.collectionName.isEmpty()
				? ShortAddress(contract)
				: view.collectionName);
			collectionLabel->resizeToNaturalWidth(available - arrowWidth);
		}
		relayout();
	};
	const auto mine = [=](const QString &changed) {
		return (changed == address);
	};
	media->changed(
	) | rpl::filter(mine) | rpl::on_next(apply, container->lifetime());
	media->repaint(
	) | rpl::filter(mine) | rpl::on_next([=] {
		artwork->update();
	}, container->lifetime());
	rpl::combine(
		name->sizeValue(),
		collection->sizeValue()
	) | rpl::on_next(relayout, container->lifetime());
	apply();
}

class ActionRow final : public Ui::RpWidget {
public:
	ActionRow(QWidget *parent, ActionRowArgs &&args);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	const RowAvatar _avatar = RowAvatar::Gear;
	const not_null<Ui::FlatLabel*> _title;
	Ui::FlatLabel *_subtitle = nullptr;
	Ui::FlatLabel *_major = nullptr;
	Ui::FlatLabel *_minor = nullptr;
	int _circleTop = 0;

};

[[nodiscard]] RowAvatar ActionRowAvatar(ActionRowIcon icon) {
	switch (icon) {
	case ActionRowIcon::Incoming: return RowAvatar::In;
	case ActionRowIcon::Outgoing: return RowAvatar::Out;
	case ActionRowIcon::Gear: return RowAvatar::Gear;
	}
	Unexpected("Icon in ActionRowAvatar.");
}

ActionRow::ActionRow(QWidget *parent, ActionRowArgs &&args)
: RpWidget(parent)
, _avatar(ActionRowAvatar(args.icon))
, _title(Ui::CreateChild<Ui::FlatLabel>(
	this,
	args.address.isEmpty() ? args.kind : ShortAddressForm(args.address),
	st::walletRowTitleLabel)) {
	_title->setBreakEverywhere(true);
	_title->setAttribute(Qt::WA_TransparentForMouseEvents);
	if (!args.address.isEmpty()) {
		_subtitle = Ui::CreateChild<Ui::FlatLabel>(
			this,
			args.kind,
			st::walletRowSubtitleLabel);
		_subtitle->setBreakEverywhere(true);
		_subtitle->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
	if (const auto amount = args.amountNano) {
		_major = Ui::CreateChild<Ui::FlatLabel>(
			this,
			st::walletRowAmountMajorLabel);
		_major->setAttribute(Qt::WA_TransparentForMouseEvents);
		_minor = Ui::CreateChild<Ui::FlatLabel>(
			this,
			st::walletRowAmountMinorLabel);
		_minor->setAttribute(Qt::WA_TransparentForMouseEvents);
		const auto plus = (args.sign == ActionRowSign::Plus);
		const auto minus = (args.sign == ActionRowSign::Minus);
		SetRowAmountText(
			_major,
			_minor,
			*amount,
			plus ? u"+"_q : minus ? QString(kMinus) : QString());
		SetAmountColor(
			_major,
			_minor,
			(plus
				? st::boxTextFgGood
				: minus
				? st::windowBoldFg
				: st::windowSubTextFg));
	}
}

int ActionRow::resizeGetHeight(int newWidth) {
	const auto &padding = st::walletConnectActionPadding;
	const auto amountWidth = _major
		? (_major->width() + _minor->width() + st::walletRowSkip)
		: 0;
	const auto textWidth = newWidth - padding.left() - padding.right();
	_title->resizeToWidth(std::max(textWidth - amountWidth, 1));
	if (_subtitle) {
		_subtitle->resizeToWidth(std::max(textWidth, 1));
	}
	const auto textHeight = _title->height()
		+ (_subtitle ? (st::walletRowSkip + _subtitle->height()) : 0);
	const auto result = padding.top()
		+ std::max(st::walletRowIconSize, textHeight)
		+ padding.bottom();
	const auto textTop = (result - textHeight) / 2;
	_title->moveToLeft(padding.left(), textTop, newWidth);
	if (_subtitle) {
		_subtitle->moveToLeft(
			padding.left(),
			textTop + _title->height() + st::walletRowSkip,
			newWidth);
	}
	if (_major) {
		const auto lineHeight = st::walletRowTitleLabel.style.font->height;
		const auto majorTop = textTop + (lineHeight - _major->height()) / 2;
		_minor->moveToRight(
			padding.right(),
			majorTop + st::walletRowAmountMinorSkip,
			newWidth);
		_major->moveToRight(
			padding.right() + _minor->width(),
			majorTop,
			newWidth);
	}
	_circleTop = (result - st::walletRowIconSize) / 2;
	return result;
}

void ActionRow::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto size = st::walletRowIconSize;
	PaintRowAvatar(
		p,
		style::rtlrect(
			st::walletConnectActionIconLeft,
			_circleTop,
			size,
			size,
			width()),
		_avatar);
}

} // namespace

object_ptr<Ui::RpWidget> MakeActionRow(
		not_null<QWidget*> parent,
		ActionRowArgs args) {
	return object_ptr<ActionRow>(parent, std::move(args));
}

object_ptr<Ui::RpWidget> MakeCommentBubble(
		not_null<QWidget*> parent,
		object_ptr<Ui::RpWidget> content,
		const style::color &bg) {
	auto result = object_ptr<Ui::PaddingWrap<Ui::RpWidget>>(
		parent,
		std::move(content),
		st::giveawayGiftCodeValueMargin);
	const auto raw = result.data();
	const auto background = raw->lifetime().make_state<Ui::RoundRect>(
		st::boxRadius,
		bg);
	raw->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(raw);
		background->paint(p, raw->rect());
	}, raw->lifetime());
	return result;
}

namespace {

[[nodiscard]] bool HasDetailsComment(const TransferItem &item) {
	return item.commentEncrypted || !item.comment.trimmed().isEmpty();
}

[[nodiscard]] bool SameDetailsHeader(
		const TransferItem &a,
		const TransferItem &b) {
	return (ShowsCollectible(a) == ShowsCollectible(b))
		&& (a.collectible == b.collectible)
		&& (a.amountNano == b.amountNano)
		&& (a.incoming == b.incoming)
		&& (a.status == b.status)
		&& (a.comment == b.comment)
		&& (a.commentEncrypted == b.commentEncrypted)
		&& (a.encryptedFormat == b.encryptedFormat)
		&& (a.encryptedPayload == b.encryptedPayload);
}

void AddDetailsComment(
		not_null<Ui::GenericBox*> box,
		not_null<Ui::VerticalLayout*> layout,
		std::shared_ptr<Main::SessionShow> show,
		const TransferItem &item,
		Fn<bool()> originCurrent) {
	if (!HasDetailsComment(item)) {
		return;
	}
	const auto comment = item.comment.trimmed();
	auto label = item.commentEncrypted
		? object_ptr<Ui::FlatLabel>(object_ptr<EncryptedCommentLabel>(
			layout,
			box,
			std::move(show),
			item,
			std::move(originCurrent)))
		: object_ptr<Ui::FlatLabel>(layout, comment, st::walletCommentLabel);
	// The bubble stands inside the gap between the amount and the table
	// rather than under the amount: the header leaves half of that gap and
	// the bubble takes the other half, so it reads as its own line.
	layout->add(
		MakeCommentBubble(layout, std::move(label), st::windowBg),
		style::margins(
			st::giveawayGiftCodeTableMargin.left(),
			0,
			st::giveawayGiftCodeTableMargin.right(),
			st::walletDetailsAmountBottomSkip / 2),
		style::al_top);
}

[[nodiscard]] int GaslessDailyTransfers(not_null<Main::Session*> session) {
	return session->appConfig().get<int>(
		u"wallet_gasless_daily_transfers"_q,
		kGaslessDailyTransfersDefault);
}

[[nodiscard]] rpl::producer<int> GaslessDailyTransfersValue(
		not_null<Main::Session*> session) {
	return session->appConfig().value() | rpl::map([=] {
		return GaslessDailyTransfers(session);
	}) | rpl::distinct_until_changed();
}

// The fee itself is shown in the row that opens this box.
void ShowNetworkFeesAbout(
		std::shared_ptr<Ui::Show> show,
		not_null<Main::Session*> session) {
	show->showBox(Ui::MakeInformBox({
		.text = tr::lng_wallet_fees_text(
			tr::now,
			lt_count,
			GaslessDailyTransfers(session)),
		.title = tr::lng_wallet_fees_title(),
	}));
}

[[nodiscard]] TextWithEntities GramMark(
		Ui::Text::CustomEmojiHelper &helper,
		const style::font &font) {
	auto descriptor = Ui::Text::PaletteDependentEmoji{
		.factory = [=] {
			return Ui::Earn::IconCurrencyTwoTone(
				font,
				st::windowActiveTextFg->c);
		},
	};
	const auto image = descriptor.factory();
	const auto alignedTop = Ui::Earn::AlignedMarkTop(font, image);
	const auto emojiY = (font->height - st::emojiSize) / 2;
	const auto lineShift = Ui::Fixed(font->ascent) - font->fascent;
	const auto naturalTop = (lineShift + emojiY).toInt()
		+ Ui::Emoji::GetCustomSkipNormal();
	const auto marginTop = int(base::SafeRound(alignedTop - naturalTop));
	descriptor.margin = QMargins(0, marginTop, 0, 0);
	return helper.paletteDependent(std::move(descriptor));
}

// A transaction a message named is served after the box is already open, so
// the fee row exists from the first frame and says what it is waiting for.
enum class DetailsFee {
	Known,
	Loading,
	Failed,
};

void AddPendingFeeTableRow(
		not_null<Ui::TableLayout*> table,
		DetailsFee state) {
	Expects(state != DetailsFee::Known);

	if (state == DetailsFee::Failed) {
		Ui::AddTableRow(
			table,
			tr::lng_wallet_details_fee(),
			tr::lng_wallet_details_fee_unknown(tr::marked));
		return;
	}
	const auto &font = table->st().defaultValue.style.font;
	auto helper = Ui::Text::CustomEmojiHelper();
	const auto diamond = GramMark(helper, font);
	Ui::AddTableRow(
		table,
		tr::lng_wallet_details_fee(),
		tr::lng_contacts_loading(
			tr::italic
		) | rpl::map([=](TextWithEntities text) {
			auto result = diamond;
			result.append(QChar(' '));
			result.append(std::move(text));
			return result;
		}),
		helper.context());
}

void AddFeeTableRow(
		not_null<Ui::TableLayout*> table,
		std::shared_ptr<Ui::Show> show,
		not_null<Main::Session*> session,
		const TransferItem &item) {
	const auto &font = table->st().defaultValue.style.font;
	auto helper = Ui::Text::CustomEmojiHelper();
	const auto diamond = GramMark(helper, font);
	const auto feeNano = item.feeNano.value_or(0);
	auto value = rpl::producer<TextWithEntities>();
	if (item.gasless) {
		value = tr::lng_wallet_details_fee_free(
		) | rpl::map([=](const QString &text) {
			auto result = diamond;
			result.append(QChar(' '));
			result.append(text);
			return result;
		});
	} else {
		value = FiatRateValue(session) | rpl::map([=](const FiatRate &rate) {
			auto result = diamond;
			result.append(QChar(' '));
			result.append(Ui::FormatTonAmount(feeNano).full);
			result.append(QChar(' '));
			result.append(Ui::Text::Colorized(
				FormatFiat(feeNano, rate, kFeeFiatDecimals, true)));
			return result;
		});
	}
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		table,
		std::move(value),
		st::walletDetailsFeeLabel,
		st::defaultPopupMenu,
		helper.context());
	Ui::AddTableRow(
		table,
		tr::lng_wallet_details_fee(),
		Ui::MakeMultilineValueWithSmallButton(
			table,
			label,
			rpl::single(u"?"_q),
			[=](not_null<Ui::RpWidget*>) {
				ShowNetworkFeesAbout(show, session);
			}).widget);
}

// Userpic and name leading to the chat, and a Send pill for any user.
[[nodiscard]] object_ptr<Ui::RpWidget> PeerCounterpartyValue(
		not_null<Ui::GenericBox*> box,
		not_null<Ui::TableLayout*> table,
		std::shared_ptr<Main::SessionShow> show,
		not_null<PeerData*> peer) {
	const auto chatShow = MakeChatShow(show, true);
	const auto user = peer->asUser();
	const auto offer = (user != nullptr);
	const auto weak = base::make_weak(box);
	return Ui::MakePeerTableValue(
		table,
		chatShow,
		peer->id,
		offer ? tr::lng_send_button() : nullptr,
		offer ? Fn<void()>([=] { ShowSendToUser(show, user); }) : nullptr,
		[=] {
			const auto window = chatShow->resolveWindow();
			if (!window) {
				return;
			} else if (weak && weak->hasDelegate()) {
				weak->closeBox();
			}
			window->showPeerHistory(peer);
			window->window().activate();
		});
}

void AddPeerCounterpartyRows(
		not_null<Ui::GenericBox*> box,
		not_null<Ui::TableLayout*> table,
		std::shared_ptr<Main::SessionShow> show,
		not_null<PeerData*> peer,
		const TransferItem &item) {
	const auto address = !item.counterparty.isEmpty()
		? DetailsFriendlyAddress(item)
		: std::nullopt;
	const auto domain = item.counterpartyName.trimmed();
	auto label = (item.incoming
		? tr::lng_wallet_details_sender()
		: tr::lng_wallet_details_recipient());
	auto value = PeerCounterpartyValue(box, table, show, peer);
	if (address && !domain.isEmpty()) {
		auto wrap = object_ptr<Ui::VerticalLayout>(table);
		wrap->add(std::move(value));
		wrap->add(NameValueLabel(wrap.data(), show, domain, *address));
		value = std::move(wrap);
	}
	Ui::AddTableRow(
		table,
		std::move(label),
		std::move(value),
		st::giveawayGiftCodePeerMargin);
	if (address) {
		Ui::AddTableRow(
			table,
			tr::lng_wallet_details_address(),
			AddressValueLabel(table, show, *address));
	}
}

} // namespace

not_null<Ui::TableLayout*> AddDetailsTableFrame(
		not_null<Ui::VerticalLayout*> container) {
	const auto wrap = container->add(
		object_ptr<Ui::PaddingWrap<Ui::TableLayout>>(
			container,
			object_ptr<Ui::TableLayout>(container, st::walletDetailsTable),
			style::margins()),
		st::giveawayGiftCodeTableMargin);
	const auto bg = wrap->lifetime().make_state<Ui::RoundRect>(
		st::walletDetailsTable.radius,
		st::windowBg);
	wrap->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(wrap);
		bg->paint(p, wrap->rect());
	}, wrap->lifetime());
	return wrap->entity();
}

namespace {

void AddDetailsTable(
		not_null<Ui::GenericBox*> box,
		not_null<Ui::VerticalLayout*> container,
		std::shared_ptr<Main::SessionShow> show,
		const TransferItem &item,
		DetailsFee fee) {
	const auto session = &show->session();
	const auto table = AddDetailsTableFrame(container);
	if (item.status == TransferItem::Status::Failure) {
		const auto reason = item.failureReason.trimmed();
		Ui::AddTableRow(
			table,
			tr::lng_wallet_details_status(),
			reason.isEmpty()
				? tr::lng_channel_earn_history_failed(tr::marked)
				: tr::lng_wallet_details_failed_reason(
					lt_reason,
					rpl::single(TextWithEntities{ reason }),
					tr::marked));
	}
	const auto peerKind = (item.kind == TransferItem::Kind::PeerTransfer)
		|| (item.kind == TransferItem::Kind::Collectible);
	const auto peer = (peerKind && item.counterpartyPeer)
		? session->data().peerLoaded(PeerId(item.counterpartyPeer))
		: nullptr;
	if (item.kind == TransferItem::Kind::KeyChange) {
		Ui::AddTableRow(
			table,
			tr::lng_wallet_details_operation(),
			tr::lng_wallet_row_key_change(tr::marked));
	} else if (peer) {
		AddPeerCounterpartyRows(box, table, show, peer, item);
	} else if (!item.counterparty.isEmpty()) {
		if (const auto address = DetailsFriendlyAddress(item)) {
			auto label = (item.incoming
				? tr::lng_wallet_details_sender()
				: tr::lng_wallet_details_recipient());
			const auto provider = OnrampProvider(item);
			const auto name = !provider.isEmpty()
				? provider
				: item.counterpartyName.trimmed();
			if (name.isEmpty()) {
				Ui::AddTableRow(
					table,
					std::move(label),
					AddressValueLabel(table, box->uiShow(), *address));
			} else {
				Ui::AddTableRow(
					table,
					std::move(label),
					NameValueLabel(
						table,
						box->uiShow(),
						name,
						*address));
				Ui::AddTableRow(
					table,
					tr::lng_wallet_details_address(),
					AddressValueLabel(table, box->uiShow(), *address));
			}
		}
	}
	const auto pending
		= (item.status == TransferItem::Status::Pending);
	// An incoming transfer was paid for by whoever sent it, and what the
	// wallet spends to receive one is a few nanograms, so the row is left
	// out entirely and nothing is waited for on its behalf.
	if (!item.incoming) {
		if (fee != DetailsFee::Known) {
			AddPendingFeeTableRow(table, fee);
		} else if (!pending
			&& (item.gasless || (item.feeNano && *item.feeNano > 0))) {
			AddFeeTableRow(table, box->uiShow(), session, item);
		}
	}
	if (item.date) {
		Ui::AddTableRow(
			table,
			tr::lng_wallet_details_date(),
			rpl::single(tr::marked(
				langDateTime(base::unixtime::parse(*item.date)))));
	}
}

void AddBoxCloseButton(
		not_null<Ui::GenericBox*> box,
		Fn<void()> close = nullptr) {
	box->addTopButton(st::boxTitleClose, [=] {
		if (close) {
			close();
		} else {
			box->closeBox();
		}
	});
}

// The wallet's box footers share one "working" appearance: the label goes
// blank and an infinite spinner appears centred on the button. It stays two
// halves because they attach at two different moments - the label is a
// producer handed to addButton, the spinner is a child added once the button
// exists - so every box keeps its own label choice, its own guards and its
// own position for the spinner. The size and color follow the button's own
// style.
[[nodiscard]] rpl::producer<QString> BusyFooterLabel(
		rpl::producer<QString> text,
		rpl::producer<bool> busy) {
	return rpl::combine(
		std::move(text),
		std::move(busy)
	) | rpl::map([](const QString &text, bool busy) {
		return busy ? QString() : text;
	});
}

void AddBusyFooterSpinner(
		not_null<Ui::RoundButton*> button,
		rpl::producer<bool> shown) {
	using Radial = style::InfiniteRadialAnimation;
	const auto &buttonSt = button->st();
	const auto radialSt = button->lifetime().make_state<Radial>(
		st::startGiveawayButtonLoading);
	radialSt->color = buttonSt.textFg;
	const auto loading = Info::Statistics::InfiniteRadialAnimationWidget(
		button,
		buttonSt.height / 2,
		radialSt);
	Info::Statistics::AddChildToWidgetCenter(button, loading);
	loading->showOn(std::move(shown));
}

// The row counterpart of the footer spinner: the same animation in the
// row's subtext colour at its right edge, so a settings row keeps its label
// while the action it started is still in flight.
void AddRowSpinner(
		not_null<Ui::SettingsButton*> button,
		rpl::producer<bool> shown) {
	const auto &st = button->st();
	const auto size = st.style.font->height;
	const auto loading = Info::Statistics::InfiniteRadialAnimationWidget(
		button,
		size,
		&st::walletKeysRowLoading);
	loading->setAttribute(Qt::WA_TransparentForMouseEvents);
	button->sizeValue() | rpl::on_next([=](QSize outer) {
		loading->moveToRight(
			st.padding.right(),
			(outer.height() - size) / 2,
			outer.width());
	}, loading->lifetime());
	loading->showOn(std::move(shown));
}

// An inform box whose one button is Cancel: the label says what is being
// waited for, the spinner under it that the wait is on, and closing it by
// any means reports the same dismissal.
void WalletBusyBox(
		not_null<Ui::GenericBox*> box,
		rpl::producer<QString> text) {
	Ui::InformBox(box, {
		.text = std::move(text),
		.confirmText = tr::lng_cancel(),
	});
	const auto &loading = st::walletBusyBoxLoading;
	const auto side = loading.size.height() + 2 * loading.thickness;
	const auto content = box->addRow(
		object_ptr<Ui::FixedHeightWidget>(box, side),
		st::walletBusyBoxPadding);
	const auto indicator = Info::Statistics::InfiniteRadialAnimationWidget(
		content,
		side,
		&loading);
	Info::Statistics::AddChildToWidgetCenter(content, indicator);
	indicator->show();
}

[[nodiscard]] QImage ReceiveQrCenter(int side, int markSide) {
	auto result = QImage(side, side, QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::white);
	auto p = QPainter(&result);
	auto hq = PainterHighQualityEnabler(p);
	auto svg = QSvgRenderer(
		Ui::Earn::CurrencySvgTwoTone(st::activeButtonBg->c));
	const auto skip = (side - markSide) / 2;
	svg.render(&p, QRectF(skip, skip, markSide, markSide));
	return result;
}

[[nodiscard]] QImage ReceiveQrImage(
		const QString &address,
		int size,
		int ratio,
		int quietZoneModules = 0) {
	const auto data = Qr::Encode(address, Qr::Redundancy::Quartile);
	const auto pixel = std::max(size / std::max(data.size, 1), 1);
	auto image = Qr::Generate(data, pixel * ratio, Qt::black, Qt::white);
	const auto replaceSide = Qr::ReplaceSize(data, pixel * ratio);
	const auto markSide = std::min(
		st::walletReceiveMarkSize * ratio,
		replaceSide - 2 * pixel * ratio);
	image = Qr::ReplaceCenter(
		std::move(image),
		ReceiveQrCenter(replaceSide, markSide));
	if (quietZoneModules > 0) {
		const auto skip = quietZoneModules * pixel * ratio;
		auto padded = QImage(
			image.width() + 2 * skip,
			image.height() + 2 * skip,
			QImage::Format_ARGB32_Premultiplied);
		padded.fill(Qt::white);
		auto p = QPainter(&padded);
		p.drawImage(skip, skip, image);
		p.end();
		image = std::move(padded);
	}
	image.setDevicePixelRatio(ratio);
	return image;
}

struct WalletBoxTitleBar {
	not_null<Ui::FlatLabel*> title;
	not_null<Ui::IconButton*> close;
};

[[nodiscard]] WalletBoxTitleBar AddWalletBoxTitleBar(
		not_null<Ui::GenericBox*> box,
		rpl::producer<QString> title) {
	const auto row = box->addRow(
		object_ptr<Ui::FixedHeightWidget>(
			box,
			st::walletReceiveTitleHeight),
		style::margins(),
		style::al_justify);
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		row,
		std::move(title),
		st::boxTitle);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto close = Ui::CreateChild<Ui::IconButton>(
		row,
		st::boxTitleClose);
	Ui::ToggleChildrenVisibility(row, true);
	row->sizeValue(
	) | rpl::on_next([=](QSize size) {
		label->moveToLeft(
			st::boxTitlePosition.x(),
			st::boxTitlePosition.y(),
			size.width());
		close->moveToRight(st::walletReceiveTitleButtonSkip, 0, size.width());
	}, row->lifetime());
	return { label, close };
}

struct OldWalletAppLink {
	QString appname;
	QString startapp;
	std::optional<QString> startattach;
	bool compact = false;
	bool fullscreen = false;
};

[[nodiscard]] bool IsValidOnrampUrl(const QString &url) {
	const auto parsed = QUrl(url, QUrl::StrictMode);
	return parsed.isValid()
		&& parsed.scheme() == u"https"_q
		&& !parsed.host().isEmpty();
}

[[nodiscard]] std::optional<OldWalletAppLink> ParseOldWalletAppLink(
		not_null<Main::Session*> session,
		const QString &url) {
	const auto configured = session->appConfig().oldWalletBotUsername();
	if (configured.isEmpty()) {
		return {};
	}
	const auto prefix = u"tg://resolve?"_q;
	const auto local = Core::TryConvertUrlToLocal(url);
	if (!local.startsWith(prefix, Qt::CaseInsensitive)) {
		return {};
	}
	const auto params = qthelp::url_parse_params(
		local.mid(prefix.size()),
		qthelp::UrlParamNameTransform::ToLower);
	if (params.value(u"domain"_q).compare(configured, Qt::CaseInsensitive)) {
		return {};
	}
	const auto mode = params.value(u"mode"_q);
	return OldWalletAppLink{
		.appname = params.value(u"appname"_q),
		.startapp = params.value(u"startapp"_q),
		.startattach = (params.contains(u"startattach"_q)
			? params.value(u"startattach"_q)
			: std::optional<QString>()),
		.compact = (mode == u"compact"_q),
		.fullscreen = (mode == u"fullscreen"_q),
	};
}

void OpenOldWalletApp(
		not_null<UserData*> bot,
		std::shared_ptr<Ui::Show> show,
		const OldWalletAppLink &link) {
	const auto startCommand = link.startattach.value_or(link.startapp);
	auto source = link.startattach
		? InlineBots::WebViewSource(InlineBots::WebViewSourceLinkAttachMenu{
			.token = startCommand,
		})
		: !link.appname.isEmpty()
		? InlineBots::WebViewSource(InlineBots::WebViewSourceLinkApp{
			.appname = link.appname,
			.token = startCommand,
		})
		: InlineBots::WebViewSource(InlineBots::WebViewSourceLinkBotProfile{
			.token = startCommand,
			.compact = link.compact,
		});
	bot->session().attachWebView().open({
		.bot = bot,
		.parentShow = std::move(show),
		.context = {
			.action = ::Api::SendAction(bot->owner().history(bot)),
			.fullscreen = link.fullscreen,
			.maySkipConfirmation = true,
		},
		.button = { .startCommand = startCommand },
		.source = std::move(source),
	});
}

void OpenOldWalletAppLink(
		not_null<Main::Session*> session,
		std::shared_ptr<Ui::Show> show,
		const OldWalletAppLink &link,
		const QString &url) {
	const auto username = session->appConfig().oldWalletBotUsername();
	const auto byUsername = session->data().peerByUsername(username);
	if (const auto bot = byUsername ? byUsername->asUser() : nullptr) {
		if (bot->isOldWalletBot()) {
			OpenOldWalletApp(bot, std::move(show), link);
			return;
		}
	}
	session->api().request(MTPcontacts_ResolveUsername(
		MTP_flags(0),
		MTP_string(username),
		MTP_string()
	)).done([=](const MTPcontacts_ResolvedPeer &result) {
		const auto &data = result.data();
		session->data().processUsers(data.vusers());
		session->data().processChats(data.vchats());
		const auto peerId = peerFromMTP(data.vpeer());
		const auto bot = peerId
			? session->data().peer(peerId)->asUser()
			: nullptr;
		if (bot && bot->isOldWalletBot()) {
			OpenOldWalletApp(bot, show, link);
		} else {
			UrlClickHandler::Open(url);
		}
	}).fail([=] {
		UrlClickHandler::Open(url);
	}).send();
}

void OpenWalletUrl(
		not_null<Main::Session*> session,
		std::shared_ptr<Ui::Show> show,
		const QString &url) {
	if (const auto link = ParseOldWalletAppLink(session, url)) {
		OpenOldWalletAppLink(session, std::move(show), *link, url);
	} else {
		UrlClickHandler::Open(url);
	}
}

not_null<Ui::IconButton*> AddRowChevron(not_null<Ui::RpWidget*> button) {
	const auto arrow = Ui::CreateChild<Ui::IconButton>(
		button,
		st::backButton);
	arrow->setIconOverride(
		&st::settingsPremiumArrow,
		&st::settingsPremiumArrowOver);
	arrow->setAttribute(Qt::WA_TransparentForMouseEvents);
	arrow->show();
	button->sizeValue(
	) | rpl::on_next([=](QSize size) {
		const auto &shift = st::settingsPremiumArrowShift;
		arrow->moveToRight(
			-shift.x(),
			shift.y() + (size.height() - arrow->height()) / 2);
	}, arrow->lifetime());
	return arrow;
}

void WalletReceiveBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		const QString &address) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::walletReceiveBox);
	box->setNoContentMargin(true);
	box->setCustomCornersFilling(RectPart::FullTop | RectPart::FullBottom);

	struct State {
		rpl::variable<bool> resolving = false;
		QImage image;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto show = box->uiShow();

	box->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(box);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::activeButtonBg);
		p.drawRoundedRect(box->rect(), st::boxRadius, st::boxRadius);
	}, box->lifetime());

	const auto bar = AddWalletBoxTitleBar(box, tr::lng_wallet_add_funds());
	rpl::single(
		rpl::empty
	) | rpl::then(
		style::PaletteChanged()
	) | rpl::on_next([=] {
		const auto scope = WindowPaletteScope(box);
		bar.title->setTextColorOverride(st::activeButtonFg->c);
	}, box->lifetime());
	bar.close->setIconOverride(
		&st::walletReceiveCloseIconActive,
		&st::walletReceiveCloseIconActiveOver);
	bar.close->setRippleColorOverride(&st::activeButtonBgRipple);
	bar.close->setClickedCallback([=] {
		box->closeBox();
	});

	const auto inner = box->verticalLayout();
	{
		const auto scope = WindowPaletteScope(box);
		state->image = ReceiveQrImage(
			address,
			st::walletReceiveQrSize,
			style::DevicePixelRatio());
	}
	const auto qrSide = state->image.width() / style::DevicePixelRatio();
	const auto &padding = st::walletReceivePlatePadding;
	const auto font = st::walletReceiveAddressFont->monospace();
	const auto hintFont = st::walletReceiveHintFont;
	const auto groupWidth = font->width(address.left(kAddressGroup));
	const auto spaceWidth = font->width(QChar(' '));
	const auto lineWidth = kReceiveGroupsPerLine * groupWidth
		+ (kReceiveGroupsPerLine - 1) * spaceWidth;
	const auto lineHeight = font->height + st::walletReceiveAddressLineSkip;
	const auto blockHeight = kReceiveLines * font->height
		+ (kReceiveLines - 1) * st::walletReceiveAddressLineSkip;
	const auto qrLeft = padding.left();
	const auto qrTop = padding.top();
	const auto addressTop = qrTop + qrSide + st::walletReceiveAddressTopSkip;
	const auto hintTop = addressTop
		+ blockHeight
		+ st::walletReceiveHintTopSkip;
	const auto plateWidth = std::max(qrSide, lineWidth)
		+ padding.left()
		+ padding.right();
	const auto plateHeight = hintTop + hintFont->height + padding.bottom();

	auto plateOwned = object_ptr<Ui::FixedHeightWidget>(inner, plateHeight);
	plateOwned->setNaturalWidth(plateWidth);
	const auto plate = inner->add(
		std::move(plateOwned),
		st::walletReceivePlateMargin,
		style::al_top);
	plate->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(plate);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::white);
		p.drawRoundedRect(
			plate->rect(),
			st::walletReceivePlateRadius,
			st::walletReceivePlateRadius);
		p.drawImage(qrLeft, qrTop, state->image);
		p.setFont(font);
		const auto left = (plate->width() - lineWidth) / 2;
		for (auto i = 0; i != kAddressLength / kAddressGroup; ++i) {
			const auto line = i / kReceiveGroupsPerLine;
			const auto column = i % kReceiveGroupsPerLine;
			p.setPen((i % 2)
				? QColor(0x99, 0x99, 0x99)
				: QColor(0x22, 0x22, 0x22));
			p.drawText(
				left + column * (groupWidth + spaceWidth),
				addressTop + line * lineHeight + font->ascent,
				address.mid(i * kAddressGroup, kAddressGroup));
		}
		const auto hint = hintFont->elided(
			tr::lng_wallet_receive_copy_hint(tr::now),
			plate->width() - padding.left() - padding.right());
		p.setFont(hintFont);
		p.setPen(QColor(0x99, 0x99, 0x99));
		p.drawText(
			(plate->width() - hintFont->width(hint)) / 2,
			hintTop + hintFont->ascent,
			hint);
	}, plate->lifetime());
	style::PaletteChanged(
	) | rpl::on_next([=] {
		const auto scope = WindowPaletteScope(plate);
		state->image = ReceiveQrImage(
			address,
			st::walletReceiveQrSize,
			style::DevicePixelRatio());
		plate->update();
	}, plate->lifetime());

	const auto qrTarget = Ui::CreateChild<Ui::AbstractButton>(plate);
	qrTarget->setClickedCallback([=] {
		const auto scope = WindowPaletteScope(plate);
		QGuiApplication::clipboard()->setImage(ReceiveQrImage(
			address,
			st::walletReceiveQrCopySize,
			1,
			kQrQuietZoneModules));
		show->showToast({
			.text = { tr::lng_group_invite_qr_copied(tr::now) },
			.iconLottie = u"toast/copy"_q,
			.iconLottieSize = st::toastLottieIconSize,
		});
	});
	const auto textTarget = Ui::CreateChild<Ui::AbstractButton>(plate);
	textTarget->setClickedCallback([=] {
		TextUtilities::SetClipboardText(TextForMimeData::Simple(address));
		show->showToast({
			.text = { tr::lng_gift_unique_address_copied(tr::now) },
			.iconLottie = u"toast/copy"_q,
			.iconLottieSize = st::toastLottieIconSize,
		});
	});
	Ui::ToggleChildrenVisibility(plate, true);
	const auto textTop = addressTop - st::walletReceiveAddressTopSkip / 2;
	plate->sizeValue(
	) | rpl::on_next([=](QSize size) {
		qrTarget->setGeometry(qrLeft, qrTop, qrSide, qrSide);
		textTarget->setGeometry(
			0,
			textTop,
			size.width(),
			size.height() - textTop);
	}, plate->lifetime());

	inner->add(
		object_ptr<Ui::FlatLabel>(
			inner,
			tr::lng_wallet_receive_about(),
			st::walletReceiveAboutLabel),
		st::walletReceiveAboutMargin,
		style::al_top
	)->setTryMakeSimilarLines(true);

	// The margin belongs to the slide wrap's own padding, not to the row:
	// VerticalLayout::moveChildGetSkip() adds a row's top and bottom margin
	// unconditionally, so a row margin would keep a gap above the Buy button
	// while the caveat is collapsed.
	const auto caveat = inner->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			inner,
			object_ptr<Ui::FlatLabel>(
				inner,
				tr::lng_wallet_receive_caveat(),
				st::walletReceiveAboutLabel),
			st::walletReceiveAboutMargin),
		style::margins(),
		style::al_top);
	caveat->entity()->setTryMakeSimilarLines(true);
	caveat->toggleOn(session->wallet().deviceCustodyStateValue(
	) | rpl::map([](const DeviceCustodyState &custody) {
		return (custody.mode == DeviceMode::ReadOnlyNotRestorable);
	}));
	caveat->finishAnimating();

	const auto buy = inner->add(
		object_ptr<Ui::RoundButton>(
			inner,
			BusyFooterLabel(
				tr::lng_wallet_buy_button(),
				state->resolving.value()),
			st::walletReceiveBuyButton),
		st::walletReceiveBuyMargin,
		style::al_justify);
	buy->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	AddBusyFooterSpinner(buy, state->resolving.value());
	buy->setClickedCallback([=] {
		if (state->resolving.current()) {
			return;
		}
		state->resolving = true;
		session->wallet().onramp().requestSessionUrl(
			address,
			session->wallet().rates().current().currency,
			crl::guard(box, [=](QString url) {
				state->resolving = false;
				if (!IsValidOnrampUrl(url)) {
					show->showToast(tr::lng_wallet_buy_empty(tr::now));
					return;
				}
				OpenWalletUrl(session, show, url);
				box->closeBox();
			}));
	});
	buy->widthValue(
	) | rpl::on_next([=](int width) {
		buy->setFullWidth(width);
	}, buy->lifetime());
}

void ShowWalletReceiveBox(
		not_null<Main::Session*> session,
		std::shared_ptr<Ui::Show> show) {
	auto &wallet = session->wallet();
	const auto address = wallet.addressFriendly(false);
	if (address.size() != kAddressLength) {
		return;
	}
	show->showBox(Box(WalletReceiveBox, session, address));
}

[[nodiscard]] Ui::LayerStackWidget *BoxLayerStack(
		not_null<Ui::GenericBox*> box) {
	auto stack = (Ui::LayerStackWidget*)nullptr;
	for (auto parent = box->parentWidget(); parent && !stack;) {
		parent = parent->parentWidget();
		stack = dynamic_cast<Ui::LayerStackWidget*>(parent);
	}
	return stack;
}

void CloseByOutsideClick(not_null<Ui::GenericBox*> box) {
	const auto layer = box->parentWidget();
	const auto stack = BoxLayerStack(box);
	if (!layer || !stack) {
		return;
	}
	// WHY: a background press clears the whole stack, the Transaction box
	// below included, so it is eaten while this box is the shown layer, and
	// the close is postponed because it destroys this filter synchronously.
	base::install_event_filter(box, stack, [=](not_null<QEvent*> e) {
		if (e->type() != QEvent::MouseButtonPress || layer->isHidden()) {
			return base::EventFilterResult::Continue;
		}
		Ui::PostponeCall(box, [=] { box->closeBox(); });
		return base::EventFilterResult::Cancel;
	});
}

void AddWalletFeaturesBody(
		not_null<Ui::GenericBox*> box,
		rpl::producer<QString> title,
		rpl::producer<QString> subtitle,
		const std::vector<Ui::FeatureListEntry> &features,
		rpl::producer<QString> button) {
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			std::move(title),
			st::walletPhraseTitleLabel),
		st::boxRowPadding,
		style::al_top);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			std::move(subtitle),
			st::walletHowSubtitleLabel),
		st::walletHowSubtitleMargin,
		style::al_top
	)->setTryMakeSimilarLines(true);

	for (const auto &feature : features) {
		box->addRow(Ui::MakeFeatureListEntry(box, feature));
	}

	AddBoxCloseButton(box);

	box->addButton(std::move(button), [=] { box->closeBox(); });

	box->showFinishes() | rpl::take(1) | rpl::on_next([=] {
		CloseByOutsideClick(box);
	}, box->lifetime());
}

void WalletHowItWorksBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	AddWalletLottie(box, st::walletHowLottieMargin);

	const auto features = std::vector<Ui::FeatureListEntry>{
		{
			.icon = st::walletAboutInstantIcon,
			.title = tr::lng_wallet_about_instant_title(tr::now),
			.about = tr::lng_wallet_about_instant_text(tr::now, tr::marked),
			.similarLines = true,
		},
		{
			.icon = st::walletAboutFeesIcon,
			.title = tr::lng_wallet_about_fees_title(tr::now),
			.about = tr::lng_wallet_about_fees_text(
				tr::now,
				lt_count,
				GaslessDailyTransfers(session),
				tr::marked),
			.similarLines = true,
		},
		{
			.icon = st::walletAboutChainIcon,
			.title = tr::lng_wallet_about_chain_title(tr::now),
			.about = tr::lng_wallet_about_chain_text(tr::now, tr::marked),
			.similarLines = true,
		},
	};
	AddWalletFeaturesBody(
		box,
		tr::lng_wallet_how_title(),
		tr::lng_wallet_how_subtitle(),
		features,
		tr::lng_archive_hint_button());
}

void WalletFirstGramsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	AddWalletLottie(box, st::walletHowLottieMargin);

	auto subtitle = FiatRateValue(session) | rpl::map([](const FiatRate &rate) {
		return rate.available()
			? tr::lng_wallet_first_rate(
				lt_amount,
				rpl::single(FormatFiat(Ui::kNanosInOne, rate)))
			: tr::lng_wallet_first_rate_none();
	}) | rpl::flatten_latest();

	const auto features = std::vector<Ui::FeatureListEntry>{
		{
			.icon = st::walletFirstSendIcon,
			.title = tr::lng_wallet_first_send_title(tr::now),
			.about = tr::lng_wallet_first_send_text(
				tr::now,
				lt_attach,
				Ui::Text::IconEmoji(&st::walletFirstAttachEmoji),
				lt_money,
				tr::marked(tr::lng_wallet_menu(tr::now)),
				tr::marked),
			.similarLines = true,
		},
		{
			.icon = st::walletFirstTradeIcon,
			.title = tr::lng_wallet_first_trade_title(tr::now),
			.about = tr::lng_wallet_first_trade_text(tr::now, tr::marked),
			.similarLines = true,
		},
		{
			.icon = st::walletFirstStoreIcon,
			.title = tr::lng_wallet_first_store_title(tr::now),
			.about = tr::lng_wallet_first_store_text(
				tr::now,
				lt_menu,
				Ui::Text::IconEmoji(&st::walletFirstMenuEmoji),
				lt_wallet,
				tr::marked(tr::lng_wallet_menu(tr::now)),
				tr::marked),
			.similarLines = true,
		},
	};
	AddWalletFeaturesBody(
		box,
		tr::lng_wallet_first_title(),
		std::move(subtitle),
		features,
		tr::lng_archive_hint_button());

	box->boxClosing() | rpl::on_next([weak = base::make_weak(session)] {
		if (const auto strong = weak.get()) {
			strong->promoSuggestions().dismiss(
				Data::PromoSuggestions::SugWalletFirstIncomingTransfer());
		}
	}, box->lifetime());
}

void WalletCloudPasswordCreateBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	const auto content = box->addRow(
		object_ptr<Ui::VerticalLayout>(box),
		st::boxRowPadding);
	const auto fields = Settings::CloudPassword::SetupPasswordFields(
		content,
		Settings::CloudPassword::CreatePasswordDescriptor());

	AddBoxCloseButton(box);

	struct State {
		rpl::lifetime request;
		rpl::variable<bool> loading = false;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto submit = [=] {
		if (state->request) {
			return;
		}
		const auto password = Settings::CloudPassword::ValidatePasswordFields(
			fields);
		if (!password) {
			return;
		}
		state->loading = true;
		state->request = show->session().api().cloudPassword().set(
			QString(),
			*password,
			QString(),
			false,
			QString()
		) | rpl::on_error_done([=](const QString &type) {
			state->request.destroy();
			state->loading = false;
			fields.error->show();
			fields.error->setText(MTP::IsFloodError(type)
				? tr::lng_flood_error(tr::now)
				: Lang::Hard::ServerError());
		}, [=] {
			state->request.destroy();
			box->closeBox();
			show->showToast({
				.text = { tr::lng_cloud_password_was_set(tr::now) },
				.icon = &st::toastCheckIcon,
			});
		});
	};
	const auto button = box->addButton(
		BusyFooterLabel(
			tr::lng_settings_cloud_password_password_subtitle(),
			state->loading.value()),
		submit);
	AddBusyFooterSpinner(button, state->loading.value());

	Settings::CloudPassword::SubmitPasswordFields(fields, submit);
	box->setFocusCallback([=] {
		Settings::CloudPassword::FocusPasswordFields(fields);
	});
}

void WalletCloudPasswordIntroBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	const auto content = box->addRow(
		object_ptr<Ui::VerticalLayout>(box),
		st::boxRowPadding);
	Settings::CloudPassword::SetupIntroHeader(content, box->showFinishes());
	Ui::AddSkip(content, st::settingLocalPasscodeDescriptionBottomSkip);

	AddBoxCloseButton(box);

	box->addButton(tr::lng_settings_cloud_password_password_subtitle(), [=] {
		box->closeBox();
		show->showBox(Box(WalletCloudPasswordCreateBox, show));
	});
}

void SetupIntroTooltip(
		not_null<Ui::RpWidget*> parent,
		not_null<Ui::RpWidget*> card,
		Fn<QRect()> markRect,
		rpl::producer<> moves) {
	struct State {
		Ui::GlareTooltip *tooltip = nullptr;
		base::Timer hide;
		bool started = false;
		bool finished = false;
	};
	const auto state = parent->lifetime().make_state<State>();
	const auto colors = [=] {
		const auto scope = WindowPaletteScope(parent);
		return Ui::GlareTooltipColors{
			.edge = st::windowActiveTextFg->c,
			.center = anim::color(
				st::windowActiveTextFg,
				st::activeButtonFg,
				0.35),
			.rim = st::activeButtonFg->c,
			.text = st::activeButtonFg->c,
		};
	};
	state->tooltip = Ui::CreateChild<Ui::GlareTooltip>(
		parent.get(),
		st::walletIntroTooltip,
		st::walletIntroTooltipFont,
		tr::lng_wallet_intro_text(tr::now),
		colors());
	state->tooltip->setAttribute(Qt::WA_TransparentForMouseEvents);
	state->tooltip->finishAnimating();
	style::PaletteChanged() | rpl::on_next([=] {
		if (!state->finished) {
			state->tooltip->setColors(colors());
		}
	}, parent->lifetime());
	const auto finish = [=] {
		if (state->finished) {
			return;
		}
		state->finished = true;
		state->hide.cancel();
		state->tooltip->stopGlare();
		state->tooltip->fade(false);
	};
	state->hide.setCallback(finish);

	rpl::merge(
		card->geometryValue() | rpl::to_empty,
		parent->widthValue() | rpl::to_empty,
		std::move(moves)
	) | rpl::on_next([=] {
		if (state->finished || !parent->width()) {
			return;
		}
		const auto mark = markRect();
		if (mark.isEmpty()) {
			if (state->started) {
				finish();
			}
			return;
		}
		state->tooltip->pointAt(
			mark,
			Ui::MapFrom(parent, card, card->rect()));
		if (!state->started) {
			state->started = true;
			state->tooltip->fade(true);
			state->hide.callOnce(
				state->tooltip->glaresDuration(kWalletIntroGlares));
		}
	}, parent->lifetime());
}

[[nodiscard]] QString ExplorerTransactionUrl(
		not_null<Main::Session*> session,
		const QByteArray &traceId) {
	if (traceId.isEmpty()) {
		return QString();
	}
	return Core::TonExplorerUrl(
		session,
		u"transaction/"_q + QString::fromLatin1(traceId.toHex()));
}

void WalletTransactionBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		TransferItem item,
		bool partial,
		std::shared_ptr<CollectibleMedia> media,
		Fn<bool()> originCurrent,
		rpl::producer<> originInvalidated,
		Fn<void()> openWallet,
		rpl::producer<TransferItem> updates) {
	if (originCurrent && !originCurrent()) {
		box->closeBox();
		return;
	}
	const auto session = &show->session();
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::walletDetailsBox);
	box->setNoContentMargin(true);
	box->setTitle(tr::lng_wallet_details_title());

	struct State {
		TransferItem item;
		std::shared_ptr<CollectibleMedia> media;
		base::Timer retry;
		int attempts = 0;
		bool looking = false;
	};
	const auto looking = partial && !item.id.isEmpty();
	const auto state = box->lifetime().make_state<State>();
	state->item = std::move(item);
	state->media = std::move(media);
	state->looking = looking;
	const auto art = box->addRow(
		object_ptr<Ui::VerticalLayout>(box),
		style::margins(),
		style::al_justify);
	const auto header = box->addRow(
		object_ptr<Ui::VerticalLayout>(box),
		style::margins(),
		style::al_justify);
	const auto fillArt = [=] {
		art->clear();
		if (!ShowsCollectible(state->item)) {
			// The animation stands where a top skip used to, so the amount and
			// everything under it move up by that much under the box title.
			AddWalletLottie(box, style::margins(), art);
		}
	};
	const auto fillHeader = [=] {
		header->clear();
		// The gap between the header and the table is one skip, whether or not
		// a comment stands in it - see AddDetailsComment for the other half.
		const auto headerBottomSkip = HasDetailsComment(state->item)
			? (st::walletDetailsAmountBottomSkip / 2)
			: st::walletDetailsAmountBottomSkip;
		if (ShowsCollectible(state->item)) {
			if (!state->media) {
				state->media = std::make_shared<CollectibleMedia>(session);
			}
			state->media->resolve(state->item.collectible);
			AddDetailsCollectibleHeader(
				header,
				session,
				state->media,
				state->item,
				headerBottomSkip);
		} else {
			AddDetailsAmountHeader(
				header,
				state->item,
				0,
				headerBottomSkip,
				FiatRateValue(session));
		}
		AddDetailsComment(
			box,
			header,
			Main::MakeSessionShow(box->uiShow(), session),
			state->item,
			originCurrent);
	};
	fillArt();
	fillHeader();

	// The amount and the comment are what the message itself said, while the
	// rows below are the transaction's own record: who it went to under their
	// Telegram name, what it cost and when the chain accepted it. The table
	// is built from the message at once and again from the served record, so
	// the box shows everything it can immediately and nothing of it waits.
	const auto details = box->addRow(
		object_ptr<Ui::VerticalLayout>(box),
		style::margins());
	const auto rebuild = [=](DetailsFee fee) {
		details->clear();
		AddDetailsTable(box, details, show, state->item, fee);
	};
	rebuild(state->looking ? DetailsFee::Loading : DetailsFee::Known);
	if (updates) {
		std::move(updates) | rpl::on_next([=](TransferItem updated) {
			if (state->looking || updated == state->item) {
				return;
			}
			const auto was = std::exchange(state->item, std::move(updated));
			if (ShowsCollectible(was) != ShowsCollectible(state->item)) {
				fillArt();
			}
			if (!SameDetailsHeader(was, state->item)) {
				fillHeader();
			}
			rebuild(DetailsFee::Known);
		}, box->lifetime());
	}
	if (state->looking) {
		// The message can arrive before the transaction it names is served,
		// which is what a transfer just sent looks like, so an answer that
		// names nothing is asked again for a while before it is read as an
		// answer. The timer belongs to the box, so closing it stops asking.
		const auto lookup = [=] {
			session->wallet().resolveTransaction(
				state->item.id,
				crl::guard(box, [=](ResolvedTransaction resolved) {
					if (originCurrent && !originCurrent()) {
						return;
					} else if (resolved.item) {
						if (resolved.item->traceId.isEmpty()) {
							resolved.item->traceId = state->item.traceId;
						}
						state->item = std::move(*resolved.item);
						state->looking = false;
						rebuild(DetailsFee::Known);
						return;
					} else if (resolved.failed
						|| ++state->attempts >= kTransactionLookupAttempts) {
						rebuild(DetailsFee::Failed);
						return;
					}
					state->retry.callOnce(kTransactionLookupInterval);
				}));
		};
		state->retry.setCallback(lookup);
		lookup();
	}

	AddBoxCloseButton(box);
	const auto toggle = box->addTopButton(st::boxTitleMenu);
	const auto menu = box->lifetime().make_state<
		base::unique_qptr<Ui::PopupMenu>>();
	toggle->setClickedCallback([=] {
		if (*menu) {
			return;
		}
		*menu = base::make_unique_q<Ui::PopupMenu>(
			box,
			st::popupMenuWithIcons);
		const auto raw = menu->get();
		raw->setDestroyedCallback(crl::guard(toggle, [=] {
			toggle->setForceRippled(false);
		}));
		toggle->setForceRippled(true);
		// Read when the menu opens, not when the box was built: a served
		// transaction can name the trace a message did not carry.
		const auto url = ExplorerTransactionUrl(session, state->item.traceId);
		if (!url.isEmpty()) {
			raw->addAction(
				Ui::Text::FixAmpersandInAction(
					tr::lng_channel_earn_history_out_button(tr::now)),
				[=] { UrlClickHandler::Open(url); },
				&st::menuIconSearch);
		}
		raw->addAction(
			Ui::Text::FixAmpersandInAction(
				tr::lng_wallet_how_menu(tr::now)),
			[=] { show->showBox(Box(WalletHowItWorksBox, session)); },
			&st::menuIconFaq);
		raw->setForcedOrigin(Ui::PanelAnimation::Origin::TopRight);
		const auto scope = WindowPaletteScope(box);
		raw->popup(toggle->mapToGlobal(QPoint(
			toggle->width(),
			toggle->height())));
	});

	if (openWallet) {
		box->addButton(tr::lng_wallet_details_open_wallet(), [=] {
			const auto open = openWallet;
			box->closeBox();
			open();
		});
	} else {
		box->addButton(tr::lng_box_ok(), [=] { box->closeBox(); });
	}
	if (originCurrent) {
		const auto close = [weak = base::make_weak(box)] {
			if (weak && weak->hasDelegate()) {
				weak->closeBox();
			}
		};
		std::move(originInvalidated) | rpl::take(1) | rpl::on_next(
			close,
			box->lifetime());
		if (!originCurrent()) {
			close();
		}
	}
}

void ShowWalletTransactionBox(
		std::shared_ptr<Main::SessionShow> show,
		const TransferItem &item,
		std::shared_ptr<CollectibleMedia> media = nullptr,
		rpl::producer<TransferItem> updates = nullptr) {
	const auto current = [show, identity = item.walletIdentity] {
		if (!show || !show->valid()) {
			return false;
		}
		return !identity
			|| show->session().wallet().transferWalletIdentityCurrent(*identity);
	};
	if (!current()) {
		return;
	}
	show->showBox(Box(
		WalletTransactionBox,
		show,
		item,
		false,
		std::move(media),
		current,
		show->session().wallet().transferWalletIdentityChanges()
			| rpl::filter([=] { return !current(); }),
		Fn<void()>(),
		std::move(updates)), Ui::LayerOption::KeepOther);
}

[[nodiscard]] int CommentBytes(const QString &text) {
	return SendCommentBytes(text);
}

[[nodiscard]] bool CommentFits(const QString &text) {
	return SendCommentFits(text);
}

// A comment is limited in UTF-8 bytes, so a Cyrillic letter costs two and an
// emoji four. Saying that in words explains nothing to anyone typing, so the
// field just counts down, and it starts counting late enough that the people
// who never approach the limit never see it. The limit is in bytes while a
// field's own maximum is in UTF-16 units, so the field keeps no maximum of
// its own and the count is allowed to go negative until sending refuses it.
//
// `rightSkip` is what already stands in the field's top right corner, which
// the counter has to stand to the left of.
void ApplyCommentLimit(
		not_null<Ui::InputField*> field,
		int rightSkip = 0) {
	const auto &limitSt = st::defaultInputFieldLimit;
	auto options = Ui::LengthLimitLabelOptions{
		.customThreshold = kSendCommentWarnBytes,
		.customCharactersCount = [=] {
			return CommentBytes(field->getLastText());
		},
	};
	if (rightSkip > 0) {
		options.customUpdatePosition = [=](QSize parent, QSize label) {
			const auto &st = field->st();
			// Baseline alignment, the way the default position does it.
			const auto top = st.textMargins.top()
				+ st.style.font->ascent
				- limitSt.style.font->ascent;
			return QPoint(parent.width() - rightSkip - label.width(), top);
		};
	}
	Ui::AddLengthLimitLabel(field, kSendCommentMaxBytes, std::move(options));

	// A field's own maximum counts UTF-16 units, so it cannot enforce a
	// limit that counts bytes. What it can do is keep a paste from running
	// the counter off into the thousands. Two units per allowed byte leaves
	// every comment that fits typeable - the densest of them, all emoji or
	// all Cyrillic, is half that - and no single unit is worth more than
	// three bytes, so `deepest` is as far below zero as the counter can go.
	field->setMaxLength(kSendCommentMaxLength);
	const auto deepest = 3 * kSendCommentMaxLength - kSendCommentMaxBytes;
	const auto widest = limitSt.style.font->width(
		QChar(0x2212) + QString::number(deepest));
	field->setAdditionalMargins({
		0,
		0,
		std::max(rightSkip + widest - field->st().textMargins.right(), 0),
		0,
	});
}

struct SendQuoteDependencies {
	std::optional<TransferWalletIdentity> senderIdentity;
	GaslessTerms gaslessTerms;
	QString destination;
	SendComment comment;
	DeviceCustodyState custody;
	QByteArray recipientPublicKey;
	UserId userId;
	int64 amountNano = 0;
	int64 balanceNano = 0;
	int64 minTransferNano = 0;
	bool bounce = false;
	bool ready = false;
	bool valid = false;

	friend bool operator==(
		const SendQuoteDependencies &,
		const SendQuoteDependencies &) = default;
};

struct SendQuote {
	SendArgs args;
	SendQuoteDependencies dependencies;
	std::shared_ptr<const PreparedSend> prepared;
	int64 feeNano = 0;
	uint64 revision = 0;

	friend bool operator==(const SendQuote &, const SendQuote &) = default;
};

struct SendDraft {
	rpl::variable<SendComment> comment;
	// False once the recipient is known to take plain comments only. It lives
	// here because the comment editor outlives the box that opened it.
	rpl::variable<bool> encryptable = true;
	rpl::variable<std::optional<SendQuote>> quote;
	rpl::variable<bool> preparing = false;
	KeyAuthorization authorization;
	std::optional<quint32> privateEpoch;
};

struct SendFlow {
	QString destination;
	bool bounce = true;
	QString displayForm;
	int64 amountNano = 0;
	std::optional<uint64> expiresAt;
	std::shared_ptr<SendDraft> draft;
	QString tonName;
};

class SendCommentBubble final : public Ui::RpWidget {
public:
	SendCommentBubble(
		QWidget *parent,
		rpl::producer<SendComment> comment,
		rpl::producer<bool> clickable,
		Fn<void()> clicked);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void setText(const QString &text);

	const not_null<Ui::AbstractButton*> _button;
	Ui::Text::String _text = { 1 };
	Ui::UniqueGiftMessageBubble::Layout _layout;
	QPainterPath _path;
	bool _clickable = false;

};

[[nodiscard]] int AddressGroupsWidth(
	const style::font &font,
	const QString &address,
	int groupsPerLine);

void PaintAddressGroups(
	QPainter &p,
	const style::font &font,
	const QString &address,
	QPoint origin,
	int groupsPerLine);

class SendRecipientCard final : public Ui::RpWidget {
public:
	SendRecipientCard(
		QWidget *parent,
		std::shared_ptr<Ui::Show> show,
		UserData *user,
		rpl::producer<QString> address,
		Fn<void()> about);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	[[nodiscard]] int nameHeight() const;
	[[nodiscard]] QRect addressRect(int outerWidth) const;
	void setAddress(const QString &address);

	UserData * const _user = nullptr;
	Ui::IconButton * const _about = nullptr;
	const not_null<Ui::AbstractButton*> _copy;
	style::TextStyle _nameStyle;
	style::TextStyle _usernameStyle;
	Ui::PeerUserpicView _userpic;
	Ui::Text::String _name;
	Ui::Text::String _username;
	QString _address;

};

SendCommentBubble::SendCommentBubble(
	QWidget *parent,
	rpl::producer<SendComment> comment,
	rpl::producer<bool> clickable,
	Fn<void()> clicked)
: RpWidget(parent)
, _button(Ui::CreateChild<Ui::AbstractButton>(this)) {
	_button->setClickedCallback(std::move(clicked));
	_button->setPointerCursor(false);
	std::move(clickable) | rpl::on_next([=](bool value) {
		_clickable = value;
		_button->setPointerCursor(value);
		update();
	}, lifetime());
	std::move(comment) | rpl::map([](const SendComment &value) {
		return value.text;
	}) | rpl::distinct_until_changed() | rpl::on_next([this](
			const QString &text) {
		setText(text);
	}, lifetime());
}

int SendCommentBubble::resizeGetHeight(int newWidth) {
	if (!newWidth) {
		_button->setGeometry(QRect());
		return 0;
	}
	_layout = Ui::UniqueGiftMessageBubble::ResolveLayout(
		st::walletSendCommentBubble,
		style::margins(
			st::walletSendFieldMargin.left(),
			0,
			st::walletSendFieldMargin.right(),
			st::walletSendFieldMargin.bottom()),
		newWidth,
		_text);
	auto mirror = QTransform();
	mirror.translate(2 * QRectF(_layout.pathBounds).center().x(), 0.);
	mirror.scale(-1., 1.);
	_path = mirror.map(Ui::UniqueGiftMessageBubble::Path(
		st::walletSendCommentBubble,
		_layout));
	const auto stroke = st::lineWidth;
	_button->setGeometry(_path.boundingRect().toAlignedRect().marginsAdded(
		{ stroke, stroke, stroke, stroke }));
	const auto shift = -st::walletSendCommentBubble.tailSize.width();
	_layout.body.translate(shift, 0);
	_layout.text.translate(shift, 0);
	return _layout.sectionHeight;
}

void SendCommentBubble::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	{
		auto hq = PainterHighQualityEnabler(p);
		if (_clickable && _button->isOver()) {
			p.fillPath(_path, st::walletSendCommentBgOver);
		}
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(
			st::walletSendCommentOutline,
			st::lineWidth,
			Qt::SolidLine,
			Qt::RoundCap,
			Qt::RoundJoin));
		p.drawPath(_path);
	}
	p.setPen(st::walletSendCommentTextFg);
	_text.draw(p, {
		.position = _layout.text.topLeft(),
		.outerWidth = width(),
		.availableWidth = _layout.text.width(),
		.align = style::al_topleft,
		.elisionLines = 0,
	});
}

void SendCommentBubble::setText(const QString &text) {
	_text.setText(st::walletSendCommentTextStyle, text);
	if (width() > 0) {
		resizeToWidth(width());
	}
	update();
}

SendRecipientCard::SendRecipientCard(
	QWidget *parent,
	std::shared_ptr<Ui::Show> show,
	UserData *user,
	rpl::producer<QString> address,
	Fn<void()> about)
: RpWidget(parent)
, _user(user)
, _about(user
	? Ui::CreateChild<Ui::IconButton>(this, st::walletSendUserCardAbout)
	: nullptr)
, _copy(Ui::CreateChild<Ui::AbstractButton>(this))
, _nameStyle(st::defaultTextStyle)
, _usernameStyle(st::defaultTextStyle)
, _userpic(user ? user->createUserpicView() : Ui::PeerUserpicView()) {
	_nameStyle.font = st::walletSendUserCardNameFont;
	_usernameStyle.font = st::boxTextFont;
	if (_about) {
		_about->setClickedCallback(std::move(about));
		_about->hide();
	}
	_copy->setClickedCallback([this, show = std::move(show)] {
		CopyAddressCallback(show, _address)();
	});
	_copy->hide();

	auto name = user
		? Info::Profile::NameValue(user)
		: tr::lng_wallet_send_gram_wallet();
	std::move(name) | rpl::on_next([this](const QString &value) {
		_name.setText(_nameStyle, value, Ui::NameTextOptions());
		update();
	}, lifetime());

	if (user) {
		Info::Profile::UsernameValue(user) | rpl::on_next([this](
				const TextWithEntities &username) {
			_username.setText(
				_usernameStyle,
				username.text,
				Ui::NameTextOptions());
			update();
		}, lifetime());

		user->session().downloaderTaskFinished() | rpl::on_next([this] {
			update();
		}, lifetime());
	}

	std::move(address) | rpl::on_next([this](const QString &value) {
		setAddress(value);
	}, lifetime());
}

int SendRecipientCard::resizeGetHeight(int newWidth) {
	const auto &padding = st::walletSendUserCardPadding;
	const auto font = st::walletSendUserCardAddressFont->monospace();
	const auto lines = kAddressLength
		/ (kAddressGroup * kSendUserCardGroupsPerLine);
	const auto text = nameHeight()
		+ st::walletSendUserCardTextSkip
		+ lines * font->height;
	const auto inner = std::max(st::walletSendUserCardPhoto, text);
	if (_about) {
		_about->moveToRight(
			padding.right(),
			(padding.top() + inner + padding.bottom() - _about->height()) / 2,
			newWidth);
	}
	_copy->setGeometry(addressRect(newWidth));
	return padding.top() + inner + padding.bottom();
}

void SendRecipientCard::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::windowBgOver);
	p.drawRoundedRect(
		rect(),
		st::walletSendUserCardRadius,
		st::walletSendUserCardRadius);

	const auto &padding = st::walletSendUserCardPadding;
	const auto photo = st::walletSendUserCardPhoto;
	const auto inner = height() - padding.top() - padding.bottom();
	const auto photoTop = padding.top() + (inner - photo) / 2;
	if (_user) {
		_user->paintUserpicLeft(
			p,
			_userpic,
			padding.left(),
			photoTop,
			width(),
			photo);
	} else {
		Ui::EmptyUserpic::PaintCurrency(
			p,
			padding.left(),
			photoTop,
			width(),
			photo);
	}

	const auto left = padding.left()
		+ photo
		+ st::walletSendUserCardPhotoSkip;
	const auto about = _about
		? (_about->width() + st::walletSendUserCardAboutSkip)
		: 0;
	const auto available = width() - left - padding.right() - about;
	const auto skip = st::walletSendUserCardNameSkip;
	const auto handle = _username.isEmpty()
		? 0
		: std::min(_username.maxWidth(), (available - skip) / 2);
	const auto nameWidth = std::min(
		_name.maxWidth(),
		available - (handle ? (handle + skip) : 0));
	p.setPen(st::windowBoldFg);
	_name.drawLeftElided(p, left, padding.top(), nameWidth, width(), 1);
	if (handle) {
		p.setPen(st::windowSubTextFg);
		_username.drawLeftElided(
			p,
			left + nameWidth + skip,
			padding.top(),
			handle,
			width(),
			1);
	}
	if (!_address.isEmpty()) {
		PaintAddressGroups(
			p,
			st::walletSendUserCardAddressFont->monospace(),
			_address,
			addressRect(width()).topLeft(),
			kSendUserCardGroupsPerLine);
	}
}

int SendRecipientCard::nameHeight() const {
	const auto line = [](const style::TextStyle &text) {
		return text.lineHeight ? text.lineHeight : text.font->height;
	};
	return std::max(line(_nameStyle), line(_usernameStyle));
}

QRect SendRecipientCard::addressRect(int outerWidth) const {
	const auto &padding = st::walletSendUserCardPadding;
	const auto font = st::walletSendUserCardAddressFont->monospace();
	const auto left = padding.left()
		+ st::walletSendUserCardPhoto
		+ st::walletSendUserCardPhotoSkip;
	const auto top = padding.top()
		+ nameHeight()
		+ st::walletSendUserCardTextSkip;
	const auto blockWidth = AddressGroupsWidth(
		font,
		_address,
		kSendUserCardGroupsPerLine);
	const auto lines = kAddressLength
		/ (kAddressGroup * kSendUserCardGroupsPerLine);
	const auto x = style::RightToLeft()
		? (outerWidth - left - blockWidth)
		: left;
	return QRect(x, top, blockWidth, lines * font->height);
}

void SendRecipientCard::setAddress(const QString &address) {
	_address = address;
	if (_about) {
		_about->setVisible(!_address.isEmpty());
	}
	_copy->setGeometry(addressRect(width()));
	_copy->setVisible(!_address.isEmpty());
	update();
}

enum class KeyActionKind {
	Plain,
	Reveal,
	ResumeAfterRestore,
};

// The one ladder every key-requiring wallet action climbs: the action runs
// at once when this device holds the key, after the backup restore and its
// protection chooser when the key is only in the cloud, after the phrase
// import when there is no backup, and after the conflict is resolved when a
// different wallet is parked here. A context turns the ladder's prompts into
// one attempt that ends when any of them closes without a key.
void RunKeyRequiringAction(
	std::shared_ptr<Main::SessionShow> show,
	Fn<void()> action,
	KeyActionKind kind = KeyActionKind::Plain,
	std::shared_ptr<KeyContext> context = nullptr,
	rpl::producer<QString> importAbout = nullptr);

void WalletConflictBox(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<Main::SessionShow> show,
	Fn<void()> switched);

[[nodiscard]] std::optional<SendFlow> ParseRecipientFlow(
		const QString &text) {
	auto address = text;
	auto amountNano = int64(0);
	auto comment = QString();
	auto expiresAt = std::optional<uint64>();
	if (const auto link = ParseTransferLink(text)) {
		address = link->address;
		amountNano = link->amountNano;
		comment = link->comment;
		expiresAt = link->expiresAt;
	}
	const auto parsed = ParseAddress(address);
	if (!parsed || parsed->testnet) {
		return std::nullopt;
	}
	const auto friendly = FormatFriendly(parsed->raw, parsed->bounceable);
	if (friendly.isEmpty()) {
		return std::nullopt;
	}
	const auto draft = std::make_shared<SendDraft>();
	draft->comment = SendComment{ .text = std::move(comment) };
	return SendFlow{
		.destination = parsed->raw,
		.bounce = parsed->bounceable,
		.displayForm = (parsed->friendly ? address : friendly),
		.amountNano = amountNano,
		.expiresAt = expiresAt,
		.draft = draft,
	};
}

[[nodiscard]] QStringList SplitPhraseWords(const QString &text) {
	return text.simplified().split(QChar(' '), Qt::SkipEmptyParts);
}

enum class RecipientInputKind : uchar {
	Empty,
	Address,
	Name,
	Invalid,
	Search,
};

struct RecipientInput {
	RecipientInputKind kind = RecipientInputKind::Empty;
	std::optional<SendFlow> flow;
};

enum class RecipientError : uchar {
	Invalid,
	NameNotFound,
	NameFailed,
	LookupFailed,
	OwnWallet,
};

[[nodiscard]] rpl::producer<QString> RecipientErrorText(
		RecipientError error) {
	switch (error) {
	case RecipientError::Invalid:
		return tr::lng_wallet_send_invalid_address();
	case RecipientError::NameNotFound:
		return tr::lng_wallet_send_name_not_found();
	case RecipientError::NameFailed:
	case RecipientError::LookupFailed:
		return tr::lng_wallet_send_user_load_error();
	case RecipientError::OwnWallet:
		return tr::lng_wallet_collectible_own_wallet();
	}
	Unexpected("RecipientError in RecipientErrorText.");
}

[[nodiscard]] bool IsTonDnsName(const QString &text) {
	if (text.isEmpty() || text.size() > 126) {
		return false;
	}
	const auto parts = text.split(QChar('.'));
	if (parts.size() < 2
		|| parts.back().compare(u"ton"_q, Qt::CaseInsensitive) != 0) {
		return false;
	}
	for (const auto &part : parts) {
		if (part.isEmpty()) {
			return false;
		}
		for (const auto ch : part) {
			const auto code = ch.unicode();
			const auto good = (code >= 'a' && code <= 'z')
				|| (code >= 'A' && code <= 'Z')
				|| (code >= '0' && code <= '9')
				|| (code == '-');
			if (!good) {
				return false;
			}
		}
	}
	return true;
}

[[nodiscard]] RecipientInput ClassifyRecipientInput(const QString &text) {
	using Kind = RecipientInputKind;
	if (text.isEmpty()) {
		return {};
	} else if (auto flow = ParseRecipientFlow(text)) {
		return { .kind = Kind::Address, .flow = std::move(flow) };
	} else if (IsTonDnsName(text)) {
		return { .kind = Kind::Name };
	} else if (text.contains(u"://"_q)
		|| text.startsWith(u"ton:"_q, Qt::CaseInsensitive)
		|| ParseAddress(text)
		|| ParseTransferLink(text)
		|| (text.size() > kRecipientSearchLimit)
		|| (SplitPhraseWords(text).size() >= kImportWordCountShort)) {
		// Rejected addresses and pasted secrets never reach contacts.search.
		return { .kind = Kind::Invalid };
	} else if (TextUtilities::PrepareSearchWords(text).isEmpty()) {
		return {};
	}
	return { .kind = Kind::Search };
}

[[nodiscard]] not_null<Ui::InputField*> AddCommentField(
		not_null<Ui::GenericBox*> box,
		const QString &comment) {
	const auto field = box->addRow(
		object_ptr<Ui::InputField>(
			box,
			st::walletCommentField,
			Ui::InputField::Mode::NoNewlines,
			tr::lng_wallet_send_comment_optional(),
			comment),
		st::walletCommentFieldMargin);
	ApplyCommentLimit(field);
	return field;
}

[[nodiscard]] not_null<Ui::InputField*> AddSendField(
		not_null<Ui::VerticalLayout*> container,
		const style::InputField &st,
		rpl::producer<QString> placeholder,
		const QString &value) {
	const auto field = container->add(
		object_ptr<Ui::InputField>(
			container,
			st,
			Ui::InputField::Mode::NoNewlines,
			std::move(placeholder),
			value),
		st::walletSendFieldMargin);
	const auto paste = Ui::CreateChild<Ui::RoundButton>(
		field,
		tr::lng_mac_menu_paste(),
		st::defaultTableSmallButton);
	paste->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	paste->setClickedCallback([=] {
		field->setFocusFast();
		field->setText(QGuiApplication::clipboard()->text().trimmed());
	});
	field->widthValue(
	) | rpl::on_next([=, &st](int) {
		paste->moveToRight(0, st.textMargins.top());
	}, paste->lifetime());
	const auto updatePaste = [=] {
		paste->setVisible(field->getLastText().isEmpty());
	};
	field->changes() | rpl::on_next(updatePaste, field->lifetime());
	updatePaste();
	return field;
}

void BindCommentField(
		not_null<Ui::InputField*> field,
		const std::shared_ptr<SendDraft> &draft) {
	field->changes() | rpl::on_next([=] {
		auto comment = draft->comment.current();
		comment.text = field->getLastText();
		draft->comment = std::move(comment);
	}, field->lifetime());
	draft->comment.value() | rpl::on_next([=](const SendComment &comment) {
		if (field->getLastText() != comment.text) {
			field->setText(comment.text);
		}
		if (!CommentFits(comment.text)) {
			field->showError();
		}
	}, field->lifetime());
}

void AddCommentPrivacy(
		not_null<Ui::VerticalLayout*> container,
		const std::shared_ptr<SendDraft> &draft,
		const style::margins &margin,
		rpl::producer<bool> encryptable) {
	const auto choice = container->add(
		object_ptr<Ui::SlideWrap<Ui::Checkbox>>(
			container,
			object_ptr<Ui::Checkbox>(
				container,
				tr::lng_wallet_comment_make_public(),
				draft->comment.current().isPublic,
				st::defaultBoxCheckbox),
			margin));
	choice->toggleOn(std::move(encryptable));
	choice->finishAnimating();
	const auto checkbox = choice->entity();
	checkbox->setAllowTextLines(0);
	checkbox->checkedChanges() | rpl::on_next([=](bool checked) {
		auto comment = draft->comment.current();
		comment.isPublic = checked;
		draft->comment = std::move(comment);
	}, checkbox->lifetime());
	draft->comment.value() | rpl::on_next([=](const SendComment &comment) {
		checkbox->setChecked(
			comment.isPublic,
			Ui::Checkbox::NotifyAboutChange::DontNotify);
	}, checkbox->lifetime());
	const auto warning = container->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			container,
			object_ptr<Ui::FlatLabel>(
				container,
				tr::lng_wallet_comment_public(),
				st::walletCommentCaptionLabel),
			st::walletCommentCaptionMargin));
	warning->toggleOn(draft->comment.value() | rpl::map([](
			const SendComment &comment) {
		return comment.isPublic;
	}));
	warning->finishAnimating();
}

void WalletSendCommentBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<SendDraft> draft,
		Fn<bool()> originValid,
		rpl::producer<bool> encryptable) {
	box->setWidth(st::boxWideWidth);
	box->setTitle(tr::lng_wallet_comment_title());
	const auto staged = std::make_shared<SendDraft>();
	staged->comment = draft->comment.current();
	const auto field = AddCommentField(box, staged->comment.current().text);
	BindCommentField(field, staged);
	AddCommentPrivacy(
		box->verticalLayout(),
		staged,
		st::walletCommentPrivacyMargin,
		std::move(encryptable));

	struct State {
		bool closed = false;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto weak = base::make_weak(box.get());
	box->boxClosing() | rpl::on_next([=] {
		state->closed = true;
	}, box->lifetime());
	const auto save = [=] {
		if (state->closed || !originValid()) {
			return;
		} else if (!CommentFits(staged->comment.current().text)) {
			field->showError();
			field->setFocusFast();
			return;
		}
		state->closed = true;
		auto comment = staged->comment.current();
		if (!draft->encryptable.current()) {
			comment.isPublic = true;
		}
		draft->comment = std::move(comment);
		if (const auto alive = weak.get()) {
			alive->closeBox();
		}
	};
	box->addButton(tr::lng_wallet_comment_add(), save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	field->submits() | rpl::on_next(save, field->lifetime());
	box->setFocusCallback([=] { field->setFocusFast(); });
	AddBoxCloseButton(box);
}

void ShowKeyChangedBox(
		std::shared_ptr<Main::SessionShow> show,
		v::text::data text) {
	show->showBox(Ui::MakeConfirmBox({
		.text = std::move(text),
		.confirmed = [=](Fn<void()> close) {
			close();
			RunKeyRequiringAction(show, [] {});
		},
		.confirmText = tr::lng_wallet_restore_title(),
		.title = tr::lng_wallet_send_key_changed_title(),
	}));
}

} // namespace

QString SendErrorText(SendError error, int64 minTransferNano) {
	switch (error) {
	case SendError::None:
	case SendError::Silent:
	case SendError::SubmissionUnknown:
		return QString();
	case SendError::AmountTooSmall:
		return tr::lng_wallet_send_error_too_small(
			tr::now,
			lt_amount,
			Ui::FormatTonAmount(minTransferNano).full);
	case SendError::CommentTooLong:
		return tr::lng_wallet_comment_too_long(tr::now);
	case SendError::CommentEncryptionUnavailable:
		return tr::lng_wallet_comment_encryption_failed(tr::now);
	// A balance that covers the amount but not the fee is the same problem
	// to the sender as one that covers neither, and one sentence says it.
	case SendError::InsufficientBalance:
	case SendError::InsufficientFees:
		return tr::lng_wallet_send_error_insufficient(tr::now);
	case SendError::PreviousUnresolved:
	case SendError::AlreadySending:
		return tr::lng_wallet_send_error_in_progress(tr::now);
	case SendError::SigningUnavailable:
		return tr::lng_wallet_readonly_bar(tr::now);
	case SendError::Locked:
		return tr::lng_wallet_vault_locked(tr::now);
	case SendError::InvalidRequest:
	case SendError::Failed:
	case SendError::KeyMismatch:
	case SendError::Rejected:
	case SendError::DataInvalid:
	case SendError::CollectibleRejected:
		return tr::lng_wallet_send_error_failed(tr::now);
	case SendError::CollectibleUnavailable:
		return tr::lng_wallet_collectible_not_owned(tr::now);
	case SendError::KeyChanged:
		return tr::lng_wallet_send_key_changed_text(tr::now);
	case SendError::QuoteExpired:
		return tr::lng_wallet_send_error_quote_expired(tr::now);
	case SendError::LinkExpired:
		return tr::lng_wallet_send_link_expired(tr::now);
	}
	Unexpected("Error value in SendErrorText.");
}

void ShowWalletKeyChanged(std::shared_ptr<Main::SessionShow> show) {
	ShowKeyChangedBox(show, tr::lng_wallet_send_key_changed_text());
}

QString ErrorWithType(const QString &message, const QString &error) {
	return error.isEmpty()
		? message
		: tr::lng_wallet_error_with_type(
			tr::now,
			lt_message,
			message,
			lt_error,
			error);
}

namespace {

[[nodiscard]] QString SendUserLoadErrorText(const QString &error) {
	if (error == u"WALLET_UNAVAILABLE"_q) {
		return tr::lng_wallet_unavailable(tr::now);
	} else if (error == u"WALLET_NOT_READY"_q) {
		return tr::lng_wallet_state_error(tr::now);
	} else if (error == u"WALLET_BALANCE_EMPTY"_q) {
		return tr::lng_wallet_send_error_insufficient(tr::now);
	} else if (error == u"WALLET_USER_INVALID"_q
		|| error == u"WALLET_USER_INELIGIBLE"_q) {
		return tr::lng_wallet_send_user_unavailable(tr::now);
	} else if (error == u"WALLET_ADDRESS_INVALID"_q) {
		return tr::lng_wallet_send_user_load_error(tr::now);
	}
	return ErrorWithType(tr::lng_wallet_state_error(tr::now), error);
}

// A button that cannot be pressed yet keeps the background its own style
// gives it and fades its label halfway into that background, which is how the
// rest of the app shows a disabled button. The colors come from the button's
// own style, so an attention or light button fades into its own background
// instead of an active button's.
void ApplyButtonDisabledLook(not_null<Ui::RoundButton*> button) {
	const auto color = [&]() -> std::optional<QColor> {
		if (!button->isDisabled()) {
			return std::nullopt;
		}
		const auto scope = WindowPaletteScope(button);
		const auto &buttonStyle = button->st();
		return anim::color(buttonStyle.textBg, buttonStyle.textFg, 0.5);
	}();
	button->setTextFgOverride(color);
}

void SetButtonDisabledLook(
		not_null<Ui::RoundButton*> button,
		bool disabled) {
	if (disabled) {
		button->clearState();
	}
	button->setDisabled(disabled);
	button->setAttribute(Qt::WA_TransparentForMouseEvents, disabled);
	ApplyButtonDisabledLook(button);
	if (!button->property("walletDisabledLook").toBool()) {
		button->setProperty("walletDisabledLook", true);
		style::PaletteChanged() | rpl::on_next([=] {
			ApplyButtonDisabledLook(button);
		}, button->lifetime());
	}
}

// The send box offers to fund an empty wallet, so the balance never hides one.
[[nodiscard]] bool CanSendToUser(
		not_null<Main::Session*> session,
		UserId id) {
	const auto error = session->wallet().userAddresses().forceResolveError(id);
	return error.isEmpty() || (error == u"WALLET_BALANCE_EMPTY"_q);
}

[[nodiscard]] UserData *SendableUser(
		not_null<Main::Session*> session,
		UserId id) {
	const auto user = id ? session->data().userLoaded(id) : nullptr;
	if (!user || !CanSendToUser(session, id)) {
		return nullptr;
	}
	return user;
}

[[nodiscard]] bool SendsToOwnWallet(
		not_null<Main::Session*> session,
		const QString &destination) {
	const auto identity = session->wallet().transferWalletIdentity();
	return identity
		&& !destination.isEmpty()
		&& (CanonicalAddress(destination) == identity->address);
}

void ChooseMoneyRecipient(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		not_null<UserData*> user) {
	ShowSendToUser(show, user, nullptr, 0, nullptr, box.get());
}

class RecentMoneyRecipientsController final
	: public PeerListController
	, public base::has_weak_ptr {
public:
	RecentMoneyRecipientsController(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		Fn<void(not_null<UserData*>)> choose);

	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	Main::Session &session() const override;

	void setContent(not_null<PeerListContent*> content);
	void clear();
	[[nodiscard]] rpl::producer<bool> shownValue() const;

private:
	[[nodiscard]] bool active() const;
	[[nodiscard]] bool canOffer(not_null<UserData*> user) const;
	void fillIfEmpty();
	void refresh();
	void scheduleRefresh();
	void watchUsers();

	const base::weak_qptr<Ui::GenericBox> _box;
	const std::shared_ptr<Main::SessionShow> _show;
	const base::weak_ptr<Main::Session> _session;
	const Fn<void(not_null<UserData*>)> _choose;
	PeerListContentDelegateSimple _delegate;
	std::vector<not_null<UserData*>> _users;
	rpl::lifetime _userLifetime;
	rpl::variable<bool> _shown = false;
	bool _closed = false;
	bool _refreshQueued = false;

};

RecentMoneyRecipientsController::RecentMoneyRecipientsController(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<Main::SessionShow> show,
	Fn<void(not_null<UserData*>)> choose)
: _box(box)
, _show(std::move(show))
, _session(&_show->session())
, _choose(std::move(choose)) {
}

void RecentMoneyRecipientsController::setContent(
		not_null<PeerListContent*> content) {
	_delegate.setContent(content);
	setDelegate(&_delegate);
}

void RecentMoneyRecipientsController::prepare() {
	_box->boxClosing() | rpl::on_next([=] {
		_closed = true;
		_shown = false;
	}, lifetime());
	_session->recentMoneyRecipients().updates() | rpl::on_next([=] {
		refresh();
	}, lifetime());
	rpl::merge(
		_session->recentPeers().updates(),
		_session->topPeers().updates()
	) | rpl::on_next([=] {
		fillIfEmpty();
	}, lifetime());
	const auto schedule = [=] { scheduleRefresh(); };
	const auto &wallet = _session->wallet();
	wallet.stateKnownValue() | rpl::skip(1) | rpl::on_next(
		schedule,
		lifetime());
	wallet.balanceNanoValue() | rpl::skip(1) | rpl::on_next(
		schedule,
		lifetime());
	_session->wallet().userAddresses().unavailableValue(
	) | rpl::skip(1) | rpl::on_next(schedule, lifetime());
	fillIfEmpty();
	refresh();
}

bool RecentMoneyRecipientsController::active() const {
	return _box
		&& !_closed
		&& _session
		&& _show->valid()
		&& &_show->session() == _session.get();
}

bool RecentMoneyRecipientsController::canOffer(
		not_null<UserData*> user) const {
	return active()
		&& &user->session() == _session.get()
		&& !user->isSelf() // the list a Clear leaves behind
		&& SendableUser(_session.get(), peerToUser(user->id)) == user;
}

void RecentMoneyRecipientsController::fillIfEmpty() {
	if (!active()) {
		return;
	}
	const auto eligible = [=](not_null<UserData*> user) {
		return canOffer(user);
	};
	_session->recentMoneyRecipients().fillIfEmpty(eligible);
}

void RecentMoneyRecipientsController::watchUsers() {
	_userLifetime.destroy();
	for (const auto &user : _users) {
		user->flagsValue() | rpl::skip(1) | rpl::on_next([=] {
			scheduleRefresh();
		}, _userLifetime);
		using Flag = Data::PeerUpdate::Flag;
		_session->changes().peerUpdates(
			user,
			Flag::FullInfo | Flag::SupportInfo | Flag::OnlineStatus
		) | rpl::on_next([=](const Data::PeerUpdate &update) {
			if (!active()) {
				return;
			}
			if (update.flags & Flag::OnlineStatus) {
				if (const auto row = delegate()->peerListFindRow(user->id.value)) {
					row->refreshStatus();
					delegate()->peerListUpdateRow(row);
				}
			}
			if (update.flags & (Flag::FullInfo | Flag::SupportInfo)) {
				scheduleRefresh();
			}
		}, _userLifetime);
	}
}

void RecentMoneyRecipientsController::refresh() {
	if (!active()) {
		_shown = false;
		return;
	}
	const auto &users = _session->recentMoneyRecipients().list();
	if (_users != users) {
		_users = users;
		watchUsers();
	}
	auto rows = std::vector<not_null<UserData*>>();
	rows.reserve(_users.size());
	for (const auto &user : _users) {
		if (canOffer(user)) {
			rows.push_back(user);
		}
	}
	const auto count = delegate()->peerListFullRowsCount();
	auto changed = (count != int(rows.size()));
	for (auto i = 0; !changed && i != count; ++i) {
		changed = (delegate()->peerListRowAt(i)->peer() != rows[i]);
	}
	if (changed) {
		while (delegate()->peerListFullRowsCount()) {
			delegate()->peerListRemoveRow(delegate()->peerListRowAt(0));
		}
		for (const auto &user : rows) {
			delegate()->peerListAppendRow(std::make_unique<PeerListRow>(user));
		}
		delegate()->peerListRefreshRows();
	}
	_shown = !rows.empty();
}

void RecentMoneyRecipientsController::scheduleRefresh() {
	if (!active() || _refreshQueued) {
		return;
	}
	_refreshQueued = true;
	crl::on_main(this, [=] {
		_refreshQueued = false;
		fillIfEmpty();
		refresh();
	});
}

void RecentMoneyRecipientsController::rowClicked(
		not_null<PeerListRow*> row) {
	if (!active()) {
		return;
	}
	const auto user = row->peer()->asUser();
	if (!user || !canOffer(user)) {
		return;
	}
	_choose(user);
}

Main::Session &RecentMoneyRecipientsController::session() const {
	return *_session;
}

void RecentMoneyRecipientsController::clear() {
	if (!active()) {
		return;
	}
	const auto session = _session;
	session->recentMoneyRecipients().clear();
	if (session) {
		session->local().writeSearchSuggestionsIfNeeded();
	}
}

rpl::producer<bool> RecentMoneyRecipientsController::shownValue() const {
	return _shown.value();
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakeRecentMoneyRecipientsList(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		rpl::producer<bool> hidden,
		Fn<void(not_null<UserData*>)> choose) {
	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		box,
		object_ptr<Ui::VerticalLayout>(box));
	const auto wrap = result.data();
	const auto container = wrap->entity();
	const auto controller = container->lifetime().make_state<
		RecentMoneyRecipientsController>(
			box,
			std::move(show),
			std::move(choose));

	const auto header = container->add(object_ptr<Ui::RpWidget>(container));
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		header,
		tr::lng_recent_title(),
		st::windowFilterChatsSectionSubtitle);
	const auto clear = Ui::CreateChild<Ui::LinkButton>(
		header,
		QString(),
		st::boxLinkButton);
	tr::lng_recent_clear() | rpl::on_next([=](const QString &text) {
		clear->setText(text);
	}, clear->lifetime());
	clear->setClickedCallback([=] { controller->clear(); });
	rpl::combine(
		header->widthValue(),
		clear->naturalWidthValue(),
		label->heightValue()
	) | rpl::on_next([=](int width, int, int) {
		const auto &padding = st::walletSendRecentHeaderPadding;
		const auto available = std::max(
			width - padding.left() - padding.right(),
			0);
		clear->resizeToNaturalWidth(available);
		label->resizeToWidth(std::max(
			available - clear->width() - st::walletSendFieldMargin.bottom(),
			0));
		const auto height = std::max(
			st::windowFilterChatsSectionSubtitleHeight,
			std::max(label->height(), clear->height())
				+ padding.top()
				+ padding.bottom());
		header->resize(width, height);
		label->moveToLeft(padding.left(), (height - label->height()) / 2);
		clear->moveToRight(padding.right(), (height - clear->height()) / 2);
	}, header->lifetime());
	header->paintRequest() | rpl::on_next([=](QRect clip) {
		QPainter(header).fillRect(clip, st::searchedBarBg);
	}, header->lifetime());

	Ui::AddSkip(container, st::walletSendRecentListTopSkip);
	controller->setStyleOverrides(&st::peerListSingleRow);
	const auto content = container->add(
		object_ptr<PeerListContent>(container, controller));
	controller->setContent(content);
	Ui::AddSkip(container, st::walletSendRecentListSkip);
	const auto wasHidden = wrap->lifetime().make_state<bool>(false);
	rpl::combine(
		controller->shownValue(),
		std::move(hidden)
	) | rpl::on_next([=](bool shown, bool hide) {
		const auto animated = (hide != *wasHidden)
			? anim::type::instant
			: anim::type::normal;
		*wasHidden = hide;
		wrap->toggle(shown && !hide, animated);
	}, wrap->lifetime());
	wrap->finishAnimating();
	return result;
}

class MoneyRecipientSearchController final
	: public ChatsListBoxController {
public:
	MoneyRecipientSearchController(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		Fn<void(not_null<UserData*>)> choose);

	Main::Session &session() const override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void setContent(not_null<PeerListContent*> content);

protected:
	std::unique_ptr<Row> createRow(not_null<History*> history) override;
	void prepareViewHook() override;

private:
	[[nodiscard]] UserData *offered(not_null<PeerData*> peer) const;

	const base::weak_qptr<Ui::GenericBox> _box;
	const std::shared_ptr<Main::SessionShow> _show;
	const not_null<Main::Session*> _session;
	const Fn<void(not_null<UserData*>)> _choose;
	PeerListContentDelegateShow _delegate;
	bool _closed = false;

};

MoneyRecipientSearchController::MoneyRecipientSearchController(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<Main::SessionShow> show,
	Fn<void(not_null<UserData*>)> choose)
: ChatsListBoxController(&show->session())
, _box(box)
, _show(std::move(show))
, _session(&_show->session())
, _choose(std::move(choose))
, _delegate(_show) {
}

Main::Session &MoneyRecipientSearchController::session() const {
	return *_session;
}

void MoneyRecipientSearchController::setContent(
		not_null<PeerListContent*> content) {
	_delegate.setContent(content);
	setDelegate(&_delegate);
}

void MoneyRecipientSearchController::prepareViewHook() {
	_box->boxClosing() | rpl::on_next([=] {
		_closed = true;
		// WHY: the list outlives boxClosing by the close animation, so drop
		// the pending global search now or its late answer fills the list.
		search(QString());
	}, lifetime());
}

UserData *MoneyRecipientSearchController::offered(
		not_null<PeerData*> peer) const {
	const auto user = peer->asUser();
	return (user
		&& &user->session() == _session
		&& SendableUser(_session, peerToUser(user->id)) == user)
		? user
		: nullptr;
}

auto MoneyRecipientSearchController::createRow(not_null<History*> history)
-> std::unique_ptr<Row> {
	return offered(history->peer)
		? std::make_unique<Row>(history)
		: nullptr;
}

void MoneyRecipientSearchController::rowClicked(
		not_null<PeerListRow*> row) {
	if (!_box
		|| _closed
		|| !_show->valid()
		|| &_show->session() != _session) {
		return;
	}
	const auto user = offered(row->peer());
	if (!user) {
		return;
	}
	_choose(user);
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakeMoneyRecipientSearchList(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		rpl::producer<QString> query,
		Fn<void(not_null<UserData*>)> choose) {
	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		box,
		object_ptr<Ui::VerticalLayout>(box));
	const auto wrap = result.data();
	const auto container = wrap->entity();
	const auto controller = container->lifetime().make_state<
		MoneyRecipientSearchController>(
			box,
			std::move(show),
			std::move(choose));

	Ui::AddSkip(container, st::walletSendRecentListTopSkip);
	controller->setStyleOverrides(&st::peerListSingleRow);
	const auto content = container->add(
		object_ptr<PeerListContent>(container, controller));
	controller->setContent(content);
	Ui::AddSkip(container, st::walletSendRecentListSkip);
	std::move(query) | rpl::on_next([=](const QString &text) {
		wrap->toggle(!text.isEmpty(), anim::type::instant);
		content->searchQueryChanged(text);
	}, wrap->lifetime());
	wrap->finishAnimating();
	return result;
}

enum class TonNameStatus : uchar {
	None,
	Pending,
	Resolved,
	NotFound,
	Failed,
};

struct TonNameState {
	QString name;
	QString address;
	QString displayForm;
	TonNameStatus status = TonNameStatus::None;

	friend bool operator==(
		const TonNameState &,
		const TonNameState &) = default;
};

[[nodiscard]] bool TonNameRowShown(const TonNameState &state) {
	return (state.status == TonNameStatus::Pending)
		|| (state.status == TonNameStatus::Resolved);
}

class TonNameResultRow final : public PeerListRow {
public:
	TonNameResultRow(const QString &name, const QString &address);

	QString generateName() override;
	QString generateShortName() override;
	PaintRoundImageCallback generatePaintUserpicCallback(
		bool forceRound) override;
	void paintStatusText(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int availableWidth,
		int outerWidth,
		bool selected) override;

private:
	const QString _name;
	const QString _address;

};

class TonNameResultController final : public PeerListController {
public:
	TonNameResultController(
		not_null<Main::Session*> session,
		Fn<void()> chosen);

	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	Main::Session &session() const override;

	void setContent(not_null<PeerListContent*> content);
	void showState(const TonNameState &state);

private:
	const not_null<Main::Session*> _session;
	const Fn<void()> _chosen;
	PeerListContentDelegateSimple _delegate;

};

// WHY: engine jobs run one at a time and never coalesce, so one lookup
// stays out and the latest name is asked as soon as it returns.
class TonNameLookup final : public base::has_weak_ptr {
public:
	explicit TonNameLookup(not_null<Main::Session*> session);

	void setName(const QString &name);
	void request();
	void close();

	[[nodiscard]] const TonNameState &current() const;
	[[nodiscard]] rpl::producer<TonNameState> value() const;

private:
	void start();
	void issue();
	[[nodiscard]] bool settle(uint64 revision, bool busy);
	void resolved(uint64 revision, std::optional<QString> address);
	void failed(uint64 revision, DnsLookupError error);
	void finish(TonNameStatus status);

	const base::weak_ptr<Main::Session> _session;
	rpl::variable<TonNameState> _state;
	base::Timer _timer;
	base::Timer _deadline;
	uint64 _revision = 0;
	bool _waiting = false;
	bool _inFlight = false;
	bool _closed = false;

};

TonNameResultRow::TonNameResultRow(
	const QString &name,
	const QString &address)
: PeerListRow(PeerListRowId(1))
, _name(name)
, _address(address) {
	if (_address.isEmpty()) {
		setDisabledState(State::Disabled);
	}
}

QString TonNameResultRow::generateName() {
	return _name;
}

QString TonNameResultRow::generateShortName() {
	return _name;
}

PaintRoundImageCallback TonNameResultRow::generatePaintUserpicCallback(
		bool forceRound) {
	return [](Painter &p, int x, int y, int outerWidth, int size) {
		Ui::EmptyUserpic::PaintCurrency(p, x, y, outerWidth, size);
	};
}

void TonNameResultRow::paintStatusText(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int availableWidth,
		int outerWidth,
		bool selected) {
	const auto &font = st::contactsStatusFont;
	const auto text = _address.isEmpty()
		? font->elided(tr::lng_contacts_loading(tr::now), availableWidth)
		: font->elided(_address, availableWidth, Qt::ElideMiddle);
	p.setFont(font);
	p.setPen(selected ? st.statusFgOver : st.statusFg);
	p.drawTextLeft(x, y, outerWidth, text);
}

TonNameResultController::TonNameResultController(
	not_null<Main::Session*> session,
	Fn<void()> chosen)
: _session(session)
, _chosen(std::move(chosen)) {
}

void TonNameResultController::prepare() {
}

void TonNameResultController::rowClicked(not_null<PeerListRow*> row) {
	if (_chosen) {
		_chosen();
	}
}

Main::Session &TonNameResultController::session() const {
	return *_session;
}

void TonNameResultController::setContent(
		not_null<PeerListContent*> content) {
	_delegate.setContent(content);
	setDelegate(&_delegate);
}

void TonNameResultController::showState(const TonNameState &state) {
	while (delegate()->peerListFullRowsCount()) {
		delegate()->peerListRemoveRow(delegate()->peerListRowAt(0));
	}
	if (TonNameRowShown(state)) {
		const auto resolved = (state.status == TonNameStatus::Resolved);
		delegate()->peerListAppendRow(std::make_unique<TonNameResultRow>(
			state.name,
			resolved ? state.displayForm : QString()));
	}
	delegate()->peerListRefreshRows();
}

TonNameLookup::TonNameLookup(not_null<Main::Session*> session)
: _session(session)
, _timer([this] { start(); })
, _deadline([this] {
	if (_waiting) {
		finish(TonNameStatus::Failed);
	}
}) {
}

void TonNameLookup::setName(const QString &name) {
	++_revision;
	_waiting = false;
	_timer.cancel();
	_deadline.cancel();
	if (name.isEmpty()) {
		_state = TonNameState();
		return;
	}
	_timer.callOnce(AutoSearchTimeout);
	_state = TonNameState{ .name = name, .status = TonNameStatus::Pending };
}

void TonNameLookup::request() {
	_timer.cancel();
	start();
}

void TonNameLookup::close() {
	_closed = true;
	++_revision;
	_waiting = false;
	_timer.cancel();
	_deadline.cancel();
}

const TonNameState &TonNameLookup::current() const {
	return _state.current();
}

rpl::producer<TonNameState> TonNameLookup::value() const {
	return _state.value();
}

void TonNameLookup::start() {
	const auto name = _state.current().name;
	if (_closed || name.isEmpty()) {
		return;
	}
	if (!_waiting) {
		_waiting = true;
		_deadline.callOnce(kSendUserLoadTimeout);
		_state = TonNameState{
			.name = name,
			.status = TonNameStatus::Pending,
		};
	}
	issue();
}

void TonNameLookup::issue() {
	const auto session = _session.get();
	if (_closed || !_waiting || _inFlight || !session) {
		return;
	}
	_timer.cancel();
	_inFlight = true;
	const auto revision = _revision;
	session->wallet().resolveDnsName(
		_state.current().name,
		crl::guard(this, [=, this](std::optional<QString> address) {
			resolved(revision, std::move(address));
		}),
		crl::guard(this, [=, this](DnsLookupError error) {
			failed(revision, error);
		}));
}

bool TonNameLookup::settle(uint64 revision, bool busy) {
	_inFlight = false;
	if (_closed || !_waiting) {
		return false;
	} else if (busy) {
		_timer.callOnce(kNameBusyRetryDelay);
		return false;
	} else if (revision != _revision) {
		issue();
		return false;
	}
	return true;
}

void TonNameLookup::resolved(
		uint64 revision,
		std::optional<QString> address) {
	if (!settle(revision, false)) {
		return;
	} else if (!address) {
		finish(TonNameStatus::NotFound);
		return;
	}
	const auto flow = ParseRecipientFlow(*address);
	if (!flow) {
		finish(TonNameStatus::Failed);
		return;
	}
	_waiting = false;
	_timer.cancel();
	_deadline.cancel();
	_state = TonNameState{
		.name = _state.current().name,
		.address = *address,
		.displayForm = flow->displayForm,
		.status = TonNameStatus::Resolved,
	};
}

void TonNameLookup::failed(uint64 revision, DnsLookupError error) {
	if (settle(revision, (error == DnsLookupError::Busy))) {
		finish(TonNameStatus::Failed);
	}
}

void TonNameLookup::finish(TonNameStatus status) {
	_waiting = false;
	_timer.cancel();
	_deadline.cancel();
	_state = TonNameState{ .name = _state.current().name, .status = status };
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakeTonNameResultList(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		rpl::producer<TonNameState> state,
		Fn<void()> chosen) {
	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		box,
		object_ptr<Ui::VerticalLayout>(box));
	const auto wrap = result.data();
	const auto container = wrap->entity();
	const auto controller = container->lifetime().make_state<
		TonNameResultController>(session, std::move(chosen));

	Ui::AddSkip(container, st::walletSendRecentListTopSkip);
	controller->setStyleOverrides(&st::peerListSingleRow);
	const auto content = container->add(
		object_ptr<PeerListContent>(container, controller));
	controller->setContent(content);
	Ui::AddSkip(container, st::walletSendRecentListSkip);
	std::move(state) | rpl::on_next([=](const TonNameState &value) {
		controller->showState(value);
		wrap->toggle(TonNameRowShown(value), anim::type::instant);
	}, wrap->lifetime());
	wrap->finishAnimating();
	return result;
}

[[nodiscard]] rpl::producer<TextWithEntities> SendRecipientTitle(
		not_null<Ui::GenericBox*> box,
		const QString &recipient,
		Qt::TextElideMode mode) {
	return rpl::combine(
		box->widthValue(),
		rpl::single(rpl::empty) | rpl::then(Lang::Updated())
	) | rpl::map([=](int width, rpl::empty_value) {
		const auto available = width
			- 2 * st::boxTitlePosition.x()
			- st::boxTitleClose.width
			- st::boxTitleMenu.width;
		const auto title = [&](const QString &shown) {
			return tr::lng_wallet_send_user_title(
				tr::now,
				lt_user,
				Ui::Text::Colorized(shown),
				tr::marked);
		};
		auto measure = Ui::Text::String();
		const auto fits = [&](const TextWithEntities &text) {
			measure.setMarkedText(st::giveawayGiftCodeBox.title.style, text);
			return measure.maxWidth() <= available;
		};
		auto full = title(recipient);
		if (fits(full)) {
			return full;
		}
		const auto middle = (mode == Qt::ElideMiddle);
		const auto shortened = [&](int chars) {
			return middle
				? ShortAddressForm(recipient, chars)
				: (recipient.left(chars) + QChar(0x2026));
		};
		const auto most = middle
			? ((int(recipient.size()) - 1) / 2)
			: (int(recipient.size()) - 1);
		for (auto chars = most; chars > 1; --chars) {
			auto elided = title(shortened(chars));
			if (fits(elided)) {
				return elided;
			}
		}
		return title(shortened(1));
	});
}

struct SendConfirmFee {
	std::optional<int64> feeNano;
	bool gasless = false;
	bool pending = false;

	friend bool operator==(
		const SendConfirmFee &,
		const SendConfirmFee &) = default;
};

struct SendConfirmArgs {
	std::shared_ptr<SendDraft> draft;
	QString address;
	int64 amountNano = 0;
	rpl::producer<SendConfirmFee> fee;
	rpl::producer<QString> refusal;
	rpl::producer<bool> busy;
	rpl::producer<bool> canSend;
	Fn<void(SendConfirmFee shown)> send;
};

[[nodiscard]] SendConfirmFee QuoteConfirmFee(const SendQuote &quote) {
	return {
		.feeNano = quote.feeNano,
		.gasless = quote.dependencies.gaslessTerms.eligible(
			quote.args.amountNano,
			quote.args.destination),
	};
}

void FillSendConfirmTable(
		not_null<Ui::VerticalLayout*> container,
		std::shared_ptr<Ui::Show> show,
		not_null<Main::Session*> session,
		const QString &address,
		const SendConfirmFee &fee,
		TimeId date) {
	const auto table = AddDetailsTableFrame(container);
	Ui::AddTableRow(
		table,
		tr::lng_wallet_details_address(),
		AddressValueLabel(table, show, address));
	if (fee.feeNano) {
		auto item = TransferItem();
		item.feeNano = fee.feeNano;
		item.gasless = fee.gasless;
		AddFeeTableRow(table, std::move(show), session, item);
	} else {
		AddPendingFeeTableRow(
			table,
			fee.pending ? DetailsFee::Loading : DetailsFee::Failed);
	}
	Ui::AddTableRow(
		table,
		tr::lng_wallet_details_date(),
		rpl::single(tr::marked(langDateTime(base::unixtime::parse(date)))));
}

void AddSendCommentLock(
		not_null<Ui::InputField*> field,
		const style::InputField &st,
		std::shared_ptr<SendDraft> draft) {
	const auto lock = Ui::CreateChild<Ui::IconButton>(
		field,
		st::walletSendConfirmLock);
	lock->setClickedCallback([=] {
		auto comment = draft->comment.current();
		comment.isPublic = !comment.isPublic;
		draft->comment = std::move(comment);
	});
	draft->comment.value() | rpl::map([](const SendComment &comment) {
		return comment.isPublic;
	}) | rpl::distinct_until_changed() | rpl::on_next([=](bool isPublic) {
		const auto icon = isPublic
			? &st::walletSendConfirmLockOff
			: &st::walletSendConfirmLockOn;
		lock->setIconOverride(icon, icon);
	}, lock->lifetime());
	draft->encryptable.value() | rpl::on_next([=](bool encryptable) {
		lock->setVisible(encryptable);
	}, lock->lifetime());
	field->widthValue() | rpl::on_next([=, &st](int) {
		lock->moveToRight(
			0,
			st.textMargins.top()
				+ (st.style.font->height - lock->height()) / 2);
	}, lock->lifetime());
}

struct SendConfirmNotes {
	not_null<Ui::RpWidget*> caption;
	not_null<Ui::RpWidget*> refusal;
};

SendConfirmNotes AddSendConfirmNotes(
		not_null<Ui::GenericBox*> box,
		rpl::producer<bool> captionShown,
		rpl::producer<QString> refusal) {
	const auto caption = box->addRow(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			box,
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_wallet_comment_public(),
				st::walletCommentCaptionLabel),
			st::walletCommentCaptionMargin),
		style::margins());
	caption->toggleOn(std::move(captionShown));
	caption->finishAnimating();

	const auto shown = box->addRow(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			box,
			object_ptr<Ui::FlatLabel>(
				box,
				rpl::duplicate(refusal),
				st::walletCommentErrorLabel),
			st::walletCommentCaptionMargin),
		style::margins());
	shown->toggleOn(std::move(refusal) | rpl::map([](const QString &text) {
		return !text.isEmpty();
	}));
	shown->finishAnimating();
	box->addSkip(st::walletSendConfirmBottomSkip);
	return { .caption = caption, .refusal = shown };
}

void WalletSendConfirmBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		SendConfirmArgs args) {
	const auto session = &show->session();
	const auto draft = args.draft;
	const auto address = args.address;
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);
	// An empty title keeps the band the close button stands in.
	box->setTitle(rpl::single(QString()));

	auto item = TransferItem();
	item.amountNano = args.amountNano;
	AddWalletLottie(box);
	AddDetailsAmountHeader(
		box->verticalLayout(),
		item,
		st::walletDetailsLottieSkip,
		st::walletDetailsAmountBottomSkip,
		FiatRateValue(session));

	const auto details = box->addRow(
		object_ptr<Ui::VerticalLayout>(box),
		style::margins());
	const auto openedAt = base::unixtime::now();
	struct State {
		std::optional<SendConfirmFee> built;
		SendConfirmFee pending;
		SendConfirmFee shown;
		bool scheduled = false;
	};
	const auto state = box->lifetime().make_state<State>();
	rpl::combine(
		rpl::duplicate(args.fee),
		rpl::duplicate(args.busy)
	) | rpl::filter([](const SendConfirmFee &fee, bool busy) {
		return !busy;
	}) | rpl::map([](const SendConfirmFee &fee, auto) {
		return fee;
	}) | rpl::distinct_until_changed() | rpl::on_next([=](
			const SendConfirmFee &fee) {
		const auto lost = !fee.feeNano && !fee.pending;
		const auto keep = lost && state->built && state->built->feeNano;
		state->pending = keep ? *state->built : fee;
		if (state->scheduled) {
			return;
		}
		state->scheduled = true;
		Ui::PostponeCall(box, [=] {
			state->scheduled = false;
			// The counted value stands while the next one is counted.
			auto shown = state->pending;
			if (shown.feeNano) {
				state->shown = shown;
			} else if (shown.pending && state->shown.feeNano) {
				shown = state->shown;
			}
			if (state->built == shown) {
				return;
			}
			state->built = shown;
			details->clear();
			FillSendConfirmTable(
				details,
				box->uiShow(),
				session,
				address,
				shown,
				openedAt);
		});
	}, details->lifetime());

	const auto field = box->addRow(
		object_ptr<Ui::InputField>(
			box,
			st::walletSendConfirmCommentField,
			Ui::InputField::Mode::NoNewlines,
			tr::lng_wallet_send_comment_optional(),
			draft->comment.current().text),
		st::walletCommentFieldMargin);
	BindCommentField(field, draft);
	AddSendCommentLock(field, st::walletSendConfirmCommentField, draft);
	ApplyCommentLimit(
		field,
		st::walletSendConfirmLock.width + st::walletCommentLimitSkip);
	rpl::duplicate(args.busy) | rpl::on_next([=](bool busy) {
		field->setDisabled(busy);
	}, field->lifetime());

	AddSendConfirmNotes(
		box,
		draft->comment.value() | rpl::map([](const SendComment &comment) {
			return comment.isPublic && !comment.text.isEmpty();
		}),
		std::move(args.refusal));

	const auto send = [=, callback = args.send] {
		callback(state->built ? *state->built : SendConfirmFee());
	};
	const auto button = box->addButton(
		BusyFooterLabel(
			tr::lng_wallet_send_amount(
				lt_amount,
				rpl::single(Ui::FormatTonAmount(args.amountNano).full)),
			rpl::duplicate(args.busy)),
		send).data();
	rpl::combine(
		std::move(args.canSend),
		std::move(args.fee),
		rpl::duplicate(args.busy)
	) | rpl::on_next([=](
			bool canSend,
			const SendConfirmFee &fee,
			bool busy) {
		SetButtonDisabledLook(button, !busy && (!canSend || fee.pending));
	}, button->lifetime());
	AddBusyFooterSpinner(button, std::move(args.busy));

	field->submits() | rpl::on_next(send, field->lifetime());
	box->setFocusCallback([=] { field->setFocusFast(); });
	AddBoxCloseButton(box);
}

struct CollectibleTransfer {
	QString address;
	std::shared_ptr<CollectibleMedia> media;
};

struct CollectibleRecipient {
	QString destination;
	QString tonName;
	UserId userId;
	bool bounce = true;
};

[[nodiscard]] rpl::producer<QString> CollectibleNameValue(
		std::shared_ptr<CollectibleMedia> media,
		QString address) {
	return rpl::single(rpl::empty) | rpl::then(
		media->changed(
		) | rpl::filter([=](const QString &changed) {
			return (changed == address);
		}) | rpl::to_empty
	) | rpl::map([=] {
		return CollectibleTitleText(media->view(address)).text;
	});
}

void CollectibleTransferBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<const CollectibleTransfer> collectible,
		CollectibleRecipient recipient) {
	const auto session = &show->session();
	const auto weakSession = base::make_weak(session);
	const auto wallet = &session->wallet();
	const auto weak = base::make_weak(box);
	const auto identity = wallet->transferWalletIdentity();
	const auto sessionValid = [=] {
		return weakSession
			&& show->valid()
			&& (&show->session() == session);
	};
	const auto current = [=] {
		return weak
			&& sessionValid()
			&& identity
			&& wallet->transferWalletIdentityCurrent(*identity);
	};
	if (!current()) {
		box->closeBox();
		return;
	}
	const auto address = collectible->address;
	const auto media = collectible->media;
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::walletDetailsBox);
	box->setNoContentMargin(true);
	AddBoxCloseButton(box);

	const auto openedAt = base::unixtime::now();
	const auto item = TransferItem{
		.kind = TransferItem::Kind::Collectible,
		.incoming = false,
		.counterparty = recipient.destination,
		.counterpartyBounceable = recipient.bounce,
		.counterpartyName = recipient.tonName,
		.counterpartyPeer = (recipient.userId
			? peerFromUser(recipient.userId).value
			: quint64()),
		.collectible = address,
		.date = openedAt,
		.status = TransferItem::Status::Success,
	};
	media->resolve(address);
	AddDetailsCollectibleHeader(
		box->verticalLayout(),
		session,
		media,
		item,
		st::walletDetailsAmountBottomSkip / 2,
		false);
	const auto details = box->addRow(
		object_ptr<Ui::VerticalLayout>(box),
		style::margins());

	auto owned = object_ptr<Ui::InputField>(
		box,
		st::walletCollectibleCommentField,
		Ui::InputField::Mode::NoNewlines,
		tr::lng_wallet_send_comment_optional());
	const auto field = owned.data();
	box->addRow(
		MakeCommentBubble(box, std::move(owned), st::windowBg),
		st::walletCommentFieldMargin);
	ApplyCommentLimit(field);

	using ShownFee = std::pair<std::optional<int64>, DetailsFee>;
	struct State {
		std::shared_ptr<const PreparedSend> prepared;
		KeyAuthorization authorization;
		base::Timer signingWait;
		rpl::variable<QString> comment;
		rpl::variable<std::optional<int64>> fee;
		rpl::variable<SendError> error = SendError::None;
		rpl::variable<bool> estimating = false;
		rpl::variable<bool> sending = false;
		Fn<void()> estimate;
		Fn<void()> continueSend;
		ShownFee table;
		std::optional<ShownFee> built;
		uint64 owner = 0;
		uint64 revision = 0;
		int refusals = 0;
		bool tableQueued = false;
		bool requoteQueued = false;
		bool requoteForced = false;
		bool unlocking = false;
		bool submitted = false;
		bool handedOver = false;
		rpl::lifetime signingLifetime;
		// WHY: last, so the key ladder cancelled first finds the rest alive.
		rpl::lifetime keyLifetime;
	};
	const auto state = box->lifetime().make_state<State>();
	state->owner = wallet->createPreviewOwner(box->lifetime());

	wallet->transferWalletIdentityChanges(
	) | rpl::filter([=] {
		return !current();
	}) | rpl::take(1) | rpl::on_next([=] {
		box->closeBox();
	}, box->lifetime());

	const auto args = [=] {
		return SendArgs{
			.destination = recipient.destination,
			.collectible = address,
			.userId = recipient.userId,
			.comment = SendComment{
				.text = state->comment.current(),
				.isPublic = true,
			},
			.bounce = recipient.bounce,
		};
	};
	state->estimate = [=] {
		if (state->estimating.current()) {
			state->requoteQueued = true;
			return;
		}
		const auto text = state->comment.current();
		if (!CommentFits(text)) {
			state->prepared = nullptr;
			state->fee = std::nullopt;
			state->error = SendError::CommentTooLong;
			return;
		}
		const auto revision = ++state->revision;
		const auto signing = wallet->signingReady();
		state->estimating = true;
		wallet->estimateFee(
			KeyAuthorization(),
			state->owner,
			args(),
			crl::guard(box, [=](FeeResult result) {
				if (revision != state->revision) {
					return;
				}
				const auto forced = base::take(state->requoteForced);
				const auto requote = base::take(state->requoteQueued)
					&& (forced
						|| text != state->comment.current()
						|| signing != wallet->signingReady());
				const auto error = (result.error == SendError::None
						&& !result.prepared)
					? SendError::Failed
					: result.error;
				if (requote || error == SendError::QuoteExpired) {
					state->prepared = nullptr;
					state->estimating = false;
					state->estimate();
				} else {
					state->prepared = std::move(result.prepared);
					state->fee = (error == SendError::None)
						? std::make_optional(result.feeNano)
						: std::nullopt;
					state->error = error;
					state->estimating = false;
				}
				if (state->sending.current() && !state->submitted) {
					state->continueSend();
				}
			}));
	};
	wallet->signingReadyValue(
	) | rpl::skip(1) | rpl::on_next([=](bool ready) {
		if (ready) {
			state->signingWait.cancel();
		}
		if (!state->submitted) {
			state->prepared = nullptr;
			state->estimate();
		}
	}, box->lifetime());
	rpl::merge(
		wallet->sendStateValue() | rpl::skip(1) | rpl::to_empty,
		wallet->presenceValue() | rpl::skip(1) | rpl::to_empty
	) | rpl::on_next([=] {
		if (state->sending.current() || state->submitted) {
			return;
		}
		state->prepared = nullptr;
		state->requoteForced = true;
		state->estimate();
	}, box->lifetime());
	rpl::merge(
		wallet->balanceNanoValue() | rpl::skip(1) | rpl::to_empty,
		wallet->custodyUpdates()
	) | rpl::on_next([=] {
		if (state->sending.current()
			|| state->submitted
			|| (!state->estimating.current()
				&& state->prepared
				&& state->error.current() == SendError::None)) {
			return;
		}
		state->prepared = nullptr;
		state->requoteForced = true;
		state->estimate();
	}, box->lifetime());

	const auto insufficient = [](std::optional<int64> fee, int64 balance) {
		return fee && (CollectibleTransferAttachedNanos() + *fee > balance);
	};
	const auto refusalText = [=](
			SendError error,
			std::optional<int64> fee,
			int64 balance) {
		if (error == SendError::InsufficientBalance
			|| error == SendError::InsufficientFees
			|| insufficient(fee, balance)) {
			return SendErrorText(
				SendError::InsufficientFees,
				TransferMinNanos(session));
		} else if (error == SendError::SigningUnavailable
			|| error == SendError::QuoteExpired
			|| error == SendError::None) {
			return QString();
		}
		return SendErrorText(error, TransferMinNanos(session));
	};
	const auto refusalNow = [=] {
		return refusalText(
			state->error.current(),
			state->fee.current(),
			wallet->balanceNano());
	};

	rpl::combine(
		state->fee.value(),
		state->estimating.value(),
		state->error.value()
	) | rpl::map([](
			std::optional<int64> fee,
			bool estimating,
			SendError error) {
		const auto loading = estimating
			|| (error == SendError::None)
			|| (error == SendError::SigningUnavailable);
		const auto shown = fee
			? DetailsFee::Known
			: loading
			? DetailsFee::Loading
			: DetailsFee::Failed;
		return ShownFee(fee, shown);
	}) | rpl::distinct_until_changed() | rpl::on_next([=](ShownFee shown) {
		state->table = shown;
		if (state->tableQueued) {
			return;
		}
		state->tableQueued = true;
		Ui::PostponeCall(box, [=] {
			state->tableQueued = false;
			if (state->built == state->table) {
				return;
			}
			state->built = state->table;
			auto counted = item;
			counted.feeNano = state->table.first;
			const auto top = box->scrollTop();
			const auto atBottom = (top + box->scrollHeight()
				>= box->verticalLayout()->height());
			details->clear();
			AddDetailsTable(box, details, show, counted, state->table.second);
			box->scrollToY(atBottom ? ScrollMax : top);
		});
	}, details->lifetime());

	const auto notes = AddSendConfirmNotes(
		box,
		state->comment.value() | rpl::map([](const QString &text) {
			return !text.isEmpty();
		}),
		rpl::combine(
			state->error.value(),
			state->fee.value(),
			wallet->balanceNanoValue(),
			rpl::single(rpl::empty) | rpl::then(Lang::Updated())
		) | rpl::map([=](
				SendError error,
				std::optional<int64> fee,
				int64 balance,
				rpl::empty_value) {
			return refusalText(error, fee, balance);
		}));
	rpl::combine(
		notes.caption->heightValue(),
		notes.refusal->heightValue()
	) | rpl::map([](int captionHeight, int refusalHeight) {
		return captionHeight + refusalHeight;
	}) | rpl::combine_previous(
	) | rpl::filter([](int was, int now) {
		return (now > was);
	}) | rpl::to_empty | rpl::on_next([=] {
		box->scrollToY(ScrollMax);
	}, box->lifetime());

	const auto pressable = [=] {
		return !state->sending.current()
			&& !state->estimating.current()
			&& refusalNow().isEmpty()
			&& (state->prepared
				|| state->error.current() == SendError::SigningUnavailable);
	};
	const auto press = [=] {
		if (!pressable()) {
			return;
		}
		state->sending = true;
		state->refusals = 0;
		state->continueSend();
	};
	const auto button = box->addButton(
		BusyFooterLabel(
			tr::lng_wallet_collectible_send(
				lt_name,
				CollectibleNameValue(media, address)),
			state->sending.value()),
		press).data();
	rpl::combine(
		state->sending.value(),
		state->estimating.value(),
		state->error.value(),
		state->fee.value(),
		wallet->balanceNanoValue()
	) | rpl::to_empty | rpl::on_next([=] {
		SetButtonDisabledLook(
			button,
			!state->sending.current() && !pressable());
	}, button->lifetime());
	AddBusyFooterSpinner(button, state->sending.value());

	field->changes() | rpl::on_next([=] {
		state->comment = field->getLastText();
		if (!state->sending.current()) {
			state->estimate();
		}
	}, field->lifetime());
	field->submits() | rpl::on_next(press, field->lifetime());
	state->sending.value() | rpl::on_next([=](bool sending) {
		field->setDisabled(sending);
	}, field->lifetime());
	box->setFocusCallback([=] { field->setFocusFast(); });

	const auto stop = [=] {
		state->authorization = {};
		state->signingWait.cancel();
		state->signingLifetime.destroy();
		state->sending = false;
	};
	const auto acquireKey = [=] {
		state->keyLifetime.destroy();
		state->unlocking = true;
		AcquireWalletKey(
			show,
			current,
			state->keyLifetime,
			crl::guard(box, [=](KeyAuthorization auth) {
				state->unlocking = false;
				if (!state->sending.current() || state->submitted) {
					return;
				} else if (!auth.valid()) {
					stop();
					return;
				}
				state->authorization = std::move(auth);
				state->continueSend();
			}),
			tr::lng_wallet_restore_text());
	};
	const auto awaitSigning = [=] {
		if (state->signingWait.isActive()) {
			return;
		}
		state->signingLifetime.destroy();
		wallet->signingReadyValue(
		) | rpl::filter([](bool ready) {
			return ready;
		}) | rpl::take(1) | rpl::on_next([=] {
			state->continueSend();
		}, state->signingLifetime);
		state->signingWait.callOnce(kSigningReadyTimeout);
	};
	state->signingWait.setCallback([=] {
		stop();
		state->error = SendError::Failed;
	});
	const auto started = [=](SendStarted value) {
		state->handedOver = true;
		const auto panel = wallet->panel();
		if (panel && box->window() == panel->window()) {
			wallet->setWindowSend(value.operationId);
		}
		show->hideLayer();
	};
	const auto sent = [=](SendError error) {
		if (error == SendError::KeyChanged) {
			if (weak && !state->handedOver) {
				weak->closeBox();
			}
			if (sessionValid()) {
				ShowWalletKeyChanged(show);
			}
			return;
		} else if (!current() || state->handedOver) {
			return;
		} else if (error == SendError::None
			|| error == SendError::SubmissionUnknown) {
			show->hideLayer();
			return;
		}
		state->submitted = false;
		const auto retry = (error == SendError::QuoteExpired)
			|| (error == SendError::SigningUnavailable);
		if (retry && ++state->refusals <= kSendRefusalRetries) {
			state->estimate();
			return;
		}
		stop();
		state->error = (error == SendError::QuoteExpired)
			? SendError::Failed
			: error;
	};
	state->continueSend = [=] {
		if (!state->sending.current()
			|| state->submitted
			|| state->unlocking) {
			return;
		} else if (!current()) {
			stop();
			return;
		} else if (state->estimating.current()) {
			return;
		} else if (!state->authorization.valid()) {
			acquireKey();
			return;
		} else if (!wallet->signingReady()) {
			awaitSigning();
			return;
		} else if (!state->prepared) {
			const auto error = state->error.current();
			const auto retry = (error == SendError::None)
				|| (error == SendError::SigningUnavailable);
			if (retry && ++state->refusals <= kSendRefusalRetries) {
				state->estimate();
				return;
			}
			stop();
			if (retry) {
				state->error = SendError::Failed;
			}
			return;
		} else if (!refusalNow().isEmpty()) {
			stop();
			return;
		}
		state->submitted = true;
		wallet->send(
			state->authorization,
			base::take(state->prepared),
			crl::guard(session, sent),
			crl::guard(session, crl::guard(box, started)));
	};

	state->estimate();
}

void ShowSendRecipientWallet(
	std::shared_ptr<Ui::Show> show,
	not_null<UserData*> user,
	const QString &address,
	Fn<void()> profile);

void WalletSendBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		std::optional<SendFlow> initial,
		UserData *user,
		Fn<void()> sent,
		Fn<void()> notReady,
		int64 amountNano,
		base::weak_qptr<Ui::BoxContent> origin) {
	Expects(user || initial);

	// The box this one was opened over comes back unless the user leaves.
	const auto discardOrigin = [=] {
		if (const auto strong = origin.get()) {
			strong->closeBox();
		}
	};
	const auto self = (!user
			&& SendsToOwnWallet(&show->session(), initial->destination))
		? show->session().user().get()
		: nullptr;
	const auto recipient = user ? user : self;

	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);

	const auto weak = base::make_weak(box.get());
	const auto openProfile = recipient
		? Fn<void()>([=] {
			const auto window = MakeChatShow(show, true)->resolveWindow();
			if (!window) {
				return;
			}
			const auto peer = recipient;
			if (weak
				&& weak->hasDelegate()
				&& window->widget()->window() == weak->window()) {
				discardOrigin();
				weak->closeBox();
			}
			window->showPeerInfo(peer);
			window->window().activate();
		})
		: nullptr;
	if (recipient) {
		// WHY: the box title label is private inside lib_ui and takes no
		// click filter, so the name is clickable only while this file owns
		// the label itself.
		const auto wrap = box->setPinnedToTopContent(
			object_ptr<Ui::FixedHeightWidget>(
				box,
				st::boxTitleHeight - st::boxTopMargin));
		const auto title = Ui::CreateChild<Ui::FlatLabel>(
			wrap,
			tr::lng_wallet_send_user_title(
				lt_user,
				Info::Profile::NameValue(recipient) | rpl::map([](QString name) {
					return tr::link(name);
				}),
				tr::marked),
			st::boxTitle);
		title->setClickHandlerFilter([=](const auto &...) {
			const auto onstack = openProfile;
			onstack();
			return false;
		});
		rpl::combine(
			wrap->widthValue(),
			title->naturalWidthValue()
		) | rpl::on_next([=](int width, int) {
			const auto buttons = st::boxTitleClose.width
				+ st::boxTitleMenu.width;
			title->resizeToNaturalWidth(
				width - 2 * st::boxTitlePosition.x() - buttons);
			title->moveToLeft(
				st::boxTitlePosition.x(),
				st::boxTitlePosition.y() - st::boxTopMargin,
				width);
		}, title->lifetime());
	} else {
		box->setTitle(initial->tonName.isEmpty()
			? SendRecipientTitle(box, initial->displayForm, Qt::ElideMiddle)
			: SendRecipientTitle(box, initial->tonName, Qt::ElideRight));
	}
	AddBoxCloseButton(box);

	const auto session = &show->session();
	const auto weakSession = base::make_weak(session);
	const auto wallet = &session->wallet();

	struct State {
		std::optional<SendFlow> flow;
		std::optional<SendQuoteDependencies> previewDependencies;
		base::unique_qptr<Ui::PopupMenu> menu;
		base::weak_qptr<Ui::GenericBox> commentBox;
		base::weak_qptr<Ui::GenericBox> confirmBox;
		std::optional<TransferWalletIdentity> senderIdentity;
		QByteArray recipientKey;
		uint64 previewRevision = 0;
		uint64 loadRevision = 0;
		base::Timer loadDeadline;
		rpl::lifetime previewLifetime;
		bool forceIssued = false;
		bool terminal = false;
		bool recomputeQueued = false;
		bool recipientRequested = false;
		bool feeRefreshQueued = false;
		rpl::variable<bool> loading = false;
		rpl::variable<QString> loadError;
		rpl::variable<bool> silentFailure = false;
		rpl::variable<int64> amount = 0;
		rpl::variable<int64> fee = 0;
		rpl::variable<int64> minTransfer = kTransferMinNanosDefault;
		std::shared_ptr<SendDraft> draft = std::make_shared<SendDraft>();
		rpl::variable<bool> sending = false;
		std::optional<SendQuoteDependencies> sendRequest;
		std::optional<uint64> sendExpiresAt;
		std::optional<SendConfirmFee> heldFee;
		KeyAuthorization sendAuthorization;
		KeyAuthorization heldAuthorization;
		base::Timer heldTimeout;
		int sendRefusals = 0;
		bool unlocking = false;
		bool submitted = false;
		bool handedOver = false;
		Fn<void()> continueSend;
		rpl::variable<bool> previewInsufficient = false;
		rpl::variable<SendError> previewError = SendError::None;
		rpl::variable<bool> insufficient = false;
		rpl::variable<bool> unfunded = false;
		rpl::variable<bool> canSend = false;
		rpl::variable<bool> raisable = false;
		std::shared_ptr<KeyContext> keyContext;
		base::Timer signingWait;
		bool signingTimedOut = false;
		rpl::variable<FiatRate> rate;
		rpl::variable<bool> entryFiat = false;
		QString previousCurrency;
		bool settingUnitText = false;
		bool closed = false;
		Fn<void()> swapUnit;
	};
	const auto state = box->lifetime().make_state<State>();
	if (initial) {
		state->draft = initial->draft;
	}
	const auto draft = state->draft;
	const auto previewOwner = wallet->createPreviewOwner(state->previewLifetime);
	state->senderIdentity = user
		? std::nullopt
		: wallet->transferWalletIdentity();
	state->loading = !state->senderIdentity;
	state->rate = FiatRateValue(session);
	state->minTransfer = TransferMinNanos(session);
	const auto userId = user ? peerToUser(user->id) : UserId();
	const auto sessionValid = [=] {
		return weakSession
			&& show->valid()
			&& (&show->session() == session);
	};
	const auto userError = [=] {
		if (!sessionValid()
			|| (state->senderIdentity
				&& !wallet->transferWalletIdentityCurrent(
					*state->senderIdentity))) {
			return u"WALLET_NOT_READY"_q;
		}
		const auto error = wallet->userAddresses().forceResolveError(userId);
		if (!error.isEmpty()) {
			return error;
		} else if (session->data().userLoaded(userId) != user) {
			return u"WALLET_USER_INVALID"_q;
		} else if (state->flow
			&& (!user->gramAddress()
				|| *user->gramAddress() != state->flow->destination)) {
			return u"WALLET_ADDRESS_INVALID"_q;
		}
		return QString();
	};
	const auto originValid = [=] {
		return weak
			&& !state->closed
			&& !state->terminal
			&& sessionValid()
			&& state->senderIdentity
			&& wallet->transferWalletIdentityCurrent(*state->senderIdentity)
			&& !state->loading.current()
			&& state->loadError.current().isEmpty()
			&& (!user || (state->flow && userError().isEmpty()));
	};
	const auto receive = [=] {
		if (originValid()
			|| (state->unfunded.current()
				&& !state->closed
				&& !state->terminal
				&& sessionValid())) {
			ShowWalletReceiveBox(session, box->uiShow());
		}
	};
	const auto editComment = [=] {
		if (!originValid()
			|| state->commentBox
			|| state->sending.current()) {
			return;
		}
		auto editor = Box(
			WalletSendCommentBox,
			draft,
			originValid,
			draft->encryptable.value());
		const auto raw = editor.data();
		state->commentBox = base::make_weak(raw);
		raw->boxClosing() | rpl::on_next([=] {
			if (weak && state->commentBox.get() == raw) {
				state->commentBox = nullptr;
			}
		}, raw->lifetime());
		box->uiShow()->showBox(std::move(editor));
	};
	const auto toggle = box->addTopButton(st::boxTitleMenu);
	toggle->setClickedCallback([=] {
		if (!originValid() || state->menu) {
			return;
		}
		state->menu = base::make_unique_q<Ui::PopupMenu>(
			box,
			st::popupMenuWithIcons);
		const auto raw = state->menu.get();
		raw->setDestroyedCallback(crl::guard(toggle, [=] {
			toggle->setForceRippled(false);
		}));
		toggle->setForceRippled(true);
		raw->addAction(
			Ui::Text::FixAmpersandInAction(
				tr::lng_wallet_add_funds(tr::now)),
			receive,
			&st::menuIconAdd);
		raw->addAction(
			Ui::Text::FixAmpersandInAction(
				tr::lng_wallet_comment_title(tr::now)),
			editComment,
			&st::menuIconChatBubble);
		raw->setForcedOrigin(Ui::PanelAnimation::Origin::TopRight);
		const auto scope = WindowPaletteScope(box);
		raw->popup(toggle->mapToGlobal(QPoint(
			toggle->width(),
			toggle->height())));
	});
	const auto quoteDependencies = [=] {
		const auto validSession = sessionValid();
		return SendQuoteDependencies{
			.senderIdentity = validSession
				? wallet->transferWalletIdentity()
				: std::nullopt,
			.gaslessTerms = validSession
				? wallet->gaslessTerms()
				: GaslessTerms(),
			.destination = state->flow ? state->flow->destination : QString(),
			.comment = draft->comment.current(),
			.custody = validSession
				? wallet->deviceCustodyState()
				: DeviceCustodyState(),
			.recipientPublicKey = state->recipientKey,
			.userId = userId,
			.amountNano = state->amount.current(),
			.balanceNano = validSession ? wallet->balanceNano() : 0,
			.minTransferNano = state->minTransfer.current(),
			.bounce = state->flow && state->flow->bounce,
			.ready = validSession
				&& wallet->presenceCurrent() == Presence::Ready,
			.valid = originValid(),
		};
	};
	const auto stopSending = [=] {
		state->sending = false;
		state->sendRequest.reset();
		state->sendExpiresAt.reset();
		state->heldFee.reset();
		state->sendAuthorization = {};
		state->sendRefusals = 0;
		state->submitted = false;
		state->signingWait.cancel();
		state->signingTimedOut = false;
	};
	const auto scheduleContinueSend = [=] {
		Ui::PostponeCall(box, [=] { state->continueSend(); });
	};
	// The signing client is awaited for a bounded time only. A journal
	// recovery that keeps failing would otherwise hold the press forever,
	// while letting the press through after the bound has the session
	// refuse it typed, the way it did before the wait existed.
	const auto awaitSigning = [=] {
		if (!state->signingWait.isActive()) {
			state->signingWait.callOnce(kSigningReadyTimeout);
		}
	};
	state->signingWait.setCallback([=] {
		state->signingTimedOut = true;
		scheduleContinueSend();
	});
	const auto dropHeldKey = [=] {
		state->heldAuthorization = {};
		state->heldTimeout.cancel();
	};
	state->heldTimeout.setCallback(dropHeldKey);
	const auto failLoading = [=](const QString &error, bool silent = false) {
		if (state->closed || state->terminal) {
			return;
		}
		state->terminal = true;
		state->silentFailure = silent;
		stopSending();
		++state->loadRevision;
		state->loadDeadline.cancel();
		++state->previewRevision;
		state->previewLifetime.destroy();
		draft->preparing = false;
		draft->quote = std::nullopt;
		draft->authorization = {};
		draft->privateEpoch.reset();
		state->flow = std::nullopt;
		state->loadError = error.isEmpty() ? u"WALLET_ADDRESS_INVALID"_q : error;
		state->loading = false;
		if (!silent) {
			show->showToast(SendUserLoadErrorText(state->loadError.current()));
		}
		box->closeBox();
	};
	box->boxClosing() | rpl::on_next([=] {
		state->closed = true;
		if (const auto confirm = base::take(state->confirmBox)) {
			if (confirm->hasDelegate()) {
				confirm->closeBox();
			}
		}
		if (const auto context = base::take(state->keyContext)) {
			context->cancel();
		}
		++state->loadRevision;
		state->loadDeadline.cancel();
		++state->previewRevision;
		state->previewLifetime.destroy();
		draft->preparing = false;
		draft->quote = std::nullopt;
		draft->authorization = {};
		draft->privateEpoch.reset();
		state->sendAuthorization = {};
		dropHeldKey();
		state->flow.reset();
	}, box->lifetime());

	const auto entrySeparator = [=] {
		if (!state->entryFiat.current()) {
			return Ui::TonAmountSeparator();
		}
		const auto rule = Ui::LookupCurrencyRule(
			state->rate.current().currency);
		return QString(QChar(rule.decimal));
	};

	const auto inner = box->verticalLayout();
	auto address = user
		? rpl::producer<QString>(state->loading.value(
		) | rpl::map([=](bool loading) {
			return (!loading && state->flow)
				? state->flow->displayForm
				: QString();
		}))
		: rpl::producer<QString>(rpl::single(initial->displayForm));
	inner->add(
		object_ptr<SendRecipientCard>(
			inner,
			box->uiShow(),
			recipient,
			std::move(address),
			(recipient
				? Fn<void()>([=] {
					if (!state->flow) {
						return;
					}
					ShowSendRecipientWallet(
						box->uiShow(),
						recipient,
						state->flow->displayForm,
						openProfile);
				})
				: nullptr)),
		style::margins(
			st::boxRowPadding.left(),
			st::walletSendUserCardTopSkip,
			st::boxRowPadding.right(),
			0),
		style::al_justify);

	auto helper = Ui::Text::CustomEmojiHelper();
	const auto gramMark = GramMark(
		helper,
		st::walletSendUserFiatButton.style.font);
	auto equivalent = rpl::combine(
		state->amount.value(),
		state->entryFiat.value(),
		state->rate.value()
	) | rpl::map([=](int64 amount, bool fiat, const FiatRate &rate) {
		if (fiat) {
			const auto formatted = Ui::FormatTonAmount(amount);
			return AmountLabel{
				.prefix = TextWithEntities(gramMark).append(u" "_q),
				.amount = formatted.full,
				.decimal = formatted.separator,
			};
		}
		const auto rule = Ui::LookupCurrencyRule(rate.currency);
		return AmountLabel{
			.prefix = tr::marked(QChar('~')),
			.amount = FormatFiatAmount(amount, rate),
			.decimal = QString(QChar(rule.decimal)),
			.suffix = u" "_q + rate.currency,
			.unit = rate.currency,
		};
	});
	const auto amountField = AddAmountField(
		inner,
		st::walletSendUserCardAmountSkip,
		{
			.value = std::min(
				initial ? initial->amountNano : amountNano,
				kMaxAmountNano),
			.fractionDigits = [=] {
				return state->entryFiat.current()
					? Ui::LookupCurrencyRule(
						state->rate.current().currency).exponent
					: 9;
			},
			.separator = entrySeparator,
			.entryFiat = state->entryFiat.value(),
			.currency = state->rate.value(
			) | rpl::map([](const FiatRate &rate) {
				return rate.currency;
			}) | rpl::distinct_until_changed(),
			.equivalent = std::move(equivalent),
			.equivalentContext = helper.context(),
			.swap = [=] { state->swapUnit(); },
			.equivalentShown = rpl::combine(
				state->loading.value(),
				state->loadError.value()
			) | rpl::map([](bool loading, const QString &error) {
				return !loading && error.isEmpty();
			}),
		});
	const auto comment = inner->add(
		object_ptr<Ui::SlideWrap<SendCommentBubble>>(
			inner,
			object_ptr<SendCommentBubble>(
				inner,
				draft->comment.value(),
				rpl::combine(
					state->sending.value(),
					state->loading.value(),
					state->loadError.value()
				) | rpl::map([=](bool sending, bool, const QString &) {
					return !sending && originValid();
				}),
				editComment)),
		style::margins(),
		style::al_justify);
	comment->toggleOn(draft->comment.value() | rpl::map([](
			const SendComment &value) {
		return !value.text.isEmpty();
	}));
	comment->finishAnimating();
	const auto publicWarning = inner->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			inner,
			object_ptr<Ui::FlatLabel>(
				inner,
				tr::lng_wallet_comment_public(),
				st::walletSendUserBalanceLabel),
			style::margins(
				st::walletSendFieldMargin.left(),
				0,
				st::walletSendFieldMargin.right(),
				st::walletSendFieldMargin.bottom())),
		style::margins(),
		style::al_justify);
	publicWarning->entity()->setTryMakeSimilarLines(true);
	publicWarning->toggleOn(draft->comment.value() | rpl::map([](
			const SendComment &value) {
		return value.isPublic && !value.text.isEmpty();
	}));
	publicWarning->finishAnimating();

	const auto updateAmount = [=] {
		// The confirmation on top shows this amount, so a rate waits for it.
		if (state->settingUnitText || state->confirmBox) {
			return;
		}
		const auto parsed = Ui::ParseTonAmountString(
			amountField->getLastText(),
			entrySeparator()).value_or(0);
		const auto rate = state->rate.current();
		state->amount = !state->entryFiat.current()
			? parsed
			: rate.available()
			? std::min(
				int64(std::clamp(
					base::SafeRound(double(parsed) / rate.perGram),
					0.,
					double(kMaxAmountNano))),
				kMaxAmountNano)
			: 0;
	};
	amountField->changes(
	) | rpl::on_next(updateAmount, amountField->lifetime());
	updateAmount();

	const auto fiatUnitsText = [=](int64 units, int64 quantum) {
		const auto formatted = Ui::FormatTonAmount(
			units * quantum,
			Ui::TonFormatFlag::Simple);
		auto result = formatted.wholeString;
		if (!formatted.nanoString.isEmpty()) {
			result += entrySeparator() + formatted.nanoString;
		}
		return result;
	};
	const auto renderUnitText = [=] {
		const auto amount = state->amount.current();
		if (!state->entryFiat.current()) {
			return amount
				? Ui::FormatTonAmount(
					amount,
					Ui::TonFormatFlag::Simple).full
				: QString();
		}
		const auto rate = state->rate.current();
		const auto quantum = FiatMinorUnitNanos(rate.currency);
		const auto maxUnits = kMaxFiatUnits * (Ui::kNanosInOne / quantum);
		const auto units = int64(std::min(
			base::SafeRound(amount * rate.perGram / double(quantum)),
			double(maxUnits)));
		if (!units) {
			return QString();
		}
		return fiatUnitsText(units, quantum);
	};
	const auto setUnitText = [=] {
		state->settingUnitText = true;
		Ui::PostponeCall(amountField, [=] {
			state->settingUnitText = false;
		});
		amountField->setText(renderUnitText());
		amountField->setFocusFast();
	};
	const auto switchEntryUnit = [=](bool fiat) {
		if (state->entryFiat.current() == fiat
			|| (fiat && !state->rate.current().available())) {
			return;
		}
		state->entryFiat = fiat;
		setUnitText();
	};
	const auto raiseToMinimum = [=] {
		const auto minimum = state->minTransfer.current();
		if (!state->entryFiat.current()) {
			amountField->setText(Ui::FormatTonAmount(
				minimum,
				Ui::TonFormatFlag::Simple).full);
		} else {
			const auto rate = state->rate.current();
			if (!rate.available()) {
				return;
			}
			const auto quantum = FiatMinorUnitNanos(rate.currency);
			const auto maxUnits = kMaxFiatUnits * (Ui::kNanosInOne / quantum);
			const auto units = int64(std::min(
				std::ceil(double(minimum) * rate.perGram / double(quantum)),
				double(maxUnits)));
			amountField->setText(fiatUnitsText(units, quantum));
		}
		amountField->setFocusFast();
	};
	state->swapUnit = [=] { switchEntryUnit(!state->entryFiat.current()); };
	state->previousCurrency = state->rate.current().currency;
	state->rate.value() | rpl::on_next([=](const FiatRate &now) {
		const auto currencyChanged
			= (now.currency != state->previousCurrency);
		state->previousCurrency = now.currency;
		if (!now.available()) {
			switchEntryUnit(false);
		} else if (state->entryFiat.current()) {
			if (currencyChanged) {
				setUnitText();
			} else {
				updateAmount();
			}
		}
	}, box->lifetime());

	const auto invalidateFee = [=] {
		if (!weak || state->closed || state->terminal) {
			return;
		}
		++state->previewRevision;
		draft->preparing = false;
		draft->quote = std::nullopt;
		draft->authorization = {};
		draft->privateEpoch.reset();
		state->previewInsufficient = false;
		state->previewError = SendError::None;
		if (weakSession) {
			wallet->cancelFeeEstimate(previewOwner);
		}
	};
	// A comment encrypts for a key Telegram named for the destination, or for
	// the one the recipient's `get_public_key` answers. A wallet that was
	// never deployed answers neither unless it belongs to a Telegram user, so
	// when the engine refuses to encrypt for a destination, the box stops
	// offering encryption for it and the comment becomes a public one. The
	// send in flight stops there: a comment written to be private is never
	// published by the press that was meant to encrypt it.
	const auto switchToPlain = [=] {
		if (!state->flow) {
			return;
		}
		draft->encryptable = false;
		auto comment = draft->comment.current();
		if (!comment.isPublic) {
			comment.isPublic = true;
			draft->comment = std::move(comment);
		}
	};
	const auto prepareFee = [=](KeyAuthorization authorization) {
		if (!originValid() || draft->preparing.current()) {
			return;
		}
		invalidateFee();
		const auto dependencies = quoteDependencies();
		state->previewDependencies = dependencies;
		const auto revision = state->previewRevision;
		// A key just acquired for this press is followed by the client swap
		// to the signing one, and an estimate refused in that window is not
		// the press failing: the press waits for the signing client and
		// estimates again under the same authorization; a hidden refusal
		// during a press is the press's to state, never a silent stop.
		const auto fail = [=](SendError error) {
			if (revision != state->previewRevision) {
				return;
			}
			const auto swapping = (error == SendError::SigningUnavailable)
				&& state->sendAuthorization.valid()
				&& !wallet->signingReady()
				&& !state->signingTimedOut;
			const auto handOff = !swapping
				&& (error == SendError::SigningUnavailable)
				&& state->sending.current()
				&& !state->submitted;
			if (swapping) {
				awaitSigning();
			} else if (!state->submitted && !handOff) {
				stopSending();
			}
			invalidateFee();
			state->previewError = error;
			if (handOff) {
				scheduleContinueSend();
			}
		};
		const auto drifted = [=] {
			if (revision != state->previewRevision) {
				return;
			}
			invalidateFee();
			state->previewDependencies.reset();
			if (state->sending.current() && !state->submitted) {
				scheduleContinueSend();
			}
		};
		if (!CommentFits(dependencies.comment.text)) {
			fail(SendError::CommentTooLong);
			return;
		} else if (!dependencies.ready || dependencies.amountNano <= 0) {
			fail(SendError::InvalidRequest);
			return;
		} else if (TransferAmountBelowMinimum(
				dependencies.amountNano,
				dependencies.minTransferNano)) {
			fail(SendError::AmountTooSmall);
			return;
		} else if (dependencies.amountNano > dependencies.balanceNano) {
			fail(SendError::InsufficientBalance);
			return;
		}
		const auto args = SendArgs{
			.destination = dependencies.destination,
			.amountNano = dependencies.amountNano,
			.userId = dependencies.userId,
			.comment = dependencies.comment,
			.recipientPublicKey = dependencies.recipientPublicKey,
			.bounce = dependencies.bounce,
		};
		const auto isPrivate = !args.comment.text.isEmpty()
			&& !args.comment.isPublic;
		const auto keyed = isPrivate && authorization.valid();
		const auto privateEpoch = keyed
			? std::make_optional(wallet->vault().clearEpoch())
			: std::nullopt;
		draft->preparing = true;
		const auto current = [=] {
			if (!originValid() || revision != state->previewRevision) {
				return false;
			}
			const auto now = quoteDependencies();
			return revision == state->previewRevision && dependencies == now;
		};
		const auto estimate = crl::guard(session, crl::guard(box, [=](
				KeyAuthorization auth) {
			if (!current()) {
				drifted();
				return;
			} else if (privateEpoch
				&& *privateEpoch != wallet->vault().clearEpoch()) {
				fail(SendError::Locked);
				return;
			}
			draft->authorization = auth;
			draft->privateEpoch = privateEpoch;
			wallet->estimateFee(
				std::move(auth),
				previewOwner,
				args,
				crl::guard(session, crl::guard(box, [=](FeeResult result) {
					if (!current()) {
						drifted();
						return;
					} else if (privateEpoch
						&& (*privateEpoch != wallet->vault().clearEpoch()
							|| !draft->authorization.valid()
							|| !wallet->vault().unlocked())) {
						fail(SendError::Locked);
						return;
					}
					switch (result.error) {
					case SendError::None:
						if (!result.prepared && (keyed || !isPrivate)) {
							fail(SendError::Failed);
							return;
						}
						draft->quote = SendQuote{
							.args = args,
							.dependencies = dependencies,
							.prepared = std::move(result.prepared),
							.feeNano = result.feeNano,
							.revision = revision,
						};
						draft->preparing = false;
						if (state->sending.current() && !state->submitted) {
							scheduleContinueSend();
						}
						return;
					case SendError::InsufficientBalance:
					case SendError::InsufficientFees:
						fail(result.error);
						state->previewInsufficient = true;
						return;
					case SendError::QuoteExpired:
						drifted();
						return;
					case SendError::CommentEncryptionUnavailable:
						if (!state->submitted) {
							const auto held = state->sendAuthorization;
							stopSending();
							state->heldAuthorization = held;
							state->heldTimeout.callOnce(kHeldSendKeyTimeout);
						}
						invalidateFee();
						switchToPlain();
						return;
					case SendError::AmountTooSmall:
					case SendError::CommentTooLong:
					case SendError::InvalidRequest:
					case SendError::PreviousUnresolved:
					case SendError::AlreadySending:
					case SendError::SigningUnavailable:
					case SendError::Locked:
					case SendError::Failed:
					case SendError::Rejected:
					case SendError::DataInvalid:
					case SendError::KeyMismatch:
					case SendError::KeyChanged:
					case SendError::CollectibleUnavailable:
					case SendError::CollectibleRejected:
					case SendError::Silent:
					case SendError::SubmissionUnknown:
						fail(result.error);
						return;
					}
					Unexpected("Error value in the send box fee estimate.");
				})));
		}));
		estimate(keyed ? std::move(authorization) : KeyAuthorization());
	};
	const auto refreshFee = [=] {
		if (state->closed || state->terminal) {
			return;
		} else if (state->unfunded.current()) {
			state->previewDependencies.reset();
			invalidateFee();
			return;
		}
		const auto dependencies = quoteDependencies();
		if (state->previewDependencies == dependencies) {
			return;
		}
		// WHY: an estimate prices the whole input, so a change made while
		// one is counting waits for it and is counted once, instead of a
		// count queued for every letter typed into the comment.
		if (draft->preparing.current() && !state->sending.current()) {
			state->feeRefreshQueued = true;
			return;
		}
		state->feeRefreshQueued = false;
		state->previewDependencies = dependencies;
		invalidateFee();
		if (!CommentFits(dependencies.comment.text)) {
			state->previewError = SendError::CommentTooLong;
		} else if (TransferAmountBelowMinimum(
				dependencies.amountNano,
				dependencies.minTransferNano)) {
			state->previewError = SendError::AmountTooSmall;
		} else if (!state->sending.current() && dependencies.amountNano > 0) {
			prepareFee({});
		}
		if (state->sending.current() && !state->submitted) {
			scheduleContinueSend();
		}
	};
	// The last counted fee stands while the next one is counted.
	draft->quote.value() | rpl::on_next([=](
			const std::optional<SendQuote> &quote) {
		if (quote) {
			state->fee = quote->feeNano;
		}
	}, box->lifetime());
	draft->preparing.changes() | rpl::on_next([=](bool preparing) {
		if (preparing || !base::take(state->feeRefreshQueued)) {
			return;
		}
		Ui::PostponeCall(box, refreshFee);
	}, box->lifetime());
	state->amount.value() | rpl::on_next(refreshFee, box->lifetime());
	draft->comment.changes() | rpl::on_next(refreshFee, box->lifetime());
	state->loading.changes() | rpl::on_next(refreshFee, box->lifetime());
	state->unfunded.changes() | rpl::on_next(refreshFee, box->lifetime());
	state->loadError.changes() | rpl::on_next(refreshFee, box->lifetime());
	session->appConfig().refreshed() | rpl::on_next([=] {
		state->minTransfer = TransferMinNanos(session);
	}, box->lifetime());
	state->minTransfer.changes() | rpl::on_next(refreshFee, box->lifetime());
	wallet->gaslessTermsValue() | rpl::on_next(refreshFee, box->lifetime());
	wallet->transferWalletIdentityChanges() | rpl::on_next([=] {
		if (!state->senderIdentity && !user && sessionValid()) {
			state->senderIdentity = wallet->transferWalletIdentity();
		}
		refreshFee();
	}, box->lifetime());
	wallet->signingReadyValue() | rpl::skip(1) | rpl::on_next([=](bool ready) {
		if (ready) {
			state->signingWait.cancel();
			state->signingTimedOut = false;
		}
		state->previewDependencies.reset();
		refreshFee();
	}, box->lifetime());
	if (!user) {
		wallet->custodyUpdates() | rpl::on_next(refreshFee, box->lifetime());
		wallet->balanceNanoValue() | rpl::on_next(refreshFee, box->lifetime());
		wallet->stateKnownValue() | rpl::on_next(refreshFee, box->lifetime());
		wallet->presenceValue() | rpl::on_next(refreshFee, box->lifetime());
	}
	state->insufficient = rpl::combine(
		state->amount.value(),
		state->fee.value(),
		state->previewInsufficient.value(),
		wallet->balanceNanoValue(),
		wallet->stateKnownValue(),
		wallet->gaslessTermsValue(),
		state->unfunded.value()
	) | rpl::map([=](
			int64 amount,
			int64 fee,
			bool preview,
			int64 balance,
			bool known,
			const GaslessTerms &terms,
			bool unfunded) {
		// A fee-free transfer keeps nothing back for the fee, so the whole
		// balance is sendable when the offer covers this one.
		const auto destination = state->flow
			? state->flow->destination
			: QString();
		const auto reserve = terms.eligible(amount, destination) ? 0 : fee;
		return unfunded
			|| ((amount > 0)
				&& (preview || (known && amount > balance - reserve)));
	});
	state->canSend = rpl::combine(
		state->amount.value(),
		state->insufficient.value(),
		wallet->stateKnownValue(),
		state->previewError.value(),
		state->loading.value(),
		state->loadError.value()
	) | rpl::map([](
			int64 amount,
			bool insufficient,
			bool known,
			SendError error,
			bool loading,
			const QString &loadError) {
		return known
			&& (amount > 0)
			&& !insufficient
			&& !loading
			&& loadError.isEmpty()
			&& (error != SendError::AmountTooSmall)
			&& (error != SendError::CommentTooLong)
			&& (error != SendError::AlreadySending)
			&& (error != SendError::PreviousUnresolved);
	});
	state->raisable = rpl::combine(
		state->amount.value(),
		state->minTransfer.value(),
		state->loading.value(),
		state->loadError.value(),
		draft->comment.value(),
		state->unfunded.value()
	) | rpl::map([](
			int64 amount,
			int64 minimum,
			bool loading,
			const QString &loadError,
			const SendComment &comment,
			bool unfunded) {
		return TransferAmountBelowMinimum(amount, minimum)
			&& !loading
			&& !unfunded
			&& loadError.isEmpty()
			&& CommentFits(comment.text);
	});

	const auto balance = inner->add(
		object_ptr<Ui::VerticalLayout>(inner),
		style::margins(
			st::walletSendFieldMargin.left(),
			0,
			st::walletSendFieldMargin.right(),
			st::walletDetailsAmountBottomSkip),
		style::al_justify);
	const auto balanceWrap = balance->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			balance,
			object_ptr<Ui::FlatLabel>(
				balance,
				rpl::combine(
					tr::lng_wallet_send_balance(
						lt_amount,
						wallet->balanceNanoValue() | rpl::map([](int64 nano) {
							return Ui::FormatTonAmount(nano).full;
						})),
					state->loading.value(),
					state->loadError.value()
				) | rpl::map([](QString text, bool loading, const QString &error) {
					return (loading || !error.isEmpty())
						? QString(QChar(0xA0))
						: text;
				}),
				st::walletSendUserBalanceLabel)),
		style::al_justify);
	balanceWrap->toggleOn(rpl::combine(
		state->insufficient.value(),
		state->previewError.value(),
		state->loading.value(),
		state->loadError.value()
	) | rpl::map([](
			bool insufficient,
			SendError error,
			bool loading,
			const QString &loadError) {
		return loading || (!insufficient
			&& (error == SendError::None)
			&& loadError.isEmpty());
	}));
	balanceWrap->finishAnimating();
	const auto feeWrap = balance->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			balance,
			object_ptr<Ui::FlatLabel>(
				balance,
				tr::lng_wallet_send_network_fee(
					lt_amount,
					state->fee.value() | rpl::map([](int64 nano) {
						return Ui::FormatTonAmount(nano).full;
					})),
				st::walletSendUserBalanceLabel)),
		style::al_justify);
	feeWrap->toggleOn(rpl::combine(
		state->amount.value(),
		state->fee.value(),
		wallet->gaslessTermsValue(),
		draft->quote.value(),
		state->insufficient.value(),
		state->previewError.value(),
		state->loading.value(),
		state->loadError.value()
	) | rpl::map([=](
			int64 amount,
			int64 fee,
			const GaslessTerms &terms,
			const std::optional<SendQuote> &,
			bool insufficient,
			SendError error,
			bool loading,
			const QString &loadError) {
		const auto destination = state->flow
			? state->flow->destination
			: QString();
		return (amount > 0)
			&& (fee > 0)
			&& !terms.eligible(amount, destination)
			&& !insufficient
			&& (error == SendError::None)
			&& !loading
			&& loadError.isEmpty();
	}));
	feeWrap->finishAnimating();
	auto showInsufficient = rpl::combine(
		state->insufficient.value(),
		state->previewError.value(),
		state->loading.value(),
		state->loadError.value()
	) | rpl::map([](
			bool insufficient,
			SendError error,
			bool loading,
			const QString &loadError) {
		return insufficient
			&& (error != SendError::AmountTooSmall)
			&& !loading
			&& loadError.isEmpty();
	});
	const auto insufficientWrap = balance->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			balance,
			object_ptr<Ui::FlatLabel>(
				balance,
				tr::lng_wallet_send_error_insufficient(),
				st::walletSendUserErrorLabel)),
		style::al_justify);
	// An error that does not fit one line reads better split evenly than
	// with a full line above a single trailing word.
	insufficientWrap->entity()->setTryMakeSimilarLines(true);
	insufficientWrap->toggleOn(rpl::duplicate(showInsufficient));
	insufficientWrap->finishAnimating();
	const auto refusalValue = [=](bool insufficientStated) {
		return rpl::combine(
			state->previewError.value(),
			state->insufficient.value(),
			state->loading.value(),
			state->loadError.value(),
			state->silentFailure.value(),
			state->minTransfer.value(),
			rpl::single(rpl::empty) | rpl::then(Lang::Updated())
		) | rpl::map([insufficientStated](
				SendError error,
				bool insufficient,
				bool loading,
				QString loadError,
				bool silent,
				int64 minTransfer,
				rpl::empty_value) {
			if (loading || silent) {
				return QString();
			} else if (!loadError.isEmpty()) {
				return SendUserLoadErrorText(loadError);
			} else if (error != SendError::AmountTooSmall
				&& (insufficient
					|| error == SendError::InsufficientBalance
					|| error == SendError::InsufficientFees)) {
				return insufficientStated
					? SendErrorText(SendError::InsufficientFees, minTransfer)
					: QString();
			} else if (error == SendError::SigningUnavailable) {
				// A device without the key is not stated: the send press
				// acquires it, and the estimate is refused only while no
				// client can serve the preview at all.
				return QString();
			}
			return SendErrorText(error, minTransfer);
		});
	};
	auto refusalText = refusalValue(false);
	const auto refusalWrap = balance->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			balance,
			object_ptr<Ui::FlatLabel>(
				balance,
				rpl::duplicate(refusalText),
				st::walletSendUserErrorLabel)),
		style::al_justify);
	refusalWrap->entity()->setTryMakeSimilarLines(true);
	refusalWrap->toggleOn(std::move(refusalText) | rpl::map([](
			const QString &text) {
		return !text.isEmpty();
	}));
	refusalWrap->finishAnimating();
	const auto depositWrap = balance->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			balance,
			object_ptr<Ui::VerticalLayout>(balance)),
		style::al_justify);
	const auto depositInner = depositWrap->entity();
	const auto deposit = depositInner->add(
		object_ptr<Ui::RoundButton>(
			depositInner,
			tr::lng_wallet_add_funds(),
			st::defaultTableSmallButton),
		style::margins(0, st::walletDetailsAmountMinorSkip, 0, 0),
		style::al_top);
	deposit->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	deposit->setClickedCallback(receive);
	depositWrap->toggleOn(std::move(showInsufficient));
	depositWrap->finishAnimating();

	// This row swaps between a balance, a fee, an error and the deposit
	// button, and some of those swaps are instant while others slide, so
	// between two of them the row would have nothing in it and the whole box
	// would shrink and grow back. It keeps the height of one line whatever
	// it currently shows, which also spares the box the jump it made when
	// the rate and the balance arrived after it was already on screen.
	const auto reserve = balance->add(object_ptr<Ui::RpWidget>(balance));
	reserve->resize(reserve->width(), 0);
	const auto lineHeight = st::walletSendUserBalanceLabel.style.font->height;
	const auto errorHeight = st::walletSendUserErrorLabel.style.font->height;
	// The tallest this row goes: the balance and the fee together, or the
	// insufficient-funds line with the deposit button under it.
	const auto reserved = std::max(
		2 * lineHeight,
		errorHeight
			+ st::walletDetailsAmountMinorSkip
			+ st::defaultTableSmallButton.height);
	balance->heightValue() | rpl::on_next([=](int height) {
		const auto others = height - reserve->height();
		const auto add = std::max(reserved - others, 0);
		if (reserve->height() != add) {
			reserve->resize(reserve->width(), add);
		}
	}, reserve->lifetime());

	const auto requote = [=] {
		if (!draft->quote.current() && !draft->preparing.current()) {
			state->previewDependencies.reset();
		}
		refreshFee();
	};
	const auto refuse = [=](SendError error) {
		if (!weak || state->closed) {
			return;
		}
		stopSending();
		invalidateFee();
		state->previewError = error;
		if (error == SendError::InsufficientBalance
			|| error == SendError::InsufficientFees) {
			state->previewInsufficient = true;
		} else if (error == SendError::None) {
			requote();
		}
	};
	const auto checkQuote = [=] {
		if (!originValid() || !state->flow || draft->preparing.current()) {
			return SendError::InvalidRequest;
		} else if (state->previewError.current() != SendError::None) {
			return state->previewError.current();
		} else if (state->insufficient.current()) {
			return state->amount.current() > wallet->balanceNano()
				? SendError::InsufficientBalance
				: SendError::InsufficientFees;
		}
		const auto quote = draft->quote.current();
		const auto dependencies = quoteDependencies();
		if (!quote
			|| !quote->prepared
			|| quote != draft->quote.current()
			|| quote->revision != state->previewRevision
			|| quote->dependencies != dependencies
			|| quote->args.comment != draft->comment.current()) {
			return SendError::QuoteExpired;
		} else if (draft->privateEpoch
			&& (*draft->privateEpoch != wallet->vault().clearEpoch()
				|| !draft->authorization.valid()
				|| !wallet->vault().unlocked())) {
			return SendError::Locked;
		}
		return SendError::None;
	};
	// The send press acquires the key the way every key-requiring wallet
	// action does, through RunKeyRequiringAction: an unlock when this device
	// holds the key, the backup restore with its protection chooser when the
	// key is only in the cloud, the phrase import when there is no backup,
	// and the conflict resolution first when a different wallet is parked
	// here. The context owns every prompt of that ladder, so closing any of
	// them without a key ends the press quietly, and closing this box ends
	// it with them.
	const auto acquireSendKey = [=] {
		if (state->unlocking) {
			return;
		}
		state->unlocking = true;
		const auto context = std::make_shared<KeyContext>(
			show,
			nullptr,
			originValid,
			crl::guard(session, crl::guard(box, [=](KeyAuthorization auth) {
				state->unlocking = false;
				if (!state->sending.current() || state->submitted) {
					return;
				} else if (!auth.valid()) {
					// A declined key changes nothing the fee was quoted from.
					stopSending();
					requote();
					return;
				}
				state->sendAuthorization = std::move(auth);
				state->continueSend();
			})));
		state->keyContext = context;
		RunKeyRequiringAction(context, [=] {
			if (!context->valid()) {
				context->cancel();
				return;
			}
			AcquireVaultUnlock({
				.show = context,
				.done = [=](KeyAuthorization auth) {
					context->ready(std::move(auth));
				},
			});
		}, KeyActionKind::ResumeAfterRestore, context,
			tr::lng_wallet_restore_text());
	};

	// One press of the send button is one request: the amount, recipient
	// and comment it was pressed with, and at most one unlock. A prepared
	// transfer is bound to the exact gasless terms and balance it was
	// estimated with, and those are refreshed on their own schedule, for
	// example while the unlock prompt is still open. None of that is a
	// change the user made and none of it is shown: the transfer is
	// prepared again under the same authorization and submitted. The same
	// holds when the session refuses a signed transfer as stale, which it
	// does only before anything leaves the device; that retry is bounded
	// so a refusal that keeps repeating cannot spin forever. When the
	// user edits what is being sent, the request just stops quietly.
	// WHY: the transfer now reports itself where the user is: in the wallet
	// window's list when the box is there, otherwise in the recipient's
	// chat, so the box closes instead of reporting anything.
	const auto handOver = [=](SendStarted started) {
		if (state->closed || !sessionValid()) {
			return;
		}
		const auto panel = wallet->panel();
		if (panel && box->window() == panel->window()) {
			state->handedOver = true;
			wallet->setWindowSend(started.operationId);
			if (const auto content = dynamic_cast<Content*>(panel->inner())) {
				content->flySendDiamond(started.operationId, amountField);
			}
			discardOrigin();
			show->hideLayer();
			return;
		} else if (!started.message) {
			return;
		}
		const auto window = MakeChatShow(show, true)->resolveWindow();
		if (!window) {
			return;
		}
		state->handedOver = true;
		discardOrigin();
		box->closeBox();
		window->showPeerHistory(started.message.peer);
		window->window().activate();
	};
	state->continueSend = [=] {
		if (!state->sending.current()
			|| state->submitted
			|| state->unlocking
			|| draft->preparing.current()) {
			return;
		} else if (!originValid() || !state->sendRequest) {
			refuse(SendError::InvalidRequest);
			return;
		}
		const auto request = *state->sendRequest;
		const auto expiresAt = state->sendExpiresAt;
		const auto now = quoteDependencies();
		if (!state->flow
			|| state->flow->expiresAt != expiresAt
			|| now.destination != request.destination
			|| now.bounce != request.bounce
			|| now.amountNano != request.amountNano
			|| now.comment != request.comment) {
			refuse(SendError::None);
			return;
		} else if (TransferLinkExpired(expiresAt)) {
			refuse(SendError::LinkExpired);
			return;
		}
		if (state->heldFee) {
			if (const auto quote = draft->quote.current()) {
				const auto shown = *base::take(state->heldFee);
				if (shown != QuoteConfirmFee(*quote)) {
					const auto held = state->sendAuthorization;
					stopSending();
					state->heldAuthorization = held;
					state->heldTimeout.callOnce(kHeldSendKeyTimeout);
					state->previewError = SendError::QuoteExpired;
					return;
				}
			}
		}
		const auto isPrivate = !request.comment.text.isEmpty()
			&& !request.comment.isPublic;
		const auto error = checkQuote();
		if (error == SendError::SigningUnavailable) {
			// No client could serve the preview: the key is still to be
			// acquired, or the client is being swapped for the signing one
			// right after it was, and the readiness change resumes the
			// press. A ready signing client that still refuses, or a swap
			// that outlasts the bound, is stated as a failure.
			if (!state->sendAuthorization.valid()) {
				acquireSendKey();
			} else if (wallet->signingReady() || state->signingTimedOut) {
				refuse(SendError::Failed);
			} else {
				awaitSigning();
			}
			return;
		} else if (error == SendError::QuoteExpired) {
			if (isPrivate && !state->sendAuthorization.valid()) {
				acquireSendKey();
			} else {
				prepareFee(state->sendAuthorization);
			}
			return;
		} else if (error != SendError::None) {
			refuse(error);
			return;
		} else if (!state->sendAuthorization.valid()) {
			acquireSendKey();
			return;
		} else if (!wallet->signingReady() && !state->signingTimedOut) {
			awaitSigning();
			return;
		}
		const auto accepted = *draft->quote.current();
		const auto authorization = state->sendAuthorization;
		const auto senderIdentity = state->senderIdentity;
		const auto displayForm = state->flow->displayForm;
		state->submitted = true;
		draft->quote = std::nullopt;
		draft->authorization = {};
		draft->privateEpoch.reset();
		const auto valid = [=] {
			return sessionValid()
				&& senderIdentity
				&& wallet->transferWalletIdentityCurrent(*senderIdentity);
		};
		wallet->send(
			authorization,
			accepted.prepared,
			crl::guard(session, [=](SendError error) {
				if (error == SendError::KeyChanged) {
					if (weak && !state->closed && !state->handedOver) {
						box->closeBox();
					}
					if (sessionValid()) {
						ShowWalletKeyChanged(show);
					}
					return;
				} else if (error == SendError::KeyMismatch
						&& (!weak || state->closed || state->handedOver)) {
					if (sessionValid()) {
						show->showToast(
							SendErrorText(error, TransferMinNanos(session)));
					}
					return;
				} else if (!weak || state->closed || !valid()) {
					return;
				} else if (state->handedOver) {
					return;
				} else if (error == SendError::QuoteExpired
					&& ++state->sendRefusals <= kSendRefusalRetries) {
					state->submitted = false;
					invalidateFee();
					scheduleContinueSend();
					return;
				} else if (error == SendError::QuoteExpired) {
					refuse(SendError::Failed);
					return;
				} else if (error != SendError::None
					&& error != SendError::SubmissionUnknown) {
					refuse(error);
					return;
				}
				const auto pending = wallet->pendingSend();
				auto item = pending
					? wallet->submittedTransaction(pending->operationId)
					: std::nullopt;
				if (!item && pending) {
					item = ItemFromPending(*pending);
				}
				const auto toast = (error == SendError::None)
					? tr::lng_wallet_sent_toast(
						tr::now,
						lt_address,
						ShortAddressForm(displayForm))
					: QString();
				discardOrigin();
				show->hideLayer();
				if (!valid()) {
					return;
				}
				if (!toast.isEmpty()) {
					show->showToast(toast);
				}
				if (sent && error == SendError::None) {
					sent();
				} else if (valid() && item) {
					ShowWalletTransactionBox(show, *item);
				}
			}),
			crl::guard(session, crl::guard(box, handOver)));
	};
	const auto startSend = [=] {
		if (!draft->preparing.current()
			&& (!draft->quote.current()
				|| state->previewDependencies != quoteDependencies())) {
			invalidateFee();
		}
		state->sendRequest = quoteDependencies();
		state->sendExpiresAt = state->flow->expiresAt;
		state->sendRefusals = 0;
		const auto held = state->heldAuthorization;
		dropHeldKey();
		if (held.grant && held.grant->valid() && wallet->vault().unlocked()) {
			state->sendAuthorization = held;
		}
		state->sending = true;
		state->continueSend();
	};
	const auto confirmSend = [=](const SendConfirmFee &shown) {
		if (!state->confirmBox
			|| !state->flow
			|| state->sending.current()
			|| !CommentFits(draft->comment.current().text)
			|| !state->canSend.current()) {
			return;
		}
		// WHY: Enter reaches here whatever the button shows, so a press made
		// while counting waits and goes on only at the fee shown; this press
		// answers the previous held press's "review the new fee" refusal.
		if (state->previewError.current() == SendError::QuoteExpired) {
			state->previewError = SendError::None;
		}
		if (draft->preparing.current()) {
			state->heldFee = shown;
		}
		startSend();
	};
	const auto confirmationClosed = [=](not_null<Ui::GenericBox*> raw) {
		if (!weak || state->closed || state->confirmBox.get() != raw.get()) {
			return;
		}
		state->confirmBox = nullptr;
		dropHeldKey();
		if (state->sending.current() && !state->submitted) {
			if (const auto context = base::take(state->keyContext)) {
				context->cancel();
			}
			stopSending();
		}
		invalidateFee();
		state->previewDependencies.reset();
		updateAmount();
		refreshFee();
	};
	const auto openConfirmation = [=] {
		if (state->confirmBox) {
			return;
		}
		auto fee = rpl::combine(
			draft->quote.value(),
			draft->preparing.value()
		) | rpl::map([](
				const std::optional<SendQuote> &quote,
				bool preparing) {
			return quote
				? QuoteConfirmFee(*quote)
				: SendConfirmFee{ .pending = preparing };
		}) | rpl::distinct_until_changed();
		auto confirm = Box(WalletSendConfirmBox, show, SendConfirmArgs{
			.draft = draft,
			.address = state->flow->displayForm,
			.amountNano = state->amount.current(),
			.fee = std::move(fee),
			.refusal = refusalValue(true),
			.busy = state->sending.value(),
			.canSend = state->canSend.value(),
			.send = crl::guard(box, confirmSend),
		});
		const auto raw = confirm.data();
		state->confirmBox = base::make_weak(raw);
		raw->boxClosing() | rpl::on_next(crl::guard(box, [=] {
			confirmationClosed(raw);
		}), raw->lifetime());
		box->uiShow()->showBox(std::move(confirm));
		requote();
	};
	const auto submit = [=] {
		if (state->unfunded.current()) {
			amountField->showError();
			return;
		} else if (!originValid() || !state->flow) {
			if (user && !state->loading.current()) {
				failLoading(userError());
			}
			return;
		} else if (state->sending.current()
			|| state->commentBox
			|| state->confirmBox
			|| !CommentFits(draft->comment.current().text)) {
			return;
		} else if (TransferAmountBelowMinimum(
				state->amount.current(),
				state->minTransfer.current())) {
			// A press below the minimum only corrects the amount.
			raiseToMinimum();
			return;
		} else if (!state->canSend.current()) {
			amountField->showError();
			return;
		}
		if (!user) {
			openConfirmation();
			return;
		}
		startSend();
	};
	auto buttonBusy = rpl::combine(
		state->loading.value(),
		state->sending.value()
	) | rpl::map([](bool loading, bool sending) {
		return loading || sending;
	});
	auto buttonText = BusyFooterLabel(
		rpl::combine(
			state->amount.value(),
			tr::lng_send_button(),
			tr::lng_wallet_send_amount(
				lt_amount,
				state->amount.value() | rpl::map([](int64 amount) {
					return Ui::FormatTonAmount(amount).full;
				}))
		) | rpl::map([](int64 amount, QString empty, QString full) {
			return amount > 0 ? full : empty;
		}),
		rpl::duplicate(buttonBusy));
	const auto button = box->addButton(std::move(buttonText), submit).data();
	rpl::combine(
		state->canSend.value(),
		state->raisable.value(),
		state->sending.value()
	) | rpl::on_next([=](bool canSend, bool raisable, bool sending) {
		SetButtonDisabledLook(button, !canSend && !raisable && !sending);
	}, button->lifetime());
	AddBusyFooterSpinner(button, std::move(buttonBusy));
	amountField->submits() | rpl::on_next(submit, amountField->lifetime());

	if (!user) {
		state->flow = initial;
		state->flow->draft = draft;
		const auto cached = wallet->userAddresses().publicKey(
			state->flow->destination);
		state->recipientKey = (self && cached.isEmpty())
			? state->senderIdentity->publicKey
			: cached;
	}
	refreshFee();
	const auto resolveRecipient = [=] {
		if (state->recipientRequested || !originValid() || !state->flow) {
			return;
		}
		state->recipientRequested = true;
		wallet->resolveCommentRecipient(
			state->flow->destination,
			state->flow->bounce,
			state->recipientKey,
			crl::guard(box, [=](CommentRecipient recipient) {
				if (recipient == CommentRecipient::PlainOnly
					&& !state->closed
					&& !state->terminal) {
					switchToPlain();
				}
			}));
	};
	state->loading.value() | rpl::on_next(resolveRecipient, box->lifetime());

	box->setFocusCallback([=] { amountField->setFocusFast(); });
	if (state->loading.current()) {
		const auto recompute = [=] {
			if (state->closed || state->terminal) {
				return;
			} else if (!sessionValid()) {
				failLoading(u"WALLET_NOT_READY"_q);
				return;
			}
			const auto presence = wallet->presenceCurrent();
			const auto error = user
				? userError()
				: (presence == Presence::Ready)
				? QString()
				: u"WALLET_NOT_READY"_q;
			if (notReady
				&& user
				&& !state->forceIssued
				&& !state->senderIdentity
				&& error == u"WALLET_NOT_READY"_q
				&& presence != Presence::Unknown
				&& presence != Presence::Unavailable) {
				const auto onstack = notReady;
				failLoading(error, true);
				onstack();
				return;
			} else if (error == u"WALLET_NOT_READY"_q
				&& !state->forceIssued
				&& (presence == Presence::Unknown
					|| presence == Presence::Provisioning)) {
				return;
			} else if (user
				&& !state->forceIssued
				&& error == u"WALLET_BALANCE_EMPTY"_q) {
				if (!state->unfunded.current()) {
					state->senderIdentity = wallet->transferWalletIdentity();
					if (!state->senderIdentity) {
						failLoading(u"WALLET_NOT_READY"_q);
						return;
					}
					state->loadDeadline.cancel();
					state->unfunded = true;
					state->loading = false;
				}
				return;
			} else if (!error.isEmpty()) {
				failLoading(presence == Presence::Unavailable
					? u"WALLET_UNAVAILABLE"_q
					: error);
				return;
			}
			if (state->unfunded.current()) {
				state->loading = true;
				state->unfunded = false;
				state->loadDeadline.callOnce(kSendUserLoadTimeout);
			} else if (!state->loading.current()) {
				refreshFee();
				return;
			} else if (state->forceIssued) {
				return;
			}
			state->senderIdentity = wallet->transferWalletIdentity();
			if (!state->senderIdentity) {
				failLoading(u"WALLET_NOT_READY"_q);
				return;
			} else if (!user) {
				state->loadDeadline.cancel();
				state->loading = false;
				return;
			}
			state->forceIssued = true;
			const auto revision = state->loadRevision;
			const auto current = [=] {
				return !state->closed
					&& !state->terminal
					&& revision == state->loadRevision;
			};
			wallet->userAddresses().forceResolve(
				userId,
				crl::guard(session, crl::guard(box, [=](QString address) {
					if (!current()) {
						return;
					}
					const auto error = userError();
					if (!error.isEmpty()) {
						failLoading(error);
						return;
					}
					auto flow = ParseRecipientFlow(FormatFriendly(address, false));
					if (!flow
						|| !user->gramAddress()
						|| *user->gramAddress() != flow->destination
						|| (initial
							&& initial->destination != flow->destination)) {
						failLoading(u"WALLET_ADDRESS_INVALID"_q);
						return;
					}
					flow->draft = draft;
					if (initial) {
						flow->expiresAt = initial->expiresAt;
					}
					state->flow = std::move(flow);
					state->recipientKey
						= wallet->userAddresses().publicKey(address);
					state->loadDeadline.cancel();
					state->loading = false;
				})),
				crl::guard(session, crl::guard(box, [=](ForceResolveError error) {
					if (!current()) {
						return;
					} else if (error.silent) {
						failLoading(error.type, true);
						return;
					}
					const auto changed = userError();
					failLoading(changed.isEmpty() ? error.type : changed);
				})));
		};
		const auto schedule = [=] {
			if (state->closed || state->terminal || state->recomputeQueued) {
				return;
			}
			state->recomputeQueued = true;
			const auto revision = state->loadRevision;
			Ui::PostponeCall(box, [=] {
				state->recomputeQueued = false;
				if (revision == state->loadRevision) {
					recompute();
				}
			});
		};
		state->loadDeadline.setCallback([=] {
			failLoading(state->forceIssued
				? u"WALLET_ADDRESS_INVALID"_q
				: u"WALLET_NOT_READY"_q);
		});
		state->loadDeadline.callOnce(kSendUserLoadTimeout);
		wallet->stateKnownValue() | rpl::on_next(schedule, box->lifetime());
		wallet->balanceNanoValue() | rpl::on_next(schedule, box->lifetime());
		wallet->custodyUpdates() | rpl::on_next(schedule, box->lifetime());
		if (user) {
			wallet->userAddresses().unavailableValue(
			) | rpl::on_next(schedule, box->lifetime());
			user->flagsValue() | rpl::on_next(schedule, box->lifetime());
			session->changes().peerUpdates(
				user,
				Data::PeerUpdate::Flag::FullInfo
					| Data::PeerUpdate::Flag::Name
					| Data::PeerUpdate::Flag::SupportInfo
			) | rpl::on_next(schedule, box->lifetime());
			const auto error = userError();
			if (!error.isEmpty()
				&& error != u"WALLET_NOT_READY"_q
				&& error != u"WALLET_BALANCE_EMPTY"_q) {
				failLoading(error);
				return;
			}
		}
		wallet->presenceValue() | rpl::on_next(schedule, box->lifetime());
		wallet->transferWalletIdentityChanges(
		) | rpl::on_next(schedule, box->lifetime());
		wallet->refreshState();
	}
}

// WHY: an entry point decides about the recipient only from the served
// wallet state, and only the call that ends the wait acts, so an answer
// landing after a timeout or a dismissed busy box changes nothing.
struct SendEntryWait {
	std::shared_ptr<Main::SessionShow> show;
	rpl::variable<QString> text;
	QString timeoutError;
	Fn<void()> closeBusy;
	rpl::lifetime lifetime;
	bool ended = false;
};

bool EndSendEntryWait(const std::shared_ptr<SendEntryWait> &wait) {
	if (wait->ended) {
		return false;
	}
	wait->ended = true;
	wait->lifetime.destroy();
	if (const auto close = base::take(wait->closeBusy)) {
		close();
	}
	return wait->show->valid();
}

void FailSendEntryWait(
		const std::shared_ptr<SendEntryWait> &wait,
		const QString &error) {
	if (EndSendEntryWait(wait)) {
		wait->show->showToast(SendUserLoadErrorText(error));
	}
}

[[nodiscard]] std::shared_ptr<SendEntryWait> StartSendEntryWait(
		std::shared_ptr<Main::SessionShow> show) {
	const auto session = &show->session();
	const auto wait = std::make_shared<SendEntryWait>();
	wait->show = show;
	wait->text = tr::lng_wallet_send_wallet_loading(tr::now);
	wait->timeoutError = u"WALLET_NOT_READY"_q;
	const auto weak = std::weak_ptr<SendEntryWait>(wait);
	base::call_delayed(kSendOwnerLookupDelay, session, [=] {
		const auto strong = weak.lock();
		if (!strong || strong->ended || !show->valid()) {
			return;
		}
		strong->closeBusy = ShowWalletBusyBox(
			show,
			strong->text.value(),
			[=] {
				if (const auto strong = weak.lock()) {
					strong->closeBusy = nullptr;
					EndSendEntryWait(strong);
				}
			});
	});
	base::call_delayed(kSendUserLoadTimeout, session, [=] {
		if (const auto strong = weak.lock()) {
			FailSendEntryWait(strong, strong->timeoutError);
		}
	});
	return wait;
}

void AwaitWalletReady(
		const std::shared_ptr<SendEntryWait> &wait,
		Fn<void()> ready,
		Fn<void()> notReady = nullptr) {
	const auto session = &wait->show->session();
	const auto wallet = &session->wallet();
	if (wallet->presence() == Presence::Ready) {
		ready();
		return;
	}
	wallet->refreshState();
	// A caller with its own answer shows a wallet still being created.
	const auto waitCreated = !notReady;
	wallet->presenceValue(
	) | rpl::filter([=](Presence presence) {
		return (presence != Presence::Unknown)
			&& (!waitCreated || presence != Presence::Provisioning);
	}) | rpl::take(1) | rpl::on_next([=](Presence presence) {
		if (presence == Presence::Ready) {
			// The update that made the wallet Ready finishes first.
			crl::on_main(session, [=] {
				if (!wait->show->valid()) {
					EndSendEntryWait(wait);
				} else if (!wait->ended) {
					ready();
				}
			});
		} else if (notReady) {
			if (EndSendEntryWait(wait)) {
				notReady();
			}
		} else {
			FailSendEntryWait(wait, (presence == Presence::Unavailable)
				? u"WALLET_UNAVAILABLE"_q
				: u"WALLET_NOT_READY"_q);
		}
	}, wait->lifetime);
}

void WhenWalletReady(
		std::shared_ptr<Main::SessionShow> show,
		Fn<void()> ready,
		Fn<void()> notReady = nullptr) {
	const auto wait = StartSendEntryWait(std::move(show));
	AwaitWalletReady(wait, [=] {
		if (EndSendEntryWait(wait)) {
			ready();
		}
	}, std::move(notReady));
}

void OpenSendFlow(
		std::shared_ptr<Main::SessionShow> show,
		SendFlow flow,
		AddressOwner owner,
		base::weak_qptr<Ui::BoxContent> origin = nullptr) {
	if (TransferLinkExpired(flow.expiresAt)) {
		show->showToast(tr::lng_wallet_send_link_expired(tr::now));
		return;
	}
	// WHY: a wallet Telegram names an owner for may not be deployed yet,
	// and a bounceable message to one is returned instead of delivered.
	if (owner.userId) {
		flow.bounce = false;
		flow.displayForm = FormatFriendly(flow.destination, false);
	}
	const auto session = &show->session();
	const auto user = SendableUser(session, owner.userId);
	const auto toUser = user
		&& !user->isSelf()
		&& (!user->gramAddress()
			|| *user->gramAddress() == flow.destination);
	show->showBox(Box(
		WalletSendBox,
		show,
		std::make_optional(std::move(flow)),
		toUser ? user : nullptr,
		Fn<void()>(),
		Fn<void()>(),
		int64(0),
		origin));
}

// A transfer to a user is gasless, even when the link named a wallet.
void ResolveOwnerAndOpenSendFlow(
		std::shared_ptr<Main::SessionShow> show,
		SendFlow flow) {
	const auto session = &show->session();
	const auto wait = StartSendEntryWait(show);
	AwaitWalletReady(wait, [=] {
		if (SendsToOwnWallet(session, flow.destination)) {
			if (EndSendEntryWait(wait)) {
				OpenSendFlow(show, flow, AddressOwner());
			}
			return;
		}
		wait->text = tr::lng_wallet_send_recipient_loading(tr::now);
		wait->timeoutError = u"WALLET_ADDRESS_INVALID"_q;
		session->wallet().userAddresses().resolveOwner(
			flow.destination,
			crl::guard(session, [=](AddressOwner owner) {
				if (EndSendEntryWait(wait)) {
					OpenSendFlow(show, flow, std::move(owner));
				}
			}),
			crl::guard(session, [=](bool silent) {
				if (silent) {
					EndSendEntryWait(wait);
				} else {
					FailSendEntryWait(wait, u"WALLET_ADDRESS_INVALID"_q);
				}
			}));
	});
}

[[nodiscard]] CollectibleRecipient CollectibleRecipientFrom(
		not_null<Main::Session*> session,
		const SendFlow &flow,
		const AddressOwner &owner) {
	const auto user = SendableUser(session, owner.userId);
	const auto toUser = user
		&& !user->isSelf()
		&& (!user->gramAddress()
			|| *user->gramAddress() == flow.destination);
	return {
		.destination = flow.destination,
		.tonName = flow.tonName,
		.userId = toUser ? owner.userId : UserId(),
		.bounce = owner.userId ? false : flow.bounce,
	};
}

void WalletSendRecipientBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		QString text,
		std::shared_ptr<const CollectibleTransfer> collectible) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setTitle(collectible
		? tr::lng_gift_transfer_title(
			lt_name,
			CollectibleNameValue(collectible->media, collectible->address))
		: tr::lng_wallet_send_title());
	AddBoxCloseButton(box);

	const auto session = &show->session();

	struct State {
		std::optional<SendFlow> flow;
		QString name;
		rpl::variable<bool> valid = false;
		rpl::variable<bool> invalid = false;
		rpl::variable<RecipientError> error = RecipientError::Invalid;
		rpl::variable<bool> resolving = false;
		rpl::variable<QString> search;
		rpl::variable<bool> recentHidden = false;
		base::Timer deadline;
		Fn<void(not_null<UserData*>)> chooseUser;
		uint64 revision = 0;
		bool closed = false;
		bool searchCreated = false;
		bool proceedOnName = false;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto lookup = box->lifetime().make_state<TonNameLookup>(session);
	const auto choose = [=](not_null<UserData*> user) {
		state->chooseUser(user);
	};

	const auto recipient = box->addRow(
		object_ptr<Ui::VerticalLayout>(box),
		style::margins(),
		style::al_justify);
	const auto field = AddSendField(
		recipient,
		st::walletSendField,
		tr::lng_wallet_details_recipient(),
		text);
	const auto errorWrap = recipient->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			recipient,
			object_ptr<Ui::FlatLabel>(
				recipient,
				(state->error.value()
					| rpl::map(RecipientErrorText)
					| rpl::flatten_latest()),
				st::walletSendErrorLabel)),
		style::margins(
			st::walletSendFieldMargin.left(),
			0,
			st::walletSendFieldMargin.right(),
			0));
	errorWrap->toggleOn(state->invalid.value());
	errorWrap->finishAnimating();
	recipient->add(MakeRecentMoneyRecipientsList(
		box,
		show,
		state->recentHidden.value(),
		choose));

	const auto stop = [=] {
		++state->revision;
		state->deadline.cancel();
		state->resolving = false;
	};
	const auto parse = [=] {
		stop();
		state->proceedOnName = false;
		const auto trimmed = field->getLastText().trimmed();
		auto input = ClassifyRecipientInput(trimmed);
		state->flow = std::move(input.flow);
		state->name = (input.kind == RecipientInputKind::Name)
			? trimmed
			: QString();
		state->valid = state->flow.has_value() || !state->name.isEmpty();
		state->invalid = (input.kind == RecipientInputKind::Invalid);
		if (state->invalid.current()) {
			state->error = RecipientError::Invalid;
		}
		const auto searching = (input.kind == RecipientInputKind::Search);
		if (searching && !state->searchCreated) {
			state->searchCreated = true;
			recipient->add(MakeMoneyRecipientSearchList(
				box,
				show,
				state->search.value(),
				choose));
			recipient->resizeToWidth(recipient->width());
		}
		state->search = searching ? trimmed : QString();
		state->recentHidden = searching || !state->name.isEmpty();
		lookup->setName(state->name);
	};
	const auto proceed = [=](SendFlow flow, AddressOwner owner) {
		stop();
		if (state->closed
			|| !show->valid()
			|| &show->session() != session) {
			return;
		} else if (collectible) {
			show->showBox(Box(
				CollectibleTransferBox,
				show,
				collectible,
				CollectibleRecipientFrom(session, flow, owner)));
			return;
		}
		OpenSendFlow(show, std::move(flow), std::move(owner), box.get());
	};
	const auto failName = [=](RecipientError error) {
		stop();
		state->error = error;
		state->invalid = true;
	};
	const auto lookupOwner = [=](SendFlow flow) {
		if (SendsToOwnWallet(session, flow.destination)) {
			if (collectible) {
				failName(RecipientError::OwnWallet);
			} else {
				proceed(flow, AddressOwner());
			}
			return;
		}
		const auto revision = ++state->revision;
		const auto answer = [=](AddressOwner owner) {
			if (revision == state->revision) {
				proceed(flow, std::move(owner));
			}
		};
		const auto fail = [=](bool silent) {
			if (revision != state->revision) {
				return;
			} else if (silent) {
				stop();
			} else {
				failName(RecipientError::LookupFailed);
			}
		};
		state->resolving = true;
		state->deadline.setCallback([=] { fail(false); });
		// resolveOwner has no deadline of its own.
		state->deadline.callOnce(kSendUserLoadTimeout);
		session->wallet().userAddresses().resolveOwner(
			flow.destination,
			crl::guard(session, crl::guard(box, answer)),
			crl::guard(session, crl::guard(box, fail)));
	};
	if (!collectible) {
		state->chooseUser = [=](not_null<UserData*> user) {
			ChooseMoneyRecipient(box, show, user);
		};
	} else {
		state->chooseUser = [=](not_null<UserData*> user) {
			if (state->closed || state->resolving.current()) {
				return;
			} else if (user->isSelf()) {
				failName(RecipientError::OwnWallet);
				return;
			}
			const auto userId = peerToUser(user->id);
			const auto revision = ++state->revision;
			const auto answer = [=](QString address) {
				if (revision != state->revision) {
					return;
				}
				const auto flow = ParseRecipientFlow(
					FormatFriendly(address, false));
				if (!flow) {
					failName(RecipientError::LookupFailed);
				} else if (SendsToOwnWallet(session, flow->destination)) {
					failName(RecipientError::OwnWallet);
				} else {
					proceed(*flow, AddressOwner{
						.userId = userId,
						.address = address,
					});
				}
			};
			const auto fail = [=](ForceResolveError error) {
				if (revision != state->revision) {
					return;
				}
				stop();
				if (!error.silent) {
					show->showToast(SendUserLoadErrorText(error.type));
				}
			};
			state->resolving = true;
			state->deadline.setCallback([=] {
				if (revision == state->revision) {
					failName(RecipientError::LookupFailed);
				}
			});
			state->deadline.callOnce(kSendUserLoadTimeout);
			session->wallet().userAddresses().forceResolve(
				userId,
				crl::guard(session, crl::guard(box, answer)),
				crl::guard(session, crl::guard(box, fail)));
		};
	}
	const auto proceedName = [=](const TonNameState &value) {
		if (auto flow = ParseRecipientFlow(value.address)) {
			flow->tonName = value.name;
			lookupOwner(std::move(*flow));
		} else {
			failName(RecipientError::NameFailed);
		}
	};
	const auto submit = [=] {
		if (state->closed || state->resolving.current()) {
			return;
		} else if (!state->name.isEmpty()) {
			if (lookup->current().status == TonNameStatus::Resolved) {
				proceedName(lookup->current());
			} else {
				state->proceedOnName = true;
				state->resolving = true;
				lookup->request();
			}
			return;
		} else if (!state->flow) {
			field->showError();
			return;
		} else if (TransferLinkExpired(state->flow->expiresAt)) {
			show->showToast(tr::lng_wallet_send_link_expired(tr::now));
			return;
		}
		lookupOwner(*state->flow);
	};
	recipient->add(MakeTonNameResultList(
		box,
		session,
		lookup->value(),
		submit));
	lookup->value() | rpl::on_next([=](const TonNameState &value) {
		const auto status = value.status;
		if (status == TonNameStatus::Pending) {
			state->invalid = false;
		} else if ((status == TonNameStatus::NotFound)
			|| (status == TonNameStatus::Failed)) {
			state->proceedOnName = false;
			failName((status == TonNameStatus::NotFound)
				? RecipientError::NameNotFound
				: RecipientError::NameFailed);
		} else if ((status == TonNameStatus::Resolved)
			&& base::take(state->proceedOnName)) {
			proceedName(value);
		}
	}, box->lifetime());

	const auto button = box->addButton(
		BusyFooterLabel(tr::lng_continue(), state->resolving.value()),
		submit).data();
	state->valid.value() | rpl::on_next([=](bool valid) {
		SetButtonDisabledLook(button, !valid);
	}, button->lifetime());
	AddBusyFooterSpinner(button, state->resolving.value());

	field->changes() | rpl::on_next(parse, field->lifetime());
	field->submits() | rpl::on_next(submit, field->lifetime());
	box->boxClosing() | rpl::on_next([=] {
		state->closed = true;
		stop();
		lookup->close();
	}, box->lifetime());
	box->setFocusCallback([=] { field->setFocusFast(); });
	parse();
}

[[nodiscard]] QString PhraseBoxLottie(int wordsCount) {
	return (wordsCount > kImportWordCountShort)
		? QString()
		: u"wallet/paper"_q;
}

void AddPhraseBoxHeader(
		not_null<Ui::GenericBox*> box,
		const QString &lottieName,
		rpl::producer<QString> title,
		rpl::producer<TextWithEntities> text,
		const style::FlatLabel &textLabel,
		int lottieSize,
		const style::margins &lottieMargin,
		const style::margins &textMargin) {
	if (!lottieName.isEmpty()) {
		auto icon = Settings::CreateLottieIcon(
			box->verticalLayout(),
			{
				.name = lottieName,
				.sizeOverride = { lottieSize, lottieSize },
			},
			lottieMargin);
		box->verticalLayout()->add(std::move(icon.widget));
		box->showFinishes(
		) | rpl::on_next([animate = std::move(icon.animate)] {
			animate(anim::repeat::once);
		}, box->lifetime());
	}
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			std::move(title),
			st::walletPhraseTitleLabel),
		(lottieName.isEmpty()
			? st::walletPhraseTitlePadding
			: st::boxRowPadding),
		style::al_top);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			std::move(text),
			textLabel),
		textMargin,
		style::al_top
	)->setTryMakeSimilarLines(true);
}

void AddPhraseGrid(
		not_null<Ui::GenericBox*> box,
		const std::vector<QString> &words) {
	const auto count = int(words.size());
	const auto grid = box->addRow(
		object_ptr<Ui::RpWidget>(box),
		st::walletPhraseGridPadding);
	const auto rows = count / 2;
	auto numberLabels = std::vector<Ui::FlatLabel*>();
	auto wordLabels = std::vector<Ui::FlatLabel*>();
	for (auto i = 0; i != count; ++i) {
		numberLabels.push_back(Ui::CreateChild<Ui::FlatLabel>(
			grid,
			QString::number(i + 1) + QChar('.'),
			st::walletPhraseNumberLabel));
		wordLabels.push_back(Ui::CreateChild<Ui::FlatLabel>(
			grid,
			words[i],
			st::walletPhraseWordLabel));
	}
	const auto rowHeight = wordLabels.front()->height();
	grid->resize(
		grid->width(),
		rows * rowHeight + (rows - 1) * st::walletPhraseRowSkip);
	grid->widthValue(
	) | rpl::on_next([=](int width) {
		const auto column = (width - st::walletPhraseColumnSkip) / 2;
		for (auto i = 0; i != int(wordLabels.size()); ++i) {
			const auto x = (i < rows)
				? 0
				: (column + st::walletPhraseColumnSkip);
			const auto y = (i % rows)
				* (rowHeight + st::walletPhraseRowSkip);
			numberLabels[i]->moveToLeft(x, y, width);
			wordLabels[i]->moveToLeft(
				x + st::walletPhraseNumberWidth,
				y,
				width);
		}
	}, grid->lifetime());
}

void WalletPhraseBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		std::vector<QString> words) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	const auto count = int(words.size());
	AddPhraseBoxHeader(
		box,
		PhraseBoxLottie(count),
		tr::lng_wallet_phrase_title(),
		tr::lng_wallet_phrase_text(
			lt_count,
			rpl::single(count * 1.) | tr::to_count(),
			tr::marked),
		st::walletPhraseTextLabel,
		st::walletCoverLottieSize,
		st::walletPhraseLottieMargin,
		st::walletPhraseTextMargin);

	AddPhraseGrid(box, words);

	AddBoxCloseButton(box);
	box->addButton(tr::lng_about_done(), [=] { box->closeBox(); });
	SubmitBoxOnEnter(box, [=] { box->closeBox(); });
}

[[nodiscard]] TextWithEntities EnforcementCheckAbout(const QString &error) {
	auto result = tr::marked();
	const auto prefixes = {
		u"PASSWORD_TOO_FRESH_"_q,
		u"SESSION_TOO_FRESH_"_q,
	};
	for (const auto &prefix : prefixes) {
		if (!error.startsWith(prefix)) {
			continue;
		}
		const auto seconds = error.mid(prefix.size()).toInt();
		if (seconds > 0) {
			result.append(tr::lng_wallet_phrase_check_wait(
				tr::now,
				lt_duration,
				tr::marked(Ui::FormatResetCloudPasswordIn(seconds)),
				tr::marked)
			).append(QChar('\n')).append(QChar('\n'));
		}
		break;
	}
	result.append(tr::lng_bots_password_confirm_check_about(
		tr::now,
		tr::marked));
	return result;
}

enum class PhraseOperation {
	Reveal,
	Restore,
	DropParked,
};

void ShowPhraseError(
		std::shared_ptr<Main::SessionShow> show,
		PhraseOperation operation,
		const QString &error) {
	const auto text = [&] {
		switch (operation) {
		case PhraseOperation::Reveal:
			return tr::lng_wallet_phrase_error(tr::now);
		case PhraseOperation::Restore:
			return tr::lng_wallet_restore_error(tr::now);
		case PhraseOperation::DropParked:
			return tr::lng_wallet_conflict_switch_error(tr::now);
		}
		Unexpected("Operation in ShowPhraseError.");
	}();
	const auto &wallet = show->session().wallet();
	const auto identity = wallet.transferWalletIdentity();
	LOG(("Wallet Error: key access toast operation=%1 error=%2 "
		"address=%3 key=%4 revision=%5 presence=%6 device_mode=%7 "
		"conflict=%8."
		).arg((operation == PhraseOperation::Reveal)
			? u"reveal"_q
			: (operation == PhraseOperation::Restore)
			? u"restore"_q
			: u"drop_parked"_q
		).arg(error
		).arg(identity ? identity->address : u"(none)"_q
		).arg(QString::fromLatin1(wallet.publicKey().toHex())
		).arg(identity ? identity->revision : 0
		).arg(int(wallet.presenceCurrent())
		).arg(int(wallet.deviceCustodyState().mode)
		).arg(wallet.deviceCustodyState().conflict));
	show->showToast(ErrorWithType(text, error));
}

// ReadOnlyRestorable ends as soon as custody is installed, so an entry that
// reaches the cloud password box in that mode is by construction the first
// key use on this device: that box then carries the lock-and-key header
// explaining what the restore is about to do, while any other mode keeps
// the plain prompt with the given description.
[[nodiscard]] bool RestoreIsFirstKeyUse(not_null<Main::Session*> session) {
	const auto state = session->wallet().deviceCustodyState();
	return (state.mode == DeviceMode::ReadOnlyRestorable);
}

[[nodiscard]] PasscodeBox::CloudFields RestorePasswordFields(
		const Core::CloudPasswordState &state,
		bool firstKeyUse,
		const QString &description) {
	auto result = PasscodeBox::CloudFields::From(state);
	result.customSubmitButton = tr::lng_passcode_submit();
	if (firstKeyUse) {
		result.customHeader = PasscodeBox::CloudFields::CustomHeader{
			.lottie = u"cloud_password/intro"_q,
			.lottieSize = st::walletHowLottieSize,
			.lottieMargin = st::walletHowLottieMargin,
			.title = tr::lng_settings_cloud_password_check_subtitle(),
			.description = tr::lng_wallet_restore_explain_text(),
		};
	} else {
		result.customTitle = tr::lng_bots_password_confirm_title();
		result.customDescription = description;
	}
	return result;
}

// The backup export is sent without a password first, even when the account
// has one, because the server decides whether this export needs it. Only
// its PASSWORD_MISSING answer goes to onPasswordMissing, which asks for the
// cloud password and repeats the request with it; that repeat carries its
// password box here, so its password errors go back to that box.
void RequestPhraseReveal(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> warning,
		KeyAuthorization auth,
		std::optional<Core::CloudPasswordResult> password,
		base::weak_qptr<PasscodeBox> passcode,
		Fn<void()> unblock,
		std::optional<QByteArray> parkedKey = std::nullopt,
		Fn<void(std::vector<QString>)> onWords = nullptr,
		Fn<void()> onAuthorized = nullptr,
		Fn<void(std::vector<QString>, CustodyOutcome)> onPrepared = nullptr,
		Fn<void()> onPromptError = nullptr,
		Fn<void()> onPasswordMissing = nullptr) {
	auto &wallet = show->session().wallet();
	if (!parkedKey && passcode && wallet.revealsLocally()) {
		const auto box = base::take(passcode);
		password.reset();
		box->closeBox();
	}
	const auto done = crl::guard(warning, [=](
			std::vector<QString> words,
			CustodyOutcome outcome) {
		if (onPrepared) {
			onPrepared(std::move(words), outcome);
			return;
		}
		if (passcode) {
			passcode->closeBox();
		}
		if (onWords) {
			onWords(std::move(words));
		} else {
			warning->closeBox();
			show->showBox(Box(WalletPhraseBox, show, std::move(words)));
		}
		if (outcome != CustodyOutcome::Installed) {
			show->showToast(tr::lng_wallet_restore_not_saved(tr::now));
		}
	});
	const auto fail = crl::guard(warning, [=](const QString &error) {
		if (passcode && passcode->handleCustomCheckError(error)) {
			passcode->showLoading(false);
			if (onPromptError) {
				onPromptError();
			} else {
				unblock();
			}
			return;
		}
		if (onPasswordMissing && error == u"PASSWORD_MISSING"_q) {
			onPasswordMissing();
			return;
		}
		unblock();
		if (!onWords && !onPrepared) {
			warning->closeBox();
		}
		if (error == u"PHRASE_VAULT_LOCKED"_q) {
			if (passcode) {
				passcode->closeBox();
			}
			show->showToast(tr::lng_wallet_vault_locked(tr::now));
			return;
		}
		if (auto box = PrePasswordErrorBox(
				error,
				&show->session(),
				EnforcementCheckAbout(error))) {
			if (passcode) {
				passcode->closeBox();
			}
			show->showBox(std::move(box));
			return;
		}
		if (passcode) {
			passcode->closeBox();
		}
		ShowPhraseError(show, PhraseOperation::Reveal, error);
	});
	auto authorized = Fn<void()>();
	if (onAuthorized) {
		authorized = crl::guard(warning, [=] {
			onAuthorized();
			if (passcode) {
				passcode->closeBox();
			}
		});
	}
	if (parkedKey) {
		wallet.revealParked(std::move(auth), *parkedKey, done, fail);
	} else {
		wallet.revealPhrase(
			std::move(auth),
			std::move(password),
			done,
			fail,
			std::move(authorized));
	}
}

void StartPhraseReveal(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> warning,
		KeyAuthorization auth,
		Fn<void()> unblock,
		std::optional<QByteArray> parkedKey = std::nullopt,
		Fn<void(std::vector<QString>)> onWords = nullptr,
		Fn<void()> onAuthorized = nullptr,
		Fn<void(std::vector<QString>, CustodyOutcome)> onPrepared = nullptr,
		Fn<void()> onPromptError = nullptr,
		Fn<void()> onPromptClosed = nullptr,
		Fn<bool()> onPromptSubmit = nullptr) {
	const auto session = &show->session();
	// A parked key or a key held on this device goes straight to the words:
	// neither asks the server, so neither can be asked for a password, and
	// neither reports the backup's answer or hands its words over prepared,
	// because only a key restored from Telegram's backup is stored.
	if (parkedKey || session->wallet().revealsLocally()) {
		RequestPhraseReveal(
			show,
			warning,
			std::move(auth),
			std::nullopt,
			nullptr,
			unblock,
			parkedKey,
			onWords);
		return;
	}
	// Only this branch restores the key to the device, so only its password
	// box carries the first-use explanation header.
	const auto firstKeyUse = RestoreIsFirstKeyUse(session);
	const auto askPassword = crl::guard(warning, [=] {
		const auto cached = session->api().cloudPassword().stateCurrent();
		LOG(("Wallet Info: phrase reveal requires password; "
			"cached_state=%1 cached_has_password=%2."
			).arg(cached.has_value()).arg(cached && cached->hasPassword));
		session->api().cloudPassword().reload();
		session->api().cloudPassword().state(
		) | rpl::take(
			1
		) | rpl::on_next([=](const Core::CloudPasswordState &state) {
			if (!state.hasPassword) {
				// The server asked for a password this account does not
				// have, so a repeat without one would only be refused again.
				unblock();
				if (!onWords && !onPrepared) {
					warning->closeBox();
				}
				ShowPhraseError(
					show,
					PhraseOperation::Reveal,
					u"PHRASE_PASSWORD_STATE_MISSING"_q);
				return;
			}
			auto fields = RestorePasswordFields(
				state,
				firstKeyUse,
				tr::lng_wallet_phrase_password_description(tr::now));
			fields.customCheckCallback = [=](
					const Core::CloudPasswordResult &result,
					base::weak_qptr<PasscodeBox> passcode) {
				if (onPromptSubmit && !onPromptSubmit()) {
					return;
				}
				if (passcode) {
					passcode->showLoading(true);
				}
				RequestPhraseReveal(
					show,
					warning,
					auth,
					result,
					passcode,
					unblock,
					std::nullopt,
					onWords,
					onAuthorized,
					onPrepared,
					onPromptError);
			};
			const auto passcode = show->show(
				Box<PasscodeBox>(session, fields));
			if (passcode) {
				passcode->boxClosing(
				) | rpl::on_next([=] {
					if (onPromptClosed) {
						onPromptClosed();
					}
				}, warning->lifetime());
			}
		}, warning->lifetime());
	});
	RequestPhraseReveal(
		show,
		warning,
		std::move(auth),
		std::nullopt,
		nullptr,
		unblock,
		std::nullopt,
		onWords,
		onAuthorized,
		onPrepared,
		onPromptError,
		askPassword);
}

void WalletPhraseWarningBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		std::optional<QByteArray> parkedKey) {
	enum class Phase {
		Idle,
		Starting,
		Authorizing,
		Loading,
		Ready,
	};
	struct State {
		Phase phase = Phase::Idle;
		rpl::variable<bool> loading = false;
		bool armed = false;
		bool pointerDown = false;
		bool absorbEnter = false;
		std::optional<std::vector<QString>> words;
		CustodyOutcome outcome = CustodyOutcome::Installed;
	};
	const auto state = box->lifetime().make_state<State>();
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::walletPillBox);
	box->setNoContentMargin(true);

	AddPhraseBoxHeader(
		box,
		u"wallet/paper"_q,
		tr::lng_wallet_phrase_intro_title(),
		tr::lng_wallet_phrase_intro_text(tr::marked),
		st::walletPhraseIntroTextLabel,
		st::walletCoverLottieSize,
		st::walletPhraseLottieMargin,
		st::walletPhraseTextMargin);

	const auto container = box->verticalLayout();
	const auto addWarning = [&](
			rpl::producer<TextWithEntities> text,
			const style::icon &icon) {
		const auto label = container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				std::move(text),
				st::walletPhraseWarnLabel),
			st::walletPhraseWarnPadding);
		const auto left = Ui::CreateChild<Ui::RpWidget>(container);
		left->paintRequest(
		) | rpl::on_next([=] {
			auto p = Painter(left);
			icon.paint(p, 0, 0, left->width());
		}, left->lifetime());
		left->resize(icon.size());
		label->geometryValue(
		) | rpl::on_next([=](const QRect &g) {
			const auto oneLine = LabelLineHeight(st::walletPhraseWarnLabel);
			const auto shift = (g.height() > oneLine)
				? st::walletPhraseWarnIconSkip
				: (g.height() - left->height()) / 2;
			left->moveToLeft(
				(g.left() - left->width()) / 2,
				g.top() + shift);
		}, left->lifetime());
	};
	Ui::AddSkip(container);
	addWarning(
		tr::lng_wallet_phrase_warn_share(tr::rich),
		st::walletPhraseWarnShareIcon);
	Ui::AddSkip(container, st::walletPhraseWarnRowSkip);
	addWarning(
		tr::lng_wallet_phrase_warn_steal(tr::rich),
		st::walletPhraseWarnStealIcon);
	Ui::AddSkip(container, st::walletPhraseWarnRowSkip);
	addWarning(
		tr::lng_wallet_phrase_warn_support(tr::rich),
		st::walletPhraseWarnSupportIcon);
	Ui::AddSkip(container);

	AddBoxCloseButton(box);
	const auto idleFromPrompt = [=] {
		if (state->phase == Phase::Starting
			|| state->phase == Phase::Authorizing) {
			state->phase = Phase::Idle;
			state->loading = false;
			state->armed = false;
			state->absorbEnter = false;
			state->words.reset();
		}
	};
	const auto idleFromFail = [=] {
		if (state->phase == Phase::Ready) {
			return;
		}
		state->phase = Phase::Idle;
		state->loading = false;
		state->armed = false;
		state->absorbEnter = false;
		state->words.reset();
	};
	const auto button = box->addButton(BusyFooterLabel(
		tr::lng_wallet_keys_show_phrase(),
		state->loading.value()));
	button->setClickedCallback([=] {
		if (state->phase == Phase::Authorizing
			|| state->phase == Phase::Loading
			|| state->phase == Phase::Starting) {
			return;
		} else if (state->phase == Phase::Ready) {
			if (!state->armed || !state->words) {
				return;
			}
			auto words = *state->words;
			state->words.reset();
			box->closeBox();
			show->showBox(Box(WalletPhraseBox, show, std::move(words)));
			return;
		}
		state->phase = Phase::Starting;
		state->loading = true;
		const auto start = [=](KeyAuthorization auth) {
			StartPhraseReveal(
				show,
				box,
				std::move(auth),
				idleFromFail,
				parkedKey,
				nullptr,
				[=] {
					if (state->phase != Phase::Starting
						&& state->phase != Phase::Authorizing) {
						return;
					}
					state->phase = Phase::Loading;
					state->armed = false;
					state->loading = true;
				},
				[=](std::vector<QString> words, CustodyOutcome outcome) {
					if (state->phase != Phase::Loading) {
						return;
					}
					// A stored key always went through the protection
					// chooser, and its Save is the explicit activation that
					// shows the words right away: nothing typed into the
					// password box can carry past it. A store that was
					// cancelled or failed keeps the words behind a fresh
					// press of this button instead.
					if (outcome == CustodyOutcome::Installed) {
						state->phase = Phase::Idle;
						state->loading = false;
						box->closeBox();
						show->showBox(
							Box(WalletPhraseBox, show, std::move(words)));
						return;
					}
					state->words = std::move(words);
					state->outcome = outcome;
					state->phase = Phase::Ready;
					state->armed = !state->pointerDown;
					state->absorbEnter = true;
					state->loading = false;
					if (const auto strong = button.data()) {
						strong->clearState();
					}
					if (outcome != CustodyOutcome::Installed) {
						show->showToast(
							tr::lng_wallet_restore_not_saved(tr::now));
					}
				},
				[=] {
					if (state->phase == Phase::Authorizing) {
						state->phase = Phase::Starting;
					}
				},
				idleFromPrompt,
				[=] {
					if (state->phase == Phase::Authorizing
						|| state->phase == Phase::Loading
						|| state->phase == Phase::Ready) {
						return false;
					}
					state->phase = Phase::Authorizing;
					return true;
				});
		};
		// A key restored from Telegram's backup is stored through the install
		// ladder, whose chooser asks for the vault itself, so that path
		// carries no read grant and asks nothing before the backup is
		// fetched: the cloud password box comes only when the server asks
		// for it. A key held on this device is read right after its unlock,
		// whose submit is the explicit activation, as the chooser's Save is.
		if (!parkedKey && !show->session().wallet().revealsLocally()) {
			start(KeyAuthorization{ .install = MakeCustodyInstaller(show) });
			return;
		}
		AcquireVaultUnlock({
			.show = show,
			.done = crl::guard(box, [=](KeyAuthorization auth) {
				if (state->phase != Phase::Starting) {
					return;
				} else if (!auth.valid()) {
					idleFromPrompt();
					return;
				}
				start(std::move(auth));
			}),
		});
	});
	AddBusyFooterSpinner(button, state->loading.value());
	const auto isRevealKey = [](int key) {
		return key == Qt::Key_Return
			|| key == Qt::Key_Enter
			|| key == Qt::Key_Space;
	};
	const auto armAfterRelease = [=] {
		crl::on_main(box, [=] {
			if (state->phase == Phase::Ready && !state->pointerDown) {
				state->armed = true;
			}
		});
	};
	const auto filterRevealKey = [=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type != QEvent::KeyPress && type != QEvent::KeyRelease) {
			return base::EventFilterResult::Continue;
		}
		const auto keyEvent = static_cast<QKeyEvent*>(e.get());
		if (!isRevealKey(keyEvent->key())) {
			return base::EventFilterResult::Continue;
		}
		if (type == QEvent::KeyRelease) {
			state->absorbEnter = false;
			if (state->phase == Phase::Ready) {
				armAfterRelease();
			}
			return base::EventFilterResult::Continue;
		}
		if (state->phase == Phase::Ready
			&& (state->absorbEnter || keyEvent->isAutoRepeat())) {
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	};
	base::install_event_filter(button.data(), [=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::MouseButtonPress) {
			state->pointerDown = true;
			if (state->phase == Phase::Ready) {
				state->absorbEnter = false;
				state->armed = true;
			}
		} else if (type == QEvent::MouseButtonRelease) {
			state->pointerDown = false;
			if (state->phase == Phase::Ready) {
				armAfterRelease();
			}
		}
		return filterRevealKey(e);
	});
	base::install_event_filter(box, [=](not_null<QEvent*> e) {
		return filterRevealKey(e);
	});
	SubmitBoxOnEnter(box, [=] {
		if (const auto strong = button.data()) {
			strong->clicked(Qt::KeyboardModifiers(), Qt::LeftButton);
		}
	});
	show->session().wallet().transferWalletIdentityChanges(
	) | rpl::on_next([=] {
		state->words.reset();
		state->armed = false;
		state->absorbEnter = false;
		if (state->phase != Phase::Idle) {
			state->phase = Phase::Idle;
			state->loading = false;
		}
	}, box->lifetime());
}

// The warning sheet comes first and its Show press acquires what the reveal
// needs: the vault unlock for a key held on this device, so the box order
// is warning, passcode, phrase, or warning, phrase for an open or retained
// vault; the install ladder for a key restored from Telegram's backup, with
// the cloud password box before it when the server asks for one, and the
// chooser at the store.
void WalletRevealFlow(
		std::shared_ptr<Main::SessionShow> show,
		std::optional<QByteArray> parkedKey = std::nullopt) {
	show->showBox(Box(WalletPhraseWarningBox, show, parkedKey));
}

enum class WalletImportMode {
	Replace,
	Restore,
};

// The about text replaces the mode's default cover line, so an import that
// serves a specific action can say what the phrase is needed for.
void WalletImportBox(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<Main::SessionShow> show,
	WalletImportMode mode,
	Fn<void()> restored,
	std::shared_ptr<KeyContext> context,
	rpl::producer<QString> about);

void ShowInvalidSecretWords(
		std::shared_ptr<Main::SessionShow> show,
		bool foreign,
		std::shared_ptr<KeyContext> context = nullptr) {
	auto args = Ui::ConfirmBoxArgs{
		.confirmText = tr::lng_bot_download_retry(),
		.title = tr::lng_wallet_import_invalid_title(),
	};
	if (foreign) {
		args.text = tr::lng_wallet_import_invalid_spelling(tr::now)
			+ u"\n\n"_q
			+ tr::lng_wallet_import_invalid_scheme(tr::now);
	} else {
		args.text = tr::lng_wallet_import_invalid_spelling(tr::now);
	}
	const auto box = show->show(Ui::MakeInformBox(std::move(args)));
	if (context) {
		context->allowPromptRetry(box);
	}
}

int AddressGroupsWidth(
		const style::font &font,
		const QString &address,
		int groupsPerLine) {
	return groupsPerLine * font->width(address.left(kAddressGroup))
		+ (groupsPerLine - 1) * font->width(QChar(' '));
}

void PaintAddressGroups(
		QPainter &p,
		const style::font &font,
		const QString &address,
		QPoint origin,
		int groupsPerLine) {
	const auto groupWidth = font->width(address.left(kAddressGroup));
	const auto spaceWidth = font->width(QChar(' '));
	const auto groups = kAddressLength / kAddressGroup;
	p.setFont(font);
	for (auto i = 0; i != groups; ++i) {
		const auto line = i / groupsPerLine;
		const auto column = i % groupsPerLine;
		p.setPen((i % 2) ? st::windowSubTextFg : st::windowFg);
		p.drawText(
			origin.x() + column * (groupWidth + spaceWidth),
			origin.y() + line * font->height + font->ascent,
			address.mid(i * kAddressGroup, kAddressGroup));
	}
}

void AddAddressPlate(
		not_null<Ui::VerticalLayout*> container,
		const QString &address,
		const style::margins &margin,
		std::shared_ptr<Ui::Show> show = nullptr) {
	const auto font = st::walletAddressPlateFont->monospace();
	const auto inner = st::walletAddressPlateInner;
	const auto groups = kAddressLength / kAddressGroup;
	const auto lines = groups / kAddressGroupsPerLine;
	const auto plate = container->add(
		object_ptr<Ui::FixedHeightWidget>(
			container,
			inner + lines * font->height + inner),
		margin);
	plate->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(plate);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(
			plate->rect(),
			st::walletAddressPlateRadius,
			st::walletAddressPlateRadius);
		const auto lineWidth = AddressGroupsWidth(
			font,
			address,
			kAddressGroupsPerLine);
		const auto left = (plate->width() - lineWidth) / 2;
		PaintAddressGroups(
			p,
			font,
			address,
			{ left, inner },
			kAddressGroupsPerLine);
	}, plate->lifetime());
	if (show) {
		const auto copy = Ui::CreateChild<Ui::AbstractButton>(plate);
		copy->setClickedCallback(
			CopyAddressCallback(std::move(show), address));
		plate->sizeValue(
		) | rpl::on_next([=](QSize size) {
			copy->setGeometry(QRect(QPoint(), size));
		}, copy->lifetime());
	}
}

void ShowWrongSecretWords(
		std::shared_ptr<Main::SessionShow> show,
		bool outdated,
		std::shared_ptr<KeyContext> context) {
	const auto address = outdated
		? QString()
		: show->session().wallet().addressFriendly(false);
	const auto shown = show->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto &padding = st::boxPadding;
		const auto plate = (address.size() == kAddressLength);
		box->setTitle(tr::lng_wallet_restore_wrong_title());
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				(outdated
					? tr::lng_wallet_restore_outdated_text()
					: tr::lng_wallet_restore_other_text()),
				st::boxLabel),
			QMargins(
				padding.left(),
				0,
				padding.right(),
				plate ? 0 : padding.bottom()));
		if (plate) {
			AddAddressPlate(
				box->verticalLayout(),
				address,
				QMargins(
					padding.left(),
					st::walletAddressPlateSkip,
					padding.right(),
					padding.bottom()));
		}
		box->addButton(tr::lng_bot_download_retry(), [=] {
			box->closeBox();
		});
	}));
	if (context) {
		context->allowPromptRetry(shown);
	}
}

void ShowSendRecipientWallet(
		std::shared_ptr<Ui::Show> show,
		not_null<UserData*> user,
		const QString &address,
		Fn<void()> profile) {
	const auto shown = user->username().isEmpty()
		? user->name()
		: ('@' + user->username());
	show->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto &padding = st::boxPadding;
		box->setTitle(tr::lng_wallet_send_user_wallet_title(
			lt_name,
			rpl::single(user->shortName())));
		const auto about = box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_wallet_send_user_wallet_about(
					lt_name,
					rpl::single(tr::link(shown)),
					tr::marked),
				st::boxLabel),
			QMargins(padding.left(), 0, padding.right(), 0));
		about->setClickHandlerFilter([=](const auto &...) {
			const auto onstack = profile;
			onstack();
			return false;
		});
		AddAddressPlate(
			box->verticalLayout(),
			address,
			QMargins(
				padding.left(),
				st::walletAddressPlateSkip,
				padding.right(),
				padding.bottom()),
			box->uiShow());
		box->addButton(tr::lng_box_ok(), [=] {
			box->closeBox();
		});
	}));
}

// A restore that lands a record swaps the public-key-only client for the
// signing one asynchronously, so a continuation that reads the signing
// client - the rotation offer of a backup disable, a send - would find it
// missing if it ran at once. It runs once the session reports the signing
// client ready. The wait is bounded: a recovery that never settles lets the
// continuation run and be refused typed by the session on its own.
void RunWhenSigningReady(
		std::shared_ptr<Main::SessionShow> show,
		Fn<void()> action) {
	if (!action) {
		return;
	}
	auto &wallet = show->session().wallet();
	if (wallet.signingReady()) {
		action();
		return;
	}
	const auto weakSession = base::make_weak(&show->session());
	const auto lifetime = std::make_shared<rpl::lifetime>();
	const auto run = [=] {
		const auto owned = base::take(*lifetime);
		if (weakSession && show->valid()) {
			action();
		}
	};
	const auto timeout = lifetime->make_state<base::Timer>(run);
	timeout->callOnce(kSigningReadyTimeout);
	wallet.signingReadyValue() | rpl::filter([](bool ready) {
		return ready;
	}) | rpl::on_next(run, *lifetime);
}

void RequestCustodyRestore(
		std::shared_ptr<Main::SessionShow> show,
		KeyAuthorization auth,
		std::optional<Core::CloudPasswordResult> password,
		base::weak_qptr<PasscodeBox> passcode,
		Fn<void()> action,
		Fn<void()> unblock,
		std::shared_ptr<KeyContext> context = nullptr,
		Fn<void()> onPasswordMissing = nullptr) {
	if (context && !context->valid()) {
		context->cancel();
		return;
	}
	const auto purpose = !context
		? u"backup_management"_q
		: context->scope() ? u"decrypt_comment"_q : u"send"_q;
	LOG(("Wallet Info: key restore requested purpose=%1 password_supplied=%2."
		).arg(purpose).arg(password.has_value()));
	const auto done = [=] {
		if (passcode) {
			passcode->closeBox();
		}
		RunWhenSigningReady(show, action);
	};
	const auto fail = [=](const QString &error) {
		const auto contextValid = !context || context->valid();
		LOG(("Wallet Error: key restore result purpose=%1 error=%2 "
			"context_valid=%3 password_prompt=%4."
			).arg(purpose
			).arg(error
			).arg(contextValid
			).arg(bool(passcode)));
		if (context && (!contextValid
			|| error == u"PHRASE_ORIGIN_EXPIRED"_q
			|| error == u"PHRASE_SILENT_ERROR"_q)) {
			context->cancel();
			return;
		}
		if (onPasswordMissing && error == u"PASSWORD_MISSING"_q) {
			onPasswordMissing();
			return;
		}
		auto terminal = true;
		const auto finish = gsl::finally([&] {
			if (context && terminal) {
				context->cancel();
			}
		});
		if (!context && unblock) {
			unblock();
		}
		// A dismissed protection chooser restored nothing and has nothing to
		// state. The cloud password box goes with it, because the proof it
		// has already sent cannot be sent a second time.
		if (error == u"PHRASE_INSTALL_CANCELLED"_q) {
			if (passcode) {
				passcode->closeBox();
			}
			return;
		}
		if (error == u"PHRASE_INSTALL_FAILED"_q) {
			if (passcode) {
				passcode->closeBox();
			}
			show->showToast(tr::lng_wallet_key_save_error(tr::now));
			return;
		}
		if (passcode && passcode->handleCustomCheckError(error)) {
			terminal = false;
			return;
		}
		if (error == u"PHRASE_VAULT_LOCKED"_q) {
			if (passcode) {
				passcode->closeBox();
			}
			show->showToast(tr::lng_wallet_vault_locked(tr::now));
			return;
		}
		if (auto box = PrePasswordErrorBox(
				error,
				&show->session(),
				EnforcementCheckAbout(error),
				context)) {
			if (passcode) {
				if (context) {
					context->closePrompt(passcode);
				} else {
					passcode->closeBox();
				}
			}
			show->showBox(std::move(box));
			terminal = false;
			return;
		}
		ShowPhraseError(show, PhraseOperation::Restore, error);
	};
	auto &wallet = show->session().wallet();
	if (context) {
		wallet.restoreFromBackup(
			std::move(auth),
			std::move(password),
			context->scope(),
			[=](KeyAuthorization restored) {
				context->ready(std::move(restored));
			},
			fail);
	} else {
		wallet.restoreFromBackup(
			std::move(auth),
			std::move(password),
			done,
			fail);
	}
}

// The restore is requested without a password first, even when the account
// has one, because the server decides whether this backup needs it. Only
// its PASSWORD_MISSING answer loads the cloud password state, asks for the
// password and repeats the restore with it.
void StartCustodyRestore(
		std::shared_ptr<Main::SessionShow> show,
		KeyAuthorization auth,
		Fn<void()> action,
		Fn<void()> unblock = nullptr,
		std::shared_ptr<KeyContext> context = nullptr) {
	if (context && !context->valid()) {
		context->cancel();
		return;
	}
	const auto session = &show->session();
	const auto firstKeyUse = RestoreIsFirstKeyUse(session);
	const auto askPassword = [=] {
		if (context && !context->valid()) {
			context->cancel();
			return;
		}
		const auto cached = session->api().cloudPassword().stateCurrent();
		LOG(("Wallet Info: key restore requires password; "
			"cached_state=%1 cached_has_password=%2."
			).arg(cached.has_value()).arg(cached && cached->hasPassword));
		session->api().cloudPassword().reload();
		const auto lifetime = std::make_shared<rpl::lifetime>();
		if (context) {
			context->lifetime().add([=] { lifetime->destroy(); });
			const auto timeout = lifetime->make_state<base::Timer>([=] {
				const auto owned = base::take(*lifetime);
				if (context->valid()) {
					LOG(("Wallet Error: restore password state timed out "
						"after %1 ms; comment_scope=%2."
						).arg(kCommentPasswordStateTimeout
						).arg(bool(context->scope())));
					ShowPhraseError(
						show,
						PhraseOperation::Restore,
						u"PHRASE_PASSWORD_STATE_TIMEOUT"_q);
				}
				context->cancel();
			});
			timeout->callOnce(kCommentPasswordStateTimeout);
		}
		session->api().cloudPassword().state(
		) | rpl::take(
			1
		) | rpl::on_next([=](const Core::CloudPasswordState &state) {
			const auto owned = base::take(*lifetime);
			if (context && !context->valid()) {
				context->cancel();
				return;
			}
			if (!state.hasPassword) {
				// The server asked for a password this account does not
				// have, so a repeat without one would only be refused again.
				ShowPhraseError(
					show,
					PhraseOperation::Restore,
					u"PHRASE_PASSWORD_STATE_MISSING"_q);
				if (context) {
					context->cancel();
				} else if (unblock) {
					unblock();
				}
				return;
			}
			auto fields = RestorePasswordFields(
				state,
				firstKeyUse,
				tr::lng_bots_password_confirm_description(tr::now));
			fields.customShow = context;
			fields.customCheckCallback = [=](
					const Core::CloudPasswordResult &result,
					base::weak_qptr<PasscodeBox> passcode) {
				RequestCustodyRestore(
					show,
					auth,
					result,
					passcode,
					action,
					unblock,
					context);
			};
			const auto passcode = show->show(
				Box<PasscodeBox>(session, fields));
			if (context) {
				context->cancelOnClose(passcode, true);
			} else if (unblock) {
				unblock();
			}
		}, *lifetime);
	};
	RequestCustodyRestore(
		show,
		std::move(auth),
		std::nullopt,
		nullptr,
		action,
		unblock,
		context,
		askPassword);
}

// The device mode is unknown only while the wallet's state has not reached
// this client, which is the normal state of a freshly logged in account
// outside the Wallet window: nothing else asks for it. A press asks, waits
// for the answer and then climbs the ladder that answer names. The wait is
// bounded, and a wallet the server will not serve is stated once.
void ResolveDeviceCustody(
		std::shared_ptr<Main::SessionShow> show,
		Fn<void()> resolved,
		std::shared_ptr<KeyContext> context) {
	const auto weakSession = base::make_weak(&show->session());
	auto &wallet = show->session().wallet();
	const auto lifetime = std::make_shared<rpl::lifetime>();
	wallet.startPolling();
	lifetime->add([weakSession] {
		if (weakSession) {
			weakSession->wallet().stopPolling();
		}
	});
	if (context) {
		context->lifetime().add([lifetime] { lifetime->destroy(); });
	}
	const auto settled = std::make_shared<bool>(false);
	const auto finish = [=](bool known) {
		if (*settled) {
			return;
		}
		*settled = true;
		const auto owned = base::take(*lifetime);
		if (!weakSession || !show->valid()) {
			if (context) {
				context->cancel();
			}
			return;
		} else if (known) {
			resolved();
			return;
		}
		show->showToast(tr::lng_wallet_unavailable(tr::now));
		if (context) {
			context->cancel();
		}
	};
	const auto check = [=] {
		if (*settled || !weakSession) {
			return;
		} else if (context && !context->valid()) {
			*settled = true;
			const auto owned = base::take(*lifetime);
			context->cancel();
			return;
		}
		auto &wallet = weakSession->wallet();
		const auto presence = wallet.presence();
		if (wallet.deviceCustodyState().mode != DeviceMode::Unknown) {
			finish(true);
		} else if (presence == Presence::Unavailable
			|| presence == Presence::Missing
			|| presence == Presence::AddressUnreadable) {
			finish(false);
		}
	};
	const auto timeout = lifetime->make_state<base::Timer>([=] {
		finish(false);
	});
	timeout->callOnce(kCustodyResolveTimeout);
	rpl::merge(
		wallet.transferWalletIdentityChanges(),
		wallet.custodyUpdates()
	) | rpl::on_next(check, *lifetime);
	check();
}

void RunKeyRequiringAction(
		std::shared_ptr<Main::SessionShow> show,
		Fn<void()> action,
		KeyActionKind kind,
		std::shared_ptr<KeyContext> context,
		rpl::producer<QString> importAbout) {
	if (context && !context->valid()) {
		context->cancel();
		return;
	}
	const auto state = show->session().wallet().deviceCustodyState();
	if (state.conflict) {
		// Resolving the conflict lands no key: it drops the parked record
		// or exports its phrase. The same press climbs this ladder again
		// once the parked wallet is switched away, into whatever the served
		// wallet then needs. The export runs over the plain show, because
		// the context reads a closed prompt of its own as the end of the
		// press, and the export box is closed on the way back to the list.
		const auto plain = context ? context->plain() : show;
		const auto retry = [=] {
			if (context) {
				context->acceptClosed();
			}
			RunKeyRequiringAction(
				show,
				action,
				kind,
				context,
				rpl::duplicate(importAbout));
		};
		show->showBox(Box(WalletConflictBox, plain, retry));
	} else if (state.mode == DeviceMode::Full) {
		action();
	} else if (state.mode == DeviceMode::ReadOnlyRestorable) {
		if (kind == KeyActionKind::Reveal) {
			action();
		} else {
			StartCustodyRestore(
				show,
				KeyAuthorization{ .install = context
					? context->installer()
					: MakeCustodyInstaller(show) },
				std::move(action),
				nullptr,
				context);
		}
	} else if (state.mode == DeviceMode::ReadOnlyNotRestorable) {
		show->showBox(Box(
			WalletImportBox,
			show,
			WalletImportMode::Restore,
			(kind == KeyActionKind::ResumeAfterRestore) ? action : nullptr,
			context,
			std::move(importAbout)));
	} else {
		ResolveDeviceCustody(show, [=] {
			RunKeyRequiringAction(
				show,
				action,
				kind,
				context,
				rpl::duplicate(importAbout));
		}, context);
	}
}

void RequestWalletReplace(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		std::optional<std::vector<QString>> words,
		std::optional<Core::CloudPasswordResult> password,
		base::weak_qptr<PasscodeBox> passcode,
		Fn<void()> unblock,
		Fn<void(const QString &text)> showError) {
	const auto imported = words.has_value();
	const auto done = crl::guard(origin, [=](CustodyOutcome outcome) {
		if (passcode) {
			passcode->closeBox();
		}
		// Only an imported replace carries a custody write - replaceWithNew
		// hands finishConfirmedReplace no new record - so the imported title
		// is the right one here. The replacement itself stands, so this
		// states the half that failed instead of a failure, and closes the
		// import box with the same layer operation rather than racing a hide.
		if (outcome == CustodyOutcome::WriteFailed) {
			show->showBox(
				Ui::MakeInformBox({
					.text = tr::lng_wallet_imported_not_stored(tr::now),
					.title = tr::lng_wallet_imported_title(),
				}),
				Ui::LayerOption::CloseOther);
			return;
		}
		show->hideLayer();
		show->showToast({
			.title = (imported
				? tr::lng_wallet_imported_title(tr::now)
				: tr::lng_wallet_created_title(tr::now)),
			.text = { imported
				? tr::lng_wallet_imported_text(tr::now)
				: tr::lng_wallet_created_text(tr::now) },
			.icon = &st::toastCheckIcon,
		});
	});
	const auto fail = crl::guard(origin, [=](const QString &error) {
		unblock();
		// A dismissed protection chooser imported nothing and has nothing to
		// state. The cloud password box goes with it, because the proof it
		// has already sent cannot be sent a second time.
		if (error == u"REPLACE_INSTALL_CANCELLED"_q) {
			if (passcode) {
				passcode->closeBox();
			}
			return;
		}
		if (passcode && passcode->handleCustomCheckError(error)) {
			return;
		}
		if (error == u"REPLACE_VAULT_LOCKED"_q) {
			if (passcode) {
				passcode->closeBox();
			}
			show->showToast(tr::lng_wallet_vault_locked(tr::now));
			return;
		}
		if (auto box = PrePasswordErrorBox(
				error,
				&show->session(),
				EnforcementCheckAbout(error))) {
			if (passcode) {
				passcode->closeBox();
			}
			show->showBox(std::move(box));
			return;
		}
		if (passcode) {
			passcode->closeBox();
		}
		if (!imported) {
			show->showToast(ErrorWithType(
				tr::lng_wallet_create_error(tr::now),
				error));
		} else if (error == u"REPLACE_INVALID_PHRASE"_q
			|| error == u"REPLACE_FOREIGN_PHRASE"_q) {
			ShowInvalidSecretWords(
				show,
				error == u"REPLACE_FOREIGN_PHRASE"_q);
		} else if (error == u"REPLACE_OUTDATED_PHRASE"_q) {
			if (showError) {
				showError(QString());
			}
			ShowWrongSecretWords(show, true, nullptr);
		} else {
			// WHY: the server checks the proof against the key the chain
			// holds for the phrase's address, so a refused phrase stays
			// refused; a retry cannot help, only support can.
			const auto text = (error == u"REPLACE_KEY_CHANGING"_q)
				? tr::lng_wallet_import_key_changing(tr::now)
				: (error == u"WALLET_PROOF_INVALID"_q)
				? tr::lng_wallet_import_not_verified(tr::now)
				: ErrorWithType(tr::lng_wallet_import_failed(tr::now), error);
			if (showError) {
				showError(text);
			} else {
				show->showToast(text);
			}
		}
	});
	auto &wallet = show->session().wallet();
	if (imported) {
		wallet.replaceWithImported(
			KeyAuthorization{ .install = MakeCustodyInstaller(show) },
			std::move(*words),
			std::move(password),
			done,
			fail);
	} else {
		wallet.replaceWithNew(std::move(password), done, fail);
	}
}

void StartWalletReplace(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		std::optional<std::vector<QString>> words,
		Fn<void()> unblock,
		Fn<void(const QString &text)> showError = nullptr) {
	const auto session = &show->session();
	session->api().cloudPassword().reload();
	session->api().cloudPassword().state(
	) | rpl::take(
		1
	) | rpl::on_next([=](const Core::CloudPasswordState &state) {
		if (!state.hasPassword) {
			RequestWalletReplace(
				show,
				origin,
				words,
				std::nullopt,
				nullptr,
				unblock,
				showError);
			return;
		}
		auto fields = PasscodeBox::CloudFields::From(state);
		fields.customTitle = tr::lng_bots_password_confirm_title();
		fields.customDescription = tr::lng_bots_password_confirm_description(
			tr::now);
		fields.customSubmitButton = tr::lng_passcode_submit();
		fields.customCheckCallback = [=](
				const Core::CloudPasswordResult &result,
				base::weak_qptr<PasscodeBox> passcode) {
			RequestWalletReplace(
				show,
				origin,
				words,
				result,
				passcode,
				unblock,
				showError);
		};
		show->showBox(Box<PasscodeBox>(session, fields));
		unblock();
	}, origin->lifetime());
}

struct BackupDisableProof {
	KeyAuthorization auth;
	BackupDisableApproval approved;
};

void ShowBackupChangeError(
		std::shared_ptr<Main::SessionShow> show,
		const QString &error) {
	if (error == u"BACKUP_VAULT_LOCKED"_q) {
		show->showToast(tr::lng_wallet_vault_locked(tr::now));
	} else if (auto box = PrePasswordErrorBox(
			error,
			&show->session(),
			EnforcementCheckAbout(error))) {
		show->showBox(std::move(box));
	} else if (error == u"BACKUP_PHRASE_OUTDATED"_q) {
		ShowKeyChangedBox(show, tr::lng_wallet_send_key_changed_text());
	} else if (error == u"BACKUP_KEY_UNCONFIRMED"_q) {
		show->showToast(tr::lng_wallet_import_key_changing(tr::now));
	} else if (error == u"BACKUP_NOT_VERIFIED"_q) {
		show->showToast(tr::lng_wallet_import_not_verified(tr::now));
	} else {
		show->showToast((error == u"WALLET_BACKUP_NOT_AVAILABLE"_q)
			? tr::lng_wallet_backup_about_unavailable(tr::now)
			: ErrorWithType(tr::lng_wallet_backup_error(tr::now), error));
	}
}

void RequestBackupChange(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		std::optional<Core::CloudPasswordResult> password,
		base::weak_qptr<PasscodeBox> passcode,
		Fn<void()> unblock,
		Fn<void()> done,
		std::optional<BackupDisableProof> proof = std::nullopt) {
	const auto succeeded = crl::guard(origin, [=] {
		unblock();
		if (passcode) {
			passcode->closeBox();
		}
		done();
	});
	const auto fail = crl::guard(origin, [=](const QString &error) {
		unblock();
		if (passcode && passcode->handleCustomCheckError(error)) {
			return;
		} else if (passcode) {
			passcode->closeBox();
		}
		ShowBackupChangeError(show, error);
	});
	auto &wallet = show->session().wallet();
	if (proof) {
		wallet.disableBackupWithProof(
			std::move(proof->auth),
			std::move(proof->approved),
			succeeded,
			fail);
	} else {
		wallet.disableBackup(std::move(password), succeeded, fail);
	}
}

void StartBackupRequest(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		Fn<void()> unblock,
		Fn<void()> done) {
	const auto session = &show->session();
	session->api().cloudPassword().reload();
	session->api().cloudPassword().state(
	) | rpl::take(
		1
	) | rpl::on_next([=](const Core::CloudPasswordState &state) {
		if (!state.hasPassword) {
			RequestBackupChange(
				show,
				origin,
				std::nullopt,
				nullptr,
				unblock,
				done);
			return;
		}
		auto fields = PasscodeBox::CloudFields::From(state);
		fields.customTitle = tr::lng_bots_password_confirm_title();
		fields.customDescription = tr::lng_bots_password_confirm_description(
			tr::now);
		fields.customSubmitButton = tr::lng_passcode_submit();
		fields.customCheckCallback = [=](
				const Core::CloudPasswordResult &result,
				base::weak_qptr<PasscodeBox> passcode) {
			RequestBackupChange(
				show,
				origin,
				result,
				passcode,
				unblock,
				done);
		};
		show->showBox(Box<PasscodeBox>(session, fields));
		unblock();
	}, origin->lifetime());
}

// A usable local copy of the served wallet's current key proves ownership,
// so no cloud password is asked. Without one (the served key moved since the
// phrase was shown, or the hardware wrap cannot be opened) no proof can be
// made and the cloud password route stays as before. A locked vault still
// has usable custody: the session reports it locked and asks no password.
void RequestBackupDisable(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		BackupDisableProof proof,
		Fn<void()> unblock,
		Fn<void()> done) {
	if (!show->session().wallet().revealsLocally()) {
		StartBackupRequest(
			show,
			origin,
			std::move(unblock),
			std::move(done));
		return;
	}
	RequestBackupChange(
		show,
		origin,
		std::nullopt,
		nullptr,
		std::move(unblock),
		std::move(done),
		std::move(proof));
}

void ShowBackupEnabledToast(std::shared_ptr<Main::SessionShow> show) {
	show->showToast({
		.title = tr::lng_wallet_backup_enabled_title(tr::now),
		.text = { tr::lng_wallet_backup_enabled_text(tr::now) },
		.icon = &st::toastCheckIcon,
	});
}

void StartBackupEnable(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		not_null<bool*> busy) {
	const auto upload = crl::guard(origin, [=](KeyAuthorization auth) {
		if (*busy) {
			return;
		}
		*busy = true;
		show->session().wallet().enableBackup(
			std::move(auth),
			crl::guard(origin, [=] {
				*busy = false;
				ShowBackupEnabledToast(show);
			}),
			crl::guard(origin, [=](const QString &error) {
				*busy = false;
				ShowBackupChangeError(show, error);
			}));
	});
	// The restorable and not-restorable arms install custody first and only
	// then run this, so the acquisition sits after them and every arm reaches
	// enableBackup with a live grant.
	RunKeyRequiringAction(show, crl::guard(origin, [=] {
		if (*busy) {
			return;
		}
		AcquireVaultUnlock({
			.show = show,
			.done = [=](KeyAuthorization auth) {
				if (auth.valid()) {
					upload(std::move(auth));
				}
			},
		});
	}), KeyActionKind::ResumeAfterRestore);
}

// WHY: dismissing the write-down or its quiz silently drops the disable,
// so while it still would, the dismissal asks first; no abandons = always.
void GuardBackupDisableDismiss(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		Fn<bool()> abandons) {
	struct State {
		base::weak_qptr<Ui::GenericBox> confirmation;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto abandoning = [=] {
		return !abandons || abandons();
	};
	const auto confirm = [=] {
		if (state->confirmation) {
			return;
		}
		state->confirmation = show->show(Ui::MakeConfirmBox({
			.text = tr::lng_wallet_backup_cancel_text(),
			.confirmed = crl::guard(box, [=](Fn<void()> close) {
				close();
				box->closeBox();
			}),
			.confirmText = tr::lng_box_yes(),
			.cancelText = tr::lng_box_no(),
			.title = tr::lng_wallet_backup_cancel_title(),
		}));
	};
	box->boxClosing() | rpl::on_next([=] {
		if (const auto strong = state->confirmation.get()) {
			strong->closeBox();
		}
	}, box->lifetime());
	AddBoxCloseButton(box, [=] {
		if (abandoning()) {
			confirm();
		} else {
			box->closeBox();
		}
	});
	const auto isEscape = [](not_null<QEvent*> e) {
		return (e->type() == QEvent::KeyPress)
			&& (static_cast<QKeyEvent*>(e.get())->key() == Qt::Key_Escape);
	};
	base::install_event_filter(box, [=](not_null<QEvent*> e) {
		if (!isEscape(e) || !abandoning()) {
			return base::EventFilterResult::Continue;
		}
		confirm();
		return base::EventFilterResult::Cancel;
	});
	box->showFinishes() | rpl::take(1) | rpl::on_next([=] {
		const auto layer = box->parentWidget();
		const auto stack = BoxLayerStack(box);
		if (!layer || !stack) {
			return;
		}
		base::install_event_filter(box, stack, [=](not_null<QEvent*> e) {
			const auto dismissal = (e->type() == QEvent::MouseButtonPress)
				|| isEscape(e);
			if (!dismissal || layer->isHidden() || !abandoning()) {
				return base::EventFilterResult::Continue;
			}
			Ui::PostponeCall(box, confirm);
			return base::EventFilterResult::Cancel;
		});
	}, box->lifetime());
}

void WalletBackupPhraseBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		std::vector<QString> words,
		Fn<void(std::vector<QString>)> next,
		rpl::producer<QString> title,
		rpl::producer<TextWithEntities> text) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	AddPhraseBoxHeader(
		box,
		PhraseBoxLottie(int(words.size())),
		std::move(title),
		std::move(text),
		st::walletPhraseTextLabel,
		st::walletCoverLottieSize,
		st::walletPhraseLottieMargin,
		st::walletPhraseTextMargin);

	AddPhraseGrid(box, words);

	GuardBackupDisableDismiss(box, show, nullptr);
	const auto shownAt = crl::now();
	box->addButton(tr::lng_continue(), [=] {
		if (crl::now() - shownAt < kBackupWriteDownDelay) {
			show->showBox(Ui::MakeInformBox({
				.text = tr::lng_wallet_backup_sure_text(tr::now),
				.confirmText = tr::lng_box_ok(),
				.title = tr::lng_wallet_backup_sure_title(),
			}));
			return;
		}
		next(words);
		box->closeBox();
	});
}

// WHY: a rotated phrase carries two independent keys - the first 12 words
// restore the anchor and the last 12 the signing key - so the quiz proves
// both halves were written down, not three words of whichever half.
[[nodiscard]] std::vector<int> BackupQuizIndices(int count) {
	auto result = std::vector<int>();
	result.reserve(kBackupQuizWordCount);
	if (count >= kImportWordCountLong) {
		const auto signing = count - kImportWordCountShort;
		const auto first = base::RandomIndex(signing);
		const auto second = base::RandomIndex(signing - 1);
		result.push_back(base::RandomIndex(kImportWordCountShort));
		result.push_back(kImportWordCountShort + first);
		result.push_back(kImportWordCountShort
			+ second
			+ ((second >= first) ? 1 : 0));
	} else {
		while (int(result.size()) < kBackupQuizWordCount) {
			const auto index = base::RandomIndex(count);
			if (!ranges::contains(result, index)) {
				result.push_back(index);
			}
		}
	}
	ranges::sort(result);
	return result;
}

[[nodiscard]] rpl::producer<TextWithEntities> BackupQuizText(
		const std::vector<int> &indices) {
	const auto number = [](int index) {
		return rpl::single(tr::bold(QString::number(index + 1)));
	};
	return tr::lng_wallet_backup_test_text(
		lt_index1,
		number(indices[0]),
		lt_index2,
		number(indices[1]),
		lt_index3,
		number(indices[2]),
		tr::marked);
}

[[nodiscard]] not_null<Ui::InputField*> AddBackupQuizField(
		not_null<Ui::VerticalLayout*> container,
		int index) {
	const auto field = container->add(
		object_ptr<Ui::InputField>(
			container,
			st::walletBackupQuizField,
			Ui::InputField::Mode::SingleLine),
		st::walletImportFieldMargin);
	const auto number = Ui::CreateChild<Ui::FlatLabel>(
		field,
		QString::number(index + 1) + QChar('.'),
		st::walletPhraseNumberLabel);
	number->setAttribute(Qt::WA_TransparentForMouseEvents);
	field->widthValue(
	) | rpl::on_next([=](int width) {
		number->moveToLeft(
			st::walletImportNumberLeft,
			st::walletImportNumberTop,
			width);
	}, field->lifetime());
	return field;
}

[[nodiscard]] bool BackupQuizAnswerMatches(
		not_null<Ui::InputField*> field,
		const QString &word) {
	const auto entered = field->getLastText().trimmed();
	return (entered.compare(word.trimmed(), Qt::CaseInsensitive) == 0);
}

void WalletBackupQuizBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		std::vector<QString> words,
		Fn<void()> passed,
		Fn<void(Fn<void()> lock)> publishLock,
		Fn<bool()> abandons) {
	Expects(int(words.size()) >= kBackupQuizWordCount);

	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	struct State {
		std::vector<Ui::InputField*> fields;
		std::vector<int> indices;
		std::vector<bool> wrong;
	};
	const auto state = box->lifetime().make_state<State>();
	state->indices = BackupQuizIndices(int(words.size()));
	state->wrong.resize(kBackupQuizWordCount, false);

	AddPhraseBoxHeader(
		box,
		u"wallet/test"_q,
		tr::lng_wallet_backup_test_title(),
		BackupQuizText(state->indices),
		st::walletPhraseTextLabel,
		st::walletPhraseGridLottieSize,
		st::walletPhraseGridLottieMargin,
		st::walletPhraseGridTextMargin);

	const auto container = box->verticalLayout();
	Ui::AddSkip(container, st::walletBackupQuizFieldsTopSkip);
	for (const auto index : state->indices) {
		state->fields.push_back(AddBackupQuizField(container, index));
	}
	Ui::AddSkip(container, st::walletBackupQuizFieldsBottomSkip);

	GuardBackupDisableDismiss(box, show, std::move(abandons));
	const auto button = box->addButton(tr::lng_continue());
	const auto allFilled = [=] {
		return ranges::all_of(state->fields, [](Ui::InputField *field) {
			return !field->getLastText().trimmed().isEmpty();
		});
	};
	const auto anyWrong = [=] {
		return ranges::contains(state->wrong, true);
	};
	const auto refreshButton = [=] {
		if (const auto raw = button.data()) {
			SetButtonDisabledLook(raw, !allFilled() || anyWrong());
		}
	};
	const auto submit = [=] {
		if (!allFilled() || anyWrong()) {
			return;
		}
		for (auto i = 0; i != kBackupQuizWordCount; ++i) {
			const auto field = state->fields[i];
			if (!BackupQuizAnswerMatches(field, words[state->indices[i]])) {
				field->showError();
				state->wrong[i] = true;
			}
		}
		if (anyWrong()) {
			refreshButton();
		} else {
			passed();
		}
	};
	button->setClickedCallback(submit);
	refreshButton();

	for (auto i = 0; i != kBackupQuizWordCount; ++i) {
		const auto field = state->fields[i];
		field->changes() | rpl::on_next([=] {
			state->wrong[i] = false;
			refreshButton();
		}, field->lifetime());
		field->submits() | rpl::on_next([=] {
			if (i + 1 < kBackupQuizWordCount) {
				state->fields[i + 1]->setFocus();
			} else {
				submit();
			}
		}, field->lifetime());
	}
	box->setFocusCallback([=] {
		state->fields.front()->setFocusFast();
	});
	if (publishLock) {
		publishLock([=] {
			for (const auto field : state->fields) {
				field->setDisabled(true);
			}
			// A disabled field takes no focus, and with the focus left on
			// the layer stack Escape closes the box past
			// setCloseByEscape(false), so the box holds it itself.
			box->setFocusCallback(nullptr);
			box->setInnerFocus();
		});
	}
}

void ShowBackupDisabledToast(std::shared_ptr<Main::SessionShow> show) {
	show->showToast({
		.title = tr::lng_wallet_backup_disabled_title(tr::now),
		.text = { tr::lng_wallet_backup_disabled_text(tr::now) },
		.icon = &st::toastCheckIcon,
	});
}

void CollectBackupPhrase(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		KeyAuthorization auth) {
	const auto approved = show->session().wallet().backupDisableApproval();
	const auto proof = BackupDisableProof{
		.auth = auth,
		.approved = approved.value_or(BackupDisableApproval()),
	};
	const auto showQuiz = [=](std::vector<QString> words) {
		const auto quiz = std::make_shared<base::weak_qptr<Ui::GenericBox>>();
		const auto requesting = std::make_shared<bool>(false);
		*quiz = show->show(Box(WalletBackupQuizBox, show, words, [=] {
			const auto strong = quiz->get();
			if (!strong || *requesting) {
				return;
			}
			*requesting = true;
			RequestBackupDisable(
				show,
				strong,
				proof,
				[=] { *requesting = false; },
				[=] {
					if (const auto strong = quiz->get()) {
						strong->closeBox();
					}
					ShowBackupDisabledToast(show);
				});
		}, nullptr, [=] {
			return !*requesting;
		}));
	};
	const auto showPhrase = [=](std::vector<QString> words) {
		const auto count = int(words.size());
		show->showBox(Box(
			WalletBackupPhraseBox,
			show,
			words,
			showQuiz,
			tr::lng_wallet_phrase_title(),
			tr::lng_wallet_phrase_text(
				lt_count,
				rpl::single(count * 1.) | tr::to_count(),
				tr::marked)));
	};
	StartPhraseReveal(
		show,
		origin,
		std::move(auth),
		[] {},
		std::nullopt,
		showPhrase);
}

[[nodiscard]] QString RotationFeeText(
		tr::phrase<lngtag_amount, lngtag_fiat> phrase,
		int64 feeNano,
		const FiatRate &rate) {
	return phrase(
		tr::now,
		lt_amount,
		Ui::FormatTonAmount(feeNano).full,
		lt_fiat,
		FormatFiat(feeNano, rate, kFeeFiatDecimals, true));
}

[[nodiscard]] QString RotationFailureReason(const QString &error) {
	if (error == u"ROTATION_FEES"_q) {
		return tr::lng_wallet_send_error_insufficient(tr::now);
	} else if (error == u"ROTATION_ALREADY_SENDING"_q) {
		return tr::lng_wallet_send_error_in_progress(tr::now);
	} else if (error == u"ROTATION_VAULT_LOCKED"_q) {
		return tr::lng_wallet_vault_locked(tr::now);
	}
	return tr::lng_wallet_backup_rotate_reason_failed(tr::now);
}

void ShowRotationFailedToast(
		std::shared_ptr<Main::SessionShow> show,
		const QString &error) {
	show->showToast({
		.title = tr::lng_wallet_backup_rotate_failed_title(tr::now),
		.text = { tr::lng_wallet_backup_rotate_failed_text(
			tr::now,
			lt_reason,
			RotationFailureReason(error)) },
		.icon = &st::toastCheckIcon,
	});
}

void SetBoxBusy(not_null<Ui::GenericBox*> box) {
	box->clearButtons();
	const auto button = box->addButton(rpl::single(QString()));
	SetButtonDisabledLook(button.data(), true);
	AddBusyFooterSpinner(button, rpl::single(true));
	box->setCloseByOutsideClick(false);
	box->setCloseByEscape(false);
}

struct RotationState {
	bool submitted = false;
	base::weak_qptr<Ui::GenericBox> quiz;
	Fn<void()> lockQuiz;
	KeyAuthorization auth;
};

void SubmitRotation(
		std::shared_ptr<Main::SessionShow> show,
		base::weak_qptr<Ui::GenericBox> origin,
		not_null<bool*> busy,
		std::shared_ptr<RotationState> state) {
	const auto quiz = state->quiz.get();
	if (!quiz || state->submitted) {
		return;
	}
	state->submitted = true;
	state->lockQuiz();
	SetBoxBusy(quiz);
	const auto closeQuiz = [=] {
		if (const auto quiz = state->quiz.get()) {
			quiz->closeBox();
		}
	};
	show->session().wallet().submitRotation(state->auth, [=] {
		closeQuiz();
		const auto strong = origin.get();
		if (!strong) {
			return;
		}
		*busy = true;
		const auto approved = show->session().wallet().backupDisableApproval();
		RequestBackupDisable(
			show,
			strong,
			BackupDisableProof{
				.auth = state->auth,
				.approved = approved.value_or(BackupDisableApproval()),
			},
			[=] { *busy = false; },
			[=] { ShowBackupDisabledToast(show); });
	}, [=](const QString &error) {
		closeQuiz();
		ShowRotationFailedToast(show, error);
	});
}

void ShowRotationPhrase(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		not_null<bool*> busy,
		std::vector<QString> words,
		KeyAuthorization auth) {
	const auto state = std::make_shared<RotationState>();
	state->auth = std::move(auth);
	const auto wallet = &show->session().wallet();
	const auto weak = base::make_weak(origin);
	const auto showQuiz = [=](std::vector<QString> words) {
		const auto quiz = show->show(Box(WalletBackupQuizBox, show, words, [=] {
			SubmitRotation(show, weak, busy, state);
		}, [=](Fn<void()> lock) {
			state->lockQuiz = std::move(lock);
		}, [=] {
			return !state->submitted;
		}));
		state->quiz = quiz;
		quiz->boxClosing() | rpl::on_next([=] {
			if (!state->submitted) {
				wallet->abandonRotation();
			}
		}, quiz->lifetime());
	};
	const auto sheet = show->show(Box(
		WalletBackupPhraseBox,
		show,
		words,
		showQuiz,
		tr::lng_wallet_backup_new_phrase_title(),
		tr::lng_wallet_backup_new_phrase_text(tr::marked)));
	sheet->boxClosing() | rpl::on_next([=] {
		if (!state->quiz && !state->submitted) {
			wallet->abandonRotation();
		}
	}, sheet->lifetime());
}

void StartRotation(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		not_null<bool*> busy,
		int64 feeNano,
		KeyAuthorization auth) {
	if (*busy) {
		return;
	}
	*busy = true;
	const auto weak = base::make_weak(origin);
	show->session().wallet().prepareRotation(auth, feeNano, [=](
			std::vector<QString> words) {
		const auto strong = weak.get();
		if (!strong) {
			show->session().wallet().abandonRotation();
			return;
		}
		*busy = false;
		ShowRotationPhrase(
			show,
			strong,
			busy,
			std::move(words),
			auth);
	}, crl::guard(origin, [=](const QString &error) {
		*busy = false;
		ShowRotationFailedToast(show, error);
	}));
}

void ShowBackupTopUpAlert(
		std::shared_ptr<Main::SessionShow> show,
		int64 feeNano,
		Fn<void()> topUp) {
	const auto rate = show->session().wallet().rates().current();
	show->showBox(Ui::MakeConfirmBox({
		.text = RotationFeeText(
			tr::lng_wallet_backup_topup_text,
			feeNano,
			rate),
		.confirmed = [=](Fn<void()> close) {
			close();
			topUp();
			ShowWalletReceiveBox(&show->session(), show);
		},
		.confirmText = tr::lng_credits_buy_button_short(),
		.cancelText = tr::lng_export_suggest_cancel(),
		.title = tr::lng_wallet_backup_topup_title(),
	}));
}

struct RotationQuote {
	int64 feeNano = 0;
	SendError error = SendError::None;

	friend inline bool operator==(
		const RotationQuote &,
		const RotationQuote &) = default;
};

struct BackupDisableState {
	KeyAuthorization auth;
	rpl::variable<std::optional<RotationQuote>> quote;
	rpl::variable<bool> loading = false;
	Fn<void()> onQuoted;
	bool rotate = false;
	bool started = false;
};

[[nodiscard]] bool RotationQuoteUsable(
		const std::optional<RotationQuote> &quote) {
	return quote
		&& (quote->error == SendError::None
			|| quote->error == SendError::InsufficientFees);
}

// What a phrase update does, then its network fee, or the top-up that fee
// needs when the balance does not cover it.
[[nodiscard]] rpl::producer<TextWithEntities> BackupUpdateNoteText(
		not_null<Main::Session*> session,
		rpl::producer<std::optional<RotationQuote>> quote) {
	return rpl::combine(
		std::move(quote),
		FiatRateValue(session)
	) | rpl::map([](
			const std::optional<RotationQuote> &quote,
			const FiatRate &rate) {
		auto result = tr::lng_wallet_backup_update_text(tr::now, tr::marked);
		if (RotationQuoteUsable(quote)) {
			const auto phrase = (quote->error == SendError::None)
				? tr::lng_wallet_backup_update_fee
				: tr::lng_wallet_backup_topup_text;
			result.append(u"\n\n"_q).append(tr::marked(
				RotationFeeText(phrase, quote->feeNano, rate)));
		}
		return result;
	});
}

void AddBackupUpdateNote(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<TextWithEntities> text,
		rpl::producer<bool> shown) {
	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::PaddingWrap<Ui::FlatLabel>>>(
			container,
			object_ptr<Ui::PaddingWrap<Ui::FlatLabel>>(
				container,
				object_ptr<Ui::FlatLabel>(
					container,
					std::move(text),
					st::walletBackupNoteLabel),
				st::walletBackupNotePadding),
			st::walletBackupNoteMargin),
		st::boxRowPadding);
	// The plate is the note's own padding wrap: the slide wrap keeps one
	// more around it for the outer margins, and entity() would unwrap all
	// the way down to the label.
	const auto plate = wrap->wrapped()->wrapped();
	plate->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(plate);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::walletBackupNoteBg);
		p.drawRoundedRect(
			plate->rect(),
			st::walletBackupNoteRadius,
			st::walletBackupNoteRadius);
	}, plate->lifetime());
	wrap->toggleOn(std::move(shown));
	wrap->finishAnimating();
}

// One confirmation for the whole disable: its checkbox decides whether the
// key is rotated on the way, and the fee for that is quoted in the
// background from the moment the box shows. The checkbox slides in only
// once the box has finished showing and the quote is usable, and a quote
// that failed leaves no update to offer. A press before the quote waits
// for it, because the reveal refuses to run beside it, and then goes to the
// write-down of the current words.
void WalletBackupDisableBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		base::weak_qptr<Ui::GenericBox> origin,
		not_null<bool*> busy,
		std::shared_ptr<BackupDisableState> state,
		bool updateOffered) {
	const auto session = &show->session();
	box->setTitle(tr::lng_wallet_backup_disable_title());
	const auto padding = st::boxPadding;
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_wallet_backup_disable_text(),
			st::boxLabel),
		QMargins(padding.left(), 0, padding.right(), padding.bottom()));
	auto update = (Ui::Checkbox*)nullptr;
	if (updateOffered) {
		const auto wrap = box->addRow(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				box,
				object_ptr<Ui::VerticalLayout>(box)),
			QMargins());
		const auto inner = wrap->entity();
		update = inner->add(
			object_ptr<Ui::Checkbox>(
				inner,
				tr::lng_wallet_backup_update_check(tr::now),
				false,
				st::defaultBoxCheckbox),
			st::walletBackupUpdateCheckboxMargin);
		AddBackupUpdateNote(
			inner,
			BackupUpdateNoteText(session, state->quote.value()),
			update->checkedValue());
		wrap->hide(anim::type::instant);
		box->showFinishes() | rpl::take(1) | rpl::map([=] {
			return state->quote.value();
		}) | rpl::flatten_latest(
		) | rpl::filter([=](const std::optional<RotationQuote> &quote) {
			return RotationQuoteUsable(quote);
		}) | rpl::take(1) | rpl::on_next([=] {
			if (!state->started && !state->loading.current()) {
				wrap->show(anim::type::normal);
			}
		}, box->lifetime());
	}
	// The continuation a press leaves for a quote still out must not own
	// the state that stores it, or a quote that never answers - the engine
	// drops its callbacks at logout - would leak the state and its grant.
	const auto weakState = std::weak_ptr(state);
	const auto proceed = [=] {
		const auto locked = weakState.lock();
		if (!locked) {
			return;
		}
		const auto quote = locked->quote.current();
		Expects(quote.has_value());

		locked->loading = false;
		if (update && RotationQuoteUsable(quote)) {
			update->setDisabled(false);
		}
		if (locked->started) {
			return;
		}
		const auto strong = origin.get();
		if (!locked->rotate) {
			locked->started = true;
			box->closeBox();
			if (strong) {
				CollectBackupPhrase(show, strong, locked->auth);
			}
		} else if (quote->error == SendError::None) {
			locked->started = true;
			box->closeBox();
			if (strong) {
				StartRotation(
					show,
					strong,
					busy,
					quote->feeNano,
					locked->auth);
			}
		} else if (quote->error == SendError::InsufficientFees) {
			ShowBackupTopUpAlert(
				show,
				quote->feeNano,
				crl::guard(box, [=] { box->closeBox(); }));
		}
	};
	const auto submit = [=] {
		if (state->started || state->loading.current()) {
			return;
		}
		state->rotate = update && update->checked();
		if (state->quote.current()) {
			proceed();
			return;
		}
		state->loading = true;
		if (update) {
			update->setDisabled(true);
		}
		state->onQuoted = crl::guard(box, proceed);
	};
	const auto button = box->addButton(
		BusyFooterLabel(
			tr::lng_screen_reader_confirm_disable(),
			state->loading.value()),
		submit,
		st::attentionBoxButton);
	AddBusyFooterSpinner(button, state->loading.value());
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	box->events(
	) | rpl::on_next([=](not_null<QEvent*> e) {
		if (e->type() != QEvent::KeyPress) {
			return;
		}
		const auto k = static_cast<QKeyEvent*>(e.get());
		if (k->key() == Qt::Key_Enter || k->key() == Qt::Key_Return) {
			submit();
		}
	}, box->lifetime());
}

void ShowBackupDisableBox(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		not_null<bool*> busy,
		KeyAuthorization auth) {
	auto &wallet = show->session().wallet();
	const auto state = std::make_shared<BackupDisableState>();
	state->auth = std::move(auth);
	const auto updateOffered = wallet.rotationOffered();
	if (updateOffered) {
		// The quote runs while the box is up, so the fee is most likely
		// known by the time the update checkbox is looked at, and lands in
		// its note the moment the quote answers otherwise. It holds the keys
		// box busy until then: a quote still out refuses every other custody
		// action, a fresh quote beside it first of all, so a confirmation
		// cancelled and reopened over it would only ever see failures.
		*busy = true;
		wallet.quoteRotationFee(state->auth, crl::guard(origin, [=](
				FeeResult fee) {
			*busy = false;
			state->quote = std::make_optional(RotationQuote{
				.feeNano = fee.feeNano,
				.error = fee.error,
			});
			if (const auto onQuoted = base::take(state->onQuoted)) {
				onQuoted();
			}
		}));
	} else {
		state->quote = std::make_optional(RotationQuote{
			.error = SendError::SigningUnavailable,
		});
	}
	show->showBox(Box(
		WalletBackupDisableBox,
		show,
		base::make_weak(origin),
		busy,
		state,
		updateOffered));
}

// The Disable press is where this flow acquires its one authorization: the
// quote, the reveal and the rotation store that follow all run under the
// same grant, and a vault emptied under them fails typed instead of asking
// again in the middle of the write-down. The confirmation comes after it,
// because the quote it starts needs that grant.
void StartBackupDisable(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		not_null<bool*> busy,
		not_null<rpl::variable<bool>*> restoring) {
	const auto weak = base::make_weak(origin);
	const auto confirm = [=] {
		AcquireVaultUnlock({
			.show = show,
			.done = [=](KeyAuthorization auth) {
				const auto strong = weak.get();
				if (!strong) {
					return;
				} else if (!auth.valid()) {
					*busy = false;
					return;
				}
				ShowBackupDisableBox(show, strong, busy, std::move(auth));
			},
		});
	};
	if (show->session().wallet().revealsLocally()) {
		confirm();
		return;
	}
	*busy = true;
	*restoring = true;
	StartCustodyRestore(
		show,
		KeyAuthorization{ .install = MakeCustodyInstaller(show) },
		[=] {
			if (weak) {
				*busy = false;
				*restoring = false;
				confirm();
			}
		},
		crl::guard(origin, [=] {
			*busy = false;
			*restoring = false;
		}));
}

struct ImportCover {
	not_null<Ui::RpWidget*> widget;
	Fn<int()> height;
	Fn<void()> updateScroll;
};

[[nodiscard]] ImportCover SetupImportCover(
		not_null<Ui::GenericBox*> box,
		WalletImportMode mode,
		rpl::producer<QString> about) {
	const auto spacer = box->verticalLayout()->add(
		object_ptr<Ui::RpWidget>(box));
	const auto cover = Ui::CreateChild<Ui::RpWidget>(box.get());
	cover->show();

	struct State {
		std::unique_ptr<Lottie::Icon> icon;
		Ui::FlatLabel *about = nullptr;
		QPainterPath titlePath;
		int fullTitleTop = 0;
		int maxHeight = 0;
		float64 aboutOpacity = -1.;
	};
	const auto state = cover->lifetime().make_state<State>();
	state->icon = Lottie::MakeIcon({
		.name = u"wallet/paper"_q,
		.sizeOverride = QSize(
			st::walletCoverLottieSize,
			st::walletCoverLottieSize),
		.limitFps = true,
	});
	state->about = Ui::CreateChild<Ui::FlatLabel>(
		cover,
		(about
			? std::move(about)
			: (mode == WalletImportMode::Restore)
			? tr::lng_wallet_restore_text()
			: tr::lng_wallet_import_text()),
		st::walletPhraseTextLabel);
	state->about->setAttribute(Qt::WA_TransparentForMouseEvents);

	auto title = (mode == WalletImportMode::Restore)
		? tr::lng_wallet_restore_title()
		: tr::lng_wallet_import_title();
	std::move(title) | rpl::on_next([=](const QString &text) {
		state->titlePath = QPainterPath();
		state->titlePath.addText(
			0,
			st::boxTitle.style.font->ascent,
			st::boxTitle.style.font,
			text);
		cover->update();
	}, cover->lifetime());

	const auto countProgress = [=] {
		return (state->maxHeight > st::boxTitleHeight)
			? std::clamp(
				(cover->height() - st::boxTitleHeight)
					/ float64(state->maxHeight - st::boxTitleHeight),
				0.,
				1.)
			: 1.;
	};
	const auto countBodyOpacity = [](float64 progress) {
		return 1. - std::clamp((1. - progress) / kCoverBodyPart, 0., 1.);
	};
	const auto countArtRect = [=](float64 opacity) {
		const auto side = st::walletCoverLottieSize * opacity;
		return QRectF(
			(cover->width() - side) / 2.,
			st::walletCoverLottieMargin.top() * opacity,
			side,
			side);
	};
	const auto updateScroll = [=] {
		if (state->maxHeight <= st::boxTitleHeight) {
			return;
		}
		const auto height = std::clamp(
			state->maxHeight - box->scrollTop(),
			st::boxTitleHeight,
			state->maxHeight);
		cover->setGeometry(0, 0, box->width(), height);
		const auto opacity = countBodyOpacity(countProgress());
		if (state->aboutOpacity != opacity) {
			state->aboutOpacity = opacity;
			state->about->setOpacity(opacity);
			state->about->moveToLeft(
				st::boxRowPadding.left(),
				int(countArtRect(opacity).bottom())
					+ st::walletCoverLottieMargin.bottom()
					+ st::boxTitleFont->height
					+ st::walletPhraseTextMargin.top());
		}
	};

	const auto relayout = [=] {
		const auto width = box->width();
		if (width <= 0) {
			return;
		}
		state->about->resizeToWidth(width
			- st::boxRowPadding.left()
			- st::boxRowPadding.right());
		state->fullTitleTop = st::walletCoverLottieMargin.top()
			+ st::walletCoverLottieSize
			+ st::walletCoverLottieMargin.bottom();
		state->maxHeight = state->fullTitleTop
			+ st::boxTitleFont->height
			+ st::walletPhraseTextMargin.top()
			+ state->about->height()
			+ st::walletPhraseTextMargin.bottom();
		spacer->resize(width, state->maxHeight);
		updateScroll();
	};
	rpl::combine(
		box->widthValue(),
		state->about->heightValue()
	) | rpl::on_next(relayout, cover->lifetime());

	cover->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(cover);
		p.fillRect(cover->rect(), st::boxBg);
		auto hq = PainterHighQualityEnabler(p);
		const auto progress = countProgress();
		const auto opacity = countBodyOpacity(progress);
		if (opacity > 0.) {
			const auto artRect = countArtRect(opacity);
			const auto frame = state->icon->frame(
				QSize(int(artRect.width()), int(artRect.height())),
				[=] { cover->update(); });
			p.setOpacity(opacity);
			p.drawImage(artRect, frame.image);
		}
		p.setOpacity(1.);
		const auto titleRect = state->titlePath.boundingRect();
		p.translate(
			anim::interpolate(
				(cover->width() - titleRect.width()) / 2,
				st::boxTitlePosition.x(),
				1. - progress),
			anim::interpolate(
				state->fullTitleTop,
				st::boxTitlePosition.y(),
				1. - progress));
		p.translate(titleRect.center());
		const auto scale = 1. + kCoverTitleScale * progress;
		p.scale(scale, scale);
		p.translate(-titleRect.center());
		p.fillPath(state->titlePath, st::boxTitleFg);
	}, cover->lifetime());

	base::install_event_filter(cover, [=](not_null<QEvent*> event) {
		if (event->type() == QEvent::Wheel) {
			box->sendScrollViewportEvent(event);
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	});

	box->showFinishes() | rpl::on_next([=] {
		const auto icon = state->icon.get();
		const auto update = [=] { cover->update(); };
		if (anim::Disabled()) {
			icon->jumpTo(icon->framesCount() - 1, update);
		} else {
			icon->animate(update, 0, icon->framesCount() - 1);
		}
	}, cover->lifetime());

	return {
		.widget = cover,
		.height = [=] { return cover->height(); },
		.updateScroll = updateScroll,
	};
}

void WalletImportBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		WalletImportMode mode,
		Fn<void()> restored,
		std::shared_ptr<KeyContext> context,
		rpl::producer<QString> about) {
	if (context) {
		context->cancelOnClose(box);
	}
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	struct State {
		std::vector<Ui::InputField*> fields;
		std::vector<Ui::RoundButton*> pasteButtons;
		std::vector<Ui::CrossButton*> clearButtons;
		rpl::variable<int> count = kImportWordCountShort;
		rpl::variable<QString> error;
		std::vector<Ui::AbstractButton*> suggestionRows;
		std::vector<QString> suggestionWords;
		int suggestionField = -1;
		int suggestionSelected = 0;
		int lastFocusedField = -1;
		bool importing = false;
		bool focusRestored = false;
	};
	const auto state = box->lifetime().make_state<State>();

	const auto cover = SetupImportCover(box, mode, std::move(about));

	const auto toggle = box->addRow(
		object_ptr<Ui::SettingsSlider>(box, st::settingsSlider),
		st::walletImportToggleMargin);
	toggle->setSections({
		tr::lng_wallet_import_words(
			tr::now,
			lt_count,
			kImportWordCountShort),
		tr::lng_wallet_import_words(
			tr::now,
			lt_count,
			kImportWordCountLong),
	});
	toggle->setActiveSectionFast(0);

	const auto addWordField = [=](
			not_null<Ui::VerticalLayout*> container,
			int index) {
		const auto field = container->add(
			object_ptr<Ui::InputField>(
				container,
				st::walletImportField,
				Ui::InputField::Mode::SingleLine),
			st::walletImportFieldMargin);
		const auto number = Ui::CreateChild<Ui::FlatLabel>(
			field,
			QString::number(index + 1) + QChar('.'),
			st::walletPhraseNumberLabel);
		number->setAttribute(Qt::WA_TransparentForMouseEvents);
		const auto paste = Ui::CreateChild<Ui::RoundButton>(
			field,
			tr::lng_mac_menu_paste(),
			st::walletImportPaste);
		paste->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
		paste->hide();
		const auto clear = Ui::CreateChild<Ui::CrossButton>(
			field,
			st::walletImportClear);
		field->widthValue(
		) | rpl::on_next([=](int width) {
			number->moveToLeft(
				st::walletImportNumberLeft,
				st::walletImportNumberTop,
				width);
			paste->moveToRight(0, st::walletImportPasteTop);
			clear->moveToRight(
				st::walletImportClearPosition.x(),
				st::walletImportClearPosition.y(),
				width);
		}, field->lifetime());
		state->fields.push_back(field);
		state->pasteButtons.push_back(paste);
		state->clearButtons.push_back(clear);
	};
	for (auto i = 0; i != kImportWordCountShort; ++i) {
		addWordField(box->verticalLayout(), i);
	}
	const auto extraWrap = box->verticalLayout()->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			box->verticalLayout(),
			object_ptr<Ui::VerticalLayout>(box->verticalLayout())));
	const auto extra = extraWrap->entity();
	for (auto i = kImportWordCountShort; i != kImportWordCountLong; ++i) {
		addWordField(extra, i);
	}
	extraWrap->toggleOn(state->count.value(
	) | rpl::map([](int count) { return count == kImportWordCountLong; }));
	extraWrap->finishAnimating();

	const auto error = box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			state->error.value(),
			st::walletImportErrorLabel),
		style::margins(
			st::boxRowPadding.left(),
			st::walletImportErrorSkip,
			st::boxRowPadding.right(),
			0),
		style::al_top);
	error->setVisible(false);
	state->error.value() | rpl::on_next([=](const QString &text) {
		error->setVisible(!text.isEmpty());
	}, error->lifetime());

	AddBoxCloseButton(box);

	const auto wordAt = [=](int index) {
		return state->fields[index]->getLastText().trimmed().toLower();
	};
	const auto requiredCount = [=] {
		const auto count = state->count.current();
		for (auto i = kImportWordCountShort; i != count; ++i) {
			if (!wordAt(i).isEmpty()) {
				return count;
			}
		}
		return kImportWordCountShort;
	};
	const auto markWord = [=](int index, bool typing) {
		const auto field = state->fields[index];
		const auto word = wordAt(index);
		if (word.isEmpty()
			|| (typing
				? !WordlistSuggestions(word, 1).empty()
				: IsWordlistWord(word))) {
			return;
		}
		field->showErrorNoFocus();
		if (typing) {
			field->finishAnimating();
		}
	};
	const auto revealField = [=](not_null<Ui::InputField*> field) {
		const auto top = field->mapTo(box, QPoint()).y() + box->scrollTop();
		box->scrollToY(top - st::boxTitleHeight, top + field->height());
	};
	const auto refreshAccessories = [=](int index) {
		const auto field = state->fields[index];
		const auto focused = field->hasFocus();
		const auto empty = field->getLastText().isEmpty();
		state->pasteButtons[index]->setVisible(focused && empty);
		state->clearButtons[index]->toggle(
			focused && !empty,
			anim::type::instant);
	};
	const auto applyCount = [=](int count) {
		if (state->count.current() != count) {
			state->count = count;
		}
		const auto section = (count == kImportWordCountLong) ? 1 : 0;
		if (toggle->activeSection() != section) {
			toggle->setActiveSection(section);
		}
	};
	const auto distributePaste = [=](const QStringList &words) {
		const auto count = int(words.size());
		if (count != kImportWordCountShort
			&& count != kImportWordCountLong) {
			state->error = tr::lng_wallet_import_paste_error(tr::now);
			return;
		}
		applyCount(count);
		for (auto i = 0; i != count; ++i) {
			state->fields[i]->setText(words[i].toLower());
			state->fields[i]->forceProcessContentsChanges();
		}
		state->error = QString();
		const auto last = state->fields[count - 1];
		crl::on_main(last, [=] {
			last->setFocus();
			last->setCursorPosition(last->getLastText().size());
		});
	};
	const auto submit = [=] {
		if (context && !context->valid()) {
			context->cancel();
			return;
		}
		if (state->importing) {
			return;
		}
		const auto count = requiredCount();
		auto words = std::vector<QString>();
		words.reserve(count);
		for (auto i = 0; i != count; ++i) {
			words.push_back(wordAt(i));
		}
		const auto empty = ranges::find(words, QString());
		const auto wrong = (empty != end(words))
			? empty
			: ranges::find_if(words, [](const QString &word) {
				return !IsWordlistWord(word);
			});
		if (wrong != end(words)) {
			const auto field = state->fields[wrong - begin(words)];
			revealField(field);
			if (wrong->isEmpty()) {
				field->setFocus();
			} else {
				field->showError();
			}
			return;
		}
		const auto match = DetectPhraseMatch(words);
		if (match != PhraseMatch::Rotation) {
			state->error = QString();
			ShowInvalidSecretWords(show, match == PhraseMatch::Foreign, context);
			return;
		}
		state->importing = true;
		if (mode == WalletImportMode::Restore) {
			const auto done = crl::guard(box, [=] {
				if (restored) {
					box->closeBox();
					RunWhenSigningReady(show, restored);
				} else {
					show->hideLayer();
					show->showToast({
						.title = tr::lng_wallet_imported_title(tr::now),
						.text = { tr::lng_wallet_imported_text(tr::now) },
						.icon = &st::toastCheckIcon,
					});
				}
			});
			const auto fail = crl::guard(box, [=](const QString &error) {
				if (context && (!context->valid()
					|| error == u"PHRASE_ORIGIN_EXPIRED"_q
					|| error == u"PHRASE_SILENT_ERROR"_q
					|| error == u"PHRASE_INSTALL_CANCELLED"_q)) {
					context->cancel();
					return;
				}
				state->importing = false;
				if (error == u"PHRASE_INVALID_PHRASE"_q
					|| error == u"PHRASE_FOREIGN_PHRASE"_q) {
					state->error = QString();
					ShowInvalidSecretWords(
						show,
						error == u"PHRASE_FOREIGN_PHRASE"_q,
						context);
					return;
				} else if (error == u"PHRASE_OTHER_WALLET"_q
					|| error == u"PHRASE_OUTDATED"_q) {
					state->error = QString();
					ShowWrongSecretWords(
						show,
						error == u"PHRASE_OUTDATED"_q,
						context);
					return;
				}
				// A dismissed protection chooser restored nothing and has
				// nothing to state, so the form simply stays as it was.
				// A locked vault is stated on this label, not in the toast
				// its replace and backup-enable siblings use: unlocking
				// the vault leaves the typed words ready to resubmit.
				state->error = (error == u"PHRASE_INSTALL_CANCELLED"_q)
					? QString()
					: (error == u"PHRASE_INSTALL_FAILED"_q)
					? tr::lng_wallet_key_save_error(tr::now)
					: (error == u"PHRASE_KEY_CHANGING"_q)
					? tr::lng_wallet_import_key_changing(tr::now)
					: (error == u"PHRASE_VAULT_LOCKED"_q)
					? tr::lng_wallet_vault_locked(tr::now)
					: ErrorWithType(
						tr::lng_wallet_import_failed(tr::now),
						error);
				if (context) {
					show->showToast(state->error.current());
					context->cancel();
				}
			});
			if (context) {
				show->session().wallet().restoreFromPhrase(
					KeyAuthorization{ .install = context->installer() },
					std::move(words),
					context->scope(),
					[=](KeyAuthorization auth) {
						context->ready(std::move(auth));
					},
					fail);
			} else {
				show->session().wallet().restoreFromPhrase(
					KeyAuthorization{ .install = MakeCustodyInstaller(show) },
					std::move(words),
					done,
					fail);
			}
		} else {
			StartWalletReplace(show, box, std::move(words), [=] {
				state->importing = false;
			}, [=](const QString &text) {
				state->error = text;
			});
		}
	};
	box->addButton(tr::lng_wallet_import_button(), submit);

	box->setFocusCallback([=] {
		state->fields.front()->setFocusFast();
	});

	const auto suggestions = Ui::CreateChild<Ui::RpWidget>(box.get());
	suggestions->hide();
	suggestions->setFocusPolicy(Qt::NoFocus);
	suggestions->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(suggestions);
		auto hq = PainterHighQualityEnabler(p);
		const auto inner = suggestions->rect().marginsRemoved(
			st::boxRoundShadow.extend);
		Ui::Shadow::paint(
			p,
			inner,
			suggestions->width(),
			st::boxRoundShadow);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBg);
		p.drawRoundedRect(inner, st::boxRadius, st::boxRadius);
	}, suggestions->lifetime());

	const auto hideSuggestions = [=] {
		state->suggestionField = -1;
		state->suggestionWords.clear();
		suggestions->hide();
	};
	const auto repositionSuggestions = [=] {
		const auto index = state->suggestionField;
		if (index < 0) {
			return;
		}
		const auto field = state->fields[index];
		const auto &extend = st::boxRoundShadow.extend;
		const auto &padding = st::walletImportSuggestionsPadding;
		const auto &rowPadding = st::walletImportSuggestionRowPadding;
		auto textWidth = 0;
		for (const auto &word : state->suggestionWords) {
			textWidth = std::max(textWidth, st::normalFont->width(word));
		}
		const auto innerWidth = std::min(
			padding.left()
				+ rowPadding.left()
				+ textWidth
				+ rowPadding.right()
				+ padding.right(),
			field->width());
		const auto count = int(state->suggestionWords.size());
		const auto innerHeight = padding.top()
			+ count * st::walletImportSuggestionRowHeight
			+ padding.bottom();
		suggestions->resize(
			extend.left() + innerWidth + extend.right(),
			extend.top() + innerHeight + extend.bottom());
		for (auto i = 0; i != int(state->suggestionRows.size()); ++i) {
			const auto row = state->suggestionRows[i];
			row->setVisible(i < count);
			row->setGeometry(
				extend.left() + padding.left(),
				extend.top()
					+ padding.top()
					+ i * st::walletImportSuggestionRowHeight,
				innerWidth - padding.left() - padding.right(),
				st::walletImportSuggestionRowHeight);
		}
		const auto fieldTopLeft = field->mapTo(box, QPoint(0, 0));
		const auto fieldTop = fieldTopLeft.y();
		const auto below = fieldTop
			+ field->height()
			+ st::walletImportSuggestionsSkip;
		const auto flip = (below + innerHeight > box->height());
		const auto top = flip
			? (fieldTop
				- st::walletImportSuggestionsSkip
				- innerHeight
				- extend.top())
			: (below - extend.top());
		suggestions->move(fieldTopLeft.x() - extend.left(), top);
		if (fieldTop + field->height() <= cover.height()
			|| fieldTop >= box->height()) {
			suggestions->hide();
		} else {
			suggestions->show();
		}
	};
	const auto refreshSuggestions = [=](int index) {
		const auto field = state->fields[index];
		const auto typed = wordAt(index);
		auto words = typed.isEmpty()
			? std::vector<QString>()
			: WordlistSuggestions(typed, kImportSuggestionsLimit);
		if (!field->hasFocus()
			|| words.empty()
			|| (words.size() == 1 && words.front() == typed)) {
			hideSuggestions();
			return;
		}
		state->suggestionField = index;
		state->suggestionWords = std::move(words);
		state->suggestionSelected = 0;
		suggestions->raise();
		repositionSuggestions();
		suggestions->update();
	};
	const auto acceptSuggestion = [=] {
		const auto index = state->suggestionField;
		if (index < 0
			|| state->suggestionWords.empty()
			|| suggestions->isHidden()) {
			return false;
		}
		const auto selected = std::clamp(
			state->suggestionSelected,
			0,
			int(state->suggestionWords.size()) - 1);
		const auto word = state->suggestionWords[selected];
		const auto field = state->fields[index];
		const auto typed = wordAt(index);
		hideSuggestions();
		if (word == typed) {
			return false;
		}
		field->setText(word);
		field->forceProcessContentsChanges();
		if (index + 1 < state->count.current()) {
			state->fields[index + 1]->setFocus();
		} else {
			field->setCursorPosition(word.size());
		}
		return true;
	};
	const auto moveSuggestionSelection = [=](int delta) {
		const auto count = int(state->suggestionWords.size());
		if (!count) {
			return;
		}
		state->suggestionSelected = std::clamp(
			state->suggestionSelected + delta,
			0,
			count - 1);
		suggestions->update();
	};
	for (auto i = 0; i != kImportSuggestionsLimit; ++i) {
		const auto row = Ui::CreateChild<Ui::AbstractButton>(suggestions);
		row->setPointerCursor(true);
		row->setFocusPolicy(Qt::NoFocus);
		row->paintRequest(
		) | rpl::on_next([=] {
			auto p = QPainter(row);
			if (i == state->suggestionSelected) {
				p.fillRect(row->rect(), st::windowBgOver);
			}
			if (i >= int(state->suggestionWords.size())
				|| state->suggestionField < 0) {
				return;
			}
			const auto &word = state->suggestionWords[i];
			const auto typed = wordAt(state->suggestionField);
			const auto prefix = word.startsWith(typed)
				? typed
				: QString();
			const auto font = st::normalFont;
			p.setFont(font);
			const auto left = st::walletImportSuggestionRowPadding.left();
			const auto baseline = (row->height() - font->height) / 2
				+ font->ascent;
			p.setPen(st::windowFg);
			p.drawText(left, baseline, prefix);
			p.setPen(st::windowSubTextFg);
			p.drawText(
				left + font->width(prefix),
				baseline,
				word.mid(prefix.size()));
		}, row->lifetime());
		row->setClickedCallback([=] {
			state->suggestionSelected = i;
			acceptSuggestion();
		});
		state->suggestionRows.push_back(row);
	}

	toggle->sectionActivated(
	) | rpl::on_next([=](int section) {
		applyCount((section == 1)
			? kImportWordCountLong
			: kImportWordCountShort);
	}, toggle->lifetime());
	state->count.changes() | rpl::on_next([=] {
		state->error = QString();
		hideSuggestions();
	}, box->lifetime());

	for (auto i = 0; i != kImportWordCountLong; ++i) {
		const auto field = state->fields[i];
		state->pasteButtons[i]->setClickedCallback([=] {
			const auto text = QGuiApplication::clipboard()->text();
			const auto words = SplitPhraseWords(text);
			if (words.size() > 1) {
				distributePaste(words);
			} else {
				field->setText(text.trimmed().toLower());
				field->forceProcessContentsChanges();
				field->setFocusFast();
			}
		});
		state->clearButtons[i]->setClickedCallback([=] {
			field->setText(QString());
			field->forceProcessContentsChanges();
			field->setFocusFast();
		});
		field->setMimeDataHook([=](
				not_null<const QMimeData*> data,
				Ui::InputField::MimeAction action) {
			const auto text = data->hasText() ? data->text() : QString();
			const auto words = SplitPhraseWords(text);
			if (words.size() < 2) {
				return false;
			}
			if (action == Ui::InputField::MimeAction::Check) {
				return true;
			}
			distributePaste(words);
			return true;
		});
		field->submits() | rpl::on_next([=] {
			if (acceptSuggestion()) {
				return;
			}
			if (i + 1 < state->count.current()) {
				state->fields[i + 1]->setFocus();
			} else {
				submit();
			}
		}, field->lifetime());
		field->tabbed() | rpl::on_next([=](
				not_null<Ui::InputField::TabbedRequest*> request) {
			if (request->backward) {
				if (i > 0) {
					request->handled = true;
					state->fields[i - 1]->setFocus();
				}
				return;
			}
			request->handled = true;
			if (acceptSuggestion()) {
				return;
			}
			if (i + 1 < state->count.current()) {
				state->fields[i + 1]->setFocus();
			}
		}, field->lifetime());
		base::install_event_filter(field->rawTextEdit(), [=](
				not_null<QEvent*> event) {
			if (event->type() == QEvent::FocusIn) {
				const auto focus = static_cast<QFocusEvent*>(event.get());
				const auto reason = focus->reason();
				// Focus given back to the same field keeps a manual scroll.
				state->focusRestored = (state->lastFocusedField == i)
					&& (reason == Qt::ActiveWindowFocusReason
						|| reason == Qt::PopupFocusReason);
				state->lastFocusedField = i;
				return base::EventFilterResult::Continue;
			}
			if (event->type() != QEvent::KeyPress) {
				return base::EventFilterResult::Continue;
			}
			const auto key = static_cast<QKeyEvent*>(event.get())->key();
			const auto shown = (state->suggestionField == i)
				&& !suggestions->isHidden();
			if (shown && key == Qt::Key_Down) {
				moveSuggestionSelection(1);
				return base::EventFilterResult::Cancel;
			} else if (shown && key == Qt::Key_Up) {
				moveSuggestionSelection(-1);
				return base::EventFilterResult::Cancel;
			} else if (shown && key == Qt::Key_Escape) {
				hideSuggestions();
				return base::EventFilterResult::Cancel;
			} else if (key == Qt::Key_Backspace
				&& field->getLastText().isEmpty()
				&& i > 0) {
				state->fields[i - 1]->setFocus();
				return base::EventFilterResult::Cancel;
			}
			return base::EventFilterResult::Continue;
		});
		field->changes() | rpl::on_next([=] {
			state->error = QString();
			refreshAccessories(i);
			refreshSuggestions(i);
			if (field->hasFocus()) {
				revealField(field);
			}
			// WHY: every content-change pass starts by clearing the error,
			// including the one forceProcessContentsChanges() postpones after
			// setText(), so marking is postponed to land after it.
			Ui::PostponeCall(field, [=] {
				markWord(i, field->hasFocus());
			});
		}, field->lifetime());
		field->focusedChanges() | rpl::on_next([=](bool focused) {
			markWord(i, false);
			refreshAccessories(i);
			if (focused) {
				refreshSuggestions(i);
				if (!base::take(state->focusRestored)) {
					revealField(field);
				}
			} else if (state->suggestionField == i) {
				hideSuggestions();
			}
		}, field->lifetime());
		refreshAccessories(i);
	}

	// A pasted 24-word phrase focuses its last field while this still opens.
	extraWrap->heightValue(
	) | rpl::skip(1) | rpl::on_next([=] {
		if (!extraWrap->toggled()) {
			return;
		}
		for (auto i = kImportWordCountShort; i != kImportWordCountLong; ++i) {
			if (state->fields[i]->hasFocus()) {
				revealField(state->fields[i]);
				return;
			}
		}
	}, extraWrap->lifetime());

	box->widthValue() | rpl::skip(1) | rpl::on_next([=] {
		repositionSuggestions();
	}, box->lifetime());
	box->setInitScrollCallback([=] {
		cover.widget->raise();
		cover.updateScroll();
		box->scrolls() | rpl::on_next([=] {
			cover.updateScroll();
			repositionSuggestions();
		}, box->lifetime());
	});
}

void WalletReplaceBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show) {
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_wallet_replace_title(),
			st::walletReplaceTitleLabel),
		st::walletReplaceTitleMargin);
	const auto create = box->addRow(
		object_ptr<Ui::RoundButton>(
			box,
			tr::lng_wallet_replace_create(),
			st::walletSendButton),
		st::walletReplaceButtonMargin,
		style::al_justify);
	create->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	const auto creating = box->lifetime().make_state<bool>(false);
	create->setClickedCallback([=] {
		if (*creating) {
			return;
		}
		*creating = true;
		StartWalletReplace(show, box, std::nullopt, [=] {
			*creating = false;
		});
	});
	const auto import = box->addRow(
		object_ptr<Ui::RoundButton>(
			box,
			tr::lng_wallet_replace_import(),
			st::walletSendButton),
		st::walletReplaceButtonMargin,
		style::al_justify);
	import->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	import->setClickedCallback([=] {
		box->closeBox();
		show->showBox(Box(
			WalletImportBox,
			show,
			WalletImportMode::Replace,
			nullptr,
			nullptr,
			nullptr));
	});
	Ui::AddSkip(box->verticalLayout());
}

void WalletConflictBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		Fn<void()> switched) {
	struct State {
		bool busy = false;
	};
	// The box closes on its own once a drop leaves no parked record, and
	// that close continues the action that hit the conflict. Every other
	// close, including one while a drop is still pending, is the user giving
	// up, and the drop's completion then continues nothing. The outcome
	// outlives the box, because the settling close can destroy it before
	// the drop's callback runs.
	struct Outcome {
		bool settled = false;
		bool cancelled = false;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto outcome = std::make_shared<Outcome>();
	const auto closed = [=] {
		if (!outcome->settled) {
			outcome->cancelled = true;
		}
	};
	box->boxClosing() | rpl::on_next(closed, box->lifetime());
	box->lifetime().add(closed);
	const auto wallet = &show->session().wallet();

	box->setStyle(st::walletConflictBox);
	Ui::AddSkip(box->verticalLayout(), st::walletConflictBoxTopSkip);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_wallet_conflict_text(),
			st::boxLabel),
		st::boxRowPadding);

	const auto content = box->verticalLayout()->add(
		object_ptr<Ui::VerticalLayout>(box->verticalLayout()));

	const auto rebuild = [=] {
		const auto parked = wallet->parkedRecords();
		if (parked.empty()) {
			outcome->settled = true;
			box->closeBox();
			return;
		}
		content->clear();
		auto first = true;
		for (const auto &record : parked) {
			if (!first) {
				Ui::AddSkip(content);
				Ui::AddDivider(content);
			}
			first = false;
			const auto address = FormatFriendly(
				CanonicalAddress(record.address),
				false);
			if (address.size() == kAddressLength) {
				AddAddressPlate(
					content,
					address,
					QMargins(
						st::boxRowPadding.left(),
						st::walletAddressPlateSkip,
						st::boxRowPadding.right(),
						st::walletAddressPlateSkip));
			} else {
				Ui::AddSkip(content);
			}
			const auto key = record.publicKey;
			const auto showPhrase = content->add(
				object_ptr<Ui::RoundButton>(
					content,
					tr::lng_wallet_keys_show_phrase(),
					st::defaultLightButton),
				st::boxRowPadding,
				style::al_justify);
			showPhrase->setFullRadius(true);
			showPhrase->setClickedCallback([=] {
				WalletRevealFlow(show, key);
			});
			Ui::AddSkip(content);
			const auto switchNow = content->add(
				object_ptr<Ui::RoundButton>(
					content,
					tr::lng_wallet_conflict_switch(),
					st::attentionBoxButton),
				st::boxRowPadding,
				style::al_justify);
			switchNow->setFullRadius(true);
			switchNow->setClickedCallback([=] {
				if (state->busy) {
					return;
				}
				state->busy = true;
				// Dropping the last parked record fires the custody update
				// that closes this emptied box from rebuild() before this
				// callback runs, and with another box underneath that close
				// destroys it at once, so the continuation is not guarded
				// by the box. It runs only once no parked record is left,
				// with more of them listed the box stays for the next one,
				// and never after the user closed the box while the drop
				// was still pending.
				const auto weak = base::make_weak(box.get());
				wallet->dropParked(key, [=] {
					const auto resolved = !wallet->deviceCustodyState().conflict;
					if (const auto strong = weak.get()) {
						state->busy = false;
						if (resolved && switched && strong->hasDelegate()) {
							outcome->settled = true;
							strong->closeBox();
						}
					}
					if (resolved && switched && !outcome->cancelled) {
						switched();
					}
				}, crl::guard(box, [=](const QString &error) {
					state->busy = false;
					ShowPhraseError(show, PhraseOperation::DropParked, error);
				}));
			});
		}
		if (const auto width = content->width()) {
			content->resizeToWidth(width);
		}
	};
	rebuild();

	wallet->custodyUpdates(
	) | rpl::on_next(rebuild, box->lifetime());

	wallet->presenceValue(
	) | rpl::filter([](Presence presence) {
		return (presence != Presence::Ready);
	}) | rpl::on_next([=] {
		box->closeBox();
	}, box->lifetime());

	Ui::AddSkip(box->verticalLayout());
	const auto cancel = box->verticalLayout()->add(
		object_ptr<Ui::RoundButton>(
			box,
			tr::lng_cancel(),
			st::defaultLightButton),
		st::boxRowPadding,
		style::al_justify);
	cancel->setFullRadius(true);
	cancel->setClickedCallback([=] {
		box->closeBox();
	});
}

void AddBackupSection(
		not_null<Ui::VerticalLayout*> container,
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> box) {
	auto &wallet = show->session().wallet();
	const auto busy = box->lifetime().make_state<bool>(false);
	const auto restoring = box->lifetime().make_state<rpl::variable<bool>>(
		false);
	Ui::AddSubsectionTitle(container, tr::lng_wallet_backup_section());
	const auto disable = container->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			container,
			Settings::CreateButtonWithIcon(
				container,
				tr::lng_wallet_backup_disable(),
				st::settingsAttentionButton)));
	disable->toggleOn(wallet.capabilitiesValue(
	) | rpl::map([](const WalletCapabilities &capabilities) {
		return capabilities.backupEnabled;
	}));
	disable->finishAnimating();
	AddRowSpinner(disable->entity(), restoring->value());
	disable->entity()->addClickHandler([=] {
		if (*busy) {
			return;
		}
		RunKeyRequiringAction(show, crl::guard(box, [=] {
			StartBackupDisable(show, box, busy, restoring);
		}), KeyActionKind::Reveal);
	});
	const auto enable = container->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			container,
			Settings::CreateButtonWithIcon(
				container,
				tr::lng_wallet_backup_enable(),
				st::settingsButtonNoIcon)));
	enable->toggleOn(wallet.capabilitiesValue(
	) | rpl::map([](const WalletCapabilities &capabilities) {
		return capabilities.canEnableBackup && !capabilities.backupEnabled;
	}));
	enable->finishAnimating();
	enable->entity()->addClickHandler([=] {
		StartBackupEnable(show, box, busy);
	});
	Ui::AddSkip(container);
	Ui::AddDividerText(container, wallet.capabilitiesValue(
	) | rpl::map([](const WalletCapabilities &capabilities) {
		return (capabilities.backupEnabled || capabilities.canEnableBackup)
			? tr::lng_wallet_backup_about_on()
			: tr::lng_wallet_backup_about_unavailable();
	}) | rpl::flatten_latest());
	Ui::AddSkip(container);
}

void WalletKeysBackupBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show) {
	auto &wallet = show->session().wallet();
	if (wallet.presence() != Presence::Ready) {
		box->closeBox();
		return;
	}
	box->setTitle(tr::lng_wallet_keys_title());
	const auto container = box->verticalLayout();
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, tr::lng_wallet_phrase_intro_title());
	const auto phrase = container->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			container,
			Settings::CreateButtonWithIcon(
				container,
				tr::lng_wallet_keys_show_phrase(),
				st::settingsButtonNoIcon)));
	phrase->toggleOn(rpl::combine(
		wallet.capabilitiesValue(),
		wallet.deviceCustodyStateValue()
	) | rpl::map([](
			const WalletCapabilities &capabilities,
			const DeviceCustodyState &custody) {
		return capabilities.canExportPhrase
			|| (custody.mode == DeviceMode::Full);
	}));
	phrase->finishAnimating();
	phrase->entity()->addClickHandler([=] {
		RunKeyRequiringAction(show, [=] {
			WalletRevealFlow(show);
		}, KeyActionKind::Reveal);
	});
	const auto restore = container->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			container,
			Settings::CreateButtonWithIcon(
				container,
				tr::lng_wallet_keys_restore(),
				st::settingsButtonNoIcon)));
	restore->toggleOn(wallet.deviceCustodyStateValue(
	) | rpl::map([](const DeviceCustodyState &custody) {
		return (custody.mode == DeviceMode::ReadOnlyNotRestorable);
	}));
	restore->finishAnimating();
	restore->entity()->addClickHandler([=] {
		show->showBox(Box(
			WalletImportBox,
			show,
			WalletImportMode::Restore,
			nullptr,
			nullptr,
			nullptr));
	});
	Ui::AddSkip(container);
	Ui::AddDividerText(container, tr::lng_wallet_keys_phrase_about());
	Ui::AddSkip(container);
	AddBackupSection(container, show, box);
	Settings::AddButtonWithIcon(
		container,
		tr::lng_wallet_keys_delete(),
		st::settingsAttentionButton
	)->addClickHandler([=] {
		show->showBox(Ui::MakeConfirmBox({
			.text = tr::lng_wallet_delete_text(tr::now),
			.confirmed = [=](Fn<void()> close) {
				close();
				show->showBox(Box(WalletReplaceBox, show));
			},
			.confirmText = tr::lng_suggest_warn_delete_anyway(),
			.confirmStyle = &st::attentionBoxButton,
			.title = tr::lng_wallet_delete_title(),
		}));
	});
	Ui::AddSkip(container);
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

[[nodiscard]] BalancePalette CardBalancePalette() {
	return {
		.mark = st::activeButtonFg->c,
		.amount = st::activeButtonFg->c,
		.secondary = st::activeButtonFg->c,
	};
}

[[nodiscard]] BalancePalette SettledBalancePalette() {
	return {
		.mark = st::windowActiveTextFg->c,
		.amount = st::windowBoldFg->c,
		.secondary = st::windowSubTextFg->c,
	};
}

[[nodiscard]] Wallet::AmountStyle MoneyAmountStyle() {
	return {
		.big = st::walletCardBalanceMajorLabel.style.font,
		.small = st::walletCardBalanceMinorLabel.style.font,
		.additionWidth = st::walletCardMarkSize,
		.additionSkip = st::walletCardIconMargin.right(),
		.tickerSkip = st::walletCardTickerSkip,
	};
}

[[nodiscard]] float64 BalanceAmountScale(float64 progress) {
	const auto settled = st::walletBalanceHeaderMajorFont->height
		/ float64(st::walletCardBalanceMajorLabel.style.font->height);
	return 1. + (settled - 1.) * progress;
}

[[nodiscard]] float64 BalanceFiatScale(float64 progress) {
	const auto settled = st::walletBalanceHeaderFiatFont->height
		/ float64(st::walletCardFiatLabel.style.font->height);
	return 1. + (settled - 1.) * progress;
}

[[nodiscard]] float64 BalanceSettledTop() {
	const auto block = st::walletBalanceHeaderMajorFont->height
		+ st::walletBalanceHeaderLineSkip
		+ st::walletBalanceHeaderFiatFont->height;
	return (st::separatePanelTitleHeight - block) / 2.
		- st::separatePanelTitleHeight;
}

// The balance rows ride the folding card: each anchor is a card-local
// rest offset placed in Content coordinates against the card's rest rect
// and mapped through the fold transform, and it then travels to its
// landed position in the title band. The x weight is the square of the
// fold progress, so the row leaves the folded quad through its top edge
// instead of sliding out through the narrowing left corner.
[[nodiscard]] QPointF BalanceRowPosition(
		const CardFold &fold,
		int restTop,
		float64 landedTop) {
	const auto anchor = fold.transform.map(QPointF(
		fold.rest.x() + st::walletCardContentLeft,
		fold.rest.y() + restTop));
	const auto landedLeft = float64(st::separatePanelTitleLeft);
	return QPointF(
		anchor.x() + (landedLeft - anchor.x()) * fold.fold * fold.fold,
		anchor.y() + (landedTop - anchor.y()) * fold.fold);
}

[[nodiscard]] QRectF MarkInkBounds(const QImage &image) {
	auto left = image.width();
	auto top = image.height();
	auto right = -1;
	auto bottom = -1;
	for (auto y = 0; y != image.height(); ++y) {
		for (auto x = 0; x != image.width(); ++x) {
			if (qAlpha(image.pixel(x, y))) {
				left = std::min(left, x);
				top = std::min(top, y);
				right = std::max(right, x);
				bottom = std::max(bottom, y);
			}
		}
	}
	if (right < 0) {
		return QRectF();
	}
	const auto ratio = image.devicePixelRatio();
	return QRectF(
		left / ratio,
		top / ratio,
		(right - left + 1) / ratio,
		(bottom - top + 1) / ratio);
}

BalanceInk::BalanceInk() {
	const auto &font = st::walletCardBalanceMajorLabel.style.font;
	const auto canvas = GramDiamondCanvas(font);
	_markFrame = QRectF(
		-int(base::SafeRound(canvas * kGramDiamondLeft)),
		font->ascent - int(base::SafeRound(canvas * kGramDiamondBottom)),
		canvas,
		canvas);
	_markLottieVisible = QRectF(
		_markFrame.x() + canvas * kGramDiamondLeft,
		_markFrame.y() + canvas * kGramDiamondTop,
		canvas * (kGramDiamondRight - kGramDiamondLeft),
		canvas * (kGramDiamondBottom - kGramDiamondTop));
	_markLottie = Lottie::MakeIcon({
		.name = u"gram_white"_q,
		.sizeOverride = { canvas, canvas },
	});
}

void BalanceInk::setContent(
		CreditsAmount amount,
		const QString &fiat,
		BalanceStyle style) {
	_balance = amount;
	_fiatText = fiat;
	_style = style;
	refresh();
}

void BalanceInk::setOuterWidth(int outerWidth) {
	if (_outerWidth == outerWidth) {
		return;
	}
	_outerWidth = outerWidth;
	refresh();
}

void BalanceInk::playMark(Fn<void()> repaint) {
	if (!_markLottie->valid()) {
		return;
	}
	_markRepaint = repaint;
	_markLottie->animate(
		std::move(repaint),
		0,
		_markLottie->framesCount() - 1);
}

void BalanceInk::refresh() {
	const auto &fiatFont = st::walletCardFiatLabel.style.font;
	const auto exact = (_style != BalanceStyle::Balance);
	const auto amountNano = _balance.whole() * Ui::kNanosInOne
		+ _balance.nano();
	const auto precise = exact
		? Ui::FormatTonAmount(amountNano)
		: Ui::FormattedTonAmount();
	const auto minor = exact
		? (precise.nanoString.isEmpty()
			? QString()
			: (precise.separator + precise.nanoString))
		: _balance.nano()
		? GramMinorPart(amountNano)
		: QString();
	const auto ticker = GramTicker();
	const auto cardWidth = _outerWidth
		- st::walletCardMargin.left()
		- st::walletCardMargin.right();
	const auto qrLeft = CardQrRect(cardWidth).x();
	const auto sign = (_style == BalanceStyle::Minus)
		? QString(kMinus)
		: (_style == BalanceStyle::Plus)
		? u"+"_q
		: QString();
	const auto full = exact
		? (sign + precise.wholeString)
		: GramMajorPart(amountNano);
	_painter.setContent(MoneyAmountStyle(), {
		.whole = full,
		.fraction = minor,
		.ticker = ticker,
	});
	_painter.setAvailableWidth(_outerWidth
		? (qrLeft - st::walletCardContentSkip - st::walletCardContentLeft)
		: 0);

	_fiat = QPainterPath();
	_fiat.addText(0, fiatFont->ascent, fiatFont, _fiatText);
	_fiatWidth = fiatFont->width(_fiatText);

	_markCard = Ui::Earn::IconCurrencyMono(
		st::walletCardMarkSize,
		CardBalancePalette().mark);
	_markSettled = Ui::Earn::IconCurrencyTwoTone(
		st::walletCardMarkSize,
		SettledBalancePalette().mark);
	_markTop = Ui::Earn::AlignedMarkTop(
		st::walletCardBalanceMajorLabel.style.font,
		_markCard);
	_markMonoVisible = MarkInkBounds(_markCard).translated(0., _markTop);
}

QRectF BalanceInk::amountRect(const CardFold &fold) const {
	const auto scale = BalanceAmountScale(fold.fold);
	const auto width = _painter.size().width() * scale;
	const auto height = st::walletCardBalanceMajorLabel.style.font->height
		* scale;
	const auto position = BalanceRowPosition(
		fold,
		st::walletCardBalanceTop,
		BalanceSettledTop());
	return QRectF(position.x(), position.y(), width, height);
}

QRectF BalanceInk::fiatRect(const CardFold &fold) const {
	const auto scale = BalanceFiatScale(fold.fold);
	const auto width = _fiatWidth * scale;
	const auto height = st::walletCardFiatLabel.style.font->height * scale;
	const auto position = BalanceRowPosition(
		fold,
		st::walletCardFiatTop,
		BalanceSettledTop()
			+ st::walletBalanceHeaderMajorFont->height
			+ st::walletBalanceHeaderLineSkip);
	return QRectF(position.x(), position.y(), width, height);
}

QRectF BalanceInk::markVisible(float64 fold) const {
	const auto &from = _markLottieVisible;
	const auto &to = _markMonoVisible;
	return QRectF(
		from.x() + (to.x() - from.x()) * fold,
		from.y() + (to.y() - from.y()) * fold,
		from.width() + (to.width() - from.width()) * fold,
		from.height() + (to.height() - from.height()) * fold);
}

QRectF BalanceInk::markDrawRect(
		float64 fold,
		const QRectF &box,
		const QRectF &visible) const {
	const auto target = markVisible(fold);
	const auto k = target.height() / visible.height();
	return QRectF(
		target.center().x() - (visible.center().x() - box.x()) * k,
		target.y() - (visible.y() - box.y()) * k,
		box.width() * k,
		box.height() * k);
}

void BalanceInk::paintMark(
		QPainter &p,
		float64 fold,
		const QImage &mono,
		bool card) const {
	const auto monoBox = QRectF(
		0.,
		_markTop,
		st::walletCardMarkSize,
		st::walletCardMarkSize);
	if (_markLottieVisible.height() <= 0.
		|| _markMonoVisible.height() <= 0.) {
		p.drawImage(monoBox, mono);
		return;
	}
	// WHY: both passes map their own diamond onto one shared box, so the
	// diamond crossing the folding card's edge stays one shape, lottie
	// inside and mono outside, with no step at the seam.
	if (card && _markLottie->valid()) {
		if (!_markLottie->animating() && _markLottie->frameIndex() != 0) {
			_markLottie->jumpTo(0, _markRepaint);
		}
		p.drawImage(
			markDrawRect(fold, _markFrame, _markLottieVisible),
			_markLottie->frame());
	} else {
		p.drawImage(markDrawRect(fold, monoBox, _markMonoVisible), mono);
	}
}

void BalanceInk::paintPass(
		QPainter &p,
		const CardFold &fold,
		const BalancePalette &palette,
		const QImage &mark,
		bool card,
		float64 secondaryOpacity) const {
	p.save();
	p.setTransform(groupTransform(fold), true);
	paintMark(p, fold.fold, mark, card);
	_painter.paint(p, {
		.digits = palette.amount,
		.ticker = palette.secondary,
		.tickerOpacity = secondaryOpacity,
	});
	p.restore();

	const auto fiat = fiatRect(fold);
	const auto fiatScale = BalanceFiatScale(fold.fold);
	p.save();
	p.setOpacity(p.opacity() * secondaryOpacity);
	p.translate(fiat.x(), fiat.y());
	p.scale(fiatScale, fiatScale);
	p.fillPath(_fiat, palette.secondary);
	p.restore();
}

QTransform BalanceInk::groupTransform(const CardFold &fold) const {
	const auto amount = amountRect(fold);
	const auto foldScale = BalanceAmountScale(fold.fold);
	const auto fit = _painter.scale();
	auto result = QTransform();
	result.translate(amount.x(), amount.y());
	result.scale(foldScale, foldScale);
	result.translate(0., (1. - fit) * _painter.naturalHeight() / 2.);
	result.scale(fit, fit);
	return result;
}

void BalanceInk::paint(
		QPainter &p,
		const CardFold &fold,
		const QRegion &cardOutline,
		QRect clip) const {
	const auto ink = boundingRect(fold);
	const auto inside = cardOutline.intersected(QRegion(clip));
	if (inside.intersects(ink)) {
		p.save();
		p.setClipRegion(inside, Qt::IntersectClip);
		paintPass(
			p,
			fold,
			CardBalancePalette(),
			_markCard,
			true,
			st::walletCardSecondaryOpacity);
		p.restore();
	}
	const auto outside = QRegion(clip) - inside;
	if (outside.intersects(ink)) {
		p.save();
		p.setClipRegion(outside, Qt::IntersectClip);
		paintPass(
			p,
			fold,
			SettledBalancePalette(),
			_markSettled,
			false,
			1.);
		p.restore();
	}
}

QRect BalanceInk::boundingRect(const CardFold &fold) const {
	const auto amount = amountRect(fold);
	const auto fiat = fiatRect(fold);
	return amount.united(fiat).toAlignedRect();
}

QRect BalanceInk::markRect(QRect cardRest) const {
	const auto fit = _painter.scale();
	const auto origin = QPointF(
		cardRest.x() + st::walletCardContentLeft,
		cardRest.y()
			+ st::walletCardBalanceTop
			+ (1. - fit) * _painter.naturalHeight() / 2.);
	const auto top = int(base::SafeRound(_markTop));
	return QRectF(
		origin + QPointF(0., top * fit),
		QSizeF(st::walletCardMarkSize, st::walletCardMarkSize) * fit
	).toAlignedRect();
}

QRect BalanceInk::markPaintRect(const CardFold &fold) const {
	if (_markLottieVisible.height() <= 0.) {
		return QRect();
	}
	return groupTransform(fold).mapRect(
		markDrawRect(fold.fold, _markFrame, _markLottieVisible)
	).toAlignedRect().marginsAdded({ 1, 1, 1, 1 });
}

[[nodiscard]] rpl::producer<TextWithEntities> CardNameValue(
		not_null<Main::Session*> session) {
	return Info::Profile::NameValue(
		session->user()
	) | rpl::map([](QString name) {
		return tr::marked(std::move(name));
	});
}

void SetupCardBalance(
		not_null<BalanceInk*> ink,
		not_null<Main::Session*> session,
		Fn<void()> repaint,
		not_null<Ui::RpWidget*> owner) {
	rpl::combine(
		session->wallet().balanceNanoValue(),
		FiatRateValue(session)
	) | rpl::on_next([=](int64 nano, FiatRate rate) {
		const auto scope = WindowPaletteScope(owner);
		ink->setContent(
			CreditsAmount(
				nano / Ui::kNanosInOne,
				nano % Ui::kNanosInOne,
				CreditsType::Ton),
			FormatFiat(nano, rate));
		repaint();
	}, owner->lifetime());
}

void SetupCardMark(
		not_null<BalanceInk*> ink,
		not_null<Ui::RpWidget*> owner,
		std::shared_ptr<bool> played,
		Fn<void()> repaint) {
	// WHY: a box's layer is shown and hidden again synchronously inside its
	// show animation, so the card re-reads the window's activation on every
	// show and starts the play only once it stays visible.
	const auto open = owner->lifetime().make_state<bool>(false);
	owner->events(
	) | rpl::filter([](not_null<QEvent*> e) {
		return (e->type() == QEvent::Show);
	}) | rpl::map([=] {
		return rpl::combine(
			owner->windowActiveValue(),
			PowerSaving::OnValue(PowerSaving::kStickersChat),
			anim::Disables());
	}) | rpl::flatten_latest(
	) | rpl::on_next([=](bool active, bool saving, bool off) {
		*open = active && !saving && !off;
		if (*open && !*played) {
			InvokeQueued(owner, [=] {
				if (*open && !*played && owner->isVisible()) {
					*played = true;
					ink->playMark(repaint);
				}
			});
		}
	}, owner->lifetime());
}

Card::Card(
	QWidget *parent,
	std::shared_ptr<Main::SessionShow> show,
	rpl::producer<TextWithEntities> name)
: RpWidget(parent)
, _show(std::move(show))
, _nameStyle(st::defaultTextStyle) {
	_nameStyle.font = st::walletCardNameFont->monospace();
	_show->session().wallet().presenceValue(
	) | rpl::on_next([=](Presence) {
		refreshAddress();
	}, lifetime());

	std::move(name) | rpl::on_next([=](TextWithEntities name) {
		_name.setMarkedText(
			_nameStyle,
			tr::upper(std::move(name)),
			kMarkupTextOptions,
			Core::TextContext({
				.session = &_show->session(),
				.repaint = crl::guard(this, [=] { update(); }),
		}));
		invalidateCache();
		update();
	}, lifetime());
}

void Card::setFold(const CardFold &fold) {
	_fold = fold;
	setVisible(fold.valid && fold.opacity > 0.);
	update();
}

const CardFold &Card::fold() const {
	return _fold;
}

QPolygonF Card::paintedQuad() const {
	return _fold.quad;
}

QPolygonF Card::paintedOutline() const {
	if (!_fold.valid) {
		return QPolygonF();
	}
	auto path = QPainterPath();
	path.addRoundedRect(
		QRectF(_fold.rest),
		st::walletCardRadius,
		st::walletCardRadius);
	return _fold.transform.map(path.toFillPolygon());
}

void Card::invalidateCache() {
	_cache = QImage();
}

void Card::followCursor() {
	if (!_angle) {
		_angle = std::make_unique<CardAngle>();
	}
}

QRectF Card::paintedRect() const {
	return _fold.quad.boundingRect().translated(-QPointF(pos()));
}

float64 Card::paintAngle() {
	if (!_angle) {
		return 0.;
	}
	_angle->track(this, this, paintedRect());
	return _angle->value(crl::now());
}

QRect Card::restRect() const {
	return QRect(
		0,
		height() - st::walletCardHeight,
		width(),
		st::walletCardHeight);
}

void Card::refreshAddress() {
	auto &wallet = _show->session().wallet();
	const auto address = wallet.addressFriendly(false);
	_addressLine1 = _addressLine2 = QString();
	if (address.size() == kAddressLength) {
		_addressLine1 = GroupedAddressLine(address, 0);
		_addressLine2 = GroupedAddressLine(address, kAddressLength / 2);
	}
	invalidateCache();
	update();
}

void Card::validateCache(float64 angle) {
	const auto ratio = style::DevicePixelRatio();
	const auto size = restRect().size() * ratio;
	// WHY: the cached fold image carries the sweep, so it is keyed by the
	// angle it was painted at.
	if (!_cache.isNull() && _cache.size() == size && _cacheAngle == angle) {
		return;
	}
	_cacheAngle = angle;
	_cache = QImage(size, QImage::Format_ARGB32_Premultiplied);
	_cache.setDevicePixelRatio(ratio);
	_cache.fill(Qt::transparent);
	auto q = Painter(&_cache);
	auto hq = PainterHighQualityEnabler(q);
	paintContent(q, angle);
}

void Card::paintEvent(QPaintEvent *e) {
	if (!_fold.valid || _fold.opacity <= 0.) {
		return;
	}
	const auto angle = paintAngle();
	auto p = Painter(this);
	if (!_fold.fold) {
		auto hq = PainterHighQualityEnabler(p);
		p.translate(restRect().topLeft());
		paintContent(p, angle);
		return;
	}
	validateCache(angle);
	auto hq = PainterHighQualityEnabler(p);
	p.setOpacity(_fold.opacity);
	p.translate(-x(), -y());
	p.setTransform(_fold.transform, true);
	p.drawImage(QRectF(_fold.rest), _cache);
}

void Card::paintContent(Painter &p, float64 angle) {
	const auto size = restRect().size();
	_background.paint(p, QRect(QPoint(), size), angle);

	const auto qr = CardQrRect(size.width());
	PaintCardQrPlate(p, qr);
	st::walletCardQrIcon.paintInCenter(p, qr, CardQrIconFg());

	const auto &nameFont = _nameStyle.font;
	const auto addressFont = st::walletCardAddressFont->monospace();
	const auto addressBaseline = size.width()
		- st::walletCardAddressRight
		- addressFont->height
		- addressFont->ascent;
	const auto stripLeft = addressBaseline - addressFont->descent;
	const auto nameMax = stripLeft
		- st::walletCardContentSkip
		- st::walletCardContentLeft;
	p.setPen(st::activeButtonFg);
	_name.drawLeftElided(
		p,
		st::walletCardContentLeft,
		size.height() - st::walletCardNameBottom - nameFont->ascent,
		nameMax,
		size.width(),
		1);

	if (!_addressLine1.isEmpty()) {
		p.setPen(st::windowActiveTextFg);
		p.setFont(addressFont);
		p.save();
		p.translate(addressBaseline, st::walletCardAddressSkip);
		p.rotate(90);
		p.drawText(0, 0, _addressLine1);
		p.drawText(0, -addressFont->height, _addressLine2);
		p.restore();
	}
}

CardFold ComputeCardFold(QRect cardRest, float64 fold) {
	auto result = CardFold();
	result.rest = cardRest;
	result.fold = fold;
	result.topY = cardRest.top() - cardRest.top() * fold;
	result.bottomY = result.topY + cardRest.height() * (1. - fold);
	result.opacity = 1. - std::pow(fold, st::walletCardFoldFadePower);
	if (result.bottomY - result.topY < kCardFoldMinHeight) {
		return result;
	}
	const auto bottomWidth = cardRest.width()
		* (1. - (1. - st::walletCardFoldBottomScale) * fold);
	const auto topWidth = bottomWidth
		* (1. - (1. - st::walletCardFoldTopScale) * fold);
	const auto center = cardRest.left() + cardRest.width() / 2.;
	const auto rest = QRectF(cardRest);
	const auto restQuad = QPolygonF({
		rest.topLeft(),
		rest.topRight(),
		rest.bottomRight(),
		rest.bottomLeft(),
	});
	auto quad = QPolygonF({
		QPointF(center - topWidth / 2., result.topY),
		QPointF(center + topWidth / 2., result.topY),
		QPointF(center + bottomWidth / 2., result.bottomY),
		QPointF(center - bottomWidth / 2., result.bottomY),
	});
	auto transform = QTransform();
	if (!QTransform::quadToQuad(restQuad, quad, transform)) {
		return result;
	}
	result.quad = std::move(quad);
	result.transform = transform;
	result.valid = true;
	return result;
}

Content::Content(
	not_null<Ui::SeparatePanel*> panel,
	std::shared_ptr<Main::SessionShow> show)
: RpWidget(panel)
, _show(std::move(show))
, _panel(panel)
, _scroll(this, st::defaultScrollArea)
, _loadMoreCheck([this] { checkLoadMore(); }) {
	auto &wallet = _show->session().wallet();
	wallet.startPolling();

	setupContent();
	_scroll->show();
}

Content::~Content() {
	_show->session().wallet().stopPolling();
}

[[nodiscard]] bool HistoryShown(not_null<Main::Session*> session) {
	const auto wallet = &session->wallet();
	return !wallet->listsGated()
		&& (!wallet->historyVisibleEmpty()
			|| !wallet->listedSubmittedTransactions().empty()
			|| wallet->sendingTransaction(wallet->windowSend()));
}

[[nodiscard]] rpl::producer<bool> HistoryShownValue(
		not_null<Main::Session*> session) {
	const auto wallet = &session->wallet();
	return rpl::single(rpl::empty) | rpl::then(rpl::merge(
		wallet->historyUpdates(),
		wallet->sendStateValue() | rpl::to_empty,
		wallet->listsGatedValue() | rpl::to_empty
	)) | rpl::map([=] {
		return HistoryShown(session);
	}) | rpl::distinct_until_changed();
}

[[nodiscard]] rpl::producer<bool> CollectiblesShownValue(
		not_null<Main::Session*> session) {
	const auto wallet = &session->wallet();
	return rpl::single(rpl::empty) | rpl::then(rpl::merge(
		wallet->collectiblesUpdates(),
		wallet->listsGatedValue() | rpl::to_empty
	)) | rpl::map([=] {
		return !wallet->listsGated() && !wallet->collectibles().empty();
	}) | rpl::distinct_until_changed();
}

void PaintBottomRoundedPlate(
		QPainter &p,
		QRect rect,
		const style::color &bg) {
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(bg);
	p.drawRoundedRect(
		rect.marginsAdded({ 0, 2 * st::callRadius, 0, 0 }),
		st::callRadius,
		st::callRadius);
}

// The three faces the area under the card can show once the lists are known
// to be empty. They are derived from one producer and are mutually exclusive
// by construction, so no two of the wraps below can ever be open at once.
enum class EmptyFace {
	None,
	About,
	Unavailable,
	Unreachable,
};

struct ListRowKey {
	std::string operationId;
	QString id;

	friend bool operator==(
		const ListRowKey &,
		const ListRowKey &) = default;
};

struct ListedRow {
	ListRowKey key;
	not_null<Ui::RpWidget*> widget;
};

struct ListAnchorRow {
	ListRowKey key;
	int top = 0;
};

[[nodiscard]] std::vector<ListAnchorRow> CountListAnchor(
		const std::vector<ListedRow> &rows,
		not_null<QWidget*> column,
		int visibleTop) {
	if (rows.empty() || visibleTop <= 0) {
		return {};
	}
	auto tops = std::vector<int>();
	tops.reserve(rows.size());
	for (const auto &row : rows) {
		tops.push_back(Ui::MapFrom(column, row.widget, QPoint()).y());
	}
	if (visibleTop < tops.front()) {
		return {};
	}
	const auto count = int(rows.size());
	auto index = count - 1;
	for (auto i = 0; i != count; ++i) {
		if (tops[i] + rows[i].widget->height() > visibleTop) {
			index = i;
			break;
		}
	}
	auto result = std::vector<ListAnchorRow>();
	result.reserve(count);
	const auto add = [&](int i) {
		const auto &key = rows[i].key;
		if (!key.operationId.empty() || !key.id.isEmpty()) {
			result.push_back({ .key = key, .top = tops[i] });
		}
	};
	for (auto i = index; i != count; ++i) {
		add(i);
	}
	for (auto i = index - 1; i >= 0; --i) {
		add(i);
	}
	return result;
}

[[nodiscard]] int CountKeptScrollTop(
		const std::vector<ListAnchorRow> &anchor,
		const std::vector<ListedRow> &rows,
		not_null<QWidget*> column,
		int scrollTop,
		int reserve,
		int maxTop) {
	auto shift = 0;
	for (const auto &entry : anchor) {
		const auto found = ranges::find(rows, entry.key, &ListedRow::key);
		if (found != end(rows)) {
			shift = Ui::MapFrom(column, found->widget, QPoint()).y()
				- entry.top;
			break;
		}
	}
	const auto lower = anchor.empty() ? 0 : std::min(reserve, maxTop);
	return std::clamp(scrollTop + shift, lower, maxTop);
}

void Content::setupContent() {
	_container = _scroll->setOwnedWidget(
		object_ptr<Ui::RpWidget>(_scroll.data()));
	_column = Ui::CreateChild<Ui::PaddingWrap<Ui::VerticalLayout>>(
		_container,
		object_ptr<Ui::VerticalLayout>(_container),
		style::margins());
	_column->show();
	const auto column = _column->entity();
	const auto wallet = &_show->session().wallet();
	auto collectiblesShown = CollectiblesShownValue(&_show->session());

	setupPinned();
	setupBalance();
	setupTabs(rpl::duplicate(collectiblesShown));
	setupStrip();
	setupListsLoading();
	setupProtectRow();

	const auto media = std::make_shared<CollectibleMedia>(&_show->session());
	const auto wrap = column->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			column,
			object_ptr<Ui::VerticalLayout>(column)));
	const auto about = wrap->entity();
	Ui::AddSkip(about, st::walletAboutTopSkip);
	const auto addEntry = [&](
			rpl::producer<QString> title,
			rpl::producer<QString> text,
			const style::icon &icon) {
		const auto top = about->add(
			object_ptr<Ui::FlatLabel>(
				about,
				std::move(title),
				st::walletAboutTitleLabel),
			st::walletAboutPadding);
		Ui::AddSkip(about, st::walletAboutTitleSkip);
		about->add(
			object_ptr<Ui::FlatLabel>(
				about,
				std::move(text),
				st::walletAboutTextLabel),
			st::walletAboutPadding);
		const auto left = Ui::CreateChild<Ui::RpWidget>(about);
		left->paintRequest(
		) | rpl::on_next([=] {
			auto p = Painter(left);
			icon.paint(p, 0, 0, left->width());
		}, left->lifetime());
		left->resize(icon.size());
		top->geometryValue(
		) | rpl::on_next([=](const QRect &g) {
			left->moveToLeft(
				st::walletAboutIconLeft,
				g.top() + (top->height() - left->height()) / 2);
		}, left->lifetime());
	};
	addEntry(
		tr::lng_wallet_about_instant_title(),
		tr::lng_wallet_about_instant_text(),
		st::walletAboutInstantIcon);
	Ui::AddSkip(about, st::walletAboutRowSkip);
	addEntry(
		tr::lng_wallet_about_fees_title(),
		tr::lng_wallet_about_fees_text(
			lt_count,
			GaslessDailyTransfersValue(&_show->session()) | tr::to_count()),
		st::walletAboutFeesIcon);
	Ui::AddSkip(about, st::walletAboutRowSkip);
	addEntry(
		tr::lng_wallet_about_chain_title(),
		tr::lng_wallet_about_chain_text(),
		st::walletAboutChainIcon);
	Ui::AddSkip(about, st::walletAboutBottomSkip);

	auto emptyFace = rpl::combine(
		HistoryShownValue(&_show->session()),
		wallet->collectiblesTabValue(),
		wallet->listsEmptyStateValue(),
		wallet->presenceValue()
	) | rpl::map([](
			bool history,
			bool collectibles,
			ListsEmptyState lists,
			Presence presence) {
		return (!lists.confirmedEmpty || history || collectibles)
			? EmptyFace::None
			: (presence == Presence::Unavailable)
			? EmptyFace::Unavailable
			: (lists.unreachable || (presence == Presence::AddressUnreadable))
			? EmptyFace::Unreachable
			: EmptyFace::About;
	});
	wrap->toggleOn(rpl::duplicate(emptyFace) | rpl::map(
		rpl::mappers::_1 == EmptyFace::About));
	wrap->finishAnimating();

	const auto statementWrap = column->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			column,
			object_ptr<Ui::VerticalLayout>(column)));
	const auto statement = statementWrap->entity();
	Ui::AddSkip(statement, st::walletAboutTopSkip);
	statement->add(
		object_ptr<Ui::FlatLabel>(
			statement,
			rpl::combine(
				tr::lng_wallet_unavailable(),
				tr::lng_wallet_state_error(),
				rpl::duplicate(emptyFace)
			) | rpl::map([](
					const QString &unavailable,
					const QString &error,
					EmptyFace face) {
				return (face == EmptyFace::Unavailable) ? unavailable : error;
			}),
			st::walletAboutTextLabel),
		st::boxRowPadding,
		style::al_top);
	Ui::AddSkip(statement, st::walletAboutBottomSkip);
	statementWrap->toggleOn(std::move(emptyFace) | rpl::map(
		(rpl::mappers::_1 == EmptyFace::Unavailable)
		|| (rpl::mappers::_1 == EmptyFace::Unreachable)));
	statementWrap->finishAnimating();

	const auto rowsTopSkip = column->add(Ui::CreateSlideSkipWidget(
		column,
		st::walletRowsTopSkip));
	rowsTopSkip->toggleOn(rpl::combine(
		std::move(collectiblesShown),
		wallet->collectiblesTabValue(),
		HistoryShownValue(&_show->session())
	) | rpl::map([](bool available, bool collectibles, bool history) {
		return history || (available && collectibles);
	}));
	rowsTopSkip->finishAnimating();

	const auto listWrap = column->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			column,
			object_ptr<Ui::VerticalLayout>(column)));
	const auto rows = listWrap->entity();
	const auto list = rows->add(object_ptr<Ui::VerticalLayout>(rows));
	struct ListPlace {
		std::vector<ListedRow> rows;
		int lastTop = 0;
		bool rebuilding = false;
	};
	const auto place = lifetime().make_state<ListPlace>();
	place->lastTop = _scroll->scrollTop();
	const auto listedRows = [=] {
		auto result = std::vector<ListedRow>();
		result.reserve(place->rows.size() + 1);
		if (_sendingRow) {
			result.push_back({
				.key = { .operationId = _sendingRow->operationId },
				.widget = _sendingRow->slot,
			});
		}
		result.insert(end(result), begin(place->rows), end(place->rows));
		return result;
	};
	const auto fitContainer = [=] {
		const auto height = _column->height();
		_container->resize(
			_container->width(),
			(place->rebuilding
				? std::max(height, _container->height())
				: height));
	};
	const auto releaseSending = [=] {
		if (_sendingRow) {
			delete _sendingRow->slot;
			_sendingRow = nullptr;
		}
	};
	const auto rebuild = list->lifetime().make_state<Fn<void()>>();
	const auto holdSending = [=](
			const std::string &operationId,
			const TransferItem &item,
			bool sending) {
		auto created = false;
		if (!_sendingRow || _sendingRow->operationId != operationId) {
			releaseSending();
			_sendingRow = std::make_unique<SendingRow>(SendingRow{
				.operationId = operationId,
				.slot = rows->insert(
					0,
					object_ptr<Ui::VerticalLayout>(rows)),
				.revealPending = true,
			});
			created = true;
		}
		auto content = HistoryRowContent();
		if (sending) {
			auto shown = item;
			shown.status = TransferItem::Status::Success;
			content = RowContentFromItem(shown, &_show->session());
			content.date = tr::lng_wallet_row_sending(tr::now);
		} else {
			content = RowContentFromItem(item, &_show->session());
		}
		_sendingRow->item = item;
		const auto slot = _sendingRow->slot;
		const auto look = _sendingRow->look;
		const auto settle = look
			&& !sending
			&& (look->settling()
				|| (!look->settled()
					&& !anim::Disabled()
					&& look->surfaceShown()
					&& look->inView()
					&& (content.itemAmount
						== _sendingRow->content.itemAmount)));
		if (look && sending) {
			if (content != _sendingRow->content) {
				look->setContent(content);
				_sendingRow->content = std::move(content);
			}
			return created;
		} else if (settle) {
			if (!look->settling() || content != _sendingRow->content) {
				// Only the diamond's landing bursts; a collectible lands none.
				const auto burst = !content.itemAmount
					&& (item.status == TransferItem::Status::Success);
				look->settle(
					content,
					burst,
					crl::guard(list, [=] { (*rebuild)(); }));
				_sendingRow->content = std::move(content);
			}
			return created;
		} else if (!slot->count()
			|| _sendingRow->look
			|| content != _sendingRow->content) {
			slot->clear();
			_sendingRow->look = nullptr;
			const auto click = [=] {
				if (_sendingRow) {
					ShowWalletTransactionBox(_show, _sendingRow->item, media);
				}
			};
			if (sending) {
				_sendingRow->look = AddSendingHistoryRow(
					slot,
					column,
					listWrap,
					content,
					media,
					click);
			} else {
				AddHistoryRow(slot, content, click, media);
			}
			_sendingRow->content = std::move(content);
		}
		return created;
	};
	const auto rebuildList = [=] {
		const auto scrollTop = _scroll->scrollTop();
		const auto anchor = listWrap->toggled()
			? CountListAnchor(listedRows(), column, scrollTop - _reserve)
			: std::vector<ListAnchorRow>();
		place->rows.clear();
		// WHY: clear() collapses the column and the scroll area clamps to it
		// at once, so the geometry readers wait for the rebuilt rows, which
		// then keep their place on screen in one move.
		place->rebuilding = true;
		list->clear();
		const auto &history = wallet->history();
		auto submitted = wallet->listedSubmittedTransactions();
		// A row sits by date, so old failures stop covering fresh history.
		ranges::stable_sort(submitted, ranges::greater(), [](const auto &entry) {
			return entry.item.date.value_or(kUndatedRowDate);
		});
		const auto addItem = [=](const TransferItem &item, ListRowKey key) {
			const auto content = RowContentFromItem(item, &_show->session());
			place->rows.push_back({
				.key = std::move(key),
				.widget = AddHistoryRow(list, content, [=] {
					ShowWalletTransactionBox(_show, item, media);
				}, media),
			});
		};
		auto created = false;
		if (HistoryShown(&_show->session())) {
			const auto &op = wallet->windowSend();
			const auto sending = wallet->sendingTransaction(op);
			const auto settled = (op.empty() || sending)
				? end(submitted)
				: ranges::find(
					submitted,
					op,
					&ListedSubmittedTransfer::operationId);
			const auto newest = [&] {
				auto result = TimeId();
				for (const auto &item : history) {
					if (item.date && !wallet->historyItemHidden(item)) {
						result = std::max(result, *item.date);
					}
				}
				for (auto i = begin(submitted); i != end(submitted); ++i) {
					if (i != settled) {
						result = std::max(
							result,
							i->item.date.value_or(kUndatedRowDate));
					}
				}
				return result;
			};
			const auto settling = !sending
				&& _sendingRow
				&& _sendingRow->operationId == op
				&& _sendingRow->look
				&& _sendingRow->look->settling();
			const auto kept = !settling
				? std::optional<TransferItem>()
				: (settled != end(submitted))
				? std::make_optional(settled->item)
				: wallet->submittedTransaction(op);
			const auto held = sending
				|| kept
				|| (settled != end(submitted)
					&& _sendingRow
					&& _sendingRow->operationId == op
					&& (settled->item.date.value_or(kUndatedRowDate)
						>= newest()));
			if (!held) {
				releaseSending();
			} else if (sending) {
				created = holdSending(op, *sending, true);
			} else {
				created = holdSending(
					op,
					kept ? *kept : settled->item,
					false);
			}
			const auto skipId = !held
				? QString()
				: sending
				? sending->id
				: kept
				? kept->id
				: QString();
			auto next = begin(submitted);
			const auto addNext = [&] {
				const auto &entry = *(next++);
				if (!held || entry.operationId != op) {
					addItem(entry.item, { .operationId = entry.operationId });
				}
			};
			const auto addNewerThan = [&](TimeId date) {
				while (next != end(submitted)
					&& next->item.date.value_or(kUndatedRowDate) >= date) {
					addNext();
				}
			};
			for (const auto &item : history) {
				if (item.date) {
					addNewerThan(*item.date);
				}
				if (!wallet->historyItemHidden(item)
					&& (skipId.isEmpty() || item.id != skipId)) {
					addItem(item, { .id = item.id });
				}
			}
			while (next != end(submitted)) {
				addNext();
			}
			Ui::AddSkip(list, st::walletRowsTopSkip);
		} else {
			releaseSending();
		}
		if (const auto width = rows->width()) {
			rows->resizeToWidth(width);
		}
		if (width() && height()) {
			const auto columnHeight = column->height();
			const auto regions = countRegions(columnHeight);
			_scroll->scrollToY(CountKeptScrollTop(
				anchor,
				listedRows(),
				column,
				scrollTop,
				_reserve,
				std::max(
					columnHeight + regions.reserve - regions.scrollHeight,
					0)));
		}
		place->rebuilding = false;
		fitContainer();
		updateRegions();
		if (created) {
			Ui::PostponeCall(this, [=] { revealSendingRow(); });
		}
	};
	*rebuild = rebuildList;
	rpl::merge(
		wallet->historyUpdates(),
		wallet->collectiblesUpdates(),
		wallet->sendStateValue() | rpl::to_empty,
		wallet->listsGatedValue() | rpl::to_empty
	) | rpl::on_next(rebuildList, list->lifetime());
	listWrap->toggleOn(TransactionsShownValue(&_show->session()));
	listWrap->finishAnimating();

	const auto collectiblesWrap = column->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			column,
			object_ptr<Ui::VerticalLayout>(column)));
	AddCollectiblesList(collectiblesWrap->entity(), _show, media);
	collectiblesWrap->toggleOn(wallet->collectiblesTabValue());
	collectiblesWrap->finishAnimating();

	_scroll->scrolls(
	) | rpl::on_next([=] {
		// A reader who moved the list down is asking for more of it, so
		// the bound the session spends on hidden transaction pages is
		// re-armed here, and only for that. Every other way this fires is
		// a clamp nobody made - a section sliding shut, a
		// resize growing the viewport - and a clamp can only lower the
		// position, so requiring it to grow rejects all of them. The last
		// seen position is kept beside the handler and not inside it,
		// because rpl invokes a copy of the handler on every emission and
		// a value captured in it would never carry to the next one.
		const auto top = _scroll->scrollTop();
		// A rebuild's anchored move grows it too, and is no reader's scroll.
		const auto moved = !place->rebuilding && (top > place->lastTop);
		place->lastTop = top;
		if (moved) {
			wallet->resetHiddenHistoryPages();
		}
		_loadMoreCheck.call();
	}, lifetime());

	_scroll->scrollTopValue(
	) | rpl::on_next([=](int) {
		if (place->rebuilding) {
			return;
		}
		updatePinned();
		updateVisibleArea();
	}, lifetime());

	_container->widthValue(
	) | rpl::on_next([=](int width) {
		_column->resizeToWidth(width);
	}, _column->lifetime());
	_column->heightValue(
	) | rpl::on_next([=] {
		fitContainer();
	}, _column->lifetime());

	_pinnedInner->heightValue(
	) | rpl::on_next([=] {
		updateRegions();
	}, lifetime());

	_column->entity()->heightValue(
	) | rpl::on_next([=] {
		if (!place->rebuilding) {
			updateRegions();
		}
		_loadMoreCheck.call();
	}, lifetime());

	_pinnedBackground->raise();
	_card->raise();
	_pinned->raise();
	_cardButton->raise();
	_tabsShadow->raise();
	_headerShadow->raise();
	_stripShadow->raise();
	_strip->raise();

	const auto local = &_show->session().local();
	if (!local->readPref<bool>(kIntroTooltipShownPref)) {
		local->writePref<bool>(kIntroTooltipShownPref, true);
		SetupIntroTooltip(this, _card, [=] {
			return (foldProgress() > 0.)
				? QRect()
				: _ink->markRect(cardRest());
		}, _pinned->heightValue() | rpl::to_empty);
	}
}

void Content::setupPinned() {
	_pinnedBackground = Ui::CreateChild<Ui::RpWidget>(this);
	_pinnedBackground->setAttribute(Qt::WA_TransparentForMouseEvents);
	_pinnedBackground->setGeometry(QRect());
	_pinnedBackground->show();

	_pinned = Ui::CreateChild<Ui::RpWidget>(this);
	_pinned->show();
	_pinnedInner = Ui::CreateChild<Ui::VerticalLayout>(_pinned);
	_pinnedInner->show();

	setupInfoIsland();

	Ui::AddSkip(_pinnedInner, st::walletCardTopSkip);
	_cardPlaceholder = _pinnedInner->add(
		object_ptr<Ui::FixedHeightWidget>(
			_pinnedInner,
			st::walletCardHeight),
		st::walletCardMargin);
	_card = Ui::CreateChild<Card>(
		this,
		_show,
		CardNameValue(&_show->session()));
	_card->setGeometry(Ui::MapFrom(
		this,
		_cardPlaceholder,
		_cardPlaceholder->rect()));
	_card->setAttribute(Qt::WA_TransparentForMouseEvents);
	_card->show();
	_card->followCursor();
	_cardButton = Ui::CreateChild<Ui::AbstractButton>(this);
	_cardButton->setClickedCallback([=] {
		ShowWalletReceiveBox(&_show->session(), _show);
	});
	_cardButton->show();

	const auto buttons = _pinnedInner->add(
		object_ptr<Ui::FixedHeightWidget>(
			_pinnedInner,
			st::walletSendButton.height),
		st::walletSendButtonMargin,
		style::al_justify);
	const auto addPill = [&](
			rpl::producer<QString> text,
			Fn<void()> callback) {
		const auto button = Ui::CreateChild<Ui::RoundButton>(
			buttons,
			std::move(text),
			st::walletSendButton);
		button->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
		button->setClickedCallback(std::move(callback));
		button->show();
		return button;
	};
	const auto addFunds = addPill(tr::lng_wallet_add_funds(), [=] {
		ShowWalletReceiveBox(&_show->session(), _show);
	});
	const auto send = addPill(tr::lng_send_button(), [show = _show] {
		WhenWalletReady(show, [=] {
			show->showBox(Box(
				WalletSendRecipientBox,
				show,
				QString(),
				nullptr));
		});
	});
	buttons->widthValue(
	) | rpl::on_next([=](int width) {
		const auto single = (width - st::walletButtonsSkip) / 2;
		addFunds->setFullWidth(single);
		addFunds->moveToLeft(0, 0, width);
		const auto left = single + st::walletButtonsSkip;
		send->setFullWidth(width - left);
		send->moveToLeft(left, 0, width);
	}, buttons->lifetime());

	_show->session().wallet().presenceValue(
	) | rpl::on_next([=](Presence presence) {
		const auto ready = (presence == Presence::Ready);
		for (const auto button : { addFunds, send }) {
			SetButtonDisabledLook(button, !ready);
		}
	}, buttons->lifetime());

	Ui::AddSkip(_pinnedInner, st::walletHeaderBottomSkip);

	_headerShadow = Ui::CreateChild<Ui::PlainShadow>(this);

	_pinnedBackground->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(_pinnedBackground);
		const auto height = _pinnedBackground->height();
		const auto tabsTop = height - pinnedMin();
		p.fillRect(
			0,
			0,
			_pinnedBackground->width(),
			tabsTop,
			st::windowBgOver);
		if (tabsTop < height) {
			p.fillRect(
				0,
				tabsTop,
				_pinnedBackground->width(),
				height - tabsTop,
				st::windowBg);
		}
	}, _pinnedBackground->lifetime());

	const auto forwardWheel = [=](not_null<QEvent*> e) {
		if (e->type() != QEvent::Wheel) {
			return base::EventFilterResult::Continue;
		}
		_scroll->viewportEvent(e);
		return base::EventFilterResult::Cancel;
	};
	base::install_event_filter(_pinned, forwardWheel);
	base::install_event_filter(_cardButton, forwardWheel);
}

[[nodiscard]] TextWithEntities IslandAmount(
		const TextWithEntities &mark,
		CreditsAmount amount) {
	auto result = mark;
	result.append(QChar(' '));
	result.append(Ui::Text::Colorized(
		Lang::FormatCreditsAmountToShort(amount).string));
	return result;
}

void AddIslandRowLabel(
		not_null<InfoIslandEntry*> button,
		rpl::producer<TextWithEntities> text,
		Ui::Text::MarkedContext context) {
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		button.get(),
		std::move(text),
		st::walletIslandRowLabel,
		st::defaultPopupMenu,
		std::move(context));
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	label->setTryMakeSimilarLines(true);
	const auto updateLabelGeometry = [=] {
		const auto &padding = st::walletIslandRow.padding;
		const auto available = button->width()
			- padding.left()
			- st::walletIslandLabelRightSkip;
		if (available <= 0) {
			return;
		}
		label->resizeToWidth(available);
		button->setMinimalHeight(label->height()
			+ padding.top()
			+ padding.bottom());
		label->moveToLeft(
			padding.left(),
			(button->height() - label->height()) / 2,
			button->width());
	};
	button->widthValue(
	) | rpl::on_next(updateLabelGeometry, button->lifetime());
	label->heightValue(
	) | rpl::on_next(updateLabelGeometry, label->lifetime());
}

void Content::setupInfoIsland() {
	auto owned = object_ptr<InfoIsland>(_pinnedInner);
	const auto island = owned.data();
	const auto wrap = _pinnedInner->add(
		object_ptr<Ui::SlideWrap<InfoIsland>>(
			_pinnedInner,
			std::move(owned),
			style::margins(0, st::walletCardTopSkip, 0, 0)));
	setupCustodyEntry(island);
	setupWaltEntry(island);
	setupEarningsEntry(island);
	setupOldWalletEntry(island);
	wrap->toggleOn(island->anyShownValue(), anim::type::normal);
}

void Content::setupWaltEntry(not_null<InfoIsland*> island) {
	const auto session = &_show->session();
	const auto wrap = island->add(
		object_ptr<InfoIslandEntry>(
			island,
			tr::lng_wallet_walt_existing(),
			st::walletIslandRow));
	const auto button = wrap->entity();
	AddRowChevron(button);
	button->setClickedCallback([=] {
		const auto url = session->wallet().existingWaltBalanceUrl();
		if (!url.isEmpty()) {
			OpenWalletUrl(session, _show, url);
		}
	});
	wrap->toggleOn(session->wallet().existingWaltBalanceUrlValue(
	) | rpl::map([](const QString &url) {
		return !url.isEmpty();
	}), anim::type::normal);
}

void Content::setupEarningsEntry(not_null<InfoIsland*> island) {
	const auto session = &_show->session();
	const auto wrap = island->add(
		object_ptr<InfoIslandEntry>(island, nullptr, st::walletIslandRow));
	const auto button = wrap->entity();
	AddRowChevron(button);

	auto helper = Ui::Text::CustomEmojiHelper();
	const auto mark = GramMark(helper, st::walletIslandRowLabel.style.font);
	AddIslandRowLabel(
		button,
		tr::lng_wallet_earnings_existing(
			lt_amount,
			session->credits().tonBalanceValue(
			) | rpl::map([=](CreditsAmount value) {
				return IslandAmount(mark, value);
			}),
			tr::marked),
		helper.context());

	button->setClickedCallback([=] {
		if (const auto window = session->tryResolveWindow()) {
			window->showSettings(Settings::CurrencyId());
			window->window().activate();
		}
	});

	session->credits().tonLoad();
	wrap->toggleOn(session->credits().tonBalanceValue(
	) | rpl::map([](CreditsAmount value) {
		return !value.empty();
	}), anim::type::normal);
}

void Content::setupProtectRow() {
	const auto session = &_show->session();
	const auto column = _column->entity();
	const auto wrap = column->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			column,
			object_ptr<Ui::VerticalLayout>(column)));
	const auto inner = wrap->entity();
	Ui::AddSkip(inner, st::walletProtectRowSkip);
	const auto button = Settings::AddButtonWithIcon(
		inner,
		tr::lng_wallet_protect_account(),
		st::walletProtectRow,
		{ .icon = &st::walletProtectRowIcon });
	AddRowChevron(button);
	button->setClickedCallback([=] {
		_show->showBox(Box(WalletCloudPasswordIntroBox, _show));
	});

	auto &cloud = session->api().cloudPassword();
	cloud.reload();
	auto off = rpl::single(false) | rpl::then(cloud.state(
	) | rpl::map([](const Core::CloudPasswordState &state) {
		return !state.hasPassword && state.unconfirmedPattern.isEmpty();
	}));
	auto nonEmpty = rpl::combine(
		session->wallet().balanceNanoValue(),
		HistoryShownValue(session)
	) | rpl::map([](int64 balance, bool history) {
		return (balance != 0) || history;
	});
	wrap->toggleOn(rpl::combine(
		std::move(off),
		std::move(nonEmpty)
	) | rpl::map([](bool off, bool nonEmpty) {
		return off && nonEmpty;
	}) | rpl::distinct_until_changed(), anim::type::instant);
}

void Content::setupBalance() {
	_ink = std::make_unique<BalanceInk>();

	_pinnedBalance = Ui::CreateChild<Ui::RpWidget>(_pinned);
	_pinnedBalance->setAttribute(Qt::WA_TransparentForMouseEvents);
	_pinnedBalance->show();
	_pinnedBalance->raise();
	_pinnedBalance->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		const auto fold = cardFold();
		const auto ink = _ink->boundingRect(fold);
		if (!clip.intersects(ink)) {
			return;
		}
		auto p = QPainter(_pinnedBalance);
		auto hq = PainterHighQualityEnabler(p);
		_ink->paint(p, fold, cardOutline(), clip);
	}, _pinnedBalance->lifetime());

	_titleBalance.reset(Ui::CreateChild<Ui::RpWidget>(window()));
	_titleBalance->setAttribute(Qt::WA_TransparentForMouseEvents);
	_titleBalance->show();
	_titleBalance->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(_titleBalance.get());
		paintTitle(p, cardFold().fold);
		auto hq = PainterHighQualityEnabler(p);
		p.translate(0, st::separatePanelTitleHeight);
		_ink->paint(
			p,
			cardFold(),
			cardOutline(),
			_titleBalance->rect().translated(
				0,
				-st::separatePanelTitleHeight));
	}, _titleBalance->lifetime());

	const auto repaintBalance = [=] {
		_pinnedBalance->update();
		_titleBalance->update();
		_paintedInk = _ink->boundingRect(cardFold());
	};

	widthValue(
	) | rpl::on_next([=](int width) {
		const auto scope = WindowPaletteScope(this);
		_ink->setOuterWidth(width);
		repaintBalance();
	}, lifetime());

	SetupCardBalance(
		_ink.get(),
		&_show->session(),
		repaintBalance,
		this);

	style::PaletteChanged(
	) | rpl::on_next([=] {
		const auto scope = WindowPaletteScope(this);
		_ink->refresh();
		_card->invalidateCache();
		_card->update();
		_pinnedBackground->update();
		repaintBalance();
	}, lifetime());

	tr::lng_wallet_menu(
	) | rpl::on_next([=](const QString &title) {
		_title.setText(
			st::separatePanelTitle.style,
			title,
			kPlainTextOptions);
		const auto scope = WindowPaletteScope(this);
		_ink->refresh();
		repaintBalance();
	}, lifetime());

	SetupCardMark(_ink.get(), this, std::make_shared<bool>(), [=] {
		const auto mark = _ink->markPaintRect(cardFold());
		_pinnedBalance->update(mark);
		_titleBalance->update(
			mark.translated(0, st::separatePanelTitleHeight));
	});
}

const CardFold &Content::cardFold() const {
	return _card->fold();
}

QRegion Content::cardOutline() const {
	if (_card->isHidden()) {
		return {};
	}
	return QRegion(_card->paintedQuad().toPolygon());
}

void Content::paintTitle(QPainter &p, float64 fold) {
	const auto &st = st::separatePanelTitle;
	const auto left = st::separatePanelTitleLeft;
	const auto top = st::separatePanelTitleTop;
	const auto textWidth = std::min(
		(width()
			- left
			- st::separatePanelClose.width
			- st::separatePanelMenu.width),
		_title.maxWidth());
	const auto fullHeight = _title.countHeight(textWidth);
	const auto titleHeight = std::min(fullHeight, st.maxHeight);
	const auto elided = (st.maxHeight < fullHeight)
		|| (textWidth < _title.maxWidth());
	const auto lineHeight = std::max(
		st.style.lineHeight,
		st.style.font->height);
	const auto box = QRect(left, top, textWidth, titleHeight);
	p.save();
	p.setClipRect(box);
	if (fold > 0.) {
		const auto scale = 1. - (1. - st::walletTitleFoldScale) * fold;
		const auto center = QPointF(left, top + titleHeight / 2.);
		p.setOpacity(1. - fold);
		p.translate(center);
		p.scale(scale, scale);
		p.translate(-center);
	}
	p.setPen(_panel->titleOverridePalette()->windowFg()->c);
	_title.draw(p, {
		.position = { left, top },
		.availableWidth = textWidth,
		.align = st.align,
		.clip = box,
		.palette = &st.palette,
		.elisionHeight = (elided ? std::max(st.maxHeight, lineHeight) : 0),
		.elisionLines = 0,
	});
	p.restore();
}

void Content::setupTabs(rpl::producer<bool> collectiblesShown) {
	_tabsWrap = _pinnedInner->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsSlider>>(
			_pinnedInner,
			object_ptr<Ui::SettingsSlider>(
				_pinnedInner,
				st::walletTabsSlider)));
	const auto tabs = _tabsWrap->entity();
	tabs->setSections({
		tr::lng_wallet_rows_title(tr::now),
		tr::lng_wallet_rows_collectibles(tr::now),
	});
	tabs->fitWidthToSections();
	tabs->setNaturalWidth(tabs->width());
	_tabsShadow = Ui::CreateChild<Ui::PlainShadow>(this);

	// Without collectibles the first tab stands alone, scrolling with the list.
	const auto column = _column->entity();
	const auto title = column->insert(
		0,
		object_ptr<Ui::SlideWrap<Ui::SettingsSlider>>(
			column,
			object_ptr<Ui::SettingsSlider>(column, st::walletRowsTitle)));
	const auto label = title->entity();
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	tr::lng_wallet_rows_title(
	) | rpl::on_next([=](const QString &text) {
		label->setSections({ text });
		label->fitWidthToSections();
		label->setNaturalWidth(label->width());
	}, label->lifetime());

	const auto wallet = &_show->session().wallet();
	tabs->setActiveSectionFast(wallet->collectiblesTab() ? 1 : 0);
	tabs->sectionActivated(
	) | rpl::on_next([=](int index) {
		wallet->setCollectiblesTab(index == 1);
		_scroll->scrollToY(0);
	}, tabs->lifetime());

	wallet->collectiblesTabValue(
	) | rpl::on_next([=](bool collectibles) {
		const auto index = collectibles ? 1 : 0;
		if (tabs->activeSection() != index) {
			tabs->setActiveSectionFast(index);
		}
	}, tabs->lifetime());

	rpl::combine(
		std::move(collectiblesShown),
		TransactionsShownValue(&_show->session())
	) | rpl::map([](bool shown, bool transactions) {
		return std::make_pair(shown, transactions && !shown);
	}) | rpl::distinct_until_changed(
	) | rpl::on_next([=](std::pair<bool, bool> sections) {
		// WHY: the section that shows goes in before the one that hides, so
		// no height in between lets the scroll area clamp a scrolled reader.
		if (sections.second) {
			title->toggle(true, anim::type::instant);
		}
		_tabsShown = sections.first;
		_tabsWrap->toggle(sections.first, anim::type::instant);
		_tabsShadow->setVisible(sections.first);
		if (!sections.second) {
			title->toggle(false, anim::type::instant);
		}
		updateRegions();
	}, lifetime());
}

void Content::setupStrip() {
	_stripShadow = Ui::CreateChild<Ui::PlainShadow>(this);
	_strip = Ui::CreateChild<Ui::RpWidget>(this);
	_strip->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(_strip);
		PaintBottomRoundedPlate(p, _strip->rect(), st::windowBgOver);
	}, _strip->lifetime());

	const auto wallet = &_show->session().wallet();
	auto minAmount = rpl::single(rpl::empty) | rpl::then(
		wallet->historyUpdates()
	) | rpl::map([=] {
		return wallet->transferMinNanos();
	}) | rpl::distinct_until_changed(
	) | rpl::map([](int64 nanos) {
		return Ui::FormatTonAmount(nanos).full;
	});
	const auto hint = Ui::CreateChild<Ui::FlatLabel>(
		_strip,
		tr::lng_wallet_rows_hidden_below(lt_amount, std::move(minAmount)),
		st::defaultSubTextLabel);
	hint->setAttribute(Qt::WA_TransparentForMouseEvents);
	hint->show();
	rpl::combine(
		_strip->sizeValue(),
		hint->sizeValue()
	) | rpl::on_next([=](QSize size, QSize) {
		hint->moveToLeft(
			st::boxRowPadding.left(),
			(size.height() - hint->height()) / 2,
			size.width());
	}, hint->lifetime());

	TransactionsShownValue(
		&_show->session()
	) | rpl::on_next([=](bool shown) {
		_stripShown = shown;
		_strip->setVisible(shown);
		_stripShadow->setVisible(shown);
		updateRegions();
	}, lifetime());
}

void Content::setupListsLoading() {
	_listsLoading = Ui::CreateChild<Ui::RpWidget>(this);
	_listsLoading->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto &loading = st::walletListsLoading;
	const auto side = loading.size.height() + 2 * loading.thickness;
	const auto indicator = Info::Statistics::InfiniteRadialAnimationWidget(
		_listsLoading,
		side,
		&loading);
	indicator->setAttribute(Qt::WA_TransparentForMouseEvents);
	Info::Statistics::AddChildToWidgetCenter(_listsLoading, indicator);

	const auto addCaption = [=](rpl::producer<QString> text) {
		const auto caption = Ui::CreateChild<Ui::FlatLabel>(
			_listsLoading,
			std::move(text),
			st::walletAboutTextLabel);
		caption->setAttribute(Qt::WA_TransparentForMouseEvents);
		_listsLoading->sizeValue(
		) | rpl::on_next([=](QSize size) {
			caption->resizeToNaturalWidth(size.width());
			caption->moveToLeft(
				(size.width() - caption->width()) / 2,
				((size.height() + side) / 2) + st::walletAboutTitleSkip,
				size.width());
		}, caption->lifetime());
		return caption;
	};
	const auto caption = addCaption(tr::lng_wallet_provisioning());
	_show->session().wallet().presenceValue(
	) | rpl::map(
		rpl::mappers::_1 == Presence::Provisioning
	) | rpl::on_next([=](bool provisioning) {
		caption->setVisible(provisioning);
	}, caption->lifetime());
	const auto walkingCaption = addCaption(
		tr::lng_wallet_history_searching());

	// The gate is not the only state with nothing to paint. A feed whose
	// loaded pages are all hidden while the server still offers a cursor is
	// walking towards a row it can show, and this indicator - the one an
	// unsettled feed already renders, with its caption bound to
	// Provisioning and so hidden here - is the only face that says so. It
	// resolves into rows or into the About face when the cursor exhausts.
	// A visible row or a pending send makes it a lie, and so does the
	// Collectibles tab, whose own list this region does not describe.
	rpl::combine(
		_show->session().wallet().listsGatedValue(),
		_show->session().wallet().historyLoadingMoreValue(),
		_show->session().wallet().collectiblesTabValue(),
		HistoryShownValue(&_show->session())
	) | rpl::map([](
			bool gated,
			bool loadingMore,
			bool collectiblesTab,
			bool historyShown) {
		// The gate and the walk are the region's two reasons to show, and
		// only the walk is the one the second caption speaks for, so both
		// bits leave here together: written by one handler, the caption
		// can neither outlive the region nor appear without it.
		// updateListsGate() makes Provisioning - the state the first
		// caption is bound to - one of the gate's own disjuncts, so the
		// two captions are mutually exclusive by the same expression.
		return std::make_pair(
			gated,
			!gated && loadingMore && !collectiblesTab && !historyShown);
	}) | rpl::distinct_until_changed(
	) | rpl::on_next([=](std::pair<bool, bool> face) {
		const auto shown = face.first || face.second;
		walkingCaption->setVisible(face.second);
		indicator->setVisible(shown);
		_listsLoading->setVisible(shown);
		updateRegions();
	}, lifetime());
}

void Content::setupCustodyEntry(not_null<InfoIsland*> island) {
	const auto wrap = island->add(
		object_ptr<InfoIslandEntry>(island, nullptr, st::walletIslandRow));
	wrap->toggle(false, anim::type::instant);

	const auto button = wrap->entity();
	_custodyBarLabel = Ui::CreateChild<Ui::FlatLabel>(
		button,
		st::walletInfoBarLabel);
	_custodyBarLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
	_custodyBarLabel->setTryMakeSimilarLines(true);
	const auto updateLabelGeometry = [=] {
		const auto available = button->width()
			- 2 * st::walletInfoBarLabelSkip;
		if (available <= 0) {
			return;
		}
		_custodyBarLabel->resizeToWidth(
			std::min(_custodyBarLabel->textMaxWidth(), available));
		const auto &padding = st::walletIslandRow.padding;
		button->setMinimalHeight(_custodyBarLabel->height()
			+ padding.top()
			+ padding.bottom());
		_custodyBarLabel->moveToLeft(
			(button->width() - _custodyBarLabel->width()) / 2,
			(button->height() - _custodyBarLabel->height()) / 2,
			button->width());
	};
	button->widthValue(
	) | rpl::on_next(updateLabelGeometry, button->lifetime());

	button->setClickedCallback([=] {
		_show->showBox(Box(
			WalletImportBox,
			_show,
			WalletImportMode::Restore,
			nullptr,
			nullptr,
			nullptr));
	});

	// The old wallet entry takes a conflict, and resolving it comes first.
	auto &wallet = _show->session().wallet();
	rpl::combine(
		wallet.deviceCustodyStateValue(),
		wallet.presenceValue()
	) | rpl::map([](DeviceCustodyState state, Presence presence) {
		return (presence == Presence::Ready)
			&& !state.conflict
			&& (state.mode == DeviceMode::ReadOnlyNotRestorable);
	}) | rpl::distinct_until_changed(
	) | rpl::on_next([=](bool shown) {
		if (shown) {
			_custodyBarLabel->setText(tr::lng_wallet_readonly_bar(tr::now));
			updateLabelGeometry();
		}
		wrap->toggle(shown, anim::type::normal);
	}, lifetime());
}

void Content::setupOldWalletEntry(not_null<InfoIsland*> island) {
	const auto wallet = &_show->session().wallet();
	const auto wrap = island->add(
		object_ptr<InfoIslandEntry>(island, nullptr, st::walletIslandRow));
	wrap->toggle(false, anim::type::instant);
	const auto button = wrap->entity();
	AddRowChevron(button);

	auto helper = Ui::Text::CustomEmojiHelper();
	const auto mark = GramMark(helper, st::walletIslandRowLabel.style.font);
	AddIslandRowLabel(
		button,
		wallet->parkedBalanceNanoValue(
		) | rpl::map([=](std::optional<int64> nano) {
			return (nano && *nano > 0)
				? tr::lng_wallet_conflict_existing(
					lt_amount,
					rpl::single(IslandAmount(mark, CreditsAmount(
						*nano / Ui::kNanosInOne,
						*nano % Ui::kNanosInOne,
						CreditsType::Ton))),
					tr::marked)
				: tr::lng_wallet_conflict_bar(tr::marked);
		}) | rpl::flatten_latest(),
		helper.context());

	button->setClickedCallback([=] {
		_show->showBox(Box(WalletConflictBox, _show, nullptr));
	});

	wrap->toggleOn(rpl::combine(
		wallet->deviceCustodyStateValue(),
		wallet->presenceValue()
	) | rpl::map([](DeviceCustodyState state, Presence presence) {
		return (presence == Presence::Ready) && state.conflict;
	}), anim::type::normal);
}

int Content::pinnedMax() const {
	return _pinnedInner->height();
}

int Content::pinnedMin() const {
	return _tabsShown ? st::walletTabsSlider.height : 0;
}

QRect Content::cardRest() const {
	return QRect(
		_cardPlaceholder->x(),
		_cardPlaceholder->y(),
		_cardPlaceholder->width(),
		st::walletCardHeight);
}

Content::Regions Content::countRegions(int columnHeight) const {
	const auto max = pinnedMax();
	const auto min = pinnedMin();
	const auto stripHeight = _stripShown
		? (st::walletRowsHintHeight + st::lineWidth)
		: 0;
	const auto open = height() - max - stripHeight;
	const auto reserve = (columnHeight > open) ? (max - min) : 0;
	const auto scrollTop = max - reserve;
	return {
		.reserve = reserve,
		.scrollTop = scrollTop,
		.scrollHeight = std::max(0, height() - scrollTop - stripHeight),
	};
}

float64 Content::foldProgress() const {
	// The card's rest bottom, measured down from the pinned top, is the
	// placeholder's own bottom inside _pinnedInner, because that layout's
	// top sits at the pinned top with nothing scrolled away. So this is
	// the scroll distance that carries the card's bottom edge up to the
	// title bar, and no Content-space rest rect has to be re-derived.
	const auto travel = _cardPlaceholder->y() + _cardPlaceholder->height();
	const auto scrolled = std::clamp(_scroll->scrollTop(), 0, _reserve);
	return std::clamp(scrolled / float64(travel), 0., 1.);
}

void Content::updateRegions() {
	if (!width() || !height()) {
		return;
	}
	_container->resize(width(), _container->height());
	if (_pinnedInner->widthNoMargins() != width()) {
		_pinnedInner->resizeToWidth(width());
	}
	const auto max = pinnedMax();
	const auto regions = countRegions(_column->entity()->height());
	_reserve = regions.reserve;
	_column->setPadding({ 0, _reserve, 0, 0 });
	_scroll->setGeometry(
		0,
		regions.scrollTop,
		width(),
		regions.scrollHeight);
	if (_listsLoading && !_listsLoading->isHidden()) {
		_listsLoading->setGeometry(0, max, width(), height() - max);
	}

	const auto body = Ui::MapFrom(window(), this, rect());
	_titleBalance->setGeometry(
		body.x(),
		body.y() - st::separatePanelTitleHeight,
		(width()
			- st::separatePanelClose.width
			- st::separatePanelMenu.width),
		st::separatePanelTitleHeight);

	updatePinned();
	if (_stripShown) {
		const auto stripTop = std::max(
			regions.scrollTop,
			height() - st::walletRowsHintHeight);
		_stripShadow->setGeometry(
			0,
			stripTop - st::lineWidth,
			width(),
			st::lineWidth);
		_strip->setGeometry(0, stripTop, width(), st::walletRowsHintHeight);
	}
	_headerShadow->setGeometry(0, 0, width(), st::lineWidth);
	updateVisibleArea();
}

bool Content::revealSendingRow() {
	if (!_sendingRow || !base::take(_sendingRow->revealPending)) {
		return false;
	}
	auto &wallet = _show->session().wallet();
	if (wallet.collectiblesTab()) {
		wallet.setCollectiblesTab(false);
		_scroll->scrollToY(0);
		return false;
	}
	const auto slot = _sendingRow->slot;
	const auto top = Ui::MapFrom(_container, slot, QPoint()).y();
	const auto bottom = top + slot->height();
	const auto scrollTop = _scroll->scrollTop();
	if (top < std::max(scrollTop, _reserve)) {
		_scroll->scrollToY(top);
	} else if (bottom > scrollTop + _scroll->height()) {
		_scroll->scrollToY(bottom - _scroll->height());
	}
	return true;
}

void Content::flySendDiamond(
		const std::string &operationId,
		not_null<Ui::TonAmountInput*> amount) {
	const auto current = [&]() -> SendingHistoryRow* {
		return (_sendingRow && _sendingRow->operationId == operationId)
			? _sendingRow->look
			: nullptr;
	};
	if (!current() || anim::Disabled()) {
		return;
	}
	const auto body = dynamic_cast<Ui::RpWidget*>(parentWidget());
	const auto now = crl::now();
	const auto revealed = body && revealSendingRow();
	const auto look = current();
	if (!look) {
		return;
	}
	auto diamond = AmountDiamond();
	if (revealed && look->surfaceShown()) {
		const auto slot = look->diamondTarget(now).translated(
			QPointF(Ui::MapFrom(_scroll.data(), look, QPoint())));
		const auto occluded = _reserve
			- std::clamp(_scroll->scrollTop(), 0, _reserve);
		if (slot.top() >= occluded && slot.bottom() <= _scroll->height()) {
			diamond = TakeAmountDiamond(amount);
		}
	}
	if (!diamond.icon) {
		look->scheduleBump(now + kSendingRowFlightDuration);
		return;
	}
	look->awaitDiamond();
	struct State {
		DiamondFlight *flight = nullptr;
		bool landed = false;
	};
	const auto state = std::make_shared<State>();
	const auto weak = base::make_weak(look);
	const auto loopStarted = SendingDiamondLoopStart(diamond.icon.get(), now);
	_diamondFlight = std::make_unique<DiamondFlight>(DiamondFlightArgs{
		.body = body,
		.icon = std::move(diamond.icon),
		.from = diamond.global,
		.loopStarted = loopStarted,
		.duration = kSendingRowFlightDuration,
		.target = [=](crl::time at) -> std::optional<QRectF> {
			const auto row = weak.get();
			if (!row || !row->surfaceShown()) {
				return std::nullopt;
			}
			return row->diamondTarget(at).translated(
				QPointF(Ui::MapFrom(body, row, QPoint())));
		},
		.landed = [=](std::unique_ptr<Lottie::Icon> icon, crl::time loop) {
			if (const auto row = weak.get()) {
				state->landed = true;
				row->landDiamond(std::move(icon), loop);
			}
		},
		.finished = [=] {
			if (const auto row = state->landed ? nullptr : weak.get()) {
				row->cancelDiamondAwait();
			}
			crl::on_main(this, [=] {
				if (_diamondFlight.get() == state->flight) {
					_diamondFlight = nullptr;
				}
			});
		},
	});
	state->flight = _diamondFlight.get();
}

void Content::updateVisibleArea() {
	const auto top = _scroll->scrollTop();
	_column->setVisibleTopBottom(top, top + _scroll->height());
}

void Content::updatePinned() {
	if (!width() || !height()) {
		return;
	}
	const auto max = pinnedMax();
	const auto min = pinnedMin();
	const auto top = std::clamp(_scroll->scrollTop(), 0, _reserve);
	const auto height = max - top;
	_pinnedInner->moveToLeft(0, height - max, width());
	_pinned->setGeometry(0, 0, width(), height);
	_pinnedBackground->setGeometry(0, 0, width(), height);
	const auto rest = cardRest();
	const auto fold = ComputeCardFold(rest, foldProgress());
	const auto cardWidget = QRect(
		QPoint(rest.left(), 0),
		rest.bottomRight());
	_card->setGeometry(cardWidget);
	_card->setFold(fold);
	const auto shown = fold.valid && fold.opacity > 0.;
	if (shown) {
		const auto outline = _card->paintedOutline();
		const auto bounds = outline.boundingRect().toAlignedRect();
		_cardButton->setGeometry(bounds);
		_cardButton->setMask(QRegion(
			outline.translated(-bounds.topLeft()).toPolygon()));
	}
	_cardButton->setVisible(shown);
	_scroll->setVerticalBarTopSkip(height - min);
	_tabsShadow->setGeometry(0, height, width(), st::lineWidth);
	_headerShadow->setVisible(height == min);
	_pinnedBalance->setGeometry(_pinned->rect());

	// The card's dirty area is its whole widget rect, which contains
	// every folded quad, and the ink's is the union of the rects it was
	// and is painted into.
	const auto inkPainted = _ink->boundingRect(fold);
	const auto inkDirty = _paintedInk.united(inkPainted);
	_paintedInk = inkPainted;
	update(cardWidget);
	update(inkDirty);
	_pinnedBackground->update(cardWidget);
	_pinnedBalance->update(cardWidget);
	_pinnedBalance->update(inkDirty);
	if (_paintedHeight == height && _paintedMin == min) {
		return;
	}
	_paintedHeight = height;
	_paintedMin = min;
	_pinnedBackground->update();
	// Reaching here means the pinned height changed, and with it the
	// scrolled distance the fold progress is computed from. The band
	// overlay carries the fading title and the end of the balance's
	// travel, and it is at most the title bar's height tall, so it is
	// repainted whole rather than tracked rect by rect.
	_titleBalance->update();
}

void Content::checkLoadMore() {
	auto &wallet = _show->session().wallet();
	if (wallet.listsGated()) {
		return;
	}
	const auto collectibles = wallet.collectiblesTab();
	const auto hasNext = collectibles
		? wallet.collectiblesHasNext()
		: wallet.historyHasNext();
	if (!hasNext) {
		return;
	}
	if (_scroll->scrollTop() + _scroll->height() >= _scroll->scrollTopMax()) {
		if (collectibles) {
			wallet.loadMoreCollectibles();
		} else {
			wallet.loadMoreHistory();
		}
	}
}

void Content::focusInEvent(QFocusEvent *e) {
	_scroll->setFocus();
}

void Content::resizeEvent(QResizeEvent *e) {
	updateRegions();
	_loadMoreCheck.call();
}

void Content::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	if (_stripShown) {
		p.fillRect(0, 0, width(), _strip->y(), st::windowBg);
		return;
	}
	PaintBottomRoundedPlate(p, rect(), st::windowBg);
}

class CurrencyListWidget final : public Ui::RpWidget {
public:
	CurrencyListWidget(
		not_null<QWidget*> parent,
		std::shared_ptr<Main::SessionShow> show,
		Fn<void(QString)> chosen);

	void updateFilter(const QString &query);
	void selectSkip(int direction);
	void selectSkipPage(int height, int direction);
	void chooseSelected();
	void scrollToCurrent();

	[[nodiscard]] rpl::producer<Ui::ScrollToRequest> mustScrollTo() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	struct Row {
		QString code;
		QString name;
		QStringList words;
	};

	[[nodiscard]] const std::vector<Row> &current() const;
	[[nodiscard]] bool rowMatches(const Row &row) const;
	void refreshRows();
	void refreshFiltered();
	void refreshHeight();
	void updateSelected(QPoint localPos);
	void setSelected(int index);
	void setPressed(int pressed);
	void updateRow(int index);

	const std::shared_ptr<Main::SessionShow> _show;
	const Fn<void(QString)> _chosen;
	QString _activeCode;
	QStringList _filter;
	std::vector<Row> _rows;
	std::vector<Row> _filtered;
	std::vector<std::unique_ptr<Ui::RippleAnimation>> _ripples;
	int _selected = -1;
	int _pressed = -1;
	bool _mouseSelection = false;

	rpl::event_stream<Ui::ScrollToRequest> _mustScrollTo;

};

[[nodiscard]] bool ForwardCurrencyNavigation(
		not_null<QKeyEvent*> e,
		not_null<CurrencyListWidget*> list,
		int pageHeight) {
	if (e->key() == Qt::Key_Down) {
		list->selectSkip(1);
	} else if (e->key() == Qt::Key_Up) {
		list->selectSkip(-1);
	} else if (e->key() == Qt::Key_PageDown) {
		list->selectSkipPage(pageHeight, 1);
	} else if (e->key() == Qt::Key_PageUp) {
		list->selectSkipPage(pageHeight, -1);
	} else {
		return false;
	}
	return true;
}

void WalletChooseCurrencyBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show) {
	box->setTitle(tr::lng_wallet_currency_title());
	box->setWidth(st::boxWideWidth);
	box->setMaxHeight(st::boxMaxListHeight);
	AddBoxCloseButton(box);
	const auto select = box->setPinnedToTopContent(
		object_ptr<Ui::MultiSelect>(
			box,
			st::defaultMultiSelect,
			tr::lng_country_ph()));
	const auto list = box->addRow(
		object_ptr<CurrencyListWidget>(
			box,
			show,
			[=](QString code) {
				show->session().wallet().rates().setCurrency(code);
				box->closeBox();
			}),
		style::margins());
	box->setFocusCallback([=] { select->setInnerFocus(); });
	select->setQueryChangedCallback([=](const QString &query) {
		box->scrollToY(0);
		list->updateFilter(query);
	});
	select->setSubmittedCallback([=](Qt::KeyboardModifiers) {
		list->chooseSelected();
	});
	select->setCancelledCallback([=] { box->closeBox(); });
	list->mustScrollTo(
	) | rpl::on_next([=](Ui::ScrollToRequest request) {
		box->scrollToY(request.ymin, request.ymax);
	}, list->lifetime());
	base::install_event_filter(select, [=](not_null<QEvent*> e) {
		if (e->type() != QEvent::KeyPress) {
			return base::EventFilterResult::Continue;
		}
		const auto key = static_cast<QKeyEvent*>(e.get());
		const auto pageHeight = box->height() - select->height();
		return ForwardCurrencyNavigation(key, list, pageHeight)
			? base::EventFilterResult::Cancel
			: base::EventFilterResult::Continue;
	});
	box->setShowFinishedCallback([=] { list->scrollToCurrent(); });
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

CurrencyListWidget::CurrencyListWidget(
	not_null<QWidget*> parent,
	std::shared_ptr<Main::SessionShow> show,
	Fn<void(QString)> chosen)
: RpWidget(parent)
, _show(std::move(show))
, _chosen(std::move(chosen)) {
	setAttribute(Qt::WA_OpaquePaintEvent);
	_show->session().wallet().rates().value(
	) | rpl::on_next([=] {
		refreshRows();
	}, lifetime());
}

void CurrencyListWidget::updateFilter(const QString &query) {
	auto filter = TextUtilities::PrepareSearchWords(query);
	if (_filter == filter) {
		return;
	}
	_filter = std::move(filter);
	refreshFiltered();
	_selected = current().empty() ? -1 : 0;
	update();
}

void CurrencyListWidget::selectSkip(int direction) {
	_mouseSelection = false;
	const auto &list = current();
	const auto moved = _selected + direction;
	const auto next = (moved <= 0)
		? (list.empty() ? -1 : 0)
		: (moved >= int(list.size()))
		? -1
		: moved;
	setSelected(next);
	if (_selected >= 0) {
		_mustScrollTo.fire(Ui::ScrollToRequest(
			st::walletCurrencyListSkip
				+ _selected * st::walletCurrencyRowHeight,
			st::walletCurrencyListSkip
				+ (_selected + 1) * st::walletCurrencyRowHeight));
	}
}

void CurrencyListWidget::selectSkipPage(int height, int direction) {
	const auto rows = height / st::walletCurrencyRowHeight;
	if (!rows) {
		return;
	}
	selectSkip(rows * direction);
}

void CurrencyListWidget::chooseSelected() {
	const auto &list = current();
	if (_selected < 0 || _selected >= int(list.size())) {
		return;
	}
	const auto code = list[_selected].code;
	_chosen(code);
}

void CurrencyListWidget::scrollToCurrent() {
	const auto &list = current();
	for (auto i = 0, count = int(list.size()); i != count; ++i) {
		if (list[i].code == _activeCode) {
			_mustScrollTo.fire(Ui::ScrollToRequest(
				st::walletCurrencyListSkip
					+ i * st::walletCurrencyRowHeight,
				st::walletCurrencyListSkip
					+ (i + 1) * st::walletCurrencyRowHeight));
			return;
		}
	}
}

rpl::producer<Ui::ScrollToRequest> CurrencyListWidget::mustScrollTo() const {
	return _mustScrollTo.events();
}

void CurrencyListWidget::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto clip = e->rect();
	p.setClipRect(clip);

	const auto &list = current();
	if (list.empty()) {
		p.fillRect(clip, st::windowBg);
		p.setFont(st::noContactsFont);
		p.setPen(st::noContactsColor);
		p.drawText(
			QRect(0, 0, width(), st::noContactsHeight),
			tr::lng_wallet_currency_none(tr::now),
			style::al_center);
		return;
	}
	const auto skip = st::walletCurrencyListSkip;
	const auto rowHeight = st::walletCurrencyRowHeight;
	const auto count = int(list.size());
	const auto top = QRect(0, 0, width(), skip);
	if (clip.intersects(top)) {
		p.fillRect(clip.intersected(top), st::windowBg);
	}
	const auto from = std::clamp((clip.y() - skip) / rowHeight, 0, count);
	const auto till = std::clamp(
		(clip.y() + clip.height() - skip + rowHeight - 1) / rowHeight,
		0,
		count);
	const auto &icon = st::walletCurrencyCheckIcon;
	const auto left = st::walletCurrencyRowPadding.left();
	const auto right = st::walletCurrencyRowPadding.right();
	for (auto i = from; i != till; ++i) {
		const auto &row = list[i];
		const auto selected = (i == (_pressed >= 0 ? _pressed : _selected));
		const auto y = skip + i * rowHeight;
		p.fillRect(
			0,
			y,
			width(),
			rowHeight,
			selected ? st::windowBgOver : st::windowBg);
		if (i < int(_ripples.size()) && _ripples[i]) {
			_ripples[i]->paint(p, 0, y, width());
			if (_ripples[i]->empty()) {
				_ripples[i].reset();
			}
		}
		const auto textTop = y + st::walletCurrencyRowPadding.top();
		p.setFont(st::walletCurrencyRowCodeFont);
		p.setPen(st::walletCurrencyRowCodeFg);
		p.drawTextLeft(left, textTop, width(), row.code);
		if (!row.name.isEmpty()) {
			const auto codeWidth = st::walletCurrencyRowCodeFont->width(
				row.code);
			const auto nameLeft = left
				+ codeWidth
				+ st::walletCurrencyRowNameSkip;
			const auto available = width()
				- nameLeft
				- right
				- icon.width()
				- st::walletCurrencyRowNameSkip;
			p.setFont(st::normalFont);
			p.setPen(st::walletCurrencyRowNameFg);
			p.drawTextLeft(
				nameLeft,
				textTop,
				width(),
				st::normalFont->elided(row.name, available));
		}
		if (row.code == _activeCode) {
			icon.paint(
				p,
				width() - right - icon.width(),
				y + (rowHeight - icon.height()) / 2,
				width());
		}
	}
}

void CurrencyListWidget::enterEventHook(QEnterEvent *e) {
	setMouseTracking(true);
}

void CurrencyListWidget::leaveEventHook(QEvent *e) {
	_mouseSelection = false;
	setMouseTracking(false);
	setSelected(-1);
}

void CurrencyListWidget::mouseMoveEvent(QMouseEvent *e) {
	_mouseSelection = true;
	updateSelected(e->pos());
}

void CurrencyListWidget::mousePressEvent(QMouseEvent *e) {
	_mouseSelection = true;
	updateSelected(e->pos());
	setPressed(_selected);
	const auto &list = current();
	if (_pressed < 0 || _pressed >= int(list.size())) {
		return;
	}
	if (int(_ripples.size()) <= _pressed) {
		_ripples.resize(_pressed + 1);
	}
	if (!_ripples[_pressed]) {
		auto mask = Ui::RippleAnimation::RectMask(
			QSize(width(), st::walletCurrencyRowHeight));
		_ripples[_pressed] = std::make_unique<Ui::RippleAnimation>(
			st::defaultRippleAnimation,
			std::move(mask),
			[this, index = _pressed] { updateRow(index); });
		_ripples[_pressed]->add(e->pos() - QPoint(
			0,
			st::walletCurrencyListSkip
				+ _pressed * st::walletCurrencyRowHeight));
	}
}

void CurrencyListWidget::mouseReleaseEvent(QMouseEvent *e) {
	const auto pressed = _pressed;
	setPressed(-1);
	updateRow(_selected);
	if (e->button() == Qt::LeftButton
		&& pressed >= 0
		&& pressed == _selected) {
		chooseSelected();
	}
}

auto CurrencyListWidget::current() const
-> const std::vector<CurrencyListWidget::Row> & {
	return _filtered;
}

bool CurrencyListWidget::rowMatches(const Row &row) const {
	return ranges::all_of(_filter, [&](const QString &word) {
		return ranges::any_of(row.words, [&](const QString &name) {
			return name.startsWith(word);
		});
	});
}

void CurrencyListWidget::refreshRows() {
	auto &rates = _show->session().wallet().rates();
	_activeCode = rates.current().currency;
	_rows.clear();
	const auto codes = rates.currencies();
	const auto top = TopCurrencies(&_show->session());
	auto ordered = std::vector<QString>();
	ordered.reserve(codes.size());
	for (const auto &code : top) {
		if (ranges::contains(codes, code)) {
			ordered.push_back(code);
		}
	}
	for (const auto &code : codes) {
		if (!ranges::contains(top, code)) {
			ordered.push_back(code);
		}
	}
	_rows.reserve(ordered.size());
	for (const auto &code : ordered) {
		const auto names = LookupCurrencyNames(code);
		_rows.push_back({
			.code = code,
			.name = (names.localized.isEmpty()
				? names.english
				: names.localized),
			.words = TextUtilities::PrepareSearchWords(QStringList{
				code,
				names.english,
				names.localized,
			}.join(QChar(' '))),
		});
	}
	refreshFiltered();
}

void CurrencyListWidget::refreshFiltered() {
	_filtered.clear();
	_filtered.reserve(_rows.size());
	for (const auto &row : _rows) {
		if (_filter.isEmpty() || rowMatches(row)) {
			_filtered.push_back(row);
		}
	}
	_ripples.clear();
	_pressed = -1;
	if (_selected >= int(current().size())) {
		_selected = -1;
	}
	refreshHeight();
	update();
}

void CurrencyListWidget::refreshHeight() {
	const auto &list = current();
	resize(
		width(),
		list.empty()
			? st::noContactsHeight
			: (st::walletCurrencyListSkip
				+ int(list.size()) * st::walletCurrencyRowHeight));
}

void CurrencyListWidget::updateSelected(QPoint localPos) {
	if (!_mouseSelection) {
		return;
	}
	const auto in = visibleRegion().boundingRect().contains(
		mapFromGlobal(QCursor::pos()));
	const auto &list = current();
	const auto skip = st::walletCurrencyListSkip;
	const auto rowsHeight = int(list.size()) * st::walletCurrencyRowHeight;
	const auto selected = (in
		&& localPos.y() >= skip
		&& localPos.y() < skip + rowsHeight)
		? ((localPos.y() - skip) / st::walletCurrencyRowHeight)
		: -1;
	setSelected(selected);
}

void CurrencyListWidget::setSelected(int index) {
	if (_selected == index) {
		return;
	}
	updateRow(_selected);
	_selected = index;
	updateRow(_selected);
}

void CurrencyListWidget::setPressed(int pressed) {
	if (_pressed >= 0
		&& _pressed < int(_ripples.size())
		&& _ripples[_pressed]) {
		_ripples[_pressed]->lastStop();
	}
	_pressed = pressed;
}

void CurrencyListWidget::updateRow(int index) {
	if (index >= 0) {
		update(
			0,
			st::walletCurrencyListSkip
				+ index * st::walletCurrencyRowHeight,
			width(),
			st::walletCurrencyRowHeight);
	}
}

void AcquireKeyThroughLadder(
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<CommentScope> scope,
		Fn<bool()> current,
		rpl::lifetime &lifetime,
		Fn<void(KeyAuthorization)> done,
		rpl::producer<QString> importAbout) {
	const auto context = std::make_shared<KeyContext>(
		std::move(show),
		std::move(scope),
		std::move(current),
		std::move(done));
	lifetime.add([context] { context->cancel(); });
	RunKeyRequiringAction(context, [=] {
		if (!context->valid()) {
			context->cancel();
			return;
		}
		AcquireVaultUnlock({
			.show = context,
			.done = [=](KeyAuthorization auth) {
				context->ready(std::move(auth));
			},
		});
	}, KeyActionKind::ResumeAfterRestore, context,
		std::move(importAbout));
}

} // namespace

void AcquireTransferCommentKey(
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<CommentScope> scope,
		Fn<bool()> current,
		rpl::lifetime &lifetime,
		Fn<void(KeyAuthorization)> done) {
	AcquireKeyThroughLadder(
		std::move(show),
		std::move(scope),
		std::move(current),
		lifetime,
		std::move(done),
		nullptr);
}

void AcquireWalletKey(
		std::shared_ptr<Main::SessionShow> show,
		Fn<bool()> current,
		rpl::lifetime &lifetime,
		Fn<void(KeyAuthorization)> done,
		rpl::producer<QString> importAbout) {
	AcquireKeyThroughLadder(
		std::move(show),
		nullptr,
		std::move(current),
		lifetime,
		std::move(done),
		std::move(importAbout));
}

void ShowTransactionDetails(
		std::shared_ptr<Main::SessionShow> show,
		TransferItem item,
		bool partial,
		std::shared_ptr<CollectibleMedia> media,
		Fn<bool()> originCurrent,
		rpl::producer<> originInvalidated,
		Fn<void()> openWallet,
		Ui::LayerOptions options) {
	if (!show || !show->valid()
		|| (originCurrent && !originCurrent())) {
		return;
	}
	show->showBox(Box(
		WalletTransactionBox,
		show,
		std::move(item),
		partial,
		std::move(media),
		std::move(originCurrent),
		std::move(originInvalidated),
		std::move(openWallet),
		rpl::producer<TransferItem>()), options);
}

void ShowSubmittedTransfer(
		std::shared_ptr<Main::SessionShow> show,
		const std::string &operationId) {
	if (!show || !show->valid()) {
		return;
	}
	const auto wallet = &show->session().wallet();
	const auto find = [=]() -> std::optional<TransferItem> {
		if (auto item = wallet->trackedTransaction(operationId)) {
			return item;
		}
		const auto pending = wallet->pendingSend();
		return (pending && pending->operationId == operationId)
			? std::make_optional(ItemFromPending(*pending))
			: std::nullopt;
	};
	if (const auto item = find()) {
		ShowWalletTransactionBox(
			show,
			*item,
			nullptr,
			wallet->historyUpdates() | rpl::map(find) | rpl::filter_optional());
	}
}

bool ShowFirstGramsIfPending(std::shared_ptr<Main::SessionShow> show) {
	if (!show || !show->valid()) {
		return false;
	}
	const auto session = &show->session();
	if (!session->promoSuggestions().current(
			Data::PromoSuggestions::SugWalletFirstIncomingTransfer())) {
		return false;
	}
	show->showBox(Box(WalletFirstGramsBox, session));
	return true;
}

rpl::producer<bool> TransactionsShownValue(
		not_null<Main::Session*> session) {
	return rpl::combine(
		HistoryShownValue(session),
		session->wallet().collectiblesTabValue()
	) | rpl::map([](bool history, bool collectibles) {
		return history && !collectibles;
	}) | rpl::distinct_until_changed();
}

base::unique_qptr<Ui::RpWidget> CreateContent(
		not_null<Ui::SeparatePanel*> panel,
		std::shared_ptr<Main::SessionShow> show) {
	return base::make_unique_q<Content>(panel, std::move(show));
}

object_ptr<Ui::RpWidget> MakeWalletCard(
		QWidget *parent,
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<bool> markPlayed) {
	auto result = object_ptr<Ui::FixedHeightWidget>(
		parent,
		st::walletCardHeight);
	const auto raw = result.data();
	raw->setNaturalWidth(PanelCardWidth());

	const auto card = Ui::CreateChild<Card>(
		raw,
		show,
		CardNameValue(&show->session()));
	card->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto overlay = Ui::CreateChild<Ui::RpWidget>(raw);
	overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
	overlay->show();
	overlay->raise();

	const auto ink = raw->lifetime().make_state<BalanceInk>();
	{
		const auto scope = WindowPaletteScope(raw);
		ink->setOuterWidth(st::walletPanelSize.width());
	}

	raw->sizeValue(
	) | rpl::on_next([=](QSize size) {
		const auto rest = QRect(QPoint(), size);
		card->setGeometry(rest);
		card->setFold(ComputeCardFold(rest, 0.));
		overlay->setGeometry(rest);
	}, raw->lifetime());

	overlay->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		auto p = QPainter(overlay);
		auto hq = PainterHighQualityEnabler(p);
		ink->paint(
			p,
			ComputeCardFold(raw->rect(), 0.),
			QRegion(raw->rect()),
			clip);
	}, overlay->lifetime());

	SetupCardBalance(
		ink,
		&show->session(),
		[=] { overlay->update(); },
		raw);
	SetupCardMark(ink, raw, std::move(markPlayed), [=] {
		overlay->update(
			ink->markPaintRect(ComputeCardFold(raw->rect(), 0.)));
	});

	rpl::single(rpl::empty) | rpl::then(
		style::PaletteChanged()
	) | rpl::on_next([=] {
		const auto scope = WindowPaletteScope(raw);
		ink->refresh();
		card->invalidateCache();
		card->update();
		overlay->update();
	}, raw->lifetime());

	return result;
}

object_ptr<Ui::RpWidget> MakeTransferCard(
		QWidget *parent,
		not_null<Main::Session*> session,
		TransferCardArgs args) {
	auto result = object_ptr<Ui::FixedHeightWidget>(
		parent,
		st::walletCardHeight);
	const auto raw = result.data();
	raw->setNaturalWidth(PanelCardWidth());

	struct State {
		BalanceInk ink;
		CardBackground background;
		QStringList lines;
	};
	const auto state = raw->lifetime().make_state<State>();
	{
		const auto scope = WindowPaletteScope(raw);
		state->ink.setOuterWidth(st::walletPanelSize.width());
	}
	state->lines = TransferCardLines(args.destination, args.recipients);

	const auto info = Ui::CreateChild<Ui::AbstractButton>(raw);
	info->setClickedCallback(std::move(args.info));
	info->show();
	raw->sizeValue(
	) | rpl::on_next([=](QSize size) {
		info->setGeometry(TransferCardInfoRect(size.width()));
	}, info->lifetime());

	raw->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		auto p = QPainter(raw);
		auto hq = PainterHighQualityEnabler(p);
		const auto rect = raw->rect();
		state->background.paint(p, rect, 0.);

		const auto plate = TransferCardInfoRect(rect.width());
		PaintCardQrPlate(p, plate);
		st::walletCardInfoIcon.paintInCenter(p, plate, CardQrIconFg());

		const auto font
			= st::walletDetailsCollectionLabel.style.font->monospace();
		const auto &lines = state->lines;
		auto baseline = rect.height()
			- st::walletCardNameBottom
			- (int(lines.size()) - 1) * font->height;
		p.setPen(st::activeButtonFg);
		p.setFont(font);
		for (const auto &line : lines) {
			p.drawText(st::walletCardContentLeft, baseline, line);
			baseline += font->height;
		}

		state->ink.paint(p, ComputeCardFold(rect, 0.), QRegion(rect), clip);
	}, raw->lifetime());

	const auto amount = args.netNano.value_or(-args.totalNano);
	const auto magnitude = std::abs(amount);
	const auto style = (amount > 0)
		? BalanceStyle::Plus
		: ((amount < 0) || !args.netNano)
		? BalanceStyle::Minus
		: BalanceStyle::Exact;
	FiatRateValue(
		session
	) | rpl::on_next([=](FiatRate rate) {
		const auto scope = WindowPaletteScope(raw);
		state->ink.setContent(
			CreditsAmount(
				magnitude / Ui::kNanosInOne,
				magnitude % Ui::kNanosInOne,
				CreditsType::Ton),
			FormatFiat(magnitude, rate),
			style);
		raw->update();
	}, raw->lifetime());

	rpl::single(rpl::empty) | rpl::then(
		style::PaletteChanged()
	) | rpl::on_next([=] {
		const auto scope = WindowPaletteScope(raw);
		state->ink.refresh();
		raw->update();
	}, raw->lifetime());

	SetupCardMark(&state->ink, raw, std::move(args.markPlayed), [=] {
		raw->update(
			state->ink.markPaintRect(ComputeCardFold(raw->rect(), 0.)));
	});

	return result;
}

void FillMenu(
		std::shared_ptr<Main::SessionShow> show,
		const Ui::Menu::MenuCallback &addAction) {
	const auto currency = show->session().wallet().rates().current().currency;
	addAction(
		(Ui::Text::FixAmpersandInAction(
			tr::lng_wallet_menu_currency(tr::now))
			+ u"\t"_q
			+ currency),
		[=] {
			show->showBox(Box(WalletChooseCurrencyBox, show));
		},
		&st::walletMenuCurrencyIcon);
	// The menu is rebuilt on every open, so this reading is live: the entry
	// is absent in both read-only modes and while the mode is still Unknown.
	const auto custody = show->session().wallet().deviceCustodyState();
	if (custody.mode == DeviceMode::Full) {
		addAction(
			Ui::Text::FixAmpersandInAction(
				tr::lng_wallet_protection_title(tr::now)),
			[=] {
				ShowKeyProtectionBox(
					show,
					{ .mode = KeyProtectionMode::Change });
			},
			&st::menuIconLock);
	}
	if (show->session().wallet().presence() == Presence::Ready) {
		addAction(
			Ui::Text::FixAmpersandInAction(
				tr::lng_wallet_keys_title(tr::now)),
			[=] { show->showBox(Box(WalletKeysBackupBox, show)); },
			&st::menuIconPermissions);
		const auto &store = show->session().wallet().tonConnect();
		const auto connected = ranges::any_of(
			ranges::views::values(store.sessions()),
			TonConnectSessionConnected);
		if (connected) {
			addAction(
				Ui::Text::FixAmpersandInAction(
					tr::lng_wallet_apps_title(tr::now)),
				[=] { show->showBox(Box(TonConnectAppsBox, show)); },
				&st::menuIconLink);
		}
	}
	addAction({ .isSeparator = true });
	addAction(
		Ui::Text::FixAmpersandInAction(tr::lng_wallet_how_menu(tr::now)),
		[=] { show->showBox(Box(WalletHowItWorksBox, &show->session())); },
		&st::menuIconFaq);
}

bool TransferLinkValid(const QString &url) {
	return ParseRecipientFlow(url).has_value();
}

void ShowTransferLink(
		std::shared_ptr<Main::SessionShow> show,
		const QString &url) {
	const auto flow = ParseRecipientFlow(url);
	if (!flow) {
		return;
	} else if (TransferLinkExpired(flow->expiresAt)) {
		show->showToast(tr::lng_wallet_send_link_expired(tr::now));
		return;
	}
	ResolveOwnerAndOpenSendFlow(show, *flow);
}

void ShowWalletConflict(
		std::shared_ptr<Main::SessionShow> show,
		Fn<void()> switched) {
	show->showBox(Box(WalletConflictBox, show, std::move(switched)));
}

Fn<void()> ShowWalletBusyBox(
		std::shared_ptr<Main::SessionShow> show,
		rpl::producer<QString> text,
		Fn<void()> dismissed) {
	auto box = Box(WalletBusyBox, std::move(text));
	const auto weak = base::make_weak(box.data());
	const auto closing = std::make_shared<bool>(false);
	box->boxClosing() | rpl::on_next([=] {
		if (!*closing && dismissed) {
			dismissed();
		}
	}, box->lifetime());
	show->showBox(std::move(box));
	return [=] {
		*closing = true;
		if (const auto strong = weak.get()) {
			if (strong->hasDelegate()) {
				strong->closeBox();
			}
		}
	};
}

void ShowSendToUser(
		std::shared_ptr<Main::SessionShow> show,
		not_null<UserData*> user,
		Fn<void()> sent,
		int64 amountNano,
		Fn<void()> notReady,
		base::weak_qptr<Ui::BoxContent> origin) {
	if (!show || !show->valid() || &show->session() != &user->session()) {
		return;
	}
	const auto session = &show->session();
	const auto userId = peerToUser(user->id);
	if (session->data().userLoaded(userId) != user) {
		return;
	}
	WhenWalletReady(show, [=] {
		const auto error = session->wallet().userAddresses(
		).forceResolveError(userId);
		if (!error.isEmpty() && error != u"WALLET_BALANCE_EMPTY"_q) {
			show->showToast(SendUserLoadErrorText(error));
			return;
		} else if (user->isSelf()) {
			const auto identity = session->wallet().transferWalletIdentity();
			auto flow = identity
				? ParseRecipientFlow(FormatFriendly(identity->address, false))
				: std::nullopt;
			if (!flow) {
				show->showToast(SendUserLoadErrorText(u"WALLET_NOT_READY"_q));
				return;
			}
			flow->amountNano = amountNano;
			show->showBox(Box(
				WalletSendBox,
				show,
				std::move(flow),
				static_cast<UserData*>(nullptr),
				sent,
				notReady,
				amountNano,
				origin));
			return;
		}
		show->showBox(Box(
			WalletSendBox,
			show,
			std::nullopt,
			user.get(),
			sent,
			notReady,
			amountNano,
			origin));
	}, notReady);
}

void ShowCollectibleTransfer(
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<CollectibleMedia> media,
		const QString &collectible) {
	if (!show || !show->valid() || !media || collectible.isEmpty()) {
		return;
	}
	media->resolve(collectible);
	const auto weak = std::weak_ptr<CollectibleMedia>(media);
	WhenWalletReady(show, [=] {
		const auto strong = weak.lock();
		if (!strong || !show->valid()) {
			return;
		}
		show->showBox(Box(
			WalletSendRecipientBox,
			show,
			QString(),
			std::make_shared<const CollectibleTransfer>(CollectibleTransfer{
				.address = collectible,
				.media = strong,
			})));
	});
}

void ShowSendToLinkRecipient(
		std::shared_ptr<Main::SessionShow> show,
		const QString &recipient,
		int64 amountNano) {
	if (!show || !show->valid()) {
		return;
	}
	const auto session = &show->session();
	const auto invalid = [=] {
		show->showToast(tr::lng_wallet_send_link_invalid(tr::now));
	};
	if (auto flow = ParseRecipientFlow(recipient)) {
		if (!flow->amountNano) {
			flow->amountNano = amountNano;
		}
		ResolveOwnerAndOpenSendFlow(show, *flow);
		return;
	}
	const auto username = recipient.startsWith('@')
		? recipient.mid(1)
		: recipient;
	if (!qthelp::regex_match(u"^[a-zA-Z0-9\\_]+$"_q, username, {})) {
		invalid();
		return;
	}
	const auto open = [=](PeerData *peer) {
		if (!show->valid() || &show->session() != session) {
			return;
		}
		const auto user = peer ? peer->asUser() : nullptr;
		if (!user) {
			show->showToast(tr::lng_wallet_send_user_unavailable(tr::now));
			return;
		}
		ShowSendToUser(show, user, nullptr, amountNano);
	};
	if (const auto peer = session->data().peerByUsername(username)) {
		open(peer);
		return;
	}
	session->api().request(MTPcontacts_ResolveUsername(
		MTP_flags(0),
		MTP_string(username),
		MTP_string()
	)).done([=](const MTPcontacts_ResolvedPeer &result) {
		const auto &data = result.data();
		session->data().processUsers(data.vusers());
		session->data().processChats(data.vchats());
		const auto peerId = peerFromMTP(data.vpeer());
		open(peerId ? session->data().peer(peerId).get() : nullptr);
	}).fail([=] {
		if (show->valid() && &show->session() == session) {
			show->showToast(
				tr::lng_username_not_found(tr::now, lt_user, username));
		}
	}).send();
}

} // namespace Wallet
