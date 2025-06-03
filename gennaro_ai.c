/*
 * GENNARO AI - FLIPPER ZERO MOMENTUM FIRMWARE
 * AI Vision Assistant con ESP32-CAM
 * FIXED: Crash su Momentum, GPIO pins, memoria
 * AGGIUNTO: Controllo Flash LED
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

// GPIO pins for ESP32-CAM communication - FIXED per Momentum
#define ESP32_TX_PIN &gpio_ext_pc0  // GPIO13 -> ESP32 RX (GPIO3)
#define ESP32_RX_PIN &gpio_ext_pc1  // GPIO14 -> ESP32 TX (GPIO1)

// Views
typedef enum {
    GennaroAIViewSubmenu,
    GennaroAIViewTextBox,
    GennaroAIViewLoading,
} GennaroAIView;

// Menu items - AGGIORNATO con Flash
typedef enum {
    GennaroAIMenuVision,
    GennaroAIMenuMath,
    GennaroAIMenuOCR,
    GennaroAIMenuCount,
    GennaroAIMenuPTT,
    GennaroAIMenuFlash,     // AGGIUNTO
    GennaroAIMenuStatus,
    GennaroAIMenuHelp,
} GennaroAIMenuItem;

// App context - FIXED allocazione memoria
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
    
    // AGGIUNTO: Flash state
    bool flash_enabled;
    
} GennaroAIApp;

// FIXED: Send command to ESP32-CAM via GPIO - Gestione errori migliorata
static bool send_esp32_command(GennaroAIApp* app, const char* command) {
    if (!app || !command) {
        FURI_LOG_E(TAG, "Invalid parameters");
        return false;
    }
    
    FURI_LOG_I(TAG, "Sending command: %s", command);
    
    // FIXED: Gestione GPIO più robusta per Momentum
    furi_hal_gpio_init_simple(ESP32_TX_PIN, GpioModeOutputPushPull);
    furi_hal_gpio_init_simple(ESP32_RX_PIN, GpioModeInput);
    
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
    // AGGIUNTO: Flash control commands
    else if(strcmp(command, "FLASH_ON") == 0) {
        // 8 pulses for FLASH_ON
        for(int i = 0; i < 8; i++) {
            furi_hal_gpio_write(ESP32_TX_PIN, true);
            furi_delay_ms(30);
            furi_hal_gpio_write(ESP32_TX_PIN, false);
            furi_delay_ms(30);
        }
        app->flash_enabled = true;
    } else if(strcmp(command, "FLASH_OFF") == 0) {
        // 9 pulses for FLASH_OFF
        for(int i = 0; i < 9; i++) {
            furi_hal_gpio_write(ESP32_TX_PIN, true);
            furi_delay_ms(30);
            furi_hal_gpio_write(ESP32_TX_PIN, false);
            furi_delay_ms(30);
        }
        app->flash_enabled = false;
    } else if(strcmp(command, "FLASH_TOGGLE") == 0) {
        // 10 pulses for FLASH_TOGGLE
        for(int i = 0; i < 10; i++) {
            furi_hal_gpio_write(ESP32_TX_PIN, true);
            furi_delay_ms(30);
            furi_hal_gpio_write(ESP32_TX_PIN, false);
            furi_delay_ms(30);
        }
        app->flash_enabled = !app->flash_enabled;
    } else {
        FURI_LOG_W(TAG, "Unknown command: %s", command);
        return false;
    }
    
    // End signal
    furi_delay_ms(100);
    furi_hal_gpio_write(ESP32_TX_PIN, true);
    furi_delay_ms(200);
    furi_hal_gpio_write(ESP32_TX_PIN, false);
    
    app->command_count++;
    
    // Vibration feedback
    if (app->notifications) {
        notification_message(app->notifications, &sequence_single_vibro);
    }
    
    return true;
}

// FIXED: Submenu callback - Gestione errori e controlli migliorati
static void gennaro_ai_submenu_callback(void* context, uint32_t index) {
    GennaroAIApp* app = context;
    
    if (!app || !app->response_text) {
        FURI_LOG_E(TAG, "Invalid app context");
        return;
    }
    
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
                "Flash: %s\n"
                "Comandi inviati: %lu",
                app->flash_enabled ? "ACCESO" : "SPENTO",
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
                "Flash: %s\n"
                "Comandi inviati: %lu",
                app->flash_enabled ? "ACCESO" : "SPENTO",
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
                "Flash: %s\n"
                "Comandi inviati: %lu",
                app->flash_enabled ? "ACCESO" : "SPENTO",
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
                "Flash: %s\n"
                "Comandi inviati: %lu",
                app->flash_enabled ? "ACCESO" : "SPENTO",
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
                "• \"Conta oggetti\" → Count\n"
                "• \"Accendi luce\" → Flash ON\n"
                "• \"Spegni luce\" → Flash OFF\n\n"
                "🎯 Tieni premuto OK per iniziare!");
            break;
            
        // AGGIUNTO: Menu Flash LED
        case GennaroAIMenuFlash:
            furi_string_printf(app->response_text,
                "💡 CONTROLLO FLASH LED\n\n"
                "Stato attuale: %s\n\n"
                "🔧 FUNZIONI:\n"
                "• Migliora foto in ambienti bui\n"
                "• Accensione automatica per AI\n"
                "• Controllo manuale ON/OFF\n\n"
                "⚡ COMANDI GPIO:\n"
                "• Flash ON: 8 impulsi\n"
                "• Flash OFF: 9 impulsi\n"
                "• Toggle: 10 impulsi\n\n"
                "🎤 COMANDI VOCALI:\n"
                "• \"Accendi luce/flash\"\n"
                "• \"Spegni luce\"\n\n"
                "⚠️ NOTA: Flash si accende\n"
                "automaticamente durante le\n"
                "operazioni AI per migliorare\n"
                "la qualità delle foto.\n\n"
                "Comandi inviati: %lu",
                app->flash_enabled ? "ACCESO 💡" : "SPENTO 🔲",
                app->command_count);
            break;
            
        case GennaroAIMenuStatus:
            furi_string_printf(app->response_text,
                "📊 STATO SISTEMA\n\n"
                "Comando inviato a ESP32-CAM via GPIO13.\n\n"
                "Verificando:\n"
                "• Connessione WiFi\n"
                "• Stato camera\n"
                "• API Claude/Google\n"
                "• Memoria disponibile\n"
                "• Flash LED ESP32-CAM\n\n"
                "Lo stato completo sarà mostrato\n"
                "nel monitor seriale ESP32-CAM.\n\n"
                "Pattern GPIO: 1 impulso lungo\n"
                "Flash attuale: %s\n"
                "Comandi inviati: %lu",
                app->flash_enabled ? "ACCESO" : "SPENTO",
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
                "🎤 MICROFONO MAX9814:\n"
                "VCC → ESP32-CAM 3.3V\n"
                "GND → ESP32-CAM GND\n"
                "OUT → ESP32-CAM GPIO2\n\n"
                "📱 COMANDI:\n"
                "• Vision: Analizza immagini\n"
                "• Math: Risolve calcoli\n"
                "• OCR: Legge testo\n"
                "• Count: Conta oggetti\n"
                "• Flash: Controllo LED (GPIO4)\n"
                "• PTT: Push-to-talk vocale\n"
                "• Status: Verifica sistema\n\n"
                "📺 RISPOSTE:\n"
                "Controlla monitor seriale ESP32-CAM\n"
                "per vedere le risposte AI complete.\n\n"
                "⚡ GPIO13 invia pattern di impulsi\n"
                "diversi per ogni comando.\n\n"
                "🎤 PTT: Tieni OK per registrare voce.\n"
                "💡 Flash: Auto ON durante AI ops.");
            break;
            
        default:
            furi_string_set_str(app->response_text, "Errore: Comando non riconosciuto");
            break;
    }
    
    // Switch to text view
    if (app->text_box) {
        text_box_set_text(app->text_box, furi_string_get_cstr(app->response_text));
        view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewTextBox);
    }
}

// FIXED: Input callback con gestione errori migliorata
static bool gennaro_ai_input_callback(InputEvent* event, void* context) {
    GennaroAIApp* app = context;
    
    if (!app || !event) {
        return false;
    }
    
    bool consumed = false;
    
    // Handle PTT (OK button long press)
    if(event->key == InputKeyOk) {
        if(event->type == InputTypeLong) {
            // Start PTT
            if(!app->ptt_active) {
                app->ptt_active = true;
                app->ptt_start_time = furi_get_tick();
                
                // Send PTT_START to ESP32-CAM
                if (send_esp32_command(app, "PTT_START")) {
                    furi_string_set_str(app->response_text, 
                        "🎤 PUSH-TO-TALK ATTIVO\n\n"
                        "🔴 REGISTRANDO...\n\n"
                        "• Tieni premuto OK\n"
                        "• Parla nel microfono ESP32-CAM\n"
                        "• Rilascia per elaborare\n\n"
                        "Pattern GPIO: 6 impulsi inviati\n"
                        "Microfono ESP32-CAM in ascolto...");
                        
                    if (app->text_box) {
                        text_box_set_text(app->text_box, furi_string_get_cstr(app->response_text));
                    }
                }
                
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
                    if (send_esp32_command(app, "PTT_STOP")) {
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
                }
                
                if (app->text_box) {
                    text_box_set_text(app->text_box, furi_string_get_cstr(app->response_text));
                }
                
                // Double vibration feedback
                if (app->notifications) {
                    notification_message(app->notifications, &sequence_double_vibro);
                }
                
                consumed = true;
            }
        }
    }
    
    // AGGIUNTO: Flash control con Left/Right buttons
    if(event->key == InputKeyLeft && event->type == InputTypePress) {
        // Flash OFF
        if (send_esp32_command(app, "FLASH_OFF")) {
            furi_string_printf(app->response_text,
                "🔲 FLASH LED SPENTO\n\n"
                "Comando inviato a ESP32-CAM.\n\n"
                "Flash LED (GPIO4) ora è spento.\n\n"
                "Pattern GPIO: 9 impulsi\n"
                "Stato: %s\n"
                "Comandi: %lu",
                app->flash_enabled ? "ACCESO" : "SPENTO",
                app->command_count);
            
            if (app->text_box) {
                text_box_set_text(app->text_box, furi_string_get_cstr(app->response_text));
            }
        }
        consumed = true;
    }
    
    if(event->key == InputKeyRight && event->type == InputTypePress) {
        // Flash ON
        if (send_esp32_command(app, "FLASH_ON")) {
            furi_string_printf(app->response_text,
                "💡 FLASH LED ACCESO\n\n"
                "Comando inviato a ESP32-CAM.\n\n"
                "Flash LED (GPIO4) ora è acceso.\n\n"
                "⚠️ ATTENZIONE: Non tenere acceso\n"
                "troppo a lungo per evitare\n"
                "surriscaldamento.\n\n"
                "Pattern GPIO: 8 impulsi\n"
                "Stato: %s\n"
                "Comandi: %lu",
                app->flash_enabled ? "ACCESO" : "SPENTO",
                app->command_count);
            
            if (app->text_box) {
                text_box_set_text(app->text_box, furi_string_get_cstr(app->response_text));
            }
        }
        consumed = true;
    }
    
    // Handle back button
    if(event->key == InputKeyBack && event->type == InputTypePress) {
        view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewSubmenu);
        consumed = true;
    }
    
    return consumed;
}

// FIXED: Navigation callbacks - Controlli di validità
static uint32_t gennaro_ai_navigation_exit_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static uint32_t gennaro_ai_navigation_submenu_callback(void* context) {
    UNUSED(context);
    return GennaroAIViewSubmenu;
}

// FIXED: App allocation - Gestione memoria migliorata
static GennaroAIApp* gennaro_ai_app_alloc() {
    GennaroAIApp* app = malloc(sizeof(GennaroAIApp));
    
    if (!app) {
        FURI_LOG_E(TAG, "Failed to allocate app memory");
        return NULL;
    }
    
    // Initialize variables
    memset(app, 0, sizeof(GennaroAIApp));
    
    app->response_text = furi_string_alloc();
    if (!app->response_text) {
        FURI_LOG_E(TAG, "Failed to allocate response_text");
        free(app);
        return NULL;
    }
    
    app->esp32_connected = false;
    app->command_count = 0;
    app->ptt_active = false;
    app->ptt_start_time = 0;
    app->flash_enabled = false;  // AGGIUNTO
    
    // Get notifications
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    if (!app->notifications) {
        FURI_LOG_W(TAG, "Failed to open notifications");
    }
    
    // Create view dispatcher
    app->view_dispatcher = view_dispatcher_alloc();
    if (!app->view_dispatcher) {
        FURI_LOG_E(TAG, "Failed to allocate view_dispatcher");
        goto error_cleanup;
    }
    view_dispatcher_enable_queue(app->view_dispatcher);
    
    // Create submenu
    app->submenu = submenu_alloc();
    if (!app->submenu) {
        FURI_LOG_E(TAG, "Failed to allocate submenu");
        goto error_cleanup;
    }
    
    // FIXED: Menu items con controlli
    submenu_add_item(app->submenu, "👁️ Analizza Immagine", GennaroAIMenuVision, gennaro_ai_submenu_callback, app);
    submenu_add_item(app->submenu, "🧮 Risolvi Calcoli", GennaroAIMenuMath, gennaro_ai_submenu_callback, app);
    submenu_add_item(app->submenu, "📝 Leggi Testo (OCR)", GennaroAIMenuOCR, gennaro_ai_submenu_callback, app);
    submenu_add_item(app->submenu, "🔢 Conta Oggetti", GennaroAIMenuCount, gennaro_ai_submenu_callback, app);
    submenu_add_item(app->submenu, "🎤 Push-to-Talk", GennaroAIMenuPTT, gennaro_ai_submenu_callback, app);
    submenu_add_item(app->submenu, "💡 Flash LED", GennaroAIMenuFlash, gennaro_ai_submenu_callback, app);  // AGGIUNTO
    submenu_add_item(app->submenu, "📊 Stato Sistema", GennaroAIMenuStatus, gennaro_ai_submenu_callback, app);
    submenu_add_item(app->submenu, "❓ Aiuto", GennaroAIMenuHelp, gennaro_ai_submenu_callback, app);
    
    view_set_previous_callback(submenu_get_view(app->submenu), gennaro_ai_navigation_exit_callback);
    view_dispatcher_add_view(app->view_dispatcher, GennaroAIViewSubmenu, submenu_get_view(app->submenu));
    
    // Create text box
    app->text_box = text_box_alloc();
    if (!app->text_box) {
        FURI_LOG_E(TAG, "Failed to allocate text_box");
        goto error_cleanup;
    }
    
    text_box_set_focus(app->text_box, TextBoxFocusStart);
    view_set_previous_callback(text_box_get_view(app->text_box), gennaro_ai_navigation_submenu_callback);
    view_set_input_callback(text_box_get_view(app->text_box), gennaro_ai_input_callback);
    view_set_context(text_box_get_view(app->text_box), app);  // FIXED: Set context
    view_dispatcher_add_view(app->view_dispatcher, GennaroAIViewTextBox, text_box_get_view(app->text_box));
    
    // Create loading view
    app->loading = loading_alloc();
    if (!app->loading) {
        FURI_LOG_E(TAG, "Failed to allocate loading");
        goto error_cleanup;
    }
    
    view_set_previous_callback(loading_get_view(app->loading), gennaro_ai_navigation_submenu_callback);
    view_dispatcher_add_view(app->view_dispatcher, GennaroAIViewLoading, loading_get_view(app->loading));
    
    return app;

error_cleanup:
    FURI_LOG_E(TAG, "Error during app allocation, cleaning up");
    gennaro_ai_app_free(app);
    return NULL;
}

// FIXED: App deallocation - Gestione memoria sicura
static void gennaro_ai_app_free(GennaroAIApp* app) {
    if (!app) {
        return;
    }
    
    FURI_LOG_I(TAG, "Freeing app resources");
    
    // Remove views safely
    if (app->view_dispatcher) {
        if (app->submenu) {
            view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewSubmenu);
        }
        if (app->text_box) {
            view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewTextBox);
        }
        if (app->loading) {
            view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewLoading);
        }
        
        // Free view dispatcher
        view_dispatcher_free(app->view_dispatcher);
    }
    
    // Free views
    if (app->submenu) {
        submenu_free(app->submenu);
    }
    if (app->text_box) {
        text_box_free(app->text_box);
    }
    if (app->loading) {
        loading_free(app->loading);
    }
    
    // Free response string
    if (app->response_text) {
        furi_string_free(app->response_text);
    }
    
    // Close notifications
    if (app->notifications) {
        furi_record_close(RECORD_NOTIFICATION);
    }
    
    free(app);
}

// FIXED: Main app entry point - Gestione errori completa
int32_t gennaro_ai_app(void* p) {
    UNUSED(p);
    
    FURI_LOG_I(TAG, "Starting Gennaro AI app");
    
    GennaroAIApp* app = gennaro_ai_app_alloc();
    if (!app) {
        FURI_LOG_E(TAG, "Failed to allocate app");
        return -1;
    }
    
    // Attach to GUI
    Gui* gui = furi_record_open(RECORD_GUI);
    if (!gui) {
        FURI_LOG_E(TAG, "Failed to open GUI record");
        gennaro_ai_app_free(app);
        return -1;
    }
    
    view_dispatcher_attach_to_gui(app->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    
    // FIXED: Initialize GPIO safely
    if (furi_hal_gpio_is_valid_pin(ESP32_TX_PIN) && furi_hal_gpio_is_valid_pin(ESP32_RX_PIN)) {
        furi_hal_gpio_init_simple(ESP32_TX_PIN, GpioModeOutputPushPull);
        furi_hal_gpio_init_simple(ESP32_RX_PIN, GpioModeInput);
        FURI_LOG_I(TAG, "GPIO pins initialized successfully");
    } else {
        FURI_LOG_E(TAG, "Invalid GPIO pins");
        // Continue anyway, may still work
    }
    
    // Set starting view
    view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewSubmenu);
    
    // Welcome notification
    if (app->notifications) {
        notification_message(app->notifications, &sequence_display_backlight_on);
        notification_message(app->notifications, &sequence_single_vibro);
    }
    
    FURI_LOG_I(TAG, "App started successfully");
    
    // Run view dispatcher
    view_dispatcher_run(app->view_dispatcher);
    
    // Cleanup
    FURI_LOG_I(TAG, "App shutting down");
    furi_record_close(RECORD_GUI);
    gennaro_ai_app_free(app);
    
    return 0;
}
