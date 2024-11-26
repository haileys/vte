#include "glib-object.h"
#include "vtefd.h"

G_DEFINE_INTERFACE(VteFd, vte_fd, G_TYPE_OBJECT)

static void
vte_fd_default_init (VteFdInterface *iface)
{
    //
}
