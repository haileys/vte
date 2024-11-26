#pragma once

#include "glib-object.h"
#if !defined (__VTE_VTE_H_INSIDE__) && !defined (VTE_COMPILATION)
#error "Only <vte/vte.h> can be included directly."
#endif

#include <gio/gio.h>

#include "vteenums.h"
#include "vtemacros.h"
#include "vtepty.h"

G_BEGIN_DECLS

#define VTE_TYPE_FD vte_fd_get_type()
G_DECLARE_INTERFACE(VteFd, vte_fd, VTE, FD, GObject)

struct _VteFdInterface {
    GTypeInterface parent_iface;
};

#define VTE_TYPE_POSIX_FD vte_posix_fd_get_type()
G_DECLARE_FINAL_TYPE (VtePosixFd, vte_posix_fd, VTE, POSIX_FD, GObject);

_VTE_PUBLIC
VteFd *vte_posix_fd_new(int fd) _VTE_CXX_NOEXCEPT;

G_END_DECLS
