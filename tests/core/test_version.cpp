/**************************************************************************/
/*  test_version.cpp                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_version)

#include "core/os/os.h"
#include "core/version.h"

namespace TestVersion {

TEST_CASE("[Version] Engine display name") {
	CHECK_MESSAGE(
			String(GODOT_VERSION_NAME) == "U.ZU Engine",
			"The engine display name (version.py `name`) should be U.ZU Engine.");
}

TEST_CASE("[Version] Short name is kept for compatibility") {
	// `short_name` determines the user data, editor settings and export template
	// directories, so it must stay "godot" to keep existing data reachable.
	CHECK_MESSAGE(
			String(GODOT_VERSION_SHORT_NAME) == "godot",
			"The engine short name (version.py `short_name`) should stay godot.");
	CHECK_MESSAGE(
			OS::get_singleton()->get_godot_dir_name().to_lower() == "godot",
			"The OS user data directory name should stay godot.");
}

} // namespace TestVersion
