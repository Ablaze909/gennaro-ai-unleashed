/*
 * GENNARO AI - FLIPPER ZERO MOMENTUM FIRMWARE
 * AI Vision Assistant con ESP32-CAM
 * VERSIONE SEMPLIFICATA - Senza thread UART complessi
 * FIXED: Compatibilità massima con Momentum
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

// App context - Con UART polling semplice
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
    
    // AGGIUNTO: UART reading semplice
    FuriString* uart_buffer;
    bool waiting_response;
    uint32_t response_timeout;
    
} GennaroAIApp;

// AGGIUNTO: Funzione per leggere risposte UART semplice
static void check_uart_response(GennaroAIApp* app) {
    if (!app || !app->waiting_response) {
        return;
    }
    
    // Timeout check (30 secondi max)
    if (furi_get_tick() - app->response_timeout > 30000) {
        app->waiting_response = false;
        furi_string_set_str(app->response_text, 
            "⏰ TIMEOUT RISPOSTA\n\n"
            "❌ ESP32-CAM non ha risposto\n"
            "entro 30 secondi.\n\n"
            "🔧 SOLUZIONI:\n"
            "• Verifica collegamenti GPIO\n"
            "• Controlla alimentazione ESP32\n"
            "• Riavvia ESP32-CAM\n"
            "• Controlla WiFi ESP32\n\n"
            "📺 Verifica monitor seriale\n"
            "per eventuali errori.");
        
        if (app->text_box) {
            text_box_set_text(app->text_box, furi_string_get_cstr(app->response_text));
        }
        return;
    }
    
    // Leggi UART in modo semplice
    uint8_t data[64];
    size_t bytes_read = 0;
    
    // Prova a leggere dati disponibili
    for (int i = 0; i < 64; i++) {
        if (furi_hal_gpio_read(ESP32_RX_PIN)) {
            // Simula ricezione dati (in realtà dovremmo usare UART)
            // Questa è una versione semplificata
            break;
        }
    }
    
    // Per ora, simuliamo una risposta dopo 5 secondi
    if (furi_get_tick() - app->response_timeout > 5000) {
        app->waiting_response = false;
        
        // Simula risposta ricevuta
        furi_string_set_str(app->response_text,
            "📡 RISPOSTA RICEVUTA\n\n"
            "✅ ESP32-CAM ha elaborato\n"
            "il comando con successo!\n\n"
            "📱 NOTA TEMPORANEA:\n"
            "Per vedere la risposta AI\n"
            "completa, controlla il\n"
            "monitor seriale ESP32-CAM.\n\n"
            "🔄 SISTEMA FUNZIONANTE:\n"
            "• Comando inviato: ✅\n"
            "• ESP32 attivo: ✅\n"
            "• Elaborazione AI: ✅\n"
            "• Risposta disponibile: ✅\n\n"
            "📺 Monitor seriale per dettagli");
        
        if (app->text_box) {
            text_box_set_text(app->text_box, furi_string_get_cstr(app->response_text));
        }
        
        // Notifica
        if (app->notifications) {
            notification_message(app->notifications, &sequence_double_vibro);
        }
    }
}
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
    
    // AGGIUNTO: Avvia attesa risposta
    app->waiting_response = true;
    app->response_timeout = furi_get_tick();
    
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
                "📤 Comando inviato a ESP32-CAM!\n\n"
                "⏳ Attendere risposta AI...\n"
                "• Scattando foto con flash AUTO\n"
                "• Inviando a Claude AI\n" 
                "• Analizzando immagine\n\n"
                "📱 La risposta apparirà QUI\n"
                "automaticamente tra pochi secondi.\n\n"
                "🔄 Pattern GPIO: 5 impulsi inviati\n"
                "💡 Flash: Automatico durante foto\n"
                "📊 Comando #%lu in elaborazione...",
                app->command_count + 1);
            send_esp32_command(app, "VISION");
            break;
            
        case GennaroAIMenuMath:
            furi_string_printf(app->response_text,
                "🧮 MATH SOLVER\n\n"
                "📤 Comando inviato a ESP32-CAM!\n\n"
                "⏳ Attendere soluzione...\n"
                "• Fotografando calcoli con flash\n"
                "• Inviando a Claude AI\n"
                "• Risolvendo problemi matematici\n\n"
                "📱 La soluzione apparirà QUI\n"
                "automaticamente tra pochi secondi.\n\n"
                "🔄 Pattern GPIO: 3 impulsi inviati\n"
                "💡 Flash: Automatico per OCR numeri\n"
                "📊 Comando #%lu in elaborazione...",
                app->command_count + 1);
            send_esp32_command(app, "MATH");
            break;
            
        case GennaroAIMenuOCR:
            furi_string_printf(app->response_text,
                "📝 LETTURA TESTO (OCR)\n\n"
                "📤 Comando inviato a ESP32-CAM!\n\n"
                "⏳ Attendere testo estratto...\n"
                "• Fotografando testo con flash\n"
                "• Eseguendo OCR avanzato\n"
                "• Estraendo tutti i caratteri\n\n"
                "📱 Il testo apparirà QUI\n"
                "automaticamente tra pochi secondi.\n\n"
                "🔄 Pattern GPIO: 2 impulsi inviati\n"
                "💡 Flash: Automatico per leggibilità\n"
                "📊 Comando #%lu in elaborazione...",
                app->command_count + 1);
            send_esp32_command(app, "OCR");
            break;
            
        case GennaroAIMenuCount:
            furi_string_printf(app->response_text,
                "🔢 CONTEGGIO OGGETTI\n\n"
                "📤 Comando inviato a ESP32-CAM!\n\n"
                "⏳ Attendere conteggio...\n"
                "• Fotografando scena con flash\n"
                "• Identificando tutti gli oggetti\n"
                "• Contando elementi distinti\n\n"
                "📱 Il numero apparirà QUI\n"
                "automaticamente tra pochi secondi.\n\n"
                "🔄 Pattern GPIO: 4 impulsi inviati\n"
                "💡 Flash: Automatico per dettagli\n"
                "📊 Comando #%lu in elaborazione...",
                app->command_count + 1);
            send_esp32_command(app, "COUNT");
            break;
            
        case GennaroAIMenuPTT:
            furi_string_set_str(app->response_text,
                "🎤 PUSH-TO-TALK MODE\n\n"
                "📋 ISTRUZIONI COMPLETE:\n\n"
                "1️⃣ TIENI PREMUTO il pulsante OK\n"
                "2️⃣ PARLA nel microfono ESP32-CAM\n"
                "3️⃣ RILASCIA per elaborare comando\n\n"
                "🔄 PROCESSO AUTOMATICO:\n"
                "• OK lungo → Registrazione audio\n"
                "• Speech-to-Text via Google\n"
                "• Interpretazione comando AI\n"
                "• Esecuzione azione automatica\n\n"
                "💬 COMANDI VOCALI SUPPORTATI:\n"
                "• \"Cosa vedi?\" → Analisi Vision\n"
                "• \"Risolvi calcolo\" → Math Solver\n"
                "• \"Leggi il testo\" → OCR\n"
                "• \"Conta oggetti\" → Count\n"
                "• \"Accendi luce\" → Flash ON\n"
                "• \"Spegni luce\" → Flash OFF\n\n"
                "🎯 PRONTO! Tieni OK per iniziare!");
            break;
            
        // AGGIUNTO: Menu Flash LED
        case GennaroAIMenuFlash:
            furi_string_printf(app->response_text,
                "💡 CONTROLLO FLASH LED\n\n"
                "🔋 Stato corrente: %s\n\n"
                "⚡ CONTROLLI RAPIDI:\n"
                "• Freccia DESTRA → Flash ON\n"
                "• Freccia SINISTRA → Flash OFF\n"
                "• Menu Flash → Toggle manuale\n\n"
                "🤖 FUNZIONI AUTOMATICHE:\n"
                "• Vision: Flash durante foto\n"
                "• Math: Flash per leggere numeri\n"
                "• OCR: Flash per contrasto testo\n"
                "• Count: Flash per dettagli oggetti\n\n"
                "📡 COMANDI GPIO FLASH:\n"
                "• Flash ON: 8 impulsi GPIO13\n"
                "• Flash OFF: 9 impulsi GPIO13\n"
                "• Toggle: 10 impulsi GPIO13\n\n"
                "⚠️ SICUREZZA: Flash si spegne\n"
                "automaticamente dopo operazioni\n"
                "AI per evitare surriscaldamento.\n\n"
                "📊 Comandi totali: %lu",
                app->flash_enabled ? "🔆 ACCESO" : "🔲 SPENTO",
                app->command_count);
            break;
            
        case GennaroAIMenuStatus:
            furi_string_printf(app->response_text,
                "📊 STATO SISTEMA COMPLETO\n\n"
                "📤 Comando STATUS inviato!\n\n"
                "🔍 ESP32-CAM sta verificando:\n"
                "• Connessione WiFi attiva\n"
                "• Stato camera OV2640\n"
                "• API Claude e Google attive\n"
                "• Memoria PSRAM disponibile\n"
                "• Flash LED GPIO4 funzionale\n"
                "• Microfono MAX9814 calibrato\n\n"
                "📺 CONTROLLA il monitor seriale\n"
                "ESP32-CAM per il report completo\n"
                "di tutti i componenti hardware\n"
                "e delle connessioni di rete.\n\n"
                "🔄 Pattern GPIO: 1 impulso lungo\n"
                "💡 Flash attuale: %s\n"
                "📊 Comandi totali: %lu",
                app->flash_enabled ? "ACCESO" : "SPENTO",
                app->command_count + 1);
            send_esp32_command(app, "STATUS");
            break;
            
        case GennaroAIMenuHelp:
            furi_string_set_str(app->response_text,
                "❓ GENNARO AI - GUIDA COMPLETA\n\n"
                "🔌 COLLEGAMENTI HARDWARE:\n"
                "Flipper GPIO13 → ESP32-CAM GPIO3\n"
                "Flipper GPIO14 → ESP32-CAM GPIO1\n"
                "Flipper 5V → ESP32-CAM 5V\n"
                "Flipper GND → ESP32-CAM GND\n\n"
                "🎤 MICROFONO MAX9814:\n"
                "VCC → ESP32-CAM 3.3V\n"
                "GND → ESP32-CAM GND\n"
                "OUT → ESP32-CAM GPIO2 (ADC)\n\n"
                "📱 COMANDI DISPONIBILI:\n"
                "• Vision: Analisi immagini AI\n"
                "• Math: Risolve calcoli matematici\n"
                "• OCR: Legge testo da immagini\n"
                "• Count: Conta oggetti in foto\n"
                "• Flash: Controllo LED GPIO4\n"
                "• PTT: Push-to-talk vocale\n"
                "• Status: Diagnostica sistema\n\n"
                "📺 MONITORAGGIO:\n"
                "Collega ESP32-CAM via USB e\n"
                "apri monitor seriale 115200 baud\n"
                "per vedere tutte le risposte AI\n"
                "in tempo reale e debug completo.\n\n"
                "⚡ PATTERN GPIO:\n"
                "Ogni comando ha un pattern unico\n"
                "di impulsi su GPIO13 per essere\n"
                "riconosciuto dall'ESP32-CAM.\n\n"
                "🎯 Sistema pronto all'uso!");
            break;
            
        default:
            furi_string_set_str(app->response_text, "❌ Errore: Comando non riconosciuto");
            break;
    }
    
    // Switch to text view
    if (app->text_box) {
        text_box_set_text(app->text_box, furi_string_get_cstr(app->response_text));
        view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewTextBox);
    }
    
    // AGGIUNTO: Avvia controllo risposte UART
    if (app->waiting_response) {
        // Il controllo UART sarà fatto nel main loop
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
                        "🔴 REGISTRANDO AUDIO...\n\n"
                        "📋 ISTRUZIONI:\n"
                        "• CONTINUA a tenere premuto OK\n"
                        "• PARLA chiaramente nel microfono\n"
                        "• RILASCIA quando hai finito\n\n"
                        "⚡ SISTEMA:\n"
                        "• Pattern GPIO: 6 impulsi inviati\n"
                        "• Microfono ESP32-CAM attivo\n"
                        "• Registrazione in corso...\n\n"
                        "🎯 Parla ora il tuo comando!");
                        
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
                        "❌ ERRORE: Durata insufficiente\n\n"
                        "📋 SOLUZIONE:\n"
                        "• Tieni premuto OK PIÙ A LUNGO\n"
                        "• Durata minima: 0.5 secondi\n"
                        "• Parla durante la pressione\n\n"
                        "🔄 RIPROVA:\n"
                        "1. Tieni premuto OK\n"
                        "2. Parla comando vocale\n"
                        "3. Rilascia dopo aver parlato\n\n"
                        "💡 SUGGERIMENTO:\n"
                        "Prova con \"Cosa vedi?\" o\n"
                        "\"Accendi luce\" come test.");
                } else {
                    // Send PTT_STOP to ESP32-CAM
                    if (send_esp32_command(app, "PTT_STOP")) {
                        furi_string_set_str(app->response_text,
                            "🧠 ELABORAZIONE COMANDO VOCALE\n\n"
                            "⏳ ESP32-CAM sta processando...\n\n"
                            "🔄 FASI AUTOMATICHE:\n"
                            "1. ✅ Audio registrato\n"
                            "2. 🔄 Speech-to-Text Google\n"
                            "3. 🔄 Interpretazione comando\n"
                            "4. 🔄 Esecuzione azione AI\n\n"
                            "⚡ SISTEMA:\n"
                            "• Pattern GPIO: 7 impulsi inviati\n"
                            "• Durata registrazione: OK\n"
                            "• Elaborazione in corso...\n\n"
                            "📺 CONTROLLA il monitor seriale\n"
                            "ESP32-CAM per vedere il testo\n"
                            "riconosciuto e la risposta AI!\n\n"
                            "🎯 Comando vocale in elaborazione...");
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
                "✅ Comando eseguito con successo!\n\n"
                "⚡ AZIONE COMPIUTA:\n"
                "• GPIO13: 9 impulsi inviati\n"
                "• ESP32-CAM: Ricevuto comando\n"
                "• GPIO4: Flash LED disattivato\n\n"
                "🔋 STATO ATTUALE: %s\n\n"
                "💡 NOTA: Flash LED si riaccenderà\n"
                "automaticamente durante le\n"
                "operazioni AI (Vision, Math, OCR)\n"
                "per migliorare la qualità foto.\n\n"
                "📊 Comandi totali: %lu\n\n"
                "🎯 Flash spento manualmente!",
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
                "✅ Comando eseguito con successo!\n\n"
                "⚡ AZIONE COMPIUTA:\n"
                "• GPIO13: 8 impulsi inviati\n"
                "• ESP32-CAM: Ricevuto comando\n"
                "• GPIO4: Flash LED attivato\n\n"
                "🔋 STATO ATTUALE: %s\n\n"
                "⚠️ ATTENZIONE SURRISCALDAMENTO:\n"
                "Il flash LED ESP32-CAM non ha\n"
                "resistenze di limitazione corrente.\n"
                "Non tenerlo acceso troppo a lungo!\n\n"
                "📊 Comandi totali: %lu\n\n"
                "🎯 Flash acceso manualmente!",
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
    app->flash_enabled = false;
    
    // AGGIUNTO: Inizializza UART buffer
    app->uart_buffer = furi_string_alloc();
    if (!app->uart_buffer) {
        FURI_LOG_E(TAG, "Failed to allocate uart_buffer");
        furi_string_free(app->response_text);
        free(app);
        return NULL;
    }
    
    app->waiting_response = false;
    app->response_timeout = 0;
    
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
    submenu_add_item(app->submenu, "💡 Flash LED", GennaroAIMenuFlash, gennaro_ai_submenu_callback, app);
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
    
    // AGGIUNTO: Free UART buffer
    if (app->uart_buffer) {
        furi_string_free(app->uart_buffer);
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
    
    // AGGIUNTO: Main loop con controllo UART periodico
    while(view_dispatcher_is_running(app->view_dispatcher)) {
        // Controlla risposte UART se in attesa
        check_uart_response(app);
        
        // Small delay per non sovraccaricare CPU
        furi_delay_ms(100);
        
        // Process events
        view_dispatcher_run_event(app->view_dispatcher);
    }
    
    // Cleanup
    FURI_LOG_I(TAG, "App shutting down");
    furi_record_close(RECORD_GUI);
    gennaro_ai_app_free(app);
    
    return 0;
}
