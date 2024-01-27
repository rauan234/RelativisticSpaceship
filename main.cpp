#define COW_PATCH_FRAMERATE
// #define COW_PATCH_FRAMERATE_SLEEP
#include "include.cpp"
#include <vector>

using namespace std;

#include <chrono>
#include <thread>

using namespace std::this_thread;
using namespace std::chrono; // nanoseconds, system_clock, seconds


//  trivial functions and constants
const mat3 _Identity3x3 = IdentityMatrix<3>();
mat4 M4(mat3 M) {
	return M4(
		M(0, 0), M(0, 1), M(0, 2), 0,
		M(1, 0), M(1, 1), M(1, 2), 0,
		M(2, 0), M(2, 1), M(2, 2), 0,
		0,       0,       0,       1
	);
}
// @pre: norm(v) == 1
mat3 proj_mat(vec3 v) {
	return M3(
		v[0] * v[0], v[0] * v[1], v[0] * v[2],
		v[1] * v[0], v[1] * v[1], v[1] * v[2],
		v[2] * v[0], v[2] * v[1], v[2] * v[2]
	);
}
mat3 cross_prod_mat(vec3 v) {
	return M3(
		0, -v[2], v[1],
		v[2], 0, -v[0],
		-v[1], v[0], 0
	);
}
int int_pow(int a, int b) {  // a^b, where b >= 0
	int out = 1;
	for(int i = 0; i < b; i++) {
		out *= a;
	}
	return out;
}
vec3 reflect(vec3 v, vec3 n) {  // reflect a vector v about a unit vector n
	return -v + 2 * n * dot(n, v);
}
real angular_distance(vec3 a, vec3 b) {
	return acos(dot(a, b) / (norm(a) * norm(b)));
}



const real WAVELENGTH_MIN = 1;
const real WAVELENGTH_MAX = 200;
#define WAVELENGTH_CONVERT(x)  WAVELENGTH_MIN + ((x) - 380.0) / (780.0 - 380.0) * (WAVELENGTH_MAX - WAVELENGTH_MIN)

struct MonoColor {  // represent a color of a single wavelength
	real wavelength;
	real intensity;
};
vec3 mc_to_rgb(MonoColor color, real D) {  // D is the Doppler factor
	real l = color.wavelength * D;
	l = 380 + 400 * (l - WAVELENGTH_MIN) / (WAVELENGTH_MAX - WAVELENGTH_MIN);
	real i = color.intensity * (0.25 + 0.75 / (D * D));  // not physically realistic;
									// included the +0.25 term to make redshifted things visible even at high speeds
	return {
		tanh(i * (4860.0 / (900.0 + (l - 605) * (l - 605)) -
									937.5 / (625.0 + (l - 505) * (l - 505)) +
									135.0 / (225.0 + (l - 435) * (l - 435)))),
		tanh(i * (249.9 - 0.0204 * (l - 530) * (l - 530)) / (49.0 + 0.04 * (l - 540) * (l - 540))),
		tanh(i * (9.25 * l - 3469.0) / (125.0 + 0.008 * abs(l - 440) * abs(l - 440) * abs(l - 440)))
	};
}

const MonoColor MC_BLACK = { 1.0, 0.0 };
const MonoColor MC_RED = { WAVELENGTH_CONVERT(640), 1.0 };
const MonoColor MC_ORANGE = { WAVELENGTH_CONVERT(600), 1.0 };
const MonoColor MC_YELLOW = { WAVELENGTH_CONVERT(575), 1.0 };
const MonoColor MC_GREEN = { WAVELENGTH_CONVERT(550), 1.0};
const MonoColor MC_CYAN = { WAVELENGTH_CONVERT(475), 1.0};
const MonoColor MC_BLUE = { WAVELENGTH_CONVERT(440), 1.0};
#define MC_PURPLE(x)  MonoColor { WAVELENGTH_CONVERT(400), (x)}
const MonoColor MC_PURPLE_DIM = { WAVELENGTH_CONVERT(400), 0.8};


struct Observer {
	vec3 o;     // position of the observer
	vec3 beta;  // velocity divided by speed of light
	real gamma;      // gamma == 1 / sqrt(1 - beta^2)
};


class Quaternion {
private:
	real re;
	vec3 im;
public:
	Quaternion(real init_re, vec3 init_im) {
		this->re = init_re;
		this->im = init_im;
	}
	
	Quaternion conj() {
		return Quaternion(this->re, -this->im);
	}
	
	vec3 im_pt() {
		return this->im;
	}
	
	real norm() {
		return sqrt(re * re + dot(im, im));
	}
	
	void normalize() {
		real curr_norm = this->norm();
		re = re / curr_norm;
		im = im / curr_norm;
	}
	
	// hamilton product
	Quaternion operator*(Quaternion const& other) {
		return Quaternion(
			this->re * other.re - dot(this->im, other.im),
			this->re * other.im + other.re * this->im + cross(this->im, other.im)
		);
	}
	
	// @ pre: norm(*this) == 1
	vec3 rotate_vector(vec3 v) {
		return ((*this) * Quaternion(0, v) * (this->conj())).im_pt();
	}
	
	// compute the rotation matrix that is equivalent to the rotation operator of this quaternion
	// @ pre: norm(*this) == 1
	mat3 get_rot_mat() {
		real norm_im = sqrt(dot(this->im, this->im));
		real sin_th = 2 * this->re * norm_im;
		real cos_th = 2 * this->re * this->re - 1;
		
		if(norm_im < 0.0001) {
			return _Identity3x3;
		}
		else {
			vec3 v = this->im / norm_im;
			return _Identity3x3 * cos_th + proj_mat(v) * (1 - cos_th) + cross_prod_mat(v) * sin_th;
		}
	}
};


struct MyCam {
	real angle_of_view;  // angle of view of the camera
	Quaternion q;        // a rotation quaternion to store the camera's orientation
};
vec2 get_fp_size(MyCam* camera) {
	// return the size of the film plane, which is located at z distance = 1 from the observer
	vec2 win_size = window_get_size();
	vec2 a;
	a.y = tan(camera->angle_of_view/2);
	a.x = a.y * win_size.x / win_size.y;
	return a;
}
void camera_move(MyCam* camera) {
	{  // change the angle of view
		if(globals.mouse_wheel_offset != 0) {
			// change the angle of view while updating the orientation such that the point where the
			// mouse is pointing at retains its position on the screen
			real u = globals.mouse_position_NDC.x;
			real v = globals.mouse_position_NDC.y;
			
			vec2 a = get_fp_size(camera);
			real theta = atan(norm(V2(a.x * u, a.y * v)));
			
			// change angle of view
			camera->angle_of_view *= exp(-0.15 * globals.mouse_wheel_offset);
			camera->angle_of_view = MIN(camera->angle_of_view, 0.5 * PI);  // clip at PI/2
			
			a = get_fp_size(camera);
			real theta_new = atan(norm(V2(a.x * u, a.y * v)));
			
			// change orientation such that the point where the mouse is pointing appears to be at the same place
			camera->q = Quaternion(1, 0.5 * (theta - theta_new) * normalized(V3(-a.y * v, a.x * u, 0))) * camera->q;
			(camera->q).normalize();
		}
	}
	
	{  // change the orientation of the camera
		bool changed_q = false;
		
		// if mouse held and dragged, change the view direction such that the point where the mouse is pointing at
		// appears to be moving together with the cursor
		if(globals.mouse_left_held) {
			real u = globals.mouse_position_NDC.x;
			real v = globals.mouse_position_NDC.y;
			vec2 d = globals.mouse_change_in_position_NDC;
			
			vec2 a = get_fp_size(camera);
			mat2 M = M2(
				a.x * u * v, -a.x * u * u - 1 / a.x,
				a.y * v * v + 1 / a.y, - a.y * u * v
			);
			vec3 f = V3(-0.5 * (a.x * a.y / (1 + pow(a.x * u, 2) + pow(a.y * v, 2))) * (M * d), 0);
			camera->q = Quaternion(1, f) * camera->q;
			changed_q = true;
		}
		
		// if z or x keys held, roll
		if(globals.key_held['z']) {
			camera->q = Quaternion(1, V3(0, 0, 0.01)) * camera->q;
			changed_q = true;
		}
		if(globals.key_held['x']) {
			camera->q = Quaternion(1, V3(0, 0, -0.01)) * camera->q;
			changed_q = true;
		}
		
		// normalize q if any change was made, to make sure it remains a rotation quaternion
		if(changed_q) {
			(camera->q).normalize();
		}
	}
}
mat4 camera_get_PV(MyCam* camera, bool showing_rear_view = false) {
	if(showing_rear_view) {
		return (
			_window_get_P_perspective(camera->angle_of_view) *  // _window_get_P_perspective is from cow.cpp
			M4_Scaling(-1, 1, -1) *
			M4((camera->q).get_rot_mat())
		);
	}
	else {
		return (
			_window_get_P_perspective(camera->angle_of_view) *  // _window_get_P_perspective is from cow.cpp
			M4((camera->q).get_rot_mat())
		);
	}
}
vec3 camera_transform_vector(MyCam* camera, vec3 p) {
	return (camera->q).conj().rotate_vector(p);
}
void set_view_direction(MyCam* camera, vec3 new_view_direction) {
	vec3 d = (camera->q).rotate_vector(new_view_direction);
	
	if(norm(V2(d.x, d.y)) <= 0.0001) {
		// do nothing
	}
	else {
		real theta_new = atan(norm(V2(d.x, d.y) / d.z));
		camera->q = Quaternion(1, 0.5 * theta_new * normalized(V3(-d.y, d.x, 0))) * camera->q;
		(camera->q).normalize();
	}
}


vec3 transformStatic(Observer obs, vec3 p) {
	vec3 d = p - obs.o;
	real dnorm = norm(d);
	vec3 dhat = d / dnorm;
	
	if(dot(obs.beta, obs.beta) > 0.0001) {
		return dnorm * (dhat / obs.gamma + (1 - 1 / obs.gamma) * dot(obs.beta, dhat) * obs.beta / dot(obs.beta, obs.beta) + obs.beta) / (1 + dot(obs.beta, dhat));
	}
	else {
		return d;
	}
}


real angular_dist(vec3 a, vec3 b) {
	return acos(dot(a, b) / (norm(a) * norm(b)));
}
real angular_size(vec3 o, vec3* triangle) {
	real out = 0;
	
	vec3 p1 = triangle[0] - o;
	vec3 p2 = triangle[1] - o;
	vec3 p3 = triangle[2] - o;
	
	out = MAX(out, angular_dist(p1, p2));
	out = MAX(out, angular_dist(p2, p3));
	out = MAX(out, angular_dist(p3, p1));
	
	return out;
}


// TODO: 1. instead of taking depth as a fixed parameter, calculate it for each triangle such that the angular size of the sub-triangles does not exceed some fixed value
//             this would allow to save much computation on faraway triangles, which don't really need much subdivision, and improve quality of triangles near the camera
//       2. instead of running transformStatic on every point in the quadrisection array, use the fact that a lot of the points are repeated to save computation
//             might improve speed by a factor of around 3

// @pre: eso is started with SOUP_TRIANGLES
void draw_polygon(Observer obs, int N, vec3* vertices, int depth, MonoColor color) {
	#define n(u, v)  ((u) * (u + 1)) / 2 + (v)
	const int max_depth = 4;
	assert(depth <= max_depth);
	
	const int max_l = 16;  // 2 ** max_depth
	const int max_points_per_triangle = (max_l + 1) * (max_l + 2) / 2;
	vec3 points[max_points_per_triangle];
	vec3 colors[max_points_per_triangle];
	
	int l = int_pow(2, depth);
	int points_per_triangle = (l + 1) * (l + 2) / 2;
	
	for(int i = 0; i < N - 2; i++) {
		vec3 triangle[3] = {vertices[0], vertices[i + 1], vertices[i + 2]};
		
		vec3 c = triangle[0];
		vec3 a = (triangle[1] - triangle[0]) / l;
		vec3 b = (triangle[2] - triangle[1]) / l;
		
		for(int u = 0; u <= l; u++) {
			for(int v = 0; v <= u; v++) {
				points[n(u, v)] = c + u * a + v * b;
			}
		}
		
		for(int j = 0; j < points_per_triangle; j++) {
			real D = obs.gamma * (1 - dot(obs.beta, normalized(points[j] - obs.o)));
			colors[j] = mc_to_rgb(color, D);
			
			points[j] = transformStatic(obs, points[j]);
		}
		
		for(int u = 0; u <= l - 1; u++) {
			for(int v = 0; v <= u; v++) {
				eso_color(colors[n(u, v)]);         eso_vertex(points[n(u, v)]);
				eso_color(colors[n(u + 1, v)]);     eso_vertex(points[n(u + 1, v)]);
				eso_color(colors[n(u + 1, v + 1)]); eso_vertex(points[n(u + 1, v + 1)]);
			}
		}
		
		for(int u = 1; u <= l - 1; u++) {
			for(int v = 0; v <= u - 1; v++) {
				eso_color(colors[n(u, v)]);         eso_vertex(points[n(u, v)]);
				eso_color(colors[n(u + 1, v + 1)]); eso_vertex(points[n(u + 1, v + 1)]);
				eso_color(colors[n(u, v + 1)]);     eso_vertex(points[n(u, v + 1)]);
			}
		}
	}
}

void draw_parallelogram(Observer obs, vec3 a, vec3 b, int depth, MonoColor color) {
	vec3 v0 = V3(a.x, a.y, a.z);
	vec3 v1 = V3(a.x, a.y, b.z);
	vec3 v2 = V3(a.x, b.y, a.z);
	vec3 v3 = V3(a.x, b.y, b.z);
	vec3 v4 = V3(b.x, a.y, a.z);
	vec3 v5 = V3(b.x, a.y, b.z);
	vec3 v6 = V3(b.x, b.y, a.z);
	vec3 v7 = V3(b.x, b.y, b.z);
	
	vec3 points[8] = { v0, v1, v2, v3, v4, v5, v6, v7 };
	
	const int indexes[6][4] = {
		{0, 1, 3, 2},
		{2, 0, 4, 6},
		{3, 2, 6, 7},
		{1, 3, 7, 5},
		{0, 1, 5, 4},
		{4, 5, 7, 6}
	};
	
	for(int r = 0; r < 6; r++) {
		vec3 p1p2p3p4[4];
		for(int i = 0; i < 4; i++) { p1p2p3p4[i] = points[indexes[r][i]]; }
		draw_polygon(obs, 4, p1p2p3p4, 2, color);
	}
}

/*struct MyIndexedTriangleMesh {
	int num_vertices, num_triangles;
	vec3* vertex_positions;
	MonoColor* triangle_colors;
	int3* triangle_indices;
};
void draw_triangle(Observer obs, MonoColor color, vec3 p1, vec3 p2, vec3 p3, int l) {
	const int max_l = 16;
	const int max_points_per_triangle = (max_l + 1) * (max_l + 2) / 2;
	#define n(u, v)  ((u) * (u + 1)) / 2 + (v)
	
	vec3 points[max_points_per_triangle];
	vec3 colors[max_points_per_triangle];
	
	l = MIN(l, max_l);
	
	// l is the subdivision, which is the number of line segments into which each side of the triangle is divided
	int points_per_triangle = (l + 1) * (l + 2) / 2;
	
	vec3 a = (p2 - p1) / l;
	vec3 b = (p3 - p2) / l;
	
	for(int u = 0; u <= l; u++) {
		for(int v = 0; v <= u; v++) {
			points[n(u, v)] = p1 + u * a + v * b;
		}
	}
	
	for(int j = 0; j < points_per_triangle; j++) {
		real D = obs.gamma * (1 - dot(obs.beta, normalized(points[j] - obs.o)));
		colors[j] = mc_to_rgb(color, D);
		
		points[j] = transformStatic(obs, points[j]);
	}
	
	for(int u = 0; u <= l - 1; u++) {
		for(int v = 0; v <= u; v++) {
			eso_color(colors[n(u, v)]);         eso_vertex(points[n(u, v)]);
			eso_color(colors[n(u + 1, v)]);     eso_vertex(points[n(u + 1, v)]);
			eso_color(colors[n(u + 1, v + 1)]); eso_vertex(points[n(u + 1, v + 1)]);
		}
	}
	
	for(int u = 1; u <= l - 1; u++) {
		for(int v = 0; v <= u - 1; v++) {
			eso_color(colors[n(u, v)]);         eso_vertex(points[n(u, v)]);
			eso_color(colors[n(u + 1, v + 1)]); eso_vertex(points[n(u + 1, v + 1)]);
			eso_color(colors[n(u, v + 1)]);     eso_vertex(points[n(u, v + 1)]);
		}
	}
}
void draw_indexed_triangle_mesh(Observer obs, MyIndexedTriangleMesh mesh) {
	vec3* vertices_transformed = new vec3[mesh.num_vertices];
	for(int i = 0; i < mesh.num_vertices; i++) {
		vertices_transformed[i] = transformStatic(obs, mesh.vertex_positions[i]);
	}
	
	int subdivision = 1;
	for(int i = 0; i < mesh.num_triangles; i++) {
		vec3 a = vertices_transformed[mesh.triangle_indices[i][0]];
		vec3 b = vertices_transformed[mesh.triangle_indices[i][1]];
		vec3 c = vertices_transformed[mesh.triangle_indices[i][2]];
		
		const real tolerance = 0.02;
		
		subdivision = MAX(subdivision, floor(angular_distance(a, b) / tolerance));
		subdivision = MAX(subdivision, floor(angular_distance(b, c) / tolerance));
		subdivision = MAX(subdivision, floor(angular_distance(c, a) / tolerance));
	}
	
	for(int i = 0; i < mesh.num_triangles; i++) {
		vec3 a = mesh.vertex_positions[mesh.triangle_indices[i][0]];
		vec3 b = mesh.vertex_positions[mesh.triangle_indices[i][1]];
		vec3 c = mesh.vertex_positions[mesh.triangle_indices[i][2]];
		
		draw_triangle(obs, mesh.triangle_colors[i], a, b, c, subdivision);
	}
	
	delete vertices_transformed;
}
void draw_polygon(Observer obs, int n, vec3* vertices, int DDDDDD, MonoColor color) {
	MyIndexedTriangleMesh mesh;
	
	mesh.num_vertices = n;
	mesh.num_triangles = n - 2;
	
	mesh.vertex_positions = vertices;
	
	mesh.triangle_colors = new MonoColor[n - 2];
	mesh.triangle_indices = new int3[n - 2];
	for(int i = 0; i < n - 2; i++) {
		mesh.triangle_colors[i] = color;
		mesh.triangle_indices[i] = int3 {0, i + 1, i + 2};
	}
	
	draw_indexed_triangle_mesh(obs, mesh);
	
	delete mesh.triangle_colors, mesh.triangle_indices;
}*/


void draw_chessboard(Observer obs, vec3 o, vec3 a, vec3 b, int n, int m, int depth, MonoColor color1, MonoColor color2) {
	for(int i = 0; i < n; i++) {
		for(int j = 0; j < m; j++) {
			vec3 base = o + i * a + j * b;
			vec3 quad[] = {
				base,
				base + a,
				base + a + b,
				base + b
			};
			
			MonoColor color = ((i + j) % 2 == 0) ? color1 : color2;
			draw_polygon(obs, 4, quad, depth, color);
		}
	}
}
void draw_chessboard(Observer obs, vec3* p1p2p3p4, int n, int m, int depth, MonoColor color1, MonoColor color2) {
	vec3 o = p1p2p3p4[0];
	vec3 a = (p1p2p3p4[1] - p1p2p3p4[0]) / n;
	vec3 b = (p1p2p3p4[3] - p1p2p3p4[0]) / m;
	draw_chessboard(obs, o, a, b, n, m, depth, color1, color2);
}


class Obstacle {
private:
	// 0 for empty obstacle,
	// 1 for column obstacle
	int type;
	
	void* parameters;
	
	struct ColumnObstacleParameters {
		vec3 o;
		real radius;
		vec3 v, a, b;
		MonoColor color1, color2;
	};
	struct OctagonalHoleParameters {
		real z;
		real width;
		real inner_radius;
		MonoColor outer_color_1, outer_color_2;
		MonoColor hole_color;
	};
	struct SpiralConeParameters {
		real z1, z2;
		real r1, r2;
		int num_sections;
		int chirality;  // 1 for clockwise, 7 for counterclockwise
		MonoColor outer_color, inner_color_1, inner_color_2;
	};
	struct SphereObstacleParameters {
		vec3 o;
		real r;
		MonoColor color1, color2;
	};

public:
	Obstacle(int init_type, void* init_params) {
		type = init_type;
		parameters = init_params;
	}
	Obstacle() {
		type = 0;
		parameters = NULL;
	}
	
	void make_into_column_obstacle(real z, real dist_from_z_axis, real radius, real theta, MonoColor color1, MonoColor color2) {
		vec3 v, a, b;
		v = V3(cos(theta), sin(theta), 0); a = V3(0, 0, -1); b = V3(-sin(theta), cos(theta), 0);
		
		type = 1;
		parameters = new ColumnObstacleParameters {
			dist_from_z_axis * b + V3(0, 0, z),
			radius,
			v, a, b,
			color1, color2
		};
	}
	
	void make_into_octagonal_hole(real z, real width, real inner_radius, MonoColor outer_color_1, MonoColor outer_color_2, MonoColor hole_color) {
		type = 2;
		parameters = new OctagonalHoleParameters {
			z,
			width,
			inner_radius,
			outer_color_1, outer_color_2,
			hole_color
		};
	}
	
	void make_into_spiral_cone_obstacle(real z1, real z2, real r1, real r2, int num_sections, int chirality,
							MonoColor outer_color, MonoColor inner_color_1, MonoColor inner_color_2) {
		type = 3;
		assert((chirality == 1) || (chirality == 7));
		parameters = new SpiralConeParameters {
			z1, z2,
			r1, r2,
			num_sections,
			chirality,
			outer_color, inner_color_1, inner_color_2
		};
	}
	
	void make_into_sphere_obstacle(vec3 o, real r, MonoColor color1, MonoColor color2) {
		type = 4;
		parameters = new SphereObstacleParameters {
			o,
			r,
			color1, color2
		};
	}
	
	bool check_collision(vec3 p) {
		if(type == 0) {  // the empty obstacle
			return false;
		}
		else if(type == 1) {  // the column obstacle (which is really an octagonal prism)
			ColumnObstacleParameters* params = (ColumnObstacleParameters*)parameters;
			
			vec3 d = p - params->o;
			
			if((abs(dot(d, params->a)) <= params->radius) &&
				(abs(dot(d, params->b)) <= params->radius) &&
				(abs(dot(d, 0.707106 * (params->a + params->b))) <= params->radius) &&
				(abs(dot(d, 0.707106 * (params->a - params->b))) <= params->radius)) {
				return true;
			}
		}
		else if(type == 2) {  // the octagonal hole obstacle
			OctagonalHoleParameters* params = (OctagonalHoleParameters*)parameters;
			
			if((p.z >= params->z) && (p.z <= params->z + params->width)) {
				const vec3 diag_up = {0.707106, 0.707106, 0};
				const vec3 diag_down = {0.707106, -0.707106, 0};
				if((abs(p.x) > params->inner_radius) ||
					(abs(p.y) > params->inner_radius) ||
					(abs(dot(p, diag_up)) > params->inner_radius) ||
					(abs(dot(p, diag_down)) > params->inner_radius)) {
					return true;
				}
			}
		}
		else if(type == 3) {  // the spiral cone obstacle
			SpiralConeParameters* params = (SpiralConeParameters*)parameters;
			
			if((p.z >= params->z1) && (p.z <= params->z2)) {
				real r = params->r1 + (params->r2 - params->r1) * (p.z - params->z1) / (params->z2 - params->z1);
				return norm(V2(p.x, p.y)) > r;
			}
		}
		else if(type == 4) {  // the sphere obstacle (which is really a small rhombicuboctahedron)
			SphereObstacleParameters* params = (SphereObstacleParameters*)parameters;
			
			// the normals of a small rhombicuboctahedron
			const real s12 = 0.7071067811865476;  // sqrt(1/2)
			const real sgm = 0.5469181606780271;  // 1 / (2 sqrt(3) - 1)
			const vec3 rch_normals[] = {
				// the six "primary" square faces
				V3(1, 0, 0),
				V3(0, 1, 0),
				V3(0, 0, 1),
				// the twelve "secondary" square faces
				V3( s12,  s12, 0),
				V3(-s12,  s12, 0),
				V3( s12, 0,  s12),
				V3(-s12, 0,  s12),
				V3(0,  s12,  s12),
				V3(0, -s12,  s12),
				// the eight triangle faces
				V3( sgm,  sgm, sgm),
				V3(-sgm,  sgm, sgm),
				V3( sgm, -sgm, sgm),
				V3(-sgm, -sgm, sgm)
			};
			
			vec3 d = (p - params->o) / params->r;
			bool inside = true;
			for(int i = 0; i < 13; i++) {
				if(abs(dot(d, rch_normals[i])) > 1.0) {
					inside = false;
					break;
				}
			}
			
			return inside;
		}
		
		return false;
	}
	
	void display(Observer obs, real main_tube_radius) {
		if(type == 0) {  // the empty obstacle
			// do nothing
		}
		else if(type == 1) {  // the column obstacle (which is really an octagonal prism)
			ColumnObstacleParameters* params = (ColumnObstacleParameters*)parameters;
			
			// ASSUMING: params->o is the closest point to the z axis on the line o + v t, where t \in [-\inf, \inf]
			vec3 oct_points[8] = {
				params->o + params->radius * ( params->a - 0.4142135 * params->b),
				params->o + params->radius * ( params->a + 0.4142135 * params->b),
				params->o + params->radius * ( params->b + 0.4142135 * params->a),
				params->o + params->radius * ( params->b - 0.4142135 * params->a),
				params->o + params->radius * (-params->a + 0.4142135 * params->b),
				params->o + params->radius * (-params->a - 0.4142135 * params->b),
				params->o + params->radius * (-params->b - 0.4142135 * params->a),
				params->o + params->radius * (-params->b + 0.4142135 * params->a)
			};
			for(int j = 0; j < 8; j++) {
				vec3 p1p2p3p4[] = {
					oct_points[j]           - params->v * main_tube_radius,
					oct_points[(j + 1) % 8] - params->v * main_tube_radius,
					oct_points[(j + 1) % 8] + params->v * main_tube_radius,
					oct_points[j]           + params->v * main_tube_radius
				};
				draw_chessboard(obs, p1p2p3p4, 2, 6, 2, params->color1, params->color2);
			}
		}
		else if(type == 2) {  // the octagonal hole obstacle
			OctagonalHoleParameters* params = (OctagonalHoleParameters*)parameters;
			
			vec2 oct_points[8] = {
				 1, -0.4142135,
				 1,  0.4142135,
				 0.4142135,  1,
				-0.4142135,  1,
				-1,  0.4142135,
				-1, -0.4142135,
				-0.4142135, -1,
				 0.4142135, -1
			};
		
			real r1 = params->inner_radius;
			real r2 = main_tube_radius;
			for(int j = 0; j < 8; j++) {
				vec3 p1p2p3p4[] = {
					V3(oct_points[j] * r1,           params->z),
					V3(oct_points[(j + 1) % 8] * r1, params->z),
					V3(oct_points[(j + 1) % 8] * r2, params->z),
					V3(oct_points[j] * r2,           params->z)
				};
				draw_polygon(obs, 4, p1p2p3p4, 2, ((j % 2 == 0) ? params->outer_color_1 : params->outer_color_2));
				for(int k = 0; k < 4; k++) { p1p2p3p4[k].z += params->width; }
				draw_polygon(obs, 4, p1p2p3p4, 2, ((j % 2 == 0) ? params->outer_color_1 : params->outer_color_2));
			}
			
			for(int j = 0; j < 8; j++) {
				vec3 p1p2p3p4[] = {
					V3(oct_points[j] * r1,           params->z),
					V3(oct_points[(j + 1) % 8] * r1, params->z),
					V3(oct_points[(j + 1) % 8] * r1, params->z + params->width),
					V3(oct_points[j] * r1,           params->z + params->width)
				};
				draw_polygon(obs, 4, p1p2p3p4, 2, params->hole_color);
			}
		}
		else if(type == 3) {  // the spiral cone obstacle
			SpiralConeParameters* params = (SpiralConeParameters*)parameters;
			
			vec2 oct_points[8] = {
				 1, -0.4142135,
				 1,  0.4142135,
				 0.4142135,  1,
				-0.4142135,  1,
				-1,  0.4142135,
				-1, -0.4142135,
				-0.4142135, -1,
				 0.4142135, -1
			};
			
			{  // draw the first ring
				for(int j = 0; j < 8; j++) {  // draw octagon with a hole
					vec3 p1p2p3p4[] = {
						V3(oct_points[j] * main_tube_radius,           params->z1),
						V3(oct_points[j] * params->r1,                 params->z1),
						V3(oct_points[(j + 1) % 8] * params->r1,       params->z1),
						V3(oct_points[(j + 1) % 8] * main_tube_radius, params->z1)
					};
					draw_polygon(obs, 4, p1p2p3p4, 2, params->outer_color);
				}
				for(int j = 0; j < 8; j++) {  // draw thin black triangles at the corners
					vec3 p1p2p3[] = {
						V3((oct_points[j] + 0.1 * oct_points[(j + 7) % 8]) * main_tube_radius,           params->z1 - 0.01),
						V3((oct_points[j] + 0.1 * oct_points[(j + 1) % 8]) * main_tube_radius,           params->z1 - 0.01),
						V3(oct_points[j] * params->r1,                                                   params->z1 - 0.01)
					};
					draw_polygon(obs, 3, p1p2p3, 2, MC_BLACK);
				}
			}
			
			{  // draw the cone connecting the two rings, which is spiral-colored
				real z_curr = params->z1; real z_step = (params->z2 - params->z1) / params->num_sections;
				real r_curr = params->r1; real r_step = (params->r2 - params->r1) / params->num_sections;
				for(int i = 0; i < params->num_sections; i++) {
					real z_next = z_curr + z_step;
					real r_next = r_curr + r_step;
					
					for(int j = 0; j < 8; j++) {
						vec3 p1p2p3p4[] = {
							V3(r_curr * oct_points[j],                               z_curr),
							V3(r_curr * oct_points[(j + 1 * params->chirality) % 8], z_curr),
							V3(r_next * oct_points[(j + 1 * params->chirality) % 8], z_next),
							V3(r_next * oct_points[(j + 2 * params->chirality) % 8], z_next)
						};
						
						MonoColor color = ((i + j) % 2 == 0) ? params->inner_color_1 : params->inner_color_2;
						draw_polygon(obs, 3, &p1p2p3p4[0], 2, color);
						draw_polygon(obs, 3, &p1p2p3p4[1], 2, color);
					}
					
					z_curr = z_next;
					r_curr = r_next;
				}
			}
			
			{  // draw the second ring
				for(int j = 0; j < 8; j++) {  // draw octagon with a hole
					vec3 p1p2p3p4[] = {
						V3(oct_points[j] * main_tube_radius,           params->z2),
						V3(oct_points[j] * params->r2,                 params->z2),
						V3(oct_points[(j + 1) % 8] * params->r2,       params->z2),
						V3(oct_points[(j + 1) % 8] * main_tube_radius, params->z2)
					};
					draw_polygon(obs, 4, p1p2p3p4, 2, params->outer_color);
				}
				for(int j = 0; j < 8; j++) {  // draw thin black triangles at the corners
					vec3 p1p2p3[] = {
						V3((oct_points[j] + 0.1 * oct_points[(j + 7) % 8]) * main_tube_radius,           params->z2 + 0.01),
						V3((oct_points[j] + 0.1 * oct_points[(j + 1) % 8]) * main_tube_radius,           params->z2 + 0.01),
						V3(oct_points[j] * params->r2,                                                   params->z2 + 0.01)
					};
					draw_polygon(obs, 3, p1p2p3, 2, MC_BLACK);
				}
			}
		}
		else if(type == 4) {  // the sphere obstacle (which is really a small rhombicuboctahedron)
			SphereObstacleParameters* params = (SphereObstacleParameters*)parameters;
			
			// draw a small rhombicuboctahedron
			
			const real a = 0.41421356237309515;  // sqrt(2) - 1
			const vec3 rch_points[] = {
				V3(1,  a,  a),
				V3(1, -a,  a),
				V3(1, -a, -a),
				V3(1,  a, -a),
				V3(  a, 1,  a),
				V3( -a, 1,  a),
				V3( -a, 1, -a),
				V3(  a, 1, -a),
				V3(  a,  a, 1),
				V3( -a,  a, 1),
				V3( -a, -a, 1),
				V3(  a, -a, 1),
				V3(-1, -a, -a),
				V3(-1,  a, -a),
				V3(-1,  a,  a),
				V3(-1, -a,  a),
				V3( -a, -1, -a),
				V3(  a, -1, -a),
				V3(  a, -1,  a),
				V3( -a, -1,  a),
				V3( -a, -a, -1),
				V3(  a, -a, -1),
				V3(  a,  a, -1),
				V3( -a,  a, -1)
			};
			const int triangle_faces_indices[8][3] = {
				0, 4, 8,
				1, 18, 11,
				2, 17, 21,
				3, 7, 22,
				12, 16, 20,
				13, 6, 23,
				14, 5, 9,
				15, 19, 10
			};
			const int secondary_square_faces_indices[12][4] = {
				0, 1, 11, 8,
				1, 2, 17, 18,
				2, 3, 22, 21,
				3, 0, 4, 7,
				8, 9, 5, 4,
				10, 11, 18, 19,
				12, 13, 23, 20,
				13, 14, 5, 6,
				14, 15, 10, 9,
				15, 12, 16, 19,
				20, 21, 17, 16,
				22, 23, 6, 7
			};
			
			// draw the six "primary" square faces in color1
			for(int i = 0; i < 6; i++) {
				vec3 p1p2p3p4[] = {
					params->o + params->r * rch_points[0 + 4 * i],
					params->o + params->r * rch_points[1 + 4 * i],
					params->o + params->r * rch_points[2 + 4 * i],
					params->o + params->r * rch_points[3 + 4 * i]
				};
				draw_polygon(obs, 4, p1p2p3p4, 2, params->color1);
			}
			
			// draw the eight triangle faces in color1
			for(int i = 0; i < 8; i++) {
				vec3 p1p2p3[] = {
					params->o + params->r * rch_points[triangle_faces_indices[i][0]],
					params->o + params->r * rch_points[triangle_faces_indices[i][1]],
					params->o + params->r * rch_points[triangle_faces_indices[i][2]]
				};
				draw_polygon(obs, 3, p1p2p3, 2, params->color1);
			}
			
			// draw the twelve "secondary" square faces in color2
			for(int i = 0; i < 12; i++) {
				vec3 p1p2p3p4[] = {
					params->o + params->r * rch_points[secondary_square_faces_indices[i][0]],
					params->o + params->r * rch_points[secondary_square_faces_indices[i][1]],
					params->o + params->r * rch_points[secondary_square_faces_indices[i][2]],
					params->o + params->r * rch_points[secondary_square_faces_indices[i][3]]
				};
				draw_polygon(obs, 4, p1p2p3p4, 2, params->color2);
			}
		}
	}
};


class ObstaclesList {
private:
	int rasterization_depth = 2;
	
	real main_tube_radius = 2.0;
	
	vec2 oct_points[8];
	
	real block_size = 10.0;
	int max_obstacles_per_block = 10;
	
	vector<Obstacle*> secondary_obstacles;

public:
	ObstaclesList() {
		real r = main_tube_radius / cos(PI / 8);
		for(int i = 0; i < 8; i++) {
			real theta = PI/8 + i * PI/4;
			oct_points[i] = r * V2(cos(theta), sin(theta));
		}
	}
	
	// check if the player, located at position p, has crashed into any of the obstacles
	bool check_collision(vec3 p) {
		if(p.z < 0) {
			return true;  // player crashes into the start-of-the-game wall
		}
		const vec3 diag_up = {0.707106, 0.707106, 0};
		const vec3 diag_down = {0.707106, -0.707106, 0};
		if((abs(p.x) > main_tube_radius) ||
			(abs(p.y) > main_tube_radius) ||
			(abs(dot(p, diag_up)) > main_tube_radius) ||
			(abs(dot(p, diag_down)) > main_tube_radius)) {
			return true;  // player crashes into the main tube
		}
		
		int M = round(p.z / block_size);
		for(int i = M - 1; i <= M + 1; i++) {
			if(i < 0 || i > secondary_obstacles.size()) {
				continue;
			}
			
			for(int j = 0; j < max_obstacles_per_block; j++) {
				if(secondary_obstacles[i][j].check_collision(p)) {
					return true;
				}
			}
		}
		
		return false;  // player does not crash into anything
	}
	
	// generate obstacles between z_0 and z_0 + n * block_size, and possibly more
	void generate_secondary_obstacles(real z_0, int n) {
		if(secondary_obstacles.size() == 0) {
			// make one empty block at the start of the game
			Obstacle* obstacles = new Obstacle[max_obstacles_per_block];
			//obstacles[0].make_into_sphere_obstacle(V3(0, 0, 2), 1, MC_CYAN, MC_YELLOW);
			secondary_obstacles.push_back(obstacles);
			n--; z_0 += block_size;
		}
		
		while(n > 0) {  // while there are remaing blocks to be filled with obstacles
			int rnd = rand() % 4;
			
			if(rnd == 0) {  // dense forest of randomly placed columns
				int levels = 1 + rand() % 3;
				real step = (block_size - 1) / max_obstacles_per_block;
				for(int j = 0; j < levels; j++) {
					Obstacle* obstacles = new Obstacle[max_obstacles_per_block];
					for(int i = 0; i < max_obstacles_per_block; i++) {
						obstacles[i].make_into_column_obstacle(z_0 + 1 + step * i, fmod((double) rand(), main_tube_radius / 3),
								0.2 + fmod((double) rand(), 0.4), (double) rand(), MC_YELLOW, MC_CYAN);
					}
					secondary_obstacles.push_back(obstacles);
					n--; z_0 += block_size;
				}
			}
			else if(rnd == 1) {  // sparse forest of randomly placed columns
				int levels = 1 + rand() % 3;
				real step = (block_size - 1) / (max_obstacles_per_block / 2);
				for(int j = 0; j < levels; j++) {
					Obstacle* obstacles = new Obstacle[max_obstacles_per_block];
					for(int i = 0; i < max_obstacles_per_block / 2; i++) {
						obstacles[i].make_into_column_obstacle(z_0 + 1 + step * i, fmod((double) rand(), main_tube_radius / 3),
								0.15 + fmod((double) rand(), 0.45), (double) rand(), MC_YELLOW, MC_PURPLE(1.0));
					}
					secondary_obstacles.push_back(obstacles);
					n--; z_0 += block_size;
				}
			}
			else if(rnd == 2) {  // portal of three concentric rings
				Obstacle* obstacles = new Obstacle[max_obstacles_per_block];
				
				obstacles[0].make_into_octagonal_hole(z_0 + 1, 0.7, main_tube_radius / 2, MC_PURPLE(1.0), MC_PURPLE_DIM, MC_YELLOW);
				obstacles[1].make_into_octagonal_hole(z_0 + 2.5, 0.7, main_tube_radius / 4, MC_PURPLE_DIM, MC_PURPLE(1.0), MC_ORANGE);
				obstacles[2].make_into_octagonal_hole(z_0 + 4.0, 0.7, main_tube_radius / 6, MC_PURPLE(1.0), MC_PURPLE_DIM, MC_GREEN);
				
				secondary_obstacles.push_back(obstacles);
				n--; z_0 += block_size;
			}
			else if(rnd == 3) {  // a series of spiral cone obstacles
				Obstacle* obstacles = new Obstacle[max_obstacles_per_block];
				
				obstacles[0].make_into_spiral_cone_obstacle(z_0 + 0, z_0 + 3.75,  1.5, 0.5, 9, 1, MC_PURPLE(1.0), MC_PURPLE(1.0), MC_YELLOW);
				obstacles[1].make_into_spiral_cone_obstacle(z_0 + 3.75, z_0 + 5,  0.5, 0.5, 3, 1, MC_PURPLE(1.0), MC_YELLOW, MC_PURPLE(1.0));
				obstacles[2].make_into_spiral_cone_obstacle(z_0 + 5, z_0 + 6.23,  0.5, 0.5, 3, 7, MC_PURPLE(1.0), MC_YELLOW, MC_PURPLE(1.0));
				obstacles[3].make_into_spiral_cone_obstacle(z_0 + 6.23, z_0 + 10, 0.5, 1.5, 9, 7, MC_PURPLE(1.0), MC_PURPLE(1.0), MC_YELLOW);
				
				secondary_obstacles.push_back(obstacles);
				n--; z_0 += block_size;
				
				obstacles = new Obstacle[max_obstacles_per_block];
				
				obstacles[0].make_into_spiral_cone_obstacle(z_0 + 0, z_0 + 3.75,  1.5, 0.5, 9, 7, MC_PURPLE(1.0), MC_YELLOW, MC_PURPLE(1.0));
				obstacles[1].make_into_spiral_cone_obstacle(z_0 + 3.75, z_0 + 5,  0.5, 0.5, 3, 7, MC_PURPLE(1.0), MC_PURPLE(1.0), MC_YELLOW);
				obstacles[2].make_into_spiral_cone_obstacle(z_0 + 5, z_0 + 6.23,  0.5, 0.5, 3, 1, MC_PURPLE(1.0), MC_PURPLE(1.0), MC_YELLOW);
				obstacles[3].make_into_spiral_cone_obstacle(z_0 + 6.23, z_0 + 10, 0.5, 1.5, 9, 1, MC_PURPLE(1.0), MC_YELLOW, MC_PURPLE(1.0));
				
				secondary_obstacles.push_back(obstacles);
				n--; z_0 += block_size;
			}
		}
	}
	
	// draw a wall, spanning the entire tube width, at a given z
	// @pre: eso is started with SOUP_TRIANGLES
	void draw_wall(Observer obs, real z, MonoColor color) {
		for(int i = 0; i < 8; i++) {
			vec3 p1p2p3[] = {
				V3(0, 0, z),
				V3(oct_points[i], z),
				V3(oct_points[(i + 1) % 8], z)
			};
			draw_polygon(obs, 3, p1p2p3, 2, color);
		}
	}
	
	void draw_secondary_obstacles(Observer obs, Obstacle* obstacles) {
		for(int i = 0; i < max_obstacles_per_block; i++) {
			obstacles[i].display(obs, main_tube_radius);
		}
	}
	
	// @pre: eso is started with SOUP_TRIANGLES
	void display(Observer obs) {
		int M = round(obs.o.z / block_size);
		for(int i = M - rasterization_depth; i <= M + rasterization_depth; i++) {
			// draw the main tube
			for(int j = 0; j < 8; j++) {
				vec3 p1p2p3p4[] = {
					V3(oct_points[j],           i * block_size),
					V3(oct_points[(j + 1) % 8], i * block_size),
					V3(oct_points[(j + 1) % 8], (i + 1) * block_size),
					V3(oct_points[j],           (i + 1) * block_size)
				};
				draw_chessboard(obs, p1p2p3p4, 2, 10, 2, MC_RED, MC_PURPLE(2.0));
			}
			
			if(i >= 0) {
				int n = i - secondary_obstacles.size() + 1;
				if(n >= 0) {
					generate_secondary_obstacles(i * block_size, n);
				}
				
				draw_secondary_obstacles(obs, secondary_obstacles[i]);
			}
		}
		
		// draw the starting wall, which prevents the player from going in the -z direction
		draw_wall(obs, 0.01, MC_CYAN);
	}
};


// if x or c pressed, roll left or right, respectively
// if s or d pressed, increase or decrease the angle of view, respectively
void camera_adjust(MyCam* camera) {
	{  // change the angle of view
		if(globals.key_held['s']) {
			camera->angle_of_view *= 1.01;
			camera->angle_of_view = MIN(camera->angle_of_view, 0.5 * PI);  // clip at PI/2
		}
		if(globals.key_held['d']) {
			camera->angle_of_view *= 0.99;
			camera->angle_of_view = MIN(camera->angle_of_view, 0.5 * PI);  // clip at PI/2
		}
	}
	
	{  // if x or c keys held, roll
		bool changed_q = false;
		
		if(globals.key_held['x']) {
			camera->q = Quaternion(1, V3(0, 0, -0.01)) * camera->q;
			changed_q = true;
		}
		if(globals.key_held['c']) {
			camera->q = Quaternion(1, V3(0, 0, 0.01)) * camera->q;
			changed_q = true;
		}
		
		// normalize q if any change was made, to make sure it remains a rotation quaternion
		if(changed_q) {
			(camera->q).normalize();
		}
	}
}


// thrust if mouse left/right button pressed, change observer's velocity and camera orientation accordingly
// if spacebar is pressed, thrust against the direction of motion
void accelerate_spaceship(Observer* obs, MyCam* camera, real dt, real target_gamma, bool showing_rear_view = false) {
	real aspect = _window_get_aspect();      // window aspect ratio xsize / ysize
	const real direction_control_pow = 1.5;  // determines the sensitivity curve of flight controls
	const real r1 = 0.6;                                         // radius of the theta = PI/2 circle
	const real r2 = 0.04 * r1;                                   // radius of the red/blue circle indicating the direction of thrust
	
	{  // draw a circle and a point in the middle
		eso_begin(globals.Identity, SOUP_LINE_STRIP);
		eso_color(monokai.gray);
		for(int i = 0; i <= 40; i++) {
			real theta = 2 * PI * i / 40;
			eso_vertex(r1 * cos(theta) / aspect, r1 * sin(theta));
		}
		eso_end();
		
		eso_begin(globals.Identity, SOUP_POINTS);
		eso_vertex(0, 0);
		eso_end();
	}
	
	// if spacebar or right mouse button held, thrust against the direction of motion
	// if left mouse button held, thrust forward in the indicated direction
	{
		real throttle = 0.0;
		real theta, phi;  // polar and azimuthal angles of thrust with respect to current direction of motion
		
		if(globals.key_held[' '] || globals.mouse_right_held) {  // accelerate against the direction of motion
			throttle = showing_rear_view ? 1 : -1;
			theta = phi = 0.0;
		}
		else if(globals.mouse_left_held) {  // accelerate in a direction given by the mouse
			throttle = 1.0;
			
			vec2 mp = globals.mouse_position_NDC;
			mp.x *= aspect;
			mp /= r1;  
			if(norm(mp) > 1) {
				mp = normalized(mp);
			}
			theta = 0.5 * PI * pow(norm(mp), direction_control_pow);
			phi = atan2(mp.y, mp.x);
		}
		else {
			if(obs->gamma < target_gamma) {
				throttle = 1.0;
				theta = phi = 0.0;
			}
			else if(obs->gamma > target_gamma + 0.1) {
				throttle = -1.0;
				theta = phi = 0.0;
			}
		}
		
		if(abs(throttle) > 0.0001) {
			// compute the direction of thrust (in world coordinates)
			// @pre: the direction of view of the camera is the direction of view of the observer
			vec3 thrust_direction = V3(sin(theta) * cos(phi), sin(theta) * sin(phi), -cos(theta));
			if(showing_rear_view) {
				thrust_direction.x = -thrust_direction.x;
				thrust_direction.z = -thrust_direction.z;
			};
			thrust_direction = camera->q.conj().rotate_vector(thrust_direction);
			
			// momentum of the observer (expressed in units where m c = 1)
			vec3 current_p = obs->beta * obs->gamma;
			
			// modify the momentum of the observer
			{
				vec3 dp = throttle * 0.4 * dt * thrust_direction;
				vec3 dp_par = dot(dp, current_p) * current_p / dot(current_p, current_p); vec3 dp_perp = dp - dp_par;
				real dp_perp_max = 1.3 * dt * (norm(current_p) + 0.01);
				if(norm(dp_perp) > dp_perp_max) {
					dp_perp = normalized(dp_perp) * dp_perp_max;
				}
				real pmax = sqrt(target_gamma*target_gamma - 1);
				if((norm(current_p) > pmax) && dot(dp_par, current_p) > 0) {
					dp_par = V3(0);
				}
				dp = dp_par + dp_perp;
				
				if(dot(current_p + dp, current_p) > 0) {
					current_p += dp;
				}
			}
			
			// compute gamma and beta from the momentum
			obs->gamma = sqrt(1 + dot(current_p, current_p));
			obs->beta = current_p / obs->gamma;
			
			// set camera view direction to the direction of motion of the observer
			set_view_direction(camera, normalized(obs->beta));
			
			// draw a red circle if thrusting forward, blue circle if thrusting backwards
			// the position of the circle reflects the thrust direction
			eso_begin(globals.Identity, SOUP_LINE_STRIP);
			eso_color(throttle > 0 ? V3(1, 0, 0) : V3(0, 0, 1));
			vec2 thrust_display_location = r1 * pow(2 / PI * theta, 1 / direction_control_pow) * V2(cos(phi), sin(phi));
			for(int i = 0; i <= 40; i++) {
				real alpha = 2 * PI * i / 40;
				eso_vertex((thrust_display_location.x + r2 * cos(alpha)) / aspect, thrust_display_location.y + r2 * sin(alpha));
			}
			eso_end();
		}
	}
}


void myApp() {
	//srand(2);
	
	ObstaclesList obstacles;
	
	Observer obs; /*{
			o,
			beta, gamma
		};*/
	obs.o = V3(0, 0, 0.1);
	
	obs.beta = V3(0.0, 0.0, 0.0001);
	obs.gamma = 1 / sqrt(1 - dot(obs.beta, obs.beta));
	
	MyCam camera = { RAD(45), Quaternion(0, V3(1, 0, 0)) };  // look in the +z direction
	
	real target_gamma = 1.0;
	bool showing_rear_view = false;
	bool paused = false;
	while(cow_begin_frame()) {
		real dt = 0.0167 * obs.gamma;
		
		{  // GUI; display target gamma and current gamma, as well as camera angle of view and the front/rear selection box
			target_gamma += 0.3 * globals.mouse_wheel_offset;
			gui_slider("target gamma", &target_gamma, 1.001, 7.0);
			target_gamma = MIN(MAX(target_gamma, 1.001), 7.0);
			
			gui_slider("angle of view", &camera.angle_of_view, 0.0001, PI/2);
			gui_checkbox("show rear view", &showing_rear_view, 'r');
			gui_checkbox("pause", &paused, 'p');
			
			gui_printf("current gamma: %lf", obs.gamma);
			gui_printf("current speed: %lf", norm(obs.beta));
		}
		
		mat4 PV = camera_get_PV(&camera, showing_rear_view);
		eso_begin(PV, SOUP_TRIANGLES);
		
		obstacles.display(obs);
		
		eso_end();
		
		if(obstacles.check_collision(obs.o)) {
			return;
		}
		
		if(!paused) {
			camera_adjust(&camera);
			accelerate_spaceship(&obs, &camera, dt, target_gamma, showing_rear_view);
			
			obs.o += obs.beta * dt * 0.4;
		}
	}
}


void rasterizeBunny() {
	IndexedTriangleMesh3D bunny = library.meshes.teapot;//library.meshes.bunny;
	
	const real lighting_ambient = 0.1;
	const real lighting_diffuse = 1.0;
	const real lighting_wavelength = WAVELENGTH_CONVERT(500);
	
	const real shininess = 12.0;
	
	Observer obs; /*{
			o,
			beta, gamma
		};*/
	obs.o = V3(0, 0, -2);
	
	MyCam camera = { RAD(45), Quaternion(0, V3(1, 0, 0)) };  // look in the +z direction
	
	real target_gamma = 1.0;
	bool showing_rear_view = false;
	bool paused = false;
	obs.beta = V3(0, 0, 0.01);
	obs.gamma = 1.0;
	while(cow_begin_frame()) {
		real dt = 0.0167 * obs.gamma;
		
		{  // GUI; display target gamma and current gamma, as well as camera angle of view and the front/rear selection box
			target_gamma += 0.3 * globals.mouse_wheel_offset;
			gui_slider("target gamma", &target_gamma, 1.001, 7.0);
			target_gamma = MIN(MAX(target_gamma, 1.001), 7.0);
			
			gui_slider("angle of view", &camera.angle_of_view, 0.0001, PI/2);
			gui_checkbox("show rear view", &showing_rear_view, 'r');
			gui_checkbox("pause", &paused, 'p');
			
			gui_printf("current gamma: %lf", obs.gamma);
			gui_printf("current speed: %lf", norm(obs.beta));
		}
		
		obs.gamma = 1 / sqrt(1 - dot(obs.beta, obs.beta));
		
		vec3* points_transformed = new vec3[bunny.num_vertices];
		vec3* colors_transformed = new vec3[bunny.num_vertices];
		for(int i = 0; i < bunny.num_vertices; i++) {
			points_transformed[i] = transformStatic(obs, bunny.vertex_positions[i]);
			
			real intensity = lighting_ambient;  // ambient term
			vec3 normal = normalized(bunny.vertex_normals[i]);
			vec3 d_normalized = normalized(bunny.vertex_positions[i] - V3(0, 0, 2));
			vec3 r_normalized = reflect(-d_normalized, normal);
			if(dot(d_normalized, normal) > 0) {
				intensity += lighting_diffuse * dot(d_normalized, normal);  // diffuse term
			}
			vec3 dist_normalized = normalized(bunny.vertex_positions[i] - obs.o);
			colors_transformed[i] = mc_to_rgb(MonoColor { lighting_wavelength, intensity },
												obs.gamma * (1 - dot(obs.beta, dist_normalized)));
		}
		
		mat4 PV = camera_get_PV(&camera, false);
		eso_begin(PV, SOUP_TRIANGLES);
		for(int i = 0; i < bunny.num_triangles; i++) {
			int3 tri_ind = bunny.triangle_indices[i];
			
			eso_color(colors_transformed[tri_ind[0]]); eso_vertex(points_transformed[tri_ind[0]]);
			eso_color(colors_transformed[tri_ind[1]]); eso_vertex(points_transformed[tri_ind[1]]);
			eso_color(colors_transformed[tri_ind[2]]); eso_vertex(points_transformed[tri_ind[2]]);
		}
		eso_end();
		
		delete points_transformed;
		
		if(!paused) {
			camera_adjust(&camera);
			accelerate_spaceship(&obs, &camera, dt, target_gamma, showing_rear_view);
			
			obs.o += obs.beta * dt * 0.4;
		}
	}
}

void ty() {
	Observer obs; /*{
			o,
			beta, gamma
		};*/
	obs.o = V3(0, 4.0, 1.5);
	
	obs.beta = V3(0.0, 0.0, 0.0001);
	obs.gamma = 1 / sqrt(1 - dot(obs.beta, obs.beta));
	
	MyCam camera = { RAD(45), Quaternion(sqrt(0.5), sqrt(0.5) * V3(1, 0, 0)) };  // look in the -x direction
	
	real target_gamma = 1.0;
	bool showing_rear_view = false;
	bool paused = false;
	while(cow_begin_frame()) {
		real dt = 0.0167 * obs.gamma;
		
		{  // GUI; display target gamma and current gamma, as well as camera angle of view and the front/rear selection box
			target_gamma += 0.3 * globals.mouse_wheel_offset;
			gui_slider("target gamma", &target_gamma, 1.001, 7.0);
			target_gamma = MIN(MAX(target_gamma, 1.001), 7.0);
			
			gui_slider("angle of view", &camera.angle_of_view, 0.0001, PI/2);
			gui_checkbox("show rear view", &showing_rear_view, 'r');
			gui_checkbox("pause", &paused, 'p');
			
			gui_printf("current gamma: %lf", obs.gamma);
			gui_printf("current speed: %lf", norm(obs.beta));
		}
		
		mat4 PV = camera_get_PV(&camera, showing_rear_view);
		eso_begin(PV, SOUP_TRIANGLES);
		
		draw_parallelogram(obs, V3(-0.5, -0.5, 0), V3(0.5, 0.5, 2.5), 2, MC_RED);
		draw_parallelogram(obs, V3(-1.5, -0.5, 2.0), V3(1.5, 0.5, 2.5), 2, MC_RED);
		
		eso_end();
		
		if(!paused) {
			camera_adjust(&camera);
			accelerate_spaceship(&obs, &camera, dt, target_gamma, showing_rear_view);
			
			obs.o += obs.beta * dt * 0.01;
		}
	}
}

int main() {
    APPS {
        APP(myApp);
		APP(rasterizeBunny);
		APP(ty);
    }
    return 0;
}
