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

#include "core/config/project_settings.h"
#include "core/object/class_db.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/decal.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/sprite_3d.h"
#include "scene/3d/uzu_look.h"
#include "scene/3d/world_environment.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/camera_attributes.h"
#include "scene/resources/compositor.h"
#include "scene/resources/environment.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/material.h"
#include "scene/resources/uzu_post_effect.h"
#include "scene/resources/uzu_tilt_shift.h"

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

TEST_CASE("[SceneTree][UZULook] God rays follow the sun") {
	Node3D *root = memnew(Node3D);
	DirectionalLight3D *sun = memnew(DirectionalLight3D);
	sun->set_rotation_degrees(Vector3(-50, 30, 0));
	sun->set_color(Color(1.0, 0.7, 0.4));
	root->add_child(sun);

	MeshInstance3D *rays = UZULook::add_god_rays(root, Vector3(1, 2, 3), 2.0, 6.0);
	REQUIRE(rays != nullptr);
	CHECK(rays->get_parent() == root);
	CHECK(rays->get_owner() == root);
	CHECK(rays->get_cast_shadows_setting() == GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	// The shaft (the node's Y axis) points back toward the sun, and passes through the position.
	const Vector3 toward_sun = sun->get_transform().basis.get_column(2).normalized();
	CHECK(rays->get_transform().basis.get_column(1).normalized().is_equal_approx(toward_sun));
	CHECK((rays->get_position() - toward_sun * 3.0).is_equal_approx(Vector3(1, 2, 3)));
	Ref<QuadMesh> quad = rays->get_mesh();
	REQUIRE(quad.is_valid());
	CHECK(quad->get_size().is_equal_approx(Vector2(2, 6)));
	CHECK(quad->get_center_offset().is_equal_approx(Vector3(0, -3, 0)));
	Ref<ShaderMaterial> material = quad->get_material();
	REQUIRE(material.is_valid());
	const String code = material->get_shader()->get_code();
	CHECK(code.contains("blend_add"));
	CHECK(code.contains("unshaded"));
	CHECK(code.contains("depth_draw_never"));
	CHECK(code.contains("side_view")); // Fades out when seen end-on.
	CHECK(float(material->get_shader_parameter("intensity")) == doctest::Approx(0.35));
	// The sun's color, softened toward white.
	CHECK(Color(material->get_shader_parameter("ray_color")).is_equal_approx(Color(1.0, 0.7, 0.4).lerp(Color(1, 1, 1), 0.3)));
	memdelete(root);
}

TEST_CASE("[SceneTree][UZULook] HD-2D in one call") {
	Node3D *root = memnew(Node3D);
	Camera3D *camera = memnew(Camera3D);
	camera->set_position(Vector3(0, 4, 9));
	camera->set_rotation_degrees(Vector3(-25, 0, 0));
	camera->set_fov(35.0);
	root->add_child(camera);
	Ref<Image> image = Image::create_empty(16, 24, false, Image::FORMAT_RGBA8);
	image->fill(Color(1, 0.5, 0.2, 1));
	Ref<ImageTexture> texture = ImageTexture::create_from_image(image);
	Sprite3D *sprites[3];
	for (int i = 0; i < 3; i++) {
		sprites[i] = memnew(Sprite3D);
		sprites[i]->set_texture(texture);
		sprites[i]->set_pixel_size(0.06);
		sprites[i]->set_position(Vector3(i - 1, 0.72, 0));
		root->add_child(sprites[i]);
	}
	sprites[1]->set_scale(Vector3(1.3, 1.3, 1.3)); // Out of line: bigger texels.
	Decal *saved_blob = memnew(Decal);
	saved_blob->set_name("BlobShadow");
	const uint32_t saved_ground_layers = 0b101;
	saved_blob->set_cull_mask(saved_ground_layers | (1u << 19));
	sprites[0]->add_child(saved_blob);
	const String filter_setting = "rendering/textures/canvas_textures/default_texture_filter";
	const Variant saved_filter = GLOBAL_GET(filter_setting);

	const Dictionary result = UZULook::apply_hd2d(root);

	// The look, with a little more glow, and the tilt-shift after the post effect.
	TypedArray<Node> environments = root->find_children("*", "WorldEnvironment", true, false);
	REQUIRE(environments.size() == 1);
	WorldEnvironment *world_environment = Object::cast_to<WorldEnvironment>(environments[0]);
	CHECK(world_environment->get_environment()->get_glow_bloom() == doctest::Approx(0.15));
	const TypedArray<CompositorEffect> effects = world_environment->get_compositor()->get_compositor_effects();
	REQUIRE(effects.size() >= 2);
	Ref<UZUTiltShift> tilt_shift = effects[effects.size() - 1];
	REQUIRE(tilt_shift.is_valid());
	CHECK(tilt_shift->get_max_blur() == doctest::Approx(10.0));

	// Sprites: lit, outlined, grounded, one texel size (the median), unscaled.
	CHECK(float(result["texel"]) == doctest::Approx(0.06));
	for (Sprite3D *sprite : sprites) {
		Ref<ShaderMaterial> material = sprite->get_material_override();
		REQUIRE(material.is_valid());
		CHECK(float(material->get_shader_parameter("outline_width")) == doctest::Approx(1.0));
		Decal *blob = Object::cast_to<Decal>(sprite->get_node_or_null(NodePath("BlobShadow")));
		REQUIRE(blob);
		CHECK(sprite->get_layer_mask() == (1u << 19));
		CHECK((blob->get_cull_mask() & sprite->get_layer_mask()) == 0);
		CHECK((blob->get_cull_mask() & 1u) != 0); // Ground on the default layer still receives the decal.
		CHECK(sprite->get_cast_shadows_setting() == GeometryInstance3D::SHADOW_CASTING_SETTING_ON);
		// B-726: a saved blob's stale geometry is repaired on re-apply too,
		// so these checks cover saved_blob as well.
		CHECK(blob->get_position().y == doctest::Approx(-0.72));
		CHECK(blob->get_size().x == doctest::Approx(0.768));
		CHECK(sprite->get_pixel_size() == doctest::Approx(0.06));
		CHECK(sprite->get_scale().is_equal_approx(Vector3(1, 1, 1)));
	}
	CHECK(sprites[0]->get_node_or_null(NodePath("BlobShadow")) == saved_blob);
	CHECK(saved_blob->get_cull_mask() == saved_ground_layers);
	CHECK(int(GLOBAL_GET(filter_setting)) == 0); // Nearest.
	ProjectSettings::get_singleton()->set_setting(filter_setting, saved_filter);

	// Camera rig: telephoto, 30 degrees down, still looking at the same ground point
	// (the old view met the ground at z = 9 - 4 / tan(25 degrees)), with the same framing.
	CHECK(camera->get_fov() == doctest::Approx(25.0));
	CHECK(camera->get_rotation_degrees().x == doctest::Approx(-30.0).epsilon(0.001));
	const Vector3 forward = -camera->get_transform().basis.get_column(2);
	const float t = camera->get_position().y / -forward.y;
	const Vector3 hit = camera->get_position() + forward * t;
	CHECK(hit.is_equal_approx(Vector3(0, 0, 9.0 - 4.0 / Math::tan(Math::deg_to_rad(25.0)))));
	const float old_distance = 4.0 / Math::sin(Math::deg_to_rad(25.0));
	CHECK(t * Math::tan(Math::deg_to_rad(12.5)) == doctest::Approx(old_distance * Math::tan(Math::deg_to_rad(17.5))));
	// The key light comes from behind the camera, 40 degrees to the side (sprites face the camera).
	TypedArray<Node> suns = root->find_children("Sun", "DirectionalLight3D", true, false);
	REQUIRE(suns.size() == 1);
	DirectionalLight3D *sun = Object::cast_to<DirectionalLight3D>(suns[0]);
	CHECK(Math::fposmod(sun->get_rotation_degrees().y - camera->get_rotation_degrees().y, 360.0f) == doctest::Approx(40.0).epsilon(0.001));
	CHECK(bool(result["sun_rotated"]));
	CHECK(sun->get_rotation_degrees().x < -10.0); // Still above the horizon.

	memdelete(root);
}

TEST_CASE("[SceneTree][UZULook] Blob shadow refreshes instead of stacking") {
	// B-726: a second add (as a re-apply makes) must reuse the saved decal
	// and repair its size and position, not stack a fresh one beside it.
	Ref<Image> image = Image::create_empty(16, 24, false, Image::FORMAT_RGBA8);
	image->fill(Color(1, 1, 1, 1));
	Ref<ImageTexture> texture = ImageTexture::create_from_image(image);
	Sprite3D *sprite = memnew(Sprite3D);
	sprite->set_texture(texture);
	sprite->set_pixel_size(0.06);

	Decal *first = UZULook::add_blob_shadow(sprite, 0.1);
	REQUIRE(first != nullptr);
	CHECK(first->get_size().x == doctest::Approx(0.2));
	Decal *second = UZULook::add_blob_shadow(sprite, 0.384);
	CHECK(second == first);
	CHECK(sprite->get_child_count() == 1);
	CHECK(first->get_size().x == doctest::Approx(0.768));
	CHECK(first->get_position().y == doctest::Approx(-0.72));

	memdelete(sprite);
}

TEST_CASE("[SceneTree][UZULook] HD-2D turns the sun to the camera even without the rig") {
	// B-724: without the camera rig, the scene's own camera still sets the key light,
	// so backlit sprites do not turn black.
	Node3D *root = memnew(Node3D);
	Camera3D *camera = memnew(Camera3D);
	camera->set_rotation_degrees(Vector3(-20, 70, 0));
	root->add_child(camera);
	const Dictionary result = UZULook::apply_hd2d(root, UZULook::ATMOSPHERE_GOLDEN_HOUR, false);
	CHECK(bool(result["sun_rotated"]));
	CHECK_FALSE(result.has("camera")); // The camera was not moved.
	CHECK(camera->get_rotation_degrees().is_equal_approx(Vector3(-20, 70, 0)));
	TypedArray<Node> suns = root->find_children("Sun", "DirectionalLight3D", true, false);
	REQUIRE(suns.size() == 1);
	const float yaw = Object::cast_to<DirectionalLight3D>(suns[0])->get_rotation_degrees().y;
	CHECK(Math::fposmod(yaw - 70.0f, 360.0f) == doctest::Approx(40.0).epsilon(0.001));
	memdelete(root);

	// Without any camera the sun stays as the atmosphere set it, and the result says so.
	Node3D *empty = memnew(Node3D);
	CHECK_FALSE(bool(UZULook::apply_hd2d(empty)["sun_rotated"]));
	memdelete(empty);
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
