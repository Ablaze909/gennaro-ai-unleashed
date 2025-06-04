#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>

#define TAG "ESP32_AI_Monitor"

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    TextBox* text_box;
    NotificationApp* notifications;
    FuriHalSerialHandle* serial_handle;
    FuriString* response_text;
    FuriThread* rx_thread;
    FuriStreamBuffer* rx_stream;
    bool thread_running;
} App;

typedef enum {
    AppViewSubmenu,
    AppViewTextBox,
} AppView;

typedef enum {
    AppEventVision = 0,
    AppEventMath,
    AppEventOCR,
    AppEventCount,
    AppEventStatus,
    AppEventFlash,
    AppEventPTT,
} AppEvent;

// UART receive thread - SEMPLICE
static int32_t uart_rx_thread(void* context) {
    App* app = context;
    uint8_t buffer[256];
    
    while(app->thread_running) {
        size_t bytes_read = 0;
        
        // Try to read from UART (simplified approach)
        if(app->serial_handle) {
            // Simple blocking read attempt
            furi_delay_ms(10);
            // For now, simulate receiving data for testing
            // Replace this with actual UART read when connected to real ESP32
        }
        
        furi_delay_ms(50);
    }
    
    return 0;
}

// Read response from ESP32
static void read_response(App* app) {
    furi_string_cat_str(app->response_text, "\n\n--- ESP32 Response ---\n");
    
    uint32_t start_time = furi_get_tick();
    uint8_t buffer[256];
    bool got_response = false;
    
    // Wait up to 10 seconds for response
    while((furi_get_tick() - start_time) < 10000) {
        if(app->rx_stream) {
            size_t bytes = furi_stream_buffer_receive(app->rx_stream, buffer, sizeof(buffer)-1, 100);
            if(bytes > 0) {
                buffer[bytes] = '\0';
                furi_string_cat_str(app->response_text, (char*)buffer);
                got_response = true;
                
                // Check if response is complete (ends with newline or specific markers)
                if(strstr((char*)buffer, "\n") || strstr((char*)buffer, "OK:") || strstr((char*)buffer, "ERROR:")) {
                    break;
                }
            }
        }
    }
    
    if(!got_response) {
        furi_string_cat_str(app->response_text, "⏰ TIMEOUT - No response from ESP32\n\n");
        furi_string_cat_str(app->response_text, "Check connections:\n");
        furi_string_cat_str(app->response_text, "• Pin 13 (TX) → ESP32 GPIO3 (RX)\n");
        furi_string_cat_str(app->response_text, "• Pin 14 (RX) → ESP32 GPIO1 (TX)\n");
        furi_string_cat_str(app->response_text, "• Pin 8/18 (GND) → ESP32 GND\n");
        furi_string_cat_str(app->response_text, "• Pin 1 (5V) → ESP32 5V\n");
        furi_string_cat_str(app->response_text, "• Enable 5V in GPIO menu");
    } else {
        notification_message(app->notifications, &sequence_success);
    }
    
    // Update text box with full conversation
    text_box_set_text(app->text_box, furi_string_get_cstr(app->response_text));
}

// Send command via UART
static void send_command(App* app, const char* cmd) {
    // Add command to display
    furi_string_cat_printf(app->response_text, "\n> %s\n", cmd);
    text_box_set_text(app->text_box, furi_string_get_cstr(app->response_text));
    
    // Send via UART
    if(app->serial_handle) {
        furi_hal_serial_tx(app->serial_handle, (uint8_t*)cmd, strlen(cmd));
        furi_hal_serial_tx(app->serial_handle, (uint8_t*)"\n", 1);
        notification_message(app->notifications, &sequence_single_vibro);
        FURI_LOG_I(TAG, "Sent: %s", cmd);
        
        // Wait and read response
        read_response(app);
    } else {
        furi_string_cat_str(app->response_text, "❌ UART not connected!\n");
        text_box_set_text(app->text_box, furi_string_get_cstr(app->response_text));
    }
}

// Clear screen
static void clear_screen(App* app) {
    furi_string_reset(app->response_text);
    furi_string_set_str(app->response_text, "ESP32-CAM AI Monitor\n");
    furi_string_cat_str(app->response_text, "Commands will appear here...\n");
    text_box_set_text(app->text_box, furi_string_get_cstr(app->response_text));
}

// Submenu callback
static void submenu_callback(void* context, uint32_t index) {
    App* app = context;
    
    const char* commands[] = {
        "VISION",       // Vision Analysis
        "MATH",         // Math Solver  
        "OCR",          // Text OCR
        "COUNT",        // Object Count
        "STATUS",       // System Status
        "FLASH_TOGGLE", // Flash Control
        "PTT_START"     // Voice Record
    };
    
    const char* names[] = {
        "Vision Analysis",
        "Math Solver",
        "Text OCR", 
        "Object Count",
        "System Status",
        "Flash Control",
        "Voice Record"
    };
    
    if(index < sizeof(commands)/sizeof(commands[0])) {
        // Switch to text view
        view_dispatcher_switch_to_view(app->view_dispatcher, AppViewTextBox);
        
        // Add command description
        furi_string_cat_printf(app->response_text, "\n📡 %s\n", names[index]);
        
        // Send command and show response
        send_command(app, commands[index]);
    }
}

// Navigation callback
static bool navigation_callback(void* context) {
    App* app = context;
    
    // If on text view, go back to menu
    view_dispatcher_switch_to_view(app->view_dispatcher, AppViewSubmenu);
    return true;
}

// Custom event callback for clearing screen
static bool custom_event_callback(void* context, uint32_t event) {
    App* app = context;
    
    if(event == 999) { // Clear screen event
        clear_screen(app);
        return true;
    }
    
    return false;
}

// App entry point
int32_t esp32_ai_monitor_app(void* p) {
    UNUSED(p);
    
    // Allocate app
    App* app = malloc(sizeof(App));
    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->response_text = furi_string_alloc();
    app->thread_running = false;
    
    // Setup UART
    app->serial_handle = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(app->serial_handle) {
        furi_hal_serial_init(app->serial_handle, 115200);
        
        // Setup RX stream and thread
        app->rx_stream = furi_stream_buffer_alloc(1024, 1);
        app->thread_running = true;
        app->rx_thread = furi_thread_alloc_ex("UART_RX", 1024, uart_rx_thread, app);
        furi_thread_start(app->rx_thread);
        
        FURI_LOG_I(TAG, "UART OK on pins 13(TX), 14(RX) @ 115200");
    } else {
        FURI_LOG_E(TAG, "UART FAILED - Check GPIO permissions");
    }
    
    // Setup GUI
    app->view_dispatcher = view_dispatcher_alloc();
    app->submenu = submenu_alloc();
    app->text_box = text_box_alloc();
    
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, navigation_callback);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, custom_event_callback);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    
    // Setup submenu
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "ESP32-CAM AI Monitor");
    submenu_add_item(app->submenu, "📷 Vision Analysis", AppEventVision, submenu_callback, app);
    submenu_add_item(app->submenu, "🧮 Math Solver", AppEventMath, submenu_callback, app);
    submenu_add_item(app->submenu, "📖 Text OCR", AppEventOCR, submenu_callback, app);
    submenu_add_item(app->submenu, "🔢 Object Count", AppEventCount, submenu_callback, app);
    submenu_add_item(app->submenu, "📊 System Status", AppEventStatus, submenu_callback, app);
    submenu_add_item(app->submenu, "💡 Flash Control", AppEventFlash, submenu_callback, app);
    submenu_add_item(app->submenu, "🎤 Voice Record", AppEventPTT, submenu_callback, app);
    
    // Setup text box
    clear_screen(app);
    text_box_set_focus(app->text_box, TextBoxFocusEnd); // Show latest text
    
    // Add views
    view_dispatcher_add_view(app->view_dispatcher, AppViewSubmenu, submenu_get_view(app->submenu));
    view_dispatcher_add_view(app->view_dispatcher, AppViewTextBox, text_box_get_view(app->text_box));
    
    // Start with submenu
    view_dispatcher_switch_to_view(app->view_dispatcher, AppViewSubmenu);
    
    // Run app
    view_dispatcher_run(app->view_dispatcher);
    
    // Cleanup
    app->thread_running = false;
    if(app->rx_thread) {
        furi_thread_join(app->rx_thread);
        furi_thread_free(app->rx_thread);
    }
    if(app->rx_stream) {
        furi_stream_buffer_free(app->rx_stream);
    }
    
    view_dispatcher_remove_view(app->view_dispatcher, AppViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, AppViewTextBox);
    submenu_free(app->submenu);
    text_box_free(app->text_box);
    view_dispatcher_free(app->view_dispatcher);
    
    if(app->serial_handle) {
        furi_hal_serial_deinit(app->serial_handle);
        furi_hal_serial_control_release(app->serial_handle);
    }
    
    furi_string_free(app->response_text);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);
    free(app);
    
    return 0;
}
