// lf_radar.c
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <rfal_rfid.h>
#include <rfal_analog.h>

typedef struct {
    bool scanning;
    uint8_t signal_strength; // 0-100
    char last_tag[32];
    FuriMutex* mutex;
} LfRadarApp;

static void draw_callback(Canvas* canvas, void* ctx) {
    LfRadarApp* app = ctx;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "LF Radar (125kHz)");
    
    if(app->scanning) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 25, "Scanning...");
        
        // Signal bars
        uint8_t bars = app->signal_strength / 10;
        for(uint8_t i = 0; i < bars; i++) {
            canvas_draw_box(canvas, 2 + i * 6, 35, 4, 8);
        }
        
        char signal_text[32];
        snprintf(signal_text, sizeof(signal_text), "Signal: %d%%", app->signal_strength);
        canvas_draw_str(canvas, 2, 55, signal_text);
        
        if(strlen(app->last_tag) > 0) {
            canvas_set_font(canvas, FontSecondary);
            canvas_draw_str(canvas, 2, 70, app->last_tag);
        }
    } else {
        canvas_draw_str(canvas, 2, 30, "Press OK to start");
    }
    
    furi_mutex_release(app->mutex);
}

static void input_callback(InputEvent* event, void* ctx) {
    LfRadarApp* app = ctx;
    if(event->type == InputTypePress && event->key == InputKeyOk) {
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        app->scanning = !app->scanning;
        furi_mutex_release(app->mutex);
    }
}

static void lf_radar_scan_loop(LfRadarApp* app) {
    while(1) {
        if(!app->scanning) {
            furi_delay_ms(100);
            continue;
        }

        int activity = 0;

        // Псевдо-оценка активности LF поля
        for(int i = 0; i < 20; i++) {
            if(rfalLfDetectTag()) {
                activity += 5;

                uint64_t card_id = rfalLfGetCardId();
                furi_mutex_acquire(app->mutex, FuriWaitForever);
                snprintf(app->last_tag, sizeof(app->last_tag), "Tag: %08llX", card_id);
                furi_mutex_release(app->mutex);

                // Вибрация
                furi_hal_vibro_on(true);
                furi_delay_ms(30);
                furi_hal_vibro_on(false);
            }

            furi_delay_ms(10);
        }

        if(activity > 100) activity = 100;

        furi_mutex_acquire(app->mutex, FuriWaitForever);
        app->signal_strength = activity;
        furi_mutex_release(app->mutex);

        // Звук в зависимости от "силы"
        if(activity > 10) {
            int freq = 800 + activity * 10;
            furi_hal_speaker_start(freq, 0.2);
            furi_delay_ms(40);
            furi_hal_speaker_stop();
        }

        furi_delay_ms(100);
    }
}

int32_t lf_radar_app(void* p) {
    UNUSED(p);

    LfRadarApp* app = malloc(sizeof(LfRadarApp));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->scanning = false;
    app->signal_strength = 0;
    app->last_tag[0] = '\0';

    // Инициализация LF RFID
    rfalLfInitialize();

    Gui* gui = furi_record_open(RECORD_GUI);
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, draw_callback, app);
    view_port_input_callback_set(view_port, input_callback, app);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    // Поток сканирования
    FuriThread* scan_thread = furi_thread_alloc();
    furi_thread_set_name(scan_thread, "LFRadar");
    furi_thread_set_callback(scan_thread, (FuriThreadCallback)lf_radar_scan_loop);
    furi_thread_set_context(scan_thread, app);
    furi_thread_start(scan_thread);

    // Главный цикл
    while(1) {
        furi_delay_ms(100);
    }

    // Cleanup (фактически не вызывается)
    furi_thread_free(scan_thread);
    furi_mutex_free(app->mutex);
    free(app);

    return 0;
}
