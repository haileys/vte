/*
 * Copyright © 2017, 2018 Christian Persch
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <string.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <glib.h>

#include "parser.hh"
#include "parser-charset-tables.hh"

static struct vte_parser* parser;

#if 0
static char const*
seq_to_str(unsigned int type)
{
        switch (type) {
        case VTE_SEQ_NONE: return "NONE";
        case VTE_SEQ_IGNORE: return "IGNORE";
        case VTE_SEQ_GRAPHIC: return "GRAPHIC";
        case VTE_SEQ_CONTROL: return "CONTROL";
        case VTE_SEQ_ESCAPE: return "ESCAPE";
        case VTE_SEQ_CSI: return "CSI";
        case VTE_SEQ_DCS: return "DCS";
        case VTE_SEQ_OSC: return "OSC";
        default:
                g_assert_not_reached();
        }
}

static char const*
cmd_to_str(unsigned int command)
{
        switch (command) {
#define _VTE_CMD(cmd) case VTE_CMD_##cmd: return #cmd;
#include "parser-cmd.hh"
#undef _VTE_CMD
        default:
                static char buf[32];
                snprintf(buf, sizeof(buf), "UNKOWN(%u)", command);
                return buf;
        }
}

static char const*
charset_to_str(unsigned int cs)
{
        switch (cs) {
#define _VTE_CHARSET_PASTE(name) case VTE_CHARSET_##name: return #name;
#define _VTE_CHARSET(name) _VTE_CHARSET_PASTE(name)
#define _VTE_CHARSET_ALIAS_PASTE(name1,name2)
#define _VTE_CHARSET_ALIAS(name1,name2)
#include "parser-charset.hh"
#undef _VTE_CHARSET_PASTE
#undef _VTE_CHARSET
#undef _VTE_CHARSET_ALIAS_PASTE
#undef _VTE_CHARSET_ALIAS
        default:
                static char buf[32];
                snprintf(buf, sizeof(buf), "UNKOWN(%u)", cs);
                return buf;
        }
}
#endif

static const char c0str[][6] = {
        "NUL", "SOH", "STX", "ETX", "EOT", "ENQ", "ACK", "BEL",
        "BS", "HT", "LF", "VT", "FF", "CR", "SO", "SI",
        "DLE", "DC1", "DC2", "DC3", "DC4", "NAK", "SYN", "ETB",
        "CAN", "EM", "SUB", "ESC", "FS", "GS", "RS", "US",
        "SPACE"
};

static const char c1str[][5] = {
        "DEL",
        "0x80", "0x81", "BPH", "NBH", "0x84", "NEL", "SSA", "ESA",
        "HTS", "HTJ", "VTS", "PLD", "PLU", "RI", "SS2", "SS3",
        "DCS", "PU1", "PU2", "STS", "CCH", "MW", "SPA", "EPA",
        "SOS", "0x99", "SCI", "CSI", "ST", "OSC", "PM", "APC"
};

static void
print_escaped(std::u32string const& s)
{
        for (auto it : s) {
                uint32_t c = (char32_t)it;

                if (c <= 0x20)
                        g_print("%s ", c0str[c]);
                else if (c < 0x7f)
                        g_print("%c ", c);
                else if (c < 0xa0)
                        g_print("%s ", c1str[c - 0x7f]);
                else
                        g_print("U+%04X", c);
        }
        g_print("\n");
}

#if 0
static void
print_seq(const struct vte_seq *seq)
{
        auto c = seq->terminator;
        if (seq->command == VTE_CMD_GRAPHIC) {
                char buf[7];
                buf[g_unichar_to_utf8(c, buf)] = 0;
                g_print("%s U+%04X [%s]\n", cmd_to_str(seq->command),
                        c,
                        g_unichar_isprint(c) ? buf : "�");
        } else {
                g_print("%s", cmd_to_str(seq->command));
                if (seq->n_args) {
                        g_print(" ");
                        for (unsigned int i = 0; i < seq->n_args; i++) {
                                if (i > 0)
                                        g_print(";");
                                g_print("%d", vte_seq_arg_value(seq->args[i]));
                        }
                }
                g_print("\n");
        }
}
#endif

class vte_seq_builder {
public:
        vte_seq_builder(unsigned int type,
                        uint32_t f) {
                memset(&m_seq, 0, sizeof(m_seq));
                m_seq.type = type;
                set_final(f);
        }

        ~vte_seq_builder() = default;

        void set_final(uint32_t raw) { m_seq.terminator = raw; }
        void set_intermediates(uint32_t* i,
                               unsigned int ni)
        {
                unsigned int flags = 0;
                for (unsigned int n = 0; n < ni; n++) {
                        flags |= (1u << (i[n] - 0x20));
                        m_i[n] = i[n];
                }
                m_ni = ni;
                m_seq.intermediates = flags;
        }

        void set_params(vte_seq_arg_t params[16])
        {
                for (unsigned int i = 0; i < 16; i++)
                        m_seq.args[i] = vte_seq_arg_init(params[i]);
        }

        void set_n_params(unsigned int n)
        {
                m_seq.n_args = n;
        }

        void set_param_byte(uint32_t p)
        {
                m_p = p;
                if (p != 0) {
                        m_seq.intermediates |= (1u << (p - 0x20));
                }
        }

        void to_string(std::u32string& s,
                       bool c1 = false);

        void assert_equal(struct vte_seq* seq);
        void assert_equal_full(struct vte_seq* seq);

        void print(bool c1 = false);

private:
        uint32_t m_i[4]{0, 0, 0, 0};
        uint32_t m_p;
        unsigned int m_ni{0};
        struct vte_seq m_seq;

};

void
vte_seq_builder::to_string(std::u32string& s,
                           bool c1)
{
        switch (m_seq.type) {
        case VTE_SEQ_ESCAPE:
                s.push_back(0x1B); // ESC
                break;
        case VTE_SEQ_CSI: {
                if (c1) {
                        s.push_back(0x9B); // CSI
                } else {
                        s.push_back(0x1B); // ESC
                        s.push_back(0x5B); // [
                }

                if (m_p != 0)
                        s.push_back(m_p);
                auto n_args = m_seq.n_args;
                for (unsigned int n = 0; n < n_args; n++) {
                        auto arg = m_seq.args[n];
                        if (n > 0)
                                s.push_back(0x3B); // semicolon
                        if (arg >= 0) {
                                char buf[16];
                                int l = g_snprintf(buf, sizeof(buf), "%d", arg);
                                for (int j = 0; j < l; j++)
                                        s.push_back(buf[j]);
                        }
                }
                break;
        }
        default:
                return;
        }

        for (unsigned int n = 0; n < m_ni; n++)
                s.push_back(m_i[n]);

        s.push_back(m_seq.terminator);
}

void
vte_seq_builder::print(bool c1)
{
        std::u32string s;
        to_string(s, c1);
        print_escaped(s);
}

void
vte_seq_builder::assert_equal(struct vte_seq* seq)
{
        g_assert_cmpuint(m_seq.type, ==, seq->type);
        g_assert_cmpuint(m_seq.terminator, ==, seq->terminator);
}

void
vte_seq_builder::assert_equal_full(struct vte_seq* seq)
{
        assert_equal(seq);
        /* We may get one arg less back, if it's at default */
        if (m_seq.n_args != seq->n_args) {
                g_assert_cmpuint(m_seq.n_args, ==, seq->n_args + 1);
                g_assert_cmpuint(m_seq.args[m_seq.n_args - 1], ==, -1);
        }
        for (unsigned int n = 0; n < seq->n_args; n++)
                g_assert_cmpint(std::min(m_seq.args[n], 0xffff), ==, vte_seq_arg_value(seq->args[n]));
}

static int
feed_parser(std::u32string const& s,
            struct vte_seq** seq)
{
        int rv = VTE_SEQ_NONE;
        for (auto it : s) {
                rv = vte_parser_feed(parser, seq, (uint32_t)(char32_t)it);
                if (rv < 0)
                        break;
        }
        return rv;
}

static int
feed_parser(vte_seq_builder& b,
            struct vte_seq** seq,
            bool c1 = false)
{
        std::u32string s;
        b.to_string(s, c1);

        return feed_parser(s, seq);
}

static void
test_seq_arg(void)
{
        /* Basic test */
        vte_seq_arg_t arg = VTE_SEQ_ARG_INIT_DEFAULT;
        g_assert_false(vte_seq_arg_started(arg));
        g_assert_true(vte_seq_arg_default(arg));

        vte_seq_arg_push(&arg, '1');
        vte_seq_arg_push(&arg, '2');
        vte_seq_arg_push(&arg, '3');
        vte_seq_arg_finish(&arg);

        g_assert_cmpint(vte_seq_arg_value(arg), ==, 123);
        g_assert_false(vte_seq_arg_default(arg));

        /* Test max value */
        arg = VTE_SEQ_ARG_INIT_DEFAULT;
        vte_seq_arg_push(&arg, '6');
        vte_seq_arg_push(&arg, '5');
        vte_seq_arg_push(&arg, '5');
        vte_seq_arg_push(&arg, '3');
        vte_seq_arg_push(&arg, '6');
        vte_seq_arg_finish(&arg);

        g_assert_cmpint(vte_seq_arg_value(arg), ==, 65535);
}

static void
test_seq_control(void)
{
        static struct {
                uint32_t c;
                unsigned int type;
                unsigned int cmd;
        } const controls [] = {
                { 0x0,  VTE_SEQ_CONTROL, VTE_CMD_NUL     },
                { 0x1,  VTE_SEQ_CONTROL, VTE_CMD_NONE    },
                { 0x2,  VTE_SEQ_CONTROL, VTE_CMD_NONE    },
                { 0x3,  VTE_SEQ_CONTROL, VTE_CMD_NONE    },
                { 0x4,  VTE_SEQ_CONTROL, VTE_CMD_NONE    },
                { 0x5,  VTE_SEQ_CONTROL, VTE_CMD_ENQ     },
                { 0x6,  VTE_SEQ_CONTROL, VTE_CMD_NONE    },
                { 0x7,  VTE_SEQ_CONTROL, VTE_CMD_BEL     },
                { 0x8,  VTE_SEQ_CONTROL, VTE_CMD_BS      },
                { 0x9,  VTE_SEQ_CONTROL, VTE_CMD_HT      },
                { 0xa,  VTE_SEQ_CONTROL, VTE_CMD_LF      },
                { 0xb,  VTE_SEQ_CONTROL, VTE_CMD_VT      },
                { 0xc,  VTE_SEQ_CONTROL, VTE_CMD_FF      },
                { 0xd,  VTE_SEQ_CONTROL, VTE_CMD_CR      },
                { 0xe,  VTE_SEQ_CONTROL, VTE_CMD_SO      },
                { 0xf,  VTE_SEQ_CONTROL, VTE_CMD_SI      },
                { 0x10, VTE_SEQ_CONTROL, VTE_CMD_NONE    },
                { 0x11, VTE_SEQ_CONTROL, VTE_CMD_DC1     },
                { 0x12, VTE_SEQ_CONTROL, VTE_CMD_NONE    },
                { 0x13, VTE_SEQ_CONTROL, VTE_CMD_DC3     },
                { 0x14, VTE_SEQ_CONTROL, VTE_CMD_NONE    },
                { 0x15, VTE_SEQ_CONTROL, VTE_CMD_NONE    },
                { 0x16, VTE_SEQ_CONTROL, VTE_CMD_NONE    },
                { 0x17, VTE_SEQ_CONTROL, VTE_CMD_NONE    },
                { 0x18, VTE_SEQ_IGNORE,  VTE_CMD_NONE    },
                { 0x19, VTE_SEQ_CONTROL, VTE_CMD_NONE    },
                { 0x1a, VTE_SEQ_CONTROL, VTE_CMD_SUB     },
                { 0x1b, VTE_SEQ_IGNORE,  VTE_CMD_NONE    },
                { 0x1c, VTE_SEQ_CONTROL, VTE_CMD_NONE    },
                { 0x1d, VTE_SEQ_CONTROL, VTE_CMD_NONE    },
                { 0x1e, VTE_SEQ_CONTROL, VTE_CMD_NONE    },
                { 0x1f, VTE_SEQ_CONTROL, VTE_CMD_NONE    },
                { 0x7f, VTE_SEQ_GRAPHIC, VTE_CMD_GRAPHIC }, // FIXMEchpe
                { 0x80, VTE_SEQ_CONTROL, VTE_CMD_NONE    },
                { 0x81, VTE_SEQ_CONTROL, VTE_CMD_NONE    },
                { 0x82, VTE_SEQ_CONTROL, VTE_CMD_NONE    },
                { 0x83, VTE_SEQ_CONTROL, VTE_CMD_NONE    },
                { 0x84, VTE_SEQ_CONTROL, VTE_CMD_IND     },
                { 0x85, VTE_SEQ_CONTROL, VTE_CMD_NEL     },
                { 0x86, VTE_SEQ_CONTROL, VTE_CMD_NONE    },
                { 0x87, VTE_SEQ_CONTROL, VTE_CMD_NONE    },
                { 0x88, VTE_SEQ_CONTROL, VTE_CMD_HTS     },
                { 0x89, VTE_SEQ_CONTROL, VTE_CMD_NONE    },
                { 0x8a, VTE_SEQ_CONTROL, VTE_CMD_NONE    },
                { 0x8b, VTE_SEQ_CONTROL, VTE_CMD_NONE    },
                { 0x8c, VTE_SEQ_CONTROL, VTE_CMD_NONE    },
                { 0x8d, VTE_SEQ_CONTROL, VTE_CMD_RI      },
                { 0x8e, VTE_SEQ_CONTROL, VTE_CMD_SS2     },
                { 0x8f, VTE_SEQ_CONTROL, VTE_CMD_SS3     },
                { 0x90, VTE_SEQ_IGNORE,  VTE_CMD_NONE    },
                { 0x91, VTE_SEQ_CONTROL, VTE_CMD_NONE    },
                { 0x92, VTE_SEQ_CONTROL, VTE_CMD_NONE    },
                { 0x93, VTE_SEQ_CONTROL, VTE_CMD_NONE    },
                { 0x94, VTE_SEQ_CONTROL, VTE_CMD_NONE    },
                { 0x95, VTE_SEQ_CONTROL, VTE_CMD_NONE    },
                { 0x96, VTE_SEQ_CONTROL, VTE_CMD_SPA     },
                { 0x97, VTE_SEQ_CONTROL, VTE_CMD_EPA     },
                { 0x98, VTE_SEQ_IGNORE,  VTE_CMD_NONE    },
                { 0x99, VTE_SEQ_CONTROL, VTE_CMD_NONE    },
                { 0x9a, VTE_SEQ_CONTROL, VTE_CMD_DECID   },
                { 0x9b, VTE_SEQ_IGNORE,  VTE_CMD_NONE    },
                { 0x9c, VTE_SEQ_IGNORE,  VTE_CMD_NONE    },
                { 0x9d, VTE_SEQ_IGNORE,  VTE_CMD_NONE    },
                { 0x9e, VTE_SEQ_IGNORE,  VTE_CMD_NONE    },
                { 0x9f, VTE_SEQ_IGNORE,  VTE_CMD_NONE    },
        };

        for (unsigned int i = 0; i < G_N_ELEMENTS(controls); i++) {
                vte_parser_reset(parser);
                struct vte_seq* seq;
                auto rv = vte_parser_feed(parser, &seq, controls[i].c);
                g_assert_cmpuint(rv, >=, 0);
                g_assert_cmpuint(controls[i].type, ==, seq->type);
                g_assert_cmpuint(controls[i].cmd, ==, seq->command);
        }
}

static void
test_seq_esc_invalid(void)
{
        /* Tests invalid ESC 0/n and ESC 1/n sequences, which should never result in
         * an VTE_SEQ_ESCAPE type sequence, but instead always in the C0 control.
         */
        for (uint32_t f = 0x0; f < 0x20; f++) {
                vte_parser_reset(parser);

                vte_seq_builder b{VTE_SEQ_ESCAPE, f};
                struct vte_seq* seq;
                auto rv = feed_parser(b, &seq);
                g_assert_cmpint(rv, !=, VTE_SEQ_ESCAPE);
        }
}

static void
test_seq_esc(uint32_t f,
             uint32_t i[],
             unsigned int ni)
{
        vte_seq_builder b{VTE_SEQ_ESCAPE, f};
        b.set_intermediates(i, ni);

        vte_parser_reset(parser);
        struct vte_seq* seq;
        auto rv = feed_parser(b, &seq);
        if (rv == VTE_SEQ_ESCAPE)
                b.assert_equal(seq);
}

static void
test_seq_esc_nF(void)
{
        /* Tests nF sequences, that is ESC 2/n [2/m..] F with F being 3/0..7/14.
         * They could have any number of itermediates, but we only test up to 4.
         */

        uint32_t i[4];
        for (uint32_t f = 0x30; f < 0x7f; f++) {
                test_seq_esc(f, i, 0);
                for (i[0] = 0x20; i[0] < 0x30; i[0]++) {
                        test_seq_esc(f, i, 1);
                        for (i[1] = 0x20; i[1] < 0x30; i[1]++) {
                                test_seq_esc(f, i, 2);
                                for (i[2] = 0x20; i[2] < 0x30; i[2]++) {
                                        test_seq_esc(f, i, 3);
                                        for (i[3] = 0x20; i[3] < 0x30; i[3]++) {
                                                test_seq_esc(f, i, 4);
                                        }
                                }
                        }
                }
        }
}

static void
test_seq_esc_charset(uint32_t f, /* final */
                     uint32_t i[], /* intermediates */
                     unsigned int ni, /* number of intermediates */
                     unsigned int cmd, /* expected command */
                     unsigned int cs /* expected charset */)
{
        vte_seq_builder b{VTE_SEQ_ESCAPE, f};
        b.set_intermediates(i, ni);

        vte_parser_reset(parser);
        struct vte_seq* seq;
        auto rv = feed_parser(b, &seq);
        g_assert_cmpint(rv, ==, VTE_SEQ_ESCAPE);
        b.assert_equal(seq);

        g_assert_cmpint(seq->command, ==, cmd);
        g_assert_cmpint(seq->charset, ==, cs);
}

static void
test_seq_esc_charset(uint32_t i[], /* intermediates */
                     unsigned int ni, /* number of intermediates */
                     uint8_t const* const table, /* table */
                     unsigned int ntable, /* number of table entries */
                     uint32_t ts, /* start of table */
                     unsigned int cmd, /* expected command */
                     unsigned int defaultcs /* default charset */)
{
        for (uint32_t f = 0x30; f < 0x7f; f++) {
                int cs;

                if (f >= ts && f < (ts + ntable))
                        cs = table[f - ts];
                else
                        cs = defaultcs;

                test_seq_esc_charset(f, i, ni, cmd, cs);
        }
}

static void
test_seq_esc_charset_94(void)
{
        uint32_t i[4];

        /* Single byte 94-sets */
        for (i[0] = 0x28; i[0] <= 0x2b; i[0]++) {
                test_seq_esc_charset(i, 1,
                                     charset_graphic_94,
                                     G_N_ELEMENTS(charset_graphic_94),
                                     0x30, VTE_CMD_GnDm, VTE_CHARSET_NONE);

                i[1] = 0x20;
                test_seq_esc_charset(i, 2, nullptr, 0, 0,
                                     VTE_CMD_GnDm, VTE_CHARSET_DRCS);

                i[1] = 0x21;
                test_seq_esc_charset(i, 2,
                                     charset_graphic_94_with_2_1,
                                     G_N_ELEMENTS(charset_graphic_94_with_2_1),
                                     0x40, VTE_CMD_GnDm, VTE_CHARSET_NONE);

                i[1] = 0x22;
                test_seq_esc_charset(i, 2,
                                     charset_graphic_94_with_2_2,
                                     G_N_ELEMENTS(charset_graphic_94_with_2_2),
                                     0x30, VTE_CMD_GnDm, VTE_CHARSET_NONE);

                i[1] = 0x23;
                test_seq_esc_charset(i, 2, nullptr, 0,
                                     0x30, VTE_CMD_GnDm, VTE_CHARSET_NONE);

                /* 2/4 is multibyte charsets */

                i[1] = 0x25;
                test_seq_esc_charset(i, 2,
                                     charset_graphic_94_with_2_5,
                                     G_N_ELEMENTS(charset_graphic_94_with_2_5),
                                     0x30, VTE_CMD_GnDm, VTE_CHARSET_NONE);

                i[1] = 0x26;
                test_seq_esc_charset(i, 2,
                                     charset_graphic_94_with_2_6,
                                     G_N_ELEMENTS(charset_graphic_94_with_2_6),
                                     0x30, VTE_CMD_GnDm, VTE_CHARSET_NONE);

                i[1] = 0x27;
                test_seq_esc_charset(i, 2, nullptr, 0, 0,
                                     VTE_CMD_GnDm, VTE_CHARSET_NONE);
        }
}

static void
test_seq_esc_charset_96(void)
{
        uint32_t i[4];

        /* Single byte 96-sets */
        for (i[0] = 0x2d; i[0] <= 0x2f; i[0]++) {
                test_seq_esc_charset(i, 1,
                                     charset_graphic_96,
                                     G_N_ELEMENTS(charset_graphic_96),
                                     0x30, VTE_CMD_GnDm, VTE_CHARSET_NONE);

                i[1] = 0x20;
                test_seq_esc_charset(i, 2, nullptr, 0, 0,
                                     VTE_CMD_GnDm, VTE_CHARSET_DRCS);

                /* 2/4 is multibyte charsets, 2/5 is DOCS. Other indermediates may be present
                 * in Fp sequences, but none are actually in use.
                 */
                for (i[1] = 0x21; i[1] < 0x28; i[1]++) {
                        if (i[1] == 0x24 || i[1] == 0x25)
                                continue;

                        test_seq_esc_charset(i, 2, nullptr, 0, 0,
                                             VTE_CMD_GnDm, VTE_CHARSET_NONE);
                }
        }
}

static void
test_seq_esc_charset_94_n(void)
{
        uint32_t i[4];

        /* Multibyte 94-sets */
        i[0] = 0x24;
        for (i[1] = 0x28; i[1] <= 0x2b; i[1]++) {
                test_seq_esc_charset(i, 2,
                                     charset_graphic_94_n,
                                     G_N_ELEMENTS(charset_graphic_94_n),
                                     0x30, VTE_CMD_GnDMm, VTE_CHARSET_NONE);

                i[2] = 0x20;
                test_seq_esc_charset(i, 3, nullptr, 0, 0,
                                     VTE_CMD_GnDMm, VTE_CHARSET_DRCS);

                /* There could be one more intermediate byte. */
                for (i[2] = 0x21; i[2] < 0x28; i[2]++) {
                        if (i[2] == 0x24) /* TODO */
                                continue;

                        test_seq_esc_charset(i, 3, nullptr, 0, 0,
                                             VTE_CMD_GnDMm, VTE_CHARSET_NONE);
                }
        }

        /* As a special exception, ESC 2/4 4/[012] are also possible */
        test_seq_esc_charset(0x40, i, 1, VTE_CMD_GnDMm, charset_graphic_94_n[0x40 - 0x30]);
        test_seq_esc_charset(0x41, i, 1, VTE_CMD_GnDMm, charset_graphic_94_n[0x41 - 0x30]);
        test_seq_esc_charset(0x42, i, 1, VTE_CMD_GnDMm, charset_graphic_94_n[0x42 - 0x30]);
}

static void
test_seq_esc_charset_96_n(void)
{
        uint32_t i[4];

        /* Multibyte 94-sets */
        i[0] = 0x24;
        for (i[1] = 0x2d; i[1] <= 0x2f; i[1]++) {
                test_seq_esc_charset(i, 2, nullptr, 0, 0,
                                     VTE_CMD_GnDMm, VTE_CHARSET_NONE);

                i[2] = 0x20;
                test_seq_esc_charset(i, 3, nullptr, 0, 0,
                                     VTE_CMD_GnDMm, VTE_CHARSET_DRCS);

                /* There could be one more intermediate byte. */
                for (i[2] = 0x21; i[2] < 0x28; i[2]++) {
                        test_seq_esc_charset(i, 3, nullptr, 0, 0,
                                             VTE_CMD_GnDMm, VTE_CHARSET_NONE);
                }
        }
}

static void
test_seq_esc_charset_control(void)
{
        uint32_t i[4];

        /* C0 controls: ESC 2/1 F */
        i[0] = 0x21;
        test_seq_esc_charset(i, 1,
                             charset_control_c0,
                             G_N_ELEMENTS(charset_control_c0),
                             0x40, VTE_CMD_CnD, VTE_CHARSET_NONE);

        /* C1 controls: ESC 2/2 F */
        i[0] = 0x22;
        test_seq_esc_charset(i, 1,
                             charset_control_c1,
                             G_N_ELEMENTS(charset_control_c1),
                             0x40, VTE_CMD_CnD, VTE_CHARSET_NONE);
}

static void
test_seq_esc_charset_other(void)
{
        uint32_t i[4];

        /* Other coding systems: ESC 2/5 F or ESC 2/5 2/15 F */
        i[0] = 0x25;
        test_seq_esc_charset(i, 1,
                             charset_ocs_with_return,
                             G_N_ELEMENTS(charset_ocs_with_return),
                             0x40, VTE_CMD_DOCS, VTE_CHARSET_NONE);

        i[1] = 0x2f;
        test_seq_esc_charset(i, 2,
                             charset_ocs_without_return,
                             G_N_ELEMENTS(charset_ocs_without_return),
                             0x40, VTE_CMD_DOCS, VTE_CHARSET_NONE);
}

static void
test_seq_esc_Fpes(void)
{
        /* Tests Fp, Fe and Ft sequences, that is ESC 3/n .. ESC 7/14 */

        for (uint32_t f = 0x30; f < 0x7f; f++) {
                vte_parser_reset(parser);

                vte_seq_builder b{VTE_SEQ_ESCAPE, f};

                struct vte_seq* seq;
                auto rv = feed_parser(b, &seq);
                int expected_rv;
                switch (f) {
                case 'P': /* DCS */
                case 'X': /* SOS */
                case '_': /* APC */
                case '[': /* CSI */
                case ']': /* OSC */
                case '^': /* PM */
                        expected_rv = VTE_SEQ_NONE;
                        break;
                default:
                        expected_rv = VTE_SEQ_ESCAPE;
                        break;
                }
                g_assert_cmpint(rv, ==, expected_rv);
                if (rv != VTE_SEQ_NONE)
                        b.assert_equal(seq);
        }
}

static void
test_seq_csi(uint32_t f,
             uint32_t p,
             vte_seq_arg_t params[16],
             uint32_t i[4],
             unsigned int ni)
{
        vte_seq_builder b{VTE_SEQ_CSI, f};
        b.set_intermediates(i, ni);
        b.set_param_byte(p);
        b.set_params(params);

        int expected_rv = (f & 0xF0) == 0x30 ? VTE_SEQ_NONE : VTE_SEQ_CSI;

        for (unsigned int n = 0; n <= 16; n++) {
                b.set_n_params(n);

                vte_parser_reset(parser);
                struct vte_seq* seq;
                /* First with C0 CSI */
                auto rv = feed_parser(b, &seq, false);
                g_assert_cmpint(rv, ==, expected_rv);
                if (rv != VTE_SEQ_NONE)
                        b.assert_equal_full(seq);

                /* Now with C1 CSI */
                rv = feed_parser(b, &seq, true);
                if (rv != VTE_SEQ_NONE)
                        b.assert_equal_full(seq);
        }
}

static void
test_seq_csi(uint32_t p,
             vte_seq_arg_t params[16])
{
        uint32_t i[4];
        for (uint32_t f = 0x30; f < 0x7f; f++) {
                test_seq_csi(f, p, params, i, 0);
                for (i[0] = 0x20; i[0] < 0x30; i[0]++) {
                        test_seq_csi(f, p, params, i, 1);
                        for (i[1] = 0x20; i[1] < 0x30; i[1]++) {
                                test_seq_csi(f, p, params, i, 2);
                        }
                }
        }
}

static void
test_seq_csi(vte_seq_arg_t params[16])
{
        test_seq_csi(0, params);
        for (uint32_t p = 0x3c; p <= 0x3f; p++)
                test_seq_csi(p, params);
}

static void
test_seq_csi(void)
{
        /* Tests CSI sequences, that is sequences of the form
         * CSI P...P I...I F
         * with parameter bytes P from 3/0..3/15, intermediate bytes I from 2/0..2/15 and
         * final byte F from 4/0..7/14.
         * There could be any number of intermediate bytes, but we only test up to 2.
         * There could be any number of extra params bytes, but we only test up to 1.
         * CSI can be either the C1 control itself, or ESC [
         */
        vte_seq_arg_t params1[16]{ -1, 0, 1, 9, 10, 99, 100, 999,
                        1000, 9999, 10000, 65534, 65535, 65536, -1, -1 };
        test_seq_csi(params1);

        vte_seq_arg_t params2[16]{ 1, -1, -1, -1, 1, -1, 1, 1,
                        1, -1, -1, -1, -1, 1, 1, 1 };
        test_seq_csi(params2);
}

static void
test_seq_csi_param(char const* str,
                   std::vector<int> args,
                   std::vector<bool> args_nonfinal)
{
        g_assert_cmpuint(args.size(), ==, args_nonfinal.size());

        std::u32string s;
        s.push_back(0x9B); /* CSI */
        for (unsigned int i = 0; str[i]; i++)
                s.push_back(str[i]);
        s.push_back(0x6d); /* m = SGR */

        struct vte_seq* seq;
        auto rv = feed_parser(s, &seq);
        g_assert_cmpint(rv, ==, VTE_SEQ_CSI);

        if (seq->n_args < VTE_PARSER_ARG_MAX)
                g_assert_cmpuint(seq->n_args, ==, args.size());

        unsigned int n_final_args = 0;
        for (unsigned int i = 0; i < seq->n_args; i++) {
                g_assert_cmpint(vte_seq_arg_value(seq->args[i]), ==, args[i]);

                auto is_nonfinal = args_nonfinal[i];
                if (!is_nonfinal)
                        n_final_args++;

                g_assert_cmpint(!!vte_seq_arg_nonfinal(seq->args[i]), ==, is_nonfinal);
        }

        g_assert_cmpuint(seq->n_final_args, ==, n_final_args);
}

static void
test_seq_csi_param(void)
{
        /* Tests that CSI parameters and subparameters are parsed correctly. */

        test_seq_csi_param("", { }, { });
        test_seq_csi_param(";", { -1, -1 }, { false, false });
        test_seq_csi_param(":", { -1, -1 }, { true, false });
        test_seq_csi_param(";:", { -1, -1, -1 }, { false, true, false });
        test_seq_csi_param("::;;", { -1, -1, -1, -1, -1 }, { true, true, false, false, false });

        test_seq_csi_param("1;2:3:4:5:6;7:8;9:0",
                           { 1, 2, 3, 4, 5, 6, 7, 8, 9, 0 },
                           { false, true, true, true, true, false, true, false, true, false });

        test_seq_csi_param("1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1",
                           { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
                           { false, false, false, false, false, false, false, false,
                                           false, false, false, false, false, false, false, false });

        test_seq_csi_param("1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1",
                           { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
                           { true, true, true, true, true, true, true, true,
                                           true, true, true, true, true, true, true, false });

}

int
main(int argc,
     char* argv[])
{
        g_test_init(&argc, &argv, nullptr);

        if (vte_parser_new(&parser) < 0)
                return 1;

        g_test_add_func("/vte/parser/sequences/arg", test_seq_arg);
        g_test_add_func("/vte/parser/sequences/control", test_seq_control);
        g_test_add_func("/vte/parser/sequences/escape/invalid", test_seq_esc_invalid);
        g_test_add_func("/vte/parser/sequences/escape/charset/94", test_seq_esc_charset_94);
        g_test_add_func("/vte/parser/sequences/escape/charset/96", test_seq_esc_charset_96);
        g_test_add_func("/vte/parser/sequences/escape/charset/94^n", test_seq_esc_charset_94_n);
        g_test_add_func("/vte/parser/sequences/escape/charset/96^n", test_seq_esc_charset_96_n);
        g_test_add_func("/vte/parser/sequences/escape/charset/control", test_seq_esc_charset_control);
        g_test_add_func("/vte/parser/sequences/escape/charset/other", test_seq_esc_charset_other);
        g_test_add_func("/vte/parser/sequences/escape/nF", test_seq_esc_nF);
        g_test_add_func("/vte/parser/sequences/escape/F[pes]", test_seq_esc_Fpes);
        g_test_add_func("/vte/parser/sequences/csi", test_seq_csi);
        g_test_add_func("/vte/parser/sequences/csi/parameters", test_seq_csi_param);

        auto rv = g_test_run();

        parser = vte_parser_free(parser);
        return rv;
}
