/**************************************************************************/
/*  uzu_look.cpp                                                          */
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

#include "uzu_look.h"

#include "core/config/project_settings.h"
#include "core/object/class_db.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/decal.h"
#include "scene/3d/gpu_particles_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/sprite_3d.h"
#include "scene/3d/world_environment.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/camera_attributes.h"
#include "scene/resources/compositor.h"
#include "scene/resources/environment.h"
#include "scene/resources/material.h"
#include "scene/resources/uzu_tilt_shift.h"

UZULook::Quality UZULook::get_default_quality(const String &p_rendering_method) {
	return Quality(LookPreset3D::get_default_quality(p_rendering_method));
}

Ref<Environment> UZULook::create_environment(Quality p_quality, Atmosphere p_atmosphere, bool p_clouds) {
	return LookPreset3D::create_environment(LookPreset3D::Quality(p_quality), LookPreset3D::Atmosphere(p_atmosphere), p_clouds);
}

Ref<CameraAttributesPractical> UZULook::create_camera_attributes(Quality p_quality) {
	return LookPreset3D::create_camera_attributes(LookPreset3D::Quality(p_quality));
}

void UZULook::configure_sun(DirectionalLight3D *p_sun, Quality p_quality, Atmosphere p_atmosphere) {
	LookPreset3D::configure_sun(p_sun, LookPreset3D::Quality(p_quality), LookPreset3D::Atmosphere(p_atmosphere));
}

void UZULook::configure_fill_light(DirectionalLight3D *p_light, Atmosphere p_atmosphere) {
	LookPreset3D::configure_fill_light(p_light, LookPreset3D::Atmosphere(p_atmosphere));
}

void UZULook::configure_rim_light(DirectionalLight3D *p_light, Atmosphere p_atmosphere) {
	LookPreset3D::configure_rim_light(p_light, LookPreset3D::Atmosphere(p_atmosphere));
}

void UZULook::configure_lamp(OmniLight3D *p_lamp, Quality p_quality) {
	LookPreset3D::configure_lamp(p_lamp, LookPreset3D::Quality(p_quality));
}

Ref<StandardMaterial3D> UZULook::create_material(MaterialKind p_kind) {
	return LookPreset3D::create_material(LookPreset3D::MaterialKind(p_kind));
}

void UZULook::configure_camera(Camera3D *p_camera, Lens p_lens, float p_shift) {
	LookPreset3D::configure_camera(p_camera, LookPreset3D::Lens(p_lens), p_shift);
}

float UZULook::get_lens_fov(Lens p_lens) {
	return LookPreset3D::get_lens_fov(LookPreset3D::Lens(p_lens));
}

int UZULook::get_shadow_filter_quality(Quality p_quality) {
	return LookPreset3D::get_shadow_filter_quality(LookPreset3D::Quality(p_quality));
}

Ref<ShaderMaterial> UZULook::create_lit_sprite_material(const Ref<Texture2D> &p_texture, const Ref<Texture2D> &p_normal_map, float p_outline_width, const Color &p_outline_color) {
	return LookPreset3D::create_lit_sprite_material(p_texture, p_normal_map, p_outline_width, p_outline_color);
}

void UZULook::configure_lit_sprite(Sprite3D *p_sprite, const Ref<Texture2D> &p_normal_map, float p_outline_width) {
	LookPreset3D::configure_lit_sprite(p_sprite, p_normal_map, p_outline_width);
}

Decal *UZULook::add_blob_shadow(Node3D *p_node, float p_radius) {
	ERR_FAIL_NULL_V(p_node, nullptr);
	Decal *decal = LookPreset3D::create_blob_shadow(p_radius);
	// At the feet: the bottom of the node's visual bounds (a centered sprite's lower edge).
	VisualInstance3D *visual = Object::cast_to<VisualInstance3D>(p_node);
	const float bottom = visual ? visual->get_aabb().position.y : 0.0f;
	decal->set_position(Vector3(0.0, bottom, 0.0));
	p_node->add_child(decal, true);
	Node *owner = p_node->get_owner() ? p_node->get_owner() : p_node;
	if (owner != decal) {
		decal->set_owner(owner);
	}
	return decal;
}

Ref<ShaderMaterial> UZULook::create_wind_foliage_material(float p_strength, float p_speed) {
	return LookPreset3D::create_wind_foliage_material(p_strength, p_speed);
}

GPUParticles3D *UZULook::create_particles(ParticleKind p_kind, const Vector3 &p_extents) {
	return LookPreset3D::create_particles(LookPreset3D::ParticleKind(p_kind), p_extents);
}

MeshInstance3D *UZULook::add_god_rays(Node *p_root, const Vector3 &p_position, float p_width, float p_length) {
	ERR_FAIL_NULL_V(p_root, nullptr);
	// The sun's color, and the shaft along its light (from the sky down through the position).
	DirectionalLight3D *sun = nullptr;
	TypedArray<Node> lights = p_root->find_children("*", "DirectionalLight3D", true, false);
	for (const Variant &v : lights) {
		DirectionalLight3D *light = Object::cast_to<DirectionalLight3D>(v);
		if (light && light->get_sky_mode() != DirectionalLight3D::SKY_MODE_LIGHT_ONLY) {
			sun = light;
			break;
		}
	}
	const Color color = sun ? sun->get_color().lerp(Color(1, 1, 1), 0.3) : Color(1.0, 0.86, 0.62);
	MeshInstance3D *rays = LookPreset3D::create_god_rays(p_width, p_length, color);
	Vector3 light_direction = Vector3(0, -1, 0);
	if (sun) {
		// Where the light travels (in the scene's space; outside the tree, the sun's own transform).
		const Transform3D sun_transform = sun->is_inside_tree() ? sun->get_global_transform() : sun->get_transform();
		light_direction = -sun_transform.basis.get_column(2).normalized();
	}
	// The shaft starts up toward the sun and runs down along the light through p_position.
	const Vector3 up = -light_direction;
	const Vector3 side = Math::abs(up.dot(Vector3(0, 0, 1))) < 0.99f ? up.cross(Vector3(0, 0, 1)).normalized() : up.cross(Vector3(1, 0, 0)).normalized();
	Basis basis(side, up, side.cross(up));
	rays->set_transform(Transform3D(basis, p_position + up * p_length * 0.5f));
	p_root->add_child(rays, true);
	rays->set_owner(p_root->get_owner() ? p_root->get_owner() : p_root);
	return rays;
}

// HD-2D (B-722): pixel-art sprites in a lit 3D diorama, in one call.
Dictionary UZULook::apply_hd2d(Node *p_root, Atmosphere p_atmosphere, bool p_camera) {
	Dictionary result;
	ERR_FAIL_NULL_V(p_root, result);

	// 1. The look: light, fog, bloom, color grade, sun; a little more glow for the
	// soft, glowing highlights of the style.
	result["look"] = apply_to_scene(p_root, QUALITY_HIGH, p_atmosphere);
	WorldEnvironment *world_environment = nullptr;
	TypedArray<Node> environments = p_root->find_children("*", "WorldEnvironment", true, false);
	if (Object::cast_to<WorldEnvironment>(p_root)) {
		environments.push_front(p_root);
	}
	if (!environments.is_empty()) {
		world_environment = Object::cast_to<WorldEnvironment>(environments[0]);
	}
	ERR_FAIL_NULL_V(world_environment, result);
	Ref<Environment> environment = world_environment->get_environment();
	if (environment.is_valid()) {
		environment->set_glow_intensity(environment->get_glow_intensity() * 1.5);
		environment->set_glow_bloom(0.15);
	}

	// 2. Tilt-shift band blur after the look's post effect: the diorama feel.
	Ref<Compositor> compositor = world_environment->get_compositor();
	if (compositor.is_null()) {
		compositor.instantiate();
		world_environment->set_compositor(compositor);
	}
	TypedArray<CompositorEffect> effects = compositor->get_compositor_effects();
	Ref<UZUTiltShift> tilt_shift;
	tilt_shift.instantiate();
	tilt_shift->set_focus_center(0.55);
	tilt_shift->set_focus_width(0.1);
	tilt_shift->set_transition(0.22);
	tilt_shift->set_max_blur(10.0);
	effects.push_back(tilt_shift);
	compositor->set_compositor_effects(effects);

	// 3. Sprites: lit, outlined, grounded with a blob shadow, all with one texel size
	// (the median: the most common size wins), crisp.
	TypedArray<Node> sprites = p_root->find_children("*", "SpriteBase3D", true, false);
	Vector<float> texels;
	for (const Variant &v : sprites) {
		SpriteBase3D *sprite = Object::cast_to<SpriteBase3D>(v);
		const Vector3 scale = sprite->get_scale().abs();
		texels.push_back(sprite->get_pixel_size() * MAX(scale.x, MAX(scale.y, scale.z)));
	}
	texels.sort();
	const float texel = texels.is_empty() ? 0.0f : texels[texels.size() / 2];
	Array sprite_paths;
	for (const Variant &v : sprites) {
		SpriteBase3D *sprite = Object::cast_to<SpriteBase3D>(v);
		sprite->set_pixel_size(texel);
		sprite->set_scale(Vector3(1, 1, 1));
		Sprite3D *sprite_3d = Object::cast_to<Sprite3D>(sprite);
		if (sprite_3d) {
			configure_lit_sprite(sprite_3d, Ref<Texture2D>(), 1.0);
		} else {
			sprite->set_texture_filter(StandardMaterial3D::TEXTURE_FILTER_NEAREST);
			sprite->set_draw_flag(SpriteBase3D::FLAG_SHADED, true);
			sprite->set_alpha_cut_mode(SpriteBase3D::ALPHA_CUT_DISCARD);
			sprite->set_layer_mask(LookPreset3D::LIT_SPRITE_LAYER_MASK);
		}
		Node *blob = sprite->get_node_or_null(NodePath("BlobShadow"));
		if (!blob) {
			add_blob_shadow(sprite, MAX(sprite->get_aabb().size.x * 0.4f, 0.1f));
		} else if (Decal *decal = Object::cast_to<Decal>(blob)) {
			decal->set_cull_mask(decal->get_cull_mask() & ~LookPreset3D::LIT_SPRITE_LAYER_MASK);
		}
		sprite_paths.push_back(String(p_root->get_path_to(sprite)));
	}
	result["sprites"] = sprite_paths;
	result["texel"] = texel;
	ProjectSettings::get_singleton()->set_setting("rendering/textures/canvas_textures/default_texture_filter", 0); // Nearest.

	// 4. Camera rig: a telephoto lens looking down at 30 degrees, keeping the point
	// the camera looked at (on the ground, or 10 m ahead) and its framing size.
	Camera3D *camera = nullptr;
	TypedArray<Node> cameras = p_root->find_children("*", "Camera3D", true, false);
	for (const Variant &v : cameras) {
		Camera3D *c = Object::cast_to<Camera3D>(v);
		if (!camera || c->is_current()) {
			camera = c;
		}
	}
	if (camera && p_camera) {
		const Transform3D old_transform = camera->get_transform();
		const Vector3 forward = -old_transform.basis.get_column(2).normalized();
		float distance = 10.0;
		if (forward.y < -0.01f && old_transform.origin.y > 0.0f) {
			distance = old_transform.origin.y / -forward.y; // Where the view meets the ground (y = 0).
		}
		const Vector3 target = old_transform.origin + forward * distance;
		const float old_fov = camera->get_fov();
		const float fov = get_lens_fov(LENS_TELEPHOTO);
		// Same framing: the view width at the target stays the same with the narrower lens.
		const float new_distance = distance * Math::tan(Math::deg_to_rad(old_fov) * 0.5f) / Math::tan(Math::deg_to_rad(fov) * 0.5f);
		const float yaw = Math::atan2(-forward.x, -forward.z);
		const float pitch = Math::deg_to_rad(-30.0f);
		const Vector3 direction = Vector3(-Math::sin(yaw) * Math::cos(pitch), Math::sin(pitch), -Math::cos(yaw) * Math::cos(pitch));
		Transform3D transform;
		transform.origin = target - direction * new_distance;
		transform = transform.looking_at(target, Vector3(0, 1, 0));
		camera->set_transform(transform);
		configure_camera(camera, LENS_TELEPHOTO);
		Dictionary rig;
		rig["camera"] = String(p_root->get_path_to(camera));
		rig["fov"] = fov;
		rig["pitch"] = -30.0;
		rig["target"] = target;
		result["camera"] = rig;
	}

	// 5. Key light: sprites are cards facing the camera, so the sun comes from behind
	// the camera, 40 degrees to the side, with the atmosphere's elevation. This uses the
	// scene's camera whether or not the rig moved it (B-724); without a camera the sun
	// stays as the atmosphere set it.
	bool sun_rotated = false;
	if (camera) {
		const float camera_yaw = camera->get_rotation_degrees().y;
		TypedArray<Node> lights = p_root->find_children("*", "DirectionalLight3D", true, false);
		for (const Variant &v : lights) {
			DirectionalLight3D *sun = Object::cast_to<DirectionalLight3D>(v);
			if (sun && sun->has_shadow() && sun->get_sky_mode() != DirectionalLight3D::SKY_MODE_LIGHT_ONLY) {
				const Vector3 rotation = sun->get_rotation_degrees();
				sun->set_rotation_degrees(Vector3(rotation.x, camera_yaw + 40.0, rotation.z));
				sun_rotated = true;
				break;
			}
		}
	}
	result["sun_rotated"] = sun_rotated;
	return result;
}

float UZULook::get_eye_height(EyeLevel p_eye_level) {
	return LookPreset3D::get_eye_height(LookPreset3D::EyeLevel(p_eye_level));
}

static void _add_owned(Node *p_root, Node *p_node, const String &p_name) {
	p_node->set_name(p_name);
	p_root->add_child(p_node, true);
	p_node->set_owner(p_root);
}

Dictionary UZULook::apply_to_scene(Node *p_root, Quality p_quality, Atmosphere p_atmosphere, bool p_clouds) {
	Dictionary result;
	ERR_FAIL_NULL_V(p_root, result);

	WorldEnvironment *world_environment = nullptr;
	DirectionalLight3D *sun = nullptr;
	TypedArray<Node> nodes = p_root->find_children("*", "", true, false);
	nodes.push_front(p_root);
	for (const Variant &v : nodes) {
		Node *node = Object::cast_to<Node>(v);
		if (!world_environment) {
			world_environment = Object::cast_to<WorldEnvironment>(node);
		}
		DirectionalLight3D *light = Object::cast_to<DirectionalLight3D>(node);
		if (!sun && light && light->get_sky_mode() != DirectionalLight3D::SKY_MODE_LIGHT_ONLY) {
			sun = light;
		}
	}

	if (world_environment) {
		result["world_environment"] = "updated";
	} else {
		world_environment = memnew(WorldEnvironment);
		_add_owned(p_root, world_environment, "WorldEnvironment");
		p_root->move_child(world_environment, 0);
		result["world_environment"] = "added";
	}
	world_environment->set_environment(create_environment(p_quality, p_atmosphere, p_clouds));
	world_environment->set_camera_attributes(create_camera_attributes(p_quality));
	world_environment->set_compositor(LookPreset3D::create_compositor(LookPreset3D::Quality(p_quality)));

	// The sun belongs to the atmosphere (its direction, color and strength), so
	// an existing one is configured too: otherwise a night look keeps a day sun.
	if (sun) {
		configure_sun(sun, p_quality, p_atmosphere);
		result["sun"] = "configured";
	} else {
		sun = memnew(DirectionalLight3D);
		configure_sun(sun, p_quality, p_atmosphere);
		_add_owned(p_root, sun, "Sun");
		p_root->move_child(sun, 1);
		result["sun"] = "added";
	}
	// The sun's filter quality is a project setting (B-714); callers that save the
	// scene should save the project settings too (ai_loop/look_tool.gd does).
	LookPreset3D::apply_shadow_filter_quality(LookPreset3D::Quality(p_quality));
	result["shadow_filter_quality"] = LookPreset3D::get_shadow_filter_quality(LookPreset3D::Quality(p_quality));
	return result;
}

Dictionary UZULook::add_lighting_rig(Node *p_root, Atmosphere p_atmosphere) {
	Dictionary result;
	ERR_FAIL_NULL_V(p_root, result);
	DirectionalLight3D *fill = memnew(DirectionalLight3D);
	configure_fill_light(fill, p_atmosphere);
	_add_owned(p_root, fill, "FillLight");
	DirectionalLight3D *rim = memnew(DirectionalLight3D);
	configure_rim_light(rim, p_atmosphere);
	_add_owned(p_root, rim, "RimLight");
	result["fill_light"] = String(p_root->get_path_to(fill));
	result["rim_light"] = String(p_root->get_path_to(rim));
	return result;
}

Dictionary UZULook::add_lamp(Node *p_root, const Vector3 &p_position, Quality p_quality) {
	Dictionary result;
	ERR_FAIL_NULL_V(p_root, result);
	OmniLight3D *lamp = memnew(OmniLight3D);
	configure_lamp(lamp, p_quality);
	_add_owned(p_root, lamp, "Lamp");
	lamp->set_position(p_position);

	MeshInstance3D *bulb = memnew(MeshInstance3D);
	Ref<SphereMesh> mesh;
	mesh.instantiate();
	mesh->set_radius(0.12);
	mesh->set_height(0.24);
	bulb->set_mesh(mesh);
	bulb->set_material_override(create_material(MATERIAL_LAMP));
	// The bulb must not block its own light.
	bulb->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	lamp->add_child(bulb, true);
	bulb->set_name("Bulb");
	bulb->set_owner(p_root);

	result["lamp"] = String(p_root->get_path_to(lamp));
	return result;
}

void UZULook::_bind_methods() {
	ClassDB::bind_static_method("UZULook", D_METHOD("get_default_quality", "rendering_method"), &UZULook::get_default_quality);
	ClassDB::bind_static_method("UZULook", D_METHOD("get_shadow_filter_quality", "quality"), &UZULook::get_shadow_filter_quality, DEFVAL(QUALITY_HIGH));
	ClassDB::bind_static_method("UZULook", D_METHOD("create_lit_sprite_material", "texture", "normal_map", "outline_width", "outline_color"), &UZULook::create_lit_sprite_material, DEFVAL(Variant()), DEFVAL(0.0), DEFVAL(Color(0.06, 0.05, 0.08)));
	ClassDB::bind_static_method("UZULook", D_METHOD("configure_lit_sprite", "sprite", "normal_map", "outline_width"), &UZULook::configure_lit_sprite, DEFVAL(Variant()), DEFVAL(0.0));
	ClassDB::bind_static_method("UZULook", D_METHOD("add_blob_shadow", "node", "radius"), &UZULook::add_blob_shadow, DEFVAL(0.4));
	ClassDB::bind_static_method("UZULook", D_METHOD("create_wind_foliage_material", "strength", "speed"), &UZULook::create_wind_foliage_material, DEFVAL(0.12), DEFVAL(1.2));
	ClassDB::bind_static_method("UZULook", D_METHOD("create_particles", "kind", "extents"), &UZULook::create_particles, DEFVAL(Vector3(4, 2, 4)));
	ClassDB::bind_static_method("UZULook", D_METHOD("add_god_rays", "root", "position", "width", "length"), &UZULook::add_god_rays, DEFVAL(3.0), DEFVAL(8.0));
	ClassDB::bind_static_method("UZULook", D_METHOD("apply_hd2d", "root", "atmosphere", "camera"), &UZULook::apply_hd2d, DEFVAL(ATMOSPHERE_GOLDEN_HOUR), DEFVAL(true));
	ClassDB::bind_static_method("UZULook", D_METHOD("create_environment", "quality", "atmosphere", "clouds"), &UZULook::create_environment, DEFVAL(QUALITY_HIGH), DEFVAL(ATMOSPHERE_CLEAR), DEFVAL(false));
	ClassDB::bind_static_method("UZULook", D_METHOD("create_camera_attributes", "quality"), &UZULook::create_camera_attributes, DEFVAL(QUALITY_HIGH));
	ClassDB::bind_static_method("UZULook", D_METHOD("configure_sun", "sun", "quality", "atmosphere"), &UZULook::configure_sun, DEFVAL(QUALITY_HIGH), DEFVAL(ATMOSPHERE_CLEAR));
	ClassDB::bind_static_method("UZULook", D_METHOD("configure_fill_light", "light", "atmosphere"), &UZULook::configure_fill_light, DEFVAL(ATMOSPHERE_CLEAR));
	ClassDB::bind_static_method("UZULook", D_METHOD("configure_rim_light", "light", "atmosphere"), &UZULook::configure_rim_light, DEFVAL(ATMOSPHERE_CLEAR));
	ClassDB::bind_static_method("UZULook", D_METHOD("configure_lamp", "lamp", "quality"), &UZULook::configure_lamp, DEFVAL(QUALITY_HIGH));
	ClassDB::bind_static_method("UZULook", D_METHOD("create_material", "kind"), &UZULook::create_material);
	ClassDB::bind_static_method("UZULook", D_METHOD("configure_camera", "camera", "lens", "shift"), &UZULook::configure_camera, DEFVAL(LENS_STANDARD), DEFVAL(0.0));
	ClassDB::bind_static_method("UZULook", D_METHOD("get_lens_fov", "lens"), &UZULook::get_lens_fov);
	ClassDB::bind_static_method("UZULook", D_METHOD("get_eye_height", "eye_level"), &UZULook::get_eye_height);
	ClassDB::bind_static_method("UZULook", D_METHOD("apply_to_scene", "root", "quality", "atmosphere", "clouds"), &UZULook::apply_to_scene, DEFVAL(QUALITY_HIGH), DEFVAL(ATMOSPHERE_CLEAR), DEFVAL(false));
	ClassDB::bind_static_method("UZULook", D_METHOD("add_lighting_rig", "root", "atmosphere"), &UZULook::add_lighting_rig, DEFVAL(ATMOSPHERE_CLEAR));
	ClassDB::bind_static_method("UZULook", D_METHOD("add_lamp", "root", "position", "quality"), &UZULook::add_lamp, DEFVAL(QUALITY_HIGH));

	BIND_ENUM_CONSTANT(QUALITY_HIGH);
	BIND_ENUM_CONSTANT(QUALITY_STANDARD);
	BIND_ENUM_CONSTANT(QUALITY_LOW);
	BIND_ENUM_CONSTANT(ATMOSPHERE_CLEAR);
	BIND_ENUM_CONSTANT(ATMOSPHERE_HUMID);
	BIND_ENUM_CONSTANT(ATMOSPHERE_GOLDEN_HOUR);
	BIND_ENUM_CONSTANT(ATMOSPHERE_NIGHT);
	BIND_ENUM_CONSTANT(MATERIAL_SKIN);
	BIND_ENUM_CONSTANT(MATERIAL_FOLIAGE);
	BIND_ENUM_CONSTANT(MATERIAL_GLASS);
	BIND_ENUM_CONSTANT(MATERIAL_WATER);
	BIND_ENUM_CONSTANT(MATERIAL_LAMP);
	BIND_ENUM_CONSTANT(LENS_WIDE);
	BIND_ENUM_CONSTANT(LENS_STANDARD);
	BIND_ENUM_CONSTANT(LENS_TELEPHOTO);
	BIND_ENUM_CONSTANT(EYE_LEVEL_STANDING);
	BIND_ENUM_CONSTANT(EYE_LEVEL_LOW);

	BIND_ENUM_CONSTANT(PARTICLES_DUST);
	BIND_ENUM_CONSTANT(PARTICLES_FIREFLIES);
	BIND_ENUM_CONSTANT(PARTICLES_SNOW);
	BIND_ENUM_CONSTANT(PARTICLES_EMBERS);
}
