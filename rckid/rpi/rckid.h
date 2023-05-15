#pragma once

#include <queue>
#include <mutex>
#include <variant>

#include "libevdev/libevdev.h"
#include "libevdev/libevdev-uinput.h"

#include "platform/platform.h"
#include "platform/peripherals/nrf24l01.h"
#include "platform/peripherals/mpu6050.h"

#include "common/config.h"
#include "common/comms.h"
#include "events.h"

/** RCKid RPI Driver

                           3V3     5V
                I2C SDA -- 2       5V
                I2C SCL -- 3      GND
                NRF_IRQ -- 4*      14 -- NRF_RXTX / UART TX
                           GND     15 -- BTN_X / UART RX
                  BTN_B -- 17      18 -- BTN_A
            DISPLAY_RST -- 27*    GND
             DISPLAY_DC -- 22*    *23 -- BTN_Y (voldown)
                           3V3    *24 -- RPI_POWEROFF
    DISPLAY MOSI (SPI0) -- 10     GND
    DISPLAY MISO (SPI0) -- 9      *25 -- AVR_IRQ
    DISPLAY SCLK (SPI0) -- 11       8 -- DISPLAY CE (SPI0 CE0)
                           GND      7 -- BTN_L
                  BTN_R -- 0        1 -- BTN_RVOL (volup)
            ()   BTN_LVOL -- 5*     GND
             HEADPHONES -- 6*      12 -- AUDIO L
                AUDIO R -- 13     GND
              SPI1 MISO -- 19      16 -- SPI1 CE0
                BTN_JOY -- 26      20 -- SPI1 MOSI
                           GND     21 -- SPI1 SCLK

*/

#define PIN_AVR_IRQ RPI_PIN_AVR_IRQ
#define PIN_HEADPHONES 6
#define PIN_NRF_CS 16
#define PIN_NRF_RXTX 14
#define PIN_NRF_IRQ 4

#define PIN_BTN_A 18
#define PIN_BTN_B 17
#define PIN_BTN_X 15
#define PIN_BTN_Y 23
#define PIN_BTN_L 7
#define PIN_BTN_R 0
#define PIN_BTN_LVOL 5
#define PIN_BTN_RVOL 1
#define PIN_BTN_JOY 26


#define MAIN_THREAD
#define DRIVER_THREAD
#define ISR_THREAD

class Window; 

/** RCKid Driver
 
 */
class RCKid {
public:

    static constexpr unsigned int RETROARCH_PAUSE = KEY_P;
    static constexpr unsigned int RETROARCH_SAVE_STATE = KEY_F2;
    static constexpr unsigned int RETROARCH_LOAD_STATE = KEY_F4;
    static constexpr unsigned int RETROARCH_SCREENSHOT = KEY_F8;
    static constexpr unsigned int VLC_PAUSE = KEY_SPACE;
    static constexpr unsigned int VLC_BACK = KEY_LEFT;
    static constexpr unsigned int VLC_FORWARD = KEY_RIGHT;
    static constexpr unsigned int VLC_DELAY_10S = KEY_LEFTALT;
    static constexpr unsigned int VLC_DELAY_1M = KEY_LEFTCTRL;
    static constexpr unsigned int VLC_SCREENSHOT = KEY_S;
    static constexpr unsigned int VLC_SCREENSHOT_MOD = KEY_LEFTSHIFT;

    static constexpr char const * LIBEVDEV_GAMEPAD_NAME = "rckid-gamepad";
    static constexpr char const * LIBEVDEV_KEYBOARD_NAME = "rckid-keyboard";

    static constexpr uint8_t BTN_DEBOUNCE_DURATION = 2;

    static constexpr uint8_t BTN_AUTOREPEAT_DURATION = 20;

    /** Initializes the RCKid driver. 

        The initializer starts the hw loop and initializes the libevdev gamepad layer. 
     */
    RCKid(Window * window) MAIN_THREAD;

    ~RCKid() {
        libevdev_uinput_destroy(gamepad_);
        libevdev_free(gamepadDev_);
    }

    /** Enables or disables automatic sending of button & analog events to the virtual gamepad. 
     */
    void enableGamepad(bool enable) {
        hwEvents_.send(EnableGamepad{enable});
    }

    void keyPress(int key, bool state) {
        hwEvents_.send(KeyPress{key, state});
    }

    /** Returns the current audio volume. 
     */
    int volume() const { return status_.volume; }

    /** Sets the current audio volume
     */
    void setVolume(int value) {
        if (value < 0)
            value = 0;
        if (value > AUDIO_MAX_VOLUME)
            value = AUDIO_MAX_VOLUME;
        status_.volume = value;
        system(STR("amixer sset -q Headphone -M " << status_.volume << "%").c_str());
    }

    void setBrightness(uint8_t value) { hwEvents_.send(msg::SetBrightness{value}); }

    comms::Mode mode() const { return status_.mode; }
    bool usb() const { return status_.usb; }
    bool charging() const { return status_.charging; }
    uint16_t vBatt() const { return status_.vBatt; }
    uint16_t vcc() const { return status_.vcc; }
    int16_t avrTemp() const { return status_.avrTemp; }
    int16_t accelTemp() const { return status_.accelTemp; }
    bool headphones() const { return status_.headphones; }
    bool wifi() const { return status_.wifi; }
    bool wifiHotspot() const { return status_.wifiHotspot; }
    std::string const & ssid() const { return status_.ssid; }
    bool nrf() const { return status_.nrf; }

private:

    friend class Window;

    /** A digital button. 
     
        We keep the current state of the button as set by the ISR and the reported state of the button, which is the last state that has been reported to the driver and ui thread. 

        When the ISR of the button's pin is activated, the current state is set accordingly and if the button is not being debounced now (debounce counter is 0), the action is taken. This means that the debounce counter is reset to its max value (BTN_DEBOUNCE_PERIOD) and the libevdev device and main thread are notified of the change. The driver's thread monitors all buttons on every tick and decreases their debounce counters. When they reach zero, we check if the current state is different than reported and if so, initiate button action. This gives the best responsivity while still debouncing. 

        Autorepeat is also handled by the ticks in the driver's thread. 

        The debounce and autorepeat counters are atomically updated (aligned addresses & unsigned should be atomic on ARM) and their values and their handling ensure that the driver thread and ISR thread will not interfere. 

        The volume buttons and the thumbstick button are already debounced on the AVR to reduce the I2C talk to minimum. 
     */
    struct ButtonState {
        bool current = false ISR_THREAD; 
        bool reported = false ISR_THREAD DRIVER_THREAD;
        unsigned debounce = 0 ISR_THREAD DRIVER_THREAD;
        unsigned autorepeat = 0 ISR_THREAD DRIVER_THREAD;
        Button button;
        unsigned const evdevId;

        /** Value to be reported when the button is pressed. 0 means it's normal button, other numbers mean the button reports the specified value on an absolute axis (its evdevId). */
        int axisValue = 0;

        /** Creates new button, parametrized by the pin and its evdev id. 
         */
        ButtonState(Button button, unsigned evdevId): button{button}, evdevId{evdevId} { }
 
        /** Creates new button represented by an axis and value. 
         */
        ButtonState(Button button, unsigned evdevId, int axisValue): button{button}, evdevId{evdevId}, axisValue{axisValue} { }

    }; // RCKid::Button

    /** An analog axis.

        We use the analog axes for the thumbstick and accelerometer. The thumbstick is debounced on the AVR.   
     */
    struct AxisState {
        uint8_t current;
        unsigned const evdevId;

        AxisState(unsigned evdevId):evdevId{evdevId} { }
    }; // RCKid:Axis

    struct Tick {};
    struct SecondTick {};
    struct Irq { unsigned pin; };
    struct KeyPress{ int key; bool state; };
    struct EnableGamepad{ bool enable; };

    /** Event for the driver's main loop to react to. Events with specified numbers are changes on the specified pins.
    */
    using HWEvent = std::variant<
        Tick, 
        SecondTick,
        Irq, 
        KeyPress,
        EnableGamepad,
        msg::AvrReset, 
        msg::Info, 
        msg::StartAudioRecording, 
        msg::StopAudioRecording,
        msg::SetBrightness,
        msg::SetTime, 
        msg::SetAlarm,
        msg::RumblerOk, 
        msg::RumblerFail, 
        msg::Rumbler,
        msg::PowerOn,
        msg::PowerDown, 
        msg::EnterRepairMode,
        msg::LeaveRepairMode 
    >;

    static RCKid * & instance() {
        static RCKid * i;
        return i;
    }

    /** The UI loop, should be called by the window's loop function. Processes the events from the driver to the main thread and calls the specific event handlers in the window. */
    void loop() MAIN_THREAD;

    void processEvent(Event & e) MAIN_THREAD; 

    /** The HW loop, proceses events from the hw event queue. This method is executed in a separate thread, which isthe only thread that accesses the GPIOs and i2c/spi connections. 
     */
    void hwLoop() DRIVER_THREAD;

#if (defined ARCH_MOCK)
    void checkMockButtons() DRIVER_THREAD;    
#endif

    /** Queries the accelerometer status and updates the X and Y accel axes. 
     */
    void accelQueryStatus() DRIVER_THREAD;

    /** Requests the fast AVR status update and processes the result.
     */
    void avrQueryState() DRIVER_THREAD;

    /** Requests the full AVR state update and processes the result. 
     */
    void avrQueryExtendedState() DRIVER_THREAD;

    /** Processes the avr status. 
     */
    void processAvrStatus(comms::Status const & status) DRIVER_THREAD;

    /** Processes the controls information sent by the AVR (buttons, thumbstick).
     */
    void processAvrControls(comms::Controls const & controls) DRIVER_THREAD;

    void processAvrExtendedInfo(comms::ExtendedInfo const & einfo) DRIVER_THREAD;

    /** Initializes the ISRs on the rpi pins so that the driver can respond properly.
     */
    void initializeISRs() MAIN_THREAD;

    /** Initializes the libevdev gamepad device for other applications. 
     */
    void initializeLibevdevGamepad() MAIN_THREAD;

    /** Initializes the libevdev keyboard device for direct keyboard control. 
     */
    void initializeLibevdevKeyboard() MAIN_THREAD;

    void initializeAvr() MAIN_THREAD;

    void initializeAccel() MAIN_THREAD;

    void initializeNrf() MAIN_THREAD;

    /** Transmits the given command to the AVR. 
     */
    template<typename T>
    void sendAvrCommand(T const & cmd) DRIVER_THREAD {
        static_assert(std::is_base_of<msg::Message, T>::value, "only applicable for mesages");
        using namespace platform;
        i2c::transmit(AVR_I2C_ADDRESS, reinterpret_cast<uint8_t const *>(& cmd), sizeof(T), nullptr, 0);
    }

    static void isrAvrIrq() ISR_THREAD {
        RCKid * self = RCKid::instance();
        self->hwEvents_.send(Irq{PIN_AVR_IRQ});
    }

    static void isrNrfIrq() ISR_THREAD {
        RCKid * self = RCKid::instance();
        self->hwEvents_.send(Irq{PIN_NRF_IRQ});
    }

    static void isrHeadphones() {
        RCKid * self = RCKid::instance();
        self->events_.send(HeadphonesEvent{platform::gpio::read(PIN_HEADPHONES)});
    }

    static void isrButtonA() {
        using namespace platform;
        RCKid * i = RCKid::instance();
        i->buttonChange(gpio::read(PIN_BTN_A), i->btnA_);
    }

    static void isrButtonB() {
        using namespace platform;
        RCKid * i = RCKid::instance();
        i->buttonChange(gpio::read(PIN_BTN_B), i->btnB_);
    }

    static void isrButtonX() {
        using namespace platform;
        RCKid * i = RCKid::instance();
        i->buttonChange(gpio::read(PIN_BTN_X), i->btnX_);
    }

    static void isrButtonY() {
        using namespace platform;
        RCKid * i = RCKid::instance();
        i->buttonChange(gpio::read(PIN_BTN_Y), i->btnY_);
    }

    static void isrButtonL() {
        using namespace platform;
        RCKid * i = RCKid::instance();
        i->buttonChange(gpio::read(PIN_BTN_L), i->btnL_);
    }

    static void isrButtonR() {
        using namespace platform;
        RCKid * i = RCKid::instance();
        i->buttonChange(gpio::read(PIN_BTN_R), i->btnR_);
    }

    static void isrButtonLVol() {
        using namespace platform;
        RCKid * i = RCKid::instance();
        i->buttonChange(gpio::read(PIN_BTN_LVOL), i->btnVolDown_);
    }

    static void isrButtonRVol() {
        using namespace platform;
        RCKid * i = RCKid::instance();
        i->buttonChange(gpio::read(PIN_BTN_RVOL), i->btnVolUp_);
    }

    static void isrButtonJoy() {
        using namespace platform;
        RCKid * i = RCKid::instance();
        i->buttonChange(gpio::read(PIN_BTN_JOY), i->btnJoy_);
    }

    /** ISR for the hardware buttons and the headphones. 
     
        Together with the driver thread's ticks, the isr is responsibel for debouncing. Called by the ISR or driver thread depending on the button. 
     */
    void buttonChange(int level, ButtonState & btn) ISR_THREAD DRIVER_THREAD {
        // always set the state
        btn.current = ! level;
        // if debounce is 0, take action and set the debounce timer, otherwise do nothing
        if (btn.debounce == 0) {
            btn.debounce = BTN_DEBOUNCE_DURATION;
            buttonAction(btn);
        }
    }

    void buttonAction(ButtonState & btn) ISR_THREAD DRIVER_THREAD;

    void buttonTick(ButtonState & btn) DRIVER_THREAD {
        if (btn.debounce > 0 && --(btn.debounce) == 0)
            if (btn.reported != btn.current)
                buttonAction(btn);
        if (btn.reported && btn.autorepeat > 0 && --(btn.autorepeat) == 0)
            buttonAction(btn);
    }

    bool axisChange(uint8_t value, AxisState & axis) DRIVER_THREAD {
        if (axis.current != value) {
            axis.current = value;
            if (activeDevice_ != nullptr) {
                libevdev_uinput_write_event(activeDevice_, EV_ABS, axis.evdevId, value);
                libevdev_uinput_write_event(activeDevice_, EV_SYN, SYN_REPORT, 0);
            }
            return true;
        } else {
            return false;
        }
    }

    Window * window_;

    /** Hardware events sent to the driver's main loop. 
     */
    EventQueue<HWEvent> hwEvents_;

    /** EVents sent from the  ISR and comm threads to the main thread. 
     */
    EventQueue<Event> events_;

    /** RCKid's state, available from the main thead. 
     */
    struct Status {
        comms::Mode mode;
        bool usb;
        bool charging;
        uint16_t vBatt = 420;
        uint16_t vcc = 430;
        int16_t avrTemp;
        int16_t accelTemp;
        bool headphones;
        unsigned volume = 0;
        bool wifi = true;
        bool wifiHotspot = true;
        std::string ssid = "Internet 10";
        bool nrf = true;
    } status_;

    /** The button state objects, managed by the ISR thread 
     */
    ButtonState btnVolDown_{Button::VolumeDown, KEY_RESERVED};
    ButtonState btnVolUp_{Button::VolumeUp, KEY_RESERVED};
    ButtonState btnJoy_{Button::Joy, BTN_THUMBL};
    ButtonState btnA_{Button::A, BTN_EAST};
    ButtonState btnB_{Button::B, BTN_SOUTH}; 
    ButtonState btnX_{Button::X, BTN_NORTH};
    ButtonState btnY_{Button::Y, BTN_WEST};
    ButtonState btnL_{Button::L, BTN_TL};
    ButtonState btnR_{Button::R, BTN_TR};
    ButtonState btnSelect_{Button::Select, BTN_SELECT};
    ButtonState btnStart_{Button::Start, BTN_START};
    ButtonState btnHome_{Button::Home, BTN_MODE};
    ButtonState btnDpadUp_{Button::Up, ABS_HAT0Y, -1};
    ButtonState btnDpadDown_{Button::Down, ABS_HAT0Y, 1};
    ButtonState btnDpadLeft_{Button::Left, ABS_HAT0X, -1};
    ButtonState btnDpadRight_{Button::Right, ABS_HAT0X, 1};

    /** Axes. 
     */
    AxisState thumbX_{ABS_X};
    AxisState thumbY_{ABS_Y};
    AxisState accelX_{ABS_RX};
    AxisState accelY_{ABS_RY};

    /** Last known state so that we can determine when to send an update
     */
    ModeEvent mode_;
    ChargingEvent charging_;
    VoltageEvent voltage_;
    TempEvent temp_;

    platform::NRF24L01 radio_{PIN_NRF_CS, PIN_NRF_RXTX};
    platform::MPU6050 accel_;

    /** Libevdev gamepad. 
     */

    struct libevdev * gamepadDev_{nullptr};
    struct libevdev_uinput * gamepad_{nullptr};
    struct libevdev_uinput * activeDevice_{nullptr};

    /** Libevdev keyboard. 
     */

    struct libevdev * keyboardDev_{nullptr};
    struct libevdev_uinput * keyboard_{nullptr};

}; // RCKid



/*

Input driver version is 1.0.1
Input device ID: bus 0x3 vendor 0x46d product 0xc21d version 0x4014
Input device name: "Logitech Gamepad F310"
Supported events:
  Event type 0 (EV_SYN)
  Event type 1 (EV_KEY)
    Event code 304 (BTN_SOUTH)
    Event code 305 (BTN_EAST)
    Event code 307 (BTN_NORTH)
    Event code 308 (BTN_WEST)
    Event code 310 (BTN_TL)
    Event code 311 (BTN_TR)
    Event code 312 (BTN_TL2)
    Event code 313 (BTN_TR2)
    Event code 314 (BTN_SELECT)
    Event code 315 (BTN_START)
    Event code 316 (BTN_MODE)
    Event code 317 (BTN_THUMBL)
    Event code 318 (BTN_THUMBR)
  Event type 3 (EV_ABS)
    Event code 0 (ABS_X)
      Value    128
      Min   -32768
      Max    32767
      Flat     128
    Event code 1 (ABS_Y)
      Value   -129
      Min   -32768
      Max    32767
      Flat     128
    Event code 3 (ABS_RX)
      Value    128
      Min   -32768
      Max    32767
      Fuzz      16
      Flat     128
    Event code 4 (ABS_RY)
      Value   -129
      Min   -32768
      Max    32767
      Fuzz      16
      Flat     128
    Event code 16 (ABS_HAT0X)
      Value      0
      Min       -1
      Max        1
    Event code 17 (ABS_HAT0Y)
      Value      0
      Min       -1
      Max        1
  Event type 21 (EV_FF)
    Event code 80 (FF_RUMBLE)
    Event code 81 (FF_PERIODIC)
    Event code 88 (FF_SQUARE)
    Event code 89 (FF_TRIANGLE)
    Event code 90 (FF_SINE)
    Event code 96 (FF_GAIN)
Properties:


*/