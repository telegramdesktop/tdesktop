/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "mtproto/sender.h"
#include "ui/rp_widget.h"
#include "ui/effects/animations.h"

namespace Main {
class Account;
} // namespace Main;

namespace Ui {
class SlideAnimation;
class CrossFadeAnimation;
class FlatLabel;
template <typename Widget>
class FadeWrap;
} // namespace Ui

namespace Intro {
namespace details {

struct Data;
enum class StackAction;
enum class Animate;

class Step : public Ui::RpWidget {
public:
	Step(
		QWidget *parent,
		not_null<Main::Account*> account,
		not_null<Data*> data,
		bool hasCover = false);
	~Step();

	[[nodiscard]] Main::Account &account() const {
		return *_account;
	}

	// It should not be called in StartWidget, in other steps it should be
	// present and not changing.
	[[nodiscard]] MTP::Sender &api() const;
	void apiClear();

	virtual void finishInit() {
	}
	virtual void setInnerFocus() {
		setFocus();
	}

	void setGoCallback(
		Fn<void(Step *step, StackAction action, Animate animate)> callback);
	void setShowResetCallback(Fn<void()> callback);
	void setShowTermsCallback(Fn<void()> callback);
	void setCancelNearestDcCallback(Fn<void()> callback);
	void setAcceptTermsCallback(
		Fn<void(Fn<void()> callback)> callback);

	void prepareShowAnimated(Step *after);
	void showAnimated(Animate animate);
	void showFast();
	[[nodiscard]] bool animating() const;
	void setShowAnimationClipping(QRect clipping);

	[[nodiscard]] bool hasCover() const;
	[[nodiscard]] virtual bool hasBack() const;
	virtual void activate();
	virtual void cancelled();
	virtual void finished();

	virtual void submit() = 0;
	[[nodiscard]] virtual rpl::producer<QString> nextButtonText() const;

	[[nodiscard]] int contentLeft() const;
	[[nodiscard]] int contentTop() const;

	void setErrorCentered(bool centered);
	void showError(rpl::producer<QString> text);
	void hideError() {
		showError(rpl::single(QString()));
	}

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void setTitleText(rpl::producer<QString> titleText);
	void setDescriptionText(rpl::producer<QString> descriptionText);
	void setDescriptionText(
		rpl::producer<TextWithEntities> richDescriptionText);
	bool paintAnimated(Painter &p, QRect clip);

	void fillSentCodeData(const MTPDauth_sentCode &type);

	void showDescription();
	void hideDescription();

	[[nodiscard]] not_null<Data*> getData() const {
		return _data;
	}
	void finish(const MTPUser &user, QImage &&photo = QImage());
	void createSession(
		const MTPUser &user,
		QImage photo,
		const QVector<MTPDialogFilter> &filters);

	void goBack();

	template <typename StepType>
	void goNext() {
		goNext(new StepType(parentWidget(), _account, _data));
	}

	template <typename StepType>
	void goReplace(Animate animate) {
		goReplace(new StepType(parentWidget(), _account, _data), animate);
	}

	void showResetButton() {
		if (_showResetCallback) _showResetCallback();
	}
	void showTerms() {
		if (_showTermsCallback) _showTermsCallback();
	}
	void acceptTerms(Fn<void()> callback) {
		if (_acceptTermsCallback) {
			_acceptTermsCallback(callback);
		}
	}
	void cancelNearestDcRequest() {
		if (_cancelNearestDcCallback) _cancelNearestDcCallback();
	}

	virtual int errorTop() const;

private:
	struct CoverAnimation {
		CoverAnimation() = default;
		CoverAnimation(CoverAnimation &&other) = default;
		CoverAnimation &operator=(CoverAnimation &&other) = default;
		~CoverAnimation();

		std::unique_ptr<Ui::CrossFadeAnimation> title;
		std::unique_ptr<Ui::CrossFadeAnimation> description;

		// From content top till the next button top.
		QPixmap contentSnapshotWas;
		QPixmap contentSnapshotNow;

		QRect clipping;
	};
	void updateLabelsPosition();
	void paintContentSnapshot(
		Painter &p,
		const QPixmap &snapshot,
		float64 alpha,
		float64 howMuchHidden);
	void refreshError(const QString &text);

	void goNext(Step *step);
	void goReplace(Step *step, Animate animate);

	[[nodiscard]] CoverAnimation prepareCoverAnimation(Step *step);
	[[nodiscard]] QPixmap prepareContentSnapshot();
	[[nodiscard]] QPixmap prepareSlideAnimation();
	void showFinished();

	void prepareCoverMask();
	void paintCover(Painter &p, int top);

	const not_null<Main::Account*> _account;
	const not_null<Data*> _data;
	mutable std::optional<MTP::Sender> _api;

	bool _hasCover = false;
	Fn<void(Step *step, StackAction action, Animate animate)> _goCallback;
	Fn<void()> _showResetCallback;
	Fn<void()> _showTermsCallback;
	Fn<void()> _cancelNearestDcCallback;
	Fn<void(Fn<void()> callback)> _acceptTermsCallback;

	rpl::variable<QString> _titleText;
	object_ptr<Ui::FlatLabel> _title;
	rpl::variable<TextWithEntities> _descriptionText;
	object_ptr<Ui::FadeWrap<Ui::FlatLabel>> _description;

	bool _errorCentered = false;
	rpl::variable<QString> _errorText;
	object_ptr<Ui::FadeWrap<Ui::FlatLabel>> _error = { nullptr };

	Ui::Animations::Simple _a_show;
	CoverAnimation _coverAnimation;
	std::unique_ptr<Ui::SlideAnimation> _slideAnimation;
	QPixmap _coverMask;

};


} // namespace details
} // namespace Intro
