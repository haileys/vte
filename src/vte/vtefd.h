#pragma once

#include "glib-object.h"
#include "glib.h"
#if !defined (__VTE_VTE_H_INSIDE__) && !defined (VTE_COMPILATION)
#error "Only <vte/vte.h> can be included directly."
#endif

#include <gio/gio.h>

#include "vteenums.h"
#include "vtemacros.h"
#include "vtepty.h"

G_BEGIN_DECLS

struct VteWindowSize {
    unsigned short rows;
    unsigned short columns;
    unsigned short xpixels;
    unsigned short ypixels;
};

#define VTE_TYPE_FD vte_fd_get_type()
G_DECLARE_INTERFACE(VteFd, vte_fd, VTE, FD, GObject)

struct _VteFdInterface {
    GTypeInterface parent_iface;
    gboolean(*get_window_size)(VteFd *, struct VteWindowSize *, GError **);
    gboolean(*set_window_size)(VteFd *, const struct VteWindowSize *, GError **);
    gboolean(*set_utf8)(VteFd *, gboolean, GError **);
};

_VTE_PUBLIC
gboolean vte_fd_get_window_size(VteFd *, struct VteWindowSize *, GError **error) _VTE_CXX_NOEXCEPT;

_VTE_PUBLIC
gboolean vte_fd_set_window_size(VteFd *, const struct VteWindowSize *, GError **error) _VTE_CXX_NOEXCEPT;

_VTE_PUBLIC
gboolean vte_fd_set_utf8(VteFd *, gboolean utf8, GError **error) _VTE_CXX_NOEXCEPT;

#define VTE_TYPE_POSIX_FD vte_posix_fd_get_type()
G_DECLARE_FINAL_TYPE (VtePosixFd, vte_posix_fd, VTE, POSIX_FD, GObject);

_VTE_PUBLIC
VteFd *vte_posix_fd_new(int fd, GCancellable *cancellable, GError **error) _VTE_CXX_NOEXCEPT;

_VTE_PUBLIC
VteFd *vte_posix_fd_open(GCancellable *cancellable, GError **error) _VTE_CXX_NOEXCEPT;

_VTE_PUBLIC
int vte_posix_fd_get_fd(VtePosixFd *) _VTE_CXX_NOEXCEPT;

_VTE_PUBLIC
int vte_posix_fd_get_peer(VtePosixFd *, int flags, GError **error) _VTE_CXX_NOEXCEPT;

G_END_DECLS
