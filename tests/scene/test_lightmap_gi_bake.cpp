/**************************************************************************/
/*  test_lightmap_gi_bake.cpp                                             */
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

TEST_FORCE_LINK(test_lightmap_gi_bake)

#ifndef _3D_DISABLED

#include "core/io/resource_loader.h"
#include "scene/3d/lightmap_gi.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"

namespace TestLightmapGIBake {

static Error fake_import(const String &p_path) {
	return OK;
}

// U.ZU Engine (B-805): LightmapGI.bake() is callable from scripts. The bake itself needs a
// RenderingDevice and the editor, so these tests pin how the call is routed and guarded.
TEST_CASE("[SceneTree][LightmapGI] bake is bound and fails safely outside the editor") {
	CHECK(ClassDB::class_has_method("LightmapGI", "bake"));

	LightmapGI *lightmap = memnew(LightmapGI);
	ResourceLoaderImport saved_import = ResourceLoader::import;

	// Without the editor's import system: an error, not a crash.
	ResourceLoader::import = nullptr;
	ERR_PRINT_OFF;
	CHECK(int(lightmap->call("bake")) == LightmapGI::BAKE_ERROR_NO_LIGHTMAPPER);
	ERR_PRINT_ON;

	// With it (faked here): a node outside the tree cannot bake.
	ResourceLoader::import = fake_import;
	ERR_PRINT_OFF;
	CHECK(int(lightmap->call("bake")) == LightmapGI::BAKE_ERROR_NO_SCENE_ROOT);
	ERR_PRINT_ON;

	// Inside the tree the call reaches the real bake, which needs a save path first.
	SceneTree::get_singleton()->get_root()->add_child(lightmap);
	CHECK(int(lightmap->call("bake")) == LightmapGI::BAKE_ERROR_NO_SAVE_PATH);

	ResourceLoader::import = saved_import;
	memdelete(lightmap);
}

} // namespace TestLightmapGIBake

#endif // _3D_DISABLED
