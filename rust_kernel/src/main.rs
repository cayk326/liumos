// src/main.rs

#![no_std] // don't link the Rust standard library
#![no_main] // disable all Rust-level entry points
#![feature(asm)]

use core::convert::TryInto;
use core::fmt;
use core::panic::PanicInfo;

/// This function is called on panic.
#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}

// https://doc.rust-lang.org/beta/unstable-book/library-features/asm.html

fn write_io_port(port: u16, data: u8) {
    unsafe {
        asm!("out dx, al",
        in("al") data,
        in("dx") port)
    }
}
fn read_io_port(port: u16) -> u8 {
    let mut data: u8;
    unsafe {
        asm!("in al, dx",
        out("al") data,
        in("dx") port)
    }
    return data;
}

// const IO_ADDR_COM1: u16 = 0x3f8;
const IO_ADDR_COM2: u16 = 0x2f8;
fn com_initialize(base_io_addr: u16) {
    write_io_port(base_io_addr + 1, 0x00); // Disable all interrupts
    write_io_port(base_io_addr + 3, 0x80); // Enable DLAB (set baud rate divisor)
    const BAUD_DIVISOR: u16 = 0x0001; // baud rate = (115200 / BAUD_DIVISOR)
    write_io_port(base_io_addr + 0, (BAUD_DIVISOR & 0xff).try_into().unwrap());
    write_io_port(base_io_addr + 1, (BAUD_DIVISOR >> 8).try_into().unwrap());
    write_io_port(base_io_addr + 3, 0x03); // 8 bits, no parity, one stop bit
    write_io_port(base_io_addr + 2, 0xC7); // Enable FIFO, clear them, with 14-byte threshold
    write_io_port(base_io_addr + 4, 0x0B); // IRQs enabled, RTS/DSR set
}

fn com_send_char(base_io_addr: u16, c: char) {
    while (read_io_port(base_io_addr + 5) & 0x20) == 0 {
        unsafe { asm!("pause") }
    }
    write_io_port(base_io_addr, c as u8)
}

fn com_send_str(base_io_addr: u16, s: &str) {
    let mut sc = s.chars();
    let slen = s.chars().count();
    for _ in 0..slen {
        com_send_char(base_io_addr, sc.next().unwrap());
    }
}

pub struct Writer {}

impl Writer {
    pub fn write_str(&mut self, s: &str) {
        com_send_str(IO_ADDR_COM2, s);
    }
}

impl fmt::Write for Writer {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        com_send_str(IO_ADDR_COM2, s);
        Ok(())
    }
}

#[no_mangle]
pub extern "C" fn _start() -> ! {
    use core::fmt::Write;
    com_initialize(IO_ADDR_COM2);
    let mut writer = Writer {};
    let mut n = 0;
    loop {
        write!(writer, "liumOS Rust kernel! n = {}\n\r", n).unwrap();
        n = n + 1;
    }
}
