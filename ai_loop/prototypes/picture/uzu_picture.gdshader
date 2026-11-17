// U.ZU Picture relief: the picture as a PBR surface.
// EMISSION keeps the picture's own (painted) light; ALBEDO / NORMAL / ROUGHNESS let real lights
// add highlights on top: lamps on wet stone, moon glints, a raking light over carvings.
shader_type spatial;
render_mode cull_disabled;

uniform sampler2D art : source_color, filter_linear_mipmap, repeat_disable;
uniform sampler2D normal_tex : hint_normal, filter_linear_mipmap, repeat_disable;
uniform bool has_normal = false;
uniform float painted_light = 1.0;   // how much of the picture's own light is kept
uniform float relight = 0.55;        // how strongly real lights act on the picture
uniform bool backdrop = false;       // the layer behind torn edges: slightly darker and softer
uniform float flash = 0.0;           // lightning, 0..1+
uniform float cloud_amount = 0.0;    // extra drifting cloud layers over the sky region
uniform float cloud_speed = 1.0;
uniform vec2 cloud_dir = vec2(1.0, 0.0);

uniform sampler2D r_glow : filter_linear, repeat_disable;
uniform vec4 c_glow;
uniform sampler2D r_wet : filter_linear, repeat_disable;
uniform vec4 c_wet;
uniform sampler2D r_sky : filter_linear, repeat_disable;
uniform vec4 c_sky;
uniform sampler2D r_flicker : filter_linear, repeat_disable;
uniform vec4 c_flicker;
uniform sampler2D r_waterfall : filter_linear, repeat_disable;
uniform vec4 c_waterfall;
uniform sampler2D r_sway : filter_linear, repeat_disable;
uniform vec4 c_sway;
uniform sampler2D r_water : filter_linear, repeat_disable;
uniform vec4 c_water;

float h21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vn(vec2 p) { vec2 i = floor(p), f = fract(p); f = f * f * (3. - 2. * f);
	return mix(mix(h21(i), h21(i + vec2(1, 0)), f.x), mix(h21(i + vec2(0, 1)), h21(i + vec2(1, 1)), f.x), f.y); }
float fbm(vec2 p) { float s = 0., a = .5; for (int i = 0; i < 4; i++) { s += a * vn(p); p = p * 2.03 + 17.1; a *= .5; } return s; }

float region(sampler2D t, vec4 c, vec2 uv) { return dot(texture(t, uv), c); }

void vertex() {
	// Banners and foliage sway; the amount grows with distance so it reads the same on screen.
	float b = region(r_sway, c_sway, UV);
	float z = -VERTEX.z;
	VERTEX.x += sin(TIME * 1.7 + UV.y * 28. + UV.x * 40.) * .0045 * z * b;
	VERTEX.z += cos(TIME * 1.3 + UV.y * 22.) * .01 * z * b;
}

void fragment() {
	vec2 uv = UV;
	float t = TIME;
	float sky = region(r_sky, c_sky, uv);
	float wet = region(r_wet, c_wet, uv);
	float water = region(r_water, c_water, uv);
	float fall = region(r_waterfall, c_waterfall, uv);
	// clouds churn, water ripples, waterfalls pull downward
	uv += (vec2(fbm(uv * vec2(3., 5.) + vec2(t * .015, 0.)), fbm(uv * vec2(4., 3.) - vec2(t * .01, t * .004))) - .5) * .014 * sky;
	uv.x += (vn(uv * vec2(60., 260.) + vec2(0., t * .9)) - .5) * .004 * (water + wet * .35);
	uv.y += (vn(uv * vec2(160., 20.) - vec2(0., t * 2.2)) - .5) * .004 * fall;
	vec3 alb = texture(art, uv).rgb;
	// Clouds: the painted clouds drift (two-phase flow, so they never stretch far), and two
	// procedural layers at different speeds roll over them; the far layer is slower.
	float cl = 0.0;
	// No branch here: texture reads inside a per-pixel branch break mip selection (blocky sky).
	{
		float sp = 0.006 * cloud_speed;
		float p0 = fract(t * 0.02), p1 = fract(t * 0.02 + 0.5);
		vec2 o = cloud_dir * 0.6;
		vec3 a0 = textureLod(art, uv - o * sp * 50.0 * p0, 0.0).rgb;
		vec3 a1 = textureLod(art, uv - o * sp * 50.0 * p1, 0.0).rgb;
		float w0 = 1.0 - abs(2.0 * p0 - 1.0);
		vec3 drift = (a0 * w0 + a1 * (1.0 - w0));
		alb = mix(alb, drift, sky);
		float near_l = fbm(uv * vec2(3.0, 7.0) + cloud_dir * t * sp * 3.0);
		float far_l = fbm(uv * vec2(6.0, 12.0) + cloud_dir * t * sp * 1.2 + 9.0);
		cl = smoothstep(0.45, 0.9, near_l) * 0.65 + smoothstep(0.5, 0.95, far_l) * 0.35;
		// Dark rolling cloud bodies with a lit rim from the sky glow behind them.
		vec3 cloud_col = mix(vec3(0.03, 0.04, 0.06), alb * 1.25 + vec3(0.02, 0.025, 0.035), smoothstep(0.55, 0.85, near_l));
		alb = mix(alb, cloud_col, cl * cloud_amount * sky);
	}

	vec3 painted = alb * mix(1., .78 + .42 * fbm(uv * vec2(4., 7.) + vec2(t * .02, t * .004)), sky);
	// Lightning: clouds light up from inside, everything else catches a cold flash.
	painted += flash * (sky * vec3(0.5, 0.58, 0.78) * (0.6 + 1.6 * cl) + alb * 0.35);
	painted += vec3(.8, .86, .95) * smoothstep(.55, 1., vn(vec2(UV.x * 420., UV.y * 30. - t * 2.6))) * fall * .25;
	float fl = region(r_flicker, c_flicker, UV);
	painted *= 1. + fl * (vn(UV * vec2(110., 110.) + vec2(t * .6, t * .35)) - .5) * .55;
	float glow = region(r_glow, c_glow, UV);
	painted += alb * glow * (.6 * (vn(UV * 35. + vec2(t * 1.7, 0.)) - .5) + .5);
	float drop = step(.992, h21(floor(UV * vec2(900., 500.)) + floor(t * 9.)));
	painted += vec3(.75, .8, .9) * drop * wet * .4;

	if (backdrop) {
		painted *= 0.8;
	}
	EMISSION = painted * painted_light;
	ALBEDO = alb * relight * (1. - sky);
	if (has_normal) {
		vec3 n = texture(normal_tex, UV).rgb * 2. - 1.;
		vec3 nw = normalize(vec3(n.xy, n.z * 1.4));
		NORMAL = normalize((VIEW_MATRIX * vec4(nw, 0.)).xyz);
	}
	ROUGHNESS = mix(.92, .12, clamp(wet + water, 0., 1.));
	SPECULAR = mix(.2, .6, clamp(wet + water, 0., 1.));
	METALLIC = 0.;
}
