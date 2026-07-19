#include "ui_frontend_theme.h"

namespace {
recompui::theme::FrontendTheme make_theme_from_current_values() {
    recompui::theme::FrontendTheme result{};
    for (std::size_t i = 0; i < result.colors.size(); i++) {
        result.colors[i] = recompui::theme::get_theme_color(static_cast<recompui::theme::color>(i));
    }
    result.in_game_player_assignment.page.set_background_color(
        result.colors[static_cast<std::size_t>(recompui::theme::color::Background1)]);
    result.in_game_player_assignment.content.set_background_color(
        result.colors[static_cast<std::size_t>(recompui::theme::color::ModalOverlay)]);
    result.in_game_player_assignment.heading.set_color(
        result.colors[static_cast<std::size_t>(recompui::theme::color::Text)]);
    result.in_game_player_assignment.description.set_color(
        result.colors[static_cast<std::size_t>(recompui::theme::color::TextDim)]);
    return result;
}

recompui::theme::FrontendTheme& current_theme() {
    static recompui::theme::FrontendTheme value = make_theme_from_current_values();
    return value;
}
} // namespace

recompui::theme::FrontendTheme recompui::theme::make_default_frontend_theme() {
    return make_theme_from_current_values();
}

void recompui::theme::set_frontend_theme(const FrontendTheme& frontend_theme) {
    current_theme() = frontend_theme;
    for (std::size_t i = 0; i < current_theme().colors.size(); i++) {
        set_theme_color(static_cast<color>(i), current_theme().colors[i]);
    }
    set_border_radius_sm(current_theme().border_radius_sm);
    set_border_radius_md(current_theme().border_radius_md);
    set_border_radius_lg(current_theme().border_radius_lg);
    set_border_width(current_theme().border_width);
}

const recompui::theme::FrontendTheme& recompui::theme::get_frontend_theme() {
    return current_theme();
}
