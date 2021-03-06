/*
 * This file is part of gtatool, a tool to manipulate Generic Tagged Arrays
 * (GTAs).
 *
 * Copyright (C) 2010, 2011, 2012, 2013
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

#include <sstream>
#include <cstdio>
#include <cctype>
#include <limits>

#include <gta/gta.hpp>

#include "base/msg.h"
#include "base/blb.h"
#include "base/opt.h"
#include "base/str.h"
#include "base/chk.h"

#include "lib.h"


extern "C" void gtatool_create_help(void)
{
    msg::req_txt(
            "create [-d|--dimensions=<d0>[,<d1>[,...]]] [-c|--components=<c0>[,<c1>[,...]]] "
            "[-v|--value=<v0>[,<v1>[,...]]] [-n|--n=<n>] [<output-file>]\n"
            "\n"
            "Creates n GTAs and writes them to standard output or the given output file.\n"
            "Default is n=1. "
            "The dimensions and components must be given as comma-separated lists. "
            "An initial value for all array elements can be given as a comma-separated list, "
            "with one entry for each element component. "
            "The default initial value is zero for all element components.\n"
            "Example: -d 256,128 -c uint8,uint8,uint8 -v 32,64,128");
}

extern "C" int gtatool_create(int argc, char *argv[])
{
    std::vector<opt::option *> options;
    opt::info help("help", '\0', opt::optional);
    options.push_back(&help);
    opt::tuple<uintmax_t> dimensions("dimensions", 'd', opt::optional, 1, std::numeric_limits<uintmax_t>::max());
    options.push_back(&dimensions);
    opt::string components("components", 'c', opt::optional);
    options.push_back(&components);
    opt::string value("value", 'v', opt::optional);
    options.push_back(&value);
    opt::val<uintmax_t> n("n", 'n', opt::optional, 1);
    options.push_back(&n);
    std::vector<std::string> arguments;
    if (!opt::parse(argc, argv, options, 0, 1, arguments))
    {
        return 1;
    }
    if (help.value())
    {
        gtatool_create_help();
        return 0;
    }

    try
    {
        array_loop_t array_loop;
        gta::header hdr;
        std::string name;
        if (!dimensions.values().empty())
        {
            hdr.set_dimensions(dimensions.value().size(), &(dimensions.value()[0]));
        }
        std::vector<gta::type> comp_types;
        std::vector<uintmax_t> comp_sizes;
        if (!components.values().empty())
        {
            typelist_from_string(components.value(), &comp_types, &comp_sizes);
            hdr.set_components(comp_types.size(), &(comp_types[0]), comp_sizes.size() == 0 ? NULL : &(comp_sizes[0]));
        }
        blob v(checked_cast<size_t>(hdr.element_size()));
        if (value.values().empty())
        {
            memset(v.ptr(), 0, hdr.element_size());
        }
        else
        {
            valuelist_from_string(value.value(), comp_types, comp_sizes, v.ptr());
        }
        array_loop.start(std::vector<std::string>(), arguments.size() == 1 ? arguments[0] : "");
        for (uintmax_t i = 0; i < n.value(); i++)
        {
            array_loop.write(hdr, name);
            element_loop_t element_loop;
            array_loop.start_element_loop(element_loop, gta::header(), hdr);
            for (uintmax_t j = 0; j < hdr.elements(); j++)
            {
                element_loop.write(v.ptr());
            }
        }
        array_loop.finish();
    }
    catch (std::exception &e)
    {
        msg::err_txt("%s", e.what());
        return 1;
    }

    return 0;
}
