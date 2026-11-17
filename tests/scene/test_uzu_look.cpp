/**************************************************************************/
/*  test_uzu_look.cpp                                                     */
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

TEST_FORCE_LINK(test_uzu_look)

#ifndef _3D_DISABLED

#include "core/object/class_db.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/uzu_look.h"
#include "scene/3d/world_environment.h"
#include "scene/resources/camera_attributes.h"
#include "scene/resources/compositor.h"
#include "scene/resources/environment.h"
#include "scene/resources/material.h"
#include "scene/resources/uzu_post_effect.h"

namespace TestUZULook {

TEST_CASE("[SceneTree][UZULook] Registered for scripts") {
	CHECK(ClassDB::class_exists("UZULook"));
	const MethodBind *method = ClassDB::get_method("UZULook", "get_lens_fov");
	REQUIRE(method != nullptr);
	CHECK(method->is_static());
	const Variant lens = UZULook::LENS_TELEPHOTO;
	const Variant *args[] = { &lens };
	Callable::CallError error;
	const Variant fov = method->call(nullptr, args, 1, error);
	CHECK(error.error == Callable::CallError::CALL_OK);
	CHECK(double(fov) == doctest::Approx(25.0));
	CHECK(ClassDB::get_integer_constant("UZULook", "ATMOSPHERE_HUMID") == UZULook::ATMOSPHERE_HUMID);
}

TEST_CASE("[SceneTree][UZULook] Apply the look to a scene") {
	Node3D *root = memnew(Node3D);

	Dictionary first = UZULook::apply_to_scene(root, UZULook::QUALITY_HIGH, UZULook::ATMOSPHERE_HUMID);
	CHECK(String(first["world_environment"]) == "added");
	CHECK(String(first["sun"]) == "added");
	WorldEnvironment *world_environment = Object::cast_to<WorldEnvironment>(root->get_node_or_null(NodePath("WorldEnvironment")));
	DirectionalLight3D *sun = Object::cast_to<DirectionalLight3D>(root->get_node_or_null(NodePath("Sun")));
	REQUIRE(world_environment != nullptr);
	REQUIRE(sun != nullptr);
	// Owned by the root, so the nodes are saved with the scene.
	CHECK(world_environment->get_owner() == root);
	CHECK(sun->get_owner() == root);
	CHECK(world_environment->get_environment()->get_fog_height_density() > 0.0);
	CHECK(world_environment->get_camera_attributes().is_valid());
	// Edge contrast and vignette at high quality.
	REQUIRE(world_environment->get_compositor().is_valid());
	TypedArray<CompositorEffect> effects = world_environment->get_compositor()->get_compositor_effects();
	REQUIRE(effects.size() == 1);
	CHECK(Object::cast_to<UZUPostEffect>(effects[0]) != nullptr);

	// Applying again updates the existing environment and reconfigures the
	// existing sun for the new atmosphere (a night look must not keep a day sun).
	Dictionary second = UZULook::apply_to_scene(root, UZULook::QUALITY_LOW, UZULook::ATMOSPHERE_NIGHT);
	CHECK(String(second["world_environment"]) == "updated");
	CHECK(String(second["sun"]) == "configured");
	CHECK(sun->get_param(Light3D::PARAM_ENERGY) < 0.5);
	CHECK(root->get_child_count() == 2);
	CHECK_FALSE(world_environment->get_environment()->is_ssao_enabled());
	// No post effect at low quality (the Compatibility renderer does not run it).
	CHECK(world_environment->get_compositor()->get_compositor_effects().is_empty());

	// Fill and rim lights do not count as a sun.
	Dictionary rig = UZULook::add_lighting_rig(root);
	CHECK(rig.has("fill_light"));
	CHECK(rig.has("rim_light"));
	CHECK(root->get_child_count() == 4);

	Dictionary lamp_result = UZULook::add_lamp(root, Vector3(1, 3, -2));
	OmniLight3D *lamp = Object::cast_to<OmniLight3D>(root->get_node_or_null(NodePath(String(lamp_result["lamp"]))));
	REQUIRE(lamp != nullptr);
	CHECK(lamp->get_position().is_equal_approx(Vector3(1, 3, -2)));
	CHECK(lamp->get_owner() == root);
	MeshInstance3D *bulb = Object::cast_to<MeshInstance3D>(lamp->get_node_or_null(NodePath("Bulb")));
	REQUIRE(bulb != nullptr);
	CHECK(bulb->get_owner() == root);
	Ref<StandardMaterial3D> bulb_material = bulb->get_material_override();
	REQUIRE(bulb_material.is_valid());
	CHECK(bulb_material->get_feature(BaseMaterial3D::FEATURE_EMISSION));

	memdelete(root);
}

} // namespace TestUZULook

#endif // _3D_DISABLED
