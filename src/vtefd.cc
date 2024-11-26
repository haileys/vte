#include "config.h"

#include <cerrno>
#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>
#include <sys/ioctl.h>
#include <termios.h>

#include "libc-glue.hh"
#include "vtefd.h"
#include "debug.h"

G_DEFINE_INTERFACE(VteFd, vte_fd, G_TYPE_OBJECT)

static void
vte_fd_default_init (VteFdInterface *iface)
{
}

static void
set_not_supported_error(GError **error, const gchar* method)
{
        g_set_error_literal(error, G_IO_ERROR, g_io_error_from_errno(ENOTSUP), method);
}

#define DISPATCH_IFACE(self, method, ...) do { \
        VteFdInterface *iface = VTE_FD_GET_IFACE(self); \
        if (!iface->method) { \
                set_not_supported_error(error, #method); \
        } \
        return iface->method(self, __VA_ARGS__); \
} while (0)

gboolean
vte_fd_get_window_size(VteFd *self, struct VteWindowSize *size, GError **error) noexcept
{
        DISPATCH_IFACE(self, get_window_size, size, error);
}

gboolean
vte_fd_set_window_size(VteFd *self, const struct VteWindowSize *size, GError **error) noexcept
{
        DISPATCH_IFACE(self, set_window_size, size, error);
}

gboolean
vte_fd_set_utf8(VteFd *self, gboolean utf8, GError **error) noexcept
{
        DISPATCH_IFACE(self, set_utf8, utf8, error);
}

struct _VtePosixFd
{
        GObject parent_instance;
        int fd;
};

static void
set_io_error(GError **error, const gchar *message)
{
        g_set_error_literal(error, G_IO_ERROR, g_io_error_from_errno(errno), message);
}

static gboolean
vte_posix_fd_get_window_size(VteFd *self, VteWindowSize *size, GError **error) noexcept
{
        struct winsize ws {};

        if (ioctl(VTE_POSIX_FD(self)->fd, TIOCGWINSZ, &ws)) {
                set_io_error(error, "ioctl(TIOCGWINSZ)");
                return false;
        }

        size->rows = ws.ws_row;
        size->columns = ws.ws_col;
        size->xpixels = ws.ws_xpixel;
        size->ypixels = ws.ws_ypixel;
        return true;
}

static gboolean
vte_posix_fd_set_window_size(VteFd *self, const VteWindowSize *size, GError **error) noexcept
{
        struct winsize ws = {
                .ws_row = size->rows,
                .ws_col = size->columns,
                .ws_xpixel = size->xpixels,
                .ws_ypixel = size->ypixels,
        };

        if (ioctl(VTE_POSIX_FD(self)->fd, TIOCSWINSZ, &ws)) {
                set_io_error(error, "ioctl(TIOCSWINSZ)");
                return false;
        }

        return true;
}

static gboolean
vte_posix_fd_set_utf8(VteFd *self, gboolean utf8, GError **error) noexcept
{
        int fd = VTE_POSIX_FD(self)->fd;

#ifdef IUTF8
	struct termios tio;
        if (tcgetattr(fd, &tio) == -1) {
                set_io_error(error, "tcgetattr");
                return false;
        }

        auto saved_cflag = tio.c_iflag;
        if (utf8) {
                tio.c_iflag |= IUTF8;
        } else {
                tio.c_iflag &= ~IUTF8;
        }

        /* Only set the flag if it changes */
        if (saved_cflag != tio.c_iflag &&
            tcsetattr(fd, TCSANOW, &tio) == -1) {
                set_io_error(error, "tcsetattr");
                return false;
	}
#endif

        return true;
}

static void
vte_posix_fd_iface_init (VteFdInterface *iface)
{
        iface->get_window_size = vte_posix_fd_get_window_size;
        iface->set_window_size = vte_posix_fd_set_window_size;
        iface->set_utf8 = vte_posix_fd_set_utf8;
}

static int
fd_set_cpkt(int fd)
{
        auto ret = 0;
#if defined(TIOCPKT)
        /* tty_ioctl(4) -> every read() gives an extra byte at the beginning
         * notifying us of stop/start (^S/^Q) events. */
        int one = 1;
        ret = ioctl(fd, TIOCPKT, &one);
#elif defined(__sun) && defined(HAVE_STROPTS_H)
        if (isastream(fd) == 1 &&
            ioctl(fd, I_FIND, "pckt") == 0)
                ret = ioctl(fd, I_PUSH, "pckt");
#endif
        return ret;
}

static gboolean
vte_posix_fd_initable_init (GInitable *initable,
                            GCancellable *cancellable,
                            GError **error) noexcept
{
        VtePosixFd *self = VTE_POSIX_FD(initable);
        int fd = self->fd;

        if (grantpt(fd) != 0) {
                GIOErrorEnum code = g_io_error_from_errno(errno);
                g_set_error_literal(error, G_IO_ERROR, code, "grantpt");
                return false;
        }

        if (unlockpt(fd) != 0) {
                GIOErrorEnum code = g_io_error_from_errno(errno);
                g_set_error_literal(error, G_IO_ERROR, code, "unlockpt");
                return false;
        }

        if (vte::libc::fd_set_cloexec(fd) < 0) {
                GIOErrorEnum code = g_io_error_from_errno(errno);
                g_set_error_literal(error, G_IO_ERROR, code, "setting CLOEXEC");
                return false;
        }

        if (vte::libc::fd_set_nonblock(fd) < 0) {
                GIOErrorEnum code = g_io_error_from_errno(errno);
                g_set_error_literal(error, G_IO_ERROR, code, "setting O_NONBLOCK");
                return false;
        }

        if (fd_set_cpkt(fd) < 0) {
                GIOErrorEnum code = g_io_error_from_errno(errno);
                g_set_error_literal(error, G_IO_ERROR, code, "setting packet mode");
                return false;
        }

        return true;
}

static void
vte_posix_fd_initable_iface_init (GInitableIface *iface)
{
        iface->init = vte_posix_fd_initable_init;
}

G_DEFINE_TYPE_WITH_CODE (VtePosixFd, vte_posix_fd, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (VTE_TYPE_FD, vte_posix_fd_iface_init)
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, vte_posix_fd_initable_iface_init));

enum {
        PROP_0,
        PROP_FD,
};

static void
vte_posix_fd_init (VtePosixFd *self)
{
        self->fd = -1;
}

static void
vte_posix_fd_get_property (GObject    *object,
                           guint       property_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
        VtePosixFd *self = VTE_POSIX_FD(object);

        switch (property_id) {
        case PROP_FD:
                g_value_set_int(value, self->fd);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        }
}

static void
vte_posix_fd_set_property (GObject      *object,
                           guint         property_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
        VtePosixFd *self = VTE_POSIX_FD(object);

        switch (property_id) {
        case PROP_FD:
                self->fd = g_value_get_int(value);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        }
}

static void
vte_posix_fd_finalize (GObject *object)
{
        int fd = VTE_POSIX_FD(object)->fd;
        if (fd >= 0) {
                close(fd);
        }
}

static void
vte_posix_fd_class_init(VtePosixFdClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = vte_posix_fd_get_property;
        object_class->set_property = vte_posix_fd_set_property;
        object_class->finalize = vte_posix_fd_finalize;

        g_object_class_install_property
                (object_class,
                 PROP_FD,
                 g_param_spec_int ("fd", NULL, NULL,
                                   -1, G_MAXINT, -1,
                                   (GParamFlags) (G_PARAM_READWRITE |
                                                  G_PARAM_CONSTRUCT_ONLY |
                                                  G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY)));
}

_VTE_PUBLIC
VteFd *vte_posix_fd_open(GCancellable *cancellable, GError **error) noexcept
{
        int pty_fd = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);

#ifndef __linux__
        /* Other kernels may not support CLOEXEC or NONBLOCK above, so try to fall back */
        bool need_cloexec = false, need_nonblocking = false;

#ifdef __NetBSD__
        // NetBSD is a special case: prior to 9.99.101, posix_openpt() will not return
        // EINVAL for unknown/unsupported flags but instead silently ignore these flags
        // and just return a valid PTY but without the NONBLOCK | CLOEXEC flags set.
        // So we need to manually apply these flags there. See issue #2575.
        int mib[2], osrev;
        size_t len;

        mib[0] = CTL_KERN;
        mib[1] = KERN_OSREV;
        len = sizeof(osrev);
        sysctl(mib, 2, &osrev, &len, NULL, 0);
        if (osrev < 999010100) {
                need_cloexec = need_nonblocking = true;
                _vte_debug_print(VTE_DEBUG_PTY,
                                 "NetBSD < 9.99.101, forcing fallback "
                                 "for NONBLOCK and CLOEXEC.\n");
        }
#else

        if (pty_fd < 0 && errno == EINVAL) {
                /* Try without NONBLOCK and apply the flag afterward */
                need_nonblocking = true;
                pty_fd = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
                if (pty_fd < 0 && errno == EINVAL) {
                        /* Try without CLOEXEC and apply the flag afterwards */
                        need_cloexec = true;
                        pty_fd = posix_openpt(O_RDWR | O_NOCTTY);
                }
        }
#endif /* __NetBSD__ */
#endif /* !linux */

        if (pty_fd < 0) {
                set_io_error(error, "posix_openpt");
                return nullptr;
        }

#ifndef __linux__
        if (need_cloexec && vte::libc::fd_set_cloexec(pty_fd) < 0) {
                auto errsv = vte::libc::ErrnoSaver{};
                _vte_debug_print(VTE_DEBUG_PTY,
                                 "%s failed: %s",
                                 "Setting CLOEXEC flag", g_strerror(errsv));
                return {};
        }

        if (need_nonblocking && vte::libc::fd_set_nonblock(pty_fd) < 0) {
                auto errsv = vte::libc::ErrnoSaver{};
                _vte_debug_print(VTE_DEBUG_PTY,
                                 "%s failed: %s",
                                 "Setting NONBLOCK flag", g_strerror(errsv));
                return {};
        }
#endif /* !linux */

        return (VteFd *)g_initable_new(VTE_TYPE_POSIX_FD,
                                       cancellable,
                                       error,
                                       "fd", pty_fd,
                                       NULL);
}

VteFd *
vte_posix_fd_new (int fd, GCancellable *cancellable, GError **error) noexcept
{
        return (VteFd *)g_initable_new(VTE_TYPE_POSIX_FD,
                                       cancellable,
                                       error,
                                       "fd", fd,
                                       NULL);
}

int
vte_posix_fd_get_fd(VtePosixFd *self) noexcept
{
        return self->fd;
}

int
vte_posix_fd_get_peer(VtePosixFd *self, int flags, GError **error) noexcept
{
        if (self->fd < 0)
                return -1;


        /* Now open the PTY peer. Note that this also makes the PTY our controlling TTY. */
        auto peer_fd = vte::libc::FD{};

#ifdef __linux__
        peer_fd = ioctl(self->fd, TIOCGPTPEER, flags);
        /* Note: According to the kernel's own tests (tools/testing/selftests/filesystems/devpts_pts.c),
         * the error returned when the running kernel does not support this ioctl should be EINVAL.
         * However it appears that the actual error returned is ENOTTY. So we check for both of them.
         * See issue#182.
         */
        if (!peer_fd &&
            errno != EINVAL &&
            errno != ENOTTY) {
                g_set_error_literal(error,
                                    G_IO_ERROR,
                                    g_io_error_from_errno(errno),
                                    "ioctl(TIOCGPTPEER)");
                return -1;
        }

        /* Fall back to ptsname + open */
#endif

        if (!peer_fd) {
                auto const name = ptsname(self->fd);
                if (name == nullptr) {
                        g_set_error_literal(error,
                                        G_IO_ERROR,
                                        g_io_error_from_errno(errno),
                                        "ptsname");
                        return -1;
                }

                _vte_debug_print (VTE_DEBUG_PTY,
                                  "Setting up child pty: master FD = %d name = %s\n",
                                  self->fd, name);

                peer_fd = ::open(name, flags);
                if (!peer_fd) {
                        g_set_error_literal(error,
                                        G_IO_ERROR,
                                        g_io_error_from_errno(errno),
                                        "Opening PTY");
                        return -1;
                }
        }

        assert(bool(peer_fd));

#if defined(__sun) && defined(HAVE_STROPTS_H)
        /* See https://illumos.org/man/7i/streamio */
        if (isastream (peer_fd.get()) == 1) {
                /* https://illumos.org/man/7m/ptem */
                if ((ioctl(peer_fd.get(), I_FIND, "ptem") == 0) &&
                    (ioctl(peer_fd.get(), I_PUSH, "ptem") == -1)) {
                        return -1;
                }
                /* https://illumos.org/man/7m/ldterm */
                if ((ioctl(peer_fd.get(), I_FIND, "ldterm") == 0) &&
                    (ioctl(peer_fd.get(), I_PUSH, "ldterm") == -1)) {
                        return -1;
                }
                /* https://illumos.org/man/7m/ttcompat */
                if ((ioctl(peer_fd.get(), I_FIND, "ttcompat") == 0) &&
                    (ioctl(peer_fd.get(), I_PUSH, "ttcompat") == -1)) {
                        return -1;
                }
        }
#endif

        return peer_fd.release();
}
