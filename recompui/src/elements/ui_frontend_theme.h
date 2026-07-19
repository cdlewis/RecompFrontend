#pragma once

#include <array>
#include <optional>

#include "ui_style.h"

namespace recompui::theme {

struct InteractiveStyle {
    Style normal;
    Style hover;
    Style focus;
    Style disabled;
};

struct TabTheme {
    Style normal;
    Style hover;
    Style selected;
    Style pulsing;
    Style indicator;
    Color indicator_color = get_theme_color(color::BorderSolid);
};

enum class TogglePreset {
    Default,
    Filled,
};

struct FrontendTheme {
    std::array<Color, static_cast<std::size_t>(color::size)> colors{};
    float border_radius_sm = border::radius_sm;
    float border_radius_md = border::radius_md;
    float border_radius_lg = border::radius_lg;
    float border_width = border::width;
    std::optional<ButtonStylePalette> primary_button;
    std::optional<ButtonStylePalette> secondary_button;
    struct {
        Style overlay;
        Style frame;
        Style header;
        Style body;
        std::optional<TabTheme> tabs;
    } modal;
    struct {
        Style overlay;
        Style content;
        Style controls;
        InteractiveStyle button;
        std::optional<InteractiveStyle> secondary_button;
    } prompt;
    struct {
        InteractiveStyle binding_button;
        TogglePreset input_device_toggle = TogglePreset::Default;
    } controls;
    struct {
        Style page;
        Style content;
        Style heading;
        Style description;
        Color card_border = get_theme_color(color::BorderSoft);
        Color card_label = get_theme_color(color::TextDim);
        Color card_waiting = get_theme_color(color::TextDim);
        Color card_active = get_theme_color(color::PrimaryL);
        Color card_assigned = get_theme_color(color::Text);
        Color card_assigned_background = get_theme_color(color::PrimaryA20);
        InteractiveStyle button;
    } in_game_player_assignment;
    struct {
        Style details;
        TogglePreset enable_toggle = TogglePreset::Default;
    } mods;
};

FrontendTheme make_default_frontend_theme();
void set_frontend_theme(const FrontendTheme& frontend_theme);
const FrontendTheme& get_frontend_theme();

} // namespace recompui::theme
