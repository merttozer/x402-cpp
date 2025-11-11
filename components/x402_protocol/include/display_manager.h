#pragma once

#include <string>
#include <functional>
#include "lvgl.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_touch.h"

/**
 * @brief Display Manager for ESP32-C6 Touch LCD 1.69"
 * 
 * Manages LVGL initialization, display lifecycle, touch input,
 * and provides simple text display methods for status updates.
 */
class DisplayManager {
public:
    DisplayManager();
    ~DisplayManager();

    // Disable copy/move
    DisplayManager(const DisplayManager&) = delete;
    DisplayManager& operator=(const DisplayManager&) = delete;

    /**
     * @brief Initialize display hardware and LVGL
     * @return true if initialization successful
     */
    bool init();

    /**
     * @brief Deinitialize and cleanup display resources
     */
    void deinit();

    /**
     * @brief Show idle screen with "Start Payment" button
     * @param callback Function to call when button is clicked
     */
    void showIdleScreen(std::function<void()> callback);

    /**
     * @brief Show text on display with black background and white text
     * @param text Text to display (supports newlines)
     * @param centered If true, center the text. If false, align left
     */
    void showText(const char* text, bool centered = true);

    /**
     * @brief Show multi-line status with title and message
     * @param title Bold title text (e.g., "WiFi")
     * @param message Optional message below title (e.g., "Connecting...")
     */
    void showStatus(const char* title, const char* message = nullptr);

    /**
     * @brief Show success message with green accent
     * @param message Success message to display
     */
    void showSuccess(const char* message);

    /**
     * @brief Show error message with red accent
     * @param message Error message to display
     */
    void showError(const char* message);

    /**
     * @brief Clear display to black (hides all UI elements)
     */
    void clear();

    /**
     * @brief Hide all UI elements (for clean state transitions)
     */
    void hideAll();

    /**
     * @brief Set display brightness (0-100%)
     * @param brightness Brightness level 0-100
     */
    void setBrightness(uint8_t brightness);

    /**
     * @brief Check if display is initialized
     */
    bool isInitialized() const { return initialized_; }

private:
    void initLVGL();
    void initDisplay();
    void initTouch();
    void initBacklight();
    void lockLVGL();
    void unlockLVGL();
    
    static void touchReadCallback(lv_indev_t* indev, lv_indev_data_t* data);
    static void buttonEventCallback(lv_event_t* e);

    // Hardware handles
    esp_lcd_panel_io_handle_t io_handle_;
    esp_lcd_panel_handle_t panel_handle_;
    esp_lcd_touch_handle_t touch_handle_;
    lv_disp_t* lvgl_disp_;
    lv_indev_t* lvgl_touch_;
    
    // UI elements
    lv_obj_t* label_;
    lv_obj_t* idle_container_;
    lv_obj_t* button_;
    lv_obj_t* button_label_;
    
    // Callback for button press
    std::function<void()> button_callback_;
    
    // State
    bool initialized_;
    uint8_t brightness_;
};