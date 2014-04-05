#!/usr/bin/env gjs
""" This file is part of GEGL
 *
 * GEGL is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * GEGL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GEGL; if not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright 2014 Jon Nordby <jononor@gmail.com>
"""

const Gegl = imports.gi.Gegl;
const Lang = imports.lang;

function test_init() {
    Gegl.init();
}

function test_exit() {
    Gegl.exit();
}

function test_config() {
    var config = Gegl.config();
    if (config.quality != 1.0) {
        throw "FAILED";
    }
}
