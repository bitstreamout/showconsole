# showconsole

The `showconsole` package provides a suite of lightweight system console and
boot logging utilities, originally developed over the years for SUSE and
openSUSE Linux.

## Included Tools

* **`blogd`**: Does boot logging on `/dev/console`.

* **`blogctl`**: Control the boot logging daemon `blogd`.

* **`blogger`**: Writes messages to a running `blogd` process.

* **`showconsole`**: Determines the real TTY of standard input.

* **`setconsole`**: Redirect system console output to a TTY.

* **`isserial`**: Determines if the underlying TTY of standard input is
     a serial line.

## The `blogd` Boot Logging Daemon

Whereas `blogd` had been written for SysVinit based systems, this newer
version also supports systemd based systems.

The `blogd` together with the new tool `blogctl` can replace the well-known
plymouth but without any frame buffer and splash support. Therefore, `blogd`
may be used on workstations as well as on so-called big irons.

### Key Features & Purpose

* **Boot Message Multiplexing:** The `blogd` causes systemd to show its boot
    messages on the system console to collect them for repeating them on all
    devices used for the system console.

* **Persistent Logging:** It writes out a copy to `boot.log` and `boot.old`
    below `/var/log/`.

* **Advanced Password Handling:** Beside boot messages and logging them the
    `blogd` can handle password or passphrase requests send by password
     agents. For those requests the `blogd` do ask on all terminal lines
     used for the system console.

* **Smart Virtual Terminal (VT) Switching:** It features robust, dynamic
    Virtual Terminal management. If the system is running a graphical
    environment (X11/Wayland) that locks the active terminal, `blogd`
    smartly allocates a free text terminal, safely switches the display
    to prompt the user, and seamlessly switches back to the graphical
    session once complete.

* **Context-Aware Prompts:** If the user is already actively viewing a text
    console, the prompt is seamlessly integrated directly into the active
    text stream without unnecessary screen switching or visual disruption.
