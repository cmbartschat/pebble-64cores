#include <pebble.h>

static Window *s_main_window;
static Layer *s_grid_layer;
static TextLayer *s_bottom_row_left;
static TextLayer *s_bottom_row_right;

static GFont s_font_24;
static GBitmap *s_sprites;
static GBitmap *s_core_sprites[8];
static GBitmap *s_core_sprites_broken[8];

static int s_load_status;
#define LoadStatusInitial 0
#define LoadStatusReset 1
#define LoadStatusLoaded 2

static uint32_t s_cycles;
static int s_tick_count;

typedef struct {
  uint8_t level;
  uint8_t broken;
} Core;

int extract_uint32(Tuple *t, uint32_t *target) {
  switch (t->type) {
    case TUPLE_UINT:
      switch (t->length) {
        case 1:
          *target = t->value->uint8;
          return 0;
        case 2:
          *target = t->value->uint16;
          return 0;
        case 4:
          *target = t->value->uint32;
          return 0;
        default:
          return 1;
      }

    case TUPLE_INT:
      switch (t->length) {
        case 1:
          if (t->value->int8 < 0) return 1;
          *target = t->value->int8;
          return 0;
        case 2:
          if (t->value->int16 < 0) return 1;
          *target = t->value->int16;
          return 0;

        case 4:
          if (t->value->int32 < 0) return 1;
          s_cycles = t->value->int32;
          return 0;
        default:
          return 1;
      }

    default:
      return 1;
  }
}

static Core cores[64];

static void update_bottom_row() {
  static char left_buffer[160];
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);
  strftime(left_buffer, sizeof(left_buffer),
           clock_is_24h_style() ? "%H:%M" : "%I:%M", tick_time);
  text_layer_set_text(s_bottom_row_left, left_buffer);

  static char right_buffer[160];

  if (s_load_status == LoadStatusLoaded) {
    if (s_cycles >= 10 * 1000 * 1000) {
      snprintf(right_buffer, sizeof(right_buffer), "%luM",
               s_cycles / (1000 * 1000));
    } else if (s_cycles >= 1000 * 1000) {
      snprintf(right_buffer, sizeof(right_buffer), "%lu.%luM",
               s_cycles / (1000 * 1000),
               (s_cycles % (1000 * 1000)) / (100 * 1000));
    } else if (s_cycles >= 10 * 1000) {
      snprintf(right_buffer, sizeof(right_buffer), "%luK", s_cycles / (1000));
    } else if (s_cycles >= 1000) {
      snprintf(right_buffer, sizeof(right_buffer), "%lu.%luK",
               s_cycles / (1000), (s_cycles % 1000) / (100));
    } else {
      snprintf(right_buffer, sizeof(right_buffer), "%lu", s_cycles);
    }
  } else if (s_load_status == LoadStatusReset) {
    snprintf(right_buffer, sizeof(right_buffer), "Needs config");
  }

  text_layer_set_text(s_bottom_row_right, right_buffer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_bottom_row();

  s_tick_count--;

  if (s_tick_count <= 0) {
    s_tick_count = 10;

    DictionaryIterator *iterator = NULL;

    AppMessageResult result = app_message_outbox_begin(&iterator);
    if (result != APP_MSG_OK) {
      return;
    }

    dict_write_cstring(iterator, MESSAGE_KEY_TYPE, "REFRESH");
    dict_write_end(iterator);

    app_message_outbox_send();
  }
}

static void grid_update_callback(Layer *layer, GContext *ctx) {
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 8; x++) {
      int i = y * 8 + x;
      Core *core = cores + i;

      GBitmap *sprite = (s_load_status == LoadStatusLoaded)
                            ? (core->broken ? s_core_sprites_broken
                                            : s_core_sprites)[core->level]
                            : s_core_sprites_broken[7];
      graphics_draw_bitmap_in_rect(ctx, sprite, GRect(x * 25, y * 25, 25, 25));
    }
  }
}

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  GRect bottom_row_bounds = GRect(4, bounds.size.h - 34, bounds.size.w - 8, 34);

  s_bottom_row_left = text_layer_create(bottom_row_bounds);
  text_layer_set_background_color(s_bottom_row_left, GColorClear);
  text_layer_set_text_color(s_bottom_row_left, GColorWhite);
  text_layer_set_font(s_bottom_row_left, s_font_24);
  text_layer_set_text_alignment(s_bottom_row_left, GTextAlignmentLeft);

  s_bottom_row_right = text_layer_create(bottom_row_bounds);
  text_layer_set_background_color(s_bottom_row_right, GColorClear);
  text_layer_set_text_color(s_bottom_row_right, GColorWhite);
  text_layer_set_font(s_bottom_row_right, s_font_24);
  text_layer_set_text_alignment(s_bottom_row_right, GTextAlignmentRight);

  s_grid_layer = layer_create(GRect(0, 0, 200, 200));
  layer_set_update_proc(s_grid_layer, grid_update_callback);

  layer_add_child(window_layer, text_layer_get_layer(s_bottom_row_left));
  layer_add_child(window_layer, text_layer_get_layer(s_bottom_row_right));
  layer_add_child(window_layer, s_grid_layer);
}

static void main_window_unload(Window *window) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "main_window_unload");
  text_layer_destroy(s_bottom_row_right);
  text_layer_destroy(s_bottom_row_left);
  layer_destroy(s_grid_layer);
}

void handle_app_message(DictionaryIterator *iterator, void *context) {
  Tuple *t = NULL;
  t = dict_find(iterator, MESSAGE_KEY_TYPE);
  if (t && t->type == TUPLE_CSTRING) {
    if (strcmp(t->value->cstring, "RESET") == 0) {
      s_load_status = LoadStatusReset;
      layer_mark_dirty(s_grid_layer);
      update_bottom_row();
    } else if (strcmp(t->value->cstring, "STATE") == 0) {
      Tuple *levels = dict_find(iterator, MESSAGE_KEY_LEVELS);
      if (levels && levels->type == TUPLE_BYTE_ARRAY) {
        for (int i = 0; i < levels->length; i++) {
          cores[i].level = levels->value->data[i];
        }
      }

      Tuple *broken = dict_find(iterator, MESSAGE_KEY_BROKEN);
      if (broken && broken->type == TUPLE_BYTE_ARRAY) {
        for (int i = 0; i < broken->length; i++) {
          cores[i].broken = broken->value->data[i];
        }
      }

      Tuple *cycles = dict_find(iterator, MESSAGE_KEY_CYCLES);
      if (cycles) {
        extract_uint32(cycles, &s_cycles);
      }

      s_load_status = LoadStatusLoaded;
      layer_mark_dirty(s_grid_layer);
      update_bottom_row();
      persist_write_int(MESSAGE_KEY_PERSISTED_CYCLES, s_cycles);
      persist_write_data(MESSAGE_KEY_PERSISTED_CORES, cores, sizeof(cores));
    }
  }
}

static void load_resources() {
  s_sprites = gbitmap_create_with_resource(RESOURCE_ID_SPRITES);

  for (int i = 0; i < 8; i++) {
    s_core_sprites[i] =
        gbitmap_create_as_sub_bitmap(s_sprites, GRect(i * 25, 0, 25, 25));
    s_core_sprites_broken[i] =
        gbitmap_create_as_sub_bitmap(s_sprites, GRect(i * 25, 25, 25, 25));
  }

  s_font_24 = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
}

static void unload_resources() {
  for (int i = 0; i < 8; i++) {
    gbitmap_destroy(s_core_sprites[i]);
    gbitmap_destroy(s_core_sprites_broken[i]);
  }
  gbitmap_destroy(s_sprites);
}

static void init() {
  s_cycles = persist_read_int(MESSAGE_KEY_PERSISTED_CYCLES);
  bool load_succeeded =
      sizeof(cores) ==
      persist_read_data(MESSAGE_KEY_PERSISTED_CORES, &cores, sizeof(cores));
  s_load_status = load_succeeded ? LoadStatusLoaded : LoadStatusInitial;
  s_tick_count = 0;

  s_main_window = window_create();
  app_message_open(app_message_inbox_size_maximum(),
                   app_message_outbox_size_maximum());
  window_set_background_color(s_main_window, GColorOxfordBlue);

  window_set_window_handlers(
      s_main_window,
      (WindowHandlers){.load = main_window_load, .unload = main_window_unload});

  window_stack_push(s_main_window, true);

  update_bottom_row();

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  app_message_register_inbox_received(handle_app_message);
}

static void deinit() { window_destroy(s_main_window); }

int main(void) {
  load_resources();
  init();
  app_event_loop();
  APP_LOG(APP_LOG_LEVEL_DEBUG, "ended: app_event_loop");
  deinit();

  APP_LOG(APP_LOG_LEVEL_DEBUG, "ended: deinit");
  unload_resources();
  APP_LOG(APP_LOG_LEVEL_DEBUG, "ended: unload_resources");
}
