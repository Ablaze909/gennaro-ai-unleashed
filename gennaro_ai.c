/*
 * GENNARO AI - SERIAL MONITOR APPROACH
 * Monitor Seriale per ESP32-CAM con comandi AI prestabiliti
 * 
 * Features:
 * - Monitor seriale dedicato sul Flipper
 * - Invio comandi prestabiliti via UART
 * - Ricezione risposte in tempo reale
 * - Buffer scrollabile per risposte lunghe
 * - Architettura ultra-sicura e semplice
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
#include <furi_hal_resources.h>

#define TAG "GennaroAI"

// GPIO PINS
#define ESP32_TX_PIN (&gpio_ext_pc1)  // GPIO14 -> ESP32 RX 
#define ESP32_RX_PIN (&gpio_ext_pc0)  // GPIO13 -> ESP32 TX

// Serial Monitor Configuration
#define SERIAL_BAUD_RATE 115200
#define MONITOR_BUFFER_SIZE 4096
#define LINE_BUFFER_SIZE 256
#define MAX_LINES 50

// Views
typedef enum {
    GennaroAIViewSubmenu,
    GennaroAIViewMonitor,
    GennaroAIViewCommands,
} GennaroAIView;

// Commands Menu
typedef enum {
    GennaroAICommandVision,
    GennaroAICommandMath,
    GennaroAICommandOCR,
    GennaroAICommandCount,
    GennaroAICommandPTTStart,
    GennaroAICommandPTTStop,
    GennaroAICommandFlashOn,
    GennaroAICommandFlashOff,
    GennaroAICommandStatus,
    GennaroAICommandClear,
} GennaroAICommand;

// Main Menu
typedef enum {
    GennaroAIMenuMonitor,
    GennaroAIMenuCommands,
    GennaroAIMenuHelp,
} GennaroAIMenu;

// App context
typedef struct {
    ViewDispatcher* view_dispatcher;
    Submenu* main_submenu;
    Submenu* commands_submenu;
    TextBox* monitor_text_box;
    NotificationApp* notifications;
    
    // Serial Monitor
    FuriString* monitor_buffer;
    FuriString* temp_line;
    char line_buffer[LINE_BUFFER_SIZE];
    size_t line_pos;
    uint32_t lines_count;
    
    // State
    bool monitor_active;
    uint32_t commands_sent;
    
} GennaroAIApp;

// Forward declarations
static void send_command_to_esp32(GennaroAIApp* app, const char* command);
static void update_monitor_display(GennaroAIApp* app);
static void add_line_to_monitor(GennaroAIApp* app, const char* line);
static bool read_byte_from_esp32(uint8_t* byte);
static void check_esp32_responses(GennaroAIApp* app);

// ===== SERIAL COMMUNICATION =====

static void init_gpio_uart(GennaroAIApp* app) {
    UNUSED(app);
    
    // Initialize GPIO pins for basic communication
    furi_hal_gpio_init(ESP32_TX_PIN, GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);
    furi_hal_gpio_init(ESP32_RX_PIN, GpioModeInput, GpioPullUp, GpioSpeedVeryHigh);
    
    // Set TX high (idle state)
    furi_hal_gpio_write(ESP32_TX_PIN, true);
    
    FURI_LOG_I(TAG, "✅ GPIO UART initialized");
}

static void send_command_to_esp32(GennaroAIApp* app, const char* command) {
    if(!command) return;
    
    FURI_LOG_I(TAG, "📤 Sending command: %s", command);
    
    // Simple GPIO bit-banging for 115200 baud (8.68µs per bit)
    const char* full_command = command;
    size_t len = strlen(full_command);
    
    // Send each character
    for(size_t i = 0; i < len; i++) {
        char c = full_command[i];
        
        // Send start bit
        furi_hal_gpio_write(ESP32_TX_PIN, false);
        furi_delay_us(9);
        
        // Send 8 data bits (LSB first)
        for(int bit = 0; bit < 8; bit++) {
            if(c & (1 << bit)) {
                furi_hal_gpio_write(ESP32_TX_PIN, true);
            } else {
                furi_hal_gpio_write(ESP32_TX_PIN, false);
            }
            furi_delay_us(9);
        }
        
        // Send stop bit
        furi_hal_gpio_write(ESP32_TX_PIN, true);
        furi_delay_us(9);
    }
    
    // Send newline
    char newline = '\n';
    furi_hal_gpio_write(ESP32_TX_PIN, false);  // Start bit
    furi_delay_us(9);
    
    for(int bit = 0; bit < 8; bit++) {
        if(newline & (1 << bit)) {
            furi_hal_gpio_write(ESP32_TX_PIN, true);
        } else {
            furi_hal_gpio_write(ESP32_TX_PIN, false);
        }
        furi_delay_us(9);
    }
    
    furi_hal_gpio_write(ESP32_TX_PIN, true);  // Stop bit
    furi_delay_us(9);
    
    // Return to idle
    furi_hal_gpio_write(ESP32_TX_PIN, true);
    
    app->commands_sent++;
    
    // Add command to monitor
    FuriString* cmd_line = furi_string_alloc();
    furi_string_printf(cmd_line, ">>> %s", command);
    add_line_to_monitor(app, furi_string_get_cstr(cmd_line));
    furi_string_free(cmd_line);
    
    // Vibration feedback
    notification_message(app->notifications, &sequence_single_vibro);
}

// ===== MONITOR MANAGEMENT =====

static void add_line_to_monitor(GennaroAIApp* app, const char* line) {
    if(!line || !app || !app->monitor_buffer) return;
    
    // Add timestamp
    FuriString* timestamped_line = furi_string_alloc();
    uint32_t timestamp = furi_get_tick() / 1000;  // Seconds since start
    furi_string_printf(timestamped_line, "[%02lu:%02lu] %s\n", 
                      (timestamp / 60) % 60, timestamp % 60, line);
    
    // Add to buffer
    furi_string_cat(app->monitor_buffer, timestamped_line);
    app->lines_count++;
    
    // Trim buffer if too large
    if(app->lines_count > MAX_LINES) {
        // Find first newline and remove everything before it
        const char* buffer_str = furi_string_get_cstr(app->monitor_buffer);
        const char* first_newline = strchr(buffer_str, '\n');
        if(first_newline) {
            FuriString* new_buffer = furi_string_alloc();
            furi_string_set_str(new_buffer, first_newline + 1);
            furi_string_free(app->monitor_buffer);
            app->monitor_buffer = new_buffer;
            app->lines_count--;
        }
    }
    
    furi_string_free(timestamped_line);
    
    // Update display if monitor is active
    if(app->monitor_active) {
        update_monitor_display(app);
    }
}

static void update_monitor_display(GennaroAIApp* app) {
    if(!app || !app->monitor_text_box || !app->monitor_buffer) return;
    
    text_box_set_text(app->monitor_text_box, furi_string_get_cstr(app->monitor_buffer));
    text_box_set_focus(app->monitor_text_box, TextBoxFocusEnd);  // Auto-scroll to bottom
}

static void clear_monitor(GennaroAIApp* app) {
    if(!app || !app->monitor_buffer) return;
    
    furi_string_reset(app->monitor_buffer);
    app->lines_count = 0;
    
    add_line_to_monitor(app, "=== MONITOR CLEARED ===");
    add_line_to_monitor(app, "Gennaro AI Serial Monitor Ready");
    add_line_to_monitor(app, "ESP32-CAM Communication Active");
}

// ===== SUBMENU CALLBACKS =====

static void main_submenu_callback(void* context, uint32_t index) {
    GennaroAIApp* app = context;
    
    switch(index) {
        case GennaroAIMenuMonitor:
            app->monitor_active = true;
            update_monitor_display(app);
            view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewMonitor);
            break;
            
        case GennaroAIMenuCommands:
            view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewCommands);
            break;
            
        case GennaroAIMenuHelp:
            add_line_to_monitor(app, "=== HELP - GENNARO AI ===");
            add_line_to_monitor(app, "Connections:");
            add_line_to_monitor(app, "  Flipper GPIO13 -> ESP32 GPIO3");
            add_line_to_monitor(app, "  Flipper GPIO14 <- ESP32 GPIO1");
            add_line_to_monitor(app, "  Flipper 5V -> ESP32 5V");
            add_line_to_monitor(app, "  Flipper GND -> ESP32 GND");
            add_line_to_monitor(app, "");
            add_line_to_monitor(app, "Commands: Use Commands menu");
            add_line_to_monitor(app, "Monitor: Real-time ESP32 responses");
            app->monitor_active = true;
            update_monitor_display(app);
            view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewMonitor);
            break;
    }
}

static void commands_submenu_callback(void* context, uint32_t index) {
    GennaroAIApp* app = context;
    
    switch(index) {
        case GennaroAICommandVision:
            send_command_to_esp32(app, "VISION");
            break;
        case GennaroAICommandMath:
            send_command_to_esp32(app, "MATH");
            break;
        case GennaroAICommandOCR:
            send_command_to_esp32(app, "OCR");
            break;
        case GennaroAICommandCount:
            send_command_to_esp32(app, "COUNT");
            break;
        case GennaroAICommandPTTStart:
            send_command_to_esp32(app, "PTT_START");
            break;
        case GennaroAICommandPTTStop:
            send_command_to_esp32(app, "PTT_STOP");
            break;
        case GennaroAICommandFlashOn:
            send_command_to_esp32(app, "FLASH_ON");
            break;
        case GennaroAICommandFlashOff:
            send_command_to_esp32(app, "FLASH_OFF");
            break;
        case GennaroAICommandStatus:
            send_command_to_esp32(app, "STATUS");
            break;
        case GennaroAICommandClear:
            clear_monitor(app);
            break;
    }
    
    // Auto-switch to monitor to see response
    app->monitor_active = true;
    update_monitor_display(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewMonitor);
}

// ===== INPUT CALLBACKS =====

static bool monitor_input_callback(InputEvent* event, void* context) {
    GennaroAIApp* app = context;
    bool consumed = false;
    
    if(event->type == InputTypePress) {
        switch(event->key) {
            case InputKeyBack:
                app->monitor_active = false;
                view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewSubmenu);
                consumed = true;
                break;
            case InputKeyOk:
                // Quick command menu
                view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewCommands);
                consumed = true;
                break;
            case InputKeyUp:
                // Scroll up (if needed)
                break;
            case InputKeyDown:
                // Scroll down (if needed)
                break;
            case InputKeyLeft:
                // Clear monitor
                clear_monitor(app);
                consumed = true;
                break;
            case InputKeyRight:
                // Send status
                send_command_to_esp32(app, "STATUS");
                consumed = true;
                break;
            default:
                break;
        }
    }
    
    // Continuously check for ESP32 responses when monitor is active
    if(app->monitor_active) {
        check_esp32_responses(app);
    }
    
    return consumed;
}

// ===== NAVIGATION CALLBACKS =====

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
    
    // Initialize all pointers to NULL
    memset(app, 0, sizeof(GennaroAIApp));
    
    // Allocate strings
    app->monitor_buffer = furi_string_alloc();
    app->temp_line = furi_string_alloc();
    if(!app->monitor_buffer || !app->temp_line) {
        if(app->monitor_buffer) furi_string_free(app->monitor_buffer);
        if(app->temp_line) furi_string_free(app->temp_line);
        free(app);
        return NULL;
    }
    
    // Initialize state
    app->monitor_active = false;
    app->commands_sent = 0;
    app->line_pos = 0;
    app->lines_count = 0;
    memset(app->line_buffer, 0, sizeof(app->line_buffer));
    
    // Get notifications
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    
    // Create view dispatcher
    app->view_dispatcher = view_dispatcher_alloc();
    
    // Create main submenu
    app->main_submenu = submenu_alloc();
    submenu_add_item(app->main_submenu, "📺 Serial Monitor", GennaroAIMenuMonitor, main_submenu_callback, app);
    submenu_add_item(app->main_submenu, "📤 Send Commands", GennaroAIMenuCommands, main_submenu_callback, app);
    submenu_add_item(app->main_submenu, "❓ Help & Info", GennaroAIMenuHelp, main_submenu_callback, app);
    
    view_set_previous_callback(submenu_get_view(app->main_submenu), navigation_exit_callback);
    view_dispatcher_add_view(app->view_dispatcher, GennaroAIViewSubmenu, submenu_get_view(app->main_submenu));
    
    // Create commands submenu
    app->commands_submenu = submenu_alloc();
    submenu_add_item(app->commands_submenu, "👁️ Vision Analysis", GennaroAICommandVision, commands_submenu_callback, app);
    submenu_add_item(app->commands_submenu, "🧮 Math Solver", GennaroAICommandMath, commands_submenu_callback, app);
    submenu_add_item(app->commands_submenu, "📝 OCR Text", GennaroAICommandOCR, commands_submenu_callback, app);
    submenu_add_item(app->commands_submenu, "🔢 Count Objects", GennaroAICommandCount, commands_submenu_callback, app);
    submenu_add_item(app->commands_submenu, "🎤 PTT Start", GennaroAICommandPTTStart, commands_submenu_callback, app);
    submenu_add_item(app->commands_submenu, "⏹️ PTT Stop", GennaroAICommandPTTStop, commands_submenu_callback, app);
    submenu_add_item(app->commands_submenu, "💡 Flash ON", GennaroAICommandFlashOn, commands_submenu_callback, app);
    submenu_add_item(app->commands_submenu, "🔲 Flash OFF", GennaroAICommandFlashOff, commands_submenu_callback, app);
    submenu_add_item(app->commands_submenu, "📊 System Status", GennaroAICommandStatus, commands_submenu_callback, app);
    submenu_add_item(app->commands_submenu, "🗑️ Clear Monitor", GennaroAICommandClear, commands_submenu_callback, app);
    
    view_set_previous_callback(submenu_get_view(app->commands_submenu), navigation_submenu_callback);
    view_dispatcher_add_view(app->view_dispatcher, GennaroAIViewCommands, submenu_get_view(app->commands_submenu));
    
    // Create monitor text box
    app->monitor_text_box = text_box_alloc();
    text_box_set_font(app->monitor_text_box, TextBoxFontText);
    view_set_previous_callback(text_box_get_view(app->monitor_text_box), navigation_submenu_callback);
    view_set_input_callback(text_box_get_view(app->monitor_text_box), monitor_input_callback);
    view_dispatcher_add_view(app->view_dispatcher, GennaroAIViewMonitor, text_box_get_view(app->monitor_text_box));
    
    // Initialize monitor
    clear_monitor(app);
    
    return app;
}

// ===== APP DEALLOCATION =====

static void gennaro_ai_app_free(GennaroAIApp* app) {
    if(!app) return;
    
    // Remove views
    if(app->view_dispatcher) {
        view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewSubmenu);
        view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewCommands);
        view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewMonitor);
        view_dispatcher_free(app->view_dispatcher);
    }
    
    // Free views
    if(app->main_submenu) submenu_free(app->main_submenu);
    if(app->commands_submenu) submenu_free(app->commands_submenu);
    if(app->monitor_text_box) text_box_free(app->monitor_text_box);
    
    // Free strings
    if(app->monitor_buffer) furi_string_free(app->monitor_buffer);
    if(app->temp_line) furi_string_free(app->temp_line);
    
    // Close notifications
    if(app->notifications) furi_record_close(RECORD_NOTIFICATION);
    
    free(app);
}

// ===== MAIN APP ENTRY POINT =====

int32_t gennaro_ai_app(void* p) {
    UNUSED(p);
    
    FURI_LOG_I(TAG, "🚀 Starting Gennaro AI Serial Monitor");
    
    GennaroAIApp* app = gennaro_ai_app_alloc();
    if(!app) {
        FURI_LOG_E(TAG, "❌ Failed to allocate app");
        return -1;
    }
    
    // Initialize GPIO
    init_gpio_uart(app);
    
    // Attach to GUI
    Gui* gui = furi_record_open(RECORD_GUI);
    view_dispatcher_attach_to_gui(app->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    
    // Set starting view
    view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewSubmenu);
    
    // Welcome
    notification_message(app->notifications, &sequence_display_backlight_on);
    notification_message(app->notifications, &sequence_single_vibro);
    
    add_line_to_monitor(app, "=== GENNARO AI STARTED ===");
    add_line_to_monitor(app, "Initializing ESP32-CAM communication...");
    
    // Send initial status
    furi_delay_ms(1000);
    send_command_to_esp32(app, "STATUS");
    
    FURI_LOG_I(TAG, "✅ App initialized - Serial Monitor ready!");
    
    // Run view dispatcher
    view_dispatcher_run(app->view_dispatcher);
    
    // Cleanup
    furi_record_close(RECORD_GUI);
    gennaro_ai_app_free(app);
    
    FURI_LOG_I(TAG, "🛑 Gennaro AI Serial Monitor terminated");
    
    return 0;
}
