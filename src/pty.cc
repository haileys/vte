/*
 * Copyright (C) 2001,2002 Red Hat, Inc.
 * Copyright © 2009, 2010, 2019 Christian Persch
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * SECTION: vte-pty
 * @short_description: Functions for starting a new process on a new pseudo-terminal and for
 * manipulating pseudo-terminals
 *
 * The terminal widget uses these functions to start commands with new controlling
 * pseudo-terminals and to resize pseudo-terminals.
 */

#include "config.h"

#include "pty.hh"

#include "glib-object.h"
#include "libc-glue.hh"

#include <vte/vte.h>
#include "refptr.hh"
#include "vteptyinternal.hh"

#include <assert.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#if __has_include(<sys/syslimits.h>)
#include <sys/syslimits.h>
#endif
#include <signal.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#if __has_include(<util.h>)
#include <util.h>
#endif
#if __has_include(<pty.h>)
#include <pty.h>
#endif
#if defined(__sun) && __has_include(<stropts.h>)
#include <stropts.h>
#define HAVE_STROPTS_H
#endif
#ifdef __NetBSD__
#include <sys/param.h>
#include <sys/sysctl.h>
#endif
#include <glib.h>
#include <gio/gio.h>
#include "debug.h"

#include <glib/gi18n-lib.h>

#include "glib-glue.hh"

#include "vtedefines.hh"

#include "missing.hh"

namespace vte::base {

Pty*
Pty::ref() noexcept
{
        g_atomic_int_inc(&m_refcount);
        return this;
}

void
Pty::unref() noexcept
{
        if (g_atomic_int_dec_and_test(&m_refcount))
                delete this;
}

int
Pty::get_peer(GError **error, bool cloexec) const noexcept
{
        /* FIXME? else if (m_flags & VTE_PTY_NO_CTTTY)
         * No session and no controlling TTY wanted, do we need to lose our controlling TTY,
         * perhaps by open("/dev/tty") + ioctl(TIOCNOTTY) ?
         */
        auto const fd_flags = int{O_RDWR |
                                  ((m_flags & VTE_PTY_NO_CTTY) ? O_NOCTTY : 0) |
                                  (cloexec ? O_CLOEXEC : 0)};

        VteFd *fd = m_fd.get();
        g_return_val_if_fail(VTE_IS_POSIX_FD(fd), -1);
        return vte_posix_fd_get_peer(VTE_POSIX_FD(fd), fd_flags, error);
}

void
Pty::child_setup() const noexcept
{
        GError *error = NULL;

        /* Unblock all signals */
        sigset_t set;
        sigemptyset(&set);
        if (pthread_sigmask(SIG_SETMASK, &set, nullptr) == -1) {
                auto errsv = vte::libc::ErrnoSaver{};
                _vte_debug_print(VTE_DEBUG_PTY, "%s failed: %s\n",
                                 "pthread_sigmask", g_strerror(errsv));
                _exit(127);
        }

        /* Reset the handlers for all signals to their defaults.  The parent
         * (or one of the libraries it links to) may have changed one to be ignored.
         */
        for (int n = 1; n < NSIG; n++) {
                if (n == SIGSTOP || n == SIGKILL)
                        continue;

                signal(n, SIG_DFL);
        }

        if (!(m_flags & VTE_PTY_NO_SESSION)) {
                /* This starts a new session; we become its process-group leader,
                 * and lose our controlling TTY.
                 */
                _vte_debug_print (VTE_DEBUG_PTY, "Starting new session\n");
                if (setsid() == -1) {
                        auto errsv = vte::libc::ErrnoSaver{};
                        _vte_debug_print(VTE_DEBUG_PTY, "%s failed: %s\n",
                                         "setsid", g_strerror(errsv));
                        _exit(127);
                }
        }


        auto peer_fd = get_peer(&error);
        if (peer_fd == -1) {
                g_critical("Pty::child_setup: get_peer: %s", error->message);
                _exit(127);
        }

#ifdef TIOCSCTTY
        /* On linux, opening the PTY peer above already made it our controlling TTY (since
         * previously there was none, after the setsid() call). However, it appears that e.g.
         * on *BSD, that doesn't happen, so we need this explicit ioctl here.
         */
        if (!(m_flags & VTE_PTY_NO_CTTY)) {
                if (ioctl(peer_fd, TIOCSCTTY, peer_fd) != 0) {
                        auto errsv = vte::libc::ErrnoSaver{};
                        _vte_debug_print(VTE_DEBUG_PTY, "%s failed: %s\n",
                                         "ioctl(TIOCSCTTY)", g_strerror(errsv));
                        _exit(127);
                }
        }
#endif

	/* now setup child I/O through the tty */
	if (peer_fd != STDIN_FILENO) {
		if (dup2(peer_fd, STDIN_FILENO) != STDIN_FILENO){
			_exit (127);
		}
	}
	if (peer_fd != STDOUT_FILENO) {
		if (dup2(peer_fd, STDOUT_FILENO) != STDOUT_FILENO){
			_exit (127);
		}
	}
	if (peer_fd != STDERR_FILENO) {
		if (dup2(peer_fd, STDERR_FILENO) != STDERR_FILENO){
			_exit (127);
		}
	}

	/* If the peer FD has not been consumed above as one of the stdio descriptors,
         * need to close it now so that it doesn't leak to the child.
         */
	if (peer_fd != STDIN_FILENO  &&
            peer_fd != STDOUT_FILENO &&
            peer_fd != STDERR_FILENO) {
                close(peer_fd);
	}
}

/*
 * Pty::set_size:
 * @rows: the desired number of rows
 * @columns: the desired number of columns
 * @cell_height_px: the height of a cell in px, or 0 for undetermined
 * @cell_width_px: the width of a cell in px, or 0 for undetermined
 *
 * Attempts to resize the pseudo terminal's window size.  If successful, the
 * OS kernel will send #SIGWINCH to the child process group.
 *
 * Returns: %true on success, or %false on error with errno set
 */
bool
Pty::set_size(int rows,
              int columns,
              int cell_height_px,
              int cell_width_px) const noexcept
{
        GError *error = NULL;

        struct VteWindowSize size;
	memset(&size, 0, sizeof(size));
	size.rows = rows > 0 ? rows : 24;
	size.columns = columns > 0 ? columns : 80;
#if WITH_SIXEL
        size.ypixels = size.ws_row * cell_height_px;
        size.xpixels = size.ws_col * cell_width_px;
#endif
	_vte_debug_print(VTE_DEBUG_PTY,
			"Setting window size to (%d,%d).\n",
			columns, rows);

        if (!vte_fd_set_window_size(fd(), &size, &error)) {
                _vte_debug_print(VTE_DEBUG_PTY,
                                 "Failed to set window size: %s\n",
                                 error->message);
                g_object_unref(error);
                return false;
        }

        return true;
}

/*
 * Pty::get_size:
 * @rows: (out) (allow-none): a location to store the number of rows, or %NULL
 * @columns: (out) (allow-none): a location to store the number of columns, or %NULL
 *
 * Reads the pseudo terminal's window size.
 *
 * If getting the window size failed, @error will be set to a #GIOError.
 *
 * Returns: %true on success, or %false on error with errno set
 */
bool
Pty::get_size(int* rows,
              int* columns) const noexcept
{
        VteWindowSize size;
        GError *error;

        if (!vte_fd_get_window_size(fd(), &size, &error)) {
                _vte_debug_print(VTE_DEBUG_PTY,
                                 "Failed to get window size: %s\n",
                                 error->message);
                g_object_unref(error);
                return false;
        }

        if (columns != nullptr) {
                *columns = size.columns;
        }
        if (rows != nullptr) {
                *rows = size.rows;
        }
        _vte_debug_print(VTE_DEBUG_PTY,
                        "Size is (%d,%d).\n",
                        size.ws_col, size.ws_row);
        return true;
}

/*
 * Pty::set_utf8:
 * @utf8: whether or not the pty is in UTF-8 mode
 *
 * Tells the kernel whether the terminal is UTF-8 or not, in case it can make
 * use of the info.  Linux 2.6.5 or so defines IUTF8 to make the line
 * discipline do multibyte backspace correctly.
 *
 * Returns: %true on success, or %false on error with errno set
 */
bool
Pty::set_utf8(bool utf8) const noexcept
{
        GError *error;
        if (!vte_fd_set_utf8(fd(), utf8, &error)) {
                _vte_debug_print(VTE_DEBUG_PTY, "vte_fd_set_utf8 failed: %s",
                                 error->message);
                g_object_unref(error);
                return false;
        }

        return true;
}

Pty*
Pty::create(VtePtyFlags flags)
{
        VteFd *fd = vte_posix_fd_open(g_cancellable_get_current(), nullptr);
        if (!fd)
                return nullptr;

        return new Pty{vte::glib::take_ref(fd), flags};
}

Pty*
Pty::create_foreign(VteFd *fd,
                    VtePtyFlags flags)
{
        return new Pty{vte::glib::take_ref(fd), flags};
}

} // namespace vte::base
