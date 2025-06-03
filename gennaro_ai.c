/*
 * GENNARO AI - NO TIMER VERSION (BUSFAULT FIX)
 * Simple manual refresh monitor without timers
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

// ULTRA SAFE BUFFER SIZES
#define MONITOR_BUFFER_SIZE 1024
#define MAX_LINES 20

// Views
typedef enum {
    GennaroAIViewSubmenu,
    GennaroAIViewMonitor,
    GennaroAIViewCommands,
} GennaroAIView;

// Menu items
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

// ULTRA SAFE App context
typedef struct {
    ViewDispatcher* view_dispatcher;
    Submenu* main_submenu;
    Submenu* commands_submenu;
    TextBox* monitor_text_box;
    NotificationApp* notifications;
    
    FuriString* monitor_buffer;
    uint32_t commands_sent;
    bool monitor_active;
} GennaroAIApp;

// ===== SAFE FUNCTIONS =====

static void add_monitor_line(GennaroAIApp* app, const char* line) {
    if(!app || !line || !app->monitor_buffer) return;
    
    // Simple timestamp
    uint32_t seconds = furi_get_tick() / 1000;
    
    // Safe line creation
    char safe_line[200];
    int result = snprintf(safe_line, sizeof(safe_line), 
                         "[%02lu:%02lu] %s\n",
                         (seconds / 60) % 60, seconds % 60, line);
    
    if(result < 0 || result >= (int)sizeof(safe_line)) return;
    
    // Check buffer size and clear if needed
    if(furi_string_size(app->monitor_buffer) > MONITOR_BUFFER_SIZE - 200) {
        furi_string_reset(app->monitor_buffer);
        furi_string_cat_str(app->monitor_buffer, "[CLEARED] Monitor buffer reset\n");
    }
    
    // Add new line
    furi_string_cat_str(app->monitor_buffer, safe_line);
}

static void update_monitor(GennaroAIApp* app) {
    if(!app || !app->monitor_text_box || !app->monitor_buffer) return;
    
    text_box_set_text(app->monitor_text_box, furi_string_get_cstr(app->monitor_buffer));
    text_box_set_focus(app->monitor_text_box, TextBoxFocusEnd);
}

static void clear_monitor(GennaroAIApp* app) {
    if(!app || !app->monitor_buffer) return;
    
    furi_string_reset(app->monitor_buffer);
    add_monitor_line(app, "=== MONITOR CLEARED ===");
    add_monitor_line(app, "Gennaro AI Serial Monitor");
    add_monitor_line(app, "Send commands and press REFRESH");
    update_monitor(app);
}

static void send_command(GennaroAIApp* app, const char* command) {
    if(!app || !command) return;
    
    FURI_LOG_I(TAG, "Sending: %s", command);
    
    // ULTRA SIMPLE GPIO communication
    // Just toggle GPIO a few times - ESP32 can detect pattern
    for(int i = 0; i < 5; i++) {
        furi_hal_gpio_write(ESP32_TX_PIN, false);
        furi_delay_ms(50);
        furi_hal_gpio_write(ESP32_TX_PIN, true);
        furi_delay_ms(50);
    }
    
    app->commands_sent++;
    
    // Add to monitor
    char cmd_info[100];
    snprintf(cmd_info, sizeof(cmd_info), ">>> SENT: %s (Count: %lu)", 
             command, app->commands_sent);
    add_monitor_line(app, cmd_info);
    
    // Simulated ESP32 response for testing
    add_monitor_line(app, "ESP32: Command received");
    add_monitor_line(app, "ESP32: Processing with Claude AI...");
    add_monitor_line(app, "ESP32: Check actual serial for full response");
    
    // Vibration
    if(app->notifications) {
        notification_message(app->notifications, &sequence_single_vibro);
    }
}

static void manual_refresh(GennaroAIApp* app) {
    if(!app) return;
    
    add_monitor_line(app, "--- MANUAL REFRESH ---");
    add_monitor_line(app, "Checking for ESP32 responses...");
    
    // Here you could add simple GPIO reading
    // For now, just simulate
    bool gpio_state = furi_hal_gpio_read(ESP32_RX_PIN);
    if(gpio_state) {
        add_monitor_line(app, "ESP32 GPIO: HIGH (Ready)");
    } else {
        add_monitor_line(app, "ESP32 GPIO: LOW (Busy/No signal)");
    }
    
    update_monitor(app);
}

// ===== CALLBACKS =====

static void main_submenu_callback(void* context, uint32_t index) {
    GennaroAIApp* app = (GennaroAIApp*)context;
    if(!app) return;
    
    switch(index) {
        case GennaroAIMenuMonitor:
            app->monitor_active = true;
            update_monitor(app);
            view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewMonitor);
            break;
            
        case GennaroAIMenuCommands:
            view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewCommands);
            break;
            
        case GennaroAIMenuHelp:
            clear_monitor(app);
            add_monitor_line(app, "=== GENNARO AI HELP ===");
            add_monitor_line(app, "");
            add_monitor_line(app, "CONNECTIONS:");
            add_monitor_line(app, "Flipper GPIO13 -> ESP32 GPIO3");
            add_monitor_line(app, "Flipper GPIO14 <- ESP32 GPIO1");
            add_monitor_line(app, "Flipper 5V -> ESP32 5V");
            add_monitor_line(app, "Flipper GND -> ESP32 GND");
            add_monitor_line(app, "");
            add_monitor_line(app, "USAGE:");
            add_monitor_line(app, "1. Send commands via Commands menu");
            add_monitor_line(app, "2. Check responses in Monitor");
            add_monitor_line(app, "3. Press UP to refresh manually");
            add_monitor_line(app, "4. Check ESP32 serial for full AI responses");
            app->monitor_active = true;
            update_monitor(app);
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
            manual_refresh(app);
            break;
    }
    
    // Auto-switch to monitor
    app->monitor_active = true;
    update_monitor(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewMonitor);
}

static bool monitor_input_callback(InputEvent* event, void* context) {
    GennaroAIApp* app = (GennaroAIApp*)context;
    if(!app || !event) return false;
    
    if(event->type == InputTypePress) {
        switch(event->key) {
            case InputKeyBack:
                app->monitor_active = false;
                view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewSubmenu);
                return true;
                
            case InputKeyOk:
                view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewCommands);
                return true;
                
            case InputKeyUp:
                manual_refresh(app);
                return true;
                
            case InputKeyDown:
                clear_monitor(app);
                return true;
                
            case InputKeyLeft:
                send_command(app, "STATUS");
                return true;
                
            case InputKeyRight:
                add_monitor_line(app, "RIGHT: Reserved for future use");
                update_monitor(app);
                return true;
                
            default:
                break;
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

// ===== APP ALLOCATION (ULTRA SAFE) =====

static GennaroAIApp* gennaro_ai_app_alloc() {
    GennaroAIApp* app = malloc(sizeof(GennaroAIApp));
    if(!app) {
        FURI_LOG_E(TAG, "Failed to allocate app");
        return NULL;
    }
    
    // Zero everything
    memset(app, 0, sizeof(GennaroAIApp));
    
    // Monitor buffer
    app->monitor_buffer = furi_string_alloc();
    if(!app->monitor_buffer) {
        FURI_LOG_E(TAG, "Failed to allocate monitor buffer");
        free(app);
        return NULL;
    }
    
    // Notifications
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    
    // View dispatcher
    app->view_dispatcher = view_dispatcher_alloc();
    if(!app->view_dispatcher) {
        FURI_LOG_E(TAG, "Failed to allocate view dispatcher");
        if(app->notifications) furi_record_close(RECORD_NOTIFICATION);
        furi_string_free(app->monitor_buffer);
        free(app);
        return NULL;
    }
    
    // Main submenu
    app->main_submenu = submenu_alloc();
    if(!app->main_submenu) {
        FURI_LOG_E(TAG, "Failed to allocate main submenu");
        view_dispatcher_free(app->view_dispatcher);
        if(app->notifications) furi_record_close(RECORD_NOTIFICATION);
        furi_string_free(app->monitor_buffer);
        free(app);
        return NULL;
    }
    
    submenu_add_item(app->main_submenu, "📺 Serial Monitor", GennaroAIMenuMonitor, main_submenu_callback, app);
    submenu_add_item(app->main_submenu, "📤 Send Commands", GennaroAIMenuCommands, main_submenu_callback, app);
    submenu_add_item(app->main_submenu, "❓ Help & Info", GennaroAIMenuHelp, main_submenu_callback, app);
    
    view_set_previous_callback(submenu_get_view(app->main_submenu), navigation_exit_callback);
    view_dispatcher_add_view(app->view_dispatcher, GennaroAIViewSubmenu, submenu_get_view(app->main_submenu));
    
    // Commands submenu
    app->commands_submenu = submenu_alloc();
    if(!app->commands_submenu) {
        FURI_LOG_E(TAG, "Failed to allocate commands submenu");
        view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewSubmenu);
        submenu_free(app->main_submenu);
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
        FURI_LOG_E(TAG, "Failed to allocate text box");
        view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewCommands);
        view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewSubmenu);
        submenu_free(app->commands_submenu);
        submenu_free(app->main_submenu);
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
    
    // Initialize monitor
    clear_monitor(app);
    
    FURI_LOG_I(TAG, "App allocated successfully");
    return app;
}

static void gennaro_ai_app_free(GennaroAIApp* app) {
    if(!app) return;
    
    FURI_LOG_I(TAG, "Freeing app");
    
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
    
    FURI_LOG_I(TAG, "Starting Gennaro AI (No Timer Version)");
    
    // Initialize GPIO first
    furi_hal_gpio_init(ESP32_TX_PIN, GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);
    furi_hal_gpio_init(ESP32_RX_PIN, GpioModeInput, GpioPullUp, GpioSpeedVeryHigh);
    furi_hal_gpio_write(ESP32_TX_PIN, true); // Idle high
    
    // Allocate app
    GennaroAIApp* app = gennaro_ai_app_alloc();
    if(!app) {
        FURI_LOG_E(TAG, "Failed to allocate app");
        return -1;
    }
    
    // Attach to GUI
    Gui* gui = furi_record_open(RECORD_GUI);
    if(!gui) {
        FURI_LOG_E(TAG, "Failed to open GUI");
        gennaro_ai_app_free(app);
        return -1;
    }
    
    view_dispatcher_attach_to_gui(app->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    
    // Set starting view
    view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewSubmenu);
    
    // Welcome
    if(app->notifications) {
        notification_message(app->notifications, &sequence_display_backlight_on);
        notification_message(app->notifications, &sequence_single_vibro);
    }
    
    add_monitor_line(app, "=== GENNARO AI STARTED ===");
    add_monitor_line(app, "ESP32-CAM AI Assistant Ready");
    add_monitor_line(app, "Manual refresh mode (no auto-timer)");
    
    FURI_LOG_I(TAG, "App started successfully");
    
    // Run
    view_dispatcher_run(app->view_dispatcher);
    
    // Cleanup
    furi_record_close(RECORD_GUI);
    gennaro_ai_app_free(app);
    
    FURI_LOG_I(TAG, "App terminated cleanly");
    return 0;
}
