/*
 * This file is part of the Micro Python project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, 2014 Damien P. George
 * Copyright (c) 2015 Daniel Campora
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdint.h>

#include "py/mpconfig.h"
#include MICROPY_HAL_H
#include "py/obj.h"
#include "py/runtime.h"
#include "py/gc.h"
#include "inc/hw_memmap.h"
#include "inc/hw_types.h"
#include "inc/hw_ints.h"
#include "inc/hw_memmap.h"
#include "rom_map.h"
#include "pin.h"
#include "prcm.h"
#include "interrupt.h"
#include "pybuart.h"
#include "pybpin.h"
#include "pybrtc.h"
#include "pyexec.h"
#include "gccollect.h"
#include "gchelper.h"
#include "readline.h"
#include "mperror.h"
#include "simplelink.h"
#include "modnetwork.h"
#include "modusocket.h"
#include "modwlan.h"
#include "serverstask.h"
#include "telnet.h"
#include "debug.h"
#include "ff.h"
#include "diskio.h"
#include "sflash_diskio.h"
#include "mpexception.h"
#include "random.h"
#include "pybi2c.h"
#include "pybsd.h"
#include "pins.h"
#include "pybsleep.h"
#include "pybtimer.h"
#include "mpcallback.h"
#include "cryptohash.h"

/******************************************************************************
 DECLARE PRIVATE CONSTANTS
 ******************************************************************************/

/******************************************************************************
 DECLARE PRIVATE FUNCTIONS
 ******************************************************************************/
STATIC void mptask_pre_init (void);
STATIC void mptask_init_sflash_filesystem (void);
STATIC void mptask_enter_ap_mode (void);
STATIC void mptask_create_main_py (void);

/******************************************************************************
 DECLARE PUBLIC DATA
 ******************************************************************************/
#ifdef DEBUG
OsiTaskHandle   svTaskHandle;
#endif

/******************************************************************************
 DECLARE PRIVATE DATA
 ******************************************************************************/
static FATFS *sflash_fatfs;

static const char fresh_main_py[] = "# main.py -- put your code here!\r\n";
static const char fresh_boot_py[] = "# boot.py -- run on boot-up\r\n"
                                    "# can run arbitrary Python, but best to keep it minimal\r\n";

/******************************************************************************
 DECLARE PUBLIC FUNCTIONS
 ******************************************************************************/

void TASK_Micropython (void *pvParameters) {
    // initialize the garbage collector with the top of our stack
    uint32_t sp = gc_helper_get_sp();
    gc_collect_init (sp);
    bool safeboot = false;
    FRESULT res;

    mptask_pre_init();

soft_reset:

    // GC init
    gc_init(&_boot, &_eheap);

    // MicroPython init
    mp_init();
    mp_obj_list_init(mp_sys_path, 0);
    mp_obj_list_init(mp_sys_argv, 0);
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR_)); // current dir (or base dir of the script)

    // execute all basic initializations
    mpexception_init0();
    mpcallback_init0();
    pybsleep_init0();
    mperror_init0();
    uart_init0();
    pin_init0();
    timer_init0();
    readline_init0();
    mod_network_init0();
#if MICROPY_HW_ENABLE_RNG
    rng_init0();
#endif

    // we are alive, so let the world know it
    mperror_enable_heartbeat();

#ifdef LAUNCHXL
    // configure the stdio uart pins with the correct alternate functions
    // param 3 ("mode") is DON'T CARE" for AFs others than GPIO
    pin_config ((pin_obj_t *)&pin_GPIO1, PIN_MODE_3, 0, PIN_TYPE_STD_PU, PIN_STRENGTH_2MA);
    pin_config ((pin_obj_t *)&pin_GPIO2, PIN_MODE_3, 0, PIN_TYPE_STD_PU, PIN_STRENGTH_2MA);
    // instantiate the stdio uart
    mp_obj_t args[2] = {
            mp_obj_new_int(MICROPY_STDIO_UART),
            mp_obj_new_int(MICROPY_STDIO_UART_BAUD),
    };
    pyb_stdio_uart = pyb_uart_type.make_new((mp_obj_t)&pyb_uart_type, MP_ARRAY_SIZE(args), 0, args);
    // create a callback for the uart, in order to enable the rx interrupts
    uart_callback_new (pyb_stdio_uart, mp_const_none, MICROPY_STDIO_UART_RX_BUF_SIZE, INT_PRIORITY_LVL_3);
#else
    pyb_stdio_uart = MP_OBJ_NULL;
#endif

    pybsleep_reset_cause_t rstcause = pybsleep_get_reset_cause();
    if (rstcause < PYB_SLP_SOFT_RESET) {
        if (rstcause == PYB_SLP_HIB_RESET) {
            // when waking up from hibernate we just want
            // to enable simplelink and leave it as is
            wlan_first_start();
        }
        else {
            // only if not comming out of hibernate or a soft reset
            mptask_enter_ap_mode();
        #ifndef DEBUG
            safeboot = PRCMIsSafeBootRequested();
        #endif
        }

        // enable telnet and ftp
        servers_start();
    }

    // initialize the serial flash file system
    mptask_init_sflash_filesystem();

    // append the flash paths to the system path
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR__slash_flash));
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR__slash_flash_slash_lib));

    // reset config variables; they should be set by boot.py
    MP_STATE_PORT(pyb_config_main) = MP_OBJ_NULL;

    if (!safeboot) {
        // run boot.py, if it exists
        const char *boot_py = "boot.py";
        res = f_stat(boot_py, NULL);
        if (res == FR_OK) {
            int ret = pyexec_file(boot_py);
            if (ret & PYEXEC_FORCED_EXIT) {
                goto soft_reset_exit;
            }
            if (!ret) {
                // flash the system led
                mperror_signal_error();
            }
        }
    }

    // now we initialise sub-systems that need configuration from boot.py,
    // or whose initialisation can be safely deferred until after running
    // boot.py.

    // at this point everything is fully configured and initialised.

    if (!safeboot) {
        // run the main script from the current directory.
        if (pyexec_mode_kind == PYEXEC_MODE_FRIENDLY_REPL) {
            const char *main_py;
            if (MP_STATE_PORT(pyb_config_main) == MP_OBJ_NULL) {
                main_py = "main.py";
            } else {
                main_py = mp_obj_str_get_str(MP_STATE_PORT(pyb_config_main));
            }
            res = f_stat(main_py, NULL);
            if (res == FR_OK) {
                int ret = pyexec_file(main_py);
                if (ret & PYEXEC_FORCED_EXIT) {
                    goto soft_reset_exit;
                }
                if (!ret) {
                    // flash the system led
                    mperror_signal_error();
                }
            }
        }
    }

    // main script is finished, so now go into REPL mode.
    // the REPL mode can change, or it can request a soft reset.
    for ( ; ; ) {
        if (pyexec_mode_kind == PYEXEC_MODE_RAW_REPL) {
            if (pyexec_raw_repl() != 0) {
                break;
            }
        } else {
            if (pyexec_friendly_repl() != 0) {
                break;
            }
        }
    }

soft_reset_exit:

    // soft reset
    pybsleep_signal_soft_reset();
    mp_printf(&mp_plat_print, "PYB: soft reboot\n");

    // disable all peripherals that could trigger a callback
    pyb_rtc_callback_disable(NULL);
    timer_disable_all();
    uart_disable_all();

    // flush the serial flash buffer
    sflash_disk_flush();

    // clean-up the user socket space
    modusocket_close_all_user_sockets();

#if MICROPY_HW_HAS_SDCARD
    pybsd_deinit();
#endif

    // wait for pending transactions to complete
    HAL_Delay(20);

    goto soft_reset;
}

/******************************************************************************
 DEFINE PRIVATE FUNCTIONS
 ******************************************************************************/
__attribute__ ((section (".boot")))
STATIC void mptask_pre_init (void) {
#if MICROPY_HW_ENABLE_RTC
    pybrtc_init();
#endif

    // Create the simple link spawn task
    ASSERT (OSI_OK == VStartSimpleLinkSpawnTask(SIMPLELINK_SPAWN_TASK_PRIORITY));

    // Allocate memory for the flash file system
    ASSERT ((sflash_fatfs = mem_Malloc(sizeof(FATFS))) != NULL);

    // this one allocates memory for the nvic vault
    pybsleep_pre_init();

    // this one allocates memory for the WLAN semaphore
    wlan_pre_init();

    // this one allocates memory for the Socket semaphore
    modusocket_pre_init();

#if MICROPY_HW_HAS_SDCARD
    pybsd_init0();
#endif

    CRYPTOHASH_Init();

#ifdef DEBUG
    ASSERT (OSI_OK == osi_TaskCreate(TASK_Servers,
                                     (const signed char *)"Servers",
                                     SERVERS_STACK_SIZE, NULL, SERVERS_PRIORITY, &svTaskHandle));
#else
    ASSERT (OSI_OK == osi_TaskCreate(TASK_Servers,
                                     (const signed char *)"Servers",
                                     SERVERS_STACK_SIZE, NULL, SERVERS_PRIORITY, NULL));
#endif
}

STATIC void mptask_init_sflash_filesystem (void) {
    FILINFO fno;
#if _USE_LFN
    fno.lfname = NULL;
    fno.lfsize = 0;
#endif

    // Initialise the local flash filesystem.
    // Create it if needed, and mount in on /flash.
    // try to mount the flash
    FRESULT res = f_mount(sflash_fatfs, "/flash", 1);
    if (res == FR_NO_FILESYSTEM) {
        // no filesystem, so create a fresh one
        res = f_mkfs("/flash", 1, 0);
        if (res == FR_OK) {
            // success creating fresh LFS
        } else {
            __fatal_error("failed to create /flash");
        }
        // create empty main.py
        mptask_create_main_py();
    } else if (res == FR_OK) {
        // mount sucessful
        if (FR_OK != f_stat("/flash/main.py", &fno)) {
            // create empty main.py
            mptask_create_main_py();
        }
    } else {
        __fatal_error("failed to create /flash");
    }

    // The current directory is used as the boot up directory.
    // It is set to the internal flash filesystem by default.
    f_chdrive("/flash");

    // Make sure we have a /flash/boot.py.  Create it if needed.
    res = f_stat("/flash/boot.py", &fno);
    if (res == FR_OK) {
        if (fno.fattrib & AM_DIR) {
            // exists as a directory
            // TODO handle this case
            // see http://elm-chan.org/fsw/ff/img/app2.c for a "rm -rf" implementation
        } else {
            // exists as a file, good!
        }
    } else {
        // doesn't exist, create fresh file
        FIL fp;
        f_open(&fp, "/flash/boot.py", FA_WRITE | FA_CREATE_ALWAYS);
        UINT n;
        f_write(&fp, fresh_boot_py, sizeof(fresh_boot_py) - 1 /* don't count null terminator */, &n);
        // TODO check we could write n bytes
        f_close(&fp);
    }
}

STATIC void mptask_enter_ap_mode (void) {
    // enable simplelink in low power mode
    wlan_sl_enable (ROLE_AP, MICROPY_PORT_WLAN_AP_SSID, strlen(MICROPY_PORT_WLAN_AP_SSID), MICROPY_PORT_WLAN_AP_SECURITY,
                    MICROPY_PORT_WLAN_AP_KEY, strlen(MICROPY_PORT_WLAN_AP_KEY), MICROPY_PORT_WLAN_AP_CHANNEL);
}

STATIC void mptask_create_main_py (void) {
    // create empty main.py
    FIL fp;
    f_open(&fp, "/flash/main.py", FA_WRITE | FA_CREATE_ALWAYS);
    UINT n;
    f_write(&fp, fresh_main_py, sizeof(fresh_main_py) - 1 /* don't count null terminator */, &n);
    f_close(&fp);
}

STATIC mp_obj_t pyb_main(mp_obj_t main) {
    if (MP_OBJ_IS_STR(main)) {
        MP_STATE_PORT(pyb_config_main) = main;
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(pyb_main_obj, pyb_main);
