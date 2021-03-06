/*
 * This file is part of gtatool, a tool to manipulate Generic Tagged Arrays
 * (GTAs).
 *
 * Copyright (C) 2010, 2011, 2012, 2013, 2014, 2015
 * Martin Lambers <marlam@marlam.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <string>
#include <limits>

#include <gta/gta.hpp>

#include "base/msg.h"
#include "base/blb.h"
#include "base/fio.h"
#include "base/opt.h"
#include "base/str.h"
#include "base/chk.h"

#include "lib.h"

#ifdef HAVE_PFS_PFS_H
# include <pfs/pfs.h>
#else
# include <pfs-1.2/pfs.h>
#endif


extern "C" void gtatool_to_pfs_help(void)
{
    msg::req_txt("to-pfs [<input-file>] <output-file>\n"
            "\n"
            "Converts GTAs to the PFS format using libpfs.");
}

extern "C" int gtatool_to_pfs(int argc, char *argv[])
{
    std::vector<opt::option *> options;
    opt::info help("help", '\0', opt::optional);
    options.push_back(&help);
    std::vector<std::string> arguments;
    if (!opt::parse(argc, argv, options, 1, 2, arguments))
    {
        return 1;
    }
    if (help.value())
    {
        gtatool_to_pfs_help();
        return 0;
    }

    FILE *fi = gtatool_stdin;
    std::string ifilename("standard input");
    std::string ofilename(arguments[0]);
    try
    {
        if (arguments.size() == 2)
        {
            ifilename = arguments[0];
            fi = fio::open(ifilename, "r");
            ofilename = arguments[1];
        }
    }
    catch (std::exception &e)
    {
        msg::err_txt("%s", e.what());
        return 1;
    }

    try
    {
        FILE *fo = fio::open(ofilename, "w");
        while (fio::has_more(fi, ifilename))
        {
            gta::header hdr;
            hdr.read_from(fi);
            if (hdr.dimensions() != 2)
            {
                throw exc("cannot export " + ifilename + ": only two-dimensional arrays can be exported to images");
            }
            if (hdr.components() < 1)
            {
                throw exc("cannot export " + ifilename + ": array has no components");
            }
            for (uintmax_t i = 0; i < hdr.components(); i++)
            {
                if (hdr.component_type(i) == gta::int32
                        || hdr.component_type(i) == gta::uint32
                        || hdr.component_type(i) == gta::int64
                        || hdr.component_type(i) == gta::uint64
#ifdef HAVE_INT128_T
                        || hdr.component_type(i) == gta::int128
#endif
#ifdef HAVE_UINT128_T
                        || hdr.component_type(i) == gta::uint128
#endif
                        || hdr.component_type(i) == gta::float64
#ifdef HAVE_FLOAT128_T
                        || hdr.component_type(i) == gta::float128
#endif
                   )
                {
                    msg::wrn_txt(std::string("converting ")
                            + type_to_string(hdr.component_type(i), hdr.component_size(i))
                            + " to "
                            + type_to_string(gta::float32, sizeof(float))
                            + " for array element component "
                            + str::from(i)
                            + " may lose precision");
                }
                else if (hdr.component_type(i) != gta::int8
                        && hdr.component_type(i) != gta::uint8
                        && hdr.component_type(i) != gta::int16
                        && hdr.component_type(i) != gta::uint16
                        && hdr.component_type(i) != gta::float32)
                {
                    throw exc("cannot export " + ifilename + ": array contains unexportable element component types");
                }
            }
            if (hdr.dimension_size(0) > static_cast<uintmax_t>(std::numeric_limits<int>::max())
                    || hdr.dimension_size(1) > static_cast<uintmax_t>(std::numeric_limits<int>::max()))
            {
                throw exc("cannot export " + ifilename + ": array too large");
            }

            pfs::DOMIO pfsio;
            pfs::Frame *frame = pfsio.createFrame(hdr.dimension_size(0), hdr.dimension_size(1));
            frame->getTags()->setString("FILE_NAME", ofilename.c_str());
            for (uintmax_t t = 0; t < hdr.global_taglist().tags(); t++)
            {
                const char *name = hdr.global_taglist().name(t);
                const char *value = hdr.global_taglist().value(t);
                if (strncmp(name, "PFS/", 4) == 0)
                {
                    frame->getTags()->setString(name + 4, value);
                }
                else
                {
                    std::string newname = std::string("X-GTA/") + name;
                    frame->getTags()->setString(newname.c_str(), value);
                }
            }
            std::vector<float *> channels(hdr.components());
            for (uintmax_t i = 0; i < hdr.components(); i++)
            {
                const char *interpretation = hdr.component_taglist(i).get("INTERPRETATION");
                const char *pfs_name = hdr.component_taglist(i).get("PFS/NAME");
                std::string channel_name;
                if (interpretation && strcmp(interpretation, "XYZ/X") == 0)
                {
                    channel_name = "X";
                }
                else if (interpretation && strcmp(interpretation, "XYZ/Y") == 0)
                {
                    channel_name = "Y";
                }
                else if (interpretation && strcmp(interpretation, "XYZ/Z") == 0)
                {
                    channel_name = "Z";
                }
                else if (interpretation && strcmp(interpretation, "ALPHA") == 0)
                {
                    channel_name = "ALPHA";
                }
                else if (pfs_name)
                {
                    channel_name = pfs_name;
                }
                else if (interpretation)
                {
                    channel_name = std::string("X-GTA/") + std::string(interpretation);
                }
                else
                {
                    channel_name = std::string("X-GTA/") + str::from(i);
                }
                pfs::Channel *channel = frame->createChannel(channel_name.c_str());
                for (uintmax_t t = 0; t < hdr.component_taglist(i).tags(); t++)
                {
                    const char *name = hdr.component_taglist(i).name(t);
                    const char *value = hdr.component_taglist(i).value(t);
                    if (strcmp(name, "PFS/NAME") == 0 || strcmp(name, "INTERPRETATION") == 0)
                    {
                        continue;
                    }
                    if (strncmp(name, "PFS/", 4) == 0)
                    {
                        channel->getTags()->setString(name + 4, value);
                    }
                    else
                    {
                        std::string newname = std::string("X-GTA/") + name;
                        channel->getTags()->setString(newname.c_str(), value);
                    }
                }
                channels[i] = channel->getRawData();
            }

            blob data(checked_cast<size_t>(hdr.data_size()));
            hdr.read_data(fi, data.ptr());
            for (uintmax_t y = 0; y < hdr.dimension_size(1); y++)
            {
                for (uintmax_t x = 0; x < hdr.dimension_size(0); x++)
                {
                    void *element = hdr.element(data.ptr(), x, y);
                    for (uintmax_t i = 0; i < hdr.components(); i++)
                    {
                        void *component = hdr.component(element, i);
                        if (hdr.component_type(i) == gta::int8)
                        {
                            int8_t v;
                            memcpy(&v, component, sizeof(int8_t));
                            channels[i][y * hdr.dimension_size(0) + x] = v;
                        }
                        else if (hdr.component_type(i) == gta::uint8)
                        {
                            uint8_t v;
                            memcpy(&v, component, sizeof(uint8_t));
                            channels[i][y * hdr.dimension_size(0) + x] = v;
                        }
                        else if (hdr.component_type(i) == gta::int16)
                        {
                            int16_t v;
                            memcpy(&v, component, sizeof(int16_t));
                            channels[i][y * hdr.dimension_size(0) + x] = v;
                        }
                        else if (hdr.component_type(i) == gta::uint16)
                        {
                            uint16_t v;
                            memcpy(&v, component, sizeof(uint16_t));
                            channels[i][y * hdr.dimension_size(0) + x] = v;
                        }
                        else if (hdr.component_type(i) == gta::int32)
                        {
                            int32_t v;
                            memcpy(&v, component, sizeof(int32_t));
                            channels[i][y * hdr.dimension_size(0) + x] = v;
                        }
                        else if (hdr.component_type(i) == gta::uint32)
                        {
                            uint32_t v;
                            memcpy(&v, component, sizeof(uint32_t));
                            channels[i][y * hdr.dimension_size(0) + x] = v;
                        }
                        else if (hdr.component_type(i) == gta::int64)
                        {
                            int64_t v;
                            memcpy(&v, component, sizeof(int64_t));
                            channels[i][y * hdr.dimension_size(0) + x] = v;
                        }
                        else if (hdr.component_type(i) == gta::uint64)
                        {
                            uint64_t v;
                            memcpy(&v, component, sizeof(uint64_t));
                            channels[i][y * hdr.dimension_size(0) + x] = v;
                        }
#ifdef HAVE_INT128_T
                        else if (hdr.component_type(i) == gta::int128)
                        {
                            int128_t v;
                            memcpy(&v, component, sizeof(int128_t));
                            channels[i][y * hdr.dimension_size(0) + x] = v;
                        }
#endif
#ifdef HAVE_UINT128_T
                        else if (hdr.component_type(i) == gta::uint128)
                        {
                            uint128_t v;
                            memcpy(&v, component, sizeof(uint128_t));
                            channels[i][y * hdr.dimension_size(0) + x] = v;
                        }
#endif
                        else if (hdr.component_type(i) == gta::float32)
                        {
                            float v;
                            memcpy(&v, component, sizeof(float));
                            channels[i][y * hdr.dimension_size(0) + x] = v;
                        }
                        else if (hdr.component_type(i) == gta::float64)
                        {
                            double v;
                            memcpy(&v, component, sizeof(double));
                            channels[i][y * hdr.dimension_size(0) + x] = v;
                        }
#ifdef HAVE_FLOAT128_T
                        else if (hdr.component_type(i) == gta::float128)
                        {
                            float128_t v;
                            memcpy(&v, component, sizeof(float128_t));
                            channels[i][y * hdr.dimension_size(0) + x] = v;
                        }
#endif
                        else
                        {
                            // cannot happen
                            assert(false);
                            channels[i][y * hdr.dimension_size(0) + x] = 0;
                        }
                    }
                }
            }
            pfsio.writeFrame(frame, fo);
            pfsio.freeFrame(frame);
        }
        if (fi != gtatool_stdin)
        {
            fio::close(fi);
        }
        fio::close(fo);
    }
    catch (std::exception &e)
    {
        msg::err_txt("%s", e.what());
        return 1;
    }

    return 0;
}
