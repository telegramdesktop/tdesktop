/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/preview_ai_tone_box.h"

#include "api/api_compose_with_ai.h"
#include "boxes/create_ai_tone_box.h"
#include "core/click_handler_types.h"
#include "core/ui_integration.h"
#include "data/data_ai_compose_tones.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "ui/controls/custom_emoji_toast_icon.h"
#include "ui/effects/animation_value.h"
#include "ui/effects/animations.h"
#include "ui/effects/skeleton_animation.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/painter.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/text/text_entity.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"

#include <optional>

#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"

namespace {

constexpr auto kToastDuration = crl::time(4000);
constexpr auto kSpinDuration = crl::time(600);
constexpr auto kWaitDuration = crl::time(1000);

class RefreshSpinEmoji final : public Ui::Text::CustomEmoji {
public:
	RefreshSpinEmoji(
		std::shared_ptr<rpl::variable<bool>> loading,
		Fn<void()> repaint);

	int width() override;
	QString entityData() override;
	void paint(QPainter &p, const Context &context) override;
	void unload() override;
	bool ready() override;
	bool readyInDefaultState() override;

private:
	enum class Phase : uchar { Idle, Spinning, Waiting };

	void handleLoading(bool now);
	void tick(crl::time now);
	[[nodiscard]] float64 angleDegrees(crl::time now) const;

	rpl::variable<bool> _loading;
	const Fn<void()> _repaint;
	Ui::Animations::Basic _animation;
	crl::time _phaseStarted = 0;
	Phase _phase = Phase::Idle;
	rpl::lifetime _lifetime;

};

RefreshSpinEmoji::RefreshSpinEmoji(
	std::shared_ptr<rpl::variable<bool>> loading,
	Fn<void()> repaint)
: _loading(loading->value())
, _repaint(std::move(repaint))
, _animation([=](crl::time now) { tick(now); }) {
	_loading.value(
	) | rpl::on_next([=](bool value) {
		handleLoading(value);
	}, _lifetime);
}

int RefreshSpinEmoji::width() {
	const auto &e = st::aiTonePreviewAnotherExampleIcon;
	return e.padding.left() + e.icon.width() + e.padding.right();
}

QString RefreshSpinEmoji::entityData() {
	return u"ai-tone-refresh"_q;
}

float64 RefreshSpinEmoji::angleDegrees(crl::time now) const {
	if (_phase != Phase::Spinning) {
		return 0.;
	}
	const auto elapsed = now - _phaseStarted;
	const auto dt = std::clamp(
		elapsed / float64(kSpinDuration),
		0.,
		1.);
	return anim::easeOutBack(360., dt);
}

void RefreshSpinEmoji::paint(QPainter &p, const Context &context) {
	const auto &e = st::aiTonePreviewAnotherExampleIcon;
	const auto size = e.icon.size();
	const auto pos = context.position
		+ QPoint(e.padding.left(), e.padding.top());
	const auto angle = angleDegrees(context.now);
	auto hq = PainterHighQualityEnabler(p);
	if (angle != 0.) {
		const auto center = QPointF(pos)
			+ QPointF(size.width() / 2.0, size.height() / 2.0);
		p.save();
		p.translate(center);
		p.rotate(angle);
		p.translate(-center);
		e.icon.paint(p, pos, 0, context.textColor);
		p.restore();
	} else {
		e.icon.paint(p, pos, 0, context.textColor);
	}
}

void RefreshSpinEmoji::unload() {
}

bool RefreshSpinEmoji::ready() {
	return true;
}

bool RefreshSpinEmoji::readyInDefaultState() {
	return _phase == Phase::Idle;
}

void RefreshSpinEmoji::handleLoading(bool now) {
	if (now) {
		if (_phase == Phase::Idle) {
			_phase = Phase::Spinning;
			_phaseStarted = crl::now();
			_animation.start();
			if (_repaint) {
				_repaint();
			}
		}
	} else if (_phase == Phase::Waiting) {
		_phase = Phase::Idle;
		_animation.stop();
		if (_repaint) {
			_repaint();
		}
	}
}

void RefreshSpinEmoji::tick(crl::time now) {
	const auto elapsed = now - _phaseStarted;
	if (_phase == Phase::Spinning && elapsed >= kSpinDuration) {
		if (_loading.current()) {
			_phase = Phase::Waiting;
			_phaseStarted = now;
		} else {
			_phase = Phase::Idle;
			_animation.stop();
		}
	} else if (_phase == Phase::Waiting && elapsed >= kWaitDuration) {
		if (_loading.current()) {
			_phase = Phase::Spinning;
			_phaseStarted = now;
		} else {
			_phase = Phase::Idle;
			_animation.stop();
		}
	}
	if (_repaint) {
		_repaint();
	}
}

class PreviewAiToneExampleCard final : public Ui::RpWidget {
public:
	PreviewAiToneExampleCard(
		QWidget *parent,
		not_null<Main::Session*> session,
		std::shared_ptr<rpl::variable<bool>> loading);

	void showExample(Data::AiComposeToneExample example);
	void showSkeleton(bool shown);
	void setAnotherVisible(bool visible);
	[[nodiscard]] rpl::producer<> anotherExampleRequested() const;

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	const not_null<Main::Session*> _session;
	const not_null<Ui::VerticalLayout*> _layout;
	const not_null<Ui::FlatLabel*> _beforeTitle;
	const not_null<Ui::FlatLabel*> _beforeBody;
	const style::complex_color _shadowColor;
	const not_null<Ui::PlainShadow*> _shadow;
	const not_null<Ui::FlatLabel*> _afterTitle;
	const not_null<Ui::FlatLabel*> _afterBody;
	const not_null<Ui::RoundButton*> _another;
	Ui::SkeletonAnimation _beforeSkeleton;
	Ui::SkeletonAnimation _afterSkeleton;
	rpl::event_stream<> _anotherExampleRequested;

};

PreviewAiToneExampleCard::PreviewAiToneExampleCard(
	QWidget *parent,
	not_null<Main::Session*> session,
	std::shared_ptr<rpl::variable<bool>> loading)
: RpWidget(parent)
, _session(session)
, _layout(Ui::CreateChild<Ui::VerticalLayout>(this))
, _beforeTitle(_layout->add(
	object_ptr<Ui::FlatLabel>(
		_layout,
		tr::lng_ai_compose_before(tr::now),
		st::aiComposeCardTitle),
	QMargins(
		st::aiTonePreviewExampleCardPadding.left(),
		st::aiTonePreviewExampleCardPadding.top(),
		st::aiTonePreviewExampleCardPadding.right(),
		0)))
, _beforeBody(_layout->add(
	object_ptr<Ui::FlatLabel>(_layout, st::aiComposeBodyLabel),
	QMargins(
		st::aiTonePreviewExampleCardPadding.left(),
		st::aiTonePreviewExampleCardTitleSkip,
		st::aiTonePreviewExampleCardPadding.right(),
		0)))
, _shadowColor([] {
	auto color = st::windowSubTextFg->c;
	color.setAlphaF(st::aiComposeShadowOpacity);
	return color;
})
, _shadow(_layout->add(
	object_ptr<Ui::PlainShadow>(_layout, _shadowColor.color()),
	QMargins(
		st::aiTonePreviewExampleCardPadding.left(),
		st::aiTonePreviewExampleCardSectionSkip / 2,
		st::aiTonePreviewExampleCardPadding.right(),
		0)))
, _afterTitle(_layout->add(
	object_ptr<Ui::FlatLabel>(
		_layout,
		tr::lng_ai_compose_after(tr::now),
		st::aiComposeCardTitle),
	QMargins(
		st::aiTonePreviewExampleCardPadding.left(),
		st::aiTonePreviewExampleCardSectionSkip / 2,
		st::aiTonePreviewExampleCardPadding.right(),
		0)))
, _afterBody(_layout->add(
	object_ptr<Ui::FlatLabel>(_layout, st::aiComposeBodyLabel),
	QMargins(
		st::aiTonePreviewExampleCardPadding.left(),
		st::aiTonePreviewExampleCardTitleSkip,
		st::aiTonePreviewExampleCardPadding.right(),
		st::aiTonePreviewExampleCardPadding.bottom())))
, _another(Ui::CreateChild<Ui::RoundButton>(
	this,
	rpl::single(QString()),
	st::aiTonePreviewAnotherExampleButton))
, _beforeSkeleton(_beforeBody)
, _afterSkeleton(_afterBody) {
	_beforeBody->setSelectable(true);
	_afterBody->setSelectable(true);
	_another->raise();
	rpl::combine(
		widthValue(),
		_beforeTitle->geometryValue(),
		_another->widthValue()
	) | rpl::on_next([=](int width, QRect titleGeometry, int) {
		const auto right = st::aiTonePreviewExampleCardPadding.left();
		const auto &button = st::aiTonePreviewAnotherExampleButton;
		const auto &title = st::aiComposeCardTitle;
		const auto shift = title.style.font->ascent
			- button.style.font->ascent
			- button.textTop
			- button.padding.top();
		_another->moveToRight(
			right,
			titleGeometry.top() + shift,
			width);
	}, lifetime());
	auto context = Ui::Text::MarkedContext();
	context.repaint = [raw = _another.get()] { raw->update(); };
	context.customEmojiFactory = [loading = std::move(loading)](
			QStringView data,
			const Ui::Text::MarkedContext &context
		) -> std::unique_ptr<Ui::Text::CustomEmoji> {
		if (data != u"ai-tone-refresh"_q) {
			return nullptr;
		}
		return std::make_unique<RefreshSpinEmoji>(loading, context.repaint);
	};
	_another->setContext(context);
	_another->setText(rpl::single(
		Ui::Text::SingleCustomEmoji(u"ai-tone-refresh"_q)
			.append(tr::lng_ai_compose_tone_preview_add_example(tr::now))));
	_another->setClickedCallback([=] {
		_anotherExampleRequested.fire({});
	});
}

void PreviewAiToneExampleCard::showExample(
		Data::AiComposeToneExample example) {
	const auto context = Core::TextContext({ .session = _session });
	_beforeBody->setMarkedText(example.from, context);
	_afterBody->setMarkedText(example.to, context);
	_beforeSkeleton.stop();
	_afterSkeleton.stop();
	if (width() > 0) {
		resizeToWidth(width());
	}
}

void PreviewAiToneExampleCard::showSkeleton(bool shown) {
	if (shown) {
		_beforeSkeleton.start();
		_afterSkeleton.start();
	} else {
		_beforeSkeleton.stop();
		_afterSkeleton.stop();
	}
}

void PreviewAiToneExampleCard::setAnotherVisible(bool visible) {
	_another->setVisible(visible);
}

rpl::producer<> PreviewAiToneExampleCard::anotherExampleRequested() const {
	return _anotherExampleRequested.events();
}

int PreviewAiToneExampleCard::resizeGetHeight(int newWidth) {
	_layout->resizeToWidth(newWidth);
	_layout->moveToLeft(0, 0, newWidth);
	return _layout->heightNoMargins();
}

void PreviewAiToneExampleCard::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::aiTonePreviewExampleCardBg);
	p.drawRoundedRect(
		rect(),
		st::aiTonePreviewExampleCardRadius,
		st::aiTonePreviewExampleCardRadius);
}

void ShowToneAddedToast(
		std::shared_ptr<Ui::Show> show,
		not_null<Main::Session*> session,
		const Data::AiComposeTone &tone) {
	const auto size = QSize(
		st::aiComposeToneToastIconSize.width(),
		st::aiComposeToneToastIconSize.height());
	show->showToast(Ui::Toast::Config{
		.title = tr::lng_ai_compose_tone_added(tr::now),
		.text = tr::lng_ai_compose_tone_added_description(
			tr::now,
			lt_name,
			tr::marked(tone.title),
			tr::marked),
		.iconContent = Ui::MakeCustomEmojiToastIcon(
			session,
			tone.emojiId,
			size),
		.iconPadding = st::aiComposeToneToastIconPadding,
		.duration = kToastDuration,
	});
}

void ShowToneRemovedToast(std::shared_ptr<Ui::Show> show, bool deleted) {
	show->showToast(Ui::Toast::Config{
		.text = { (deleted
			? tr::lng_ai_compose_tone_deleted
			: tr::lng_ai_compose_tone_removed)(tr::now) },
		.icon = &st::aiComposeToneRemovedToastIcon,
		.duration = kToastDuration,
	});
}

[[nodiscard]] auto FindInstalledCustomTone(
		not_null<Main::Session*> session,
		const Data::AiComposeTone &tone)
-> std::optional<Data::AiComposeTone> {
	if (tone.isDefault) {
		return std::nullopt;
	}
	for (const auto &installedTone : session->data().aiComposeTones().list()) {
		if (!installedTone.isDefault && (installedTone.id == tone.id)) {
			return installedTone;
		}
	}
	return std::nullopt;
}

[[nodiscard]] bool BoundsToThisTone(const Data::AiComposeTone &tone) {
	if (tone.slug.isEmpty()) {
		return false;
	}
	const auto bound = Api::AiApplyBoundSlug();
	return !bound.isEmpty() && (bound == tone.slug);
}

} // namespace

void PreviewAiToneBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		Data::AiComposeTone tone,
		base::weak_ptr<Window::SessionController> controller) {
	box->setStyle(st::aiComposeBox);
	box->setNoContentMargin(true);
	box->setWidth(st::boxWideWidth);
	box->addTopButton(st::aiComposeBoxClose, [=] { box->closeBox(); });

	const auto top = box->setPinnedToTopContent(
		object_ptr<Ui::VerticalLayout>(box));
	Ui::AddSkip(top, st::defaultVerticalListSkip * 4);
	AddAiToneIconPreview(top, session, rpl::single(tone.emojiId), nullptr);
	top->add(
		object_ptr<Ui::FlatLabel>(
			top,
			rpl::single(tone.title),
			st::aiTonePreviewTitleLabel),
		st::aiTonePreviewTitleMargin,
		style::al_top);
	top->add(
		object_ptr<Ui::FlatLabel>(
			top,
			tr::lng_ai_compose_tone_preview_about(),
			st::aiTonePreviewAboutLabel),
		st::aiTonePreviewAboutMargin,
		style::al_top
	)->setTryMakeSimilarLines(true);

	const auto body = box->verticalLayout();

	struct State {
		int examplesCount = 0;
		std::shared_ptr<rpl::variable<bool>> requesting
			= std::make_shared<rpl::variable<bool>>(false);
	};
	const auto state = box->lifetime().make_state<State>();
	state->examplesCount = tone.firstExample ? 1 : 0;

	const auto card = body->add(
		object_ptr<PreviewAiToneExampleCard>(
			body,
			session,
			state->requesting),
		st::aiTonePreviewExampleCardMargin);
	const auto maxExamples = session->appConfig().get<int>(
		u"aicompose_tone_examples_num"_q,
		3);
	const auto updateAnother = [=] {
		card->setAnotherVisible(state->examplesCount < maxExamples);
	};
	updateAnother();
	const auto loadAnother = [=] {
		if (state->requesting->current()) {
			return;
		}
		*state->requesting = true;
		card->showSkeleton(true);
		const auto num = state->examplesCount;
		session->data().aiComposeTones().getToneExample(
			tone,
			num,
			crl::guard(box, [=](Data::AiComposeToneExample example) {
				*state->requesting = false;
				++state->examplesCount;
				card->showExample(std::move(example));
				updateAnother();
			}),
			crl::guard(box, [=](const MTP::Error &error) {
				*state->requesting = false;
				card->showSkeleton(false);
				if (!MTP::IgnoreError(error)) {
					box->showToast(error.type());
				}
			}));
	};
	card->anotherExampleRequested(
	) | rpl::on_next(loadAnother, card->lifetime());

	if (tone.firstExample) {
		card->showExample(*tone.firstExample);
	} else {
		loadAnother();
	}

	auto text = tr::marked();
	if (tone.installsCount > 0) {
		text = tr::lng_ai_compose_tone_preview_used_by(
			tr::now,
			lt_count,
			tone.installsCount,
			tr::marked);
	}
	if (const auto user = session->data().userLoaded(tone.authorId)) {
		const auto name = user->shortName();
		auto mention = tr::marked(name);
		mention.entities.push_back(EntityInText(
			EntityType::MentionName,
			0,
			name.size(),
			TextUtilities::MentionNameDataFromFields({
				.selfId = session->userId().bare,
				.userId = tone.authorId.bare,
				.accessHash = user->accessHash(),
			})));
		auto createdBy = tr::lng_ai_compose_tone_preview_created_by(
			tr::now,
			lt_user,
			std::move(mention),
			tr::marked);
		if (!text.empty()) {
			text.append(' ').append(std::move(createdBy));
		} else {
			text = std::move(createdBy);
		}
	}
	if (!text.empty()) {
		const auto attribution = body->add(
			object_ptr<Ui::FlatLabel>(body, st::aiTonePreviewAttributionLabel),
			st::aiTonePreviewAttributionMargin,
			style::al_top);
		attribution->setMarkedText(
			std::move(text),
			Core::TextContext({ .session = session }));
		attribution->setClickHandlerFilter([=](
				const ClickHandlerPtr &handler,
				Qt::MouseButton button) {
			ActivateClickHandler(attribution, handler, ClickContext{
				.button = button,
				.other = QVariant::fromValue(ClickHandlerContext{
					.sessionWindow = controller,
				}),
			});
			return false;
		});
	}
	if (const auto shortcutText = Api::AiApplyShortcutText()
			; !tone.slug.isEmpty() && !shortcutText.isEmpty()) {
		const auto label = tr::lng_ai_compose_bind_use_hotkey(
			tr::now,
			lt_keys,
			shortcutText);
		const auto checkbox = body->add(
			object_ptr<Ui::Checkbox>(
				body,
				label,
				st::aiComposeEmojifyCheckbox,
				std::make_unique<Ui::RoundCheckView>(
					st::defaultCheck,
					BoundsToThisTone(tone))),
			st::aiToneAuthorCheckboxMargin,
			style::al_top);
		checkbox->checkedChanges(
		) | rpl::on_next([=](bool toggled) {
			if (toggled) {
				Api::SetAiApplyBoundSlug(tone.slug);
			} else if (BoundsToThisTone(tone)) {
				Api::ClearAiApplyBoundSlug();
			}
		}, checkbox->lifetime());
	}

	Ui::AddSkip(body, st::aiTonePreviewBottomSkip);

	const auto installedTone = FindInstalledCustomTone(session, tone);

	if (installedTone) {
		const auto remove = box->addButton(
			installedTone->creator
				? tr::lng_ai_compose_tone_delete()
				: tr::lng_ai_compose_tone_remove(),
			[=] {
				const auto show = box->uiShow();
				ConfirmDeleteAiTone(
					show,
					session,
					*installedTone,
					crl::guard(box, [=] {
						box->closeBox();
						ShowToneRemovedToast(show, installedTone->creator);
					}));
			},
			st::aiToneDeleteButton);
		remove->setFullRadius(true);
	} else {
		const auto add = box->addButton(
			tr::lng_ai_compose_tone_preview_add(),
			[=] {
				const auto show = box->uiShow();
				session->data().aiComposeTones().save(
					tone,
					false,
					crl::guard(box, [=] {
						box->closeBox();
						ShowToneAddedToast(show, session, tone);
					}),
					crl::guard(box, [=](const MTP::Error &error) {
						if (error.type() == u"TONES_SAVED_TOO_MANY"_q) {
							ShowAiComposeToneLimitError(show, session);
						} else if (!MTP::IgnoreError(error)) {
							box->showToast(error.type());
						}
					}));
			});
		add->setFullRadius(true);
	}
}
