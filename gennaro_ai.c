/*
 * GENNARO AI - WITH SAFE SERIAL MONITOR
 * Mostra le risposte ESP32 in tempo reale
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

// SAFE BUFFER SIZES
#define MONITOR_BUFFER_SIZE 2048
#define LINE_BUFFER_SIZE 100
#define MAX_LINES 30

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
    GennaroAICommandClear,
} GennaroAICommand;

// SAFE App context
typedef struct {
    ViewDispatcher* view_dispatcher;
    Submenu* main_submenu;
    Submenu* commands_submenu;
    TextBox* monitor_text_box;
    NotificationApp* notifications;
    
    FuriString* monitor_buffer;
    char receive_buffer[LINE_BUFFER_SIZE];
    size_t receive_pos;
    uint32_t lines_count;
    uint32_t commands_sent;
    
    bool monitor_active;
    FuriTimer* read_timer;
} GennaroAIApp;

// Forward declarations
static void send_command_to_esp32(GennaroAIApp* app, const char* command);
static void read_timer_callback(void* context);
static void add_line_to_monitor(GennaroAIApp* app, const char* line);
static void update_monitor_display(GennaroAIApp* app);

// ===== SERIAL COMMUNICATION =====

static void init_gpio_serial(GennaroAIApp* app) {
    UNUSED(app);
    
    // Initialize GPIO pins
    furi_hal_gpio_init(ESP32_TX_PIN, GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);
    furi_hal_gpio_init(ESP32_RX_PIN, GpioModeInput, GpioPullUp, GpioSpeedVeryHigh);
    
    // TX idle high
    furi_hal_gpio_write(ESP32_TX_PIN, true);
    
    FURI_LOG_I(TAG, "GPIO Serial initialized");
}

static void send_command_to_esp32(GennaroAIApp* app, const char* command) {
    if(!app || !command) return;
    
    FURI_LOG_I(TAG, "Sending: %s", command);
    
    // Simple command transmission via GPIO pulses
    size_t len = strlen(command);
    
    // Send start sequence
    furi_hal_gpio_write(ESP32_TX_PIN, false);
    furi_delay_ms(50);
    furi_hal_gpio_write(ESP32_TX_PIN, true);
    furi_delay_ms(50);
    
    // Send command as pulse pattern (simplified)
    for(size_t i = 0; i < len && i < 10; i++) {
        furi_hal_gpio_write(ESP32_TX_PIN, false);
        furi_delay_ms(20);
        furi_hal_gpio_write(ESP32_TX_PIN, true);
        furi_delay_ms(20);
    }
    
    // Send end sequence
    furi_hal_gpio_write(ESP32_TX_PIN, false);
    furi_delay_ms(100);
    furi_hal_gpio_write(ESP32_TX_PIN, true);
    
    app->commands_sent++;
    
    // Add to monitor
    char cmd_line[128];
    snprintf(cmd_line, sizeof(cmd_line), ">>> SENT: %s", command);
    add_line_to_monitor(app, cmd_line);
    
    // Vibration feedback
    if(app->notifications) {
        notification_message(app->notifications, &sequence_single_vibro);
    }
}

// ===== SERIAL READING (SAFE) =====

static bool read_byte_from_esp32(uint8_t* byte) {
    if(!byte) return false;
    
    // Simple GPIO reading with timeout
    uint32_t timeout = 100; // 100 iterations max
    
    // Wait for start bit (low)
    while(furi_hal_gpio_read(ESP32_RX_PIN) && timeout > 0) {
        furi_delay_us(10);
        timeout--;
    }
    
    if(timeout == 0) return false;
    
    // Simple bit reading (not exact UART, but works for basic data)
    furi_delay_us(50); // Half bit time
    
    uint8_t data = 0;
    for(int i = 0; i < 8; i++) {
        furi_delay_us(87); // Bit time for 115200 (approximate)
        if(furi_hal_gpio_read(ESP32_RX_PIN)) {
            data |= (1 << i);
        }
    }
    
    *byte = data;
    return true;
}

static void read_timer_callback(void* context) {
    GennaroAIApp* app = (GennaroAIApp*)context;
    if(!app || !app->monitor_active) return;
    
    // Read bytes from ESP32
    uint8_t byte;
    int bytes_read = 0;
    
    while(bytes_read < 10 && read_byte_from_esp32(&byte)) { // Limit reads
        if(byte >= 32 && byte <= 126) { // Printable ASCII
            if(app->receive_pos < LINE_BUFFER_SIZE - 1) {
                app->receive_buffer[app->receive_pos++] = byte;
            }
        } else if(byte == '\n' || byte == '\r') {
            // End of line
            if(app->receive_pos > 0) {
                app->receive_buffer[app->receive_pos] = '\0';
                add_line_to_monitor(app, app->receive_buffer);
                app->receive_pos = 0;
            }
        }
        bytes_read++;
    }
}

// ===== MONITOR MANAGEMENT =====

static void add_line_to_monitor(GennaroAIApp* app, const char* line) {
    if(!app || !line || !app->monitor_buffer) return;
    
    // Add timestamp
    uint32_t timestamp = furi_get_tick() / 1000;
    char timestamped_line[200];
    
    int result = snprintf(timestamped_line, sizeof(timestamped_line),
                         "[%02lu:%02lu] %s\n",
                         (timestamp / 60) % 60, timestamp % 60, line);
    
    if(result < 0 || result >= (int)sizeof(timestamped_line)) return;
    
    // Check buffer size
    size_t current_size = furi_string_size(app->monitor_buffer);
    if(current_size > MONITOR_BUFFER_SIZE - 300) {
        // Clear old data
        const char* buffer_str = furi_string_get_cstr(app->monitor_buffer);
        const char* halfway = buffer_str + (current_size / 2);
        const char* newline = strchr(halfway, '\n');
        if(newline) {
            furi_string_set_str(app->monitor_buffer, newline + 1);
        } else {
            furi_string_reset(app->monitor_buffer);
        }
        app->lines_count = 15; // Approximate
    }
    
    // Add new line
    furi_string_cat_str(app->monitor_buffer, timestamped_line);
    app->lines_count++;
    
    // Update display
    if(app->monitor_active) {
        update_monitor_display(app);
    }
}

static void update_monitor_display(GennaroAIApp* app) {
    if(!app || !app->monitor_text_box || !app->monitor_buffer) return;
    
    text_box_set_text(app->monitor_text_box, furi_string_get_cstr(app->monitor_buffer));
    text_box_set_focus(app->monitor_text_box, TextBoxFocusEnd);
}

static void clear_monitor(GennaroAIApp* app) {
    if(!app || !app->monitor_buffer) return;
    
    furi_string_reset(app->monitor_buffer);
    app->lines_count = 0;
    app->receive_pos = 0;
    
    add_line_to_monitor(app, "=== MONITOR CLEARED ===");
    add_line_to_monitor(app, "Gennaro AI Serial Monitor");
    add_line_to_monitor(app, "Listening for ESP32-CAM responses...");
}

// ===== CALLBACKS =====

static void main_submenu_callback(void* context, uint32_t index) {
    GennaroAIApp* app = (GennaroAIApp*)context;
    if(!app) return;
    
    switch(index) {
        case GennaroAIMenuMonitor:
            app->monitor_active = true;
            if(app->read_timer) {
                furi_timer_start(app->read_timer, 50); // Read every 50ms
            }
            update_monitor_display(app);
            view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewMonitor);
            break;
            
        case GennaroAIMenuCommands:
            view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewCommands);
            break;
            
        case GennaroAIMenuHelp:
            add_line_to_monitor(app, "=== GENNARO AI HELP ===");
            add_line_to_monitor(app, "Hardware Connections:");
            add_line_to_monitor(app, "  Flipper GPIO13 -> ESP32 GPIO3");
            add_line_to_monitor(app, "  Flipper GPIO14 <- ESP32 GPIO1");
            add_line_to_monitor(app, "  Flipper 5V -> ESP32 5V");
            add_line_to_monitor(app, "  Flipper GND -> ESP32 GND");
            add_line_to_monitor(app, "");
            add_line_to_monitor(app, "Usage:");
            add_line_to_monitor(app, "1. Send commands via Commands menu");
            add_line_to_monitor(app, "2. View ESP32 responses in Monitor");
            add_line_to_monitor(app, "3. ESP32 processes with Claude AI");
            app->monitor_active = true;
            if(app->read_timer) {
                furi_timer_start(app->read_timer, 50);
            }
            update_monitor_display(app);
            view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewMonitor);
            break;
    }
}

static void commands_submenu_callback(void* context, uint32_t index) {
    GennaroAIApp* app = (GennaroAIApp*)context;
    if(!app) return;
    
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
        case GennaroAICommandStatus:
            send_command_to_esp32(app, "STATUS");
            break;
        case GennaroAICommandClear:
            clear_monitor(app);
            break;
    }
    
    // Auto-switch to monitor
    app->monitor_active = true;
    if(app->read_timer) {
        furi_timer_start(app->read_timer, 50);
    }
    update_monitor_display(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewMonitor);
}

static bool monitor_input_callback(InputEvent* event, void* context) {
    GennaroAIApp* app = (GennaroAIApp*)context;
    if(!app || !event) return false;
    
    if(event->type == InputTypePress) {
        switch(event->key) {
            case InputKeyBack:
                app->monitor_active = false;
                if(app->read_timer) {
                    furi_timer_stop(app->read_timer);
                }
                view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewSubmenu);
                return true;
            case InputKeyOk:
                view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewCommands);
                return true;
            case InputKeyLeft:
                clear_monitor(app);
                return true;
            case InputKeyRight:
                send_command_to_esp32(app, "STATUS");
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
    
    // Main submenu
    app->main_submenu = submenu_alloc();
    if(!app->main_submenu) {
        view_dispatcher_free(app->view_dispatcher);
        if(app->notifications) furi_record_close(RECORD_NOTIFICATION);
        furi_string_free(app->monitor_buffer);
        free(app);
        return NULL;
    }
    
    submenu_add_item(app->main_submenu, "📺 Serial Monitor", GennaroAIMenuMonitor, main_submenu_callback, app);
    submenu_add_item(app->main_submenu, "📤 Send Commands", GennaroAIMenuCommands, main_submenu_callback, app);
    submenu_add_item(app->main_submenu, "❓ Help & Setup", GennaroAIMenuHelp, main_submenu_callback, app);
    
    view_set_previous_callback(submenu_get_view(app->main_submenu), navigation_exit_callback);
    view_dispatcher_add_view(app->view_dispatcher, GennaroAIViewSubmenu, submenu_get_view(app->main_submenu));
    
    // Commands submenu
    app->commands_submenu = submenu_alloc();
    if(!app->commands_submenu) {
        view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewSubmenu);
        submenu_free(app->main_submenu);
        view_dispatcher_free(app->view_dispatcher);
        if(app->notifications) furi_record_close(RECORD_NOTIFICATION);
        furi_string_free(app->monitor_buffer);
        free(app);
        return NULL;
    }
    
    submenu_add_item(app->commands_submenu, "👁️ VISION Analysis", GennaroAICommandVision, commands_submenu_callback, app);
    submenu_add_item(app->commands_submenu, "🧮 MATH Solver", GennaroAICommandMath, commands_submenu_callback, app);
    submenu_add_item(app->commands_submenu, "📝 OCR Text Read", GennaroAICommandOCR, commands_submenu_callback, app);
    submenu_add_item(app->commands_submenu, "📊 STATUS Check", GennaroAICommandStatus, commands_submenu_callback, app);
    submenu_add_item(app->commands_submenu, "🗑️ CLEAR Monitor", GennaroAICommandClear, commands_submenu_callback, app);
    
    view_set_previous_callback(submenu_get_view(app->commands_submenu), navigation_submenu_callback);
    view_dispatcher_add_view(app->view_dispatcher, GennaroAIViewCommands, submenu_get_view(app->commands_submenu));
    
    // Monitor text box
    app->monitor_text_box = text_box_alloc();
    if(!app->monitor_text_box) {
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
    
    // Create read timer
    app->read_timer = furi_timer_alloc(read_timer_callback, FuriTimerTypePeriodic, app);
    
    // Initialize monitor
    clear_monitor(app);
    
    return app;
}

static void gennaro_ai_app_free(GennaroAIApp* app) {
    if(!app) return;
    
    // Stop timer
    if(app->read_timer) {
        furi_timer_stop(app->read_timer);
        furi_timer_free(app->read_timer);
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
    
    FURI_LOG_I(TAG, "Starting Gennaro AI with Serial Monitor");
    
    GennaroAIApp* app = gennaro_ai_app_alloc();
    if(!app) {
        FURI_LOG_E(TAG, "Failed to allocate app");
        return -1;
    }
    
    // Initialize GPIO
    init_gpio_serial(app);
    
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
    
    add_line_to_monitor(app, "=== GENNARO AI STARTED ===");
    add_line_to_monitor(app, "ESP32-CAM AI Vision Assistant Ready");
    add_line_to_monitor(app, "Use Commands menu to send AI requests");
    
    FURI_LOG_I(TAG, "App started with serial monitor");
    
    // Run
    view_dispatcher_run(app->view_dispatcher);
    
    // Cleanup
    furi_record_close(RECORD_GUI);
    gennaro_ai_app_free(app);
    
    FURI_LOG_I(TAG, "App terminated");
    return 0;
}
