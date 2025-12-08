#include <Arduino.h>
#include <driver/spi_slave.h>
#include "config.h"

// ==================== SPI BUFFERS ====================
WORD_ALIGNED_ATTR uint8_t rx_buf[BUFFER_SIZE];
WORD_ALIGNED_ATTR uint8_t tx_buf[BUFFER_SIZE];

// ==================== SPI STATE ====================
static bool spiInitialized = false;
static bool transactionQueued = false;
static spi_slave_transaction_t currentTrans;

// ==================== SPI INITIALIZATION ====================
void setup_spi_slave() {
    spi_bus_config_t buscfg = {
        .mosi_io_num = SPI_MOSI_PIN,
        .miso_io_num = SPI_MISO_PIN,
        .sclk_io_num = SPI_SCLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BUFFER_SIZE
    };
    
    spi_slave_interface_config_t slvcfg = {
        .spics_io_num = SPI_CS_PIN,
        .flags = 0,
        .queue_size = 3,
        .mode = 0
    };

    esp_err_t ret = spi_slave_initialize(SPI2_HOST, &buscfg, &slvcfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        Serial.printf("❌ SPI init failed: %d\n", ret);
        while(1) delay(1000);
    }
    
    memset(tx_buf, 0xFF, BUFFER_SIZE);
    memset(rx_buf, 0, BUFFER_SIZE);
    spiInitialized = true;
    transactionQueued = false;
    
    Serial.println("✓ SPI Slave ready (non-blocking mode)");
}

// ==================== CHECK IF DATA READY ====================
bool is_spi_data_ready() {
    if (!spiInitialized) return false;
    
    // Queue a transaction if none pending
    if (!transactionQueued) {
        memset(&currentTrans, 0, sizeof(currentTrans));
        currentTrans.length = BUFFER_SIZE * 8;
        currentTrans.rx_buffer = rx_buf;
        currentTrans.tx_buffer = tx_buf;
        
        if (spi_slave_queue_trans(SPI2_HOST, &currentTrans, 0) == ESP_OK) {
            transactionQueued = true;
        }
    }
    
    return transactionQueued;
}

// ==================== NON-BLOCKING SPI TRANSACTION ====================
bool handle_spi_transaction_nonblocking() {
    if (!transactionQueued) return false;
    
    spi_slave_transaction_t* completedTrans;
    
    // Non-blocking check for completed transaction
    esp_err_t ret = spi_slave_get_trans_result(SPI2_HOST, &completedTrans, 0);
    
    if (ret == ESP_OK && completedTrans->trans_len > 0) {
        transactionQueued = false;
        return true;
    }
    
    if (ret == ESP_ERR_TIMEOUT) {
        // No data yet, transaction still pending
        return false;
    }
    
    // Error occurred
    transactionQueued = false;
    return false;
}

// ==================== LEGACY BLOCKING VERSION ====================
bool handle_spi_transaction(void* ws_handle) {
    spi_slave_transaction_t trans;
    memset(&trans, 0, sizeof(trans));
    trans.length = BUFFER_SIZE * 8;
    trans.rx_buffer = rx_buf;
    trans.tx_buffer = tx_buf;
    
    // Reduced timeout - 10ms instead of 100ms
    if (spi_slave_transmit(SPI2_HOST, &trans, pdMS_TO_TICKS(10)) == ESP_OK) {
        return (trans.trans_len > 0);
    }
    return false;
}

// ==================== GET BUFFER ACCESS ====================
uint8_t* get_rx_buffer() {
    return rx_buf;
}

size_t get_last_transaction_length() {
    return BUFFER_SIZE;
}