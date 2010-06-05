/*
 * component-compute.cpp
 *
 * This file is part of gtatool, a tool to manipulate Generic Tagged Arrays
 * (GTAs).
 *
 * Copyright (C) 2010  Martin Lambers <marlam@marlam.de>
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

#include <gta/gta.hpp>

#include <muParser.h>

#include "msg.h"
#include "blob.h"
#include "opt.h"
#include "cio.h"
#include "str.h"
#include "intcheck.h"

#include "lib.h"


extern "C" void gtatool_component_compute_help(void)
{
    msg::req_txt(
            "component-compute -e|--expression=<exp0> [-e|--expression=<exp1> [...]] [<files>...]\n"
            "\n"
            "Compute array element components. For each array element in an input GTA with n array element components, "
            "the components c0..c(n-1) can be recomputed using the given expression(s). "
            "All computations are done in double precision floating point, regardless of the original component type. "
            "For complex components (cfloat), there are two component variables for the real and imaginary part, e.g. "
            "c4re and c4im for component 4. In addition to the modifiable variables c0..c(n-1), the following "
            "non-modifiable variables are defined: c (the number of components of an array element), "
            "d (the number of dimensions of the array), d0..d(d-1) (the array size in each dimension), "
            "i0..i(d-1) (the index of the current array element).\n"
            "The expressions are evaluated using the muParser library; see <http://muparser.sourceforge.net/> for an "
            "overview of functions and operators that can be used.\n"
            "Example: component-compute -e 'c3 = 0.2126 * c0 + 0.7152 * c1 + 0.0722 * c2' rgba.gta > rgb+lum.gta");
}

extern "C" int gtatool_component_compute(int argc, char *argv[])
{
    std::vector<opt::option *> options;
    opt::info help("help", '\0', opt::optional);
    options.push_back(&help);
    opt::string expressions("expression", 'e', opt::required);
    options.push_back(&expressions);
    std::vector<std::string> arguments;
    if (!opt::parse(argc, argv, options, -1, -1, arguments))
    {
        return 1;
    }
    if (help.value())
    {
        gtatool_component_compute_help();
        return 0;
    }

    if (cio::isatty(gtatool_stdout))
    {
        msg::err_txt("refusing to write to a tty");
        return 1;
    }

    try
    {
        gta::header hdri;
        gta::header hdro;
        // Loop over all input files
        size_t arg = 0;
        do
        {
            std::string finame = (arguments.size() == 0 ? "standard input" : arguments[arg]);
            FILE *fi = (arguments.size() == 0 ? gtatool_stdin : cio::open(finame, "r"));

            // Loop over all GTAs inside the current file
            uintmax_t array_index = 0;
            while (cio::has_more(fi, finame))
            {
                // Determine the name of the array for error messages
                std::string array_name = finame + " array " + str::from(array_index);
                // Read the GTA header
                hdri.read_from(fi);
                if (hdri.dimensions() > std::numeric_limits<size_t>::max())
                {
                    throw exc(array_name + ": too many dimensions");
                }
                // Set up variables
                std::vector<double> comp_vars;
                for (uintmax_t i = 0; i < hdri.components(); i++)
                {
                    if (hdri.component_type(i) == gta::blob
                            || hdri.component_type(i) == gta::int128
                            || hdri.component_type(i) == gta::uint128
                            || hdri.component_type(i) == gta::float128
                            || hdri.component_type(i) == gta::cfloat128)
                    {
                        throw exc(array_name + ": cannot compute variables of type "
                                + type_to_string(hdri.component_type(i), hdri.component_size(i)));
                    }
                    if (hdri.component_type(i) == gta::cfloat32 || hdri.component_type(i) == gta::cfloat64)
                    {
                        comp_vars.push_back(0.0);
                        comp_vars.push_back(0.0);
                    }
                    else
                    {
                        comp_vars.push_back(0.0);
                    }
                }
                double components_var;
                double dimensions_var;
                std::vector<double> dim_vars(hdri.dimensions());
                std::vector<uintmax_t> index_vars_orig(hdri.dimensions());
                std::vector<double> index_vars(hdri.dimensions());
                std::vector<mu::Parser> parsers;
                parsers.resize(expressions.values().size());
                for (size_t p = 0; p < expressions.values().size(); p++)
                {
                    size_t comp_vars_index = 0;
                    for (uintmax_t i = 0; i < hdri.components(); i++)
                    {
                        if (hdri.component_type(i) == gta::cfloat32 || hdri.component_type(i) == gta::cfloat64)
                        {
                            parsers[p].DefineVar(std::string("c") + str::from(i) + "re", &(comp_vars[comp_vars_index++]));
                            parsers[p].DefineVar(std::string("c") + str::from(i) + "im", &(comp_vars[comp_vars_index++]));
                        }
                        else
                        {
                            parsers[p].DefineVar(std::string("c") + str::from(i), &(comp_vars[comp_vars_index++]));
                        }
                    }
                    parsers[p].DefineVar("c", &components_var);
                    parsers[p].DefineVar("d", &dimensions_var);
                    for (uintmax_t i = 0; i < hdri.dimensions(); i++)
                    {
                        parsers[p].DefineVar(std::string("d") + str::from(i), &(dim_vars[i]));
                        parsers[p].DefineVar(std::string("i") + str::from(i), &(index_vars[i]));
                    }
                    parsers[p].SetExpr(expressions.values()[p]);
                }
                // Write the GTA header
                hdro = hdri;
                hdro.set_compression(gta::none);
                hdro.write_to(gtatool_stdout);
                // Manipulate the GTA data
                blob element(checked_cast<size_t>(hdri.element_size()));
                gta::io_state si, so;
                for (uintmax_t e = 0; e < hdro.elements(); e++)
                {
                    hdri.read_elements(si, fi, 1, element.ptr());
                    // set the variables
                    components_var = hdri.components();
                    dimensions_var = hdri.dimensions();
                    for (uintmax_t i = 0; i < hdri.dimensions(); i++)
                    {
                        dim_vars[i] = hdri.dimension_size(i);
                    }
                    linear_index_to_indices(hdri, e, &(index_vars_orig[0]));
                    for (uintmax_t i = 0; i < hdri.dimensions(); i++)
                    {
                        index_vars[i] = index_vars_orig[i];
                    }
                    size_t comp_var_index = 0;
                    for (uintmax_t i = 0; i < hdri.components(); i++)
                    {
                        switch (hdri.component_type(i))
                        {
                        case gta::int8:
                            {
                                int8_t v;
                                memcpy(&v, hdri.component(element.ptr(), i), sizeof(int8_t));
                                comp_vars[comp_var_index++] = v;
                            }
                            break;
                        case gta::uint8:
                            {
                                uint8_t v;
                                memcpy(&v, hdri.component(element.ptr(), i), sizeof(uint8_t));
                                comp_vars[comp_var_index++] = v;
                            }
                            break;
                        case gta::int16:
                            {
                                int16_t v;
                                memcpy(&v, hdri.component(element.ptr(), i), sizeof(int16_t));
                                comp_vars[comp_var_index++] = v;
                            }
                            break;
                        case gta::uint16:
                            {
                                uint16_t v;
                                memcpy(&v, hdri.component(element.ptr(), i), sizeof(uint16_t));
                                comp_vars[comp_var_index++] = v;
                            }
                            break;
                        case gta::int32:
                            {
                                int32_t v;
                                memcpy(&v, hdri.component(element.ptr(), i), sizeof(int32_t));
                                comp_vars[comp_var_index++] = v;
                            }
                            break;
                        case gta::uint32:
                            {
                                uint32_t v;
                                memcpy(&v, hdri.component(element.ptr(), i), sizeof(uint32_t));
                                comp_vars[comp_var_index++] = v;
                            }
                            break;
                        case gta::int64:
                            {
                                int64_t v;
                                memcpy(&v, hdri.component(element.ptr(), i), sizeof(int64_t));
                                comp_vars[comp_var_index++] = v;
                            }
                            break;
                        case gta::uint64:
                            {
                                uint64_t v;
                                memcpy(&v, hdri.component(element.ptr(), i), sizeof(uint64_t));
                                comp_vars[comp_var_index++] = v;
                            }
                            break;
                        case gta::float32:
                            {
                                float v;
                                memcpy(&v, hdri.component(element.ptr(), i), sizeof(float));
                                comp_vars[comp_var_index++] = v;
                            }
                            break;
                        case gta::float64:
                            {
                                memcpy(&(comp_vars[comp_var_index++]), hdri.component(element.ptr(), i), sizeof(double));
                            }
                            break;
                        case gta::cfloat32:
                            {
                                float v[2];
                                memcpy(v, hdri.component(element.ptr(), i), 2 * sizeof(float));
                                comp_vars[comp_var_index++] = v[0];
                                comp_vars[comp_var_index++] = v[1];
                            }
                            break;
                        case gta::cfloat64:
                            {
                                double v[2];
                                memcpy(v, hdri.component(element.ptr(), i), 2 * sizeof(double));
                                comp_vars[comp_var_index++] = v[0];
                                comp_vars[comp_var_index++] = v[1];
                            }
                            break;
                        default:
                            // cannot happen
                            break;
                        }
                    }
                    // evaluate the expressions
                    for (size_t p = 0; p < parsers.size(); p++)
                    {
                        parsers[p].Eval();
                    }
                    // read back the component variables
                    comp_var_index = 0;
                    for (uintmax_t i = 0; i < hdro.components(); i++)
                    {
                        switch (hdro.component_type(i))
                        {
                        case gta::int8:
                            {
                                int8_t v = comp_vars[comp_var_index++];
                                memcpy(hdri.component(element.ptr(), i), &v, sizeof(int8_t));
                            }
                            break;
                        case gta::uint8:
                            {
                                uint8_t v = comp_vars[comp_var_index++];
                                memcpy(hdri.component(element.ptr(), i), &v, sizeof(uint8_t));
                            }
                            break;
                        case gta::int16:
                            {
                                int16_t v = comp_vars[comp_var_index++];
                                memcpy(hdri.component(element.ptr(), i), &v, sizeof(int16_t));
                            }
                            break;
                        case gta::uint16:
                            {
                                uint16_t v = comp_vars[comp_var_index++];
                                memcpy(hdri.component(element.ptr(), i), &v, sizeof(uint16_t));
                            }
                            break;
                        case gta::int32:
                            {
                                int32_t v = comp_vars[comp_var_index++];
                                memcpy(hdri.component(element.ptr(), i), &v, sizeof(int32_t));
                            }
                            break;
                        case gta::uint32:
                            {
                                uint32_t v = comp_vars[comp_var_index++];
                                memcpy(hdri.component(element.ptr(), i), &v, sizeof(uint32_t));
                            }
                            break;
                        case gta::int64:
                            {
                                int64_t v = comp_vars[comp_var_index++];
                                memcpy(hdri.component(element.ptr(), i), &v, sizeof(int64_t));
                            }
                            break;
                        case gta::uint64:
                            {
                                uint64_t v = comp_vars[comp_var_index++];
                                memcpy(hdri.component(element.ptr(), i), &v, sizeof(uint64_t));
                            }
                            break;
                        case gta::float32:
                            {
                                float v = comp_vars[comp_var_index++];
                                memcpy(hdri.component(element.ptr(), i), &v, sizeof(float));
                            }
                            break;
                        case gta::float64:
                            {
                                memcpy(hdri.component(element.ptr(), i), &(comp_vars[comp_var_index++]), sizeof(double));
                            }
                            break;
                        case gta::cfloat32:
                            {
                                float v[2] = { comp_vars[comp_var_index], comp_vars[comp_var_index + 1] };
                                comp_var_index += 2;
                                memcpy(hdri.component(element.ptr(), i), v, 2 * sizeof(float));
                            }
                            break;
                        case gta::cfloat64:
                            {
                                memcpy(hdri.component(element.ptr(), i), &(comp_vars[comp_var_index]), 2 * sizeof(double));
                                comp_var_index += 2;
                            }
                            break;
                        default:
                            // cannot happen
                            break;
                        }
                    }
                    hdro.write_elements(so, gtatool_stdout, 1, element.ptr());
                }
                array_index++;
            }
            if (fi != gtatool_stdin)
            {
                cio::close(fi);
            }
            arg++;
        }
        while (arg < arguments.size());
    }
    catch (mu::Parser::exception_type &e)
    {
        msg::err_txt(e.GetMsg());
        return 1;
    }
    catch (std::exception &e)
    {
        msg::err_txt("%s", e.what());
        return 1;
    }

    return 0;
}
