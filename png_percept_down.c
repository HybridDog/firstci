// this is two times as fast when compiled with -Ofast

// see https://graphics.ethz.ch/~cengizo/Files/Sig15PerceptualDownscaling.pdf

#include <stdlib.h> // malloc, EXIT_*
#include <string.h> // memset
#include <math.h>
#include <png.h>

#define SQR_NP 2 // squareroot of the patch size, recommended: 2


#define EXIT_PNG(F) if (!F) { \
	fprintf(stderr, "%s\n", bild.message); \
	return EXIT_FAILURE; \
}

#define CLAMP(V, A, B) ((V) < (A) ? (A) : (V) > (B) ? (B) : (V))
#define MIN(V, R) ((V) < (R) ? (V) : (R))
#define MAX(V, R) ((V) > (R) ? (V) : (R))
#define INDEX(X, Y, STRIDE) ((Y) * (STRIDE) + (X))

#define u8 unsigned char

struct pixel {
	u8 r;
	u8 g;
	u8 b;
	u8 a;
};
#define PIXELBYTES 4

struct matrix {
	int w;
	int h;
	float *data;
};

struct image {
	int w;
	int h;
	struct pixel *pixels;
};

#if !GAMMA_INCORRECT

/*! \brief linear to sRGB conversion
 *
 * taken from https://github.com/tobspr/GLSL-Color-Spaces/
 */
float linear_to_srgb(float v)
{
	if (v > 0.0031308f)
		return 1.055f * powf(v, 1.0f / 2.4f) - 0.055f;
	return 12.92f * v;
}
float srgb_to_linear(float v)
{
	if (v > 0.04045f)
		return powf((v + 0.055f) / 1.055f, 2.4f);
	return v / 12.92f;
}

/*! \brief sRGB to linear table for a slight performance increase
 */
static float *srgb2lin;
static void get_srgb2lin_map()
{
	srgb2lin = malloc(256 * sizeof(float));
	float divider = 1.0f / 255.0f;
	for (int i = 0; i < 256; ++i)
		srgb2lin[i] = srgb_to_linear(i * divider);
}

#endif // !GAMMA_INCORRECT

/*! \brief get y, cb and cr values each in [0;1] from u8 r, g and b values
 *
 * there's gamma correction,
 * see http://www.ericbrasseur.org/gamma.html?i=1#Assume_a_gamma_of_2.2
 * 0.5 is added to cb and cr to have them in [0;1]
 */
static void rgb2ycbcr(u8 or, u8 og, u8 ob, float *y, float *cb, float *cr)
{
#if GAMMA_INCORRECT
	float r = or / 255.0f;
	float g = og / 255.0f;
	float b = ob / 255.0f;
#else
	float r = srgb2lin[or];
	float g = srgb2lin[og];
	float b = srgb2lin[ob];
#endif
	*y = (0.299f * r + 0.587f * g + 0.114f * b);
	*cb = (-0.168736f * r - 0.331264f * g + 0.5f * b) + 0.5f;
	*cr = (0.5f * r - 0.418688f * g - 0.081312f * b) + 0.5f;
}

/*! \brief the inverse of the function above
 *
 * numbers from http://www.equasys.de/colorconversion.html
 * if values are too big or small, they're clamped
 */
static void ycbcr2rgb(float y, float cb, float cr, u8 *r, u8 *g, u8 *b)
{
	float vr = (y + 1.402f * (cr - 0.5f));
	float vg = (y - 0.344136f * (cb - 0.5f) - 0.714136f * (cr - 0.5f));
	float vb = (y + 1.772f * (cb - 0.5f));
#if !GAMMA_INCORRECT
	vr = linear_to_srgb(vr);
	vg = linear_to_srgb(vg);
	vb = linear_to_srgb(vb);
#endif
	*r = CLAMP(vr * 255.0f, 0, 255);
	*g = CLAMP(vg * 255.0f, 0, 255);
	*b = CLAMP(vb * 255.0f, 0, 255);
}

/*! \brief Convert an rgba image to 4 ycbcr matrices with values in [0, 1]
 */
static struct matrix *image_to_matrices(struct image *bild)
{
	int w = bild->w;
	int h = bild->h;
	struct matrix *matrices = malloc(
		PIXELBYTES * sizeof(struct matrix));
	for (int i = 0; i < PIXELBYTES; ++i) {
		matrices[i].w = w;
		matrices[i].h = h;
		matrices[i].data = malloc(w * h * sizeof(float));
	}
	for (int i = 0; i < w * h; ++i) {
		struct pixel px = bild->pixels[i];
		// put y, cb, cr and transpatency into the matrices
		rgb2ycbcr(px.r, px.g, px.b,
			&matrices[0].data[i], &matrices[1].data[i], &matrices[2].data[i]);
		float divider = 1.0f / 255.0f;
		matrices[3].data[i] = px.a * divider;
	}
	return matrices;
}

/*! \brief Convert 4 matrices to an rgba image
 *
 * Note that matrices becomes freed.
 */
static struct image *matrices_to_image(struct matrix *matrices)
{
	struct image *bild = malloc(sizeof(struct image));
	int w = matrices[0].w;
	int h = matrices[0].h;
	bild->w = w;
	bild->h = h;
	struct pixel *pixels = malloc(w * h * PIXELBYTES);
	for (int i = 0; i < w * h; ++i) {
		struct pixel *px = &pixels[i];
		ycbcr2rgb(matrices[0].data[i], matrices[1].data[i], matrices[2].data[i],
			&px->r, &px->g, &px->b);
		float a = matrices[3].data[i] * 255;
		px->a = CLAMP(a, 0, 255);
	}
	for (int i = 0; i < PIXELBYTES; ++i) {
		free(matrices[i].data);
	}
	free(matrices);
	bild->pixels = pixels;
	return bild;
}

/*! \brief The actual downscaling algorithm
 *
 * \param mat The 4 matrices obtained form image_to_matrices.
 * \param s The factor by which the image should become downscaled.
 */
static void downscale_perc(struct matrix *mat, int s)
{
	// preparation
	int w = mat->w; // input width
	int h = mat->h;
	float *input = mat->data;
	int w2 = w / s; // output width
	int h2 = h / s;
	int input_size = w * h * sizeof(float);
	int output_size = input_size / (s * s);
	//~ fprintf(stderr, "w, h, s: %d, %d, %d\n", w,h,s);
	float *l = malloc(output_size);
	float *l2 = malloc(output_size);
	float *m_all = malloc(output_size);
	float *r_all = malloc(output_size);
	float *d = malloc(output_size);

	// get l and l2, the input image and it's size are used only here
	float divider_s = 1.0f / (s * s);
	for (int y_start = 0; y_start < h2; ++y_start) {
		for (int x_start = 0; x_start < w2; ++x_start) {
			// x_start and y_start are coordinates for the subsampled image
			int x = x_start * s;
			int y = y_start * s;
			float acc = 0;
			float acc2 = 0;
			for (int yc = y; yc < y + s; ++yc) {
				for (int xc = x; xc < x + s; ++xc) {
					// xc, yc are always inside bounds
					float v = input[INDEX(xc, yc, w)];
					acc += v;
					acc2 += v * v;
				}
			}
			int i = INDEX(x_start, y_start, w2);
			l[i] = acc * divider_s;
			l2[i] = acc2 * divider_s;
		}
	}

	float patch_sz_div = 1.0f / (SQR_NP * SQR_NP);

	// Calculate m and r for all patch offsets
	for (int y_start = 0; y_start < h2; ++y_start) {
		for (int x_start = 0; x_start < w2; ++x_start) {
			float acc_m = 0;
			float acc_r_1 = 0;
			float acc_r_2 = 0;
			for (int y = y_start; y < y_start + SQR_NP; ++y) {
				for (int x = x_start; x < x_start + SQR_NP; ++x) {
					int xi = x;
					int yi = y;
#if TILEABLE
					xi = xi % w2;
					yi = yi % h2;
#else
					xi = MIN(xi, w2-1);
					yi = MIN(yi, h2-1);
#endif
					int i = INDEX(xi, yi, w2);
					acc_m += l[i];
					acc_r_1 += l[i] * l[i];
					acc_r_2 += l2[i];
				}
			}
			float mv = acc_m * patch_sz_div;
			float slv = acc_r_1 * patch_sz_div - mv * mv;
			float shv = acc_r_2 * patch_sz_div - mv * mv;
			int i = INDEX(x_start, y_start, w2);
			m_all[i] = mv;
			if (slv >= 0.000001f) // epsilon is 10⁻⁶
				r_all[i] = sqrtf(shv / slv);
			else
				r_all[i] = 2.0f;
		}
	}

	// Calculate the average of the results of all possible patch sets
	// d is the output
	for (int y = 0; y < h2; ++y) {
		for (int x = 0; x < w2; ++x) {
			int i = INDEX(x, y, w2);
			float liner_scaled = l[i];
			float acc_d = 0;
			for (int y_offset = 0; y_offset > -SQR_NP; --y_offset) {
				for (int x_offset = 0; x_offset > -SQR_NP; --x_offset) {
					int x_patch_off = x + x_offset;
					int y_patch_off = y + y_offset;
#if TILEABLE
					x_patch_off = (x_patch_off + w2) % w2;
					y_patch_off = (y_patch_off + h2) % h2;
#else
					x_patch_off = MAX(x_patch_off, 0);
					y_patch_off = MAX(y_patch_off, 0);
#endif
					int i_patch_off = INDEX(x_patch_off, y_patch_off, w2);
					float mv = m_all[i_patch_off];
					float rv = r_all[i_patch_off];
					acc_d += mv + rv * liner_scaled - rv * mv;
				}
			}
			d[i] = acc_d * patch_sz_div;
		}
	}


	// update the matrix
	mat->data = d;
	mat->w = w2;
	mat->h = h2;

	// tidy up
	free(input);
	free(l);
	free(l2);
	free(m_all);
	free(r_all);
}

/*! \brief Function which calls functions for downscaling
 *
 * \param bild The image, it's content is changed when finished.
 * \param downscale_factor Must be a natural number.
 */
void downscale_an_image(struct image **bild, int downscale_factor)
{
	struct matrix *matrices = image_to_matrices(*bild);
	for (int i = 0; i < PIXELBYTES; ++i) {
		downscale_perc(&(matrices[i]), downscale_factor);
	}
	*bild = matrices_to_image(matrices);
}

int main(int argc, char **args)
{
	if (argc != 2) {
		fprintf(stderr, "Missing arguments, usage: <cmdname> "
			"<downscaling_factor>\n");
		return EXIT_FAILURE;
	}
	int downscaling_factor = atoi(args[1]);
	if (downscaling_factor < 2) {
		fprintf(stderr, "Invalid downscaling factor: %d\n",
			downscaling_factor);
		return EXIT_FAILURE;
	}

	png_image bild;
	memset(&bild, 0, sizeof(bild));
	bild.version = PNG_IMAGE_VERSION;
	EXIT_PNG(png_image_begin_read_from_stdio(&bild, stdin))

	int w = bild.width;
	int h = bild.height;
	bild.format = PNG_FORMAT_RGBA;
	struct pixel *pixels = malloc(w * h * 4);
	EXIT_PNG(png_image_finish_read(&bild, NULL, pixels, 0, NULL))

	if (w % downscaling_factor || h % downscaling_factor) {
		fprintf(stderr, "Image size is not a multiple of the downscaling "
			"factor; %d,%d pixels will be discarded from the right,bottom "
			"borders\n", w % downscaling_factor, h % downscaling_factor);
	}

#if !GAMMA_INCORRECT
	get_srgb2lin_map();
#endif
	struct image origpic = {w = w, h = h, pixels = pixels};
	struct image *newpic = &origpic;
	downscale_an_image(&newpic, downscaling_factor);
	bild.width = newpic->w;
	bild.height = newpic->h;
	free(pixels);
	pixels = newpic->pixels;
	free(newpic);


	EXIT_PNG(png_image_write_to_stdio(&bild, stdout, 0, pixels, 0, NULL));
	free(pixels); // redundant free to feed valgrind
	return EXIT_SUCCESS;
}
