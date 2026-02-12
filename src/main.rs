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
    let _ = writeln!(uart, "Hello, world! Welcome to moto on ESP32-C5 (RISC-V)");

    // Nothing left to do — sleep forever.
    loop {
        riscv::asm::wfi();
    }
}
