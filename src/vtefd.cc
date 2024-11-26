#include "glib-object.h"
#include "vtefd.h"

G_DEFINE_INTERFACE(VteFd, vte_fd, G_TYPE_OBJECT)

static void
vte_fd_default_init (VteFdInterface *iface)
{
}

struct _VtePosixFd
{
    GObject parent_instance;
    int fd;
};

static void
vte_posix_fd_iface_init (VteFdInterface *iface)
{
}

G_DEFINE_TYPE_WITH_CODE (VtePosixFd, vte_posix_fd, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE(VTE_TYPE_FD, vte_posix_fd_iface_init));

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

VteFd *
vte_posix_fd_new (int fd) noexcept
{
        return (VteFd *) g_object_new (VTE_TYPE_POSIX_FD,
                                       "fd", fd,
                                       NULL);
}
