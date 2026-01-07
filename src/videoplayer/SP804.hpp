#ifndef SP804_HPP
#define SP804_HPP

#include <stdint.h>

// Delay function to wait for timer register writes to take effect
// At 32.768kHz (slowest timer), 1 TIMCLK = ~30.5 microseconds
constexpr uint32_t CPU_Freq_hz = 396000000; // 396 MHz
template<uint32_t TimerFreq_hz>
inline void waitForTimerRegisterWrite() {
    for (volatile uint32_t i = 0; i < CPU_Freq_hz / TimerFreq_hz; i = i + 1 ) {
        // Wait
    }
}

/*
 * SP804 Dual Timer Module Documentation:
 * https://developer.arm.com/documentation/ddi0271/d/programmer-s-model/summary-of-registers?lang=en
*/
enum class SP804TimerMode {
    Periodic,
    FreeRunning
};
enum class SP804OneshotMode {
    OneShot,
    Wrapping,
};
enum class SP804TimerPrescale {
    Div1,
    Div16,
    Div256
};
enum class SP804TimerSize {
    Size16Bit,
    Size32Bit
};

struct CreateSP804TimerInfo {
    SP804TimerMode mode;
    SP804OneshotMode oneshotMode;
    SP804TimerPrescale prescale;
    SP804TimerSize size;
    bool interruptEnable;
    bool enableTimer;
};

struct SP804TimerState {
    uint32_t loadValue;       // The Load register (reload/period value)
    uint32_t currentValue;    // Current counter position
    CreateSP804TimerInfo config;
};

// only supports single timer for now
template<uintptr_t BaseAddress, uint32_t TimerFreq_hz>
class SP804Timer {
private:
    constexpr static uintptr_t Timer1LoadOffset       = 0x00;
    constexpr static uintptr_t Timer1ValueOffset      = 0x04;
    constexpr static uintptr_t Timer1ControlOffset    = 0x08;
    constexpr static uintptr_t Timer1IntClrOffset     = 0x0C;
    constexpr static uintptr_t Timer1RISOffset        = 0x10;
    constexpr static uintptr_t Timer1MISOffset        = 0x14;
    constexpr static uintptr_t Timer1BGLoadOffset     = 0x18;

    constexpr static uintptr_t TimerControl_WrapBit             = (1 << 0); // 0 = wrap, 1 = one-shot
    constexpr static uintptr_t TimerControl_TimerSizeBit        = (1 << 1); // 0 = 16-bit, 1 = 32-bit
    constexpr static uintptr_t TimerControl_PrescaleBits        = (3 << 2); // 00 = div1, 01 = div16, 10 = div256, 11 = undefined
    constexpr static uintptr_t TimerControl_InterruptEnabledBit = (1 << 5); // 0 = disable, 1 = enable 
    constexpr static uintptr_t TimerControl_ModeBit             = (1 << 6); // 0 = free-running, 1 = periodic
    constexpr static uintptr_t TimerControl_TimerEnableBit      = (1 << 7); // 0 = disable, 1 = enable
    
public:

    virtual ~SP804Timer() = default;

    SP804TimerState recordTimerState() {
        SP804TimerState state;
        state.loadValue = getLoadValue();           // Save the period/reload value
        state.currentValue = getCurrentValue32();   // Save current countdown position
        state.config = getConfiguration();
        return state;
    }
    void restoreTimerState(const SP804TimerState& state) {
        // stop timer first
        stop();
        clearIRQ();

        // Step 1: Set current counter position via Load register
        // (this sets both Load and Counter to currentValue)
        setLoadValue(state.currentValue);

        // Step 2: Set the actual reload/period via BGLoad register
        // (this sets both BGLoad and Load to loadValue, but does NOT touch Counter)
        setBackgroundLoadValue(state.loadValue);

        // Now: Counter = currentValue, Load = loadValue, BGLoad = loadValue

        // set configuration
        // dont start the timer when configuring
        CreateSP804TimerInfo config = state.config;
        bool enableTimer = config.enableTimer;
        config.enableTimer = false;
        configure(config);
        if(enableTimer) start();
    }

    void configure(const CreateSP804TimerInfo& info) {
        uint32_t controlReg = 0;

        // Set wrap/one-shot
        if (info.oneshotMode == SP804OneshotMode::OneShot) {
            controlReg |= TimerControl_WrapBit;
        }

        // Set mode
        if (info.mode == SP804TimerMode::Periodic) {
            controlReg |= TimerControl_ModeBit;
        }

        // Set prescale
        switch (info.prescale) {
            case SP804TimerPrescale::Div1:
                controlReg |= (0 << 2);
                break;
            case SP804TimerPrescale::Div16:
                controlReg |= (1 << 2);
                break;
            case SP804TimerPrescale::Div256:
                controlReg |= (2 << 2);
                break;
        }

        // Set size
        if (info.size == SP804TimerSize::Size32Bit) {
            controlReg |= TimerControl_TimerSizeBit;
        }

        // Set interrupt
        if (info.interruptEnable) {
            controlReg |= TimerControl_InterruptEnabledBit;
        }

        // Set enable
        if (info.enableTimer) {
            controlReg |= TimerControl_TimerEnableBit;
        }

        // Write to control register
        *((volatile uint32_t*)(BaseAddress + Timer1ControlOffset)) = controlReg;
        waitForTimerRegisterWrite<TimerFreq_hz>();
    }
    CreateSP804TimerInfo getConfiguration() {
        uint32_t controlReg = *((volatile uint32_t*)(BaseAddress + Timer1ControlOffset));
        CreateSP804TimerInfo info;

        // Get wrap/one-shot
        info.oneshotMode = (controlReg & TimerControl_WrapBit) ? SP804OneshotMode::OneShot : SP804OneshotMode::Wrapping;

        // Get mode
        info.mode = (controlReg & TimerControl_ModeBit) ? SP804TimerMode::Periodic : SP804TimerMode::FreeRunning;

        // Get prescale
        uint32_t prescaleBits = (controlReg & TimerControl_PrescaleBits) >> 2;
        switch (prescaleBits) {
            case 0:
                info.prescale = SP804TimerPrescale::Div1;
                break;
            case 1:
                info.prescale = SP804TimerPrescale::Div16;
                break;
            case 2:
                info.prescale = SP804TimerPrescale::Div256;
                break;
        }

        // Get size
        info.size = (controlReg & TimerControl_TimerSizeBit) ? SP804TimerSize::Size32Bit : SP804TimerSize::Size16Bit;

        // Get interrupt
        info.interruptEnable = (controlReg & TimerControl_InterruptEnabledBit) != 0;

        // Get enable
        info.enableTimer = (controlReg & TimerControl_TimerEnableBit) != 0;

        return info;
    }

    void start (){
        uint32_t controlReg = *((volatile uint32_t*)(BaseAddress + Timer1ControlOffset));
        controlReg |= TimerControl_TimerEnableBit;
        *((volatile uint32_t*)(BaseAddress + Timer1ControlOffset)) = controlReg;
        waitForTimerRegisterWrite<TimerFreq_hz>();
    }
    void stop() {
        uint32_t controlReg = *((volatile uint32_t*)(BaseAddress + Timer1ControlOffset));
        controlReg &= ~TimerControl_TimerEnableBit;
        *((volatile uint32_t*)(BaseAddress + Timer1ControlOffset)) = controlReg;
        waitForTimerRegisterWrite<TimerFreq_hz>();
    }
    uint32_t getIRQStatus() {
        return *((volatile uint32_t*)(BaseAddress + Timer1MISOffset));
    }
    void clearIRQ() {
        *((volatile uint32_t*)(BaseAddress + Timer1IntClrOffset)) = 1;
        waitForTimerRegisterWrite<TimerFreq_hz>();
    }

    void setLoadValue(uint32_t value) {
        *((volatile uint32_t*)(BaseAddress + Timer1LoadOffset)) = value;
        waitForTimerRegisterWrite<TimerFreq_hz>();
    }
    uint32_t getLoadValue() {
        return *((volatile uint32_t*)(BaseAddress + Timer1LoadOffset));
    }
    void setBackgroundLoadValue(uint32_t value) {
        *((volatile uint32_t*)(BaseAddress + Timer1BGLoadOffset)) = value;
        waitForTimerRegisterWrite<TimerFreq_hz> ();
    }
    uint32_t getBackgroundLoadValue() {
        return *((volatile uint32_t*)(BaseAddress + Timer1BGLoadOffset));
    }

    uint32_t getCurrentValue32() {
        return *((volatile uint32_t*)(BaseAddress + Timer1ValueOffset));
    }
    uint16_t getCurrentValue16() {
        return *((volatile uint16_t*)(BaseAddress + Timer1ValueOffset)) & 0xFFFF;
    }
};

enum class FastSP804TimerSpeed {
    MHZ_CPU_DIV_4, // 0
    MHZ_12, // 1
    Hz_32768 // 2
};

template<uintptr_t BaseAddress, uint32_t TimerFreq_hz>
class FastSP804Timer : public SP804Timer<BaseAddress, TimerFreq_hz> {
private:
    // https://hackspire.org/index.php?title=Timers
    constexpr static uintptr_t SpecialConfigurableSpeedRegister = 0x80;
public:

    void setSpeed(FastSP804TimerSpeed speed) {
        uint32_t speedValue = 0;
        switch (speed) {
            case FastSP804TimerSpeed::MHZ_CPU_DIV_4:
                speedValue = 0x0;
                break;
            case FastSP804TimerSpeed::MHZ_12:
                speedValue = 0x1;
                break;
            case FastSP804TimerSpeed::Hz_32768:
                speedValue = 0x2;
                break;
        }
        *((volatile uint32_t*)(BaseAddress + SpecialConfigurableSpeedRegister)) = speedValue;
        waitForTimerRegisterWrite<TimerFreq_hz>();
    }
    FastSP804TimerSpeed getSpeed() {
        uint32_t speedValue = *((volatile uint32_t*)(BaseAddress + SpecialConfigurableSpeedRegister));
        if (speedValue == 0x0) {
            return FastSP804TimerSpeed::MHZ_CPU_DIV_4;
        } else if (speedValue & 2) { // bit 1 has priority over 0
            return FastSP804TimerSpeed::Hz_32768;
        }
        return FastSP804TimerSpeed::MHZ_12;
    }
};

#endif // SP804_HPP