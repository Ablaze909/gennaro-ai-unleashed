/*
 * GENNARO AI - MOMENTUM FIRMWARE COMPATIBLE v2.0
 * AI Vision Assistant con ESP32-CAM - UART REAL RESPONSES
 * 
 * Features:
 * - Invio comandi via GPIO13 + UART TX
 * - Ricezione risposte REALI via GPIO14 + UART RX  
 * - Display risposte AI sul Flipper in tempo reale
 * - Gestione PTT Push-to-Talk
 * - Controllo Flash LED
 * - UI robusta anti-crash
 * - Derek Jamison UART approach (testato e funzionante)
 */

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/modules/loading.h>
#include <gui/modules/dialog_ex.h>
#include <notification/notification_messages.h>
#include <furi_hal.h>
#include <furi_hal_gpio.h>
#include <furi_hal_resources.h>
#include <furi_hal_serial.h>

#define TAG "GennaroAI"

// GPIO PINS - VERIFICATI PER MOMENTUM
#define ESP32_TX_PIN (&gpio_ext_pc1)  // GPIO14 -> ESP32 RX (GPIO3)
#define ESP32_RX_PIN (&gpio_ext_pc0)  // GPIO13 -> ESP32 TX (GPIO1)

// UART Configuration
#define UART_BAUD_RATE 115200
#define RESPONSE_BUFFER_SIZE 2048
#define RESPONSE_TIMEOUT_MS 30000

// Views
typedef enum {
    GennaroAIViewSubmenu,
    GennaroAIViewTextBox,
    GennaroAIViewLoading,
    GennaroAIViewDialog,
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

// App state
typedef enum {
    StateIdle,
    StateSending,
    StateWaiting,
    StateReceiving,
    StatePTTActive,
    StateError
} AppState;

// Worker event flags for thread communication
typedef enum {
    WorkerEventDataWaiting = 1 << 0, // Data waiting to be processed  
    WorkerEventExiting = 1 << 1,     // Worker thread is exiting
} WorkerEventFlags;

// App context
typedef struct {
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    TextBox* text_box;
    Loading* loading;
    DialogEx* dialog;
    NotificationApp* notifications;
    FuriString* response_text;
    FuriString* temp_buffer;
    
    // Communication - Derek Jamison WORKING approach
    FuriHalSerialHandle* serial_handle;
    FuriStreamBuffer* rx_stream;
    FuriThread* uart_thread;
    FuriMutex* data_mutex;
    bool uart_init_by_app;
    
    // State management
    AppState current_state;
    uint32_t command_count;
    uint32_t last_command_time;
    
    // PTT state
    bool ptt_active;
    uint32_t ptt_start_time;
    
    // Response parsing
    char response_buffer[RESPONSE_BUFFER_SIZE];
    size_t response_pos;
    bool response_complete;
    
} GennaroAIApp;

// Forward declarations
static void uart_received_byte_callback(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* context);
static int32_t uart_worker(void* context);
static void send_esp32_command(GennaroAIApp* app, const char* command);
static void process_esp32_response(GennaroAIApp* app, const char* response);

// ===== UART COMMUNICATION - Derek Jamison WORKING Approach =====

static void uart_received_byte_callback(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* context) {
    GennaroAIApp* app = context;
    
    if(event == FuriHalSerialRxEventData) {
        uint8_t data = furi_hal_serial_async_rx(handle);
        furi_stream_buffer_send(app->rx_stream, (void*)&data, 1, 0);
        furi_thread_flags_set(furi_thread_get_id(app->uart_thread), WorkerEventDataWaiting);
    }
}

static void init_uart(GennaroAIApp* app) {
    // Initialize GPIO pins for UART alternate function
    furi_hal_gpio_init(ESP32_TX_PIN, GpioModeAltFunctionPushPull, GpioPullNo, GpioSpeedVeryHigh);
    furi_hal_gpio_init(ESP32_RX_PIN, GpioModeAltFunctionPushPull, GpioPullUp, GpioSpeedVeryHigh);
    
    // Try to acquire UART handle
    app->serial_handle = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(app->serial_handle) {
        // Initialize UART with standard settings
        furi_hal_serial_init(app->serial_handle, UART_BAUD_RATE);
        
        // Enable async RX with callback - Derek Jamison approach
        furi_hal_serial_async_rx_start(app->serial_handle, uart_received_byte_callback, app, false);
        
        app->uart_init_by_app = true;
        FURI_LOG_I(TAG, "✅ UART initialized successfully at %d baud with RX callback", UART_BAUD_RATE);
    } else {
        FURI_LOG_E(TAG, "❌ Failed to acquire UART, GPIO only mode");
        // Fallback to GPIO-only communication
        furi_hal_gpio_init(ESP32_TX_PIN, GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);
        furi_hal_gpio_init(ESP32_RX_PIN, GpioModeInput, GpioPullUp, GpioSpeedVeryHigh);
    }
}

static void deinit_uart(GennaroAIApp* app) {
    if(app->uart_init_by_app && app->serial_handle) {
        // Stop async RX first
        furi_hal_serial_async_rx_stop(app->serial_handle);
        furi_hal_serial_deinit(app->serial_handle);
        furi_hal_serial_control_release(app->serial_handle);
        app->serial_handle = NULL;
        app->uart_init_by_app = false;
    }
    
    // Reset GPIO to analog mode
    furi_hal_gpio_init(ESP32_TX_PIN, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
    furi_hal_gpio_init(ESP32_RX_PIN, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
}

static int32_t uart_worker(void* context) {
    GennaroAIApp* app = context;
    
    FURI_LOG_I(TAG, "🔄 UART worker thread started - Ready to receive ESP32 responses!");
    
    uint32_t events;
    uint8_t buffer[64];
    char line_buffer[RESPONSE_BUFFER_SIZE];
    size_t line_pos = 0;
    
    while(true) {
        events = furi_thread_flags_wait(
            WorkerEventDataWaiting | WorkerEventExiting, 
            FuriFlagWaitAny, 
            FuriWaitForever);
            
        if(events & WorkerEventExiting) {
            break;
        }
        
        if(events & WorkerEventDataWaiting) {
            // Process all available data from stream buffer
            size_t length_read;
            do {
                length_read = furi_stream_buffer_receive(app->rx_stream, buffer, sizeof(buffer), 0);
                
                for(size_t i = 0; i < length_read; i++) {
                    char byte = buffer[i];
                    
                    // Build line until we hit delimiter
                    if(byte == '\n' || byte == '\r') {
                        if(line_pos > 0) {
                            line_buffer[line_pos] = '\0';
                            
                            // Process complete line from ESP32
                            FURI_LOG_I(TAG, "📥 Received ESP32 response: %s", line_buffer);
                            process_esp32_response(app, line_buffer);
                            
                            line_pos = 0;
                        }
                    } else if(line_pos < sizeof(line_buffer) - 1) {
                        line_buffer[line_pos++] = byte;
                    }
                }
            } while(length_read > 0);
        }
    }
    
    FURI_LOG_I(TAG, "🛑 UART worker thread stopped");
    return 0;
}

// ===== ESP32 COMMAND SENDING =====

static void send_esp32_command(GennaroAIApp* app, const char* command) {
    FURI_LOG_I(TAG, "📤 Sending command: %s", command);
    
    app->current_state = StateSending;
    app->last_command_time = furi_get_tick();
    
    // Send via UART first
    if(app->serial_handle) {
        size_t command_len = strlen(command);
        
        // Send command
        furi_hal_serial_tx(app->serial_handle, (uint8_t*)command, command_len);
        
        // Send newline
        furi_hal_serial_tx(app->serial_handle, (uint8_t*)"\n", 1);
        
        FURI_LOG_I(TAG, "📤 Command sent via UART: %s", command);
    }
    
    // Also send GPIO pattern for backward compatibility
    furi_hal_gpio_init(ESP32_TX_PIN, GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);
    
    // Send start signal
    furi_hal_gpio_write(ESP32_TX_PIN, true);
    furi_delay_ms(100);
    furi_hal_gpio_write(ESP32_TX_PIN, false);
    furi_delay_ms(50);
    
    // Command-specific patterns
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
    } else if(strncmp(command, "FLASH_", 6) == 0) {
        // Flash commands - single pulse
        furi_hal_gpio_write(ESP32_TX_PIN, true);
        furi_delay_ms(100);
        furi_hal_gpio_write(ESP32_TX_PIN, false);
    }
    
    // End signal
    furi_delay_ms(100);
    furi_hal_gpio_write(ESP32_TX_PIN, true);
    furi_delay_ms(200);
    furi_hal_gpio_write(ESP32_TX_PIN, false);
    
    app->command_count++;
    app->current_state = StateWaiting;  // Wait for ESP32 response
    
    // Vibration feedback
    notification_message(app->notifications, &sequence_single_vibro);
    
    FURI_LOG_I(TAG, "📤 Command sent, waiting for ESP32 response...");
}

// ===== RESPONSE PROCESSING =====

static void process_esp32_response(GennaroAIApp* app, const char* response) {
    if(!app || !response || strlen(response) == 0) return;
    
    FURI_LOG_I(TAG, "📥 Processing ESP32 response: %s", response);
    
    // SAFE mutex operation
    if(app->data_mutex) {
        furi_mutex_acquire(app->data_mutex, FuriWaitForever);
    }
    
    // Parse response type and content SAFELY
    if(app->response_text) {
        if(strncmp(response, "VISION:", 7) == 0) {
            furi_string_printf(app->response_text, 
                "👁️ ANALISI IMMAGINE\n\n%s\n\n✅ Risposta AI ricevuta sul Flipper!", 
                response + 7);
        } else if(strncmp(response, "MATH:", 5) == 0) {
            furi_string_printf(app->response_text, 
                "🧮 MATH SOLVER\n\n%s\n\n✅ Calcolo completato!", 
                response + 5);
        } else if(strncmp(response, "OCR:", 4) == 0) {
            furi_string_printf(app->response_text, 
                "📝 LETTURA TESTO\n\n%s\n\n✅ OCR completato!", 
                response + 4);
        } else if(strncmp(response, "COUNT:", 6) == 0) {
            furi_string_printf(app->response_text, 
                "🔢 CONTEGGIO OGGETTI\n\n%s\n\n✅ Conteggio completato!", 
                response + 6);
        } else if(strncmp(response, "STATUS:", 7) == 0) {
            furi_string_printf(app->response_text, 
                "📊 STATO SISTEMA\n\n%s\n\n✅ Controllo completato!", 
                response + 7);
        } else if(strncmp(response, "FLASH:", 6) == 0) {
            furi_string_printf(app->response_text, 
                "💡 CONTROLLO FLASH LED\n\n%s\n\n✅ Stato aggiornato!", 
                response + 6);
        } else if(strncmp(response, "ERROR:", 6) == 0) {
            furi_string_printf(app->response_text, 
                "❌ ERRORE ESP32\n\n%s\n\n⚠️ Controlla connessioni.", 
                response + 6);
            app->current_state = StateError;
        } else if(strncmp(response, "READY", 5) == 0) {
            furi_string_set_str(app->response_text, 
                "✅ ESP32-CAM PRONTO\n\n🔗 Connesso e funzionante.\n🎯 Pronto per comandi AI!\n\n📋 Seleziona comando dal menu.");
        } else {
            // Generic response - show full ESP32 response
            furi_string_printf(app->response_text, 
                "📨 RISPOSTA ESP32\n\n%s\n\n📊 Comandi: %lu", 
                response, app->command_count);
        }
    }
    
    if(app->data_mutex) {
        furi_mutex_release(app->data_mutex);
    }
    
    // Update UI SAFELY on main thread
    app->current_state = StateIdle;
    
    if(app->text_box && app->response_text) {
        text_box_set_text(app->text_box, furi_string_get_cstr(app->response_text));
    }
    
    if(app->view_dispatcher) {
        view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewTextBox);
    }
    
    // Success notification
    if(app->notifications) {
        notification_message(app->notifications, &sequence_success);
    }
    
    FURI_LOG_I(TAG, "✅ Response processed and displayed");
}

// ===== SUBMENU CALLBACK =====

static void gennaro_ai_submenu_callback(void* context, uint32_t index) {
    GennaroAIApp* app = context;
    
    if(app->current_state == StateSending || app->current_state == StateWaiting) {
        // Prevent multiple commands
        return;
    }
    
    // Show loading view
    view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewLoading);
    
    switch(index) {
        case GennaroAIMenuVision:
            furi_string_set_str(app->response_text, 
                "👁️ ANALISI IMMAGINE\n\n📤 Inviando comando a ESP32-CAM...\n🔄 Attendi risposta AI...");
            send_esp32_command(app, "VISION");
            break;
            
        case GennaroAIMenuMath:
            furi_string_set_str(app->response_text, 
                "🧮 MATH SOLVER\n\n📤 Inviando comando a ESP32-CAM...\n🔄 Attendi risoluzione...");
            send_esp32_command(app, "MATH");
            break;
            
        case GennaroAIMenuOCR:
            furi_string_set_str(app->response_text, 
                "📝 LETTURA TESTO\n\n📤 Inviando comando a ESP32-CAM...\n🔄 Attendi OCR...");
            send_esp32_command(app, "OCR");
            break;
            
        case GennaroAIMenuCount:
            furi_string_set_str(app->response_text, 
                "🔢 CONTEGGIO OGGETTI\n\n📤 Inviando comando a ESP32-CAM...\n🔄 Attendi conteggio...");
            send_esp32_command(app, "COUNT");
            break;
            
        case GennaroAIMenuPTT:
            furi_string_set_str(app->response_text,
                "🎤 PUSH-TO-TALK MODE\n\n📋 ISTRUZIONI:\n\n"
                "• TIENI PREMUTO il pulsante OK\n"
                "• PARLA nel microfono ESP32-CAM\n"
                "• RILASCIA per elaborare\n\n"
                "🎯 Pronto per comando vocale...");
            break;
            
        case GennaroAIMenuFlashOn:
            furi_string_set_str(app->response_text, 
                "💡 ACCENSIONE FLASH LED\n\n📤 Inviando comando...");
            send_esp32_command(app, "FLASH_ON");
            break;
            
        case GennaroAIMenuFlashOff:
            furi_string_set_str(app->response_text, 
                "🔲 SPEGNIMENTO FLASH LED\n\n📤 Inviando comando...");
            send_esp32_command(app, "FLASH_OFF");
            break;
            
        case GennaroAIMenuFlashToggle:
            furi_string_set_str(app->response_text, 
                "🔄 TOGGLE FLASH LED\n\n📤 Inviando comando...");
            send_esp32_command(app, "FLASH_TOGGLE");
            break;
            
        case GennaroAIMenuStatus:
            furi_string_set_str(app->response_text, 
                "📊 STATO SISTEMA\n\n📤 Verificando ESP32-CAM...");
            send_esp32_command(app, "STATUS");
            break;
            
        case GennaroAIMenuHelp:
            furi_string_set_str(app->response_text,
                "❓ AIUTO - GENNARO AI v2.0\n\n"
                "🔌 COLLEGAMENTI:\n"
                "Flipper GPIO13 → ESP32-CAM GPIO3\n"
                "Flipper GPIO14 ← ESP32-CAM GPIO1\n"
                "Flipper 5V → ESP32-CAM 5V\n"
                "Flipper GND → ESP32-CAM GND\n\n"
                "📱 COMANDI:\n"
                "• Vision: Analizza immagini\n"
                "• Math: Risolve calcoli\n"
                "• OCR: Legge testo\n"
                "• Count: Conta oggetti\n"
                "• PTT: Push-to-talk vocale\n"
                "• Flash: Controllo LED\n"
                "• Status: Verifica sistema\n\n"
                "📺 RISPOSTE:\n"
                "Le risposte AI appaiono in tempo reale\n"
                "sul display del Flipper.\n\n"
                "🎤 PTT: Tieni OK per registrare.\n\n"
                "⚡ UART + GPIO dual communication");
            break;
    }
    
    // Switch to text view
    text_box_set_text(app->text_box, furi_string_get_cstr(app->response_text));
    view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewTextBox);
}

// ===== INPUT CALLBACK WITH ROBUST PTT =====

static bool gennaro_ai_input_callback(InputEvent* event, void* context) {
    GennaroAIApp* app = context;
    bool consumed = false;
    
    // Handle PTT (OK button long press)
    if(event->key == InputKeyOk) {
        if(event->type == InputTypeLong) {
            // Start PTT
            if(!app->ptt_active && app->current_state != StateSending && app->current_state != StateWaiting) {
                app->ptt_active = true;
                app->ptt_start_time = furi_get_tick();
                
                send_esp32_command(app, "PTT_START");
                
                furi_string_set_str(app->response_text, 
                    "🎤 PUSH-TO-TALK ATTIVO\n\n"
                    "🔴 REGISTRANDO...\n\n"
                    "• Tieni premuto OK\n"
                    "• Parla nel microfono ESP32-CAM\n"
                    "• Rilascia per elaborare\n\n"
                    "🎙️ Registrazione in corso...");
                    
                text_box_set_text(app->text_box, furi_string_get_cstr(app->response_text));
                notification_message(app->notifications, &sequence_single_vibro);
                consumed = true;
            }
        } else if(event->type == InputTypeRelease) {
            // Stop PTT
            if(app->ptt_active) {
                app->ptt_active = false;
                
                uint32_t duration = furi_get_tick() - app->ptt_start_time;
                
                if(duration < 500) {
                    furi_string_set_str(app->response_text,
                        "⚠️ REGISTRAZIONE TROPPO BREVE\n\n"
                        "Tieni premuto OK più a lungo\n"
                        "per registrare comando vocale.\n\n"
                        "Durata minima: 0.5 secondi\n"
                        "Riprova con pressione più lunga.");
                } else {
                    send_esp32_command(app, "PTT_STOP");
                    
                    furi_string_set_str(app->response_text,
                        "🧠 ELABORAZIONE COMANDO VOCALE\n\n"
                        "⏳ ESP32-CAM sta elaborando...\n\n"
                        "• Speech-to-Text in corso\n"
                        "• Interpretazione comando\n"
                        "• Esecuzione azione AI\n\n"
                        "📥 Attendi risposta sul Flipper...");
                }
                
                text_box_set_text(app->text_box, furi_string_get_cstr(app->response_text));
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

// ===== NAVIGATION CALLBACKS =====

static uint32_t gennaro_ai_navigation_exit_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static uint32_t gennaro_ai_navigation_submenu_callback(void* context) {
    UNUSED(context);
    return GennaroAIViewSubmenu;
}

// ===== APP ALLOCATION =====

static GennaroAIApp* gennaro_ai_app_alloc() {
    GennaroAIApp* app = malloc(sizeof(GennaroAIApp));
    if(!app) {
        FURI_LOG_E(TAG, "❌ Failed to allocate app memory");
        return NULL;
    }
    
    // Initialize ALL pointers to NULL first (CRITICAL)
    memset(app, 0, sizeof(GennaroAIApp));
    
    // Initialize strings and state
    app->response_text = furi_string_alloc();
    app->temp_buffer = furi_string_alloc();
    if(!app->response_text || !app->temp_buffer) {
        FURI_LOG_E(TAG, "❌ Failed to allocate strings");
        if(app->response_text) furi_string_free(app->response_text);
        if(app->temp_buffer) furi_string_free(app->temp_buffer);
        free(app);
        return NULL;
    }
    
    app->current_state = StateIdle;
    app->command_count = 0;
    app->ptt_active = false;
    app->ptt_start_time = 0;
    app->response_pos = 0;
    app->response_complete = false;
    memset(app->response_buffer, 0, sizeof(app->response_buffer));
    
    // Initialize communication - SAFE allocation
    app->rx_stream = furi_stream_buffer_alloc(1024, 1);  // Reduced buffer size
    app->data_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    if(!app->rx_stream || !app->data_mutex) {
        FURI_LOG_E(TAG, "❌ Failed to allocate communication resources");
        // Cleanup on failure
        if(app->rx_stream) furi_stream_buffer_free(app->rx_stream);
        if(app->data_mutex) furi_mutex_free(app->data_mutex);
        furi_string_free(app->response_text);
        furi_string_free(app->temp_buffer);
        free(app);
        return NULL;
    }
    
    app->uart_init_by_app = false;
    
    // Get notifications
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    if(!app->notifications) {
        FURI_LOG_E(TAG, "❌ Failed to open notifications");
        furi_stream_buffer_free(app->rx_stream);
        furi_mutex_free(app->data_mutex);
        furi_string_free(app->response_text);
        furi_string_free(app->temp_buffer);
        free(app);
        return NULL;
    }
    
    // Create view dispatcher
    app->view_dispatcher = view_dispatcher_alloc();
    if(!app->view_dispatcher) {
        FURI_LOG_E(TAG, "❌ Failed to allocate view dispatcher");
        furi_record_close(RECORD_NOTIFICATION);
        furi_stream_buffer_free(app->rx_stream);
        furi_mutex_free(app->data_mutex);
        furi_string_free(app->response_text);
        furi_string_free(app->temp_buffer);
        free(app);
        return NULL;
    }
    
    // Create submenu
    app->submenu = submenu_alloc();
    if(!app->submenu) {
        FURI_LOG_E(TAG, "❌ Failed to allocate submenu");
        view_dispatcher_free(app->view_dispatcher);
        furi_record_close(RECORD_NOTIFICATION);
        furi_stream_buffer_free(app->rx_stream);
        furi_mutex_free(app->data_mutex);
        furi_string_free(app->response_text);
        furi_string_free(app->temp_buffer);
        free(app);
        return NULL;
    }
    
    submenu_add_item(app->submenu, "👁️ Vision Analysis", GennaroAIMenuVision, gennaro_ai_submenu_callback, app);
    submenu_add_item(app->submenu, "🧮 Math Solver", GennaroAIMenuMath, gennaro_ai_submenu_callback, app);
    submenu_add_item(app->submenu, "📝 OCR Text", GennaroAIMenuOCR, gennaro_ai_submenu_callback, app);
    submenu_add_item(app->submenu, "🔢 Count Objects", GennaroAIMenuCount, gennaro_ai_submenu_callback, app);
    submenu_add_item(app->submenu, "🎤 Push-to-Talk", GennaroAIMenuPTT, gennaro_ai_submenu_callback, app);
    submenu_add_item(app->submenu, "💡 Flash ON", GennaroAIMenuFlashOn, gennaro_ai_submenu_callback, app);
    submenu_add_item(app->submenu, "🔲 Flash OFF", GennaroAIMenuFlashOff, gennaro_ai_submenu_callback, app);
    submenu_add_item(app->submenu, "🔄 Flash Toggle", GennaroAIMenuFlashToggle, gennaro_ai_submenu_callback, app);
    submenu_add_item(app->submenu, "📊 System Status", GennaroAIMenuStatus, gennaro_ai_submenu_callback, app);
    submenu_add_item(app->submenu, "❓ Help", GennaroAIMenuHelp, gennaro_ai_submenu_callback, app);
    
    view_set_previous_callback(submenu_get_view(app->submenu), gennaro_ai_navigation_exit_callback);
    view_dispatcher_add_view(app->view_dispatcher, GennaroAIViewSubmenu, submenu_get_view(app->submenu));
    
    // Create text box
    app->text_box = text_box_alloc();
    if(!app->text_box) {
        FURI_LOG_E(TAG, "❌ Failed to allocate text box");
        submenu_free(app->submenu);
        view_dispatcher_free(app->view_dispatcher);
        furi_record_close(RECORD_NOTIFICATION);
        furi_stream_buffer_free(app->rx_stream);
        furi_mutex_free(app->data_mutex);
        furi_string_free(app->response_text);
        furi_string_free(app->temp_buffer);
        free(app);
        return NULL;
    }
    
    text_box_set_focus(app->text_box, TextBoxFocusStart);
    view_set_previous_callback(text_box_get_view(app->text_box), gennaro_ai_navigation_submenu_callback);
    view_set_input_callback(text_box_get_view(app->text_box), gennaro_ai_input_callback);
    view_dispatcher_add_view(app->view_dispatcher, GennaroAIViewTextBox, text_box_get_view(app->text_box));
    
    // Create loading view
    app->loading = loading_alloc();
    if(!app->loading) {
        FURI_LOG_E(TAG, "❌ Failed to allocate loading view");
        text_box_free(app->text_box);
        submenu_free(app->submenu);
        view_dispatcher_free(app->view_dispatcher);
        furi_record_close(RECORD_NOTIFICATION);
        furi_stream_buffer_free(app->rx_stream);
        furi_mutex_free(app->data_mutex);
        furi_string_free(app->response_text);
        furi_string_free(app->temp_buffer);
        free(app);
        return NULL;
    }
    
    view_set_previous_callback(loading_get_view(app->loading), gennaro_ai_navigation_submenu_callback);
    view_dispatcher_add_view(app->view_dispatcher, GennaroAIViewLoading, loading_get_view(app->loading));
    
    FURI_LOG_I(TAG, "✅ App structure allocated successfully");
    return app;
}

// ===== APP DEALLOCATION =====

static void gennaro_ai_app_free(GennaroAIApp* app) {
    if(!app) return;  // Safety check
    
    FURI_LOG_I(TAG, "🔄 Starting app cleanup...");
    
    // Stop UART thread FIRST and SAFELY
    if(app->uart_thread) {
        FURI_LOG_I(TAG, "Stopping UART thread...");
        furi_thread_flags_set(furi_thread_get_id(app->uart_thread), WorkerEventExiting);
        furi_thread_join(app->uart_thread);
        furi_thread_free(app->uart_thread);
        app->uart_thread = NULL;
    }
    
    // Deinitialize UART BEFORE freeing anything else
    deinit_uart(app);
    
    // Remove views from dispatcher BEFORE freeing them
    if(app->view_dispatcher) {
        if(app->submenu) {
            view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewSubmenu);
        }
        if(app->text_box) {
            view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewTextBox);
        }
        if(app->loading) {
            view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewLoading);
        }
    }
    
    // Free views SAFELY
    if(app->submenu) {
        submenu_free(app->submenu);
        app->submenu = NULL;
    }
    if(app->text_box) {
        text_box_free(app->text_box);
        app->text_box = NULL;
    }
    if(app->loading) {
        loading_free(app->loading);
        app->loading = NULL;
    }
    
    // Free view dispatcher
    if(app->view_dispatcher) {
        view_dispatcher_free(app->view_dispatcher);
        app->view_dispatcher = NULL;
    }
    
    // Free communication resources
    if(app->rx_stream) {
        furi_stream_buffer_free(app->rx_stream);
        app->rx_stream = NULL;
    }
    if(app->data_mutex) {
        furi_mutex_free(app->data_mutex);
        app->data_mutex = NULL;
    }
    
    // Free strings
    if(app->response_text) {
        furi_string_free(app->response_text);
        app->response_text = NULL;
    }
    if(app->temp_buffer) {
        furi_string_free(app->temp_buffer);
        app->temp_buffer = NULL;
    }
    
    // Close notifications
    if(app->notifications) {
        furi_record_close(RECORD_NOTIFICATION);
        app->notifications = NULL;
    }
    
    // Final cleanup
    free(app);
    
    FURI_LOG_I(TAG, "✅ App cleanup completed");
}

// ===== MAIN APP ENTRY POINT =====

int32_t gennaro_ai_app(void* p) {
    UNUSED(p);
    
    FURI_LOG_I(TAG, "🚀 Starting Gennaro AI v2.0 for Momentum Firmware");
    
    GennaroAIApp* app = gennaro_ai_app_alloc();
    if(!app) {
        FURI_LOG_E(TAG, "❌ Failed to allocate app - exiting");
        return -1;
    }
    
    // Initialize UART communication AFTER all GUI is ready
    FURI_LOG_I(TAG, "Initializing UART communication...");
    init_uart(app);
    
    // Start UART worker thread ONLY if UART is initialized
    if(app->serial_handle || true) {  // Always start thread for GPIO fallback
        app->uart_thread = furi_thread_alloc_ex("GennaroAI_UART", 1024, uart_worker, app);
        if(app->uart_thread) {
            furi_thread_start(app->uart_thread);
            FURI_LOG_I(TAG, "✅ UART worker thread started");
        } else {
            FURI_LOG_W(TAG, "⚠️ Failed to start UART thread - continuing without");
        }
    }
    
    // Attach to GUI
    Gui* gui = furi_record_open(RECORD_GUI);
    if(!gui) {
        FURI_LOG_E(TAG, "❌ Failed to open GUI record");
        gennaro_ai_app_free(app);
        return -1;
    }
    
    view_dispatcher_attach_to_gui(app->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    
    // Set starting view
    view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewSubmenu);
    
    // Welcome notification
    notification_message(app->notifications, &sequence_display_backlight_on);
    notification_message(app->notifications, &sequence_single_vibro);
    
    // Give ESP32 time to boot, then send status check
    furi_delay_ms(2000);
    if(app->current_state == StateIdle) {  // Only if app is ready
        send_esp32_command(app, "STATUS");
    }
    
    FURI_LOG_I(TAG, "✅ App initialized successfully - Ready for ESP32 responses!");
    
    // Run view dispatcher (this blocks until app exits)
    view_dispatcher_run(app->view_dispatcher);
    
    // Cleanup
    furi_record_close(RECORD_GUI);
    gennaro_ai_app_free(app);
    
    FURI_LOG_I(TAG, "🛑 Gennaro AI v2.0 terminated cleanly");
    
    return 0;
}
