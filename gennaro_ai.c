/*
 * GENNARO AI - REAL UART RECEPTION
 * Riceve davvero i dati seriali dall'ESP32
 */

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <notification/notification_messages.h>
#include <furi_hal_gpio.h>

#define TAG "GennaroAI"

// GPIO PINS
#define ESP32_TX_PIN (&gpio_ext_pc1)  // GPIO14 -> ESP32 RX
#define ESP32_RX_PIN (&gpio_ext_pc0)  // GPIO13 <- ESP32 TX

// UART SETTINGS
#define UART_BAUD_RATE 115200
#define BIT_TIME_US (1000000 / UART_BAUD_RATE)  // ~8.68µs per bit

// SAFE BUFFER SIZES
#define MONITOR_BUFFER_SIZE 2048
#define RECEIVE_BUFFER_SIZE 256

// Views
typedef enum {
    GennaroAIViewSubmenu,
    GennaroAIViewMonitor,
    GennaroAIViewCommands,
} GennaroAIView;

typedef enum {
    GennaroAIMenuMonitor,
    GennaroAIMenuCommands,
    GennaroAIMenuHelp,
} GennaroAIMainMenu;

typedef enum {
    GennaroAICommandVision,
    GennaroAICommandMath,
    GennaroAICommandOCR,
    GennaroAICommandStatus,
    GennaroAICommandRefresh,
} GennaroAICommand;

// App context
typedef struct {
    ViewDispatcher* view_dispatcher;
    Submenu* main_submenu;
    Submenu* commands_submenu;
    TextBox* monitor_text_box;
    NotificationApp* notifications;
    
    FuriString* monitor_buffer;
    char receive_line_buffer[RECEIVE_BUFFER_SIZE];
    size_t receive_pos;
    uint32_t commands_sent;
    bool monitor_active;
    
    FuriTimer* uart_timer;
} GennaroAIApp;

// ===== REAL UART FUNCTIONS =====

static bool uart_read_byte(uint8_t* byte) {
    if(!byte) return false;
    
    // Wait for start bit (falling edge) with timeout
    uint32_t timeout = 1000;
    while(furi_hal_gpio_read(ESP32_RX_PIN) && timeout > 0) {
        furi_delay_us(1);
        timeout--;
    }
    
    if(timeout == 0) return false; // No start bit
    
    // Wait to middle of start bit
    furi_delay_us(BIT_TIME_US / 2);
    
    // Verify we're still in start bit
    if(furi_hal_gpio_read(ESP32_RX_PIN)) {
        return false; // False start bit
    }
    
    uint8_t data = 0;
    
    // Read 8 data bits (LSB first)
    for(int i = 0; i < 8; i++) {
        furi_delay_us(BIT_TIME_US);
        if(furi_hal_gpio_read(ESP32_RX_PIN)) {
            data |= (1 << i);
        }
    }
    
    // Wait for stop bit
    furi_delay_us(BIT_TIME_US);
    
    // Verify stop bit is high
    if(!furi_hal_gpio_read(ESP32_RX_PIN)) {
        return false; // Invalid stop bit
    }
    
    *byte = data;
    return true;
}

static void uart_send_string(const char* str) {
    if(!str) return;
    
    size_t len = strlen(str);
    
    for(size_t i = 0; i < len; i++) {
        char c = str[i];
        
        // Send start bit (LOW)
        furi_hal_gpio_write(ESP32_TX_PIN, false);
        furi_delay_us(BIT_TIME_US);
        
        // Send 8 data bits (LSB first)
        for(int bit = 0; bit < 8; bit++) {
            if(c & (1 << bit)) {
                furi_hal_gpio_write(ESP32_TX_PIN, true);
            } else {
                furi_hal_gpio_write(ESP32_TX_PIN, false);
            }
            furi_delay_us(BIT_TIME_US);
        }
        
        // Send stop bit (HIGH)
        furi_hal_gpio_write(ESP32_TX_PIN, true);
        furi_delay_us(BIT_TIME_US);
    }
    
    // Send newline
    // Start bit
    furi_hal_gpio_write(ESP32_TX_PIN, false);
    furi_delay_us(BIT_TIME_US);
    
    // Newline character (0x0A = 00001010)
    uint8_t newline = 0x0A;
    for(int bit = 0; bit < 8; bit++) {
        if(newline & (1 << bit)) {
            furi_hal_gpio_write(ESP32_TX_PIN, true);
        } else {
            furi_hal_gpio_write(ESP32_TX_PIN, false);
        }
        furi_delay_us(BIT_TIME_US);
    }
    
    // Stop bit
    furi_hal_gpio_write(ESP32_TX_PIN, true);
    furi_delay_us(BIT_TIME_US);
}

static void uart_timer_callback(void* context) {
    GennaroAIApp* app = (GennaroAIApp*)context;
    if(!app || !app->monitor_active) return;
    
    // Try to read incoming bytes
    uint8_t byte;
    int bytes_read = 0;
    
    // Limit reads per timer tick to prevent blocking
    while(bytes_read < 5 && uart_read_byte(&byte)) {
        if(byte >= 32 && byte <= 126) { // Printable ASCII
            if(app->receive_pos < RECEIVE_BUFFER_SIZE - 1) {
                app->receive_line_buffer[app->receive_pos++] = byte;
            }
        } else if(byte == '\n' || byte == '\r') {
            // End of line - process complete line
            if(app->receive_pos > 0) {
                app->receive_line_buffer[app->receive_pos] = '\0';
                
                // Add received line to monitor
                add_monitor_line(app, app->receive_line_buffer);
                
                // Reset buffer
                app->receive_pos = 0;
            }
        }
        // Ignore other control characters
        
        bytes_read++;
    }
}

// ===== MONITOR FUNCTIONS =====

static void add_monitor_line(GennaroAIApp* app, const char* line) {
    if(!app || !line || !app->monitor_buffer) return;
    
    uint32_t seconds = furi_get_tick() / 1000;
    char timestamped_line[300];
    
    int result = snprintf(timestamped_line, sizeof(timestamped_line),
                         "[%02lu:%02lu] %s\n",
                         (seconds / 60) % 60, seconds % 60, line);
    
    if(result < 0 || result >= (int)sizeof(timestamped_line)) return;
    
    // Check buffer size
    if(furi_string_size(app->monitor_buffer) > MONITOR_BUFFER_SIZE - 400) {
        // Clear old data
        const char* buffer_str = furi_string_get_cstr(app->monitor_buffer);
        const char* halfway = buffer_str + (furi_string_size(app->monitor_buffer) / 2);
        const char* newline = strchr(halfway, '\n');
        if(newline) {
            furi_string_set_str(app->monitor_buffer, newline + 1);
        } else {
            furi_string_reset(app->monitor_buffer);
        }
    }
    
    furi_string_cat_str(app->monitor_buffer, timestamped_line);
    
    // Update display
    if(app->monitor_active && app->monitor_text_box) {
        text_box_set_text(app->monitor_text_box, furi_string_get_cstr(app->monitor_buffer));
        text_box_set_focus(app->monitor_text_box, TextBoxFocusEnd);
    }
}

static void clear_monitor(GennaroAIApp* app) {
    if(!app || !app->monitor_buffer) return;
    
    furi_string_reset(app->monitor_buffer);
    app->receive_pos = 0;
    
    add_monitor_line(app, "=== MONITOR CLEARED ===");
    add_monitor_line(app, "Gennaro AI Real UART Monitor");
    add_monitor_line(app, "Listening for ESP32 responses...");
}

static void send_command(GennaroAIApp* app, const char* command) {
    if(!app || !command) return;
    
    FURI_LOG_I(TAG, "Sending UART: %s", command);
    
    // Send real UART command to ESP32
    uart_send_string(command);
    
    app->commands_sent++;
    
    // Show in monitor
    char cmd_info[128];
    snprintf(cmd_info, sizeof(cmd_info), ">>> SENT: %s", command);
    add_monitor_line(app, cmd_info);
    
    // Vibration
    if(app->notifications) {
        notification_message(app->notifications, &sequence_single_vibro);
    }
}

// ===== CALLBACKS =====

static void main_submenu_callback(void* context, uint32_t index) {
    GennaroAIApp* app = (GennaroAIApp*)context;
    if(!app) return;
    
    switch(index) {
        case GennaroAIMenuMonitor:
            app->monitor_active = true;
            if(app->uart_timer) {
                furi_timer_start(app->uart_timer, 100); // Check every 100ms
            }
            view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewMonitor);
            break;
            
        case GennaroAIMenuCommands:
            view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewCommands);
            break;
            
        case GennaroAIMenuHelp:
            clear_monitor(app);
            add_monitor_line(app, "=== GENNARO AI REAL UART ===");
            add_monitor_line(app, "");
            add_monitor_line(app, "CONNECTIONS:");
            add_monitor_line(app, "Flipper GPIO13 <- ESP32 GPIO1 (TX)");
            add_monitor_line(app, "Flipper GPIO14 -> ESP32 GPIO3 (RX)");
            add_monitor_line(app, "Flipper 5V -> ESP32 5V");
            add_monitor_line(app, "Flipper GND -> ESP32 GND");
            add_monitor_line(app, "");
            add_monitor_line(app, "FEATURES:");
            add_monitor_line(app, "- Real 115200 baud UART communication");
            add_monitor_line(app, "- Receives actual ESP32 responses");
            add_monitor_line(app, "- Shows Claude AI output in real-time");
            add_monitor_line(app, "- Bit-banged software UART");
            app->monitor_active = true;
            if(app->uart_timer) {
                furi_timer_start(app->uart_timer, 100);
            }
            view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewMonitor);
            break;
    }
}

static void commands_submenu_callback(void* context, uint32_t index) {
    GennaroAIApp* app = (GennaroAIApp*)context;
    if(!app) return;
    
    switch(index) {
        case GennaroAICommandVision:
            send_command(app, "VISION");
            break;
        case GennaroAICommandMath:
            send_command(app, "MATH");
            break;
        case GennaroAICommandOCR:
            send_command(app, "OCR");
            break;
        case GennaroAICommandStatus:
            send_command(app, "STATUS");
            break;
        case GennaroAICommandRefresh:
            add_monitor_line(app, "--- MANUAL REFRESH ---");
            add_monitor_line(app, "Timer-based UART reading active");
            break;
    }
    
    // Auto-switch to monitor
    app->monitor_active = true;
    if(app->uart_timer) {
        furi_timer_start(app->uart_timer, 100);
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewMonitor);
}

static bool monitor_input_callback(InputEvent* event, void* context) {
    GennaroAIApp* app = (GennaroAIApp*)context;
    if(!app || !event) return false;
    
    if(event->type == InputTypePress) {
        switch(event->key) {
            case InputKeyBack:
                app->monitor_active = false;
                if(app->uart_timer) {
                    furi_timer_stop(app->uart_timer);
                }
                view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewSubmenu);
                return true;
                
            case InputKeyOk:
                view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewCommands);
                return true;
                
            case InputKeyUp:
                send_command(app, "STATUS");
                return true;
                
            case InputKeyDown:
                clear_monitor(app);
                return true;
                
            case InputKeyLeft:
                add_monitor_line(app, "--- UART STATUS ---");
                bool rx_state = furi_hal_gpio_read(ESP32_RX_PIN);
                char state_msg[64];
                snprintf(state_msg, sizeof(state_msg), "GPIO13 (RX): %s", rx_state ? "HIGH" : "LOW");
                add_monitor_line(app, state_msg);
                return true;
                
            case InputKeyRight:
                send_command(app, "PING");
                return true;
        }
    }
    
    return false;
}

// Navigation callbacks
static uint32_t navigation_exit_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static uint32_t navigation_submenu_callback(void* context) {
    UNUSED(context);
    return GennaroAIViewSubmenu;
}

// ===== APP ALLOCATION =====

static GennaroAIApp* gennaro_ai_app_alloc() {
    GennaroAIApp* app = malloc(sizeof(GennaroAIApp));
    if(!app) return NULL;
    
    memset(app, 0, sizeof(GennaroAIApp));
    
    // Allocate monitor buffer
    app->monitor_buffer = furi_string_alloc();
    if(!app->monitor_buffer) {
        free(app);
        return NULL;
    }
    
    // Notifications
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    
    // View dispatcher
    app->view_dispatcher = view_dispatcher_alloc();
    if(!app->view_dispatcher) {
        if(app->notifications) furi_record_close(RECORD_NOTIFICATION);
        furi_string_free(app->monitor_buffer);
        free(app);
        return NULL;
    }
    
    // Create UART timer
    app->uart_timer = furi_timer_alloc(uart_timer_callback, FuriTimerTypePeriodic, app);
    if(!app->uart_timer) {
        view_dispatcher_free(app->view_dispatcher);
        if(app->notifications) furi_record_close(RECORD_NOTIFICATION);
        furi_string_free(app->monitor_buffer);
        free(app);
        return NULL;
    }
    
    // Main submenu
    app->main_submenu = submenu_alloc();
    if(!app->main_submenu) {
        furi_timer_free(app->uart_timer);
        view_dispatcher_free(app->view_dispatcher);
        if(app->notifications) furi_record_close(RECORD_NOTIFICATION);
        furi_string_free(app->monitor_buffer);
        free(app);
        return NULL;
    }
    
    submenu_add_item(app->main_submenu, "📺 UART Monitor", GennaroAIMenuMonitor, main_submenu_callback, app);
    submenu_add_item(app->main_submenu, "📤 Send Commands", GennaroAIMenuCommands, main_submenu_callback, app);
    submenu_add_item(app->main_submenu, "❓ Help & Info", GennaroAIMenuHelp, main_submenu_callback, app);
    
    view_set_previous_callback(submenu_get_view(app->main_submenu), navigation_exit_callback);
    view_dispatcher_add_view(app->view_dispatcher, GennaroAIViewSubmenu, submenu_get_view(app->main_submenu));
    
    // Commands submenu
    app->commands_submenu = submenu_alloc();
    if(!app->commands_submenu) {
        view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewSubmenu);
        submenu_free(app->main_submenu);
        furi_timer_free(app->uart_timer);
        view_dispatcher_free(app->view_dispatcher);
        if(app->notifications) furi_record_close(RECORD_NOTIFICATION);
        furi_string_free(app->monitor_buffer);
        free(app);
        return NULL;
    }
    
    submenu_add_item(app->commands_submenu, "👁️ VISION", GennaroAICommandVision, commands_submenu_callback, app);
    submenu_add_item(app->commands_submenu, "🧮 MATH", GennaroAICommandMath, commands_submenu_callback, app);
    submenu_add_item(app->commands_submenu, "📝 OCR", GennaroAICommandOCR, commands_submenu_callback, app);
    submenu_add_item(app->commands_submenu, "📊 STATUS", GennaroAICommandStatus, commands_submenu_callback, app);
    submenu_add_item(app->commands_submenu, "🔄 REFRESH", GennaroAICommandRefresh, commands_submenu_callback, app);
    
    view_set_previous_callback(submenu_get_view(app->commands_submenu), navigation_submenu_callback);
    view_dispatcher_add_view(app->view_dispatcher, GennaroAIViewCommands, submenu_get_view(app->commands_submenu));
    
    // Monitor text box
    app->monitor_text_box = text_box_alloc();
    if(!app->monitor_text_box) {
        view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewCommands);
        view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewSubmenu);
        submenu_free(app->commands_submenu);
        submenu_free(app->main_submenu);
        furi_timer_free(app->uart_timer);
        view_dispatcher_free(app->view_dispatcher);
        if(app->notifications) furi_record_close(RECORD_NOTIFICATION);
        furi_string_free(app->monitor_buffer);
        free(app);
        return NULL;
    }
    
    text_box_set_font(app->monitor_text_box, TextBoxFontText);
    view_set_previous_callback(text_box_get_view(app->monitor_text_box), navigation_submenu_callback);
    view_set_input_callback(text_box_get_view(app->monitor_text_box), monitor_input_callback);
    view_dispatcher_add_view(app->view_dispatcher, GennaroAIViewMonitor, text_box_get_view(app->monitor_text_box));
    
    // Initialize
    clear_monitor(app);
    
    return app;
}

static void gennaro_ai_app_free(GennaroAIApp* app) {
    if(!app) return;
    
    // Stop and free timer
    if(app->uart_timer) {
        furi_timer_stop(app->uart_timer);
        furi_timer_free(app->uart_timer);
    }
    
    // Remove views
    view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewCommands);
    view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewMonitor);
    
    // Free views
    submenu_free(app->main_submenu);
    submenu_free(app->commands_submenu);
    text_box_free(app->monitor_text_box);
    view_dispatcher_free(app->view_dispatcher);
    
    // Free resources
    furi_string_free(app->monitor_buffer);
    if(app->notifications) furi_record_close(RECORD_NOTIFICATION);
    
    free(app);
}

// ===== MAIN ENTRY POINT =====

int32_t gennaro_ai_app(void* p) {
    UNUSED(p);
    
    FURI_LOG_I(TAG, "Starting Gennaro AI Real UART");
    
    // Initialize GPIO
    furi_hal_gpio_init(ESP32_TX_PIN, GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);
    furi_hal_gpio_init(ESP32_RX_PIN, GpioModeInput, GpioPullUp, GpioSpeedVeryHigh);
    furi_hal_gpio_write(ESP32_TX_PIN, true); // UART idle state
    
    // Allocate app
    GennaroAIApp* app = gennaro_ai_app_alloc();
    if(!app) {
        FURI_LOG_E(TAG, "Failed to allocate app");
        return -1;
    }
    
    // Attach to GUI
    Gui* gui = furi_record_open(RECORD_GUI);
    view_dispatcher_attach_to_gui(app->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    
    // Set starting view
    view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewSubmenu);
    
    // Welcome
    if(app->notifications) {
        notification_message(app->notifications, &sequence_display_backlight_on);
        notification_message(app->notifications, &sequence_single_vibro);
    }
    
    add_monitor_line(app, "=== GENNARO AI REAL UART STARTED ===");
    add_monitor_line(app, "Software UART @ 115200 baud");
    add_monitor_line(app, "Ready to receive ESP32 responses!");
    
    FURI_LOG_I(TAG, "Real UART app started");
    
    // Run
    view_dispatcher_run(app->view_dispatcher);
    
    // Cleanup
    furi_record_close(RECORD_GUI);
    gennaro_ai_app_free(app);
    
    FURI_LOG_I(TAG, "Real UART app terminated");
    return 0;
}
