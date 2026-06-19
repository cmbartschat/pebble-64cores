#![no_main]
#![no_std]

extern crate alloc;

mod app;

use crate::app::run_cores;
use pebble_rust_2026 as _;

#[unsafe(no_mangle)]
fn main() -> i32 {
    run_cores();
    0
}
