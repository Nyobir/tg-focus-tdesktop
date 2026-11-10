/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_focus.h"

#include "apiwrap.h"
#include "base/unixtime.h"
#include "boxes/peer_list_controllers.h"
#include "core/focus_mode.h"
#include "data/data_chat_filters.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_thread.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/business/settings_working_hours.h"
#include "settings/settings_common.h"
#include "settings/settings_common_session.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

using Core::Focus::Mode;
using FocusSettings = Core::Focus::Logic::Settings;

constexpr auto kDelayOptions = std::array{ 0, 1, 5, 10, 15, 30, 60 };
constexpr auto kStarsOptions = std::array{ 1, 10, 25, 50, 100, 250, 500 };

class FocusMode : public Section<FocusMode> {
public:
	FocusMode(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();
	void fill(not_null<Ui::VerticalLayout*> container);
	void fillStatus(not_null<Ui::VerticalLayout*> container);
	void fillWhen(not_null<Ui::VerticalLayout*> container);
	void fillChats(not_null<Ui::VerticalLayout*> container);
	void fillUnlock(not_null<Ui::VerticalLayout*> container);

};

[[nodiscard]] rpl::producer<FocusSettings> SettingsValue() {
	return rpl::single(rpl::empty) | rpl::then(
		Core::Focus::SettingsChanges()
	) | rpl::map([] {
		return Core::Focus::CurrentSettings();
	});
}

template <typename Type>
[[nodiscard]] rpl::producer<Type> SettingsField(Type FocusSettings::*field) {
	return SettingsValue() | rpl::map([=](const FocusSettings &settings) {
		return settings.*field;
	}) | rpl::distinct_until_changed();
}

void Update(
		not_null<Main::Session*> session,
		Fn<void(FocusSettings&)> change) {
	if (Core::Focus::Active()) {
		return;
	}
	const auto &list = session->data().chatsFilters().list();
	Core::Focus::UpdateSettings([&](FocusSettings &settings) {
		if (!settings.foldersInitialized) {
			for (const auto &filter : list) {
				if (filter.id()) {
					settings.folders[filter.id()] = Core::Focus::FolderMode(
						filter);
				}
			}
			settings.foldersInitialized = true;
		}
		change(settings);
	});
}

[[nodiscard]] QString ModeText(Mode mode) {
	switch (mode) {
	case Mode::Show: return tr::lng_focus_mode_show(tr::now);
	case Mode::Mute: return tr::lng_focus_mode_mute(tr::now);
	case Mode::Hide: return tr::lng_focus_mode_hide(tr::now);
	}
	Unexpected("Mode in ModeText.");
}

[[nodiscard]] QString DelayText(int minutes) {
	return minutes
		? tr::lng_focus_delay_minutes(
			tr::now,
			lt_minutes,
			QString::number(minutes))
		: tr::lng_focus_delay_none(tr::now);
}

[[nodiscard]] QString StarsText(int stars) {
	return QString::number(stars) + QString::fromUtf8(" \xE2\xAD\x90");
}

[[nodiscard]] QString DaysText(int days) {
	const auto names = std::array{
		&tr::lng_weekday1,
		&tr::lng_weekday2,
		&tr::lng_weekday3,
		&tr::lng_weekday4,
		&tr::lng_weekday5,
		&tr::lng_weekday6,
		&tr::lng_weekday7,
	};
	auto result = QStringList();
	for (auto i = 0; i != 7; ++i) {
		if (days & (1 << i)) {
			result.push_back((*names[i])(tr::now));
		}
	}
	return result.isEmpty()
		? tr::lng_focus_days_none(tr::now)
		: result.join(u", "_q);
}

void ChooseModeBox(
		not_null<Ui::GenericBox*> box,
		rpl::producer<QString> title,
		Mode current,
		Fn<void(Mode)> save) {
	const auto options = std::vector<QString>{
		ModeText(Mode::Show),
		ModeText(Mode::Mute),
		ModeText(Mode::Hide),
	};
	SingleChoiceBox(box, {
		.title = std::move(title),
		.options = options,
		.initialSelection = int(current),
		.callback = [=](int index) { save(Mode(index)); },
	});
}

template <size_t Size>
void ChooseValueBox(
		not_null<Ui::GenericBox*> box,
		rpl::producer<QString> title,
		const std::array<int, Size> &values,
		Fn<QString(int)> text,
		int current,
		Fn<void(int)> save) {
	auto options = std::vector<QString>();
	for (const auto value : values) {
		options.push_back(text(value));
	}
	const auto i = ranges::find(values, current);
	SingleChoiceBox(box, {
		.title = std::move(title),
		.options = options,
		.initialSelection = (i != end(values)) ? int(i - begin(values)) : 0,
		.callback = [=](int index) { save(values[index]); },
	});
}

void ChooseDaysBox(
		not_null<Ui::GenericBox*> box,
		int current,
		Fn<void(int)> save) {
	box->setTitle(tr::lng_focus_days());
	const auto names = std::array{
		&tr::lng_hours_monday,
		&tr::lng_hours_tuesday,
		&tr::lng_hours_wednesday,
		&tr::lng_hours_thursday,
		&tr::lng_hours_friday,
		&tr::lng_hours_saturday,
		&tr::lng_hours_sunday,
	};
	auto checks = std::vector<not_null<Ui::Checkbox*>>();
	for (auto i = 0; i != 7; ++i) {
		checks.push_back(box->addRow(
			object_ptr<Ui::Checkbox>(
				box,
				(*names[i])(tr::now),
				(current & (1 << i)) != 0,
				st::defaultBoxCheckbox),
			st::boxRowPadding + QMargins(0, 0, 0, st::boxOptionListSkip)));
	}
	box->addButton(tr::lng_settings_save(), [=] {
		auto days = 0;
		for (auto i = 0; i != 7; ++i) {
			if (checks[i]->checked()) {
				days |= (1 << i);
			}
		}
		save(days);
		box->closeBox();
	});
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

void ChooseStarsPeer(
		not_null<Window::SessionController*> controller,
		Fn<void(not_null<PeerData*>)> save) {
	const auto weak = std::make_shared<base::weak_qptr<Ui::BoxContent>>();
	auto chosen = [=](not_null<Data::Thread*> thread) {
		save(thread->peer());
		if (const auto strong = weak->get()) {
			strong->closeBox();
		}
	};
	auto filter = [](not_null<Data::Thread*> thread) {
		return thread->peer()->isChannel();
	};
	auto initBox = [](not_null<PeerListBox*> box) {
		box->setTitle(tr::lng_focus_stars_choose());
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	};
	*weak = controller->show(Box<PeerListBox>(
		std::make_unique<ChooseRecipientBoxController>(ChooseRecipientArgs{
			.session = &controller->session(),
			.callback = std::move(chosen),
			.filter = std::move(filter),
		}),
		std::move(initBox)));
}

[[nodiscard]] PeerData *StarsPeer(not_null<Main::Session*> session) {
	const auto id = Core::Focus::CurrentSettings().starsPeerId;
	return id
		? session->data().peerLoaded(PeerId(PeerIdHelper(id)))
		: nullptr;
}

void SendStars(
		not_null<Main::Session*> session,
		not_null<PeerData*> peer,
		int stars,
		Fn<void()> done,
		Fn<void()> fail) {
	const auto api = &session->api();
	api->request(MTPmessages_GetHistory(
		peer->input(),
		MTP_int(0), // offset_id
		MTP_int(0), // offset_date
		MTP_int(0), // add_offset
		MTP_int(1), // limit
		MTP_int(0), // max_id
		MTP_int(0), // min_id
		MTP_long(0) // hash
	)).done([=](const MTPmessages_Messages &result) {
		const auto messageId = result.match([](
				const MTPDmessages_messagesNotModified &) {
			return MsgId();
		}, [](const auto &data) {
			const auto &list = data.vmessages().v;
			return list.isEmpty() ? MsgId() : IdFromMessage(list.front());
		});
		if (!messageId) {
			fail();
			return;
		}
		api->request(MTPmessages_SendPaidReaction(
			MTP_flags(0),
			peer->input(),
			MTP_int(messageId.bare),
			MTP_int(stars),
			MTP_long(base::unixtime::mtproto_msg_id()),
			MTPPaidReactionPrivacy()
		)).done([=](const MTPUpdates &updates) {
			api->applyUpdates(updates);
			done();
		}).fail([=](const MTP::Error &error) {
			LOG(("Focus Error: paid reaction failed, %1.").arg(error.type()));
			fail();
		}).send();
	}).fail([=](const MTP::Error &error) {
		LOG(("Focus Error: could not get history, %1.").arg(error.type()));
		fail();
	}).send();
}

FocusMode::FocusMode(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> FocusMode::title() {
	return tr::lng_focus_title();
}

void FocusMode::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto session = &controller()->session();

	rpl::combine(
		Core::Focus::ActiveValue(),
		Core::Focus::StatusValue() | rpl::map([] {
			return Core::Focus::UnlockPending();
		}) | rpl::distinct_until_changed(),
		rpl::single(rpl::empty) | rpl::then(
			session->data().chatsFilters().changed())
	) | rpl::to_empty | rpl::on_next([=] {
		crl::on_main(content, [=] {
			content->clear();
			fill(content);
			content->resizeToWidth(width());
		});
	}, content->lifetime());

	Ui::ResizeFitChild(this, content);
}

void FocusMode::fill(not_null<Ui::VerticalLayout*> container) {
	fillStatus(container);
	if (Core::Focus::Active()) {
		return;
	}
	fillWhen(container);
	fillChats(container);
	fillUnlock(container);
}

void FocusMode::fillStatus(not_null<Ui::VerticalLayout*> container) {
	Ui::AddSkip(container);
	if (!Core::Focus::Active()) {
		AddButtonWithIcon(
			container,
			tr::lng_focus_start(),
			st::settingsButtonActive,
			{ &st::menuIconNightMode }
		)->setClickedCallback([] {
			Core::Focus::StartNow();
		});
	} else if (Core::Focus::UnlockPending()) {
		AddButtonWithIcon(
			container,
			Core::Focus::StatusValue(
			) | rpl::map([](const Core::Focus::Logic::Status &status) {
				return tr::lng_focus_status_unlocking(
					tr::now,
					lt_minutes,
					QString::number(status.minutesLeft));
			}),
			st::settingsButtonNoIconLocked);
	} else {
		AddButtonWithIcon(
			container,
			tr::lng_focus_stop(),
			st::settingsAttentionButtonWithIcon,
			{ &st::menuIconNightMode }
		)->setClickedCallback([=] {
			ShowFocusUnlockBox(controller());
		});
	}
	Ui::AddSkip(container);
	Ui::AddDividerText(
		container,
		(Core::Focus::Active()
			? tr::lng_focus_locked_about()
			: tr::lng_focus_about()));
}

void FocusMode::fillWhen(not_null<Ui::VerticalLayout*> container) {
	const auto session = &controller()->session();
	const auto show = controller()->uiShow();

	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, tr::lng_focus_when());

	const auto schedule = AddButtonWithIcon(
		container,
		tr::lng_focus_schedule(),
		st::settingsButton,
		{ &st::menuIconSchedule });
	schedule->toggleOn(SettingsField(&FocusSettings::scheduleEnabled));
	schedule->toggledChanges(
	) | rpl::on_next([=](bool enabled) {
		Update(session, [=](FocusSettings &s) { s.scheduleEnabled = enabled; });
	}, schedule->lifetime());

	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto inner = wrap->entity();
	AddButtonWithLabel(
		inner,
		tr::lng_focus_days(),
		SettingsField(&FocusSettings::scheduleDays) | rpl::map(DaysText),
		st::settingsButtonNoIcon
	)->setClickedCallback([=] {
		const auto current = Core::Focus::CurrentSettings().scheduleDays;
		show->show(Box(ChooseDaysBox, current, [=](int days) {
			Update(session, [=](FocusSettings &s) { s.scheduleDays = days; });
		}));
	});
	const auto addTime = [&](
			rpl::producer<QString> title,
			int FocusSettings::*field) {
		AddButtonWithLabel(
			inner,
			std::move(title),
			SettingsField(field) | rpl::map([](int minutes) {
				return FormatWorkingTime(minutes * 60);
			}),
			st::settingsButtonNoIcon
		)->setClickedCallback([=] {
			const auto now = Core::Focus::CurrentSettings().*field;
			show->show(Box(ChooseWorkingTimeBox, now * 60, [=](TimeId t) {
				Update(session, [=](FocusSettings &s) {
					s.*field = int(t / 60);
				});
			}));
		});
	};
	addTime(tr::lng_focus_from(), &FocusSettings::scheduleStart);
	addTime(tr::lng_focus_till(), &FocusSettings::scheduleEnd);
	wrap->toggleOn(SettingsField(&FocusSettings::scheduleEnabled));
	wrap->finishAnimating();

	const auto system = AddButtonWithIcon(
		container,
		tr::lng_focus_sync_system(),
		st::settingsButton,
		{ &st::menuIconNightMode });
	system->toggleOn(SettingsField(&FocusSettings::syncSystem));
	system->toggledChanges(
	) | rpl::on_next([=](bool enabled) {
		Update(session, [=](FocusSettings &s) { s.syncSystem = enabled; });
	}, system->lifetime());

	Ui::AddSkip(container);
	Ui::AddDividerText(
		container,
		tr::lng_focus_when_about(
			lt_command,
			rpl::single(Core::Focus::CommandPath())));
}

void FocusMode::fillChats(not_null<Ui::VerticalLayout*> container) {
	const auto session = &controller()->session();
	const auto show = controller()->uiShow();

	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, tr::lng_focus_chats());

	const auto privateChats = AddButtonWithIcon(
		container,
		tr::lng_focus_private(),
		st::settingsButtonNoIcon);
	privateChats->toggleOn(
		SettingsField(&FocusSettings::privateAlwaysShown));
	privateChats->toggledChanges(
	) | rpl::on_next([=](bool enabled) {
		Update(session, [=](FocusSettings &s) {
			s.privateAlwaysShown = enabled;
		});
	}, privateChats->lifetime());

	for (const auto &filter : session->data().chatsFilters().list()) {
		const auto id = filter.id();
		if (!id) {
			continue;
		}
		const auto title = filter.title().text.text;
		AddButtonWithLabel(
			container,
			rpl::single(title),
			SettingsValue() | rpl::map([=] {
				const auto &list = session->data().chatsFilters().list();
				const auto i = ranges::find(list, id, &Data::ChatFilter::id);
				return (i != end(list))
					? ModeText(Core::Focus::FolderMode(*i))
					: QString();
			}),
			st::settingsButtonNoIcon
		)->setClickedCallback([=] {
			const auto &list = session->data().chatsFilters().list();
			const auto i = ranges::find(list, id, &Data::ChatFilter::id);
			if (i == end(list)) {
				return;
			}
			const auto now = Core::Focus::FolderMode(*i);
			show->show(Box(ChooseModeBox, rpl::single(title), now, [=](Mode m) {
				Update(session, [=](FocusSettings &s) { s.folders[id] = m; });
			}));
		});
	}

	AddButtonWithLabel(
		container,
		tr::lng_focus_other_chats(),
		SettingsField(&FocusSettings::otherChats) | rpl::map(ModeText),
		st::settingsButtonNoIcon
	)->setClickedCallback([=] {
		const auto now = Core::Focus::CurrentSettings().otherChats;
		show->show(Box(
			ChooseModeBox,
			tr::lng_focus_other_chats(),
			now,
			[=](Mode m) {
				Update(session, [=](FocusSettings &s) { s.otherChats = m; });
			}));
	});

	Ui::AddSkip(container);
	Ui::AddDividerText(container, tr::lng_focus_chats_about());
}

void FocusMode::fillUnlock(not_null<Ui::VerticalLayout*> container) {
	const auto session = &controller()->session();
	const auto window = controller();
	const auto show = window->uiShow();

	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, tr::lng_focus_unlock());

	AddButtonWithLabel(
		container,
		tr::lng_focus_delay(),
		SettingsField(
			&FocusSettings::unlockDelayMinutes) | rpl::map(DelayText),
		st::settingsButton,
		{ &st::menuIconTimer }
	)->setClickedCallback([=] {
		const auto now = Core::Focus::CurrentSettings().unlockDelayMinutes;
		show->show(Box(
			ChooseValueBox<kDelayOptions.size()>,
			tr::lng_focus_delay(),
			kDelayOptions,
			DelayText,
			now,
			[=](int minutes) {
				Update(session, [=](FocusSettings &s) {
					s.unlockDelayMinutes = minutes;
				});
			}));
	});

	const auto stars = AddButtonWithIcon(
		container,
		tr::lng_focus_stars(),
		st::settingsButton,
		{ &st::menuIconEarn });
	stars->toggleOn(SettingsField(&FocusSettings::starsEnabled));
	stars->toggledChanges(
	) | rpl::on_next([=](bool enabled) {
		Update(session, [=](FocusSettings &s) { s.starsEnabled = enabled; });
	}, stars->lifetime());

	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto inner = wrap->entity();
	AddButtonWithLabel(
		inner,
		tr::lng_focus_stars_amount(),
		SettingsField(&FocusSettings::starsAmount) | rpl::map(StarsText),
		st::settingsButtonNoIcon
	)->setClickedCallback([=] {
		const auto now = Core::Focus::CurrentSettings().starsAmount;
		show->show(Box(
			ChooseValueBox<kStarsOptions.size()>,
			tr::lng_focus_stars_amount(),
			kStarsOptions,
			StarsText,
			now,
			[=](int amount) {
				Update(session, [=](FocusSettings &s) {
					s.starsAmount = amount;
				});
			}));
	});
	AddButtonWithLabel(
		inner,
		tr::lng_focus_stars_peer(),
		SettingsField(&FocusSettings::starsPeerId) | rpl::map([=](quint64) {
			const auto peer = StarsPeer(session);
			return peer
				? peer->name()
				: tr::lng_focus_stars_peer_none(tr::now);
		}),
		st::settingsButtonNoIcon
	)->setClickedCallback([=] {
		ChooseStarsPeer(window, [=](not_null<PeerData*> peer) {
			Update(session, [=](FocusSettings &s) {
				s.starsPeerId = peer->id.value;
			});
		});
	});
	wrap->toggleOn(SettingsField(&FocusSettings::starsEnabled));
	wrap->finishAnimating();

	Ui::AddSkip(container);
	Ui::AddDividerText(container, tr::lng_focus_unlock_about());
}

} // namespace

Type FocusModeId() {
	return FocusMode::Id();
}

void ShowFocusUnlockBox(not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	const auto &settings = Core::Focus::CurrentSettings();
	const auto delay = settings.unlockDelayMinutes;
	const auto stars = settings.starsEnabled ? settings.starsAmount : 0;
	const auto peer = stars ? StarsPeer(session) : nullptr;
	const auto show = controller->uiShow();
	if (stars && !peer) {
		show->showToast(tr::lng_focus_stars_no_peer(tr::now));
		return;
	}
	const auto minutes = QString::number(delay);
	const auto starsText = QString::number(stars);
	const auto text = !stars
		? (delay
			? tr::lng_focus_unlock_delay(tr::now, lt_minutes, minutes)
			: tr::lng_focus_unlock_now(tr::now))
		: delay
		? tr::lng_focus_unlock_stars_delay(
			tr::now,
			lt_stars,
			starsText,
			lt_channel,
			peer->name(),
			lt_minutes,
			minutes)
		: tr::lng_focus_unlock_stars(
			tr::now,
			lt_stars,
			starsText,
			lt_channel,
			peer->name());
	const auto confirmText = stars
		? tr::lng_focus_unlock_pay(tr::now, lt_stars, starsText)
		: tr::lng_focus_unlock_confirm(tr::now);
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_focus_unlock_title());
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			text,
			st::boxLabel));
		const auto sending = std::make_shared<bool>(false);
		const auto weak = base::make_weak(box);
		box->addButton(rpl::single(confirmText), [=] {
			if (!stars) {
				Core::Focus::StartUnlock();
				box->closeBox();
				return;
			} else if (*sending) {
				return;
			}
			*sending = true;
			SendStars(session, peer, stars, [=] {
				Core::Focus::StartUnlock();
				if (const auto strong = weak.get()) {
					strong->closeBox();
				}
			}, crl::guard(box, [=] {
				*sending = false;
				show->showToast(tr::lng_focus_stars_failed(tr::now));
			}));
		}, st::attentionBoxButton);
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	}));
}

} // namespace Settings
