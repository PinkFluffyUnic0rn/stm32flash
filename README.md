STM32 VFS
===========

Simple VFS for SMT32 MCU and FS for spi flash devices. FS using only
static allocation and requires minimum 8kB of RAM for it's operation.
Because of only 1-level indirection maximum allowed file size is limited
with approximately 16Mb. Every block has a CRC protection.

Structure
=========

 * `main.c` &mdash; a simple terminal program to demonstrate and test
vfs functions.
 * `uartterm.c` and `uartterm.h` &mdash; API and functions that
implement unix-like ternimal working through UART.
 * `driver.h` &mdash; common interface for drivers in this project.
 * `filesystem.c` and `filesystem.h` &mdash; common interface for
filesystems in this project.
 * `w25.c` and `w25.h` &mdash; driver that implement most basic function
of W25Q SPI flash memory.
 * `sfs.c` and `sfs.h` &mdash; A simple filesystem.
 * `rfs.c` and `rfs.h` &mdash; Filesystem that resides in RAM.
 * `call.c` and `call.h` &mdash; Implementation for system call not
related to VFS.

UART terminal
=============

UART terminal is used for testing and resembeles typical UNIX shell.

Driver commands
-------------------
 * `sd [dev]` -- set current device to `[dev]`
 * `rd [addr]` -- read data at address `[addr]`
 * `wd [addr] [str]` -- write string `[str]` into `[addr]`

Filesystem commands
-------------------
 * `f` -- format choosen device           
 * `i [struct] {[addr]}` -- dump filesystem `[struct]` (`sb`, `in`, `bm`) at `[addr]`
 * `c [sz]` -- create inode for data size of `[size]`
 * `d [addr]` -- delete inode with address `[addr]`
 * `s [addr] [data]` -- set data for inode with address `[addr]` to `[data]`
 * `g [addr]` -- get data from inode with address `[addr]`
 * `r [addr] [off] [sz]` -- read [sz] bytes from inode with address `[addr]` with offset of [off] bytes
 * `w [addr] [off] [data]` -- write data into inode with address `[addr]` with offset of [off] bytes

Virtual filesystem commands
---------------------------
 * `mount [dev] [target]` -- mount `[dev]` to `[target]`    
 * `format [target]` -- format device mounted at `[target]`
 * `umount [dev] [target]` -- unmount `[target]`
 * `mountlist` -- get list of mounted devices     
 * `open [path] [flags]` -- open file with `[path]`, if `[flags]` is 'c', create it
 * `read [fd] [sz]` -- read [sz] bytes from opened file with descriptor `[fd]`
 * `write [fd] [data]` -- write `[data]` into opened file with descriptor `[fd]`
 * `close [fd]` -- close opened file with descriptor `[fd]`
 * `mkdir [path]` -- create directory `[path]`
 * `rm [path]` -- delete file or directory `[path]`
 * `ls [path]` -- get list of file in directory `[path]`
 * `cd [path]` -- change current working directory to `[path]`

What's done
===========
 * Filesystem mounting/unmounting.
 * open/close and read/write calls in VFS.
 * Simple filesystem, bad version of ext with built-in checksums.
 * Naive implementations of malloc/free/realloc.
 * RAM filesystem and that malloc/free/realloc calls.
 * Common interfaces for filesystems and drivers.


What's not done
==============
 * Device files
 * Page cache
 * Unit tests
