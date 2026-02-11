#![no_std]
#![no_main]

use core::fmt::Write;
use cortex_m_rt::entry;
use panic_halt as _;

// ---------------------------------------------------------------------------
// NS16550-compatible UART mapped at the i.MX RT1062 LPUART1 address region.
// Register offsets use 32-bit (4-byte) spacing as expected by Renode's
// NS16550 model.
// ---------------------------------------------------------------------------

/// Base address — matches LPUART1 on the i.MX RT1062 and the Renode platform.
const UART_BASE: usize = 0x4018_4000;

/// Transmit Holding Register (write a byte here to send).
const UART_THR: *mut u32 = UART_BASE as *mut u32;

/// Line Status Register.
const UART_LSR: *const u32 = (UART_BASE + 0x14) as *const u32;

/// Bit 5 of LSR — Transmit Holding Register Empty.
const LSR_THRE: u32 = 1 << 5;

// ---------------------------------------------------------------------------

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

#[entry]
fn main() -> ! {
    let mut uart = Uart;
    let _ = writeln!(uart, "Hello, world! Welcome to moto on Teensy 4.1 (i.MX RT1062)");

    // Nothing left to do — sleep forever.
    loop {
        cortex_m::asm::wfi();
    }
}
