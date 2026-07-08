#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <nfc/nfc.h>
#include <nfc/protocols/mf_classic/mf_classic.h>
#include <nfc/protocols/mf_classic/mf_classic_poller_sync.h>

typedef struct {
    char saldo_texto[32];
    bool tarjeta_leida;
    FuriMutex* mutex;
} AppState;

static void draw_callback(Canvas* canvas, void* ctx) {
    AppState* state = ctx;
    if(!state) return;

    furi_mutex_acquire(state->mutex, FuriWaitForever);
    canvas_clear(canvas);
    
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 5, 15, "MalaH4ck3d");

    if(state->tarjeta_leida) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 5, 30, "Tarjeta leida con exito!");
        
        canvas_set_font(canvas, FontBigNumbers);
        canvas_draw_str(canvas, 5, 48, state->saldo_texto);
        
        uint16_t anchura_numeros = canvas_string_width(canvas, state->saldo_texto);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 5 + anchura_numeros + 4, 48, "€");
        
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 3, 62, "[Pulsar ATRAS para volver]");
    } else {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 5, 35, "Aproxima tu tarjeta...");
        canvas_draw_str(canvas, 5, 52, "Buscando Value Blocks...");
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
    state->tarjeta_leida = false;
    strcpy(state->saldo_texto, "0.00");
    state->mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, draw_callback, state);
    view_port_input_callback_set(view_port, input_callback, event_queue);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    Nfc* nfc = nfc_alloc();

    uint8_t bloque_objetivo = 37; 
    MfClassicKey clave_sector_9 = {{0x99, 0x10, 0x02, 0x25, 0xD8, 0x3B}};
    MfClassicBlock datos_bloque;

    InputEvent event;
    uint32_t modo_lector = 0;

    while(1) {
        if(furi_message_queue_get(event_queue, &event, 100) == FuriStatusOk) {
            if(event.type == InputTypeShort && event.key == InputKeyBack) {
                if(modo_lector == 1) {
                    furi_mutex_acquire(state->mutex, FuriWaitForever);
                    state->tarjeta_leida = false;
                    furi_mutex_release(state->mutex);
                    view_port_update(view_port);
                    modo_lector = 0;
                } else {
                    break;
                }
            }
        }

        if(modo_lector == 0) {
            MfClassicError error = mf_classic_poller_sync_read_block(
                nfc, 
                bloque_objetivo, 
                &clave_sector_9, 
                MfClassicKeyTypeA, 
                &datos_bloque
            );

            if(error == MfClassicErrorNone) {
                int32_t saldo_raw = (datos_bloque.data[1] << 8) | datos_bloque.data[0];
 
                float saldo_euros = ((float)saldo_raw / 100.0f) / 2.0f;

                furi_mutex_acquire(state->mutex, FuriWaitForever);
                snprintf(state->saldo_texto, sizeof(state->saldo_texto), "%.2f", (double)saldo_euros);
                state->tarjeta_leida = true;
                furi_mutex_release(state->mutex);

                view_port_update(view_port);
                modo_lector = 1;
            }
        }
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