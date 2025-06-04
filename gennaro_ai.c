/*
 * GENNARO AI - Integrated with Working UART Terminal
 * Based on working uart_terminal.fap structure
 * Added ESP32-CAM AI commands to proven terminal base
 */

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/text_input.h>
#include <notification/notification_messages.h>
#include <furi_hal_uart.h>
#include <furi_hal_console.h>
#include <stream_buffer.h>

#define TAG "GennaroAI"

// UART Configuration (from working terminal)
#define UART_CH (FuriHalUartIdUSART1)
#define UART_BUFFER_SIZE (1024)
#define UART_TIMEOUT (100)

// Views (based on working terminal structure)
typedef enum {
    GennaroAIViewSubmenu,
    GennaroAIViewConsole,
    GennaroAIViewTextInput,
    GennaroAIViewCommands,
} GennaroAIView;

// Main menu items
typedef enum {
    GennaroAIMenuConsole,
    GennaroAIMenuCommands,
    GennaroAIMenuTextInput,
    GennaroAIMenuSettings,
} GennaroAIMenuItem;

// ESP32-CAM Commands
typedef enum {
    GennaroAICommandVision,
    GennaroAICommandMath,
    GennaroAICommandOCR,
    GennaroAICommandCount,
    GennaroAICommandStatus,
    GennaroAICommandCustom,
} GennaroAICommand;

// Baud rates (from working terminal)
typedef enum {
    GennaroAIBaud9600,
    GennaroAIBaud115200,
} GennaroAIBaudRate;

const uint32_t baud_rates[] = {9600, 115200};
const char* baud_labels[] = {"9600", "115200"};

// App structure (based on working terminal)
typedef struct {
    ViewDispatcher* view_dispatcher;
    Submenu* main_submenu;
    Submenu* commands_submenu;
    TextBox* console_text_box;
    TextInput* text_input;
    VariableItemList* settings_list;
    NotificationApp* notifications;
    
    // UART handling (from working terminal)
    FuriThread* uart_thread;
    StreamBufferHandle_t uart_stream;
    FuriString* console_text;
    FuriString* text_input_buffer;
    
    // Settings
    GennaroAIBaudRate baud_rate;
    
    // State
    bool console_active;
    uint32_t commands_sent;
    
} GennaroAIApp;

// ===== UART FUNCTIONS (from working terminal base) =====

static void uart_on_irq_cb(UartIrqEvent ev, uint8_t data, void* context) {
    GennaroAIApp* app = (GennaroAIApp*)context;
    
    if(ev == UartIrqEventRXNE) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xStreamBufferSendFromISR(app->uart_stream, &data, 1, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

static int32_t uart_worker(void* context) {
    GennaroAIApp* app = (GennaroAIApp*)context;
    
    uint8_t data[64];
    while(1) {
        size_t received = xStreamBufferReceive(app->uart_stream, data, sizeof(data), 100);
        if(received > 0) {
            // Add received data to console
            for(size_t i = 0; i < received; i++) {
                if(data[i] >= 32 || data[i] == '\n' || data[i] == '\r') {
                    furi_string_push_back(app->console_text, data[i]);
                }
            }
            
            // Limit console buffer size
            if(furi_string_size(app->console_text) > 8192) {
                furi_string_right(app->console_text, 4096);
            }
            
            // Update display if console is active
            if(app->console_active) {
                view_dispatcher_send_custom_event(app->view_dispatcher, 0);
            }
        }
        
        if(furi_thread_flags_wait(FuriThreadFlagExit, FuriFlagWaitAny, 0) == FuriThreadFlagExit) {
            break;
        }
    }
    
    return 0;
}

static void uart_init(GennaroAIApp* app) {
    // Initialize UART (based on working terminal)
    furi_hal_console_disable();
    furi_hal_uart_set_br(UART_CH, baud_rates[app->baud_rate]);
    furi_hal_uart_set_irq_cb(UART_CH, uart_on_irq_cb, app);
    
    // Create stream buffer
    app->uart_stream = xStreamBufferCreate(UART_BUFFER_SIZE, 1);
    
    // Create UART worker thread
    app->uart_thread = furi_thread_alloc();
    furi_thread_set_name(app->uart_thread, "UartRxThread");
    furi_thread_set_stack_size(app->uart_thread, 1024);
    furi_thread_set_callback(app->uart_thread, uart_worker);
    furi_thread_set_context(app->uart_thread, app);
    furi_thread_start(app->uart_thread);
}

static void uart_free(GennaroAIApp* app) {
    // Stop UART worker
    furi_thread_flags_set(furi_thread_get_id(app->uart_thread), FuriThreadFlagExit);
    furi_thread_join(app->uart_thread);
    furi_thread_free(app->uart_thread);
    
    // Free stream buffer
    vStreamBufferDelete(app->uart_stream);
    
    // Restore console
    furi_hal_uart_set_irq_cb(UART_CH, NULL, NULL);
    furi_hal_console_enable();
}

static void uart_send_string(GennaroAIApp* app, const char* str) {
    if(!str) return;
    
    size_t len = strlen(str);
    furi_hal_uart_tx(UART_CH, (uint8_t*)str, len);
    furi_hal_uart_tx(UART_CH, (uint8_t*)"\r\n", 2);
    
    app->commands_sent++;
}

// ===== ESP32-CAM COMMAND FUNCTIONS =====

static void send_esp32_command(GennaroAIApp* app, const char* command) {
    if(!app || !command) return;
    
    // Add command to console display
    furi_string_cat_printf(app->console_text, "\n>>> %s\n", command);
    
    // Send via UART
    uart_send_string(app, command);
    
    // Notification
    if(app->notifications) {
        notification_message(app->notifications, &sequence_single_vibro);
    }
}

// ===== CONSOLE VIEW =====

static void console_view_update(GennaroAIApp* app) {
    if(app->console_text_box && app->console_text) {
        text_box_set_text(app->console_text_box, furi_string_get_cstr(app->console_text));
        text_box_set_focus(app->console_text_box, TextBoxFocusEnd);
    }
}

static bool console_input_callback(InputEvent* event, void* context) {
    GennaroAIApp* app = (GennaroAIApp*)context;
    
    if(event->type == InputTypePress) {
        if(event->key == InputKeyBack) {
            app->console_active = false;
            view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewSubmenu);
            return true;
        }
    }
    
    return false;
}

static void console_enter_callback(void* context) {
    GennaroAIApp* app = (GennaroAIApp*)context;
    app->console_active = true;
    console_view_update(app);
}

static void console_exit_callback(void* context) {
    GennaroAIApp* app = (GennaroAIApp*)context;
    app->console_active = false;
}

// ===== MAIN MENU CALLBACKS =====

static void main_submenu_callback(void* context, uint32_t index) {
    GennaroAIApp* app = (GennaroAIApp*)context;
    
    switch(index) {
        case GennaroAIMenuConsole:
            view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewConsole);
            break;
        case GennaroAIMenuCommands:
            view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewCommands);
            break;
        case GennaroAIMenuTextInput:
            furi_string_reset(app->text_input_buffer);
            view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewTextInput);
            break;
        case GennaroAIMenuSettings:
            view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewSubmenu); // TODO: Settings
            break;
    }
}

// ===== COMMANDS MENU CALLBACKS =====

static void commands_submenu_callback(void* context, uint32_t index) {
    GennaroAIApp* app = (GennaroAIApp*)context;
    
    const char* commands[] = {"VISION", "MATH", "OCR", "COUNT", "STATUS"};
    
    if(index < sizeof(commands) / sizeof(commands[0])) {
        send_esp32_command(app, commands[index]);
    }
    
    // Auto-switch to console to see response
    view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewConsole);
}

// ===== TEXT INPUT CALLBACKS =====

static void text_input_callback(void* context) {
    GennaroAIApp* app = (GennaroAIApp*)context;
    
    const char* input_text = furi_string_get_cstr(app->text_input_buffer);
    if(strlen(input_text) > 0) {
        send_esp32_command(app, input_text);
    }
    
    // Go to console to see response
    view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewConsole);
}

// ===== CUSTOM EVENT CALLBACK =====

static bool custom_event_callback(void* context, uint32_t event) {
    GennaroAIApp* app = (GennaroAIApp*)context;
    UNUSED(event);
    
    // Update console display
    if(app->console_active) {
        console_view_update(app);
    }
    
    return true;
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
    
    memset(app, 0, sizeof(GennaroAIApp));
    
    // Initialize strings
    app->console_text = furi_string_alloc();
    app->text_input_buffer = furi_string_alloc_set_str("");
    
    // Initialize settings
    app->baud_rate = GennaroAIBaud115200; // Default to 115200 for ESP32
    
    // Notifications
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    
    // View dispatcher
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_enable_queue(app->view_dispatcher);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, custom_event_callback);
    
    // Main submenu
    app->main_submenu = submenu_alloc();
    submenu_add_item(app->main_submenu, "📺 Console Monitor", GennaroAIMenuConsole, main_submenu_callback, app);
    submenu_add_item(app->main_submenu, "🤖 AI Commands", GennaroAIMenuCommands, main_submenu_callback, app);
    submenu_add_item(app->main_submenu, "⌨️ Send Custom", GennaroAIMenuTextInput, main_submenu_callback, app);
    submenu_add_item(app->main_submenu, "⚙️ Settings", GennaroAIMenuSettings, main_submenu_callback, app);
    
    view_set_previous_callback(submenu_get_view(app->main_submenu), navigation_exit_callback);
    view_dispatcher_add_view(app->view_dispatcher, GennaroAIViewSubmenu, submenu_get_view(app->main_submenu));
    
    // Commands submenu
    app->commands_submenu = submenu_alloc();
    submenu_add_item(app->commands_submenu, "👁️ VISION - Analyze Image", GennaroAICommandVision, commands_submenu_callback, app);
    submenu_add_item(app->commands_submenu, "🧮 MATH - Solve Problems", GennaroAICommandMath, commands_submenu_callback, app);
    submenu_add_item(app->commands_submenu, "📝 OCR - Read Text", GennaroAICommandOCR, commands_submenu_callback, app);
    submenu_add_item(app->commands_submenu, "🔢 COUNT - Count Objects", GennaroAICommandCount, commands_submenu_callback, app);
    submenu_add_item(app->commands_submenu, "📊 STATUS - System Info", GennaroAICommandStatus, commands_submenu_callback, app);
    
    view_set_previous_callback(submenu_get_view(app->commands_submenu), navigation_submenu_callback);
    view_dispatcher_add_view(app->view_dispatcher, GennaroAIViewCommands, submenu_get_view(app->commands_submenu));
    
    // Console text box
    app->console_text_box = text_box_alloc();
    text_box_set_font(app->console_text_box, TextBoxFontText);
    view_set_previous_callback(text_box_get_view(app->console_text_box), navigation_submenu_callback);
    view_set_input_callback(text_box_get_view(app->console_text_box), console_input_callback);
    view_set_enter_callback(text_box_get_view(app->console_text_box), console_enter_callback);
    view_set_exit_callback(text_box_get_view(app->console_text_box), console_exit_callback);
    view_dispatcher_add_view(app->view_dispatcher, GennaroAIViewConsole, text_box_get_view(app->console_text_box));
    
    // Text input
    app->text_input = text_input_alloc();
    text_input_set_header_text(app->text_input, "Send Custom Command to ESP32-CAM");
    text_input_set_result_callback(app->text_input, text_input_callback, app, 
                                   app->text_input_buffer, 64, true);
    view_set_previous_callback(text_input_get_view(app->text_input), navigation_submenu_callback);
    view_dispatcher_add_view(app->view_dispatcher, GennaroAIViewTextInput, text_input_get_view(app->text_input));
    
    // Initialize console with welcome message
    furi_string_cat_str(app->console_text, 
        "=== GENNARO AI - ESP32-CAM Assistant ===\n"
        "UART Terminal Ready @ 115200 baud\n"
        "Use AI Commands menu for quick actions\n"
        "Or send custom commands via text input\n\n"
        "Waiting for ESP32-CAM responses...\n\n");
    
    // Initialize UART
    uart_init(app);
    
    return app;
}

static void gennaro_ai_app_free(GennaroAIApp* app) {
    if(!app) return;
    
    // Free UART
    uart_free(app);
    
    // Remove views
    view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewCommands);
    view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewConsole);
    view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewTextInput);
    
    // Free views
    submenu_free(app->main_submenu);
    submenu_free(app->commands_submenu);
    text_box_free(app->console_text_box);
    text_input_free(app->text_input);
    view_dispatcher_free(app->view_dispatcher);
    
    // Free strings
    furi_string_free(app->console_text);
    furi_string_free(app->text_input_buffer);
    
    // Close notifications
    if(app->notifications) furi_record_close(RECORD_NOTIFICATION);
    
    free(app);
}

// ===== MAIN ENTRY POINT =====

int32_t gennaro_ai_app(void* p) {
    UNUSED(p);
    
    FURI_LOG_I(TAG, "Starting Gennaro AI with proven UART terminal base");
    
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
    
    // Welcome notification
    if(app->notifications) {
        notification_message(app->notifications, &sequence_display_backlight_on);
        notification_message(app->notifications, &sequence_single_vibro);
    }
    
    FURI_LOG_I(TAG, "App started with proven UART terminal integration");
    
    // Run
    view_dispatcher_run(app->view_dispatcher);
    
    // Cleanup
    furi_record_close(RECORD_GUI);
    gennaro_ai_app_free(app);
    
    FURI_LOG_I(TAG, "App terminated");
    return 0;
}
