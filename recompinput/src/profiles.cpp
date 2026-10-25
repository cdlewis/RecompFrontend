#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
#include "recompinput/profiles.h"
#include "./json.h"
#include "xxHash/xxh3.h"

// Arrays that hold the mappings for every input for keyboard and controller respectively.
using input_mapping = std::array<recompinput::InputField, recompinput::num_bindings_per_input>;
using input_mapping_array = std::array<input_mapping, static_cast<size_t>(recompinput::GameInput::COUNT)>;

struct InputProfile {
    std::string key;
    std::string name;
    recompinput::InputDevice device;
    input_mapping_array mappings;
    bool custom = false;
};

static std::vector<InputProfile> input_profiles{};
static std::array<std::pair<int, int>, recompinput::max_num_players_supported> players_input_profile_indices = [] {
    std::array<std::pair<int, int>, recompinput::max_num_players_supported> result;
    result.fill({ -1, -1 });
    return result;
}();
static std::array<recompinput::InputDevice, recompinput::max_num_players_supported> players_assigned_devices = [] {
    std::array<recompinput::InputDevice, recompinput::max_num_players_supported> result;
    result.fill(recompinput::InputDevice::COUNT);
    return result;
}();
static std::unordered_map<std::string, int> input_profile_key_index_map{};
static std::vector<int> input_profile_custom_indices[static_cast<size_t>(recompinput::InputDevice::COUNT)]{};


struct Controller {
    recompinput::ControllerGUID guid;
    int profile_index;
};

static std::vector<Controller> controllers;
static std::unordered_map<uint64_t, int> controller_hash_index_map{};

static int keyboard_sp_profile_index = -1;
static int controller_sp_profile_index = -1;

static const std::string keyboard_sp_profile_key = "keyboard_sp";
static const std::string controller_sp_profile_key = "controller_sp";
static const std::string keyboard_sp_profile_name = "Keyboard (SP)";
static const std::string controller_sp_profile_name = "Controller (SP)";

static const std::string keyboard_mp_profile_key = "keyboard_mp_player_"; // + player index
static const std::string keyboard_mp_profile_name = "Keyboard "; // + "(player number)"
static std::filesystem::path controls_config_path;

// Make the button value array, which maps a button index to its bit field.
#define DEFINE_INPUT(name, value, readable) uint16_t(value##u),
static const std::array n64_button_values = {
    DEFINE_N64_BUTTON_INPUTS()
};
#undef DEFINE_INPUT

using json = nlohmann::json;

namespace recompinput {
    std::recursive_mutex& get_input_mutex() {
        static std::recursive_mutex mutex;
        return mutex;
    }

    void ControllerGUID::recompute_hash() {
        XXH3_state_t state;
        XXH3_64bits_reset(&state);
        XXH3_64bits_update(&state, &vendor, sizeof(vendor));
        XXH3_64bits_update(&state, &product, sizeof(product));
        XXH3_64bits_update(&state, &version, sizeof(version));
        XXH3_64bits_update(&state, &crc16, sizeof(crc16));
        XXH3_64bits_update(&state, serial.data(), serial.size());
        hash = XXH3_64bits_digest(&state);
    }

    // Due to an RmlUi limitation this can't be const. Ideally it would return a const reference or even just a straight up copy.
    recompinput::InputField& profiles::get_input_binding(int profile_index, GameInput input, size_t binding_index) {
        input_mapping& cur_input_mapping = input_profiles[profile_index].mappings.at(static_cast<size_t>(input));
        if (binding_index < cur_input_mapping.size()) {
            return cur_input_mapping[binding_index];
        }
        else {
            static recompinput::InputField dummy_field = {};
            return dummy_field;
        }
    }

    void profiles::set_input_binding(int profile_index, GameInput input, size_t binding_index, InputField value) {
        std::lock_guard lock{ get_input_mutex() };
        input_mapping& cur_input_mapping = input_profiles[profile_index].mappings.at(static_cast<size_t>(input));
        if (binding_index < cur_input_mapping.size()) {
            cur_input_mapping[binding_index] = value;
        }
    }

    void profiles::clear_input_binding(int profile_index, GameInput input) {
        std::lock_guard lock{ get_input_mutex() };
        for (size_t binding_index = 0; binding_index < recompinput::num_bindings_per_input; binding_index++) {
            profiles::set_input_binding(profile_index, input, binding_index, InputField{});
        }
    }

    void profiles::reset_input_binding(int profile_index, InputDevice device, GameInput input) {
        std::lock_guard lock{ get_input_mutex() };
        std::vector<InputField> new_mappings = recompinput::get_default_mapping_for_input(device, input);
        for (size_t binding_index = 0; binding_index < recompinput::num_bindings_per_input; binding_index++) {
            if (binding_index >= new_mappings.size()) {
                profiles::set_input_binding(profile_index, input, binding_index, InputField{});
            } else {
                profiles::set_input_binding(profile_index, input, binding_index, new_mappings[binding_index]);
            }
        }
    }

    static int get_mp_keyboard_profile_index(int player_index) {
        return profiles::get_input_profile_by_key(profiles::get_mp_keyboard_profile_key(player_index));
    }

    static int get_mp_keyboard_player_index(int profile_index) {
        for (int player_index = 0; player_index < recompinput::max_num_players_supported; player_index++) {
            if (get_mp_keyboard_profile_index(player_index) == profile_index) {
                return player_index;
            }
        }
        return -1;
    }

    void profiles::reset_profile_bindings(int profile_index, InputDevice device) {
        std::lock_guard lock{ get_input_mutex() };
        const bool clear_bindings = device == InputDevice::Keyboard && get_mp_keyboard_player_index(profile_index) > 0;
        for (size_t i = 0; i < recompinput::num_game_inputs; i++) {
            GameInput input = static_cast<GameInput>(i);
            if (clear_bindings) {
                profiles::clear_input_binding(profile_index, input);
                continue;
            }

            auto &new_mappings = recompinput::get_default_mapping_for_input(device, input);
            for (size_t binding_index = 0; binding_index < recompinput::num_bindings_per_input; binding_index++) {
                if (binding_index >= new_mappings.size()) {
                    profiles::set_input_binding(profile_index, input, binding_index, InputField{});
                } else {
                    profiles::set_input_binding(profile_index, input, binding_index, new_mappings[binding_index]);
                }
            }
        }
    }

    int profiles::add_input_profile(const std::string &key, const std::string &name, InputDevice device, bool custom) {
        std::lock_guard lock{ get_input_mutex() };
        auto it = input_profile_key_index_map.find(key);
        if (it != input_profile_key_index_map.end()) {
            return it->second;
        }

        int index = (int)(input_profiles.size());
        InputProfile profile{};
        profile.key = key;
        profile.name = name;
        profile.device = device;
        profile.custom = custom;
        input_profiles.emplace_back(profile);
        input_profile_key_index_map.emplace(key, index);

        if (custom) {
            switch (device) {
            case InputDevice::Controller:
                input_profile_custom_indices[0].emplace_back(index);
                break;
            case InputDevice::Keyboard:
                input_profile_custom_indices[1].emplace_back(index);
                break;
            default:
                assert(false && "Unknown input device.");
                break;
            }
        }

        return index;
    }

    int profiles::get_input_profile_by_key(const std::string &key) {
        auto it = input_profile_key_index_map.find(key);
        if (it != input_profile_key_index_map.end()) {
            return it->second;
        }
        else {
            return -1;
        }
    }

    const std::string &profiles::get_input_profile_key(int profile_index) {
        return input_profiles[profile_index].key;
    }

    const std::string &profiles::get_input_profile_name(int profile_index) {
        return input_profiles[profile_index].name;
    }

    InputDevice profiles::get_input_profile_device(int profile_index) {
        return input_profiles[profile_index].device;
    }

    bool profiles::is_input_profile_custom(int profile_index) {
        return input_profiles[profile_index].custom;
    }

    int profiles::get_input_profile_count() {
        return (int)(input_profiles.size());
    }

    const std::vector<int> profiles::get_indices_for_custom_profiles(InputDevice device) {
        static std::vector<int> empty;

        switch (device) {
        case InputDevice::Controller:
            return input_profile_custom_indices[0];
        case InputDevice::Keyboard:
            return input_profile_custom_indices[1];
        default:
            assert(false && "Unknown input device.");
            return empty;
        }
    }

    void profiles::set_input_profile_for_player(int player_index, int profile_index, InputDevice device) {
        std::lock_guard lock{ get_input_mutex() };
        switch (device) {
        case InputDevice::Controller:
            players_input_profile_indices[player_index].first = profile_index;
            break;
        case InputDevice::Keyboard:
            players_input_profile_indices[player_index].second = profile_index;
            break;
        default:
            assert(false && "Unknown input device.");
            break;
        }
    }

    int profiles::get_input_profile_for_player(int player_index, InputDevice device) {
        switch (device) {
        case InputDevice::Controller:
            return players_input_profile_indices[player_index].first;
        case InputDevice::Keyboard:
            return players_input_profile_indices[player_index].second;
        default:
            assert(false && "Unknown input device.");
            return -1;
        }
    }

    void profiles::clear_player_assignments() {
        std::lock_guard lock{ get_input_mutex() };
        players_assigned_devices.fill(InputDevice::COUNT);
    }

    void profiles::set_assigned_input_device_for_player(int player_index, InputDevice device) {
        std::lock_guard lock{ get_input_mutex() };
        if (player_index >= 0 && player_index < max_num_players_supported) {
            players_assigned_devices[player_index] = device;
        }
    }

    InputDevice profiles::get_assigned_input_device_for_player(int player_index) {
        if (player_index >= 0 && player_index < max_num_players_supported) {
            return players_assigned_devices[player_index];
        }
        return InputDevice::COUNT;
    }

    static void assign_default_player_one_if_unassigned() {
        if (profiles::get_assigned_input_device_for_player(0) != InputDevice::COUNT) {
            return;
        }

        profiles::set_assigned_input_device_for_player(0, InputDevice::Keyboard);
        profiles::set_input_profile_for_player(0, profiles::get_sp_keyboard_profile_index(), InputDevice::Keyboard);
    }

    int profiles::add_controller(ControllerGUID guid, int profile_index) {
        std::lock_guard lock{ get_input_mutex() };
        auto it = controller_hash_index_map.find(guid.hash);
        if (it != controller_hash_index_map.end()) {
            controllers[it->second].profile_index = profile_index;
            return it->second;
        }

        int index = (int)(controllers.size());
        Controller controller;
        controller.guid = guid;
        controller.profile_index = profile_index;
        controllers.emplace_back(controller);
        controller_hash_index_map.emplace(guid.hash, index);
        return index;
    }

    const ControllerGUID &profiles::get_controller_guid(int controller_index) {
        return controllers[controller_index].guid;
    }

    int profiles::get_controller_profile_index(int controller_index) {
        return controllers[controller_index].profile_index;
    }

    int profiles::get_controller_by_guid(ControllerGUID guid) {
        auto it = controller_hash_index_map.find(guid.hash);
        if (it != controller_hash_index_map.end()) {
            return it->second;
        }
        else {
            return -1;
        }
    }

    int profiles::get_controller_count() {
        return (int)(controllers.size());
    }

    ControllerGUID profiles::get_guid_from_sdl_controller(SDL_GameController *game_controller) {
        if (game_controller == nullptr) {
            return {};
        }

        SDL_Joystick *joystick = SDL_GameControllerGetJoystick(game_controller);
        if (joystick == nullptr) {
            return {};
        }

        Uint16 vendor, product, version, crc16;
        const char *joystick_serial = SDL_JoystickGetSerial(joystick);
        SDL_JoystickGUID joystick_guid = SDL_JoystickGetGUID(joystick);
        SDL_GetJoystickGUIDInfo(joystick_guid, &vendor, &product, &version, &crc16);

        ControllerGUID guid;
        guid.serial = joystick_serial != nullptr ? joystick_serial : "";
        guid.vendor = vendor;
        guid.product = product;
        guid.version = version;
        guid.crc16 = crc16;

        guid.recompute_hash();

        return guid;
    }

    int profiles::get_controller_profile_index_from_sdl_controller(SDL_GameController *game_controller) {
        if (game_controller == nullptr) {
            return -1;
        }

        ControllerGUID guid = profiles::get_guid_from_sdl_controller(game_controller);
        if (guid.hash == 0) {
            return -1;
        }

        int controller_index = profiles::get_controller_by_guid(guid);
        if (controller_index < 0) {
            return -1;
        }

        return profiles::get_controller_profile_index(controller_index);
    }

    int profiles::get_controller_profile_index_for_menu(SDL_GameController *game_controller) {
        if (game_controller == nullptr) {
            return -1;
        }

        if (players::is_single_player_mode()) {
            return profiles::get_sp_controller_profile_index();
        }

        for (size_t i = 0; i < players::get_configured_max_number_of_players(); i++) {
            if (players::get_player_is_assigned(i) && players::get_player(i).controller == game_controller) {
                int profile_index = profiles::get_input_profile_for_player(i, InputDevice::Controller);
                if (profile_index >= 0) {
                    return profile_index;
                }
                break;
            }
        }

        int profile_index = profiles::get_controller_profile_index_from_sdl_controller(game_controller);
        return profile_index >= 0 ? profile_index : profiles::get_sp_controller_profile_index();
    }

    std::string profiles::get_mp_keyboard_profile_key(int player_index) {
        return keyboard_mp_profile_key + std::to_string(player_index);
    }

    std::string profiles::get_mp_keyboard_profile_name(int player_index) {
        return keyboard_mp_profile_name + "(Player " + std::to_string(player_index + 1) + ")";
    }

    void profiles::initialize_input_bindings() {
        std::lock_guard lock{ get_input_mutex() };
        keyboard_sp_profile_index = profiles::add_input_profile(keyboard_sp_profile_key, keyboard_sp_profile_name, recompinput::InputDevice::Keyboard, false);
        controller_sp_profile_index = profiles::add_input_profile(controller_sp_profile_key, controller_sp_profile_name, recompinput::InputDevice::Controller, false);

        // Set Player 1 to the SP profiles by default.
        profiles::set_input_profile_for_player(0, keyboard_sp_profile_index, recompinput::InputDevice::Keyboard);
        profiles::set_input_profile_for_player(0, controller_sp_profile_index, recompinput::InputDevice::Controller);
    }

    int profiles::get_sp_controller_profile_index() {
        return controller_sp_profile_index;
    }

    int profiles::get_sp_keyboard_profile_index() {
        return keyboard_sp_profile_index;
    }

    int profiles::get_or_create_mp_keyboard_profile_index(int player_index) {
        std::lock_guard lock{ get_input_mutex() };
        int profile_index = get_mp_keyboard_profile_index(player_index);
        if (profile_index < 0) {
            profile_index = profiles::add_input_profile(
                get_mp_keyboard_profile_key(player_index),
                get_mp_keyboard_profile_name(player_index),
                InputDevice::Keyboard,
                false
            );
            if (player_index == 0) {
                profiles::reset_profile_bindings(profile_index, InputDevice::Keyboard);
            }
        }
        return profile_index;
    }

    std::string profiles::get_string_from_controller_guid(ControllerGUID guid) {
        return "SERIAL_" + guid.serial + "_VID_" + std::to_string(guid.vendor) + "_PID_" + std::to_string(guid.product) +"_VERSION_" + std::to_string(guid.version) +"_CRC16_" + std::to_string(guid.crc16);
    }

    bool profiles::get_n64_input(int player_index, uint16_t* buttons_out, float* x_out, float* y_out) {
        uint16_t cur_buttons = 0;
        float cur_x = 0.0f;
        float cur_y = 0.0f;
        if (!recompinput::game_input_disabled()) {
            auto check_buttons = [&](int profile_index) {
                if (profile_index < 0) {
                    return;
                }

                const input_mapping_array &mappings = input_profiles[profile_index].mappings;
                for (size_t i = 0; i < n64_button_values.size(); i++) {
                    cur_buttons |= recompinput::get_input_digital(player_index, mappings[(size_t)(GameInput::N64_BUTTON_START) + i]) ? n64_button_values[i] : 0;
                }
            };

            auto check_joystick = [&](int profile_index) {
                if (profile_index < 0) {
                    return;
                }

                const input_mapping_array &mappings = input_profiles[profile_index].mappings;
                cur_x += recompinput::get_input_analog(player_index, mappings[(size_t)GameInput::X_AXIS_POS]) - recompinput::get_input_analog(player_index, mappings[(size_t)GameInput::X_AXIS_NEG]);
                cur_y += recompinput::get_input_analog(player_index, mappings[(size_t)GameInput::Y_AXIS_POS]) - recompinput::get_input_analog(player_index, mappings[(size_t)GameInput::Y_AXIS_NEG]);
            };

            if (players::is_single_player_mode()) {
                int profile_index_cont = profiles::get_sp_controller_profile_index();
                int profile_index_kb = profiles::get_sp_keyboard_profile_index();
                check_buttons(profile_index_cont);
                check_buttons(profile_index_kb);

                check_joystick(profile_index_cont);
                recompinput::apply_joystick_deadzone(cur_x, cur_y, &cur_x, &cur_y);
                check_joystick(profile_index_kb);
            } else if (players::get_player_is_assigned(player_index)) {
                switch (players::get_player_input_device(player_index)) {
                case InputDevice::Controller: {
                    int profile_index = players_input_profile_indices[player_index].first;
                    check_buttons(profile_index);
                    check_joystick(profile_index);
                    recompinput::apply_joystick_deadzone(cur_x, cur_y, &cur_x, &cur_y);
                    break;
                }
                case InputDevice::Keyboard: {
                    int profile_index = players_input_profile_indices[player_index].second;
                    check_buttons(profile_index);
                    check_joystick(profile_index);
                    break;
                }
                default:
                    break;
                }
            }
        }

        *buttons_out = cur_buttons;
        *x_out = std::clamp(cur_x, -1.0f, 1.0f);
        *y_out = std::clamp(cur_y, -1.0f, 1.0f);

        return true;
    }

    static void add_input_bindings(json& out, int profile_index, GameInput input) {
        const std::string& input_name = recompinput::get_game_input_enum_name(input);
        json& out_array = out[input_name];
        out_array = json::array();
        for (size_t binding_index = 0; binding_index < recompinput::num_bindings_per_input; binding_index++) {
            out_array[binding_index] = profiles::get_input_binding(profile_index, input, binding_index);
        }
    }

    bool profiles::save_controls_config(const std::filesystem::path& path) {
        std::lock_guard lock{ get_input_mutex() };
        controls_config_path = path;
        int profile_count = profiles::get_input_profile_count();
        int controller_count = profiles::get_controller_count();
        json config_json{};
        config_json["version"] = profiles::controls_config_version;
        config_json["profiles"] = std::vector<json>(profile_count);
        config_json["controllers"] = std::vector<json>(controller_count);

        json &profiles = config_json["profiles"];
        for (int i = 0; i < profile_count; i++) {
            json &profile = profiles[i];
            profile["key"] = profiles::get_input_profile_key(i);
            profile["name"] = profiles::get_input_profile_name(i);
            profile["device"] = profiles::get_input_profile_device(i);
            profile["custom"] = profiles::is_input_profile_custom(i);
            profile["mappings"] = json();

            for (int j = 0; j < (int)(GameInput::COUNT); j++) {
                add_input_bindings(profile["mappings"], i, static_cast<GameInput>(j));
            }
        }

        json &controllers = config_json["controllers"];
        for (int i = 0; i < controller_count; i++) {
            json &controller = controllers[i];
            controller["guid"] = profiles::get_controller_guid(i);
            controller["profile"] = profiles::get_input_profile_key(profiles::get_controller_profile_index(i));
        }

        json& assignments = config_json["player_assignments"];
        assignments = json::array();
        for (size_t i = 0; i < players::get_configured_max_number_of_players(); i++) {
            json assignment;
            InputDevice device = profiles::get_assigned_input_device_for_player(static_cast<int>(i));
            assignment["device"] = device;
            if (device != InputDevice::COUNT) {
                int profile_index = profiles::get_input_profile_for_player(static_cast<int>(i), device);
                if (profile_index >= 0) {
                    assignment["profile"] = profiles::get_input_profile_key(profile_index);
                }
            }
            assignments.push_back(std::move(assignment));
        }

        return recompinput::save_json_with_backups(path, config_json);
    }

    bool profiles::save_controls_config() {
        std::lock_guard lock{ get_input_mutex() };
        return !controls_config_path.empty() && profiles::save_controls_config(controls_config_path);
    }

    static void assign_mapping(int profile_index, GameInput input, const std::vector<InputField>& value) {
        for (size_t binding_index = 0; binding_index < std::min(value.size(), recompinput::num_bindings_per_input); binding_index++) {
            profiles::set_input_binding(profile_index, input, binding_index, value[binding_index]);
        }
    }

    // same as assign_mapping, except will clear unassigned bindings if not in value
    static void assign_mapping_complete(int profile_index, GameInput input, const std::vector<InputField>& value) {
        for (size_t binding_index = 0; binding_index < recompinput::num_bindings_per_input; binding_index++) {
            if (binding_index >= value.size()) {
                profiles::set_input_binding(profile_index, input, binding_index, InputField{});
            } else {
                profiles::set_input_binding(profile_index, input, binding_index, value[binding_index]);
            }
        }
    }

    static void assign_all_mappings(int profile_index, InputDevice device) {
        for (size_t i = 0; i < recompinput::num_game_inputs; i++) {
            GameInput cur_input = static_cast<GameInput>(i);
            assign_mapping_complete(profile_index, cur_input, recompinput::get_default_mapping_for_input(device, cur_input));
        }
    }

    static bool load_input_device_from_json(const json& config_json, int profile_index, InputDevice device, const std::string& key) {
        // Check if the json object for the given key exists.
        auto find_it = config_json.find(key);
        if (find_it == config_json.end()) {
            return false;
        }

        const json& mappings_json = *find_it;

        for (size_t i = 0; i < recompinput::num_game_inputs; i++) {
            GameInput cur_input = static_cast<GameInput>(i);
            const std::string& input_name = recompinput::get_game_input_enum_name(cur_input);

            // Check if the json object for the given input exists and that it's an array.
            auto find_input_it = mappings_json.find(input_name);
            if (find_input_it == mappings_json.end() || !find_input_it->is_array()) {
                assign_mapping(
                    profile_index,
                    cur_input,
                    recompinput::get_default_mapping_for_input(device, cur_input)
                );
                continue;
            }
            const json& input_json = *find_input_it;

            // Deserialize all the bindings from the json array (up to the max number of bindings per input).
            for (size_t binding_index = 0; binding_index < std::min(recompinput::num_bindings_per_input, input_json.size()); binding_index++) {
                InputField cur_field{};
                recompinput::from_json(input_json[binding_index], cur_field);
                profiles::set_input_binding(profile_index, cur_input, binding_index, cur_field);
            }
        }

        return true;
    }

    bool profiles::load_controls_config(const std::filesystem::path& path) {
        std::lock_guard lock{ get_input_mutex() };
        controls_config_path = path;
        profiles::initialize_input_bindings();
        profiles::clear_player_assignments();

        json config_json{};
        if (!recompinput::read_json_with_backups(path, config_json)) {
            assign_all_mappings(profiles::get_sp_keyboard_profile_index(), InputDevice::Keyboard);
            assign_all_mappings(profiles::get_sp_controller_profile_index(), InputDevice::Controller);
            assign_default_player_one_if_unassigned();
            return false;
        }

        auto version_it = config_json.find("version");
        if (version_it != config_json.end()) {
            auto profiles = config_json.find("profiles");
            if (profiles == config_json.end() || !profiles->is_array()) {
                assign_default_player_one_if_unassigned();
                return false;
            }

            for (const json &profile : *profiles) {
                std::string key = profile.value("key", std::string());
                std::string name = profile.value("name", std::string());
                InputDevice device = profile.value("device", InputDevice::COUNT);
                bool custom = profile.value("custom", false);
                if (!key.empty() && !name.empty() && device != InputDevice::COUNT) {
                    int profile_index = profiles::add_input_profile(key, name, device, custom);
                    if (!load_input_device_from_json(profile, profile_index, device, "mappings")) {
                        assign_all_mappings(profile_index, device);
                    }
                }
            }

            auto controllers = config_json.find("controllers");
            if (controllers == config_json.end() || !controllers->is_array()) {
                assign_default_player_one_if_unassigned();
                return false;
            }

            for (const json &controller : *controllers) {
                auto guid = controller.find("guid");
                auto profile = controller.find("profile");
                if (guid != controller.end() && guid->is_object() && profile != controller.end() && profile->is_string()) {
                    int profile_index = profiles::get_input_profile_by_key(*profile);
                    if (profile_index >= 0) {
                        profiles::add_controller(*guid, profile_index);
                    }
                }
            }
        }
        else {
            // Version 1 of the format only had bindings for Player 1 on the root element.
            if (!load_input_device_from_json(config_json, profiles::get_sp_keyboard_profile_index(), InputDevice::Keyboard, "keyboard")) {
                assign_all_mappings(profiles::get_sp_keyboard_profile_index(), InputDevice::Keyboard);
            }

            if (!load_input_device_from_json(config_json, profiles::get_sp_controller_profile_index(), InputDevice::Controller, "controller")) {
                assign_all_mappings(profiles::get_sp_controller_profile_index(), InputDevice::Controller);
            }
        }

        auto assignments = config_json.find("player_assignments");
        if (assignments != config_json.end() && assignments->is_array()) {
            for (size_t player_index = 0;
                 player_index < std::min(assignments->size(), recompinput::max_num_players_supported); player_index++) {
                const json& assignment = (*assignments)[player_index];
                InputDevice device = assignment.value("device", InputDevice::COUNT);
                std::string profile_key = assignment.value("profile", std::string());
                int profile_index = profiles::get_input_profile_by_key(profile_key);

                if (device != InputDevice::Keyboard && device != InputDevice::Controller) {
                    continue;
                }

                if (profile_index < 0) {
                    profile_index =
                        device == InputDevice::Controller
                            ? profiles::get_sp_controller_profile_index()
                            : profiles::get_or_create_mp_keyboard_profile_index(static_cast<int>(player_index));
                }
                profiles::set_assigned_input_device_for_player(static_cast<int>(player_index), device);
                profiles::set_input_profile_for_player(static_cast<int>(player_index), profile_index, device);
            }
        }

        assign_default_player_one_if_unassigned();

        return true;
    }
}
