/**************************************************************************/
/*  test_editor_file_system_exit.cpp                                      */
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

TEST_FORCE_LINK(test_editor_file_system_exit)

#ifdef TOOLS_ENABLED

#include "editor/editor_node.h"
#include "editor/file_system/editor_file_system.h"
#include "tests/test_tools.h"

namespace TestEditorFileSystemExit {

// U.ZU Engine (B-806). At the exit of --import, a late file system update ran after the
// editor node was freed: EditorNode::is_cmdline_mode() reported "Parameter singleton is
// null" and returned false, so the documentation update went on into freed editor parts
// and aborted (SIGABRT, intermittently). The editor-side updates now need the node.
TEST_CASE("[EditorFileSystem] Without the editor node, the editor-side updates are skipped quietly") {
	REQUIRE(EditorNode::get_singleton() == nullptr); // The state at the exit of --import.
	// No error is printed: the node is checked before is_cmdline_mode().
	ErrorDetector detector;
	CHECK_FALSE(EditorFileSystem::can_update_editor_data());
	CHECK_FALSE(detector.has_error);
	// The old check alone is the trap: it reports an error and answers "not command line".
	CHECK_FALSE(EditorNode::is_cmdline_mode());
	CHECK(detector.has_error);
}

} // namespace TestEditorFileSystemExit

#endif // TOOLS_ENABLED
