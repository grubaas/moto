#![no_std]
#![no_main]

use core::fmt::Write;
use riscv_rt::entry;

extern crate panic_halt;

// ---------------------------------------------------------------------------
// NS16550-compatible UART mapped at the ESP32-C5 UART0 peripheral address.
// Register offsets use 32-bit (4-byte) spacing as expected by Renode's
// NS16550 model.
// ---------------------------------------------------------------------------

/// Base address — matches UART0 on the ESP32-C5 peripheral bus.
const UART_BASE: usize = 0x6000_0000;

/// Transmit Holding Register (write a byte here to send).
const UART_THR: *mut u32 = UART_BASE as *mut u32;

/// Line Status Register.
const UART_LSR: *const u32 = (UART_BASE + 0x14) as *const u32;

/// Bit 5 of LSR — Transmit Holding Register Empty.
const LSR_THRE: u32 = 1 << 5;

struct Uart;

impl Uart {
    fn putc(&self, byte: u8) {
        unsafe {
            // Spin until the transmit holding register is empty.
            while core::ptr::read_volatile(UART_LSR) & LSR_THRE == 0 {}
            core::ptr::write_volatile(UART_THR, byte as u32);
        }
    }
}

impl core::fmt::Write for Uart {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        for byte in s.bytes() {
            self.putc(byte);
        }
        Ok(())
    }
}

// ---------------------------------------------------------------------------
// CLINT timer — used for PWM timing.
//
// The Core-Level Interruptor (CLINT) provides a 64-bit monotonic counter
// (`mtime`) running at the core frequency (160 MHz in our Renode model).
// On RV32 we must read it as two 32-bit halves with the standard
// high-low-high pattern to avoid tearing.
// ---------------------------------------------------------------------------

const CLINT_BASE: usize = 0x0200_0000;
const MTIME_LO: *const u32 = (CLINT_BASE + 0xBFF8) as *const u32;
const MTIME_HI: *const u32 = (CLINT_BASE + 0xBFFC) as *const u32;

/// Read the 64-bit `mtime` counter atomically on RV32.
fn mtime() -> u64 {
    unsafe {
        loop {
            let hi1 = core::ptr::read_volatile(MTIME_HI);
            let lo = core::ptr::read_volatile(MTIME_LO);
            let hi2 = core::ptr::read_volatile(MTIME_HI);
            if hi1 == hi2 {
                return ((hi1 as u64) << 32) | (lo as u64);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// GPIO — simple MMIO output register for the LED.
//
// On real ESP32-C5 silicon the GPIO_OUT register lives at 0x6009_1000.
// In Renode we back this address with a small MappedMemory block so that
// read/write accesses succeed without bus faults.
// ---------------------------------------------------------------------------

const GPIO_OUT: *mut u32 = 0x6009_1000 as *mut u32;

// ---------------------------------------------------------------------------
// PWM breathing parameters
//
// Software PWM: the main loop toggles the LED pin high/low within each
// PWM cycle based on the current duty ratio.  A triangle-wave envelope
// ramps duty from 0 → DUTY_STEPS → 0 to produce the breathing effect.
//
//   PWM frequency   = TIMER_FREQ / PWM_PERIOD  = 1 kHz  (1 ms cycle)
//   Duty steps      = 100  (1 % resolution)
//   Cycles per step = 2    (hold each step for 2 ms)
//   Half-breath     = 100 steps × 2 ms = 200 ms
//   Full breath     = 400 ms
// ---------------------------------------------------------------------------

/// CLINT timer frequency (Hz) — must match the `frequency` in esp32c5.repl.
const TIMER_FREQ: u64 = 160_000_000;

/// PWM carrier frequency (Hz).
const PWM_FREQ: u64 = 1_000;

/// Timer ticks per PWM cycle.
const PWM_PERIOD: u64 = TIMER_FREQ / PWM_FREQ;

/// Number of discrete duty levels (0 = fully off, DUTY_STEPS = fully on).
const DUTY_STEPS: u64 = 100;

/// How many PWM cycles to hold each duty level before stepping.
const CYCLES_PER_STEP: u64 = 2;

// ---------------------------------------------------------------------------

#[entry]
fn main() -> ! {
    let mut uart = Uart;
    let _ = writeln!(uart, "moto: breathing LED on ESP32-C5 (RISC-V)");
    let _ = writeln!(
        uart,
        "pwm: freq={}Hz steps={} cycles_per_step={}",
        PWM_FREQ, DUTY_STEPS, CYCLES_PER_STEP
    );

    let mut duty: u64 = 0;
    let mut rising = true;
    let mut cycle_count: u64 = 0;
    let mut cycle_start = mtime();
    let mut led_on = false;

    let _ = writeln!(uart, "breathe: duty=0 rising");

    loop {
        let now = mtime();
        let elapsed = now.wrapping_sub(cycle_start);

        // ---- End of PWM cycle: advance envelope --------------------------
        if elapsed >= PWM_PERIOD {
            cycle_start = now;
            cycle_count += 1;

            if cycle_count >= CYCLES_PER_STEP {
                cycle_count = 0;

                if rising {
                    duty += 1;
                    if duty >= DUTY_STEPS {
                        rising = false;
                        let _ = writeln!(uart, "breathe: peak duty={}", duty);
                    }
                } else {
                    duty -= 1;
                    if duty == 0 {
                        rising = true;
                        let _ = writeln!(uart, "breathe: trough duty={}", duty);
                    }
                }
            }

            continue; // re-evaluate with the new cycle_start
        }

        // ---- Within a PWM cycle: drive the LED pin -----------------------
        let on_ticks = (PWM_PERIOD * duty) / DUTY_STEPS;
        let should_be_on = elapsed < on_ticks;

        if should_be_on != led_on {
            led_on = should_be_on;
            unsafe {
                core::ptr::write_volatile(GPIO_OUT, u32::from(led_on));
            }
        }
    }
}
