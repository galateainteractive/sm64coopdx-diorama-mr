#pragma once

#include <stdbool.h>
#include <stdint.h>

enum QuestPassthroughTheme {
    QUEST_PASSTHROUGH_THEME_NEUTRAL = 0,
    QUEST_PASSTHROUGH_THEME_GRASS,
    QUEST_PASSTHROUGH_THEME_WATER,
    QUEST_PASSTHROUGH_THEME_SNOW,
    QUEST_PASSTHROUGH_THEME_SKY,
    QUEST_PASSTHROUGH_THEME_FIRE,
    QUEST_PASSTHROUGH_THEME_CAVE,
    QUEST_PASSTHROUGH_THEME_DESERT,
    QUEST_PASSTHROUGH_THEME_SPOOKY,
};

enum QuestPassthroughEffect {
    QUEST_PASSTHROUGH_EFFECT_NONE = 0,
    QUEST_PASSTHROUGH_EFFECT_DAMAGE,
    QUEST_PASSTHROUGH_EFFECT_HEAL,
    QUEST_PASSTHROUGH_EFFECT_STAR,
    QUEST_PASSTHROUGH_EFFECT_WING_CAP,
    QUEST_PASSTHROUGH_EFFECT_METAL_CAP,
    QUEST_PASSTHROUGH_EFFECT_VANISH_CAP,
};

bool quest_game_initialize(void);
void quest_game_load_early_config(void);
void quest_game_flush_persistent_state(void);
unsigned int quest_game_render_scale_percent(void);
bool quest_game_ultra_performance_enabled(void);
bool quest_game_ui_requires_full_quality(void);
unsigned int quest_game_refresh_rate_index(void);
void quest_game_tick(void);
void quest_game_pump_network(void);
bool quest_game_render_eye(uint32_t eye, uint32_t width, uint32_t height);
void quest_game_prepare_vr_frame(void);
bool quest_game_is_ready(void);
bool quest_game_tabletop_mr_requested(void);
enum QuestPassthroughTheme quest_game_passthrough_theme(void);
uint32_t quest_game_star_collection_serial(void);
enum QuestPassthroughEffect quest_game_passthrough_effect(void);
uint32_t quest_game_passthrough_effect_serial(void);
enum QuestPassthroughEffect quest_game_passthrough_cap_effect(void);
bool quest_game_tabletop_mario_tracking_position(float out_position[3]);
bool quest_game_room_pulse_test_requested(void);
