/*
 * GENNARO AI - FLIPPER ZERO UNLEASHED/MOMENTUM
 * AI Vision Assistant con ESP32-CAM + Flash Control
 * Compatibile con Unleashed e Momentum Firmware
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
    GennaroAIMenuFlashOn,
    GennaroAIMenuFlashOff,
    GennaroAIMenuFlashToggle,
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
    
    // Flash state
    bool flash_state;
    
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
    } else if(strcmp(command, "FLASH_ON") == 0) {
        // FLASH_ON: 8 pulses
        for(int i = 0; i < 8; i++) {
            furi_hal_gpio_write(ESP32_TX_PIN, true);
            furi_delay_ms(40);
            furi_hal_gpio_write(ESP32_TX_PIN, false);
            furi_delay_ms(40);
        }
        app->flash_state = true;
    } else if(strcmp(command, "FLASH_OFF") == 0) {
        // FLASH_OFF: 9 pulses
        for(int i = 0; i < 9; i++) {
            furi_hal_gpio_write(ESP32_TX_PIN, true);
            furi_delay_ms(35);
            furi_hal_gpio_write(ESP32_TX_PIN, false);
            furi_delay_ms(35);
        }
        app->flash_state = false;
    } else if(strcmp(command, "FLASH_TOGGLE") == 0) {
        // FLASH_TOGGLE: 10 pulses
        for(int i = 0; i < 10; i++) {
            furi_hal_gpio_write(ESP32_TX_PIN, true);
            furi_delay_ms(30);
            furi_hal_gpio_write(ESP32_TX_PIN, false);
            furi_delay_ms(30);
        }
        app->flash_state = !app->flash_state;
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
                "• \"Conta oggetti\" → Count\n"
                "• \"Accendi flash\" → Flash On\n"
                "• \"Spegni flash\" → Flash Off\n"
                "• \"Flash\" → Flash Toggle\n\n"
                "🎯 Tieni premuto OK per iniziare!");
            break;
            
        case GennaroAIMenuFlashOn:
            furi_string_printf(app->response_text,
                "💡 FLASH LED ACCESO\n\n"
                "Comando inviato a ESP32-CAM via GPIO13.\n\n"
                "ESP32-CAM ha acceso il LED flash\n"
                "integrato per migliorare l'illuminazione\n"
                "delle foto in condizioni di scarsa luce.\n\n"
                "Il flash rimarrà acceso fino a quando\n"
                "non viene spento manualmente.\n\n"
                "💡 UTILE PER:\n"
                "• Foto al buio\n"
                "• OCR su testo poco illuminato\n"
                "• Migliore riconoscimento oggetti\n\n"
                "Pattern GPIO: 8 impulsi\n"
                "Flash: ACCESO\n"
                "Comandi inviati: %lu",
                app->command_count + 1);
            send_esp32_command(app, "FLASH_ON");
            break;
            
        case GennaroAIMenuFlashOff:
            furi_string_printf(app->response_text,
                "🔦 FLASH LED SPENTO\n\n"
                "Comando inviato a ESP32-CAM via GPIO13.\n\n"
                "ESP32-CAM ha spento il LED flash\n"
                "integrato per risparmiare energia\n"
                "e tornare alle condizioni normali.\n\n"
                "Il flash è ora disattivato e verrà\n"
                "utilizzata solo la luce ambientale.\n\n"
                "🔋 VANTAGGI:\n"
                "• Risparmio batteria\n"
                "• Nessun riflesso indesiderato\n"
                "• Modalità discreta\n\n"
                "Pattern GPIO: 9 impulsi\n"
                "Flash: SPENTO\n"
                "Comandi inviati: %lu",
                app->command_count + 1);
            send_esp32_command(app, "FLASH_OFF");
            break;
            
        case GennaroAIMenuFlashToggle:
            furi_string_printf(app->response_text,
                "🔄 FLASH LED TOGGLE\n\n"
                "Comando inviato a ESP32-CAM via GPIO13.\n\n"
                "ESP32-CAM alternerà automaticamente\n"
                "lo stato del LED flash:\n"
                "• Se spento → si accende\n"
                "• Se acceso → si spegne\n\n"
                "🚀 CONTROLLO RAPIDO:\n"
                "Utile per controllo rapido\n"
                "dell'illuminazione senza dover\n"
                "sapere lo stato attuale.\n\n"
                "⚡ Il comando più veloce per\n"
                "gestire il flash!\n\n"
                "Pattern GPIO: 10 impulsi\n"
                "Flash: %s → %s\n"
                "Comandi inviati: %lu",
                app->flash_state ? "ACCESO" : "SPENTO",
                app->flash_state ? "SPENTO" : "ACCESO",
                app->command_count + 1);
            send_esp32_command(app, "FLASH_TOGGLE");
            break;
            
        case GennaroAIMenuStatus:
            furi_string_printf(app->response_text,
                "📊 STATO SISTEMA\n\n"
                "Comando inviato a ESP32-CAM via GPIO13.\n\n"
                "Verificando:\n"
                "• Connessione WiFi\n"
                "• Stato camera\n"
                "• Stato microfono\n"
                "• Stato flash LED\n"
                "• API Claude/Google\n"
                "• Memoria disponibile\n"
                "• Frequenza CPU\n\n"
                "Lo stato completo sarà mostrato\n"
                "nel monitor seriale ESP32-CAM.\n\n"
                "🔍 Include anche informazioni\n"
                "dettagliate sulle performance.\n\n"
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
                "• Flash On/Off/Toggle: Controllo LED\n"
                "• Status: Verifica sistema\n\n"
                "📺 RISPOSTE:\n"
                "Controlla monitor seriale ESP32-CAM\n"
                "per vedere le risposte AI complete.\n\n"
                "⚡ PATTERN GPIO:\n"
                "Ogni comando invia impulsi diversi\n"
                "su GPIO13 per identificazione.\n\n"
                "💡 FLASH:\n"
                "Migliora drasticamente foto al buio\n"
                "e riconoscimento testo/oggetti.\n\n"
                "🎤 COMANDI VOCALI:\n"
                "\"Accendi/spegni flash\" funziona\n"
                "anche con Push-to-Talk!");
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
                    "💡 COMANDI VOCALI:\n"
                    "• \"Cosa vedi?\" → Vision\n"
                    "• \"Risolvi\" → Math\n"
                    "• \"Leggi testo\" → OCR\n"
                    "• \"Conta oggetti\" → Count\n"
                    "• \"Accendi flash\" → Flash On\n"
                    "• \"Spegni flash\" → Flash Off\n"
                    "• \"Flash\" → Flash Toggle\n\n"
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
                        "Riprova con pressione più lunga.\n\n"
                        "💡 SUGGERIMENTO:\n"
                        "1. Premi e tieni OK\n"
                        "2. Aspetta il feedback vibrazione\n"
                        "3. Parla chiaramente\n"
                        "4. Rilascia dopo aver parlato");
                } else {
                    // Send PTT_STOP to ESP32-CAM
                    send_esp32_command(app, "PTT_STOP");
                    
                    furi_string_set_str(app->response_text,
                        "🧠 ELABORAZIONE COMANDO VOCALE\n\n"
                        "⏳ ESP32-CAM sta elaborando...\n\n"
                        "🔄 PROCESSO:\n"
                        "1. ✅ Audio registrato\n"
                        "2. 🔄 Speech-to-Text (Google)\n"
                        "3. ⏳ Interpretazione comando\n"
                        "4. ⏳ Esecuzione azione AI\n\n"
                        "📡 Pattern GPIO: 7 impulsi inviati\n\n"
                        "📺 RISULTATO:\n"
                        "Controlla monitor seriale ESP32-CAM\n"
                        "per vedere la risposta completa.\n\n"
                        "⚡ Il comando vocale viene elaborato\n"
                        "e l'azione corrispondente eseguita\n"
                        "automaticamente.");
