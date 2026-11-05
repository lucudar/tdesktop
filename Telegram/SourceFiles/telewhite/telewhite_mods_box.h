/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class GenericBox;
class RpWidget;
} // namespace Ui

namespace Telewhite::Preview {
enum class Scene;
} // namespace Telewhite::Preview

namespace Telewhite {

// "How it works" box, opened by the "?" button near the mod toggle.
void HowItWorksBox(
	not_null<Ui::GenericBox*> box,
	Preview::Scene scene,
	rpl::producer<bool> value,
	Fn<void(bool)> set);

// Minimal accent row for the main settings, opens Telewhite Mods on click.
[[nodiscard]] object_ptr<Ui::RpWidget> CreateModsBanner(
	not_null<QWidget*> parent,
	Fn<void()> open);

} // namespace Telewhite
