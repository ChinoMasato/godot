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

#include "core/object/class_db.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/world_environment.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/camera_attributes.h"
#include "scene/resources/environment.h"
#include "scene/resources/material.h"

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
}
