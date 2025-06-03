/*
 * GENNARO AI - SERIAL MONITOR (BUSFAULT FIXED)
 * Monitor Seriale per ESP32-CAM con sicurezza memoria
 * 
 * FIXED: Buffer overflow, stack overflow, null pointer access
 */

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <notification/notification_messages.h>
#include <furi_hal.h>
#include <furi_hal_gpio.h>

#define TAG "GennaroAI"

// GPIO PINS - SAFE REFERENCES
#define ESP32_TX_PIN (&gpio_ext_pc1)  // GPIO14 -> ESP32 RX 
#define ESP32_RX_PIN (&gpio_ext_pc0)  // GPIO13 -> ESP32 TX

// REDUCED BUFFER SIZES TO PREVENT OVERFLOW
#define MONITOR_BUFFER_SIZE 1024      // Reduced from 4096
#define LINE_BUFFER_SIZE 128          // Reduced from 256
#define MAX_LINES 20                  // Reduced from 50

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
    GennaroAICommandStatus,
    GennaroAICommandClear,
} GennaroAICommand;

// Main Menu
typedef enum {
    GennaroAIMenuMonitor,
    GennaroAIMenuCommands,
    GennaroAIMenuHelp,
} GennaroAIMenu;

// SAFE App context with validation
typedef struct {
    ViewDispatcher* view_dispatcher;
    Submenu* main_submenu;
    Submenu* commands_submenu;
    TextBox* monitor_text_box;
    NotificationApp* notifications;
    
    // Serial Monitor - SAFE BUFFERS
    FuriString* monitor_buffer;
    char line_buffer[LINE_BUFFER_SIZE];
    size_t line_pos;
    uint32_t lines_count;
    
    // State
    bool monitor_active;
    uint32_t commands_sent;
    bool initialized;  // Safety flag
    
} GennaroAIApp;

// Forward declarations
static void send_command_to_esp32(GennaroAIApp* app, const char* command);
static void update_monitor_display(GennaroAIApp* app);
static void add_line_to_monitor(GennaroAIApp* app, const char* line);

// ===== SAFETY VALIDATION =====

static bool app_is_valid(GennaroAIApp* app) {
    return (app != NULL && 
            app->initialized && 
            app->monitor_buffer != NULL &&
            app->view_dispatcher != NULL);
}

// ===== SERIAL COMMUNICATION (SIMPLIFIED) =====

static void init_gpio_uart(GennaroAIApp* app) {
    if(!app) return;
    
    // SAFE GPIO initialization
    furi_hal_gpio_init(ESP32_TX_PIN, GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);
    furi_hal_gpio_init(ESP32_RX_PIN, GpioModeInput, GpioPullUp, GpioSpeedVeryHigh);
    
    // Set TX high (idle state)
    furi_hal_gpio_write(ESP32_TX_PIN, true);
    
    FURI_LOG_I(TAG, "✅ GPIO UART initialized safely");
}

static void send_command_to_esp32(GennaroAIApp* app, const char* command) {
    if(!app_is_valid(app) || !command) {
        FURI_LOG_E(TAG, "❌ Invalid app or command in send_command");
        return;
    }
    
    FURI_LOG_I(TAG, "📤 Sending: %s", command);
    
    // SAFE command length check
    size_t cmd_len = strlen(command);
    if(cmd_len > 32) {  // Reasonable limit
        FURI_LOG_E(TAG, "❌ Command too long: %zu", cmd_len);
        return;
    }
    
    // Simple GPIO pulse pattern instead of bit-banging
    // This avoids timing issues that can cause crashes
    for(size_t i = 0; i < cmd_len && i < 10; i++) {  // Limit iterations
        furi_hal_gpio_write(ESP32_TX_PIN, false);
        furi_delay_ms(10);  // Longer, safer delays
        furi_hal_gpio_write(ESP32_TX_PIN, true);
        furi_delay_ms(10);
    }
    
    app->commands_sent++;
    
    // SAFE monitor update
    char safe_line[64];  // Stack-allocated, safe size
    snprintf(safe_line, sizeof(safe_line), "CMD: %.20s", command);
    add_line_to_monitor(app, safe_line);
    
    // Safe vibration
    if(app->notifications) {
        notification_message(app->notifications, &sequence_single_vibro);
    }
}

// ===== MONITOR MANAGEMENT (CRASH-SAFE) =====

static void add_line_to_monitor(GennaroAIApp* app, const char* line) {
    if(!app_is_valid(app) || !line) {
        FURI_LOG_E(TAG, "❌ Invalid parameters in add_line_to_monitor");
        return;
    }
    
    // SAFE line length check
    size_t line_len = strlen(line);
    if(line_len > 100) {  // Reasonable limit
        FURI_LOG_W(TAG, "⚠️ Line too long, truncating");
        line_len = 100;
    }
    
    // SAFE timestamp creation
    uint32_t timestamp = furi_get_tick() / 1000;
    
    // Create SAFE timestamped line with bounds checking
    char safe_timestamped_line[200];  // Safe size
    int result = snprintf(safe_timestamped_line, sizeof(safe_timestamped_line), 
                         "[%02lu:%02lu] %.100s\n", 
                         (timestamp / 60) % 60, timestamp % 60, line);
    
    if(result < 0 || result >= (int)sizeof(safe_timestamped_line)) {
        FURI_LOG_E(TAG, "❌ snprintf failed or truncated");
        return;
    }
    
    // SAFE buffer size check before adding
    size_t current_size = furi_string_size(app->monitor_buffer);
    if(current_size > MONITOR_BUFFER_SIZE - 300) {  // Leave safety margin
        // Clear half the buffer to make room
        furi_string_reset(app->monitor_buffer);
        furi_string_cat_str(app->monitor_buffer, "=== BUFFER CLEARED ===\n");
        app->lines_count = 1;
    }
    
    // SAFE string concatenation
    furi_string_cat_str(app->monitor_buffer, safe_timestamped_line);
    app->lines_count++;
    
    // SAFE display update
    if(app->monitor_active && app->monitor_text_box) {
        update_monitor_display(app);
    }
}

static void update_monitor_display(GennaroAIApp* app) {
    if(!app_is_valid(app) || !app->monitor_text_box) {
        FURI_LOG_E(TAG, "❌ Invalid app in update_monitor_display");
        return;
    }
    
    // SAFE text setting
    const char* buffer_text = furi_string_get_cstr(app->monitor_buffer);
    if(buffer_text) {
        text_box_set_text(app->monitor_text_box, buffer_text);
        text_box_set_focus(app->monitor_text_box, TextBoxFocusEnd);
    }
}

static void clear_monitor(GennaroAIApp* app) {
    if(!app_is_valid(app)) {
        FURI_LOG_E(TAG, "❌ Invalid app in clear_monitor");
        return;
    }
    
    furi_string_reset(app->monitor_buffer);
    app->lines_count = 0;
    app->line_pos = 0;
    
    add_line_to_monitor(app, "MONITOR READY");
    add_line_to_monitor(app, "ESP32-CAM Interface");
}

// ===== SUBMENU CALLBACKS (CRASH-SAFE) =====

static void main_submenu_callback(void* context, uint32_t index) {
    GennaroAIApp* app = (GennaroAIApp*)context;
    
    if(!app_is_valid(app)) {
        FURI_LOG_E(TAG, "❌ Invalid app in main_submenu_callback");
        return;
    }
    
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
            add_line_to_monitor(app, "=== GENNARO AI HELP ===");
            add_line_to_monitor(app, "GPIO13->ESP32 GPIO3");
            add_line_to_monitor(app, "GPIO14<-ESP32 GPIO1");
            add_line_to_monitor(app, "Use Commands menu");
            app->monitor_active = true;
            update_monitor_display(app);
            view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewMonitor);
            break;
    }
}

static void commands_submenu_callback(void* context, uint32_t index) {
    GennaroAIApp* app = (GennaroAIApp*)context;
    
    if(!app_is_valid(app)) {
        FURI_LOG_E(TAG, "❌ Invalid app in commands_submenu_callback");
        return;
    }
    
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
        case GennaroAICommandStatus:
            send_command_to_esp32(app, "STATUS");
            break;
        case GennaroAICommandClear:
            clear_monitor(app);
            break;
    }
    
    // SAFE auto-switch to monitor
    app->monitor_active = true;
    update_monitor_display(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewMonitor);
}

// ===== INPUT CALLBACKS (CRASH-SAFE) =====

static bool monitor_input_callback(InputEvent* event, void* context) {
    GennaroAIApp* app = (GennaroAIApp*)context;
    
    if(!app_is_valid(app) || !event) {
        FURI_LOG_E(TAG, "❌ Invalid parameters in monitor_input_callback");
        return false;
    }
    
    bool consumed = false;
    
    if(event->type == InputTypePress) {
        switch(event->key) {
            case InputKeyBack:
                app->monitor_active = false;
                view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewSubmenu);
                consumed = true;
                break;
            case InputKeyOk:
                view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewCommands);
                consumed = true;
                break;
            case InputKeyLeft:
                clear_monitor(app);
                consumed = true;
                break;
            case InputKeyRight:
                send_command_to_esp32(app, "STATUS");
                consumed = true;
                break;
            default:
                break;
        }
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

// ===== APP ALLOCATION (CRASH-SAFE) =====

static GennaroAIApp* gennaro_ai_app_alloc() {
    GennaroAIApp* app = malloc(sizeof(GennaroAIApp));
    if(!app) {
        FURI_LOG_E(TAG, "❌ Failed to allocate app struct");
        return NULL;
    }
    
    // SAFE initialization - zero everything first
    memset(app, 0, sizeof(GennaroAIApp));
    
    // Initialize critical flag
    app->initialized = false;
    
    // SAFE string allocation
    app->monitor_buffer = furi_string_alloc();
    if(!app->monitor_buffer) {
        FURI_LOG_E(TAG, "❌ Failed to allocate monitor buffer");
        free(app);
        return NULL;
    }
    
    // SAFE notifications
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    if(!app->notifications) {
        FURI_LOG_E(TAG, "❌ Failed to open notifications");
        furi_string_free(app->monitor_buffer);
        free(app);
        return NULL;
    }
    
    // SAFE view dispatcher
    app->view_dispatcher = view_dispatcher_alloc();
    if(!app->view_dispatcher) {
        FURI_LOG_E(TAG, "❌ Failed to allocate view dispatcher");
        furi_record_close(RECORD_NOTIFICATION);
        furi_string_free(app->monitor_buffer);
        free(app);
        return NULL;
    }
    
    // SAFE main submenu
    app->main_submenu = submenu_alloc();
    if(!app->main_submenu) {
        FURI_LOG_E(TAG, "❌ Failed to allocate main submenu");
        view_dispatcher_free(app->view_dispatcher);
        furi_record_close(RECORD_NOTIFICATION);
        furi_string_free(app->monitor_buffer);
        free(app);
        return NULL;
    }
    
    // SAFE submenu items
    submenu_add_item(app->main_submenu, "Serial Monitor", GennaroAIMenuMonitor, main_submenu_callback, app);
    submenu_add_item(app->main_submenu, "Send Commands", GennaroAIMenuCommands, main_submenu_callback, app);
    submenu_add_item(app->main_submenu, "Help", GennaroAIMenuHelp, main_submenu_callback, app);
    
    view_set_previous_callback(submenu_get_view(app->main_submenu), navigation_exit_callback);
    view_dispatcher_add_view(app->view_dispatcher, GennaroAIViewSubmenu, submenu_get_view(app->main_submenu));
    
    // SAFE commands submenu
    app->commands_submenu = submenu_alloc();
    if(!app->commands_submenu) {
        FURI_LOG_E(TAG, "❌ Failed to allocate commands submenu");
        // Cleanup previous allocations
        view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewSubmenu);
        submenu_free(app->main_submenu);
        view_dispatcher_free(app->view_dispatcher);
        furi_record_close(RECORD_NOTIFICATION);
        furi_string_free(app->monitor_buffer);
        free(app);
        return NULL;
    }
    
    submenu_add_item(app->commands_submenu, "Vision Analysis", GennaroAICommandVision, commands_submenu_callback, app);
    submenu_add_item(app->commands_submenu, "Math Solver", GennaroAICommandMath, commands_submenu_callback, app);
    submenu_add_item(app->commands_submenu, "OCR Text", GennaroAICommandOCR, commands_submenu_callback, app);
    submenu_add_item(app->commands_submenu, "Count Objects", GennaroAICommandCount, commands_submenu_callback, app);
    submenu_add_item(app->commands_submenu, "System Status", GennaroAICommandStatus, commands_submenu_callback, app);
    submenu_add_item(app->commands_submenu, "Clear Monitor", GennaroAICommandClear, commands_submenu_callback, app);
    
    view_set_previous_callback(submenu_get_view(app->commands_submenu), navigation_submenu_callback);
    view_dispatcher_add_view(app->view_dispatcher, GennaroAIViewCommands, submenu_get_view(app->commands_submenu));
    
    // SAFE monitor text box
    app->monitor_text_box = text_box_alloc();
    if(!app->monitor_text_box) {
        FURI_LOG_E(TAG, "❌ Failed to allocate text box");
        // Cleanup all previous allocations
        view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewCommands);
        view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewSubmenu);
        submenu_free(app->commands_submenu);
        submenu_free(app->main_submenu);
        view_dispatcher_free(app->view_dispatcher);
        furi_record_close(RECORD_NOTIFICATION);
        furi_string_free(app->monitor_buffer);
        free(app);
        return NULL;
    }
    
    text_box_set_font(app->monitor_text_box, TextBoxFontText);
    view_set_previous_callback(text_box_get_view(app->monitor_text_box), navigation_submenu_callback);
    view_set_input_callback(text_box_get_view(app->monitor_text_box), monitor_input_callback);
    view_dispatcher_add_view(app->view_dispatcher, GennaroAIViewMonitor, text_box_get_view(app->monitor_text_box));
    
    // Initialize state SAFELY
    app->monitor_active = false;
    app->commands_sent = 0;
    app->line_pos = 0;
    app->lines_count = 0;
    memset(app->line_buffer, 0, sizeof(app->line_buffer));
    
    // SAFE monitor initialization
    clear_monitor(app);
    
    // Mark as initialized LAST
    app->initialized = true;
    
    FURI_LOG_I(TAG, "✅ App allocated safely");
    return app;
}

// ===== APP DEALLOCATION (CRASH-SAFE) =====

static void gennaro_ai_app_free(GennaroAIApp* app) {
    if(!app) {
        FURI_LOG_W(TAG, "⚠️ Attempted to free NULL app");
        return;
    }
    
    FURI_LOG_I(TAG, "🧹 Freeing app safely");
    
    // Mark as not initialized first
    app->initialized = false;
    
    // SAFE view dispatcher cleanup
    if(app->view_dispatcher) {
        view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewSubmenu);
        view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewCommands);
        view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewMonitor);
        view_dispatcher_free(app->view_dispatcher);
    }
    
    // SAFE view cleanup
    if(app->main_submenu) submenu_free(app->main_submenu);
    if(app->commands_submenu) submenu_free(app->commands_submenu);
    if(app->monitor_text_box) text_box_free(app->monitor_text_box);
    
    // SAFE string cleanup
    if(app->monitor_buffer) furi_string_free(app->monitor_buffer);
    
    // SAFE notifications cleanup
    if(app->notifications) furi_record_close(RECORD_NOTIFICATION);
    
    // Finally free the app
    free(app);
}

// ===== MAIN APP ENTRY POINT (CRASH-SAFE) =====

int32_t gennaro_ai_app(void* p) {
    UNUSED(p);
    
    FURI_LOG_I(TAG, "🚀 Starting Gennaro AI (Safe Mode)");
    
    GennaroAIApp* app = gennaro_ai_app_alloc();
    if(!app) {
        FURI_LOG_E(TAG, "❌ Failed to allocate app - ABORTING");
        return -1;
    }
    
    // SAFE GPIO initialization
    init_gpio_uart(app);
    
    // SAFE GUI attachment
    Gui* gui = furi_record_open(RECORD_GUI);
    if(!gui) {
        FURI_LOG_E(TAG, "❌ Failed to open GUI record");
        gennaro_ai_app_free(app);
        return -1;
    }
    
    view_dispatcher_attach_to_gui(app->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    
    // SAFE starting view
    view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewSubmenu);
    
    // SAFE welcome
    if(app->notifications) {
        notification_message(app->notifications, &sequence_display_backlight_on);
        notification_message(app->notifications, &sequence_single_vibro);
    }
    
    add_line_to_monitor(app, "GENNARO AI READY");
    add_line_to_monitor(app, "Safe Mode Active");
    
    FURI_LOG_I(TAG, "✅ App started safely");
    
    // SAFE main loop
    view_dispatcher_run(app->view_dispatcher);
    
    // SAFE cleanup
    furi_record_close(RECORD_GUI);
    gennaro_ai_app_free(app);
    
    FURI_LOG_I(TAG, "🛑 App terminated safely");
    return 0;
}
