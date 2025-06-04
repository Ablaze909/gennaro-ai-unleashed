/*
 * GENNARO AI - ESP32-CAM AI Assistant
 * Unleashed Firmware Compatible Version
 * Based on official Unleashed documentation and API
 */

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/modules/text_input.h>
#include <notification/notification_messages.h>
#include <furi_hal_uart.h>
#include <furi_hal_console.h>
#include <stream_buffer.h>

#define TAG "GennaroAI"

// UART Configuration for ESP32-CAM
#define UART_CH (FuriHalUartIdUSART1)
#define UART_BAUD_RATE (115200)
#define RX_BUFFER_SIZE (512)

// Views
typedef enum {
    GennaroAIViewSubmenu,
    GennaroAIViewConsole,
    GennaroAIViewTextInput,
} GennaroAIView;

// Menu items
typedef enum {
    GennaroAIMenuConsole,
    GennaroAIMenuVision,
    GennaroAIMenuMath,
    GennaroAIMenuOCR,
    GennaroAIMenuCount,
    GennaroAIMenuStatus,
    GennaroAIMenuCustom,
} GennaroAIMenuItem;

// App structure
typedef struct {
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    TextBox* text_box;
    TextInput* text_input;
    NotificationApp* notifications;
    
    // UART handling
    FuriThread* worker_thread;
    StreamBufferHandle_t rx_stream;
    FuriString* console_text;
    FuriString* input_text;
    
    // State
    bool running;
    uint32_t commands_sent;
} GennaroAIApp;

// ===== UART WORKER THREAD =====

static void uart_on_irq_cb(UartIrqEvent ev, uint8_t data, void* context) {
    GennaroAIApp* app = (GennaroAIApp*)context;
    
    if(ev == UartIrqEventRXNE) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xStreamBufferSendFromISR(app->rx_stream, &data, 1, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

static int32_t uart_worker(void* context) {
    GennaroAIApp* app = (GennaroAIApp*)context;
    
    uint8_t data[32];
    while(app->running) {
        size_t received = xStreamBufferReceive(app->rx_stream, data, sizeof(data), 100);
        
        if(received > 0) {
            FURI_CRITICAL_ENTER();
            
            for(size_t i = 0; i < received; i++) {
                char c = (char)data[i];
                if(c >= 32 && c <= 126) {
                    // Printable character
                    furi_string_push_back(app->console_text, c);
                } else if(c == '\n' || c == '\r') {
                    // Newline
                    furi_string_push_back(app->console_text, '\n');
                }
            }
            
            // Limit buffer size
            if(furi_string_size(app->console_text) > 4096) {
                furi_string_right(app->console_text, 2048);
            }
            
            FURI_CRITICAL_EXIT();
        }
    }
    
    return 0;
}

// ===== UART FUNCTIONS =====

static void uart_init(GennaroAIApp* app) {
    // Disable console to use UART
    furi_hal_console_disable();
    
    // Setup UART
    furi_hal_uart_set_br(UART_CH, UART_BAUD_RATE);
    furi_hal_uart_set_irq_cb(UART_CH, uart_on_irq_cb, app);
    
    // Create stream buffer for RX data
    app->rx_stream = xStreamBufferCreate(RX_BUFFER_SIZE, 1);
    
    // Start worker thread
    app->worker_thread = furi_thread_alloc();
    furi_thread_set_name(app->worker_thread, "UartWorker");
    furi_thread_set_stack_size(app->worker_thread, 1024);
    furi_thread_set_callback(app->worker_thread, uart_worker);
    furi_thread_set_context(app->worker_thread, app);
    furi_thread_start(app->worker_thread);
}

static void uart_deinit(GennaroAIApp* app) {
    // Stop worker thread
    app->running = false;
    furi_thread_join(app->worker_thread);
    furi_thread_free(app->worker_thread);
    
    // Clean up UART
    furi_hal_uart_set_irq_cb(UART_CH, NULL, NULL);
    
    // Clean up stream buffer
    vStreamBufferDelete(app->rx_stream);
    
    // Re-enable console
    furi_hal_console_enable();
}

static void send_command_to_esp32(GennaroAIApp* app, const char* command) {
    if(!app || !command) return;
    
    FURI_LOG_I(TAG, "Sending: %s", command);
    
    // Add command to console display
    FURI_CRITICAL_ENTER();
    furi_string_cat_printf(app->console_text, "\n>>> %s\n", command);
    FURI_CRITICAL_EXIT();
    
    // Send via UART
    size_t len = strlen(command);
    furi_hal_uart_tx(UART_CH, (uint8_t*)command, len);
    furi_hal_uart_tx(UART_CH, (uint8_t*)"\r\n", 2);
    
    app->commands_sent++;
    
    // Notification
    if(app->notifications) {
        notification_message(app->notifications, &sequence_single_vibro);
    }
}

// ===== VIEW CALLBACKS =====

static void submenu_callback(void* context, uint32_t index) {
    GennaroAIApp* app = (GennaroAIApp*)context;
    
    switch(index) {
        case GennaroAIMenuConsole:
            text_box_set_text(app->text_box, furi_string_get_cstr(app->console_text));
            text_box_set_focus(app->text_box, TextBoxFocusEnd);
            view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewConsole);
            break;
            
        case GennaroAIMenuVision:
            send_command_to_esp32(app, "VISION");
            text_box_set_text(app->text_box, furi_string_get_cstr(app->console_text));
            text_box_set_focus(app->text_box, TextBoxFocusEnd);
            view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewConsole);
            break;
            
        case GennaroAIMenuMath:
            send_command_to_esp32(app, "MATH");
            text_box_set_text(app->text_box, furi_string_get_cstr(app->console_text));
            text_box_set_focus(app->text_box, TextBoxFocusEnd);
            view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewConsole);
            break;
            
        case GennaroAIMenuOCR:
            send_command_to_esp32(app, "OCR");
            text_box_set_text(app->text_box, furi_string_get_cstr(app->console_text));
            text_box_set_focus(app->text_box, TextBoxFocusEnd);
            view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewConsole);
            break;
            
        case GennaroAIMenuCount:
            send_command_to_esp32(app, "COUNT");
            text_box_set_text(app->text_box, furi_string_get_cstr(app->console_text));
            text_box_set_focus(app->text_box, TextBoxFocusEnd);
            view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewConsole);
            break;
            
        case GennaroAIMenuStatus:
            send_command_to_esp32(app, "STATUS");
            text_box_set_text(app->text_box, furi_string_get_cstr(app->console_text));
            text_box_set_focus(app->text_box, TextBoxFocusEnd);
            view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewConsole);
            break;
            
        case GennaroAIMenuCustom:
            furi_string_reset(app->input_text);
            view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewTextInput);
            break;
    }
}

static void text_input_callback(void* context) {
    GennaroAIApp* app = (GennaroAIApp*)context;
    
    const char* command = furi_string_get_cstr(app->input_text);
    if(strlen(command) > 0) {
        send_command_to_esp32(app, command);
        text_box_set_text(app->text_box, furi_string_get_cstr(app->console_text));
        text_box_set_focus(app->text_box, TextBoxFocusEnd);
        view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewConsole);
    } else {
        view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewSubmenu);
    }
}

static bool console_input_callback(InputEvent* event, void* context) {
    GennaroAIApp* app = (GennaroAIApp*)context;
    
    if(event->type == InputTypePress) {
        switch(event->key) {
            case InputKeyBack:
                view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewSubmenu);
                return true;
            case InputKeyOk:
                // Refresh console
                text_box_set_text(app->text_box, furi_string_get_cstr(app->console_text));
                text_box_set_focus(app->text_box, TextBoxFocusEnd);
                return true;
            default:
                break;
        }
    }
    
    return false;
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

// ===== APP LIFECYCLE =====

static GennaroAIApp* gennaro_ai_app_alloc() {
    GennaroAIApp* app = malloc(sizeof(GennaroAIApp));
    if(!app) return NULL;
    
    // Zero initialize
    memset(app, 0, sizeof(GennaroAIApp));
    app->running = true;
    
    // Allocate strings
    app->console_text = furi_string_alloc();
    app->input_text = furi_string_alloc();
    
    // Set initial console text
    furi_string_set_str(app->console_text, 
        "=== GENNARO AI - ESP32-CAM Assistant ===\n"
        "UART @ 115200 baud initialized\n"
        "Ready to send AI commands to ESP32-CAM\n\n"
        "Available commands:\n"
        "- VISION: AI image analysis\n"
        "- MATH: Solve mathematical problems\n" 
        "- OCR: Optical character recognition\n"
        "- COUNT: Count objects in image\n"
        "- STATUS: System status check\n\n"
        "Use menu to send commands or type custom ones.\n"
        "ESP32-CAM responses will appear here.\n\n");
    
    // Get notifications
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    
    // Create view dispatcher
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_enable_queue(app->view_dispatcher);
    
    // Create submenu
    app->submenu = submenu_alloc();
    submenu_add_item(app->submenu, "📺 Console Monitor", GennaroAIMenuConsole, submenu_callback, app);
    submenu_add_item(app->submenu, "👁️ VISION Analysis", GennaroAIMenuVision, submenu_callback, app);
    submenu_add_item(app->submenu, "🧮 MATH Solver", GennaroAIMenuMath, submenu_callback, app);
    submenu_add_item(app->submenu, "📝 OCR Text", GennaroAIMenuOCR, submenu_callback, app);
    submenu_add_item(app->submenu, "🔢 COUNT Objects", GennaroAIMenuCount, submenu_callback, app);
    submenu_add_item(app->submenu, "📊 STATUS Check", GennaroAIMenuStatus, submenu_callback, app);
    submenu_add_item(app->submenu, "⌨️ Custom Command", GennaroAIMenuCustom, submenu_callback, app);
    
    view_set_previous_callback(submenu_get_view(app->submenu), navigation_exit_callback);
    view_dispatcher_add_view(app->view_dispatcher, GennaroAIViewSubmenu, submenu_get_view(app->submenu));
    
    // Create text box for console
    app->text_box = text_box_alloc();
    text_box_set_font(app->text_box, TextBoxFontText);
    view_set_previous_callback(text_box_get_view(app->text_box), navigation_submenu_callback);
    view_set_input_callback(text_box_get_view(app->text_box), console_input_callback);
    view_dispatcher_add_view(app->view_dispatcher, GennaroAIViewConsole, text_box_get_view(app->text_box));
    
    // Create text input
    app->text_input = text_input_alloc();
    text_input_set_header_text(app->text_input, "Enter command for ESP32-CAM");
    text_input_set_result_callback(app->text_input, text_input_callback, app, app->input_text, 32, true);
    view_set_previous_callback(text_input_get_view(app->text_input), navigation_submenu_callback);
    view_dispatcher_add_view(app->view_dispatcher, GennaroAIViewTextInput, text_input_get_view(app->text_input));
    
    return app;
}

static void gennaro_ai_app_free(GennaroAIApp* app) {
    if(!app) return;
    
    // Stop UART worker
    uart_deinit(app);
    
    // Remove views
    view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewConsole);
    view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewTextInput);
    
    // Free views
    submenu_free(app->submenu);
    text_box_free(app->text_box);
    text_input_free(app->text_input);
    view_dispatcher_free(app->view_dispatcher);
    
    // Free strings
    furi_string_free(app->console_text);
    furi_string_free(app->input_text);
    
    // Close notifications
    if(app->notifications) {
        furi_record_close(RECORD_NOTIFICATION);
    }
    
    free(app);
}

// ===== MAIN ENTRY POINT =====

int32_t gennaro_ai_app(void* p) {
    UNUSED(p);
    
    FURI_LOG_I(TAG, "Starting Gennaro AI for Unleashed firmware");
    
    // Allocate app
    GennaroAIApp* app = gennaro_ai_app_alloc();
    if(!app) {
        FURI_LOG_E(TAG, "Failed to allocate app");
        return -1;
    }
    
    // Initialize UART
    uart_init(app);
    
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
    
    FURI_LOG_I(TAG, "Gennaro AI ready - UART initialized at 115200 baud");
    
    // Run app
    view_dispatcher_run(app->view_dispatcher);
    
    // Cleanup
    furi_record_close(RECORD_GUI);
    gennaro_ai_app_free(app);
    
    FURI_LOG_I(TAG, "Gennaro AI terminated");
    return 0;
}
