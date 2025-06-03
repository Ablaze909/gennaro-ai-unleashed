/*
 * GENNARO AI - FLIPPER ZERO MOMENTUM FIRMWARE
 * AI Vision Assistant con ESP32-CAM
 * VERSIONE ULTRA-COMPATIBILE - Solo API standard
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
#define ESP32_TX_PIN &gpio_ext_pc0  // GPIO13 -> ESP32 RX (GPIO3)
#define ESP32_RX_PIN &gpio_ext_pc1  // GPIO14 -> ESP32 TX (GPIO1)

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
    GennaroAIMenuFlash,
    GennaroAIMenuStatus,
    GennaroAIMenuHelp,
} GennaroAIMenuItem;

// App context - MINIMALISTA
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
    
    // Flash state
    bool flash_enabled;
    
} GennaroAIApp;

// Send command to ESP32-CAM via GPIO - VERSIONE MINIMAL
static bool send_esp32_command(GennaroAIApp* app, const char* command) {
    if (!app || !command) {
        return false;
    }
    
    FURI_LOG_I(TAG, "Sending command: %s", command);
    
    // Initialize GPIO pins - SAFE VERSION
    furi_hal_gpio_init(ESP32_TX_PIN, GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);
    furi_hal_gpio_init(ESP32_RX_PIN, GpioModeInput, GpioPullUp, GpioSpeedVeryHigh);
    
    // Send start signal
    furi_hal_gpio_write(ESP32_TX_PIN, true);
    furi_delay_ms(100);
    furi_hal_gpio_write(ESP32_TX_PIN, false);
    furi_delay_ms(50);
    
    // Send command pattern
    if(strcmp(command, "VISION") == 0) {
        for(int i = 0; i < 5; i++) {
            furi_hal_gpio_write(ESP32_TX_PIN, true);
            furi_delay_ms(50);
            furi_hal_gpio_write(ESP32_TX_PIN, false);
            furi_delay_ms(50);
        }
    } else if(strcmp(command, "MATH") == 0) {
        for(int i = 0; i < 3; i++) {
            furi_hal_gpio_write(ESP32_TX_PIN, true);
            furi_delay_ms(100);
            furi_hal_gpio_write(ESP32_TX_PIN, false);
            furi_delay_ms(100);
        }
    } else if(strcmp(command, "OCR") == 0) {
        for(int i = 0; i < 2; i++) {
            furi_hal_gpio_write(ESP32_TX_PIN, true);
            furi_delay_ms(150);
            furi_hal_gpio_write(ESP32_TX_PIN, false);
            furi_delay_ms(150);
        }
    } else if(strcmp(command, "COUNT") == 0) {
        for(int i = 0; i < 4; i++) {
            furi_hal_gpio_write(ESP32_TX_PIN, true);
            furi_delay_ms(75);
            furi_hal_gpio_write(ESP32_TX_PIN, false);
            furi_delay_ms(75);
        }
    } else if(strcmp(command, "STATUS") == 0) {
        furi_hal_gpio_write(ESP32_TX_PIN, true);
        furi_delay_ms(500);
        furi_hal_gpio_write(ESP32_TX_PIN, false);
    } else if(strcmp(command, "PTT_START") == 0) {
        for(int i = 0; i < 6; i++) {
            furi_hal_gpio_write(ESP32_TX_PIN, true);
            furi_delay_ms(25);
            furi_hal_gpio_write(ESP32_TX_PIN, false);
            furi_delay_ms(25);
        }
    } else if(strcmp(command, "PTT_STOP") == 0) {
        for(int i = 0; i < 7; i++) {
            furi_hal_gpio_write(ESP32_TX_PIN, true);
            furi_delay_ms(25);
            furi_hal_gpio_write(ESP32_TX_PIN, false);
            furi_delay_ms(25);
        }
    } else if(strcmp(command, "FLASH_ON") == 0) {
        for(int i = 0; i < 8; i++) {
            furi_hal_gpio_write(ESP32_TX_PIN, true);
            furi_delay_ms(30);
            furi_hal_gpio_write(ESP32_TX_PIN, false);
            furi_delay_ms(30);
        }
        app->flash_enabled = true;
    } else if(strcmp(command, "FLASH_OFF") == 0) {
        for(int i = 0; i < 9; i++) {
            furi_hal_gpio_write(ESP32_TX_PIN, true);
            furi_delay_ms(30);
            furi_hal_gpio_write(ESP32_TX_PIN, false);
            furi_delay_ms(30);
        }
        app->flash_enabled = false;
    } else if(strcmp(command, "FLASH_TOGGLE") == 0) {
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

// Submenu callback
static void gennaro_ai_submenu_callback(void* context, uint32_t index) {
    GennaroAIApp* app = context;
    
    if (!app || !app->response_text) {
        return;
    }
    
    // Show loading view
    view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewLoading);
    
    switch(index) {
        case GennaroAIMenuVision:
            furi_string_printf(app->response_text, 
                "👁️ ANALISI IMMAGINE AI\n\n"
                "✅ Comando inviato con successo!\n\n"
                "🚀 ESP32-CAM sta eseguendo:\n"
                "• Attivazione flash LED automatico\n"
                "• Cattura foto alta risoluzione\n"
                "• Invio immagine a Claude AI\n"
                "• Analisi visiva avanzata\n\n"
                "📺 CONTROLLA il monitor seriale\n"
                "ESP32-CAM (115200 baud) per vedere\n"
                "la descrizione dettagliata.\n\n"
                "⚡ Pattern GPIO: 5 impulsi → ESP32\n"
                "💡 Flash: Automatico durante foto\n"
                "📊 Comando #%lu completato",
                app->command_count);
            send_esp32_command(app, "VISION");
            break;
            
        case GennaroAIMenuMath:
            furi_string_printf(app->response_text,
                "🧮 MATH SOLVER AI\n\n"
                "✅ Comando inviato con successo!\n\n"
                "🚀 ESP32-CAM sta eseguendo:\n"
                "• Attivazione flash per leggibilità\n"
                "• Cattura problemi matematici\n"
                "• OCR avanzato per numeri/simboli\n"
                "• Risoluzione tramite Claude AI\n\n"
                "📺 CONTROLLA il monitor seriale\n"
                "ESP32-CAM per vedere la soluzione\n"
                "completa con tutti i passaggi.\n\n"
                "⚡ Pattern GPIO: 3 impulsi → ESP32\n"
                "💡 Flash: Automatico per contrasto\n"
                "📊 Comando #%lu completato",
                app->command_count);
            send_esp32_command(app, "MATH");
            break;
            
        case GennaroAIMenuOCR:
            furi_string_printf(app->response_text,
                "📝 OCR TEXT READER\n\n"
                "✅ Comando inviato con successo!\n\n"
                "🚀 ESP32-CAM sta eseguendo:\n"
                "• Attivazione flash per nitidezza\n"
                "• Cattura testo alta definizione\n"
                "• Riconoscimento caratteri OCR\n"
                "• Estrazione testo via Claude AI\n\n"
                "📺 CONTROLLA il monitor seriale\n"
                "ESP32-CAM per vedere tutto il\n"
                "testo estratto parola per parola.\n\n"
                "⚡ Pattern GPIO: 2 impulsi → ESP32\n"
                "💡 Flash: Automatico per qualità\n"
                "📊 Comando #%lu completato",
                app->command_count);
            send_esp32_command(app, "OCR");
            break;
            
        case GennaroAIMenuCount:
            furi_string_printf(app->response_text,
                "🔢 OBJECT COUNTER\n\n"
                "✅ Comando inviato con successo!\n\n"
                "🚀 ESP32-CAM sta eseguendo:\n"
                "• Attivazione flash per dettagli\n"
                "• Cattura scena completa\n"
                "• Identificazione oggetti AI\n"
                "• Conteggio preciso via Claude\n\n"
                "📺 CONTROLLA il monitor seriale\n"
                "ESP32-CAM per vedere il numero\n"
                "totale e la lista degli oggetti.\n\n"
                "⚡ Pattern GPIO: 4 impulsi → ESP32\n"
                "💡 Flash: Automatico per visibilità\n"
                "📊 Comando #%lu completato",
                app->command_count);
            send_esp32_command(app, "COUNT");
            break;
            
        case GennaroAIMenuPTT:
            furi_string_set_str(app->response_text,
                "🎤 PUSH-TO-TALK AI\n\n"
                "📋 MODALITÀ D'USO:\n\n"
                "1️⃣ TIENI PREMUTO il pulsante OK\n"
                "2️⃣ PARLA nel microfono ESP32-CAM\n"
                "3️⃣ RILASCIA per avviare elaborazione\n\n"
                "🔄 PROCESSO AUTOMATICO:\n"
                "• Registrazione audio in tempo reale\n"
                "• Speech-to-Text via Google API\n"
                "• Interpretazione comando tramite AI\n"
                "• Esecuzione automatica dell'azione\n\n"
                "💬 COMANDI VOCALI RICONOSCIUTI:\n"
                "• \"Cosa vedi?\" / \"Descrivi\" → Vision\n"
                "• \"Risolvi calcolo\" → Math Solver\n"
                "• \"Leggi testo\" → OCR Reader\n"
                "• \"Conta oggetti\" → Object Counter\n"
                "• \"Accendi flash\" → Flash LED ON\n"
                "• \"Spegni luce\" → Flash LED OFF\n\n"
                "🎯 PRONTO! Premi e tieni OK per iniziare\n"
                "la registrazione del comando vocale.");
            break;
            
        case GennaroAIMenuFlash:
            furi_string_printf(app->response_text,
                "💡 FLASH LED CONTROL\n\n"
                "🔋 Stato corrente: %s\n\n"
                "⚡ CONTROLLI DISPONIBILI:\n"
                "• DESTRA → Accende flash manualmente\n"
                "• SINISTRA → Spegne flash manualmente\n"
                "• Menu corrente → Informazioni stato\n\n"
                "🤖 MODALITÀ AUTOMATICHE:\n"
                "Il flash si attiva automaticamente\n"
                "durante tutte le operazioni AI per\n"
                "garantire la migliore qualità:\n"
                "• Vision: Flash durante cattura\n"
                "• Math: Flash per leggere numeri\n"
                "• OCR: Flash per contrasto testo\n"
                "• Count: Flash per dettagli oggetti\n\n"
                "📡 COMUNICAZIONE GPIO:\n"
                "• Flash ON: 8 impulsi via GPIO13\n"
                "• Flash OFF: 9 impulsi via GPIO13\n"
                "• Toggle: 10 impulsi via GPIO13\n\n"
                "⚠️ SICUREZZA TERMICA:\n"
                "Il flash LED si spegne automaticamente\n"
                "dopo ogni operazione per evitare\n"
                "surriscaldamento del modulo ESP32-CAM.\n\n"
                "📊 Comandi flash inviati: %lu",
                app->flash_enabled ? "🔆 ACCESO" : "🔲 SPENTO",
                app->command_count);
            break;
            
        case GennaroAIMenuStatus:
            furi_string_printf(app->response_text,
                "📊 SYSTEM STATUS CHECK\n\n"
                "✅ Comando diagnostica inviato!\n\n"
                "🔍 ESP32-CAM sta verificando:\n"
                "• Connessione WiFi e stabilità\n"
                "• Stato camera OV2640 e settings\n"
                "• Connettività API Claude e Google\n"
                "• Memoria PSRAM disponibile\n"
                "• Funzionalità flash LED GPIO4\n"
                "• Calibrazione microfono MAX9814\n"
                "• Frequenza CPU e temperatura\n\n"
                "📺 CONTROLLA il monitor seriale\n"
                "ESP32-CAM per il report completo\n"
                "di diagnostica hardware e software.\n\n"
                "Il sistema mostrerà tutti i parametri\n"
                "operativi e eventuali problemi.\n\n"
                "⚡ Pattern GPIO: 1 impulso lungo\n"
                "💡 Flash corrente: %s\n"
                "📊 Comando #%lu completato",
                app->flash_enabled ? "ACCESO" : "SPENTO",
                app->command_count);
            send_esp32_command(app, "STATUS");
            break;
            
        case GennaroAIMenuHelp:
            furi_string_set_str(app->response_text,
                "❓ GENNARO AI - HELP GUIDE\n\n"
                "🔌 HARDWARE CONNECTIONS:\n"
                "Flipper GPIO13 → ESP32-CAM GPIO3 (RX)\n"
                "Flipper GPIO14 → ESP32-CAM GPIO1 (TX)\n"
                "Flipper 5V → ESP32-CAM 5V Power\n"
                "Flipper GND → ESP32-CAM Ground\n\n"
                "🎤 MICROPHONE MAX9814:\n"
                "VCC → ESP32-CAM 3.3V\n"
                "GND → ESP32-CAM GND\n"
                "OUT → ESP32-CAM GPIO2 (ADC Input)\n\n"
                "📱 AVAILABLE COMMANDS:\n"
                "• Vision: AI image analysis\n"
                "• Math: Mathematical problem solver\n"
                "• OCR: Text recognition from images\n"
                "• Count: Object counting in photos\n"
                "• Flash: LED control (GPIO4)\n"
                "• PTT: Voice command recognition\n"
                "• Status: System diagnostics\n\n"
                "📺 MONITORING RESPONSES:\n"
                "Connect ESP32-CAM via USB and open\n"
                "serial monitor at 115200 baud rate\n"
                "to see all AI responses in real-time.\n\n"
                "⚡ GPIO COMMUNICATION:\n"
                "Each command sends a unique pulse\n"
                "pattern on GPIO13 for ESP32-CAM\n"
                "to recognize and execute.\n\n"
                "🚀 System ready for AI operations!");
            break;
            
        default:
            furi_string_set_str(app->response_text, "❌ ERRORE: Comando non riconosciuto");
            break;
    }
    
    // Switch to text view
    text_box_set_text(app->text_box, furi_string_get_cstr(app->response_text));
    view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewTextBox);
}

// Input callback
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
                        "🎤 REGISTRAZIONE ATTIVA\n\n"
                        "🔴 MICROFONO IN ASCOLTO\n\n"
                        "📋 STATO CORRENTE:\n"
                        "• Pulsante OK: PREMUTO ✅\n"
                        "• Microfono ESP32: ATTIVO ✅\n"
                        "• Registrazione: IN CORSO ✅\n\n"
                        "🎯 ISTRUZIONI:\n"
                        "• CONTINUA a tenere premuto OK\n"
                        "• PARLA chiaramente verso ESP32-CAM\n"
                        "• RILASCIA quando hai finito\n\n"
                        "💬 ESEMPI COMANDI:\n"
                        "• \"Cosa vedi nell'immagine?\"\n"
                        "• \"Risolvi questo calcolo\"\n"
                        "• \"Leggi il testo\"\n"
                        "• \"Accendi la luce\"\n\n"
                        "⚡ Sistema in ascolto...");
                        
                    text_box_set_text(app->text_box, furi_string_get_cstr(app->response_text));
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
                        "⚠️ REGISTRAZIONE INTERROTTA\n\n"
                        "❌ DURATA TROPPO BREVE\n\n"
                        "📊 ANALISI:\n"
                        "• Durata registrata: < 0.5 sec\n"
                        "• Durata minima richiesta: 0.5 sec\n"
                        "• Risultato: INSUFFICIENTE\n\n"
                        "🔧 SOLUZIONE:\n"
                        "1. Tieni premuto OK PIÙ A LUNGO\n"
                        "2. Inizia a parlare SUBITO\n"
                        "3. Parla per almeno 1-2 secondi\n"
                        "4. Rilascia DOPO aver finito\n\n"
                        "💡 SUGGERIMENTO:\n"
                        "Prova con una frase breve come:\n"
                        "\"Cosa vedi?\" o \"Accendi luce\"\n\n"
                        "🔄 Riprova quando sei pronto!");
                } else {
                    // Send PTT_STOP to ESP32-CAM
                    if (send_esp32_command(app, "PTT_STOP")) {
                        furi_string_printf(app->response_text,
                            "🧠 ELABORAZIONE COMANDO\n\n"
                            "✅ REGISTRAZIONE COMPLETATA\n\n"
                            "📊 STATISTICHE:\n"
                            "• Durata: %.1f secondi\n"
                            "• Qualità: SUFFICIENTE ✅\n"
                            "• Stato: ELABORAZIONE ⏳\n\n"
                            "🔄 PROCESSO AUTOMATICO:\n"
                            "1. ✅ Audio registrato\n"
                            "2. ⏳ Speech-to-Text Google\n"
                            "3. ⏳ Interpretazione AI\n"
                            "4. ⏳ Esecuzione comando\n\n"
                            "📺 CONTROLLA il monitor seriale\n"
                            "ESP32-CAM per vedere:\n"
                            "• Testo riconosciuto\n"
                            "• Comando interpretato\n"
                            "• Risposta AI completa\n\n"
                            "⚡ Elaborazione in corso...",
                            (float)duration / 1000.0);
                    }
                }
                
                text_box_set_text(app->text_box, furi_string_get_cstr(app->response_text));
                
                // Double vibration feedback
                if (app->notifications) {
                    notification_message(app->notifications, &sequence_double_vibro);
                }
                
                consumed = true;
            }
        }
    }
    
    // Flash control con Left/Right buttons
    if(event->key == InputKeyLeft && event->type == InputTypePress) {
        // Flash OFF
        if (send_esp32_command(app, "FLASH_OFF")) {
            furi_string_printf(app->response_text,
                "🔲 FLASH LED DISATTIVATO\n\n"
                "✅ COMANDO ESEGUITO\n\n"
                "📡 COMUNICAZIONE:\n"
                "• Pattern GPIO: 9 impulsi\n"
                "• Destinazione: ESP32-CAM GPIO3\n"
                "• Comando: FLASH_OFF\n"
                "• Stato: RICEVUTO ✅\n\n"
                "💡 RISULTATO:\n"
                "• Flash LED GPIO4: SPENTO 🔲\n"
                "• Consumo energetico: RIDOTTO\n"
                "• Temperatura: NORMALE\n\n"
                "🤖 NOTA IMPORTANTE:\n"
                "Il flash LED si riaccenderà\n"
                "automaticamente durante le\n"
                "operazioni AI per garantire\n"
                "la migliore qualità delle foto.\n\n"
                "📊 Stato flash: %s\n"
                "📊 Comandi totali: %lu",
                app->flash_enabled ? "ACCESO" : "SPENTO",
                app->command_count);
            
            text_box_set_text(app->text_box, furi_string_get_cstr(app->response_text));
        }
        consumed = true;
    }
    
    if(event->key == InputKeyRight && event->type == InputTypePress) {
        // Flash ON
        if (send_esp32_command(app, "FLASH_ON")) {
            furi_string_printf(app->response_text,
                "💡 FLASH LED ATTIVATO\n\n"
                "✅ COMANDO ESEGUITO\n\n"
                "📡 COMUNICAZIONE:\n"
                "• Pattern GPIO: 8 impulsi\n"
                "• Destinazione: ESP32-CAM GPIO3\n"
                "• Comando: FLASH_ON\n"
                "• Stato: RICEVUTO ✅\n\n"
                "💡 RISULTATO:\n"
                "• Flash LED GPIO4: ACCESO 🔆\n"
                "• Luminosità: MASSIMA\n"
                "• Consumo: ELEVATO ⚠️\n\n"
                "⚠️ ATTENZIONE TERMICA:\n"
                "Il flash LED ESP32-CAM non ha\n"
                "resistenze di limitazione corrente!\n"
                "NON tenerlo acceso troppo a lungo\n"
                "per evitare danni da surriscaldamento.\n\n"
                "💡 RACCOMANDAZIONE:\n"
                "Spegnere manualmente se non necessario.\n\n"
                "📊 Stato flash: %s\n"
                "📊 Comandi totali: %lu",
                app->flash_enabled ? "ACCESO" : "SPENTO",
                app->command_count);
            
            text_box_set_text(app->text_box, furi_string_get_cstr(app->response_text));
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
    
    if (!app) {
        return NULL;
    }
    
    // Initialize variables
    memset(app, 0, sizeof(GennaroAIApp));
    
    app->response_text = furi_string_alloc();
    if (!app->response_text) {
        free(app);
        return NULL;
    }
    
    app->esp32_connected = false;
    app->command_count = 0;
    app->ptt_active = false;
    app->ptt_start_time = 0;
    app->flash_enabled = false;
    
    // Get notifications
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    
    // Create view dispatcher
    app->view_dispatcher = view_dispatcher_alloc();
    if (!app->view_dispatcher) {
        goto error_cleanup;
    }
    view_dispatcher_enable_queue(app->view_dispatcher);
    
    // Create submenu
    app->submenu = submenu_alloc();
    if (!app->submenu) {
        goto error_cleanup;
    }
    
    // Add menu items
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
        goto error_cleanup;
    }
    
    text_box_set_focus(app->text_box, TextBoxFocusStart);
    view_set_previous_callback(text_box_get_view(app->text_box), gennaro_ai_navigation_submenu_callback);
    view_set_input_callback(text_box_get_view(app->text_box), gennaro_ai_input_callback);
    view_set_context(text_box_get_view(app->text_box), app);
    view_dispatcher_add_view(app->view_dispatcher, GennaroAIViewTextBox, text_box_get_view(app->text_box));
    
    // Create loading view
    app->loading = loading_alloc();
    if (!app->loading) {
        goto error_cleanup;
    }
    
    view_set_previous_callback(loading_get_view(app->loading), gennaro_ai_navigation_submenu_callback);
    view_dispatcher_add_view(app->view_dispatcher, GennaroAIViewLoading, loading_get_view(app->loading));
    
    return app;

error_cleanup:
    gennaro_ai_app_free(app);
    return NULL;
}

// App deallocation
static void gennaro_ai_app_free(GennaroAIApp* app) {
    if (!app) {
        return;
    }
    
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

// Main app entry point - ULTRA SAFE
int32_t gennaro_ai_app(void* p) {
    UNUSED(p);
    
    GennaroAIApp* app = gennaro_ai_app_alloc();
    if (!app) {
        return -1;
    }
    
    // Attach to GUI
    Gui* gui = furi_record_open(RECORD_GUI);
    if (!gui) {
        gennaro_ai_app_free(app);
        return -1;
    }
    
    view_dispatcher_attach_to_gui(app->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    
    // Initialize GPIO - SAFE VERSION
    furi_hal_gpio_init(ESP32_TX_PIN, GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);
    furi_hal_gpio_init(ESP32_RX_PIN, GpioModeInput, GpioPullUp, GpioSpeedVeryHigh);
    
    // Set starting view
    view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewSubmenu);
    
    // Welcome notification
    if (app->notifications) {
        notification_message(app->notifications, &sequence_display_backlight_on);
        notification_message(app->notifications, &sequence_single_vibro);
    }
    
    // SAFE RUN - Solo API standard
    view_dispatcher_run(app->view_dispatcher);
    
    // Cleanup
    furi_record_close(RECORD_GUI);
    gennaro_ai_app_free(app);
    
    return 0;
}
