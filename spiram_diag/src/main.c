#include <stdint.h>
#include <stddef.h>

#include <soc/soc_memory_layout.h>
#include <zephyr/kernel.h>
#include <zephyr/multi_heap/shared_multi_heap.h>
#include <zephyr/sys/printk.h>

#define SMALL_TEST_SIZE (32U * 1024U)
#define LCD_WIDTH 320U
#define LCD_HEIGHT 240U
#define LCD_FRAMEBUFFER_SIZE (LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t))
#define ALLOC_ALIGNMENT 32U

#define BYTE_PATTERN 0xa5U
#define WORD_PATTERN 0xaa55U
#define DWORD_PATTERN 0xaaaa5555U

static uint16_t static_lcd_framebuffer[LCD_WIDTH * LCD_HEIGHT]
    __attribute__((section(".lvgl_buf.spiram_diag_framebuffer"), aligned(ALLOC_ALIGNMENT)));

static int test_byte_access(uint8_t *buffer, size_t size)
{
    printk("byte test start: %zu bytes\n", size);

    for (size_t i = 0; i < size; i++) {
        buffer[i] = BYTE_PATTERN;
    }

    for (size_t i = 0; i < size; i++) {
        if (buffer[i] != BYTE_PATTERN) {
            printk("byte test failed at %zu: 0x%02x\n", i, buffer[i]);
            return -EIO;
        }
    }

    printk("byte test OK\n");
    return 0;
}

static int test_word_access(uint16_t *buffer, size_t words)
{
    printk("word test start: %zu words\n", words);

    for (size_t i = 0; i < words; i++) {
        buffer[i] = WORD_PATTERN;
    }

    for (size_t i = 0; i < words; i++) {
        if (buffer[i] != WORD_PATTERN) {
            printk("word test failed at %zu: 0x%04x\n", i, buffer[i]);
            return -EIO;
        }
    }

    printk("word test OK\n");
    return 0;
}

static int test_dword_access(uint32_t *buffer, size_t dwords)
{
    printk("dword test start: %zu dwords\n", dwords);

    for (size_t i = 0; i < dwords; i++) {
        buffer[i] = DWORD_PATTERN;
    }

    for (size_t i = 0; i < dwords; i++) {
        if (buffer[i] != DWORD_PATTERN) {
            printk("dword test failed at %zu: 0x%08x\n", i, buffer[i]);
            return -EIO;
        }
    }

    printk("dword test OK\n");
    return 0;
}

static int run_psram_static_block_test(void)
{
    void *buffer = static_lcd_framebuffer;
    int ret;

    printk("static PSRAM framebuffer test start\n");
    printk("static PSRAM framebuffer addr: %p\n", buffer);
    printk("static PSRAM framebuffer size: %u bytes\n", LCD_FRAMEBUFFER_SIZE);

    if (!esp_ptr_external_ram(buffer)) {
        printk("static framebuffer is not external RAM\n");
        return -EFAULT;
    }

    printk("static framebuffer is external RAM\n");

    ret = test_byte_access((uint8_t *)buffer, LCD_FRAMEBUFFER_SIZE);
    if (ret != 0) {
        return ret;
    }

    ret = test_word_access((uint16_t *)buffer,
                           LCD_FRAMEBUFFER_SIZE / sizeof(uint16_t));
    if (ret != 0) {
        return ret;
    }

    ret = test_dword_access((uint32_t *)buffer,
                            LCD_FRAMEBUFFER_SIZE / sizeof(uint32_t));
    if (ret != 0) {
        return ret;
    }

    printk("static PSRAM framebuffer test OK\n");
    return 0;
}

static int run_psram_block_test(size_t size)
{
    void *buffer;
    int ret;

    printk("external heap alloc start: %zu bytes\n", size);
    buffer = shared_multi_heap_aligned_alloc(SMH_REG_ATTR_EXTERNAL,
                                             ALLOC_ALIGNMENT,
                                             size);
    printk("external heap alloc result: %p\n", buffer);

    if (buffer == NULL) {
        printk("external heap alloc failed\n");
        return -ENOMEM;
    }

    if (!esp_ptr_external_ram(buffer)) {
        printk("allocated buffer is not external RAM\n");
        shared_multi_heap_free(buffer);
        return -EFAULT;
    }

    printk("allocated buffer is external RAM\n");

    ret = test_byte_access((uint8_t *)buffer, size);
    if (ret != 0) {
        shared_multi_heap_free(buffer);
        return ret;
    }

    ret = test_word_access((uint16_t *)buffer, size / sizeof(uint16_t));
    if (ret != 0) {
        shared_multi_heap_free(buffer);
        return ret;
    }

    ret = test_dword_access((uint32_t *)buffer, size / sizeof(uint32_t));
    if (ret != 0) {
        shared_multi_heap_free(buffer);
        return ret;
    }

    shared_multi_heap_free(buffer);
    printk("external heap block test OK: %zu bytes\n", size);
    return 0;
}

int main(void)
{
    int ret;

    printk("SPIRAM diagnostic start\n");
    printk("small block test size: %u bytes\n", SMALL_TEST_SIZE);
    printk("LCD framebuffer test size: %u bytes\n", LCD_FRAMEBUFFER_SIZE);

    ret = run_psram_static_block_test();
    if (ret != 0) {
        printk("static framebuffer test failed: %d\n", ret);
        return 0;
    }

    ret = run_psram_block_test(SMALL_TEST_SIZE);
    if (ret != 0) {
        printk("small block test failed: %d\n", ret);
        return 0;
    }

    ret = run_psram_block_test(LCD_FRAMEBUFFER_SIZE);
    if (ret != 0) {
        printk("LCD framebuffer-sized test failed: %d\n", ret);
        return 0;
    }

    printk("SPIRAM diagnostic PASS\n");
    return 0;
}
