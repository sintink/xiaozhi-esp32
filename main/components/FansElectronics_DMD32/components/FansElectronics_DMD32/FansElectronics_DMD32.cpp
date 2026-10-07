//----------------------------------------------------------------------------------------------
/*
 * Creator             : Irfan Indra Kurniawan, ST
 * Website             : https://www.fanselectronics.com
 * Buy Me a Coffee     : https://saweria.co/FansElectronics
 * Ported for Multi-Panel Type (HUB08 & HUB12) on ESP32
 * Support P10, P4.25, P7.62, dan panel custom dengan konfigurasi pin fleksibel
 */

#include "FansElectronics_DMD32.h"

// ID: Makro pintar menjamin kompatibilitas fungsi LEDC lintas versi Arduino ESP32 Core
// EN: Smart macro ensuring LEDC function compatibility across Arduino ESP32 Core versions
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
#define WRITE_PWM(pin, chan, val) ledcWrite(pin, val)
#else
#define WRITE_PWM(pin, chan, val) ledcWrite(chan, val)
#endif

// ID: Bendera sinyal interupsi perangkat keras
// EN: Hardware interrupt signal flags
volatile bool tickOccured = false;

// ID: Fungsi Callback Timer dijalankan langsung di IRAM demi kecepatan mutlak
// EN: Timer Callback function executed directly from IRAM for absolute speed
void IRAM_ATTR timerCallback(void *pArg)
{
    tickOccured = true;
}

// ID: Tabel pembalik urutan bit (MSB ke LSB) untuk pemindaian baris zigzag HUB12
// EN: Bit reversal lookup table (MSB to LSB) for HUB12 zigzag row scanning
static const uint8_t flipBits[256] PROGMEM = {
    0x00, 0x80, 0x40, 0xC0, 0x20, 0xA0, 0x60, 0xE0, 0x10, 0x90, 0x50, 0xD0,
    0x30, 0xB0, 0x70, 0xF0, 0x08, 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8,
    0x18, 0x98, 0x58, 0xD8, 0x38, 0xB8, 0x78, 0xF8, 0x04, 0x84, 0x44, 0xC4,
    0x24, 0xA4, 0x64, 0xE4, 0x14, 0x94, 0x54, 0xD4, 0x34, 0xB4, 0x74, 0xF4,
    0x0C, 0x8C, 0x4C, 0xCC, 0x2C, 0xAC, 0x6C, 0xEC, 0x1C, 0x9C, 0x5C, 0xDC,
    0x3C, 0xBC, 0x7C, 0xFC, 0x02, 0x82, 0x42, 0xC2, 0x22, 0xA2, 0x62, 0xE2,
    0x12, 0x92, 0x52, 0xD2, 0x32, 0xB2, 0x72, 0xF2, 0x0A, 0x8A, 0x4A, 0xCA,
    0x2A, 0xAA, 0x6A, 0xEA, 0x1A, 0x9A, 0x5A, 0xDA, 0x3A, 0xBA, 0x7A, 0xFA,
    0x06, 0x86, 0x46, 0xC6, 0x26, 0xA6, 0x66, 0xE6, 0x16, 0x96, 0x56, 0xD6,
    0x36, 0xB6, 0x76, 0xF6, 0x0E, 0x8E, 0x4E, 0xCE, 0x2E, 0xAE, 0x6E, 0xEE,
    0x1E, 0x9E, 0x5E, 0xDE, 0x3E, 0xBE, 0x7E, 0xFE, 0x01, 0x81, 0x41, 0xC1,
    0x21, 0xA1, 0x61, 0xE1, 0x11, 0x91, 0x51, 0xD1, 0x31, 0xB1, 0x71, 0xF1,
    0x09, 0x89, 0x49, 0xC9, 0x29, 0xA9, 0x69, 0xE9, 0x19, 0x99, 0x59, 0xD9,
    0x39, 0xB9, 0x79, 0xF9, 0x05, 0x85, 0x45, 0xC5, 0x25, 0xA5, 0x65, 0xE5,
    0x15, 0x95, 0x55, 0xD5, 0x35, 0xB5, 0x75, 0xF5, 0x0D, 0x8D, 0x4D, 0xCD,
    0x2D, 0xAD, 0x6D, 0xED, 0x1D, 0x9D, 0x5D, 0xDD, 0x3D, 0xBD, 0x7D, 0xFD,
    0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3, 0x13, 0x93, 0x53, 0xD3,
    0x33, 0xB3, 0x73, 0xF3, 0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB,
    0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB, 0x07, 0x87, 0x47, 0xC7,
    0x27, 0xA7, 0x67, 0xE7, 0x17, 0x97, 0x57, 0xD7, 0x37, 0xB7, 0x77, 0xF7,
    0x0F, 0x8F, 0x4F, 0xCF, 0x2F, 0xAF, 0x6F, 0xEF, 0x1F, 0x9F, 0x5F, 0xDF,
    0x3F, 0xBF, 0x7F, 0xFF};

//----------------------------------------------------------------------------------------------
FansElectronics_DMD32::FansElectronics_DMD32()
    : FansElectronics_Bitmap(0, 0), _doubleBuffer(false), phase(0), fb0(0), fb1(0), displayfb(0), dmdSPI(NULL), timerHandle(NULL)
{
    brightenss = 255;
}

//----------------------------------------------------------------------------------------------
FansElectronics_DMD32::~FansElectronics_DMD32()
{
    // ID: Pembersihan memori alokasi dinamis untuk mencegah memori bocor
    // EN: Deallocate dynamic memory regions to avoid serious memory leaks
    if (timerHandle)
    {
        esp_timer_stop(timerHandle);
        esp_timer_delete(timerHandle);
    }
    if (fb0)
        free(fb0);
    if (fb1)
        free(fb1);
    if (dmdSPI)
        delete dmdSPI;
    fb = 0;
}

//----------------------------------------------------------------------------------------------
void FansElectronics_DMD32::begin(PanelType type, int panelsWide, int panelsHigh, DmdPins pins, int panelWidthPx, int panelHeightPx)
{
    _panelType = type;
    _panelsWide = panelsWide;
    _panelsHigh = panelsHigh;
    _pins = pins;

    // ID: Pengaman deteksi resolusi otomatis jika masukan pengguna bernilai nol
    // EN: Auto-resolution fallback mechanism if user parameters are zero
    if (panelWidthPx == 0 || panelHeightPx == 0)
    {
        if (_panelType == PANEL_HUB08)
        {
            _panelWidthPx = 64; // Standar HUB08 (P4.25 / P7.62)
            _panelHeightPx = 16;
        }
        else
        {
            _panelWidthPx = 32; // Standar HUB12 (P10)
            _panelHeightPx = 16;
        }
    }
    else
    {
        _panelWidthPx = panelWidthPx;
        _panelHeightPx = panelHeightPx;
    }

    // ID: Menghitung total dimensi kanvas horizontal dan vertikal
    // EN: Computing global horizontal and vertical canvas boundaries
    _width = _panelsWide * _panelWidthPx;
    _height = _panelsHigh * _panelHeightPx;
    _stride = (_width + 7) / 8;

    // ID: Alokasi heap memori untuk buffer frame visual primer
    // EN: Dynamic heap allocation for the primary video framebuffer
    unsigned int size = _stride * _height;
    fb0 = (uint8_t *)malloc(size);
    if (fb0)
    {
        memset(fb0, 0xFF, size);
    }
    fb = displayfb = fb0;

    // ID: Konfigurasi arah kendali GPIO register matriks
    // EN: Configuration of matrix shift-register GPIO control flow
    pinMode(_pins.pin_A, OUTPUT);
    pinMode(_pins.pin_B, OUTPUT);
    if (_pins.pin_C != -1)
        pinMode(_pins.pin_C, OUTPUT);
    if (_pins.pin_D != -1)
        pinMode(_pins.pin_D, OUTPUT);
    pinMode(_pins.pin_latch, OUTPUT);
    pinMode(_pins.pin_oe, OUTPUT);

    digitalWrite(_pins.pin_A, LOW);
    digitalWrite(_pins.pin_B, LOW);
    if (_pins.pin_C != -1)
        digitalWrite(_pins.pin_C, LOW);
    if (_pins.pin_D != -1)
        digitalWrite(_pins.pin_D, LOW);
    digitalWrite(_pins.pin_latch, LOW);
    digitalWrite(_pins.pin_oe, HIGH); // ID: Matikan display saat init (Active Low) | EN: Disable display during init (Active Low)

    // ID: Konfigurasi periferal generator PWM internal kontrol kecerahan
    // EN: Hardware setup for internal PWM generator controlling brightness levels
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcAttach(_pins.pin_oe, 10000, 8);
#else
    ledcSetup(0, 10000, 8);
    ledcAttachPin(_pins.pin_oe, 0);
#endif

    // ID: Alokasi jalur transmisi data SPI perangkat keras pada bus HSPI
    // EN: SPI hardware controller allocation hosted on the HSPI bus peripheral
    dmdSPI = new SPIClass(HSPI);
    dmdSPI->begin(_pins.pin_sck, -1, _pins.pin_mosi, -1);
    dmdSPI->setFrequency(10000000); // 10 MHz

    // ID: Konfigurasi interupsi timer berkala menggunakan ESP-IDF API murni
    // EN: Native ESP-IDF execution of high-priority periodic interrupt timers
    tickOccured = false;
    const esp_timer_create_args_t timerArgs = {
        .callback = &timerCallback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "DMD_Refresh_Timer"};

    esp_timer_create(&timerArgs, &timerHandle);

    // ID: Interval pemindaian disesuaikan demi penyegaran visual optimal
    // EN: Periodic interval fine-tuned for smooth refresh visual quality
    uint64_t refreshIntervalUs = (_panelType == PANEL_HUB08) ? 100 : 1000;
    esp_timer_start_periodic(timerHandle, refreshIntervalUs);
}

//----------------------------------------------------------------------------------------------
void FansElectronics_DMD32::update()
{
    if (tickOccured)
    {
        tickOccured = false;
        refresh();
    }
}

//----------------------------------------------------------------------------------------------
void FansElectronics_DMD32::refresh()
{
    if (!displayfb)
        return;

    // ID: Membuka kunci transaksi SPI prioritas tinggi
    // EN: Opening exclusive high-priority hardware SPI transaction
    dmdSPI->beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));

    if (_panelType == PANEL_HUB08)
    {
        // ====================================================================
        // ID: BLOK PEMINDAIAN REGISTRI UNTUK TIPE PANEL HUB08 (SCAN 1/16)
        // EN: REGISTER SCANNING ROUTINE FOR HUB08 PANEL CONFIGURATIONS (1/16 SCAN)
        // ====================================================================
        int stride16 = _stride * 16;
        volatile uint8_t *data0;

        for (int y = 0; y < _height; y += 32)
        {
            data0 = displayfb + _stride * (y + phase);
            for (int x = _stride; x > 0; --x)
            {
                // ID: Gerbang logika NOT (~) membalik bit agar piksel aktif bernilai menyala
                // EN: Bitwise NOT gate (~) inverts framebuffer values for true active-low lighting
                dmdSPI->transfer(~*data0++);
            }
            data0 = data0 + stride16;
        }

        dmdSPI->endTransaction();

        // ID: Latch Data HUB08 (Kunci data ke register shift panel)
        // EN: HUB08 Data Latching (Latches registers to external shift chips)
        digitalWrite(_pins.pin_oe, HIGH); // ID: Cegah efek bayangan | EN: Avoid ghosting artifacts

        digitalWrite(_pins.pin_latch, HIGH);
        digitalWrite(_pins.pin_latch, LOW);

        // ID: Demultiplexing jalur pin alamat baris fisik (16 fase siklus)
        // EN: Demultiplexing physical row addressing lines (16 phase steps)
        digitalWrite(_pins.pin_A, (phase & 0x01) ? HIGH : LOW);
        digitalWrite(_pins.pin_B, (phase & 0x02) ? HIGH : LOW);
        if (_pins.pin_C != -1)
            digitalWrite(_pins.pin_C, (phase & 0x04) ? HIGH : LOW);
        if (_pins.pin_D != -1)
            digitalWrite(_pins.pin_D, (phase & 0x08) ? HIGH : LOW);

        // ID: Pemicuan emisi cahaya dengan menyuntikkan pulsa PWM
        // EN: Triggering visual panel emissions with accurate PWM pulse widths
        WRITE_PWM(_pins.pin_oe, 0, 255 - brightenss);

        // ID: Menggeser indeks fase pemindaian baris matrix
        // EN: Moving forward to the next multiplexed matrix scan index
        phase = (phase + 1) & 0x0F;
    }
    else
    {
        // ====================================================================
        // ID: BLOK PEMINDAIAN ZIGZAG ORIGINAL UNTUK PANEL HUB12 (SCAN 1/4)
        // EN: ORIGINAL ZIGZAG ROUTINE FOR HUB12 MATRICES (1/4 STRUCTURAL SCAN)
        // ====================================================================
        int stride4 = _stride * 4;
        volatile uint8_t *data0;
        volatile uint8_t *data1;
        volatile uint8_t *data2;
        volatile uint8_t *data3;
        bool flipRow = ((_height & 0x10) == 0);

        digitalWrite(_pins.pin_oe, HIGH); // ID: Blanking sementara | EN: Immediate display blanking

        for (byte y = 0; y < _height; y += 16)
        {
            if (!flipRow)
            {
                data0 = displayfb + _stride * (y + phase);
                data1 = data0 + stride4;
                data2 = data1 + stride4;
                data3 = data2 + stride4;
                for (int x = _stride; x > 0; --x)
                {
                    dmdSPI->transfer(*data3++);
                    dmdSPI->transfer(*data2++);
                    dmdSPI->transfer(*data1++);
                    dmdSPI->transfer(*data0++);
                }
                flipRow = true;
            }
            else
            {
                data0 = displayfb + _stride * (y + 16 - phase) - 1;
                data1 = data0 - stride4;
                data2 = data1 - stride4;
                data3 = data2 - stride4;
                for (int x = _stride; x > 0; --x)
                {
                    dmdSPI->transfer(pgm_read_byte(&(flipBits[*data3--])));
                    dmdSPI->transfer(pgm_read_byte(&(flipBits[*data2--])));
                    dmdSPI->transfer(pgm_read_byte(&(flipBits[*data1--])));
                    dmdSPI->transfer(pgm_read_byte(&(flipBits[*data0--])));
                }
                flipRow = false;
            }
        }

        dmdSPI->endTransaction();

        // ID: Kunci pulsa latch baris baru HUB12
        // EN: Fast latch pulse generation for HUB12 row locks
        digitalWrite(_pins.pin_latch, HIGH);
        digitalWrite(_pins.pin_latch, LOW);

        // ID: Pengalamatan 2 baris scan dual-channel (Pin A dan B)
        // EN: Twin-channel scan address line manipulation (Pin A & B)
        digitalWrite(_pins.pin_A, (phase & 0x01) ? HIGH : LOW);
        digitalWrite(_pins.pin_B, (phase & 0x02) ? HIGH : LOW);

        // ID: Nyalakan emisi cahaya untuk interupsi saat ini
        // EN: Enable display emission for the current scan cycle
        WRITE_PWM(_pins.pin_oe, 0, brightenss);
        phase = (phase + 1) & 0x03; // ID: Pembagi fase scan HUB12 | EN: Masking phase for HUB12 scans
    }
}
//----------------------------------------------------------------------------------------------
void FansElectronics_DMD32::setBrightness(uint8_t b)
{
    brightenss = b;
}

//----------------------------------------------------------------------------------------------
FansElectronics_DMD32::Color FansElectronics_DMD32::fromRGB(uint8_t r, uint8_t g, uint8_t b)
{
    return (r || g || b) ? White : Black;
}

//----------------------------------------------------------------------------------------------
void FansElectronics_DMD32::setDoubleBuffer(bool doubleBuffer)
{
    if (doubleBuffer != _doubleBuffer)
    {
        _doubleBuffer = doubleBuffer;
        if (doubleBuffer)
        {
            // ID: Alokasi framebuffer bayangan sekunder di RAM internal
            // EN: Allocation of background rendering framebuffer in RAM
            unsigned int size = _stride * _height;
            fb1 = (uint8_t *)malloc(size);
            if (fb1)
            {
                memset(fb1, 0xFF, size);
                noInterrupts(); // ID: Matikan interupsi saat menukar memori pointer | EN: Guard critical pointer swapping
                fb = fb1;
                displayfb = fb0;
                interrupts();
            }
            else
            {
                _doubleBuffer = false;
            }
        }
        else if (fb1)
        {
            noInterrupts();
            fb = fb0;
            displayfb = fb0;
            interrupts();
            free(fb1);
            fb1 = 0;
        }
    }
}

//----------------------------------------------------------------------------------------------
void FansElectronics_DMD32::swapBuffers()
{
    // ID: Tukar pointer memori latar belakang ke memori aktif secara instan (Anti-Kedip)
    // EN: Seamless swapping of background frame buffers to avoid visual flickers
    if (_doubleBuffer)
    {
        noInterrupts();
        if (fb == fb0)
        {
            fb = fb1;
            displayfb = fb0;
        }
        else
        {
            fb = fb0;
            displayfb = fb1;
        }
        interrupts();
    }
}

//----------------------------------------------------------------------------------------------
void FansElectronics_DMD32::swapBuffersAndCopy()
{
    swapBuffers();
    if (_doubleBuffer)
        memcpy((void *)fb, (void *)displayfb, _stride * _height);
}
//----------------------------------------------------------------------------------------------
