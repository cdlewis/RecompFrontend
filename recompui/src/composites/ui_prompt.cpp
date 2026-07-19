#include <mutex>

#include "recompui.h"

#include "elements/ui_element.h"
#include "elements/ui_document.h"
#include "elements/ui_label.h"
#include "elements/ui_button.h"
#include "elements/ui_frontend_theme.h"

struct {
    recompui::ContextId ui_context = recompui::ContextId::null();
    recompui::Element* prompt_overlay = nullptr;
    recompui::Element* prompt_content = nullptr;
    recompui::Label* prompt_header = nullptr;
    recompui::Label* prompt_label = nullptr;
    recompui::Element* prompt_controls = nullptr;
    recompui::Button* confirm_button = nullptr;
    recompui::Button* cancel_button = nullptr;
    std::function<void()> confirm_action;
    std::function<void()> cancel_action;
    std::mutex mutex;
} prompt_state;

void run_confirm_callback() {
    std::function<void()> confirm_action;
    {
        std::lock_guard lock{ prompt_state.mutex };
        confirm_action = std::move(prompt_state.confirm_action);
    }
    if (confirm_action) {
        confirm_action();
    }
    recompui::hide_context(prompt_state.ui_context);
}

void run_cancel_callback() {
    std::function<void()> cancel_action;
    {
        std::lock_guard lock{ prompt_state.mutex };
        cancel_action = std::move(prompt_state.cancel_action);
    }
    if (cancel_action) {
        cancel_action();
    }
    recompui::hide_context(prompt_state.ui_context);

    // TODO nav: focus on return_element_id
    // or just remove it as the usage of the prompt can change now
}

void apply_prompt_button_style(recompui::Button *button) {
    const auto &style = recompui::theme::get_frontend_theme().prompt.button;
    static_cast<recompui::Style&>(*button).apply_style(style.normal);
    button->get_hover_style()->apply_style(style.hover);
    button->get_focus_style()->apply_style(style.focus);
    button->get_disabled_style()->apply_style(style.disabled);
    button->set_border_width(recompui::theme::border::width);
    button->apply_styles();
}

void apply_prompt_theme() {
    if (prompt_state.prompt_overlay == nullptr) {
        return;
    }
    const auto &style = recompui::theme::get_frontend_theme().prompt;
    prompt_state.prompt_overlay->apply_style(style.overlay);
    prompt_state.prompt_content->apply_style(style.content);
    prompt_state.prompt_controls->apply_style(style.controls);
    apply_prompt_button_style(prompt_state.confirm_button);
    apply_prompt_button_style(prompt_state.cancel_button);
}

void recompui::init_prompt_context() {
    ContextId context = create_context();

    std::lock_guard lock{ prompt_state.mutex };

    context.open();

    prompt_state.ui_context = context;

    Element* window = context.create_element<Element>(context.get_root_element());
    window->set_display(Display::Flex);
    window->set_flex_direction(FlexDirection::Column);
    window->set_background_color(theme::color::Transparent);

    prompt_state.prompt_overlay = context.create_element<Element>(window);
    prompt_state.prompt_overlay->set_background_color(theme::color::BGOverlay);
    prompt_state.prompt_overlay->set_position(Position::Absolute);
    prompt_state.prompt_overlay->set_top(0);
    prompt_state.prompt_overlay->set_right(0);
    prompt_state.prompt_overlay->set_bottom(0);
    prompt_state.prompt_overlay->set_left(0);

    Element* prompt_content_wrapper = context.create_element<Element>(window);
    prompt_content_wrapper->set_display(Display::Flex);
    prompt_content_wrapper->set_position(Position::Absolute);
    prompt_content_wrapper->set_top(0);
    prompt_content_wrapper->set_right(0);
    prompt_content_wrapper->set_bottom(0);
    prompt_content_wrapper->set_left(0);
    prompt_content_wrapper->set_align_items(AlignItems::Center);
    prompt_content_wrapper->set_justify_content(JustifyContent::Center);

    prompt_state.prompt_content = context.create_element<Element>(prompt_content_wrapper);
    prompt_state.prompt_content->set_display(Display::Flex);
    prompt_state.prompt_content->set_position(Position::Relative);
    prompt_state.prompt_content->set_flex(1.0f, 1.0f);
    prompt_state.prompt_content->set_flex_basis(100, Unit::Percent);
    prompt_state.prompt_content->set_flex_direction(FlexDirection::Column);
    prompt_state.prompt_content->set_width(100, Unit::Percent);
    prompt_state.prompt_content->set_max_width(700, Unit::Dp);
    prompt_state.prompt_content->set_height_auto();
    prompt_state.prompt_content->set_margin_auto();
    prompt_state.prompt_content->set_border_width(theme::border::width, Unit::Dp);
    prompt_state.prompt_content->set_border_radius(theme::border::radius_lg, Unit::Dp);
    prompt_state.prompt_content->set_border_color(theme::color::WhiteA20);
    prompt_state.prompt_content->set_background_color(theme::color::ModalOverlay);
    prompt_state.prompt_content->set_overflow(Overflow::Hidden);
    
    prompt_state.prompt_header = context.create_element<Label>(prompt_state.prompt_content, "", LabelStyle::Large);
    prompt_state.prompt_header->set_margin(32, Unit::Dp);

    prompt_state.prompt_label = context.create_element<Label>(prompt_state.prompt_content, "", LabelStyle::Small);
    prompt_state.prompt_label->set_margin(32, Unit::Dp);
    prompt_state.prompt_label->set_margin_top(0);
    
    prompt_state.prompt_controls = context.create_element<Element>(prompt_state.prompt_content);
    
    prompt_state.prompt_controls->set_display(Display::Flex);
    prompt_state.prompt_controls->set_flex_direction(FlexDirection::Row);
    prompt_state.prompt_controls->set_as_navigation_container(NavigationType::Horizontal);
    prompt_state.prompt_controls->set_justify_content(JustifyContent::SpaceBetween);
    prompt_state.prompt_controls->set_align_items(AlignItems::Center);
    prompt_state.prompt_controls->set_padding(32, Unit::Dp);
    prompt_state.prompt_controls->set_border_top_width(theme::border::width, Unit::Dp);
    prompt_state.prompt_controls->set_border_top_color(theme::color::BorderSoft);
    prompt_state.prompt_controls->set_border_bottom_left_radius(theme::border::radius_lg, Unit::Dp);
    prompt_state.prompt_controls->set_border_bottom_right_radius(theme::border::radius_lg, Unit::Dp);

    prompt_state.confirm_button = context.create_element<Button>(prompt_state.prompt_controls, "", ButtonStyle::Success);
    prompt_state.confirm_button->set_min_width(185.0f, Unit::Dp);
    prompt_state.confirm_button->add_pressed_callback(run_confirm_callback);

    prompt_state.cancel_button = context.create_element<Button>(prompt_state.prompt_controls, "", ButtonStyle::Danger);
    prompt_state.cancel_button->set_min_width(185.0f, Unit::Dp);
    prompt_state.cancel_button->add_pressed_callback(run_cancel_callback);

    apply_prompt_theme();

    context.close();
}

// Must be called while prompt_state.mutex is locked.
void show_prompt(std::function<void()>& prev_cancel_action, bool focus_on_cancel) {
    if (focus_on_cancel) {
        prompt_state.ui_context.set_autofocus_element(prompt_state.cancel_button);
    }
    else {
        prompt_state.ui_context.set_autofocus_element(prompt_state.confirm_button);
    }

    if (!recompui::is_context_shown(prompt_state.ui_context)) {
        recompui::show_context(prompt_state.ui_context, "");
    }
    else {
        // Call the previous cancel action to effectively close the previous prompt.
        if (prev_cancel_action) {
            prev_cancel_action();
        }
    }
}

void recompui::open_choice_prompt(
    const std::string& header_text,
    const std::string& content_text,
    const std::string& confirm_label_text,
    const std::string& cancel_label_text,
    std::function<void()> confirm_action,
    std::function<void()> cancel_action,
    ButtonStyle confirm_variant,
    ButtonStyle cancel_variant,
    bool focus_on_cancel
) {
    std::lock_guard lock{ prompt_state.mutex };

    std::function<void()> prev_cancel_action = std::move(prompt_state.cancel_action);

    ContextId prev_context = try_close_current_context();

    prompt_state.ui_context.open();

    prompt_state.prompt_header->set_text(header_text);
    prompt_state.prompt_label->set_text(content_text);
    prompt_state.prompt_controls->set_display(Display::Flex);
    prompt_state.confirm_button->set_display(Display::Flex);
    prompt_state.cancel_button->set_display(Display::Flex);
    prompt_state.confirm_button->set_text(confirm_label_text);
    prompt_state.cancel_button->set_text(cancel_label_text);
    prompt_state.confirm_action = confirm_action;
    prompt_state.cancel_action = cancel_action;
    prompt_state.confirm_button->apply_button_style(confirm_variant);
    prompt_state.cancel_button->apply_button_style(cancel_variant);
    apply_prompt_button_style(prompt_state.confirm_button);
    apply_prompt_button_style(prompt_state.cancel_button);
    if (cancel_variant == ButtonStyle::Secondary) {
        const auto &secondary_style = theme::get_frontend_theme().prompt.secondary_button;
        if (secondary_style) {
            static_cast<Style&>(*prompt_state.cancel_button).apply_style(secondary_style->normal);
            prompt_state.cancel_button->get_hover_style()->apply_style(secondary_style->hover);
            prompt_state.cancel_button->get_focus_style()->apply_style(secondary_style->focus);
            prompt_state.cancel_button->get_disabled_style()->apply_style(secondary_style->disabled);
        } else {
            prompt_state.cancel_button->apply_button_style(cancel_variant);
        }
        prompt_state.cancel_button->apply_styles();
    }

    prompt_state.ui_context.close();

    if (prev_context != ContextId::null()) {
        prev_context.open();
    }

    show_prompt(prev_cancel_action, focus_on_cancel);
}

void recompui::open_info_prompt(
    const std::string& header_text,
    const std::string& content_text,
    const std::string& okay_label_text,
    std::function<void()> okay_action,
    ButtonStyle okay_variant
) {
    std::lock_guard lock{ prompt_state.mutex };

    std::function<void()> prev_cancel_action = std::move(prompt_state.cancel_action);

    ContextId prev_context = try_close_current_context();

    prompt_state.ui_context.open();

    prompt_state.prompt_header->set_text(header_text);
    prompt_state.prompt_label->set_text(content_text);
    prompt_state.prompt_controls->set_display(Display::Flex);
    prompt_state.confirm_button->set_display(Display::None);
    prompt_state.cancel_button->set_display(Display::Flex);
    prompt_state.cancel_button->set_text(okay_label_text);
    prompt_state.confirm_action = {};
    prompt_state.cancel_action = okay_action;

    prompt_state.cancel_button->apply_button_style(okay_variant);
    apply_prompt_button_style(prompt_state.cancel_button);

    prompt_state.ui_context.close();

    if (prev_context != ContextId::null()) {
        prev_context.open();
    }

    show_prompt(prev_cancel_action, true);
}

void recompui::open_notification(
    const std::string& header_text,
    const std::string& content_text
) {
    std::lock_guard lock{ prompt_state.mutex };

    std::function<void()> prev_cancel_action = std::move(prompt_state.cancel_action);

    ContextId prev_context = try_close_current_context();

    prompt_state.ui_context.open();

    prompt_state.prompt_header->set_text(header_text);
    prompt_state.prompt_label->set_text(content_text);
    prompt_state.prompt_controls->set_display(Display::None);
    prompt_state.confirm_button->set_display(Display::None);
    prompt_state.cancel_button->set_display(Display::None);
    prompt_state.confirm_action = {};
    prompt_state.cancel_action = {};

    prompt_state.ui_context.close();

    if (prev_context != ContextId::null()) {
        prev_context.open();
    }

    show_prompt(prev_cancel_action, false);
}

void recompui::close_prompt() {
    std::lock_guard lock{ prompt_state.mutex };

    if (recompui::is_context_shown(prompt_state.ui_context)) {
        if (prompt_state.cancel_action) {
            prompt_state.cancel_action();
        }

        recompui::hide_context(prompt_state.ui_context);
    }
}

bool recompui::is_prompt_open() {
    std::lock_guard lock{ prompt_state.mutex };

	return recompui::is_context_shown(prompt_state.ui_context);
}
