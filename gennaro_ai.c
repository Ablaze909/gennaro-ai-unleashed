/*
 * GENNARO AI - FLIPPER ZERO UNLEASHED
 * AI Vision Assistant con ESP32-CAM
 * Compatibile con Unleashed Firmware
 */

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/modules/loading.h>
#include <notification/notification_messages.h>
#include <furi_hal.h>
#include <furi_hal_gpio.h>

#define TAG "GennaroAI"

// GPIO pins for ESP32-CAM communication
#define ESP32_TX_PIN &gpio_ext_pc0  // GPIO13 -> ESP32 RX
#define ESP32_RX_PIN &gpio_ext_pc1  // GPIO14 -> ESP32 TX

// Views
typedef enum {
    GennaroAIViewSubmenu,
    GennaroAIViewTextBox,
    GennaroAIViewLoading,
} GennaroAIView;

// Menu items
typedef enum {
    GennaroAIMenuVision,
    GennaroAIMenuMath,
    GennaroAIMenuOCR,
    GennaroAIMenuCount,
    GennaroAIMenuPTT,
    GennaroAIMenuStatus,
    GennaroAIMenuHelp,
} GennaroAIMenuItem;

// App context
typedef struct {
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    TextBox* text_box;
    Loading* loading;
    NotificationApp* notifications;
    FuriString* response_text;
    
    // Communication state
    bool esp32_connected;
    uint32_t command_count;
    
    // PTT state
    bool ptt_active;
    uint32_t ptt_start_time;
    
} GennaroAIApp;

// Send command to ESP32-CAM via GPIO
static void send_esp32_command(GennaroAIApp* app, const char* command) {
    FURI_LOG_I(TAG, "Sending command: %s", command);
    
    // Initialize GPIO pins
    furi_hal_gpio_init(ESP32_TX_PIN, GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);
    furi_hal_gpio_init(ESP32_RX_PIN, GpioModeInput, GpioPullUp, GpioSpeedVeryHigh);
    
    // Send start signal
    furi_hal_gpio_write(ESP32_TX_PIN, true);
    furi_delay_ms(100);
    furi_hal_gpio_write(ESP32_TX_PIN, false);
    furi_delay_ms(50);
    
    // Send command pattern based on command type
    if(strcmp(command, "VISION") == 0) {
        // 5 pulses for VISION
        for(int i = 0; i < 5; i++) {
            furi_hal_gpio_write(ESP32_TX_PIN, true);
            furi_delay_ms(50);
            furi_hal_gpio_write(ESP32_TX_PIN, false);
            furi_delay_ms(50);
        }
    } else if(strcmp(command, "MATH") == 0) {
        // 3 pulses for MATH
        for(int i = 0; i < 3; i++) {
            furi_hal_gpio_write(ESP32_TX_PIN, true);
            furi_delay_ms(100);
            furi_hal_gpio_write(ESP32_TX_PIN, false);
            furi_delay_ms(100);
        }
    } else if(strcmp(command, "OCR") == 0) {
        // 2 pulses for OCR
        for(int i = 0; i < 2; i++) {
            furi_hal_gpio_write(ESP32_TX_PIN, true);
            furi_delay_ms(150);
            furi_hal_gpio_write(ESP32_TX_PIN, false);
            furi_delay_ms(150);
        }
    } else if(strcmp(command, "COUNT") == 0) {
        // 4 pulses for COUNT
        for(int i = 0; i < 4; i++) {
            furi_hal_gpio_write(ESP32_TX_PIN, true);
            furi_delay_ms(75);
            furi_hal_gpio_write(ESP32_TX_PIN, false);
            furi_delay_ms(75);
        }
    } else if(strcmp(command, "STATUS") == 0) {
        // 1 long pulse for STATUS
        furi_hal_gpio_write(ESP32_TX_PIN, true);
        furi_delay_ms(500);
        furi_hal_gpio_write(ESP32_TX_PIN, false);
    } else if(strcmp(command, "PTT_START") == 0) {
        // PTT_START: 6 short pulses
        for(int i = 0; i < 6; i++) {
            furi_hal_gpio_write(ESP32_TX_PIN, true);
            furi_delay_ms(25);
            furi_hal_gpio_write(ESP32_TX_PIN, false);
            furi_delay_ms(25);
        }
    } else if(strcmp(command, "PTT_STOP") == 0) {
        // PTT_STOP: 7 short pulses
        for(int i = 0; i < 7; i++) {
            furi_hal_gpio_write(ESP32_TX_PIN, true);
            furi_delay_ms(25);
            furi_hal_gpio_write(ESP32_TX_PIN, false);
            furi_delay_ms(25);
        }
    }
    
    // End signal
    furi_delay_ms(100);
    furi_hal_gpio_write(ESP32_TX_PIN, true);
    furi_delay_ms(200);
    furi_hal_gpio_write(ESP32_TX_PIN, false);
    
    app->command_count++;
    
    // Vibration feedback
    notification_message(app->notifications, &sequence_single_vibro);
}

// Submenu callback
static void gennaro_ai_submenu_callback(void* context, uint32_t index) {
    GennaroAIApp* app = context;
    
    // Show loading view
    view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewLoading);
    
    switch(index) {
        case GennaroAIMenuVision:
            furi_string_printf(app->response_text, 
                "👁️ ANALISI IMMAGINE\n\n"
                "Comando inviato a ESP32-CAM via GPIO13.\n\n"
                "ESP32-CAM sta:\n"
                "• Scattando foto con camera\n"
                "• Inviando a Claude AI\n" 
                "• Analizzando immagine\n\n"
                "Controlla il monitor seriale ESP32-CAM\n"
                "per vedere la risposta AI completa.\n\n"
                "Pattern GPIO: 5 impulsi\n"
                "Comandi inviati: %lu",
                app->command_count + 1);
            send_esp32_command(app, "VISION");
            break;
            
        case GennaroAIMenuMath:
            furi_string_printf(app->response_text,
                "🧮 MATH SOLVER\n\n"
                "Comando inviato a ESP32-CAM via GPIO13.\n\n"
                "ESP32-CAM sta:\n"
                "• Fotografando problemi matematici\n"
                "• Inviando a Claude AI\n"
                "• Risolvendo calcoli\n\n"
                "I risultati appariranno nel\n"
                "monitor seriale ESP32-CAM.\n\n"
                "Pattern GPIO: 3 impulsi\n"
                "Comandi inviati: %lu",
                app->command_count + 1);
            send_esp32_command(app, "MATH");
            break;
            
        case GennaroAIMenuOCR:
            furi_string_printf(app->response_text,
                "📝 LETTURA TESTO (OCR)\n\n"
                "Comando inviato a ESP32-CAM via GPIO13.\n\n"
                "ESP32-CAM sta:\n"
                "• Fotografando testo\n"
                "• Eseguendo riconoscimento OCR\n"
                "• Estraendo caratteri\n\n"
                "Il testo letto sarà mostrato nel\n"
                "monitor seriale ESP32-CAM.\n\n"
                "Pattern GPIO: 2 impulsi\n"
                "Comandi inviati: %lu",
                app->command_count + 1);
            send_esp32_command(app, "OCR");
            break;
            
        case GennaroAIMenuCount:
            furi_string_printf(app->response_text,
                "🔢 CONTEGGIO OGGETTI\n\n"
                "Comando inviato a ESP32-CAM via GPIO13.\n\n"
                "ESP32-CAM sta:\n"
                "• Analizzando immagine\n"
                "• Identificando oggetti\n"
                "• Contando elementi\n\n"
                "Il numero di oggetti sarà mostrato\n"
                "nel monitor seriale ESP32-CAM.\n\n"
                "Pattern GPIO: 4 impulsi\n"
                "Comandi inviati: %lu",
                app->command_count + 1);
            send_esp32_command(app, "COUNT");
            break;
            
        case GennaroAIMenuPTT:
            furi_string_set_str(app->response_text,
                "🎤 PUSH-TO-TALK MODE\n\n"
                "📋 ISTRUZIONI:\n\n"
                "• TIENI PREMUTO il pulsante OK\n"
                "• PARLA nel microfono ESP32-CAM\n"
                "• RILASCIA per elaborare\n\n"
                "🔄 PROCESSO:\n"
                "1. OK lungo → Inizia registrazione\n"
                "2. Parla comando vocale\n"
                "3. Rilascia → Speech-to-Text\n"
                "4. AI interpreta ed esegue\n\n"
                "💡 COMANDI VOCALI:\n"
                "• \"Cosa vedi?\" → Analisi\n"
                "• \"Risolvi\" → Math\n"
                "• \"Leggi testo\" → OCR\n"
                "• \"Conta oggetti\" → Count\n\n"
                "🎯 Tieni premuto OK per iniziare!");
            break;
            
        case GennaroAIMenuStatus:
            furi_string_printf(app->response_text,
                "📊 STATO SISTEMA\n\n"
                "Comando inviato a ESP32-CAM via GPIO13.\n\n"
                "Verificando:\n"
                "• Connessione WiFi\n"
                "• Stato camera\n"
                "• API Claude/Google\n"
                "• Memoria disponibile\n\n"
                "Lo stato completo sarà mostrato\n"
                "nel monitor seriale ESP32-CAM.\n\n"
                "Pattern GPIO: 1 impulso lungo\n"
                "Comandi inviati: %lu",
                app->command_count + 1);
            send_esp32_command(app, "STATUS");
            break;
            
        case GennaroAIMenuHelp:
            furi_string_set_str(app->response_text,
                "❓ AIUTO - GENNARO AI\n\n"
                "🔌 COLLEGAMENTI:\n"
                "Flipper GPIO13 → ESP32-CAM GPIO3\n"
                "Flipper GPIO14 → ESP32-CAM GPIO1\n"
                "Flipper 5V → ESP32-CAM 5V\n"
                "Flipper GND → ESP32-CAM GND\n\n"
                "📱 COMANDI:\n"
                "• Vision: Analizza immagini\n"
                "• Math: Risolve calcoli\n"
                "• OCR: Legge testo\n"
                "• Count: Conta oggetti\n"
                "• PTT: Push-to-talk vocale\n"
                "• Status: Verifica sistema\n\n"
                "📺 RISPOSTE:\n"
                "Controlla monitor seriale ESP32-CAM\n"
                "per vedere le risposte AI complete.\n\n"
                "⚡ GPIO13 invia pattern di impulsi\n"
                "diversi per ogni comando.\n\n"
                "🎤 PTT: Tieni OK per registrare voce.");
            break;
    }
    
    // Switch to text view
    text_box_set_text(app->text_box, furi_string_get_cstr(app->response_text));
    view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewTextBox);
}

// Input callback with PTT support
static bool gennaro_ai_input_callback(InputEvent* event, void* context) {
    GennaroAIApp* app = context;
    bool consumed = false;
    
    // Handle PTT (OK button long press)
    if(event->key == InputKeyOk) {
        if(event->type == InputTypeLong) {
            // Start PTT
            if(!app->ptt_active) {
                app->ptt_active = true;
                app->ptt_start_time = furi_get_tick();
                
                // Send PTT_START to ESP32-CAM
                send_esp32_command(app, "PTT_START");
                
                furi_string_set_str(app->response_text, 
                    "🎤 PUSH-TO-TALK ATTIVO\n\n"
                    "🔴 REGISTRANDO...\n\n"
                    "• Tieni premuto OK\n"
                    "• Parla nel microfono ESP32-CAM\n"
                    "• Rilascia per elaborare\n\n"
                    "Pattern GPIO: 6 impulsi inviati\n"
                    "Microfono ESP32-CAM in ascolto...");
                    
                text_box_set_text(app->text_box, furi_string_get_cstr(app->response_text));
                
                // Vibration feedback
                notification_message(app->notifications, &sequence_single_vibro);
                
                consumed = true;
            }
        } else if(event->type == InputTypeRelease) {
            // Stop PTT
            if(app->ptt_active) {
                app->ptt_active = false;
                
                uint32_t duration = furi_get_tick() - app->ptt_start_time;
                
                if(duration < 500) {
                    // Too short
                    furi_string_set_str(app->response_text,
                        "⚠️ REGISTRAZIONE TROPPO BREVE\n\n"
                        "Tieni premuto OK più a lungo\n"
                        "per registrare comando vocale.\n\n"
                        "Durata minima: 0.5 secondi\n"
                        "Riprova con pressione più lunga.");
                } else {
                    // Send PTT_STOP to ESP32-CAM
                    send_esp32_command(app, "PTT_STOP");
                    
                    furi_string_set_str(app->response_text,
                        "🧠 ELABORAZIONE COMANDO VOCALE\n\n"
                        "⏳ ESP32-CAM sta elaborando...\n\n"
                        "• Speech-to-Text (Google)\n"
                        "• Interpretazione comando\n"
                        "• Esecuzione azione AI\n\n"
                        "Pattern GPIO: 7 impulsi inviati\n\n"
                        "Controlla monitor seriale ESP32-CAM\n"
                        "per vedere la risposta completa.\n\n"
                        "Comando vocale in elaborazione...");
                }
                
                text_box_set_text(app->text_box, furi_string_get_cstr(app->response_text));
                
                // Double vibration feedback
                notification_message(app->notifications, &sequence_double_vibro);
                
                consumed = true;
            }
        }
    }
    
    // Handle back button
    if(event->key == InputKeyBack && event->type == InputTypePress) {
        view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewSubmenu);
        consumed = true;
    }
    
    return consumed;
}

// Navigation callbacks
static uint32_t gennaro_ai_navigation_exit_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static uint32_t gennaro_ai_navigation_submenu_callback(void* context) {
    UNUSED(context);
    return GennaroAIViewSubmenu;
}

// App allocation
static GennaroAIApp* gennaro_ai_app_alloc() {
    GennaroAIApp* app = malloc(sizeof(GennaroAIApp));
    
    // Initialize variables
    app->response_text = furi_string_alloc();
    app->esp32_connected = false;
    app->command_count = 0;
    app->ptt_active = false;
    app->ptt_start_time = 0;
    
    // Get notifications
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    
    // Create view dispatcher
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_enable_queue(app->view_dispatcher);
    
    // Create submenu
    app->submenu = submenu_alloc();
    submenu_add_item(app->submenu, "👁️ Analizza Immagine", GennaroAIMenuVision, gennaro_ai_submenu_callback, app);
    submenu_add_item(app->submenu, "🧮 Risolvi Calcoli", GennaroAIMenuMath, gennaro_ai_submenu_callback, app);
    submenu_add_item(app->submenu, "📝 Leggi Testo (OCR)", GennaroAIMenuOCR, gennaro_ai_submenu_callback, app);
    submenu_add_item(app->submenu, "🔢 Conta Oggetti", GennaroAIMenuCount, gennaro_ai_submenu_callback, app);
    submenu_add_item(app->submenu, "🎤 Push-to-Talk", GennaroAIMenuPTT, gennaro_ai_submenu_callback, app);
    submenu_add_item(app->submenu, "📊 Stato Sistema", GennaroAIMenuStatus, gennaro_ai_submenu_callback, app);
    submenu_add_item(app->submenu, "❓ Aiuto", GennaroAIMenuHelp, gennaro_ai_submenu_callback, app);
    
    view_set_previous_callback(submenu_get_view(app->submenu), gennaro_ai_navigation_exit_callback);
    view_dispatcher_add_view(app->view_dispatcher, GennaroAIViewSubmenu, submenu_get_view(app->submenu));
    
    // Create text box
    app->text_box = text_box_alloc();
    text_box_set_focus(app->text_box, TextBoxFocusStart);
    view_set_previous_callback(text_box_get_view(app->text_box), gennaro_ai_navigation_submenu_callback);
    view_set_input_callback(text_box_get_view(app->text_box), gennaro_ai_input_callback);
    view_dispatcher_add_view(app->view_dispatcher, GennaroAIViewTextBox, text_box_get_view(app->text_box));
    
    // Create loading view
    app->loading = loading_alloc();
    view_set_previous_callback(loading_get_view(app->loading), gennaro_ai_navigation_submenu_callback);
    view_dispatcher_add_view(app->view_dispatcher, GennaroAIViewLoading, loading_get_view(app->loading));
    
    return app;
}

// App deallocation
static void gennaro_ai_app_free(GennaroAIApp* app) {
    furi_assert(app);
    
    // Remove views
    view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewTextBox);
    view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewLoading);
    
    // Free views
    submenu_free(app->submenu);
    text_box_free(app->text_box);
    loading_free(app->loading);
    
    // Free view dispatcher
    view_dispatcher_free(app->view_dispatcher);
    
    // Free response string
    furi_string_free(app->response_text);
    
    // Close notifications
    furi_record_close(RECORD_NOTIFICATION);
    
    free(app);
}

// Main app entry point
int32_t gennaro_ai_app(void* p) {
    UNUSED(p);
    
    GennaroAIApp* app = gennaro_ai_app_alloc();
    
    // Attach to GUI
    Gui* gui = furi_record_open(RECORD_GUI);
    view_dispatcher_attach_to_gui(app->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    
    // Initialize GPIO
    furi_hal_gpio_init(ESP32_TX_PIN, GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);
    furi_hal_gpio_init(ESP32_RX_PIN, GpioModeInput, GpioPullUp, GpioSpeedVeryHigh);
    
    // Set starting view
    view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewSubmenu);
    
    // Welcome notification
    notification_message(app->notifications, &sequence_display_backlight_on);
    notification_message(app->notifications, &sequence_single_vibro);
    
    // Run view dispatcher
    view_dispatcher_run(app->view_dispatcher);
    
    // Cleanup
    furi_record_close(RECORD_GUI);
    gennaro_ai_app_free(app);
    
    return 0;
}